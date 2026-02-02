#include "exif_tiff_decode_internal.h"

namespace openmeta::exif_internal {

namespace {

struct PentaxSubdirCandidate final {
    uint16_t tag = 0;
    MetaValue value;
};

static void
decode_pentax_u8_table(std::string_view ifd_name,
                       std::span<const std::byte> raw, MetaStore& store,
                       const ExifDecodeOptions& options,
                       ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || raw.empty()) {
        return;
    }
    if (raw.size() > options.limits.max_entries_per_ifd) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    uint32_t order = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
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
        entry.origin.wire_type      = WireType { WireFamily::Tiff, 1 };
        entry.origin.wire_count     = 1;
        entry.flags |= EntryFlags::Derived;
        entry.value = make_u8(u8(raw[i]));

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}

static void
decode_pentax_binary_subdirs_impl(std::string_view mk_ifd0, MetaStore& store,
                                  bool le, const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out) noexcept
{
    (void)le;
    if (mk_ifd0.empty()) {
        return;
    }

    PentaxSubdirCandidate cands[48];
    uint32_t cand_count = 0;

    const ByteArena& arena               = store.arena();
    const std::span<const Entry> entries = store.entries();

    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        if (e.key.kind != MetaKeyKind::ExifTag) {
            continue;
        }
        if (arena_string(arena, e.key.data.exif_tag.ifd) != mk_ifd0) {
            continue;
        }
        if (e.value.kind != MetaValueKind::Bytes
            && e.value.kind != MetaValueKind::Array) {
            continue;
        }
        const uint16_t tag = e.key.data.exif_tag.tag;
        switch (tag) {
        case 0x003f:  // LensRec
        case 0x005c:  // ShakeReductionInfo
        case 0x0060:  // FaceInfo
        case 0x0068:  // AWBInfo
        case 0x006b:  // TimeInfo
        case 0x007d:  // LensCorr
        case 0x0205:  // CameraSettings
        case 0x0206:  // AEInfo
        case 0x0207:  // LensInfo
        case 0x0208:  // FlashInfo
        case 0x0215:  // CameraInfo
        case 0x0216:  // BatteryInfo
        case 0x021f:  // AFInfo
        case 0x0221:  // KelvinWB
        case 0x0222:  // ColorInfo
        case 0x0224:  // EVStepInfo
        case 0x0226:  // ShotInfo
        case 0x0227:  // FacePos
        case 0x0228:  // FaceSize
        case 0x022a:  // FilterInfo
        case 0x022b:  // LevelInfo
        case 0x022d:  // WBLevels
        case 0x0239:  // LensInfoQ
        case 0x0243:  // PixelShiftInfo
        case 0x0245:  // AFPointInfo
        case 0x03ff:  // TempInfo
            break;
        default: continue;
        }
        if (cand_count < sizeof(cands) / sizeof(cands[0])) {
            cands[cand_count].tag   = tag;
            cands[cand_count].value = e.value;
            cand_count += 1;
        }
    }

    if (cand_count == 0) {
        return;
    }

    char sub_ifd_buf[96];

    uint32_t idx_camerasettings = 0;
    uint32_t idx_aeinfo         = 0;
    uint32_t idx_lensinfo       = 0;
    uint32_t idx_flashinfo      = 0;
    uint32_t idx_camerainfo     = 0;
    uint32_t idx_batteryinfo    = 0;
    uint32_t idx_afinfo         = 0;
    uint32_t idx_kelvinwb       = 0;
    uint32_t idx_colorinfo      = 0;
    uint32_t idx_evstepinfo     = 0;
    uint32_t idx_shotinfo       = 0;
    uint32_t idx_facepos        = 0;
    uint32_t idx_facesize       = 0;
    uint32_t idx_filterinfo     = 0;
    uint32_t idx_levelinfo      = 0;
    uint32_t idx_wblevels       = 0;
    uint32_t idx_lensinfoq      = 0;
    uint32_t idx_pixelshift     = 0;
    uint32_t idx_afpointinfo    = 0;
    uint32_t idx_tempinfo       = 0;
    uint32_t idx_srinfo         = 0;
    uint32_t idx_faceinfo       = 0;
    uint32_t idx_awbinfo        = 0;
    uint32_t idx_timeinfo       = 0;
    uint32_t idx_lensrec        = 0;
    uint32_t idx_lenscorr       = 0;
    uint32_t idx_lensdata       = 0;

    const std::string_view mk_prefix = "mk_pentax";

    for (uint32_t i = 0; i < cand_count; ++i) {
        const uint16_t tag                       = cands[i].tag;
        const ByteSpan raw_span                  = cands[i].value.data.span;
        const std::span<const std::byte> raw_src = store.arena().span(raw_span);
        if (raw_src.empty()) {
            continue;
        }
        const size_t raw_bytes = raw_src.size();

        if (tag == 0x003f) {  // LensRec
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "lensrec", idx_lensrec++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x005c) {  // ShakeReductionInfo
            const std::string_view subtable
                = (raw_bytes == 4) ? "srinfo" : "srinfo2";
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, subtable, idx_srinfo++, std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0060) {  // FaceInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "faceinfo", idx_faceinfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0068) {  // AWBInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "awbinfo", idx_awbinfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x006b) {  // TimeInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "timeinfo", idx_timeinfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x007d) {  // LensCorr
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "lenscorr", idx_lenscorr++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0205) {  // CameraSettings
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "camerasettings", idx_camerasettings++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0206) {  // AEInfo
            std::string_view subtable;
            if (raw_bytes == 21) {
                subtable = "aeinfo2";
            } else if (raw_bytes == 48) {
                subtable = "aeinfo3";
            } else if (raw_bytes != 0 && raw_bytes <= 25 && raw_bytes != 21) {
                subtable = "aeinfo";
            }
            if (subtable.empty()) {
                continue;
            }
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, subtable, idx_aeinfo++, std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0207) {  // LensInfo
            std::string_view subtable = "lensinfo2";
            if (raw_bytes == 90) {
                subtable = "lensinfo3";
            } else if (raw_bytes == 91) {
                subtable = "lensinfo4";
            } else if (raw_bytes == 80 || raw_bytes == 128) {
                subtable = "lensinfo5";
            } else if (raw_bytes == 168) {
                continue;
            }
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, subtable, idx_lensinfo++, std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);

            size_t lensdata_off = 0;
            size_t lensdata_len = 0;
            if (subtable == "lensinfo") {
                lensdata_off = 3;
                lensdata_len = 17;
            } else if (subtable == "lensinfo2") {
                lensdata_off = 4;
                lensdata_len = 17;
            } else if (subtable == "lensinfo3") {
                lensdata_off = 13;
                lensdata_len = 17;
            } else if (subtable == "lensinfo4") {
                lensdata_off = 12;
                lensdata_len = 18;
            } else if (subtable == "lensinfo5") {
                lensdata_off = 15;
                lensdata_len = 17;
            }
            if (lensdata_len != 0U
                && raw_bytes >= (lensdata_off + lensdata_len)) {
                const std::string_view lensdata_ifd
                    = make_mk_subtable_ifd_token(
                        mk_prefix, "lensdata", idx_lensdata++,
                        std::span<char>(sub_ifd_buf));
                if (!lensdata_ifd.empty()) {
                    decode_pentax_u8_table(
                        lensdata_ifd,
                        raw_src.subspan(lensdata_off, lensdata_len), store,
                        options, status_out);
                }
            }
            continue;
        }

        if (tag == 0x0208) {  // FlashInfo
            if (raw_bytes != 27) {
                continue;
            }
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "flashinfo", idx_flashinfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0215) {  // CameraInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "camerainfo", idx_camerainfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0216) {  // BatteryInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "batteryinfo", idx_batteryinfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x021f) {  // AFInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "afinfo", idx_afinfo++, std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0221) {  // KelvinWB
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "kelvinwb", idx_kelvinwb++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0222) {  // ColorInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "colorinfo", idx_colorinfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0224) {  // EVStepInfo
            if (raw_bytes > 200) {
                continue;
            }
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "evstepinfo", idx_evstepinfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0226) {  // ShotInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "shotinfo", idx_shotinfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0227) {  // FacePos
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "facepos", idx_facepos++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0228) {  // FaceSize
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "facesize", idx_facesize++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x022a) {  // FilterInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "filterinfo", idx_filterinfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x022b) {  // LevelInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "levelinfo", idx_levelinfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x022d) {  // WBLevels
            if (raw_bytes != 100) {
                continue;
            }
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "wblevels", idx_wblevels++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0239) {  // LensInfoQ
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "lensinfoq", idx_lensinfoq++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0243) {  // PixelShiftInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "pixelshiftinfo", idx_pixelshift++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x0245) {  // AFPointInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "afpointinfo", idx_afpointinfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }

        if (tag == 0x03ff) {  // TempInfo
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "tempinfo", idx_tempinfo++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_pentax_u8_table(ifd_name, raw_src, store, options,
                                   status_out);
            continue;
        }
    }
}

}  // namespace

void
decode_pentax_binary_subdirs(std::string_view mk_ifd0, MetaStore& store, bool le,
                             const ExifDecodeOptions& options,
                             ExifDecodeResult* status_out) noexcept
{
    decode_pentax_binary_subdirs_impl(mk_ifd0, store, le, options, status_out);
}

bool decode_pentax_makernote(std::span<const std::byte> maker_note_bytes,
                             std::string_view mk_ifd0, MetaStore& store,
                             const ExifDecodeOptions& options,
                             ExifDecodeResult* status_out) noexcept
{
    if (maker_note_bytes.size() < 16) {
        return false;
    }
    if (!match_bytes(maker_note_bytes, 0, "AOC\0", 4)) {
        if (match_bytes(maker_note_bytes, 0, "PENTAX ", 7)) {
            const uint64_t hdr_off = 8;
            if (hdr_off >= maker_note_bytes.size()) {
                return false;
            }
            const std::span<const std::byte> body
                = maker_note_bytes.subspan(static_cast<size_t>(hdr_off));

            ClassicIfdCandidate best;
            if (find_best_classic_ifd_candidate(body, 1024, options.limits,
                                                &best)) {
                TiffConfig pent_cfg;
                pent_cfg.bigtiff = false;
                pent_cfg.le      = best.le;
                decode_classic_ifd_no_header(pent_cfg, body, best.offset,
                                             mk_ifd0, store, options,
                                             status_out, EntryFlags::None);
                decode_pentax_binary_subdirs(mk_ifd0, store, pent_cfg.le,
                                             options, status_out);
                return true;
            }
            return false;
        }

        if (maker_note_bytes.size() >= 4) {
            const uint8_t b0 = u8(maker_note_bytes[0]);
            const uint8_t b1 = u8(maker_note_bytes[1]);
            const uint8_t b2 = u8(maker_note_bytes[2]);
            const uint8_t b3 = u8(maker_note_bytes[3]);
            if ((b0 == 0x49 && b1 == 0x49 && b2 == 0x2A && b3 == 0x00)
                || (b0 == 0x4D && b1 == 0x4D && b2 == 0x00 && b3 == 0x2A)) {
                return false;
            }
        }

        TiffConfig alt_cfg;
        alt_cfg.bigtiff = false;
        alt_cfg.le      = true;
        if (!looks_like_classic_ifd(alt_cfg, maker_note_bytes, 0,
                                    options.limits)) {
            alt_cfg.le = false;
            if (!looks_like_classic_ifd(alt_cfg, maker_note_bytes, 0,
                                        options.limits)) {
                return false;
            }
        }
        decode_classic_ifd_no_header(alt_cfg, maker_note_bytes, 0, mk_ifd0,
                                     store, options, status_out,
                                     EntryFlags::None);
        decode_pentax_binary_subdirs(mk_ifd0, store, alt_cfg.le, options,
                                     status_out);
        return true;
    }

    const uint8_t b4 = u8(maker_note_bytes[4]);
    const uint8_t b5 = u8(maker_note_bytes[5]);

    TiffConfig cfg;
    cfg.bigtiff = false;
    if (b4 == 0x49 && b5 == 0x49) {  // "II"
        cfg.le = true;
    } else if (b4 == 0x4D && b5 == 0x4D) {  // "MM"
        cfg.le = false;
    } else if (b4 == 0x20 && b5 == 0x20) {  // "  "
        cfg.le = false;
    } else if (b4 == 0x00 && b5 == 0x00 && maker_note_bytes.size() >= 10) {
        const uint8_t t0 = u8(maker_note_bytes[8]);
        const uint8_t t1 = u8(maker_note_bytes[9]);
        if (t0 == 0x01 && t1 == 0x00) {
            cfg.le = true;
        } else if (t0 == 0x00 && t1 == 0x01) {
            cfg.le = false;
        } else {
            cfg.le = false;
        }
    } else {
        // Default to big-endian for unknown AOC header variants.
        cfg.le = false;
    }

    uint16_t entry_count = 0;
    if (!read_tiff_u16(cfg, maker_note_bytes, 6, &entry_count)) {
        return false;
    }
    if (entry_count == 0 || entry_count > options.limits.max_entries_per_ifd) {
        return false;
    }
    if (entry_count > 2048) {
        return false;
    }

    const uint64_t entries_off = 8;
    const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
    const uint64_t needed      = entries_off + table_bytes + 4ULL;
    if (needed > maker_note_bytes.size()) {
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
        if (!read_tiff_u16(cfg, maker_note_bytes, eoff + 0, &tag)
            || !read_tiff_u16(cfg, maker_note_bytes, eoff + 2, &type)) {
            return true;
        }

        uint32_t count32        = 0;
        uint32_t value_or_off32 = 0;
        if (!read_tiff_u32(cfg, maker_note_bytes, eoff + 4, &count32)
            || !read_tiff_u32(cfg, maker_note_bytes, eoff + 8,
                              &value_or_off32)) {
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

        const uint64_t inline_cap      = 4;
        const uint64_t value_field_off = eoff + 8;
        const uint64_t value_off
            = (value_bytes <= inline_cap) ? value_field_off : value_or_off32;

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return true;
        }

        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), mk_ifd0, tag);
        entry.origin.block          = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
        entry.origin.wire_count     = static_cast<uint32_t>(count);

        if (value_bytes > options.limits.max_value_bytes) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            entry.flags |= EntryFlags::Truncated;
        } else if (value_off + value_bytes > maker_note_bytes.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            entry.flags |= EntryFlags::Unreadable;
        } else {
            entry.value = decode_tiff_value(cfg, maker_note_bytes, type, count,
                                            value_off, value_bytes,
                                            store.arena(), options.limits,
                                            status_out);
        }

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }

    decode_pentax_binary_subdirs(mk_ifd0, store, cfg.le, options, status_out);

    return true;
}

}  // namespace openmeta::exif_internal
