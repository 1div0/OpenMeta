#include "openmeta/exif_tiff_decode.h"

#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include "openmeta/printim_decode.h"

#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>
#include <string_view>

namespace openmeta {
namespace {

    static constexpr uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
    }


    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 8U)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 0U);
        *out = v;
        return true;
    }


    static bool read_u16le(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 0U)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 8U);
        *out = v;
        return true;
    }


    static bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (offset + 4 > bytes.size()) {
            return false;
        }
        const uint32_t v
            = (static_cast<uint32_t>(u8(bytes[offset + 0])) << 24U)
              | (static_cast<uint32_t>(u8(bytes[offset + 1])) << 16U)
              | (static_cast<uint32_t>(u8(bytes[offset + 2])) << 8U)
              | (static_cast<uint32_t>(u8(bytes[offset + 3])) << 0U);
        *out = v;
        return true;
    }


    static bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (offset + 4 > bytes.size()) {
            return false;
        }
        const uint32_t v
            = (static_cast<uint32_t>(u8(bytes[offset + 0])) << 0U)
              | (static_cast<uint32_t>(u8(bytes[offset + 1])) << 8U)
              | (static_cast<uint32_t>(u8(bytes[offset + 2])) << 16U)
              | (static_cast<uint32_t>(u8(bytes[offset + 3])) << 24U);
        *out = v;
        return true;
    }


    static bool read_u64be(std::span<const std::byte> bytes, uint64_t offset,
                           uint64_t* out) noexcept
    {
        if (offset + 8 > bytes.size()) {
            return false;
        }
        uint64_t v = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            v = (v << 8U) | static_cast<uint64_t>(u8(bytes[offset + i]));
        }
        *out = v;
        return true;
    }


    static bool read_u64le(std::span<const std::byte> bytes, uint64_t offset,
                           uint64_t* out) noexcept
    {
        if (offset + 8 > bytes.size()) {
            return false;
        }
        uint64_t v = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(u8(bytes[offset + i])) << (i * 8U);
        }
        *out = v;
        return true;
    }


    static bool read_tiff_u16(const TiffConfig& cfg,
                              std::span<const std::byte> bytes, uint64_t offset,
                              uint16_t* out) noexcept
    {
        if (cfg.le) {
            return read_u16le(bytes, offset, out);
        }
        return read_u16be(bytes, offset, out);
    }


    static bool read_tiff_u32(const TiffConfig& cfg,
                              std::span<const std::byte> bytes, uint64_t offset,
                              uint32_t* out) noexcept
    {
        if (cfg.le) {
            return read_u32le(bytes, offset, out);
        }
        return read_u32be(bytes, offset, out);
    }


    static bool read_tiff_u64(const TiffConfig& cfg,
                              std::span<const std::byte> bytes, uint64_t offset,
                              uint64_t* out) noexcept
    {
        if (cfg.le) {
            return read_u64le(bytes, offset, out);
        }
        return read_u64be(bytes, offset, out);
    }


    static bool is_classic_tiff_header(std::span<const std::byte> bytes,
                                       uint64_t offset) noexcept
    {
        if (offset + 8 > bytes.size()) {
            return false;
        }
        const uint8_t a = u8(bytes[offset + 0]);
        const uint8_t b = u8(bytes[offset + 1]);
        const uint8_t c = u8(bytes[offset + 2]);
        const uint8_t d = u8(bytes[offset + 3]);

        if (a == 'I' && b == 'I' && c == 0x2A && d == 0x00) {
            uint32_t ifd_off = 0;
            if (!read_u32le(bytes, offset + 4, &ifd_off)) {
                return false;
            }
            return static_cast<uint64_t>(ifd_off) < bytes.size();
        }
        if (a == 'M' && b == 'M' && c == 0x00 && d == 0x2A) {
            uint32_t ifd_off = 0;
            if (!read_u32be(bytes, offset + 4, &ifd_off)) {
                return false;
            }
            return static_cast<uint64_t>(ifd_off) < bytes.size();
        }
        return false;
    }


    static uint64_t find_embedded_tiff_header(std::span<const std::byte> bytes,
                                              uint64_t max_search) noexcept
    {
        const uint64_t limit = (max_search < bytes.size()) ? max_search
                                                           : bytes.size();
        for (uint64_t off = 0; off + 8 <= limit; ++off) {
            if (is_classic_tiff_header(bytes, off)) {
                return off;
            }
        }
        return UINT64_MAX;
    }


    static bool match_bytes(std::span<const std::byte> bytes, uint64_t offset,
                            const char* s, uint32_t s_len) noexcept
    {
        if (offset + s_len > bytes.size()) {
            return false;
        }
        return std::memcmp(bytes.data() + static_cast<size_t>(offset), s,
                           static_cast<size_t>(s_len))
               == 0;
    }


    static uint8_t ascii_lower(uint8_t c) noexcept
    {
        if (c >= 'A' && c <= 'Z') {
            return static_cast<uint8_t>(c + ('a' - 'A'));
        }
        return c;
    }


    static bool ascii_starts_with_insensitive(std::string_view s,
                                              std::string_view prefix) noexcept
    {
        if (prefix.size() > s.size()) {
            return false;
        }
        for (size_t i = 0; i < prefix.size(); ++i) {
            const uint8_t a = ascii_lower(static_cast<uint8_t>(s[i]));
            const uint8_t b = ascii_lower(static_cast<uint8_t>(prefix[i]));
            if (a != b) {
                return false;
            }
        }
        return true;
    }


    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }


    static std::string_view
    find_first_exif_ascii_value(const MetaStore& store, std::string_view ifd,
                                uint16_t tag) noexcept
    {
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

            if (e.value.kind == MetaValueKind::Text) {
                return arena_string(arena, e.value.data.span);
            }
            if (e.value.kind != MetaValueKind::Bytes) {
                continue;
            }

            const std::span<const std::byte> raw = arena.span(e.value.data.span);
            size_t n                             = 0;
            while (n < raw.size() && raw[n] != std::byte { 0 }) {
                n += 1;
            }
            while (n > 0 && raw[n - 1] == std::byte { ' ' }) {
                n -= 1;
            }
            if (n == 0) {
                continue;
            }
            return std::string_view(reinterpret_cast<const char*>(raw.data()),
                                    n);
        }
        return {};
    }


    enum class MakerNoteVendor : uint8_t {
        Unknown,
        Nikon,
        Canon,
        Sony,
        Minolta,
        Fuji,
        Apple,
        Olympus,
        Pentax,
        Casio,
        Panasonic,
        Kodak,
        Flir,
        Ricoh,
        Samsung,
        Jvc,
        Dji,
        Ge,
        Motorola,
        Reconyx,
        Hp,
        Nintendo,
    };

    static MakerNoteVendor
    detect_makernote_vendor(std::span<const std::byte> maker_note_bytes,
                            const MetaStore& store) noexcept
    {
        if (maker_note_bytes.size() >= 6
            && match_bytes(maker_note_bytes, 0, "Nikon\0", 6)) {
            return MakerNoteVendor::Nikon;
        }
        // Hasselblad-branded Sony cameras store Sony MakerNotes with a "VHAB"
        // prefix.
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "VHAB", 4)) {
            return MakerNoteVendor::Sony;
        }
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "SONY", 4)) {
            return MakerNoteVendor::Sony;
        }
        if (maker_note_bytes.size() >= 8
            && match_bytes(maker_note_bytes, 0, "FUJIFILM", 8)) {
            return MakerNoteVendor::Fuji;
        }
        if (maker_note_bytes.size() >= 9
            && match_bytes(maker_note_bytes, 0, "Apple iOS", 9)) {
            return MakerNoteVendor::Apple;
        }
        if (maker_note_bytes.size() >= 9
            && match_bytes(maker_note_bytes, 0, "OM SYSTEM", 9)) {
            return MakerNoteVendor::Olympus;
        }
        if (maker_note_bytes.size() >= 6
            && match_bytes(maker_note_bytes, 0, "OLYMP\0", 6)) {
            return MakerNoteVendor::Olympus;
        }
        if (maker_note_bytes.size() >= 6
            && match_bytes(maker_note_bytes, 0, "CAMER\0", 6)) {
            return MakerNoteVendor::Olympus;
        }
        if (maker_note_bytes.size() >= 8
            && match_bytes(maker_note_bytes, 0, "OLYMPUS\0", 8)) {
            return MakerNoteVendor::Olympus;
        }
        if (maker_note_bytes.size() >= 7
            && match_bytes(maker_note_bytes, 0, "PENTAX ", 7)) {
            return MakerNoteVendor::Pentax;
        }
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "AOC\0", 4)) {
            return MakerNoteVendor::Pentax;
        }
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "QVC\0", 4)) {
            return MakerNoteVendor::Casio;
        }
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "IIII", 4)) {
            return MakerNoteVendor::Hp;
        }
        if (maker_note_bytes.size() >= 9
            && match_bytes(maker_note_bytes, 0, "Panasonic", 9)) {
            return MakerNoteVendor::Panasonic;
        }
        if (maker_note_bytes.size() >= 7
            && match_bytes(maker_note_bytes, 0, "RECONYX", 7)) {
            return MakerNoteVendor::Reconyx;
        }
        if (maker_note_bytes.size() >= 4 && u8(maker_note_bytes[0]) == 0x01
            && u8(maker_note_bytes[1]) == 0xF1 && u8(maker_note_bytes[2]) == 0x03
            && u8(maker_note_bytes[3]) == 0x00) {
            return MakerNoteVendor::Reconyx;
        }
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "DJI\0", 4)) {
            return MakerNoteVendor::Dji;
        }

        const std::string_view make
            = find_first_exif_ascii_value(store, "ifd0", 0x010F /* Make */);

        if (!make.empty()) {
            if (ascii_starts_with_insensitive(make, "Nikon")) {
                return MakerNoteVendor::Nikon;
            }
            if (ascii_starts_with_insensitive(make, "Canon")) {
                return MakerNoteVendor::Canon;
            }
            if (ascii_starts_with_insensitive(make, "Sony")) {
                return MakerNoteVendor::Sony;
            }
            if (ascii_starts_with_insensitive(make, "Konica Minolta")
                || ascii_starts_with_insensitive(make, "Minolta")) {
                return MakerNoteVendor::Minolta;
            }
            if (ascii_starts_with_insensitive(make, "FUJIFILM")) {
                return MakerNoteVendor::Fuji;
            }
            if (ascii_starts_with_insensitive(make, "Apple")) {
                return MakerNoteVendor::Apple;
            }
            if (ascii_starts_with_insensitive(make, "OLYMPUS")) {
                return MakerNoteVendor::Olympus;
            }
            if (ascii_starts_with_insensitive(make, "OM Digital")) {
                return MakerNoteVendor::Olympus;
            }
            if (ascii_starts_with_insensitive(make, "PENTAX")) {
                return MakerNoteVendor::Pentax;
            }
            if (ascii_starts_with_insensitive(make, "Asahi")) {
                return MakerNoteVendor::Pentax;
            }
            if (ascii_starts_with_insensitive(make, "CASIO")) {
                return MakerNoteVendor::Casio;
            }
            if (ascii_starts_with_insensitive(make, "Panasonic")) {
                return MakerNoteVendor::Panasonic;
            }
            if (ascii_starts_with_insensitive(make, "Kodak")
                || ascii_starts_with_insensitive(make, "Eastman Kodak")) {
                return MakerNoteVendor::Kodak;
            }
            if (ascii_starts_with_insensitive(make, "FLIR")) {
                return MakerNoteVendor::Flir;
            }
            if (ascii_starts_with_insensitive(make, "RICOH")) {
                return MakerNoteVendor::Ricoh;
            }
            if (ascii_starts_with_insensitive(make, "SAMSUNG")) {
                return MakerNoteVendor::Samsung;
            }
            if (ascii_starts_with_insensitive(make, "JVC")) {
                return MakerNoteVendor::Jvc;
            }
            if (ascii_starts_with_insensitive(make, "DJI")) {
                return MakerNoteVendor::Dji;
            }
            if (ascii_starts_with_insensitive(make, "General Imaging")) {
                return MakerNoteVendor::Ge;
            }
            if (ascii_starts_with_insensitive(make, "Motorola")) {
                return MakerNoteVendor::Motorola;
            }
            if (ascii_starts_with_insensitive(make, "HP")
                || ascii_starts_with_insensitive(make, "hp")
                || ascii_starts_with_insensitive(make, "Hewlett-Packard")
                || ascii_starts_with_insensitive(make, "Hewlett Packard")) {
                return MakerNoteVendor::Hp;
            }
            if (ascii_starts_with_insensitive(make, "Nintendo")) {
                return MakerNoteVendor::Nintendo;
            }
        }

        return MakerNoteVendor::Unknown;
    }


    static void set_makernote_tokens(ExifDecodeOptions* opts,
                                     MakerNoteVendor vendor) noexcept
    {
        if (!opts) {
            return;
        }

        switch (vendor) {
        case MakerNoteVendor::Nikon:
            opts->tokens.ifd_prefix        = "mk_nikon";
            opts->tokens.subifd_prefix     = "mk_nikon_subifd";
            opts->tokens.exif_ifd_token    = "mk_nikon_exififd";
            opts->tokens.gps_ifd_token     = "mk_nikon_gpsifd";
            opts->tokens.interop_ifd_token = "mk_nikon_interopifd";
            return;
        case MakerNoteVendor::Canon:
            opts->tokens.ifd_prefix        = "mk_canon";
            opts->tokens.subifd_prefix     = "mk_canon_subifd";
            opts->tokens.exif_ifd_token    = "mk_canon_exififd";
            opts->tokens.gps_ifd_token     = "mk_canon_gpsifd";
            opts->tokens.interop_ifd_token = "mk_canon_interopifd";
            return;
        case MakerNoteVendor::Sony:
            opts->tokens.ifd_prefix        = "mk_sony";
            opts->tokens.subifd_prefix     = "mk_sony_subifd";
            opts->tokens.exif_ifd_token    = "mk_sony_exififd";
            opts->tokens.gps_ifd_token     = "mk_sony_gpsifd";
            opts->tokens.interop_ifd_token = "mk_sony_interopifd";
            return;
        case MakerNoteVendor::Minolta:
            opts->tokens.ifd_prefix        = "mk_minolta";
            opts->tokens.subifd_prefix     = "mk_minolta_subifd";
            opts->tokens.exif_ifd_token    = "mk_minolta_exififd";
            opts->tokens.gps_ifd_token     = "mk_minolta_gpsifd";
            opts->tokens.interop_ifd_token = "mk_minolta_interopifd";
            return;
        case MakerNoteVendor::Fuji:
            opts->tokens.ifd_prefix        = "mk_fuji";
            opts->tokens.subifd_prefix     = "mk_fuji_subifd";
            opts->tokens.exif_ifd_token    = "mk_fuji_exififd";
            opts->tokens.gps_ifd_token     = "mk_fuji_gpsifd";
            opts->tokens.interop_ifd_token = "mk_fuji_interopifd";
            return;
        case MakerNoteVendor::Apple:
            opts->tokens.ifd_prefix        = "mk_apple";
            opts->tokens.subifd_prefix     = "mk_apple_subifd";
            opts->tokens.exif_ifd_token    = "mk_apple_exififd";
            opts->tokens.gps_ifd_token     = "mk_apple_gpsifd";
            opts->tokens.interop_ifd_token = "mk_apple_interopifd";
            return;
        case MakerNoteVendor::Olympus:
            opts->tokens.ifd_prefix        = "mk_olympus";
            opts->tokens.subifd_prefix     = "mk_olympus_subifd";
            opts->tokens.exif_ifd_token    = "mk_olympus_exififd";
            opts->tokens.gps_ifd_token     = "mk_olympus_gpsifd";
            opts->tokens.interop_ifd_token = "mk_olympus_interopifd";
            return;
        case MakerNoteVendor::Pentax:
            opts->tokens.ifd_prefix        = "mk_pentax";
            opts->tokens.subifd_prefix     = "mk_pentax_subifd";
            opts->tokens.exif_ifd_token    = "mk_pentax_exififd";
            opts->tokens.gps_ifd_token     = "mk_pentax_gpsifd";
            opts->tokens.interop_ifd_token = "mk_pentax_interopifd";
            return;
        case MakerNoteVendor::Casio:
            // Casio MakerNote "type2" uses a non-TIFF header ("QVC\0") and
            // a big-endian directory.
            opts->tokens.ifd_prefix        = "mk_casio_type2_";
            opts->tokens.subifd_prefix     = "mk_casio_subifd_";
            opts->tokens.exif_ifd_token    = "mk_casio_exififd";
            opts->tokens.gps_ifd_token     = "mk_casio_gpsifd";
            opts->tokens.interop_ifd_token = "mk_casio_interopifd";
            return;
        case MakerNoteVendor::Panasonic:
            opts->tokens.ifd_prefix        = "mk_panasonic";
            opts->tokens.subifd_prefix     = "mk_panasonic_subifd";
            opts->tokens.exif_ifd_token    = "mk_panasonic_exififd";
            opts->tokens.gps_ifd_token     = "mk_panasonic_gpsifd";
            opts->tokens.interop_ifd_token = "mk_panasonic_interopifd";
            return;
        case MakerNoteVendor::Kodak:
            opts->tokens.ifd_prefix        = "mk_kodak";
            opts->tokens.subifd_prefix     = "mk_kodak_subifd";
            opts->tokens.exif_ifd_token    = "mk_kodak_exififd";
            opts->tokens.gps_ifd_token     = "mk_kodak_gpsifd";
            opts->tokens.interop_ifd_token = "mk_kodak_interopifd";
            return;
        case MakerNoteVendor::Flir:
            opts->tokens.ifd_prefix        = "mk_flir";
            opts->tokens.subifd_prefix     = "mk_flir_subifd";
            opts->tokens.exif_ifd_token    = "mk_flir_exififd";
            opts->tokens.gps_ifd_token     = "mk_flir_gpsifd";
            opts->tokens.interop_ifd_token = "mk_flir_interopifd";
            return;
        case MakerNoteVendor::Ricoh:
            opts->tokens.ifd_prefix        = "mk_ricoh";
            opts->tokens.subifd_prefix     = "mk_ricoh_subifd";
            opts->tokens.exif_ifd_token    = "mk_ricoh_exififd";
            opts->tokens.gps_ifd_token     = "mk_ricoh_gpsifd";
            opts->tokens.interop_ifd_token = "mk_ricoh_interopifd";
            return;
        case MakerNoteVendor::Samsung:
            opts->tokens.ifd_prefix        = "mk_samsung";
            opts->tokens.subifd_prefix     = "mk_samsung_subifd";
            opts->tokens.exif_ifd_token    = "mk_samsung_exififd";
            opts->tokens.gps_ifd_token     = "mk_samsung_gpsifd";
            opts->tokens.interop_ifd_token = "mk_samsung_interopifd";
            return;
        case MakerNoteVendor::Jvc:
            opts->tokens.ifd_prefix        = "mk_jvc";
            opts->tokens.subifd_prefix     = "mk_jvc_subifd";
            opts->tokens.exif_ifd_token    = "mk_jvc_exififd";
            opts->tokens.gps_ifd_token     = "mk_jvc_gpsifd";
            opts->tokens.interop_ifd_token = "mk_jvc_interopifd";
            return;
        case MakerNoteVendor::Dji:
            opts->tokens.ifd_prefix        = "mk_dji";
            opts->tokens.subifd_prefix     = "mk_dji_subifd";
            opts->tokens.exif_ifd_token    = "mk_dji_exififd";
            opts->tokens.gps_ifd_token     = "mk_dji_gpsifd";
            opts->tokens.interop_ifd_token = "mk_dji_interopifd";
            return;
        case MakerNoteVendor::Ge:
            opts->tokens.ifd_prefix        = "mk_ge";
            opts->tokens.subifd_prefix     = "mk_ge_subifd";
            opts->tokens.exif_ifd_token    = "mk_ge_exififd";
            opts->tokens.gps_ifd_token     = "mk_ge_gpsifd";
            opts->tokens.interop_ifd_token = "mk_ge_interopifd";
            return;
        case MakerNoteVendor::Motorola:
            opts->tokens.ifd_prefix        = "mk_motorola";
            opts->tokens.subifd_prefix     = "mk_motorola_subifd";
            opts->tokens.exif_ifd_token    = "mk_motorola_exififd";
            opts->tokens.gps_ifd_token     = "mk_motorola_gpsifd";
            opts->tokens.interop_ifd_token = "mk_motorola_interopifd";
            return;
        case MakerNoteVendor::Reconyx:
            opts->tokens.ifd_prefix        = "mk_reconyx";
            opts->tokens.subifd_prefix     = "mk_reconyx_subifd";
            opts->tokens.exif_ifd_token    = "mk_reconyx_exififd";
            opts->tokens.gps_ifd_token     = "mk_reconyx_gpsifd";
            opts->tokens.interop_ifd_token = "mk_reconyx_interopifd";
            return;
        case MakerNoteVendor::Hp:
            opts->tokens.ifd_prefix        = "mk_hp";
            opts->tokens.subifd_prefix     = "mk_hp_subifd";
            opts->tokens.exif_ifd_token    = "mk_hp_exififd";
            opts->tokens.gps_ifd_token     = "mk_hp_gpsifd";
            opts->tokens.interop_ifd_token = "mk_hp_interopifd";
            return;
        case MakerNoteVendor::Nintendo:
            opts->tokens.ifd_prefix        = "mk_nintendo";
            opts->tokens.subifd_prefix     = "mk_nintendo_subifd";
            opts->tokens.exif_ifd_token    = "mk_nintendo_exififd";
            opts->tokens.gps_ifd_token     = "mk_nintendo_gpsifd";
            opts->tokens.interop_ifd_token = "mk_nintendo_interopifd";
            return;
        case MakerNoteVendor::Unknown: break;
        }

        opts->tokens.ifd_prefix        = "mkifd";
        opts->tokens.subifd_prefix     = "mk_subifd";
        opts->tokens.exif_ifd_token    = "mk_exififd";
        opts->tokens.gps_ifd_token     = "mk_gpsifd";
        opts->tokens.interop_ifd_token = "mk_interopifd";
    }


    static uint64_t tiff_type_size(uint16_t type) noexcept;

    static void update_status(ExifDecodeResult* out,
                              ExifDecodeStatus status) noexcept;

    static MetaValue decode_tiff_value(const TiffConfig& cfg,
                                       std::span<const std::byte> bytes,
                                       uint16_t type, uint64_t count,
                                       uint64_t value_off, uint64_t value_bytes,
                                       ByteArena& arena,
                                       const ExifDecodeLimits& limits,
                                       ExifDecodeResult* result) noexcept;

    static bool score_classic_ifd_candidate(const TiffConfig& cfg,
                                            std::span<const std::byte> bytes,
                                            uint64_t ifd_off,
                                            const ExifDecodeLimits& limits,
                                            ClassicIfdCandidate* out) noexcept
    {
        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, bytes, ifd_off, &entry_count)) {
            return false;
        }
        if (entry_count == 0 || entry_count > limits.max_entries_per_ifd) {
            return false;
        }
        // Heuristic scan cap: avoid quadratic work across many candidate offsets.
        if (entry_count > 512) {
            return false;
        }

        const uint64_t entries_off = ifd_off + 2;
        const uint64_t table_bytes = uint64_t(entry_count) * 12ULL;
        const uint64_t needed      = entries_off + table_bytes + 4ULL;
        if (needed > bytes.size()) {
            return false;
        }

        uint32_t valid = 0;
        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            uint16_t type = 0;
            if (!read_tiff_u16(cfg, bytes, eoff + 2, &type)) {
                break;
            }

            uint32_t count32        = 0;
            uint32_t value_or_off32 = 0;
            if (!read_tiff_u32(cfg, bytes, eoff + 4, &count32)
                || !read_tiff_u32(cfg, bytes, eoff + 8, &value_or_off32)) {
                break;
            }

            const uint64_t unit = tiff_type_size(type);
            if (unit == 0) {
                continue;
            }
            const uint64_t count = count32;
            if (count > (UINT64_MAX / unit)) {
                continue;
            }

            const uint64_t value_bytes = count * unit;
            if (value_bytes > limits.max_value_bytes) {
                continue;
            }

            const uint64_t inline_cap      = 4;
            const uint64_t value_field_off = eoff + 8;
            const uint64_t value_off       = (value_bytes <= inline_cap)
                                                 ? value_field_off
                                                 : value_or_off32;

            if (value_off + value_bytes > bytes.size()) {
                continue;
            }
            valid += 1;
        }

        if (valid == 0) {
            return false;
        }
        const uint32_t min_valid = (entry_count > 4)
                                       ? (uint32_t(entry_count) / 2U)
                                       : uint32_t(entry_count);
        if (valid < min_valid) {
            return false;
        }

        if (out) {
            out->offset        = ifd_off;
            out->le            = cfg.le;
            out->entry_count   = entry_count;
            out->valid_entries = valid;
        }
        return true;
    }


    static bool find_best_classic_ifd_candidate(
        std::span<const std::byte> bytes, uint64_t max_scan_off,
        const ExifDecodeLimits& limits, ClassicIfdCandidate* out) noexcept
    {
        if (!out) {
            return false;
        }
        *out       = ClassicIfdCandidate {};
        bool found = false;

        const uint64_t scan_cap = (max_scan_off < bytes.size()) ? max_scan_off
                                                                : bytes.size();

        for (uint64_t off = 0; off + 2 <= scan_cap; off += 2) {
            for (int endian = 0; endian < 2; ++endian) {
                TiffConfig cfg;
                cfg.le      = (endian == 0);
                cfg.bigtiff = false;
                ClassicIfdCandidate cand;
                if (!score_classic_ifd_candidate(cfg, bytes, off, limits,
                                                 &cand)) {
                    continue;
                }

                if (!found || cand.valid_entries > out->valid_entries
                    || (cand.valid_entries == out->valid_entries
                        && cand.offset < out->offset)) {
                    *out  = cand;
                    found = true;
                }
            }
        }

        return found;
    }


    static bool looks_like_classic_ifd(const TiffConfig& cfg,
                                       std::span<const std::byte> bytes,
                                       uint64_t ifd_off,
                                       const ExifDecodeLimits& limits) noexcept
    {
        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, bytes, ifd_off, &entry_count)) {
            return false;
        }
        if (entry_count == 0 || entry_count > limits.max_entries_per_ifd) {
            return false;
        }
        const uint64_t entries_off = ifd_off + 2;
        const uint64_t needed = entries_off + (uint64_t(entry_count) * 12ULL)
                                + 4ULL;
        return needed <= bytes.size();
    }


    static void decode_classic_ifd_no_header(
        const TiffConfig& cfg, std::span<const std::byte> bytes,
        uint64_t ifd_off, std::string_view ifd_name, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out,
        EntryFlags extra_flags) noexcept
    {
        if (ifd_name.empty()) {
            return;
        }
        if (!looks_like_classic_ifd(cfg, bytes, ifd_off, options.limits)) {
            return;
        }

        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, bytes, ifd_off, &entry_count)) {
            return;
        }
        const uint64_t entries_off = ifd_off + 2;

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            uint16_t tag  = 0;
            uint16_t type = 0;
            if (!read_tiff_u16(cfg, bytes, eoff + 0, &tag)
                || !read_tiff_u16(cfg, bytes, eoff + 2, &type)) {
                return;
            }

            uint32_t count32        = 0;
            uint32_t value_or_off32 = 0;
            if (!read_tiff_u32(cfg, bytes, eoff + 4, &count32)
                || !read_tiff_u32(cfg, bytes, eoff + 8, &value_or_off32)) {
                return;
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
            const uint64_t value_off       = (value_bytes <= inline_cap)
                                                 ? value_field_off
                                                 : value_or_off32;

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
            } else if (value_off + value_bytes > bytes.size()) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                entry.flags |= EntryFlags::Unreadable;
            } else {
                entry.value = decode_tiff_value(cfg, bytes, type, count,
                                                value_off, value_bytes,
                                                store.arena(), options.limits,
                                                status_out);
            }

            entry.flags |= extra_flags;

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }


    static std::string_view
    make_mk_subtable_ifd_token(std::string_view vendor_prefix,
                               std::string_view subtable, uint32_t index,
                               std::span<char> scratch) noexcept
    {
        if (vendor_prefix.empty() || subtable.empty() || scratch.empty()) {
            return {};
        }

        static constexpr size_t kMaxIndexDigits = 11;
        const uint64_t min_needed = uint64_t(vendor_prefix.size()) + 1U
                                    + uint64_t(subtable.size()) + 1U
                                    + kMaxIndexDigits;
        if (min_needed > scratch.size()) {
            return {};
        }

        size_t n = 0;
        for (size_t i = 0; i < vendor_prefix.size(); ++i) {
            scratch[n++] = vendor_prefix[i];
        }
        scratch[n++] = '_';
        for (size_t i = 0; i < subtable.size(); ++i) {
            scratch[n++] = subtable[i];
        }
        scratch[n++] = '_';

        // Decimal index suffix (at least one digit).
        char tmp[kMaxIndexDigits];
        size_t t   = 0;
        uint32_t v = index;
        do {
            tmp[t++] = static_cast<char>('0' + (v % 10U));
            v /= 10U;
        } while (v != 0U && t < kMaxIndexDigits);
        while (t > 0) {
            scratch[n++] = tmp[--t];
        }

        return std::string_view(scratch.data(), n);
    }

    static bool read_u16_endian(bool le, std::span<const std::byte> bytes,
                                uint64_t offset, uint16_t* out) noexcept
    {
        return le ? read_u16le(bytes, offset, out)
                  : read_u16be(bytes, offset, out);
    }


    static bool read_i16_endian(bool le, std::span<const std::byte> bytes,
                                uint64_t offset, int16_t* out) noexcept
    {
        uint16_t raw = 0;
        if (!read_u16_endian(le, bytes, offset, &raw)) {
            return false;
        }
        *out = static_cast<int16_t>(raw);
        return true;
    }


    static MetaValue
    make_fixed_ascii_text(ByteArena& arena,
                          std::span<const std::byte> raw) noexcept
    {
        size_t trimmed = 0;
        while (trimmed < raw.size() && raw[trimmed] != std::byte { 0 }) {
            trimmed += 1;
        }
        const std::span<const std::byte> payload = raw.subspan(0, trimmed);
        const std::string_view text(reinterpret_cast<const char*>(
                                        payload.data()),
                                    payload.size());
        return make_text(arena, text, TextEncoding::Ascii);
    }


    static void emit_bin_dir_entries(std::string_view ifd_name,
                                     MetaStore& store,
                                     std::span<const uint16_t> tags,
                                     std::span<const MetaValue> values,
                                     const ExifDecodeLimits& limits,
                                     ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty() || tags.size() != values.size()) {
            return;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        for (uint32_t i = 0; i < tags.size(); ++i) {
            if (status_out
                && (status_out->entries_decoded + 1U)
                       > limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return;
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tags[i]);
            entry.origin.block          = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
            entry.origin.wire_count     = values[i].count;
            entry.value                 = values[i];
            entry.flags |= EntryFlags::Derived;

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }
    static uint64_t tiff_type_size(uint16_t type) noexcept
    {
        switch (type) {
        case 1:    // BYTE
        case 2:    // ASCII
        case 6:    // SBYTE
        case 7:    // UNDEFINED
        case 129:  // UTF-8 (EXIF)
            return 1;
        case 3:  // SHORT
        case 8:  // SSHORT
            return 2;
        case 4:   // LONG
        case 9:   // SLONG
        case 11:  // FLOAT
        case 13:  // IFD
            return 4;
        case 5:   // RATIONAL
        case 10:  // SRATIONAL
        case 12:  // DOUBLE
            return 8;
        case 16:  // LONG8
        case 17:  // SLONG8
        case 18:  // IFD8
            return 8;
        default: return 0;
        }
    }


    static bool contains_nul(std::span<const std::byte> bytes) noexcept
    {
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] == std::byte { 0 }) {
                return true;
            }
        }
        return false;
    }


    static uint32_t write_u32_decimal(char* out, uint32_t value) noexcept
    {
        char tmp[16];
        uint32_t tmp_len = 0;
        do {
            const uint32_t digit = value % 10U;
            tmp[tmp_len]         = static_cast<char>('0' + digit);
            tmp_len += 1;
            value /= 10U;
        } while (value != 0U && tmp_len < sizeof(tmp));

        for (uint32_t i = 0; i < tmp_len; ++i) {
            out[i] = tmp[tmp_len - 1 - i];
        }
        return tmp_len;
    }


    static std::string_view ifd_token(const ExifIfdTokenPolicy& tokens,
                                      ExifIfdKind kind, uint32_t index,
                                      std::span<char> scratch) noexcept
    {
        switch (kind) {
        case ExifIfdKind::Ifd: {
            const std::string_view prefix = tokens.ifd_prefix;
            if (prefix.empty()) {
                return std::string_view();
            }
            if (scratch.size() < prefix.size() + 16U) {
                return std::string_view();
            }
            std::memcpy(scratch.data(), prefix.data(), prefix.size());
            const uint32_t digits = write_u32_decimal(
                scratch.data() + static_cast<uint32_t>(prefix.size()), index);
            return std::string_view(scratch.data(), prefix.size() + digits);
        }
        case ExifIfdKind::ExifIfd: return tokens.exif_ifd_token;
        case ExifIfdKind::GpsIfd: return tokens.gps_ifd_token;
        case ExifIfdKind::InteropIfd: return tokens.interop_ifd_token;
        case ExifIfdKind::SubIfd: {
            const std::string_view prefix = tokens.subifd_prefix;
            if (prefix.empty()) {
                return std::string_view();
            }
            if (scratch.size() < prefix.size() + 16U) {
                return std::string_view();
            }
            std::memcpy(scratch.data(), prefix.data(), prefix.size());
            const uint32_t digits = write_u32_decimal(
                scratch.data() + static_cast<uint32_t>(prefix.size()), index);
            return std::string_view(scratch.data(), prefix.size() + digits);
        }
        }
        return std::string_view();
    }


    struct IfdTask final {
        ExifIfdKind kind = ExifIfdKind::Ifd;
        uint32_t index   = 0;
        uint64_t offset  = 0;
    };

    struct IfdSink final {
        ExifIfdRef* out = nullptr;
        uint32_t cap    = 0;
        ExifDecodeResult result;
    };

    static void sink_emit(IfdSink* sink, const ExifIfdRef& ref) noexcept
    {
        sink->result.ifds_needed += 1;
        if (sink->result.ifds_written < sink->cap) {
            sink->out[sink->result.ifds_written] = ref;
            sink->result.ifds_written += 1;
        } else if (sink->result.status == ExifDecodeStatus::Ok) {
            sink->result.status = ExifDecodeStatus::OutputTruncated;
        }
    }


    static uint8_t ifd_kind_bit(ExifIfdKind kind) noexcept
    {
        switch (kind) {
        case ExifIfdKind::Ifd: return 1U << 0U;
        case ExifIfdKind::ExifIfd: return 1U << 1U;
        case ExifIfdKind::GpsIfd: return 1U << 2U;
        case ExifIfdKind::InteropIfd: return 1U << 3U;
        case ExifIfdKind::SubIfd: return 1U << 4U;
        default: break;
        }
        return 0;
    }


    static uint32_t find_visited(uint64_t off,
                                 std::span<const uint64_t> visited_offs,
                                 uint32_t visited_count) noexcept
    {
        for (uint32_t i = 0; i < visited_count; ++i) {
            if (visited_offs[i] == off) {
                return i;
            }
        }
        return 0xffffffffU;
    }


    static bool allow_revisit_kind(ExifIfdKind kind,
                                   uint8_t existing_mask) noexcept
    {
        // In some malformed files, GPSInfoIFDPointer references the same IFD as
        // InteropIFDPointer. ExifTool reports both groups. Preserve that
        // behavior by allowing a second decode pass for the GPS/Interop pair.
        const uint8_t gps    = ifd_kind_bit(ExifIfdKind::GpsIfd);
        const uint8_t intero = ifd_kind_bit(ExifIfdKind::InteropIfd);

        if (kind == ExifIfdKind::GpsIfd) {
            return existing_mask == intero;
        }
        if (kind == ExifIfdKind::InteropIfd) {
            return existing_mask == gps;
        }
        return false;
    }


    static uint8_t ifd_priority(ExifIfdKind kind) noexcept
    {
        // Prefer structured sub-directories over generic IFD chain when offsets
        // collide in malformed files (observed in the ExifTool sample corpus).
        switch (kind) {
        case ExifIfdKind::ExifIfd: return 5;
        case ExifIfdKind::InteropIfd: return 4;
        case ExifIfdKind::GpsIfd: return 3;
        case ExifIfdKind::SubIfd: return 2;
        case ExifIfdKind::Ifd: return 1;
        default: break;
        }
        return 0;
    }


    static uint32_t select_next_task_index(std::span<const IfdTask> tasks,
                                           uint32_t task_count) noexcept
    {
        uint32_t best_index   = 0;
        uint8_t best_priority = 0;
        uint64_t best_off     = 0;

        for (uint32_t i = 0; i < task_count; ++i) {
            const uint8_t prio = ifd_priority(tasks[i].kind);
            const uint64_t off = tasks[i].offset;
            if (i == 0 || prio > best_priority
                || (prio == best_priority && off < best_off)) {
                best_index    = i;
                best_priority = prio;
                best_off      = off;
            }
        }
        return best_index;
    }


    static void update_status(ExifDecodeResult* out,
                              ExifDecodeStatus status) noexcept
    {
        if (out->status == ExifDecodeStatus::LimitExceeded) {
            return;
        }
        if (status == ExifDecodeStatus::LimitExceeded) {
            out->status = status;
            return;
        }
        if (out->status == ExifDecodeStatus::Malformed) {
            return;
        }
        if (status == ExifDecodeStatus::Malformed) {
            out->status = status;
            return;
        }
        if (out->status == ExifDecodeStatus::Unsupported) {
            return;
        }
        if (status == ExifDecodeStatus::Unsupported) {
            out->status = status;
            return;
        }
        if (out->status == ExifDecodeStatus::OutputTruncated) {
            return;
        }
        if (status == ExifDecodeStatus::OutputTruncated) {
            out->status = status;
            return;
        }
    }


    static bool decode_u32_or_u64_offset(const TiffConfig& cfg,
                                         std::span<const std::byte> bytes,
                                         uint16_t type, uint64_t value_off,
                                         uint64_t count,
                                         std::span<uint64_t> out_ptrs,
                                         uint32_t* out_count) noexcept
    {
        *out_count = 0;

        const uint64_t unit = tiff_type_size(type);
        if (unit == 0) {
            return false;
        }
        if (count > (UINT64_MAX / unit)) {
            return false;
        }
        const uint64_t total_bytes = count * unit;
        if (value_off + total_bytes > bytes.size()) {
            return false;
        }

        const uint32_t cap = static_cast<uint32_t>(out_ptrs.size());
        const uint32_t n   = (count < cap) ? static_cast<uint32_t>(count) : cap;
        for (uint32_t i = 0; i < n; ++i) {
            uint64_t ptr = 0;
            if (unit == 4) {
                uint32_t v = 0;
                if (!read_tiff_u32(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 4U,
                                   &v)) {
                    break;
                }
                ptr = v;
            } else if (unit == 8) {
                if (!read_tiff_u64(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 8U,
                                   &ptr)) {
                    break;
                }
            } else {
                break;
            }
            out_ptrs[i] = ptr;
            *out_count += 1;
        }
        return true;
    }


    static MetaValue decode_text_value(ByteArena& arena,
                                       std::span<const std::byte> raw,
                                       TextEncoding enc) noexcept
    {
        if (raw.empty()) {
            return make_text(arena, std::string_view(), enc);
        }

        size_t trimmed = raw.size();
        if (raw[trimmed - 1] == std::byte { 0 }) {
            trimmed -= 1;
        }
        const std::span<const std::byte> payload = raw.subspan(0, trimmed);
        if (contains_nul(payload)) {
            return make_bytes(arena, raw);
        }

        const std::string_view text(reinterpret_cast<const char*>(
                                        payload.data()),
                                    payload.size());
        return make_text(arena, text, enc);
    }


    static MetaValue decode_tiff_value(const TiffConfig& cfg,
                                       std::span<const std::byte> bytes,
                                       uint16_t type, uint64_t count,
                                       uint64_t value_off, uint64_t value_bytes,
                                       ByteArena& arena,
                                       const ExifDecodeLimits& limits,
                                       ExifDecodeResult* result) noexcept
    {
        if (value_bytes > limits.max_value_bytes) {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return MetaValue {};
        }

        switch (type) {
        case 1: {  // BYTE
            if (count == 1) {
                return make_u8(u8(bytes[value_off]));
            }
            const uint32_t n = (count > UINT32_MAX)
                                   ? UINT32_MAX
                                   : static_cast<uint32_t>(count);
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::U8;
            v.count     = n;
            v.data.span = arena.append(
                bytes.subspan(value_off, static_cast<size_t>(value_bytes)));
            return v;
        }
        case 2: {  // ASCII
            return decode_text_value(
                arena,
                bytes.subspan(value_off, static_cast<size_t>(value_bytes)),
                TextEncoding::Ascii);
        }
        case 3: {  // SHORT
            if (count == 1) {
                uint16_t v = 0;
                if (!read_tiff_u16(cfg, bytes, value_off, &v)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_u16(v);
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::U16;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 2U),
                                         alignof(uint16_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint16_t value = 0;
                if (!read_tiff_u16(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 2U,
                                   &value)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 2U, &value, 2U);
            }
            return v;
        }
        case 4:     // LONG
        case 13: {  // IFD
            if (count == 1) {
                uint32_t v = 0;
                if (!read_tiff_u32(cfg, bytes, value_off, &v)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_u32(v);
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::U32;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 4U),
                                         alignof(uint32_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t value = 0;
                if (!read_tiff_u32(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 4U,
                                   &value)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 4U, &value, 4U);
            }
            return v;
        }
        case 5: {  // RATIONAL
            if (count == 1) {
                uint32_t numer = 0;
                uint32_t denom = 0;
                if (!read_tiff_u32(cfg, bytes, value_off + 0, &numer)
                    || !read_tiff_u32(cfg, bytes, value_off + 4, &denom)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_urational(numer, denom);
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::URational;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(
                                             count * sizeof(URational)),
                                         alignof(URational));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t numer      = 0;
                uint32_t denom      = 0;
                const uint64_t base = value_off + static_cast<uint64_t>(i) * 8U;
                if (!read_tiff_u32(cfg, bytes, base + 0, &numer)
                    || !read_tiff_u32(cfg, bytes, base + 4, &denom)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const URational r { numer, denom };
                std::memcpy(dst.data() + i * sizeof(URational), &r,
                            sizeof(URational));
            }
            return v;
        }
        case 6: {  // SBYTE
            if (count == 1) {
                return make_i8(static_cast<int8_t>(u8(bytes[value_off])));
            }
            const uint32_t n = (count > UINT32_MAX)
                                   ? UINT32_MAX
                                   : static_cast<uint32_t>(count);
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::I8;
            v.count     = n;
            v.data.span = arena.append(
                bytes.subspan(value_off, static_cast<size_t>(value_bytes)));
            return v;
        }
        case 7: {  // UNDEFINED
            return make_bytes(arena,
                              bytes.subspan(value_off,
                                            static_cast<size_t>(value_bytes)));
        }
        case 8: {  // SSHORT
            if (count == 1) {
                uint16_t raw = 0;
                if (!read_tiff_u16(cfg, bytes, value_off, &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_i16(static_cast<int16_t>(raw));
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::I16;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 2U),
                                         alignof(int16_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint16_t raw = 0;
                if (!read_tiff_u16(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 2U,
                                   &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const int16_t value = static_cast<int16_t>(raw);
                std::memcpy(dst.data() + i * 2U, &value, 2U);
            }
            return v;
        }
        case 9: {  // SLONG
            if (count == 1) {
                uint32_t raw = 0;
                if (!read_tiff_u32(cfg, bytes, value_off, &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_i32(static_cast<int32_t>(raw));
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::I32;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 4U),
                                         alignof(int32_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t raw = 0;
                if (!read_tiff_u32(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 4U,
                                   &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const int32_t value = static_cast<int32_t>(raw);
                std::memcpy(dst.data() + i * 4U, &value, 4U);
            }
            return v;
        }
        case 10: {  // SRATIONAL
            if (count == 1) {
                uint32_t numer_u = 0;
                uint32_t denom_u = 0;
                if (!read_tiff_u32(cfg, bytes, value_off + 0, &numer_u)
                    || !read_tiff_u32(cfg, bytes, value_off + 4, &denom_u)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_srational(static_cast<int32_t>(numer_u),
                                      static_cast<int32_t>(denom_u));
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::SRational;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(
                                             count * sizeof(SRational)),
                                         alignof(SRational));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t numer_u    = 0;
                uint32_t denom_u    = 0;
                const uint64_t base = value_off + static_cast<uint64_t>(i) * 8U;
                if (!read_tiff_u32(cfg, bytes, base + 0, &numer_u)
                    || !read_tiff_u32(cfg, bytes, base + 4, &denom_u)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const SRational r { static_cast<int32_t>(numer_u),
                                    static_cast<int32_t>(denom_u) };
                std::memcpy(dst.data() + i * sizeof(SRational), &r,
                            sizeof(SRational));
            }
            return v;
        }
        case 11: {  // FLOAT
            if (count == 1) {
                uint32_t bits = 0;
                if (!read_tiff_u32(cfg, bytes, value_off, &bits)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_f32_bits(bits);
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::F32;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 4U),
                                         alignof(uint32_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint32_t bits = 0;
                if (!read_tiff_u32(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 4U,
                                   &bits)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 4U, &bits, 4U);
            }
            return v;
        }
        case 12: {  // DOUBLE
            if (count == 1) {
                uint64_t bits = 0;
                if (!read_tiff_u64(cfg, bytes, value_off, &bits)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_f64_bits(bits);
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::F64;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 8U),
                                         alignof(uint64_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint64_t bits = 0;
                if (!read_tiff_u64(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 8U,
                                   &bits)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 8U, &bits, 8U);
            }
            return v;
        }
        case 16:    // LONG8
        case 18: {  // IFD8
            if (count == 1) {
                uint64_t v = 0;
                if (!read_tiff_u64(cfg, bytes, value_off, &v)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_u64(v);
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::U64;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 8U),
                                         alignof(uint64_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint64_t value = 0;
                if (!read_tiff_u64(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 8U,
                                   &value)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                std::memcpy(dst.data() + i * 8U, &value, 8U);
            }
            return v;
        }
        case 17: {  // SLONG8
            if (count == 1) {
                uint64_t raw = 0;
                if (!read_tiff_u64(cfg, bytes, value_off, &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    return MetaValue {};
                }
                return make_i64(static_cast<int64_t>(raw));
            }
            if (count > UINT32_MAX) {
                update_status(result, ExifDecodeStatus::LimitExceeded);
                return MetaValue {};
            }
            MetaValue v;
            v.kind      = MetaValueKind::Array;
            v.elem_type = MetaElementType::I64;
            v.count     = static_cast<uint32_t>(count);
            v.data.span = arena.allocate(static_cast<uint32_t>(count * 8U),
                                         alignof(int64_t));
            const std::span<std::byte> dst = arena.span_mut(v.data.span);
            for (uint32_t i = 0; i < v.count; ++i) {
                uint64_t raw = 0;
                if (!read_tiff_u64(cfg, bytes,
                                   value_off + static_cast<uint64_t>(i) * 8U,
                                   &raw)) {
                    update_status(result, ExifDecodeStatus::Malformed);
                    break;
                }
                const int64_t value = static_cast<int64_t>(raw);
                std::memcpy(dst.data() + i * 8U, &value, 8U);
            }
            return v;
        }
        case 129: {  // UTF-8 (EXIF)
            return decode_text_value(
                arena,
                bytes.subspan(value_off, static_cast<size_t>(value_bytes)),
                TextEncoding::Utf8);
        }
        default: break;
        }

        return MetaValue {};
    }


    static bool
    follow_ifd_pointers(const TiffConfig& cfg, std::span<const std::byte> bytes,
                        uint16_t tag, uint16_t type, uint64_t count,
                        uint64_t value_off, std::span<IfdTask> stack,
                        uint32_t* stack_size, uint32_t* next_subifd_index,
                        const ExifDecodeLimits& limits,
                        ExifDecodeResult* result) noexcept
    {
        if (tag != 0x8769 && tag != 0x8825 && tag != 0xA005 && tag != 0x014A) {
            return true;
        }

        if (*stack_size >= limits.max_ifds) {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return false;
        }

        std::array<uint64_t, 32> ptrs {};
        uint32_t ptr_count = 0;
        if (!decode_u32_or_u64_offset(cfg, bytes, type, value_off, count,
                                      std::span<uint64_t>(ptrs), &ptr_count)) {
            return true;
        }

        if (tag == 0x014A) {  // SubIFDs: may be an array of offsets.
            for (uint32_t i = 0; i < ptr_count; ++i) {
                if (*stack_size >= stack.size()
                    || *stack_size >= limits.max_ifds) {
                    update_status(result, ExifDecodeStatus::LimitExceeded);
                    return false;
                }
                IfdTask t;
                t.kind  = ExifIfdKind::SubIfd;
                t.index = *next_subifd_index;
                *next_subifd_index += 1;
                t.offset           = ptrs[i];
                stack[*stack_size] = t;
                *stack_size += 1;
            }
            return true;
        }

        if (ptr_count == 0) {
            return true;
        }

        IfdTask t;
        t.offset = ptrs[0];
        t.index  = 0;
        if (tag == 0x8769) {
            t.kind = ExifIfdKind::ExifIfd;
        } else if (tag == 0x8825) {
            t.kind = ExifIfdKind::GpsIfd;
        } else if (tag == 0xA005) {
            t.kind = ExifIfdKind::InteropIfd;
        } else {
            return true;
        }

        if (*stack_size < stack.size() && *stack_size < limits.max_ifds) {
            stack[*stack_size] = t;
            *stack_size += 1;
        } else {
            update_status(result, ExifDecodeStatus::LimitExceeded);
            return false;
        }

        return true;
    }

}  // namespace

namespace exif_internal {

bool match_bytes(std::span<const std::byte> bytes, uint64_t offset,
                 const char* magic, uint32_t magic_len) noexcept
{
    return ::openmeta::match_bytes(bytes, offset, magic, magic_len);
}

bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                uint16_t* out) noexcept
{
    return ::openmeta::read_u16be(bytes, offset, out);
}

bool read_u16le(std::span<const std::byte> bytes, uint64_t offset,
                uint16_t* out) noexcept
{
    return ::openmeta::read_u16le(bytes, offset, out);
}

bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                uint32_t* out) noexcept
{
    return ::openmeta::read_u32be(bytes, offset, out);
}

bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                uint32_t* out) noexcept
{
    return ::openmeta::read_u32le(bytes, offset, out);
}

bool read_tiff_u16(const TiffConfig& cfg, std::span<const std::byte> bytes,
                   uint64_t offset, uint16_t* out) noexcept
{
    return ::openmeta::read_tiff_u16(cfg, bytes, offset, out);
}

bool read_tiff_u32(const TiffConfig& cfg, std::span<const std::byte> bytes,
                   uint64_t offset, uint32_t* out) noexcept
{
    return ::openmeta::read_tiff_u32(cfg, bytes, offset, out);
}

bool read_u16_endian(bool le, std::span<const std::byte> bytes, uint64_t offset,
                     uint16_t* out) noexcept
{
    return ::openmeta::read_u16_endian(le, bytes, offset, out);
}

bool read_i16_endian(bool le, std::span<const std::byte> bytes, uint64_t offset,
                     int16_t* out) noexcept
{
    return ::openmeta::read_i16_endian(le, bytes, offset, out);
}

std::string_view make_mk_subtable_ifd_token(std::string_view vendor_prefix,
                                            std::string_view subtable,
                                            uint32_t index,
                                            std::span<char> scratch) noexcept
{
    return ::openmeta::make_mk_subtable_ifd_token(vendor_prefix, subtable,
                                                  index, scratch);
}

MetaValue make_fixed_ascii_text(ByteArena& arena,
                                std::span<const std::byte> raw) noexcept
{
    return ::openmeta::make_fixed_ascii_text(arena, raw);
}

void emit_bin_dir_entries(std::string_view ifd_name, MetaStore& store,
                          std::span<const uint16_t> tags,
                          std::span<const MetaValue> values,
                          const ExifDecodeLimits& limits,
                          ExifDecodeResult* status_out) noexcept
{
    ::openmeta::emit_bin_dir_entries(ifd_name, store, tags, values, limits,
                                     status_out);
}

uint64_t tiff_type_size(uint16_t type) noexcept
{
    return ::openmeta::tiff_type_size(type);
}

void update_status(ExifDecodeResult* out, ExifDecodeStatus status) noexcept
{
    ::openmeta::update_status(out, status);
}

MetaValue decode_tiff_value(const TiffConfig& cfg,
                            std::span<const std::byte> bytes, uint16_t type,
                            uint64_t count, uint64_t value_off,
                            uint64_t value_bytes, ByteArena& arena,
                            const ExifDecodeLimits& limits,
                            ExifDecodeResult* result) noexcept
{
    return ::openmeta::decode_tiff_value(cfg, bytes, type, count, value_off,
                                         value_bytes, arena, limits, result);
}

bool score_classic_ifd_candidate(const TiffConfig& cfg,
                                 std::span<const std::byte> bytes,
                                 uint64_t ifd_off,
                                 const ExifDecodeLimits& limits,
                                 ClassicIfdCandidate* out) noexcept
{
    return ::openmeta::score_classic_ifd_candidate(cfg, bytes, ifd_off, limits,
                                                   out);
}

bool find_best_classic_ifd_candidate(std::span<const std::byte> bytes,
                                     uint64_t scan_bytes,
                                     const ExifDecodeLimits& limits,
                                     ClassicIfdCandidate* out) noexcept
{
    return ::openmeta::find_best_classic_ifd_candidate(bytes, scan_bytes,
                                                       limits, out);
}

bool looks_like_classic_ifd(const TiffConfig& cfg,
                            std::span<const std::byte> bytes, uint64_t ifd_off,
                            const ExifDecodeLimits& limits) noexcept
{
    return ::openmeta::looks_like_classic_ifd(cfg, bytes, ifd_off, limits);
}

void decode_classic_ifd_no_header(const TiffConfig& cfg,
                                  std::span<const std::byte> bytes,
                                  uint64_t ifd_off, std::string_view ifd_name,
                                  MetaStore& store,
                                  const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out,
                                  EntryFlags extra_flags) noexcept
{
    ::openmeta::decode_classic_ifd_no_header(cfg, bytes, ifd_off, ifd_name,
                                             store, options, status_out,
                                             extra_flags);
}

}  // namespace exif_internal

ExifDecodeResult
decode_exif_tiff(std::span<const std::byte> tiff_bytes, MetaStore& store,
                 std::span<ExifIfdRef> out_ifds,
                 const ExifDecodeOptions& options) noexcept
{
    IfdSink sink;
    sink.out = out_ifds.data();
    sink.cap = static_cast<uint32_t>(out_ifds.size());

    if (tiff_bytes.size() < 8) {
        sink.result.status = ExifDecodeStatus::Malformed;
        return sink.result;
    }

    TiffConfig cfg;
    const uint8_t b0 = u8(tiff_bytes[0]);
    const uint8_t b1 = u8(tiff_bytes[1]);
    if (b0 == 0x49 && b1 == 0x49) {
        cfg.le = true;
    } else if (b0 == 0x4D && b1 == 0x4D) {
        cfg.le = false;
    } else {
        sink.result.status = ExifDecodeStatus::Unsupported;
        return sink.result;
    }

    uint16_t version = 0;
    if (!read_tiff_u16(cfg, tiff_bytes, 2, &version)) {
        sink.result.status = ExifDecodeStatus::Malformed;
        return sink.result;
    }
    if (version == 42) {
        cfg.bigtiff = false;
    } else if (version == 43) {
        cfg.bigtiff = true;
    } else {
        sink.result.status = ExifDecodeStatus::Unsupported;
        return sink.result;
    }

    uint64_t first_ifd = 0;
    if (!cfg.bigtiff) {
        uint32_t off32 = 0;
        if (!read_tiff_u32(cfg, tiff_bytes, 4, &off32)) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
        first_ifd = off32;
    } else {
        if (tiff_bytes.size() < 16) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
        uint16_t off_size = 0;
        uint16_t reserved = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, 4, &off_size)
            || !read_tiff_u16(cfg, tiff_bytes, 6, &reserved)) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
        if (off_size != 8 || reserved != 0) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
        if (!read_tiff_u64(cfg, tiff_bytes, 8, &first_ifd)) {
            sink.result.status = ExifDecodeStatus::Malformed;
            return sink.result;
        }
    }

    std::array<IfdTask, 256> stack_buf {};
    std::array<uint64_t, 256> visited_offs {};
    std::array<uint8_t, 256> visited_masks {};
    uint32_t stack_size        = 0;
    uint32_t visited_count     = 0;
    uint32_t next_subifd_index = 0;

    if (first_ifd != 0) {
        stack_buf[0] = IfdTask { ExifIfdKind::Ifd, 0, first_ifd };
        stack_size   = 1;
    }

    while (stack_size > 0) {
        const uint32_t next_index
            = select_next_task_index(std::span<const IfdTask>(stack_buf),
                                     stack_size);
        IfdTask task          = stack_buf[next_index];
        stack_buf[next_index] = stack_buf[stack_size - 1];
        stack_size -= 1;

        if (task.offset == 0 || task.offset >= tiff_bytes.size()) {
            continue;
        }

        const uint8_t kind_bit = ifd_kind_bit(task.kind);
        const uint32_t vi
            = find_visited(task.offset, std::span<const uint64_t>(visited_offs),
                           visited_count);
        if (vi != 0xffffffffU) {
            const uint8_t mask = visited_masks[vi];
            if ((mask & kind_bit) != 0) {
                continue;
            }
            if (!allow_revisit_kind(task.kind, mask)) {
                continue;
            }
            visited_masks[vi] = static_cast<uint8_t>(mask | kind_bit);
        } else {
            if (visited_count < visited_offs.size()) {
                visited_offs[visited_count]  = task.offset;
                visited_masks[visited_count] = kind_bit;
                visited_count += 1;
            } else {
                update_status(&sink.result, ExifDecodeStatus::LimitExceeded);
                break;
            }
        }

        if (sink.result.ifds_needed >= options.limits.max_ifds) {
            update_status(&sink.result, ExifDecodeStatus::LimitExceeded);
            break;
        }

        uint64_t entry_count      = 0;
        uint64_t entries_off      = 0;
        uint64_t entry_size       = 0;
        uint64_t next_ifd_off_pos = 0;

        if (!cfg.bigtiff) {
            uint16_t n16 = 0;
            if (!read_tiff_u16(cfg, tiff_bytes, task.offset, &n16)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            entry_count      = n16;
            entries_off      = task.offset + 2;
            entry_size       = 12;
            next_ifd_off_pos = entries_off + entry_count * entry_size;
            if (task.kind == ExifIfdKind::Ifd) {
                if (next_ifd_off_pos + 4 <= tiff_bytes.size()) {
                    uint32_t next32 = 0;
                    if (read_tiff_u32(cfg, tiff_bytes, next_ifd_off_pos, &next32)
                        && next32 != 0) {
                        if (stack_size < stack_buf.size()
                            && stack_size < options.limits.max_ifds) {
                            stack_buf[stack_size] = IfdTask { ExifIfdKind::Ifd,
                                                              task.index + 1,
                                                              next32 };
                            stack_size += 1;
                        } else {
                            update_status(&sink.result,
                                          ExifDecodeStatus::LimitExceeded);
                        }
                    }
                } else {
                    // Truncated next-IFD pointer field. Decode entries anyway.
                    update_status(&sink.result, ExifDecodeStatus::Malformed);
                }
            }
        } else {
            uint64_t n64 = 0;
            if (!read_tiff_u64(cfg, tiff_bytes, task.offset, &n64)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            entry_count      = n64;
            entries_off      = task.offset + 8;
            entry_size       = 20;
            next_ifd_off_pos = entries_off + entry_count * entry_size;
            if (task.kind == ExifIfdKind::Ifd) {
                if (next_ifd_off_pos + 8 <= tiff_bytes.size()) {
                    uint64_t next64 = 0;
                    if (read_tiff_u64(cfg, tiff_bytes, next_ifd_off_pos, &next64)
                        && next64 != 0) {
                        if (stack_size < stack_buf.size()
                            && stack_size < options.limits.max_ifds) {
                            stack_buf[stack_size] = IfdTask { ExifIfdKind::Ifd,
                                                              task.index + 1,
                                                              next64 };
                            stack_size += 1;
                        } else {
                            update_status(&sink.result,
                                          ExifDecodeStatus::LimitExceeded);
                        }
                    }
                } else {
                    // Truncated next-IFD pointer field. Decode entries anyway.
                    update_status(&sink.result, ExifDecodeStatus::Malformed);
                }
            }
        }

        if (entry_count > options.limits.max_entries_per_ifd) {
            update_status(&sink.result, ExifDecodeStatus::LimitExceeded);
            continue;
        }
        if (entries_off + entry_count * entry_size > tiff_bytes.size()) {
            update_status(&sink.result, ExifDecodeStatus::Malformed);
            continue;
        }
        if (sink.result.entries_decoded + entry_count
            > options.limits.max_total_entries) {
            update_status(&sink.result, ExifDecodeStatus::LimitExceeded);
            continue;
        }

        BlockId block = store.add_block(BlockInfo {});
        ExifIfdRef ref;
        ref.kind   = task.kind;
        ref.index  = task.index;
        ref.offset = task.offset;
        ref.block  = block;
        sink_emit(&sink, ref);

        char token_scratch_buf[64];
        const std::string_view ifd_name
            = ifd_token(options.tokens, task.kind, task.index,
                        std::span<char>(token_scratch_buf));
        if (ifd_name.empty()) {
            update_status(&sink.result, ExifDecodeStatus::Malformed);
            continue;
        }

        for (uint64_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + i * entry_size;

            uint16_t tag  = 0;
            uint16_t type = 0;
            if (!read_tiff_u16(cfg, tiff_bytes, eoff + 0, &tag)
                || !read_tiff_u16(cfg, tiff_bytes, eoff + 2, &type)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }

            uint64_t count           = 0;
            uint64_t value_or_off    = 0;
            uint64_t value_field_off = 0;
            if (!cfg.bigtiff) {
                uint32_t c32 = 0;
                uint32_t v32 = 0;
                if (!read_tiff_u32(cfg, tiff_bytes, eoff + 4, &c32)
                    || !read_tiff_u32(cfg, tiff_bytes, eoff + 8, &v32)) {
                    update_status(&sink.result, ExifDecodeStatus::Malformed);
                    continue;
                }
                count           = c32;
                value_or_off    = v32;
                value_field_off = eoff + 8;
            } else {
                uint64_t c64 = 0;
                uint64_t v64 = 0;
                if (!read_tiff_u64(cfg, tiff_bytes, eoff + 4, &c64)
                    || !read_tiff_u64(cfg, tiff_bytes, eoff + 12, &v64)) {
                    update_status(&sink.result, ExifDecodeStatus::Malformed);
                    continue;
                }
                count           = c64;
                value_or_off    = v64;
                value_field_off = eoff + 12;
            }

            const uint64_t unit = tiff_type_size(type);
            if (unit == 0) {
                continue;
            }
            if (count > (UINT64_MAX / unit)) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }
            const uint64_t value_bytes = count * unit;

            uint64_t value_off        = 0;
            const uint64_t inline_cap = cfg.bigtiff ? 8U : 4U;
            if (value_bytes <= inline_cap) {
                value_off = value_field_off;
            } else {
                value_off = value_or_off;
            }
            if (value_off + value_bytes > tiff_bytes.size()) {
                update_status(&sink.result, ExifDecodeStatus::Malformed);
                continue;
            }

            (void)follow_ifd_pointers(cfg, tiff_bytes, tag, type, count,
                                      value_off, std::span<IfdTask>(stack_buf),
                                      &stack_size, &next_subifd_index,
                                      options.limits, &sink.result);

            if (count > UINT32_MAX) {
                update_status(&sink.result, ExifDecodeStatus::LimitExceeded);
                continue;
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = static_cast<uint32_t>(i);
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
            entry.origin.wire_count     = static_cast<uint32_t>(count);
            entry.value = decode_tiff_value(cfg, tiff_bytes, type, count,
                                            value_off, value_bytes,
                                            store.arena(), options.limits,
                                            &sink.result);

            if (!options.include_pointer_tags
                && (tag == 0x8769 || tag == 0x8825 || tag == 0xA005
                    || tag == 0x014A)) {
                continue;
            }

            (void)store.add_entry(entry);
            sink.result.entries_decoded += 1;

            // PrintIM (0xC4A5) is an embedded binary block that ExifTool
            // exposes as a separate "PrintIM" group. Decode it into
            // MetaKeyKind::PrintImField entries as a best-effort parse.
            if (options.decode_printim && tag == 0xC4A5 && value_bytes != 0U
                && value_bytes <= options.limits.max_value_bytes) {
                PrintImDecodeLimits plim;
                plim.max_entries = options.limits.max_entries_per_ifd;
                plim.max_bytes   = options.limits.max_value_bytes;
                (void)decode_printim(
                    tiff_bytes.subspan(static_cast<size_t>(value_off),
                                       static_cast<size_t>(value_bytes)),
                    store, plim);
            }

            // MakerNote (0x927C) is vendor-defined. As a minimal starting point,
            // attempt to decode embedded TIFF headers found inside the blob
            // (covers common cases like Nikon).
            if (options.decode_makernote && tag == 0x927C && value_bytes != 0U
                && value_bytes <= options.limits.max_value_bytes) {
                const std::span<const std::byte> mn
                    = tiff_bytes.subspan(static_cast<size_t>(value_off),
                                         static_cast<size_t>(value_bytes));
                const MakerNoteVendor vendor = detect_makernote_vendor(mn,
                                                                       store);

                ExifDecodeOptions mn_opts = options;
                mn_opts.decode_printim    = false;
                mn_opts.decode_makernote  = false;
                set_makernote_tokens(&mn_opts, vendor);

                char token_scratch_buf2[64];
                const std::string_view mk_ifd0
                    = ifd_token(mn_opts.tokens, ExifIfdKind::Ifd, 0,
                                std::span<char>(token_scratch_buf2));

                // Olympus MakerNote: classic IFD at +8, offsets relative to the
                // outer EXIF TIFF header.
                if (vendor == MakerNoteVendor::Olympus
                    && exif_internal::decode_olympus_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // Pentax MakerNote: "AOC\0" header + endianness marker +
                // u16 entry count at +6, then classic IFD entries at +8.
                if (vendor == MakerNoteVendor::Pentax
                    && exif_internal::decode_pentax_makernote(
                        mn, mk_ifd0, store, mn_opts, &sink.result)) {
                    continue;
                }

                // Casio MakerNote type2: "QVC\0" header + big-endian entries.
                if (vendor == MakerNoteVendor::Casio
                    && exif_internal::decode_casio_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // Panasonic MakerNote: classic IFD located within the blob, but
                // value offsets are commonly relative to the outer EXIF/TIFF.
                if (vendor == MakerNoteVendor::Panasonic
                    && exif_internal::decode_panasonic_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // Canon MakerNote: classic IFD at offset 0 (parent endianness),
                // plus Canon-specific BinaryData subdirectories.
                if (vendor == MakerNoteVendor::Canon
                    && exif_internal::decode_canon_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // Sony MakerNote: classic IFD located within the blob, but
                // value offsets are commonly relative to the outer EXIF/TIFF.
                if (vendor == MakerNoteVendor::Sony
                    && exif_internal::decode_sony_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    exif_internal::decode_sony_cipher_subdirs(
                        mk_ifd0, store, mn_opts, &sink.result);
                    continue;
                }

                // Kodak MakerNote: supports both KDK fixed-layout blobs and
                // embedded TIFF headers with vendor sub-IFDs.
                if (vendor == MakerNoteVendor::Kodak
                    && exif_internal::decode_kodak_makernote(
                        cfg, tiff_bytes, value_off, value_bytes, mk_ifd0, store,
                        mn_opts, &sink.result)) {
                    continue;
                }

                // 1) Embedded TIFF header inside MakerNote (common for Nikon).
                const uint64_t hdr_off = find_embedded_tiff_header(mn, 128);
                if (hdr_off != UINT64_MAX) {
                    if (value_off > (UINT64_MAX - hdr_off)) {
                        continue;
                    }
                    const uint64_t hdr_abs = value_off + hdr_off;
                    if (hdr_abs >= tiff_bytes.size()) {
                        continue;
                    }

                    // Some real-world MakerNotes store out-of-line values
                    // beyond the declared MakerNote byte count. Decode the
                    // embedded TIFF header using the full EXIF/TIFF buffer so
                    // these values can be resolved safely (bounds-checked by
                    // the decoder limits and input span size).
                    const std::span<const std::byte> hdr_bytes
                        = tiff_bytes.subspan(static_cast<size_t>(hdr_abs));

                    std::array<ExifIfdRef, 128> mn_ifds;
                    (void)decode_exif_tiff(
                        hdr_bytes, store,
                        std::span<ExifIfdRef>(mn_ifds.data(), mn_ifds.size()),
                        mn_opts);

                    if (vendor == MakerNoteVendor::Nikon
                        && (hdr_abs + 2U) <= tiff_bytes.size()) {
                        const uint8_t hdr_b0 = u8(tiff_bytes[hdr_abs + 0]);
                        const uint8_t hdr_b1 = u8(tiff_bytes[hdr_abs + 1]);
                        const bool le        = (hdr_b0 == 'I' && hdr_b1 == 'I');
                        exif_internal::decode_nikon_binary_subdirs(
                            mk_ifd0, store, le, mn_opts, &sink.result);
                    }
                    if (vendor == MakerNoteVendor::Pentax
                        && (hdr_abs + 2U) <= tiff_bytes.size()) {
                        const uint8_t hdr_b0 = u8(tiff_bytes[hdr_abs + 0]);
                        const uint8_t hdr_b1 = u8(tiff_bytes[hdr_abs + 1]);
                        bool le              = cfg.le;
                        if (hdr_b0 == 'I' && hdr_b1 == 'I') {
                            le = true;
                        } else if (hdr_b0 == 'M' && hdr_b1 == 'M') {
                            le = false;
                        }
                        exif_internal::decode_pentax_binary_subdirs(
                            mk_ifd0, store, le, mn_opts, &sink.result);
                    }
                    continue;
                }

                // Nikon MakerNote (older/compact cameras): classic IFD at the
                // MakerNote start, but value offsets are commonly relative to
                // the outer EXIF/TIFF header (not the MakerNote start).
                if (vendor == MakerNoteVendor::Nikon) {
                    // Nikon type1 MakerNote: "Nikon\0" + u16 version (usually 1),
                    // then a classic IFD starting at offset 8 within the
                    // MakerNote payload.
                    //
                    // The IFD value offsets are commonly TIFF-relative, so
                    // decode against the outer TIFF buffer.
                    if (mn.size() >= 10 && match_bytes(mn, 0, "Nikon\0", 6)) {
                        uint16_t ver = 0;
                        if (read_u16le(mn, 6, &ver) && ver == 1) {
                            const uint64_t ifd_off = value_off + 8ULL;
                            if (ifd_off < tiff_bytes.size()) {
                                TiffConfig mn_cfg = cfg;
                                if (!looks_like_classic_ifd(mn_cfg, tiff_bytes,
                                                            ifd_off,
                                                            options.limits)) {
                                    mn_cfg.le = !mn_cfg.le;
                                }
                                decode_classic_ifd_no_header(
                                    mn_cfg, tiff_bytes, ifd_off, mk_ifd0,
                                    store, mn_opts, &sink.result,
                                    EntryFlags::None);
                                exif_internal::decode_nikon_binary_subdirs(
                                    mk_ifd0, store, mn_cfg.le, mn_opts,
                                    &sink.result);
                                continue;
                            }
                        }
                    }

                    decode_classic_ifd_no_header(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, store, mn_opts,
                                                 &sink.result,
                                                 EntryFlags::None);
                    exif_internal::decode_nikon_binary_subdirs(
                        mk_ifd0, store, cfg.le, mn_opts, &sink.result);
                    continue;
                }

                // 2) FUJIFILM MakerNote: "FUJIFILM" + u32le IFD offset.
                if (vendor == MakerNoteVendor::Fuji && mn.size() >= 12
                    && match_bytes(mn, 0, "FUJIFILM", 8)) {
                    uint32_t ifd_off32 = 0;
                    if (read_u32le(mn, 8, &ifd_off32)) {
                        const uint64_t ifd_off = ifd_off32;
                        if (ifd_off < mn.size()) {
                            TiffConfig fuji_cfg;
                            fuji_cfg.le      = true;
                            fuji_cfg.bigtiff = false;
                            decode_classic_ifd_no_header(fuji_cfg, mn, ifd_off,
                                                         mk_ifd0, store,
                                                         mn_opts, &sink.result,
                                                         EntryFlags::None);
                            continue;
                        }
                    }
                }

                // 3) Best-effort scan for a classic TIFF IFD inside MakerNote
                // (covers cases like Apple iOS, Olympus, etc.).
                ClassicIfdCandidate best;
                if (find_best_classic_ifd_candidate(mn, 256, options.limits,
                                                    &best)) {
                    TiffConfig best_cfg;
                    best_cfg.le      = best.le;
                    best_cfg.bigtiff = false;
                    decode_classic_ifd_no_header(best_cfg, mn, best.offset,
                                                 mk_ifd0, store, mn_opts,
                                                 &sink.result,
                                                 EntryFlags::None);
                    if (vendor == MakerNoteVendor::Sony) {
                        exif_internal::decode_sony_cipher_subdirs(
                            mk_ifd0, store, mn_opts, &sink.result);
                    }
                    continue;
                }

                // 4) Canon-style MakerNotes: raw IFD starting at offset 0,
                // offsets relative to MakerNote start, using parent endianness.
                decode_classic_ifd_no_header(cfg, mn, 0, mk_ifd0, store,
                                             mn_opts, &sink.result,
                                             EntryFlags::None);
                if (vendor == MakerNoteVendor::Sony) {
                    exif_internal::decode_sony_cipher_subdirs(
                        mk_ifd0, store, mn_opts, &sink.result);
                }
            }
        }
    }

    return sink.result;
}

}  // namespace openmeta
