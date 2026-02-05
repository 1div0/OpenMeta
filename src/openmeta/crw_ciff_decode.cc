#include "crw_ciff_decode_internal.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <array>
#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace openmeta::ciff_internal {
namespace {

    static uint8_t u8(std::byte b) noexcept { return static_cast<uint8_t>(b); }


    struct CiffConfig final {
        bool le = true;
    };


    static bool read_u16le(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 0U)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 8U);
        *out = v;
        return true;
    }


    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 8U)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 0U);
        *out = v;
        return true;
    }


    static bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4 > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 0])) << 0U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 1])) << 8U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 2])) << 16U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 3])) << 24U;
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
        v |= static_cast<uint32_t>(u8(bytes[offset + 0])) << 24U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 1])) << 16U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 2])) << 8U;
        v |= static_cast<uint32_t>(u8(bytes[offset + 3])) << 0U;
        *out = v;
        return true;
    }


    static bool read_u16(const CiffConfig& cfg,
                         std::span<const std::byte> bytes, uint64_t offset,
                         uint16_t* out) noexcept
    {
        return cfg.le ? read_u16le(bytes, offset, out)
                      : read_u16be(bytes, offset, out);
    }


    static bool read_u32(const CiffConfig& cfg,
                         std::span<const std::byte> bytes, uint64_t offset,
                         uint32_t* out) noexcept
    {
        return cfg.le ? read_u32le(bytes, offset, out)
                      : read_u32be(bytes, offset, out);
    }


    static void update_status(ExifDecodeResult* out,
                              ExifDecodeStatus in) noexcept
    {
        if (!out) {
            return;
        }
        if (out->status == ExifDecodeStatus::LimitExceeded) {
            return;
        }
        if (in == ExifDecodeStatus::LimitExceeded) {
            out->status = in;
            return;
        }
        if (out->status == ExifDecodeStatus::Malformed) {
            return;
        }
        if (in == ExifDecodeStatus::Malformed) {
            out->status = in;
            return;
        }
        if (out->status == ExifDecodeStatus::OutputTruncated) {
            return;
        }
        if (in == ExifDecodeStatus::OutputTruncated) {
            out->status = in;
            return;
        }
        if (out->status == ExifDecodeStatus::Ok) {
            return;
        }
        if (in == ExifDecodeStatus::Ok) {
            out->status = in;
            return;
        }
    }


    static bool contains_nul(std::span<const std::byte> bytes) noexcept
    {
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] == std::byte { 0 }) {
                return true;
            }
        }
        return false;
    }


    static MetaValue decode_text_value(ByteArena& arena,
                                       std::span<const std::byte> raw,
                                       TextEncoding enc) noexcept
    {
        if (raw.empty()) {
            return MetaValue {};
        }

        size_t trimmed = raw.size();
        if (raw[trimmed - 1] == std::byte { 0 }) {
            trimmed -= 1;
        }
        const std::span<const std::byte> payload = raw.subspan(0, trimmed);
        if (contains_nul(payload)) {
            return make_bytes(arena, raw);
        }

        const std::string_view text(reinterpret_cast<const char*>(
                                        payload.data()),
                                    payload.size());
        return make_text(arena, text, enc);
    }


    static MetaValue decode_u16_array(const CiffConfig& cfg, ByteArena& arena,
                                      std::span<const std::byte> raw,
                                      ExifDecodeResult* status_out) noexcept
    {
        if (raw.size() == 2) {
            uint16_t v = 0;
            if (!read_u16(cfg, raw, 0, &v)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                return MetaValue {};
            }
            return make_u16(v);
        }
        if (raw.size() % 2U != 0) {
            return make_bytes(arena, raw);
        }
        const uint64_t count64 = raw.size() / 2U;
        if (count64 > UINT32_MAX) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return MetaValue {};
        }
        MetaValue v;
        v.kind      = MetaValueKind::Array;
        v.elem_type = MetaElementType::U16;
        v.count     = static_cast<uint32_t>(count64);
        v.data.span = arena.allocate(static_cast<uint32_t>(v.count * 2U),
                                     alignof(uint16_t));
        const std::span<std::byte> dst = arena.span_mut(v.data.span);
        for (uint32_t i = 0; i < v.count; ++i) {
            uint16_t value = 0;
            if (!read_u16(cfg, raw, static_cast<uint64_t>(i) * 2U, &value)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                break;
            }
            std::memcpy(dst.data() + i * 2U, &value, 2U);
        }
        return v;
    }


    static MetaValue decode_u32_array(const CiffConfig& cfg, ByteArena& arena,
                                      std::span<const std::byte> raw,
                                      ExifDecodeResult* status_out) noexcept
    {
        if (raw.size() == 4) {
            uint32_t v = 0;
            if (!read_u32(cfg, raw, 0, &v)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                return MetaValue {};
            }
            return make_u32(v);
        }
        if (raw.size() % 4U != 0) {
            return make_bytes(arena, raw);
        }
        const uint64_t count64 = raw.size() / 4U;
        if (count64 > UINT32_MAX) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return MetaValue {};
        }
        MetaValue v;
        v.kind      = MetaValueKind::Array;
        v.elem_type = MetaElementType::U32;
        v.count     = static_cast<uint32_t>(count64);
        v.data.span = arena.allocate(static_cast<uint32_t>(v.count * 4U),
                                     alignof(uint32_t));
        const std::span<std::byte> dst = arena.span_mut(v.data.span);
        for (uint32_t i = 0; i < v.count; ++i) {
            uint32_t value = 0;
            if (!read_u32(cfg, raw, static_cast<uint64_t>(i) * 4U, &value)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                break;
            }
            std::memcpy(dst.data() + i * 4U, &value, 4U);
        }
        return v;
    }


    static uint16_t ciff_tag_id(uint16_t tag) noexcept
    {
        return static_cast<uint16_t>(tag & 0x3fffU);
    }

    static uint16_t ciff_type_bits(uint16_t tag) noexcept
    {
        return static_cast<uint16_t>(tag & 0x3800U);
    }

    static uint16_t ciff_loc_bits(uint16_t tag) noexcept
    {
        return static_cast<uint16_t>(tag & 0xc000U);
    }

    static bool ciff_is_directory(uint16_t tag) noexcept
    {
        const uint16_t t = ciff_type_bits(tag);
        return t == 0x2800U || t == 0x3000U;
    }


    static bool decode_directory(const CiffConfig& cfg,
                                 std::span<const std::byte> dir_bytes,
                                 std::string_view ifd_token, MetaStore& store,
                                 const ExifDecodeLimits& limits,
                                 ExifDecodeResult* status_out, uint32_t depth,
                                 uint32_t* dir_index) noexcept
    {
        if (dir_bytes.size() < 6) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }
        if (depth > 32) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return false;
        }
        if (status_out && status_out->ifds_written >= limits.max_ifds) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return false;
        }

        uint32_t entry_off32 = 0;
        if (!read_u32(cfg, dir_bytes, dir_bytes.size() - 4, &entry_off32)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }
        const uint64_t entry_off = entry_off32;
        if (entry_off > dir_bytes.size() - 2) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }

        uint16_t entry_count = 0;
        if (!read_u16(cfg, dir_bytes, entry_off, &entry_count)) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }

        const uint64_t entries_start = entry_off + 2;
        const uint64_t needed
            = entries_start + uint64_t(entry_count) * 10ULL;
        if (needed > dir_bytes.size()) {
            update_status(status_out, ExifDecodeStatus::Malformed);
            return false;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return false;
        }

        const ByteSpan ifd_span = store.arena().append_string(ifd_token);

        bool any = false;

        if (status_out) {
            status_out->ifds_written += 1U;
        }

        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_start + uint64_t(i) * 10ULL;

            uint16_t tag = 0;
            if (!read_u16(cfg, dir_bytes, eoff + 0, &tag)) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                break;
            }

            const uint16_t tag_id = ciff_tag_id(tag);
            const uint16_t loc    = ciff_loc_bits(tag);

            uint64_t value_off   = 0;
            uint64_t value_bytes = 0;

            if (loc == 0x4000U) {  // directoryData
                value_off   = eoff + 2;
                value_bytes = 8;
            } else if (loc == 0x0000U) {  // valueData
                uint32_t size32 = 0;
                uint32_t off32  = 0;
                if (!read_u32(cfg, dir_bytes, eoff + 2, &size32)
                    || !read_u32(cfg, dir_bytes, eoff + 6, &off32)) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                    break;
                }
                value_off   = off32;
                value_bytes = size32;

                // Ensure the referenced region doesn't overlap the entry header.
                if (value_off < eoff) {
                    if (value_bytes > (eoff - value_off)) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                        continue;
                    }
                } else {
                    if (value_off < eoff + 10) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                        continue;
                    }
                }
            } else {
                update_status(status_out, ExifDecodeStatus::Malformed);
                continue;
            }

            if (value_off > dir_bytes.size()
                || value_bytes > dir_bytes.size() - value_off) {
                update_status(status_out, ExifDecodeStatus::Malformed);
                continue;
            }

            if (ciff_is_directory(tag)) {
                if (!dir_index) {
                    continue;
                }
                const uint32_t idx = (*dir_index)++;
                std::array<char, 32> name {};
                const int n = std::snprintf(name.data(), name.size(),
                                            "ciff_%04X_%u",
                                            static_cast<unsigned>(tag_id),
                                            static_cast<unsigned>(idx));
                if (n <= 0 || static_cast<size_t>(n) >= name.size()) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                    continue;
                }
                const std::string_view child_token(name.data(),
                                                   static_cast<size_t>(n));
                const std::span<const std::byte> child
                    = dir_bytes.subspan(static_cast<size_t>(value_off),
                                        static_cast<size_t>(value_bytes));
                (void)decode_directory(cfg, child, child_token, store, limits,
                                       status_out, depth + 1, dir_index);
                any = true;
                continue;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                break;
            }

            Entry entry;
            entry.key.kind              = MetaKeyKind::ExifTag;
            entry.key.data.exif_tag.ifd = ifd_span;
            entry.key.data.exif_tag.tag = tag_id;
            entry.origin.block          = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Other, tag };
            entry.origin.wire_count     = (value_bytes > UINT32_MAX)
                                              ? UINT32_MAX
                                              : static_cast<uint32_t>(value_bytes);

            if (value_bytes > limits.max_value_bytes) {
                entry.flags |= EntryFlags::Truncated;
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            } else {
                const std::span<const std::byte> raw = dir_bytes.subspan(
                    static_cast<size_t>(value_off),
                    static_cast<size_t>(value_bytes));

                switch (ciff_type_bits(tag)) {
                case 0x0000: {  // unsignedByte
                    if (raw.size() == 1) {
                        entry.value = make_u8(u8(raw[0]));
                    } else {
                        MetaValue v;
                        v.kind      = MetaValueKind::Array;
                        v.elem_type = MetaElementType::U8;
                        v.count     = (raw.size() > UINT32_MAX)
                                          ? UINT32_MAX
                                          : static_cast<uint32_t>(raw.size());
                        v.data.span = store.arena().append(raw);
                        entry.value = v;
                    }
                    break;
                }
                case 0x0800:  // asciiString
                    entry.value = decode_text_value(store.arena(), raw,
                                                    TextEncoding::Ascii);
                    break;
                case 0x1000:  // unsignedShort
                    entry.value = decode_u16_array(cfg, store.arena(), raw,
                                                   status_out);
                    break;
                case 0x1800:  // unsignedLong
                    entry.value = decode_u32_array(cfg, store.arena(), raw,
                                                   status_out);
                    break;
                case 0x2000:  // undefined
                default: entry.value = make_bytes(store.arena(), raw); break;
                }
            }

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1U;
            }
            any = true;
        }

        return any;
    }

}  // namespace

bool
decode_crw_ciff(std::span<const std::byte> file_bytes, MetaStore& store,
                const ExifDecodeLimits& limits,
                ExifDecodeResult* status_out) noexcept
{
    if (status_out) {
        status_out->status = ExifDecodeStatus::Unsupported;
    }
    if (file_bytes.size() < 14) {
        update_status(status_out, ExifDecodeStatus::Unsupported);
        return false;
    }

    const uint8_t b0 = u8(file_bytes[0]);
    const uint8_t b1 = u8(file_bytes[1]);
    const bool le    = (b0 == 0x49 && b1 == 0x49);
    const bool be    = (b0 == 0x4D && b1 == 0x4D);
    if (!le && !be) {
        update_status(status_out, ExifDecodeStatus::Unsupported);
        return false;
    }

    if (std::memcmp(file_bytes.data() + 6, "HEAPCCDR", 8) != 0) {
        update_status(status_out, ExifDecodeStatus::Unsupported);
        return false;
    }

    CiffConfig cfg;
    cfg.le = le;

    uint32_t root_off = 0;
    if (!read_u32(cfg, file_bytes, 2, &root_off)) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return false;
    }
    if (root_off < 14U || static_cast<uint64_t>(root_off) > file_bytes.size()) {
        update_status(status_out, ExifDecodeStatus::Malformed);
        return false;
    }

    const std::span<const std::byte> root
        = file_bytes.subspan(static_cast<size_t>(root_off));
    uint32_t dir_index = 0;
    const bool any
        = decode_directory(cfg, root, "ciff_root", store, limits, status_out,
                           0, &dir_index);
    if (any) {
        update_status(status_out, ExifDecodeStatus::Ok);
    }
    return any;
}

}  // namespace openmeta::ciff_internal
