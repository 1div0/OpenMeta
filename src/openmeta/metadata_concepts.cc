// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_concepts.h"

#include "openmeta/byte_arena.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/exif_value_names.h"
#include "openmeta/meta_flags.h"
#include "openmeta/metadata_interpretation.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static constexpr uint16_t kExifOrientationTag             = 0x0112U;
    static constexpr uint16_t kExifDateTimeTag                = 0x0132U;
    static constexpr uint16_t kExifExposureTimeTag            = 0x829AU;
    static constexpr uint16_t kExifFNumberTag                 = 0x829DU;
    static constexpr uint16_t kExifExposureProgramTag         = 0x8822U;
    static constexpr uint16_t kExifPhotographicSensitivityTag = 0x8827U;
    static constexpr uint16_t kExifDateTimeOriginalTag        = 0x9003U;
    static constexpr uint16_t kExifDateTimeDigitizedTag       = 0x9004U;
    static constexpr uint16_t kExifShutterSpeedValueTag       = 0x9201U;
    static constexpr uint16_t kExifApertureValueTag           = 0x9202U;
    static constexpr uint16_t kExifExposureBiasValueTag       = 0x9204U;
    static constexpr uint16_t kExifMaxApertureValueTag        = 0x9205U;
    static constexpr uint16_t kExifExposureIndexTag           = 0x9215U;
    static constexpr uint16_t kExifColorSpaceTag              = 0xA001U;
    static constexpr uint16_t kExifGainControlTag             = 0xA407U;
    static constexpr uint16_t kDngBaselineExposureTag         = 0xC62AU;
    static constexpr uint16_t kDngBaselineExposureOffsetTag   = 0xC7A5U;
    static constexpr uint16_t kDngRawToPreviewGainTag         = 0xC7A8U;
    static constexpr uint16_t kDngProfileGainTableMapTag      = 0xCD2DU;
    static constexpr uint16_t kDngProfileGainTableMap2Tag     = 0xCD40U;
    static constexpr uint16_t kGpsLatitudeRefTag              = 0x0001U;
    static constexpr uint16_t kGpsLatitudeTag                 = 0x0002U;
    static constexpr uint16_t kGpsLongitudeRefTag             = 0x0003U;
    static constexpr uint16_t kGpsLongitudeTag                = 0x0004U;
    static constexpr uint16_t kGpsAltitudeRefTag              = 0x0005U;
    static constexpr uint16_t kGpsAltitudeTag                 = 0x0006U;
    static constexpr uint16_t kGpsTimeStampTag                = 0x0007U;
    static constexpr uint16_t kGpsDateStampTag                = 0x001DU;
    static constexpr uint16_t kIptcDateCreatedDataset         = 55U;
    static constexpr uint16_t kIptcTimeCreatedDataset         = 60U;
    static constexpr uint32_t kIccHeaderRgbColorSpaceOffset   = 16U;

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

    static bool ascii_is_digit(char c) noexcept { return c >= '0' && c <= '9'; }

    static bool ascii_is_alnum(char c) noexcept
    {
        if (c >= '0' && c <= '9') {
            return true;
        }
        if (c >= 'A' && c <= 'Z') {
            return true;
        }
        return c >= 'a' && c <= 'z';
    }

    static bool ascii_equal_ci(std::string_view a, std::string_view b) noexcept
    {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0U; i < a.size(); ++i) {
            if (ascii_lower(a[i]) != ascii_lower(b[i])) {
                return false;
            }
        }
        return true;
    }

    static bool ascii_contains_ci(std::string_view text,
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

    static bool ascii_ends_with_ci(std::string_view text,
                                   std::string_view suffix) noexcept
    {
        if (text.size() < suffix.size()) {
            return false;
        }
        const size_t offset = text.size() - suffix.size();
        for (size_t i = 0U; i < suffix.size(); ++i) {
            if (ascii_lower(text[offset + i]) != ascii_lower(suffix[i])) {
                return false;
            }
        }
        return true;
    }

    static bool xmp_leaf_matches(std::string_view path,
                                 std::string_view name) noexcept
    {
        if (ascii_equal_ci(path, name)) {
            return true;
        }
        if (!ascii_ends_with_ci(path, name)) {
            return false;
        }
        const size_t offset = path.size() - name.size();
        if (offset == 0U) {
            return true;
        }
        const char c = path[offset - 1U];
        return c == ':' || c == '/' || c == '.';
    }

    static MetadataConceptSourceFamily
    source_family_for_entry(const Entry& entry) noexcept
    {
        switch (entry.key.kind) {
        case MetaKeyKind::ExifTag: return MetadataConceptSourceFamily::Exif;
        case MetaKeyKind::XmpProperty: return MetadataConceptSourceFamily::Xmp;
        case MetaKeyKind::IptcDataset: return MetadataConceptSourceFamily::Iptc;
        case MetaKeyKind::IccHeaderField:
        case MetaKeyKind::IccTag: return MetadataConceptSourceFamily::Icc;
        case MetaKeyKind::PngText: return MetadataConceptSourceFamily::PngText;
        case MetaKeyKind::Comment:
        case MetaKeyKind::ExrAttribute:
        case MetaKeyKind::PhotoshopIrb:
        case MetaKeyKind::PhotoshopIrbField:
        case MetaKeyKind::GeotiffKey:
        case MetaKeyKind::PrintImField:
        case MetaKeyKind::BmffField:
        case MetaKeyKind::JumbfField:
        case MetaKeyKind::JumbfCborKey: break;
        }
        return MetadataConceptSourceFamily::Unknown;
    }

    static uint32_t element_size(MetaElementType type) noexcept
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
        case MetaElementType::F32: {
            float f = 0.0F;
            std::memcpy(&f, &value.data.f32_bits, sizeof(f));
            *out = static_cast<double>(f);
            return true;
        }
        case MetaElementType::F64: {
            double d = 0.0;
            std::memcpy(&d, &value.data.f64_bits, sizeof(d));
            *out = d;
            return true;
        }
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
                                        MetaElementType type, uint32_t index,
                                        double* out) noexcept
    {
        if (!out) {
            return false;
        }
        const uint32_t elem_size = element_size(type);
        if (elem_size == 0U) {
            return false;
        }
        const size_t offset = static_cast<size_t>(index)
                              * static_cast<size_t>(elem_size);
        if (offset > bytes.size()
            || bytes.size() - offset < static_cast<size_t>(elem_size)) {
            return false;
        }
        const std::byte* data = bytes.data() + offset;
        switch (type) {
        case MetaElementType::U8: {
            uint8_t v = 0U;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::I8: {
            int8_t v = 0;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::U16: {
            uint16_t v = 0U;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::I16: {
            int16_t v = 0;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::U32: {
            uint32_t v = 0U;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::I32: {
            int32_t v = 0;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::U64: {
            uint64_t v = 0U;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::I64: {
            int64_t v = 0;
            std::memcpy(&v, data, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::F32: {
            uint32_t bits = 0U;
            float v       = 0.0F;
            std::memcpy(&bits, data, sizeof(bits));
            std::memcpy(&v, &bits, sizeof(v));
            *out = static_cast<double>(v);
            return true;
        }
        case MetaElementType::F64: {
            uint64_t bits = 0U;
            double v      = 0.0;
            std::memcpy(&bits, data, sizeof(bits));
            std::memcpy(&v, &bits, sizeof(v));
            *out = v;
            return true;
        }
        case MetaElementType::URational: {
            URational v;
            std::memcpy(&v, data, sizeof(v));
            if (v.denom == 0U) {
                return false;
            }
            *out = static_cast<double>(v.numer) / static_cast<double>(v.denom);
            return true;
        }
        case MetaElementType::SRational: {
            SRational v;
            std::memcpy(&v, data, sizeof(v));
            if (v.denom == 0) {
                return false;
            }
            *out = static_cast<double>(v.numer) / static_cast<double>(v.denom);
            return true;
        }
        }
        return false;
    }

    static uint8_t value_to_numeric_array(const ByteArena& arena,
                                          const MetaValue& value, double* out,
                                          uint8_t max_count) noexcept
    {
        if (!out || max_count == 0U) {
            return 0U;
        }
        if (value.kind == MetaValueKind::Scalar) {
            double v = 0.0;
            if (!scalar_to_double(value, &v)) {
                return 0U;
            }
            out[0] = v;
            return 1U;
        }
        if (value.kind != MetaValueKind::Array) {
            return 0U;
        }
        const uint32_t elem_size               = element_size(value.elem_type);
        const std::span<const std::byte> bytes = arena.span(value.data.span);
        if (elem_size == 0U) {
            return 0U;
        }
        if (value.count > bytes.size() / elem_size) {
            return 0U;
        }
        const uint8_t count = static_cast<uint8_t>(
            std::min<uint32_t>(value.count, max_count));
        uint8_t written = 0U;
        for (uint8_t i = 0U; i < count; ++i) {
            double v = 0.0;
            if (!array_element_to_double(bytes, value.elem_type, i, &v)) {
                break;
            }
            out[written] = v;
            written += 1U;
        }
        return written;
    }

    static bool value_to_text(const ByteArena& arena, const MetaValue& value,
                              std::string* out)
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (value.kind == MetaValueKind::Text) {
            const std::span<const std::byte> bytes = arena.span(
                value.data.span);
            out->assign(reinterpret_cast<const char*>(bytes.data()),
                        bytes.size());
            return true;
        }
        if (value.kind != MetaValueKind::Scalar) {
            return false;
        }
        double v = 0.0;
        if (!scalar_to_double(value, &v)) {
            return false;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.12g", v);
        out->assign(buf);
        return true;
    }

    static void normalize_text_key(std::string_view text, std::string* out)
    {
        if (!out) {
            return;
        }
        out->clear();
        out->reserve(text.size());
        for (size_t i = 0U; i < text.size(); ++i) {
            const char c = text[i];
            if (ascii_is_alnum(c)) {
                out->push_back(ascii_lower(c));
            }
        }
    }

    static uint32_t parse_decimal_digits(std::string_view text, size_t offset,
                                         size_t count) noexcept
    {
        uint32_t value = 0U;
        for (size_t i = 0U; i < count; ++i) {
            value *= 10U;
            value += static_cast<uint32_t>(text[offset + i] - '0');
        }
        return value;
    }

    static bool valid_date(uint32_t year, uint32_t month, uint32_t day) noexcept
    {
        if (year < 1U || year > 9999U || month < 1U || month > 12U) {
            return false;
        }
        static constexpr uint8_t days_in_month[] = {
            31U, 28U, 31U, 30U, 31U, 30U, 31U, 31U, 30U, 31U, 30U, 31U,
        };
        uint32_t max_day = days_in_month[month - 1U];
        const bool leap  = (year % 4U == 0U)
                          && ((year % 100U) != 0U || (year % 400U) == 0U);
        if (month == 2U && leap) {
            max_day = 29U;
        }
        return day >= 1U && day <= max_day;
    }

    static bool valid_time(uint32_t hour, uint32_t minute,
                           uint32_t second) noexcept
    {
        return hour < 24U && minute < 60U && second < 61U;
    }

    static void set_datetime_precision(MetadataConceptCandidate* candidate)
    {
        if (!candidate || !candidate->has_date_time) {
            return;
        }
        if (candidate->date_time_has_time) {
            candidate->date_time_precision
                = MetadataConceptDateTimePrecision::DateTime;
        } else {
            candidate->date_time_precision
                = MetadataConceptDateTimePrecision::Date;
        }
    }

    static void set_datetime_timezone(MetadataConceptCandidate* candidate,
                                      bool has_offset,
                                      int16_t offset_min) noexcept
    {
        if (!candidate) {
            return;
        }
        candidate->date_time_has_utc_offset = has_offset;
        if (!has_offset) {
            candidate->date_time_zone
                = candidate->date_time_has_time
                      ? MetadataConceptTimeZoneKind::Local
                      : MetadataConceptTimeZoneKind::Unknown;
            return;
        }
        candidate->date_time_utc_offset_min = offset_min;
        if (offset_min == 0) {
            candidate->date_time_zone = MetadataConceptTimeZoneKind::Utc;
        } else {
            candidate->date_time_zone = MetadataConceptTimeZoneKind::Offset;
        }
    }

    static void format_datetime_key(const MetadataConceptCandidate& candidate,
                                    std::string* out)
    {
        if (!out) {
            return;
        }
        out->clear();
        if (!candidate.has_date_time) {
            return;
        }
        char buf[32];
        if (candidate.date_time_has_time) {
            std::snprintf(buf, sizeof(buf), "%04d%02u%02u%02u%02u%02u",
                          static_cast<int>(candidate.date_time_year),
                          static_cast<unsigned>(candidate.date_time_month),
                          static_cast<unsigned>(candidate.date_time_day),
                          static_cast<unsigned>(candidate.date_time_hour),
                          static_cast<unsigned>(candidate.date_time_minute),
                          static_cast<unsigned>(candidate.date_time_second));
        } else {
            std::snprintf(buf, sizeof(buf), "%04d%02u%02u",
                          static_cast<int>(candidate.date_time_year),
                          static_cast<unsigned>(candidate.date_time_month),
                          static_cast<unsigned>(candidate.date_time_day));
        }
        out->assign(buf);
    }

    static bool timezone_offset_from_text(std::string_view text,
                                          uint32_t min_digits_before,
                                          int16_t* offset_min) noexcept
    {
        if (!offset_min) {
            return false;
        }
        uint32_t digits_before = 0U;
        for (size_t i = 0U; i < text.size(); ++i) {
            if (ascii_is_digit(text[i])) {
                digits_before += 1U;
                continue;
            }
            if ((text[i] == 'Z' || text[i] == 'z')
                && digits_before >= min_digits_before) {
                *offset_min = 0;
                return true;
            }
            if ((text[i] == '+' || text[i] == '-')
                && digits_before >= min_digits_before && i + 2U < text.size()
                && ascii_is_digit(text[i + 1U])
                && ascii_is_digit(text[i + 2U])) {
                const uint32_t hour = parse_decimal_digits(text, i + 1U, 2U);
                size_t minute_pos   = i + 3U;
                if (minute_pos < text.size() && text[minute_pos] == ':') {
                    minute_pos += 1U;
                }
                uint32_t minute = 0U;
                if (minute_pos + 1U < text.size()
                    && ascii_is_digit(text[minute_pos])
                    && ascii_is_digit(text[minute_pos + 1U])) {
                    minute = parse_decimal_digits(text, minute_pos, 2U);
                }
                if (hour > 23U || minute > 59U) {
                    return false;
                }
                int32_t signed_offset = static_cast<int32_t>(hour * 60U
                                                             + minute);
                if (text[i] == '-') {
                    signed_offset = -signed_offset;
                }
                *offset_min = static_cast<int16_t>(signed_offset);
                return true;
            }
        }
        return false;
    }

    static bool fill_datetime_from_text(std::string_view text,
                                        MetadataConceptCandidate* candidate)
    {
        if (!candidate) {
            return false;
        }
        std::string digits;
        digits.reserve(14U);
        for (size_t i = 0U; i < text.size(); ++i) {
            if (ascii_is_digit(text[i])) {
                digits.push_back(text[i]);
                if (digits.size() == 14U) {
                    break;
                }
            }
        }
        if (digits.size() < 8U) {
            return false;
        }
        const uint32_t year  = parse_decimal_digits(digits, 0U, 4U);
        const uint32_t month = parse_decimal_digits(digits, 4U, 2U);
        const uint32_t day   = parse_decimal_digits(digits, 6U, 2U);
        if (!valid_date(year, month, day)) {
            return false;
        }

        candidate->has_date_time      = true;
        candidate->date_time_year     = static_cast<int16_t>(year);
        candidate->date_time_month    = static_cast<uint8_t>(month);
        candidate->date_time_day      = static_cast<uint8_t>(day);
        candidate->date_time_has_time = false;
        candidate->date_time_zone     = MetadataConceptTimeZoneKind::Unknown;
        if (digits.size() >= 14U) {
            const uint32_t hour   = parse_decimal_digits(digits, 8U, 2U);
            const uint32_t minute = parse_decimal_digits(digits, 10U, 2U);
            const uint32_t second = parse_decimal_digits(digits, 12U, 2U);
            if (valid_time(hour, minute, second)) {
                candidate->date_time_has_time = true;
                candidate->date_time_hour     = static_cast<uint8_t>(hour);
                candidate->date_time_minute   = static_cast<uint8_t>(minute);
                candidate->date_time_second   = static_cast<uint8_t>(second);
                candidate->date_time_zone = MetadataConceptTimeZoneKind::Local;
            }
        }
        set_datetime_precision(candidate);
        int16_t offset = 0;
        if (timezone_offset_from_text(text, 14U, &offset)) {
            set_datetime_timezone(candidate, true, offset);
        }
        format_datetime_key(*candidate, &candidate->value_key);
        return true;
    }

    static bool fill_time_from_value(const ByteArena& arena,
                                     const MetaValue& value, uint8_t* hour,
                                     uint8_t* minute, uint8_t* second,
                                     bool* has_utc_offset,
                                     int16_t* utc_offset_min)
    {
        if (!hour || !minute || !second || !has_utc_offset || !utc_offset_min) {
            return false;
        }
        *has_utc_offset = false;
        *utc_offset_min = 0;
        double values[3] {};
        if (value_to_numeric_array(arena, value, values, 3U) == 3U) {
            if (values[0] < 0.0 || values[1] < 0.0 || values[2] < 0.0) {
                return false;
            }
            const uint32_t h = static_cast<uint32_t>(values[0]);
            const uint32_t m = static_cast<uint32_t>(values[1]);
            const uint32_t s = static_cast<uint32_t>(values[2]);
            if (!valid_time(h, m, s)) {
                return false;
            }
            *hour   = static_cast<uint8_t>(h);
            *minute = static_cast<uint8_t>(m);
            *second = static_cast<uint8_t>(s);
            return true;
        }

        std::string text;
        if (!value_to_text(arena, value, &text)) {
            return false;
        }
        std::string digits;
        digits.reserve(6U);
        for (size_t i = 0U; i < text.size(); ++i) {
            if (ascii_is_digit(text[i])) {
                digits.push_back(text[i]);
                if (digits.size() == 6U) {
                    break;
                }
            }
        }
        if (digits.size() < 6U) {
            return false;
        }
        const uint32_t h = parse_decimal_digits(digits, 0U, 2U);
        const uint32_t m = parse_decimal_digits(digits, 2U, 2U);
        const uint32_t s = parse_decimal_digits(digits, 4U, 2U);
        if (!valid_time(h, m, s)) {
            return false;
        }
        *hour          = static_cast<uint8_t>(h);
        *minute        = static_cast<uint8_t>(m);
        *second        = static_cast<uint8_t>(s);
        int16_t offset = 0;
        if (timezone_offset_from_text(text, 6U, &offset)) {
            *has_utc_offset = true;
            *utc_offset_min = offset;
        }
        return true;
    }

    static bool attach_time_to_candidate(MetadataConceptCandidate* candidate,
                                         uint8_t hour, uint8_t minute,
                                         uint8_t second, bool has_utc_offset,
                                         int16_t utc_offset_min) noexcept
    {
        if (!candidate || !candidate->has_date_time) {
            return false;
        }
        if (!valid_time(hour, minute, second)) {
            return false;
        }
        candidate->date_time_has_time = true;
        candidate->date_time_hour     = hour;
        candidate->date_time_minute   = minute;
        candidate->date_time_second   = second;
        set_datetime_precision(candidate);
        set_datetime_timezone(candidate, has_utc_offset, utc_offset_min);
        format_datetime_key(*candidate, &candidate->value_key);
        return true;
    }

    static std::string numeric_key(double v)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.12g", v);
        return std::string(buf);
    }

    static std::string gps_numeric_key(double v)
    {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.8f", v);
        return std::string(buf);
    }

    static bool add_unique_entry(std::vector<EntryId>* entries,
                                 EntryId entry_id)
    {
        if (!entries || entry_id == kInvalidEntryId) {
            return false;
        }
        for (size_t i = 0U; i < entries->size(); ++i) {
            if ((*entries)[i] == entry_id) {
                return false;
            }
        }
        entries->push_back(entry_id);
        return true;
    }

    static void set_transfer_hint(MetadataConceptCandidate* candidate,
                                  MetadataConceptTransferHint hint,
                                  bool compatible_safe,
                                  bool rendered_safe) noexcept
    {
        if (!candidate) {
            return;
        }
        candidate->transfer_hint        = hint;
        candidate->compatible_file_safe = compatible_safe;
        candidate->rendered_image_safe  = rendered_safe;
        candidate->requires_target_image_spec
            = hint == MetadataConceptTransferHint::RequiresTargetImageSpec;
        candidate->source_bound
            = hint == MetadataConceptTransferHint::SourceBound
              || hint == MetadataConceptTransferHint::RenderedUnsafe;
    }

    static void
    assign_transfer_hint(MetadataConceptCandidate* candidate) noexcept
    {
        if (!candidate) {
            return;
        }
        switch (candidate->kind) {
        case MetadataConceptKind::DateTime:
        case MetadataConceptKind::Gps:
            set_transfer_hint(candidate, MetadataConceptTransferHint::Safe,
                              true, true);
            return;
        case MetadataConceptKind::Exposure:
            if (candidate->role == MetadataConceptRole::RawExposureAdjustment) {
                set_transfer_hint(candidate,
                                  MetadataConceptTransferHint::RenderedUnsafe,
                                  true, false);
                return;
            }
            set_transfer_hint(candidate, MetadataConceptTransferHint::Safe,
                              true, true);
            return;
        case MetadataConceptKind::Orientation:
            set_transfer_hint(
                candidate, MetadataConceptTransferHint::RequiresTargetImageSpec,
                true, false);
            return;
        case MetadataConceptKind::Geometry:
            set_transfer_hint(
                candidate, MetadataConceptTransferHint::RequiresTargetImageSpec,
                true, false);
            return;
        case MetadataConceptKind::LensCorrection:
            set_transfer_hint(candidate,
                              MetadataConceptTransferHint::RenderedUnsafe, true,
                              false);
            return;
        case MetadataConceptKind::RawProcessing:
            switch (candidate->role) {
            case MetadataConceptRole::BlackLevel:
            case MetadataConceptRole::WhiteLevel:
            case MetadataConceptRole::Linearization:
                set_transfer_hint(candidate,
                                  MetadataConceptTransferHint::RenderedUnsafe,
                                  true, false);
                return;
            case MetadataConceptRole::CfaLayout:
            case MetadataConceptRole::SensorGeometry:
            case MetadataConceptRole::RawStorage:
            case MetadataConceptRole::SourceProcessing:
            case MetadataConceptRole::ComputationalProcessing:
            case MetadataConceptRole::ThermalProcessing:
            case MetadataConceptRole::StitchProcessing:
            case MetadataConceptRole::ExposureTime:
            case MetadataConceptRole::Aperture:
            case MetadataConceptRole::IsoSensitivity:
            case MetadataConceptRole::ExposureBias:
            case MetadataConceptRole::ExposureProgram:
            case MetadataConceptRole::Gain:
            case MetadataConceptRole::RawExposureAdjustment:
            case MetadataConceptRole::Primary:
                set_transfer_hint(candidate,
                                  MetadataConceptTransferHint::SourceBound,
                                  true, false);
                return;
            case MetadataConceptRole::Orientation:
            case MetadataConceptRole::Created:
            case MetadataConceptRole::Digitized:
            case MetadataConceptRole::Modified:
            case MetadataConceptRole::MetadataDate:
            case MetadataConceptRole::DateCreated:
            case MetadataConceptRole::ColorSpace:
            case MetadataConceptRole::IccProfile:
            case MetadataConceptRole::ColorMatrix:
            case MetadataConceptRole::WhiteBalance:
            case MetadataConceptRole::SourceColorTransform:
            case MetadataConceptRole::Latitude:
            case MetadataConceptRole::Longitude:
            case MetadataConceptRole::Altitude:
            case MetadataConceptRole::Timestamp:
            case MetadataConceptRole::Crop:
            case MetadataConceptRole::ActiveArea:
            case MetadataConceptRole::Border:
            case MetadataConceptRole::LensCorrection: break;
            }
            break;
        case MetadataConceptKind::ColorProfile:
            switch (candidate->role) {
            case MetadataConceptRole::ColorMatrix:
            case MetadataConceptRole::WhiteBalance:
            case MetadataConceptRole::SourceColorTransform:
                set_transfer_hint(candidate,
                                  MetadataConceptTransferHint::RenderedUnsafe,
                                  true, false);
                return;
            case MetadataConceptRole::ColorSpace:
            case MetadataConceptRole::IccProfile:
            case MetadataConceptRole::Primary:
                set_transfer_hint(
                    candidate,
                    MetadataConceptTransferHint::RequiresTargetImageSpec, true,
                    false);
                return;
            case MetadataConceptRole::Orientation:
            case MetadataConceptRole::Created:
            case MetadataConceptRole::Digitized:
            case MetadataConceptRole::Modified:
            case MetadataConceptRole::MetadataDate:
            case MetadataConceptRole::DateCreated:
            case MetadataConceptRole::Latitude:
            case MetadataConceptRole::Longitude:
            case MetadataConceptRole::Altitude:
            case MetadataConceptRole::Timestamp:
            case MetadataConceptRole::Crop:
            case MetadataConceptRole::ActiveArea:
            case MetadataConceptRole::Border:
            case MetadataConceptRole::SensorGeometry:
            case MetadataConceptRole::LensCorrection:
            case MetadataConceptRole::BlackLevel:
            case MetadataConceptRole::WhiteLevel:
            case MetadataConceptRole::Linearization:
            case MetadataConceptRole::CfaLayout:
            case MetadataConceptRole::RawStorage:
            case MetadataConceptRole::SourceProcessing:
            case MetadataConceptRole::ComputationalProcessing:
            case MetadataConceptRole::ThermalProcessing:
            case MetadataConceptRole::StitchProcessing:
            case MetadataConceptRole::ExposureTime:
            case MetadataConceptRole::Aperture:
            case MetadataConceptRole::IsoSensitivity:
            case MetadataConceptRole::ExposureBias:
            case MetadataConceptRole::ExposureProgram:
            case MetadataConceptRole::Gain:
            case MetadataConceptRole::RawExposureAdjustment: break;
            }
            break;
        }
        set_transfer_hint(candidate, MetadataConceptTransferHint::Unknown,
                          false, false);
    }

    static MetadataConceptCandidate*
    find_candidate(MetadataConceptResolution* resolution, EntryId entry_id,
                   MetadataConceptRole role,
                   MetadataQueryValueShape shape) noexcept
    {
        if (!resolution || entry_id == kInvalidEntryId) {
            return nullptr;
        }
        for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
            MetadataConceptCandidate& candidate = resolution->candidates[i];
            if (candidate.entry_id == entry_id && candidate.role == role
                && candidate.shape == shape) {
                return &candidate;
            }
        }
        return nullptr;
    }

    static void merge_candidate(MetadataConceptCandidate* dst,
                                const MetadataConceptCandidate& src)
    {
        if (!dst) {
            return;
        }
        if (src.priority > dst->priority) {
            dst->priority = src.priority;
        }
        if (dst->semantic == MetadataQuerySemanticKind::Unknown) {
            dst->semantic = src.semantic;
        }
        if (dst->shape == MetadataQueryValueShape::Unknown) {
            dst->shape = src.shape;
        }
        for (size_t i = 0U; i < src.source_entries.size(); ++i) {
            add_unique_entry(&dst->source_entries, src.source_entries[i]);
        }
        if (!dst->has_numeric && src.has_numeric) {
            dst->has_numeric   = true;
            dst->numeric_count = src.numeric_count;
            for (uint8_t i = 0U; i < src.numeric_count; ++i) {
                dst->numeric[i] = src.numeric[i];
            }
        }
        if (!dst->has_values && src.has_values) {
            dst->has_values = true;
            dst->values     = src.values;
        }
        if (!dst->has_origin && src.has_origin) {
            dst->has_origin = true;
            dst->origin[0]  = src.origin[0];
            dst->origin[1]  = src.origin[1];
        }
        if (!dst->has_size && src.has_size) {
            dst->has_size = true;
            dst->size[0]  = src.size[0];
            dst->size[1]  = src.size[1];
        }
        if (!dst->has_rect && src.has_rect) {
            dst->has_rect = true;
            for (uint8_t i = 0U; i < 4U; ++i) {
                dst->rect[i] = src.rect[i];
            }
        }
        if (!dst->has_margins && src.has_margins) {
            dst->has_margins = true;
            for (uint8_t i = 0U; i < 4U; ++i) {
                dst->margins[i] = src.margins[i];
            }
        }
        if (dst->text.empty() && !src.text.empty()) {
            dst->text = src.text;
        }
        if ((dst->value_key.empty()
             || src.value_key.size() > dst->value_key.size())
            && !src.value_key.empty()) {
            dst->value_key = src.value_key;
        }
        if (!dst->has_date_time && src.has_date_time) {
            dst->has_date_time            = true;
            dst->date_time_has_time       = src.date_time_has_time;
            dst->date_time_has_utc_offset = src.date_time_has_utc_offset;
            dst->date_time_precision      = src.date_time_precision;
            dst->date_time_zone           = src.date_time_zone;
            dst->date_time_year           = src.date_time_year;
            dst->date_time_month          = src.date_time_month;
            dst->date_time_day            = src.date_time_day;
            dst->date_time_hour           = src.date_time_hour;
            dst->date_time_minute         = src.date_time_minute;
            dst->date_time_second         = src.date_time_second;
            dst->date_time_utc_offset_min = src.date_time_utc_offset_min;
        } else if (dst->has_date_time && src.has_date_time
                   && dst->date_time_year == src.date_time_year
                   && dst->date_time_month == src.date_time_month
                   && dst->date_time_day == src.date_time_day) {
            if (!dst->date_time_has_time && src.date_time_has_time) {
                dst->date_time_has_time  = true;
                dst->date_time_precision = src.date_time_precision;
                dst->date_time_hour      = src.date_time_hour;
                dst->date_time_minute    = src.date_time_minute;
                dst->date_time_second    = src.date_time_second;
            }
            if (!dst->date_time_has_utc_offset
                && src.date_time_has_utc_offset) {
                dst->date_time_has_utc_offset = true;
                dst->date_time_zone           = src.date_time_zone;
                dst->date_time_utc_offset_min = src.date_time_utc_offset_min;
            }
        }
        if (!dst->has_gps_altitude_reference
            && src.has_gps_altitude_reference) {
            dst->has_gps_altitude_reference = src.has_gps_altitude_reference;
            dst->gps_altitude_below_sea_level = src.gps_altitude_below_sea_level;
            dst->gps_altitude_reference_code = src.gps_altitude_reference_code;
        }
    }

    static void append_candidate(MetadataConceptResolution* resolution,
                                 const MetadataConceptCandidate& candidate)
    {
        if (!resolution) {
            return;
        }
        MetadataConceptCandidate* existing
            = find_candidate(resolution, candidate.entry_id, candidate.role,
                             candidate.shape);
        if (existing) {
            merge_candidate(existing, candidate);
            return;
        }
        resolution->candidates.push_back(candidate);
    }

    static MetadataConceptCandidate
    make_entry_candidate(const MetaStore& store, EntryId entry_id,
                         MetadataConceptKind kind, MetadataConceptRole role,
                         MetadataQuerySemanticKind semantic,
                         MetadataQueryValueShape shape, uint8_t priority)
    {
        MetadataConceptCandidate out;
        out.kind     = kind;
        out.role     = role;
        out.semantic = semantic;
        out.shape    = shape;
        out.entry_id = entry_id;
        out.priority = priority;
        if (entry_id != kInvalidEntryId) {
            out.family = source_family_for_entry(store.entry(entry_id));
            out.source_entries.push_back(entry_id);
        }
        return out;
    }

    static bool exif_entry_ifd_and_tag(const MetaStore& store,
                                       const Entry& entry, std::string_view ifd,
                                       uint16_t tag) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }
        if (entry.key.data.exif_tag.tag != tag) {
            return false;
        }
        const std::string_view entry_ifd
            = arena_string(store.arena(), entry.key.data.exif_tag.ifd);
        return ascii_equal_ci(entry_ifd, ifd);
    }

    static bool exif_entry_tag(const Entry& entry, uint16_t tag) noexcept
    {
        return entry.key.kind == MetaKeyKind::ExifTag
               && entry.key.data.exif_tag.tag == tag;
    }

    static bool find_exif_text(const MetaStore& store, std::string_view ifd,
                               uint16_t tag, std::string* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (!exif_entry_ifd_and_tag(store, entry, ifd, tag)) {
                continue;
            }
            return value_to_text(store.arena(), entry.value, out);
        }
        if (out) {
            out->clear();
        }
        return false;
    }

    static bool find_exif_text_entry(const MetaStore& store,
                                     std::string_view ifd, uint16_t tag,
                                     EntryId* out_id, std::string* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (!exif_entry_ifd_and_tag(store, entry, ifd, tag)) {
                continue;
            }
            if (!value_to_text(store.arena(), entry.value, out)) {
                continue;
            }
            if (out_id) {
                *out_id = id;
            }
            return true;
        }
        if (out_id) {
            *out_id = kInvalidEntryId;
        }
        if (out) {
            out->clear();
        }
        return false;
    }

    static bool find_exif_time_entry(const MetaStore& store,
                                     std::string_view ifd, uint16_t tag,
                                     EntryId* out_id, uint8_t* hour,
                                     uint8_t* minute, uint8_t* second)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (!exif_entry_ifd_and_tag(store, entry, ifd, tag)) {
                continue;
            }
            bool has_offset = false;
            int16_t offset  = 0;
            if (!fill_time_from_value(store.arena(), entry.value, hour, minute,
                                      second, &has_offset, &offset)) {
                continue;
            }
            if (out_id) {
                *out_id = id;
            }
            return true;
        }
        if (out_id) {
            *out_id = kInvalidEntryId;
        }
        return false;
    }

    static bool find_exif_numeric_entry(const MetaStore& store,
                                        std::string_view ifd, uint16_t tag,
                                        EntryId* out_id, double* out) noexcept
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (!exif_entry_ifd_and_tag(store, entry, ifd, tag)) {
                continue;
            }
            double values[1] {};
            if (value_to_numeric_array(store.arena(), entry.value, values, 1U)
                != 1U) {
                continue;
            }
            if (out_id) {
                *out_id = id;
            }
            if (out) {
                *out = values[0];
            }
            return true;
        }
        if (out_id) {
            *out_id = kInvalidEntryId;
        }
        return false;
    }

    static void fill_numeric_candidate(MetadataConceptCandidate* candidate,
                                       const double* values, uint8_t count)
    {
        if (!candidate || !values || count == 0U) {
            return;
        }
        const uint8_t copy_count = std::min<uint8_t>(count, 4U);
        candidate->has_numeric   = true;
        candidate->numeric_count = copy_count;
        for (uint8_t i = 0U; i < copy_count; ++i) {
            candidate->numeric[i] = values[i];
        }
    }

    static void fill_values_candidate(MetadataConceptCandidate* candidate,
                                      const std::vector<double>& values)
    {
        if (!candidate || values.empty()) {
            return;
        }
        candidate->has_values = true;
        candidate->values     = values;
        if (!candidate->has_numeric) {
            const uint8_t count = static_cast<uint8_t>(
                std::min<size_t>(values.size(), 4U));
            fill_numeric_candidate(candidate, values.data(), count);
        }
    }

    static std::string values_key(const std::vector<double>& values)
    {
        std::string out;
        if (values.empty()) {
            return out;
        }
        out.reserve(values.size() * 16U);
        char buf[64];
        for (size_t i = 0U; i < values.size(); ++i) {
            if (i != 0U) {
                out.push_back(',');
            }
            std::snprintf(buf, sizeof(buf), "%.12g", values[i]);
            out.append(buf);
        }
        return out;
    }

    static void fill_pair_candidate(bool present, const double* src,
                                    bool* dst_present, double* dst) noexcept
    {
        if (!dst_present || !dst) {
            return;
        }
        *dst_present = present;
        if (!present || !src) {
            return;
        }
        dst[0] = src[0];
        dst[1] = src[1];
    }

    static void fill_quad_candidate(bool present, const double* src,
                                    bool* dst_present, double* dst) noexcept
    {
        if (!dst_present || !dst) {
            return;
        }
        *dst_present = present;
        if (!present || !src) {
            return;
        }
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
    }

    static std::string geometry_key(const MetadataConceptCandidate& candidate)
    {
        char buf[160];
        if (candidate.has_rect) {
            std::snprintf(buf, sizeof(buf), "rect:%.12g,%.12g,%.12g,%.12g",
                          candidate.rect[0], candidate.rect[1],
                          candidate.rect[2], candidate.rect[3]);
            return std::string(buf);
        }
        if (candidate.has_margins) {
            std::snprintf(buf, sizeof(buf), "margins:%.12g,%.12g,%.12g,%.12g",
                          candidate.margins[0], candidate.margins[1],
                          candidate.margins[2], candidate.margins[3]);
            return std::string(buf);
        }
        if (candidate.has_size) {
            std::snprintf(buf, sizeof(buf), "size:%.12g,%.12g",
                          candidate.size[0], candidate.size[1]);
            return std::string(buf);
        }
        if (candidate.has_origin) {
            std::snprintf(buf, sizeof(buf), "origin:%.12g,%.12g",
                          candidate.origin[0], candidate.origin[1]);
            return std::string(buf);
        }
        if (candidate.has_numeric && candidate.numeric_count != 0U) {
            return numeric_key(candidate.numeric[0]);
        }
        return std::string();
    }

    static bool parse_xmp_gps_coordinate(std::string_view text, double* out)
    {
        if (!out || text.empty()) {
            return false;
        }
        std::string tmp(text);
        const char* cursor = tmp.c_str();
        char* end          = nullptr;
        double degrees     = std::strtod(cursor, &end);
        if (end == cursor) {
            return false;
        }
        double value = degrees;
        if (*end == ',') {
            cursor         = end + 1;
            double minutes = std::strtod(cursor, &end);
            if (end != cursor) {
                value = std::fabs(degrees) + (minutes / 60.0);
                if (*end == ',') {
                    cursor         = end + 1;
                    double seconds = std::strtod(cursor, &end);
                    if (end != cursor) {
                        value += seconds / 3600.0;
                    }
                }
            }
        }
        bool explicit_negative = degrees < 0.0;
        for (size_t i = 0U; i < tmp.size(); ++i) {
            const char c = ascii_lower(tmp[i]);
            if (c == 's' || c == 'w') {
                explicit_negative = true;
            }
            if (c == 'n' || c == 'e') {
                explicit_negative = false;
            }
        }
        if (explicit_negative) {
            value = -std::fabs(value);
        }
        *out = value;
        return true;
    }

    static bool gps_coordinate_from_value(const MetaStore& store,
                                          const MetaValue& value,
                                          std::string_view ref,
                                          double* out) noexcept
    {
        if (!out) {
            return false;
        }
        double values[4] {};
        const uint8_t count = value_to_numeric_array(store.arena(), value,
                                                     values, 4U);
        if (count == 0U) {
            return false;
        }
        double result = values[0];
        if (count >= 2U) {
            result += values[1] / 60.0;
        }
        if (count >= 3U) {
            result += values[2] / 3600.0;
        }
        if (!ref.empty()) {
            const char c = ascii_lower(ref[0]);
            if (c == 's' || c == 'w') {
                result = -std::fabs(result);
            }
        }
        *out = result;
        return true;
    }

    static void append_interpretation_candidates(
        const MetaStore& store, MetadataQueryKind query_kind,
        MetadataConceptKind concept_kind, MetadataConceptRole role,
        uint8_t priority, MetadataConceptResolution* out)
    {
        MetadataInterpretationResult result
            = interpret_metadata_query(store, query_kind);
        for (size_t i = 0U; i < result.records.size(); ++i) {
            const MetadataInterpretationRecord& record = result.records[i];
            for (size_t e = 0U; e < record.source_entries.size(); ++e) {
                const EntryId entry_id = record.source_entries[e];
                if (entry_id == kInvalidEntryId) {
                    continue;
                }
                MetadataConceptCandidate candidate
                    = make_entry_candidate(store, entry_id, concept_kind, role,
                                           record.semantic, record.shape,
                                           priority);
                if (record.has_values && !record.values.empty()) {
                    fill_values_candidate(&candidate, record.values);
                    candidate.value_key = numeric_key(record.values[0]);
                }
                append_candidate(out, candidate);
            }
        }
    }

    static void
    append_exif_orientation_candidate(const MetaStore& store, EntryId id,
                                      const Entry& entry,
                                      MetadataConceptResolution* out)
    {
        double values[1] {};
        if (value_to_numeric_array(store.arena(), entry.value, values, 1U)
            != 1U) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::Orientation,
                                   MetadataConceptRole::Orientation,
                                   MetadataQuerySemanticKind::Orientation,
                                   MetadataQueryValueShape::Scalar, 100U);
        fill_numeric_candidate(&candidate, values, 1U);
        candidate.value_key = numeric_key(values[0]);
        append_candidate(out, candidate);
    }

    static void append_xmp_orientation_candidate(const MetaStore& store,
                                                 EntryId id, const Entry& entry,
                                                 MetadataConceptResolution* out)
    {
        const std::string_view path
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.property_path);
        if (!xmp_leaf_matches(path, "Orientation")) {
            return;
        }

        std::string text;
        if (!value_to_text(store.arena(), entry.value, &text)) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::Orientation,
                                   MetadataConceptRole::Orientation,
                                   MetadataQuerySemanticKind::Orientation,
                                   MetadataQueryValueShape::Scalar, 80U);
        double numeric = 0.0;
        if (parse_xmp_gps_coordinate(text, &numeric)) {
            fill_numeric_candidate(&candidate, &numeric, 1U);
            candidate.value_key = numeric_key(numeric);
        } else {
            normalize_text_key(text, &candidate.value_key);
        }
        candidate.text = text;
        append_candidate(out, candidate);
    }

    static void append_orientation_candidates(const MetaStore& store,
                                              MetadataConceptResolution* out)
    {
        append_interpretation_candidates(store, MetadataQueryKind::Orientation,
                                         MetadataConceptKind::Orientation,
                                         MetadataConceptRole::Orientation, 95U,
                                         out);

        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (exif_entry_tag(entry, kExifOrientationTag)) {
                append_exif_orientation_candidate(store, id, entry, out);
                continue;
            }
            if (entry.key.kind == MetaKeyKind::XmpProperty) {
                append_xmp_orientation_candidate(store, id, entry, out);
            }
        }
    }

    static void append_date_text_candidate(const MetaStore& store, EntryId id,
                                           MetadataConceptRole role,
                                           uint8_t priority,
                                           MetadataConceptResolution* out)
    {
        const Entry& entry = store.entry(id);
        std::string text;
        if (!value_to_text(store.arena(), entry.value, &text)) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::DateTime,
                                   role, MetadataQuerySemanticKind::Unknown,
                                   MetadataQueryValueShape::Text, priority);
        candidate.text = text;
        if (!fill_datetime_from_text(text, &candidate)) {
            normalize_text_key(text, &candidate.value_key);
        }
        append_candidate(out, candidate);
    }

    static void append_exif_datetime_candidate(const MetaStore& store,
                                               EntryId id, const Entry& entry,
                                               MetadataConceptResolution* out)
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return;
        }
        MetadataConceptRole role = MetadataConceptRole::Primary;
        uint8_t priority         = 0U;
        switch (entry.key.data.exif_tag.tag) {
        case kExifDateTimeOriginalTag:
            role     = MetadataConceptRole::Created;
            priority = 100U;
            break;
        case kExifDateTimeDigitizedTag:
            role     = MetadataConceptRole::Digitized;
            priority = 90U;
            break;
        case kExifDateTimeTag:
            role     = MetadataConceptRole::Modified;
            priority = 80U;
            break;
        default: return;
        }
        append_date_text_candidate(store, id, role, priority, out);
    }

    static void append_xmp_datetime_candidate(const MetaStore& store,
                                              EntryId id, const Entry& entry,
                                              MetadataConceptResolution* out)
    {
        const std::string_view path
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.property_path);
        MetadataConceptRole role = MetadataConceptRole::Primary;
        uint8_t priority         = 0U;
        if (xmp_leaf_matches(path, "CreateDate")
            || xmp_leaf_matches(path, "DateCreated")) {
            role     = MetadataConceptRole::Created;
            priority = 95U;
        } else if (xmp_leaf_matches(path, "ModifyDate")) {
            role     = MetadataConceptRole::Modified;
            priority = 75U;
        } else if (xmp_leaf_matches(path, "MetadataDate")) {
            role     = MetadataConceptRole::MetadataDate;
            priority = 70U;
        } else if (xmp_leaf_matches(path, "DateTimeOriginal")) {
            role     = MetadataConceptRole::Created;
            priority = 90U;
        } else {
            return;
        }
        append_date_text_candidate(store, id, role, priority, out);
    }

    static void append_iptc_datetime_candidate(const MetaStore& store,
                                               EntryId id, const Entry& entry,
                                               MetadataConceptResolution* out)
    {
        if (entry.key.kind != MetaKeyKind::IptcDataset) {
            return;
        }
        if (entry.key.data.iptc_dataset.record != 2U) {
            return;
        }
        if (entry.key.data.iptc_dataset.dataset == kIptcDateCreatedDataset) {
            append_date_text_candidate(store, id,
                                       MetadataConceptRole::DateCreated, 70U,
                                       out);
        } else if (entry.key.data.iptc_dataset.dataset
                   == kIptcTimeCreatedDataset) {
            append_date_text_candidate(store, id,
                                       MetadataConceptRole::Timestamp, 65U,
                                       out);
        }
    }

    static bool find_iptc_text_entry(const MetaStore& store, uint16_t record,
                                     uint16_t dataset, EntryId* out_id,
                                     std::string* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (entry.key.kind != MetaKeyKind::IptcDataset) {
                continue;
            }
            if (entry.key.data.iptc_dataset.record != record
                || entry.key.data.iptc_dataset.dataset != dataset) {
                continue;
            }
            if (!value_to_text(store.arena(), entry.value, out)) {
                continue;
            }
            if (out_id) {
                *out_id = id;
            }
            return true;
        }
        if (out_id) {
            *out_id = kInvalidEntryId;
        }
        if (out) {
            out->clear();
        }
        return false;
    }

    static void append_iptc_datetime_composite(const MetaStore& store,
                                               MetadataConceptResolution* out)
    {
        EntryId date_id = kInvalidEntryId;
        EntryId time_id = kInvalidEntryId;
        std::string date_text;
        std::string time_text;
        if (!find_iptc_text_entry(store, 2U, kIptcDateCreatedDataset, &date_id,
                                  &date_text)) {
            return;
        }
        if (!find_iptc_text_entry(store, 2U, kIptcTimeCreatedDataset, &time_id,
                                  &time_text)) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, date_id,
                                   MetadataConceptKind::DateTime,
                                   MetadataConceptRole::DateCreated,
                                   MetadataQuerySemanticKind::Unknown,
                                   MetadataQueryValueShape::Text, 76U);
        candidate.text = date_text;
        candidate.text.push_back(' ');
        candidate.text.append(time_text);
        if (!fill_datetime_from_text(date_text, &candidate)) {
            return;
        }
        const Entry& time_entry = store.entry(time_id);
        uint8_t hour            = 0U;
        uint8_t minute          = 0U;
        uint8_t second          = 0U;
        bool has_offset         = false;
        int16_t offset          = 0;
        if (!fill_time_from_value(store.arena(), time_entry.value, &hour,
                                  &minute, &second, &has_offset, &offset)) {
            return;
        }
        (void)attach_time_to_candidate(&candidate, hour, minute, second,
                                       has_offset, offset);
        add_unique_entry(&candidate.source_entries, time_id);
        append_candidate(out, candidate);
    }

    static void append_datetime_candidates(const MetaStore& store,
                                           MetadataConceptResolution* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (entry.key.kind == MetaKeyKind::ExifTag) {
                append_exif_datetime_candidate(store, id, entry, out);
            } else if (entry.key.kind == MetaKeyKind::XmpProperty) {
                append_xmp_datetime_candidate(store, id, entry, out);
            } else if (entry.key.kind == MetaKeyKind::IptcDataset) {
                append_iptc_datetime_candidate(store, id, entry, out);
            }
        }
        append_iptc_datetime_composite(store, out);
    }

    static void
    copy_interpretation_values(const MetadataInterpretationRecord& record,
                               MetadataConceptCandidate* candidate);

    static bool is_dng_exposure_adjustment_tag(uint16_t tag) noexcept
    {
        switch (tag) {
        case kDngBaselineExposureTag:
        case kDngBaselineExposureOffsetTag:
        case kDngRawToPreviewGainTag:
        case kDngProfileGainTableMapTag:
        case kDngProfileGainTableMap2Tag: return true;
        default: break;
        }
        return false;
    }

    static MetadataConceptRole exposure_role_from_exif_tag(uint16_t tag) noexcept
    {
        switch (tag) {
        case kExifExposureTimeTag:
        case kExifShutterSpeedValueTag:
            return MetadataConceptRole::ExposureTime;
        case kExifFNumberTag:
        case kExifApertureValueTag:
        case kExifMaxApertureValueTag: return MetadataConceptRole::Aperture;
        case kExifPhotographicSensitivityTag:
        case kExifExposureIndexTag: return MetadataConceptRole::IsoSensitivity;
        case kExifExposureBiasValueTag:
            return MetadataConceptRole::ExposureBias;
        case kExifExposureProgramTag:
            return MetadataConceptRole::ExposureProgram;
        case kExifGainControlTag: return MetadataConceptRole::Gain;
        default: break;
        }
        if (is_dng_exposure_adjustment_tag(tag)) {
            return MetadataConceptRole::RawExposureAdjustment;
        }
        return MetadataConceptRole::Primary;
    }

    static bool name_in_list_ci(std::string_view name,
                                const std::string_view* names,
                                size_t count) noexcept
    {
        for (size_t i = 0U; i < count; ++i) {
            if (ascii_equal_ci(name, names[i])) {
                return true;
            }
        }
        return false;
    }

    static MetadataConceptRole
    exposure_role_from_exif_name(std::string_view name) noexcept
    {
        static constexpr std::string_view kExposureTimeNames[] = {
            "ExposureTime",        "TargetExposureTime", "ShutterSpeed",
            "ShutterSpeedSetting", "ShutterSpeedValue",
        };
        static constexpr std::string_view kApertureNames[] = {
            "Aperture",        "ApertureSetting",      "ApertureValue",
            "DisplayAperture", "EffectiveMaxAperture", "FNumber",
            "MaxAperture",     "MaxApertureValue",     "MinAperture",
            "TargetAperture",
        };
        static constexpr std::string_view kIsoNames[] = {
            "BaseISO",  "ISO",      "ISOSetting",
            "ISOSpeed", "ISOValue", "PhotographicSensitivity",
        };
        static constexpr std::string_view kExposureBiasNames[] = {
            "BaseExposureCompensation",    "CMExposureCompensation",
            "EasyExposureCompensation",    "ExposureBias",
            "ExposureBiasValue",           "ExposureCompensation",
            "ExposureCompensation2",       "ExposureCompensationSet",
            "ExposureCompensationSetting", "NetExposureCompensation",
            "RawDevExposureBiasValue",
        };
        static constexpr std::string_view kExposureProgramNames[] = {
            "CanonExposureMode",
            "ExposureMode",
            "ExposureProgram",
        };
        static constexpr std::string_view kGainNames[] = {
            "GainControl",
        };

        if (name_in_list_ci(name, kExposureTimeNames,
                            std::size(kExposureTimeNames))) {
            return MetadataConceptRole::ExposureTime;
        }
        if (name_in_list_ci(name, kApertureNames, std::size(kApertureNames))) {
            return MetadataConceptRole::Aperture;
        }
        if (name_in_list_ci(name, kIsoNames, std::size(kIsoNames))) {
            return MetadataConceptRole::IsoSensitivity;
        }
        if (name_in_list_ci(name, kExposureBiasNames,
                            std::size(kExposureBiasNames))) {
            return MetadataConceptRole::ExposureBias;
        }
        if (name_in_list_ci(name, kExposureProgramNames,
                            std::size(kExposureProgramNames))) {
            return MetadataConceptRole::ExposureProgram;
        }
        if (name_in_list_ci(name, kGainNames, std::size(kGainNames))) {
            return MetadataConceptRole::Gain;
        }
        return MetadataConceptRole::Primary;
    }

    static bool is_raw_xmp_namespace(std::string_view ns) noexcept
    {
        return ascii_contains_ci(ns, "camera-raw-settings")
               || ascii_contains_ci(ns, "/crs/")
               || ascii_contains_ci(ns, "photoshop/camera/raw")
               || ascii_contains_ci(ns, "crs/1.0");
    }

    static MetadataConceptRole
    exposure_role_from_xmp_path(std::string_view ns,
                                std::string_view path) noexcept
    {
        if (ascii_contains_ci(path, "ExposureTime")
            || ascii_contains_ci(path, "ShutterSpeed")) {
            return MetadataConceptRole::ExposureTime;
        }
        if (ascii_contains_ci(path, "FNumber")
            || ascii_contains_ci(path, "Aperture")) {
            return MetadataConceptRole::Aperture;
        }
        if (ascii_contains_ci(path, "PhotographicSensitivity")
            || ascii_contains_ci(path, "ISOSpeed")
            || xmp_leaf_matches(path, "ISO")) {
            return MetadataConceptRole::IsoSensitivity;
        }
        if (ascii_contains_ci(path, "ExposureBias")
            || ascii_contains_ci(path, "ExposureCompensation")) {
            return MetadataConceptRole::ExposureBias;
        }
        if (ascii_contains_ci(path, "ExposureProgram")) {
            return MetadataConceptRole::ExposureProgram;
        }
        if (is_raw_xmp_namespace(ns)
            && (ascii_contains_ci(path, "Exposure")
                || ascii_contains_ci(path, "Gain"))) {
            return MetadataConceptRole::RawExposureAdjustment;
        }
        if (ascii_contains_ci(path, "Gain")) {
            return MetadataConceptRole::Gain;
        }
        return MetadataConceptRole::Primary;
    }

    static MetadataConceptRole
    exposure_role_from_record(const MetaStore& store,
                              const MetadataInterpretationRecord& record)
    {
        if (!record.source_entries.empty()) {
            const EntryId entry_id = record.source_entries[0];
            if (entry_id != kInvalidEntryId) {
                const Entry& entry = store.entry(entry_id);
                if (entry.key.kind == MetaKeyKind::ExifTag) {
                    const MetadataConceptRole tag_role
                        = exposure_role_from_exif_tag(
                            entry.key.data.exif_tag.tag);
                    if (tag_role != MetadataConceptRole::Primary) {
                        return tag_role;
                    }
                    return exposure_role_from_exif_name(
                        exif_entry_name(store, entry,
                                        ExifTagNamePolicy::ExifToolCompat));
                }
                if (entry.key.kind == MetaKeyKind::XmpProperty) {
                    const std::string_view ns
                        = arena_string(store.arena(),
                                       entry.key.data.xmp_property.schema_ns);
                    const std::string_view path = arena_string(
                        store.arena(),
                        entry.key.data.xmp_property.property_path);
                    return exposure_role_from_xmp_path(ns, path);
                }
            }
        }

        switch (record.semantic) {
        case MetadataQuerySemanticKind::Gain: return MetadataConceptRole::Gain;
        case MetadataQuerySemanticKind::ExposureGain:
            return MetadataConceptRole::RawExposureAdjustment;
        case MetadataQuerySemanticKind::Exposure:
            return MetadataConceptRole::Primary;
        case MetadataQuerySemanticKind::Unknown:
        case MetadataQuerySemanticKind::Crop:
        case MetadataQuerySemanticKind::Border:
        case MetadataQuerySemanticKind::ActiveArea:
        case MetadataQuerySemanticKind::Color:
        case MetadataQuerySemanticKind::ColorProfile:
        case MetadataQuerySemanticKind::WhiteBalance:
        case MetadataQuerySemanticKind::ColorMatrix:
        case MetadataQuerySemanticKind::SourceColorTransform:
        case MetadataQuerySemanticKind::LensCorrection:
        case MetadataQuerySemanticKind::Orientation:
        case MetadataQuerySemanticKind::BlackLevel:
        case MetadataQuerySemanticKind::WhiteLevel:
        case MetadataQuerySemanticKind::Linearization:
        case MetadataQuerySemanticKind::CfaLayout:
        case MetadataQuerySemanticKind::SensorGeometry:
        case MetadataQuerySemanticKind::RawStorage:
        case MetadataQuerySemanticKind::SourceProcessing:
        case MetadataQuerySemanticKind::ComputationalProcessing:
        case MetadataQuerySemanticKind::ThermalProcessing:
        case MetadataQuerySemanticKind::StitchProcessing:
        case MetadataQuerySemanticKind::Title:
        case MetadataQuerySemanticKind::Description:
        case MetadataQuerySemanticKind::Creator:
        case MetadataQuerySemanticKind::Keywords: break;
        }
        return MetadataConceptRole::Primary;
    }

    static bool double_to_u64_enum(double value, uint64_t* out) noexcept
    {
        if (!out || value < 0.0 || value > 4294967295.0
            || std::floor(value) != value) {
            return false;
        }
        *out = static_cast<uint64_t>(value);
        return true;
    }

    static const char* exposure_exif_value_label(
        const MetaStore& store, const Entry& entry,
        const MetadataConceptCandidate& candidate) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return "";
        }

        double value = 0.0;
        if (candidate.has_values && candidate.values.size() == 1U) {
            value = candidate.values[0];
        } else {
            double values[1] {};
            if (value_to_numeric_array(store.arena(), entry.value, values, 1U)
                != 1U) {
                return "";
            }
            value = values[0];
        }

        uint64_t enum_value = 0U;
        if (!double_to_u64_enum(value, &enum_value)) {
            return "";
        }
        const uint16_t tag         = entry.key.data.exif_tag.tag;
        const std::string_view ifd = arena_string(store.arena(),
                                                  entry.key.data.exif_tag.ifd);
        return exif_tag_numeric_value_name(ifd, tag, enum_value);
    }

    static void apply_exposure_display_text(const MetaStore& store,
                                            EntryId entry_id,
                                            MetadataConceptCandidate* candidate)
    {
        if (!candidate || entry_id == kInvalidEntryId) {
            return;
        }
        const Entry& entry = store.entry(entry_id);
        const char* label = exposure_exif_value_label(store, entry, *candidate);
        if (!label || label[0] == '\0') {
            return;
        }
        candidate->text = label;
        normalize_text_key(candidate->text, &candidate->value_key);
    }

    static void append_exposure_candidates(const MetaStore& store,
                                           MetadataConceptResolution* out)
    {
        MetadataInterpretationResult result
            = interpret_metadata_query(store, MetadataQueryKind::ExposureGain);
        for (size_t i = 0U; i < result.records.size(); ++i) {
            const MetadataInterpretationRecord& record = result.records[i];
            if (record.source_entries.empty()) {
                continue;
            }
            const EntryId entry_id = record.source_entries[0];
            if (entry_id == kInvalidEntryId) {
                continue;
            }
            const MetadataConceptRole role = exposure_role_from_record(store,
                                                                       record);
            MetadataConceptCandidate candidate = make_entry_candidate(
                store, entry_id, MetadataConceptKind::Exposure, role,
                record.semantic, record.shape, record.confidence);
            candidate.source_entries.clear();
            for (size_t e = 0U; e < record.source_entries.size(); ++e) {
                add_unique_entry(&candidate.source_entries,
                                 record.source_entries[e]);
            }
            copy_interpretation_values(record, &candidate);
            apply_exposure_display_text(store, entry_id, &candidate);
            append_candidate(out, candidate);
        }
    }

    static MetadataConceptRole
    color_role_from_semantic(MetadataQuerySemanticKind semantic) noexcept
    {
        switch (semantic) {
        case MetadataQuerySemanticKind::ColorMatrix:
            return MetadataConceptRole::ColorMatrix;
        case MetadataQuerySemanticKind::WhiteBalance:
            return MetadataConceptRole::WhiteBalance;
        case MetadataQuerySemanticKind::ColorProfile:
            return MetadataConceptRole::IccProfile;
        case MetadataQuerySemanticKind::SourceColorTransform:
            return MetadataConceptRole::SourceColorTransform;
        case MetadataQuerySemanticKind::Color:
            return MetadataConceptRole::Primary;
        case MetadataQuerySemanticKind::Unknown:
        case MetadataQuerySemanticKind::Crop:
        case MetadataQuerySemanticKind::Border:
        case MetadataQuerySemanticKind::ActiveArea:
        case MetadataQuerySemanticKind::Exposure:
        case MetadataQuerySemanticKind::Gain:
        case MetadataQuerySemanticKind::LensCorrection:
        case MetadataQuerySemanticKind::Orientation:
        case MetadataQuerySemanticKind::ExposureGain:
        case MetadataQuerySemanticKind::BlackLevel:
        case MetadataQuerySemanticKind::WhiteLevel:
        case MetadataQuerySemanticKind::Linearization:
        case MetadataQuerySemanticKind::CfaLayout:
        case MetadataQuerySemanticKind::SensorGeometry:
        case MetadataQuerySemanticKind::RawStorage:
        case MetadataQuerySemanticKind::SourceProcessing:
        case MetadataQuerySemanticKind::ComputationalProcessing:
        case MetadataQuerySemanticKind::ThermalProcessing:
        case MetadataQuerySemanticKind::StitchProcessing:
        case MetadataQuerySemanticKind::Title:
        case MetadataQuerySemanticKind::Description:
        case MetadataQuerySemanticKind::Creator:
        case MetadataQuerySemanticKind::Keywords: break;
        }
        return MetadataConceptRole::Primary;
    }

    static MetadataConceptRole
    color_role_from_record(const MetaStore& store,
                           const MetadataInterpretationRecord& record)
    {
        if (!record.source_entries.empty()) {
            const EntryId entry_id = record.source_entries[0];
            if (entry_id != kInvalidEntryId) {
                const Entry& entry = store.entry(entry_id);
                if (entry.key.kind == MetaKeyKind::ExifTag
                    && entry.key.data.exif_tag.tag == kExifColorSpaceTag) {
                    return MetadataConceptRole::ColorSpace;
                }
                if (entry.key.kind == MetaKeyKind::IccHeaderField) {
                    if (entry.key.data.icc_header_field.offset
                        == kIccHeaderRgbColorSpaceOffset) {
                        return MetadataConceptRole::ColorSpace;
                    }
                    return MetadataConceptRole::IccProfile;
                }
                if (entry.key.kind == MetaKeyKind::IccTag
                    || entry.key.kind == MetaKeyKind::PngText) {
                    return MetadataConceptRole::IccProfile;
                }
                if (entry.key.kind == MetaKeyKind::XmpProperty) {
                    const std::string_view path = arena_string(
                        store.arena(),
                        entry.key.data.xmp_property.property_path);
                    if (xmp_leaf_matches(path, "ICCProfile")
                        || xmp_leaf_matches(path, "ICCProfileName")
                        || ascii_contains_ci(path, "iccprofile")) {
                        return MetadataConceptRole::IccProfile;
                    }
                    if (xmp_leaf_matches(path, "ColorSpace")
                        || ascii_contains_ci(path, "colorspace")) {
                        return MetadataConceptRole::ColorSpace;
                    }
                }
            }
        }
        return color_role_from_semantic(record.semantic);
    }

    static MetadataConceptRole
    lens_role_from_semantic(MetadataQuerySemanticKind semantic) noexcept
    {
        if (semantic == MetadataQuerySemanticKind::LensCorrection) {
            return MetadataConceptRole::LensCorrection;
        }
        return MetadataConceptRole::Primary;
    }

    static MetadataConceptRole
    raw_role_from_semantic(MetadataQuerySemanticKind semantic) noexcept
    {
        switch (semantic) {
        case MetadataQuerySemanticKind::BlackLevel:
            return MetadataConceptRole::BlackLevel;
        case MetadataQuerySemanticKind::WhiteLevel:
            return MetadataConceptRole::WhiteLevel;
        case MetadataQuerySemanticKind::Linearization:
            return MetadataConceptRole::Linearization;
        case MetadataQuerySemanticKind::CfaLayout:
            return MetadataConceptRole::CfaLayout;
        case MetadataQuerySemanticKind::SensorGeometry:
            return MetadataConceptRole::SensorGeometry;
        case MetadataQuerySemanticKind::RawStorage:
            return MetadataConceptRole::RawStorage;
        case MetadataQuerySemanticKind::SourceProcessing:
            return MetadataConceptRole::SourceProcessing;
        case MetadataQuerySemanticKind::ComputationalProcessing:
            return MetadataConceptRole::ComputationalProcessing;
        case MetadataQuerySemanticKind::ThermalProcessing:
            return MetadataConceptRole::ThermalProcessing;
        case MetadataQuerySemanticKind::StitchProcessing:
            return MetadataConceptRole::StitchProcessing;
        case MetadataQuerySemanticKind::Unknown:
        case MetadataQuerySemanticKind::Crop:
        case MetadataQuerySemanticKind::Border:
        case MetadataQuerySemanticKind::ActiveArea:
        case MetadataQuerySemanticKind::Exposure:
        case MetadataQuerySemanticKind::Gain:
        case MetadataQuerySemanticKind::Color:
        case MetadataQuerySemanticKind::ColorProfile:
        case MetadataQuerySemanticKind::WhiteBalance:
        case MetadataQuerySemanticKind::ColorMatrix:
        case MetadataQuerySemanticKind::SourceColorTransform:
        case MetadataQuerySemanticKind::LensCorrection:
        case MetadataQuerySemanticKind::Orientation:
        case MetadataQuerySemanticKind::ExposureGain:
        case MetadataQuerySemanticKind::Title:
        case MetadataQuerySemanticKind::Description:
        case MetadataQuerySemanticKind::Creator:
        case MetadataQuerySemanticKind::Keywords: break;
        }
        return MetadataConceptRole::Primary;
    }

    static void
    copy_interpretation_values(const MetadataInterpretationRecord& record,
                               MetadataConceptCandidate* candidate)
    {
        if (!candidate) {
            return;
        }
        if (!record.has_values || record.values.empty()) {
            return;
        }
        fill_values_candidate(candidate, record.values);
        candidate->value_key = values_key(record.values);
    }

    typedef MetadataConceptRole (*ConceptRoleFromSemanticFn)(
        MetadataQuerySemanticKind) noexcept;

    static void append_query_concept_candidates(
        const MetaStore& store, MetadataQueryKind query_kind,
        MetadataConceptKind concept_kind, ConceptRoleFromSemanticFn role_fn,
        uint8_t default_priority, MetadataConceptResolution* out)
    {
        MetadataInterpretationResult result
            = interpret_metadata_query(store, query_kind);
        for (size_t i = 0U; i < result.records.size(); ++i) {
            const MetadataInterpretationRecord& record = result.records[i];
            if (!role_fn || record.source_entries.empty()) {
                continue;
            }
            const MetadataConceptRole role = role_fn(record.semantic);
            const EntryId entry_id         = record.source_entries[0];
            if (entry_id == kInvalidEntryId) {
                continue;
            }
            MetadataConceptCandidate candidate = make_entry_candidate(
                store, entry_id, concept_kind, role, record.semantic,
                record.shape,
                record.confidence != 0U ? record.confidence : default_priority);
            candidate.source_entries.clear();
            for (size_t e = 0U; e < record.source_entries.size(); ++e) {
                add_unique_entry(&candidate.source_entries,
                                 record.source_entries[e]);
            }
            copy_interpretation_values(record, &candidate);
            append_candidate(out, candidate);
        }
    }

    static void
    append_color_query_concept_candidates(const MetaStore& store,
                                          MetadataQueryKind query_kind,
                                          MetadataConceptResolution* out)
    {
        MetadataInterpretationResult result
            = interpret_metadata_query(store, query_kind);
        for (size_t i = 0U; i < result.records.size(); ++i) {
            const MetadataInterpretationRecord& record = result.records[i];
            if (record.source_entries.empty()) {
                continue;
            }
            const EntryId entry_id = record.source_entries[0];
            if (entry_id == kInvalidEntryId) {
                continue;
            }
            const MetadataConceptRole role     = color_role_from_record(store,
                                                                        record);
            MetadataConceptCandidate candidate = make_entry_candidate(
                store, entry_id, MetadataConceptKind::ColorProfile, role,
                record.semantic, record.shape,
                record.confidence != 0U ? record.confidence : 60U);
            candidate.source_entries.clear();
            for (size_t e = 0U; e < record.source_entries.size(); ++e) {
                add_unique_entry(&candidate.source_entries,
                                 record.source_entries[e]);
            }
            copy_interpretation_values(record, &candidate);
            append_candidate(out, candidate);
        }
    }

    static void
    append_color_interpretation_candidates(const MetaStore& store,
                                           MetadataConceptResolution* out)
    {
        append_color_query_concept_candidates(store, MetadataQueryKind::Color,
                                              out);
        append_color_query_concept_candidates(store,
                                              MetadataQueryKind::WhiteBalance,
                                              out);
    }

    static void append_lens_correction_candidates(const MetaStore& store,
                                                  MetadataConceptResolution* out)
    {
        append_query_concept_candidates(store,
                                        MetadataQueryKind::LensCorrection,
                                        MetadataConceptKind::LensCorrection,
                                        lens_role_from_semantic, 70U, out);
    }

    static void append_raw_processing_candidates(const MetaStore& store,
                                                 MetadataConceptResolution* out)
    {
        append_query_concept_candidates(store, MetadataQueryKind::RawProcessing,
                                        MetadataConceptKind::RawProcessing,
                                        raw_role_from_semantic, 70U, out);
    }

    static void append_exif_colorspace_candidate(const MetaStore& store,
                                                 EntryId id, const Entry& entry,
                                                 MetadataConceptResolution* out)
    {
        double values[1] {};
        if (value_to_numeric_array(store.arena(), entry.value, values, 1U)
            != 1U) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::ColorProfile,
                                   MetadataConceptRole::ColorSpace,
                                   MetadataQuerySemanticKind::ColorProfile,
                                   MetadataQueryValueShape::Scalar, 90U);
        fill_numeric_candidate(&candidate, values, 1U);
        candidate.value_key = numeric_key(values[0]);
        append_candidate(out, candidate);
    }

    static void append_icc_candidate(const MetaStore& store, EntryId id,
                                     const Entry& entry,
                                     MetadataConceptResolution* out)
    {
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::ColorProfile,
                                   MetadataConceptRole::IccProfile,
                                   MetadataQuerySemanticKind::ColorProfile,
                                   MetadataQueryValueShape::Blob, 100U);
        if (entry.key.kind == MetaKeyKind::IccHeaderField
            && entry.key.data.icc_header_field.offset
                   == kIccHeaderRgbColorSpaceOffset) {
            candidate.role  = MetadataConceptRole::ColorSpace;
            candidate.shape = MetadataQueryValueShape::Scalar;
            double values[1] {};
            if (value_to_numeric_array(store.arena(), entry.value, values, 1U)
                == 1U) {
                fill_numeric_candidate(&candidate, values, 1U);
            }
        }
        append_candidate(out, candidate);
    }

    static void append_xmp_color_candidate(const MetaStore& store, EntryId id,
                                           const Entry& entry,
                                           MetadataConceptResolution* out)
    {
        const std::string_view path
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.property_path);
        MetadataConceptRole role = MetadataConceptRole::Primary;
        uint8_t priority         = 0U;
        if (xmp_leaf_matches(path, "ICCProfile")
            || xmp_leaf_matches(path, "ICCProfileName")
            || ascii_contains_ci(path, "iccprofile")) {
            role     = MetadataConceptRole::IccProfile;
            priority = 80U;
        } else if (xmp_leaf_matches(path, "ColorSpace")
                   || ascii_contains_ci(path, "colorspace")) {
            role     = MetadataConceptRole::ColorSpace;
            priority = 75U;
        } else {
            return;
        }

        std::string text;
        (void)value_to_text(store.arena(), entry.value, &text);
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::ColorProfile,
                                   role,
                                   MetadataQuerySemanticKind::ColorProfile,
                                   MetadataQueryValueShape::Text, priority);
        candidate.text = text;
        normalize_text_key(text, &candidate.value_key);
        append_candidate(out, candidate);
    }

    static void append_png_color_candidate(const MetaStore& store, EntryId id,
                                           const Entry& entry,
                                           MetadataConceptResolution* out)
    {
        const std::string_view keyword
            = arena_string(store.arena(), entry.key.data.png_text.keyword);
        if (!ascii_contains_ci(keyword, "icc")
            && !ascii_contains_ci(keyword, "profile")
            && !ascii_contains_ci(keyword, "colorspace")) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::ColorProfile,
                                   MetadataConceptRole::IccProfile,
                                   MetadataQuerySemanticKind::ColorProfile,
                                   MetadataQueryValueShape::Text, 65U);
        value_to_text(store.arena(), entry.value, &candidate.text);
        normalize_text_key(candidate.text, &candidate.value_key);
        append_candidate(out, candidate);
    }

    static void append_color_profile_candidates(const MetaStore& store,
                                                MetadataConceptResolution* out)
    {
        append_color_interpretation_candidates(store, out);
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (exif_entry_tag(entry, kExifColorSpaceTag)) {
                append_exif_colorspace_candidate(store, id, entry, out);
            } else if (entry.key.kind == MetaKeyKind::IccHeaderField
                       || entry.key.kind == MetaKeyKind::IccTag) {
                append_icc_candidate(store, id, entry, out);
            } else if (entry.key.kind == MetaKeyKind::XmpProperty) {
                append_xmp_color_candidate(store, id, entry, out);
            } else if (entry.key.kind == MetaKeyKind::PngText) {
                append_png_color_candidate(store, id, entry, out);
            }
        }
    }

    static MetadataConceptRole
    geometry_role_from_semantic(MetadataQuerySemanticKind semantic) noexcept
    {
        switch (semantic) {
        case MetadataQuerySemanticKind::Crop: return MetadataConceptRole::Crop;
        case MetadataQuerySemanticKind::ActiveArea:
            return MetadataConceptRole::ActiveArea;
        case MetadataQuerySemanticKind::Border:
            return MetadataConceptRole::Border;
        case MetadataQuerySemanticKind::SensorGeometry:
            return MetadataConceptRole::SensorGeometry;
        case MetadataQuerySemanticKind::Unknown:
        case MetadataQuerySemanticKind::Exposure:
        case MetadataQuerySemanticKind::Gain:
        case MetadataQuerySemanticKind::Color:
        case MetadataQuerySemanticKind::ColorProfile:
        case MetadataQuerySemanticKind::WhiteBalance:
        case MetadataQuerySemanticKind::ColorMatrix:
        case MetadataQuerySemanticKind::SourceColorTransform:
        case MetadataQuerySemanticKind::LensCorrection:
        case MetadataQuerySemanticKind::Orientation:
        case MetadataQuerySemanticKind::ExposureGain:
        case MetadataQuerySemanticKind::BlackLevel:
        case MetadataQuerySemanticKind::WhiteLevel:
        case MetadataQuerySemanticKind::Linearization:
        case MetadataQuerySemanticKind::CfaLayout:
        case MetadataQuerySemanticKind::RawStorage:
        case MetadataQuerySemanticKind::SourceProcessing:
        case MetadataQuerySemanticKind::ComputationalProcessing:
        case MetadataQuerySemanticKind::ThermalProcessing:
        case MetadataQuerySemanticKind::StitchProcessing:
        case MetadataQuerySemanticKind::Title:
        case MetadataQuerySemanticKind::Description:
        case MetadataQuerySemanticKind::Creator:
        case MetadataQuerySemanticKind::Keywords: break;
        }
        return MetadataConceptRole::Primary;
    }

    static void
    copy_interpretation_geometry(const MetadataInterpretationRecord& record,
                                 MetadataConceptCandidate* candidate)
    {
        if (!candidate) {
            return;
        }
        fill_pair_candidate(record.has_origin, record.origin,
                            &candidate->has_origin, candidate->origin);
        fill_pair_candidate(record.has_size, record.size, &candidate->has_size,
                            candidate->size);
        fill_quad_candidate(record.has_rect, record.rect, &candidate->has_rect,
                            candidate->rect);
        fill_quad_candidate(record.has_margins, record.margins,
                            &candidate->has_margins, candidate->margins);
        if (record.has_values && !record.values.empty()) {
            fill_values_candidate(candidate, record.values);
        }
        candidate->value_key = geometry_key(*candidate);
    }

    static void append_geometry_candidates(const MetaStore& store,
                                           MetadataConceptResolution* out)
    {
        if (!out) {
            return;
        }
        MetadataInterpretationResult result
            = interpret_metadata_query(store, MetadataQueryKind::Crop);
        for (size_t i = 0U; i < result.records.size(); ++i) {
            const MetadataInterpretationRecord& record = result.records[i];
            const MetadataConceptRole role = geometry_role_from_semantic(
                record.semantic);
            if (role == MetadataConceptRole::Primary) {
                continue;
            }
            if (record.source_entries.empty()) {
                continue;
            }
            const EntryId entry_id = record.source_entries[0];
            if (entry_id == kInvalidEntryId) {
                continue;
            }
            MetadataConceptCandidate candidate = make_entry_candidate(
                store, entry_id, MetadataConceptKind::Geometry, role,
                record.semantic, record.shape, record.confidence);
            candidate.source_entries.clear();
            for (size_t e = 0U; e < record.source_entries.size(); ++e) {
                add_unique_entry(&candidate.source_entries,
                                 record.source_entries[e]);
            }
            copy_interpretation_geometry(record, &candidate);
            append_candidate(out, candidate);
        }
    }

    static void append_gps_numeric_candidate(const MetaStore& store, EntryId id,
                                             MetadataConceptRole role,
                                             double value, uint8_t priority,
                                             MetadataConceptResolution* out)
    {
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::Gps, role,
                                   MetadataQuerySemanticKind::Unknown,
                                   MetadataQueryValueShape::Scalar, priority);
        fill_numeric_candidate(&candidate, &value, 1U);
        candidate.value_key = gps_numeric_key(value);
        append_candidate(out, candidate);
    }

    static void append_exif_gps_candidate(const MetaStore& store, EntryId id,
                                          const Entry& entry,
                                          MetadataConceptResolution* out)
    {
        if (!exif_entry_ifd_and_tag(store, entry, "gpsifd",
                                    entry.key.data.exif_tag.tag)) {
            return;
        }

        if (entry.key.data.exif_tag.tag == kGpsLatitudeTag) {
            std::string ref;
            (void)find_exif_text(store, "gpsifd", kGpsLatitudeRefTag, &ref);
            double value = 0.0;
            if (gps_coordinate_from_value(store, entry.value, ref, &value)) {
                append_gps_numeric_candidate(store, id,
                                             MetadataConceptRole::Latitude,
                                             value, 100U, out);
            }
        } else if (entry.key.data.exif_tag.tag == kGpsLongitudeTag) {
            std::string ref;
            (void)find_exif_text(store, "gpsifd", kGpsLongitudeRefTag, &ref);
            double value = 0.0;
            if (gps_coordinate_from_value(store, entry.value, ref, &value)) {
                append_gps_numeric_candidate(store, id,
                                             MetadataConceptRole::Longitude,
                                             value, 100U, out);
            }
        } else if (entry.key.data.exif_tag.tag == kGpsAltitudeTag) {
            double value = 0.0;
            if (value_to_numeric_array(store.arena(), entry.value, &value, 1U)
                == 1U) {
                double ref         = 0.0;
                EntryId ref_id     = kInvalidEntryId;
                const bool has_ref = find_exif_numeric_entry(store, "gpsifd",
                                                             kGpsAltitudeRefTag,
                                                             &ref_id, &ref);
                const bool below_sea_level = has_ref && ref > 0.0;
                if (below_sea_level) {
                    value = -std::fabs(value);
                }
                MetadataConceptCandidate candidate
                    = make_entry_candidate(store, id, MetadataConceptKind::Gps,
                                           MetadataConceptRole::Altitude,
                                           MetadataQuerySemanticKind::Unknown,
                                           MetadataQueryValueShape::Scalar,
                                           90U);
                fill_numeric_candidate(&candidate, &value, 1U);
                candidate.value_key = gps_numeric_key(value);
                if (has_ref) {
                    candidate.has_gps_altitude_reference   = true;
                    candidate.gps_altitude_below_sea_level = below_sea_level;
                    candidate.gps_altitude_reference_code
                        = static_cast<uint8_t>(ref > 0.0 ? 1U : 0U);
                    add_unique_entry(&candidate.source_entries, ref_id);
                }
                append_candidate(out, candidate);
            }
        } else if (entry.key.data.exif_tag.tag == kGpsTimeStampTag
                   || entry.key.data.exif_tag.tag == kGpsDateStampTag) {
            std::string text;
            if (!value_to_text(store.arena(), entry.value, &text)) {
                return;
            }
            MetadataConceptCandidate candidate
                = make_entry_candidate(store, id, MetadataConceptKind::Gps,
                                       MetadataConceptRole::Timestamp,
                                       MetadataQuerySemanticKind::Unknown,
                                       MetadataQueryValueShape::Text, 80U);
            candidate.text = text;
            if (!fill_datetime_from_text(text, &candidate)) {
                normalize_text_key(text, &candidate.value_key);
            }
            append_candidate(out, candidate);
        }
    }

    static bool find_xmp_gps_altitude_ref(const MetaStore& store,
                                          EntryId* out_id,
                                          uint8_t* out_ref_code)
    {
        if (out_id) {
            *out_id = kInvalidEntryId;
        }
        if (out_ref_code) {
            *out_ref_code = 0U;
        }
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (entry.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }
            const std::string_view path
                = arena_string(store.arena(),
                               entry.key.data.xmp_property.property_path);
            if (!ascii_contains_ci(path, "GPSAltitudeRef")) {
                continue;
            }

            double numeric = 0.0;
            if (value_to_numeric_array(store.arena(), entry.value, &numeric, 1U)
                == 1U) {
                if (out_id) {
                    *out_id = id;
                }
                if (out_ref_code) {
                    *out_ref_code = numeric > 0.0 ? 1U : 0U;
                }
                return true;
            }

            std::string text;
            if (!value_to_text(store.arena(), entry.value, &text)) {
                continue;
            }
            std::string key;
            normalize_text_key(text, &key);
            uint8_t ref_code = 0U;
            if (!key.empty() && key[0] == '1') {
                ref_code = 1U;
            } else if (ascii_contains_ci(key, "below")
                       || ascii_contains_ci(key, "sealevelbelow")) {
                ref_code = 1U;
            }
            if (out_id) {
                *out_id = id;
            }
            if (out_ref_code) {
                *out_ref_code = ref_code;
            }
            return true;
        }
        return false;
    }

    static void append_xmp_gps_candidate(const MetaStore& store, EntryId id,
                                         const Entry& entry,
                                         MetadataConceptResolution* out)
    {
        const std::string_view path
            = arena_string(store.arena(),
                           entry.key.data.xmp_property.property_path);
        MetadataConceptRole role = MetadataConceptRole::Primary;
        uint8_t priority         = 0U;
        if (ascii_contains_ci(path, "GPSLatitude")) {
            role     = MetadataConceptRole::Latitude;
            priority = 80U;
        } else if (ascii_contains_ci(path, "GPSLongitude")) {
            role     = MetadataConceptRole::Longitude;
            priority = 80U;
        } else if (ascii_contains_ci(path, "GPSAltitudeRef")) {
            return;
        } else if (ascii_contains_ci(path, "GPSAltitude")) {
            role     = MetadataConceptRole::Altitude;
            priority = 75U;
        } else if (ascii_contains_ci(path, "GPSTime")
                   || ascii_contains_ci(path, "GPSDate")) {
            role     = MetadataConceptRole::Timestamp;
            priority = 70U;
        } else {
            return;
        }

        std::string text;
        if (!value_to_text(store.arena(), entry.value, &text)) {
            return;
        }
        MetadataConceptCandidate candidate
            = make_entry_candidate(store, id, MetadataConceptKind::Gps, role,
                                   MetadataQuerySemanticKind::Unknown,
                                   MetadataQueryValueShape::Text, priority);
        candidate.text = text;
        if (role == MetadataConceptRole::Latitude
            || role == MetadataConceptRole::Longitude) {
            double numeric = 0.0;
            if (parse_xmp_gps_coordinate(text, &numeric)) {
                fill_numeric_candidate(&candidate, &numeric, 1U);
                candidate.value_key = gps_numeric_key(numeric);
            }
        } else if (role == MetadataConceptRole::Altitude) {
            double numeric = 0.0;
            if (parse_xmp_gps_coordinate(text, &numeric)) {
                EntryId ref_id     = kInvalidEntryId;
                uint8_t ref_code   = 0U;
                const bool has_ref = find_xmp_gps_altitude_ref(store, &ref_id,
                                                               &ref_code);
                if (has_ref && ref_code != 0U) {
                    numeric = -std::fabs(numeric);
                }
                fill_numeric_candidate(&candidate, &numeric, 1U);
                candidate.value_key = gps_numeric_key(numeric);
                if (has_ref) {
                    candidate.has_gps_altitude_reference   = true;
                    candidate.gps_altitude_below_sea_level = ref_code != 0U;
                    candidate.gps_altitude_reference_code  = ref_code;
                    add_unique_entry(&candidate.source_entries, ref_id);
                }
            }
        }
        if (candidate.value_key.empty()) {
            if (role == MetadataConceptRole::Timestamp) {
                (void)fill_datetime_from_text(text, &candidate);
            }
            if (candidate.value_key.empty()) {
                normalize_text_key(text, &candidate.value_key);
            }
        }
        append_candidate(out, candidate);
    }

    static void
    append_exif_gps_timestamp_composite(const MetaStore& store,
                                        MetadataConceptResolution* out)
    {
        EntryId date_id = kInvalidEntryId;
        EntryId time_id = kInvalidEntryId;
        std::string date_text;
        if (!find_exif_text_entry(store, "gpsifd", kGpsDateStampTag, &date_id,
                                  &date_text)) {
            return;
        }
        uint8_t hour   = 0U;
        uint8_t minute = 0U;
        uint8_t second = 0U;
        if (!find_exif_time_entry(store, "gpsifd", kGpsTimeStampTag, &time_id,
                                  &hour, &minute, &second)) {
            return;
        }

        MetadataConceptCandidate candidate
            = make_entry_candidate(store, date_id, MetadataConceptKind::Gps,
                                   MetadataConceptRole::Timestamp,
                                   MetadataQuerySemanticKind::Unknown,
                                   MetadataQueryValueShape::Text, 90U);
        candidate.text = date_text;
        char time_buf[16];
        std::snprintf(time_buf, sizeof(time_buf), " %02u:%02u:%02uZ",
                      static_cast<unsigned>(hour),
                      static_cast<unsigned>(minute),
                      static_cast<unsigned>(second));
        candidate.text.append(time_buf);
        if (!fill_datetime_from_text(date_text, &candidate)) {
            return;
        }
        (void)attach_time_to_candidate(&candidate, hour, minute, second, true,
                                       0);
        add_unique_entry(&candidate.source_entries, time_id);
        append_candidate(out, candidate);
    }

    static void append_gps_candidates(const MetaStore& store,
                                      MetadataConceptResolution* out)
    {
        const std::span<const Entry> entries = store.entries();
        for (EntryId id = 0U; id < entries.size(); ++id) {
            const Entry& entry = entries[id];
            if (any(entry.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (entry.key.kind == MetaKeyKind::ExifTag) {
                append_exif_gps_candidate(store, id, entry, out);
            } else if (entry.key.kind == MetaKeyKind::XmpProperty) {
                append_xmp_gps_candidate(store, id, entry, out);
            }
        }
        append_exif_gps_timestamp_composite(store, out);
    }

    static MetadataConceptRole
    conflict_group_role(MetadataConceptRole role) noexcept
    {
        if (role == MetadataConceptRole::DateCreated) {
            return MetadataConceptRole::Created;
        }
        return role;
    }

    static bool
    date_time_candidates_conflict(const MetadataConceptCandidate& a,
                                  const MetadataConceptCandidate& b) noexcept
    {
        if (!a.has_date_time || !b.has_date_time) {
            return false;
        }
        if (a.date_time_year != b.date_time_year
            || a.date_time_month != b.date_time_month
            || a.date_time_day != b.date_time_day) {
            return true;
        }
        if (a.date_time_has_time && b.date_time_has_time) {
            if (a.date_time_hour != b.date_time_hour
                || a.date_time_minute != b.date_time_minute
                || a.date_time_second != b.date_time_second) {
                return true;
            }
            if (a.date_time_has_utc_offset && b.date_time_has_utc_offset
                && a.date_time_utc_offset_min != b.date_time_utc_offset_min) {
                return true;
            }
        }
        return false;
    }

    static bool
    candidates_share_source_entries(const MetadataConceptCandidate& a,
                                    const MetadataConceptCandidate& b) noexcept
    {
        for (size_t i = 0U; i < a.source_entries.size(); ++i) {
            const EntryId entry_id = a.source_entries[i];
            if (entry_id == kInvalidEntryId) {
                continue;
            }
            for (size_t j = 0U; j < b.source_entries.size(); ++j) {
                if (b.source_entries[j] == entry_id) {
                    return true;
                }
            }
        }
        return false;
    }

    static double numeric_conflict_tolerance(MetadataConceptRole role) noexcept
    {
        switch (role) {
        case MetadataConceptRole::Latitude:
        case MetadataConceptRole::Longitude: return 0.0000001;
        case MetadataConceptRole::Altitude: return 0.001;
        case MetadataConceptRole::Primary:
        case MetadataConceptRole::Orientation:
        case MetadataConceptRole::Created:
        case MetadataConceptRole::Digitized:
        case MetadataConceptRole::Modified:
        case MetadataConceptRole::MetadataDate:
        case MetadataConceptRole::DateCreated:
        case MetadataConceptRole::ColorSpace:
        case MetadataConceptRole::IccProfile:
        case MetadataConceptRole::ColorMatrix:
        case MetadataConceptRole::WhiteBalance:
        case MetadataConceptRole::SourceColorTransform:
        case MetadataConceptRole::Timestamp:
        case MetadataConceptRole::Crop:
        case MetadataConceptRole::ActiveArea:
        case MetadataConceptRole::Border:
        case MetadataConceptRole::SensorGeometry:
        case MetadataConceptRole::LensCorrection:
        case MetadataConceptRole::BlackLevel:
        case MetadataConceptRole::WhiteLevel:
        case MetadataConceptRole::Linearization:
        case MetadataConceptRole::CfaLayout:
        case MetadataConceptRole::RawStorage:
        case MetadataConceptRole::SourceProcessing:
        case MetadataConceptRole::ComputationalProcessing:
        case MetadataConceptRole::ThermalProcessing:
        case MetadataConceptRole::StitchProcessing:
        case MetadataConceptRole::ExposureTime:
        case MetadataConceptRole::Aperture:
        case MetadataConceptRole::IsoSensitivity:
        case MetadataConceptRole::ExposureBias:
        case MetadataConceptRole::ExposureProgram:
        case MetadataConceptRole::Gain:
        case MetadataConceptRole::RawExposureAdjustment: break;
        }
        return 0.0;
    }

    static bool
    numeric_candidates_conflict(const MetadataConceptCandidate& a,
                                const MetadataConceptCandidate& b) noexcept
    {
        if (!a.has_numeric || !b.has_numeric) {
            return false;
        }
        if (a.numeric_count != b.numeric_count) {
            return true;
        }
        const double tolerance = numeric_conflict_tolerance(
            conflict_group_role(a.role));
        for (uint8_t i = 0U; i < a.numeric_count; ++i) {
            if (std::fabs(a.numeric[i] - b.numeric[i]) > tolerance) {
                return true;
            }
        }
        if (conflict_group_role(a.role) == MetadataConceptRole::Altitude
            && a.has_gps_altitude_reference && b.has_gps_altitude_reference
            && a.gps_altitude_reference_code != b.gps_altitude_reference_code) {
            return true;
        }
        return false;
    }

    static bool
    concept_values_conflict(const MetadataConceptCandidate& a,
                            const MetadataConceptCandidate& b) noexcept
    {
        if (candidates_share_source_entries(a, b)) {
            return false;
        }
        if (a.has_date_time && b.has_date_time) {
            return date_time_candidates_conflict(a, b);
        }
        if (a.has_numeric && b.has_numeric) {
            return numeric_candidates_conflict(a, b);
        }
        if (a.value_key.empty() || b.value_key.empty()) {
            return false;
        }
        return a.value_key != b.value_key;
    }

    static void mark_role_conflicts(MetadataConceptResolution* resolution,
                                    MetadataConceptRole role)
    {
        if (!resolution) {
            return;
        }
        const MetadataConceptRole group = conflict_group_role(role);
        bool conflict                   = false;
        for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
            const MetadataConceptCandidate& a = resolution->candidates[i];
            if (conflict_group_role(a.role) != group) {
                continue;
            }
            for (size_t j = i + 1U; j < resolution->candidates.size(); ++j) {
                const MetadataConceptCandidate& b = resolution->candidates[j];
                if (conflict_group_role(b.role) != group) {
                    continue;
                }
                if (concept_values_conflict(a, b)) {
                    conflict = true;
                    break;
                }
            }
            if (conflict) {
                break;
            }
        }
        if (!conflict) {
            return;
        }
        resolution->conflict = true;
        for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
            MetadataConceptCandidate& candidate = resolution->candidates[i];
            if (conflict_group_role(candidate.role) == group) {
                candidate.conflict = true;
            }
        }
    }

    static void mark_role_preferred(MetadataConceptResolution* resolution,
                                    MetadataConceptRole role)
    {
        if (!resolution) {
            return;
        }
        size_t best_index  = resolution->candidates.size();
        uint8_t best_score = 0U;
        for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate
                = resolution->candidates[i];
            if (candidate.role != role) {
                continue;
            }
            if (best_index == resolution->candidates.size()
                || candidate.priority > best_score) {
                best_index = i;
                best_score = candidate.priority;
            }
        }
        if (best_index < resolution->candidates.size()) {
            resolution->candidates[best_index].preferred = true;
        }
    }

    static void finalize_resolution(MetadataConceptResolution* resolution)
    {
        if (!resolution) {
            return;
        }
        resolution->found           = !resolution->candidates.empty();
        resolution->conflict        = false;
        resolution->preferred_entry = kInvalidEntryId;
        resolution->source_entries.clear();

        for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
            MetadataConceptCandidate& candidate = resolution->candidates[i];
            candidate.preferred                 = false;
            candidate.conflict                  = false;
            assign_transfer_hint(&candidate);
            if (candidate.source_entries.empty()) {
                add_unique_entry(&resolution->source_entries,
                                 candidate.entry_id);
            } else {
                for (size_t e = 0U; e < candidate.source_entries.size(); ++e) {
                    add_unique_entry(&resolution->source_entries,
                                     candidate.source_entries[e]);
                }
            }
        }

        const MetadataConceptRole roles[] = {
            MetadataConceptRole::Primary,
            MetadataConceptRole::Orientation,
            MetadataConceptRole::Created,
            MetadataConceptRole::Digitized,
            MetadataConceptRole::Modified,
            MetadataConceptRole::MetadataDate,
            MetadataConceptRole::DateCreated,
            MetadataConceptRole::ColorSpace,
            MetadataConceptRole::IccProfile,
            MetadataConceptRole::ColorMatrix,
            MetadataConceptRole::WhiteBalance,
            MetadataConceptRole::Latitude,
            MetadataConceptRole::Longitude,
            MetadataConceptRole::Altitude,
            MetadataConceptRole::Timestamp,
            MetadataConceptRole::Crop,
            MetadataConceptRole::ActiveArea,
            MetadataConceptRole::Border,
            MetadataConceptRole::SensorGeometry,
            MetadataConceptRole::LensCorrection,
            MetadataConceptRole::BlackLevel,
            MetadataConceptRole::WhiteLevel,
            MetadataConceptRole::Linearization,
            MetadataConceptRole::CfaLayout,
            MetadataConceptRole::RawStorage,
            MetadataConceptRole::SourceProcessing,
            MetadataConceptRole::ComputationalProcessing,
            MetadataConceptRole::ThermalProcessing,
            MetadataConceptRole::StitchProcessing,
            MetadataConceptRole::ExposureTime,
            MetadataConceptRole::Aperture,
            MetadataConceptRole::IsoSensitivity,
            MetadataConceptRole::ExposureBias,
            MetadataConceptRole::ExposureProgram,
            MetadataConceptRole::Gain,
            MetadataConceptRole::RawExposureAdjustment,
            MetadataConceptRole::SourceColorTransform,
        };
        for (size_t i = 0U; i < std::size(roles); ++i) {
            mark_role_preferred(resolution, roles[i]);
            mark_role_conflicts(resolution, roles[i]);
        }

        uint8_t best_score = 0U;
        for (size_t i = 0U; i < resolution->candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate
                = resolution->candidates[i];
            if (!candidate.preferred) {
                continue;
            }
            if (resolution->preferred_entry == kInvalidEntryId
                || candidate.priority > best_score) {
                resolution->preferred_entry = candidate.entry_id;
                best_score                  = candidate.priority;
            }
        }
    }

}  // namespace

MetadataConceptResolution
resolve_metadata_concept(const MetaStore& store, MetadataConceptKind kind)
{
    MetadataConceptResolution out;
    out.kind = kind;
    switch (kind) {
    case MetadataConceptKind::Orientation:
        append_orientation_candidates(store, &out);
        break;
    case MetadataConceptKind::DateTime:
        append_datetime_candidates(store, &out);
        break;
    case MetadataConceptKind::ColorProfile:
        append_color_profile_candidates(store, &out);
        break;
    case MetadataConceptKind::Gps: append_gps_candidates(store, &out); break;
    case MetadataConceptKind::Geometry:
        append_geometry_candidates(store, &out);
        break;
    case MetadataConceptKind::LensCorrection:
        append_lens_correction_candidates(store, &out);
        break;
    case MetadataConceptKind::RawProcessing:
        append_raw_processing_candidates(store, &out);
        break;
    case MetadataConceptKind::Exposure:
        append_exposure_candidates(store, &out);
        break;
    }
    finalize_resolution(&out);
    return out;
}

MetadataConceptResult
resolve_metadata_concepts(const MetaStore& store)
{
    MetadataConceptResult out;
    out.concepts.reserve(8U);
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Orientation));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::DateTime));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Exposure));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::ColorProfile));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Gps));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::Geometry));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::LensCorrection));
    out.concepts.push_back(
        resolve_metadata_concept(store, MetadataConceptKind::RawProcessing));
    return out;
}

const char*
metadata_concept_kind_name(MetadataConceptKind kind) noexcept
{
    switch (kind) {
    case MetadataConceptKind::Orientation: return "orientation";
    case MetadataConceptKind::DateTime: return "date_time";
    case MetadataConceptKind::ColorProfile: return "color_profile";
    case MetadataConceptKind::Gps: return "gps";
    case MetadataConceptKind::Geometry: return "geometry";
    case MetadataConceptKind::LensCorrection: return "lens_correction";
    case MetadataConceptKind::RawProcessing: return "raw_processing";
    case MetadataConceptKind::Exposure: return "exposure";
    }
    return "unknown";
}

const char*
metadata_concept_source_family_name(MetadataConceptSourceFamily family) noexcept
{
    switch (family) {
    case MetadataConceptSourceFamily::Unknown: return "unknown";
    case MetadataConceptSourceFamily::Exif: return "exif";
    case MetadataConceptSourceFamily::Xmp: return "xmp";
    case MetadataConceptSourceFamily::Iptc: return "iptc";
    case MetadataConceptSourceFamily::Icc: return "icc";
    case MetadataConceptSourceFamily::PngText: return "png_text";
    case MetadataConceptSourceFamily::InterpretationRecord:
        return "interpretation_record";
    }
    return "unknown";
}

const char*
metadata_concept_role_name(MetadataConceptRole role) noexcept
{
    switch (role) {
    case MetadataConceptRole::Primary: return "primary";
    case MetadataConceptRole::Orientation: return "orientation";
    case MetadataConceptRole::Created: return "created";
    case MetadataConceptRole::Digitized: return "digitized";
    case MetadataConceptRole::Modified: return "modified";
    case MetadataConceptRole::MetadataDate: return "metadata_date";
    case MetadataConceptRole::DateCreated: return "date_created";
    case MetadataConceptRole::ColorSpace: return "color_space";
    case MetadataConceptRole::IccProfile: return "icc_profile";
    case MetadataConceptRole::ColorMatrix: return "color_matrix";
    case MetadataConceptRole::WhiteBalance: return "white_balance";
    case MetadataConceptRole::Latitude: return "latitude";
    case MetadataConceptRole::Longitude: return "longitude";
    case MetadataConceptRole::Altitude: return "altitude";
    case MetadataConceptRole::Timestamp: return "timestamp";
    case MetadataConceptRole::Crop: return "crop";
    case MetadataConceptRole::ActiveArea: return "active_area";
    case MetadataConceptRole::Border: return "border";
    case MetadataConceptRole::SensorGeometry: return "sensor_geometry";
    case MetadataConceptRole::LensCorrection: return "lens_correction";
    case MetadataConceptRole::BlackLevel: return "black_level";
    case MetadataConceptRole::WhiteLevel: return "white_level";
    case MetadataConceptRole::Linearization: return "linearization";
    case MetadataConceptRole::CfaLayout: return "cfa_layout";
    case MetadataConceptRole::RawStorage: return "raw_storage";
    case MetadataConceptRole::SourceProcessing: return "source_processing";
    case MetadataConceptRole::ComputationalProcessing:
        return "computational_processing";
    case MetadataConceptRole::ThermalProcessing: return "thermal_processing";
    case MetadataConceptRole::StitchProcessing: return "stitch_processing";
    case MetadataConceptRole::ExposureTime: return "exposure_time";
    case MetadataConceptRole::Aperture: return "aperture";
    case MetadataConceptRole::IsoSensitivity: return "iso_sensitivity";
    case MetadataConceptRole::ExposureBias: return "exposure_bias";
    case MetadataConceptRole::ExposureProgram: return "exposure_program";
    case MetadataConceptRole::Gain: return "gain";
    case MetadataConceptRole::RawExposureAdjustment:
        return "raw_exposure_adjustment";
    case MetadataConceptRole::SourceColorTransform:
        return "source_color_transform";
    }
    return "unknown";
}

const char*
metadata_concept_datetime_precision_name(
    MetadataConceptDateTimePrecision precision) noexcept
{
    switch (precision) {
    case MetadataConceptDateTimePrecision::Unknown: return "unknown";
    case MetadataConceptDateTimePrecision::Date: return "date";
    case MetadataConceptDateTimePrecision::DateTime: return "date_time";
    }
    return "unknown";
}

const char*
metadata_concept_timezone_kind_name(MetadataConceptTimeZoneKind kind) noexcept
{
    switch (kind) {
    case MetadataConceptTimeZoneKind::Unknown: return "unknown";
    case MetadataConceptTimeZoneKind::Local: return "local";
    case MetadataConceptTimeZoneKind::Utc: return "utc";
    case MetadataConceptTimeZoneKind::Offset: return "offset";
    }
    return "unknown";
}

const char*
metadata_concept_transfer_hint_name(MetadataConceptTransferHint hint) noexcept
{
    switch (hint) {
    case MetadataConceptTransferHint::Unknown: return "unknown";
    case MetadataConceptTransferHint::Safe: return "safe";
    case MetadataConceptTransferHint::SourceBound: return "source_bound";
    case MetadataConceptTransferHint::RenderedUnsafe: return "rendered_unsafe";
    case MetadataConceptTransferHint::RequiresTargetImageSpec:
        return "requires_target_image_spec";
    }
    return "unknown";
}

const char*
metadata_concept_gps_altitude_reference_name(uint8_t code) noexcept
{
    switch (code) {
    case 0U: return "above_sea_level";
    case 1U: return "below_sea_level";
    default: break;
    }
    return "unknown";
}

}  // namespace openmeta
