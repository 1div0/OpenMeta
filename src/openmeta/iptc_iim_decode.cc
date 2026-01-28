#include "openmeta/iptc_iim_decode.h"

namespace openmeta {
namespace {

    static uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
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

    static bool read_iptc_length(std::span<const std::byte> bytes, uint64_t off,
                                 uint64_t* value_len, uint64_t* header_len) noexcept
    {
        // Base length field is 2 bytes. If MSB is set, the low 15 bits specify
        // the number of subsequent bytes used to encode the real length.
        uint16_t len16 = 0;
        if (!read_u16be(bytes, off, &len16)) {
            return false;
        }
        if ((len16 & 0x8000U) == 0) {
            *value_len  = static_cast<uint64_t>(len16);
            *header_len = 2;
            return true;
        }
        const uint16_t nbytes = static_cast<uint16_t>(len16 & 0x7FFFU);
        if (nbytes == 0U || nbytes > 4U) {
            return false;
        }
        if (off + 2 + nbytes > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        for (uint16_t i = 0; i < nbytes; ++i) {
            v = (v << 8) | static_cast<uint32_t>(u8(bytes[off + 2 + i]));
        }
        *value_len  = static_cast<uint64_t>(v);
        *header_len = static_cast<uint64_t>(2 + nbytes);
        return true;
    }

}  // namespace

IptcIimDecodeResult
decode_iptc_iim(std::span<const std::byte> iptc_bytes, MetaStore& store,
                EntryFlags flags, const IptcIimDecodeOptions& options) noexcept
{
    IptcIimDecodeResult result;

    if (iptc_bytes.empty() || u8(iptc_bytes[0]) != 0x1C) {
        result.status = IptcIimDecodeStatus::Unsupported;
        return result;
    }

    const uint64_t max_total = options.limits.max_total_bytes;
    if (max_total != 0U && iptc_bytes.size() > max_total) {
        result.status = IptcIimDecodeStatus::LimitExceeded;
        return result;
    }

    const BlockId block = store.add_block(BlockInfo {});

    uint64_t total_value_bytes = 0;
    uint64_t p                 = 0;
    uint32_t order             = 0;
    while (p < iptc_bytes.size()) {
        if (order >= options.limits.max_datasets) {
            result.status = IptcIimDecodeStatus::LimitExceeded;
            return result;
        }

        // Marker + record + dataset + len(2+) => min 5 bytes.
        if (p + 5 > iptc_bytes.size()) {
            result.status = IptcIimDecodeStatus::Malformed;
            return result;
        }
        if (u8(iptc_bytes[p]) != 0x1C) {
            result.status = IptcIimDecodeStatus::Malformed;
            return result;
        }
        const uint8_t record  = u8(iptc_bytes[p + 1]);
        const uint8_t dataset = u8(iptc_bytes[p + 2]);

        uint64_t value_len  = 0;
        uint64_t header_len = 0;
        if (!read_iptc_length(iptc_bytes, p + 3, &value_len, &header_len)) {
            result.status = IptcIimDecodeStatus::Malformed;
            return result;
        }
        if (value_len > options.limits.max_dataset_bytes) {
            result.status = IptcIimDecodeStatus::LimitExceeded;
            return result;
        }

        const uint64_t value_off = p + 3 + header_len;
        if (value_off + value_len > iptc_bytes.size()) {
            result.status = IptcIimDecodeStatus::Malformed;
            return result;
        }

        total_value_bytes += value_len;
        if (max_total != 0U && total_value_bytes > max_total) {
            result.status = IptcIimDecodeStatus::LimitExceeded;
            return result;
        }

        const std::span<const std::byte> payload
            = iptc_bytes.subspan(static_cast<size_t>(value_off),
                                 static_cast<size_t>(value_len));

        Entry entry;
        entry.key                   = make_iptc_dataset_key(record, dataset);
        entry.value                 = make_bytes(store.arena(), payload);
        entry.origin.block          = block;
        entry.origin.order_in_block = order;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = static_cast<uint32_t>(value_len);
        entry.flags                 = flags;

        (void)store.add_entry(entry);
        result.entries_decoded += 1;
        order += 1;

        p = value_off + value_len;
    }

    return result;
}

}  // namespace openmeta
