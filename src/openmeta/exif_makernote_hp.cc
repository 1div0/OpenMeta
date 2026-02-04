#include "exif_tiff_decode_internal.h"

#include <array>

namespace openmeta::exif_internal {

namespace {

static std::string_view
trim_ascii_nul_and_space(std::string_view s) noexcept
{
    while (!s.empty()) {
        const char c = s.front();
        if (c != '\0' && c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        s.remove_prefix(1);
    }
    while (!s.empty()) {
        const char c = s.back();
        if (c != '\0' && c != ' ' && c != '\t' && c != '\r' && c != '\n') {
            break;
        }
        s.remove_suffix(1);
    }
    return s;
}

static MetaValue
read_fixed_ascii_text(ByteArena& arena, std::span<const std::byte> bytes,
                      uint64_t off, uint64_t n) noexcept
{
    if (off >= bytes.size()) {
        return MetaValue {};
    }
    const uint64_t max_n = bytes.size() - off;
    const uint64_t take  = (n <= max_n) ? n : max_n;
    if (take == 0) {
        return MetaValue {};
    }
    return make_fixed_ascii_text(arena,
                                 bytes.subspan(static_cast<size_t>(off),
                                               static_cast<size_t>(take)));
}

static MetaValue
read_hp_serial(ByteArena& arena, std::span<const std::byte> bytes,
               uint64_t off) noexcept
{
    static constexpr uint64_t kLen = 26;
    if (off + kLen > bytes.size()) {
        return MetaValue {};
    }

    const std::span<const std::byte> raw = bytes.subspan(static_cast<size_t>(off),
                                                         static_cast<size_t>(kLen));

    size_t trimmed = 0;
    while (trimmed < raw.size() && raw[trimmed] != std::byte { 0 }) {
        trimmed += 1;
    }

    const std::string_view text(reinterpret_cast<const char*>(raw.data()), trimmed);
    std::string_view s = trim_ascii_nul_and_space(text);

    static constexpr std::string_view kPrefix = "SERIAL NUMBER:";
    if (s.starts_with(kPrefix)) {
        s.remove_prefix(kPrefix.size());
        s = trim_ascii_nul_and_space(s);
    }

    if (s.empty()) {
        return MetaValue {};
    }
    return make_text(arena, s, TextEncoding::Ascii);
}

}  // namespace

bool
decode_hp_makernote(std::span<const std::byte> maker_note_bytes,
                    std::string_view /*mk_ifd0*/, MetaStore& store,
                    const ExifDecodeOptions& options,
                    ExifDecodeResult* status_out) noexcept
{
    if (maker_note_bytes.size() < 6) {
        return false;
    }
    if (!match_bytes(maker_note_bytes, 0, "IIII", 4)) {
        return false;
    }
    if (u8(maker_note_bytes[5]) != 0) {
        return false;
    }

    const uint8_t kind = u8(maker_note_bytes[4]);
    const bool is_type6 = (kind == 0x06);
    const bool is_type4 = (kind == 0x04 || kind == 0x05);
    if (!is_type4 && !is_type6) {
        return false;
    }

    const std::string_view subtable = is_type6 ? "type6" : "type4";

    // Use a subtable token so registry lookups resolve tag names:
    //   mk_hp_type6_0 -> makernote:hp:type6
    //   mk_hp_type4_0 -> makernote:hp:type4
    char scratch[64];
    const std::string_view ifd_name = make_mk_subtable_ifd_token(
        "mk_hp", subtable, 0, std::span<char>(scratch));
    if (ifd_name.empty()) {
        return false;
    }

    // HP Type4/Type6 MakerNotes are fixed-layout binary blobs where the tag id
    // doubles as the byte offset (ExifTool: ProcessBinaryData).
    std::array<uint16_t, 5> tags {};
    std::array<MetaValue, 5> vals {};
    uint32_t n = 0;

    ByteArena& arena = store.arena();

    if (is_type6) {
        // 0x000c: FNumber (int16u, /10)
        uint16_t fnum10 = 0;
        if (read_u16le(maker_note_bytes, 0x000c, &fnum10)) {
            tags[n] = 0x000c;
            vals[n] = make_urational(fnum10, 10);
            n += 1;
        }
    } else {
        // 0x000c: MaxAperture (int16u, /10)
        uint16_t max_ap10 = 0;
        if (read_u16le(maker_note_bytes, 0x000c, &max_ap10)) {
            tags[n] = 0x000c;
            vals[n] = make_urational(max_ap10, 10);
            n += 1;
        }
    }

    // 0x0010: ExposureTime (int32u, microseconds)
    uint32_t exp_us = 0;
    if (read_u32le(maker_note_bytes, 0x0010, &exp_us)) {
        tags[n] = 0x0010;
        vals[n] = make_urational(exp_us, 1000000U);
        n += 1;
    }

    // 0x0014: CameraDateTime (string[20])
    {
        MetaValue dt = read_fixed_ascii_text(arena, maker_note_bytes, 0x0014, 20);
        if (dt.kind != MetaValueKind::Empty) {
            tags[n] = 0x0014;
            vals[n] = dt;
            n += 1;
        }
    }

    // 0x0034: ISO (int16u)
    uint16_t iso = 0;
    if (read_u16le(maker_note_bytes, 0x0034, &iso)) {
        tags[n] = 0x0034;
        vals[n] = make_u16(iso);
        n += 1;
    }

    // 0x0058/0x005c: SerialNumber (string[26], often "SERIAL NUMBER:<...>")
    {
        const uint64_t serial_off = is_type6 ? 0x0058ULL : 0x005cULL;
        MetaValue serial = read_hp_serial(arena, maker_note_bytes, serial_off);
        if (serial.kind != MetaValueKind::Empty) {
            tags[n] = static_cast<uint16_t>(serial_off);
            vals[n] = serial;
            n += 1;
        }
    }

    if (n == 0) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::Malformed);
        }
        return false;
    }

    emit_bin_dir_entries(ifd_name, store,
                         std::span<const uint16_t>(tags.data(), n),
                         std::span<const MetaValue>(vals.data(), n),
                         options.limits, status_out);
    return true;
}

}  // namespace openmeta::exif_internal

