#include "openmeta/photoshop_irb_decode.h"

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


    static uint64_t pad2(uint64_t n) noexcept { return (n + 1U) & ~1ULL; }

}  // namespace

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

    const BlockId block = store.add_block(BlockInfo {});

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
            // Some variants may pad with zeros; treat trailing zeros as EOF.
            bool only_zeros = true;
            for (uint64_t i = p; i < irb_bytes.size(); ++i) {
                if (u8(irb_bytes[i]) != 0U) {
                    only_zeros = false;
                    break;
                }
            }
            if (only_zeros) {
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

        // IPTC/NAA resource.
        if (options.decode_iptc_iim && resource_id == 0x0404) {
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

}  // namespace openmeta
