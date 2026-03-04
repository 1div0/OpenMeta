#include "openmeta/icc_interpret.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace openmeta {
namespace {

    static constexpr uint32_t fourcc(char a, char b, char c, char d) noexcept
    {
        return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24)
               | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16)
               | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8)
               | static_cast<uint32_t>(static_cast<uint8_t>(d));
    }

    static uint8_t u8(std::byte b) noexcept { return static_cast<uint8_t>(b); }

    static bool read_u16be(std::span<const std::byte> bytes, uint64_t off,
                           uint16_t* out) noexcept
    {
        if (!out || off + 2U > bytes.size()) {
            return false;
        }
        *out = static_cast<uint16_t>(u8(bytes[off]) << 8)
               | static_cast<uint16_t>(u8(bytes[off + 1U]));
        return true;
    }

    static bool read_u32be(std::span<const std::byte> bytes, uint64_t off,
                           uint32_t* out) noexcept
    {
        if (!out || off + 4U > bytes.size()) {
            return false;
        }
        *out = (static_cast<uint32_t>(u8(bytes[off + 0U])) << 24)
               | (static_cast<uint32_t>(u8(bytes[off + 1U])) << 16)
               | (static_cast<uint32_t>(u8(bytes[off + 2U])) << 8)
               | static_cast<uint32_t>(u8(bytes[off + 3U]));
        return true;
    }

    static bool read_i32be(std::span<const std::byte> bytes, uint64_t off,
                           int32_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        uint32_t u = 0;
        if (!read_u32be(bytes, off, &u)) {
            return false;
        }
        *out = static_cast<int32_t>(u);
        return true;
    }

    static bool is_printable_ascii4(uint32_t v) noexcept
    {
        const char c0 = static_cast<char>((v >> 24) & 0xFFU);
        const char c1 = static_cast<char>((v >> 16) & 0xFFU);
        const char c2 = static_cast<char>((v >> 8) & 0xFFU);
        const char c3 = static_cast<char>((v >> 0) & 0xFFU);
        return c0 >= 0x20 && c0 <= 0x7E && c1 >= 0x20 && c1 <= 0x7E
               && c2 >= 0x20 && c2 <= 0x7E && c3 >= 0x20 && c3 <= 0x7E;
    }

    static void fourcc_to_string(uint32_t v, std::string* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        if (is_printable_ascii4(v)) {
            out->push_back(static_cast<char>((v >> 24) & 0xFFU));
            out->push_back(static_cast<char>((v >> 16) & 0xFFU));
            out->push_back(static_cast<char>((v >> 8) & 0xFFU));
            out->push_back(static_cast<char>((v >> 0) & 0xFFU));
            return;
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(v));
        out->append(buf);
    }

    static bool append_xyz_values(std::span<const std::byte> bytes,
                                  uint32_t triplets, uint32_t max_values,
                                  std::vector<double>* out,
                                  bool* truncated) noexcept
    {
        if (!out) {
            return false;
        }
        if (truncated) {
            *truncated = false;
        }
        const uint64_t total_values = static_cast<uint64_t>(triplets) * 3ULL;
        uint32_t limit              = static_cast<uint32_t>(total_values);
        if (max_values != 0U && limit > max_values) {
            limit = max_values;
            if (truncated) {
                *truncated = true;
            }
        }
        out->reserve(limit);
        for (uint32_t i = 0; i < limit; ++i) {
            int32_t s15        = 0;
            const uint64_t off = 8ULL + static_cast<uint64_t>(i) * 4ULL;
            if (!read_i32be(bytes, off, &s15)) {
                return false;
            }
            out->push_back(static_cast<double>(s15) / 65536.0);
        }
        return true;
    }

    static bool append_curve_values(std::span<const std::byte> bytes,
                                    uint32_t count, uint32_t max_values,
                                    std::vector<double>* out,
                                    bool* truncated) noexcept
    {
        if (!out) {
            return false;
        }
        if (truncated) {
            *truncated = false;
        }
        uint32_t limit = count;
        if (max_values != 0U && limit > max_values) {
            limit = max_values;
            if (truncated) {
                *truncated = true;
            }
        }
        out->reserve(limit);
        for (uint32_t i = 0; i < limit; ++i) {
            uint16_t u         = 0;
            const uint64_t off = 12ULL + static_cast<uint64_t>(i) * 2ULL;
            if (!read_u16be(bytes, off, &u)) {
                return false;
            }
            if (count == 1U) {
                out->push_back(static_cast<double>(u) / 256.0);
            } else {
                out->push_back(static_cast<double>(u) / 65535.0);
            }
        }
        return true;
    }

    static bool append_fixed_array_values(std::span<const std::byte> bytes,
                                          uint32_t max_values, bool is_signed,
                                          std::vector<double>* out,
                                          bool* truncated) noexcept
    {
        if (!out || bytes.size() < 12U) {
            return false;
        }
        if (truncated) {
            *truncated = false;
        }
        const uint64_t payload = bytes.size() - 8U;
        if ((payload % 4ULL) != 0ULL) {
            return false;
        }
        const uint32_t count = static_cast<uint32_t>(payload / 4ULL);
        if (count == 0U) {
            return false;
        }
        uint32_t limit = count;
        if (max_values != 0U && limit > max_values) {
            limit = max_values;
            if (truncated) {
                *truncated = true;
            }
        }
        out->reserve(limit);
        for (uint32_t i = 0; i < limit; ++i) {
            const uint64_t off = 8ULL + static_cast<uint64_t>(i) * 4ULL;
            if (is_signed) {
                int32_t v = 0;
                if (!read_i32be(bytes, off, &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v) / 65536.0);
            } else {
                uint32_t v = 0;
                if (!read_u32be(bytes, off, &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v) / 65536.0);
            }
        }
        return true;
    }

    static bool append_unsigned_integer_array_values(
        std::span<const std::byte> bytes, uint32_t item_bytes,
        uint32_t max_values, std::vector<double>* out, bool* truncated) noexcept
    {
        if (!out || item_bytes == 0U || bytes.size() < 8U) {
            return false;
        }
        if (truncated) {
            *truncated = false;
        }
        const uint64_t payload = bytes.size() - 8U;
        if ((payload % static_cast<uint64_t>(item_bytes)) != 0ULL) {
            return false;
        }
        const uint32_t count = static_cast<uint32_t>(
            payload / static_cast<uint64_t>(item_bytes));
        if (count == 0U) {
            return false;
        }

        uint32_t limit = count;
        if (max_values != 0U && limit > max_values) {
            limit = max_values;
            if (truncated) {
                *truncated = true;
            }
        }
        out->reserve(limit);
        for (uint32_t i = 0U; i < limit; ++i) {
            const uint64_t off = 8ULL
                                 + static_cast<uint64_t>(i)
                                       * static_cast<uint64_t>(item_bytes);
            if (item_bytes == 1U) {
                if (off + 1U > bytes.size()) {
                    return false;
                }
                const uint8_t v = u8(bytes[off]);
                out->push_back(static_cast<double>(v));
                continue;
            }
            if (item_bytes == 2U) {
                uint16_t v = 0U;
                if (!read_u16be(bytes, off, &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
                continue;
            }
            if (item_bytes == 4U) {
                uint32_t v = 0U;
                if (!read_u32be(bytes, off, &v)) {
                    return false;
                }
                out->push_back(static_cast<double>(v));
                continue;
            }
            return false;
        }
        return true;
    }

    static bool checked_mul_u64(uint64_t a, uint64_t b, uint64_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (a == 0U || b == 0U) {
            *out = 0U;
            return true;
        }
        if (a > (std::numeric_limits<uint64_t>::max() / b)) {
            return false;
        }
        *out = a * b;
        return true;
    }

    static bool checked_add_u64(uint64_t a, uint64_t b, uint64_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (a > (std::numeric_limits<uint64_t>::max() - b)) {
            return false;
        }
        *out = a + b;
        return true;
    }

    static bool checked_pow_u64(uint64_t base, uint32_t exp,
                                uint64_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        uint64_t v = 1U;
        for (uint32_t i = 0; i < exp; ++i) {
            if (!checked_mul_u64(v, base, &v)) {
                return false;
            }
        }
        *out = v;
        return true;
    }

    static void append_u64_dec(uint64_t v, std::string* out)
    {
        if (!out) {
            return;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llu",
                      static_cast<unsigned long long>(v));
        out->append(buf);
    }

    static bool
    format_lut_summary(std::string_view type, uint8_t in_channels,
                       uint8_t out_channels, uint8_t clut_points,
                       uint64_t in_table_values, uint64_t clut_values,
                       uint64_t out_table_values, uint64_t needed_bytes,
                       uint64_t available_bytes, std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        out->append(type.data(), type.size());
        out->append(" in=");
        append_u64_dec(in_channels, out);
        out->append(" out=");
        append_u64_dec(out_channels, out);
        out->append(" clut_points=");
        append_u64_dec(clut_points, out);
        out->append(" in_tbl=");
        append_u64_dec(in_table_values, out);
        out->append(" clut=");
        append_u64_dec(clut_values, out);
        out->append(" out_tbl=");
        append_u64_dec(out_table_values, out);
        out->append(" need=");
        append_u64_dec(needed_bytes, out);
        if (available_bytes > needed_bytes) {
            out->append(" extra=");
            append_u64_dec(available_bytes - needed_bytes, out);
        }
        return true;
    }

    static IccTagInterpretStatus
    decode_lut8_summary(std::span<const std::byte> bytes,
                        const IccTagInterpretOptions& options,
                        IccTagInterpretation* out) noexcept
    {
        if (!out || bytes.size() < 48U) {
            return IccTagInterpretStatus::Malformed;
        }
        const uint8_t in_channels  = u8(bytes[8U]);
        const uint8_t out_channels = u8(bytes[9U]);
        const uint8_t clut_points  = u8(bytes[10U]);
        if (in_channels == 0U || out_channels == 0U || clut_points == 0U) {
            return IccTagInterpretStatus::Malformed;
        }

        uint64_t clut_nodes = 0;
        if (!checked_pow_u64(clut_points, in_channels, &clut_nodes)) {
            return IccTagInterpretStatus::Malformed;
        }

        uint64_t in_table_values  = 0;
        uint64_t out_table_values = 0;
        uint64_t clut_values      = 0;
        if (!checked_mul_u64(in_channels, 256U, &in_table_values)
            || !checked_mul_u64(out_channels, 256U, &out_table_values)
            || !checked_mul_u64(clut_nodes, out_channels, &clut_values)) {
            return IccTagInterpretStatus::Malformed;
        }

        uint64_t data_values = 0;
        uint64_t needed      = 48U;
        if (!checked_add_u64(in_table_values, clut_values, &data_values)
            || !checked_add_u64(data_values, out_table_values, &data_values)
            || !checked_add_u64(needed, data_values, &needed)) {
            return IccTagInterpretStatus::Malformed;
        }
        if (needed > bytes.size()) {
            return IccTagInterpretStatus::Malformed;
        }

        if (!format_lut_summary("mft1", in_channels, out_channels, clut_points,
                                in_table_values, clut_values, out_table_values,
                                needed, bytes.size(), &out->text)) {
            return IccTagInterpretStatus::Malformed;
        }
        if (options.limits.max_text_bytes != 0U
            && out->text.size() > options.limits.max_text_bytes) {
            out->text.resize(options.limits.max_text_bytes);
            return IccTagInterpretStatus::LimitExceeded;
        }
        return IccTagInterpretStatus::Ok;
    }

    static IccTagInterpretStatus
    decode_lut16_summary(std::span<const std::byte> bytes,
                         const IccTagInterpretOptions& options,
                         IccTagInterpretation* out) noexcept
    {
        if (!out || bytes.size() < 52U) {
            return IccTagInterpretStatus::Malformed;
        }
        const uint8_t in_channels  = u8(bytes[8U]);
        const uint8_t out_channels = u8(bytes[9U]);
        const uint8_t clut_points  = u8(bytes[10U]);
        if (in_channels == 0U || out_channels == 0U || clut_points == 0U) {
            return IccTagInterpretStatus::Malformed;
        }
        uint16_t in_entries  = 0;
        uint16_t out_entries = 0;
        if (!read_u16be(bytes, 48U, &in_entries)
            || !read_u16be(bytes, 50U, &out_entries)) {
            return IccTagInterpretStatus::Malformed;
        }
        if (in_entries == 0U || out_entries == 0U) {
            return IccTagInterpretStatus::Malformed;
        }

        uint64_t clut_nodes = 0;
        if (!checked_pow_u64(clut_points, in_channels, &clut_nodes)) {
            return IccTagInterpretStatus::Malformed;
        }

        uint64_t in_table_values  = 0;
        uint64_t out_table_values = 0;
        uint64_t clut_values      = 0;
        if (!checked_mul_u64(in_channels, in_entries, &in_table_values)
            || !checked_mul_u64(out_channels, out_entries, &out_table_values)
            || !checked_mul_u64(clut_nodes, out_channels, &clut_values)) {
            return IccTagInterpretStatus::Malformed;
        }

        uint64_t data_values = 0;
        uint64_t data_bytes  = 0;
        uint64_t needed      = 52U;
        if (!checked_add_u64(in_table_values, clut_values, &data_values)
            || !checked_add_u64(data_values, out_table_values, &data_values)
            || !checked_mul_u64(data_values, 2U, &data_bytes)
            || !checked_add_u64(needed, data_bytes, &needed)) {
            return IccTagInterpretStatus::Malformed;
        }
        if (needed > bytes.size()) {
            return IccTagInterpretStatus::Malformed;
        }

        if (!format_lut_summary("mft2", in_channels, out_channels, clut_points,
                                in_table_values, clut_values, out_table_values,
                                needed, bytes.size(), &out->text)) {
            return IccTagInterpretStatus::Malformed;
        }
        out->text.append(" in_entries=");
        append_u64_dec(in_entries, &out->text);
        out->text.append(" out_entries=");
        append_u64_dec(out_entries, &out->text);
        if (options.limits.max_text_bytes != 0U
            && out->text.size() > options.limits.max_text_bytes) {
            out->text.resize(options.limits.max_text_bytes);
            return IccTagInterpretStatus::LimitExceeded;
        }
        return IccTagInterpretStatus::Ok;
    }

    static bool append_offset_item(const char* label, uint32_t value,
                                   bool* first, std::string* out)
    {
        if (!label || !first || !out) {
            return false;
        }
        if (!*first) {
            out->append(", ");
        }
        *first = false;
        out->append(label);
        out->append(":");
        append_u64_dec(value, out);
        return true;
    }

    static IccTagInterpretStatus
    decode_lutab_summary(std::string_view type,
                         std::span<const std::byte> bytes,
                         const IccTagInterpretOptions& options,
                         IccTagInterpretation* out) noexcept
    {
        if (!out || bytes.size() < 32U) {
            return IccTagInterpretStatus::Malformed;
        }
        const uint8_t in_channels  = u8(bytes[8U]);
        const uint8_t out_channels = u8(bytes[9U]);
        if (in_channels == 0U || out_channels == 0U) {
            return IccTagInterpretStatus::Malformed;
        }

        uint32_t off_b      = 0;
        uint32_t off_matrix = 0;
        uint32_t off_m      = 0;
        uint32_t off_clut   = 0;
        uint32_t off_a      = 0;
        if (!read_u32be(bytes, 12U, &off_b)
            || !read_u32be(bytes, 16U, &off_matrix)
            || !read_u32be(bytes, 20U, &off_m)
            || !read_u32be(bytes, 24U, &off_clut)
            || !read_u32be(bytes, 28U, &off_a)) {
            return IccTagInterpretStatus::Malformed;
        }

        const uint32_t offsets[5] = { off_b, off_matrix, off_m, off_clut,
                                      off_a };
        for (uint32_t i = 0U; i < 5U; ++i) {
            if (offsets[i] == 0U) {
                continue;
            }
            if (offsets[i] < 32U || offsets[i] >= bytes.size()) {
                return IccTagInterpretStatus::Malformed;
            }
        }

        out->text.clear();
        out->text.append(type.data(), type.size());
        out->text.append(" in=");
        append_u64_dec(in_channels, &out->text);
        out->text.append(" out=");
        append_u64_dec(out_channels, &out->text);

        out->text.append(" blocks=");
        bool first_block = true;
        if (off_b != 0U) {
            if (!first_block) {
                out->text.push_back(',');
            }
            out->text.append("B");
            first_block = false;
        }
        if (off_matrix != 0U) {
            if (!first_block) {
                out->text.push_back(',');
            }
            out->text.append("matrix");
            first_block = false;
        }
        if (off_m != 0U) {
            if (!first_block) {
                out->text.push_back(',');
            }
            out->text.append("M");
            first_block = false;
        }
        if (off_clut != 0U) {
            if (!first_block) {
                out->text.push_back(',');
            }
            out->text.append("CLUT");
            first_block = false;
        }
        if (off_a != 0U) {
            if (!first_block) {
                out->text.push_back(',');
            }
            out->text.append("A");
            first_block = false;
        }
        if (first_block) {
            out->text.append("none");
        }

        out->text.append(" offs=");
        bool first_offset = true;
        if (off_b != 0U) {
            (void)append_offset_item("B", off_b, &first_offset, &out->text);
        }
        if (off_matrix != 0U) {
            (void)append_offset_item("matrix", off_matrix, &first_offset,
                                     &out->text);
        }
        if (off_m != 0U) {
            (void)append_offset_item("M", off_m, &first_offset, &out->text);
        }
        if (off_clut != 0U) {
            (void)append_offset_item("CLUT", off_clut, &first_offset,
                                     &out->text);
        }
        if (off_a != 0U) {
            (void)append_offset_item("A", off_a, &first_offset, &out->text);
        }
        if (first_offset) {
            out->text.append("none");
        }

        if (options.limits.max_text_bytes != 0U
            && out->text.size() > options.limits.max_text_bytes) {
            out->text.resize(options.limits.max_text_bytes);
            return IccTagInterpretStatus::LimitExceeded;
        }
        return IccTagInterpretStatus::Ok;
    }

    static const char* standard_illuminant_name(uint32_t code) noexcept
    {
        switch (code) {
        case 1U: return "D50";
        case 2U: return "D65";
        case 3U: return "D93";
        case 4U: return "F2";
        case 5U: return "D55";
        case 6U: return "A";
        case 7U: return "EquiPowerE";
        case 8U: return "F8";
        default: return nullptr;
        }
    }

    static const char* measurement_observer_name(uint32_t code) noexcept
    {
        switch (code) {
        case 1U: return "CIE1931_2deg";
        case 2U: return "CIE1964_10deg";
        default: return nullptr;
        }
    }

    static const char* measurement_geometry_name(uint32_t code) noexcept
    {
        switch (code) {
        case 1U: return "0_45_or_45_0";
        case 2U: return "0_d_or_d_0";
        default: return nullptr;
        }
    }

    static const char* chromaticity_colorant_name(uint32_t code) noexcept
    {
        switch (code) {
        case 1U: return "ITU-R_BT.709";
        case 2U: return "SMPTE_RP145";
        case 3U: return "EBU_Tech_3213";
        case 4U: return "P22";
        default: return nullptr;
        }
    }

    static bool push_limited_value(double value, uint32_t max_values,
                                   std::vector<double>* out,
                                   bool* truncated) noexcept
    {
        if (!out) {
            return false;
        }
        if (max_values != 0U && out->size() >= max_values) {
            if (truncated) {
                *truncated = true;
            }
            return true;
        }
        out->push_back(value);
        return true;
    }

    static void append_enum_label(const char* label, uint32_t code,
                                  const char* name, std::string* out)
    {
        if (!label || !out) {
            return;
        }
        if (!out->empty()) {
            out->push_back(' ');
        }
        out->append(label);
        out->push_back('=');
        append_u64_dec(code, out);
        if (name && name[0] != '\0') {
            out->push_back('(');
            out->append(name);
            out->push_back(')');
        }
    }

    static IccTagInterpretStatus
    decode_viewing_conditions(std::span<const std::byte> bytes,
                              const IccTagInterpretOptions& options,
                              IccTagInterpretation* out) noexcept
    {
        if (!out || bytes.size() < 36U) {
            return IccTagInterpretStatus::Malformed;
        }

        int32_t illum_x = 0;
        int32_t illum_y = 0;
        int32_t illum_z = 0;
        int32_t sur_x   = 0;
        int32_t sur_y   = 0;
        int32_t sur_z   = 0;
        uint32_t illum  = 0;
        if (!read_i32be(bytes, 8U, &illum_x)
            || !read_i32be(bytes, 12U, &illum_y)
            || !read_i32be(bytes, 16U, &illum_z)
            || !read_i32be(bytes, 20U, &sur_x)
            || !read_i32be(bytes, 24U, &sur_y)
            || !read_i32be(bytes, 28U, &sur_z)
            || !read_u32be(bytes, 32U, &illum)) {
            return IccTagInterpretStatus::Malformed;
        }

        bool truncated = false;
        out->values.clear();
        out->values.reserve(6U);
        (void)push_limited_value(static_cast<double>(illum_x) / 65536.0,
                                 options.limits.max_values, &out->values,
                                 &truncated);
        (void)push_limited_value(static_cast<double>(illum_y) / 65536.0,
                                 options.limits.max_values, &out->values,
                                 &truncated);
        (void)push_limited_value(static_cast<double>(illum_z) / 65536.0,
                                 options.limits.max_values, &out->values,
                                 &truncated);
        (void)push_limited_value(static_cast<double>(sur_x) / 65536.0,
                                 options.limits.max_values, &out->values,
                                 &truncated);
        (void)push_limited_value(static_cast<double>(sur_y) / 65536.0,
                                 options.limits.max_values, &out->values,
                                 &truncated);
        (void)push_limited_value(static_cast<double>(sur_z) / 65536.0,
                                 options.limits.max_values, &out->values,
                                 &truncated);

        if (out->values.size() == 6U) {
            out->rows = 2U;
            out->cols = 3U;
        } else {
            out->rows = 1U;
            out->cols = static_cast<uint32_t>(out->values.size());
        }

        out->text.clear();
        out->text.append("view");
        append_enum_label("illuminant_type", illum,
                          standard_illuminant_name(illum), &out->text);

        if (options.limits.max_text_bytes != 0U
            && out->text.size() > options.limits.max_text_bytes) {
            out->text.resize(options.limits.max_text_bytes);
            truncated = true;
        }
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    static IccTagInterpretStatus
    decode_measurement(std::span<const std::byte> bytes,
                       const IccTagInterpretOptions& options,
                       IccTagInterpretation* out) noexcept
    {
        if (!out || bytes.size() < 36U) {
            return IccTagInterpretStatus::Malformed;
        }

        uint32_t observer = 0;
        int32_t back_x    = 0;
        int32_t back_y    = 0;
        int32_t back_z    = 0;
        uint32_t geometry = 0;
        uint32_t flare    = 0;
        uint32_t illum    = 0;
        if (!read_u32be(bytes, 8U, &observer)
            || !read_i32be(bytes, 12U, &back_x)
            || !read_i32be(bytes, 16U, &back_y)
            || !read_i32be(bytes, 20U, &back_z)
            || !read_u32be(bytes, 24U, &geometry)
            || !read_u32be(bytes, 28U, &flare)
            || !read_u32be(bytes, 32U, &illum)) {
            return IccTagInterpretStatus::Malformed;
        }

        bool truncated = false;
        out->values.clear();
        out->values.reserve(4U);
        (void)push_limited_value(static_cast<double>(back_x) / 65536.0,
                                 options.limits.max_values, &out->values,
                                 &truncated);
        (void)push_limited_value(static_cast<double>(back_y) / 65536.0,
                                 options.limits.max_values, &out->values,
                                 &truncated);
        (void)push_limited_value(static_cast<double>(back_z) / 65536.0,
                                 options.limits.max_values, &out->values,
                                 &truncated);
        (void)push_limited_value(static_cast<double>(flare) / 65536.0,
                                 options.limits.max_values, &out->values,
                                 &truncated);
        out->rows = 1U;
        out->cols = static_cast<uint32_t>(out->values.size());

        out->text.clear();
        out->text.append("meas");
        append_enum_label("observer", observer,
                          measurement_observer_name(observer), &out->text);
        append_enum_label("geometry", geometry,
                          measurement_geometry_name(geometry), &out->text);
        append_enum_label("illuminant", illum, standard_illuminant_name(illum),
                          &out->text);

        if (options.limits.max_text_bytes != 0U
            && out->text.size() > options.limits.max_text_bytes) {
            out->text.resize(options.limits.max_text_bytes);
            truncated = true;
        }
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    static IccTagInterpretStatus
    decode_chromaticity(std::span<const std::byte> bytes,
                        const IccTagInterpretOptions& options,
                        IccTagInterpretation* out) noexcept
    {
        if (!out || bytes.size() < 12U) {
            return IccTagInterpretStatus::Malformed;
        }

        uint16_t channels = 0;
        uint16_t colorant = 0;
        if (!read_u16be(bytes, 8U, &channels)
            || !read_u16be(bytes, 10U, &colorant)) {
            return IccTagInterpretStatus::Malformed;
        }
        if (channels == 0U) {
            return IccTagInterpretStatus::Malformed;
        }

        const uint64_t pair_count = static_cast<uint64_t>(channels) * 2ULL;
        const uint64_t need       = 12ULL + pair_count * 4ULL;
        if (need > bytes.size()) {
            return IccTagInterpretStatus::Malformed;
        }

        bool truncated = false;
        out->values.clear();
        out->values.reserve(channels * 2U);
        for (uint32_t i = 0U; i < pair_count; ++i) {
            uint32_t fixed = 0;
            if (!read_u32be(bytes, 12ULL + static_cast<uint64_t>(i) * 4ULL,
                            &fixed)) {
                return IccTagInterpretStatus::Malformed;
            }
            (void)push_limited_value(static_cast<double>(fixed) / 65536.0,
                                     options.limits.max_values, &out->values,
                                     &truncated);
        }
        if (out->values.empty()) {
            return IccTagInterpretStatus::Malformed;
        }

        if ((out->values.size() % 2U) == 0U) {
            out->rows = static_cast<uint32_t>(out->values.size() / 2U);
            out->cols = 2U;
        } else {
            out->rows = 1U;
            out->cols = static_cast<uint32_t>(out->values.size());
        }

        out->text.clear();
        out->text.append("chrm");
        append_enum_label("channels", channels, nullptr, &out->text);
        append_enum_label("colorant", colorant,
                          chromaticity_colorant_name(colorant), &out->text);

        if (options.limits.max_text_bytes != 0U
            && out->text.size() > options.limits.max_text_bytes) {
            out->text.resize(options.limits.max_text_bytes);
            truncated = true;
        }
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    static uint32_t
    trim_trailing_nul_bytes(std::span<const std::byte> bytes) noexcept
    {
        uint32_t n = static_cast<uint32_t>(bytes.size());
        while (n > 0U && bytes[n - 1U] == std::byte { 0x00 }) {
            n -= 1U;
        }
        return n;
    }

    static bool append_utf8_codepoint(uint32_t cp, std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (cp <= 0x7FU) {
            out->push_back(static_cast<char>(cp));
            return true;
        }
        if (cp <= 0x7FFU) {
            out->push_back(static_cast<char>(0xC0U | ((cp >> 6) & 0x1FU)));
            out->push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            return true;
        }
        if (cp <= 0xFFFFU) {
            if (cp >= 0xD800U && cp <= 0xDFFFU) {
                return false;
            }
            out->push_back(static_cast<char>(0xE0U | ((cp >> 12) & 0x0FU)));
            out->push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
            out->push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            return true;
        }
        if (cp <= 0x10FFFFU) {
            out->push_back(static_cast<char>(0xF0U | ((cp >> 18) & 0x07U)));
            out->push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3FU)));
            out->push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
            out->push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            return true;
        }
        return false;
    }

    static bool decode_utf16be_to_utf8(std::span<const std::byte> bytes,
                                       uint32_t max_output_bytes,
                                       std::string* out,
                                       bool* truncated) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (truncated) {
            *truncated = false;
        }
        if ((bytes.size() % 2U) != 0U) {
            return false;
        }
        for (size_t off = 0U; off + 1U < bytes.size();) {
            uint16_t u = 0;
            if (!read_u16be(bytes, static_cast<uint64_t>(off), &u)) {
                return false;
            }
            off += 2U;

            uint32_t cp = static_cast<uint32_t>(u);
            if (u >= 0xD800U && u <= 0xDBFFU) {
                if (off + 1U >= bytes.size()) {
                    return false;
                }
                uint16_t lo = 0;
                if (!read_u16be(bytes, static_cast<uint64_t>(off), &lo)) {
                    return false;
                }
                if (lo < 0xDC00U || lo > 0xDFFFU) {
                    return false;
                }
                off += 2U;
                cp = 0x10000U
                     + (((static_cast<uint32_t>(u) - 0xD800U) << 10)
                        | (static_cast<uint32_t>(lo) - 0xDC00U));
            } else if (u >= 0xDC00U && u <= 0xDFFFU) {
                return false;
            }

            const size_t before = out->size();
            if (!append_utf8_codepoint(cp, out)) {
                return false;
            }
            if (max_output_bytes != 0U && out->size() > max_output_bytes) {
                out->resize(before);
                if (truncated) {
                    *truncated = true;
                }
                break;
            }
        }
        return true;
    }

    static void append_double_fixed6_trim(double d, std::string* out)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%.6f", d);
        size_t len = std::strlen(buf);
        while (len > 0U && buf[len - 1U] == '0') {
            len -= 1U;
        }
        if (len > 0U && buf[len - 1U] == '.') {
            len -= 1U;
        }
        out->append(buf, len);
    }

    static void append_interpreted_values_text(const IccTagInterpretation& in,
                                               uint32_t max_values,
                                               std::string* out)
    {
        if (!out) {
            return;
        }
        out->clear();
        if (in.values.empty()) {
            return;
        }
        if (in.rows > 1U && in.cols > 0U) {
            char dims[32];
            std::snprintf(dims, sizeof(dims), "%ux%u ",
                          static_cast<unsigned>(in.rows),
                          static_cast<unsigned>(in.cols));
            out->append(dims);
        }
        out->push_back('[');
        const uint32_t n     = static_cast<uint32_t>(in.values.size());
        const uint32_t shown = (max_values != 0U && n > max_values) ? max_values
                                                                    : n;
        for (uint32_t i = 0; i < shown; ++i) {
            if (i != 0U) {
                out->append(", ");
            }
            append_double_fixed6_trim(in.values[i], out);
        }
        if (shown < n) {
            out->append(", ...");
        }
        out->push_back(']');
    }

    static uint32_t parametric_coefficient_count(uint16_t fn) noexcept
    {
        switch (fn) {
        case 0: return 1U;
        case 1: return 3U;
        case 2: return 4U;
        case 3: return 5U;
        case 4: return 7U;
        default: return 0U;
        }
    }

}  // namespace


std::string_view
icc_tag_name(uint32_t signature) noexcept
{
    switch (signature) {
    case fourcc('A', '2', 'B', '0'): return "A2B0";
    case fourcc('A', '2', 'B', '1'): return "A2B1";
    case fourcc('A', '2', 'B', '2'): return "A2B2";
    case fourcc('B', '2', 'A', '0'): return "B2A0";
    case fourcc('B', '2', 'A', '1'): return "B2A1";
    case fourcc('B', '2', 'A', '2'): return "B2A2";
    case fourcc('b', 'X', 'Y', 'Z'): return "BlueMatrixColumn";
    case fourcc('b', 'T', 'R', 'C'): return "BlueTRC";
    case fourcc('b', 'k', 'p', 't'): return "MediaBlackPoint";
    case fourcc('c', 'h', 'a', 'd'): return "ChromaticAdaptation";
    case fourcc('c', 'h', 'r', 'm'): return "Chromaticity";
    case fourcc('c', 'p', 'r', 't'): return "Copyright";
    case fourcc('d', 'e', 's', 'c'): return "ProfileDescription";
    case fourcc('d', 'm', 'd', 'd'): return "DeviceModelDesc";
    case fourcc('d', 'm', 'n', 'd'): return "DeviceMfgDesc";
    case fourcc('g', 'a', 'm', 't'): return "Gamut";
    case fourcc('g', 'X', 'Y', 'Z'): return "GreenMatrixColumn";
    case fourcc('g', 'T', 'R', 'C'): return "GreenTRC";
    case fourcc('k', 'T', 'R', 'C'): return "GrayTRC";
    case fourcc('l', 'u', 'm', 'i'): return "Luminance";
    case fourcc('m', 'e', 'a', 's'): return "Measurement";
    case fourcc('n', 'c', 'l', '2'): return "NamedColor2";
    case fourcc('r', 'e', 's', 'p'): return "OutputResponse";
    case fourcc('r', 'X', 'Y', 'Z'): return "RedMatrixColumn";
    case fourcc('r', 'T', 'R', 'C'): return "RedTRC";
    case fourcc('t', 'a', 'r', 'g'): return "CharTarget";
    case fourcc('t', 'e', 'c', 'h'): return "Technology";
    case fourcc('v', 'i', 'e', 'w'): return "ViewingConditions";
    case fourcc('w', 't', 'p', 't'): return "MediaWhitePoint";
    default: return {};
    }
}


IccTagInterpretStatus
interpret_icc_tag(uint32_t signature, std::span<const std::byte> tag_bytes,
                  IccTagInterpretation* out,
                  const IccTagInterpretOptions& options) noexcept
{
    if (!out) {
        return IccTagInterpretStatus::Malformed;
    }
    *out           = IccTagInterpretation {};
    out->signature = signature;
    out->name      = icc_tag_name(signature);

    if (tag_bytes.size() < 8U) {
        return IccTagInterpretStatus::Malformed;
    }

    uint32_t type_sig = 0;
    if (!read_u32be(tag_bytes, 0, &type_sig)) {
        return IccTagInterpretStatus::Malformed;
    }
    fourcc_to_string(type_sig, &out->type);

    if (type_sig == fourcc('d', 'e', 's', 'c')) {
        if (tag_bytes.size() < 12U) {
            return IccTagInterpretStatus::Malformed;
        }
        uint32_t n = 0;
        if (!read_u32be(tag_bytes, 8, &n)) {
            return IccTagInterpretStatus::Malformed;
        }
        uint32_t text_len = (n > 0U) ? (n - 1U) : 0U;
        bool truncated    = false;
        if (options.limits.max_text_bytes != 0U
            && text_len > options.limits.max_text_bytes) {
            text_len  = options.limits.max_text_bytes;
            truncated = true;
        }
        if (12ULL + static_cast<uint64_t>(text_len) > tag_bytes.size()) {
            return IccTagInterpretStatus::Malformed;
        }
        out->text.assign(reinterpret_cast<const char*>(tag_bytes.data() + 12U),
                         text_len);
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('t', 'e', 'x', 't')) {
        if (tag_bytes.size() < 8U) {
            return IccTagInterpretStatus::Malformed;
        }
        const std::span<const std::byte> payload = tag_bytes.subspan(8U);
        uint32_t text_len = trim_trailing_nul_bytes(payload);
        bool truncated    = false;
        if (options.limits.max_text_bytes != 0U
            && text_len > options.limits.max_text_bytes) {
            text_len  = options.limits.max_text_bytes;
            truncated = true;
        }
        out->text.assign(reinterpret_cast<const char*>(payload.data()),
                         text_len);
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('s', 'i', 'g', ' ')) {
        if (tag_bytes.size() < 12U) {
            return IccTagInterpretStatus::Malformed;
        }
        uint32_t sig = 0;
        if (!read_u32be(tag_bytes, 8U, &sig)) {
            return IccTagInterpretStatus::Malformed;
        }
        fourcc_to_string(sig, &out->text);
        return IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('d', 'a', 't', 'a')) {
        if (tag_bytes.size() < 12U) {
            return IccTagInterpretStatus::Malformed;
        }
        uint32_t flags = 0;
        if (!read_u32be(tag_bytes, 8U, &flags)) {
            return IccTagInterpretStatus::Malformed;
        }
        const std::span<const std::byte> payload = tag_bytes.subspan(12U);
        bool truncated                           = false;

        if (flags == 0U) {
            uint32_t text_len = trim_trailing_nul_bytes(payload);
            bool printable    = true;
            for (uint32_t i = 0U; i < text_len; ++i) {
                const unsigned char c = static_cast<unsigned char>(payload[i]);
                if (c < 0x20U || c > 0x7EU) {
                    printable = false;
                    break;
                }
            }
            if (printable) {
                if (options.limits.max_text_bytes != 0U
                    && text_len > options.limits.max_text_bytes) {
                    text_len  = options.limits.max_text_bytes;
                    truncated = true;
                }
                out->text.assign(reinterpret_cast<const char*>(payload.data()),
                                 text_len);
                return truncated ? IccTagInterpretStatus::LimitExceeded
                                 : IccTagInterpretStatus::Ok;
            }
        }

        out->text.clear();
        out->text.append("data flags=");
        append_u64_dec(flags, &out->text);
        out->text.append(" bytes=");
        append_u64_dec(static_cast<uint64_t>(payload.size()), &out->text);
        if (flags == 0U) {
            out->text.append(" non_ascii");
        }
        if (options.limits.max_text_bytes != 0U
            && out->text.size() > options.limits.max_text_bytes) {
            out->text.resize(options.limits.max_text_bytes);
            return IccTagInterpretStatus::LimitExceeded;
        }
        return IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('n', 'c', 'l', '2')) {
        if (tag_bytes.size() < 84U) {
            return IccTagInterpretStatus::Malformed;
        }
        uint32_t named_color_count = 0U;
        uint32_t device_coords     = 0U;
        if (!read_u32be(tag_bytes, 12U, &named_color_count)
            || !read_u32be(tag_bytes, 16U, &device_coords)) {
            return IccTagInterpretStatus::Malformed;
        }

        uint64_t device_bytes = 0U;
        uint64_t entry_size   = 0U;
        uint64_t table_bytes  = 0U;
        uint64_t need         = 0U;
        if (!checked_mul_u64(static_cast<uint64_t>(device_coords), 2U,
                             &device_bytes)
            || !checked_add_u64(38U, device_bytes, &entry_size)
            || !checked_mul_u64(static_cast<uint64_t>(named_color_count),
                                entry_size, &table_bytes)
            || !checked_add_u64(84U, table_bytes, &need)) {
            return IccTagInterpretStatus::Malformed;
        }
        if (need > tag_bytes.size()) {
            return IccTagInterpretStatus::Malformed;
        }

        out->text.clear();
        out->text.append("ncl2 count=");
        append_u64_dec(named_color_count, &out->text);
        out->text.append(" device_coords=");
        append_u64_dec(device_coords, &out->text);

        if (named_color_count > 0U) {
            const std::span<const std::byte> root_name = tag_bytes.subspan(84U,
                                                                           32U);
            uint32_t name_len = trim_trailing_nul_bytes(root_name);
            bool printable    = true;
            for (uint32_t i = 0U; i < name_len; ++i) {
                const unsigned char c = static_cast<unsigned char>(
                    root_name[i]);
                if (c < 0x20U || c > 0x7EU) {
                    printable = false;
                    break;
                }
            }
            if (printable && name_len != 0U) {
                out->text.append(" first=\"");
                out->text.append(reinterpret_cast<const char*>(root_name.data()),
                                 name_len);
                out->text.push_back('"');
            }
        }
        if (options.limits.max_text_bytes != 0U
            && out->text.size() > options.limits.max_text_bytes) {
            out->text.resize(options.limits.max_text_bytes);
            return IccTagInterpretStatus::LimitExceeded;
        }
        return IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('d', 't', 'i', 'm')) {
        if (tag_bytes.size() < 20U) {
            return IccTagInterpretStatus::Malformed;
        }
        uint16_t year   = 0;
        uint16_t month  = 0;
        uint16_t day    = 0;
        uint16_t hour   = 0;
        uint16_t minute = 0;
        uint16_t second = 0;
        if (!read_u16be(tag_bytes, 8U, &year)
            || !read_u16be(tag_bytes, 10U, &month)
            || !read_u16be(tag_bytes, 12U, &day)
            || !read_u16be(tag_bytes, 14U, &hour)
            || !read_u16be(tag_bytes, 16U, &minute)
            || !read_u16be(tag_bytes, 18U, &second)) {
            return IccTagInterpretStatus::Malformed;
        }
        if (month < 1U || month > 12U || day < 1U || day > 31U || hour > 23U
            || minute > 59U || second > 59U) {
            return IccTagInterpretStatus::Malformed;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u:%02u:%02u",
                      static_cast<unsigned>(year), static_cast<unsigned>(month),
                      static_cast<unsigned>(day), static_cast<unsigned>(hour),
                      static_cast<unsigned>(minute),
                      static_cast<unsigned>(second));
        out->text = buf;
        if (options.limits.max_text_bytes != 0U
            && out->text.size() > options.limits.max_text_bytes) {
            out->text.resize(options.limits.max_text_bytes);
            return IccTagInterpretStatus::LimitExceeded;
        }
        return IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('v', 'i', 'e', 'w')) {
        return decode_viewing_conditions(tag_bytes, options, out);
    }

    if (type_sig == fourcc('m', 'e', 'a', 's')) {
        return decode_measurement(tag_bytes, options, out);
    }

    if (type_sig == fourcc('c', 'h', 'r', 'm')) {
        return decode_chromaticity(tag_bytes, options, out);
    }

    if (type_sig == fourcc('m', 'l', 'u', 'c')) {
        if (tag_bytes.size() < 16U) {
            return IccTagInterpretStatus::Malformed;
        }
        uint32_t rec_count = 0;
        uint32_t rec_size  = 0;
        if (!read_u32be(tag_bytes, 8U, &rec_count)
            || !read_u32be(tag_bytes, 12U, &rec_size)) {
            return IccTagInterpretStatus::Malformed;
        }
        if (rec_count == 0U || rec_size < 12U) {
            return IccTagInterpretStatus::Malformed;
        }
        const uint64_t table_bytes = static_cast<uint64_t>(rec_count)
                                     * static_cast<uint64_t>(rec_size);
        if (16ULL + table_bytes > tag_bytes.size()) {
            return IccTagInterpretStatus::Malformed;
        }

        bool have_selected    = false;
        bool selected_is_en   = false;
        uint32_t selected_off = 0U;
        uint32_t selected_len = 0U;

        for (uint32_t i = 0; i < rec_count; ++i) {
            const uint64_t rec_off = 16ULL
                                     + static_cast<uint64_t>(i)
                                           * static_cast<uint64_t>(rec_size);
            if (rec_off + 12ULL > tag_bytes.size()) {
                return IccTagInterpretStatus::Malformed;
            }

            const uint8_t lang0    = u8(tag_bytes[rec_off + 0U]);
            const uint8_t lang1    = u8(tag_bytes[rec_off + 1U]);
            const uint8_t country0 = u8(tag_bytes[rec_off + 2U]);
            const uint8_t country1 = u8(tag_bytes[rec_off + 3U]);

            uint32_t rec_len      = 0;
            uint32_t rec_data_off = 0;
            if (!read_u32be(tag_bytes, rec_off + 4U, &rec_len)
                || !read_u32be(tag_bytes, rec_off + 8U, &rec_data_off)) {
                return IccTagInterpretStatus::Malformed;
            }
            if (rec_len == 0U || (rec_len % 2U) != 0U) {
                continue;
            }
            if (static_cast<uint64_t>(rec_data_off)
                    + static_cast<uint64_t>(rec_len)
                > tag_bytes.size()) {
                continue;
            }

            const bool is_en = (lang0 == static_cast<uint8_t>('e')
                                && lang1 == static_cast<uint8_t>('n')
                                && country0 == static_cast<uint8_t>('U')
                                && country1 == static_cast<uint8_t>('S'));
            if (!have_selected || (is_en && !selected_is_en)) {
                selected_off   = rec_data_off;
                selected_len   = rec_len;
                selected_is_en = is_en;
                have_selected  = true;
            }
        }

        if (!have_selected) {
            return IccTagInterpretStatus::Malformed;
        }
        bool truncated = false;
        if (!decode_utf16be_to_utf8(tag_bytes.subspan(selected_off,
                                                      selected_len),
                                    options.limits.max_text_bytes, &out->text,
                                    &truncated)) {
            return IccTagInterpretStatus::Malformed;
        }
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('s', 'f', '3', '2')) {
        bool truncated = false;
        if (!append_fixed_array_values(tag_bytes, options.limits.max_values,
                                       true, &out->values, &truncated)) {
            return IccTagInterpretStatus::Malformed;
        }
        out->rows = 1U;
        out->cols = static_cast<uint32_t>(out->values.size());
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('u', 'f', '3', '2')) {
        bool truncated = false;
        if (!append_fixed_array_values(tag_bytes, options.limits.max_values,
                                       false, &out->values, &truncated)) {
            return IccTagInterpretStatus::Malformed;
        }
        out->rows = 1U;
        out->cols = static_cast<uint32_t>(out->values.size());
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('u', 'i', '0', '8')) {
        bool truncated = false;
        if (!append_unsigned_integer_array_values(tag_bytes, 1U,
                                                  options.limits.max_values,
                                                  &out->values, &truncated)) {
            return IccTagInterpretStatus::Malformed;
        }
        out->rows = 1U;
        out->cols = static_cast<uint32_t>(out->values.size());
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('u', 'i', '1', '6')) {
        bool truncated = false;
        if (!append_unsigned_integer_array_values(tag_bytes, 2U,
                                                  options.limits.max_values,
                                                  &out->values, &truncated)) {
            return IccTagInterpretStatus::Malformed;
        }
        out->rows = 1U;
        out->cols = static_cast<uint32_t>(out->values.size());
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('u', 'i', '3', '2')) {
        bool truncated = false;
        if (!append_unsigned_integer_array_values(tag_bytes, 4U,
                                                  options.limits.max_values,
                                                  &out->values, &truncated)) {
            return IccTagInterpretStatus::Malformed;
        }
        out->rows = 1U;
        out->cols = static_cast<uint32_t>(out->values.size());
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('m', 'f', 't', '1')) {
        return decode_lut8_summary(tag_bytes, options, out);
    }

    if (type_sig == fourcc('m', 'f', 't', '2')) {
        return decode_lut16_summary(tag_bytes, options, out);
    }

    if (type_sig == fourcc('m', 'A', 'B', ' ')) {
        return decode_lutab_summary("mAB", tag_bytes, options, out);
    }

    if (type_sig == fourcc('m', 'B', 'A', ' ')) {
        return decode_lutab_summary("mBA", tag_bytes, options, out);
    }

    if (type_sig == fourcc('X', 'Y', 'Z', ' ')) {
        if (tag_bytes.size() < 20U) {
            return IccTagInterpretStatus::Malformed;
        }
        const uint64_t payload = tag_bytes.size() - 8U;
        if ((payload % 12ULL) != 0ULL) {
            return IccTagInterpretStatus::Malformed;
        }
        const uint32_t triplets = static_cast<uint32_t>(payload / 12ULL);
        bool truncated          = false;
        if (!append_xyz_values(tag_bytes, triplets, options.limits.max_values,
                               &out->values, &truncated)) {
            return IccTagInterpretStatus::Malformed;
        }
        out->rows = triplets;
        out->cols = 3U;
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('c', 'u', 'r', 'v')) {
        if (tag_bytes.size() < 12U) {
            return IccTagInterpretStatus::Malformed;
        }
        uint32_t count = 0;
        if (!read_u32be(tag_bytes, 8, &count)) {
            return IccTagInterpretStatus::Malformed;
        }
        const uint64_t need = 12ULL + static_cast<uint64_t>(count) * 2ULL;
        if (need > tag_bytes.size()) {
            return IccTagInterpretStatus::Malformed;
        }
        bool truncated = false;
        if (!append_curve_values(tag_bytes, count, options.limits.max_values,
                                 &out->values, &truncated)) {
            return IccTagInterpretStatus::Malformed;
        }
        out->rows = 1U;
        out->cols = static_cast<uint32_t>(out->values.size());
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    if (type_sig == fourcc('p', 'a', 'r', 'a')) {
        if (tag_bytes.size() < 12U) {
            return IccTagInterpretStatus::Malformed;
        }
        uint16_t fn = 0;
        if (!read_u16be(tag_bytes, 8, &fn)) {
            return IccTagInterpretStatus::Malformed;
        }
        const uint32_t coeff_count = parametric_coefficient_count(fn);
        if (coeff_count == 0U) {
            return IccTagInterpretStatus::Unsupported;
        }
        const uint64_t need = 12ULL + static_cast<uint64_t>(coeff_count) * 4ULL;
        if (need > tag_bytes.size()) {
            return IccTagInterpretStatus::Malformed;
        }
        bool truncated = false;
        uint32_t limit = coeff_count;
        if (options.limits.max_values != 0U
            && limit > options.limits.max_values) {
            limit     = options.limits.max_values;
            truncated = true;
        }
        out->values.reserve(limit);
        for (uint32_t i = 0; i < limit; ++i) {
            int32_t s15 = 0;
            if (!read_i32be(tag_bytes, 12ULL + static_cast<uint64_t>(i) * 4ULL,
                            &s15)) {
                return IccTagInterpretStatus::Malformed;
            }
            out->values.push_back(static_cast<double>(s15) / 65536.0);
        }
        out->rows = 1U;
        out->cols = static_cast<uint32_t>(out->values.size());
        char buf[32];
        std::snprintf(buf, sizeof(buf), "para(f=%u)",
                      static_cast<unsigned>(fn));
        out->type = buf;
        return truncated ? IccTagInterpretStatus::LimitExceeded
                         : IccTagInterpretStatus::Ok;
    }

    return IccTagInterpretStatus::Unsupported;
}

bool
format_icc_tag_display_value(uint32_t signature,
                             std::span<const std::byte> tag_bytes,
                             uint32_t max_values, uint32_t max_text_bytes,
                             std::string* out) noexcept
{
    if (!out) {
        return false;
    }
    out->clear();
    IccTagInterpretOptions options;
    options.limits.max_values     = max_values;
    options.limits.max_text_bytes = max_text_bytes;

    IccTagInterpretation interpretation;
    const IccTagInterpretStatus status
        = interpret_icc_tag(signature, tag_bytes, &interpretation, options);
    if (status != IccTagInterpretStatus::Ok
        && status != IccTagInterpretStatus::LimitExceeded) {
        return false;
    }

    if (!interpretation.text.empty()) {
        out->assign(interpretation.text);
        return true;
    }
    if (!interpretation.values.empty()) {
        append_interpreted_values_text(interpretation, max_values, out);
        return true;
    }
    return false;
}

}  // namespace openmeta
