#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>

namespace openmeta::exif_internal {

namespace {

    static bool span_at(std::span<const std::byte> bytes, uint64_t off,
                        uint64_t n, std::span<const std::byte>* out) noexcept
    {
        if (!out) {
            return false;
        }
        *out = {};
        if (n == 0 || off >= bytes.size()) {
            return false;
        }
        if (off + n > bytes.size()) {
            return false;
        }
        *out = bytes.subspan(static_cast<size_t>(off), static_cast<size_t>(n));
        return true;
    }

    static MetaValue read_ascii(ByteArena& arena,
                                std::span<const std::byte> bytes, uint64_t off,
                                uint64_t n) noexcept
    {
        std::span<const std::byte> raw;
        if (!span_at(bytes, off, n, &raw)) {
            return MetaValue {};
        }
        return make_fixed_ascii_text(arena, raw);
    }

    static MetaValue read_utf16le_text(ByteArena& arena,
                                       std::span<const std::byte> bytes,
                                       uint64_t off, uint64_t n_bytes) noexcept
    {
        std::span<const std::byte> raw;
        if (!span_at(bytes, off, n_bytes, &raw)) {
            return MetaValue {};
        }
        const std::string_view text(reinterpret_cast<const char*>(raw.data()),
                                    raw.size());
        return make_text(arena, text, TextEncoding::Utf16LE);
    }

    static MetaValue read_u8_array(ByteArena& arena,
                                   std::span<const std::byte> bytes,
                                   uint64_t off, uint64_t n) noexcept
    {
        std::span<const std::byte> raw;
        if (!span_at(bytes, off, n, &raw)) {
            return MetaValue {};
        }
        return make_array(arena, MetaElementType::U8, raw, 1);
    }

    static MetaValue read_u16_array(ByteArena& arena,
                                    std::span<const std::byte> bytes,
                                    uint64_t off, uint32_t count) noexcept
    {
        if (count == 0) {
            return MetaValue {};
        }
        const uint64_t n_bytes = static_cast<uint64_t>(count) * 2U;
        std::span<const std::byte> raw;
        if (!span_at(bytes, off, n_bytes, &raw)) {
            return MetaValue {};
        }
        return make_array(arena, MetaElementType::U16, raw, 2);
    }

    static MetaValue read_i16(ByteArena& /*arena*/,
                              std::span<const std::byte> bytes,
                              uint64_t off) noexcept
    {
        int16_t v = 0;
        if (!read_i16_endian(true, bytes, off, &v)) {
            return MetaValue {};
        }
        return make_i16(v);
    }

    static MetaValue read_u16(ByteArena& /*arena*/,
                              std::span<const std::byte> bytes,
                              uint64_t off) noexcept
    {
        uint16_t v = 0;
        if (!read_u16le(bytes, off, &v)) {
            return MetaValue {};
        }
        return make_u16(v);
    }

    static MetaValue read_u32(ByteArena& /*arena*/,
                              std::span<const std::byte> bytes,
                              uint64_t off) noexcept
    {
        uint32_t v = 0;
        if (!read_u32le(bytes, off, &v)) {
            return MetaValue {};
        }
        return make_u32(v);
    }

    static void append_tag(uint16_t tag, const MetaValue& v, uint16_t* tags,
                           MetaValue* vals, uint32_t cap, uint32_t* n) noexcept
    {
        if (!tags || !vals || !n) {
            return;
        }
        if (*n >= cap) {
            return;
        }
        if (v.kind == MetaValueKind::Empty) {
            return;
        }
        tags[*n] = tag;
        vals[*n] = v;
        *n += 1;
    }

    static constexpr uint64_t word_off_u16(uint16_t idx) noexcept
    {
        return static_cast<uint64_t>(idx) * 2U;
    }

    static void append_bytes_tag(ByteArena& arena,
                                 std::span<const std::byte> bytes, uint16_t tag,
                                 uint64_t off, uint64_t len, uint16_t* tags,
                                 MetaValue* vals, uint32_t cap,
                                 uint32_t* n) noexcept
    {
        std::span<const std::byte> raw;
        if (!span_at(bytes, off, len, &raw)) {
            return;
        }
        append_tag(tag, make_bytes(arena, raw), tags, vals, cap, n);
    }

    static bool decode_reconyx_hyperfire(std::span<const std::byte> mn,
                                         std::string_view ifd_name,
                                         MetaStore& store,
                                         const ExifDecodeOptions& options,
                                         ExifDecodeResult* status_out) noexcept
    {
        // HyperFire MakerNotes are an int16u array. Tag ids are word indices.
        //
        // ExifTool: %Image::ExifTool::Reconyx::HyperFire (FORMAT => int16u)
        const uint64_t base = 0;
        ByteArena& arena    = store.arena();

        // Emit only the tags that ExifTool reports in the sample corpus.
        // (This keeps output size bounded while still matching coverage.)
        std::array<uint16_t, 20> tags {};
        std::array<MetaValue, 20> vals {};
        uint32_t n = 0;

        // Word index -> byte offset: `word_off_u16(tag)`.

        // 0x0000: MakerNoteVersion (int16u)
        {
            MetaValue v = read_u16(arena, mn, base + word_off_u16(0x0000));
            append_tag(0x0000, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x0001: FirmwareVersion (int16u[3])
        {
            MetaValue v = read_u16_array(arena, mn, base + word_off_u16(0x0001),
                                         3);
            append_tag(0x0001, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x0004: FirmwareDate (int16u[2])
        {
            MetaValue v = read_u16_array(arena, mn, base + word_off_u16(0x0004),
                                         2);
            append_tag(0x0004, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x0006: TriggerMode (string[2])
        {
            MetaValue v = read_ascii(arena, mn, base + word_off_u16(0x0006), 2);
            append_tag(0x0006, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x0007: Sequence (int16u[2])
        {
            MetaValue v = read_u16_array(arena, mn, base + word_off_u16(0x0007),
                                         2);
            append_tag(0x0007, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x0009: EventNumber (int16u[2])
        {
            MetaValue v = read_u16_array(arena, mn, base + word_off_u16(0x0009),
                                         2);
            append_tag(0x0009, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x000b: DateTimeOriginal (int16u[6])
        {
            MetaValue v = read_u16_array(arena, mn, base + word_off_u16(0x000b),
                                         6);
            append_tag(0x000b, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x0012: MoonPhase (int16u)
        {
            MetaValue v = read_u16(arena, mn, base + word_off_u16(0x0012));
            append_tag(0x0012, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x0013: AmbientTemperatureFahrenheit (int16s)
        {
            MetaValue v = read_i16(arena, mn, base + word_off_u16(0x0013));
            append_tag(0x0013, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x0014: AmbientTemperature (int16s)
        {
            MetaValue v = read_i16(arena, mn, base + word_off_u16(0x0014));
            append_tag(0x0014, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x0015: SerialNumber (unicode[15])
        {
            MetaValue v = read_utf16le_text(arena, mn,
                                            base + word_off_u16(0x0015), 30);
            append_tag(0x0015, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x0024..0x0027: Contrast/Brightness/Sharpness/Saturation (int16u)
        for (uint16_t tag = 0x0024; tag <= 0x0027; ++tag) {
            MetaValue v = read_u16(arena, mn, base + word_off_u16(tag));
            append_tag(tag, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x0028: InfraredIlluminator (int16u)
        {
            MetaValue v = read_u16(arena, mn, base + word_off_u16(0x0028));
            append_tag(0x0028, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x0029: MotionSensitivity (int16u)
        {
            MetaValue v = read_u16(arena, mn, base + word_off_u16(0x0029));
            append_tag(0x0029, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x002a: BatteryVoltage (int16u)
        {
            MetaValue v = read_u16(arena, mn, base + word_off_u16(0x002a));
            append_tag(0x002a, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        // 0x002b: UserLabel (string[22])
        {
            MetaValue v = read_ascii(arena, mn, base + word_off_u16(0x002b),
                                     22);
            append_tag(0x002b, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }

        if (n == 0) {
            return false;
        }
        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags.data(), n),
                             std::span<const MetaValue>(vals.data(), n),
                             options.limits, status_out);
        return true;
    }

    static bool decode_reconyx_hyperfire2(std::span<const std::byte> mn,
                                          std::string_view ifd_name,
                                          MetaStore& store,
                                          const ExifDecodeOptions& options,
                                          ExifDecodeResult* status_out) noexcept
    {
        ByteArena& arena = store.arena();

        std::array<uint16_t, 32> tags {};
        std::array<MetaValue, 32> vals {};
        uint32_t n = 0;

        // Core fields seen in the sample corpus.
        append_tag(0x0010, read_u16(arena, mn, 0x0010), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // FileNumber
        append_tag(0x0012, read_u16(arena, mn, 0x0012), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // DirectoryNumber
        {
            MetaValue v = read_u16_array(arena, mn, 0x0014, 2);
            append_tag(0x0014, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        {
            MetaValue v = read_u16_array(arena, mn, 0x002a, 3);
            append_tag(0x002a, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        {
            MetaValue v = read_u16_array(arena, mn, 0x0030, 2);
            append_tag(0x0030, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        {
            MetaValue v = read_ascii(arena, mn, 0x0034, 2);  // TriggerMode
            append_tag(0x0034, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        {
            MetaValue v = read_u16_array(arena, mn, 0x0036, 2);  // Sequence
            append_tag(0x0036, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        {
            MetaValue v = read_u16_array(arena, mn, 0x003a, 2);  // EventNumber
            append_tag(0x003a, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        {
            MetaValue v = read_u16_array(arena, mn, 0x003e,
                                         6);  // DateTimeOriginal
            append_tag(0x003e, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        append_tag(0x004a, read_u16(arena, mn, 0x004a), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // DayOfWeek
        append_tag(0x004c, read_u16(arena, mn, 0x004c), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // MoonPhase
        append_tag(0x004e, read_i16(arena, mn, 0x004e), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // AmbientTemperatureFahrenheit
        append_tag(0x0050, read_i16(arena, mn, 0x0050), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // AmbientTemperature
        append_tag(0x0052, read_u16(arena, mn, 0x0052), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // Contrast
        append_tag(0x0054, read_u16(arena, mn, 0x0054), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // Brightness
        append_tag(0x0056, read_u16(arena, mn, 0x0056), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // Sharpness
        append_tag(0x0058, read_u16(arena, mn, 0x0058), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // Saturation
        append_tag(0x005a, read_u16(arena, mn, 0x005a), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // Flash
        append_tag(0x005c, read_u16(arena, mn, 0x005c), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // AmbientInfrared
        append_tag(0x005e, read_u16(arena, mn, 0x005e), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // AmbientLight
        append_tag(0x0060, read_u16(arena, mn, 0x0060), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // MotionSensitivity
        append_tag(0x0062, read_u16(arena, mn, 0x0062), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // BatteryVoltage
        append_tag(0x0064, read_u16(arena, mn, 0x0064), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // BatteryVoltageAvg
        append_tag(0x0066, read_u16(arena, mn, 0x0066), tags.data(),
                   vals.data(), static_cast<uint32_t>(tags.size()),
                   &n);  // BatteryType
        {
            MetaValue v = read_ascii(arena, mn, 0x0068, 22);  // UserLabel
            append_tag(0x0068, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }
        {
            MetaValue v = read_utf16le_text(arena, mn, 0x007e,
                                            30);  // SerialNumber
            append_tag(0x007e, v, tags.data(), vals.data(),
                       static_cast<uint32_t>(tags.size()), &n);
        }

        if (n == 0) {
            return false;
        }
        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags.data(), n),
                             std::span<const MetaValue>(vals.data(), n),
                             options.limits, status_out);
        return true;
    }

    static bool decode_reconyx_ultrafire(std::span<const std::byte> mn,
                                         std::string_view ifd_name,
                                         MetaStore& store,
                                         const ExifDecodeOptions& options,
                                         ExifDecodeResult* status_out) noexcept
    {
        ByteArena& arena = store.arena();

        std::array<uint16_t, 20> tags {};
        std::array<MetaValue, 20> vals {};
        uint32_t n = 0;

        const uint32_t cap = static_cast<uint32_t>(tags.size());

        // These tags are the ones ExifTool reports in the sample UltraFire file.
        append_bytes_tag(arena, mn, 0x0018, 0x0018, 7, tags.data(), vals.data(),
                         cap, &n);  // FirmwareVersion (versionInfo)
        append_bytes_tag(arena, mn, 0x001f, 0x001f, 7, tags.data(), vals.data(),
                         cap,
                         &n);  // Micro1Version
        append_bytes_tag(arena, mn, 0x0026, 0x0026, 7, tags.data(), vals.data(),
                         cap,
                         &n);  // BootLoaderVersion
        append_bytes_tag(arena, mn, 0x002d, 0x002d, 7, tags.data(), vals.data(),
                         cap,
                         &n);  // Micro2Version

        append_tag(0x0034, read_ascii(arena, mn, 0x0034, 1), tags.data(),
                   vals.data(), cap, &n);  // TriggerMode
        append_tag(0x0035, read_u8_array(arena, mn, 0x0035, 2), tags.data(),
                   vals.data(), cap, &n);  // Sequence
        append_tag(0x0037, read_u32(arena, mn, 0x0037), tags.data(),
                   vals.data(), cap,
                   &n);  // EventNumber
        append_tag(0x003b, read_u8_array(arena, mn, 0x003b, 7), tags.data(),
                   vals.data(), cap, &n);  // DateTimeOriginal (packed)
        append_tag(0x0042, read_u8_array(arena, mn, 0x0042, 1), tags.data(),
                   vals.data(), cap, &n);  // DayOfWeek
        append_tag(0x0043, read_u8_array(arena, mn, 0x0043, 1), tags.data(),
                   vals.data(), cap, &n);  // MoonPhase
        append_tag(0x0044, read_i16(arena, mn, 0x0044), tags.data(),
                   vals.data(), cap,
                   &n);  // AmbientTemperatureFahrenheit
        append_tag(0x0046, read_i16(arena, mn, 0x0046), tags.data(),
                   vals.data(), cap,
                   &n);  // AmbientTemperature
        append_tag(0x0048, read_u16(arena, mn, 0x0048), tags.data(),
                   vals.data(), cap,
                   &n);  // Illumination
        append_tag(0x0049, read_u16(arena, mn, 0x0049), tags.data(),
                   vals.data(), cap,
                   &n);  // BatteryVoltage
        append_tag(0x004b, read_ascii(arena, mn, 0x004b, 15), tags.data(),
                   vals.data(), cap, &n);  // SerialNumber
        append_tag(0x005a, read_ascii(arena, mn, 0x005a, 21), tags.data(),
                   vals.data(), cap, &n);  // UserLabel

        if (n == 0) {
            return false;
        }
        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags.data(), n),
                             std::span<const MetaValue>(vals.data(), n),
                             options.limits, status_out);
        return true;
    }

}  // namespace

bool
decode_reconyx_makernote(std::span<const std::byte> maker_note_bytes,
                         std::string_view /*mk_ifd0*/, MetaStore& store,
                         const ExifDecodeOptions& options,
                         ExifDecodeResult* status_out) noexcept
{
    if (maker_note_bytes.size() < 4) {
        return false;
    }

    const bool is_hyperfire = (u8(maker_note_bytes[0]) == 0x01
                               && u8(maker_note_bytes[1]) == 0xF1);
    const bool is_h2        = (maker_note_bytes.size() >= 9
                        && match_bytes(maker_note_bytes, 0, "RECONYXH2\0", 9));
    const bool is_uf        = (maker_note_bytes.size() >= 9
                        && match_bytes(maker_note_bytes, 0, "RECONYXUF\0", 9));

    const char* subtable = nullptr;
    if (is_hyperfire) {
        subtable = "hyperfire";
    } else if (is_h2) {
        subtable = "hyperfire2";
    } else if (is_uf) {
        subtable = "ultrafire";
    } else {
        return false;
    }

    char scratch[64];
    const std::string_view ifd_name
        = make_mk_subtable_ifd_token("mk_reconyx", subtable, 0,
                                     std::span<char>(scratch));
    if (ifd_name.empty()) {
        return false;
    }

    if (is_hyperfire) {
        return decode_reconyx_hyperfire(maker_note_bytes, ifd_name, store,
                                        options, status_out);
    }
    if (is_h2) {
        return decode_reconyx_hyperfire2(maker_note_bytes, ifd_name, store,
                                         options, status_out);
    }
    return decode_reconyx_ultrafire(maker_note_bytes, ifd_name, store, options,
                                    status_out);
}

}  // namespace openmeta::exif_internal
