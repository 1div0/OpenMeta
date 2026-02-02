#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstdio>

namespace openmeta::exif_internal {
namespace {

    static bool starts_with_kdk(std::span<const std::byte> bytes) noexcept
    {
        return bytes.size() >= 3 && match_bytes(bytes, 0, "KDK", 3);
    }


    static bool read_u8(std::span<const std::byte> bytes, uint64_t offset,
                        uint8_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (offset >= bytes.size()) {
            return false;
        }
        *out = u8(bytes[offset]);
        return true;
    }


    static MetaValue make_u8_text_time(ByteArena& arena, uint8_t hh,
                                       uint8_t mm, uint8_t ss,
                                       uint8_t frac) noexcept
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "%02u:%02u:%02u.%02u",
                      static_cast<unsigned>(hh), static_cast<unsigned>(mm),
                      static_cast<unsigned>(ss), static_cast<unsigned>(frac));
        return make_text(arena, std::string_view(buf), TextEncoding::Ascii);
    }


    static MetaValue make_u8_text_month_day(ByteArena& arena, uint8_t month,
                                            uint8_t day) noexcept
    {
        char buf[12];
        std::snprintf(buf, sizeof(buf), "%02u:%02u",
                      static_cast<unsigned>(month),
                      static_cast<unsigned>(day));
        return make_text(arena, std::string_view(buf), TextEncoding::Ascii);
    }


    static bool decode_kodak_kdk(std::span<const std::byte> mn,
                                 std::string_view mk_ifd0, MetaStore& store,
                                 const ExifDecodeOptions& options,
                                 ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty()) {
            return false;
        }
        if (!starts_with_kdk(mn)) {
            return false;
        }
        if (mn.size() < 0x70) {
            return false;
        }

        const uint8_t model_c0 = u8(mn[0x08]);
        if (model_c0 < 0x20 || model_c0 > 0x7E) {
            return false;
        }

        // KDK layout (best-effort): observed across ExifTool Kodak samples.
        // Offsets are fixed and values are little-endian unless noted.
        //
        // We emit derived entries (WireFamily::Other) for these fields to keep
        // the core EXIF/TIFF parser clean.
        std::array<uint16_t, 34> tags {};
        std::array<MetaValue, 34> values {};
        size_t n = 0;

        // 0 KodakModel: ASCII starting at +0x08 (trim at space/NUL).
        {
            const uint64_t off = 0x08;
            uint64_t end       = off;
            while (end < mn.size() && (end - off) < 16) {
                const char c = static_cast<char>(u8(mn[end]));
                if (c == '\0' || c == ' ') {
                    break;
                }
                if (c < 0x20 || c > 0x7E) {
                    break;
                }
                end += 1;
            }
            const std::string_view s(
                reinterpret_cast<const char*>(mn.data() + off),
                static_cast<size_t>(end - off));
            tags[n]   = 0x0000;
            values[n] = make_text(store.arena(), s, TextEncoding::Ascii);
            n += 1;
        }

        uint8_t quality = 0;
        uint8_t burst   = 0;
        (void)read_u8(mn, 0x11, &quality);
        (void)read_u8(mn, 0x12, &burst);
        tags[n]   = 0x0009;
        values[n] = make_u8(quality);
        n += 1;
        tags[n]   = 0x000a;
        values[n] = make_u8(burst);
        n += 1;

        uint16_t width = 0;
        uint16_t height = 0;
        (void)read_u16le(mn, 0x14, &width);
        (void)read_u16le(mn, 0x16, &height);
        tags[n]   = 0x000c;
        values[n] = make_u16(width);
        n += 1;
        tags[n]   = 0x000e;
        values[n] = make_u16(height);
        n += 1;

        uint16_t year = 0;
        (void)read_u16le(mn, 0x18, &year);
        tags[n]   = 0x0010;
        values[n] = make_u16(year);
        n += 1;

        uint8_t month = 0;
        uint8_t day   = 0;
        (void)read_u8(mn, 0x1a, &month);
        (void)read_u8(mn, 0x1b, &day);
        tags[n]   = 0x0012;
        values[n] = make_u8_text_month_day(store.arena(), month, day);
        n += 1;

        uint8_t hh = 0;
        uint8_t mm = 0;
        uint8_t ss = 0;
        uint8_t ff = 0;
        (void)read_u8(mn, 0x1c, &hh);
        (void)read_u8(mn, 0x1d, &mm);
        (void)read_u8(mn, 0x1e, &ss);
        (void)read_u8(mn, 0x1f, &ff);
        tags[n]   = 0x0014;
        values[n] = make_u8_text_time(store.arena(), hh, mm, ss, ff);
        n += 1;

        uint16_t burst2 = 0;
        (void)read_u16le(mn, 0x20, &burst2);
        tags[n]   = 0x0018;
        values[n] = make_u16(burst2);
        n += 1;

        uint8_t shutter_mode = 0;
        uint8_t metering_mode = 0;
        (void)read_u8(mn, 0x23, &shutter_mode);
        (void)read_u8(mn, 0x21, &metering_mode);
        tags[n]   = 0x001b;
        values[n] = make_u8(shutter_mode);
        n += 1;
        tags[n]   = 0x001c;
        values[n] = make_u8(metering_mode);
        n += 1;

        uint16_t seq = 0;
        (void)read_u16le(mn, 0x24, &seq);
        tags[n]   = 0x001d;
        values[n] = make_u16(seq);
        n += 1;

        uint16_t fnum100 = 0;
        (void)read_u16le(mn, 0x26, &fnum100);
        tags[n]   = 0x001e;
        values[n] = make_urational(static_cast<uint32_t>(fnum100), 100);
        n += 1;

        uint32_t exp100k = 0;
        (void)read_u32le(mn, 0x28, &exp100k);
        tags[n]   = 0x0020;
        values[n] = make_urational(exp100k, 100000);
        n += 1;

        int16_t exp_comp_raw = 0;
        (void)read_i16_endian(true, mn, 0x2c, &exp_comp_raw);
        tags[n]   = 0x0024;
        values[n] = make_i16(exp_comp_raw);
        n += 1;

        uint16_t various = 0;
        (void)read_u16le(mn, 0x2e, &various);
        tags[n]   = 0x0026;
        values[n] = make_u16(various);
        n += 1;

        uint16_t d1 = 0;
        uint16_t d2 = 0;
        uint16_t d3 = 0;
        uint16_t d4 = 0;
        (void)read_u16le(mn, 0x30, &d1);
        (void)read_u16le(mn, 0x34, &d2);
        (void)read_u16le(mn, 0x38, &d3);
        (void)read_u16le(mn, 0x3c, &d4);
        tags[n]   = 0x0028;
        values[n] = make_u16(d1);
        n += 1;
        tags[n]   = 0x002c;
        values[n] = make_u16(d2);
        n += 1;
        tags[n]   = 0x0030;
        values[n] = make_u16(d3);
        n += 1;
        tags[n]   = 0x0034;
        values[n] = make_u16(d4);
        n += 1;

        uint16_t focus_mode = 0;
        uint16_t various2   = 0;
        uint16_t panorama   = 0;
        uint16_t subject_distance = 0;
        (void)read_u16le(mn, 0x40, &focus_mode);
        (void)read_u16le(mn, 0x42, &various2);
        (void)read_u16le(mn, 0x44, &panorama);
        (void)read_u16le(mn, 0x46, &subject_distance);
        tags[n]   = 0x0038;
        values[n] = make_u16(focus_mode);
        n += 1;
        tags[n]   = 0x003a;
        values[n] = make_u16(various2);
        n += 1;
        tags[n]   = 0x003c;
        values[n] = make_u16(panorama);
        n += 1;
        tags[n]   = 0x003e;
        values[n] = make_u16(subject_distance);
        n += 1;

        uint8_t white_balance = 0;
        (void)read_u8(mn, 0x48, &white_balance);
        tags[n]   = 0x0040;
        values[n] = make_u8(white_balance);
        n += 1;

        uint8_t flash_mode = 0;
        uint8_t flash_fired = 0;
        (void)read_u8(mn, 0x60, &flash_mode);
        (void)read_u8(mn, 0x5c, &flash_fired);
        tags[n]   = 0x005c;
        values[n] = make_u8(flash_mode);
        n += 1;
        tags[n]   = 0x005d;
        values[n] = make_u8(flash_fired);
        n += 1;

        uint8_t iso_setting = 0;
        uint8_t iso = 0;
        (void)read_u8(mn, 0x66, &iso_setting);
        (void)read_u8(mn, 0x68, &iso);
        tags[n]   = 0x005e;
        values[n] = make_u8(iso_setting);
        n += 1;
        tags[n]   = 0x0060;
        values[n] = make_u8(iso);
        n += 1;

        uint16_t zoom100 = 0;
        (void)read_u16le(mn, 0x6a, &zoom100);
        tags[n]   = 0x0062;
        values[n] = make_urational(static_cast<uint32_t>(zoom100), 100);
        n += 1;

        uint8_t date_time_stamp = 0;
        (void)read_u8(mn, 0x65, &date_time_stamp);
        tags[n]   = 0x0064;
        values[n] = make_u8(date_time_stamp);
        n += 1;

        uint16_t color_mode = 0;
        (void)read_u16le(mn, 0x12, &color_mode);
        tags[n]   = 0x0066;
        values[n] = make_u16(color_mode);
        n += 1;

        uint8_t digital_zoom = 0;
        (void)read_u8(mn, 0x5e, &digital_zoom);
        tags[n]   = 0x0068;
        values[n] = make_u8(digital_zoom);
        n += 1;

        uint8_t sharpness = 0;
        (void)read_u8(mn, 0x67, &sharpness);
        tags[n]   = 0x006b;
        values[n] = make_u8(sharpness);
        n += 1;

        if (n != tags.size()) {
            return false;
        }

        emit_bin_dir_entries(mk_ifd0, store,
                             std::span<const uint16_t>(tags.data(), n),
                             std::span<const MetaValue>(values.data(), n),
                             options.limits, status_out);
        return true;
    }


    static bool find_best_ifd_near(std::span<const std::byte> bytes,
                                   uint64_t approx_off, uint64_t radius,
                                   const ExifDecodeLimits& limits,
                                   ClassicIfdCandidate* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (bytes.size() < 16) {
            return false;
        }

        ClassicIfdCandidate best;
        bool found = false;

        const uint64_t start = (approx_off > radius) ? (approx_off - radius)
                                                     : 0;
        const uint64_t end
            = ((approx_off + radius) < bytes.size()) ? (approx_off + radius)
                                                     : bytes.size();

        for (uint64_t off = start; off + 2 <= end; off += 2) {
            for (int endian = 0; endian < 2; ++endian) {
                TiffConfig cfg;
                cfg.le      = (endian == 0);
                cfg.bigtiff = false;

                ClassicIfdCandidate cand;
                if (!score_classic_ifd_candidate(cfg, bytes, off, limits,
                                                 &cand)) {
                    continue;
                }

                const uint64_t dist
                    = (off >= approx_off) ? (off - approx_off)
                                          : (approx_off - off);

                if (!found || cand.valid_entries > best.valid_entries
                    || (cand.valid_entries == best.valid_entries
                        && dist < ((best.offset >= approx_off)
                                       ? (best.offset - approx_off)
                                       : (approx_off - best.offset)))
                    || (cand.valid_entries == best.valid_entries && dist == 0
                        && cand.offset < best.offset)) {
                    best  = cand;
                    found = true;
                }
            }
        }

        if (!found) {
            return false;
        }
        *out = best;
        return true;
    }


    static bool decode_kodak_tiff_subifd0(std::span<const std::byte> mn,
                                         uint64_t ptr_off,
                                         std::string_view mk_prefix,
                                         MetaStore& store,
                                         const ExifDecodeOptions& options,
                                         ExifDecodeResult* status_out) noexcept
    {
        if (ptr_off == 0 || ptr_off >= mn.size()) {
            return false;
        }

        ClassicIfdCandidate cand;
        if (!find_best_ifd_near(mn, ptr_off, 512, options.limits, &cand)) {
            return false;
        }

        char scratch[64];
        const std::string_view sub_ifd0
            = make_mk_subtable_ifd_token(mk_prefix, "subifd0", 0,
                                         std::span<char>(scratch));
        if (sub_ifd0.empty()) {
            return false;
        }

        TiffConfig cfg;
        cfg.le      = cand.le;
        cfg.bigtiff = false;

        decode_classic_ifd_no_header(cfg, mn, cand.offset, sub_ifd0, store,
                                     options, status_out, EntryFlags::None);
        return true;
    }


    static void decode_kodak_embedded_subifd(
        std::span<const std::byte> bytes, std::string_view mk_prefix,
        std::string_view table, MetaStore& store,
        const ExifDecodeOptions& options,
        ExifDecodeResult* status_out) noexcept
    {
        if (bytes.size() < 4 || mk_prefix.empty() || table.empty()) {
            return;
        }

        TiffConfig cfg;
        cfg.bigtiff = false;
        uint64_t ifd_off = 0;

        if ((u8(bytes[0]) == 'I' && u8(bytes[1]) == 'I')
            || (u8(bytes[0]) == 'M' && u8(bytes[1]) == 'M')) {
            cfg.le = (u8(bytes[0]) == 'I');
            if (bytes.size() >= 8) {
                uint16_t version = 0;
                if (read_tiff_u16(cfg, bytes, 2, &version) && version == 42) {
                    uint32_t off32 = 0;
                    if (read_tiff_u32(cfg, bytes, 4, &off32)
                        && off32 < bytes.size()) {
                        ifd_off = off32;
                    } else {
                        return;
                    }
                } else {
                    ifd_off = 2;
                }
            } else {
                ifd_off = 2;
            }
        } else {
            cfg.le = true;
            ifd_off = 0;
        }

        if (!looks_like_classic_ifd(cfg, bytes, ifd_off, options.limits)) {
            return;
        }

        char scratch[64];
        const std::string_view ifd_token
            = make_mk_subtable_ifd_token(mk_prefix, table, 0,
                                         std::span<char>(scratch));
        if (ifd_token.empty()) {
            return;
        }

        decode_classic_ifd_no_header(cfg, bytes, ifd_off, ifd_token, store,
                                     options, status_out, EntryFlags::None);
    }


    static bool decode_kodak_tiff(std::span<const std::byte> mn,
                                  std::string_view mk_ifd0, MetaStore& store,
                                  const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out) noexcept
    {
        if (mn.size() < 8) {
            return false;
        }

        const uint8_t b0 = u8(mn[0]);
        const uint8_t b1 = u8(mn[1]);
        bool le = true;
        if (b0 == 'I' && b1 == 'I') {
            le = true;
        } else if (b0 == 'M' && b1 == 'M') {
            le = false;
        } else {
            return false;
        }

        TiffConfig cfg;
        cfg.le      = le;
        cfg.bigtiff = false;

        uint16_t version = 0;
        if (!read_tiff_u16(cfg, mn, 2, &version)) {
            return false;
        }
        if (version != 42) {
            return false;
        }

        uint32_t ifd0_off32 = 0;
        if (!read_tiff_u32(cfg, mn, 4, &ifd0_off32)) {
            return false;
        }
        const uint64_t ifd0_off = ifd0_off32;
        if (ifd0_off == 0 || ifd0_off >= mn.size()) {
            return false;
        }

        // Decode the Kodak MakerNote IFD0 as-is (keeps raw pointer tags).
        decode_classic_ifd_no_header(cfg, mn, ifd0_off, mk_ifd0, store, options,
                                     status_out, EntryFlags::None);

        // Extract the FC00 offset pointer from the IFD0 table and try to locate
        // the corresponding SubIFD0 by scanning near it. Kodak offsets are
        // sometimes stored relative to a vendor base, so we treat the pointer
        // as an approximate location rather than a trusted absolute offset.
        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, mn, ifd0_off, &entry_count)) {
            return true;
        }
        const uint64_t entries_off = ifd0_off + 2;
        const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
        if (entries_off + table_bytes + 4ULL > mn.size()) {
            return true;
        }

        uint32_t fc00 = 0;
        bool have_fc00 = false;
        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;
            uint16_t tag = 0;
            uint16_t type = 0;
            uint32_t count = 0;
            uint32_t value32 = 0;
            if (!read_tiff_u16(cfg, mn, eoff + 0, &tag)
                || !read_tiff_u16(cfg, mn, eoff + 2, &type)
                || !read_tiff_u32(cfg, mn, eoff + 4, &count)
                || !read_tiff_u32(cfg, mn, eoff + 8, &value32)) {
                break;
            }

            if (tag == 0xFC00 && type == 4 && count == 1) {
                fc00 = value32;
                have_fc00 = true;
            }

            if (type == 7 && count > 4) {
                const std::string_view mk_prefix = options.tokens.ifd_prefix;
                std::string_view table;
                switch (tag) {
                case 0xFC00: table = "subifd0"; break;
                case 0xFC01: table = "subifd1"; break;
                case 0xFC02: table = "subifd2"; break;
                case 0xFC03: table = "subifd3"; break;
                case 0xFC04: table = "subifd4"; break;
                case 0xFC05: table = "subifd5"; break;
                case 0xFC06: table = "subifd6"; break;
                case 0xFCFF: table = "subifd255"; break;
                default: break;
                }
                if (!table.empty()) {
                    const uint64_t unit = tiff_type_size(type);
                    if (unit != 0) {
                        const uint64_t value_bytes = uint64_t(count) * unit;
                        if (value_bytes <= options.limits.max_value_bytes) {
                            const uint64_t value_off = value32;
                            if (value_off + value_bytes <= mn.size()) {
                                const std::span<const std::byte> sub_bytes
                                    = mn.subspan(
                                        static_cast<size_t>(value_off),
                                        static_cast<size_t>(value_bytes));
                                decode_kodak_embedded_subifd(
                                    sub_bytes, mk_prefix, table, store, options,
                                    status_out);
                            }
                        }
                    }
                }
            }
        }

        if (!have_fc00) {
            return true;
        }

        const std::string_view mk_prefix = options.tokens.ifd_prefix;
        (void)decode_kodak_tiff_subifd0(mn, fc00, mk_prefix, store, options,
                                        status_out);
        return true;
    }

}  // namespace

bool decode_kodak_makernote(const TiffConfig& /*parent_cfg*/,
                            std::span<const std::byte> tiff_bytes,
                            uint64_t maker_note_off,
                            uint64_t maker_note_bytes,
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

    const std::span<const std::byte> mn = tiff_bytes.subspan(
        static_cast<size_t>(maker_note_off), static_cast<size_t>(maker_note_bytes));

    if (starts_with_kdk(mn)) {
        return decode_kodak_kdk(mn, mk_ifd0, store, options, status_out);
    }

    return decode_kodak_tiff(mn, mk_ifd0, store, options, status_out);
}

}  // namespace openmeta::exif_internal
