#include "exif_tiff_decode_internal.h"

namespace openmeta::exif_internal {

namespace {

    static std::string_view olympus_main_subifd_table(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x2010: return "equipment";
        case 0x2020: return "camerasettings";
        case 0x2030: return "rawdevelopment";
        case 0x2031: return "rawdevelopment2";
        case 0x2040: return "imageprocessing";
        case 0x2050: return "focusinfo";
        default: return {};
        }
    }


    static void olympus_decode_ifd(const TiffConfig& cfg,
                                   std::span<const std::byte> mn,
                                   uint64_t ifd_off,
                                   std::string_view ifd_token,
                                   MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept
    {
        if (!looks_like_classic_ifd(cfg, mn, ifd_off, options.limits)) {
            return;
        }
        decode_classic_ifd_no_header(cfg, mn, ifd_off, ifd_token, store, options,
                                     status_out, EntryFlags::None);
    }


    static void olympus_decode_camerasettings_nested(
        const TiffConfig& cfg, std::span<const std::byte> mn,
        uint64_t ifd_off, std::string_view vendor_prefix, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
    {
        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, mn, ifd_off, &entry_count)) {
            return;
        }

        const uint64_t entries_off = ifd_off + 2;
        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            uint16_t tag  = 0;
            uint16_t type = 0;
            uint32_t count = 0;
            if (!read_tiff_u16(cfg, mn, eoff + 0, &tag)
                || !read_tiff_u16(cfg, mn, eoff + 2, &type)
                || !read_tiff_u32(cfg, mn, eoff + 4, &count)) {
                return;
            }

            // Olympus CameraSettings contains nested IFD offsets for some
            // substructures (e.g. AFTargetInfo, SubjectDetectInfo). Only follow
            // scalar offset-style entries (IFD/LONG, count=1).
            if (count != 1U) {
                continue;
            }
            if (type != 4 && type != 13) {
                continue;
            }

            std::string_view subtable;
            switch (tag) {
            case 0x030a: subtable = "aftargetinfo"; break;
            case 0x030b: subtable = "subjectdetectinfo"; break;
            default: break;
            }
            if (subtable.empty()) {
                continue;
            }

            uint32_t sub_ifd_off32 = 0;
            if (!read_tiff_u32(cfg, mn, eoff + 8, &sub_ifd_off32)) {
                continue;
            }
            const uint64_t sub_ifd_off = sub_ifd_off32;
            if (sub_ifd_off >= mn.size()) {
                continue;
            }

            char ifd_buf[96];
            const std::string_view ifd_token = make_mk_subtable_ifd_token(
                vendor_prefix, subtable, 0, std::span<char>(ifd_buf));
            if (ifd_token.empty()) {
                continue;
            }
            olympus_decode_ifd(cfg, mn, sub_ifd_off, ifd_token, store, options,
                               status_out);
        }
    }

}  // namespace

bool decode_olympus_makernote(const TiffConfig& parent_cfg,
                              std::span<const std::byte> tiff_bytes,
                              uint64_t maker_note_off,
                              uint64_t maker_note_bytes,
                              std::string_view mk_ifd0, MetaStore& store,
                              const ExifDecodeOptions& options,
                              ExifDecodeResult* status_out) noexcept
{
    if (maker_note_off > tiff_bytes.size()) {
        return false;
    }
    if (maker_note_bytes > (tiff_bytes.size() - maker_note_off)) {
        return false;
    }
    const std::span<const std::byte> mn
        = tiff_bytes.subspan(static_cast<size_t>(maker_note_off),
                             static_cast<size_t>(maker_note_bytes));
    if (mn.size() < 10) {
        return false;
    }

    // Olympus MakerNotes commonly start with:
    //   "OLYMP\0" + u16(version) + classic IFD (u16 entry_count) at +8
    // with offsets relative to the outer EXIF TIFF header.
    if (match_bytes(mn, 0, "OLYMP\0", 6) || match_bytes(mn, 0, "CAMER\0", 6)) {
        const uint64_t ifd_off = maker_note_off + 8;
        if (!looks_like_classic_ifd(parent_cfg, tiff_bytes, ifd_off,
                                    options.limits)) {
            return false;
        }
        decode_classic_ifd_no_header(parent_cfg, tiff_bytes, ifd_off, mk_ifd0,
                                     store, options, status_out,
                                     EntryFlags::None);
        return true;
    }

    // Newer Olympus MakerNotes start with:
    //   "OLYMPUS\0" + byte order marker + u16(magic?) + classic IFD at +12
    // where sub-IFD offsets (type=IFD) are relative to the MakerNote start.
    if (!match_bytes(mn, 0, "OLYMPUS\0", 8)) {
        return false;
    }
    if (mn.size() < 16) {
        return false;
    }

    const uint8_t b0 = u8(mn[8]);
    const uint8_t b1 = u8(mn[9]);
    TiffConfig cfg;
    if (b0 == 'I' && b1 == 'I') {
        cfg.le = true;
    } else if (b0 == 'M' && b1 == 'M') {
        cfg.le = false;
    } else {
        return false;
    }
    cfg.bigtiff = false;

    const uint64_t main_ifd_off = 12;
    if (!looks_like_classic_ifd(cfg, mn, main_ifd_off, options.limits)) {
        return false;
    }

    olympus_decode_ifd(cfg, mn, main_ifd_off, mk_ifd0, store, options,
                       status_out);

    uint16_t entry_count = 0;
    if (!read_tiff_u16(cfg, mn, main_ifd_off, &entry_count)) {
        return true;
    }

    const std::string_view vendor_prefix = options.tokens.ifd_prefix;

    // Decode known Olympus sub-IFDs.
    const uint64_t entries_off = main_ifd_off + 2;
    for (uint32_t i = 0; i < entry_count; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        uint16_t tag  = 0;
        uint16_t type = 0;
        uint32_t count = 0;
        if (!read_tiff_u16(cfg, mn, eoff + 0, &tag)
            || !read_tiff_u16(cfg, mn, eoff + 2, &type)
            || !read_tiff_u32(cfg, mn, eoff + 4, &count)) {
            break;
        }
        if (type != 13 || count != 1U) {
            continue;
        }

        const std::string_view table = olympus_main_subifd_table(tag);
        if (table.empty()) {
            continue;
        }

        uint32_t sub_ifd_off32 = 0;
        if (!read_tiff_u32(cfg, mn, eoff + 8, &sub_ifd_off32)) {
            continue;
        }
        const uint64_t sub_ifd_off = sub_ifd_off32;
        if (sub_ifd_off >= mn.size()) {
            continue;
        }

        char ifd_buf[96];
        const std::string_view sub_ifd_token = make_mk_subtable_ifd_token(
            vendor_prefix, table, 0, std::span<char>(ifd_buf));
        if (sub_ifd_token.empty()) {
            continue;
        }

        olympus_decode_ifd(cfg, mn, sub_ifd_off, sub_ifd_token, store, options,
                           status_out);

        // Camerasettings commonly contains nested IFD offsets (AFTargetInfo,
        // SubjectDetectInfo).
        if (table == "camerasettings") {
            olympus_decode_camerasettings_nested(cfg, mn, sub_ifd_off,
                                                 vendor_prefix, store, options,
                                                 status_out);
        }
    }

    return true;
}

}  // namespace openmeta::exif_internal
