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


    static void decode_known_resource_fields(std::span<const std::byte> payload,
                                             uint16_t resource_id,
                                             MetaStore& store, BlockId block,
                                             uint32_t order,
                                             PhotoshopIrbDecodeResult* result)
    {
        switch (resource_id) {
        case 0x03EDU:
            decode_resolution_info(payload, store, block, order, result);
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
        case 0x040BU:
            decode_ascii_text_resource(payload, resource_id, "URL", store,
                                       block, order, result);
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
        default: break;
        }
    }

}  // namespace

std::string_view
photoshop_irb_resource_name(uint16_t resource_id) noexcept
{
    switch (resource_id) {
    case 0x03EDU: return "ResolutionInfo";
    case 0x03F3U: return "PrintFlags";
    case 0x03FBU: return "EffectiveBW";
    case 0x0404U: return "IPTCData";
    case 0x0400U: return "TargetLayerID";
    case 0x040AU: return "CopyrightFlag";
    case 0x040BU: return "URL";
    case 0x040DU: return "GlobalAngle";
    case 0x0410U: return "Watermark";
    case 0x0411U: return "ICC_Untagged";
    case 0x0412U: return "EffectsVisible";
    case 0x0414U: return "IDsBaseValue";
    case 0x0416U: return "IndexedColorTableCount";
    case 0x0417U: return "TransparentIndex";
    case 0x0419U: return "GlobalAltitude";
    case 0x0422U: return "EXIFInfo";
    case 0x0423U: return "ExifInfo2";
    case 0x0425U: return "IPTCDigest";
    case 0x0426U: return "PrintScaleInfo";
    case 0x0428U: return "PixelInfo";
    case 0x0430U: return "LayerGroupsEnabledID";
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

        decode_known_resource_fields(payload, resource_id, store, block, order,
                                     &result);

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
