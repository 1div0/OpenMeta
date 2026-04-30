// SPDX-License-Identifier: Apache-2.0

#include "openmeta/phaseone_geometry.h"

#include "openmeta/byte_arena.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <cmath>
#include <cstddef>
#include <cstring>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static constexpr uint16_t kPhaseOneSensorWidthTag       = 0x0108U;
    static constexpr uint16_t kPhaseOneSensorHeightTag      = 0x0109U;
    static constexpr uint16_t kPhaseOneSensorLeftMarginTag  = 0x010AU;
    static constexpr uint16_t kPhaseOneSensorTopMarginTag   = 0x010BU;
    static constexpr uint16_t kPhaseOneImageWidthTag        = 0x010CU;
    static constexpr uint16_t kPhaseOneImageHeightTag       = 0x010DU;
    static constexpr std::string_view kPhaseOneMainIfd      = "mk_phaseone0";
    static constexpr std::string_view kPhaseOneSensorCalIfd =
        "mk_phaseone_sensorcalibration_0";

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool scalar_to_u32(const MetaValue& value, uint32_t* out) noexcept
    {
        if (!out || value.kind != MetaValueKind::Scalar) {
            return false;
        }
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
            *out = static_cast<uint32_t>(value.data.u64);
            return true;
        case MetaElementType::U64:
            if (value.data.u64 > 0xffffffffULL) {
                return false;
            }
            *out = static_cast<uint32_t>(value.data.u64);
            return true;
        default: return false;
        }
    }

    static bool is_phaseone_main_tag(const MetaStore& store, const Entry& entry,
                                     uint16_t tag) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }
        if (entry.key.data.exif_tag.tag != tag) {
            return false;
        }
        return arena_string(store.arena(), entry.key.data.exif_tag.ifd)
               == kPhaseOneMainIfd;
    }

    static bool entry_ifd_is(const MetaStore& store, const Entry& entry,
                             std::string_view ifd) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }
        return arena_string(store.arena(), entry.key.data.exif_tag.ifd) == ifd;
    }

    static PhaseOneRawGeometryStatus read_phaseone_u32(const MetaStore& store,
                                                       uint16_t tag,
                                                       uint32_t* out) noexcept
    {
        bool saw_tag = false;
        const std::span<const Entry> entries = store.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& entry = entries[i];
            if (!is_phaseone_main_tag(store, entry, tag)) {
                continue;
            }
            saw_tag = true;
            if (scalar_to_u32(entry.value, out)) {
                return PhaseOneRawGeometryStatus::Ok;
            }
        }
        if (saw_tag) {
            return PhaseOneRawGeometryStatus::InvalidValue;
        }
        return PhaseOneRawGeometryStatus::MissingField;
    }

    static uint32_t elem_type_size(MetaElementType type) noexcept
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
        default: return 0U;
        }
    }

    static uint64_t value_payload_bytes(const MetaStore& store,
                                        const Entry& entry) noexcept
    {
        const MetaValue& value = entry.value;
        if (value.kind == MetaValueKind::Bytes || value.kind == MetaValueKind::Text
            || value.kind == MetaValueKind::Array) {
            return store.arena().span(value.data.span).size();
        }
        if (value.kind == MetaValueKind::Scalar) {
            return elem_type_size(value.elem_type);
        }
        return 0U;
    }

    static uint32_t read_u32_le(std::span<const std::byte> bytes,
                                size_t off) noexcept
    {
        return uint32_t(static_cast<uint8_t>(bytes[off + 0U]))
               | (uint32_t(static_cast<uint8_t>(bytes[off + 1U])) << 8U)
               | (uint32_t(static_cast<uint8_t>(bytes[off + 2U])) << 16U)
               | (uint32_t(static_cast<uint8_t>(bytes[off + 3U])) << 24U);
    }

    static uint32_t read_u32_be(std::span<const std::byte> bytes,
                                size_t off) noexcept
    {
        return (uint32_t(static_cast<uint8_t>(bytes[off + 0U])) << 24U)
               | (uint32_t(static_cast<uint8_t>(bytes[off + 1U])) << 16U)
               | (uint32_t(static_cast<uint8_t>(bytes[off + 2U])) << 8U)
               | uint32_t(static_cast<uint8_t>(bytes[off + 3U]));
    }

    static double f32_bits_to_double(uint32_t bits) noexcept
    {
        float value = 0.0F;
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&value, &bits, sizeof(value));
        return static_cast<double>(value);
    }

    static int f32_reasonable_score(const double* values,
                                    uint32_t count) noexcept
    {
        int score = 0;
        for (uint32_t i = 0U; i < count; ++i) {
            const double value = values[i];
            if (!std::isfinite(value) || value < -1000000.0
                || value > 1000000.0) {
                return -1;
            }
            const double abs_value = value < 0.0 ? -value : value;
            if (abs_value == 0.0) {
                score += 1;
            } else if (abs_value >= 0.000001 && abs_value <= 10000.0) {
                score += 2;
            }
        }
        return score;
    }

    static bool read_f32_array_from_bytes(std::span<const std::byte> bytes,
                                          uint32_t count,
                                          double* out) noexcept
    {
        if (!out || bytes.size() < size_t(count) * 4U) {
            return false;
        }
        double le_values[9] {};
        double be_values[9] {};
        if (count > 9U) {
            return false;
        }
        for (uint32_t i = 0U; i < count; ++i) {
            const size_t off = size_t(i) * 4U;
            le_values[i] = f32_bits_to_double(read_u32_le(bytes, off));
            be_values[i] = f32_bits_to_double(read_u32_be(bytes, off));
        }

        const int le_score = f32_reasonable_score(le_values, count);
        const int be_score = f32_reasonable_score(be_values, count);
        if (le_score < 0 && be_score < 0) {
            return false;
        }
        const double* chosen = le_score >= be_score ? le_values : be_values;
        for (uint32_t i = 0U; i < count; ++i) {
            out[i] = chosen[i];
        }
        return true;
    }

    static bool read_f32_array_value(const MetaStore& store,
                                     const Entry& entry, uint32_t count,
                                     double* out) noexcept
    {
        const MetaValue& value = entry.value;
        if (!out) {
            return false;
        }
        if (value.kind == MetaValueKind::Bytes) {
            return read_f32_array_from_bytes(store.arena().span(value.data.span),
                                             count, out);
        }
        if (value.kind == MetaValueKind::Array
            && value.elem_type == MetaElementType::F32
            && value.count >= count) {
            const std::span<const std::byte> bytes = store.arena().span(
                value.data.span);
            if (bytes.size() < size_t(count) * 4U) {
                return false;
            }
            for (uint32_t i = 0U; i < count; ++i) {
                uint32_t bits = 0U;
                std::memcpy(&bits, bytes.data() + size_t(i) * 4U,
                            sizeof(bits));
                out[i] = f32_bits_to_double(bits);
            }
            return f32_reasonable_score(out, count) >= 0;
        }
        if (value.kind == MetaValueKind::Scalar && count == 1U) {
            uint32_t bits = 0U;
            if (value.elem_type == MetaElementType::F32) {
                bits = value.data.f32_bits;
            } else if (value.elem_type == MetaElementType::U32) {
                bits = static_cast<uint32_t>(value.data.u64);
            } else {
                return false;
            }
            out[0] = f32_bits_to_double(bits);
            return f32_reasonable_score(out, 1U) >= 0;
        }
        return false;
    }

    static void mark_seen(PhaseOneRawProcessingResult* result) noexcept
    {
        if (result) {
            result->fields_seen += 1U;
        }
    }

    static void mark_decoded(PhaseOneRawProcessingResult* result) noexcept
    {
        if (result) {
            result->fields_decoded += 1U;
        }
    }

    static void mark_invalid(PhaseOneRawProcessingResult* result) noexcept
    {
        if (result) {
            result->invalid_fields += 1U;
        }
    }

    static void decode_f32_array_field(const MetaStore& store,
                                       const Entry& entry, uint32_t count,
                                       bool* present, double* out,
                                       PhaseOneRawProcessingResult* result)
        noexcept
    {
        mark_seen(result);
        if (present && *present) {
            return;
        }
        if (read_f32_array_value(store, entry, count, out)) {
            if (present) {
                *present = true;
            }
            mark_decoded(result);
        } else {
            mark_invalid(result);
        }
    }

    static void decode_u32_field(const Entry& entry, bool* present,
                                 uint32_t* out,
                                 PhaseOneRawProcessingResult* result) noexcept
    {
        mark_seen(result);
        if (present && *present) {
            return;
        }
        if (scalar_to_u32(entry.value, out)) {
            if (present) {
                *present = true;
            }
            mark_decoded(result);
        } else {
            mark_invalid(result);
        }
    }

    static void decode_bytes_size_field(const MetaStore& store,
                                        const Entry& entry, bool* present,
                                        uint64_t* out,
                                        PhaseOneRawProcessingResult* result)
        noexcept
    {
        mark_seen(result);
        const uint64_t bytes = value_payload_bytes(store, entry);
        if (present && !*present) {
            *present = true;
            if (out) {
                *out = bytes;
            }
            mark_decoded(result);
        }
    }

    static uint32_t phaseone_numeric_value_count(const MetaStore& store,
                                                 const Entry& entry) noexcept
    {
        if (entry.origin.wire_count != 0U) {
            return entry.origin.wire_count;
        }
        const uint64_t bytes = value_payload_bytes(store, entry);
        if (bytes > 0xffffffffULL) {
            return 0U;
        }
        return static_cast<uint32_t>(bytes / 4U);
    }

}  // namespace

PhaseOneRawGeometryResult
phaseone_raw_geometry_from_values(uint32_t sensor_width,
                                  uint32_t sensor_height,
                                  uint32_t sensor_left_margin,
                                  uint32_t sensor_top_margin,
                                  uint32_t image_width,
                                  uint32_t image_height) noexcept
{
    PhaseOneRawGeometryResult result;
    PhaseOneRawGeometry* geom = &result.geometry;

    geom->sensor_width       = sensor_width;
    geom->sensor_height      = sensor_height;
    geom->sensor_left_margin = sensor_left_margin;
    geom->sensor_top_margin  = sensor_top_margin;
    geom->image_width        = image_width;
    geom->image_height       = image_height;
    geom->active_x           = sensor_left_margin;
    geom->active_y           = sensor_top_margin;
    geom->active_width       = image_width;
    geom->active_height      = image_height;

    if (sensor_width == 0U || sensor_height == 0U || image_width == 0U
        || image_height == 0U) {
        result.status = PhaseOneRawGeometryStatus::InvalidValue;
        return result;
    }
    if (sensor_left_margin > sensor_width
        || image_width > sensor_width - sensor_left_margin
        || sensor_top_margin > sensor_height
        || image_height > sensor_height - sensor_top_margin) {
        result.status = PhaseOneRawGeometryStatus::OutOfBounds;
        return result;
    }

    geom->right_margin  = sensor_width - sensor_left_margin - image_width;
    geom->bottom_margin = sensor_height - sensor_top_margin - image_height;
    result.status       = PhaseOneRawGeometryStatus::Ok;
    return result;
}

PhaseOneRawGeometryResult
phaseone_raw_geometry_from_store(const MetaStore& store) noexcept
{
    uint32_t sensor_width       = 0;
    uint32_t sensor_height      = 0;
    uint32_t sensor_left_margin = 0;
    uint32_t sensor_top_margin  = 0;
    uint32_t image_width        = 0;
    uint32_t image_height       = 0;

    PhaseOneRawGeometryStatus status = read_phaseone_u32(
        store, kPhaseOneSensorWidthTag, &sensor_width);
    if (status != PhaseOneRawGeometryStatus::Ok) {
        PhaseOneRawGeometryResult result;
        result.status = status;
        return result;
    }
    status = read_phaseone_u32(store, kPhaseOneSensorHeightTag,
                               &sensor_height);
    if (status != PhaseOneRawGeometryStatus::Ok) {
        PhaseOneRawGeometryResult result;
        result.status = status;
        return result;
    }
    status = read_phaseone_u32(store, kPhaseOneSensorLeftMarginTag,
                               &sensor_left_margin);
    if (status != PhaseOneRawGeometryStatus::Ok) {
        PhaseOneRawGeometryResult result;
        result.status = status;
        return result;
    }
    status = read_phaseone_u32(store, kPhaseOneSensorTopMarginTag,
                               &sensor_top_margin);
    if (status != PhaseOneRawGeometryStatus::Ok) {
        PhaseOneRawGeometryResult result;
        result.status = status;
        return result;
    }
    status = read_phaseone_u32(store, kPhaseOneImageWidthTag, &image_width);
    if (status != PhaseOneRawGeometryStatus::Ok) {
        PhaseOneRawGeometryResult result;
        result.status = status;
        return result;
    }
    status = read_phaseone_u32(store, kPhaseOneImageHeightTag, &image_height);
    if (status != PhaseOneRawGeometryStatus::Ok) {
        PhaseOneRawGeometryResult result;
        result.status = status;
        return result;
    }

    return phaseone_raw_geometry_from_values(sensor_width, sensor_height,
                                             sensor_left_margin,
                                             sensor_top_margin, image_width,
                                             image_height);
}

PhaseOneRawProcessingResult
phaseone_raw_processing_from_store(const MetaStore& store) noexcept
{
    PhaseOneRawProcessingResult result;
    PhaseOneRawProcessingInfo* info = &result.info;

    const std::span<const Entry> entries = store.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        if (entry_ifd_is(store, entry, kPhaseOneMainIfd)) {
            const uint16_t tag = entry.key.data.exif_tag.tag;
            switch (tag) {
            case 0x0106U:
                decode_f32_array_field(store, entry, 9U,
                                       &info->has_color_matrix1,
                                       info->color_matrix1, &result);
                break;
            case 0x0107U:
                decode_f32_array_field(store, entry, 3U,
                                       &info->has_wb_rgb_levels,
                                       info->wb_rgb_levels, &result);
                break;
            case 0x010EU:
                decode_u32_field(entry, &info->has_raw_format,
                                 &info->raw_format, &result);
                break;
            case 0x010FU:
                decode_bytes_size_field(store, entry, &info->has_raw_data,
                                        &info->raw_data_bytes, &result);
                break;
            case 0x0110U:
                decode_bytes_size_field(store, entry,
                                        &info->has_sensor_calibration,
                                        &info->sensor_calibration_payload_bytes,
                                        &result);
                break;
            case 0x0210U:
                decode_f32_array_field(store, entry, 1U,
                                       &info->has_sensor_temperature_c,
                                       &info->sensor_temperature_c, &result);
                break;
            case 0x0211U:
                decode_f32_array_field(store, entry, 1U,
                                       &info->has_sensor_temperature2_c,
                                       &info->sensor_temperature2_c, &result);
                break;
            case 0x021CU:
                decode_bytes_size_field(store, entry, &info->has_strip_offsets,
                                        &info->strip_offsets_bytes, &result);
                break;
            case 0x021DU:
                decode_u32_field(entry, &info->has_black_level,
                                 &info->black_level, &result);
                break;
            case 0x0223U:
                decode_bytes_size_field(store, entry,
                                        &info->has_black_level_data,
                                        &info->black_level_data_bytes,
                                        &result);
                break;
            case 0x0226U:
                decode_f32_array_field(store, entry, 9U,
                                       &info->has_color_matrix2,
                                       info->color_matrix2, &result);
                break;
            default: break;
            }
            continue;
        }

        if (!entry_ifd_is(store, entry, kPhaseOneSensorCalIfd)) {
            continue;
        }

        mark_seen(&result);
        mark_decoded(&result);
        info->has_sensor_calibration = true;
        info->sensor_calibration_entry_count += 1U;
        info->sensor_calibration_payload_bytes += value_payload_bytes(store,
                                                                      entry);

        const uint16_t tag = entry.key.data.exif_tag.tag;
        if (tag == 0x0400U) {
            info->has_sensor_defects = true;
            info->sensor_defects_bytes += value_payload_bytes(store, entry);
        } else if (tag == 0x0401U || tag == 0x040BU || tag == 0x0410U
                   || tag == 0x0416U) {
            info->has_flat_field = true;
            info->flat_field_bytes += value_payload_bytes(store, entry);
        } else if (tag == 0x0419U || tag == 0x041AU) {
            info->has_linearization_coefficients = true;
            info->linearization_coefficients_count += phaseone_numeric_value_count(
                store, entry);
        }
    }

    if (result.fields_seen == 0U) {
        result.status = PhaseOneRawProcessingStatus::MissingField;
    } else if (result.fields_decoded == 0U && result.invalid_fields != 0U) {
        result.status = PhaseOneRawProcessingStatus::InvalidValue;
    } else if (result.invalid_fields != 0U) {
        result.status = PhaseOneRawProcessingStatus::Partial;
    } else {
        result.status = PhaseOneRawProcessingStatus::Ok;
    }
    return result;
}

const char*
phaseone_raw_geometry_status_name(PhaseOneRawGeometryStatus status) noexcept
{
    switch (status) {
    case PhaseOneRawGeometryStatus::Ok: return "ok";
    case PhaseOneRawGeometryStatus::MissingField: return "missing_field";
    case PhaseOneRawGeometryStatus::InvalidValue: return "invalid_value";
    case PhaseOneRawGeometryStatus::OutOfBounds: return "out_of_bounds";
    default: return "unknown";
    }
}

const char*
phaseone_raw_processing_status_name(PhaseOneRawProcessingStatus status) noexcept
{
    switch (status) {
    case PhaseOneRawProcessingStatus::Ok: return "ok";
    case PhaseOneRawProcessingStatus::MissingField: return "missing_field";
    case PhaseOneRawProcessingStatus::InvalidValue: return "invalid_value";
    case PhaseOneRawProcessingStatus::Partial: return "partial";
    default: return "unknown";
    }
}

}  // namespace openmeta
