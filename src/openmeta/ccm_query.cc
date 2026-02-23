#include "openmeta/ccm_query.h"

#include <cstring>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    struct CcmTagInfo final {
        uint16_t tag            = 0;
        const char* name        = nullptr;
        bool is_matrix3xN       = false;
        bool is_reduction       = false;
        bool is_scalar_illuminant = false;
    };

    static constexpr CcmTagInfo kCcmTags[] = {
        { 0xC621, "ColorMatrix1", true, false, false },
        { 0xC622, "ColorMatrix2", true, false, false },
        { 0xCD33, "ColorMatrix3", true, false, false },
        { 0xC623, "CameraCalibration1", true, false, false },
        { 0xC624, "CameraCalibration2", true, false, false },
        { 0xCD32, "CameraCalibration3", true, false, false },
        { 0xC714, "ForwardMatrix1", true, false, false },
        { 0xC715, "ForwardMatrix2", true, false, false },
        { 0xCD34, "ForwardMatrix3", true, false, false },
        { 0xC625, "ReductionMatrix1", true, true, false },
        { 0xC626, "ReductionMatrix2", true, true, false },
        { 0xC627, "AnalogBalance", false, false, false },
        { 0xC628, "AsShotNeutral", false, false, false },
        { 0xC629, "AsShotWhiteXY", false, false, false },
        { 0xC65A, "CalibrationIlluminant1", false, false, true },
        { 0xC65B, "CalibrationIlluminant2", false, false, true },
    };

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool is_dng_version_tag(const Entry& e) noexcept
    {
        return e.key.kind == MetaKeyKind::ExifTag && e.key.data.exif_tag.tag == 0xC612U;
    }

    static const CcmTagInfo* find_ccm_tag(uint16_t tag) noexcept
    {
        for (size_t i = 0; i < sizeof(kCcmTags) / sizeof(kCcmTags[0]); ++i) {
            if (kCcmTags[i].tag == tag) {
                return &kCcmTags[i];
            }
        }
        return nullptr;
    }

    static bool ifd_is_dng_context(std::string_view ifd,
                                   const std::vector<std::string>& dng_ifds) noexcept
    {
        for (size_t i = 0; i < dng_ifds.size(); ++i) {
            if (dng_ifds[i] == ifd) {
                return true;
            }
        }
        return false;
    }

    static bool read_u16(std::span<const std::byte> bytes, size_t off,
                         uint16_t* out) noexcept
    {
        if (!out || off + sizeof(uint16_t) > bytes.size()) {
            return false;
        }
        uint16_t v = 0;
        std::memcpy(&v, bytes.data() + off, sizeof(uint16_t));
        *out = v;
        return true;
    }

    static bool read_i16(std::span<const std::byte> bytes, size_t off,
                         int16_t* out) noexcept
    {
        if (!out || off + sizeof(int16_t) > bytes.size()) {
            return false;
        }
        int16_t v = 0;
        std::memcpy(&v, bytes.data() + off, sizeof(int16_t));
        *out = v;
        return true;
    }

    static bool read_u32(std::span<const std::byte> bytes, size_t off,
                         uint32_t* out) noexcept
    {
        if (!out || off + sizeof(uint32_t) > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        std::memcpy(&v, bytes.data() + off, sizeof(uint32_t));
        *out = v;
        return true;
    }

    static bool read_i32(std::span<const std::byte> bytes, size_t off,
                         int32_t* out) noexcept
    {
        if (!out || off + sizeof(int32_t) > bytes.size()) {
            return false;
        }
        int32_t v = 0;
        std::memcpy(&v, bytes.data() + off, sizeof(int32_t));
        *out = v;
        return true;
    }

    static bool read_u64(std::span<const std::byte> bytes, size_t off,
                         uint64_t* out) noexcept
    {
        if (!out || off + sizeof(uint64_t) > bytes.size()) {
            return false;
        }
        uint64_t v = 0;
        std::memcpy(&v, bytes.data() + off, sizeof(uint64_t));
        *out = v;
        return true;
    }

    static bool read_i64(std::span<const std::byte> bytes, size_t off,
                         int64_t* out) noexcept
    {
        if (!out || off + sizeof(int64_t) > bytes.size()) {
            return false;
        }
        int64_t v = 0;
        std::memcpy(&v, bytes.data() + off, sizeof(int64_t));
        *out = v;
        return true;
    }

    static bool read_f32(std::span<const std::byte> bytes, size_t off,
                         float* out) noexcept
    {
        if (!out || off + sizeof(float) > bytes.size()) {
            return false;
        }
        float v = 0.0F;
        std::memcpy(&v, bytes.data() + off, sizeof(float));
        *out = v;
        return true;
    }

    static bool read_f64(std::span<const std::byte> bytes, size_t off,
                         double* out) noexcept
    {
        if (!out || off + sizeof(double) > bytes.size()) {
            return false;
        }
        double v = 0.0;
        std::memcpy(&v, bytes.data() + off, sizeof(double));
        *out = v;
        return true;
    }

    static bool append_scalar_value(const MetaValue& value,
                                    std::vector<double>* out) noexcept
    {
        if (!out) {
            return false;
        }
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64:
            out->push_back(static_cast<double>(value.data.u64));
            return true;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            out->push_back(static_cast<double>(value.data.i64));
            return true;
        case MetaElementType::F32: {
            float f = 0.0F;
            std::memcpy(&f, &value.data.f32_bits, sizeof(float));
            out->push_back(static_cast<double>(f));
            return true;
        }
        case MetaElementType::F64: {
            double d = 0.0;
            std::memcpy(&d, &value.data.f64_bits, sizeof(double));
            out->push_back(d);
            return true;
        }
        case MetaElementType::URational: {
            const uint32_t den = value.data.ur.denom;
            if (den == 0U) {
                return false;
            }
            out->push_back(static_cast<double>(value.data.ur.numer)
                           / static_cast<double>(den));
            return true;
        }
        case MetaElementType::SRational: {
            const int32_t den = value.data.sr.denom;
            if (den == 0) {
                return false;
            }
            out->push_back(static_cast<double>(value.data.sr.numer)
                           / static_cast<double>(den));
            return true;
        }
        }
        return false;
    }

    static bool append_array_values(const ByteArena& arena, const MetaValue& value,
                                    uint32_t max_values, std::vector<double>* out,
                                    bool* limit_exceeded) noexcept
    {
        if (!out) {
            return false;
        }
        if (limit_exceeded) {
            *limit_exceeded = false;
        }
        const std::span<const std::byte> raw = arena.span(value.data.span);
        const uint32_t n                     = value.count;
        if (n == 0U) {
            return true;
        }
        uint32_t limit = n;
        if (max_values != 0U && limit > max_values) {
            limit = max_values;
            if (limit_exceeded) {
                *limit_exceeded = true;
            }
        }

        switch (value.elem_type) {
        case MetaElementType::U8: {
            for (uint32_t i = 0; i < limit; ++i) {
                if (static_cast<size_t>(i) >= raw.size()) {
                    return false;
                }
                out->push_back(static_cast<double>(
                    static_cast<uint8_t>(raw[static_cast<size_t>(i)])));
            }
            return true;
        }
        case MetaElementType::I8: {
            for (uint32_t i = 0; i < limit; ++i) {
                if (static_cast<size_t>(i) >= raw.size()) {
                    return false;
                }
                const int8_t v
                    = static_cast<int8_t>(static_cast<uint8_t>(
                        raw[static_cast<size_t>(i)]));
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::U16: {
            for (uint32_t i = 0; i < limit; ++i) {
                uint16_t v = 0;
                if (!read_u16(raw, static_cast<size_t>(i) * sizeof(uint16_t),
                              &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::I16: {
            for (uint32_t i = 0; i < limit; ++i) {
                int16_t v = 0;
                if (!read_i16(raw, static_cast<size_t>(i) * sizeof(int16_t),
                              &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::U32: {
            for (uint32_t i = 0; i < limit; ++i) {
                uint32_t v = 0;
                if (!read_u32(raw, static_cast<size_t>(i) * sizeof(uint32_t),
                              &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::I32: {
            for (uint32_t i = 0; i < limit; ++i) {
                int32_t v = 0;
                if (!read_i32(raw, static_cast<size_t>(i) * sizeof(int32_t),
                              &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::U64: {
            for (uint32_t i = 0; i < limit; ++i) {
                uint64_t v = 0;
                if (!read_u64(raw, static_cast<size_t>(i) * sizeof(uint64_t),
                              &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::I64: {
            for (uint32_t i = 0; i < limit; ++i) {
                int64_t v = 0;
                if (!read_i64(raw, static_cast<size_t>(i) * sizeof(int64_t),
                              &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::F32: {
            for (uint32_t i = 0; i < limit; ++i) {
                float v = 0.0F;
                if (!read_f32(raw, static_cast<size_t>(i) * sizeof(float), &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
            }
            return true;
        }
        case MetaElementType::F64: {
            for (uint32_t i = 0; i < limit; ++i) {
                double v = 0.0;
                if (!read_f64(raw, static_cast<size_t>(i) * sizeof(double), &v)) {
                    return false;
                }
                out->push_back(v);
            }
            return true;
        }
        case MetaElementType::URational: {
            for (uint32_t i = 0; i < limit; ++i) {
                URational r {};
                const size_t off = static_cast<size_t>(i) * sizeof(URational);
                if (off + sizeof(URational) > raw.size()) {
                    return false;
                }
                std::memcpy(&r, raw.data() + off, sizeof(URational));
                if (r.denom == 0U) {
                    return false;
                }
                out->push_back(static_cast<double>(r.numer)
                               / static_cast<double>(r.denom));
            }
            return true;
        }
        case MetaElementType::SRational: {
            for (uint32_t i = 0; i < limit; ++i) {
                SRational r {};
                const size_t off = static_cast<size_t>(i) * sizeof(SRational);
                if (off + sizeof(SRational) > raw.size()) {
                    return false;
                }
                std::memcpy(&r, raw.data() + off, sizeof(SRational));
                if (r.denom == 0) {
                    return false;
                }
                out->push_back(static_cast<double>(r.numer)
                               / static_cast<double>(r.denom));
            }
            return true;
        }
        }
        return false;
    }

    static bool decode_values(const ByteArena& arena, const MetaValue& value,
                              uint32_t max_values, std::vector<double>* out,
                              bool* limit_exceeded) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (limit_exceeded) {
            *limit_exceeded = false;
        }
        if (value.kind == MetaValueKind::Scalar) {
            return append_scalar_value(value, out);
        }
        if (value.kind == MetaValueKind::Array) {
            return append_array_values(arena, value, max_values, out,
                                       limit_exceeded);
        }
        return false;
    }

    static void infer_shape(const CcmTagInfo& info, size_t value_count,
                            uint32_t* rows, uint32_t* cols) noexcept
    {
        if (!rows || !cols) {
            return;
        }
        *rows = 1U;
        *cols = static_cast<uint32_t>(value_count);
        if (value_count == 0U) {
            return;
        }
        if (info.is_scalar_illuminant) {
            *rows = 1U;
            *cols = 1U;
            return;
        }
        if (info.is_matrix3xN && (value_count % 3U) == 0U) {
            *rows = 3U;
            *cols = static_cast<uint32_t>(value_count / 3U);
            return;
        }
    }

}  // namespace


CcmQueryResult
collect_dng_ccm_fields(const MetaStore& store, std::vector<CcmField>* out,
                       const CcmQueryOptions& options) noexcept
{
    CcmQueryResult result;
    if (!out) {
        return result;
    }
    out->clear();

    const ByteArena& arena           = store.arena();
    const std::span<const Entry> all = store.entries();

    std::vector<std::string> dng_ifds;
    dng_ifds.reserve(8);

    for (size_t i = 0; i < all.size(); ++i) {
        const Entry& e = all[i];
        if (any(e.flags, EntryFlags::Deleted)) {
            continue;
        }
        if (!is_dng_version_tag(e)) {
            continue;
        }
        const std::string_view ifd = arena_string(arena, e.key.data.exif_tag.ifd);
        if (ifd.empty()) {
            continue;
        }
        bool exists = false;
        for (size_t j = 0; j < dng_ifds.size(); ++j) {
            if (dng_ifds[j] == ifd) {
                exists = true;
                break;
            }
        }
        if (!exists) {
            dng_ifds.emplace_back(ifd.data(), ifd.size());
        }
    }

    if (options.require_dng_context && dng_ifds.empty()) {
        return result;
    }

    for (size_t i = 0; i < all.size(); ++i) {
        const Entry& e = all[i];
        if (any(e.flags, EntryFlags::Deleted)) {
            continue;
        }
        if (e.key.kind != MetaKeyKind::ExifTag) {
            continue;
        }
        const CcmTagInfo* info = find_ccm_tag(e.key.data.exif_tag.tag);
        if (!info) {
            continue;
        }
        if (!options.include_reduction_matrices && info->is_reduction) {
            continue;
        }

        const std::string_view ifd = arena_string(arena, e.key.data.exif_tag.ifd);
        if (options.require_dng_context && !ifd_is_dng_context(ifd, dng_ifds)) {
            continue;
        }

        if (options.limits.max_fields != 0U
            && out->size() >= options.limits.max_fields) {
            result.status = CcmQueryStatus::LimitExceeded;
            break;
        }

        CcmField field;
        field.name.assign(info->name ? info->name : "");
        field.ifd.assign(ifd.data(), ifd.size());
        field.tag = info->tag;

        bool values_limit_exceeded = false;
        if (!decode_values(arena, e.value, options.limits.max_values_per_field,
                           &field.values, &values_limit_exceeded)) {
            continue;
        }
        if (field.values.empty()) {
            continue;
        }
        if (values_limit_exceeded) {
            result.status = CcmQueryStatus::LimitExceeded;
        }
        infer_shape(*info, field.values.size(), &field.rows, &field.cols);
        out->push_back(std::move(field));
    }

    result.fields_found = static_cast<uint32_t>(out->size());
    return result;
}

}  // namespace openmeta

