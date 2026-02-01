#include "exif_tiff_decode_internal.h"

#include "openmeta/exif_tag_names.h"

#include <cstring>

namespace openmeta::exif_internal {

static bool
canon_is_printable_ascii(uint8_t c) noexcept
{
    return (c >= 0x20U && c <= 0x7EU) || c == '\t' || c == '\n' || c == '\r';
}

static bool
canon_looks_like_text(std::span<const std::byte> raw) noexcept
{
    if (raw.empty()) {
        return false;
    }

    size_t trimmed = raw.size();
    if (raw[trimmed - 1] == std::byte { 0 }) {
        trimmed -= 1;
    }
    if (trimmed == 0) {
        return false;
    }

    for (size_t i = 0; i < trimmed; ++i) {
        const uint8_t c = u8(raw[i]);
        if (c == 0) {
            return false;
        }
        if (!canon_is_printable_ascii(c)) {
            return false;
        }
    }
    return true;
}

static uint64_t
guess_canon_value_base(const TiffConfig& cfg,
                       std::span<const std::byte> tiff_bytes,
                       uint64_t maker_note_off, uint64_t maker_note_bytes,
                       uint16_t entry_count, uint64_t ifd_needed_bytes,
                       const ExifDecodeLimits& limits) noexcept
{
    if (tiff_bytes.empty() || maker_note_bytes == 0 || entry_count == 0
        || ifd_needed_bytes == 0) {
        return 0;
    }
    if (maker_note_off > tiff_bytes.size()
        || maker_note_bytes > (tiff_bytes.size() - maker_note_off)) {
        return 0;
    }

    const uint64_t entries_off = maker_note_off + 2ULL;
    if (uint64_t(entry_count) > (UINT64_MAX / 12ULL)) {
        return 0;
    }
    const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
    const uint64_t needed      = 2ULL + table_bytes + 4ULL;
    // Some Canon MakerNotes are stored as a truncated directory (count too
    // small) with out-of-line values placed elsewhere in the EXIF stream.
    // Treat maker_note_bytes as a soft bound: require only that the directory
    // itself fits in the available EXIF/TIFF buffer.
    if (needed > (tiff_bytes.size() - maker_note_off)) {
        return 0;
    }

    uint64_t min_off32 = UINT64_MAX;
    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        uint16_t type = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, eoff + 2, &type)) {
            break;
        }

        uint32_t count32        = 0;
        uint32_t value_or_off32 = 0;
        if (!read_tiff_u32(cfg, tiff_bytes, eoff + 4, &count32)
            || !read_tiff_u32(cfg, tiff_bytes, eoff + 8, &value_or_off32)) {
            break;
        }

        const uint64_t count = count32;
        const uint64_t unit  = tiff_type_size(type);
        if (unit == 0 || count == 0 || count > (UINT64_MAX / unit)) {
            continue;
        }
        const uint64_t value_bytes = count * unit;
        if (value_bytes <= 4) {
            continue;
        }
        if (value_bytes > limits.max_value_bytes) {
            continue;
        }

        const uint64_t off = uint64_t(value_or_off32);
        min_off32          = (off < min_off32) ? off : min_off32;
    }

    // Candidate bases:
    //  - 0: offsets are absolute (TIFF-relative).
    //  - maker_note_off: offsets are MakerNote-relative.
    //  - auto_base: offsets are relative to an adjusted base (ExifTool's
    //    "Adjusted MakerNotes base by ..."), chosen such that the earliest
    //    out-of-line value lands at the start of the MakerNote value area.
    const uint64_t base_abs = 0;
    const uint64_t base_mn  = maker_note_off;

    uint64_t base_auto = UINT64_MAX;
    if (min_off32 != UINT64_MAX) {
        const uint64_t value_area_off = maker_note_off + ifd_needed_bytes;
        if (min_off32 <= value_area_off) {
            base_auto = value_area_off - min_off32;
        }
    }

    struct Candidate final {
        uint64_t base  = 0;
        uint32_t score = 0;
        uint32_t in_mn = 0;
    };

    Candidate cands[3];
    cands[0].base = base_abs;
    cands[1].base = base_mn;
    cands[2].base = (base_auto != UINT64_MAX) ? base_auto : base_abs;

    for (size_t c = 0; c < 3; ++c) {
        Candidate& cand = cands[c];
        if (c == 2 && base_auto == UINT64_MAX) {
            continue;
        }

        uint16_t type = 0;
        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            if (!read_tiff_u16(cfg, tiff_bytes, eoff + 2, &type)) {
                break;
            }

            uint32_t count32        = 0;
            uint32_t value_or_off32 = 0;
            if (!read_tiff_u32(cfg, tiff_bytes, eoff + 4, &count32)
                || !read_tiff_u32(cfg, tiff_bytes, eoff + 8, &value_or_off32)) {
                break;
            }

            const uint64_t count = count32;
            const uint64_t unit  = tiff_type_size(type);
            if (unit == 0 || count == 0 || count > (UINT64_MAX / unit)) {
                continue;
            }
            const uint64_t value_bytes = count * unit;
            if (value_bytes <= 4 || value_bytes > limits.max_value_bytes) {
                continue;
            }

            const uint64_t off32 = uint64_t(value_or_off32);
            if (cand.base > (UINT64_MAX - off32)) {
                continue;
            }
            const uint64_t abs_off = cand.base + off32;

            if (abs_off + value_bytes > tiff_bytes.size()) {
                continue;
            }

            cand.score += 1;

            if (abs_off >= maker_note_off
                && (abs_off + value_bytes)
                       <= (maker_note_off + maker_note_bytes)) {
                cand.in_mn += 1;
                cand.score += 1;
                if (abs_off >= (maker_note_off + ifd_needed_bytes)) {
                    cand.score += 1;
                }
            }

            if (type == 2 || type == 129) {
                const std::span<const std::byte> raw
                    = tiff_bytes.subspan(static_cast<size_t>(abs_off),
                                         static_cast<size_t>(value_bytes));
                if (canon_looks_like_text(raw)) {
                    cand.score += 3;
                }
            }
        }
    }

    Candidate best = cands[0];
    for (size_t i = 1; i < 3; ++i) {
        const Candidate cand = cands[i];
        if (i == 2 && base_auto == UINT64_MAX) {
            continue;
        }
        if (cand.score > best.score) {
            best = cand;
            continue;
        }
        if (cand.score < best.score) {
            continue;
        }
        if (cand.in_mn > best.in_mn) {
            best = cand;
            continue;
        }
    }

    return best.base;
}

enum class CanonCustomMode : uint8_t {
    LowByteAsU8,
    U16,
};

static void
decode_canon_custom_word_table(const TiffConfig& cfg,
                               std::span<const std::byte> tiff_bytes,
                               uint64_t value_off, uint32_t count,
                               std::string_view ifd_name, uint16_t tag_base,
                               CanonCustomMode mode, MetaStore& store,
                               const ExifDecodeOptions& options,
                               ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || count == 0) {
        return;
    }
    if (count > options.limits.max_entries_per_ifd) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    uint16_t first = 0;
    bool has_first = read_tiff_u16(cfg, tiff_bytes, value_off, &first);

    uint32_t start = 0;
    if (has_first && count <= (UINT16_MAX / 2U)) {
        const uint16_t expected = static_cast<uint16_t>(count * 2U);
        if (first == expected) {
            start = 1;
        }
    }

    uint32_t order = 0;
    for (uint32_t i = start; i < count; ++i) {
        const uint32_t tag32 = uint32_t(tag_base) + (i - start);
        if (tag32 > 0xFFFFu) {
            break;
        }
        const uint16_t tag = static_cast<uint16_t>(tag32);

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        uint16_t w = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, value_off + uint64_t(i) * 2ULL,
                           &w)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = order++;
        entry.flags |= EntryFlags::Derived;

        if (mode == CanonCustomMode::LowByteAsU8) {
            entry.origin.wire_type  = WireType { WireFamily::Tiff, 1 };
            entry.origin.wire_count = 1;
            entry.value = make_u8(static_cast<uint8_t>(w & 0xFFu));
        } else {
            entry.origin.wire_type  = WireType { WireFamily::Tiff, 3 };
            entry.origin.wire_count = 1;
            entry.value             = make_u16(w);
        }

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}

static void
decode_canon_u16_table(const TiffConfig& cfg, std::span<const std::byte> bytes,
                       uint64_t value_off, uint32_t count,
                       std::string_view ifd_name, MetaStore& store,
                       const ExifDecodeOptions& options,
                       ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || count == 0) {
        return;
    }
    if (count > options.limits.max_entries_per_ifd) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (i > 0xFFFFu) {
            break;
        }

        const uint16_t tag = static_cast<uint16_t>(i);
        if (exif_tag_name(ifd_name, tag).empty()) {
            continue;
        }

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        uint16_t v = 0;
        if (!read_tiff_u16(cfg, bytes, value_off + uint64_t(i) * 2ULL, &v)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, 3 };
        entry.origin.wire_count     = 1;
        entry.value                 = make_u16(v);
        entry.flags |= EntryFlags::Derived;

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}


static void
decode_canon_u32_table(const TiffConfig& cfg, std::span<const std::byte> bytes,
                       uint64_t value_off, uint32_t count,
                       std::string_view ifd_name, MetaStore& store,
                       const ExifDecodeOptions& options,
                       ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || count == 0) {
        return;
    }
    if (count > options.limits.max_entries_per_ifd) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (i > 0xFFFFu) {
            break;
        }

        const uint16_t tag = static_cast<uint16_t>(i);
        if (exif_tag_name(ifd_name, tag).empty()) {
            continue;
        }

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        uint32_t v = 0;
        if (!read_tiff_u32(cfg, bytes, value_off + uint64_t(i) * 4ULL, &v)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, 4 };
        entry.origin.wire_count     = 1;
        entry.value                 = make_u32(v);
        entry.flags |= EntryFlags::Derived;

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}

static int32_t
canon_to_i32(uint32_t v) noexcept
{
    // Avoid implementation-defined conversions from uint32_t -> int32_t.
    return (v <= 0x7FFFFFFFu)
               ? static_cast<int32_t>(v)
               : static_cast<int32_t>(static_cast<int64_t>(v) - 4294967296LL);
}

static void
decode_canon_i32_table(const TiffConfig& cfg, std::span<const std::byte> bytes,
                       uint64_t value_off, uint32_t count,
                       std::string_view ifd_name, MetaStore& store,
                       const ExifDecodeOptions& options,
                       ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || count == 0) {
        return;
    }
    if (count > options.limits.max_entries_per_ifd) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    for (uint32_t i = 0; i < count; ++i) {
        if (i > 0xFFFFu) {
            break;
        }

        const uint16_t tag = static_cast<uint16_t>(i);
        if (exif_tag_name(ifd_name, tag).empty()) {
            continue;
        }

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        uint32_t v = 0;
        if (!read_tiff_u32(cfg, bytes, value_off + uint64_t(i) * 4ULL, &v)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, 9 };
        entry.origin.wire_count     = 1;
        entry.value                 = make_i32(canon_to_i32(v));
        entry.flags |= EntryFlags::Derived;

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}


static void
decode_canon_psinfo_table(std::span<const std::byte> bytes, uint64_t value_off,
                          uint64_t value_bytes, std::string_view ifd_name,
                          MetaStore& store, const ExifDecodeOptions& options,
                          ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty()) {
        return;
    }
    if (value_bytes == 0) {
        return;
    }
    if (value_off > bytes.size()) {
        return;
    }
    if (value_bytes > (bytes.size() - value_off)) {
        return;
    }

    // psinfo: fixed-layout Canon picture style table (byte offsets).
    // Most fields are int32, with a few u16 fields near the end.
    static constexpr uint16_t kUserDefTag1 = 0x00d8;
    static constexpr uint16_t kUserDefTag2 = 0x00da;
    static constexpr uint16_t kUserDefTag3 = 0x00dc;
    static constexpr uint16_t kMaxTag      = 0x00dc;

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    uint32_t order = 0;
    for (uint16_t tag = 0; tag <= kMaxTag; tag = uint16_t(tag + 2U)) {
        if ((uint64_t(tag) + 2U) > value_bytes) {
            break;
        }

        if (exif_tag_name(ifd_name, tag).empty()) {
            continue;
        }

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = order++;
        entry.flags |= EntryFlags::Derived;

        if (tag == kUserDefTag1 || tag == kUserDefTag2 || tag == kUserDefTag3) {
            if ((uint64_t(tag) + 2U) > value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }

            uint16_t v = 0;
            if (!read_u16le(bytes, value_off + tag, &v)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }
            entry.origin.wire_type  = WireType { WireFamily::Tiff, 3 };
            entry.origin.wire_count = 1;
            entry.value             = make_u16(v);
        } else {
            if ((uint64_t(tag) + 4U) > value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }

            uint32_t u = 0;
            if (!read_u32le(bytes, value_off + tag, &u)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }
            entry.origin.wire_type  = WireType { WireFamily::Tiff, 9 };
            entry.origin.wire_count = 1;
            entry.value             = make_i32(static_cast<int32_t>(u));
        }

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}


static bool
decode_canon_afinfo2_add_u16_scalar(
    const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
    uint64_t value_off, std::string_view mk_ifd0, BlockId block, uint32_t order,
    uint16_t tag, uint32_t word_index, MetaStore& store,
    const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
{
    if (status_out
        && (status_out->entries_decoded + 1U)
               > options.limits.max_total_entries) {
        update_status(status_out, ExifDecodeStatus::LimitExceeded);
        return false;
    }

    uint16_t v = 0;
    if (!read_tiff_u16(cfg, tiff_bytes, value_off + uint64_t(word_index) * 2ULL,
                       &v)) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::Malformed);
        }
        return false;
    }

    Entry entry;
    entry.key          = make_exif_tag_key(store.arena(), mk_ifd0, tag);
    entry.origin.block = block;
    entry.origin.order_in_block = order;
    entry.origin.wire_type      = WireType { WireFamily::Tiff, 3 };
    entry.origin.wire_count     = 1;
    entry.value                 = make_u16(v);
    entry.flags |= EntryFlags::Derived;

    (void)store.add_entry(entry);
    if (status_out) {
        status_out->entries_decoded += 1;
    }
    return true;
}

static bool
decode_canon_afinfo2(const TiffConfig& cfg,
                     std::span<const std::byte> tiff_bytes, uint64_t value_off,
                     uint64_t value_bytes, std::string_view mk_ifd0,
                     MetaStore& store, const ExifDecodeOptions& options,
                     ExifDecodeResult* status_out) noexcept
{
    if (mk_ifd0.empty()) {
        return false;
    }
    if (value_bytes < 16) {
        return false;
    }
    if (value_bytes > options.limits.max_value_bytes) {
        return false;
    }
    if (value_off + value_bytes > tiff_bytes.size()) {
        return false;
    }
    if ((value_bytes % 2U) != 0U) {
        return false;
    }

    const uint32_t word_count = static_cast<uint32_t>(value_bytes / 2U);
    if (word_count < 10) {
        return false;
    }

    uint16_t size_bytes = 0;
    if (!read_tiff_u16(cfg, tiff_bytes, value_off + 0, &size_bytes)) {
        return false;
    }
    if (size_bytes != value_bytes) {
        return false;
    }

    uint16_t num_points = 0;
    if (!read_tiff_u16(cfg, tiff_bytes, value_off + 2U * 2U, &num_points)) {
        return false;
    }
    if (num_points == 0
        || num_points
               > static_cast<uint16_t>(options.limits.max_entries_per_ifd)) {
        return false;
    }

    const uint32_t needed_words = 1U + 7U + 4U * uint32_t(num_points) + 3U;
    if (word_count < needed_words) {
        return false;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return true;
    }

    // CanonAFInfo2 layout (word offsets):
    // [0]=size(bytes), [1]=AFAreaMode, [2]=NumAFPoints, [3]=ValidAFPoints,
    // [4..7]=image dimensions, then 4 arrays of length NumAFPoints,
    // then three scalar fields.
    uint32_t order = 0;
    if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                             mk_ifd0, block, order++, 0x0000, 0,
                                             store, options, status_out)) {
        return true;
    }
    if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                             mk_ifd0, block, order++, 0x0001, 1,
                                             store, options, status_out)) {
        return true;
    }
    if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                             mk_ifd0, block, order++, 0x0002, 2,
                                             store, options, status_out)) {
        return true;
    }
    if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                             mk_ifd0, block, order++, 0x0003, 3,
                                             store, options, status_out)) {
        return true;
    }
    if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                             mk_ifd0, block, order++, 0x0004, 4,
                                             store, options, status_out)) {
        return true;
    }
    if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                             mk_ifd0, block, order++, 0x0005, 5,
                                             store, options, status_out)) {
        return true;
    }
    if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                             mk_ifd0, block, order++, 0x0006, 6,
                                             store, options, status_out)) {
        return true;
    }
    if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                             mk_ifd0, block, order++, 0x0007, 7,
                                             store, options, status_out)) {
        return true;
    }

    const uint32_t base = 8U;
    const uint32_t n    = uint32_t(num_points);

    struct ArrSpec final {
        uint16_t tag   = 0;
        uint16_t type  = 0;
        uint32_t words = 0;
    };
    const ArrSpec arrays[4] = {
        { 0x0008, 3, base + 0U * n },  // widths
        { 0x0009, 3, base + 1U * n },  // heights
        { 0x000a, 8, base + 2U * n },  // x positions (signed)
        { 0x000b, 8, base + 3U * n },  // y positions (signed)
    };

    for (uint32_t i = 0; i < 4; ++i) {
        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return true;
        }

        const ArrSpec& a     = arrays[i];
        const uint64_t off   = value_off + uint64_t(a.words) * 2ULL;
        const uint64_t bytes = uint64_t(n) * 2ULL;
        if (off + bytes > tiff_bytes.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), mk_ifd0, a.tag);
        entry.origin.block = block;
        entry.origin.order_in_block = order++;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, a.type };
        entry.origin.wire_count     = n;
        entry.value = decode_tiff_value(cfg, tiff_bytes, a.type, n, off, bytes,
                                        store.arena(), options.limits,
                                        status_out);
        entry.flags |= EntryFlags::Derived;
        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }

    if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                             mk_ifd0, block, order++, 0x000c,
                                             base + 4U * n + 0U, store, options,
                                             status_out)) {
        return true;
    }
    if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                             mk_ifd0, block, order++, 0x000d,
                                             base + 4U * n + 1U, store, options,
                                             status_out)) {
        return true;
    }
    if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                             mk_ifd0, block, order++, 0x000e,
                                             base + 4U * n + 2U, store, options,
                                             status_out)) {
        return true;
    }

    return true;
}


static bool
decode_canon_custom_functions2(const TiffConfig& cfg,
                               std::span<const std::byte> tiff_bytes,
                               uint64_t value_off, uint64_t value_bytes,
                               std::string_view mk_ifd0, MetaStore& store,
                               const ExifDecodeOptions& options,
                               ExifDecodeResult* status_out) noexcept
{
    if (mk_ifd0.empty()) {
        return false;
    }
    if (value_bytes < 8) {
        return false;
    }
    if (value_off + value_bytes > tiff_bytes.size()) {
        return false;
    }

    uint16_t len16 = 0;
    if (!read_tiff_u16(cfg, tiff_bytes, value_off + 0, &len16)) {
        return false;
    }
    if (len16 != value_bytes) {
        return false;
    }

    uint32_t group_count = 0;
    if (!read_tiff_u32(cfg, tiff_bytes, value_off + 4, &group_count)) {
        return false;
    }
    (void)group_count;

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return true;
    }

    const uint64_t end = value_off + value_bytes;
    uint64_t pos       = value_off + 8;
    uint32_t order     = 0;

    while (pos + 12 <= end) {
        uint32_t rec_num   = 0;
        uint32_t rec_len   = 0;
        uint32_t rec_count = 0;
        if (!read_tiff_u32(cfg, tiff_bytes, pos + 0, &rec_num)
            || !read_tiff_u32(cfg, tiff_bytes, pos + 4, &rec_len)
            || !read_tiff_u32(cfg, tiff_bytes, pos + 8, &rec_count)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }
        (void)rec_num;

        if (rec_len < 8) {
            break;
        }

        pos += 12;
        const uint64_t rec_end = pos + uint64_t(rec_len) - 8ULL;
        if (rec_end > end) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }

        uint64_t rec_pos = pos;
        uint32_t i       = 0;
        for (; rec_pos + 8 <= rec_end && i < rec_count; ++i) {
            uint32_t tag32 = 0;
            uint32_t num   = 0;
            if (!read_tiff_u32(cfg, tiff_bytes, rec_pos + 0, &tag32)
                || !read_tiff_u32(cfg, tiff_bytes, rec_pos + 4, &num)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return true;
            }
            if (tag32 > 0xFFFFu) {
                // OpenMeta uses 16-bit EXIF tag ids. Skip unknown/extended ids.
                break;
            }
            if (num == 0) {
                break;
            }
            if (num > options.limits.max_entries_per_ifd) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                break;
            }

            const uint64_t payload_bytes = uint64_t(num) * 4ULL;
            if (payload_bytes > options.limits.max_value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                break;
            }

            const uint64_t payload_off = rec_pos + 8;
            const uint64_t next        = payload_off + payload_bytes;
            if (next > rec_end) {
                break;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return true;
            }

            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), mk_ifd0,
                                                   static_cast<uint16_t>(tag32));
            entry.origin.block = block;
            entry.origin.order_in_block = order++;
            entry.origin.wire_type      = WireType { WireFamily::Other, 4 };
            entry.origin.wire_count     = num;
            entry.flags |= EntryFlags::Derived;

            if (num == 1) {
                uint32_t v = 0;
                if (!read_tiff_u32(cfg, tiff_bytes, payload_off, &v)) {
                    if (status_out) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    }
                    return true;
                }
                entry.value = make_u32(v);
            } else {
                ByteArena& arena       = store.arena();
                const uint64_t bytes64 = payload_bytes;
                if (bytes64 > UINT32_MAX) {
                    if (status_out) {
                        update_status(status_out,
                                      ExifDecodeStatus::LimitExceeded);
                    }
                    return true;
                }
                const ByteSpan span
                    = arena.allocate(static_cast<uint32_t>(bytes64),
                                     static_cast<uint32_t>(alignof(uint32_t)));
                std::span<std::byte> out = arena.span_mut(span);
                if (out.size() != bytes64) {
                    if (status_out) {
                        update_status(status_out,
                                      ExifDecodeStatus::LimitExceeded);
                    }
                    return true;
                }

                for (uint32_t k = 0; k < num; ++k) {
                    uint32_t v = 0;
                    if (!read_tiff_u32(cfg, tiff_bytes,
                                       payload_off + uint64_t(k) * 4ULL, &v)) {
                        if (status_out) {
                            update_status(status_out,
                                          ExifDecodeStatus::Malformed);
                        }
                        return true;
                    }
                    std::memcpy(out.data() + size_t(k) * 4U, &v, 4U);
                }

                MetaValue v;
                v.kind      = MetaValueKind::Array;
                v.elem_type = MetaElementType::U32;
                v.count     = num;
                v.data.span = span;
                entry.value = v;
            }

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }

            rec_pos = next;
        }

        pos = rec_end;
    }

    return true;
}

static bool
decode_canon_u32_bin_dir(const TiffConfig& cfg,
                         std::span<const std::byte> tiff_bytes,
                         uint64_t value_off, uint64_t value_bytes,
                         std::string_view ifd_name, MetaStore& store,
                         const ExifDecodeOptions& options,
                         ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty()) {
        return false;
    }
    if (value_bytes < 8) {
        return false;
    }
    if (value_off + value_bytes > tiff_bytes.size()) {
        return false;
    }

    uint32_t len32 = 0;
    if (!read_tiff_u32(cfg, tiff_bytes, value_off + 0, &len32)) {
        return false;
    }
    if (len32 != value_bytes) {
        return false;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return true;
    }

    const uint64_t end = value_off + value_bytes;
    uint64_t pos       = value_off + 8;
    uint32_t order     = 0;

    while (pos + 12 <= end) {
        uint32_t rec_num   = 0;
        uint32_t rec_len   = 0;
        uint32_t rec_count = 0;
        if (!read_tiff_u32(cfg, tiff_bytes, pos + 0, &rec_num)
            || !read_tiff_u32(cfg, tiff_bytes, pos + 4, &rec_len)
            || !read_tiff_u32(cfg, tiff_bytes, pos + 8, &rec_count)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }
        (void)rec_num;

        if (rec_len < 8) {
            break;
        }

        pos += 12;
        const uint64_t rec_end = pos + uint64_t(rec_len) - 8ULL;
        if (rec_end > end) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }

        uint64_t rec_pos = pos;
        uint32_t i       = 0;
        for (; rec_pos + 8 <= rec_end && i < rec_count; ++i) {
            uint32_t tag32 = 0;
            uint32_t num   = 0;
            if (!read_tiff_u32(cfg, tiff_bytes, rec_pos + 0, &tag32)
                || !read_tiff_u32(cfg, tiff_bytes, rec_pos + 4, &num)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return true;
            }
            if (tag32 > 0xFFFFu) {
                break;
            }
            if (num == 0) {
                break;
            }
            if (num > options.limits.max_entries_per_ifd) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                break;
            }

            const uint64_t payload_bytes = uint64_t(num) * 4ULL;
            if (payload_bytes > options.limits.max_value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                break;
            }

            const uint64_t payload_off = rec_pos + 8;
            const uint64_t next        = payload_off + payload_bytes;
            if (next > rec_end) {
                break;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return true;
            }

            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), ifd_name,
                                                   static_cast<uint16_t>(tag32));
            entry.origin.block = block;
            entry.origin.order_in_block = order++;
            entry.origin.wire_type      = WireType { WireFamily::Other, 4 };
            entry.origin.wire_count     = num;
            entry.flags |= EntryFlags::Derived;

            if (num == 1) {
                uint32_t v = 0;
                if (!read_tiff_u32(cfg, tiff_bytes, payload_off, &v)) {
                    if (status_out) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    }
                    return true;
                }
                entry.value = make_u32(v);
            } else {
                ByteArena& arena       = store.arena();
                const uint64_t bytes64 = payload_bytes;
                if (bytes64 > UINT32_MAX) {
                    if (status_out) {
                        update_status(status_out,
                                      ExifDecodeStatus::LimitExceeded);
                    }
                    return true;
                }
                const ByteSpan span
                    = arena.allocate(static_cast<uint32_t>(bytes64),
                                     static_cast<uint32_t>(alignof(uint32_t)));
                std::span<std::byte> out = arena.span_mut(span);
                if (out.size() != bytes64) {
                    if (status_out) {
                        update_status(status_out,
                                      ExifDecodeStatus::LimitExceeded);
                    }
                    return true;
                }

                for (uint32_t k = 0; k < num; ++k) {
                    uint32_t v = 0;
                    if (!read_tiff_u32(cfg, tiff_bytes,
                                       payload_off + uint64_t(k) * 4ULL, &v)) {
                        if (status_out) {
                            update_status(status_out,
                                          ExifDecodeStatus::Malformed);
                        }
                        return true;
                    }
                    std::memcpy(out.data() + size_t(k) * 4U, &v, 4U);
                }

                MetaValue v;
                v.kind      = MetaValueKind::Array;
                v.elem_type = MetaElementType::U32;
                v.count     = num;
                v.data.span = span;
                entry.value = v;
            }

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }

            rec_pos = next;
        }

        pos = rec_end;
    }

    return true;
}

static void
decode_canon_colorcalib_from_colordata(
    const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
    uint64_t colordata_off, uint32_t colordata_count, MetaStore& store,
    const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
{
    // Canon "ColorCalib" appears inside ColorData8 at word offset 0x0107
    // (byte offset 0x020e), with 15 records of 4 int16 values (120 bytes).
    if (colordata_count <= 0x0107u + 4u * 15u - 1u) {
        return;
    }

    const uint64_t base = colordata_off + 2ULL * 0x0107ULL;
    const uint64_t need = 120ULL;
    if (base + need > tiff_bytes.size()) {
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    const std::string_view ifd_name = "mk_canon_colorcalib_0";

    uint32_t order = 0;
    for (uint32_t k = 0; k < 15; ++k) {
        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        const uint16_t tag = static_cast<uint16_t>(4U * k);

        int16_t vals_i16[4];
        for (uint32_t j = 0; j < 4; ++j) {
            uint16_t u         = 0;
            const uint64_t off = base + uint64_t((4U * k + j) * 2U);
            if (!read_tiff_u16(cfg, tiff_bytes, off, &u)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }
            vals_i16[j] = static_cast<int16_t>(u);
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = order++;
        entry.origin.wire_type      = WireType { WireFamily::Other, 2 };
        entry.origin.wire_count     = 4;
        entry.value                 = make_i16_array(store.arena(),
                                                     std::span<const int16_t>(vals_i16));
        entry.flags |= EntryFlags::Derived;

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}


bool
decode_canon_makernote(const TiffConfig& cfg,
                       std::span<const std::byte> tiff_bytes,
                       uint64_t maker_note_off, uint64_t maker_note_bytes,
                       std::string_view mk_ifd0, MetaStore& store,
                       const ExifDecodeOptions& options,
                       ExifDecodeResult* status_out) noexcept
{
    if (mk_ifd0.empty()) {
        return false;
    }
    if (maker_note_off > tiff_bytes.size()) {
        return false;
    }
    if (maker_note_bytes > (tiff_bytes.size() - maker_note_off)) {
        return false;
    }

    TiffConfig mk_cfg = cfg;

    uint16_t entry_count = 0;
    if (!read_tiff_u16(mk_cfg, tiff_bytes, maker_note_off, &entry_count)
        || entry_count == 0
        || entry_count > options.limits.max_entries_per_ifd) {
        // Some Canon MakerNotes are little-endian even when the outer EXIF
        // stream is big-endian. If the parent endianness yields an invalid
        // directory, retry with the opposite endianness.
        mk_cfg.le = !mk_cfg.le;
        if (!read_tiff_u16(mk_cfg, tiff_bytes, maker_note_off, &entry_count)) {
            return false;
        }
    }
    if (entry_count == 0 || entry_count > options.limits.max_entries_per_ifd) {
        return false;
    }
    if (uint64_t(entry_count) > (UINT64_MAX / 12ULL)) {
        return false;
    }
    const uint64_t entries_off = maker_note_off + 2ULL;
    const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
    const uint64_t needed      = 2ULL + table_bytes + 4ULL;
    if (needed > (tiff_bytes.size() - maker_note_off)) {
        return false;
    }

    // Some Canon MakerNotes are stored as a truncated directory (count too
    // small) with out-of-line values placed elsewhere in the EXIF stream.
    const uint64_t maker_note_span_bytes
        = (maker_note_bytes < needed) ? needed : maker_note_bytes;

    const uint64_t value_base
        = guess_canon_value_base(mk_cfg, tiff_bytes, maker_note_off,
                                 maker_note_span_bytes, entry_count, needed,
                                 options.limits);

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return true;
    }

    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        uint16_t tag  = 0;
        uint16_t type = 0;
        if (!read_tiff_u16(mk_cfg, tiff_bytes, eoff + 0, &tag)
            || !read_tiff_u16(mk_cfg, tiff_bytes, eoff + 2, &type)) {
            return true;
        }

        uint32_t count32        = 0;
        uint32_t value_or_off32 = 0;
        if (!read_tiff_u32(mk_cfg, tiff_bytes, eoff + 4, &count32)
            || !read_tiff_u32(mk_cfg, tiff_bytes, eoff + 8, &value_or_off32)) {
            return true;
        }
        const uint64_t count = count32;

        const uint64_t unit = tiff_type_size(type);
        if (unit == 0) {
            continue;
        }
        if (count > (UINT64_MAX / unit)) {
            continue;
        }
        const uint64_t value_bytes = count * unit;
        if (value_bytes > options.limits.max_value_bytes) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            continue;
        }

        const uint64_t inline_cap      = 4;
        const uint64_t value_field_off = eoff + 8;
        const uint64_t abs_value_off   = (value_bytes <= inline_cap)
                                             ? value_field_off
                                             : (value_base
                                              + uint64_t(value_or_off32));

        if (abs_value_off + value_bytes > tiff_bytes.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            continue;
        }

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return true;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), mk_ifd0, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
        entry.origin.wire_count     = static_cast<uint32_t>(count);
        entry.value = decode_tiff_value(mk_cfg, tiff_bytes, type, count,
                                        abs_value_off, value_bytes,
                                        store.arena(), options.limits,
                                        status_out);

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }

        // Decode common Canon BinaryData subdirectories into derived blocks.
        // The raw MakerNote entries are always preserved in mk_canon0.
        char sub_ifd_buf[96];
        const std::string_view mk_prefix = "mk_canon";

        // CanonCameraInfo* (tag 0x000d) often contains an embedded TIFF-like
        // IFD stream describing a "CameraInfo" block. Best-effort: locate a
        // plausible classic IFD and decode it into mk_canon_camerainfo_0.
        if (tag == 0x000d && type == 7 && value_bytes != 0U) {
            const std::span<const std::byte> cam
                = tiff_bytes.subspan(static_cast<size_t>(abs_value_off),
                                     static_cast<size_t>(value_bytes));
            ClassicIfdCandidate best;
            if (find_best_classic_ifd_candidate(cam, 512, options.limits,
                                                &best)) {
                TiffConfig cam_cfg;
                cam_cfg.le      = best.le;
                cam_cfg.bigtiff = false;

                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "camerainfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_classic_ifd_no_header(cam_cfg, cam, best.offset, sub_ifd,
                                             store, options, status_out,
                                             EntryFlags::Derived);
            } else {
                // CanonCameraInfo: some models use fixed-layout byte offsets
                // for common fields (ExifTool exposes them with tag ids equal
                // to the byte offset). Decode a minimal subset used in the
                // ExifTool sample corpus.
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "camerainfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                const BlockId block2 = store.add_block(BlockInfo {});
                if (block2 != kInvalidBlockId) {
                    struct Field final {
                        uint16_t tag;
                        uint8_t bytes;
                    };
                    static constexpr Field kFields[] = {
                        { 0x0041, 1 },  // SharpnessFrequency
                        { 0x0042, 1 },  // Sharpness
                        { 0x0044, 1 },  // WhiteBalance
                        { 0x0048, 2 },  // ColorTemperature
                        { 0x004B, 1 },  // PictureStyle
                    };

                    uint32_t order = 0;
                    for (size_t fi = 0;
                         fi < sizeof(kFields) / sizeof(kFields[0]); ++fi) {
                        const Field f = kFields[fi];
                        if (uint64_t(f.tag) + f.bytes > value_bytes) {
                            continue;
                        }

                        if (status_out
                            && (status_out->entries_decoded + 1U)
                                   > options.limits.max_total_entries) {
                            update_status(status_out,
                                          ExifDecodeStatus::LimitExceeded);
                            return true;
                        }

                        Entry e;
                        e.key = make_exif_tag_key(store.arena(), sub_ifd,
                                                  f.tag);
                        e.origin.block          = block2;
                        e.origin.order_in_block = order++;
                        e.flags |= EntryFlags::Derived;

                        if (f.bytes == 1) {
                            e.origin.wire_type = WireType { WireFamily::Tiff,
                                                            1 };
                            e.origin.wire_count = 1;
                            e.value             = make_u8(u8(cam[f.tag]));
                        } else {
                            uint16_t v = 0;
                            const bool ok = mk_cfg.le
                                                ? read_u16le(cam, f.tag, &v)
                                                : read_u16be(cam, f.tag, &v);
                            if (!ok) {
                                continue;
                            }
                            e.origin.wire_type  = WireType { WireFamily::Tiff,
                                                             3 };
                            e.origin.wire_count = 1;
                            e.value             = make_u16(v);
                        }

                        (void)store.add_entry(e);
                        if (status_out) {
                            status_out->entries_decoded += 1;
                        }
                    }
                }
            }
        }

        // Canon LensInfo (tag 0x4019) contains the raw lens serial bytes.
        if (tag == 0x4019 && type == 7 && value_bytes != 0U) {
            const uint64_t serial_bytes = (value_bytes < 5) ? value_bytes : 5;
            if (serial_bytes != 0) {
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "lensinfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                const uint16_t tags_out[]  = { 0x0000 };
                const MetaValue vals_out[] = { make_bytes(
                    store.arena(),
                    tiff_bytes.subspan(static_cast<size_t>(abs_value_off),
                                       static_cast<size_t>(serial_bytes))) };
                emit_bin_dir_entries(sub_ifd, store,
                                     std::span<const uint16_t>(tags_out),
                                     std::span<const MetaValue>(vals_out),
                                     options.limits, status_out);
            }
        }

        // CanonCameraInfo* blobs (tag 0x000d) may embed a PictureStyleInfo
        // table at a fixed offset for some models. Best-effort: decode a
        // psinfo table from the tail starting at 0x025b.
        if (tag == 0x000d && type == 7 && value_bytes > 0x025b) {
            const uint64_t ps_off   = abs_value_off + 0x025b;
            const uint64_t ps_bytes = value_bytes - 0x025b;
            if (ps_bytes >= 0x00dc + 2U
                && ps_off + ps_bytes <= tiff_bytes.size()) {
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "psinfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_psinfo_table(tiff_bytes, ps_off, ps_bytes, sub_ifd,
                                          store, options, status_out);
            }
        }

        if (tag == 0x0099 && value_bytes != 0U) {  // CustomFunctions2
            char canoncustom_ifd_buf[96];
            const std::string_view canoncustom_ifd = make_mk_subtable_ifd_token(
                "mk_canoncustom", "functions2", 0,
                std::span<char>(canoncustom_ifd_buf));
            (void)decode_canon_custom_functions2(mk_cfg, tiff_bytes,
                                                 abs_value_off, value_bytes,
                                                 canoncustom_ifd, store,
                                                 options, status_out);
        }

        if (tag == 0x4011 && type == 7 && value_bytes >= 2U
            && (value_bytes % 2U) == 0U) {
            const uint32_t count16 = static_cast<uint32_t>(value_bytes / 2U);
            const std::string_view sub_ifd
                = make_mk_subtable_ifd_token(mk_prefix, "vignettingcorr", 0,
                                             std::span<char>(sub_ifd_buf));
            decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off, count16,
                                   sub_ifd, store, options, status_out);
        }

        if (type == 3 && count32 != 0) {  // SHORT
            if (tag == 0x0001) {          // CanonCameraSettings
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "camerasettings", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x0090) {  // CustomFunctions1D (EOS-1D/1Ds)
                char canoncustom_ifd_buf[96];
                const std::string_view canoncustom_ifd = make_mk_subtable_ifd_token(
                    "mk_canoncustom", "functions1d", 0,
                    std::span<char>(canoncustom_ifd_buf));
                decode_canon_custom_word_table(
                    mk_cfg, tiff_bytes, abs_value_off, count32, canoncustom_ifd,
                    0x0000, CanonCustomMode::LowByteAsU8, store, options,
                    status_out);
            } else if (tag == 0x0091) {  // PersonalFunctions
                char canoncustom_ifd_buf[96];
                const std::string_view canoncustom_ifd = make_mk_subtable_ifd_token(
                    "mk_canoncustom", "personalfuncs", 0,
                    std::span<char>(canoncustom_ifd_buf));
                decode_canon_custom_word_table(
                    mk_cfg, tiff_bytes, abs_value_off, count32, canoncustom_ifd,
                    0x0001, CanonCustomMode::LowByteAsU8, store, options,
                    status_out);
            } else if (tag == 0x0092) {  // PersonalFunctionValues
                char canoncustom_ifd_buf[96];
                const std::string_view canoncustom_ifd = make_mk_subtable_ifd_token(
                    "mk_canoncustom", "personalfuncvalues", 0,
                    std::span<char>(canoncustom_ifd_buf));
                decode_canon_custom_word_table(
                    mk_cfg, tiff_bytes, abs_value_off, count32, canoncustom_ifd,
                    0x0001, CanonCustomMode::U16, store, options, status_out);
            } else if (tag == 0x0005) {  // CanonPanorama
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "panorama", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x0026) {  // CanonAFInfo2
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "afinfo2", 0,
                                                 std::span<char>(sub_ifd_buf));
                (void)decode_canon_afinfo2(mk_cfg, tiff_bytes, abs_value_off,
                                           value_bytes, sub_ifd, store, options,
                                           status_out);
            } else if (tag == 0x0002) {  // CanonFocalLength
                bool use_unknown = false;
                if (count32 > 3U) {
                    uint16_t x = 0;
                    uint16_t y = 0;
                    if (read_tiff_u16(mk_cfg, tiff_bytes,
                                      abs_value_off + 2ULL * 2ULL, &x)
                        && read_tiff_u16(mk_cfg, tiff_bytes,
                                         abs_value_off + 2ULL * 3ULL, &y)) {
                        const bool plausible_size
                            = (x > 0U && y > 0U && x <= 5000U && y <= 5000U);
                        use_unknown = !plausible_size;
                    }
                }

                const std::string_view table = use_unknown
                                                   ? "focallength_unknown"
                                                   : "focallength";
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, table, 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x0012) {  // CanonAFInfo (older models)
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "afinfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x0004) {  // CanonShotInfo
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "shotinfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x0093) {  // CanonFileInfo
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "fileinfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x0098) {  // CropInfo
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "cropinfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x001d) {  // MyColors
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "mycolors", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x00aa) {  // MeasuredColor
                // Emit the full MeasuredRGGB array (4x u16) as tag 0x0001.
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "measuredcolor", 0,
                                                 std::span<char>(sub_ifd_buf));
                if (count32 >= 5) {
                    uint16_t v_u16[4];
                    for (uint32_t k = 0; k < 4; ++k) {
                        (void)read_tiff_u16(mk_cfg, tiff_bytes,
                                            abs_value_off
                                                + uint64_t(2U * (k + 1U)),
                                            &v_u16[k]);
                    }
                    const BlockId block2 = store.add_block(BlockInfo {});
                    if (block2 != kInvalidBlockId) {
                        Entry e;
                        e.key = make_exif_tag_key(store.arena(), sub_ifd,
                                                  0x0001);
                        e.origin.block          = block2;
                        e.origin.order_in_block = 0;
                        e.origin.wire_type  = WireType { WireFamily::Other, 2 };
                        e.origin.wire_count = 4;
                        e.value
                            = make_u16_array(store.arena(),
                                             std::span<const uint16_t>(v_u16));
                        e.flags |= EntryFlags::Derived;
                        (void)store.add_entry(e);
                        if (status_out) {
                            status_out->entries_decoded += 1;
                        }
                    }
                } else {
                    decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off,
                                           count32, sub_ifd, store, options,
                                           status_out);
                }
            } else if (tag == 0x00e0) {  // SensorInfo
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "sensorinfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x00A0) {  // ProcessingInfo
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "processing", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x4001) {  // ColorData (multiple versions)
                bool looks_like_colorcalib = false;
                if (count32 > 0x0107u + 3u) {
                    uint16_t maybe_temp = 0;
                    if (read_tiff_u16(mk_cfg, tiff_bytes,
                                      abs_value_off
                                          + 2ULL * uint64_t(0x0107u + 3u),
                                      &maybe_temp)) {
                        looks_like_colorcalib = (maybe_temp >= 1500u
                                                 && maybe_temp <= 20000u);
                    }
                }

                const std::string_view table = looks_like_colorcalib
                                                   ? "colordata8"
                                                   : "colordata";
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, table, 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u16_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);

                if (looks_like_colorcalib) {
                    decode_canon_colorcalib_from_colordata(mk_cfg, tiff_bytes,
                                                           abs_value_off,
                                                           count32, store,
                                                           options, status_out);
                }
            }
        } else if (type == 4 && count32 != 0) {  // LONG
            if (tag == 0x0035) {                 // TimeInfo
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "timeinfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u32_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x009A) {  // AspectInfo
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "aspectinfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u32_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x000d) {  // CanonCameraInfo (older models)
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "camerainfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u32_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x4016) {  // VignettingCorr2
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "vignettingcorr2",
                                                 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u32_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x4013) {  // AFMicroAdj
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "afmicroadj", 0,
                                                 std::span<char>(sub_ifd_buf));

                uint32_t mode  = 0;
                uint32_t numer = 0;
                uint32_t denom = 0;
                if (value_bytes >= 16
                    && read_tiff_u32(mk_cfg, tiff_bytes, abs_value_off + 4,
                                     &mode)
                    && read_tiff_u32(mk_cfg, tiff_bytes, abs_value_off + 8,
                                     &numer)
                    && read_tiff_u32(mk_cfg, tiff_bytes, abs_value_off + 12,
                                     &denom)) {
                    const BlockId block2 = store.add_block(BlockInfo {});
                    if (block2 != kInvalidBlockId) {
                        Entry e_mode;
                        e_mode.key = make_exif_tag_key(store.arena(), sub_ifd,
                                                       0x0001);
                        e_mode.origin.block          = block2;
                        e_mode.origin.order_in_block = 0;
                        e_mode.origin.wire_type  = WireType { WireFamily::Other,
                                                             4 };
                        e_mode.origin.wire_count = 1;
                        e_mode.value             = make_u32(mode);
                        e_mode.flags |= EntryFlags::Derived;
                        (void)store.add_entry(e_mode);

                        Entry e_val;
                        e_val.key = make_exif_tag_key(store.arena(), sub_ifd,
                                                      0x0002);
                        e_val.origin.block          = block2;
                        e_val.origin.order_in_block = 1;
                        e_val.origin.wire_type  = WireType { WireFamily::Other,
                                                            10 };
                        e_val.origin.wire_count = 1;
                        e_val.value             = make_urational(numer, denom);
                        e_val.flags |= EntryFlags::Derived;
                        (void)store.add_entry(e_val);

                        if (status_out) {
                            status_out->entries_decoded += 2;
                        }
                    }
                } else {
                    decode_canon_u32_table(mk_cfg, tiff_bytes, abs_value_off,
                                           count32, sub_ifd, store, options,
                                           status_out);
                }
            } else if (tag == 0x4018) {  // LightingOpt
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "lightingopt", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u32_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x4020) {  // AmbienceInfo
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "ambience", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u32_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x4021) {  // MultiExp
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "multiexp", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_i32_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x4024) {  // FilterInfo (BinaryData directory)
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "filterinfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                (void)decode_canon_u32_bin_dir(mk_cfg, tiff_bytes,
                                               abs_value_off, value_bytes,
                                               sub_ifd, store, options,
                                               status_out);
            } else if (tag == 0x4025) {  // HDRInfo
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "hdrinfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u32_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x4028) {  // AFConfig
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "afconfig", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_i32_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            } else if (tag == 0x403f) {  // RawBurstInfo
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "rawburstinfo", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u32_table(mk_cfg, tiff_bytes, abs_value_off,
                                       count32, sub_ifd, store, options,
                                       status_out);
            }
        }
    }

    return true;
}

}  // namespace openmeta::exif_internal
