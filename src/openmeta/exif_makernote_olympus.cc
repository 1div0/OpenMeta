#include "exif_tiff_decode_internal.h"

namespace openmeta::exif_internal {

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
    if (!match_bytes(mn, 0, "OLYMP\0", 6) && !match_bytes(mn, 0, "CAMER\0", 6)) {
        return false;
    }

    // Olympus MakerNotes commonly start with:
    //   "OLYMP\0" + u16(version) + classic IFD (u16 entry_count) at +8
    // with offsets relative to the outer EXIF TIFF header.
    const uint64_t ifd_off = maker_note_off + 8;
    if (!looks_like_classic_ifd(parent_cfg, tiff_bytes, ifd_off,
                                options.limits)) {
        return false;
    }
    decode_classic_ifd_no_header(parent_cfg, tiff_bytes, ifd_off, mk_ifd0,
                                 store, options, status_out, EntryFlags::None);
    return true;
}

}  // namespace openmeta::exif_internal

