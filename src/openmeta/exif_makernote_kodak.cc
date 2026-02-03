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

    static bool decode_kodak_type6(std::span<const std::byte> mn,
                                   std::string_view mk_ifd0, bool le,
                                   MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty() || mn.size() < 0x24) {
            return false;
        }

        uint32_t exposure_u32 = 0;
        uint32_t iso_setting_u32 = 0;
        uint16_t fnumber_u16 = 0;
        uint16_t iso_u16 = 0;
        uint16_t optical_zoom_u16 = 0;
        uint16_t digital_zoom_u16 = 0;
        uint16_t flash_u16 = 0;

        const bool ok = le ? (read_u32le(mn, 0x10, &exposure_u32)
                                  && read_u32le(mn, 0x14, &iso_setting_u32)
                                  && read_u16le(mn, 0x18, &fnumber_u16)
                                  && read_u16le(mn, 0x1a, &iso_u16)
                                  && read_u16le(mn, 0x1c, &optical_zoom_u16)
                                  && read_u16le(mn, 0x1e, &digital_zoom_u16)
                                  && read_u16le(mn, 0x22, &flash_u16))
                           : (read_u32be(mn, 0x10, &exposure_u32)
                                  && read_u32be(mn, 0x14, &iso_setting_u32)
                                  && read_u16be(mn, 0x18, &fnumber_u16)
                                  && read_u16be(mn, 0x1a, &iso_u16)
                                  && read_u16be(mn, 0x1c, &optical_zoom_u16)
                                  && read_u16be(mn, 0x1e, &digital_zoom_u16)
                                  && read_u16be(mn, 0x22, &flash_u16));
        if (!ok) {
            return false;
        }

        const uint16_t tags_out[] = {
            0x0010,  // ExposureTime
            0x0014,  // ISOSetting
            0x0018,  // FNumber
            0x001a,  // ISO
            0x001c,  // OpticalZoom
            0x001e,  // DigitalZoom
            0x0022,  // Flash
        };

        const MetaValue vals_out[] = {
            make_u32(exposure_u32),
            make_u32(iso_setting_u32),
            make_u16(fnumber_u16),
            make_u16(iso_u16),
            make_u16(optical_zoom_u16),
            make_u16(digital_zoom_u16),
            make_u16(flash_u16),
        };

        emit_bin_dir_entries(mk_ifd0, store,
                             std::span<const uint16_t>(tags_out),
                             std::span<const MetaValue>(vals_out),
                             options.limits, status_out);
        return true;
    }

    static bool looks_like_ascii_blob(std::span<const std::byte> mn,
                                      uint64_t off, uint64_t len) noexcept
    {
        if (off + len > mn.size()) {
            return false;
        }
        bool have_printable = false;
        for (uint64_t i = 0; i < len; ++i) {
            const uint8_t c = u8(mn[static_cast<size_t>(off + i)]);
            if (c == 0) {
                break;
            }
            if (c < 0x20 || c > 0x7E) {
                return false;
            }
            have_printable = true;
        }
        return have_printable;
    }

    static bool decode_kodak_type2(std::span<const std::byte> mn,
                                   std::string_view mk_ifd0, MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept
    {
        // ExifTool Kodak::Type2: KodakMaker/KodakModel strings + image width/height.
        if (mk_ifd0.empty() || mn.size() < 0x74) {
            return false;
        }

        if (!looks_like_ascii_blob(mn, 0x08, 32)
            || !looks_like_ascii_blob(mn, 0x28, 32)) {
            return false;
        }

        uint32_t width = 0;
        uint32_t height = 0;
        if (!read_u32be(mn, 0x6c, &width) || !read_u32be(mn, 0x70, &height)) {
            return false;
        }
        if (width == 0 || height == 0 || width > 200000 || height > 200000) {
            return false;
        }

        const uint16_t tags_out[] = {
            0x0008,  // KodakMaker
            0x0028,  // KodakModel
            0x006c,  // KodakImageWidth
            0x0070,  // KodakImageHeight
        };
        MetaValue vals_out[] = {
            make_fixed_ascii_text(store.arena(), mn.subspan(0x08, 32)),
            make_fixed_ascii_text(store.arena(), mn.subspan(0x28, 32)),
            make_u32(width),
            make_u32(height),
        };
        emit_bin_dir_entries(mk_ifd0, store, std::span<const uint16_t>(tags_out),
                             std::span<const MetaValue>(vals_out), options.limits,
                             status_out);
        return true;
    }

    static bool decode_kodak_type3(std::span<const std::byte> mn,
                                   std::string_view mk_ifd0, MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept
    {
        // ExifTool Kodak::Type3: DC240/DC280/DC3400/DC5000.
        if (mk_ifd0.empty() || mn.size() < 0x50) {
            return false;
        }

        uint16_t year = 0;
        uint16_t optical_zoom = 0;
        uint32_t exposure_time = 0;
        uint16_t fnumber = 0;
        uint16_t iso = 0;
        if (!read_u16be(mn, 0x0c, &year) || !read_u16be(mn, 0x1e, &optical_zoom)
            || !read_u32be(mn, 0x38, &exposure_time)
            || !read_u16be(mn, 0x3c, &fnumber) || !read_u16be(mn, 0x4e, &iso)) {
            return false;
        }

        const uint8_t month = u8(mn[0x0e]);
        const uint8_t day   = u8(mn[0x0f]);
        const uint8_t hh    = u8(mn[0x10]);
        const uint8_t mm    = u8(mn[0x11]);
        const uint8_t ss    = u8(mn[0x12]);
        const uint8_t ff    = u8(mn[0x13]);

        int8_t sharpness = 0;
        uint8_t sharp_u8 = 0;
        if (!read_u8(mn, 0x37, &sharp_u8)) {
            return false;
        }
        sharpness = static_cast<int8_t>(sharp_u8);

        const uint16_t tags_out[] = {
            0x000c,  // YearCreated
            0x000e,  // MonthDayCreated
            0x0010,  // TimeCreated
            0x001e,  // OpticalZoom
            0x0037,  // Sharpness
            0x0038,  // ExposureTime
            0x003c,  // FNumber
            0x004e,  // ISO
        };
        MetaValue vals_out[] = {
            make_u16(year),
            make_u8_text_month_day(store.arena(), month, day),
            make_u8_text_time(store.arena(), hh, mm, ss, ff),
            make_u16(optical_zoom),
            make_i8(sharpness),
            make_u32(exposure_time),
            make_u16(fnumber),
            make_u16(iso),
        };
        emit_bin_dir_entries(mk_ifd0, store, std::span<const uint16_t>(tags_out),
                             std::span<const MetaValue>(vals_out), options.limits,
                             status_out);
        return true;
    }

    static bool decode_kodak_type4(std::span<const std::byte> mn,
                                   std::string_view mk_ifd0, MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept
    {
        // ExifTool Kodak::Type4: DC200/DC215 original file name.
        if (mk_ifd0.empty() || mn.size() < (0x20 + 12)) {
            return false;
        }

        const uint16_t tags_out[] = { 0x0020 /* OriginalFileName */ };
        MetaValue vals_out[] = {
            make_fixed_ascii_text(store.arena(), mn.subspan(0x20, 12)),
        };
        emit_bin_dir_entries(mk_ifd0, store, std::span<const uint16_t>(tags_out),
                             std::span<const MetaValue>(vals_out), options.limits,
                             status_out);
        return true;
    }

    static bool decode_kodak_serial_only(std::span<const std::byte> mn,
                                         std::string_view mk_ifd0,
                                         MetaStore& store,
                                         const ExifDecodeOptions& options,
                                         ExifDecodeResult* status_out) noexcept
    {
        // Some Kodak maker notes are just an ASCII serial number at offset 0.
        if (mk_ifd0.empty() || mn.size() < 8) {
            return false;
        }

        size_t n = 0;
        while (n < mn.size() && n < 32) {
            const uint8_t c = u8(mn[n]);
            if (c == 0) {
                break;
            }
            if (c < 0x20 || c > 0x7E) {
                break;
            }
            n += 1;
        }
        if (n < 8) {
            return false;
        }

        bool have_digit = false;
        bool have_alpha = false;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t c = u8(mn[i]);
            if (c >= '0' && c <= '9') {
                have_digit = true;
            } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
                have_alpha = true;
            }
        }
        if (!have_digit || !have_alpha) {
            return false;
        }

        const std::string_view s(reinterpret_cast<const char*>(mn.data()), n);
        const uint16_t tags_out[] = { 0x0000 /* SerialNumber */ };
        const MetaValue vals_out[] = {
            make_text(store.arena(), s, TextEncoding::Ascii),
        };

        emit_bin_dir_entries(mk_ifd0, store, std::span<const uint16_t>(tags_out),
                             std::span<const MetaValue>(vals_out),
                             options.limits, status_out);
        return true;
    }

    static bool decode_kodak_type5(std::span<const std::byte> mn,
                                   std::string_view mk_ifd0, MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept
    {
        // ExifTool Kodak::Type5: CX4200/CX4210/CX4230/CX4300/CX4310/CX6200/CX6230.
        if (mk_ifd0.empty() || mn.size() < 0x2c) {
            return false;
        }

        uint32_t exposure_time = 0;
        uint16_t fnumber = 0;
        uint16_t iso = 0;
        uint16_t optical_zoom = 0;
        uint16_t digital_zoom = 0;
        uint8_t white_balance = 0;
        uint8_t flash_mode = 0;
        uint8_t image_rotated = 0;
        uint8_t macro = 0;
        if (!read_u32be(mn, 0x14, &exposure_time)
            || !read_u16be(mn, 0x1c, &fnumber) || !read_u16be(mn, 0x1e, &iso)
            || !read_u16be(mn, 0x20, &optical_zoom)
            || !read_u16be(mn, 0x22, &digital_zoom)
            || !read_u8(mn, 0x1a, &white_balance)
            || !read_u8(mn, 0x27, &flash_mode)
            || !read_u8(mn, 0x2a, &image_rotated) || !read_u8(mn, 0x2b, &macro)) {
            return false;
        }

        const uint16_t tags_out[] = {
            0x0014,  // ExposureTime
            0x001a,  // WhiteBalance
            0x001c,  // FNumber
            0x001e,  // ISO
            0x0020,  // OpticalZoom
            0x0022,  // DigitalZoom
            0x0027,  // FlashMode
            0x002a,  // ImageRotated
            0x002b,  // Macro
        };
        MetaValue vals_out[] = {
            make_u32(exposure_time),
            make_u8(white_balance),
            make_u16(fnumber),
            make_u16(iso),
            make_u16(optical_zoom),
            make_u16(digital_zoom),
            make_u8(flash_mode),
            make_u8(image_rotated),
            make_u8(macro),
        };
        emit_bin_dir_entries(mk_ifd0, store, std::span<const uint16_t>(tags_out),
                             std::span<const MetaValue>(vals_out), options.limits,
                             status_out);
        return true;
    }

    static bool decode_kodak_type9(std::span<const std::byte> mn,
                                   std::string_view mk_ifd0, MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty() || mn.size() < 0xc4 + 12) {
            return false;
        }
        // MakerNoteKodak9 begins with "IIII" and contains an ASCII
        // YYYY/MM/DD timestamp early in the block.
        if (!match_bytes(mn, 0, "IIII", 4)) {
            return false;
        }

        uint16_t fnum = 0;
        uint32_t exp = 0;
        uint16_t iso = 0;
        (void)read_u16le(mn, 0x0c, &fnum);
        (void)read_u32le(mn, 0x10, &exp);
        (void)read_u16le(mn, 0x34, &iso);

        const std::span<const std::byte> dt_raw
            = mn.subspan(0x14, 20);  // "YYYY/MM/DD HH:MM:SS\0"
        const std::span<const std::byte> fw_raw
            = mn.subspan(0x57, 16);
        const std::span<const std::byte> num_a8_raw
            = mn.subspan(0xa8, 12);
        const std::span<const std::byte> num_c4_raw
            = mn.subspan(0xc4, 12);

        const uint16_t tags_out[] = {
            0x000c,  // FNumber
            0x0010,  // ExposureTime
            0x0014,  // DateTimeOriginal
            0x0034,  // ISO
            0x0057,  // FirmwareVersion
            0x00a8,  // UnknownNumber
            0x00c4,  // UnknownNumber
        };
        MetaValue vals_out[] = {
            make_u16(fnum),
            make_u32(exp),
            make_fixed_ascii_text(store.arena(), dt_raw),
            make_u16(iso),
            make_fixed_ascii_text(store.arena(), fw_raw),
            make_fixed_ascii_text(store.arena(), num_a8_raw),
            make_fixed_ascii_text(store.arena(), num_c4_raw),
        };

        emit_bin_dir_entries(mk_ifd0, store,
                             std::span<const uint16_t>(tags_out),
                             std::span<const MetaValue>(vals_out),
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
            // Bare IFD (no TIFF header): try both endian variants and pick the
            // best-scoring candidate.
            ClassicIfdCandidate best;
            bool have_best = false;
            for (int endian = 0; endian < 2; ++endian) {
                TiffConfig tmp;
                tmp.bigtiff = false;
                tmp.le      = (endian == 0);
                ClassicIfdCandidate cand;
                if (!score_classic_ifd_candidate(tmp, bytes, 0, options.limits,
                                                 &cand)) {
                    continue;
                }
                if (!have_best || cand.valid_entries > best.valid_entries) {
                    best      = cand;
                    have_best = true;
                }
            }
            if (!have_best || best.valid_entries < 2) {
                return;
            }
            cfg.le   = best.le;
            ifd_off  = 0;
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

    static bool decode_kodak_padded_ifd(const TiffConfig& cfg,
                                        std::span<const std::byte> mn,
                                        uint64_t ifd_off,
                                        std::string_view ifd_name,
                                        MetaStore& store,
                                        const ExifDecodeOptions& options,
                                        ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty()) {
            return false;
        }
        if (mn.size() < 16 || ifd_off + 8 > mn.size()) {
            return false;
        }

        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, mn, ifd_off, &entry_count)) {
            return false;
        }
        if (entry_count == 0 || entry_count > options.limits.max_entries_per_ifd
            || entry_count > 4096) {
            return false;
        }

        // Extra 2 bytes after the entry count.
        const uint64_t entries_off = ifd_off + 4;
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
            if (!read_tiff_u16(cfg, mn, eoff + 0, &tag)
                || !read_tiff_u16(cfg, mn, eoff + 2, &type)) {
                return true;
            }

            uint32_t count32        = 0;
            uint32_t value_or_off32 = 0;
            if (!read_tiff_u32(cfg, mn, eoff + 4, &count32)
                || !read_tiff_u32(cfg, mn, eoff + 8, &value_or_off32)) {
                return true;
            }

            const uint64_t count = count32;
            const uint64_t unit  = tiff_type_size(type);
            if (unit == 0 || count > (UINT64_MAX / unit)) {
                continue;
            }
            const uint64_t value_bytes = count * unit;

            const uint64_t inline_cap      = 4;
            const uint64_t value_field_off = eoff + 8;
            const uint64_t value_off       = (value_bytes <= inline_cap)
                                                 ? value_field_off
                                                 : value_or_off32;

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

        TiffConfig cfg;
        cfg.bigtiff = false;

        uint64_t ifd0_off = 0;

        if ((b0 == 'I' && b1 == 'I') || (b0 == 'M' && b1 == 'M')) {
            cfg.le = (b0 == 'I');

            uint16_t version = 0;
            if (!read_tiff_u16(cfg, mn, 2, &version)) {
                return false;
            }

            if (version == 42) {
                uint32_t ifd0_off32 = 0;
                if (!read_tiff_u32(cfg, mn, 4, &ifd0_off32)) {
                    return false;
                }
                ifd0_off = ifd0_off32;
                if (ifd0_off == 0 || ifd0_off >= mn.size()) {
                    return false;
                }

                // Some Kodak maker notes (eg. PixPro models) include an extra
                // 2 bytes after the IFD entry count.
                {
                    uint16_t entry_count = 0;
                    if (ifd0_off + 8 <= mn.size()
                        && read_tiff_u16(cfg, mn, ifd0_off, &entry_count)
                        && entry_count != 0
                        && entry_count <= options.limits.max_entries_per_ifd) {
                        uint16_t tag0 = 0;
                        uint16_t type0 = 0;
                        uint16_t tag1 = 0;
                        uint16_t type1 = 0;
                        const bool have0
                            = read_tiff_u16(cfg, mn, ifd0_off + 2, &tag0)
                              && read_tiff_u16(cfg, mn, ifd0_off + 4, &type0);
                        const bool have1
                            = read_tiff_u16(cfg, mn, ifd0_off + 4, &tag1)
                              && read_tiff_u16(cfg, mn, ifd0_off + 6, &type1);
                        if (have0 && have1 && tiff_type_size(type0) == 0
                            && tiff_type_size(type1) != 0) {
                            return decode_kodak_padded_ifd(
                                cfg, mn, ifd0_off, mk_ifd0, store, options,
                                status_out);
                        }
                    }
                }

                decode_classic_ifd_no_header(cfg, mn, ifd0_off, mk_ifd0, store,
                                             options, status_out,
                                             EntryFlags::None);
            } else {
                // Kodak Type10: endian marker then classic IFD immediately
                // after it (Start => $valuePtr + 2 in ExifTool).
                ifd0_off = 2;
                if (!looks_like_classic_ifd(cfg, mn, ifd0_off, options.limits)) {
                    return false;
                }
                decode_classic_ifd_no_header(cfg, mn, ifd0_off, mk_ifd0, store,
                                             options, status_out,
                                             EntryFlags::None);
            }
        } else {
            // Kodak Type8a: classic IFD without a TIFF header (ByteOrder unknown).
            ClassicIfdCandidate best;
            bool have_best = false;
            for (int endian = 0; endian < 2; ++endian) {
                TiffConfig tmp;
                tmp.bigtiff = false;
                tmp.le      = (endian == 0);
                ClassicIfdCandidate cand;
                if (!score_classic_ifd_candidate(tmp, mn, 0, options.limits,
                                                 &cand)) {
                    continue;
                }
                if (!have_best || cand.valid_entries > best.valid_entries) {
                    best      = cand;
                    have_best = true;
                }
            }
            if (!have_best || best.valid_entries < 4) {
                return false;
            }
            cfg.le   = best.le;
            ifd0_off = 0;
            decode_classic_ifd_no_header(cfg, mn, ifd0_off, mk_ifd0, store,
                                         options, status_out, EntryFlags::None);
        }

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

            // Pointer-form SubIFDs (FC01..FC06, FF00). These are stored as
            // standard EXIF SubIFD pointers (LONG count 1).
            if (type == 4 && count == 1 && value32 != 0) {
                const std::string_view mk_prefix = options.tokens.ifd_prefix;
                std::string_view table;
                switch (tag) {
                case 0xFC01: table = "subifd1"; break;
                case 0xFC02: table = "subifd2"; break;
                case 0xFC03: table = "subifd3"; break;
                case 0xFC04: table = "subifd4"; break;
                case 0xFC05: table = "subifd5"; break;
                case 0xFC06: table = "subifd6"; break;
                case 0xFF00: table = "camerainfo"; break;
                default: break;
                }
                if (!table.empty()) {
                    const uint64_t off = value32;
                    if (off < mn.size()) {
                        bool decoded = false;

                        // Many Kodak SubIFD pointers (FC01+) are prefixed with
                        // a byte order mark and the IFD begins at +2.
                        if (off + 4 <= mn.size()) {
                            const uint8_t m0 = u8(mn[static_cast<size_t>(off)]);
                            const uint8_t m1
                                = u8(mn[static_cast<size_t>(off + 1)]);
                            if ((m0 == 'I' && m1 == 'I')
                                || (m0 == 'M' && m1 == 'M')) {
                                TiffConfig sub_cfg;
                                sub_cfg.bigtiff = false;
                                sub_cfg.le      = (m0 == 'I');
                                const uint64_t sub_ifd_off = off + 2;
                                if (looks_like_classic_ifd(sub_cfg, mn,
                                                           sub_ifd_off,
                                                           options.limits)) {
                                    char scratch[64];
                                    const std::string_view ifd_token
                                        = make_mk_subtable_ifd_token(
                                            mk_prefix, table, 0,
                                            std::span<char>(scratch));
                                    if (!ifd_token.empty()) {
                                        decode_classic_ifd_no_header(
                                            sub_cfg, mn, sub_ifd_off, ifd_token,
                                            store, options, status_out,
                                            EntryFlags::None);
                                        decoded = true;
                                    }
                                }
                            }
                        }

                        if (!decoded) {
                            ClassicIfdCandidate cand;
                            if (find_best_ifd_near(mn, off, 2048, options.limits,
                                                   &cand)) {
                                char scratch[64];
                                const std::string_view ifd_token
                                    = make_mk_subtable_ifd_token(
                                        mk_prefix, table, 0,
                                        std::span<char>(scratch));
                                if (!ifd_token.empty()) {
                                    TiffConfig sub_cfg;
                                    sub_cfg.le      = cand.le;
                                    sub_cfg.bigtiff = false;
                                    decode_classic_ifd_no_header(
                                        sub_cfg, mn, cand.offset, ifd_token,
                                        store, options, status_out,
                                        EntryFlags::None);
                                }
                            }
                        }
                    }
                }
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

    static bool decode_kodak_type8_absolute(const TiffConfig& parent_cfg,
                                            std::span<const std::byte> tiff_bytes,
                                            uint64_t maker_note_off,
                                            uint64_t maker_note_bytes,
                                            std::string_view mk_ifd0,
                                            MetaStore& store,
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
            static_cast<size_t>(maker_note_off),
            static_cast<size_t>(maker_note_bytes));

        // Skip self-contained TIFF-header variants (handled by decode_kodak_tiff).
        if (mn.size() >= 4
            && ((u8(mn[0]) == 'I' && u8(mn[1]) == 'I')
                || (u8(mn[0]) == 'M' && u8(mn[1]) == 'M'))) {
            TiffConfig tmp;
            tmp.bigtiff = false;
            tmp.le      = (u8(mn[0]) == 'I');
            uint16_t version = 0;
            if (read_tiff_u16(tmp, mn, 2, &version) && version == 42) {
                return false;
            }
        }

        // Kodak Type8a/Type10: classic IFD inside the MakerNote blob, but
        // offsets are relative to the outer EXIF/TIFF header.
        uint64_t ifd0_off = maker_note_off;
        TiffConfig cfg;
        cfg.bigtiff = false;

        if (mn.size() >= 2
            && ((u8(mn[0]) == 'I' && u8(mn[1]) == 'I')
                || (u8(mn[0]) == 'M' && u8(mn[1]) == 'M'))) {
            // Kodak Type10: endian marker then IFD at +2.
            cfg.le   = (u8(mn[0]) == 'I');
            ifd0_off = maker_note_off + 2;
            if (!looks_like_classic_ifd(cfg, tiff_bytes, ifd0_off,
                                        options.limits)) {
                return false;
            }
        } else {
            // Kodak Type8a: byte order unknown.
            ClassicIfdCandidate best;
            bool have_best = false;
            for (int endian = 0; endian < 2; ++endian) {
                TiffConfig tmp;
                tmp.bigtiff = false;
                tmp.le      = (endian == 0);
                ClassicIfdCandidate cand;
                if (!score_classic_ifd_candidate(tmp, tiff_bytes, maker_note_off,
                                                 options.limits, &cand)) {
                    continue;
                }
                if (!have_best || cand.valid_entries > best.valid_entries) {
                    best      = cand;
                    have_best = true;
                }
            }
            if (!have_best || best.valid_entries < 4) {
                return false;
            }
            cfg.le   = best.le;
            ifd0_off = maker_note_off;
        }

        decode_classic_ifd_no_header(cfg, tiff_bytes, ifd0_off, mk_ifd0, store,
                                     options, status_out, EntryFlags::None);

        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, ifd0_off, &entry_count)) {
            return true;
        }
        const uint64_t entries_off = ifd0_off + 2;
        const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
        if (entries_off + table_bytes + 4ULL > tiff_bytes.size()) {
            return true;
        }

        const std::string_view mk_prefix = options.tokens.ifd_prefix;

        uint32_t fc00 = 0;
        bool have_fc00 = false;
        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;
            uint16_t tag = 0;
            uint16_t type = 0;
            uint32_t count = 0;
            uint32_t value32 = 0;
            if (!read_tiff_u16(cfg, tiff_bytes, eoff + 0, &tag)
                || !read_tiff_u16(cfg, tiff_bytes, eoff + 2, &type)
                || !read_tiff_u32(cfg, tiff_bytes, eoff + 4, &count)
                || !read_tiff_u32(cfg, tiff_bytes, eoff + 8, &value32)) {
                break;
            }

            if (tag == 0xFC00 && type == 4 && count == 1) {
                fc00      = value32;
                have_fc00 = true;
            }

            // Pointer-form SubIFDs (FC01..FC06, FF00) are absolute offsets
            // into the outer EXIF/TIFF. SubIFD1+ begins with a byte order mark.
            if (type == 4 && count == 1 && value32 != 0) {
                std::string_view table;
                switch (tag) {
                case 0xFC01: table = "subifd1"; break;
                case 0xFC02: table = "subifd2"; break;
                case 0xFC03: table = "subifd3"; break;
                case 0xFC04: table = "subifd4"; break;
                case 0xFC05: table = "subifd5"; break;
                case 0xFC06: table = "subifd6"; break;
                case 0xFF00: table = "camerainfo"; break;
                default: break;
                }
                if (!table.empty()) {
                    const uint64_t dir_off = value32;
                    if (dir_off + 4 <= tiff_bytes.size()) {
                        const uint8_t m0 = u8(tiff_bytes[static_cast<size_t>(dir_off)]);
                        const uint8_t m1 = u8(tiff_bytes[static_cast<size_t>(dir_off + 1)]);

                        bool decoded = false;
                        if ((m0 == 'I' && m1 == 'I') || (m0 == 'M' && m1 == 'M')) {
                            TiffConfig sub_cfg;
                            sub_cfg.bigtiff = false;
                            sub_cfg.le      = (m0 == 'I');
                            const uint64_t sub_ifd_off = dir_off + 2;
                            if (looks_like_classic_ifd(sub_cfg, tiff_bytes,
                                                       sub_ifd_off,
                                                       options.limits)) {
                                char scratch[64];
                                const std::string_view ifd_token
                                    = make_mk_subtable_ifd_token(
                                        mk_prefix, table, 0,
                                        std::span<char>(scratch));
                                if (!ifd_token.empty()) {
                                    decode_classic_ifd_no_header(
                                        sub_cfg, tiff_bytes, sub_ifd_off,
                                        ifd_token, store, options, status_out,
                                        EntryFlags::None);
                                    decoded = true;
                                }
                            }
                        }

                        if (!decoded) {
                            ClassicIfdCandidate cand;
                            if (find_best_ifd_near(tiff_bytes, dir_off, 2048,
                                                   options.limits, &cand)) {
                                char scratch[64];
                                const std::string_view ifd_token
                                    = make_mk_subtable_ifd_token(
                                        mk_prefix, table, 0,
                                        std::span<char>(scratch));
                                if (!ifd_token.empty()) {
                                    TiffConfig sub_cfg;
                                    sub_cfg.bigtiff = false;
                                    sub_cfg.le      = cand.le;
                                    decode_classic_ifd_no_header(
                                        sub_cfg, tiff_bytes, cand.offset,
                                        ifd_token, store, options, status_out,
                                        EntryFlags::None);
                                }
                            }
                        }
                    }
                }
            }

            // Embedded 'undef' SubIFDs (M580+): data begins at value32 and
            // includes a byte order mark.
            if (type == 7 && count > 4 && value32 != 0) {
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
                        const uint64_t value_off   = value32;
                        if (value_bytes <= options.limits.max_value_bytes
                            && value_off + value_bytes <= tiff_bytes.size()) {
                            decode_kodak_embedded_subifd(
                                tiff_bytes.subspan(static_cast<size_t>(value_off),
                                                   static_cast<size_t>(value_bytes)),
                                mk_prefix, table, store, options, status_out);
                        }
                    }
                }
            }
        }

        // SubIFD0 (FC00) is not preceded by a byte order mark. It uses the
        // outer EXIF byte order in ExifTool.
        if (have_fc00 && fc00 != 0 && fc00 < tiff_bytes.size()) {
            TiffConfig sub_cfg;
            sub_cfg.bigtiff = false;
            sub_cfg.le      = parent_cfg.le;

            // Prefer decoding exactly at the pointer with the parent byte
            // order (avoids accidentally latching onto SubIFD1+ nearby).
            if (looks_like_classic_ifd(sub_cfg, tiff_bytes, fc00,
                                       options.limits)) {
                char scratch[64];
                const std::string_view ifd_token
                    = make_mk_subtable_ifd_token(mk_prefix, "subifd0", 0,
                                                 std::span<char>(scratch));
                if (!ifd_token.empty()) {
                    decode_classic_ifd_no_header(sub_cfg, tiff_bytes, fc00,
                                                 ifd_token, store, options,
                                                 status_out, EntryFlags::None);
                }
            } else {
                // Fallback: scan near the pointer for a plausible IFD.
                ClassicIfdCandidate cand;
                if (find_best_ifd_near(tiff_bytes, fc00, 512, options.limits,
                                       &cand)) {
                    char scratch[64];
                    const std::string_view ifd_token
                        = make_mk_subtable_ifd_token(
                            mk_prefix, "subifd0", 0, std::span<char>(scratch));
                    if (!ifd_token.empty()) {
                        TiffConfig scan_cfg;
                        scan_cfg.bigtiff = false;
                        scan_cfg.le      = cand.le;
                        decode_classic_ifd_no_header(
                            scan_cfg, tiff_bytes, cand.offset, ifd_token, store,
                            options, status_out, EntryFlags::None);
                    }
                }
            }
        }

        return true;
    }

}  // namespace

bool decode_kodak_makernote(const TiffConfig& parent_cfg,
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

    if (decode_kodak_serial_only(mn, mk_ifd0, store, options, status_out)) {
        return true;
    }

    const std::string_view model = find_first_exif_text_value(
        store, "ifd0", 0x0110 /* Model */);
    if (!model.empty()) {
        if (model.find("DX3215") != std::string_view::npos) {
            return decode_kodak_type6(mn, mk_ifd0, false, store, options,
                                      status_out);
        }
        if (model.find("DX3700") != std::string_view::npos) {
            return decode_kodak_type6(mn, mk_ifd0, true, store, options,
                                      status_out);
        }
    }

    if (decode_kodak_type9(mn, mk_ifd0, store, options, status_out)) {
        return true;
    }

    if (!model.empty()) {
        if (model.find("DC200") != std::string_view::npos
            || model.find("DC210") != std::string_view::npos
            || model.find("DC215") != std::string_view::npos) {
            if (decode_kodak_type4(mn, mk_ifd0, store, options, status_out)) {
                return true;
            }
        }

        if (model.find("DC240") != std::string_view::npos
            || model.find("DC280") != std::string_view::npos
            || model.find("DC3400") != std::string_view::npos
            || model.find("DC5000") != std::string_view::npos) {
            if (decode_kodak_type3(mn, mk_ifd0, store, options, status_out)) {
                return true;
            }
        }

        if (model.find("CX4200") != std::string_view::npos
            || model.find("CX4210") != std::string_view::npos
            || model.find("CX4230") != std::string_view::npos
            || model.find("CX4300") != std::string_view::npos
            || model.find("CX4310") != std::string_view::npos
            || model.find("CX6200") != std::string_view::npos
            || model.find("CX6230") != std::string_view::npos) {
            if (decode_kodak_type5(mn, mk_ifd0, store, options, status_out)) {
                return true;
            }
        }
    }

    if (decode_kodak_type2(mn, mk_ifd0, store, options, status_out)) {
        return true;
    }

    if (decode_kodak_type8_absolute(parent_cfg, tiff_bytes, maker_note_off,
                                    maker_note_bytes, mk_ifd0, store, options,
                                    status_out)) {
        return true;
    }

    return decode_kodak_tiff(mn, mk_ifd0, store, options, status_out);
}

}  // namespace openmeta::exif_internal
