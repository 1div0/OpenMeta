// SPDX-License-Identifier: Apache-2.0

#include "openmeta/photoshop_irb_decode.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

namespace openmeta {
namespace {

    static uint8_t u8(std::byte b) noexcept { return static_cast<uint8_t>(b); }


    static bool match(std::span<const std::byte> bytes, uint64_t off,
                      const char* s, uint64_t n) noexcept
    {
        if (!s || off + n > bytes.size()) {
            return false;
        }
        for (uint64_t i = 0; i < n; ++i) {
            if (u8(bytes[off + i]) != static_cast<uint8_t>(s[i])) {
                return false;
            }
        }
        return true;
    }


    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset]) << 8)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]));
        *out = v;
        return true;
    }


    static bool read_i16be(std::span<const std::byte> bytes, uint64_t offset,
                           int16_t* out) noexcept
    {
        uint16_t raw = 0;
        if (!out || !read_u16be(bytes, offset, &raw)) {
            return false;
        }
        *out = static_cast<int16_t>(raw);
        return true;
    }


    static bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4 > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 0])) << 24;
        v |= static_cast<uint32_t>(u8(bytes[offset + 1])) << 16;
        v |= static_cast<uint32_t>(u8(bytes[offset + 2])) << 8;
        v |= static_cast<uint32_t>(u8(bytes[offset + 3])) << 0;
        *out = v;
        return true;
    }


    static bool read_i32be(std::span<const std::byte> bytes, uint64_t offset,
                           int32_t* out) noexcept
    {
        uint32_t raw = 0;
        if (!out || !read_u32be(bytes, offset, &raw)) {
            return false;
        }
        if (raw <= 0x7FFFFFFFU) {
            *out = static_cast<int32_t>(raw);
        } else {
            *out = -1 - static_cast<int32_t>(~raw);
        }
        return true;
    }


    static bool read_u64be(std::span<const std::byte> bytes, uint64_t offset,
                           uint64_t* out) noexcept
    {
        if (!out || offset + 8 > bytes.size()) {
            return false;
        }
        uint64_t v = 0;
        v |= static_cast<uint64_t>(u8(bytes[offset + 0])) << 56;
        v |= static_cast<uint64_t>(u8(bytes[offset + 1])) << 48;
        v |= static_cast<uint64_t>(u8(bytes[offset + 2])) << 40;
        v |= static_cast<uint64_t>(u8(bytes[offset + 3])) << 32;
        v |= static_cast<uint64_t>(u8(bytes[offset + 4])) << 24;
        v |= static_cast<uint64_t>(u8(bytes[offset + 5])) << 16;
        v |= static_cast<uint64_t>(u8(bytes[offset + 6])) << 8;
        v |= static_cast<uint64_t>(u8(bytes[offset + 7])) << 0;
        *out = v;
        return true;
    }


    static bool read_f32be_bits(std::span<const std::byte> bytes,
                                uint64_t offset, uint32_t* out) noexcept
    {
        return read_u32be(bytes, offset, out);
    }


    static bool append_utf8_codepoint(uint32_t cp, std::string* out) noexcept
    {
        if (!out || cp > 0x10FFFFU) {
            return false;
        }
        if (cp <= 0x7FU) {
            out->push_back(static_cast<char>(cp));
            return true;
        }
        if (cp <= 0x7FFU) {
            out->push_back(static_cast<char>(0xC0U | (cp >> 6)));
            out->push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            return true;
        }
        if (cp <= 0xFFFFU) {
            if (cp >= 0xD800U && cp <= 0xDFFFU) {
                return false;
            }
            out->push_back(static_cast<char>(0xE0U | (cp >> 12)));
            out->push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
            out->push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            return true;
        }
        out->push_back(static_cast<char>(0xF0U | (cp >> 18)));
        out->push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3FU)));
        out->push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
        out->push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
        return true;
    }


    static bool decode_utf16be_to_utf8(std::span<const std::byte> bytes,
                                       std::string* out) noexcept
    {
        if (!out || (bytes.size() % 2U) != 0U) {
            return false;
        }
        out->clear();
        out->reserve(bytes.size());
        for (size_t off = 0U; off + 1U < bytes.size();) {
            uint16_t hi = 0;
            if (!read_u16be(bytes, static_cast<uint64_t>(off), &hi)) {
                return false;
            }
            off += 2U;

            uint32_t cp = static_cast<uint32_t>(hi);
            if (hi >= 0xD800U && hi <= 0xDBFFU) {
                if (off + 1U >= bytes.size()) {
                    return false;
                }
                uint16_t lo = 0;
                if (!read_u16be(bytes, static_cast<uint64_t>(off), &lo)
                    || lo < 0xDC00U || lo > 0xDFFFU) {
                    return false;
                }
                off += 2U;
                cp = 0x10000U
                     + (((static_cast<uint32_t>(hi) - 0xD800U) << 10)
                        | (static_cast<uint32_t>(lo) - 0xDC00U));
            } else if (hi >= 0xDC00U && hi <= 0xDFFFU) {
                return false;
            }
            if (!append_utf8_codepoint(cp, out)) {
                return false;
            }
        }
        return true;
    }


    static bool read_var_ustr32_utf8(std::span<const std::byte> bytes,
                                     uint64_t* io_offset,
                                     std::string* out) noexcept
    {
        if (!io_offset || !out) {
            return false;
        }
        uint32_t code_unit_count = 0;
        if (!read_u32be(bytes, *io_offset, &code_unit_count)) {
            return false;
        }
        const uint64_t text_offset = *io_offset + 4U;
        const uint64_t byte_count = static_cast<uint64_t>(code_unit_count) * 2U;
        if (text_offset > bytes.size() || byte_count > bytes.size()
            || text_offset + byte_count > bytes.size()) {
            return false;
        }
        if (!decode_utf16be_to_utf8(
                bytes.subspan(static_cast<size_t>(text_offset),
                              static_cast<size_t>(byte_count)),
                out)) {
            return false;
        }
        *io_offset = text_offset + byte_count;
        return true;
    }


    static uint64_t f64_bits(double v) noexcept
    {
        uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        return bits;
    }


    static uint64_t pad2(uint64_t n) noexcept { return (n + 1U) & ~1ULL; }


    static size_t
    find_trailing_zero_padding_start(std::span<const std::byte> bytes) noexcept
    {
        size_t end = bytes.size();
        while (end > 0U && u8(bytes[end - 1U]) == 0U) {
            end -= 1U;
        }
        return end;
    }


    static void append_hex_lower(std::span<const std::byte> bytes,
                                 std::string* out)
    {
        static constexpr char kHex[] = "0123456789abcdef";
        if (!out) {
            return;
        }
        out->clear();
        out->reserve(bytes.size() * 2U);
        for (size_t i = 0; i < bytes.size(); ++i) {
            const uint8_t v = u8(bytes[i]);
            out->push_back(kHex[(v >> 4) & 0x0FU]);
            out->push_back(kHex[(v >> 0) & 0x0FU]);
        }
    }


    static void emit_derived_field(MetaStore& store, BlockId block,
                                   uint32_t order, uint16_t resource_id,
                                   std::string_view field,
                                   const MetaValue& value,
                                   PhotoshopIrbDecodeResult* result) noexcept
    {
        Entry entry;
        entry.key   = make_photoshop_irb_field_key(store.arena(), resource_id,
                                                   field);
        entry.value = value;
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = 1;
        entry.flags                 = EntryFlags::Derived;
        (void)store.add_entry(entry);
        if (result) {
            result->entries_decoded += 1U;
        }
    }


    static void decode_resolution_info(std::span<const std::byte> payload,
                                       MetaStore& store, BlockId block,
                                       uint32_t order,
                                       PhotoshopIrbDecodeResult* result)
    {
        uint32_t x_fixed = 0;
        uint32_t y_fixed = 0;
        uint16_t units_x = 0;
        uint16_t units_y = 0;
        if (!read_u32be(payload, 0, &x_fixed)
            || !read_u16be(payload, 4, &units_x)
            || !read_u32be(payload, 8, &y_fixed)
            || !read_u16be(payload, 12, &units_y)) {
            return;
        }

        const double x = static_cast<double>(x_fixed) / 65536.0;
        const double y = static_cast<double>(y_fixed) / 65536.0;
        emit_derived_field(store, block, order, 0x03EDU, "XResolution",
                           make_f64_bits(f64_bits(x)), result);
        emit_derived_field(store, block, order, 0x03EDU, "DisplayedUnitsX",
                           make_u16(units_x), result);
        emit_derived_field(store, block, order, 0x03EDU, "YResolution",
                           make_f64_bits(f64_bits(y)), result);
        emit_derived_field(store, block, order, 0x03EDU, "DisplayedUnitsY",
                           make_u16(units_y), result);
    }


    static void decode_iptc_digest(std::span<const std::byte> payload,
                                   MetaStore& store, BlockId block,
                                   uint32_t order,
                                   PhotoshopIrbDecodeResult* result)
    {
        std::string hex;
        append_hex_lower(payload, &hex);
        emit_derived_field(store, block, order, 0x0425U, "IPTCDigest",
                           make_text(store.arena(), hex, TextEncoding::Ascii),
                           result);
    }


    static void decode_photoshop2_info(std::span<const std::byte> payload,
                                       MetaStore& store, BlockId block,
                                       uint32_t order,
                                       PhotoshopIrbDecodeResult* result)
    {
        uint16_t channels = 0;
        uint16_t rows     = 0;
        uint16_t columns  = 0;
        uint16_t depth    = 0;
        uint16_t mode     = 0;
        if (!read_u16be(payload, 0U, &channels)
            || !read_u16be(payload, 2U, &rows)
            || !read_u16be(payload, 4U, &columns)
            || !read_u16be(payload, 6U, &depth)
            || !read_u16be(payload, 8U, &mode)) {
            return;
        }
        emit_derived_field(store, block, order, 0x03E8U,
                           "Photoshop2ChannelCount", make_u16(channels),
                           result);
        emit_derived_field(store, block, order, 0x03E8U, "Photoshop2Rows",
                           make_u16(rows), result);
        emit_derived_field(store, block, order, 0x03E8U, "Photoshop2Columns",
                           make_u16(columns), result);
        emit_derived_field(store, block, order, 0x03E8U, "Photoshop2Depth",
                           make_u16(depth), result);
        emit_derived_field(store, block, order, 0x03E8U, "Photoshop2Mode",
                           make_u16(mode), result);
    }


    static void decode_photoshop2_color_table(
        std::span<const std::byte> payload, MetaStore& store, BlockId block,
        uint32_t order, PhotoshopIrbDecodeResult* result)
    {
        if (payload.empty()) {
            return;
        }
        const uint64_t size_u64 = payload.size();
        const uint32_t size     = (size_u64 > UINT32_MAX)
                                      ? UINT32_MAX
                                      : static_cast<uint32_t>(size_u64);
        emit_derived_field(store, block, order, 0x03EBU,
                           "Photoshop2ColorTableBytes", make_u32(size), result);
        if ((payload.size() % 3U) == 0U) {
            const uint64_t count_u64 = payload.size() / 3U;
            const uint32_t count     = (count_u64 > UINT32_MAX)
                                           ? UINT32_MAX
                                           : static_cast<uint32_t>(count_u64);
            emit_derived_field(store, block, order, 0x03EBU,
                               "Photoshop2ColorCount", make_u32(count), result);
        }
    }


    static void decode_u8_scalar_resource(std::span<const std::byte> payload,
                                          uint16_t resource_id,
                                          std::string_view field,
                                          MetaStore& store, BlockId block,
                                          uint32_t order,
                                          PhotoshopIrbDecodeResult* result)
    {
        if (payload.empty()) {
            return;
        }
        emit_derived_field(store, block, order, resource_id, field,
                           make_u8(u8(payload[0])), result);
    }


    static void decode_print_flags(std::span<const std::byte> payload,
                                   MetaStore& store, BlockId block,
                                   uint32_t order,
                                   PhotoshopIrbDecodeResult* result)
    {
        static constexpr const char* kFlagNames[] = {
            "PrintFlagLabels",      "PrintFlagCornerCropMarks",
            "PrintFlagColorBars",   "PrintFlagRegistrationMarks",
            "PrintFlagNegative",    "PrintFlagEmulsionDown",
            "PrintFlagInterpolate", "PrintFlagDescription",
            "PrintFlagPrintFlags",
        };
        if (payload.empty()) {
            return;
        }
        emit_derived_field(store, block, order, 0x03F3U, "PrintFlags",
                           make_u8(u8(payload[0])), result);
        const size_t field_count = sizeof(kFlagNames) / sizeof(kFlagNames[0]);
        const size_t emit_count  = (payload.size() < field_count)
                                       ? payload.size()
                                       : field_count;
        for (size_t i = 0U; i < emit_count; ++i) {
            emit_derived_field(store, block, order, 0x03F3U, kFlagNames[i],
                               make_u8(u8(payload[i])), result);
        }
    }


    static void decode_u32_scalar_resource(std::span<const std::byte> payload,
                                           uint16_t resource_id,
                                           std::string_view field,
                                           MetaStore& store, BlockId block,
                                           uint32_t order,
                                           PhotoshopIrbDecodeResult* result)
    {
        uint32_t value = 0;
        if (!read_u32be(payload, 0, &value)) {
            return;
        }
        emit_derived_field(store, block, order, resource_id, field,
                           make_u32(value), result);
    }


    static void decode_payload_size_resource(std::span<const std::byte> payload,
                                             uint16_t resource_id,
                                             std::string_view field,
                                             MetaStore& store, BlockId block,
                                             uint32_t order,
                                             PhotoshopIrbDecodeResult* result)
    {
        const uint64_t size    = payload.size();
        const uint32_t clamped = (size > UINT32_MAX)
                                     ? UINT32_MAX
                                     : static_cast<uint32_t>(size);
        emit_derived_field(store, block, order, resource_id, field,
                           make_u32(clamped), result);
    }


    static void decode_spot_halftone(std::span<const std::byte> payload,
                                     MetaStore& store, BlockId block,
                                     uint32_t order,
                                     PhotoshopIrbDecodeResult* result)
    {
        uint32_t version        = 0;
        uint32_t declared_bytes = 0;
        if (!read_u32be(payload, 0U, &version)
            || !read_u32be(payload, 4U, &declared_bytes)) {
            return;
        }
        const uint64_t data_bytes_u64 = payload.size() - 8U;
        const uint32_t data_bytes     = (data_bytes_u64 > UINT32_MAX)
                                            ? UINT32_MAX
                                            : static_cast<uint32_t>(data_bytes_u64);
        emit_derived_field(store, block, order, 0x0413U, "SpotHalftoneVersion",
                           make_u32(version), result);
        emit_derived_field(store, block, order, 0x0413U,
                           "SpotHalftoneDeclaredBytes",
                           make_u32(declared_bytes), result);
        emit_derived_field(store, block, order, 0x0413U,
                           "SpotHalftoneDataBytes", make_u32(data_bytes),
                           result);
    }


    static void decode_halftone_summary(std::span<const std::byte> payload,
                                        uint16_t resource_id, MetaStore& store,
                                        BlockId block, uint32_t order,
                                        PhotoshopIrbDecodeResult* result)
    {
        if (payload.empty()) {
            return;
        }
        decode_payload_size_resource(payload, resource_id, "HalftoneInfoBytes",
                                     store, block, order, result);
        uint16_t first_word = 0;
        if (read_u16be(payload, 0U, &first_word)) {
            emit_derived_field(store, block, order, resource_id,
                               "HalftoneInfoFirstWord", make_u16(first_word),
                               result);
        }
    }


    static void
    decode_transfer_function_summary(std::span<const std::byte> payload,
                                     uint16_t resource_id, MetaStore& store,
                                     BlockId block, uint32_t order,
                                     PhotoshopIrbDecodeResult* result)
    {
        if (payload.empty()) {
            return;
        }
        decode_payload_size_resource(payload, resource_id,
                                     "TransferFunctionBytes", store, block,
                                     order, result);
        uint16_t first_word = 0;
        if (read_u16be(payload, 0U, &first_word)) {
            emit_derived_field(store, block, order, resource_id,
                               "TransferFunctionFirstWord",
                               make_u16(first_word), result);
        }
        if ((payload.size() % 28U) == 0U) {
            const uint32_t count = static_cast<uint32_t>(payload.size() / 28U);
            emit_derived_field(store, block, order, resource_id,
                               "TransferFunctionCurveCount", make_u32(count),
                               result);
        }
    }


    static void decode_jump_to_xpep(std::span<const std::byte> payload,
                                    MetaStore& store, BlockId block,
                                    uint32_t order,
                                    PhotoshopIrbDecodeResult* result)
    {
        uint16_t major_version = 0;
        uint16_t minor_version = 0;
        uint32_t jump_count    = 0;
        if (!read_u16be(payload, 0U, &major_version)
            || !read_u16be(payload, 2U, &minor_version)
            || !read_u32be(payload, 4U, &jump_count)) {
            return;
        }
        emit_derived_field(store, block, order, 0x041CU,
                           "JumpToXPEPMajorVersion", make_u16(major_version),
                           result);
        emit_derived_field(store, block, order, 0x041CU,
                           "JumpToXPEPMinorVersion", make_u16(minor_version),
                           result);
        emit_derived_field(store, block, order, 0x041CU, "JumpToXPEPJumpCount",
                           make_u32(jump_count), result);
    }


    static void decode_path_resource(std::span<const std::byte> payload,
                                     uint16_t resource_id, MetaStore& store,
                                     BlockId block, uint32_t order,
                                     PhotoshopIrbDecodeResult* result)
    {
        static constexpr uint32_t kPathRecordBytes = 26U;
        const uint32_t count = static_cast<uint32_t>(payload.size()
                                                     / kPathRecordBytes);
        if (count == 0U
            || static_cast<uint64_t>(count) * kPathRecordBytes
                   != static_cast<uint64_t>(payload.size())) {
            return;
        }
        emit_derived_field(store, block, order, resource_id, "PathRecordCount",
                           make_u32(count), result);
        for (uint32_t i = 0U; i < count; ++i) {
            uint16_t selector            = 0;
            const uint64_t record_offset = static_cast<uint64_t>(i)
                                           * kPathRecordBytes;
            if (!read_u16be(payload, record_offset, &selector)) {
                return;
            }
            emit_derived_field(store, block, order, resource_id,
                               "PathRecordSelector", make_u16(selector),
                               result);
        }
    }


    static void decode_u16_scalar_resource(std::span<const std::byte> payload,
                                           uint16_t resource_id,
                                           std::string_view field,
                                           MetaStore& store, BlockId block,
                                           uint32_t order,
                                           PhotoshopIrbDecodeResult* result)
    {
        uint16_t value = 0;
        if (!read_u16be(payload, 0, &value)) {
            return;
        }
        emit_derived_field(store, block, order, resource_id, field,
                           make_u16(value), result);
    }


    static void decode_u16_list_resource(std::span<const std::byte> payload,
                                         uint16_t resource_id,
                                         std::string_view count_field,
                                         std::string_view item_field,
                                         MetaStore& store, BlockId block,
                                         uint32_t order,
                                         PhotoshopIrbDecodeResult* result)
    {
        const uint32_t count = static_cast<uint32_t>(payload.size() / 2U);
        if (count == 0U) {
            return;
        }
        emit_derived_field(store, block, order, resource_id, count_field,
                           make_u32(count), result);
        for (uint32_t i = 0; i < count; ++i) {
            uint16_t value = 0;
            if (!read_u16be(payload, static_cast<uint64_t>(i) * 2U, &value)) {
                return;
            }
            emit_derived_field(store, block, order, resource_id, item_field,
                               make_u16(value), result);
        }
    }


    static bool bytes_ascii_no_nul(std::span<const std::byte> bytes) noexcept
    {
        for (size_t i = 0; i < bytes.size(); ++i) {
            const uint8_t v = u8(bytes[i]);
            if (v == 0U || v >= 0x80U) {
                return false;
            }
        }
        return true;
    }


    static constexpr uint32_t descriptor_fourcc(char a, char b, char c,
                                                char d) noexcept
    {
        return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24U)
               | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16U)
               | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8U)
               | static_cast<uint32_t>(static_cast<uint8_t>(d));
    }


    static bool descriptor_fourcc_ascii(uint32_t code, char out[4]) noexcept
    {
        if (!out) {
            return false;
        }
        for (uint32_t i = 0U; i < 4U; ++i) {
            const uint32_t shift = 24U - (i * 8U);
            const uint8_t v = static_cast<uint8_t>((code >> shift) & 0xFFU);
            if (v == 0U || v >= 0x80U) {
                return false;
            }
            out[i] = static_cast<char>(v);
        }
        return true;
    }


    static std::string_view descriptor_type_name(uint32_t type) noexcept
    {
        switch (type) {
        case descriptor_fourcc('b', 'o', 'o', 'l'): return "boolean";
        case descriptor_fourcc('l', 'o', 'n', 'g'): return "integer";
        case descriptor_fourcc('d', 'o', 'u', 'b'): return "double";
        case descriptor_fourcc('U', 'n', 't', 'F'): return "unit_float";
        case descriptor_fourcc('T', 'E', 'X', 'T'): return "text";
        case descriptor_fourcc('e', 'n', 'u', 'm'): return "enum";
        case descriptor_fourcc('O', 'b', 'j', 'c'): return "object";
        case descriptor_fourcc('G', 'l', 'b', 'O'): return "global_object";
        case descriptor_fourcc('V', 'l', 'L', 's'): return "list";
        case descriptor_fourcc('t', 'd', 't', 'a'): return "raw_data";
        default: return "unknown";
        }
    }


    static bool decode_latin1_to_utf8(std::span<const std::byte> bytes,
                                      std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        out->reserve(bytes.size() * 2U);
        for (size_t i = 0; i < bytes.size(); ++i) {
            const uint8_t cp = u8(bytes[i]);
            if (cp == 0U || !append_utf8_codepoint(cp, out)) {
                return false;
            }
        }
        return true;
    }


    static bool
    decode_pascal_string_text(std::span<const std::byte> payload,
                              PhotoshopIrbStringCharset string_charset,
                              std::string* out, TextEncoding* encoding) noexcept
    {
        if (!out || !encoding || payload.empty()) {
            return false;
        }
        const size_t text_len = static_cast<size_t>(u8(payload[0]));
        if (text_len == 0U || text_len + 1U > payload.size()) {
            return false;
        }
        const std::span<const std::byte> text_bytes = payload.subspan(1U,
                                                                      text_len);
        switch (string_charset) {
        case PhotoshopIrbStringCharset::Latin:
            if (!decode_latin1_to_utf8(text_bytes, out)) {
                return false;
            }
            *encoding = TextEncoding::Utf8;
            return true;
        case PhotoshopIrbStringCharset::Ascii:
            if (!bytes_ascii_no_nul(text_bytes)) {
                return false;
            }
            out->assign(reinterpret_cast<const char*>(text_bytes.data()),
                        text_bytes.size());
            *encoding = TextEncoding::Ascii;
            return true;
        }
        return false;
    }


    static void decode_ascii_text_resource(std::span<const std::byte> payload,
                                           uint16_t resource_id,
                                           std::string_view field,
                                           MetaStore& store, BlockId block,
                                           uint32_t order,
                                           PhotoshopIrbDecodeResult* result)
    {
        const size_t end = find_trailing_zero_padding_start(payload);
        const std::span<const std::byte> trimmed = payload.first(end);
        if (trimmed.empty() || !bytes_ascii_no_nul(trimmed)) {
            return;
        }
        const std::string_view text(reinterpret_cast<const char*>(
                                        trimmed.data()),
                                    trimmed.size());
        emit_derived_field(store, block, order, resource_id, field,
                           make_text(store.arena(), text, TextEncoding::Ascii),
                           result);
    }


    static void decode_unicode_text_resource(std::span<const std::byte> payload,
                                             uint16_t resource_id,
                                             std::string_view field,
                                             MetaStore& store, BlockId block,
                                             uint32_t order,
                                             PhotoshopIrbDecodeResult* result)
    {
        uint64_t offset = 0U;
        std::string text;
        if (!read_var_ustr32_utf8(payload, &offset, &text)) {
            return;
        }
        emit_derived_field(store, block, order, resource_id, field,
                           make_text(store.arena(), text, TextEncoding::Utf8),
                           result);
    }


    static void
    decode_pascal_text_resource(std::span<const std::byte> payload,
                                uint16_t resource_id, std::string_view field,
                                PhotoshopIrbStringCharset string_charset,
                                MetaStore& store, BlockId block, uint32_t order,
                                PhotoshopIrbDecodeResult* result)
    {
        std::string text;
        TextEncoding encoding = TextEncoding::Unknown;
        if (!decode_pascal_string_text(payload, string_charset, &text,
                                       &encoding)) {
            return;
        }
        emit_derived_field(store, block, order, resource_id, field,
                           make_text(store.arena(), text, encoding), result);
    }

    static void decode_pascal_text_list_resource(
        std::span<const std::byte> payload, uint16_t resource_id,
        std::string_view count_field, std::string_view item_field,
        PhotoshopIrbStringCharset string_charset, MetaStore& store,
        BlockId block, uint32_t order, PhotoshopIrbDecodeResult* result)
    {
        uint64_t offset = 0U;
        uint32_t count  = 0U;
        while (offset < payload.size()) {
            const std::span<const std::byte> remaining = payload.subspan(
                static_cast<size_t>(offset));
            std::string text;
            TextEncoding encoding = TextEncoding::Unknown;
            if (!decode_pascal_string_text(remaining, string_charset, &text,
                                           &encoding)) {
                return;
            }
            const uint64_t consumed = pad2(
                static_cast<uint64_t>(u8(remaining[0])) + 1U);
            if (consumed == 0U || consumed > remaining.size()) {
                return;
            }
            offset += consumed;
            count += 1U;
        }
        if (count == 0U) {
            return;
        }
        emit_derived_field(store, block, order, resource_id, count_field,
                           make_u32(count), result);
        offset = 0U;
        while (offset < payload.size()) {
            const std::span<const std::byte> remaining = payload.subspan(
                static_cast<size_t>(offset));
            std::string text;
            TextEncoding encoding = TextEncoding::Unknown;
            if (!decode_pascal_string_text(remaining, string_charset, &text,
                                           &encoding)) {
                return;
            }
            emit_derived_field(store, block, order, resource_id, item_field,
                               make_text(store.arena(), text, encoding),
                               result);
            offset += pad2(static_cast<uint64_t>(u8(remaining[0])) + 1U);
        }
    }

    static void decode_unicode_text_list_resource(
        std::span<const std::byte> payload, uint16_t resource_id,
        std::string_view count_field, std::string_view item_field,
        MetaStore& store, BlockId block, uint32_t order,
        PhotoshopIrbDecodeResult* result)
    {
        uint64_t offset = 0U;
        uint32_t count  = 0U;
        while (offset < payload.size()) {
            std::string text;
            if (!read_var_ustr32_utf8(payload, &offset, &text)) {
                return;
            }
            if (text.empty()) {
                continue;
            }
            count += 1U;
        }
        if (count == 0U) {
            return;
        }
        emit_derived_field(store, block, order, resource_id, count_field,
                           make_u32(count), result);
        offset = 0U;
        while (offset < payload.size()) {
            std::string text;
            if (!read_var_ustr32_utf8(payload, &offset, &text)) {
                return;
            }
            if (text.empty()) {
                continue;
            }
            emit_derived_field(store, block, order, resource_id, item_field,
                               make_text(store.arena(), text,
                                         TextEncoding::Utf8),
                               result);
        }
    }

    static void decode_u32_list_resource(std::span<const std::byte> payload,
                                         uint16_t resource_id,
                                         std::string_view count_field,
                                         std::string_view item_field,
                                         MetaStore& store, BlockId block,
                                         uint32_t order,
                                         PhotoshopIrbDecodeResult* result)
    {
        const uint32_t count = static_cast<uint32_t>(payload.size() / 4U);
        if (count == 0U
            || static_cast<uint64_t>(count) * 4U
                   != static_cast<uint64_t>(payload.size())) {
            return;
        }
        emit_derived_field(store, block, order, resource_id, count_field,
                           make_u32(count), result);
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t value = 0;
            if (!read_u32be(payload, static_cast<uint64_t>(i) * 4U, &value)) {
                return;
            }
            emit_derived_field(store, block, order, resource_id, item_field,
                               make_u32(value), result);
        }
    }

    static bool read_color_structure(std::span<const std::byte> payload,
                                     uint64_t offset, uint16_t* color_space,
                                     uint16_t* color_0, uint16_t* color_1,
                                     uint16_t* color_2,
                                     uint16_t* color_3) noexcept
    {
        if (!color_space || !color_0 || !color_1 || !color_2 || !color_3) {
            return false;
        }
        return read_u16be(payload, offset + 0U, color_space)
               && read_u16be(payload, offset + 2U, color_0)
               && read_u16be(payload, offset + 4U, color_1)
               && read_u16be(payload, offset + 6U, color_2)
               && read_u16be(payload, offset + 8U, color_3);
    }

    static void decode_border_info(std::span<const std::byte> payload,
                                   MetaStore& store, BlockId block,
                                   uint32_t order,
                                   PhotoshopIrbDecodeResult* result)
    {
        uint32_t width_fixed = 0;
        uint16_t units       = 0;
        if (!read_u32be(payload, 0U, &width_fixed)
            || !read_u16be(payload, 4U, &units)) {
            return;
        }
        const double width = static_cast<double>(width_fixed) / 65536.0;
        emit_derived_field(store, block, order, 0x03F1U, "BorderWidth",
                           make_f64_bits(f64_bits(width)), result);
        emit_derived_field(store, block, order, 0x03F1U, "BorderUnits",
                           make_u16(units), result);
    }

    static void decode_background_color(std::span<const std::byte> payload,
                                        MetaStore& store, BlockId block,
                                        uint32_t order,
                                        PhotoshopIrbDecodeResult* result)
    {
        uint16_t color_space = 0;
        uint16_t color_0     = 0;
        uint16_t color_1     = 0;
        uint16_t color_2     = 0;
        uint16_t color_3     = 0;
        if (!read_color_structure(payload, 0U, &color_space, &color_0, &color_1,
                                  &color_2, &color_3)) {
            return;
        }
        emit_derived_field(store, block, order, 0x03F2U, "BackgroundColorSpace",
                           make_u16(color_space), result);
        emit_derived_field(store, block, order, 0x03F2U, "BackgroundColorData",
                           make_u16(color_0), result);
        emit_derived_field(store, block, order, 0x03F2U, "BackgroundColorData",
                           make_u16(color_1), result);
        emit_derived_field(store, block, order, 0x03F2U, "BackgroundColorData",
                           make_u16(color_2), result);
        emit_derived_field(store, block, order, 0x03F2U, "BackgroundColorData",
                           make_u16(color_3), result);
    }

    static void decode_effective_bw(std::span<const std::byte> payload,
                                    MetaStore& store, BlockId block,
                                    uint32_t order,
                                    PhotoshopIrbDecodeResult* result)
    {
        if (payload.empty()) {
            return;
        }
        emit_derived_field(store, block, order, 0x03FBU, "EffectiveBW",
                           make_u8(u8(payload[0])), result);
        if (payload.size() < 2U) {
            return;
        }
        emit_derived_field(store, block, order, 0x03FBU, "EffectiveBlack",
                           make_u8(u8(payload[0])), result);
        emit_derived_field(store, block, order, 0x03FBU, "EffectiveWhite",
                           make_u8(u8(payload[1])), result);
    }

    static void decode_display_info(std::span<const std::byte> payload,
                                    MetaStore& store, BlockId block,
                                    uint32_t order,
                                    PhotoshopIrbDecodeResult* result)
    {
        const uint32_t count = static_cast<uint32_t>(payload.size() / 14U);
        if (count == 0U
            || static_cast<uint64_t>(count) * 14U
                   != static_cast<uint64_t>(payload.size())) {
            return;
        }
        emit_derived_field(store, block, order, 0x03EFU, "DisplayInfoCount",
                           make_u32(count), result);
        for (uint32_t i = 0U; i < count; ++i) {
            const uint64_t offset = static_cast<uint64_t>(i) * 14U;
            uint16_t color_space  = 0;
            uint16_t color_0      = 0;
            uint16_t color_1      = 0;
            uint16_t color_2      = 0;
            uint16_t color_3      = 0;
            uint16_t opacity      = 0;
            if (!read_u16be(payload, offset + 0U, &color_space)
                || !read_u16be(payload, offset + 2U, &color_0)
                || !read_u16be(payload, offset + 4U, &color_1)
                || !read_u16be(payload, offset + 6U, &color_2)
                || !read_u16be(payload, offset + 8U, &color_3)
                || !read_u16be(payload, offset + 10U, &opacity)) {
                return;
            }
            emit_derived_field(store, block, order, 0x03EFU,
                               "DisplayColorSpace", make_u16(color_space),
                               result);
            emit_derived_field(store, block, order, 0x03EFU, "DisplayColorData",
                               make_u16(color_0), result);
            emit_derived_field(store, block, order, 0x03EFU, "DisplayColorData",
                               make_u16(color_1), result);
            emit_derived_field(store, block, order, 0x03EFU, "DisplayColorData",
                               make_u16(color_2), result);
            emit_derived_field(store, block, order, 0x03EFU, "DisplayColorData",
                               make_u16(color_3), result);
            emit_derived_field(store, block, order, 0x03EFU, "DisplayOpacity",
                               make_u16(opacity), result);
            emit_derived_field(store, block, order, 0x03EFU, "DisplayKind",
                               make_u8(u8(payload[offset + 12U])), result);
        }
    }


    static void decode_grid_guides_info(std::span<const std::byte> payload,
                                        MetaStore& store, BlockId block,
                                        uint32_t order,
                                        PhotoshopIrbDecodeResult* result)
    {
        uint32_t version          = 0;
        uint32_t horizontal_cycle = 0;
        uint32_t vertical_cycle   = 0;
        uint32_t declared_count   = 0;
        if (!read_u32be(payload, 0U, &version)
            || !read_u32be(payload, 4U, &horizontal_cycle)
            || !read_u32be(payload, 8U, &vertical_cycle)
            || !read_u32be(payload, 12U, &declared_count)) {
            return;
        }

        const uint32_t available_count = static_cast<uint32_t>(
            (payload.size() >= 16U) ? ((payload.size() - 16U) / 5U) : 0U);
        const uint32_t emit_count = (declared_count < available_count)
                                        ? declared_count
                                        : available_count;
        emit_derived_field(store, block, order, 0x0408U, "GridGuidesVersion",
                           make_u32(version), result);
        emit_derived_field(store, block, order, 0x0408U, "GridHorizontalCycle",
                           make_u32(horizontal_cycle), result);
        emit_derived_field(store, block, order, 0x0408U, "GridVerticalCycle",
                           make_u32(vertical_cycle), result);
        emit_derived_field(store, block, order, 0x0408U, "GuideCount",
                           make_u32(declared_count), result);
        for (uint32_t i = 0U; i < emit_count; ++i) {
            const uint64_t offset = 16U + static_cast<uint64_t>(i) * 5U;
            uint32_t location     = 0;
            if (!read_u32be(payload, offset, &location)) {
                return;
            }
            emit_derived_field(store, block, order, 0x0408U, "GuideLocation",
                               make_u32(location), result);
            emit_derived_field(store, block, order, 0x0408U, "GuideDirection",
                               make_u8(u8(payload[offset + 4U])), result);
        }
    }


    static void decode_color_samplers(std::span<const std::byte> payload,
                                      uint16_t resource_id, MetaStore& store,
                                      BlockId block, uint32_t order,
                                      PhotoshopIrbDecodeResult* result)
    {
        uint32_t version        = 0;
        uint32_t declared_count = 0;
        if (!read_u32be(payload, 0U, &version)
            || !read_u32be(payload, 4U, &declared_count)) {
            return;
        }

        uint32_t record_size    = 0;
        bool has_record_version = false;
        bool has_depth          = false;
        if (version == 1U) {
            record_size = 10U;
        } else if (version == 2U) {
            record_size = 12U;
            has_depth   = true;
        } else if (version == 3U) {
            record_size        = 16U;
            has_record_version = true;
            has_depth          = true;
        } else {
            emit_derived_field(store, block, order, resource_id,
                               "ColorSamplerVersion", make_u32(version),
                               result);
            emit_derived_field(store, block, order, resource_id,
                               "ColorSamplerCount", make_u32(declared_count),
                               result);
            return;
        }

        const uint32_t available_count = static_cast<uint32_t>(
            (payload.size() >= 8U) ? ((payload.size() - 8U) / record_size)
                                   : 0U);
        const uint32_t emit_count = (declared_count < available_count)
                                        ? declared_count
                                        : available_count;
        emit_derived_field(store, block, order, resource_id,
                           "ColorSamplerVersion", make_u32(version), result);
        emit_derived_field(store, block, order, resource_id,
                           "ColorSamplerCount", make_u32(declared_count),
                           result);
        for (uint32_t i = 0U; i < emit_count; ++i) {
            uint64_t offset = 8U + static_cast<uint64_t>(i) * record_size;
            if (has_record_version) {
                uint32_t record_version = 0;
                if (!read_u32be(payload, offset, &record_version)) {
                    return;
                }
                emit_derived_field(store, block, order, resource_id,
                                   "ColorSamplerRecordVersion",
                                   make_u32(record_version), result);
                offset += 4U;
            }

            uint32_t horizontal  = 0;
            uint32_t vertical    = 0;
            uint16_t color_space = 0;
            if (!read_u32be(payload, offset + 0U, &horizontal)
                || !read_u32be(payload, offset + 4U, &vertical)
                || !read_u16be(payload, offset + 8U, &color_space)) {
                return;
            }
            emit_derived_field(store, block, order, resource_id,
                               "ColorSamplerHorizontalRaw",
                               make_u32(horizontal), result);
            emit_derived_field(store, block, order, resource_id,
                               "ColorSamplerVerticalRaw", make_u32(vertical),
                               result);
            emit_derived_field(store, block, order, resource_id,
                               "ColorSamplerColorSpace", make_u16(color_space),
                               result);
            if (has_depth) {
                uint16_t depth = 0;
                if (!read_u16be(payload, offset + 10U, &depth)) {
                    return;
                }
                emit_derived_field(store, block, order, resource_id,
                                   "ColorSamplerDepth", make_u16(depth),
                                   result);
            }
        }
    }


    static void decode_quick_mask_info(std::span<const std::byte> payload,
                                       MetaStore& store, BlockId block,
                                       uint32_t order,
                                       PhotoshopIrbDecodeResult* result)
    {
        uint16_t channel_id = 0U;
        if (!read_u16be(payload, 0U, &channel_id) || payload.size() < 3U) {
            return;
        }
        emit_derived_field(store, block, order, 0x03FEU, "QuickMaskChannelID",
                           make_u16(channel_id), result);
        emit_derived_field(store, block, order, 0x03FEU, "QuickMaskWasEmpty",
                           make_u8(u8(payload[2U])), result);
    }

    static void decode_channel_options(std::span<const std::byte> payload,
                                       MetaStore& store, BlockId block,
                                       uint32_t order,
                                       PhotoshopIrbDecodeResult* result)
    {
        const uint32_t count = static_cast<uint32_t>(payload.size() / 13U);
        if (count == 0U) {
            return;
        }
        emit_derived_field(store, block, order, 0x0435U, "ChannelOptionsCount",
                           make_u32(count), result);
        for (uint32_t i = 0; i < count; ++i) {
            const uint64_t offset = static_cast<uint64_t>(i) * 13U;
            uint16_t color_space  = 0;
            uint16_t color_data_0 = 0;
            uint16_t color_data_1 = 0;
            uint16_t color_data_2 = 0;
            uint16_t color_data_3 = 0;
            if (!read_u16be(payload, offset + 0U, &color_space)
                || !read_u16be(payload, offset + 2U, &color_data_0)
                || !read_u16be(payload, offset + 4U, &color_data_1)
                || !read_u16be(payload, offset + 6U, &color_data_2)
                || !read_u16be(payload, offset + 8U, &color_data_3)) {
                return;
            }
            emit_derived_field(store, block, order, 0x0435U, "ChannelIndex",
                               make_u32(i), result);
            emit_derived_field(store, block, order, 0x0435U,
                               "ChannelColorSpace", make_u16(color_space),
                               result);
            emit_derived_field(store, block, order, 0x0435U, "ChannelColorData",
                               make_u16(color_data_0), result);
            emit_derived_field(store, block, order, 0x0435U, "ChannelColorData",
                               make_u16(color_data_1), result);
            emit_derived_field(store, block, order, 0x0435U, "ChannelColorData",
                               make_u16(color_data_2), result);
            emit_derived_field(store, block, order, 0x0435U, "ChannelColorData",
                               make_u16(color_data_3), result);
            emit_derived_field(store, block, order, 0x0435U, "ChannelOpacity",
                               make_u8(u8(payload[offset + 11U])), result);
            emit_derived_field(store, block, order, 0x0435U,
                               "ChannelColorIndicates",
                               make_u8(u8(payload[offset + 12U])), result);
        }
    }

    static void decode_print_flags_info(std::span<const std::byte> payload,
                                        MetaStore& store, BlockId block,
                                        uint32_t order,
                                        PhotoshopIrbDecodeResult* result)
    {
        uint16_t version           = 0;
        uint32_t bleed_width_value = 0;
        uint16_t bleed_width_scale = 0;
        if (!read_u16be(payload, 0U, &version)
            || !read_u32be(payload, 4U, &bleed_width_value)
            || !read_u16be(payload, 8U, &bleed_width_scale)) {
            return;
        }
        emit_derived_field(store, block, order, 0x2710U,
                           "PrintFlagsInfoVersion", make_u16(version), result);
        emit_derived_field(store, block, order, 0x2710U, "CenterCropMarks",
                           make_u8(u8(payload[2U])), result);
        emit_derived_field(store, block, order, 0x2710U, "BleedWidthValue",
                           make_u32(bleed_width_value), result);
        emit_derived_field(store, block, order, 0x2710U, "BleedWidthScale",
                           make_u16(bleed_width_scale), result);
    }


    static void decode_pixel_info(std::span<const std::byte> payload,
                                  MetaStore& store, BlockId block,
                                  uint32_t order,
                                  PhotoshopIrbDecodeResult* result)
    {
        uint64_t pixel_aspect = 0;
        if (!read_u64be(payload, 4, &pixel_aspect)) {
            return;
        }
        emit_derived_field(store, block, order, 0x0428U, "PixelAspectRatio",
                           make_f64_bits(pixel_aspect), result);
    }


    static void decode_thumbnail_info(std::span<const std::byte> payload,
                                      uint16_t resource_id, MetaStore& store,
                                      BlockId block, uint32_t order,
                                      PhotoshopIrbDecodeResult* result)
    {
        if (payload.size() < 28U) {
            return;
        }

        uint32_t format           = 0;
        uint32_t width            = 0;
        uint32_t height           = 0;
        uint32_t row_bytes        = 0;
        uint32_t total_bytes      = 0;
        uint32_t compressed_bytes = 0;
        uint16_t bits_per_pixel   = 0;
        uint16_t planes           = 0;
        if (!read_u32be(payload, 0U, &format)
            || !read_u32be(payload, 4U, &width)
            || !read_u32be(payload, 8U, &height)
            || !read_u32be(payload, 12U, &row_bytes)
            || !read_u32be(payload, 16U, &total_bytes)
            || !read_u32be(payload, 20U, &compressed_bytes)
            || !read_u16be(payload, 24U, &bits_per_pixel)
            || !read_u16be(payload, 26U, &planes)) {
            return;
        }

        const uint64_t data_size_u64 = payload.size() - 28U;
        const uint32_t data_size     = (data_size_u64 > UINT32_MAX)
                                           ? UINT32_MAX
                                           : static_cast<uint32_t>(data_size_u64);

        emit_derived_field(store, block, order, resource_id, "ThumbnailFormat",
                           make_u32(format), result);
        emit_derived_field(store, block, order, resource_id, "ThumbnailWidth",
                           make_u32(width), result);
        emit_derived_field(store, block, order, resource_id, "ThumbnailHeight",
                           make_u32(height), result);
        emit_derived_field(store, block, order, resource_id,
                           "ThumbnailRowBytes", make_u32(row_bytes), result);
        emit_derived_field(store, block, order, resource_id,
                           "ThumbnailTotalBytes", make_u32(total_bytes),
                           result);
        emit_derived_field(store, block, order, resource_id,
                           "ThumbnailCompressedBytes",
                           make_u32(compressed_bytes), result);
        emit_derived_field(store, block, order, resource_id,
                           "ThumbnailBitsPerPixel", make_u16(bits_per_pixel),
                           result);
        emit_derived_field(store, block, order, resource_id, "ThumbnailPlanes",
                           make_u16(planes), result);
        emit_derived_field(store, block, order, resource_id,
                           "ThumbnailDataBytes", make_u32(data_size), result);
    }


    static void decode_jpeg_quality(std::span<const std::byte> payload,
                                    MetaStore& store, BlockId block,
                                    uint32_t order,
                                    PhotoshopIrbDecodeResult* result)
    {
        int16_t quality = 0;
        int16_t format  = 0;
        int16_t scans   = 0;
        if (!read_i16be(payload, 0, &quality)
            || !read_i16be(payload, 2, &format)) {
            return;
        }
        emit_derived_field(store, block, order, 0x0406U, "PhotoshopQuality",
                           make_i16(quality), result);
        emit_derived_field(store, block, order, 0x0406U, "PhotoshopFormat",
                           make_i16(format), result);
        if (format == static_cast<int16_t>(0x0101)
            && read_i16be(payload, 4, &scans)) {
            emit_derived_field(store, block, order, 0x0406U, "ProgressiveScans",
                               make_i16(scans), result);
        }
    }


    static void decode_print_scale_info(std::span<const std::byte> payload,
                                        MetaStore& store, BlockId block,
                                        uint32_t order,
                                        PhotoshopIrbDecodeResult* result)
    {
        uint16_t print_style      = 0;
        uint32_t position_x_bits  = 0;
        uint32_t position_y_bits  = 0;
        uint32_t print_scale_bits = 0;
        if (!read_u16be(payload, 0, &print_style)
            || !read_f32be_bits(payload, 2, &position_x_bits)
            || !read_f32be_bits(payload, 6, &position_y_bits)
            || !read_f32be_bits(payload, 10, &print_scale_bits)) {
            return;
        }
        emit_derived_field(store, block, order, 0x0426U, "PrintStyle",
                           make_u16(print_style), result);
        emit_derived_field(store, block, order, 0x0426U, "PrintPositionX",
                           make_f32_bits(position_x_bits), result);
        emit_derived_field(store, block, order, 0x0426U, "PrintPositionY",
                           make_f32_bits(position_y_bits), result);
        emit_derived_field(store, block, order, 0x0426U, "PrintScale",
                           make_f32_bits(print_scale_bits), result);
    }


    static void trim_trailing_nul(std::string* text) noexcept
    {
        if (text && !text->empty() && text->back() == '\0') {
            text->pop_back();
        }
    }


    static bool read_descriptor_class_id(std::span<const std::byte> payload,
                                         uint64_t* io_offset,
                                         std::string* out) noexcept
    {
        if (!io_offset || !out) {
            return false;
        }
        uint32_t declared_len = 0;
        if (!read_u32be(payload, *io_offset, &declared_len)) {
            return false;
        }
        const uint64_t value_offset = *io_offset + 4U;
        const uint64_t byte_count   = declared_len == 0U ? 4U : declared_len;
        if (byte_count > 256U || value_offset > payload.size()
            || byte_count > payload.size()
            || value_offset + byte_count > payload.size()) {
            return false;
        }
        const std::span<const std::byte> bytes
            = payload.subspan(static_cast<size_t>(value_offset),
                              static_cast<size_t>(byte_count));
        if (!bytes_ascii_no_nul(bytes)) {
            return false;
        }
        out->assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        *io_offset = value_offset + byte_count;
        return true;
    }


    static constexpr uint32_t kMaxDescriptorItems  = 64U;
    static constexpr uint32_t kMaxDescriptorValues = 128U;
    static constexpr uint32_t kMaxDescriptorDepth  = 4U;


    struct DescriptorParseState final {
        MetaStore* store                 = nullptr;
        BlockId block                    = {};
        uint32_t order                   = 0U;
        uint16_t resource_id             = 0U;
        PhotoshopIrbDecodeResult* result = nullptr;
        uint32_t parsed_values           = 0U;
        bool parse_truncated             = false;
    };


    static void descriptor_make_child_path(std::string_view parent,
                                           std::string_view key,
                                           std::string* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        if (parent.empty()) {
            out->assign(key.data(), key.size());
            return;
        }
        out->assign(parent.data(), parent.size());
        if (!key.empty()) {
            out->push_back('.');
            out->append(key.data(), key.size());
        }
    }


    static void descriptor_make_list_path(std::string_view parent,
                                          uint32_t list_index,
                                          std::string* out) noexcept
    {
        if (!out) {
            return;
        }
        char index_text[32] = {};
        const int n = std::snprintf(index_text, sizeof(index_text), "%u",
                                    list_index);
        out->assign(parent.data(), parent.size());
        out->push_back('[');
        if (n > 0) {
            out->append(index_text, static_cast<size_t>(n));
        }
        out->push_back(']');
    }


    static void emit_descriptor_item_common(DescriptorParseState* state,
                                            std::string_view key,
                                            std::string_view path,
                                            uint32_t item_type, uint32_t depth,
                                            bool have_list_index,
                                            uint32_t list_index) noexcept
    {
        if (!state || !state->store) {
            return;
        }
        MetaStore& store = *state->store;
        if (!key.empty()) {
            emit_derived_field(store, state->block, state->order,
                               state->resource_id, "DescriptorItemKey",
                               make_text(store.arena(), key,
                                         TextEncoding::Ascii),
                               state->result);
        }
        if (!path.empty()) {
            emit_derived_field(store, state->block, state->order,
                               state->resource_id, "DescriptorItemPath",
                               make_text(store.arena(), path,
                                         TextEncoding::Ascii),
                               state->result);
        }
        emit_derived_field(store, state->block, state->order,
                           state->resource_id, "DescriptorItemDepth",
                           make_u32(depth), state->result);
        if (have_list_index) {
            emit_derived_field(store, state->block, state->order,
                               state->resource_id, "DescriptorItemListIndex",
                               make_u32(list_index), state->result);
        }
        emit_derived_field(store, state->block, state->order,
                           state->resource_id, "DescriptorItemType",
                           make_u32(item_type), state->result);
        emit_derived_field(store, state->block, state->order,
                           state->resource_id, "DescriptorItemTypeName",
                           make_text(store.arena(),
                                     descriptor_type_name(item_type),
                                     TextEncoding::Ascii),
                           state->result);
    }


    static bool parse_descriptor_value(
        std::span<const std::byte> payload, uint64_t* io_offset,
        DescriptorParseState* state, std::string_view item_key,
        std::string_view item_path, uint32_t item_type, uint32_t depth,
        bool have_list_index, uint32_t list_index) noexcept;


    static bool parse_descriptor_keyed_item(std::span<const std::byte> payload,
                                            uint64_t* io_offset,
                                            DescriptorParseState* state,
                                            std::string_view parent_path,
                                            uint32_t depth) noexcept
    {
        if (!io_offset || !state) {
            return false;
        }
        std::string item_key;
        if (!read_descriptor_class_id(payload, io_offset, &item_key)) {
            return false;
        }
        uint32_t item_type = 0U;
        if (!read_u32be(payload, *io_offset, &item_type)) {
            return false;
        }
        *io_offset += 4U;

        std::string item_path;
        descriptor_make_child_path(parent_path, item_key, &item_path);
        return parse_descriptor_value(payload, io_offset, state, item_key,
                                      item_path, item_type, depth, false, 0U);
    }


    static bool parse_descriptor_children(std::span<const std::byte> payload,
                                          uint64_t* io_offset,
                                          DescriptorParseState* state,
                                          std::string_view parent_path,
                                          uint32_t item_count,
                                          uint32_t depth) noexcept
    {
        if (!io_offset || !state) {
            return false;
        }
        if (item_count == 0U) {
            return true;
        }
        if (depth >= kMaxDescriptorDepth) {
            state->parse_truncated = true;
            return true;
        }
        const uint32_t item_limit = item_count > kMaxDescriptorItems
                                        ? kMaxDescriptorItems
                                        : item_count;
        if (item_count > kMaxDescriptorItems) {
            state->parse_truncated = true;
        }
        for (uint32_t i = 0U; i < item_limit; ++i) {
            if (!parse_descriptor_keyed_item(payload, io_offset, state,
                                             parent_path, depth + 1U)) {
                state->parse_truncated = true;
                return true;
            }
        }
        return true;
    }


    static bool parse_descriptor_list_values(std::span<const std::byte> payload,
                                             uint64_t* io_offset,
                                             DescriptorParseState* state,
                                             std::string_view parent_path,
                                             uint32_t item_count,
                                             uint32_t depth) noexcept
    {
        if (!io_offset || !state) {
            return false;
        }
        if (item_count == 0U) {
            return true;
        }
        if (depth >= kMaxDescriptorDepth) {
            state->parse_truncated = true;
            return true;
        }
        const uint32_t item_limit = item_count > kMaxDescriptorItems
                                        ? kMaxDescriptorItems
                                        : item_count;
        if (item_count > kMaxDescriptorItems) {
            state->parse_truncated = true;
        }
        for (uint32_t i = 0U; i < item_limit; ++i) {
            uint32_t item_type = 0U;
            if (!read_u32be(payload, *io_offset, &item_type)) {
                state->parse_truncated = true;
                return true;
            }
            *io_offset += 4U;

            std::string item_path;
            descriptor_make_list_path(parent_path, i, &item_path);
            if (!parse_descriptor_value(payload, io_offset, state,
                                        std::string_view(), item_path,
                                        item_type, depth + 1U, true, i)) {
                state->parse_truncated = true;
                return true;
            }
        }
        return true;
    }


    static bool parse_descriptor_value(
        std::span<const std::byte> payload, uint64_t* io_offset,
        DescriptorParseState* state, std::string_view item_key,
        std::string_view item_path, uint32_t item_type, uint32_t depth,
        bool have_list_index, uint32_t list_index) noexcept
    {
        if (!io_offset || !state || !state->store) {
            return false;
        }
        if (state->parsed_values >= kMaxDescriptorValues) {
            state->parse_truncated = true;
            return false;
        }

        MetaStore& store         = *state->store;
        uint8_t bool_value       = 0U;
        int32_t int_value        = 0;
        uint64_t double_bits     = 0U;
        uint32_t unit_code       = 0U;
        uint64_t unit_float_bits = 0U;
        std::string text_value;
        std::string enum_type;
        std::string enum_value;
        std::string object_class_name;
        std::string object_class_id;
        uint32_t object_item_count = 0U;
        uint32_t list_count        = 0U;
        uint32_t raw_data_bytes    = 0U;

        switch (item_type) {
        case descriptor_fourcc('b', 'o', 'o', 'l'):
            if (*io_offset + 1U > payload.size()) {
                return false;
            }
            bool_value = u8(payload[static_cast<size_t>(*io_offset)]);
            *io_offset += 1U;
            break;
        case descriptor_fourcc('l', 'o', 'n', 'g'):
            if (!read_i32be(payload, *io_offset, &int_value)) {
                return false;
            }
            *io_offset += 4U;
            break;
        case descriptor_fourcc('d', 'o', 'u', 'b'):
            if (!read_u64be(payload, *io_offset, &double_bits)) {
                return false;
            }
            *io_offset += 8U;
            break;
        case descriptor_fourcc('U', 'n', 't', 'F'):
            if (!read_u32be(payload, *io_offset, &unit_code)
                || !read_u64be(payload, *io_offset + 4U, &unit_float_bits)) {
                return false;
            }
            *io_offset += 12U;
            break;
        case descriptor_fourcc('T', 'E', 'X', 'T'):
            if (!read_var_ustr32_utf8(payload, io_offset, &text_value)) {
                return false;
            }
            trim_trailing_nul(&text_value);
            break;
        case descriptor_fourcc('e', 'n', 'u', 'm'):
            if (!read_descriptor_class_id(payload, io_offset, &enum_type)
                || !read_descriptor_class_id(payload, io_offset, &enum_value)) {
                return false;
            }
            break;
        case descriptor_fourcc('O', 'b', 'j', 'c'):
        case descriptor_fourcc('G', 'l', 'b', 'O'):
            if (!read_var_ustr32_utf8(payload, io_offset, &object_class_name)
                || !read_descriptor_class_id(payload, io_offset,
                                             &object_class_id)
                || !read_u32be(payload, *io_offset, &object_item_count)) {
                return false;
            }
            *io_offset += 4U;
            trim_trailing_nul(&object_class_name);
            break;
        case descriptor_fourcc('V', 'l', 'L', 's'):
            if (!read_u32be(payload, *io_offset, &list_count)) {
                return false;
            }
            *io_offset += 4U;
            break;
        case descriptor_fourcc('t', 'd', 't', 'a'):
            if (!read_u32be(payload, *io_offset, &raw_data_bytes)) {
                return false;
            }
            *io_offset += 4U;
            if (*io_offset > payload.size()
                || raw_data_bytes > payload.size() - *io_offset) {
                return false;
            }
            *io_offset += raw_data_bytes;
            break;
        default: state->parse_truncated = true; return false;
        }

        emit_descriptor_item_common(state, item_key, item_path, item_type,
                                    depth, have_list_index, list_index);
        switch (item_type) {
        case descriptor_fourcc('b', 'o', 'o', 'l'):
            emit_derived_field(store, state->block, state->order,
                               state->resource_id, "DescriptorItemBoolean",
                               make_u8(bool_value), state->result);
            break;
        case descriptor_fourcc('l', 'o', 'n', 'g'):
            emit_derived_field(store, state->block, state->order,
                               state->resource_id, "DescriptorItemInteger",
                               make_i32(int_value), state->result);
            break;
        case descriptor_fourcc('d', 'o', 'u', 'b'):
            emit_derived_field(store, state->block, state->order,
                               state->resource_id, "DescriptorItemDouble",
                               make_f64_bits(double_bits), state->result);
            break;
        case descriptor_fourcc('U', 'n', 't', 'F'): {
            char unit_text[4] = {};
            if (descriptor_fourcc_ascii(unit_code, unit_text)) {
                emit_derived_field(store, state->block, state->order,
                                   state->resource_id, "DescriptorItemUnit",
                                   make_text(store.arena(),
                                             std::string_view(unit_text, 4U),
                                             TextEncoding::Ascii),
                                   state->result);
            }
            emit_derived_field(store, state->block, state->order,
                               state->resource_id, "DescriptorItemUnitFloat",
                               make_f64_bits(unit_float_bits), state->result);
            break;
        }
        case descriptor_fourcc('T', 'E', 'X', 'T'):
            emit_derived_field(store, state->block, state->order,
                               state->resource_id, "DescriptorItemText",
                               make_text(store.arena(), text_value,
                                         TextEncoding::Utf8),
                               state->result);
            break;
        case descriptor_fourcc('e', 'n', 'u', 'm'):
            emit_derived_field(store, state->block, state->order,
                               state->resource_id, "DescriptorItemEnumType",
                               make_text(store.arena(), enum_type,
                                         TextEncoding::Ascii),
                               state->result);
            emit_derived_field(store, state->block, state->order,
                               state->resource_id, "DescriptorItemEnumValue",
                               make_text(store.arena(), enum_value,
                                         TextEncoding::Ascii),
                               state->result);
            break;
        case descriptor_fourcc('O', 'b', 'j', 'c'):
        case descriptor_fourcc('G', 'l', 'b', 'O'):
            if (!object_class_name.empty()) {
                emit_derived_field(store, state->block, state->order,
                                   state->resource_id,
                                   "DescriptorItemObjectClassName",
                                   make_text(store.arena(), object_class_name,
                                             TextEncoding::Utf8),
                                   state->result);
            }
            if (!object_class_id.empty()) {
                emit_derived_field(store, state->block, state->order,
                                   state->resource_id,
                                   "DescriptorItemObjectClassID",
                                   make_text(store.arena(), object_class_id,
                                             TextEncoding::Ascii),
                                   state->result);
            }
            emit_derived_field(store, state->block, state->order,
                               state->resource_id,
                               "DescriptorItemObjectItemCount",
                               make_u32(object_item_count), state->result);
            break;
        case descriptor_fourcc('V', 'l', 'L', 's'):
            emit_derived_field(store, state->block, state->order,
                               state->resource_id, "DescriptorItemListCount",
                               make_u32(list_count), state->result);
            break;
        case descriptor_fourcc('t', 'd', 't', 'a'):
            emit_derived_field(store, state->block, state->order,
                               state->resource_id, "DescriptorItemRawDataBytes",
                               make_u32(raw_data_bytes), state->result);
            break;
        default: break;
        }

        state->parsed_values += 1U;

        if (item_type == descriptor_fourcc('O', 'b', 'j', 'c')
            || item_type == descriptor_fourcc('G', 'l', 'b', 'O')) {
            return parse_descriptor_children(payload, io_offset, state,
                                             item_path, object_item_count,
                                             depth);
        }
        if (item_type == descriptor_fourcc('V', 'l', 'L', 's')) {
            return parse_descriptor_list_values(payload, io_offset, state,
                                                item_path, list_count, depth);
        }
        return true;
    }


    static void
    decode_descriptor_header_resource(std::span<const std::byte> payload,
                                      uint16_t resource_id, MetaStore& store,
                                      BlockId block, uint32_t order,
                                      PhotoshopIrbDecodeResult* result)
    {
        uint32_t descriptor_version = 0;
        if (!read_u32be(payload, 0U, &descriptor_version)) {
            return;
        }
        const uint64_t bytes         = payload.size() - 4U;
        const uint32_t clamped_bytes = (bytes > UINT32_MAX)
                                           ? UINT32_MAX
                                           : static_cast<uint32_t>(bytes);
        emit_derived_field(store, block, order, resource_id,
                           "DescriptorVersion", make_u32(descriptor_version),
                           result);
        emit_derived_field(store, block, order, resource_id, "DescriptorBytes",
                           make_u32(clamped_bytes), result);

        uint64_t offset = 4U;
        std::string class_name;
        if (!read_var_ustr32_utf8(payload, &offset, &class_name)) {
            return;
        }
        trim_trailing_nul(&class_name);

        std::string class_id;
        if (!read_descriptor_class_id(payload, &offset, &class_id)) {
            return;
        }
        uint32_t item_count = 0U;
        if (!read_u32be(payload, offset, &item_count)) {
            return;
        }
        offset += 4U;

        if (!class_name.empty()) {
            emit_derived_field(store, block, order, resource_id,
                               "DescriptorClassName",
                               make_text(store.arena(), class_name,
                                         TextEncoding::Utf8),
                               result);
        }
        if (!class_id.empty()) {
            emit_derived_field(store, block, order, resource_id,
                               "DescriptorClassID",
                               make_text(store.arena(), class_id,
                                         TextEncoding::Ascii),
                               result);
        }
        emit_derived_field(store, block, order, resource_id,
                           "DescriptorItemCount", make_u32(item_count), result);

        const uint32_t item_limit = item_count > kMaxDescriptorItems
                                        ? kMaxDescriptorItems
                                        : item_count;
        uint32_t parsed_items     = 0U;
        DescriptorParseState parse_state;
        parse_state.store           = &store;
        parse_state.block           = block;
        parse_state.order           = order;
        parse_state.resource_id     = resource_id;
        parse_state.result          = result;
        parse_state.parse_truncated = item_count > kMaxDescriptorItems;
        for (uint32_t item_index = 0U; item_index < item_limit; ++item_index) {
            if (!parse_descriptor_keyed_item(payload, &offset, &parse_state,
                                             std::string_view(), 0U)) {
                parse_state.parse_truncated = true;
                break;
            }
            parsed_items += 1U;
        }

        if (item_count != 0U) {
            emit_derived_field(store, block, order, resource_id,
                               "DescriptorParsedItemCount",
                               make_u32(parsed_items), result);
            emit_derived_field(store, block, order, resource_id,
                               "DescriptorParsedValueCount",
                               make_u32(parse_state.parsed_values), result);
            if (parse_state.parse_truncated || parsed_items != item_count) {
                emit_derived_field(store, block, order, resource_id,
                                   "DescriptorItemParseTruncated", make_u8(1U),
                                   result);
            }
        }
    }


    static void decode_version_info(std::span<const std::byte> payload,
                                    MetaStore& store, BlockId block,
                                    uint32_t order,
                                    PhotoshopIrbDecodeResult* result)
    {
        uint32_t version = 0;
        if (!read_u32be(payload, 0, &version) || payload.size() < 5U) {
            return;
        }
        uint64_t offset = 5U;
        std::string writer_name;
        std::string reader_name;
        if (!read_var_ustr32_utf8(payload, &offset, &writer_name)
            || !read_var_ustr32_utf8(payload, &offset, &reader_name)) {
            return;
        }
        uint32_t file_version = 0;
        if (!read_u32be(payload, offset, &file_version)) {
            return;
        }
        emit_derived_field(store, block, order, 0x0421U, "HasRealMergedData",
                           make_u8(u8(payload[4])), result);
        emit_derived_field(store, block, order, 0x0421U, "WriterName",
                           make_text(store.arena(), writer_name,
                                     TextEncoding::Utf8),
                           result);
        emit_derived_field(store, block, order, 0x0421U, "ReaderName",
                           make_text(store.arena(), reader_name,
                                     TextEncoding::Utf8),
                           result);
    }


    static void decode_layer_selection_ids(std::span<const std::byte> payload,
                                           MetaStore& store, BlockId block,
                                           uint32_t order,
                                           PhotoshopIrbDecodeResult* result)
    {
        uint16_t declared_count = 0;
        if (!read_u16be(payload, 0, &declared_count)) {
            return;
        }
        const uint32_t available_count = static_cast<uint32_t>(
            (payload.size() >= 2U) ? ((payload.size() - 2U) / 4U) : 0U);
        const uint32_t emit_count = (static_cast<uint32_t>(declared_count)
                                     < available_count)
                                        ? static_cast<uint32_t>(declared_count)
                                        : available_count;
        emit_derived_field(store, block, order, 0x042DU,
                           "LayerSelectionIDCount", make_u32(emit_count),
                           result);
        for (uint32_t i = 0; i < emit_count; ++i) {
            uint32_t item_id = 0;
            if (!read_u32be(payload, 2U + static_cast<uint64_t>(i) * 4U,
                            &item_id)) {
                return;
            }
            emit_derived_field(store, block, order, 0x042DU, "LayerSelectionID",
                               make_u32(item_id), result);
        }
    }


    static void decode_url_list(std::span<const std::byte> payload,
                                MetaStore& store, BlockId block, uint32_t order,
                                PhotoshopIrbDecodeResult* result)
    {
        uint32_t declared_count = 0;
        if (!read_u32be(payload, 0, &declared_count)) {
            return;
        }
        uint64_t offset        = 4U;
        uint32_t emitted_count = 0;
        for (uint32_t i = 0; i < declared_count; ++i) {
            if (offset + 8U > payload.size()) {
                break;
            }
            offset += 8U;
            std::string url;
            if (!read_var_ustr32_utf8(payload, &offset, &url)) {
                break;
            }
            emit_derived_field(store, block, order, 0x041EU, "URL",
                               make_text(store.arena(), url, TextEncoding::Utf8),
                               result);
            emitted_count += 1U;
        }
        emit_derived_field(store, block, order, 0x041EU, "URLListCount",
                           make_u32(emitted_count), result);
    }


    static void decode_slice_info(std::span<const std::byte> payload,
                                  MetaStore& store, BlockId block,
                                  uint32_t order,
                                  PhotoshopIrbDecodeResult* result)
    {
        if (payload.size() < 20U) {
            return;
        }
        uint64_t offset = 20U;
        std::string group_name;
        if (!read_var_ustr32_utf8(payload, &offset, &group_name)) {
            return;
        }
        uint32_t num_slices = 0;
        if (!read_u32be(payload, offset, &num_slices)) {
            return;
        }
        emit_derived_field(store, block, order, 0x041AU, "SlicesGroupName",
                           make_text(store.arena(), group_name,
                                     TextEncoding::Utf8),
                           result);
        emit_derived_field(store, block, order, 0x041AU, "NumSlices",
                           make_u32(num_slices), result);
    }


    static void decode_known_resource_fields(
        std::span<const std::byte> payload, uint16_t resource_id,
        const PhotoshopIrbDecodeOptions& options, MetaStore& store,
        BlockId block, uint32_t order, PhotoshopIrbDecodeResult* result)
    {
        if (resource_id == 0x0401U
            || (resource_id >= 0x07D0U && resource_id <= 0x0BB6U)) {
            decode_path_resource(payload, resource_id, store, block, order,
                                 result);
            return;
        }

        switch (resource_id) {
        case 0x03E8U:
            decode_photoshop2_info(payload, store, block, order, result);
            break;
        case 0x03EAU:
            decode_ascii_text_resource(payload, resource_id, "XMLData", store,
                                       block, order, result);
            break;
        case 0x03EBU:
            decode_photoshop2_color_table(payload, store, block, order, result);
            break;
        case 0x03EDU:
            decode_resolution_info(payload, store, block, order, result);
            break;
        case 0x03EEU:
            decode_pascal_text_list_resource(payload, resource_id,
                                             "AlphaChannelNameCount",
                                             "AlphaChannelName",
                                             options.string_charset, store,
                                             block, order, result);
            break;
        case 0x03EFU:
            decode_display_info(payload, store, block, order, result);
            break;
        case 0x0421U:
            decode_version_info(payload, store, block, order, result);
            break;
        case 0x03F0U:
            decode_pascal_text_resource(payload, resource_id, "Caption",
                                        options.string_charset, store, block,
                                        order, result);
            break;
        case 0x03F1U:
            decode_border_info(payload, store, block, order, result);
            break;
        case 0x03F2U:
            decode_background_color(payload, store, block, order, result);
            break;
        case 0x03F3U:
            decode_print_flags(payload, store, block, order, result);
            break;
        case 0x03F4U:
        case 0x03F5U:
        case 0x03F6U:
            decode_halftone_summary(payload, resource_id, store, block, order,
                                    result);
            break;
        case 0x03F7U:
        case 0x03F8U:
        case 0x03F9U:
            decode_transfer_function_summary(payload, resource_id, store, block,
                                             order, result);
            break;
        case 0x03FAU:
            decode_payload_size_resource(payload, resource_id,
                                         "DuotoneImageInfoBytes", store, block,
                                         order, result);
            break;
        case 0x03FDU:
            decode_payload_size_resource(payload, resource_id,
                                         "EPSOptionsBytes", store, block, order,
                                         result);
            break;
        case 0x03FBU:
            decode_effective_bw(payload, store, block, order, result);
            break;
        case 0x0400U:
            decode_u16_scalar_resource(payload, resource_id, "TargetLayerID",
                                       store, block, order, result);
            break;
        case 0x0404U:
            decode_payload_size_resource(payload, resource_id, "IPTCDataBytes",
                                         store, block, order, result);
            break;
        case 0x0402U:
            decode_u16_list_resource(payload, resource_id,
                                     "LayersGroupInfoCount", "LayersGroupInfo",
                                     store, block, order, result);
            break;
        case 0x03FEU:
            decode_quick_mask_info(payload, store, block, order, result);
            break;
        case 0x0405U:
            decode_u16_scalar_resource(payload, resource_id, "RawImageMode",
                                       store, block, order, result);
            break;
        case 0x0406U:
            decode_jpeg_quality(payload, store, block, order, result);
            break;
        case 0x0408U:
            decode_grid_guides_info(payload, store, block, order, result);
            break;
        case 0x0409U:
        case 0x040CU:
            decode_thumbnail_info(payload, resource_id, store, block, order,
                                  result);
            break;
        case 0x040BU:
            decode_ascii_text_resource(payload, resource_id, "URL", store,
                                       block, order, result);
            break;
        case 0x040EU:
            decode_color_samplers(payload, resource_id, store, block, order,
                                  result);
            break;
        case 0x0BB7U:
            decode_pascal_text_resource(payload, resource_id,
                                        "ClippingPathName",
                                        options.string_charset, store, block,
                                        order, result);
            break;
        case 0x041AU:
            decode_slice_info(payload, store, block, order, result);
            break;
        case 0x041BU:
            decode_unicode_text_resource(payload, resource_id, "WorkflowURL",
                                         store, block, order, result);
            break;
        case 0x041CU:
            decode_jump_to_xpep(payload, store, block, order, result);
            break;
        case 0x041EU:
            decode_url_list(payload, store, block, order, result);
            break;
        case 0x0422U:
            decode_payload_size_resource(payload, resource_id, "EXIFInfoBytes",
                                         store, block, order, result);
            break;
        case 0x0423U:
            decode_payload_size_resource(payload, resource_id, "EXIFInfo2Bytes",
                                         store, block, order, result);
            break;
        case 0x0424U:
            decode_payload_size_resource(payload, resource_id, "XMPPacketBytes",
                                         store, block, order, result);
            break;
        case 0x0425U:
            decode_iptc_digest(payload, store, block, order, result);
            break;
        case 0x0426U:
            decode_print_scale_info(payload, store, block, order, result);
            break;
        case 0x0428U:
            decode_pixel_info(payload, store, block, order, result);
            break;
        case 0x0429U:
            decode_descriptor_header_resource(payload, resource_id, store,
                                              block, order, result);
            break;
        case 0x042EU:
        case 0x042FU:
            decode_descriptor_header_resource(payload, resource_id, store,
                                              block, order, result);
            break;
        case 0x042DU:
            decode_layer_selection_ids(payload, store, block, order, result);
            break;
        case 0x040AU:
            decode_u8_scalar_resource(payload, resource_id, "CopyrightFlag",
                                      store, block, order, result);
            break;
        case 0x040DU:
            decode_u32_scalar_resource(payload, resource_id, "GlobalAngle",
                                       store, block, order, result);
            break;
        case 0x040FU:
            decode_payload_size_resource(payload, resource_id,
                                         "ICCProfileBytes", store, block, order,
                                         result);
            break;
        case 0x0412U:
            decode_u8_scalar_resource(payload, resource_id, "EffectsVisible",
                                      store, block, order, result);
            break;
        case 0x0413U:
            decode_spot_halftone(payload, store, block, order, result);
            break;
        case 0x0410U:
            decode_u8_scalar_resource(payload, resource_id, "Watermark", store,
                                      block, order, result);
            break;
        case 0x0411U:
            decode_u8_scalar_resource(payload, resource_id, "ICC_Untagged",
                                      store, block, order, result);
            break;
        case 0x0414U:
            decode_u32_scalar_resource(payload, resource_id, "IDsBaseValue",
                                       store, block, order, result);
            break;
        case 0x0415U:
            decode_unicode_text_list_resource(payload, resource_id,
                                              "UnicodeAlphaNameCount",
                                              "UnicodeAlphaName", store, block,
                                              order, result);
            break;
        case 0x0416U:
            decode_u16_scalar_resource(payload, resource_id,
                                       "IndexedColorTableCount", store, block,
                                       order, result);
            break;
        case 0x0417U:
            decode_u16_scalar_resource(payload, resource_id, "TransparentIndex",
                                       store, block, order, result);
            break;
        case 0x0419U:
            decode_u32_scalar_resource(payload, resource_id, "GlobalAltitude",
                                       store, block, order, result);
            break;
        case 0x041DU:
            decode_u32_list_resource(payload, resource_id,
                                     "AlphaIdentifierCount", "AlphaIdentifier",
                                     store, block, order, result);
            break;
        case 0x0430U:
            decode_u8_scalar_resource(payload, resource_id,
                                      "LayerGroupsEnabledID", store, block,
                                      order, result);
            break;
        case 0x0431U:
            decode_color_samplers(payload, resource_id, store, block, order,
                                  result);
            break;
        case 0x0432U:
        case 0x0433U:
        case 0x0434U:
            decode_descriptor_header_resource(payload, resource_id, store,
                                              block, order, result);
            break;
        case 0x0435U:
            decode_channel_options(payload, store, block, order, result);
            break;
        case 0x0436U:
        case 0x0438U:
        case 0x043AU:
        case 0x043BU:
        case 0x0440U:
        case 0x0BB8U:
            decode_descriptor_header_resource(payload, resource_id, store,
                                              block, order, result);
            break;
        case 0x043EU:
            decode_unicode_text_resource(payload, resource_id,
                                         "AutoSaveFilePath", store, block,
                                         order, result);
            break;
        case 0x043FU:
            decode_unicode_text_resource(payload, resource_id, "AutoSaveFormat",
                                         store, block, order, result);
            break;
        case 0x1B58U:
            decode_ascii_text_resource(payload, resource_id,
                                       "ImageReadyVariables", store, block,
                                       order, result);
            break;
        case 0x1B59U:
            decode_ascii_text_resource(payload, resource_id,
                                       "ImageReadyDataSets", store, block,
                                       order, result);
            break;
        case 0x1F40U:
            decode_ascii_text_resource(payload, resource_id,
                                       "LightroomWorkflow", store, block, order,
                                       result);
            break;
        case 0x2710U:
            decode_print_flags_info(payload, store, block, order, result);
            break;
        default: break;
        }
    }

}  // namespace

std::string_view
photoshop_irb_resource_name(uint16_t resource_id) noexcept
{
    switch (resource_id) {
    case 0x03E8U: return "Photoshop2Info";
    case 0x03E9U: return "MacintoshPrintInfo";
    case 0x03EAU: return "XMLData";
    case 0x03EBU: return "Photoshop2ColorTable";
    case 0x03EDU: return "ResolutionInfo";
    case 0x03EEU: return "AlphaChannelsNames";
    case 0x03EFU: return "DisplayInfo";
    case 0x03F3U: return "PrintFlags";
    case 0x03FBU: return "EffectiveBW";
    case 0x03F0U: return "PStringCaption";
    case 0x03F1U: return "BorderInformation";
    case 0x03F2U: return "BackgroundColor";
    case 0x03F4U: return "BW_HalftoningInfo";
    case 0x03F5U: return "ColorHalftoningInfo";
    case 0x03F6U: return "DuotoneHalftoningInfo";
    case 0x03F7U: return "BW_TransferFunc";
    case 0x03F8U: return "ColorTransferFuncs";
    case 0x03F9U: return "DuotoneTransferFuncs";
    case 0x03FAU: return "DuotoneImageInfo";
    case 0x03FCU: return "ObsoletePhotoshopTag1";
    case 0x03FDU: return "EPSOptions";
    case 0x03FEU: return "QuickMaskInfo";
    case 0x03FFU: return "ObsoletePhotoshopTag2";
    case 0x0404U: return "IPTCData";
    case 0x0421U: return "VersionInfo";
    case 0x0401U: return "WorkingPath";
    case 0x0403U: return "ObsoletePhotoshopTag3";
    case 0x0405U: return "RawImageMode";
    case 0x0400U: return "TargetLayerID";
    case 0x0402U: return "LayersGroupInfo";
    case 0x0406U: return "JPEG_Quality";
    case 0x0408U: return "GridGuidesInfo";
    case 0x0409U: return "PhotoshopBGRThumbnail";
    case 0x040AU: return "CopyrightFlag";
    case 0x040BU: return "URL";
    case 0x040CU: return "PhotoshopThumbnail";
    case 0x040DU: return "GlobalAngle";
    case 0x040EU: return "ColorSamplersResource";
    case 0x040FU: return "ICC_Profile";
    case 0x0BB7U: return "ClippingPathName";
    case 0x0410U: return "Watermark";
    case 0x0411U: return "ICC_Untagged";
    case 0x0412U: return "EffectsVisible";
    case 0x0413U: return "SpotHalftone";
    case 0x0414U: return "IDsBaseValue";
    case 0x0415U: return "UnicodeAlphaNames";
    case 0x0416U: return "IndexedColorTableCount";
    case 0x0417U: return "TransparentIndex";
    case 0x0419U: return "GlobalAltitude";
    case 0x041AU: return "SliceInfo";
    case 0x041BU: return "WorkflowURL";
    case 0x041CU: return "JumpToXPEP";
    case 0x041DU: return "AlphaIdentifiers";
    case 0x041EU: return "URL_List";
    case 0x0422U: return "EXIFInfo";
    case 0x0423U: return "ExifInfo2";
    case 0x0424U: return "XMP";
    case 0x0425U: return "IPTCDigest";
    case 0x0426U: return "PrintScaleInfo";
    case 0x0428U: return "PixelInfo";
    case 0x0429U: return "LayerComps";
    case 0x042AU: return "AlternateDuotoneColors";
    case 0x042BU: return "AlternateSpotColors";
    case 0x042DU: return "LayerSelectionIDs";
    case 0x042EU: return "HDRToningInfo";
    case 0x042FU: return "PrintInfo";
    case 0x0430U: return "LayerGroupsEnabledID";
    case 0x0431U: return "ColorSamplersResource2";
    case 0x0432U: return "MeasurementScale";
    case 0x0433U: return "TimelineInfo";
    case 0x0434U: return "SheetDisclosure";
    case 0x0435U: return "ChannelOptions";
    case 0x0436U: return "OnionSkins";
    case 0x0438U: return "CountInfo";
    case 0x043AU: return "PrintInfo2";
    case 0x043BU: return "PrintStyle";
    case 0x043CU: return "MacintoshNSPrintInfo";
    case 0x043DU: return "WindowsDEVMODE";
    case 0x043EU: return "AutoSaveFilePath";
    case 0x043FU: return "AutoSaveFormat";
    case 0x0440U: return "PathSelectionState";
    case 0x0BB8U: return "OriginPathInfo";
    case 0x1B58U: return "ImageReadyVariables";
    case 0x1B59U: return "ImageReadyDataSets";
    case 0x1F40U: return "LightroomWorkflow";
    case 0x2710U: return "PrintFlagsInfo";
    default: return {};
    }
}

PhotoshopIrbDecodeResult
decode_photoshop_irb(std::span<const std::byte> irb_bytes, MetaStore& store,
                     const PhotoshopIrbDecodeOptions& options) noexcept
{
    PhotoshopIrbDecodeResult result;

    if (irb_bytes.empty() || !match(irb_bytes, 0, "8BIM", 4)) {
        result.status = PhotoshopIrbDecodeStatus::Unsupported;
        return result;
    }

    const uint64_t max_total = options.limits.max_total_bytes;
    if (max_total != 0U && irb_bytes.size() > max_total) {
        result.status = PhotoshopIrbDecodeStatus::LimitExceeded;
        return result;
    }

    const BlockId block              = store.add_block(BlockInfo {});
    const size_t trailing_zero_start = find_trailing_zero_padding_start(
        irb_bytes);

    uint64_t total_value_bytes = 0;
    uint64_t p                 = 0;
    uint32_t order             = 0;
    while (p < irb_bytes.size()) {
        if (order >= options.limits.max_resources) {
            result.status = PhotoshopIrbDecodeStatus::LimitExceeded;
            return result;
        }
        if (p + 4 > irb_bytes.size()) {
            break;
        }
        if (!match(irb_bytes, p, "8BIM", 4)) {
            if (p >= static_cast<uint64_t>(trailing_zero_start)) {
                break;
            }
            result.status = PhotoshopIrbDecodeStatus::Malformed;
            return result;
        }
        p += 4;

        uint16_t resource_id = 0;
        if (!read_u16be(irb_bytes, p, &resource_id)) {
            result.status = PhotoshopIrbDecodeStatus::Malformed;
            return result;
        }
        p += 2;

        if (p >= irb_bytes.size()) {
            result.status = PhotoshopIrbDecodeStatus::Malformed;
            return result;
        }
        const uint8_t name_len    = u8(irb_bytes[p]);
        const uint64_t name_total = pad2(static_cast<uint64_t>(1 + name_len));
        if (p + name_total > irb_bytes.size()) {
            result.status = PhotoshopIrbDecodeStatus::Malformed;
            return result;
        }
        p += name_total;

        uint32_t data_len32 = 0;
        if (!read_u32be(irb_bytes, p, &data_len32)) {
            result.status = PhotoshopIrbDecodeStatus::Malformed;
            return result;
        }
        p += 4;

        const uint64_t data_len = static_cast<uint64_t>(data_len32);
        if (data_len > options.limits.max_resource_len) {
            result.status = PhotoshopIrbDecodeStatus::LimitExceeded;
            return result;
        }

        const uint64_t data_off = p;
        const uint64_t padded   = pad2(data_len);
        if (data_off + padded > irb_bytes.size()) {
            result.status = PhotoshopIrbDecodeStatus::Malformed;
            return result;
        }

        total_value_bytes += data_len;
        if (max_total != 0U && total_value_bytes > max_total) {
            result.status = PhotoshopIrbDecodeStatus::LimitExceeded;
            return result;
        }

        const std::span<const std::byte> payload
            = irb_bytes.subspan(static_cast<size_t>(data_off),
                                static_cast<size_t>(data_len));

        Entry entry;
        entry.key                   = make_photoshop_irb_key(resource_id);
        entry.value                 = make_bytes(store.arena(), payload);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = static_cast<uint32_t>(data_len);

        (void)store.add_entry(entry);
        result.resources_decoded += 1;
        result.entries_decoded += 1;

        decode_known_resource_fields(payload, resource_id, options, store,
                                     block, order, &result);

        if (options.decode_iptc_iim && resource_id == 0x0404U) {
            const IptcIimDecodeResult iptc
                = decode_iptc_iim(payload, store, EntryFlags::Derived,
                                  options.iptc);
            if (iptc.status == IptcIimDecodeStatus::Ok) {
                result.iptc_entries_decoded += iptc.entries_decoded;
            }
        }
        if (options.decode_xmp_packet && resource_id == 0x0424U) {
            const XmpDecodeResult xmp = decode_xmp_packet(payload, store,
                                                          EntryFlags::Derived,
                                                          options.xmp);
            if (xmp.status == XmpDecodeStatus::Ok
                || xmp.status == XmpDecodeStatus::OutputTruncated) {
                result.xmp_entries_decoded += xmp.entries_decoded;
            }
        }
        if (options.decode_icc_profile && resource_id == 0x040FU) {
            const IccDecodeResult icc = decode_icc_profile(payload, store,
                                                           options.icc);
            if (icc.status == IccDecodeStatus::Ok
                || icc.status == IccDecodeStatus::Malformed) {
                result.icc_entries_decoded += icc.entries_decoded;
            }
        }

        order += 1;
        p = data_off + padded;
    }

    return result;
}

PhotoshopIrbDecodeResult
measure_photoshop_irb(std::span<const std::byte> irb_bytes,
                      const PhotoshopIrbDecodeOptions& options) noexcept
{
    MetaStore scratch;
    return decode_photoshop_irb(irb_bytes, scratch, options);
}

}  // namespace openmeta
