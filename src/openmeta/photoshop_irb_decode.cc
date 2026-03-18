#include "openmeta/photoshop_irb_decode.h"

#include <cstring>
#include <string>

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
        switch (resource_id) {
        case 0x03EDU:
            decode_resolution_info(payload, store, block, order, result);
            break;
        case 0x0421U:
            decode_version_info(payload, store, block, order, result);
            break;
        case 0x03F3U:
            decode_u8_scalar_resource(payload, resource_id, "PrintFlags", store,
                                      block, order, result);
            break;
        case 0x03FBU:
            decode_u8_scalar_resource(payload, resource_id, "EffectiveBW",
                                      store, block, order, result);
            break;
        case 0x0400U:
            decode_u16_scalar_resource(payload, resource_id, "TargetLayerID",
                                       store, block, order, result);
            break;
        case 0x0402U:
            decode_u16_list_resource(payload, resource_id,
                                     "LayersGroupInfoCount", "LayersGroupInfo",
                                     store, block, order, result);
            break;
        case 0x0406U:
            decode_jpeg_quality(payload, store, block, order, result);
            break;
        case 0x040BU:
            decode_ascii_text_resource(payload, resource_id, "URL", store,
                                       block, order, result);
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
        case 0x041EU:
            decode_url_list(payload, store, block, order, result);
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
        case 0x0412U:
            decode_u8_scalar_resource(payload, resource_id, "EffectsVisible",
                                      store, block, order, result);
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
        case 0x0430U:
            decode_u8_scalar_resource(payload, resource_id,
                                      "LayerGroupsEnabledID", store, block,
                                      order, result);
            break;
        case 0x0435U:
            decode_channel_options(payload, store, block, order, result);
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
