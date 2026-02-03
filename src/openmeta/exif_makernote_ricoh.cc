#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>
#include <vector>

namespace openmeta::exif_internal {
namespace {

static uint32_t score_ascii_blob(std::span<const std::byte> raw) noexcept;
static uint32_t score_ricoh_faceinfo_blob(std::span<const std::byte> raw) noexcept;

static bool decode_ricoh_type2_ricoh_header_ifd(std::span<const std::byte> mn,
                                                std::string_view mk_prefix,
                                                MetaStore& store,
                                                const ExifDecodeOptions& options,
                                                ExifDecodeResult* status_out) noexcept
{
    // ExifTool Ricoh::Type2: MakerNote data begins with "RICOH\0", followed by a
    // little-endian IFD-like structure (with occasional padding/format errors).
    if (mn.size() < 16 || mk_prefix.empty()) {
        return false;
    }
    if (!match_bytes(mn, 0, "RICOH", 5)) {
        return false;
    }

    // Entry count is at offset 8 for this structure (little-endian).
    TiffConfig cfg;
    cfg.bigtiff = false;
    cfg.le      = true;

    if (mn.size() < 12) {
        return false;
    }

    uint16_t entry_count = 0;
    if (!read_tiff_u16(cfg, mn, 8, &entry_count)) {
        return false;
    }
    if (entry_count == 0 || entry_count > options.limits.max_entries_per_ifd
        || entry_count > 4096) {
        return false;
    }

    // Most samples include 2 bytes of padding after the entry count.
    const uint64_t entries_off = 12;
    const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
    const uint64_t needed      = entries_off + table_bytes + 4ULL;
    if (needed > mn.size()) {
        return false;
    }

    char scratch[64];
    const std::string_view ifd_name = make_mk_subtable_ifd_token(
        mk_prefix, "type2", 0, std::span<char>(scratch));
    if (ifd_name.empty()) {
        return false;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return false;
    }

    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        uint16_t tag  = 0;
        uint16_t type = 0;
        uint32_t count32 = 0;
        uint32_t value_or_off32 = 0;
        if (!read_tiff_u16(cfg, mn, eoff + 0, &tag)
            || !read_tiff_u16(cfg, mn, eoff + 2, &type)
            || !read_tiff_u32(cfg, mn, eoff + 4, &count32)
            || !read_tiff_u32(cfg, mn, eoff + 8, &value_or_off32)) {
            return true;
        }

        const uint64_t count = count32;
        const uint64_t unit  = tiff_type_size(type);
        const uint64_t inline_cap = 4;
        const uint64_t value_field_off = eoff + 8;

        uint64_t value_bytes = 0;
        uint64_t value_off   = value_field_off;
        bool have_value_bytes = false;
        if (unit != 0 && count <= (UINT64_MAX / unit)) {
            value_bytes = count * unit;
            value_off = (value_bytes <= inline_cap) ? value_field_off
                                                    : value_or_off32;
            have_value_bytes = true;
        }

        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block          = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
        entry.origin.wire_count     = static_cast<uint32_t>(count);

        if (!have_value_bytes) {
            entry.flags |= EntryFlags::Unreadable;
        } else if (value_bytes > options.limits.max_value_bytes) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            entry.flags |= EntryFlags::Truncated;
        } else if (value_off + value_bytes > mn.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            entry.flags |= EntryFlags::Unreadable;
        } else {
            entry.value = decode_tiff_value(cfg, mn, type, count, value_off,
                                            value_bytes, store.arena(),
                                            options.limits, status_out);
        }

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }

    return true;
}

static bool decode_ricoh_type2_padded_ifd(std::span<const std::byte> mn,
                                         std::string_view mk_ifd0,
                                         MetaStore& store,
                                         const ExifDecodeOptions& options,
                                         ExifDecodeResult* status_out) noexcept
{
    if (mn.size() < 16 || mk_ifd0.empty()) {
        return false;
    }

    const uint8_t b0 = u8(mn[0]);
    const uint8_t b1 = u8(mn[1]);
    if (!((b0 == 'I' && b1 == 'I') || (b0 == 'M' && b1 == 'M'))) {
        return false;
    }

    TiffConfig cfg;
    cfg.bigtiff = false;
    cfg.le      = (b0 == 'I');

    uint16_t version = 0;
    if (!read_tiff_u16(cfg, mn, 2, &version) || version != 42) {
        return false;
    }

    uint32_t ifd0_off32 = 0;
    if (!read_tiff_u32(cfg, mn, 4, &ifd0_off32)) {
        return false;
    }
    const uint64_t ifd0_off = ifd0_off32;
    if (ifd0_off == 0 || ifd0_off + 8 > mn.size()) {
        return false;
    }

    uint16_t entry_count = 0;
    if (!read_tiff_u16(cfg, mn, ifd0_off, &entry_count)) {
        return false;
    }
    if (entry_count == 0 || entry_count > options.limits.max_entries_per_ifd
        || entry_count > 4096) {
        return false;
    }

    // Some Ricoh "Type2" maker notes have an extra 2 bytes of padding after
    // the entry count. Others appear to be standard IFDs.
    const bool padded
        = (mn[static_cast<size_t>(ifd0_off + 2)] == std::byte { 0 })
          && (mn[static_cast<size_t>(ifd0_off + 3)] == std::byte { 0 });

    const uint64_t entries_off = ifd0_off + (padded ? 4 : 2);
    const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
    const uint64_t needed      = entries_off + table_bytes + 4ULL;
    if (needed > mn.size()) {
        return false;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return false;
    }

    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        uint16_t tag  = 0;
        uint16_t type = 0;
        uint32_t count32 = 0;
        uint32_t value_or_off32 = 0;
        if (!read_tiff_u16(cfg, mn, eoff + 0, &tag)
            || !read_tiff_u16(cfg, mn, eoff + 2, &type)
            || !read_tiff_u32(cfg, mn, eoff + 4, &count32)
            || !read_tiff_u32(cfg, mn, eoff + 8, &value_or_off32)) {
            return true;
        }

        const uint64_t count = count32;
        const uint64_t unit  = tiff_type_size(type);
        const uint64_t inline_cap = 4;
        const uint64_t value_field_off = eoff + 8;

        uint64_t value_bytes = 0;
        uint64_t value_off   = value_field_off;
        bool have_value_bytes = false;
        if (unit != 0 && count <= (UINT64_MAX / unit)) {
            value_bytes = count * unit;
            value_off = (value_bytes <= inline_cap) ? value_field_off
                                                    : value_or_off32;
            have_value_bytes = true;
        }

        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), mk_ifd0, tag);
        entry.origin.block          = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
        entry.origin.wire_count     = static_cast<uint32_t>(count);

        if (!have_value_bytes) {
            entry.flags |= EntryFlags::Unreadable;
        } else if (value_bytes > options.limits.max_value_bytes) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            entry.flags |= EntryFlags::Truncated;
        } else if (value_off + value_bytes > mn.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            entry.flags |= EntryFlags::Unreadable;
        } else {
            entry.value = decode_tiff_value(cfg, mn, type, count, value_off,
                                            value_bytes, store.arena(),
                                            options.limits, status_out);
        }

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }

    return true;
}

static void decode_ricoh_main_ifd_with_fallback_offsets(
    const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
    std::span<const std::byte> mn, uint64_t ifd_off, uint64_t base,
    std::string_view ifd_name, MetaStore& store,
    const ExifDecodeOptions& options,
    ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty()) {
        return;
    }
    if (ifd_off + 2 > mn.size()) {
        return;
    }

    uint16_t entry_count = 0;
    if (!read_tiff_u16(cfg, mn, ifd_off, &entry_count)) {
        return;
    }
    if (entry_count == 0 || entry_count > options.limits.max_entries_per_ifd) {
        return;
    }

    const uint64_t entries_off = ifd_off + 2;
    const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
    if (entries_off + table_bytes + 4ULL > mn.size()) {
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        uint16_t tag  = 0;
        uint16_t type = 0;
        if (!read_tiff_u16(cfg, mn, eoff + 0, &tag)
            || !read_tiff_u16(cfg, mn, eoff + 2, &type)) {
            return;
        }

        uint32_t count32 = 0;
        uint32_t value_or_off32 = 0;
        if (!read_tiff_u32(cfg, mn, eoff + 4, &count32)
            || !read_tiff_u32(cfg, mn, eoff + 8, &value_or_off32)) {
            return;
        }

        const uint64_t count = count32;
        const uint64_t unit  = tiff_type_size(type);
        if (unit == 0 || count > (UINT64_MAX / unit)) {
            continue;
        }
        const uint64_t value_bytes = count * unit;

        const uint64_t inline_cap      = 4;
        const uint64_t value_field_off = eoff + 8;

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block          = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
        entry.origin.wire_count     = static_cast<uint32_t>(count);

        if (value_bytes > options.limits.max_value_bytes) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            entry.flags |= EntryFlags::Truncated;
        } else if (value_bytes <= inline_cap) {
            entry.value = decode_tiff_value(cfg, mn, type, count, value_field_off,
                                            value_bytes, store.arena(),
                                            options.limits, status_out);
        } else {
            // Ricoh MakerNotes commonly store offsets relative to Start=$valuePtr+8,
            // but there are real-world variants:
            // - offsets relative to the MakerNote start ($valuePtr)
            // - offsets relative to Start=$valuePtr+8
            // - absolute offsets relative to the outer EXIF/TIFF header
            const uint64_t off_rel = value_or_off32;

            bool decoded = false;

            uint64_t off_mn_base = UINT64_MAX;
            uint64_t off_mn_0    = off_rel;

            const bool have_mn_0
                = (off_mn_0 + value_bytes <= mn.size());

            bool have_mn_base = false;
            if (base <= (UINT64_MAX - off_rel)) {
                off_mn_base = base + off_rel;
                have_mn_base = (off_mn_base + value_bytes <= mn.size());
            }

            const bool have_abs
                = (off_rel + value_bytes <= tiff_bytes.size());

            uint32_t score_base = 0;
            uint32_t score_0    = 0;
            uint32_t score_abs  = 0;
            if (type == 2 /* ASCII */ || type == 129 /* UTF-8 */) {
                if (have_mn_base) {
                    score_base = score_ascii_blob(
                        mn.subspan(static_cast<size_t>(off_mn_base),
                                   static_cast<size_t>(value_bytes)));
                }
                if (have_mn_0) {
                    score_0 = score_ascii_blob(
                        mn.subspan(static_cast<size_t>(off_mn_0),
                                   static_cast<size_t>(value_bytes)));
                }
                if (have_abs) {
                    score_abs = score_ascii_blob(
                        tiff_bytes.subspan(static_cast<size_t>(off_rel),
                                           static_cast<size_t>(value_bytes)));
                }
            }

            // Prefer the candidate with the best string score for text types.
            if ((type == 2 || type == 129) && (score_base || score_0 || score_abs)) {
                if (score_base >= score_0 && score_base >= score_abs
                    && have_mn_base) {
                    entry.value = decode_tiff_value(cfg, mn, type, count,
                                                    off_mn_base, value_bytes,
                                                    store.arena(), options.limits,
                                                    status_out);
                    decoded = true;
                } else if (score_0 >= score_abs && have_mn_0) {
                    entry.value = decode_tiff_value(cfg, mn, type, count,
                                                    off_mn_0, value_bytes,
                                                    store.arena(), options.limits,
                                                    status_out);
                    decoded = true;
                } else if (have_abs) {
                    entry.value = decode_tiff_value(cfg, tiff_bytes, type, count,
                                                    off_rel, value_bytes,
                                                    store.arena(), options.limits,
                                                    status_out);
                    decoded = true;
                }
            }

            if (!decoded && have_mn_base) {
                entry.value = decode_tiff_value(cfg, mn, type, count, off_mn_base,
                                                value_bytes, store.arena(),
                                                options.limits, status_out);
                decoded = true;
            }

            if (!decoded && have_mn_0) {
                entry.value = decode_tiff_value(cfg, mn, type, count, off_mn_0,
                                                value_bytes, store.arena(),
                                                options.limits, status_out);
                decoded = true;
            }

            if (!decoded && have_abs) {
                entry.value = decode_tiff_value(cfg, tiff_bytes, type, count,
                                                off_rel, value_bytes,
                                                store.arena(), options.limits,
                                                status_out);
                decoded = true;
            }

            if (!decoded) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                entry.flags |= EntryFlags::Unreadable;
            }
        }

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}

static void decode_ricoh_imageinfo_u8_table(std::string_view mk_prefix,
                                            std::span<const std::byte> raw,
                                            MetaStore& store,
                                            const ExifDecodeOptions& options,
                                            ExifDecodeResult* status_out) noexcept
{
    if (mk_prefix.empty() || raw.empty()) {
        return;
    }
    if (raw.size() > options.limits.max_entries_per_ifd) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    // `raw` often references `store.arena()` memory. Adding derived entries may
    // grow the arena (realloc), invalidating `raw.data()`. Copy to a stable
    // local buffer first.
    std::array<std::byte, 4096> stable_buf {};
    if (raw.size() > stable_buf.size()) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }
    std::memcpy(stable_buf.data(), raw.data(), raw.size());
    const std::span<const std::byte> stable(stable_buf.data(), raw.size());

    char scratch[64];
    const std::string_view ifd_name = make_mk_subtable_ifd_token(
        mk_prefix, "imageinfo", 0, std::span<char>(scratch));
    if (ifd_name.empty()) {
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    uint32_t order = 0;
    for (size_t i = 0; i < stable.size(); ++i) {
        if (i > 0xFFFFu) {
            break;
        }
        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd_name,
                                      static_cast<uint16_t>(i));
        entry.origin.block          = block;
        entry.origin.order_in_block = order++;
        entry.origin.wire_type      = WireType { WireFamily::Other, 1 };
        entry.origin.wire_count     = 1;
        entry.flags |= EntryFlags::Derived;
        entry.value = make_u8(u8(stable[i]));

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}

static void decode_ricoh_faceinfo(std::string_view mk_prefix,
                                  std::span<const std::byte> raw,
                                  MetaStore& store,
                                  const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out) noexcept
{
    // ExifTool Ricoh::FaceInfo: a binary table containing face detection
    // metadata used by some models (eg. CX4, GXR).
    if (mk_prefix.empty() || raw.size() <= 0xB6 + 4) {
        return;
    }

    // The input span may point into store.arena(); copy to keep it stable.
    std::array<std::byte, 4096> stable_buf {};
    if (raw.size() > stable_buf.size()) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }
    std::memcpy(stable_buf.data(), raw.data(), raw.size());
    const std::span<const std::byte> stable(stable_buf.data(), raw.size());

    const uint8_t faces_detected = u8(stable[0xB5]);

    uint16_t frame[2] = { 0, 0 };
    (void)read_u16be(stable, 0xB6, &frame[0]);
    (void)read_u16be(stable, 0xB8, &frame[1]);

    char scratch[64];
    const std::string_view ifd_name = make_mk_subtable_ifd_token(
        mk_prefix, "faceinfo", 0, std::span<char>(scratch));
    if (ifd_name.empty()) {
        return;
    }

    std::array<uint16_t, 10> tags_out {};
    std::array<MetaValue, 10> vals_out {};
    size_t n = 0;

    tags_out[n] = 0x00B5;  // FacesDetected
    vals_out[n] = make_u8(faces_detected);
    n += 1;

    tags_out[n] = 0x00B6;  // FaceDetectFrameSize
    vals_out[n] = make_u16_array(store.arena(), std::span<const uint16_t>(frame));
    n += 1;

    // Face positions (optional). Only emit if faces were detected and the
    // input is large enough for the expected blocks.
    static constexpr uint16_t kFaceTags[] = {
        0x00BC,  // Face1Position
        0x00C8,  // Face2Position
        0x00D4,  // Face3Position
        0x00E0,  // Face4Position
        0x00EC,  // Face5Position
        0x00F8,  // Face6Position
        0x0104,  // Face7Position
        0x0110,  // Face8Position
    };
    const uint32_t faces = (faces_detected > 8) ? 8 : faces_detected;
    for (uint32_t fi = 0; fi < faces && n < tags_out.size(); ++fi) {
        const uint64_t pos_off = 0xBC + uint64_t(fi) * 0x0C;
        if (pos_off + 8 > stable.size()) {
            break;
        }
        uint16_t box[4] = { 0, 0, 0, 0 };
        (void)read_u16be(stable, pos_off + 0, &box[0]);
        (void)read_u16be(stable, pos_off + 2, &box[1]);
        (void)read_u16be(stable, pos_off + 4, &box[2]);
        (void)read_u16be(stable, pos_off + 6, &box[3]);

        tags_out[n] = kFaceTags[fi];
        vals_out[n] = make_u16_array(store.arena(), std::span<const uint16_t>(box));
        n += 1;
    }

    emit_bin_dir_entries(ifd_name, store,
                         std::span<const uint16_t>(tags_out.data(), n),
                         std::span<const MetaValue>(vals_out.data(), n),
                         options.limits, status_out);
}

static uint32_t score_ascii_blob(std::span<const std::byte> raw) noexcept
{
    // Prefer buffers that look like normal ASCII strings (digits, punctuation,
    // spaces) and contain at least one NUL terminator.
    if (raw.empty()) {
        return 0;
    }

    const size_t n = (raw.size() < 64) ? raw.size() : 64;
    uint32_t score = 0;
    bool have_nul = false;
    for (size_t i = 0; i < n; ++i) {
        const uint8_t b = u8(raw[i]);
        if (b == 0) {
            have_nul = true;
            score += 2;
            continue;
        }
        if (b >= 0x20 && b <= 0x7E) {
            score += 3;
        } else {
            // Penalize control/non-ASCII bytes heavily.
            if (score > 0) {
                score -= 1;
            }
        }
    }

    if (have_nul) {
        score += 10;
    }
    return score;
}

static uint32_t score_ricoh_faceinfo_blob(std::span<const std::byte> raw) noexcept
{
    // Prefer buffers that match ExifTool's Ricoh::FaceInfo structure:
    // - FacesDetected at 0xB5 should be a small count (<= 8)
    // - Frame size at 0xB6/0xB8 should be reasonable.
    if (raw.size() <= 0xB6 + 4) {
        return 0;
    }

    const uint8_t faces = u8(raw[0xB5]);
    if (faces > 8) {
        return 0;
    }

    uint16_t w = 0;
    uint16_t h = 0;
    (void)read_u16be(raw, 0xB6, &w);
    (void)read_u16be(raw, 0xB8, &h);

    uint32_t score = 100;
    if (faces == 0) {
        score += 50;
    } else {
        score += (8U - static_cast<uint32_t>(faces));
    }

    // Basic plausibility: frame dims often fit in 16-bit and are not tiny.
    if (w == 0 && h == 0) {
        score += 25;
    } else if (w > 16 && h > 16) {
        score += 10;
    }

    if (w <= 20000 && h <= 20000) {
        score += 5;
    }

    return score;
}

static bool decode_ricoh_subdir(std::string_view mk_prefix,
                                std::span<const std::byte> tiff_bytes,
                                std::span<const std::byte> raw, MetaStore& store,
                                const ExifDecodeOptions& options,
                                ExifDecodeResult* status_out) noexcept
{
    if (mk_prefix.empty() || raw.size() < 24) {
        return false;
    }

    // `raw` may point into store.arena(); decode against a stable local copy
    // because decoding may append to the arena (invalidating spans).
    std::vector<std::byte> stable_storage(raw.begin(), raw.end());
    const std::span<const std::byte> stable(stable_storage.data(),
                                            stable_storage.size());

    // ExifTool: Start => $valuePtr + 20 (skip "[Ricoh Camera Info]\0" header),
    // ByteOrder => BigEndian.
    //
    // Some samples include leading padding before the header, so locate the
    // marker string and decode relative to it.
    static constexpr char kHdr[] = "[Ricoh Camera Info]";
    const uint64_t kHdrLen       = sizeof(kHdr) - 1;  // no NUL

    uint64_t hdr = UINT64_MAX;
    for (uint64_t i = 0; i + kHdrLen <= stable.size(); ++i) {
        if (match_bytes(stable, i, kHdr, static_cast<uint32_t>(kHdrLen))) {
            // Include the trailing NUL if present.
            hdr = i + 20;
            break;
        }
    }
    if (hdr == UINT64_MAX) {
        // ExifTool validates this block via the header marker. If we don't see
        // it, best-effort decoding tends to produce garbage.
        return false;
    }
    const uint64_t base_alt = (hdr >= 20) ? (hdr - 20) : 0;
    if (hdr >= stable.size()) {
        return false;
    }

    TiffConfig cfg;
    cfg.bigtiff = false;
    cfg.le      = false;  // BigEndian

    if (hdr + 2 > stable.size()) {
        return false;
    }

    char scratch[64];
    const std::string_view ifd_name = make_mk_subtable_ifd_token(
        mk_prefix, "subdir", 0, std::span<char>(scratch));
    if (ifd_name.empty()) {
        return false;
    }

    uint16_t entry_count = 0;
    if (!read_tiff_u16(cfg, stable, hdr, &entry_count)) {
        return false;
    }
    if (entry_count == 0 || entry_count > options.limits.max_entries_per_ifd) {
        return false;
    }

    const uint64_t entries_off = hdr + 2;
    const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
    if (entries_off + table_bytes + 4ULL > stable.size()) {
        return false;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return false;
    }

    bool added = false;
    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        uint16_t tag  = 0;
        uint16_t type = 0;
        uint32_t count32 = 0;
        uint32_t value_or_off32 = 0;
        if (!read_tiff_u16(cfg, stable, eoff + 0, &tag)
            || !read_tiff_u16(cfg, stable, eoff + 2, &type)
            || !read_tiff_u32(cfg, stable, eoff + 4, &count32)
            || !read_tiff_u32(cfg, stable, eoff + 8, &value_or_off32)) {
            return added;
        }

        const uint64_t count = count32;
        const uint64_t unit  = tiff_type_size(type);
        const uint64_t inline_cap      = 4;
        const uint64_t value_field_off = eoff + 8;

        uint64_t value_bytes = 0;
        bool have_value_bytes = false;
        if (unit != 0 && count <= (UINT64_MAX / unit)) {
            value_bytes = count * unit;
            have_value_bytes = true;
        }

        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block          = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
        entry.origin.wire_count     = static_cast<uint32_t>(count);

        if (!have_value_bytes) {
            entry.flags |= EntryFlags::Unreadable;
        } else if (value_bytes > options.limits.max_value_bytes) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            entry.flags |= EntryFlags::Truncated;
        } else {
            bool decoded = false;

            if (value_bytes <= inline_cap) {
                // Inline value bytes live inside the entry itself.
                if (value_field_off + value_bytes <= stable.size()) {
                    entry.value = decode_tiff_value(cfg, stable, type, count,
                                                    value_field_off, value_bytes,
                                                    store.arena(), options.limits,
                                                    status_out);
                    decoded = true;
                }
            } else {
                // ExifTool Ricoh::Subdir uses a non-standard base: offsets often
                // point into the outer TIFF/EXIF stream (not relative to the
                // start of this subdir block). Decode against `tiff_bytes` first.
                const uint64_t off_abs = value_or_off32;
                if (off_abs + value_bytes <= tiff_bytes.size()) {
                    // FaceInfo is a binary subtable inside the Subdir block.
                    if (tag == 0x001A) {
                        decode_ricoh_faceinfo(
                            mk_prefix,
                            tiff_bytes.subspan(static_cast<size_t>(off_abs),
                                               static_cast<size_t>(value_bytes)),
                            store, options, status_out);
                    }

                    entry.value = decode_tiff_value(
                        cfg, tiff_bytes, type, count, off_abs, value_bytes,
                        store.arena(), options.limits, status_out);
                    decoded = true;
                } else {
                    // Fallback: treat offsets as relative to the subdir block
                    // (`hdr` or `hdr-20`) when they don't fit in the outer TIFF.
                    const uint64_t off_rel = value_or_off32;

                    uint64_t off_a = UINT64_MAX;  // base_alt + off_rel
                    uint64_t off_b = UINT64_MAX;  // hdr + off_rel
                    if (base_alt <= (UINT64_MAX - off_rel)) {
                        off_a = base_alt + off_rel;
                    }
                    if (hdr <= (UINT64_MAX - off_rel)) {
                        off_b = hdr + off_rel;
                    }

                    const bool ok_a = (off_a != UINT64_MAX)
                                      && (off_a + value_bytes <= stable.size());
                    const bool ok_b = (off_b != UINT64_MAX)
                                      && (off_b + value_bytes <= stable.size());

                    if (ok_a || ok_b) {
                        uint64_t value_off = ok_b ? off_b : off_a;
                        if (ok_a && ok_b) {
                            const std::span<const std::byte> a
                                = stable.subspan(static_cast<size_t>(off_a),
                                                 static_cast<size_t>(value_bytes));
                            const std::span<const std::byte> b
                                = stable.subspan(static_cast<size_t>(off_b),
                                                 static_cast<size_t>(value_bytes));

                            if (type == 2 /* ASCII */ || type == 129 /* UTF-8 */) {
                                const uint32_t sa = score_ascii_blob(a);
                                const uint32_t sb = score_ascii_blob(b);
                                value_off = (sb >= sa) ? off_b : off_a;
                            } else if (tag == 0x001A && type == 1 /* BYTE */) {
                                const uint32_t sa = score_ricoh_faceinfo_blob(a);
                                const uint32_t sb = score_ricoh_faceinfo_blob(b);
                                value_off = (sb >= sa) ? off_b : off_a;
                            } else {
                                value_off = off_b;
                            }
                        }

                        if (value_off + value_bytes <= stable.size()) {
                            if (tag == 0x001A) {
                                decode_ricoh_faceinfo(
                                    mk_prefix,
                                    stable.subspan(static_cast<size_t>(value_off),
                                                   static_cast<size_t>(value_bytes)),
                                    store, options, status_out);
                            }

                            entry.value = decode_tiff_value(
                                cfg, stable, type, count, value_off, value_bytes,
                                store.arena(), options.limits, status_out);
                            decoded = true;
                        }
                    }
                }
            }

            if (!decoded) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                entry.flags |= EntryFlags::Unreadable;
            }
        }

        (void)store.add_entry(entry);
        added = true;
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }

    return added;
}

}  // namespace

bool decode_ricoh_makernote(const TiffConfig& parent_cfg,
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

    const std::span<const std::byte> mn
        = tiff_bytes.subspan(static_cast<size_t>(maker_note_off),
                             static_cast<size_t>(maker_note_bytes));

    const std::string_view mk_prefix = options.tokens.ifd_prefix;

    // Ricoh::Type2 maker notes (RICOH header + little-endian IFD-like table).
    if (decode_ricoh_type2_ricoh_header_ifd(mn, mk_prefix, store, options,
                                            status_out)) {
        return true;
    }

    // Ricoh "Type2" maker notes (eg. Ricoh HZ15, Pentax XG-1).
    if (decode_ricoh_type2_padded_ifd(mn, mk_ifd0, store, options, status_out)) {
        return true;
    }

    // Ricoh MakerNote IFD: ExifTool uses Start => $valuePtr + 8, but some
    // real-world samples appear to have 2 bytes of padding after the 8-byte
    // header. Try both locations and pick the best-scoring IFD.
    ClassicIfdCandidate best;
    bool have_best = false;

    const uint64_t candidates[] = { 8, 10 };
    for (uint32_t ci = 0; ci < sizeof(candidates) / sizeof(candidates[0]);
         ++ci) {
        const uint64_t off = candidates[ci];
        for (int endian = 0; endian < 2; ++endian) {
            TiffConfig cfg;
            cfg.bigtiff = false;
            cfg.le      = (endian == 0);
            ClassicIfdCandidate cand;
            if (!score_classic_ifd_candidate(cfg, mn, off, options.limits,
                                             &cand)) {
                continue;
            }
            if (!have_best || cand.valid_entries > best.valid_entries) {
                best      = cand;
                have_best = true;
            }
        }
    }

    if (!have_best) {
        ClassicIfdCandidate any;
        if (!find_best_classic_ifd_candidate(mn, 256, options.limits, &any)) {
            return false;
        }
        best      = any;
        have_best = true;
    }

    // ExifTool uses Start => $valuePtr + 8 for Ricoh MakerNotes. Many values
    // are stored relative to this base, but some models store absolute offsets
    // relative to the outer EXIF/TIFF. Decode the IFD with a per-entry fallback.
    const uint64_t base = 8;
    if (mn.size() < base + 2) {
        return false;
    }

    TiffConfig cfg;
    cfg.bigtiff = false;
    cfg.le      = best.le;
    decode_ricoh_main_ifd_with_fallback_offsets(cfg, tiff_bytes, mn, best.offset,
                                                base, mk_ifd0, store, options,
                                                status_out);

    const std::span<const std::byte> mn_body = mn.subspan(static_cast<size_t>(base));

    const ByteArena& arena = store.arena();

    // Decode binary substructures. We must not mutate `store` while iterating a
    // snapshot of `store.entries()` because adding derived entries can
    // reallocate the entry vector and invalidate references/spans.
    struct PendingSubdir {
        std::vector<std::byte> bytes {};
        uint64_t abs_off = 0;
        bool pointer_form = false;
    };

    std::vector<std::vector<std::byte>> imageinfo_blobs;
    std::vector<PendingSubdir> subdir_items;
    std::vector<uint64_t> theta_abs_offsets;

    const std::span<const Entry> entries = store.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        if (e.key.kind != MetaKeyKind::ExifTag) {
            continue;
        }
        if (arena_string(arena, e.key.data.exif_tag.ifd) != mk_ifd0) {
            continue;
        }
        const uint16_t tag = e.key.data.exif_tag.tag;
        if (tag == 0x1001 && e.origin.wire_type.family == WireFamily::Tiff
            && e.origin.wire_type.code != 3 /* SHORT */) {
            if (e.value.kind == MetaValueKind::Bytes
                || e.value.kind == MetaValueKind::Array) {
                const std::span<const std::byte> raw = arena.span(e.value.data.span);
                imageinfo_blobs.emplace_back(raw.begin(), raw.end());
            }
        } else if (tag == 0x2001) {
            if (e.value.kind == MetaValueKind::Bytes) {
                const std::span<const std::byte> raw = arena.span(e.value.data.span);
                PendingSubdir item;
                item.bytes.assign(raw.begin(), raw.end());
                item.pointer_form = false;
                subdir_items.emplace_back(std::move(item));
            } else if (e.value.kind == MetaValueKind::Scalar
                       && e.value.elem_type == MetaElementType::U32) {
                // Pointer form: ExifTool uses Start => $val + 20. The pointer
                // is relative to the outer EXIF/TIFF header.
                PendingSubdir item;
                item.abs_off = static_cast<uint32_t>(e.value.data.u64);
                item.pointer_form = true;
                subdir_items.emplace_back(std::move(item));
            }
        } else if (tag == 0x4001 && e.value.kind == MetaValueKind::Scalar
                   && e.value.elem_type == MetaElementType::U32) {
            // ThetaSubdir: ExifTool Start => $val. In practice this behaves
            // like a standard EXIF SubIFD pointer, relative to the outer TIFF.
            theta_abs_offsets.emplace_back(static_cast<uint32_t>(e.value.data.u64));
        }
    }

    bool have_subdir = false;

    // Many real-world Ricoh MakerNotes contain an embedded RicohSubdir block
    // starting with the ASCII marker "[Ricoh Camera Info]". Prefer decoding
    // this block directly instead of guessing bases from other blobs.
    static constexpr char kSubdirHdr[] = "[Ricoh Camera Info]";
    static constexpr uint32_t kSubdirHdrLen = sizeof(kSubdirHdr) - 1;
    uint64_t embedded = UINT64_MAX;
    for (uint64_t i = 0; i + kSubdirHdrLen <= mn_body.size(); ++i) {
        if (match_bytes(mn_body, i, kSubdirHdr, kSubdirHdrLen)) {
            embedded = i;
            break;
        }
    }
    if (embedded != UINT64_MAX) {
        have_subdir
            = decode_ricoh_subdir(
                  mk_prefix,
                  tiff_bytes,
                  mn_body.subspan(static_cast<size_t>(embedded)),
                  store, options, status_out)
              || have_subdir;
    }

    for (const std::vector<std::byte>& blob : imageinfo_blobs) {
        decode_ricoh_imageinfo_u8_table(mk_prefix,
                                        std::span<const std::byte>(blob.data(),
                                                                   blob.size()),
                                        store, options, status_out);
    }

    for (const PendingSubdir& item : subdir_items) {
        if (!item.pointer_form) {
            have_subdir
                = decode_ricoh_subdir(
                      mk_prefix,
                      tiff_bytes,
                      std::span<const std::byte>(item.bytes.data(),
                                                 item.bytes.size()),
                      store, options, status_out)
                  || have_subdir;
            continue;
        }

        if (item.abs_off < tiff_bytes.size()) {
            have_subdir
                = decode_ricoh_subdir(
                      mk_prefix,
                      tiff_bytes,
                      tiff_bytes.subspan(static_cast<size_t>(item.abs_off)),
                      store, options, status_out)
                  || have_subdir;
        }
    }

    uint32_t idx_theta = 0;
    for (uint64_t off_abs : theta_abs_offsets) {
        if (off_abs >= tiff_bytes.size()) {
            continue;
        }
        char scratch[64];
        const std::string_view ifd_name = make_mk_subtable_ifd_token(
            mk_prefix, "thetasubdir", idx_theta++, std::span<char>(scratch));
        if (ifd_name.empty()) {
            continue;
        }
        decode_classic_ifd_no_header(parent_cfg, tiff_bytes, off_abs, ifd_name,
                                     store, options, status_out, EntryFlags::None);
    }

    // If tag-based extraction didn't work, scan for a BigEndian IFD candidate
    // as a best-effort fallback (covers some samples with unusual Subdir bases).
    if (!have_subdir) {
        ClassicIfdCandidate best_be;
        bool have_be = false;

        TiffConfig be_cfg;
        be_cfg.bigtiff = false;
        be_cfg.le      = false;

        const uint64_t scan_bytes
            = (mn_body.size() < 4096) ? mn_body.size() : 4096;
        for (uint64_t off = 0; off + 2 <= scan_bytes; off += 2) {
            ClassicIfdCandidate cand;
            if (!score_classic_ifd_candidate(be_cfg, mn_body, off,
                                             options.limits, &cand)) {
                continue;
            }
            if (!have_be || cand.valid_entries > best_be.valid_entries) {
                best_be = cand;
                have_be = true;
            }
        }

        if (have_be && best_be.valid_entries >= 4) {
            char scratch[64];
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "subdir", 0, std::span<char>(scratch));
            if (!ifd_name.empty()) {
                if (best_be.offset < mn_body.size()) {
                    // Best-effort: for these embedded BigEndian IFDs, offsets
                    // tend to be relative to the IFD start (not the outer
                    // MakerNote base). Decode against a subspan starting at
                    // the candidate IFD.
                    const std::span<const std::byte> sub
                        = mn_body.subspan(static_cast<size_t>(best_be.offset));
                    decode_classic_ifd_no_header(be_cfg, sub, 0, ifd_name, store,
                                                 options, status_out,
                                                 EntryFlags::None);
                }
            }
        }
    }

    // Best-effort decode: FaceInfo lives inside the Subdir table as tag 0x001A.
    // Prefer decoding from the already-decoded mk_* subdir entry to work across
    // both the binary-wrapper path and the generic IFD fallback.
    {
        char scratch_subdir[64];
        const std::string_view subdir_ifd = make_mk_subtable_ifd_token(
            mk_prefix, "subdir", 0, std::span<char>(scratch_subdir));

        // If we already emitted the derived table, don't emit it again.
        char scratch_faceinfo[64];
        const std::string_view face_ifd = make_mk_subtable_ifd_token(
            mk_prefix, "faceinfo", 0, std::span<char>(scratch_faceinfo));

        bool have_faceinfo = false;
        if (!face_ifd.empty()) {
            const std::span<const Entry> all = store.entries();
            for (size_t i = 0; i < all.size(); ++i) {
                const Entry& e = all[i];
                if (e.key.kind != MetaKeyKind::ExifTag) {
                    continue;
                }
                if (arena_string(arena, e.key.data.exif_tag.ifd) == face_ifd) {
                    have_faceinfo = true;
                    break;
                }
            }
        }

        if (!have_faceinfo && !subdir_ifd.empty()) {
            std::vector<std::byte> face_blob;

            const std::span<const Entry> all = store.entries();
            for (size_t i = 0; i < all.size(); ++i) {
                const Entry& e = all[i];
                if (e.key.kind != MetaKeyKind::ExifTag) {
                    continue;
                }
                if (arena_string(arena, e.key.data.exif_tag.ifd) != subdir_ifd) {
                    continue;
                }
                if (e.key.data.exif_tag.tag != 0x001A) {
                    continue;
                }
                if (e.value.kind != MetaValueKind::Bytes
                    && e.value.kind != MetaValueKind::Array) {
                    continue;
                }

                const std::span<const std::byte> raw = arena.span(e.value.data.span);
                face_blob.assign(raw.begin(), raw.end());
                break;
            }

            if (!face_blob.empty()) {
                decode_ricoh_faceinfo(
                    mk_prefix,
                    std::span<const std::byte>(face_blob.data(), face_blob.size()),
                    store, options, status_out);
            }
        }
    }

    return true;
}

}  // namespace openmeta::exif_internal
