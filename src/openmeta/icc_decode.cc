#include "openmeta/icc_decode.h"

#include "openmeta/meta_value.h"

namespace openmeta {
namespace {

    static uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
    }

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

    static void update_status(IccDecodeStatus* out, IccDecodeStatus in) noexcept
    {
        if (!out || in == IccDecodeStatus::Ok) {
            return;
        }
        if (*out == IccDecodeStatus::LimitExceeded) {
            return;
        }
        if (in == IccDecodeStatus::LimitExceeded) {
            *out = in;
            return;
        }
        if (*out == IccDecodeStatus::Malformed) {
            return;
        }
        if (in == IccDecodeStatus::Malformed) {
            *out = in;
            return;
        }
        if (*out == IccDecodeStatus::Ok) {
            *out = in;
        }
    }

    static void emit_header_bytes(MetaStore& store, BlockId block,
                                  uint32_t order, uint32_t offset,
                                  std::span<const std::byte> bytes,
                                  EntryFlags flags)
    {
        Entry e;
        e.key                   = make_icc_header_field_key(offset);
        e.value                 = make_bytes(store.arena(), bytes);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = static_cast<uint32_t>(bytes.size());
        e.flags                 = flags;
        (void)store.add_entry(e);
    }

    static void emit_header_u32(MetaStore& store, BlockId block, uint32_t order,
                                uint32_t offset, uint32_t value,
                                EntryFlags flags)
    {
        Entry e;
        e.key                   = make_icc_header_field_key(offset);
        e.value                 = make_u32(value);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = flags;
        (void)store.add_entry(e);
    }

    static void emit_header_u16_array(MetaStore& store, BlockId block,
                                      uint32_t order, uint32_t offset,
                                      std::span<const uint16_t> values,
                                      EntryFlags flags)
    {
        Entry e;
        e.key                   = make_icc_header_field_key(offset);
        e.value                 = make_u16_array(store.arena(), values);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = static_cast<uint32_t>(values.size());
        e.flags                 = flags;
        (void)store.add_entry(e);
    }

}  // namespace

IccDecodeResult
decode_icc_profile(std::span<const std::byte> icc_bytes, MetaStore& store,
                   const IccDecodeOptions& options) noexcept
{
    IccDecodeResult result;

    // ICC header is 128 bytes, followed by tag count (4 bytes).
    if (icc_bytes.size() < 132) {
        result.status = IccDecodeStatus::Unsupported;
        return result;
    }
    if (!match(icc_bytes, 36, "acsp", 4)) {
        result.status = IccDecodeStatus::Unsupported;
        return result;
    }

    uint32_t declared_size = 0;
    if (!read_u32be(icc_bytes, 0, &declared_size)) {
        result.status = IccDecodeStatus::Malformed;
        return result;
    }
    if (declared_size != 0U && declared_size != icc_bytes.size()) {
        // Keep going, but flag the profile as malformed.
        update_status(&result.status, IccDecodeStatus::Malformed);
    }

    const BlockId block = store.add_block(BlockInfo {});
    EntryFlags flags    = EntryFlags::None;

    uint32_t order = 0;
    // Emit common header fields (raw bytes unless scalar is clearly useful).
    emit_header_u32(store, block, order++, 0, declared_size, flags);
    emit_header_bytes(store, block, order++, 4, icc_bytes.subspan(4, 4), flags);   // CMM
    uint32_t ver = 0;
    if (read_u32be(icc_bytes, 8, &ver)) {
        emit_header_u32(store, block, order++, 8, ver, flags);
    } else {
        emit_header_bytes(store, block, order++, 8, icc_bytes.subspan(8, 4), flags);
        update_status(&result.status, IccDecodeStatus::Malformed);
    }
    emit_header_bytes(store, block, order++, 12, icc_bytes.subspan(12, 4), flags);  // class
    emit_header_bytes(store, block, order++, 16, icc_bytes.subspan(16, 4), flags);  // data space
    emit_header_bytes(store, block, order++, 20, icc_bytes.subspan(20, 4), flags);  // PCS

    // Date/time: 6x u16 big-endian.
    uint16_t dt[6] = {};
    bool dt_ok     = true;
    for (uint32_t i = 0; i < 6; ++i) {
        if (!read_u16be(icc_bytes, 24 + i * 2, &dt[i])) {
            dt_ok = false;
        }
    }
    if (dt_ok) {
        emit_header_u16_array(store, block, order++, 24,
                              std::span<const uint16_t>(dt, 6), flags);
    } else {
        emit_header_bytes(store, block, order++, 24, icc_bytes.subspan(24, 12), flags);
        update_status(&result.status, IccDecodeStatus::Malformed);
    }

    emit_header_bytes(store, block, order++, 36, icc_bytes.subspan(36, 4), flags);  // "acsp"
    emit_header_bytes(store, block, order++, 40, icc_bytes.subspan(40, 4), flags);  // platform
    emit_header_bytes(store, block, order++, 44, icc_bytes.subspan(44, 4), flags);  // flags
    emit_header_bytes(store, block, order++, 48, icc_bytes.subspan(48, 4), flags);  // manufacturer
    emit_header_bytes(store, block, order++, 52, icc_bytes.subspan(52, 4), flags);  // model
    emit_header_bytes(store, block, order++, 56, icc_bytes.subspan(56, 8), flags);  // attributes
    emit_header_bytes(store, block, order++, 64, icc_bytes.subspan(64, 4), flags);  // intent
    emit_header_bytes(store, block, order++, 68, icc_bytes.subspan(68, 12), flags); // illuminant
    emit_header_bytes(store, block, order++, 80, icc_bytes.subspan(80, 4), flags);  // creator
    emit_header_bytes(store, block, order++, 84, icc_bytes.subspan(84, 16), flags); // profile ID

    result.entries_decoded += order;

    uint32_t tag_count = 0;
    if (!read_u32be(icc_bytes, 128, &tag_count)) {
        result.status = IccDecodeStatus::Malformed;
        return result;
    }

    if (tag_count > options.limits.max_tags) {
        result.status = IccDecodeStatus::LimitExceeded;
        return result;
    }

    const uint64_t table_bytes = 4ULL + static_cast<uint64_t>(tag_count) * 12ULL;
    if (128ULL + table_bytes > icc_bytes.size()) {
        result.status = IccDecodeStatus::Malformed;
        return result;
    }

    uint64_t total_tag_bytes = 0;
    for (uint32_t i = 0; i < tag_count; ++i) {
        const uint64_t eoff = 132ULL + static_cast<uint64_t>(i) * 12ULL;
        uint32_t sig  = 0;
        uint32_t off  = 0;
        uint32_t size = 0;
        if (!read_u32be(icc_bytes, eoff + 0, &sig)
            || !read_u32be(icc_bytes, eoff + 4, &off)
            || !read_u32be(icc_bytes, eoff + 8, &size)) {
            update_status(&result.status, IccDecodeStatus::Malformed);
            continue;
        }

        if (size > options.limits.max_tag_bytes) {
            update_status(&result.status, IccDecodeStatus::LimitExceeded);
            continue;
        }

        total_tag_bytes += size;
        if (options.limits.max_total_tag_bytes != 0U
            && total_tag_bytes > options.limits.max_total_tag_bytes) {
            update_status(&result.status, IccDecodeStatus::LimitExceeded);
            continue;
        }

        const uint64_t uoff  = static_cast<uint64_t>(off);
        const uint64_t usize = static_cast<uint64_t>(size);
        if (uoff + usize > icc_bytes.size()) {
            update_status(&result.status, IccDecodeStatus::Malformed);
            continue;
        }

        const std::span<const std::byte> tag_bytes
            = icc_bytes.subspan(static_cast<size_t>(uoff),
                                static_cast<size_t>(usize));

        Entry e;
        e.key                   = make_icc_tag_key(sig);
        e.value                 = make_bytes(store.arena(), tag_bytes);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = static_cast<uint32_t>(size);
        (void)store.add_entry(e);
        result.entries_decoded += 1;
        order += 1;
    }

    return result;
}

}  // namespace openmeta

