// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_query.h"

#include "openmeta/byte_arena.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_flags.h"
#include "openmeta/phaseone_geometry.h"
#include "openmeta/vendor_raw_processing.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static constexpr uint16_t kDngDefaultCropOriginTag        = 0xC61FU;
    static constexpr uint16_t kDngDefaultCropSizeTag          = 0xC620U;
    static constexpr uint16_t kDngActiveAreaTag               = 0xC68DU;
    static constexpr uint16_t kDngMaskedAreasTag              = 0xC68EU;
    static constexpr uint16_t kExifOrientationTag             = 0x0112U;
    static constexpr uint16_t kExifThumbnailOrientationTag    = 0x5029U;
    static constexpr uint16_t kExifExposureTimeTag            = 0x829AU;
    static constexpr uint16_t kExifFNumberTag                 = 0x829DU;
    static constexpr uint16_t kExifExposureProgramTag         = 0x8822U;
    static constexpr uint16_t kExifPhotographicSensitivityTag = 0x8827U;
    static constexpr uint16_t kExifShutterSpeedValueTag       = 0x9201U;
    static constexpr uint16_t kExifApertureValueTag           = 0x9202U;
    static constexpr uint16_t kExifBrightnessValueTag         = 0x9203U;
    static constexpr uint16_t kExifExposureBiasValueTag       = 0x9204U;
    static constexpr uint16_t kExifMaxApertureValueTag        = 0x9205U;
    static constexpr uint16_t kExifLightSourceTag             = 0x9208U;
    static constexpr uint16_t kExifExposureIndexTag           = 0x9215U;
    static constexpr uint16_t kExifWhiteBalanceTag            = 0xA403U;
    static constexpr uint16_t kExifGainControlTag             = 0xA407U;
    static constexpr uint16_t kDngColorMatrix1Tag             = 0xC621U;
    static constexpr uint16_t kDngColorMatrix2Tag             = 0xC622U;
    static constexpr uint16_t kDngCameraCalibration1Tag       = 0xC623U;
    static constexpr uint16_t kDngCameraCalibration2Tag       = 0xC624U;
    static constexpr uint16_t kDngReductionMatrix1Tag         = 0xC625U;
    static constexpr uint16_t kDngReductionMatrix2Tag         = 0xC626U;
    static constexpr uint16_t kDngAnalogBalanceTag            = 0xC627U;
    static constexpr uint16_t kDngAsShotNeutralTag            = 0xC628U;
    static constexpr uint16_t kDngAsShotWhiteXyTag            = 0xC629U;
    static constexpr uint16_t kDngBaselineExposureTag         = 0xC62AU;
    static constexpr uint16_t kDngCalibrationIlluminant1Tag   = 0xC65AU;
    static constexpr uint16_t kDngCalibrationIlluminant2Tag   = 0xC65BU;
    static constexpr uint16_t kDngForwardMatrix1Tag           = 0xC714U;
    static constexpr uint16_t kDngForwardMatrix2Tag           = 0xC715U;
    static constexpr uint16_t kDngOpcodeList1Tag              = 0xC740U;
    static constexpr uint16_t kDngOpcodeList2Tag              = 0xC741U;
    static constexpr uint16_t kDngOpcodeList3Tag              = 0xC74EU;
    static constexpr uint16_t kDngBaselineExposureOffsetTag   = 0xC7A5U;
    static constexpr uint16_t kDngRawToPreviewGainTag         = 0xC7A8U;
    static constexpr uint16_t kDngProfileGainTableMapTag      = 0xCD2DU;
    static constexpr uint16_t kDngCalibrationIlluminant3Tag   = 0xCD31U;
    static constexpr uint16_t kDngCameraCalibration3Tag       = 0xCD32U;
    static constexpr uint16_t kDngColorMatrix3Tag             = 0xCD33U;
    static constexpr uint16_t kDngForwardMatrix3Tag           = 0xCD34U;
    static constexpr uint16_t kDngReductionMatrix3Tag         = 0xCD3AU;
    static constexpr uint16_t kDngProfileGainTableMap2Tag     = 0xCD40U;
    static constexpr uint16_t kSamsungVignettingCorrParamsTag = 0x7032U;
    static constexpr uint16_t kSamsungChromaticAberrationCorrParamsTag = 0x7035U;
    static constexpr uint16_t kSamsungDistortionCorrParamsTag = 0x7037U;

    static constexpr uint16_t kPhaseOneSensorWidthTag      = 0x0108U;
    static constexpr uint16_t kPhaseOneSensorHeightTag     = 0x0109U;
    static constexpr uint16_t kPhaseOneSensorLeftMarginTag = 0x010AU;
    static constexpr uint16_t kPhaseOneSensorTopMarginTag  = 0x010BU;
    static constexpr uint16_t kPhaseOneImageWidthTag       = 0x010CU;
    static constexpr uint16_t kPhaseOneImageHeightTag      = 0x010DU;
    static constexpr std::string_view kPhaseOneMainIfd     = "mk_phaseone0";

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static char ascii_lower(char c) noexcept
    {
        if (c >= 'A' && c <= 'Z') {
            return static_cast<char>(c + ('a' - 'A'));
        }
        return c;
    }

    static bool contains_ascii_case_insensitive(std::string_view text,
                                                std::string_view needle) noexcept
    {
        if (needle.empty()) {
            return true;
        }
        if (text.size() < needle.size()) {
            return false;
        }
        const size_t limit = text.size() - needle.size();
        for (size_t pos = 0U; pos <= limit; ++pos) {
            bool matched = true;
            for (size_t i = 0U; i < needle.size(); ++i) {
                if (ascii_lower(text[pos + i]) != ascii_lower(needle[i])) {
                    matched = false;
                    break;
                }
            }
            if (matched) {
                return true;
            }
        }
        return false;
    }

    static bool
    starts_with_ascii_case_insensitive(std::string_view text,
                                       std::string_view prefix) noexcept
    {
        if (text.size() < prefix.size()) {
            return false;
        }
        for (size_t i = 0U; i < prefix.size(); ++i) {
            if (ascii_lower(text[i]) != ascii_lower(prefix[i])) {
                return false;
            }
        }
        return true;
    }

    static MetadataQueryValueShape value_shape(const MetaValue& value) noexcept
    {
        switch (value.kind) {
        case MetaValueKind::Empty: return MetadataQueryValueShape::Unknown;
        case MetaValueKind::Scalar: return MetadataQueryValueShape::Scalar;
        case MetaValueKind::Bytes: return MetadataQueryValueShape::Blob;
        case MetaValueKind::Text: return MetadataQueryValueShape::Text;
        case MetaValueKind::Array:
            switch (value.count) {
            case 2U: return MetadataQueryValueShape::Vec2;
            case 3U: return MetadataQueryValueShape::Vec3;
            case 4U: return MetadataQueryValueShape::Vec4;
            case 9U: return MetadataQueryValueShape::Matrix3x3;
            default: return MetadataQueryValueShape::Array;
            }
        }
        return MetadataQueryValueShape::Unknown;
    }

    static uint32_t numeric_element_size(MetaElementType type) noexcept
    {
        switch (type) {
        case MetaElementType::U8:
        case MetaElementType::I8: return 1U;
        case MetaElementType::U16:
        case MetaElementType::I16: return 2U;
        case MetaElementType::U32:
        case MetaElementType::I32:
        case MetaElementType::F32: return 4U;
        case MetaElementType::U64:
        case MetaElementType::I64:
        case MetaElementType::F64:
        case MetaElementType::URational:
        case MetaElementType::SRational: return 8U;
        }
        return 0U;
    }

    static double f32_bits_to_double(uint32_t bits) noexcept
    {
        float value = 0.0F;
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&value, &bits, sizeof(value));
        return static_cast<double>(value);
    }

    static double f64_bits_to_double(uint64_t bits) noexcept
    {
        double value = 0.0;
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    static bool scalar_to_double(const MetaValue& value, double* out) noexcept
    {
        if (!out || value.kind != MetaValueKind::Scalar) {
            return false;
        }
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64:
            *out = static_cast<double>(value.data.u64);
            return true;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            *out = static_cast<double>(value.data.i64);
            return true;
        case MetaElementType::F32:
            *out = f32_bits_to_double(value.data.f32_bits);
            return true;
        case MetaElementType::F64:
            *out = f64_bits_to_double(value.data.f64_bits);
            return true;
        case MetaElementType::URational:
            if (value.data.ur.denom == 0U) {
                return false;
            }
            *out = static_cast<double>(value.data.ur.numer)
                   / static_cast<double>(value.data.ur.denom);
            return true;
        case MetaElementType::SRational:
            if (value.data.sr.denom == 0) {
                return false;
            }
            *out = static_cast<double>(value.data.sr.numer)
                   / static_cast<double>(value.data.sr.denom);
            return true;
        }
        return false;
    }

    static bool array_element_to_double(std::span<const std::byte> bytes,
                                        MetaElementType type, size_t offset,
                                        double* out) noexcept
    {
        if (!out || offset > bytes.size()) {
            return false;
        }
        switch (type) {
        case MetaElementType::U8:
            if (offset + 1U > bytes.size()) {
                return false;
            }
            *out = static_cast<double>(static_cast<uint8_t>(bytes[offset]));
            return true;
        case MetaElementType::I8:
            if (offset + 1U > bytes.size()) {
                return false;
            }
            *out = static_cast<double>(
                static_cast<int8_t>(static_cast<uint8_t>(bytes[offset])));
            return true;
        case MetaElementType::U16: {
            uint16_t value = 0U;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            *out = static_cast<double>(value);
            return true;
        }
        case MetaElementType::I16: {
            int16_t value = 0;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            *out = static_cast<double>(value);
            return true;
        }
        case MetaElementType::U32: {
            uint32_t value = 0U;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            *out = static_cast<double>(value);
            return true;
        }
        case MetaElementType::I32: {
            int32_t value = 0;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            *out = static_cast<double>(value);
            return true;
        }
        case MetaElementType::U64: {
            uint64_t value = 0U;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            *out = static_cast<double>(value);
            return true;
        }
        case MetaElementType::I64: {
            int64_t value = 0;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            *out = static_cast<double>(value);
            return true;
        }
        case MetaElementType::F32: {
            uint32_t bits = 0U;
            if (offset + sizeof(bits) > bytes.size()) {
                return false;
            }
            std::memcpy(&bits, bytes.data() + offset, sizeof(bits));
            *out = f32_bits_to_double(bits);
            return true;
        }
        case MetaElementType::F64: {
            uint64_t bits = 0U;
            if (offset + sizeof(bits) > bytes.size()) {
                return false;
            }
            std::memcpy(&bits, bytes.data() + offset, sizeof(bits));
            *out = f64_bits_to_double(bits);
            return true;
        }
        case MetaElementType::URational: {
            URational value;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            if (value.denom == 0U) {
                return false;
            }
            *out = static_cast<double>(value.numer)
                   / static_cast<double>(value.denom);
            return true;
        }
        case MetaElementType::SRational: {
            SRational value;
            if (offset + sizeof(value) > bytes.size()) {
                return false;
            }
            std::memcpy(&value, bytes.data() + offset, sizeof(value));
            if (value.denom == 0) {
                return false;
            }
            *out = static_cast<double>(value.numer)
                   / static_cast<double>(value.denom);
            return true;
        }
        }
        return false;
    }

    static bool value_to_double_array(const MetaStore& store,
                                      const MetaValue& value,
                                      double* out_values, uint32_t max_values,
                                      uint32_t* out_count) noexcept
    {
        if (!out_values || !out_count || max_values == 0U) {
            return false;
        }
        *out_count = 0U;
        if (value.kind == MetaValueKind::Scalar) {
            if (!scalar_to_double(value, &out_values[0])) {
                return false;
            }
            *out_count = 1U;
            return true;
        }
        if (value.kind != MetaValueKind::Array) {
            return false;
        }
        const uint32_t element_size = numeric_element_size(value.elem_type);
        if (element_size == 0U) {
            return false;
        }
        const std::span<const std::byte> bytes = store.arena().span(
            value.data.span);
        const uint32_t available = static_cast<uint32_t>(bytes.size()
                                                         / element_size);
        uint32_t count           = value.count;
        if (count > available) {
            count = available;
        }
        if (count > max_values) {
            count = max_values;
        }
        for (uint32_t i = 0U; i < count; ++i) {
            if (!array_element_to_double(bytes, value.elem_type,
                                         static_cast<size_t>(i) * element_size,
                                         &out_values[i])) {
                return false;
            }
        }
        *out_count = count;
        return count > 0U;
    }

    static bool entry_is_deleted(const Entry& entry) noexcept
    {
        return any(entry.flags, EntryFlags::Deleted);
    }

    static void append_unique_entry(std::vector<EntryId>* entries, EntryId id)
    {
        if (!entries || id == kInvalidEntryId) {
            return;
        }
        for (size_t i = 0U; i < entries->size(); ++i) {
            if ((*entries)[i] == id) {
                return;
            }
        }
        entries->push_back(id);
    }

    static uint32_t crop_match_terms(std::string_view name,
                                     std::string_view group) noexcept
    {
        uint32_t terms = 0U;
        if (contains_ascii_case_insensitive(name, "crop")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Crop);
        }
        if (contains_ascii_case_insensitive(name, "border")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Border);
        }
        if (contains_ascii_case_insensitive(name, "margin")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Margin);
        }
        if (contains_ascii_case_insensitive(name, "padding")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Padding);
        }
        if (contains_ascii_case_insensitive(name, "activearea")
            || contains_ascii_case_insensitive(name, "active area")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::ActiveArea);
        }
        if (contains_ascii_case_insensitive(name, "origin")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Origin);
        }
        if (contains_ascii_case_insensitive(name, "offset")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Offset);
        }
        if (contains_ascii_case_insensitive(name, "size")
            || contains_ascii_case_insensitive(name, "width")
            || contains_ascii_case_insensitive(name, "height")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Size);
        }
        if (contains_ascii_case_insensitive(name, "sensor")
            || contains_ascii_case_insensitive(group, "phaseone")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor);
        }
        if (contains_ascii_case_insensitive(name, "image")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Image);
        }
        return terms;
    }

    static uint32_t exposure_gain_match_terms(std::string_view name) noexcept
    {
        uint32_t terms = 0U;
        if (contains_ascii_case_insensitive(name, "exposure")
            || contains_ascii_case_insensitive(name, "shutter")
            || contains_ascii_case_insensitive(name, "aperture")
            || contains_ascii_case_insensitive(name, "brightness")
            || contains_ascii_case_insensitive(name, "iso")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Exposure);
        }
        if (contains_ascii_case_insensitive(name, "bias")
            || contains_ascii_case_insensitive(name, "compensation")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Bias);
        }
        if (contains_ascii_case_insensitive(name, "gain")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Gain);
        }
        return terms;
    }

    static uint32_t white_balance_match_terms(std::string_view name) noexcept
    {
        uint32_t terms = 0U;
        if (contains_ascii_case_insensitive(name, "whitebalance")
            || contains_ascii_case_insensitive(name, "white balance")
            || contains_ascii_case_insensitive(name, "asshotneutral")
            || contains_ascii_case_insensitive(name, "asshotwhitexy")
            || contains_ascii_case_insensitive(name, "colortemp")
            || contains_ascii_case_insensitive(name, "color temperature")
            || starts_with_ascii_case_insensitive(name, "wb")
            || contains_ascii_case_insensitive(name, "_wb")
            || contains_ascii_case_insensitive(name, " wb")) {
            terms |= static_cast<uint32_t>(
                MetadataQueryMatchTerm::WhiteBalance);
        }
        return terms;
    }

    static uint32_t color_match_terms(std::string_view name) noexcept
    {
        uint32_t terms = 0U;
        if (contains_ascii_case_insensitive(name, "color")
            || contains_ascii_case_insensitive(name, "colour")
            || contains_ascii_case_insensitive(name, "illuminant")
            || contains_ascii_case_insensitive(name, "profile")
            || contains_ascii_case_insensitive(name, "tonecurve")
            || contains_ascii_case_insensitive(name, "tone curve")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Color);
        }
        if (contains_ascii_case_insensitive(name, "matrix")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Matrix);
        }
        if (contains_ascii_case_insensitive(name, "calibration")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Calibration);
        }
        if (contains_ascii_case_insensitive(name, "profile")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Profile);
        }
        return terms;
    }

    static uint32_t lens_correction_match_terms(std::string_view name) noexcept
    {
        uint32_t terms = 0U;
        if (contains_ascii_case_insensitive(name, "lens")
            || contains_ascii_case_insensitive(name, "distort")
            || contains_ascii_case_insensitive(name, "vignet")
            || contains_ascii_case_insensitive(name, "aberration")
            || contains_ascii_case_insensitive(name, "shading")
            || contains_ascii_case_insensitive(name, "peripheral")
            || contains_ascii_case_insensitive(name, "diffraction")
            || contains_ascii_case_insensitive(name, "opcode")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Lens);
        }
        if (contains_ascii_case_insensitive(name, "correction")
            || contains_ascii_case_insensitive(name, "corr")
            || contains_ascii_case_insensitive(name, "distort")
            || contains_ascii_case_insensitive(name, "vignet")
            || contains_ascii_case_insensitive(name, "aberration")
            || contains_ascii_case_insensitive(name, "shading")
            || contains_ascii_case_insensitive(name, "opcode")) {
            terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Correction);
        }
        return terms;
    }

    static uint32_t orientation_match_terms(std::string_view name) noexcept
    {
        if (contains_ascii_case_insensitive(name, "orientation")) {
            return static_cast<uint32_t>(MetadataQueryMatchTerm::Orientation);
        }
        return 0U;
    }

    static uint32_t exact_exif_terms_for_kind(uint16_t tag,
                                              MetadataQueryKind kind) noexcept
    {
        switch (kind) {
        case MetadataQueryKind::Crop:
            switch (tag) {
            case kDngDefaultCropOriginTag:
            case kDngDefaultCropSizeTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Crop);
            case kDngActiveAreaTag:
                return static_cast<uint32_t>(
                    MetadataQueryMatchTerm::ActiveArea);
            case kDngMaskedAreasTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Border);
            default: break;
            }
            return 0U;
        case MetadataQueryKind::ExposureGain:
            switch (tag) {
            case kExifExposureTimeTag:
            case kExifFNumberTag:
            case kExifExposureProgramTag:
            case kExifPhotographicSensitivityTag:
            case kExifShutterSpeedValueTag:
            case kExifApertureValueTag:
            case kExifBrightnessValueTag:
            case kExifMaxApertureValueTag:
            case kExifExposureIndexTag:
            case kDngBaselineExposureTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Exposure);
            case kExifExposureBiasValueTag:
            case kDngBaselineExposureOffsetTag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Exposure)
                       | static_cast<uint32_t>(MetadataQueryMatchTerm::Bias);
            case kExifGainControlTag:
            case kDngRawToPreviewGainTag:
            case kDngProfileGainTableMapTag:
            case kDngProfileGainTableMap2Tag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Gain);
            default: break;
            }
            return 0U;
        case MetadataQueryKind::WhiteBalance:
            switch (tag) {
            case kExifLightSourceTag:
            case kExifWhiteBalanceTag:
            case kDngAnalogBalanceTag:
            case kDngAsShotNeutralTag:
            case kDngAsShotWhiteXyTag:
            case kDngCalibrationIlluminant1Tag:
            case kDngCalibrationIlluminant2Tag:
            case kDngCalibrationIlluminant3Tag:
                return static_cast<uint32_t>(
                    MetadataQueryMatchTerm::WhiteBalance);
            default: break;
            }
            return 0U;
        case MetadataQueryKind::Color:
            switch (tag) {
            case kDngColorMatrix1Tag:
            case kDngColorMatrix2Tag:
            case kDngReductionMatrix1Tag:
            case kDngReductionMatrix2Tag:
            case kDngForwardMatrix1Tag:
            case kDngForwardMatrix2Tag:
            case kDngColorMatrix3Tag:
            case kDngForwardMatrix3Tag:
            case kDngReductionMatrix3Tag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Color)
                       | static_cast<uint32_t>(MetadataQueryMatchTerm::Matrix);
            case kDngCameraCalibration1Tag:
            case kDngCameraCalibration2Tag:
            case kDngCameraCalibration3Tag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Color)
                       | static_cast<uint32_t>(
                           MetadataQueryMatchTerm::Calibration);
            case kDngCalibrationIlluminant1Tag:
            case kDngCalibrationIlluminant2Tag:
            case kDngCalibrationIlluminant3Tag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Color);
            default: break;
            }
            return 0U;
        case MetadataQueryKind::LensCorrection:
            switch (tag) {
            case kSamsungVignettingCorrParamsTag:
            case kSamsungChromaticAberrationCorrParamsTag:
            case kSamsungDistortionCorrParamsTag:
            case kDngOpcodeList1Tag:
            case kDngOpcodeList2Tag:
            case kDngOpcodeList3Tag:
                return static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                       | static_cast<uint32_t>(
                           MetadataQueryMatchTerm::Correction);
            default: break;
            }
            return 0U;
        case MetadataQueryKind::Orientation:
            switch (tag) {
            case kExifOrientationTag:
            case kExifThumbnailOrientationTag:
                return static_cast<uint32_t>(
                    MetadataQueryMatchTerm::Orientation);
            default: break;
            }
            return 0U;
        }
        return 0U;
    }

    static uint32_t vendor_terms_for_kind(VendorRawProcessingGroup groups,
                                          MetadataQueryKind kind) noexcept
    {
        uint32_t terms = 0U;
        switch (kind) {
        case MetadataQueryKind::Crop:
            if (vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::Geometry)) {
                terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor);
            }
            break;
        case MetadataQueryKind::ExposureGain:
            break;
        case MetadataQueryKind::WhiteBalance:
            if (vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::WhiteBalance)) {
                terms |= static_cast<uint32_t>(
                    MetadataQueryMatchTerm::WhiteBalance);
            }
            break;
        case MetadataQueryKind::Color:
            if (vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::Color)) {
                terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Color);
            }
            break;
        case MetadataQueryKind::LensCorrection:
            if (vendor_raw_processing_group_has(
                    groups, VendorRawProcessingGroup::LensCorrection)) {
                terms |= static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                         | static_cast<uint32_t>(
                             MetadataQueryMatchTerm::Correction);
            }
            break;
        case MetadataQueryKind::Orientation: break;
        }
        return terms;
    }

    static uint32_t match_terms_for_kind(std::string_view name,
                                         std::string_view group,
                                         MetadataQueryKind kind) noexcept
    {
        switch (kind) {
        case MetadataQueryKind::Crop: return crop_match_terms(name, group);
        case MetadataQueryKind::ExposureGain:
            return exposure_gain_match_terms(name);
        case MetadataQueryKind::WhiteBalance:
            return white_balance_match_terms(name);
        case MetadataQueryKind::Color: return color_match_terms(name);
        case MetadataQueryKind::LensCorrection:
            return lens_correction_match_terms(name);
        case MetadataQueryKind::Orientation:
            return orientation_match_terms(name);
        }
        return 0U;
    }

    static MetadataQuerySemanticKind
    crop_semantic_from_terms(uint32_t terms) noexcept
    {
        if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::ActiveArea))
            != 0U) {
            return MetadataQuerySemanticKind::ActiveArea;
        }
        if ((terms
             & (static_cast<uint32_t>(MetadataQueryMatchTerm::Border)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Margin)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Padding)))
            != 0U) {
            return MetadataQuerySemanticKind::Border;
        }
        if ((terms
             & (static_cast<uint32_t>(MetadataQueryMatchTerm::Crop)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Origin)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Size)))
            != 0U) {
            return MetadataQuerySemanticKind::Crop;
        }
        return MetadataQuerySemanticKind::Unknown;
    }

    static MetadataQuerySemanticKind
    semantic_from_terms(MetadataQueryKind kind, uint32_t terms) noexcept
    {
        switch (kind) {
        case MetadataQueryKind::Crop: return crop_semantic_from_terms(terms);
        case MetadataQueryKind::ExposureGain:
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Gain))
                != 0U) {
                return MetadataQuerySemanticKind::Gain;
            }
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Exposure)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Bias)))
                != 0U) {
                return MetadataQuerySemanticKind::Exposure;
            }
            break;
        case MetadataQueryKind::WhiteBalance:
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::WhiteBalance))
                != 0U) {
                return MetadataQuerySemanticKind::WhiteBalance;
            }
            break;
        case MetadataQueryKind::Color:
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Matrix)
                    | static_cast<uint32_t>(
                        MetadataQueryMatchTerm::Calibration)))
                != 0U) {
                return MetadataQuerySemanticKind::ColorMatrix;
            }
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Color)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Profile)))
                != 0U) {
                return MetadataQuerySemanticKind::Color;
            }
            break;
        case MetadataQueryKind::LensCorrection:
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Correction)))
                != 0U) {
                return MetadataQuerySemanticKind::LensCorrection;
            }
            break;
        case MetadataQueryKind::Orientation:
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::Orientation))
                != 0U) {
                return MetadataQuerySemanticKind::Orientation;
            }
            break;
        }
        return MetadataQuerySemanticKind::Unknown;
    }

    static uint8_t crop_confidence_from_terms(uint32_t terms) noexcept
    {
        if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Crop))
            != 0U) {
            return 90U;
        }
        if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::ActiveArea))
            != 0U) {
            return 88U;
        }
        if ((terms
             & (static_cast<uint32_t>(MetadataQueryMatchTerm::Border)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Margin)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Padding)))
            != 0U) {
            return 70U;
        }
        if ((terms
             & (static_cast<uint32_t>(MetadataQueryMatchTerm::Sensor)
                | static_cast<uint32_t>(MetadataQueryMatchTerm::Image)))
            != 0U) {
            return 45U;
        }
        return 0U;
    }

    static uint8_t confidence_from_terms(MetadataQueryKind kind,
                                         uint32_t terms) noexcept
    {
        switch (kind) {
        case MetadataQueryKind::Crop: return crop_confidence_from_terms(terms);
        case MetadataQueryKind::ExposureGain:
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Exposure)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Gain)))
                != 0U) {
                return 90U;
            }
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Bias))
                != 0U) {
                return 82U;
            }
            break;
        case MetadataQueryKind::WhiteBalance:
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::WhiteBalance))
                != 0U) {
                return 90U;
            }
            break;
        case MetadataQueryKind::Color:
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Matrix))
                != 0U) {
                return 94U;
            }
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::Calibration))
                != 0U) {
                return 88U;
            }
            if ((terms & static_cast<uint32_t>(MetadataQueryMatchTerm::Color))
                != 0U) {
                return 70U;
            }
            break;
        case MetadataQueryKind::LensCorrection:
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Correction)))
                == (static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                    | static_cast<uint32_t>(
                        MetadataQueryMatchTerm::Correction))) {
                return 92U;
            }
            if ((terms
                 & (static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)
                    | static_cast<uint32_t>(MetadataQueryMatchTerm::Correction)))
                != 0U) {
                return 68U;
            }
            break;
        case MetadataQueryKind::Orientation:
            if ((terms
                 & static_cast<uint32_t>(MetadataQueryMatchTerm::Orientation))
                != 0U) {
                return 95U;
            }
            break;
        }
        return 0U;
    }

    static void append_match(MetadataQueryResult* result, EntryId entry_id,
                             const Entry& entry, std::string_view group,
                             std::string_view name, MetadataQueryKind kind,
                             uint32_t terms)
    {
        if (!result || terms == 0U) {
            return;
        }
        MetadataQueryMatch match;
        match.entry_id      = entry_id;
        match.key_kind      = entry.key.kind;
        match.semantic      = semantic_from_terms(kind, terms);
        match.shape         = value_shape(entry.value);
        match.confidence    = confidence_from_terms(kind, terms);
        match.matched_terms = terms;
        if (entry.key.kind == MetaKeyKind::ExifTag) {
            match.exif_tag = entry.key.data.exif_tag.tag;
        }
        match.group.assign(group.data(), group.size());
        match.name.assign(name.data(), name.size());
        result->matches.push_back(match);
    }

    static void append_exif_match_if_relevant(const MetaStore& store,
                                              MetadataQueryResult* result,
                                              EntryId entry_id,
                                              const Entry& entry,
                                              MetadataQueryKind kind)
    {
        const std::string_view ifd = arena_string(store.arena(),
                                                  entry.key.data.exif_tag.ifd);
        const std::string_view name
            = exif_entry_name(store, entry, ExifTagNamePolicy::ExifToolCompat);
        uint32_t terms = match_terms_for_kind(name, ifd, kind);
        terms |= exact_exif_terms_for_kind(entry.key.data.exif_tag.tag, kind);
        const VendorRawProcessingGroup groups
            = classify_vendor_raw_processing_field(ifd, name,
                                                   entry.key.data.exif_tag.tag);
        terms |= vendor_terms_for_kind(groups, kind);
        append_match(result, entry_id, entry, ifd, name, kind, terms);
    }

    static void append_xmp_match_if_relevant(const MetaStore& store,
                                             MetadataQueryResult* result,
                                             EntryId entry_id,
                                             const Entry& entry,
                                             MetadataQueryKind kind)
    {
        const std::string_view ns
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.schema_ns);
        const std::string_view path
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.property_path);
        const uint32_t terms = match_terms_for_kind(path, ns, kind);
        append_match(result, entry_id, entry, ns, path, kind, terms);
    }

    static bool exif_entry_is(const MetaStore& store, const Entry& entry,
                              std::string_view ifd, uint16_t tag) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag
            || entry.key.data.exif_tag.tag != tag) {
            return false;
        }
        return arena_string(store.arena(), entry.key.data.exif_tag.ifd) == ifd;
    }

    static EntryId find_first_exif_entry(const MetaStore& store,
                                         std::string_view ifd,
                                         uint16_t tag) noexcept
    {
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            if (entry_is_deleted(entries[i])) {
                continue;
            }
            if (exif_entry_is(store, entries[i], ifd, tag)) {
                return static_cast<EntryId>(i);
            }
        }
        return kInvalidEntryId;
    }

    static EntryId find_first_exif_tag_any_ifd(const MetaStore& store,
                                               uint16_t tag) noexcept
    {
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            if (entry_is_deleted(entries[i])
                || entries[i].key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            if (entries[i].key.data.exif_tag.tag == tag) {
                return static_cast<EntryId>(i);
            }
        }
        return kInvalidEntryId;
    }

    static void append_default_crop_candidate(const MetaStore& store,
                                              MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }

        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& origin_entry = entries[i];
            if (entry_is_deleted(origin_entry)
                || origin_entry.key.kind != MetaKeyKind::ExifTag
                || origin_entry.key.data.exif_tag.tag
                       != kDngDefaultCropOriginTag) {
                continue;
            }
            const std::string_view ifd
                = arena_string(store.arena(),
                               origin_entry.key.data.exif_tag.ifd);
            const EntryId origin_id = static_cast<EntryId>(i);
            const EntryId size_id
                = find_first_exif_entry(store, ifd, kDngDefaultCropSizeTag);
            if (size_id == kInvalidEntryId) {
                continue;
            }

            double origin_values[4] {};
            double size_values[4] {};
            uint32_t origin_count = 0U;
            uint32_t size_count   = 0U;
            if (!value_to_double_array(store, store.entry(origin_id).value,
                                       origin_values, 4U, &origin_count)
                || !value_to_double_array(store, store.entry(size_id).value,
                                          size_values, 4U, &size_count)
                || origin_count < 2U || size_count < 2U) {
                continue;
            }

            MetadataQueryCandidate candidate;
            candidate.semantic         = MetadataQuerySemanticKind::Crop;
            candidate.normalized_shape = MetadataQueryValueShape::Rect;
            candidate.confidence       = 95U;
            append_unique_entry(&candidate.source_entries, origin_id);
            append_unique_entry(&candidate.source_entries, size_id);
            candidate.has_origin = true;
            candidate.origin[0]  = origin_values[0];
            candidate.origin[1]  = origin_values[1];
            candidate.has_size   = true;
            candidate.size[0]    = size_values[0];
            candidate.size[1]    = size_values[1];
            candidate.has_rect   = true;
            candidate.rect[0]    = origin_values[0];
            candidate.rect[1]    = origin_values[1];
            candidate.rect[2]    = size_values[0];
            candidate.rect[3]    = size_values[1];
            result->candidates.push_back(candidate);
        }
    }

    static void append_active_area_candidate(const MetaStore& store,
                                             MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }
        const EntryId active_id
            = find_first_exif_tag_any_ifd(store, kDngActiveAreaTag);
        if (active_id == kInvalidEntryId) {
            return;
        }
        double values[8] {};
        uint32_t count = 0U;
        if (!value_to_double_array(store, store.entry(active_id).value, values,
                                   8U, &count)
            || count < 4U) {
            return;
        }
        const double top    = values[0];
        const double left   = values[1];
        const double bottom = values[2];
        const double right  = values[3];
        if (right < left || bottom < top) {
            return;
        }

        MetadataQueryCandidate candidate;
        candidate.semantic         = MetadataQuerySemanticKind::ActiveArea;
        candidate.normalized_shape = MetadataQueryValueShape::Rect;
        candidate.confidence       = 92U;
        append_unique_entry(&candidate.source_entries, active_id);
        candidate.has_origin = true;
        candidate.origin[0]  = left;
        candidate.origin[1]  = top;
        candidate.has_size   = true;
        candidate.size[0]    = right - left;
        candidate.size[1]    = bottom - top;
        candidate.has_rect   = true;
        candidate.rect[0]    = left;
        candidate.rect[1]    = top;
        candidate.rect[2]    = right - left;
        candidate.rect[3]    = bottom - top;
        result->candidates.push_back(candidate);
    }

    static void append_phaseone_crop_candidate(const MetaStore& store,
                                               MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }
        const PhaseOneRawGeometryResult geometry
            = phaseone_raw_geometry_from_store(store);
        if (geometry.status != PhaseOneRawGeometryStatus::Ok) {
            return;
        }
        MetadataQueryCandidate candidate;
        candidate.semantic         = MetadataQuerySemanticKind::ActiveArea;
        candidate.normalized_shape = MetadataQueryValueShape::Rect;
        candidate.confidence       = 96U;
        append_unique_entry(&candidate.source_entries,
                            find_first_exif_entry(store, kPhaseOneMainIfd,
                                                  kPhaseOneSensorWidthTag));
        append_unique_entry(&candidate.source_entries,
                            find_first_exif_entry(store, kPhaseOneMainIfd,
                                                  kPhaseOneSensorHeightTag));
        append_unique_entry(&candidate.source_entries,
                            find_first_exif_entry(store, kPhaseOneMainIfd,
                                                  kPhaseOneSensorLeftMarginTag));
        append_unique_entry(&candidate.source_entries,
                            find_first_exif_entry(store, kPhaseOneMainIfd,
                                                  kPhaseOneSensorTopMarginTag));
        append_unique_entry(&candidate.source_entries,
                            find_first_exif_entry(store, kPhaseOneMainIfd,
                                                  kPhaseOneImageWidthTag));
        append_unique_entry(&candidate.source_entries,
                            find_first_exif_entry(store, kPhaseOneMainIfd,
                                                  kPhaseOneImageHeightTag));
        candidate.has_origin = true;
        candidate.origin[0]  = geometry.geometry.active_x;
        candidate.origin[1]  = geometry.geometry.active_y;
        candidate.has_size   = true;
        candidate.size[0]    = geometry.geometry.active_width;
        candidate.size[1]    = geometry.geometry.active_height;
        candidate.has_rect   = true;
        candidate.rect[0]    = geometry.geometry.active_x;
        candidate.rect[1]    = geometry.geometry.active_y;
        candidate.rect[2]    = geometry.geometry.active_width;
        candidate.rect[3]    = geometry.geometry.active_height;
        candidate.has_values = true;
        candidate.values.reserve(4U);
        candidate.values.push_back(geometry.geometry.sensor_left_margin);
        candidate.values.push_back(geometry.geometry.sensor_top_margin);
        candidate.values.push_back(geometry.geometry.right_margin);
        candidate.values.push_back(geometry.geometry.bottom_margin);
        result->candidates.push_back(candidate);
    }

    static void append_query_value_candidate(const MetaStore& store,
                                             MetadataQueryResult* result,
                                             const MetadataQueryMatch& match)
    {
        if (!result || match.entry_id == kInvalidEntryId
            || match.semantic == MetadataQuerySemanticKind::Unknown) {
            return;
        }
        const Entry& entry = store.entry(match.entry_id);
        MetadataQueryCandidate candidate;
        candidate.semantic         = match.semantic;
        candidate.normalized_shape = value_shape(entry.value);
        candidate.confidence       = match.confidence;
        candidate.source_entries.reserve(1U);
        candidate.source_entries.push_back(match.entry_id);

        double values[16] {};
        uint32_t count = 0U;
        if (value_to_double_array(store, entry.value, values, 16U, &count)) {
            candidate.has_values = true;
            candidate.values.reserve(count);
            for (uint32_t i = 0U; i < count; ++i) {
                candidate.values.push_back(values[i]);
            }
        }
        result->candidates.push_back(candidate);
    }

    static void append_query_value_candidates(const MetaStore& store,
                                              MetadataQueryResult* result)
    {
        if (!result) {
            return;
        }
        result->candidates.reserve(result->matches.size());
        for (size_t i = 0U; i < result->matches.size(); ++i) {
            append_query_value_candidate(store, result, result->matches[i]);
        }
    }

    static MetadataQueryResult query_semantic_metadata(const MetaStore& store,
                                                       MetadataQueryKind kind)
    {
        MetadataQueryResult result;
        result.kind = kind;

        const std::span<const Entry> entries = store.entries();
        result.matches.reserve(entries.size());
        for (size_t i = 0U; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (entry_is_deleted(entry)) {
                continue;
            }
            if (entry.key.kind == MetaKeyKind::ExifTag) {
                append_exif_match_if_relevant(store, &result,
                                              static_cast<EntryId>(i), entry,
                                              kind);
            } else if (entry.key.kind == MetaKeyKind::XmpProperty) {
                append_xmp_match_if_relevant(store, &result,
                                             static_cast<EntryId>(i), entry,
                                             kind);
            }
        }
        append_query_value_candidates(store, &result);
        return result;
    }

}  // namespace

MetadataQueryResult
query_metadata(const MetaStore& store, MetadataQueryKind kind)
{
    switch (kind) {
    case MetadataQueryKind::Crop: return query_crop_metadata(store);
    case MetadataQueryKind::ExposureGain:
        return query_exposure_gain_metadata(store);
    case MetadataQueryKind::WhiteBalance:
        return query_white_balance_metadata(store);
    case MetadataQueryKind::Color: return query_color_metadata(store);
    case MetadataQueryKind::LensCorrection:
        return query_lens_correction_metadata(store);
    case MetadataQueryKind::Orientation:
        return query_orientation_metadata(store);
    }
    MetadataQueryResult result;
    result.kind = kind;
    return result;
}

MetadataQueryResult
query_crop_metadata(const MetaStore& store)
{
    MetadataQueryResult result;
    result.kind = MetadataQueryKind::Crop;

    const std::span<const Entry> entries = store.entries();
    result.matches.reserve(entries.size());
    for (size_t i = 0U; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        if (entry_is_deleted(entry)) {
            continue;
        }
        if (entry.key.kind == MetaKeyKind::ExifTag) {
            append_exif_match_if_relevant(store, &result,
                                          static_cast<EntryId>(i), entry,
                                          MetadataQueryKind::Crop);
        } else if (entry.key.kind == MetaKeyKind::XmpProperty) {
            append_xmp_match_if_relevant(store, &result,
                                         static_cast<EntryId>(i), entry,
                                         MetadataQueryKind::Crop);
        }
    }

    append_default_crop_candidate(store, &result);
    append_active_area_candidate(store, &result);
    append_phaseone_crop_candidate(store, &result);
    return result;
}

MetadataQueryResult
query_exposure_gain_metadata(const MetaStore& store)
{
    return query_semantic_metadata(store, MetadataQueryKind::ExposureGain);
}

MetadataQueryResult
query_white_balance_metadata(const MetaStore& store)
{
    return query_semantic_metadata(store, MetadataQueryKind::WhiteBalance);
}

MetadataQueryResult
query_color_metadata(const MetaStore& store)
{
    return query_semantic_metadata(store, MetadataQueryKind::Color);
}

MetadataQueryResult
query_lens_correction_metadata(const MetaStore& store)
{
    return query_semantic_metadata(store, MetadataQueryKind::LensCorrection);
}

MetadataQueryResult
query_orientation_metadata(const MetaStore& store)
{
    return query_semantic_metadata(store, MetadataQueryKind::Orientation);
}

const char*
metadata_query_kind_name(MetadataQueryKind kind) noexcept
{
    switch (kind) {
    case MetadataQueryKind::Crop: return "crop";
    case MetadataQueryKind::ExposureGain: return "exposure_gain";
    case MetadataQueryKind::WhiteBalance: return "white_balance";
    case MetadataQueryKind::Color: return "color";
    case MetadataQueryKind::LensCorrection: return "lens_correction";
    case MetadataQueryKind::Orientation: return "orientation";
    }
    return "unknown";
}

const char*
metadata_query_semantic_kind_name(MetadataQuerySemanticKind kind) noexcept
{
    switch (kind) {
    case MetadataQuerySemanticKind::Unknown: return "unknown";
    case MetadataQuerySemanticKind::Crop: return "crop";
    case MetadataQuerySemanticKind::Border: return "border";
    case MetadataQuerySemanticKind::ActiveArea: return "active_area";
    case MetadataQuerySemanticKind::Exposure: return "exposure";
    case MetadataQuerySemanticKind::Gain: return "gain";
    case MetadataQuerySemanticKind::Color: return "color";
    case MetadataQuerySemanticKind::WhiteBalance: return "white_balance";
    case MetadataQuerySemanticKind::ColorMatrix: return "color_matrix";
    case MetadataQuerySemanticKind::LensCorrection: return "lens_correction";
    case MetadataQuerySemanticKind::Orientation: return "orientation";
    }
    return "unknown";
}

const char*
metadata_query_value_shape_name(MetadataQueryValueShape shape) noexcept
{
    switch (shape) {
    case MetadataQueryValueShape::Unknown: return "unknown";
    case MetadataQueryValueShape::Scalar: return "scalar";
    case MetadataQueryValueShape::Vec2: return "vec2";
    case MetadataQueryValueShape::Vec3: return "vec3";
    case MetadataQueryValueShape::Vec4: return "vec4";
    case MetadataQueryValueShape::Rect: return "rect";
    case MetadataQueryValueShape::Matrix3x3: return "matrix3x3";
    case MetadataQueryValueShape::Array: return "array";
    case MetadataQueryValueShape::Blob: return "blob";
    case MetadataQueryValueShape::Text: return "text";
    }
    return "unknown";
}

}  // namespace openmeta
