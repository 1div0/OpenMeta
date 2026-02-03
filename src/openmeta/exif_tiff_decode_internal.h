#pragma once

#include "openmeta/exif_tiff_decode.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace openmeta {

// Internal-only (non-installed) types for EXIF/TIFF decoding internals.
// This header is used to split vendor MakerNote decoders into separate
// translation units without exposing these helpers as part of the public API.

struct TiffConfig final {
    bool le      = true;
    bool bigtiff = false;
};

struct ClassicIfdCandidate final {
    uint64_t offset        = 0;
    bool le                = true;
    uint16_t entry_count   = 0;
    uint32_t valid_entries = 0;
};

namespace exif_internal {

    constexpr uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
    }

    // Low-level helpers (implemented in exif_tiff_decode.cc).
    bool match_bytes(std::span<const std::byte> bytes, uint64_t offset,
                     const char* magic, uint32_t magic_len) noexcept;

    bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                    uint16_t* out) noexcept;
    bool read_u16le(std::span<const std::byte> bytes, uint64_t offset,
                    uint16_t* out) noexcept;
    bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                    uint32_t* out) noexcept;
    bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                    uint32_t* out) noexcept;

    bool read_tiff_u16(const TiffConfig& cfg, std::span<const std::byte> bytes,
                       uint64_t offset, uint16_t* out) noexcept;
    bool read_tiff_u32(const TiffConfig& cfg, std::span<const std::byte> bytes,
                       uint64_t offset, uint32_t* out) noexcept;

    bool read_u16_endian(bool le, std::span<const std::byte> bytes,
                         uint64_t offset, uint16_t* out) noexcept;
    bool read_i16_endian(bool le, std::span<const std::byte> bytes,
                         uint64_t offset, int16_t* out) noexcept;

    std::string_view
    make_mk_subtable_ifd_token(std::string_view vendor_prefix,
                               std::string_view subtable, uint32_t index,
                               std::span<char> scratch) noexcept;

    inline std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    inline std::string_view find_first_exif_text_value(const MetaStore& store,
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

    MetaValue make_fixed_ascii_text(ByteArena& arena,
                                    std::span<const std::byte> raw) noexcept;

    void emit_bin_dir_entries(std::string_view ifd_name, MetaStore& store,
                              std::span<const uint16_t> tags,
                              std::span<const MetaValue> values,
                              const ExifDecodeLimits& limits,
                              ExifDecodeResult* status_out) noexcept;

    uint64_t tiff_type_size(uint16_t type) noexcept;

    void update_status(ExifDecodeResult* out, ExifDecodeStatus status) noexcept;

    MetaValue decode_tiff_value(const TiffConfig& cfg,
                                std::span<const std::byte> bytes, uint16_t type,
                                uint64_t count, uint64_t value_off,
                                uint64_t value_bytes, ByteArena& arena,
                                const ExifDecodeLimits& limits,
                                ExifDecodeResult* result) noexcept;

    bool score_classic_ifd_candidate(const TiffConfig& cfg,
                                     std::span<const std::byte> bytes,
                                     uint64_t ifd_off,
                                     const ExifDecodeLimits& limits,
                                     ClassicIfdCandidate* out) noexcept;

    bool find_best_classic_ifd_candidate(std::span<const std::byte> bytes,
                                         uint64_t scan_bytes,
                                         const ExifDecodeLimits& limits,
                                         ClassicIfdCandidate* out) noexcept;

    bool looks_like_classic_ifd(const TiffConfig& cfg,
                                std::span<const std::byte> bytes,
                                uint64_t ifd_off,
                                const ExifDecodeLimits& limits) noexcept;

    void decode_classic_ifd_no_header(
        const TiffConfig& cfg, std::span<const std::byte> bytes,
        uint64_t ifd_off, std::string_view ifd_name, MetaStore& store,
        const ExifDecodeOptions& options, ExifDecodeResult* status_out,
        EntryFlags extra_flags) noexcept;

    // Vendor MakerNote entry points (implemented in their own .cc files).
    bool decode_olympus_makernote(const TiffConfig& parent_cfg,
                                  std::span<const std::byte> tiff_bytes,
                                  uint64_t maker_note_off,
                                  uint64_t maker_note_bytes,
                                  std::string_view mk_ifd0, MetaStore& store,
                                  const ExifDecodeOptions& options,
                                  ExifDecodeResult* status_out) noexcept;

    bool decode_pentax_makernote(std::span<const std::byte> maker_note_bytes,
                                 std::string_view mk_ifd0, MetaStore& store,
                                 const ExifDecodeOptions& options,
                                 ExifDecodeResult* status_out) noexcept;

    bool decode_casio_makernote(const TiffConfig& parent_cfg,
                                std::span<const std::byte> tiff_bytes,
                                uint64_t maker_note_off,
                                uint64_t maker_note_bytes,
                                std::string_view mk_ifd0, MetaStore& store,
                                const ExifDecodeOptions& options,
                                ExifDecodeResult* status_out) noexcept;

    bool decode_casio_qvci(std::span<const std::byte> qvci_bytes,
                           std::string_view mk_ifd0, MetaStore& store,
                           const ExifDecodeLimits& limits,
                           ExifDecodeResult* status_out) noexcept;

    bool decode_canon_makernote(const TiffConfig& parent_cfg,
                                std::span<const std::byte> tiff_bytes,
                                uint64_t maker_note_off,
                                uint64_t maker_note_bytes,
                                std::string_view mk_ifd0, MetaStore& store,
                                const ExifDecodeOptions& options,
                                ExifDecodeResult* status_out) noexcept;

    bool decode_sony_makernote(const TiffConfig& parent_cfg,
                               std::span<const std::byte> tiff_bytes,
                               uint64_t maker_note_off,
                               uint64_t maker_note_bytes,
                               std::string_view mk_ifd0, MetaStore& store,
                               const ExifDecodeOptions& options,
                               ExifDecodeResult* status_out) noexcept;

    bool decode_panasonic_makernote(const TiffConfig& parent_cfg,
                                    std::span<const std::byte> tiff_bytes,
                                    uint64_t maker_note_off,
                                    uint64_t maker_note_bytes,
                                    std::string_view mk_ifd0, MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept;

    bool decode_kodak_makernote(const TiffConfig& parent_cfg,
                                std::span<const std::byte> tiff_bytes,
                                uint64_t maker_note_off,
                                uint64_t maker_note_bytes,
                                std::string_view mk_ifd0, MetaStore& store,
                                const ExifDecodeOptions& options,
                                ExifDecodeResult* status_out) noexcept;

    void decode_sony_cipher_subdirs(std::string_view mk_ifd0, MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept;

    void decode_nikon_binary_subdirs(std::string_view mk_ifd0, MetaStore& store,
                                     bool le, const ExifDecodeOptions& options,
                                     ExifDecodeResult* status_out) noexcept;

    void decode_pentax_binary_subdirs(std::string_view mk_ifd0,
                                      MetaStore& store, bool le,
                                      const ExifDecodeOptions& options,
                                      ExifDecodeResult* status_out) noexcept;

}  // namespace exif_internal
}  // namespace openmeta
