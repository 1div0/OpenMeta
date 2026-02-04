#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>

namespace openmeta::exif_internal {

namespace {

    static bool find_mk_tag_value(std::string_view ifd, uint16_t tag,
                                  const MetaStore& store,
                                  MetaValue* out) noexcept
    {
        if (!out) {
            return false;
        }
        *out = MetaValue {};

        const ByteArena& arena               = store.arena();
        const std::span<const Entry> entries = store.entries();

        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (e.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            if (e.key.data.exif_tag.tag != tag) {
                continue;
            }
            if (arena_string(arena, e.key.data.exif_tag.ifd) != ifd) {
                continue;
            }
            *out = e.value;
            return true;
        }
        return false;
    }

}  // namespace

bool
decode_nintendo_makernote(const TiffConfig& parent_cfg,
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

    // Nintendo MakerNotes start with a classic IFD at offset 0. Some files use
    // value offsets relative to the outer EXIF/TIFF stream.
    TiffConfig cfg      = parent_cfg;
    bool ok_abs_offsets = false;
    bool ok_rel_offsets = false;
    bool ok             = false;

    for (uint32_t attempt = 0; attempt < 2; ++attempt) {
        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, maker_note_off, &entry_count)) {
            cfg.le = !cfg.le;
            continue;
        }
        if (entry_count == 0
            || entry_count > options.limits.max_entries_per_ifd) {
            cfg.le = !cfg.le;
            continue;
        }

        const uint64_t ifd_table_bytes = 2U + uint64_t(entry_count) * 12ULL
                                         + 4ULL;
        const uint64_t mn_end = maker_note_off + maker_note_bytes;
        if (maker_note_off + ifd_table_bytes > mn_end) {
            cfg.le = !cfg.le;
            continue;
        }

        // Decide whether out-of-line value offsets are absolute (into the
        // outer TIFF stream) or relative to the MakerNote blob.
        ok_abs_offsets = false;
        ok_rel_offsets = false;

        const uint64_t entries_off = maker_note_off + 2U;
        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            uint16_t type = 0;
            if (!read_tiff_u16(cfg, tiff_bytes, eoff + 2U, &type)) {
                break;
            }
            const uint64_t unit = tiff_type_size(type);
            if (unit == 0) {
                continue;
            }

            uint32_t count32 = 0;
            uint32_t off32   = 0;
            if (!read_tiff_u32(cfg, tiff_bytes, eoff + 4U, &count32)
                || !read_tiff_u32(cfg, tiff_bytes, eoff + 8U, &off32)) {
                break;
            }
            if (count32 == 0) {
                continue;
            }
            const uint64_t count = count32;
            if (count > (UINT64_MAX / unit)) {
                continue;
            }

            const uint64_t value_bytes = count * unit;
            if (value_bytes <= 4) {
                continue;  // inline
            }

            const uint64_t rel_off = uint64_t(off32);
            const uint64_t abs_off = uint64_t(off32);

            if (rel_off + value_bytes <= maker_note_bytes) {
                ok_rel_offsets = true;
            }
            if (abs_off + value_bytes <= tiff_bytes.size()) {
                ok_abs_offsets = true;
            }

            // If any out-of-line offset is beyond the MakerNote byte count,
            // it can't be a relative offset.
            if (rel_off >= maker_note_bytes && ok_abs_offsets) {
                ok_rel_offsets = false;
                break;
            }
        }

        ok = true;
        break;
    }
    if (!ok) {
        return false;
    }

    if (ok_abs_offsets && !ok_rel_offsets) {
        decode_classic_ifd_no_header(cfg, tiff_bytes, maker_note_off, mk_ifd0,
                                     store, options, status_out,
                                     EntryFlags::None);
    } else {
        decode_classic_ifd_no_header(cfg, mn, 0, mk_ifd0, store, options,
                                     status_out, EntryFlags::None);
    }

    // ExifTool flattens Nintendo CameraInfo fields (tag 0x1101) into the
    // Nintendo group. Decode this binary subdirectory best-effort so `metaread`
    // prints the same tag ids as ExifTool (-D).
    MetaValue cam_dir;
    if (!find_mk_tag_value(mk_ifd0, 0x1101, store, &cam_dir)) {
        return true;  // handled main IFD
    }
    if (cam_dir.kind != MetaValueKind::Bytes
        && cam_dir.kind != MetaValueKind::Array) {
        return true;
    }

    const std::span<const std::byte> cam_src = store.arena().span(
        cam_dir.data.span);
    if (cam_src.empty()) {
        return true;
    }

    // Adding derived tags may grow the arena (realloc), invalidating cam_src.
    // Copy to a stable local buffer first.
    std::array<std::byte, 256> stable {};
    if (cam_src.size() > stable.size()) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return true;
    }
    std::memcpy(stable.data(), cam_src.data(), cam_src.size());
    const std::span<const std::byte> cam(stable.data(), cam_src.size());

    char scratch[64];
    const std::string_view cam_ifd
        = make_mk_subtable_ifd_token("mk_nintendo", "camerainfo", 0,
                                     std::span<char>(scratch));
    if (cam_ifd.empty()) {
        return true;
    }

    std::array<uint16_t, 5> tags {};
    std::array<MetaValue, 5> vals {};
    uint32_t n = 0;

    ByteArena& arena = store.arena();

    // 0x0000: ModelID (undef[4], typically ASCII like "3DS1")
    if (cam.size() >= 4) {
        tags[n] = 0x0000;
        vals[n] = make_fixed_ascii_text(arena, cam.subspan(0, 4));
        n += 1;
    }

    // 0x0008: TimeStamp (int32u)
    uint32_t ts = 0;
    if (read_u32le(cam, 0x0008, &ts)) {
        tags[n] = 0x0008;
        vals[n] = make_u32(ts);
        n += 1;
    }

    // 0x0018: InternalSerialNumber (undef[4])
    if (cam.size() >= 0x0018 + 4) {
        tags[n] = 0x0018;
        vals[n] = make_bytes(arena, cam.subspan(0x0018, 4));
        n += 1;
    }

    // 0x0028: Parallax (float)
    uint32_t par_bits = 0;
    if (read_u32le(cam, 0x0028, &par_bits)) {
        tags[n] = 0x0028;
        vals[n] = make_f32_bits(par_bits);
        n += 1;
    }

    // 0x0030: Category (int16u)
    uint16_t cat = 0;
    if (read_u16le(cam, 0x0030, &cat)) {
        tags[n] = 0x0030;
        vals[n] = make_u16(cat);
        n += 1;
    }

    if (n != 0) {
        emit_bin_dir_entries(cam_ifd, store,
                             std::span<const uint16_t>(tags.data(), n),
                             std::span<const MetaValue>(vals.data(), n),
                             options.limits, status_out);
    }

    return true;
}

}  // namespace openmeta::exif_internal
