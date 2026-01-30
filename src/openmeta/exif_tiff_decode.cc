#include "openmeta/exif_tiff_decode.h"

#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"
#include "openmeta/printim_decode.h"

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


    struct TiffConfig final {
        bool le      = true;
        bool bigtiff = false;
    };

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


    static std::string_view find_first_exif_text_value(const MetaStore& store,
                                                       std::string_view ifd,
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
            if (e.value.kind != MetaValueKind::Text) {
                continue;
            }
            return arena_string(arena, e.value.data.span);
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
    };

    static MakerNoteVendor
    detect_makernote_vendor(std::span<const std::byte> maker_note_bytes,
                            const MetaStore& store) noexcept
    {
        if (maker_note_bytes.size() >= 6
            && match_bytes(maker_note_bytes, 0, "Nikon\0", 6)) {
            return MakerNoteVendor::Nikon;
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
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "AOC\0", 4)) {
            return MakerNoteVendor::Pentax;
        }
        if (maker_note_bytes.size() >= 4
            && match_bytes(maker_note_bytes, 0, "QVC\0", 4)) {
            return MakerNoteVendor::Casio;
        }

        const std::string_view make
            = find_first_exif_text_value(store, "ifd0", 0x010F /* Make */);

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
            if (ascii_starts_with_insensitive(make, "CASIO")) {
                return MakerNoteVendor::Casio;
            }
        }

        return MakerNoteVendor::Unknown;
    }


    static bool decode_olympus_makernote(const TiffConfig& parent_cfg,
                                         std::span<const std::byte> tiff_bytes,
                                         uint64_t maker_note_off,
                                         uint64_t maker_note_bytes,
                                         std::string_view mk_ifd0,
                                         MetaStore& store,
                                         const ExifDecodeOptions& options,
                                         ExifDecodeResult* status_out) noexcept;

    static bool decode_sony_makernote(const TiffConfig& parent_cfg,
                                      std::span<const std::byte> tiff_bytes,
                                      uint64_t maker_note_off,
                                      uint64_t maker_note_bytes,
                                      std::string_view mk_ifd0,
                                      MetaStore& store,
                                      const ExifDecodeOptions& options,
                                      ExifDecodeResult* status_out) noexcept;

    static bool decode_canon_makernote(const TiffConfig& cfg,
                                       std::span<const std::byte> tiff_bytes,
                                       uint64_t maker_note_off,
                                       uint64_t maker_note_bytes,
                                       std::string_view mk_ifd0,
                                       MetaStore& store,
                                       const ExifDecodeOptions& options,
                                       ExifDecodeResult* status_out) noexcept;

    static bool
    decode_pentax_makernote(std::span<const std::byte> maker_note_bytes,
                            std::string_view mk_ifd0, MetaStore& store,
                            const ExifDecodeOptions& options,
                            ExifDecodeResult* status_out) noexcept;

    static bool
    decode_casio_makernote(std::span<const std::byte> maker_note_bytes,
                           std::string_view mk_ifd0, MetaStore& store,
                           const ExifDecodeOptions& options,
                           ExifDecodeResult* status_out) noexcept;

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

    struct ClassicIfdCandidate final {
        uint64_t offset        = 0;
        bool le                = true;
        uint16_t entry_count   = 0;
        uint32_t valid_entries = 0;
    };

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
            if (value_bytes > options.limits.max_value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                continue;
            }

            const uint64_t inline_cap      = 4;
            const uint64_t value_field_off = eoff + 8;
            const uint64_t value_off       = (value_bytes <= inline_cap)
                                                 ? value_field_off
                                                 : value_or_off32;

            if (value_off + value_bytes > bytes.size()) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                continue;
            }

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
            entry.value = decode_tiff_value(cfg, bytes, type, count, value_off,
                                            value_bytes, store.arena(),
                                            options.limits, status_out);
            entry.flags |= extra_flags;

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }


    static bool decode_olympus_makernote(
        const TiffConfig& parent_cfg, std::span<const std::byte> tiff_bytes,
        uint64_t maker_note_off, uint64_t maker_note_bytes,
        std::string_view mk_ifd0, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
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
        if (!match_bytes(mn, 0, "OLYMP\0", 6)
            && !match_bytes(mn, 0, "CAMER\0", 6)) {
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
                                     store, options, status_out,
                                     EntryFlags::None);
        return true;
    }

    static bool decode_sony_makernote(
        const TiffConfig& parent_cfg, std::span<const std::byte> tiff_bytes,
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
        const std::span<const std::byte> mn
            = tiff_bytes.subspan(static_cast<size_t>(maker_note_off),
                                 static_cast<size_t>(maker_note_bytes));
        if (mn.size() < 8) {
            return false;
        }

        // Some Sony MakerNotes start directly with a classic TIFF IFD at offset 0,
        // but use value offsets relative to the outer EXIF/TIFF stream.
        //
        // Example: ARW where the MakerNote begins with a u16 entry count (no "SONY"
        // ASCII marker) and out-of-line values use absolute offsets.
        if (looks_like_classic_ifd(parent_cfg, tiff_bytes, maker_note_off,
                                   options.limits)) {
            uint16_t entry_count = 0;
            if (read_tiff_u16(parent_cfg, tiff_bytes, maker_note_off,
                              &entry_count)
                && entry_count != 0
                && entry_count <= options.limits.max_entries_per_ifd) {
                const uint64_t ifd_table_bytes
                    = 2U + uint64_t(entry_count) * 12ULL + 4ULL;
                const uint64_t mn_end = maker_note_off + maker_note_bytes;
                if (maker_note_off + ifd_table_bytes <= mn_end) {
                    // Heuristic: require at least one out-of-line value offset to
                    // land after the IFD table when interpreted as an absolute
                    // offset into the outer TIFF stream.
                    bool has_abs_offsets       = false;
                    const uint64_t entries_off = maker_note_off + 2;
                    for (uint32_t i = 0; i < entry_count; ++i) {
                        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;
                        uint16_t type       = 0;
                        if (!read_tiff_u16(parent_cfg, tiff_bytes, eoff + 2,
                                           &type)) {
                            break;
                        }
                        const uint64_t unit = tiff_type_size(type);
                        if (unit == 0) {
                            continue;
                        }
                        uint32_t count32 = 0;
                        uint32_t off32   = 0;
                        if (!read_tiff_u32(parent_cfg, tiff_bytes, eoff + 4,
                                           &count32)
                            || !read_tiff_u32(parent_cfg, tiff_bytes, eoff + 8,
                                              &off32)) {
                            break;
                        }
                        if (count32 == 0) {
                            continue;
                        }
                        const uint64_t count = count32;
                        if (count > (UINT64_MAX / unit)) {
                            continue;
                        }
                        const uint64_t bytes = count * unit;
                        if (bytes <= 4) {
                            continue;
                        }
                        const uint64_t abs_off = off32;
                        if (abs_off >= maker_note_off + ifd_table_bytes
                            && abs_off + bytes <= tiff_bytes.size()) {
                            has_abs_offsets = true;
                            break;
                        }
                    }

                    if (has_abs_offsets) {
                        decode_classic_ifd_no_header(parent_cfg, tiff_bytes,
                                                     maker_note_off, mk_ifd0,
                                                     store, options, status_out,
                                                     EntryFlags::None);
                        return true;
                    }
                }
            }
        }

        // Sony MakerNotes can also embed classic IFDs after a "SONY" prefix.
        // These typically use offsets relative to the outer EXIF/TIFF stream.
        if (!match_bytes(mn, 0, "SONY", 4)) {
            return false;
        }

        ClassicIfdCandidate best;
        bool found = false;

        const uint64_t scan_bytes = (maker_note_bytes < 256U) ? maker_note_bytes
                                                              : 256U;
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


    static void decode_nikon_bin_dir_entries(
        std::string_view ifd_name, MetaStore& store,
        std::span<const uint16_t> tags, std::span<const MetaValue> values,
        const ExifDecodeLimits& limits, ExifDecodeResult* status_out) noexcept
    {
        emit_bin_dir_entries(ifd_name, store, tags, values, limits, status_out);
    }


    static void decode_nikon_settings_dir(std::string_view ifd_name,
                                          std::span<const std::byte> raw,
                                          MetaStore& store,
                                          const ExifDecodeOptions& options,
                                          ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty()) {
            return;
        }
        if (raw.size() < 24) {
            return;
        }
        if ((raw.size() % 8U) != 0U) {
            return;
        }

        uint32_t rec_count = 0;
        if (!read_u32le(raw, 20, &rec_count)) {
            return;
        }
        if (rec_count == 0) {
            return;
        }
        if (rec_count > options.limits.max_entries_per_ifd) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            return;
        }

        const uint64_t rec_bytes = uint64_t(rec_count) * 8ULL;
        if (rec_bytes > (raw.size() - 24ULL)) {
            return;
        }
        if (24ULL + rec_bytes != raw.size()) {
            return;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        for (uint32_t i = 0; i < rec_count; ++i) {
            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return;
            }

            const uint64_t off = 24ULL + uint64_t(i) * 8ULL;
            uint16_t tag       = 0;
            uint16_t type_be   = 0;
            uint32_t val32     = 0;
            if (!read_u16le(raw, off + 0, &tag)
                || !read_u16be(raw, off + 2, &type_be)
                || !read_u32le(raw, off + 4, &val32)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type_be };
            entry.origin.wire_count     = 1;
            entry.flags |= EntryFlags::Derived;

            switch (type_be) {
            case 1: entry.value = make_u8(static_cast<uint8_t>(val32)); break;
            case 3: entry.value = make_u16(static_cast<uint16_t>(val32)); break;
            case 4: entry.value = make_u32(val32); break;
            case 8:
                entry.value = make_i16(static_cast<int16_t>(
                    static_cast<uint16_t>(val32)));
                break;
            case 9: entry.value = make_i32(static_cast<int32_t>(val32)); break;
            default: entry.value = make_u32(val32); break;
            }

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }


    static void
    decode_nikon_binary_subdirs(std::string_view mk_ifd0, MetaStore& store,
                                bool le, const ExifDecodeOptions& options,
                                ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty()) {
            return;
        }

        struct Candidate final {
            uint16_t tag = 0;
            MetaValue value;
        };

        Candidate cands[32];
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
            if (e.value.kind != MetaValueKind::Bytes) {
                continue;
            }
            const uint16_t tag = e.key.data.exif_tag.tag;
            switch (tag) {
            case 0x001F:  // VRInfo
            case 0x0023:  // PictureControlData
            case 0x00BD:  // PictureControlData (alt)
            case 0x0024:  // WorldTime
            case 0x0025:  // ISOInfo
            case 0x002C:  // UnknownInfo
            case 0x0032:  // UnknownInfo2
            case 0x004E:  // NikonSettings
            case 0x0091:  // ShotInfoUnknown
            case 0x0097:  // ColorBalanceUnknown2
            case 0x0098:  // LensData
            case 0x00B7:  // AFInfo2
            case 0x00B9:  // AFTune
                break;
            default: continue;
            }
            if (cand_count < sizeof(cands) / sizeof(cands[0])) {
                cands[cand_count].tag   = tag;
                cands[cand_count].value = e.value;
                cand_count += 1;
            }
        }

        char sub_ifd_buf[96];

        uint32_t idx_vrinfo         = 0;
        uint32_t idx_picturecontrol = 0;
        uint32_t idx_worldtime      = 0;
        uint32_t idx_isoinfo        = 0;
        uint32_t idx_unknowninfo    = 0;
        uint32_t idx_unknowninfo2   = 0;
        uint32_t idx_settings       = 0;
        uint32_t idx_shotinfo       = 0;
        uint32_t idx_colorbalance   = 0;
        uint32_t idx_lensdata       = 0;
        uint32_t idx_afinfo2        = 0;
        uint32_t idx_aftune         = 0;

        for (uint32_t i = 0; i < cand_count; ++i) {
            const uint16_t tag                       = cands[i].tag;
            const ByteSpan raw_span                  = cands[i].value.data.span;
            const std::span<const std::byte> raw_src = store.arena().span(
                raw_span);
            if (raw_src.empty()) {
                continue;
            }

            const std::string_view mk_prefix = "mk_nikon";

            if (tag == 0x001F) {  // VRInfo
                if (raw_src.size() < 7) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "vrinfo",
                                                 idx_vrinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 8> raw;
                const size_t n = (raw_src.size() < raw.size()) ? raw_src.size()
                                                               : raw.size();
                std::memcpy(raw.data(), raw_src.data(), n);

                uint8_t vr_enabled = 0;
                uint8_t vr_mode    = 0;
                if (n > 4) {
                    vr_enabled = u8(raw[4]);
                }
                if (n > 6) {
                    vr_mode = u8(raw[6]);
                }

                const uint16_t tags_out[]  = { 0x0000, 0x0004, 0x0006 };
                const MetaValue vals_out[] = {
                    make_fixed_ascii_text(store.arena(),
                                          std::span<const std::byte>(raw.data(),
                                                                     4)),
                    make_u8(vr_enabled),
                    make_u8(vr_mode),
                };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x0023 || tag == 0x00BD) {  // PictureControlData
                if (raw_src.size() < 52) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "picturecontrol",
                                                 idx_picturecontrol++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 58> raw;
                const size_t n = (raw_src.size() < raw.size()) ? raw_src.size()
                                                               : raw.size();
                std::memcpy(raw.data(), raw_src.data(), n);

                const MetaValue version = make_fixed_ascii_text(
                    store.arena(), std::span<const std::byte>(raw.data(), 4));
                const MetaValue name = make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(raw.data() + 4, 20));
                const MetaValue base = make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(raw.data() + 24, 20));

                const uint8_t adjust       = (n > 48) ? u8(raw[48]) : 0;
                const uint8_t quick_adjust = (n > 49) ? u8(raw[49]) : 0;

                MetaValue extras[8];
                uint16_t extra_tags[8];
                uint32_t extra_count = 0;
                for (uint16_t t = 0x0032; t <= 0x0039; ++t) {
                    const uint64_t off = t;
                    if (off >= n) {
                        break;
                    }
                    extra_tags[extra_count] = t;
                    extras[extra_count]     = make_u8(u8(raw[off]));
                    extra_count += 1;
                    if (extra_count == 8) {
                        break;
                    }
                }

                uint16_t tags_out[13];
                MetaValue vals_out[13];
                uint32_t out_count = 0;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = version;
                out_count += 1;
                tags_out[out_count] = 0x0004;
                vals_out[out_count] = name;
                out_count += 1;
                tags_out[out_count] = 0x0018;
                vals_out[out_count] = base;
                out_count += 1;
                tags_out[out_count] = 0x0030;
                vals_out[out_count] = make_u8(adjust);
                out_count += 1;
                tags_out[out_count] = 0x0031;
                vals_out[out_count] = make_u8(quick_adjust);
                out_count += 1;

                for (uint32_t k = 0; k < extra_count; ++k) {
                    tags_out[out_count] = extra_tags[k];
                    vals_out[out_count] = extras[k];
                    out_count += 1;
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            if (tag == 0x0024) {  // WorldTime
                if (raw_src.size() < 4) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "worldtime",
                                                 idx_worldtime++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 4> raw;
                std::memcpy(raw.data(), raw_src.data(), raw.size());

                int16_t tz = 0;
                if (!read_i16_endian(le, raw, 0, &tz)) {
                    continue;
                }
                const uint8_t dst = u8(raw[2]);
                const uint8_t fmt = u8(raw[3]);

                const uint16_t tags_out[]  = { 0x0000, 0x0002, 0x0003 };
                const MetaValue vals_out[] = { make_i16(tz), make_u8(dst),
                                               make_u8(fmt) };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x0025) {  // ISOInfo
                if (raw_src.size() < 12) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "isoinfo",
                                                 idx_isoinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 14> raw;
                const size_t n = (raw_src.size() < raw.size()) ? raw_src.size()
                                                               : raw.size();
                std::memcpy(raw.data(), raw_src.data(), n);

                uint16_t iso_expansion  = 0;
                uint16_t iso_expansion2 = 0;
                if (!read_u16_endian(le, raw, 4, &iso_expansion)
                    || !read_u16_endian(le, raw, 10, &iso_expansion2)) {
                    continue;
                }
                const uint8_t iso  = u8(raw[0]);
                const uint8_t iso2 = u8(raw[6]);

                const uint16_t tags_out[]  = { 0x0000, 0x0004, 0x0006, 0x000A };
                const MetaValue vals_out[] = {
                    make_u8(iso),
                    make_u16(iso_expansion),
                    make_u8(iso2),
                    make_u16(iso_expansion2),
                };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x002C) {  // UnknownInfo
                if (raw_src.size() < 4) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "unknowninfo",
                                                 idx_unknowninfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                std::array<std::byte, 4> raw;
                std::memcpy(raw.data(), raw_src.data(), raw.size());
                const uint16_t tags_out[]  = { 0x0000 };
                const MetaValue vals_out[] = { make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(raw.data(), raw.size())) };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x0032) {  // UnknownInfo2
                if (raw_src.size() < 4) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "unknowninfo2",
                                                 idx_unknowninfo2++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                std::array<std::byte, 4> raw;
                std::memcpy(raw.data(), raw_src.data(), raw.size());
                const uint16_t tags_out[]  = { 0x0000 };
                const MetaValue vals_out[] = { make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(raw.data(), raw.size())) };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x0091) {  // ShotInfoUnknown
                if (raw_src.size() < 9) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "shotinfo",
                                                 idx_shotinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 9> raw;
                std::memcpy(raw.data(), raw_src.data(), raw.size());

                const uint16_t tags_out[]  = { 0x0000, 0x0004 };
                const MetaValue vals_out[] = {
                    make_fixed_ascii_text(store.arena(),
                                          std::span<const std::byte>(raw.data(),
                                                                     4)),
                    make_fixed_ascii_text(
                        store.arena(),
                        std::span<const std::byte>(raw.data() + 4, 5)),
                };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x0097) {  // ColorBalanceUnknown2
                if (raw_src.size() < 4) {
                    continue;
                }
                const std::string_view ifd_name = make_mk_subtable_ifd_token(
                    mk_prefix, "colorbalanceunknown2", idx_colorbalance++,
                    std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                std::array<std::byte, 4> raw;
                std::memcpy(raw.data(), raw_src.data(), raw.size());
                const uint16_t tags_out[]  = { 0x0000 };
                const MetaValue vals_out[] = { make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(raw.data(), raw.size())) };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x0098) {  // LensData
                if (raw_src.size() < 4) {
                    continue;
                }
                std::array<std::byte, 4> ver_bytes;
                std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());
                const std::string_view ver = std::string_view(
                    reinterpret_cast<const char*>(ver_bytes.data()),
                    ver_bytes.size());

                std::string_view subtable = "lensdataunknown";
                uint16_t lens_model_tag   = 0;
                uint64_t lens_model_off   = 0;
                uint64_t lens_model_bytes = 0;
                if (ver == "0400") {
                    subtable         = "lensdata0400";
                    lens_model_tag   = 0x018a;
                    lens_model_off   = 0x018a;
                    lens_model_bytes = 64;
                } else if (ver == "0402") {
                    subtable         = "lensdata0402";
                    lens_model_tag   = 0x018b;
                    lens_model_off   = 0x018b;
                    lens_model_bytes = 64;
                } else if (ver == "0403") {
                    subtable         = "lensdata0403";
                    lens_model_tag   = 0x02ac;
                    lens_model_off   = 0x02ac;
                    lens_model_bytes = 64;
                }

                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, subtable,
                                                 idx_lensdata++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                uint16_t tags_out[2];
                MetaValue vals_out[2];
                uint32_t out_count = 0;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(ver_bytes.data(),
                                               ver_bytes.size()));
                out_count += 1;

                if (lens_model_tag != 0 && lens_model_off < raw_src.size()
                    && lens_model_off + lens_model_bytes <= raw_src.size()) {
                    std::array<std::byte, 64> model_bytes;
                    std::memcpy(model_bytes.data(),
                                raw_src.data()
                                    + static_cast<size_t>(lens_model_off),
                                model_bytes.size());
                    tags_out[out_count] = lens_model_tag;
                    vals_out[out_count] = make_fixed_ascii_text(
                        store.arena(),
                        std::span<const std::byte>(model_bytes.data(),
                                                   model_bytes.size()));
                    out_count += 1;
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            if (tag == 0x004E) {  // NikonSettings
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token("mk_nikonsettings", "main",
                                                 idx_settings++,
                                                 std::span<char>(sub_ifd_buf));
                decode_nikon_settings_dir(ifd_name, raw_src, store, options,
                                          status_out);
                continue;
            }

            if (tag == 0x00B7) {  // AFInfo2
                if (raw_src.size() < 9) {
                    continue;
                }

                std::array<std::byte, 25> raw;
                const size_t n = (raw_src.size() < raw.size()) ? raw_src.size()
                                                               : raw.size();
                std::memcpy(raw.data(), raw_src.data(), n);
                const std::string_view ver = std::string_view(
                    reinterpret_cast<const char*>(raw.data()), 4);
                std::string_view subtable = "afinfo";
                if (ver == "0200") {
                    subtable = "afinfo2v0200";
                }

                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, subtable,
                                                 idx_afinfo2++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                MetaValue points;
                if (n >= 25) {
                    points
                        = make_bytes(store.arena(),
                                     std::span<const std::byte>(raw.data() + 8,
                                                                17));
                }

                uint16_t tags_out[6];
                MetaValue vals_out[6];
                uint32_t out_count = 0;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(), std::span<const std::byte>(raw.data(), 4));
                out_count += 1;
                tags_out[out_count] = 0x0004;
                vals_out[out_count] = make_u8((n > 4) ? u8(raw[4]) : 0);
                out_count += 1;
                tags_out[out_count] = 0x0005;
                vals_out[out_count] = make_u8((n > 5) ? u8(raw[5]) : 0);
                out_count += 1;
                tags_out[out_count] = 0x0006;
                vals_out[out_count] = make_u8((n > 6) ? u8(raw[6]) : 0);
                out_count += 1;
                tags_out[out_count] = 0x0007;
                vals_out[out_count] = make_u8((n > 7) ? u8(raw[7]) : 0);
                out_count += 1;

                if (points.kind != MetaValueKind::Empty) {
                    tags_out[out_count] = 0x0008;
                    vals_out[out_count] = points;
                    out_count += 1;
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            if (tag == 0x00B9) {  // AFTune
                if (raw_src.size() < 4) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "aftune",
                                                 idx_aftune++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 4> raw;
                std::memcpy(raw.data(), raw_src.data(), raw.size());

                const uint16_t tags_out[]  = { 0x0000, 0x0001, 0x0002, 0x0003 };
                const MetaValue vals_out[] = {
                    make_u8(u8(raw[0])),
                    make_u8(u8(raw[1])),
                    make_i8(static_cast<int8_t>(u8(raw[2]))),
                    make_i8(static_cast<int8_t>(u8(raw[3]))),
                };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }
        }
    }


    static void decode_canon_u16_table(const TiffConfig& cfg,
                                       std::span<const std::byte> bytes,
                                       uint64_t value_off, uint32_t count,
                                       std::string_view ifd_name,
                                       MetaStore& store,
                                       const ExifDecodeOptions& options,
                                       ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty() || count == 0) {
            return;
        }
        if (count > options.limits.max_entries_per_ifd) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            return;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        for (uint32_t i = 0; i < count; ++i) {
            if (i > 0xFFFFu) {
                break;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return;
            }

            uint16_t v = 0;
            if (!read_tiff_u16(cfg, bytes, value_off + uint64_t(i) * 2ULL, &v)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }

            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), ifd_name,
                                                   static_cast<uint16_t>(i));
            entry.origin.block = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, 3 };
            entry.origin.wire_count     = 1;
            entry.value                 = make_u16(v);
            entry.flags |= EntryFlags::Derived;

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }


    static void decode_canon_u32_table(const TiffConfig& cfg,
                                       std::span<const std::byte> bytes,
                                       uint64_t value_off, uint32_t count,
                                       std::string_view ifd_name,
                                       MetaStore& store,
                                       const ExifDecodeOptions& options,
                                       ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty() || count == 0) {
            return;
        }
        if (count > options.limits.max_entries_per_ifd) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            return;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        for (uint32_t i = 0; i < count; ++i) {
            if (i > 0xFFFFu) {
                break;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return;
            }

            uint32_t v = 0;
            if (!read_tiff_u32(cfg, bytes, value_off + uint64_t(i) * 4ULL, &v)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }

            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), ifd_name,
                                                   static_cast<uint16_t>(i));
            entry.origin.block = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, 4 };
            entry.origin.wire_count     = 1;
            entry.value                 = make_u32(v);
            entry.flags |= EntryFlags::Derived;

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }


    static void decode_canon_psinfo_table(
        std::span<const std::byte> bytes, uint64_t value_off,
        uint64_t value_bytes, std::string_view ifd_name, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty()) {
            return;
        }
        if (value_bytes == 0) {
            return;
        }
        if (value_off > bytes.size()) {
            return;
        }
        if (value_bytes > (bytes.size() - value_off)) {
            return;
        }

        // psinfo: fixed-layout Canon picture style table (byte offsets).
        // Most fields are int32, with a few u16 fields near the end.
        static constexpr uint16_t kUserDefTag1 = 0x00d8;
        static constexpr uint16_t kUserDefTag2 = 0x00da;
        static constexpr uint16_t kUserDefTag3 = 0x00dc;
        static constexpr uint16_t kMaxTag      = 0x00dc;

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        uint32_t order = 0;
        for (uint16_t tag = 0; tag <= kMaxTag; tag = uint16_t(tag + 2U)) {
            if ((uint64_t(tag) + 2U) > value_bytes) {
                break;
            }

            if (exif_tag_name(ifd_name, tag).empty()) {
                continue;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return;
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = order++;
            entry.flags |= EntryFlags::Derived;

            if (tag == kUserDefTag1 || tag == kUserDefTag2
                || tag == kUserDefTag3) {
                if ((uint64_t(tag) + 2U) > value_bytes) {
                    if (status_out) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    }
                    return;
                }

                uint16_t v = 0;
                if (!read_u16le(bytes, value_off + tag, &v)) {
                    if (status_out) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    }
                    return;
                }
                entry.origin.wire_type  = WireType { WireFamily::Tiff, 3 };
                entry.origin.wire_count = 1;
                entry.value             = make_u16(v);
            } else {
                if ((uint64_t(tag) + 4U) > value_bytes) {
                    if (status_out) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    }
                    return;
                }

                uint32_t u = 0;
                if (!read_u32le(bytes, value_off + tag, &u)) {
                    if (status_out) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    }
                    return;
                }
                entry.origin.wire_type  = WireType { WireFamily::Tiff, 9 };
                entry.origin.wire_count = 1;
                entry.value             = make_i32(static_cast<int32_t>(u));
            }

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }


    static bool decode_canon_afinfo2_add_u16_scalar(
        const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
        uint64_t value_off, std::string_view mk_ifd0, BlockId block,
        uint32_t order, uint16_t tag, uint32_t word_index, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out) noexcept
    {
        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return false;
        }

        uint16_t v = 0;
        if (!read_tiff_u16(cfg, tiff_bytes,
                           value_off + uint64_t(word_index) * 2ULL, &v)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return false;
        }

        Entry entry;
        entry.key          = make_exif_tag_key(store.arena(), mk_ifd0, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = order;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, 3 };
        entry.origin.wire_count     = 1;
        entry.value                 = make_u16(v);
        entry.flags |= EntryFlags::Derived;

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
        return true;
    }


    constexpr uint32_t sony_mod_mul_249(uint32_t a, uint32_t b) noexcept
    {
        return (a * b) % 249U;
    }


    constexpr uint8_t sony_mod_pow_249(uint8_t base, uint8_t exp) noexcept
    {
        uint32_t result = 1;
        uint32_t cur    = base;
        uint8_t e       = exp;
        while (e != 0) {
            if ((e & 1U) != 0U) {
                result = sony_mod_mul_249(result, cur);
            }
            cur = sony_mod_mul_249(cur, cur);
            e   = static_cast<uint8_t>(e >> 1U);
        }
        return static_cast<uint8_t>(result);
    }


    constexpr std::array<uint8_t, 249> make_sony_decipher_lut() noexcept
    {
        std::array<uint8_t, 249> out {};
        for (uint32_t i = 0; i < out.size(); ++i) {
            // ExifTool's Sony substitution cipher uses:
            //   encipher: c = (b^3) % 249 for b in [0..248]
            // so decipher is the modular cube root: b = (c^55) % 249
            // because 3*55 ≡ 1 (mod phi(249)=164).
            out[i] = sony_mod_pow_249(static_cast<uint8_t>(i), 55);
        }
        return out;
    }


    static constexpr std::array<uint8_t, 249> kSonyDecipherLut
        = make_sony_decipher_lut();


    static uint8_t sony_decipher_once(uint8_t b) noexcept
    {
        if (b >= 249U) {
            return b;
        }
        return kSonyDecipherLut[b];
    }


    static uint8_t sony_decipher(uint8_t b, uint32_t rounds) noexcept
    {
        uint8_t out = b;
        for (uint32_t i = 0; i < rounds; ++i) {
            out = sony_decipher_once(out);
        }
        return out;
    }


    static bool sony_read_u8(std::span<const std::byte> bytes, uint64_t off,
                             uint32_t rounds, uint8_t* out) noexcept
    {
        if (!out || off >= bytes.size()) {
            return false;
        }
        *out = sony_decipher(u8(bytes[off]), rounds);
        return true;
    }


    static bool sony_read_u16le(std::span<const std::byte> bytes, uint64_t off,
                                uint32_t rounds, uint16_t* out) noexcept
    {
        if (!out || off + 2U > bytes.size()) {
            return false;
        }
        const uint8_t b0 = sony_decipher(u8(bytes[off + 0]), rounds);
        const uint8_t b1 = sony_decipher(u8(bytes[off + 1]), rounds);
        *out = static_cast<uint16_t>(uint16_t(b0) | (uint16_t(b1) << 8));
        return true;
    }


    static bool sony_read_i16le(std::span<const std::byte> bytes, uint64_t off,
                                uint32_t rounds, int16_t* out) noexcept
    {
        uint16_t raw = 0;
        if (!sony_read_u16le(bytes, off, rounds, &raw)) {
            return false;
        }
        if (out) {
            *out = static_cast<int16_t>(raw);
        }
        return out != nullptr;
    }


    static bool sony_read_u32le(std::span<const std::byte> bytes, uint64_t off,
                                uint32_t rounds, uint32_t* out) noexcept
    {
        if (!out || off + 4U > bytes.size()) {
            return false;
        }
        const uint8_t b0 = sony_decipher(u8(bytes[off + 0]), rounds);
        const uint8_t b1 = sony_decipher(u8(bytes[off + 1]), rounds);
        const uint8_t b2 = sony_decipher(u8(bytes[off + 2]), rounds);
        const uint8_t b3 = sony_decipher(u8(bytes[off + 3]), rounds);
        *out = uint32_t(b0) | (uint32_t(b1) << 8) | (uint32_t(b2) << 16)
               | (uint32_t(b3) << 24);
        return true;
    }

    static MetaValue
    make_sony_deciphered_bytes(ByteArena& arena,
                               std::span<const std::byte> bytes, uint64_t off,
                               uint32_t size, uint32_t rounds) noexcept
    {
        MetaValue v;
        if (size == 0) {
            return v;
        }
        if (off > bytes.size() || uint64_t(size) > (bytes.size() - off)) {
            return v;
        }

        const ByteSpan span            = arena.allocate(size, 1);
        const std::span<std::byte> out = arena.span_mut(span);
        if (out.size() != size) {
            return v;
        }
        for (uint32_t i = 0; i < size; ++i) {
            out[i] = std::byte { sony_decipher(u8(bytes[off + i]), rounds) };
        }

        v.kind          = MetaValueKind::Bytes;
        v.elem_type     = MetaElementType::U8;
        v.count         = size;
        v.data.span     = span;
        v.text_encoding = TextEncoding::Unknown;
        return v;
    }


    static uint32_t
    sony_guess_cipher_rounds(std::span<const std::byte> bytes, uint64_t off,
                             std::span<const uint8_t> allowed) noexcept
    {
        uint8_t b = 0;
        if (!sony_read_u8(bytes, off, 1, &b)) {
            return 1;
        }
        for (size_t i = 0; i < allowed.size(); ++i) {
            if (b == allowed[i]) {
                return 1;
            }
        }

        if (!sony_read_u8(bytes, off, 2, &b)) {
            return 1;
        }
        for (size_t i = 0; i < allowed.size(); ++i) {
            if (b == allowed[i]) {
                return 2;
            }
        }
        return 1;
    }


    static void decode_sony_tag9400(std::span<const std::byte> bytes,
                                    std::string_view mk_prefix,
                                    MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        // Tag9400 variant selection (based on first deciphered byte).
        const uint8_t allowed[] = { 0x07, 0x09, 0x0A, 0x0C, 0x23, 0x24,
                                    0x26, 0x28, 0x31, 0x32, 0x33 };
        const uint32_t rounds
            = sony_guess_cipher_rounds(bytes, 0,
                                       std::span<const uint8_t>(allowed));

        uint8_t v0 = 0;
        if (!sony_read_u8(bytes, 0, rounds, &v0)) {
            return;
        }

        // In practice, tag9400c is the most common layout. tag9400b appears on
        // some older bodies and uses different offsets.
        std::string_view subtable = "tag9400c";
        if (v0 == 0x0C) {
            subtable = "tag9400b";
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, subtable, 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        uint16_t tags_out[16];
        MetaValue vals_out[16];
        uint32_t out_count = 0;

        // tag9400c common fields (byte offsets into deciphered data):
        // - SequenceImageNumber (u32) at 0x0012
        // - SequenceFileNumber (u32) at 0x001A
        // - ModelReleaseYear (u16) at 0x0053
        uint32_t u32 = 0;
        if (sony_read_u32le(bytes, 0x0012, rounds, &u32)) {
            tags_out[out_count] = 0x0012;
            vals_out[out_count] = make_u32(u32);
            out_count += 1;
        }
        if (sony_read_u32le(bytes, 0x001A, rounds, &u32)) {
            tags_out[out_count] = 0x001A;
            vals_out[out_count] = make_u32(u32);
            out_count += 1;
        }
        uint16_t u16v = 0;
        if (sony_read_u16le(bytes, 0x0053, rounds, &u16v)) {
            tags_out[out_count] = 0x0053;
            vals_out[out_count] = make_u16(u16v);
            out_count += 1;
        }

        uint8_t u8v              = 0;
        const uint16_t u8_tags[] = { 0x0009, 0x000A, 0x0016,
                                     0x001E, 0x0029, 0x002A };
        for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
            const uint16_t t = u8_tags[i];
            if (!sony_read_u8(bytes, t, rounds, &u8v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u8(u8v);
            out_count += 1;
            if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                break;
            }
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void decode_sony_tag9402(std::span<const std::byte> bytes,
                                    std::string_view mk_prefix,
                                    MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "tag9402", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        // Tag9402 uses the same Sony substitution cipher.
        const uint32_t rounds = 1;

        uint16_t tags_out[8];
        MetaValue vals_out[8];
        uint32_t out_count = 0;

        const uint16_t u8_tags[] = { 0x0002, 0x0004, 0x0016, 0x0017, 0x002D };
        for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
            const uint16_t t = u8_tags[i];
            uint8_t v        = 0;
            if (!sony_read_u8(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u8(v);
            out_count += 1;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void decode_sony_tag9403(std::span<const std::byte> bytes,
                                    std::string_view mk_prefix,
                                    MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "tag9403", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        const uint32_t rounds = 1;

        uint16_t tags_out[8];
        MetaValue vals_out[8];
        uint32_t out_count = 0;

        const uint16_t u8_tags[] = { 0x0004, 0x0005 };
        for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
            const uint16_t t = u8_tags[i];
            uint8_t v        = 0;
            if (!sony_read_u8(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u8(v);
            out_count += 1;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void decode_sony_tag9406(std::span<const std::byte> bytes,
                                    std::string_view mk_prefix,
                                    MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "tag9406", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        const uint32_t rounds = 1;

        uint16_t tags_out[8];
        MetaValue vals_out[8];
        uint32_t out_count = 0;

        const uint16_t u8_tags[] = { 0x0005, 0x0006, 0x0007, 0x0008 };
        for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
            const uint16_t t = u8_tags[i];
            uint8_t v        = 0;
            if (!sony_read_u8(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u8(v);
            out_count += 1;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void decode_sony_tag940c(std::span<const std::byte> bytes,
                                    std::string_view mk_prefix,
                                    MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "tag940c", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        const uint32_t rounds = 1;

        uint16_t tags_out[8];
        MetaValue vals_out[8];
        uint32_t out_count = 0;

        uint8_t u8v = 0;
        if (sony_read_u8(bytes, 0x0008, rounds, &u8v)) {
            tags_out[out_count] = 0x0008;
            vals_out[out_count] = make_u8(u8v);
            out_count += 1;
        }

        uint16_t u16v = 0;
        if (sony_read_u16le(bytes, 0x0009, rounds, &u16v)) {
            tags_out[out_count] = 0x0009;
            vals_out[out_count] = make_u16(u16v);
            out_count += 1;
        }

        if (sony_read_u16le(bytes, 0x000B, rounds, &u16v)) {
            tags_out[out_count] = 0x000B;
            vals_out[out_count] = make_u16(u16v);
            out_count += 1;
        }

        if (sony_read_u16le(bytes, 0x000D, rounds, &u16v)) {
            tags_out[out_count] = 0x000D;
            vals_out[out_count] = make_u16(u16v);
            out_count += 1;
        }

        if (sony_read_u16le(bytes, 0x0014, rounds, &u16v)) {
            tags_out[out_count] = 0x0014;
            vals_out[out_count] = make_u16(u16v);
            out_count += 1;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void decode_sony_isoinfo_from_tag9401(
        std::span<const std::byte> bytes, std::string_view mk_prefix,
        MetaStore& store, const ExifDecodeOptions& options,
        ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        const uint32_t rounds = 1;

        // Try a set of known ISOInfo locations inside Tag9401 and pick the first
        // plausible candidate.
        const uint16_t iso_offsets[] = {
            0x03E2, 0x03F4, 0x044E, 0x0498, 0x049D, 0x049E,
            0x04A1, 0x04A2, 0x04BA, 0x059D, 0x0634, 0x0636,
            0x064C, 0x0653, 0x0678, 0x06B8, 0x06DE, 0x06E7,
        };

        uint16_t best_off        = 0;
        uint32_t best_score      = 0;
        uint8_t best_iso_setting = 0;
        uint8_t best_iso_min     = 0;
        uint8_t best_iso_max     = 0;

        for (uint32_t i = 0; i < sizeof(iso_offsets) / sizeof(iso_offsets[0]);
             ++i) {
            const uint64_t base = iso_offsets[i];
            if (base + 5U > bytes.size()) {
                continue;
            }

            uint8_t iso_setting = 0;
            uint8_t iso_min     = 0;
            uint8_t iso_max     = 0;
            if (!sony_read_u8(bytes, base + 0, rounds, &iso_setting)
                || !sony_read_u8(bytes, base + 2, rounds, &iso_min)
                || !sony_read_u8(bytes, base + 4, rounds, &iso_max)) {
                continue;
            }

            // Heuristic: ISO codes are small enum values.
            uint32_t score = 0;
            if (iso_setting <= 80U) {
                score += 1;
            }
            if (iso_min <= 80U) {
                score += 1;
            }
            if (iso_max <= 80U) {
                score += 1;
            }
            if (iso_setting == 0U) {
                score += 1;
            }

            if (score > best_score) {
                best_score       = score;
                best_off         = iso_offsets[i];
                best_iso_setting = iso_setting;
                best_iso_min     = iso_min;
                best_iso_max     = iso_max;
            }
        }

        if (best_score == 0) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "isoinfo", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        const uint16_t tags_out[]  = { 0x0000, 0x0002, 0x0004 };
        const MetaValue vals_out[] = { make_u8(best_iso_setting),
                                       make_u8(best_iso_min),
                                       make_u8(best_iso_max) };
        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out),
                             std::span<const MetaValue>(vals_out),
                             options.limits, status_out);
        (void)best_off;
    }

    static void decode_sony_tag9404(std::span<const std::byte> bytes,
                                    std::string_view mk_prefix,
                                    MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "tag9404c", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        const uint32_t rounds = 1;

        uint16_t tags_out[8];
        MetaValue vals_out[8];
        uint32_t out_count = 0;

        uint8_t u8v              = 0;
        const uint16_t u8_tags[] = { 0x000B, 0x000D };
        for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
            const uint16_t t = u8_tags[i];
            if (!sony_read_u8(bytes, t, rounds, &u8v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u8(u8v);
            out_count += 1;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void decode_sony_tag202a(std::span<const std::byte> bytes,
                                    std::string_view mk_prefix,
                                    MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "tag202a", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        const uint32_t rounds = 1;

        uint8_t u8v = 0;
        if (!sony_read_u8(bytes, 0x0001, rounds, &u8v)) {
            return;
        }

        const uint16_t tags_out[]  = { 0x0001 };
        const MetaValue vals_out[] = { make_u8(u8v) };
        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out),
                             std::span<const MetaValue>(vals_out),
                             options.limits, status_out);
    }

    static void decode_sony_shotinfo_from_tag3000(
        std::span<const std::byte> bytes, std::string_view mk_prefix,
        MetaStore& store, const ExifDecodeOptions& options,
        ExifDecodeResult* status_out) noexcept
    {
        // Tag3000 is a small binary directory where offsets correspond to tag
        // ids (e.g. tag 0x001C stored at byte offset 0x001C).
        if (bytes.size() < 0x0044) {
            return;
        }

        const uint8_t b0 = u8(bytes[0]);
        const uint8_t b1 = u8(bytes[1]);
        bool le          = true;
        if (b0 == 'I' && b1 == 'I') {
            le = true;
        } else if (b0 == 'M' && b1 == 'M') {
            le = false;
        } else {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "shotinfo", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        uint16_t tags_out[16];
        MetaValue vals_out[16];
        uint32_t out_count = 0;

        uint16_t u16v = 0;
        if (read_u16_endian(le, bytes, 0x0002, &u16v)) {
            tags_out[out_count] = 0x0002;
            vals_out[out_count] = make_u16(u16v);
            out_count += 1;
        }

        if (0x0006 + 20U <= bytes.size()
            && 20U <= options.limits.max_value_bytes) {
            tags_out[out_count] = 0x0006;
            vals_out[out_count]
                = make_fixed_ascii_text(store.arena(),
                                        bytes.subspan(0x0006, 20));
            out_count += 1;
        }

        if (read_u16_endian(le, bytes, 0x001A, &u16v)) {
            tags_out[out_count] = 0x001A;
            vals_out[out_count] = make_u16(u16v);
            out_count += 1;
        }
        if (read_u16_endian(le, bytes, 0x001C, &u16v)) {
            tags_out[out_count] = 0x001C;
            vals_out[out_count] = make_u16(u16v);
            out_count += 1;
        }

        if (read_u16_endian(le, bytes, 0x0030, &u16v)) {
            tags_out[out_count] = 0x0030;
            vals_out[out_count] = make_u16(u16v);
            out_count += 1;
        }
        if (read_u16_endian(le, bytes, 0x0032, &u16v)) {
            tags_out[out_count] = 0x0032;
            vals_out[out_count] = make_u16(u16v);
            out_count += 1;
        }

        if (0x0034 + 16U <= bytes.size()
            && 16U <= options.limits.max_value_bytes) {
            tags_out[out_count] = 0x0034;
            vals_out[out_count]
                = make_fixed_ascii_text(store.arena(),
                                        bytes.subspan(0x0034, 16));
            out_count += 1;
        }

        if (out_count == 0) {
            return;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void decode_sony_meterinfo9_from_tag2010(
        std::span<const std::byte> bytes, std::string_view mk_prefix,
        MetaStore& store, const ExifDecodeOptions& options,
        ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "meterinfo9", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        const uint32_t rounds = 1;

        struct Row final {
            uint16_t tag  = 0;
            uint16_t size = 0;
        };

        // MeterInfo row ranges are inferred from the known tag spacing:
        // - MeterInfo1 rows: 0x5A bytes each
        // - MeterInfo2 rows: 0x6E bytes each
        const Row rows[] = {
            { 0x0000, 0x005A }, { 0x005A, 0x005A }, { 0x00B4, 0x005A },
            { 0x010E, 0x005A }, { 0x0168, 0x005A }, { 0x01C2, 0x005A },
            { 0x021C, 0x005A }, { 0x0276, 0x006E }, { 0x02E4, 0x006E },
            { 0x0352, 0x006E }, { 0x03C0, 0x006E }, { 0x042E, 0x006E },
            { 0x049C, 0x006E }, { 0x050A, 0x006E }, { 0x0578, 0x006E },
            { 0x05E6, 0x006E },
        };

        uint16_t tags_out[32];
        MetaValue vals_out[32];
        uint32_t out_count = 0;

        for (uint32_t i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
            const uint16_t tag = rows[i].tag;
            const uint16_t len = rows[i].size;
            if (out_count >= sizeof(tags_out) / sizeof(tags_out[0])) {
                break;
            }
            if (tag + len > bytes.size()) {
                continue;
            }
            if (len == 0 || len > options.limits.max_value_bytes) {
                continue;
            }

            const MetaValue v = make_sony_deciphered_bytes(store.arena(), bytes,
                                                           tag, len, rounds);
            if (v.kind != MetaValueKind::Bytes) {
                continue;
            }
            tags_out[out_count] = tag;
            vals_out[out_count] = v;
            out_count += 1;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void decode_sony_tag2010i(std::span<const std::byte> bytes,
                                     std::string_view mk_prefix,
                                     MetaStore& store,
                                     const ExifDecodeOptions& options,
                                     ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "tag2010i", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        const uint32_t rounds = 1;

        uint16_t tags_out[64];
        MetaValue vals_out[64];
        uint32_t out_count = 0;

        uint8_t u8v              = 0;
        const uint16_t u8_tags[] = { 0x0004, 0x004E, 0x0204, 0x0208, 0x0210,
                                     0x0211, 0x021B, 0x021F, 0x0237, 0x0238,
                                     0x023C, 0x0247, 0x024B, 0x024C, 0x17F1,
                                     0x17F2, 0x17F8, 0x17F9, 0x188C };
        for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
            const uint16_t t = u8_tags[i];
            if (!sony_read_u8(bytes, t, rounds, &u8v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u8(u8v);
            out_count += 1;
        }

        // Fixed-point-ish fields (best-effort, stored as i16).
        const uint16_t i16_tags[] = { 0x0217, 0x0219, 0x0223 };
        for (uint32_t i = 0; i < sizeof(i16_tags) / sizeof(i16_tags[0]); ++i) {
            const uint16_t t = i16_tags[i];
            int16_t v        = 0;
            if (!sony_read_i16le(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_i16(v);
            out_count += 1;
        }

        // WB_RGBLevels u16[3].
        {
            std::array<uint16_t, 3> v {};
            bool ok = true;
            for (uint32_t i = 0; i < v.size(); ++i) {
                ok = ok
                     && sony_read_u16le(bytes, 0x0252 + i * 2U, rounds, &v[i]);
            }
            if (ok) {
                tags_out[out_count] = 0x0252;
                vals_out[out_count]
                    = make_u16_array(store.arena(),
                                     std::span<const uint16_t>(v));
                out_count += 1;
            }
        }

        // Focal lengths (best-effort u16).
        const uint16_t focal_tags[] = { 0x030A, 0x030C, 0x030E };
        for (uint32_t i = 0; i < sizeof(focal_tags) / sizeof(focal_tags[0]);
             ++i) {
            const uint16_t t = focal_tags[i];
            uint16_t v       = 0;
            if (!sony_read_u16le(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u16(v);
            out_count += 1;
        }

        // SonyISO u16.
        {
            uint16_t v = 0;
            if (sony_read_u16le(bytes, 0x0320, rounds, &v)) {
                tags_out[out_count] = 0x0320;
                vals_out[out_count] = make_u16(v);
                out_count += 1;
            }
        }

        // LensType2/LensType u16.
        const uint16_t lens_u16_tags[] = { 0x17F3, 0x17F6 };
        for (uint32_t i = 0;
             i < sizeof(lens_u16_tags) / sizeof(lens_u16_tags[0]); ++i) {
            uint16_t v       = 0;
            const uint16_t t = lens_u16_tags[i];
            if (!sony_read_u16le(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u16(v);
            out_count += 1;
        }

        // DistortionCorrParamsPresent/Number (u8) already emitted above; add a small
        // deciphered prefix for DistortionCorrParams itself.
        if (0x17D0 + 32U <= bytes.size()
            && 32U <= options.limits.max_value_bytes) {
            const MetaValue v = make_sony_deciphered_bytes(store.arena(), bytes,
                                                           0x17D0, 32, rounds);
            if (v.kind == MetaValueKind::Bytes) {
                tags_out[out_count] = 0x17D0;
                vals_out[out_count] = v;
                out_count += 1;
            }
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void decode_sony_tag940e(std::span<const std::byte> bytes,
                                    std::string_view mk_prefix,
                                    MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "tag940e", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        // Tag940e contains the metering image; width/height are u8s at 0x1A06/0x1A07.
        // Data is commonly ciphered with the Sony substitution cipher.
        uint8_t w       = 0;
        uint8_t h       = 0;
        uint32_t rounds = 1;
        bool ok         = sony_read_u8(bytes, 0x1A06, 1, &w)
                  && sony_read_u8(bytes, 0x1A07, 1, &h);
        if (!ok || w == 0 || h == 0) {
            rounds = 2;
            ok     = sony_read_u8(bytes, 0x1A06, 2, &w)
                 && sony_read_u8(bytes, 0x1A07, 2, &h);
        }
        if (!ok || w == 0 || h == 0) {
            return;
        }

        const uint32_t image_bytes = uint32_t(w) * uint32_t(h) * 2U;
        if (image_bytes == 0U || image_bytes > options.limits.max_value_bytes) {
            return;
        }
        if (0x1A08 + uint64_t(image_bytes) > bytes.size()) {
            return;
        }

        const MetaValue img = make_sony_deciphered_bytes(store.arena(), bytes,
                                                         0x1A08, image_bytes,
                                                         rounds);
        if (img.kind != MetaValueKind::Bytes) {
            return;
        }

        const uint16_t tags_out[]  = { 0x1A06, 0x1A07, 0x1A08 };
        const MetaValue vals_out[] = { make_u8(w), make_u8(h), img };
        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out),
                             std::span<const MetaValue>(vals_out),
                             options.limits, status_out);
    }


    static void decode_sony_tag9050b(std::span<const std::byte> bytes,
                                     std::string_view mk_prefix,
                                     MetaStore& store,
                                     const ExifDecodeOptions& options,
                                     ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "tag9050b", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        const uint32_t rounds = 1;

        uint16_t tags_out[64];
        MetaValue vals_out[64];
        uint32_t out_count = 0;

        // u8 scalars.
        const uint16_t u8_tags[] = { 0x0000, 0x0001, 0x0039, 0x004B, 0x006B,
                                     0x006D, 0x0073, 0x0105, 0x0106, 0x010B,
                                     0x0114, 0x01EB, 0x01EE, 0x021A };
        for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
            uint8_t v        = 0;
            const uint16_t t = u8_tags[i];
            if (!sony_read_u8(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u8(v);
            out_count += 1;
        }

        // u16 scalars.
        const uint16_t u16_tags[] = { 0x0046, 0x0048 };
        for (uint32_t i = 0; i < sizeof(u16_tags) / sizeof(u16_tags[0]); ++i) {
            uint16_t v       = 0;
            const uint16_t t = u16_tags[i];
            if (!sony_read_u16le(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u16(v);
            out_count += 1;
        }

        // u16[3] shutter.
        {
            std::array<uint16_t, 3> v {};
            bool ok = true;
            for (uint32_t i = 0; i < v.size(); ++i) {
                if (!sony_read_u16le(bytes, 0x0026 + i * 2U, rounds, &v[i])) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                tags_out[out_count] = 0x0026;
                vals_out[out_count]
                    = make_u16_array(store.arena(),
                                     std::span<const uint16_t>(v));
                out_count += 1;
            }
        }

        // u32 counters.
        const uint16_t u32_tags[] = { 0x003A, 0x0050, 0x0052, 0x0058,
                                      0x019F, 0x01CB, 0x01CD };
        for (uint32_t i = 0; i < sizeof(u32_tags) / sizeof(u32_tags[0]); ++i) {
            uint32_t v       = 0;
            const uint16_t t = u32_tags[i];
            if (!sony_read_u32le(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u32(v);
            out_count += 1;
        }

        // LensType2/LensType (int16u).
        const uint16_t lens_u16_tags[] = { 0x0107, 0x0109 };
        for (uint32_t i = 0;
             i < sizeof(lens_u16_tags) / sizeof(lens_u16_tags[0]); ++i) {
            uint16_t v       = 0;
            const uint16_t t = lens_u16_tags[i];
            if (!sony_read_u16le(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u16(v);
            out_count += 1;
        }

        // SonyTimeMinSec (2 raw bytes).
        if (0x0061 + 2U <= bytes.size()) {
            uint8_t b0 = 0;
            uint8_t b1 = 0;
            if (sony_read_u8(bytes, 0x0061, rounds, &b0)
                && sony_read_u8(bytes, 0x0062, rounds, &b1)) {
                const uint8_t tmp[] = { b0, b1 };
                tags_out[out_count] = 0x0061;
                vals_out[out_count]
                    = make_u8_array(store.arena(),
                                    std::span<const uint8_t>(tmp, 2));
                out_count += 1;
            }
        }

        // InternalSerialNumber (6 bytes).
        if (0x0088 + 6U <= bytes.size()) {
            uint8_t tmp[6];
            bool ok = true;
            for (uint32_t i = 0; i < 6; ++i) {
                ok = ok && sony_read_u8(bytes, 0x0088 + i, rounds, &tmp[i]);
            }
            if (ok) {
                tags_out[out_count] = 0x0088;
                vals_out[out_count]
                    = make_u8_array(store.arena(),
                                    std::span<const uint8_t>(tmp, 6));
                out_count += 1;
            }
        }

        // LensSpecFeatures (undef[2]) at a handful of known offsets.
        const uint16_t spec_offsets[] = { 0x0116, 0x01ED, 0x01F0, 0x021C,
                                          0x021E };
        for (uint32_t i = 0; i < sizeof(spec_offsets) / sizeof(spec_offsets[0]);
             ++i) {
            const uint16_t t = spec_offsets[i];
            if (t + 2U > bytes.size()) {
                continue;
            }
            uint8_t b0 = 0;
            uint8_t b1 = 0;
            if (!sony_read_u8(bytes, t + 0, rounds, &b0)
                || !sony_read_u8(bytes, t + 1, rounds, &b1)) {
                continue;
            }
            const uint8_t tmp[] = { b0, b1 };
            tags_out[out_count] = t;
            vals_out[out_count]
                = make_u8_array(store.arena(),
                                std::span<const uint8_t>(tmp, 2));
            out_count += 1;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void decode_sony_tag9050c(std::span<const std::byte> bytes,
                                     std::string_view mk_prefix,
                                     MetaStore& store,
                                     const ExifDecodeOptions& options,
                                     ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "tag9050c", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        const uint32_t rounds = 1;

        uint16_t tags_out[16];
        MetaValue vals_out[16];
        uint32_t out_count = 0;

        // u16[3] shutter.
        {
            std::array<uint16_t, 3> v {};
            bool ok = true;
            for (uint32_t i = 0; i < v.size(); ++i) {
                if (!sony_read_u16le(bytes, 0x0026 + i * 2U, rounds, &v[i])) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                tags_out[out_count] = 0x0026;
                vals_out[out_count]
                    = make_u16_array(store.arena(),
                                     std::span<const uint16_t>(v));
                out_count += 1;
            }
        }

        // u8 scalars.
        const uint16_t u8_tags[] = { 0x0039, 0x004B, 0x006B };
        for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
            uint8_t v        = 0;
            const uint16_t t = u8_tags[i];
            if (!sony_read_u8(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u8(v);
            out_count += 1;
        }

        // u16 scalars.
        const uint16_t u16_tags[] = { 0x0046, 0x0048, 0x0066, 0x0068 };
        for (uint32_t i = 0; i < sizeof(u16_tags) / sizeof(u16_tags[0]); ++i) {
            uint16_t v       = 0;
            const uint16_t t = u16_tags[i];
            if (!sony_read_u16le(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u16(v);
            out_count += 1;
        }

        // u32 counters.
        const uint16_t u32_tags[] = { 0x003A, 0x0050 };
        for (uint32_t i = 0; i < sizeof(u32_tags) / sizeof(u32_tags[0]); ++i) {
            uint32_t v       = 0;
            const uint16_t t = u32_tags[i];
            if (!sony_read_u32le(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u32(v);
            out_count += 1;
        }

        // InternalSerialNumber (6 bytes).
        if (0x0088 + 6U <= bytes.size()) {
            uint8_t tmp[6];
            bool ok = true;
            for (uint32_t i = 0; i < 6; ++i) {
                ok = ok && sony_read_u8(bytes, 0x0088 + i, rounds, &tmp[i]);
            }
            if (ok) {
                tags_out[out_count] = 0x0088;
                vals_out[out_count]
                    = make_u8_array(store.arena(),
                                    std::span<const uint8_t>(tmp, 6));
                out_count += 1;
            }
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void decode_sony_tag9405b(std::span<const std::byte> bytes,
                                     std::string_view mk_prefix,
                                     MetaStore& store,
                                     const ExifDecodeOptions& options,
                                     ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "tag9405b", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        const uint32_t rounds = 1;

        uint16_t tags_out[128];
        MetaValue vals_out[128];
        uint32_t out_count = 0;

        uint16_t u16v             = 0;
        const uint16_t u16_tags[] = { 0x0004, 0x0006, 0x000A, 0x000E, 0x0014,
                                      0x0016, 0x003E, 0x0040, 0x0342, 0x034E };
        for (uint32_t i = 0; i < sizeof(u16_tags) / sizeof(u16_tags[0]); ++i) {
            const uint16_t t = u16_tags[i];
            if (!sony_read_u16le(bytes, t, rounds, &u16v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u16(u16v);
            out_count += 1;
        }

        uint8_t u8v              = 0;
        const uint16_t u8_tags[] = { 0x0034, 0x0042, 0x0044, 0x0046,
                                     0x0048, 0x004A, 0x0052, 0x005A,
                                     0x005B, 0x005D, 0x005E };
        for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
            const uint16_t t = u8_tags[i];
            if (!sony_read_u8(bytes, t, rounds, &u8v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u8(u8v);
            out_count += 1;
        }

        // ExposureTime rational32u at 0x0010.
        {
            uint32_t numer = 0;
            uint32_t denom = 0;
            if (sony_read_u32le(bytes, 0x0010, rounds, &numer)
                && sony_read_u32le(bytes, 0x0010 + 4U, rounds, &denom)) {
                tags_out[out_count] = 0x0010;
                vals_out[out_count] = make_urational(numer, denom);
                out_count += 1;
            }
        }

        // SequenceImageNumber (int32u) at 0x0024.
        {
            uint32_t v = 0;
            if (sony_read_u32le(bytes, 0x0024, rounds, &v)) {
                tags_out[out_count] = 0x0024;
                vals_out[out_count] = make_u32(v);
                out_count += 1;
            }
        }

        // LensMount (u8) at 0x005e already decoded above.

        // LensType2/LensType (int16u) at 0x0060/0x0062.
        const uint16_t lens_u16_tags[] = { 0x0060, 0x0062 };
        for (uint32_t i = 0;
             i < sizeof(lens_u16_tags) / sizeof(lens_u16_tags[0]); ++i) {
            const uint16_t t = lens_u16_tags[i];
            uint16_t v       = 0;
            if (!sony_read_u16le(bytes, t, rounds, &v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u16(v);
            out_count += 1;
        }

        // DistortionCorrParams int16s[16] at 0x0064.
        {
            std::array<int16_t, 16> v {};
            bool ok = true;
            for (uint32_t i = 0; i < v.size(); ++i) {
                ok = ok
                     && sony_read_i16le(bytes, 0x0064 + i * 2U, rounds, &v[i]);
            }
            if (ok) {
                tags_out[out_count] = 0x0064;
                vals_out[out_count]
                    = make_i16_array(store.arena(),
                                     std::span<const int16_t>(v));
                out_count += 1;
            }
        }

        // VignettingCorrParams int16s[16].
        const uint16_t vign_tags[] = { 0x034A, 0x0350, 0x035C, 0x0368 };
        for (uint32_t ti = 0; ti < sizeof(vign_tags) / sizeof(vign_tags[0]);
             ++ti) {
            const uint16_t t = vign_tags[ti];
            std::array<int16_t, 16> v {};
            bool ok = true;
            for (uint32_t i = 0; i < v.size(); ++i) {
                ok = ok && sony_read_i16le(bytes, t + i * 2U, rounds, &v[i]);
            }
            if (!ok) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_i16_array(store.arena(),
                                                 std::span<const int16_t>(v));
            out_count += 1;
        }

        // ChromaticAberrationCorrParams int16s[32].
        const uint16_t ca_tags[] = { 0x037C, 0x0384, 0x039C, 0x03B0, 0x03B8 };
        for (uint32_t ti = 0; ti < sizeof(ca_tags) / sizeof(ca_tags[0]); ++ti) {
            const uint16_t t = ca_tags[ti];
            std::array<int16_t, 32> v {};
            bool ok = true;
            for (uint32_t i = 0; i < v.size(); ++i) {
                ok = ok && sony_read_i16le(bytes, t + i * 2U, rounds, &v[i]);
            }
            if (!ok) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_i16_array(store.arena(),
                                                 std::span<const int16_t>(v));
            out_count += 1;
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void decode_sony_tag9416(std::span<const std::byte> bytes,
                                    std::string_view mk_prefix,
                                    MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (bytes.empty()) {
            return;
        }

        char sub_ifd_buf[96];
        const std::string_view ifd_name
            = make_mk_subtable_ifd_token(mk_prefix, "tag9416", 0,
                                         std::span<char>(sub_ifd_buf));
        if (ifd_name.empty()) {
            return;
        }

        const uint8_t allowed_versions[] = { 0x06, 0x07, 0x08, 0x09, 0x0C, 0x0D,
                                             0x0F, 0x10, 0x11, 0x17, 0x1B };
        const uint32_t rounds            = sony_guess_cipher_rounds(
            bytes, 0x0000, std::span<const uint8_t>(allowed_versions));

        uint16_t tags_out[128];
        MetaValue vals_out[128];
        uint32_t out_count = 0;

        // u8 tags.
        uint8_t u8v              = 0;
        const uint16_t u8_tags[] = { 0x0000, 0x002B, 0x0035, 0x0037,
                                     0x0048, 0x0049, 0x004A, 0x0070 };
        for (uint32_t i = 0; i < sizeof(u8_tags) / sizeof(u8_tags[0]); ++i) {
            const uint16_t t = u8_tags[i];
            if (!sony_read_u8(bytes, t, rounds, &u8v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u8(u8v);
            out_count += 1;
        }

        // u16 tags.
        uint16_t u16v             = 0;
        const uint16_t u16_tags[] = { 0x0004, 0x0006, 0x000A, 0x0010, 0x0012,
                                      0x004B, 0x0071, 0x0073, 0x0075 };
        for (uint32_t i = 0; i < sizeof(u16_tags) / sizeof(u16_tags[0]); ++i) {
            const uint16_t t = u16_tags[i];
            if (!sony_read_u16le(bytes, t, rounds, &u16v)) {
                continue;
            }
            tags_out[out_count] = t;
            vals_out[out_count] = make_u16(u16v);
            out_count += 1;
        }

        // SequenceImageNumber int32u at 0x001d.
        {
            uint32_t v = 0;
            if (sony_read_u32le(bytes, 0x001D, rounds, &v)) {
                tags_out[out_count] = 0x001D;
                vals_out[out_count] = make_u32(v);
                out_count += 1;
            }
        }

        // ExposureTime rational32u at 0x000c.
        {
            uint32_t numer = 0;
            uint32_t denom = 0;
            if (sony_read_u32le(bytes, 0x000C, rounds, &numer)
                && sony_read_u32le(bytes, 0x000C + 4U, rounds, &denom)) {
                tags_out[out_count] = 0x000C;
                vals_out[out_count] = make_urational(numer, denom);
                out_count += 1;
            }
        }

        // DistortionCorrParams int16s[16] at 0x004f.
        {
            std::array<int16_t, 16> v {};
            bool ok = true;
            for (uint32_t i = 0; i < v.size(); ++i) {
                ok = ok
                     && sony_read_i16le(bytes, 0x004F + i * 2U, rounds, &v[i]);
            }
            if (ok) {
                tags_out[out_count] = 0x004F;
                vals_out[out_count]
                    = make_i16_array(store.arena(),
                                     std::span<const int16_t>(v));
                out_count += 1;
            }
        }

        // VignettingCorrParams int16s[32] at 0x089d.
        {
            std::array<int16_t, 32> v {};
            bool ok = true;
            for (uint32_t i = 0; i < v.size(); ++i) {
                ok = ok
                     && sony_read_i16le(bytes, 0x089D + i * 2U, rounds, &v[i]);
            }
            if (ok) {
                tags_out[out_count] = 0x089D;
                vals_out[out_count]
                    = make_i16_array(store.arena(),
                                     std::span<const int16_t>(v));
                out_count += 1;
            }
        }

        // APS-CSizeCapture (u8) at 0x08e5.
        if (sony_read_u8(bytes, 0x08E5, rounds, &u8v)) {
            tags_out[out_count] = 0x08E5;
            vals_out[out_count] = make_u8(u8v);
            out_count += 1;
        }

        // ChromaticAberrationCorrParams int16s[32] at 0x0945.
        {
            std::array<int16_t, 32> v {};
            bool ok = true;
            for (uint32_t i = 0; i < v.size(); ++i) {
                ok = ok
                     && sony_read_i16le(bytes, 0x0945 + i * 2U, rounds, &v[i]);
            }
            if (ok) {
                tags_out[out_count] = 0x0945;
                vals_out[out_count]
                    = make_i16_array(store.arena(),
                                     std::span<const int16_t>(v));
                out_count += 1;
            }
        }

        emit_bin_dir_entries(ifd_name, store,
                             std::span<const uint16_t>(tags_out, out_count),
                             std::span<const MetaValue>(vals_out, out_count),
                             options.limits, status_out);
    }


    static void
    decode_sony_cipher_subdirs(std::string_view mk_ifd0, MetaStore& store,
                               const ExifDecodeOptions& options,
                               ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty()) {
            return;
        }

        struct Candidate final {
            uint16_t tag = 0;
            MetaValue value;
        };

        Candidate cands[16];
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
            if (e.value.kind != MetaValueKind::Bytes) {
                continue;
            }
            const uint16_t tag = e.key.data.exif_tag.tag;
            switch (tag) {
            case 0x9050:  // Tag9050*
            case 0x3000:  // ShotInfo
            case 0x9400:  // Tag9400*
            case 0x9401:  // Tag9401 (ISOInfo)
            case 0x9402:  // Tag9402
            case 0x9403:  // Tag9403
            case 0x9404:  // Tag9404*
            case 0x9405:  // Tag9405*
            case 0x9406:  // Tag9406*
            case 0x940C:  // Tag940c
            case 0x940E:  // Tag940e
            case 0x9416:  // Tag9416
            case 0x2010:  // Tag2010*
            case 0x202A:  // Tag202a
                break;
            default: continue;
            }
            if (cand_count < sizeof(cands) / sizeof(cands[0])) {
                cands[cand_count].tag   = tag;
                cands[cand_count].value = e.value;
                cand_count += 1;
            }
        }

        const std::string_view mk_prefix = "mk_sony";
        const std::string_view model
            = find_first_exif_text_value(store, "ifd0", 0x0110 /* Model */);

        for (uint32_t i = 0; i < cand_count; ++i) {
            const uint16_t tag                   = cands[i].tag;
            const ByteSpan raw_span              = cands[i].value.data.span;
            const std::span<const std::byte> raw = store.arena().span(raw_span);
            if (raw.empty()) {
                continue;
            }

            if (tag == 0x3000) {
                decode_sony_shotinfo_from_tag3000(raw, mk_prefix, store,
                                                  options, status_out);
                continue;
            }
            if (tag == 0x2010) {
                decode_sony_tag2010i(raw, mk_prefix, store, options,
                                     status_out);
                decode_sony_meterinfo9_from_tag2010(raw, mk_prefix, store,
                                                    options, status_out);
                continue;
            }
            if (tag == 0x202A) {
                decode_sony_tag202a(raw, mk_prefix, store, options, status_out);
                continue;
            }
            if (tag == 0x9404) {
                decode_sony_tag9404(raw, mk_prefix, store, options, status_out);
                continue;
            }
            if (tag == 0x940E) {
                decode_sony_tag940e(raw, mk_prefix, store, options, status_out);
                continue;
            }
            if (tag == 0x9400) {
                decode_sony_tag9400(raw, mk_prefix, store, options, status_out);
                continue;
            }
            if (tag == 0x9401) {
                decode_sony_isoinfo_from_tag9401(raw, mk_prefix, store, options,
                                                 status_out);
                continue;
            }
            if (tag == 0x9402) {
                decode_sony_tag9402(raw, mk_prefix, store, options, status_out);
                continue;
            }
            if (tag == 0x9403) {
                decode_sony_tag9403(raw, mk_prefix, store, options, status_out);
                continue;
            }
            if (tag == 0x9406) {
                decode_sony_tag9406(raw, mk_prefix, store, options, status_out);
                continue;
            }
            if (tag == 0x940C) {
                decode_sony_tag940c(raw, mk_prefix, store, options, status_out);
                continue;
            }
            if (tag == 0x9405) {
                // Best-effort: Tag9405b is common for ILCE-7M3/7RM3/7RM4 etc.
                decode_sony_tag9405b(raw, mk_prefix, store, options,
                                     status_out);
                continue;
            }
            if (tag == 0x9416) {
                decode_sony_tag9416(raw, mk_prefix, store, options, status_out);
                continue;
            }
            if (tag == 0x9050) {
                // Tag9050 variant depends on camera generation.
                if (model.find("7RM5") != std::string_view::npos
                    || model.find("7M4") != std::string_view::npos
                    || model.find("7SM3") != std::string_view::npos
                    || model.starts_with("ILCE-1")
                    || model.starts_with("ILME-")) {
                    decode_sony_tag9050c(raw, mk_prefix, store, options,
                                         status_out);
                } else {
                    decode_sony_tag9050b(raw, mk_prefix, store, options,
                                         status_out);
                }
                continue;
            }
        }
    }


    static bool decode_canon_afinfo2(const TiffConfig& cfg,
                                     std::span<const std::byte> tiff_bytes,
                                     uint64_t value_off, uint64_t value_bytes,
                                     std::string_view mk_ifd0, MetaStore& store,
                                     const ExifDecodeOptions& options,
                                     ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty()) {
            return false;
        }
        if (value_bytes < 16) {
            return false;
        }
        if (value_off + value_bytes > tiff_bytes.size()) {
            return false;
        }
        if ((value_bytes % 2U) != 0U) {
            return false;
        }

        const uint32_t word_count = static_cast<uint32_t>(value_bytes / 2U);
        if (word_count < 10) {
            return false;
        }

        uint16_t size_bytes = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, value_off + 0, &size_bytes)) {
            return false;
        }
        if (size_bytes != value_bytes) {
            return false;
        }

        uint16_t num_points = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, value_off + 2U * 2U, &num_points)) {
            return false;
        }
        if (num_points == 0 || num_points > 256) {
            return false;
        }

        const uint32_t needed_words = 1U + 7U + 4U * uint32_t(num_points) + 3U;
        if (word_count < needed_words) {
            return false;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return true;
        }

        // CanonAFInfo2 layout (word offsets):
        // [0]=size(bytes), [1]=AFAreaMode, [2]=NumAFPoints, [3]=ValidAFPoints,
        // [4..7]=image dimensions, then 4 arrays of length NumAFPoints,
        // then three scalar fields.
        uint32_t order = 0;
        if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, block, order++,
                                                 0x0000, 0, store, options,
                                                 status_out)) {
            return true;
        }
        if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, block, order++,
                                                 0x0001, 1, store, options,
                                                 status_out)) {
            return true;
        }
        if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, block, order++,
                                                 0x0002, 2, store, options,
                                                 status_out)) {
            return true;
        }
        if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, block, order++,
                                                 0x0003, 3, store, options,
                                                 status_out)) {
            return true;
        }
        if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, block, order++,
                                                 0x0004, 4, store, options,
                                                 status_out)) {
            return true;
        }
        if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, block, order++,
                                                 0x0005, 5, store, options,
                                                 status_out)) {
            return true;
        }
        if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, block, order++,
                                                 0x0006, 6, store, options,
                                                 status_out)) {
            return true;
        }
        if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, block, order++,
                                                 0x0007, 7, store, options,
                                                 status_out)) {
            return true;
        }

        const uint32_t base = 8U;
        const uint32_t n    = uint32_t(num_points);

        struct ArrSpec final {
            uint16_t tag   = 0;
            uint16_t type  = 0;
            uint32_t words = 0;
        };
        const ArrSpec arrays[4] = {
            { 0x0008, 3, base + 0U * n },  // widths
            { 0x0009, 3, base + 1U * n },  // heights
            { 0x000a, 8, base + 2U * n },  // x positions (signed)
            { 0x000b, 8, base + 3U * n },  // y positions (signed)
        };

        for (uint32_t i = 0; i < 4; ++i) {
            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return true;
            }

            const ArrSpec& a     = arrays[i];
            const uint64_t off   = value_off + uint64_t(a.words) * 2ULL;
            const uint64_t bytes = uint64_t(n) * 2ULL;
            if (off + bytes > tiff_bytes.size()) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return true;
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), mk_ifd0, a.tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = order++;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, a.type };
            entry.origin.wire_count     = n;
            entry.value = decode_tiff_value(cfg, tiff_bytes, a.type, n, off,
                                            bytes, store.arena(),
                                            options.limits, status_out);
            entry.flags |= EntryFlags::Derived;
            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }

        if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, block, order++,
                                                 0x000c, base + 4U * n + 0U,
                                                 store, options, status_out)) {
            return true;
        }
        if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, block, order++,
                                                 0x000d, base + 4U * n + 1U,
                                                 store, options, status_out)) {
            return true;
        }
        if (!decode_canon_afinfo2_add_u16_scalar(cfg, tiff_bytes, value_off,
                                                 mk_ifd0, block, order++,
                                                 0x000e, base + 4U * n + 2U,
                                                 store, options, status_out)) {
            return true;
        }

        return true;
    }


    static bool decode_canon_custom_functions2(
        const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
        uint64_t value_off, uint64_t value_bytes, std::string_view mk_ifd0,
        MetaStore& store, const ExifDecodeOptions& options,
        ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty()) {
            return false;
        }
        if (value_bytes < 8) {
            return false;
        }
        if (value_off + value_bytes > tiff_bytes.size()) {
            return false;
        }

        uint16_t len16 = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, value_off + 0, &len16)) {
            return false;
        }
        if (len16 != value_bytes) {
            return false;
        }

        uint32_t group_count = 0;
        if (!read_tiff_u32(cfg, tiff_bytes, value_off + 4, &group_count)) {
            return false;
        }
        (void)group_count;

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return true;
        }

        const uint64_t end = value_off + value_bytes;
        uint64_t pos       = value_off + 8;
        uint32_t order     = 0;

        while (pos + 12 <= end) {
            uint32_t rec_num   = 0;
            uint32_t rec_len   = 0;
            uint32_t rec_count = 0;
            if (!read_tiff_u32(cfg, tiff_bytes, pos + 0, &rec_num)
                || !read_tiff_u32(cfg, tiff_bytes, pos + 4, &rec_len)
                || !read_tiff_u32(cfg, tiff_bytes, pos + 8, &rec_count)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return true;
            }
            (void)rec_num;

            if (rec_len < 8) {
                break;
            }

            pos += 12;
            const uint64_t rec_end = pos + uint64_t(rec_len) - 8ULL;
            if (rec_end > end) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return true;
            }

            uint64_t rec_pos = pos;
            uint32_t i       = 0;
            for (; rec_pos + 8 <= rec_end && i < rec_count; ++i) {
                uint32_t tag32 = 0;
                uint32_t num   = 0;
                if (!read_tiff_u32(cfg, tiff_bytes, rec_pos + 0, &tag32)
                    || !read_tiff_u32(cfg, tiff_bytes, rec_pos + 4, &num)) {
                    if (status_out) {
                        update_status(status_out, ExifDecodeStatus::Malformed);
                    }
                    return true;
                }
                if (tag32 > 0xFFFFu) {
                    // OpenMeta uses 16-bit EXIF tag ids. Skip unknown/extended ids.
                    break;
                }
                if (num == 0) {
                    break;
                }
                if (num > options.limits.max_entries_per_ifd) {
                    if (status_out) {
                        update_status(status_out,
                                      ExifDecodeStatus::LimitExceeded);
                    }
                    break;
                }

                const uint64_t payload_bytes = uint64_t(num) * 4ULL;
                if (payload_bytes > options.limits.max_value_bytes) {
                    if (status_out) {
                        update_status(status_out,
                                      ExifDecodeStatus::LimitExceeded);
                    }
                    break;
                }

                const uint64_t payload_off = rec_pos + 8;
                const uint64_t next        = payload_off + payload_bytes;
                if (next > rec_end) {
                    break;
                }

                if (status_out
                    && (status_out->entries_decoded + 1U)
                           > options.limits.max_total_entries) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                    return true;
                }

                Entry entry;
                entry.key          = make_exif_tag_key(store.arena(), mk_ifd0,
                                                       static_cast<uint16_t>(tag32));
                entry.origin.block = block;
                entry.origin.order_in_block = order++;
                entry.origin.wire_type      = WireType { WireFamily::Other, 4 };
                entry.origin.wire_count     = num;
                entry.flags |= EntryFlags::Derived;

                if (num == 1) {
                    uint32_t v = 0;
                    if (!read_tiff_u32(cfg, tiff_bytes, payload_off, &v)) {
                        if (status_out) {
                            update_status(status_out,
                                          ExifDecodeStatus::Malformed);
                        }
                        return true;
                    }
                    entry.value = make_u32(v);
                } else {
                    ByteArena& arena       = store.arena();
                    const uint64_t bytes64 = payload_bytes;
                    if (bytes64 > UINT32_MAX) {
                        if (status_out) {
                            update_status(status_out,
                                          ExifDecodeStatus::LimitExceeded);
                        }
                        return true;
                    }
                    const ByteSpan span = arena.allocate(
                        static_cast<uint32_t>(bytes64),
                        static_cast<uint32_t>(alignof(uint32_t)));
                    std::span<std::byte> out = arena.span_mut(span);
                    if (out.size() != bytes64) {
                        if (status_out) {
                            update_status(status_out,
                                          ExifDecodeStatus::LimitExceeded);
                        }
                        return true;
                    }

                    for (uint32_t k = 0; k < num; ++k) {
                        uint32_t v = 0;
                        if (!read_tiff_u32(cfg, tiff_bytes,
                                           payload_off + uint64_t(k) * 4ULL,
                                           &v)) {
                            if (status_out) {
                                update_status(status_out,
                                              ExifDecodeStatus::Malformed);
                            }
                            return true;
                        }
                        std::memcpy(out.data() + size_t(k) * 4U, &v, 4U);
                    }

                    MetaValue v;
                    v.kind      = MetaValueKind::Array;
                    v.elem_type = MetaElementType::U32;
                    v.count     = num;
                    v.data.span = span;
                    entry.value = v;
                }

                (void)store.add_entry(entry);
                if (status_out) {
                    status_out->entries_decoded += 1;
                }

                rec_pos = next;
            }

            pos = rec_end;
        }

        return true;
    }


    static bool decode_canon_makernote(
        const TiffConfig& cfg, std::span<const std::byte> tiff_bytes,
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
        if (!looks_like_classic_ifd(cfg, tiff_bytes, maker_note_off,
                                    options.limits)) {
            return false;
        }

        uint16_t entry_count = 0;
        if (!read_tiff_u16(cfg, tiff_bytes, maker_note_off, &entry_count)) {
            return false;
        }
        if (entry_count == 0
            || entry_count > options.limits.max_entries_per_ifd) {
            return false;
        }
        const uint64_t entries_off = maker_note_off + 2;

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return true;
        }

        for (uint32_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            uint16_t tag  = 0;
            uint16_t type = 0;
            if (!read_tiff_u16(cfg, tiff_bytes, eoff + 0, &tag)
                || !read_tiff_u16(cfg, tiff_bytes, eoff + 2, &type)) {
                return true;
            }

            uint32_t count32        = 0;
            uint32_t value_or_off32 = 0;
            if (!read_tiff_u32(cfg, tiff_bytes, eoff + 4, &count32)
                || !read_tiff_u32(cfg, tiff_bytes, eoff + 8, &value_or_off32)) {
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
            if (value_bytes > options.limits.max_value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                continue;
            }

            const uint64_t inline_cap      = 4;
            const uint64_t value_field_off = eoff + 8;
            const uint64_t abs_value_off   = (value_bytes <= inline_cap)
                                                 ? value_field_off
                                                 : value_or_off32;

            if (abs_value_off + value_bytes > tiff_bytes.size()) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                continue;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return true;
            }

            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), mk_ifd0, tag);
            entry.origin.block = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
            entry.origin.wire_count     = static_cast<uint32_t>(count);
            entry.value = decode_tiff_value(cfg, tiff_bytes, type, count,
                                            abs_value_off, value_bytes,
                                            store.arena(), options.limits,
                                            status_out);

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }

            // Decode common Canon BinaryData subdirectories into derived blocks.
            // The raw MakerNote entries are always preserved in mk_canon0.
            char sub_ifd_buf[96];
            const std::string_view mk_prefix = "mk_canon";

            // CanonCameraInfo* (tag 0x000d) often contains an embedded TIFF-like
            // IFD stream describing a "CameraInfo" block. Best-effort: locate a
            // plausible classic IFD and decode it into mk_canon_camerainfo_0.
            if (tag == 0x000d && type == 7 && value_bytes != 0U) {
                const std::span<const std::byte> cam
                    = tiff_bytes.subspan(static_cast<size_t>(abs_value_off),
                                         static_cast<size_t>(value_bytes));
                ClassicIfdCandidate best;
                if (find_best_classic_ifd_candidate(cam, 512, options.limits,
                                                    &best)) {
                    TiffConfig cam_cfg;
                    cam_cfg.le      = best.le;
                    cam_cfg.bigtiff = false;

                    const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                        mk_prefix, "camerainfo", 0,
                        std::span<char>(sub_ifd_buf));
                    decode_classic_ifd_no_header(cam_cfg, cam, best.offset,
                                                 sub_ifd, store, options,
                                                 status_out,
                                                 EntryFlags::Derived);
                }
            }

            // CanonCameraInfo* blobs (tag 0x000d) may embed a PictureStyleInfo
            // table at a fixed offset for some models. Best-effort: decode a
            // psinfo table from the tail starting at 0x025b.
            if (tag == 0x000d && type == 7 && value_bytes > 0x025b) {
                const uint64_t ps_off   = abs_value_off + 0x025b;
                const uint64_t ps_bytes = value_bytes - 0x025b;
                if (ps_bytes >= 0x00dc + 2U
                    && ps_off + ps_bytes <= tiff_bytes.size()) {
                    const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                        mk_prefix, "psinfo", 0, std::span<char>(sub_ifd_buf));
                    decode_canon_psinfo_table(tiff_bytes, ps_off, ps_bytes,
                                              sub_ifd, store, options,
                                              status_out);
                }
            }

            if (tag == 0x0099 && value_bytes != 0U) {  // CustomFunctions2
                char canoncustom_ifd_buf[96];
                const std::string_view canoncustom_ifd
                    = make_mk_subtable_ifd_token("mk_canoncustom", "functions2",
                                                 0,
                                                 std::span<char>(
                                                     canoncustom_ifd_buf));
                (void)decode_canon_custom_functions2(cfg, tiff_bytes,
                                                     abs_value_off, value_bytes,
                                                     canoncustom_ifd, store,
                                                     options, status_out);
            }

            if (tag == 0x4011 && type == 7 && value_bytes >= 2U
                && (value_bytes % 2U) == 0U) {
                const uint32_t count16 = static_cast<uint32_t>(value_bytes
                                                               / 2U);
                const std::string_view sub_ifd
                    = make_mk_subtable_ifd_token(mk_prefix, "vignettingcorr", 0,
                                                 std::span<char>(sub_ifd_buf));
                decode_canon_u16_table(cfg, tiff_bytes, abs_value_off, count16,
                                       sub_ifd, store, options, status_out);
            }

            if (type == 3 && count32 != 0) {  // SHORT
                if (tag == 0x0001) {          // CanonCameraSettings
                    const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                        mk_prefix, "camerasettings", 0,
                        std::span<char>(sub_ifd_buf));
                    decode_canon_u16_table(cfg, tiff_bytes, abs_value_off,
                                           count32, sub_ifd, store, options,
                                           status_out);
                } else if (tag == 0x0026) {  // CanonAFInfo2
                    const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                        mk_prefix, "afinfo2", 0, std::span<char>(sub_ifd_buf));
                    (void)decode_canon_afinfo2(cfg, tiff_bytes, abs_value_off,
                                               value_bytes, sub_ifd, store,
                                               options, status_out);
                } else if (tag == 0x0002) {  // CanonFocalLength
                    const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                        mk_prefix, "focallength", 0,
                        std::span<char>(sub_ifd_buf));
                    decode_canon_u16_table(cfg, tiff_bytes, abs_value_off,
                                           count32, sub_ifd, store, options,
                                           status_out);
                } else if (tag == 0x0004) {  // CanonShotInfo
                    const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                        mk_prefix, "shotinfo", 0, std::span<char>(sub_ifd_buf));
                    decode_canon_u16_table(cfg, tiff_bytes, abs_value_off,
                                           count32, sub_ifd, store, options,
                                           status_out);
                } else if (tag == 0x0093) {  // CanonFileInfo
                    const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                        mk_prefix, "fileinfo", 0, std::span<char>(sub_ifd_buf));
                    decode_canon_u16_table(cfg, tiff_bytes, abs_value_off,
                                           count32, sub_ifd, store, options,
                                           status_out);
                } else if (tag == 0x0098) {  // CropInfo
                    const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                        mk_prefix, "cropinfo", 0, std::span<char>(sub_ifd_buf));
                    decode_canon_u16_table(cfg, tiff_bytes, abs_value_off,
                                           count32, sub_ifd, store, options,
                                           status_out);
                } else if (tag == 0x00A0) {  // ProcessingInfo
                    const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                        mk_prefix, "processing", 0,
                        std::span<char>(sub_ifd_buf));
                    decode_canon_u16_table(cfg, tiff_bytes, abs_value_off,
                                           count32, sub_ifd, store, options,
                                           status_out);
                } else if (tag == 0x4001) {  // ColorData
                    const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                        mk_prefix, "colordata", 0,
                        std::span<char>(sub_ifd_buf));
                    decode_canon_u16_table(cfg, tiff_bytes, abs_value_off,
                                           count32, sub_ifd, store, options,
                                           status_out);
                }
            } else if (type == 4 && count32 != 0) {  // LONG
                if (tag == 0x0035) {                 // TimeInfo
                    const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                        mk_prefix, "timeinfo", 0, std::span<char>(sub_ifd_buf));
                    decode_canon_u32_table(cfg, tiff_bytes, abs_value_off,
                                           count32, sub_ifd, store, options,
                                           status_out);
                } else if (tag == 0x009A) {  // AspectInfo
                    const std::string_view sub_ifd = make_mk_subtable_ifd_token(
                        mk_prefix, "aspectinfo", 0,
                        std::span<char>(sub_ifd_buf));
                    decode_canon_u32_table(cfg, tiff_bytes, abs_value_off,
                                           count32, sub_ifd, store, options,
                                           status_out);
                }
            }
        }

        return true;
    }


    static bool
    decode_pentax_makernote(std::span<const std::byte> maker_note_bytes,
                            std::string_view mk_ifd0, MetaStore& store,
                            const ExifDecodeOptions& options,
                            ExifDecodeResult* status_out) noexcept
    {
        if (maker_note_bytes.size() < 16) {
            return false;
        }
        if (!match_bytes(maker_note_bytes, 0, "AOC\0", 4)) {
            return false;
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
        if (entry_count == 0
            || entry_count > options.limits.max_entries_per_ifd) {
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
            if (value_bytes > options.limits.max_value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                continue;
            }

            const uint64_t inline_cap      = 4;
            const uint64_t value_field_off = eoff + 8;
            const uint64_t value_off       = (value_bytes <= inline_cap)
                                                 ? value_field_off
                                                 : value_or_off32;

            if (value_off + value_bytes > maker_note_bytes.size()) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                continue;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return true;
            }

            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), mk_ifd0, tag);
            entry.origin.block = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
            entry.origin.wire_count     = static_cast<uint32_t>(count);
            entry.value = decode_tiff_value(cfg, maker_note_bytes, type, count,
                                            value_off, value_bytes,
                                            store.arena(), options.limits,
                                            status_out);

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }

        return true;
    }


    static bool
    decode_casio_makernote(std::span<const std::byte> maker_note_bytes,
                           std::string_view mk_ifd0, MetaStore& store,
                           const ExifDecodeOptions& options,
                           ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty()) {
            return false;
        }
        if (maker_note_bytes.size() < 8) {
            return false;
        }
        if (!match_bytes(maker_note_bytes, 0, "QVC\0", 4)) {
            return false;
        }

        uint32_t entry_count32 = 0;
        if (!read_u32be(maker_note_bytes, 4, &entry_count32)) {
            return false;
        }
        const uint64_t entry_count = entry_count32;
        if (entry_count == 0
            || entry_count > options.limits.max_entries_per_ifd) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            return true;
        }

        const uint64_t entries_off = 8;
        if (entry_count > (UINT64_MAX / 12ULL)) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }
        const uint64_t table_bytes = entry_count * 12ULL;
        if (entries_off + table_bytes > maker_note_bytes.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return true;
        }

        TiffConfig cfg;
        cfg.le      = false;
        cfg.bigtiff = false;

        for (uint32_t i = 0; i < entry_count32; ++i) {
            const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

            uint16_t tag  = 0;
            uint16_t type = 0;
            if (!read_u16be(maker_note_bytes, eoff + 0, &tag)
                || !read_u16be(maker_note_bytes, eoff + 2, &type)) {
                return true;
            }

            uint32_t count32        = 0;
            uint32_t value_or_off32 = 0;
            if (!read_u32be(maker_note_bytes, eoff + 4, &count32)
                || !read_u32be(maker_note_bytes, eoff + 8, &value_or_off32)) {
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
            if (value_bytes > options.limits.max_value_bytes) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::LimitExceeded);
                }
                continue;
            }

            const uint64_t inline_cap      = 4;
            const uint64_t value_field_off = eoff + 8;
            const uint64_t value_off       = (value_bytes <= inline_cap)
                                                 ? value_field_off
                                                 : value_or_off32;

            if (value_off + value_bytes > maker_note_bytes.size()) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                continue;
            }

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return true;
            }

            Entry entry;
            entry.key          = make_exif_tag_key(store.arena(), mk_ifd0, tag);
            entry.origin.block = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
            entry.origin.wire_count     = count32;
            entry.value = decode_tiff_value(cfg, maker_note_bytes, type, count,
                                            value_off, value_bytes,
                                            store.arena(), options.limits,
                                            status_out);

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }

        return true;
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
                    && decode_olympus_makernote(cfg, tiff_bytes, value_off,
                                                value_bytes, mk_ifd0, store,
                                                mn_opts, &sink.result)) {
                    continue;
                }

                // Pentax MakerNote: "AOC\0" header + endianness marker +
                // u16 entry count at +6, then classic IFD entries at +8.
                if (vendor == MakerNoteVendor::Pentax
                    && decode_pentax_makernote(mn, mk_ifd0, store, mn_opts,
                                               &sink.result)) {
                    continue;
                }

                // Casio MakerNote type2: "QVC\0" header + big-endian entries.
                if (vendor == MakerNoteVendor::Casio
                    && decode_casio_makernote(mn, mk_ifd0, store, mn_opts,
                                              &sink.result)) {
                    continue;
                }

                // Canon MakerNote: classic IFD at offset 0 (parent endianness),
                // plus Canon-specific BinaryData subdirectories.
                if (vendor == MakerNoteVendor::Canon
                    && decode_canon_makernote(cfg, tiff_bytes, value_off,
                                              value_bytes, mk_ifd0, store,
                                              mn_opts, &sink.result)) {
                    continue;
                }

                // Sony MakerNote: classic IFD located within the blob, but
                // value offsets are commonly relative to the outer EXIF/TIFF.
                if (vendor == MakerNoteVendor::Sony
                    && decode_sony_makernote(cfg, tiff_bytes, value_off,
                                             value_bytes, mk_ifd0, store,
                                             mn_opts, &sink.result)) {
                    decode_sony_cipher_subdirs(mk_ifd0, store, mn_opts,
                                               &sink.result);
                    continue;
                }

                // 1) Embedded TIFF header inside MakerNote (common for Nikon).
                const uint64_t hdr_off = find_embedded_tiff_header(mn, 128);
                if (hdr_off != UINT64_MAX) {
                    std::array<ExifIfdRef, 128> mn_ifds;
                    (void)decode_exif_tiff(
                        mn.subspan(static_cast<size_t>(hdr_off)), store,
                        std::span<ExifIfdRef>(mn_ifds.data(), mn_ifds.size()),
                        mn_opts);

                    if (vendor == MakerNoteVendor::Nikon
                        && (hdr_off + 2U) <= mn.size()) {
                        const uint8_t hdr_b0 = u8(mn[hdr_off + 0]);
                        const uint8_t hdr_b1 = u8(mn[hdr_off + 1]);
                        const bool le        = (hdr_b0 == 'I' && hdr_b1 == 'I');
                        decode_nikon_binary_subdirs(mk_ifd0, store, le, mn_opts,
                                                    &sink.result);
                    }
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
                        decode_sony_cipher_subdirs(mk_ifd0, store, mn_opts,
                                                   &sink.result);
                    }
                    continue;
                }

                // 4) Canon-style MakerNotes: raw IFD starting at offset 0,
                // offsets relative to MakerNote start, using parent endianness.
                decode_classic_ifd_no_header(cfg, mn, 0, mk_ifd0, store,
                                             mn_opts, &sink.result,
                                             EntryFlags::None);
                if (vendor == MakerNoteVendor::Sony) {
                    decode_sony_cipher_subdirs(mk_ifd0, store, mn_opts,
                                               &sink.result);
                }
            }
        }
    }

    return sink.result;
}

}  // namespace openmeta
