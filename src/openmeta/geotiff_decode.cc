#include "geotiff_decode_internal.h"

#include "openmeta/meta_flags.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace openmeta::exif_internal {
namespace {

    static bool read_u16(const TiffConfig& cfg, std::span<const std::byte> bytes,
                         uint64_t offset, uint16_t* out) noexcept
    {
        return read_tiff_u16(cfg, bytes, offset, out);
    }


    static bool read_u64(const TiffConfig& cfg, std::span<const std::byte> bytes,
                         uint64_t offset, uint64_t* out) noexcept
    {
        if (!out || offset + 8U > bytes.size()) {
            return false;
        }
        const uint8_t b0 = u8(bytes[offset + 0]);
        const uint8_t b1 = u8(bytes[offset + 1]);
        const uint8_t b2 = u8(bytes[offset + 2]);
        const uint8_t b3 = u8(bytes[offset + 3]);
        const uint8_t b4 = u8(bytes[offset + 4]);
        const uint8_t b5 = u8(bytes[offset + 5]);
        const uint8_t b6 = u8(bytes[offset + 6]);
        const uint8_t b7 = u8(bytes[offset + 7]);

        if (cfg.le) {
            *out = static_cast<uint64_t>(b0)
                   | (static_cast<uint64_t>(b1) << 8U)
                   | (static_cast<uint64_t>(b2) << 16U)
                   | (static_cast<uint64_t>(b3) << 24U)
                   | (static_cast<uint64_t>(b4) << 32U)
                   | (static_cast<uint64_t>(b5) << 40U)
                   | (static_cast<uint64_t>(b6) << 48U)
                   | (static_cast<uint64_t>(b7) << 56U);
            return true;
        }

        *out = (static_cast<uint64_t>(b0) << 56U)
               | (static_cast<uint64_t>(b1) << 48U)
               | (static_cast<uint64_t>(b2) << 40U)
               | (static_cast<uint64_t>(b3) << 32U)
               | (static_cast<uint64_t>(b4) << 24U)
               | (static_cast<uint64_t>(b5) << 16U)
               | (static_cast<uint64_t>(b6) << 8U)
               | (static_cast<uint64_t>(b7) << 0U);
        return true;
    }


    static std::string_view trim_geotiff_ascii(std::string_view s) noexcept
    {
        // GeoTIFF ASCII params commonly use '|' as a separator with an optional
        // trailing '|'. Strip trailing separators and NULs for readability.
        while (!s.empty()) {
            const unsigned char c
                = static_cast<unsigned char>(s[s.size() - 1]);
            if (c == 0 || c == '|') {
                s.remove_suffix(1);
                continue;
            }
            break;
        }
        return s;
    }


    static void emit_key_u16(MetaStore& store, BlockId block, uint32_t order,
                             uint16_t key_id, uint16_t v) noexcept
    {
        Entry e;
        e.key                   = make_geotiff_key(key_id);
        e.value                 = make_u16(v);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    static void emit_key_f64_bits(MetaStore& store, BlockId block, uint32_t order,
                                  uint16_t key_id, uint64_t bits) noexcept
    {
        Entry e;
        e.key                   = make_geotiff_key(key_id);
        e.value                 = make_f64_bits(bits);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    static void emit_key_f64_bits_array(MetaStore& store, BlockId block,
                                        uint32_t order, uint16_t key_id,
                                        std::span<const uint64_t> bits) noexcept
    {
        Entry e;
        e.key                   = make_geotiff_key(key_id);
        e.value                 = make_f64_bits_array(store.arena(), bits);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = static_cast<uint32_t>(bits.size());
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    static void emit_key_text(MetaStore& store, BlockId block, uint32_t order,
                              uint16_t key_id, std::string_view s) noexcept
    {
        Entry e;
        e.key                   = make_geotiff_key(key_id);
        e.value                 = make_text(store.arena(), s, TextEncoding::Ascii);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = static_cast<uint32_t>(s.size());
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }

}  // namespace

void decode_geotiff_keys(const TiffConfig& cfg,
                         std::span<const std::byte> tiff_bytes,
                         const GeoTiffTagRef& key_directory,
                         const GeoTiffTagRef& double_params,
                         const GeoTiffTagRef& ascii_params,
                         MetaStore& store,
                         const ExifDecodeLimits& limits) noexcept
{
    if (!key_directory.present) {
        return;
    }

    // GeoTIFF tags:
    //   GeoKeyDirectoryTag (0x87AF, SHORT[])
    //   GeoDoubleParamsTag (0x87B0, DOUBLE[])
    //   GeoAsciiParamsTag  (0x87B1, ASCII)
    //
    // We only emit derived keys when the directory is structurally valid.
    if (key_directory.type != 3U /*SHORT*/) {
        return;
    }
    if (key_directory.count32 < 4U) {
        return;
    }
    const uint64_t dir_bytes
        = static_cast<uint64_t>(key_directory.count32) * 2ULL;
    if (dir_bytes != key_directory.value_bytes) {
        // Defensive: avoid trusting mismatched size computations.
        return;
    }
    if (key_directory.value_off > tiff_bytes.size()
        || dir_bytes > tiff_bytes.size() - key_directory.value_off) {
        return;
    }

    std::array<uint16_t, 4> hdr {};
    if (!read_u16(cfg, tiff_bytes, key_directory.value_off + 0, &hdr[0])
        || !read_u16(cfg, tiff_bytes, key_directory.value_off + 2, &hdr[1])
        || !read_u16(cfg, tiff_bytes, key_directory.value_off + 4, &hdr[2])
        || !read_u16(cfg, tiff_bytes, key_directory.value_off + 6, &hdr[3])) {
        return;
    }

    const uint32_t key_count = hdr[3];
    if (key_count == 0U) {
        return;
    }
    if (limits.max_entries_per_ifd != 0U && key_count > limits.max_entries_per_ifd) {
        return;
    }
    const uint64_t needed_u16 = 4ULL + static_cast<uint64_t>(key_count) * 4ULL;
    if (needed_u16 > static_cast<uint64_t>(key_directory.count32)) {
        return;
    }

    const uint16_t kGeoDoubleParamsTag = 0x87B0u;
    const uint16_t kGeoAsciiParamsTag  = 0x87B1u;

    const bool have_double = double_params.present
                             && double_params.type == 12U /*DOUBLE*/
                             && double_params.value_bytes
                                    == static_cast<uint64_t>(double_params.count32)
                                           * 8ULL
                             && double_params.value_off <= tiff_bytes.size()
                             && double_params.value_bytes
                                    <= tiff_bytes.size() - double_params.value_off;
    const bool have_ascii = ascii_params.present
                            && ascii_params.type == 2U /*ASCII*/
                            && ascii_params.value_bytes
                                   == static_cast<uint64_t>(ascii_params.count32)
                            && ascii_params.value_off <= tiff_bytes.size()
                            && ascii_params.value_bytes
                                   <= tiff_bytes.size() - ascii_params.value_off;

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    uint32_t order = 0;
    for (uint32_t i = 0; i < key_count; ++i) {
        const uint64_t off
            = key_directory.value_off + 8ULL + static_cast<uint64_t>(i) * 8ULL;
        uint16_t key_id = 0;
        uint16_t loc    = 0;
        uint16_t count  = 0;
        uint16_t valoff = 0;
        if (!read_u16(cfg, tiff_bytes, off + 0, &key_id)
            || !read_u16(cfg, tiff_bytes, off + 2, &loc)
            || !read_u16(cfg, tiff_bytes, off + 4, &count)
            || !read_u16(cfg, tiff_bytes, off + 6, &valoff)) {
            break;
        }
        if (count == 0U) {
            continue;
        }

        if (loc == 0U) {
            // When TIFFTagLocation==0, the value is stored directly in Value_Offset
            // (Count is typically 1). Preserve Value_Offset as-is.
            emit_key_u16(store, block, order++, key_id, valoff);
            continue;
        }

        if (loc == kGeoDoubleParamsTag && have_double) {
            const uint32_t idx = valoff;
            const uint32_t avail = double_params.count32;
            if (idx >= avail) {
                continue;
            }
            const uint32_t room = avail - idx;
            const uint32_t take = (count < room) ? count : room;
            if (take == 0U) {
                continue;
            }
            if (limits.max_value_bytes != 0U
                && static_cast<uint64_t>(take) * 8ULL > limits.max_value_bytes) {
                continue;
            }

            if (take == 1U) {
                const uint64_t elem_off = double_params.value_off
                                          + static_cast<uint64_t>(idx) * 8ULL;
                uint64_t bits = 0;
                if (!read_u64(cfg, tiff_bytes, elem_off, &bits)) {
                    continue;
                }
                emit_key_f64_bits(store, block, order++, key_id, bits);
                continue;
            }

            if (take > 32U) {
                continue;
            }

            std::array<uint64_t, 32> bits_buf {};
            for (uint32_t j = 0; j < take; ++j) {
                const uint64_t elem_off
                    = double_params.value_off
                      + (static_cast<uint64_t>(idx + j) * 8ULL);
                uint64_t bits = 0;
                if (!read_u64(cfg, tiff_bytes, elem_off, &bits)) {
                    bits = 0;
                }
                bits_buf[j] = bits;
            }
            emit_key_f64_bits_array(
                store, block, order++, key_id,
                std::span<const uint64_t>(bits_buf.data(), take));
            continue;
        }

        if (loc == kGeoAsciiParamsTag && have_ascii) {
            const uint32_t idx = valoff;
            const uint32_t n = count;
            if (idx >= ascii_params.count32) {
                continue;
            }
            const uint32_t room = ascii_params.count32 - idx;
            const uint32_t take = (n < room) ? n : room;
            if (limits.max_value_bytes != 0U && take > limits.max_value_bytes) {
                continue;
            }
            const std::span<const std::byte> raw
                = tiff_bytes.subspan(static_cast<size_t>(ascii_params.value_off
                                                         + idx),
                                     static_cast<size_t>(take));
            const std::string_view s(reinterpret_cast<const char*>(raw.data()),
                                     raw.size());
            emit_key_text(store, block, order++, key_id, trim_geotiff_ascii(s));
            continue;
        }
    }
}

}  // namespace openmeta::exif_internal
