#include "exif_tiff_decode_internal.h"

namespace openmeta::exif_internal {

bool decode_panasonic_makernote(
        const TiffConfig& /*parent_cfg*/, std::span<const std::byte> tiff_bytes,
        uint64_t maker_note_off, uint64_t maker_note_bytes,
        std::string_view mk_ifd0, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
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

        ClassicIfdCandidate best;
        bool found = false;

        const uint64_t scan_bytes = (maker_note_bytes < 512U) ? maker_note_bytes
                                                              : 512U;
        const uint64_t scan_end   = maker_note_off + scan_bytes;
        const uint64_t mn_end     = maker_note_off + maker_note_bytes;

        for (uint64_t abs_off = maker_note_off; abs_off + 2 <= scan_end;
             abs_off += 2) {
            for (int endian = 0; endian < 2; ++endian) {
                TiffConfig cfg;
                cfg.le      = (endian == 0);
                cfg.bigtiff = false;

                ClassicIfdCandidate cand;
                if (!score_classic_ifd_candidate(cfg, tiff_bytes, abs_off,
                                                 options.limits, &cand)) {
                    continue;
                }

                const uint64_t table_bytes
                    = 2U + (uint64_t(cand.entry_count) * 12ULL) + 4ULL;
                if (abs_off + table_bytes > mn_end) {
                    continue;
                }

                if (!found || cand.valid_entries > best.valid_entries
                    || (cand.valid_entries == best.valid_entries
                        && cand.offset < best.offset)) {
                    best  = cand;
                    found = true;
                }
            }
        }

        if (!found) {
            return false;
        }

        TiffConfig best_cfg;
        best_cfg.le      = best.le;
        best_cfg.bigtiff = false;

        decode_classic_ifd_no_header(best_cfg, tiff_bytes, best.offset, mk_ifd0,
                                     store, options, status_out,
                                     EntryFlags::None);
        return true;
    }

}  // namespace openmeta::exif_internal

