#include "openmeta/container_scan.h"

#include <array>
#include <cstring>

namespace openmeta {
namespace {

    static constexpr uint32_t kPngSignatureSize                             = 8;
    static constexpr std::array<std::byte, kPngSignatureSize> kPngSignature = {
        std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
        std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
        std::byte { 0x1A }, std::byte { 0x0A },
    };

    static constexpr std::array<std::byte, 4> kJp2Signature = {
        std::byte { 0x0D },
        std::byte { 0x0A },
        std::byte { 0x87 },
        std::byte { 0x0A },
    };

    static constexpr std::array<std::byte, 16> kJp2UuidExif = {
        std::byte { 0x4a }, std::byte { 0x70 }, std::byte { 0x67 },
        std::byte { 0x54 }, std::byte { 0x69 }, std::byte { 0x66 },
        std::byte { 0x66 }, std::byte { 0x45 }, std::byte { 0x78 },
        std::byte { 0x69 }, std::byte { 0x66 }, std::byte { 0x2d },
        std::byte { 0x3e }, std::byte { 0x4a }, std::byte { 0x50 },
        std::byte { 0x32 },
    };

    static constexpr std::array<std::byte, 16> kJp2UuidIptc = {
        std::byte { 0x33 }, std::byte { 0xc7 }, std::byte { 0xa4 },
        std::byte { 0xd2 }, std::byte { 0xb8 }, std::byte { 0x1d },
        std::byte { 0x47 }, std::byte { 0x23 }, std::byte { 0xa0 },
        std::byte { 0xba }, std::byte { 0xf1 }, std::byte { 0xa3 },
        std::byte { 0xe0 }, std::byte { 0x97 }, std::byte { 0xad },
        std::byte { 0x38 },
    };

    static constexpr std::array<std::byte, 16> kJp2UuidXmp = {
        std::byte { 0xbe }, std::byte { 0x7a }, std::byte { 0xcf },
        std::byte { 0xcb }, std::byte { 0x97 }, std::byte { 0xa9 },
        std::byte { 0x42 }, std::byte { 0xe8 }, std::byte { 0x9c },
        std::byte { 0x71 }, std::byte { 0x99 }, std::byte { 0x94 },
        std::byte { 0x91 }, std::byte { 0xe3 }, std::byte { 0xaf },
        std::byte { 0xac },
    };

    // GeoJP2 / GeoTIFF UUID box (OGC GeoJP2). Payload is a classic TIFF stream.
    // UUID: B14BF8BD-083D-4B43-A5AE-8CD7D5A6CE03
    static constexpr std::array<std::byte, 16> kJp2UuidGeoTiff = {
        std::byte { 0xb1 }, std::byte { 0x4b }, std::byte { 0xf8 },
        std::byte { 0xbd }, std::byte { 0x08 }, std::byte { 0x3d },
        std::byte { 0x4b }, std::byte { 0x43 }, std::byte { 0xa5 },
        std::byte { 0xae }, std::byte { 0x8c }, std::byte { 0xd7 },
        std::byte { 0xd5 }, std::byte { 0xa6 }, std::byte { 0xce },
        std::byte { 0x03 },
    };

    // Canon CR3 metadata UUID found under `moov` (contains `CMT1..CMT4` TIFF blocks).
    static constexpr std::array<std::byte, 16> kCr3CanonUuid = {
        std::byte { 0x85 }, std::byte { 0xc0 }, std::byte { 0xb6 },
        std::byte { 0x87 }, std::byte { 0x82 }, std::byte { 0x0f },
        std::byte { 0x11 }, std::byte { 0xe0 }, std::byte { 0x81 },
        std::byte { 0x11 }, std::byte { 0xf4 }, std::byte { 0xce },
        std::byte { 0x46 }, std::byte { 0x2b }, std::byte { 0x6a },
        std::byte { 0x48 },
    };

    struct BlockSink final {
        ContainerBlockRef* out = nullptr;
        uint32_t cap           = 0;
        ScanResult result;
    };

    static constexpr uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
    }


    static bool match(std::span<const std::byte> bytes, uint64_t offset,
                      const char* s, uint32_t s_len) noexcept
    {
        const uint64_t size = static_cast<uint64_t>(bytes.size());
        if (offset + s_len > size) {
            return false;
        }
        return std::memcmp(bytes.data() + static_cast<size_t>(offset), s,
                           static_cast<size_t>(s_len))
               == 0;
    }


    static bool match_bytes(std::span<const std::byte> bytes, uint64_t offset,
                            const std::byte* data, uint32_t data_len) noexcept
    {
        const uint64_t size = static_cast<uint64_t>(bytes.size());
        if (offset + data_len > size) {
            return false;
        }
        return std::memcmp(bytes.data() + static_cast<size_t>(offset), data,
                           static_cast<size_t>(data_len))
               == 0;
    }


    static void sink_emit(BlockSink* sink,
                          const ContainerBlockRef& block) noexcept
    {
        sink->result.needed += 1;
        if (sink->result.written < sink->cap) {
            sink->out[sink->result.written] = block;
            sink->result.written += 1;
        } else if (sink->result.status == ScanStatus::Ok) {
            sink->result.status = ScanStatus::OutputTruncated;
        }
    }


    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 8)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 0);
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
            = (static_cast<uint32_t>(u8(bytes[offset + 0])) << 24)
              | (static_cast<uint32_t>(u8(bytes[offset + 1])) << 16)
              | (static_cast<uint32_t>(u8(bytes[offset + 2])) << 8)
              | (static_cast<uint32_t>(u8(bytes[offset + 3])) << 0);
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
            = (static_cast<uint32_t>(u8(bytes[offset + 0])) << 0)
              | (static_cast<uint32_t>(u8(bytes[offset + 1])) << 8)
              | (static_cast<uint32_t>(u8(bytes[offset + 2])) << 16)
              | (static_cast<uint32_t>(u8(bytes[offset + 3])) << 24);
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
            v = (v << 8) | static_cast<uint64_t>(u8(bytes[offset + i]));
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
            v |= static_cast<uint64_t>(u8(bytes[offset + i])) << (i * 8);
        }
        *out = v;
        return true;
    }


    static bool looks_like_tiff_at(std::span<const std::byte> bytes,
                                   uint64_t offset) noexcept
    {
        if (offset + 8 > bytes.size()) {
            return false;
        }

        const uint8_t b0 = u8(bytes[offset + 0]);
        const uint8_t b1 = u8(bytes[offset + 1]);
        const bool le    = (b0 == 0x49 && b1 == 0x49);
        const bool be    = (b0 == 0x4D && b1 == 0x4D);
        if (!le && !be) {
            return false;
        }

        const uint16_t version
            = le ? static_cast<uint16_t>(u8(bytes[offset + 2])
                                         | (u8(bytes[offset + 3]) << 8U))
                 : static_cast<uint16_t>((u8(bytes[offset + 2]) << 8U)
                                         | u8(bytes[offset + 3]));
        if (version != 42 && version != 43 && version != 0x0055
            && version != 0x4F52) {
            return false;
        }

        // Classic TIFF (and RW2/ORF variants) store a u32 IFD0 offset at +4.
        if (version != 43) {
            uint32_t ifd0 = 0;
            if (le) {
                if (!read_u32le(bytes, offset + 4, &ifd0)) {
                    return false;
                }
            } else {
                if (!read_u32be(bytes, offset + 4, &ifd0)) {
                    return false;
                }
            }
            return offset + static_cast<uint64_t>(ifd0) < bytes.size();
        }

        // BigTIFF header:
        //   u16 version=43, u16 offsize (8), u16 zero, u64 IFD0 offset.
        if (offset + 16 > bytes.size()) {
            return false;
        }
        const uint16_t offsize
            = le ? static_cast<uint16_t>(u8(bytes[offset + 4])
                                         | (u8(bytes[offset + 5]) << 8U))
                 : static_cast<uint16_t>((u8(bytes[offset + 4]) << 8U)
                                         | u8(bytes[offset + 5]));
        if (offsize != 8) {
            return false;
        }
        uint64_t ifd0 = 0;
        if (le) {
            if (!read_u64le(bytes, offset + 8, &ifd0)) {
                return false;
            }
        } else {
            if (!read_u64be(bytes, offset + 8, &ifd0)) {
                return false;
            }
        }
        return offset + ifd0 < bytes.size();
    }


    static uint64_t fnv1a_64(std::span<const std::byte> data) noexcept
    {
        uint64_t hash = 14695981039346656037ULL;
        for (size_t i = 0; i < data.size(); ++i) {
            hash ^= static_cast<uint64_t>(u8(data[i]));
            hash *= 1099511628211ULL;
        }
        return hash;
    }


    static void skip_exif_preamble(ContainerBlockRef* block,
                                   std::span<const std::byte> bytes) noexcept
    {
        // EXIF segment preamble is typically "Exif\0\0" before the TIFF header.
        // Some real-world files use a non-zero second terminator byte; accept
        // these variants as long as the following bytes look like a TIFF header.
        if (block->data_size < 10) {
            return;
        }
        if (!match(bytes, block->data_offset, "Exif", 4)) {
            return;
        }
        if (u8(bytes[block->data_offset + 4]) != 0) {
            return;
        }

        const uint64_t tiff_off = block->data_offset + 6;
        if (tiff_off + 4 > bytes.size()) {
            return;
        }
        const uint8_t a    = u8(bytes[tiff_off + 0]);
        const uint8_t b    = u8(bytes[tiff_off + 1]);
        const uint8_t c    = u8(bytes[tiff_off + 2]);
        const uint8_t d    = u8(bytes[tiff_off + 3]);
        const bool is_tiff = (a == 'I' && b == 'I' && c == 0x2A && d == 0x00)
                             || (a == 'M' && b == 'M' && c == 0x00
                                 && d == 0x2A);
        if (!is_tiff) {
            return;
        }

        block->data_offset += 6;
        block->data_size -= 6;
    }


    static void skip_bmff_exif_offset(ContainerBlockRef* block,
                                      std::span<const std::byte> bytes) noexcept
    {
        if (block->data_size < 4) {
            return;
        }
        uint32_t tiff_off = 0;
        if (!read_u32be(bytes, block->data_offset, &tiff_off)) {
            return;
        }
        // ISO-BMFF Exif item payload begins with a u32be offset to the TIFF
        // header *relative to the start of the Exif data after this field*.
        //
        // Example (common HEIC):
        //   00 00 00 06  45 78 69 66 00 00  MM 00 2A ...
        //     offset=6     \"Exif\\0\\0\"     TIFF header
        const uint64_t skip = 4ULL + static_cast<uint64_t>(tiff_off);
        if (skip >= block->data_size) {
            return;
        }
        block->chunking = BlockChunking::BmffExifTiffOffsetU32Be;
        block->aux_u32  = tiff_off;
        block->data_offset += skip;
        block->data_size -= skip;
    }

}  // namespace

ScanResult
scan_jpeg(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept
{
    BlockSink sink;
    sink.out = out.data();
    sink.cap = static_cast<uint32_t>(out.size());

    if (bytes.size() < 2) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }
    if (u8(bytes[0]) != 0xFF || u8(bytes[1]) != 0xD8) {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
    }

    uint64_t offset = 2;
    while (offset + 2 <= bytes.size()) {
        if (u8(bytes[offset]) != 0xFF) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }
        const uint64_t prefix_off = offset;
        while (offset < bytes.size() && u8(bytes[offset]) == 0xFF) {
            offset += 1;
        }
        if (offset >= bytes.size()) {
            break;
        }
        if (offset <= prefix_off) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }
        const uint64_t marker_off = offset - 1;
        const uint8_t marker_lo   = u8(bytes[offset]);
        offset += 1;

        const uint16_t marker = static_cast<uint16_t>(
            0xFF00U | static_cast<uint16_t>(marker_lo));

        if (marker == 0xFFD9) {
            break;
        }
        if (marker == 0xFFDA) {
            // Start of Scan: metadata lives before the compressed scan stream.
            break;
        }
        if ((marker >= 0xFFD0 && marker <= 0xFFD7) || marker == 0xFF01) {
            continue;
        }

        uint16_t seg_len = 0;
        if (!read_u16be(bytes, offset, &seg_len)) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }
        if (seg_len < 2) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }
        const uint64_t seg_payload_off  = offset + 2;
        const uint64_t seg_payload_size = static_cast<uint64_t>(seg_len - 2);
        const uint64_t seg_total_size   = 2 + static_cast<uint64_t>(seg_len);
        const uint64_t seg_total_off    = marker_off;
        if (seg_payload_off + seg_payload_size > bytes.size()
            || seg_total_off + seg_total_size > bytes.size()) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }

        if (marker == 0xFFE1) {
            if (seg_payload_size >= 10
                && match(bytes, seg_payload_off, "Exif", 4)
                && u8(bytes[seg_payload_off + 4]) == 0) {
                ContainerBlockRef block;
                block.format       = ContainerFormat::Jpeg;
                block.kind         = ContainerBlockKind::Exif;
                block.outer_offset = seg_total_off;
                block.outer_size   = seg_total_size;
                block.data_offset  = seg_payload_off;
                block.data_size    = seg_payload_size;
                block.id           = marker;
                skip_exif_preamble(&block, bytes);
                if (block.data_offset != seg_payload_off) {
                    sink_emit(&sink, block);
                }
            } else if (match(bytes, seg_payload_off,
                             "http://ns.adobe.com/xap/1.0/\0", 29)) {
                ContainerBlockRef block;
                block.format       = ContainerFormat::Jpeg;
                block.kind         = ContainerBlockKind::Xmp;
                block.outer_offset = seg_total_off;
                block.outer_size   = seg_total_size;
                block.data_offset  = seg_payload_off + 29;
                block.data_size    = seg_payload_size - 29;
                block.id           = marker;
                sink_emit(&sink, block);
            } else if (match(bytes, seg_payload_off,
                             "http://ns.adobe.com/xmp/extension/\0", 35)) {
                // Extended XMP:
                // signature (35) + guid (32) + full_len (4) + offset (4) + data...
                if (seg_payload_size >= 35 + 32 + 8) {
                    const uint64_t guid_off = seg_payload_off + 35;
                    const uint64_t full_off = guid_off + 32;
                    uint32_t full_len       = 0;
                    uint32_t part_off       = 0;
                    if (read_u32be(bytes, full_off, &full_len)
                        && read_u32be(bytes, full_off + 4, &part_off)) {
                        ContainerBlockRef block;
                        block.format = ContainerFormat::Jpeg;
                        block.kind   = ContainerBlockKind::XmpExtended;
                        block.chunking
                            = BlockChunking::JpegXmpExtendedGuidOffset;
                        block.outer_offset   = seg_total_off;
                        block.outer_size     = seg_total_size;
                        block.data_offset    = full_off + 8;
                        block.data_size      = seg_payload_size - (35 + 32 + 8);
                        block.id             = marker;
                        block.logical_offset = part_off;
                        block.logical_size   = full_len;
                        block.group          = fnv1a_64(
                            bytes.subspan(static_cast<size_t>(guid_off),
                                                   32));  // stable per GUID.
                        sink_emit(&sink, block);
                    }
                }
            } else if (seg_payload_size >= 4
                       && match(bytes, seg_payload_off, "QVCI", 4)) {
                // Casio QV-7000SX: APP1 "QVCI" maker note directory.
                ContainerBlockRef block;
                block.format       = ContainerFormat::Jpeg;
                block.kind         = ContainerBlockKind::MakerNote;
                block.outer_offset = seg_total_off;
                block.outer_size   = seg_total_size;
                block.data_offset  = seg_payload_off;
                block.data_size    = seg_payload_size;
                block.id           = marker;
                block.aux_u32      = fourcc('Q', 'V', 'C', 'I');
                sink_emit(&sink, block);
            } else if (seg_payload_size >= 8
                       && match(bytes, seg_payload_off, "FLIR", 4)
                       && u8(bytes[seg_payload_off + 4]) == 0) {
                // FLIR: APP1 multi-part stream containing an FFF/AFF payload.
                // Preamble:
                //   "FLIR\0" + u8(0x01) + u8(part_index) + u8(part_count_minus_1)
                ContainerBlockRef block;
                block.format       = ContainerFormat::Jpeg;
                block.kind         = ContainerBlockKind::MakerNote;
                block.outer_offset = seg_total_off;
                block.outer_size   = seg_total_size;
                block.data_offset  = seg_payload_off + 8;
                block.data_size    = seg_payload_size - 8;
                block.id           = marker;
                block.aux_u32      = fourcc('F', 'L', 'I', 'R');
                block.part_index   = u8(bytes[seg_payload_off + 6]);
                block.part_count   = static_cast<uint32_t>(
                                       u8(bytes[seg_payload_off + 7]))
                                   + 1U;
                block.group = fourcc('F', 'L', 'I', 'R');
                sink_emit(&sink, block);
            }
        } else if (marker == 0xFFE2) {
            if (match(bytes, seg_payload_off, "ICC_PROFILE\0", 12)) {
                if (seg_payload_size >= 14) {
                    const uint8_t seq = u8(bytes[seg_payload_off + 12]);
                    const uint8_t tot = u8(bytes[seg_payload_off + 13]);
                    ContainerBlockRef block;
                    block.format       = ContainerFormat::Jpeg;
                    block.kind         = ContainerBlockKind::Icc;
                    block.chunking     = BlockChunking::JpegApp2SeqTotal;
                    block.outer_offset = seg_total_off;
                    block.outer_size   = seg_total_size;
                    block.data_offset  = seg_payload_off + 14;
                    block.data_size    = seg_payload_size - 14;
                    block.id           = marker;
                    block.part_index   = (seq > 0)
                                             ? static_cast<uint32_t>(seq - 1U)
                                             : 0U;
                    block.part_count   = tot;
                    sink_emit(&sink, block);
                }
            } else if (match(bytes, seg_payload_off, "MPF\0", 4)) {
                ContainerBlockRef block;
                block.format       = ContainerFormat::Jpeg;
                block.kind         = ContainerBlockKind::Mpf;
                block.outer_offset = seg_total_off;
                block.outer_size   = seg_total_size;
                block.data_offset  = seg_payload_off + 4;
                block.data_size    = seg_payload_size - 4;
                block.id           = marker;
                sink_emit(&sink, block);
            }
        } else if (marker == 0xFFE4) {
            // Some vendors store metadata in APP4 (e.g. DJI thermal parameters).
            // Emit the payload as a vendor block and let higher-level decode
            // decide whether it recognizes the contents.
            ContainerBlockRef block;
            block.format       = ContainerFormat::Jpeg;
            block.kind         = ContainerBlockKind::MakerNote;
            block.outer_offset = seg_total_off;
            block.outer_size   = seg_total_size;
            block.data_offset  = seg_payload_off;
            block.data_size    = seg_payload_size;
            block.id           = marker;
            if (seg_payload_size >= 4) {
                uint32_t sig = 0;
                if (read_u32be(bytes, seg_payload_off, &sig)) {
                    block.aux_u32 = sig;
                }
            }
            sink_emit(&sink, block);
        } else if (marker == 0xFFED) {
            if (match(bytes, seg_payload_off, "Photoshop 3.0\0", 14)) {
                ContainerBlockRef block;
                block.format       = ContainerFormat::Jpeg;
                block.kind         = ContainerBlockKind::PhotoshopIrB;
                block.chunking     = BlockChunking::PsIrB8Bim;
                block.outer_offset = seg_total_off;
                block.outer_size   = seg_total_size;
                block.data_offset  = seg_payload_off + 14;
                block.data_size    = seg_payload_size - 14;
                block.id           = marker;
                sink_emit(&sink, block);
            }
        } else if (marker == 0xFFFE) {
            ContainerBlockRef block;
            block.format       = ContainerFormat::Jpeg;
            block.kind         = ContainerBlockKind::Comment;
            block.outer_offset = seg_total_off;
            block.outer_size   = seg_total_size;
            block.data_offset  = seg_payload_off;
            block.data_size    = seg_payload_size;
            block.id           = marker;
            sink_emit(&sink, block);
        }

        offset = seg_payload_off + seg_payload_size;
    }

    return sink.result;
}


ScanResult
scan_png(std::span<const std::byte> bytes,
         std::span<ContainerBlockRef> out) noexcept
{
    BlockSink sink;
    sink.out = out.data();
    sink.cap = static_cast<uint32_t>(out.size());

    if (bytes.size() < kPngSignatureSize) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }
    if (!match_bytes(bytes, 0, kPngSignature.data(), kPngSignatureSize)) {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
    }

    uint64_t offset = kPngSignatureSize;
    while (offset + 12 <= bytes.size()) {
        const uint64_t chunk_off = offset;
        uint32_t len             = 0;
        uint32_t type            = 0;
        if (!read_u32be(bytes, offset, &len)
            || !read_u32be(bytes, offset + 4, &type)) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }
        const uint64_t data_off   = offset + 8;
        const uint64_t data_size  = static_cast<uint64_t>(len);
        const uint64_t crc_off    = data_off + data_size;
        const uint64_t chunk_size = 12 + data_size;
        if (crc_off + 4 > bytes.size()) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }

        if (type == fourcc('e', 'X', 'I', 'f')) {
            ContainerBlockRef block;
            block.format       = ContainerFormat::Png;
            block.kind         = ContainerBlockKind::Exif;
            block.outer_offset = chunk_off;
            block.outer_size   = chunk_size;
            block.data_offset  = data_off;
            block.data_size    = data_size;
            block.id           = type;
            sink_emit(&sink, block);
        } else if (type == fourcc('i', 'C', 'C', 'P')) {
            // profile_name\0 + compression_method + compressed_profile
            uint64_t p         = data_off;
            const uint64_t end = data_off + data_size;
            while (p < end && u8(bytes[p]) != 0) {
                p += 1;
            }
            if (p + 2 <= end) {
                const uint64_t comp_method_off = p + 1;
                (void)u8(bytes[comp_method_off]);
                ContainerBlockRef block;
                block.format       = ContainerFormat::Png;
                block.kind         = ContainerBlockKind::Icc;
                block.compression  = BlockCompression::Deflate;
                block.outer_offset = chunk_off;
                block.outer_size   = chunk_size;
                block.data_offset  = comp_method_off + 1;
                block.data_size    = end - (comp_method_off + 1);
                block.id           = type;
                sink_emit(&sink, block);
            }
        } else if (type == fourcc('i', 'T', 'X', 't')) {
            // keyword\0 + comp_flag + comp_method + lang\0 + trans\0 + text
            uint64_t p         = data_off;
            const uint64_t end = data_off + data_size;
            while (p < end && u8(bytes[p]) != 0) {
                p += 1;
            }
            const uint64_t keyword_end = p;
            if (keyword_end + 3 <= end) {
                const uint64_t comp_flag_off = keyword_end + 1;
                const uint8_t comp_flag      = u8(bytes[comp_flag_off]);
                (void)u8(bytes[comp_flag_off + 1]);  // comp method

                const uint32_t kXmpKeywordLen = 17;
                bool is_xmp                   = false;
                if (keyword_end - data_off == kXmpKeywordLen) {
                    is_xmp = match(bytes, data_off, "XML:com.adobe.xmp",
                                   kXmpKeywordLen);
                }

                uint64_t lang = comp_flag_off + 2;
                while (lang < end && u8(bytes[lang]) != 0) {
                    lang += 1;
                }
                if (lang >= end) {
                    goto next_chunk;
                }
                uint64_t trans = lang + 1;
                while (trans < end && u8(bytes[trans]) != 0) {
                    trans += 1;
                }
                if (trans >= end) {
                    goto next_chunk;
                }
                const uint64_t text_off = trans + 1;
                if (text_off <= end && is_xmp) {
                    ContainerBlockRef block;
                    block.format       = ContainerFormat::Png;
                    block.kind         = ContainerBlockKind::Xmp;
                    block.outer_offset = chunk_off;
                    block.outer_size   = chunk_size;
                    block.data_offset  = text_off;
                    block.data_size    = end - text_off;
                    block.id           = type;
                    if (comp_flag != 0) {
                        block.compression = BlockCompression::Deflate;
                    }
                    sink_emit(&sink, block);
                }
            }
        } else if (type == fourcc('z', 'T', 'X', 't')) {
            // keyword\0 + comp_method + compressed_text
            uint64_t p         = data_off;
            const uint64_t end = data_off + data_size;
            while (p < end && u8(bytes[p]) != 0) {
                p += 1;
            }
            if (p + 2 <= end) {
                ContainerBlockRef block;
                block.format       = ContainerFormat::Png;
                block.kind         = ContainerBlockKind::Text;
                block.compression  = BlockCompression::Deflate;
                block.outer_offset = chunk_off;
                block.outer_size   = chunk_size;
                block.data_offset  = (p + 2);
                block.data_size    = end - (p + 2);
                block.id           = type;
                sink_emit(&sink, block);
            }
        } else if (type == fourcc('t', 'E', 'X', 't')) {
            ContainerBlockRef block;
            block.format       = ContainerFormat::Png;
            block.kind         = ContainerBlockKind::Text;
            block.outer_offset = chunk_off;
            block.outer_size   = chunk_size;
            block.data_offset  = data_off;
            block.data_size    = data_size;
            block.id           = type;
            sink_emit(&sink, block);
        }

    next_chunk:
        offset += chunk_size;
        if (type == fourcc('I', 'E', 'N', 'D')) {
            break;
        }
    }

    return sink.result;
}


ScanResult
scan_webp(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept
{
    BlockSink sink;
    sink.out = out.data();
    sink.cap = static_cast<uint32_t>(out.size());

    if (bytes.size() < 12) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }
    if (!match(bytes, 0, "RIFF", 4) || !match(bytes, 8, "WEBP", 4)) {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
    }

    uint32_t riff_size = 0;
    if (!read_u32le(bytes, 4, &riff_size)) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }
    const uint64_t file_end = (riff_size + 8ULL < bytes.size())
                                  ? (riff_size + 8ULL)
                                  : static_cast<uint64_t>(bytes.size());

    uint64_t offset = 12;
    while (offset + 8 <= file_end) {
        const uint64_t chunk_off = offset;
        uint32_t type            = 0;
        uint32_t size_le         = 0;
        if (!read_u32be(bytes, offset, &type)
            || !read_u32le(bytes, offset + 4, &size_le)) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }

        const uint64_t data_off  = offset + 8;
        const uint64_t data_size = size_le;
        uint64_t next            = data_off + data_size;
        if (next > file_end) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }
        if ((data_size & 1U) != 0U) {
            next += 1;
        }

        if (type == fourcc('E', 'X', 'I', 'F')) {
            ContainerBlockRef block;
            block.format       = ContainerFormat::Webp;
            block.kind         = ContainerBlockKind::Exif;
            block.outer_offset = chunk_off;
            block.outer_size   = next - chunk_off;
            block.data_offset  = data_off;
            block.data_size    = data_size;
            block.id           = type;
            skip_exif_preamble(&block, bytes);
            sink_emit(&sink, block);
        } else if (type == fourcc('X', 'M', 'P', ' ')) {
            ContainerBlockRef block;
            block.format       = ContainerFormat::Webp;
            block.kind         = ContainerBlockKind::Xmp;
            block.outer_offset = chunk_off;
            block.outer_size   = next - chunk_off;
            block.data_offset  = data_off;
            block.data_size    = data_size;
            block.id           = type;
            sink_emit(&sink, block);
        } else if (type == fourcc('I', 'C', 'C', 'P')) {
            ContainerBlockRef block;
            block.format       = ContainerFormat::Webp;
            block.kind         = ContainerBlockKind::Icc;
            block.outer_offset = chunk_off;
            block.outer_size   = next - chunk_off;
            block.data_offset  = data_off;
            block.data_size    = data_size;
            block.id           = type;
            sink_emit(&sink, block);
        }

        offset = next;
    }

    return sink.result;
}


ScanResult
scan_gif(std::span<const std::byte> bytes,
         std::span<ContainerBlockRef> out) noexcept
{
    BlockSink sink;
    sink.out = out.data();
    sink.cap = static_cast<uint32_t>(out.size());

    if (bytes.size() < 13) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }
    if (!match(bytes, 0, "GIF87a", 6) && !match(bytes, 0, "GIF89a", 6)) {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
    }

    uint64_t offset = 6;
    // Logical Screen Descriptor.
    if (offset + 7 > bytes.size()) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }
    const uint8_t packed = u8(bytes[offset + 4]);
    offset += 7;

    if ((packed & 0x80U) != 0U) {
        const uint8_t gct_size_pow = packed & 0x07U;
        const uint64_t gct_bytes   = 3ULL << (gct_size_pow + 1U);
        if (offset + gct_bytes > bytes.size()) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }
        offset += gct_bytes;
    }

    while (offset < bytes.size()) {
        const uint8_t introducer = u8(bytes[offset]);
        if (introducer == 0x3B) {  // trailer
            break;
        }
        if (introducer == 0x21) {  // extension
            if (offset + 2 > bytes.size()) {
                sink.result.status = ScanStatus::Malformed;
                return sink.result;
            }
            const uint8_t label = u8(bytes[offset + 1]);
            if (label == 0xFF) {  // application extension
                if (offset + 3 > bytes.size()) {
                    sink.result.status = ScanStatus::Malformed;
                    return sink.result;
                }
                const uint8_t app_block_size = u8(bytes[offset + 2]);
                if (app_block_size != 11) {
                    // Skip: data sub-blocks.
                    uint64_t p = offset + 3 + app_block_size;
                    while (p < bytes.size()) {
                        const uint8_t sub = u8(bytes[p]);
                        p += 1;
                        if (sub == 0) {
                            break;
                        }
                        if (p + sub > bytes.size()) {
                            sink.result.status = ScanStatus::Malformed;
                            return sink.result;
                        }
                        p += sub;
                    }
                    offset = p;
                    continue;
                }

                if (offset + 3 + 11 > bytes.size()) {
                    sink.result.status = ScanStatus::Malformed;
                    return sink.result;
                }
                const uint64_t app_id_off = offset + 3;
                const bool is_xmp = match(bytes, app_id_off, "XMP Data", 8)
                                    && match(bytes, app_id_off + 8, "XMP", 3);
                const bool is_icc = match(bytes, app_id_off, "ICCRGBG1", 8)
                                    && match(bytes, app_id_off + 8, "012", 3);

                uint64_t p              = app_id_off + 11;
                const uint64_t data_off = p;
                while (p < bytes.size()) {
                    const uint8_t sub = u8(bytes[p]);
                    p += 1;
                    if (sub == 0) {
                        break;
                    }
                    if (p + sub > bytes.size()) {
                        sink.result.status = ScanStatus::Malformed;
                        return sink.result;
                    }
                    p += sub;
                }
                const uint64_t ext_end = p;

                if (is_xmp || is_icc) {
                    ContainerBlockRef block;
                    block.format       = ContainerFormat::Gif;
                    block.kind         = is_xmp ? ContainerBlockKind::Xmp
                                                : ContainerBlockKind::Icc;
                    block.chunking     = BlockChunking::GifSubBlocks;
                    block.outer_offset = offset;
                    block.outer_size   = ext_end - offset;
                    block.data_offset  = data_off;
                    block.data_size    = ext_end - data_off;
                    block.id           = 0x21FF;  // extension + app label
                    sink_emit(&sink, block);
                }

                offset = ext_end;
                continue;
            }

            // Skip other extension types: 0x21 <label> <sub-blocks>
            uint64_t p = offset + 2;
            while (p < bytes.size()) {
                const uint8_t sub = u8(bytes[p]);
                p += 1;
                if (sub == 0) {
                    break;
                }
                if (p + sub > bytes.size()) {
                    sink.result.status = ScanStatus::Malformed;
                    return sink.result;
                }
                p += sub;
            }
            offset = p;
            continue;
        }

        if (introducer == 0x2C) {  // image descriptor
            if (offset + 10 > bytes.size()) {
                sink.result.status = ScanStatus::Malformed;
                return sink.result;
            }
            const uint8_t img_packed = u8(bytes[offset + 9]);
            offset += 10;
            if ((img_packed & 0x80U) != 0U) {
                const uint8_t lct_size_pow = img_packed & 0x07U;
                const uint64_t lct_bytes   = 3ULL << (lct_size_pow + 1U);
                if (offset + lct_bytes > bytes.size()) {
                    sink.result.status = ScanStatus::Malformed;
                    return sink.result;
                }
                offset += lct_bytes;
            }
            if (offset + 1 > bytes.size()) {
                sink.result.status = ScanStatus::Malformed;
                return sink.result;
            }
            offset += 1;  // LZW min code size
            // Image data sub-blocks.
            while (offset < bytes.size()) {
                const uint8_t sub = u8(bytes[offset]);
                offset += 1;
                if (sub == 0) {
                    break;
                }
                if (offset + sub > bytes.size()) {
                    sink.result.status = ScanStatus::Malformed;
                    return sink.result;
                }
                offset += sub;
            }
            continue;
        }

        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }

    return sink.result;
}


namespace {

    struct BmffBox final {
        uint64_t offset      = 0;
        uint64_t size        = 0;
        uint64_t header_size = 0;
        uint32_t type        = 0;
        bool has_uuid        = false;
        std::array<std::byte, 16> uuid {};
    };

    static bool parse_bmff_box(std::span<const std::byte> bytes,
                               uint64_t offset, uint64_t parent_end,
                               BmffBox* out) noexcept
    {
        if (offset + 8 > parent_end || offset + 8 > bytes.size()) {
            return false;
        }
        uint32_t size32 = 0;
        uint32_t type   = 0;
        if (!read_u32be(bytes, offset + 0, &size32)
            || !read_u32be(bytes, offset + 4, &type)) {
            return false;
        }

        uint64_t header_size = 8;
        uint64_t box_size    = size32;
        if (size32 == 1) {
            uint64_t size64 = 0;
            if (!read_u64be(bytes, offset + 8, &size64)) {
                return false;
            }
            header_size = 16;
            box_size    = size64;
        } else if (size32 == 0) {
            box_size = parent_end - offset;
        }

        if (box_size < header_size) {
            return false;
        }
        if (offset + box_size > parent_end
            || offset + box_size > bytes.size()) {
            return false;
        }

        bool has_uuid = false;
        std::array<std::byte, 16> uuid {};
        if (type == fourcc('u', 'u', 'i', 'd')) {
            if (header_size + 16 > box_size) {
                return false;
            }
            has_uuid                = true;
            const uint64_t uuid_off = offset + header_size;
            if (uuid_off + 16 > bytes.size()) {
                return false;
            }
            for (uint32_t i = 0; i < 16; ++i) {
                uuid[i] = bytes[uuid_off + i];
            }
            header_size += 16;
        }

        out->offset      = offset;
        out->size        = box_size;
        out->header_size = header_size;
        out->type        = type;
        out->has_uuid    = has_uuid;
        out->uuid        = uuid;
        return true;
    }


    static void scan_jp2_box_payload(std::span<const std::byte> bytes,
                                     const BmffBox& box,
                                     BlockSink* sink) noexcept
    {
        const uint64_t payload_off  = box.offset + box.header_size;
        const uint64_t payload_size = box.size - box.header_size;

        if (box.type == fourcc('u', 'u', 'i', 'd') && box.has_uuid) {
            ContainerBlockRef block;
            block.format       = ContainerFormat::Jp2;
            block.outer_offset = box.offset;
            block.outer_size   = box.size;
            block.data_offset  = payload_off;
            block.data_size    = payload_size;
            block.id           = box.type;
            block.chunking     = BlockChunking::Jp2UuidPayload;

            if (box.uuid == kJp2UuidExif) {
                block.kind = ContainerBlockKind::Exif;
                skip_exif_preamble(&block, bytes);
                sink_emit(sink, block);
            } else if (box.uuid == kJp2UuidXmp) {
                block.kind = ContainerBlockKind::Xmp;
                sink_emit(sink, block);
            } else if (box.uuid == kJp2UuidIptc) {
                block.kind = ContainerBlockKind::IptcIim;
                sink_emit(sink, block);
            } else if (box.uuid == kJp2UuidGeoTiff) {
                // GeoJP2 uses a UUID box that stores a TIFF stream containing
                // GeoTIFF tags (ModelPixelScale, ModelTiepoint, GeoKeyDirectory...).
                // Expose it as an EXIF/TIFF payload for unified decode.
                block.kind = ContainerBlockKind::Exif;
                sink_emit(sink, block);
            }
            return;
        }

        if (box.type == fourcc('c', 'o', 'l', 'r')) {
            if (payload_size < 3) {
                return;
            }
            const uint8_t method = u8(bytes[payload_off + 0]);
            if (method == 2 || method == 3) {
                ContainerBlockRef block;
                block.format       = ContainerFormat::Jp2;
                block.kind         = ContainerBlockKind::Icc;
                block.outer_offset = box.offset;
                block.outer_size   = box.size;
                block.data_offset  = payload_off + 3;
                block.data_size    = payload_size - 3;
                block.id           = box.type;
                block.aux_u32      = method;
                sink_emit(sink, block);
            }
            return;
        }
    }

}  // namespace

ScanResult
scan_jp2(std::span<const std::byte> bytes,
         std::span<ContainerBlockRef> out) noexcept
{
    BlockSink sink;
    sink.out = out.data();
    sink.cap = static_cast<uint32_t>(out.size());

    if (bytes.size() < 12) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }

    uint32_t first_size = 0;
    uint32_t first_type = 0;
    if (!read_u32be(bytes, 0, &first_size)
        || !read_u32be(bytes, 4, &first_type)) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }
    if (first_size != 12 || first_type != fourcc('j', 'P', ' ', ' ')) {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
    }
    if (!match_bytes(bytes, 8, kJp2Signature.data(), 4)) {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
    }

    uint64_t offset    = 0;
    const uint64_t end = bytes.size();
    while (offset < end) {
        BmffBox box;
        if (!parse_bmff_box(bytes, offset, end, &box)) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }

        scan_jp2_box_payload(bytes, box, &sink);

        // jp2h contains child boxes (ihdr/colr/...). Scan its children for ICC.
        if (box.type == fourcc('j', 'p', '2', 'h')) {
            uint64_t child_off       = box.offset + box.header_size;
            const uint64_t child_end = box.offset + box.size;
            while (child_off < child_end) {
                BmffBox child;
                if (!parse_bmff_box(bytes, child_off, child_end, &child)) {
                    break;
                }
                scan_jp2_box_payload(bytes, child, &sink);
                child_off += child.size;
                if (child.size == 0) {
                    break;
                }
            }
        }

        offset += box.size;
        if (box.size == 0) {
            break;
        }
    }

    return sink.result;
}


ScanResult
scan_jxl(std::span<const std::byte> bytes,
         std::span<ContainerBlockRef> out) noexcept
{
    BlockSink sink;
    sink.out = out.data();
    sink.cap = static_cast<uint32_t>(out.size());

    if (bytes.size() < 12) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }

    uint32_t first_size = 0;
    uint32_t first_type = 0;
    if (!read_u32be(bytes, 0, &first_size)
        || !read_u32be(bytes, 4, &first_type)) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }
    if (first_size != 12 || first_type != fourcc('J', 'X', 'L', ' ')) {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
    }
    if (!match_bytes(bytes, 8, kJp2Signature.data(), 4)) {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
    }

    uint64_t offset    = 0;
    const uint64_t end = bytes.size();
    while (offset < end) {
        BmffBox box;
        if (!parse_bmff_box(bytes, offset, end, &box)) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }

        const uint64_t payload_off  = box.offset + box.header_size;
        const uint64_t payload_size = box.size - box.header_size;

        if (box.type == fourcc('E', 'x', 'i', 'f')) {
            ContainerBlockRef block;
            block.format       = ContainerFormat::Jxl;
            block.kind         = ContainerBlockKind::Exif;
            block.outer_offset = box.offset;
            block.outer_size   = box.size;
            block.data_offset  = payload_off;
            block.data_size    = payload_size;
            block.id           = box.type;
            skip_bmff_exif_offset(&block, bytes);
            sink_emit(&sink, block);
        } else if (box.type == fourcc('x', 'm', 'l', ' ')) {
            ContainerBlockRef block;
            block.format       = ContainerFormat::Jxl;
            block.kind         = ContainerBlockKind::Xmp;
            block.outer_offset = box.offset;
            block.outer_size   = box.size;
            block.data_offset  = payload_off;
            block.data_size    = payload_size;
            block.id           = box.type;
            sink_emit(&sink, block);
        } else if (box.type == fourcc('b', 'r', 'o', 'b')) {
            if (payload_size >= 4) {
                uint32_t realtype = 0;
                if (read_u32be(bytes, payload_off, &realtype)) {
                    ContainerBlockRef block;
                    block.format       = ContainerFormat::Jxl;
                    block.kind         = ContainerBlockKind::CompressedMetadata;
                    block.compression  = BlockCompression::Brotli;
                    block.chunking     = BlockChunking::BrobU32BeRealTypePrefix;
                    block.outer_offset = box.offset;
                    block.outer_size   = box.size;
                    block.data_offset  = payload_off + 4;
                    block.data_size    = payload_size - 4;
                    block.id           = box.type;
                    block.aux_u32      = realtype;
                    sink_emit(&sink, block);
                }
            }
        }

        offset += box.size;
        if (box.size == 0) {
            break;
        }
    }

    return sink.result;
}


namespace {

    struct BmffMetaItem final {
        uint32_t item_id        = 0;
        uint32_t item_type      = 0;
        ContainerBlockKind kind = ContainerBlockKind::Unknown;
    };

    static void bmff_note_brand(uint32_t brand, bool* is_heif, bool* is_avif,
                                bool* is_cr3) noexcept
    {
        if (brand == fourcc('c', 'r', 'x', ' ')
            || brand == fourcc('C', 'R', '3', ' ')) {
            *is_cr3 = true;
        }

        if (brand == fourcc('a', 'v', 'i', 'f')
            || brand == fourcc('a', 'v', 'i', 's')) {
            *is_avif = true;
        }

        if (brand == fourcc('m', 'i', 'f', '1')
            || brand == fourcc('m', 's', 'f', '1')
            || brand == fourcc('h', 'e', 'i', 'c')
            || brand == fourcc('h', 'e', 'i', 'x')
            || brand == fourcc('h', 'e', 'v', 'c')
            || brand == fourcc('h', 'e', 'v', 'x')) {
            *is_heif = true;
        }
    }


    static bool bmff_format_from_ftyp(std::span<const std::byte> bytes,
                                      const BmffBox& ftyp,
                                      ContainerFormat* out) noexcept
    {
        const uint64_t payload_off  = ftyp.offset + ftyp.header_size;
        const uint64_t payload_size = ftyp.size - ftyp.header_size;
        if (payload_size < 8) {
            return false;
        }

        uint32_t major_brand = 0;
        if (!read_u32be(bytes, payload_off + 0, &major_brand)) {
            return false;
        }

        bool is_heif = false;
        bool is_avif = false;
        bool is_cr3  = false;
        bmff_note_brand(major_brand, &is_heif, &is_avif, &is_cr3);

        const uint64_t brands_off = payload_off + 8;
        const uint64_t brands_end = payload_off + payload_size;
        for (uint64_t off = brands_off; off + 4 <= brands_end; off += 4) {
            uint32_t brand = 0;
            if (!read_u32be(bytes, off, &brand)) {
                return false;
            }
            bmff_note_brand(brand, &is_heif, &is_avif, &is_cr3);
        }

        if (is_cr3) {
            *out = ContainerFormat::Cr3;
            return true;
        }
        if (is_avif) {
            *out = ContainerFormat::Avif;
            return true;
        }
        if (is_heif) {
            *out = ContainerFormat::Heif;
            return true;
        }
        return false;
    }


    static bool read_uint_be_n(std::span<const std::byte> bytes,
                               uint64_t offset, uint32_t n,
                               uint64_t* out) noexcept
    {
        if (n == 0) {
            *out = 0;
            return true;
        }
        if (n > 8 || offset + n > bytes.size()) {
            return false;
        }
        uint64_t v = 0;
        for (uint32_t i = 0; i < n; ++i) {
            v = (v << 8) | static_cast<uint64_t>(u8(bytes[offset + i]));
        }
        *out = v;
        return true;
    }


    static bool find_cstring_end(std::span<const std::byte> bytes,
                                 uint64_t start, uint64_t end,
                                 uint64_t* out_end) noexcept
    {
        const uint64_t limit = (end < bytes.size()) ? end : bytes.size();
        for (uint64_t p = start; p < limit; ++p) {
            if (u8(bytes[p]) == 0) {
                *out_end = p;
                return true;
            }
        }
        return false;
    }


    static uint8_t ascii_lower_u8(uint8_t c) noexcept
    {
        if (c >= static_cast<uint8_t>('A') && c <= static_cast<uint8_t>('Z')) {
            return static_cast<uint8_t>(c + 0x20U);
        }
        return c;
    }


    static bool cstring_equals_icase(std::span<const std::byte> bytes,
                                     uint64_t start, uint64_t end,
                                     const char* s, uint32_t s_len) noexcept
    {
        uint64_t s_end = 0;
        if (!find_cstring_end(bytes, start, end, &s_end)) {
            return false;
        }
        const uint64_t len = s_end - start;
        if (len != s_len) {
            return false;
        }
        if (start + len > bytes.size()) {
            return false;
        }
        for (uint32_t i = 0; i < s_len; ++i) {
            const uint8_t a = ascii_lower_u8(u8(bytes[start + i]));
            const uint8_t b = ascii_lower_u8(static_cast<uint8_t>(s[i]));
            if (a != b) {
                return false;
            }
        }
        return true;
    }


    static bool ascii_span_equals_icase(std::span<const std::byte> bytes,
                                        uint64_t start, uint64_t len,
                                        const char* s, uint32_t s_len) noexcept
    {
        if (len != s_len || start + len > bytes.size()) {
            return false;
        }
        for (uint32_t i = 0; i < s_len; ++i) {
            const uint8_t a = ascii_lower_u8(u8(bytes[start + i]));
            const uint8_t b = ascii_lower_u8(static_cast<uint8_t>(s[i]));
            if (a != b) {
                return false;
            }
        }
        return true;
    }


    static bool bmff_mime_content_is_xmp(std::span<const std::byte> bytes,
                                         uint64_t start, uint64_t end) noexcept
    {
        uint64_t s_end = 0;
        if (!find_cstring_end(bytes, start, end, &s_end)) {
            return false;
        }
        if (start > s_end || s_end > bytes.size()) {
            return false;
        }

        uint64_t token_begin = start;
        while (token_begin < s_end) {
            const uint8_t c = u8(bytes[token_begin]);
            if (c != static_cast<uint8_t>(' ')
                && c != static_cast<uint8_t>('\t')) {
                break;
            }
            token_begin += 1;
        }

        uint64_t token_end = token_begin;
        while (token_end < s_end) {
            const uint8_t c = u8(bytes[token_end]);
            if (c == static_cast<uint8_t>(';') || c == static_cast<uint8_t>(' ')
                || c == static_cast<uint8_t>('\t')) {
                break;
            }
            token_end += 1;
        }

        if (token_end <= token_begin) {
            return false;
        }
        const uint64_t token_len = token_end - token_begin;

        static constexpr char kXmpMimeRdf[] = "application/rdf+xml";
        static constexpr char kXmpMimeXmp[] = "application/xmp+xml";
        static constexpr char kXmlText[]    = "text/xml";
        static constexpr char kXmlApp[]     = "application/xml";

        return ascii_span_equals_icase(bytes, token_begin, token_len,
                                       kXmpMimeRdf, sizeof(kXmpMimeRdf) - 1)
               || ascii_span_equals_icase(bytes, token_begin, token_len,
                                          kXmpMimeXmp, sizeof(kXmpMimeXmp) - 1)
               || ascii_span_equals_icase(bytes, token_begin, token_len,
                                          kXmlText, sizeof(kXmlText) - 1)
               || ascii_span_equals_icase(bytes, token_begin, token_len,
                                          kXmlApp, sizeof(kXmlApp) - 1);
    }


    static bool bmff_collect_meta_items(std::span<const std::byte> bytes,
                                        const BmffBox& iinf,
                                        std::span<BmffMetaItem> out_items,
                                        uint32_t* out_count) noexcept
    {
        const uint64_t payload_off = iinf.offset + iinf.header_size;
        const uint64_t payload_end = iinf.offset + iinf.size;
        if (payload_off + 4 > payload_end) {
            return false;
        }

        const uint8_t version = u8(bytes[payload_off + 0]);
        uint64_t p            = payload_off + 4;

        uint32_t entry_count = 0;
        if (version < 2) {
            uint16_t ec16 = 0;
            if (!read_u16be(bytes, p, &ec16)) {
                return false;
            }
            entry_count = ec16;
            p += 2;
        } else {
            uint32_t ec32 = 0;
            if (!read_u32be(bytes, p, &ec32)) {
                return false;
            }
            entry_count = ec32;
            p += 4;
        }

        const uint32_t kMaxEntries = 4096;
        if (entry_count > kMaxEntries) {
            return false;
        }

        uint32_t seen = 0;
        while (p < payload_end && seen < entry_count) {
            BmffBox infe;
            if (!parse_bmff_box(bytes, p, payload_end, &infe)) {
                return false;
            }

            if (infe.type == fourcc('i', 'n', 'f', 'e')) {
                const uint64_t infe_payload_off = infe.offset
                                                  + infe.header_size;
                const uint64_t infe_end = infe.offset + infe.size;
                if (infe_payload_off + 4 > infe_end) {
                    return false;
                }

                const uint8_t infe_ver  = u8(bytes[infe_payload_off + 0]);
                uint64_t q              = infe_payload_off + 4;
                uint32_t item_id        = 0;
                uint32_t item_type      = 0;
                ContainerBlockKind kind = ContainerBlockKind::Unknown;

                if (infe_ver < 2) {
                    // Legacy `infe` (v0/v1):
                    // item_ID(16) + item_protection_index(16) + item_name(cstr)
                    // + content_type(cstr) + content_encoding(cstr).
                    uint16_t id16 = 0;
                    uint16_t prot = 0;
                    if (!read_u16be(bytes, q, &id16)
                        || !read_u16be(bytes, q + 2, &prot)) {
                        return false;
                    }
                    item_id = id16;
                    q += 4;
                    (void)prot;

                    uint64_t name_end = 0;
                    if (!find_cstring_end(bytes, q, infe_end, &name_end)) {
                        return false;
                    }
                    if (cstring_equals_icase(bytes, q, infe_end, "Exif", 4)) {
                        kind      = ContainerBlockKind::Exif;
                        item_type = fourcc('E', 'x', 'i', 'f');
                    }
                    q = name_end + 1;

                    if (q < infe_end
                        && bmff_mime_content_is_xmp(bytes, q, infe_end)) {
                        kind      = ContainerBlockKind::Xmp;
                        item_type = fourcc('x', 'm', 'l', ' ');
                    }

                    uint64_t ct_end = 0;
                    if (find_cstring_end(bytes, q, infe_end, &ct_end)) {
                        q                = ct_end + 1;
                        uint64_t enc_end = 0;
                        if (find_cstring_end(bytes, q, infe_end, &enc_end)) {
                            q = enc_end + 1;
                        } else {
                            q = infe_end;
                        }
                    } else if (kind == ContainerBlockKind::Unknown) {
                        // Can't read content_type and no Exif hint in name.
                        continue;
                    }
                } else {
                    if (infe_ver == 2) {
                        uint16_t id16 = 0;
                        if (!read_u16be(bytes, q, &id16)) {
                            return false;
                        }
                        item_id = id16;
                        q += 2;
                    } else {
                        if (!read_u32be(bytes, q, &item_id)) {
                            return false;
                        }
                        q += 4;
                    }

                    uint16_t prot = 0;
                    if (!read_u16be(bytes, q, &prot)) {
                        return false;
                    }
                    q += 2;
                    (void)prot;

                    if (!read_u32be(bytes, q, &item_type)) {
                        return false;
                    }
                    q += 4;

                    uint64_t name_end = 0;
                    if (!find_cstring_end(bytes, q, infe_end, &name_end)) {
                        return false;
                    }
                    q = name_end + 1;

                    if (item_type == fourcc('E', 'x', 'i', 'f')) {
                        kind = ContainerBlockKind::Exif;
                    } else if (item_type == fourcc('x', 'm', 'l', ' ')) {
                        kind = ContainerBlockKind::Xmp;
                    } else if (item_type == fourcc('m', 'i', 'm', 'e')) {
                        if (bmff_mime_content_is_xmp(bytes, q, infe_end)) {
                            kind = ContainerBlockKind::Xmp;
                        }

                        uint64_t ct_end = 0;
                        if (!find_cstring_end(bytes, q, infe_end, &ct_end)) {
                            // Some files omit the content-type terminator.
                            // Skip this item instead of failing the full scan.
                            continue;
                        }
                        q                = ct_end + 1;
                        uint64_t enc_end = 0;
                        if (find_cstring_end(bytes, q, infe_end, &enc_end)) {
                            q = enc_end + 1;
                        } else {
                            // Encoding is optional in practice; treat missing
                            // terminator as an empty encoding.
                            q = infe_end;
                        }
                    }
                }

                if (kind != ContainerBlockKind::Unknown) {
                    if (*out_count < out_items.size()) {
                        BmffMetaItem item;
                        item.item_id          = item_id;
                        item.item_type        = item_type;
                        item.kind             = kind;
                        out_items[*out_count] = item;
                    }
                    *out_count += 1;
                }
            }

            p += infe.size;
            if (infe.size == 0) {
                break;
            }
            seen += 1;
        }
        return true;
    }


    static const BmffMetaItem*
    bmff_find_item(std::span<const BmffMetaItem> items,
                   uint32_t item_id) noexcept
    {
        for (size_t i = 0; i < items.size(); ++i) {
            if (items[i].item_id == item_id) {
                return &items[i];
            }
        }
        return nullptr;
    }


    static bool bmff_emit_items_from_iloc(std::span<const std::byte> bytes,
                                          const BmffBox& iloc,
                                          const BmffBox* idat,
                                          std::span<const BmffMetaItem> items,
                                          ContainerFormat format,
                                          BlockSink* sink) noexcept
    {
        const uint64_t payload_off = iloc.offset + iloc.header_size;
        const uint64_t payload_end = iloc.offset + iloc.size;
        if (payload_off + 4 > payload_end) {
            return false;
        }

        const uint8_t version = u8(bytes[payload_off + 0]);
        uint64_t p            = payload_off + 4;

        if (p + 2 > payload_end) {
            return false;
        }
        const uint8_t a          = u8(bytes[p + 0]);
        const uint8_t b          = u8(bytes[p + 1]);
        const uint32_t off_size  = (a >> 4) & 0x0F;
        const uint32_t len_size  = (a >> 0) & 0x0F;
        const uint32_t base_size = (b >> 4) & 0x0F;
        const uint32_t idx_size  = (b >> 0) & 0x0F;
        p += 2;

        if (off_size > 8 || len_size > 8 || base_size > 8 || idx_size > 8) {
            return false;
        }

        uint32_t item_count = 0;
        if (version < 2) {
            uint16_t c16 = 0;
            if (!read_u16be(bytes, p, &c16)) {
                return false;
            }
            item_count = c16;
            p += 2;
        } else {
            uint32_t c32 = 0;
            if (!read_u32be(bytes, p, &c32)) {
                return false;
            }
            item_count = c32;
            p += 4;
        }

        const uint32_t kMaxItems = 1U << 16;
        if (item_count > kMaxItems) {
            return false;
        }

        uint64_t idat_payload_off = 0;
        uint64_t idat_payload_end = 0;
        bool has_idat             = false;
        if (idat != nullptr && idat->size > 0) {
            idat_payload_off = idat->offset + idat->header_size;
            idat_payload_end = idat->offset + idat->size;
            if (idat_payload_off > idat_payload_end) {
                return false;
            }
            has_idat = true;
        }

        for (uint32_t i = 0; i < item_count; ++i) {
            uint32_t item_id = 0;
            if (version < 2) {
                uint16_t id16 = 0;
                if (!read_u16be(bytes, p, &id16)) {
                    return false;
                }
                item_id = id16;
                p += 2;
            } else {
                if (!read_u32be(bytes, p, &item_id)) {
                    return false;
                }
                p += 4;
            }

            uint32_t construction_method = 0;
            if (version == 1 || version == 2) {
                uint16_t cm = 0;
                if (!read_u16be(bytes, p, &cm)) {
                    return false;
                }
                construction_method = static_cast<uint32_t>(cm & 0x000F);
                p += 2;
            }

            uint16_t data_ref = 0;
            if (!read_u16be(bytes, p, &data_ref)) {
                return false;
            }
            p += 2;
            (void)data_ref;

            uint64_t base_off = 0;
            if (!read_uint_be_n(bytes, p, base_size, &base_off)) {
                return false;
            }
            p += base_size;

            uint16_t extent_count = 0;
            if (!read_u16be(bytes, p, &extent_count)) {
                return false;
            }
            p += 2;

            const uint32_t kMaxExtents = 1U << 14;
            if (extent_count > kMaxExtents) {
                return false;
            }

            const BmffMetaItem* item = bmff_find_item(items, item_id);

            uint64_t logical_off = 0;
            for (uint32_t e = 0; e < extent_count; ++e) {
                uint64_t extent_index = 0;
                if ((version == 1 || version == 2) && idx_size > 0) {
                    if (!read_uint_be_n(bytes, p, idx_size, &extent_index)) {
                        return false;
                    }
                    p += idx_size;
                }
                (void)extent_index;

                uint64_t extent_off = 0;
                if (!read_uint_be_n(bytes, p, off_size, &extent_off)) {
                    return false;
                }
                p += off_size;

                uint64_t extent_len = 0;
                if (!read_uint_be_n(bytes, p, len_size, &extent_len)) {
                    return false;
                }
                p += len_size;

                if (item == nullptr) {
                    continue;
                }

                uint64_t file_off = 0;
                if (construction_method == 1) {
                    if (!has_idat) {
                        continue;
                    }
                    if (idat_payload_off > UINT64_MAX - base_off) {
                        return false;
                    }
                    file_off = idat_payload_off + base_off;
                } else if (construction_method == 0) {
                    file_off = base_off;
                } else {
                    continue;
                }

                if (file_off > UINT64_MAX - extent_off) {
                    return false;
                }
                file_off += extent_off;

                const uint64_t size = static_cast<uint64_t>(bytes.size());
                if (file_off > size || extent_len > size - file_off) {
                    return false;
                }
                if (construction_method == 1) {
                    if (file_off + extent_len > idat_payload_end) {
                        return false;
                    }
                }

                ContainerBlockRef block;
                block.format       = format;
                block.kind         = item->kind;
                block.outer_offset = file_off;
                block.outer_size   = extent_len;
                block.data_offset  = file_off;
                block.data_size    = extent_len;
                block.id           = item->item_type;
                block.group        = static_cast<uint64_t>(item_id);

                if (block.kind == ContainerBlockKind::Exif) {
                    if (e == 0) {
                        skip_bmff_exif_offset(&block, bytes);
                        skip_exif_preamble(&block, bytes);
                    }
                }

                if (extent_count > 1) {
                    block.part_index     = e;
                    block.part_count     = extent_count;
                    block.logical_offset = logical_off;
                }
                if (block.data_size <= UINT64_MAX - logical_off) {
                    logical_off += block.data_size;
                }

                sink_emit(sink, block);
            }
        }

        return true;
    }

    static void bmff_scan_ipco_for_icc(std::span<const std::byte> bytes,
                                       const BmffBox& ipco,
                                       ContainerFormat format,
                                       BlockSink* sink) noexcept
    {
        if (sink->result.status != ScanStatus::Ok) {
            return;
        }

        const uint64_t payload_off = ipco.offset + ipco.header_size;
        const uint64_t payload_end = ipco.offset + ipco.size;
        if (payload_off > payload_end || payload_end > bytes.size()) {
            return;
        }

        uint64_t off             = payload_off;
        const uint32_t kMaxProps = 1U << 16;
        uint32_t seen            = 0;
        while (off + 8 <= payload_end) {
            seen += 1;
            if (seen > kMaxProps) {
                // Avoid pathological property lists; treat as malformed meta.
                sink->result.status = ScanStatus::Malformed;
                return;
            }

            BmffBox child;
            if (!parse_bmff_box(bytes, off, payload_end, &child)) {
                break;
            }

            if (child.type == fourcc('c', 'o', 'l', 'r')) {
                const uint64_t colr_payload_off = child.offset
                                                  + child.header_size;
                const uint64_t colr_payload_end  = child.offset + child.size;
                const uint64_t colr_payload_size = child.size
                                                   - child.header_size;
                if (colr_payload_off + 4 <= colr_payload_end
                    && colr_payload_end <= bytes.size()
                    && colr_payload_size >= 4) {
                    uint32_t colr_type = 0;
                    if (read_u32be(bytes, colr_payload_off, &colr_type)) {
                        if (colr_type == fourcc('p', 'r', 'o', 'f')
                            || colr_type == fourcc('r', 'I', 'C', 'C')) {
                            ContainerBlockRef block;
                            block.format       = format;
                            block.kind         = ContainerBlockKind::Icc;
                            block.outer_offset = child.offset;
                            block.outer_size   = child.size;
                            block.data_offset  = colr_payload_off + 4;
                            block.data_size    = colr_payload_size - 4;
                            block.id           = child.type;
                            block.aux_u32      = colr_type;
                            sink_emit(sink, block);
                        }
                    }
                }
            }

            off += child.size;
            if (child.size == 0) {
                break;
            }
        }
    }

    static void bmff_scan_iprp_for_icc(std::span<const std::byte> bytes,
                                       const BmffBox& iprp,
                                       ContainerFormat format,
                                       BlockSink* sink) noexcept
    {
        if (sink->result.status != ScanStatus::Ok) {
            return;
        }

        const uint64_t payload_off = iprp.offset + iprp.header_size;
        const uint64_t payload_end = iprp.offset + iprp.size;
        if (payload_off > payload_end || payload_end > bytes.size()) {
            return;
        }

        uint64_t off             = payload_off;
        const uint32_t kMaxBoxes = 1U << 16;
        uint32_t seen            = 0;
        while (off + 8 <= payload_end) {
            seen += 1;
            if (seen > kMaxBoxes) {
                sink->result.status = ScanStatus::Malformed;
                return;
            }

            BmffBox child;
            if (!parse_bmff_box(bytes, off, payload_end, &child)) {
                break;
            }

            if (child.type == fourcc('i', 'p', 'c', 'o')) {
                bmff_scan_ipco_for_icc(bytes, child, format, sink);
                if (sink->result.status != ScanStatus::Ok) {
                    return;
                }
            }

            off += child.size;
            if (child.size == 0) {
                break;
            }
        }
    }


    static void bmff_scan_meta_box(std::span<const std::byte> bytes,
                                   const BmffBox& meta, ContainerFormat format,
                                   BlockSink* sink) noexcept
    {
        const uint64_t payload_off  = meta.offset + meta.header_size;
        const uint64_t payload_size = meta.size - meta.header_size;
        if (payload_size < 4) {
            sink->result.status = ScanStatus::Malformed;
            return;
        }

        BmffBox iinf {};
        BmffBox iloc {};
        BmffBox idat {};
        BmffBox iprp {};
        bool has_iinf = false;
        bool has_iloc = false;
        bool has_idat = false;
        bool has_iprp = false;

        uint64_t child_off       = payload_off + 4;  // FullBox header.
        const uint64_t child_end = meta.offset + meta.size;
        while (child_off < child_end) {
            BmffBox child;
            if (!parse_bmff_box(bytes, child_off, child_end, &child)) {
                break;
            }

            if (child.type == fourcc('i', 'i', 'n', 'f')) {
                iinf     = child;
                has_iinf = true;
            } else if (child.type == fourcc('i', 'l', 'o', 'c')) {
                iloc     = child;
                has_iloc = true;
            } else if (child.type == fourcc('i', 'd', 'a', 't')) {
                idat     = child;
                has_idat = true;
            } else if (child.type == fourcc('i', 'p', 'r', 'p')) {
                iprp     = child;
                has_iprp = true;
            }

            child_off += child.size;
            if (child.size == 0) {
                break;
            }
        }

        std::array<BmffMetaItem, 32> items {};
        uint32_t items_count = 0;
        if (has_iinf) {
            if (!bmff_collect_meta_items(bytes, iinf, items, &items_count)) {
                sink->result.status = ScanStatus::Malformed;
                return;
            }
        }

        if (items_count > items.size()) {
            items_count = static_cast<uint32_t>(items.size());
        }

        if (has_iloc && items_count > 0) {
            const BmffBox* idat_ptr = has_idat ? &idat : nullptr;
            if (!bmff_emit_items_from_iloc(
                    bytes, iloc, idat_ptr,
                    std::span<const BmffMetaItem>(items.data(), items_count),
                    format, sink)) {
                sink->result.status = ScanStatus::Malformed;
                return;
            }
        }

        if (has_iprp) {
            bmff_scan_iprp_for_icc(bytes, iprp, format, sink);
        }
    }


    static bool bmff_is_container_box(uint32_t type) noexcept
    {
        switch (type) {
        case fourcc('m', 'o', 'o', 'v'):
        case fourcc('t', 'r', 'a', 'k'):
        case fourcc('m', 'd', 'i', 'a'):
        case fourcc('m', 'i', 'n', 'f'):
        case fourcc('s', 't', 'b', 'l'):
        case fourcc('e', 'd', 't', 's'):
        case fourcc('d', 'i', 'n', 'f'):
        case fourcc('u', 'd', 't', 'a'): return true;
        default: return false;
        }
    }


    static bool bmff_is_tiff_at(std::span<const std::byte> bytes,
                                uint64_t offset) noexcept
    {
        if (offset + 4 > bytes.size()) {
            return false;
        }
        const uint8_t b0 = u8(bytes[offset + 0]);
        const uint8_t b1 = u8(bytes[offset + 1]);
        const uint8_t b2 = u8(bytes[offset + 2]);
        const uint8_t b3 = u8(bytes[offset + 3]);
        // "II*\0" or "MM\0*"
        return (b0 == 0x49 && b1 == 0x49 && b2 == 0x2A && b3 == 0x00)
               || (b0 == 0x4D && b1 == 0x4D && b2 == 0x00 && b3 == 0x2A);
    }

    static bool bmff_is_cr3_cmt_box(uint32_t type) noexcept
    {
        return type == fourcc('C', 'M', 'T', '1')
               || type == fourcc('C', 'M', 'T', '2')
               || type == fourcc('C', 'M', 'T', '3')
               || type == fourcc('C', 'M', 'T', '4');
    }

    static bool bmff_type_looks_ascii(uint32_t type) noexcept
    {
        for (uint32_t i = 0; i < 4; ++i) {
            const uint8_t b = static_cast<uint8_t>((type >> ((3 - i) * 8))
                                                   & 0xFF);
            if (b < 0x20 || b > 0x7E) {
                return false;
            }
        }
        return true;
    }

    static bool bmff_payload_may_contain_boxes(std::span<const std::byte> bytes,
                                               uint64_t payload_off,
                                               uint64_t payload_end) noexcept
    {
        if (payload_off + 8 > payload_end || payload_end > bytes.size()) {
            return false;
        }

        uint32_t size32 = 0;
        uint32_t type   = 0;
        if (!read_u32be(bytes, payload_off + 0, &size32)
            || !read_u32be(bytes, payload_off + 4, &type)) {
            return false;
        }
        if (!bmff_type_looks_ascii(type)) {
            return false;
        }

        if (size32 == 0) {
            return true;
        }
        if (size32 == 1) {
            if (payload_off + 16 > payload_end) {
                return false;
            }
            uint64_t size64 = 0;
            if (!read_u64be(bytes, payload_off + 8, &size64)) {
                return false;
            }
            if (size64 < 16) {
                return false;
            }
            return payload_off + size64 <= payload_end;
        }
        if (size32 < 8) {
            return false;
        }
        return payload_off + static_cast<uint64_t>(size32) <= payload_end;
    }


    static void bmff_emit_uuid_payload(ContainerFormat format,
                                       ContainerBlockKind kind,
                                       const BmffBox& box,
                                       BlockSink* sink) noexcept
    {
        ContainerBlockRef block;
        block.format       = format;
        block.kind         = kind;
        block.outer_offset = box.offset;
        block.outer_size   = box.size;
        block.data_offset  = box.offset + box.header_size;
        block.data_size    = box.size - box.header_size;
        block.id           = box.type;
        block.chunking     = BlockChunking::Jp2UuidPayload;
        sink_emit(sink, block);
    }


    static void bmff_scan_cr3_canon_uuid(std::span<const std::byte> bytes,
                                         const BmffBox& box,
                                         BlockSink* sink) noexcept
    {
        const uint64_t payload_off0 = box.offset + box.header_size;
        const uint64_t payload_end  = box.offset + box.size;
        if (payload_off0 >= payload_end) {
            return;
        }

        // Some real CR3 files nest the `CMT*` TIFF boxes under intermediate
        // container boxes (e.g. `CNCV`). Best-effort: walk the BMFF box tree
        // under the Canon UUID and emit any `CMT*` payloads that look like TIFF.
        const uint32_t kMaxDepth = 12;
        const uint32_t kMaxBoxes = 1U << 16;

        struct Range final {
            uint64_t begin = 0;
            uint64_t end   = 0;
            uint32_t depth = 0;
        };

        std::array<Range, 64> stack {};
        uint32_t sp = 0;
        stack[sp++] = Range { payload_off0, payload_end, 0 };

        uint32_t seen_boxes = 0;
        while (sp > 0) {
            const Range r = stack[--sp];
            if (r.depth > kMaxDepth) {
                continue;
            }

            uint64_t off = r.begin;
            while (off + 8 <= r.end) {
                seen_boxes += 1;
                if (seen_boxes > kMaxBoxes) {
                    // Treat excessively nested/fragmented UUID payloads as
                    // malformed to avoid pathological scans.
                    sink->result.status = ScanStatus::Malformed;
                    return;
                }

                BmffBox child;
                if (!parse_bmff_box(bytes, off, r.end, &child)) {
                    // The Canon UUID payload may contain non-box data; stop
                    // scanning this range without failing the full scan.
                    break;
                }

                const uint64_t child_payload_off = child.offset
                                                   + child.header_size;
                const uint64_t child_payload_end  = child.offset + child.size;
                const uint64_t child_payload_size = child.size
                                                    - child.header_size;

                if (bmff_is_cr3_cmt_box(child.type)
                    && bmff_is_tiff_at(bytes, child_payload_off)) {
                    ContainerBlockRef block;
                    block.format       = ContainerFormat::Cr3;
                    block.kind         = ContainerBlockKind::Exif;
                    block.outer_offset = child.offset;
                    block.outer_size   = child.size;
                    block.data_offset  = child_payload_off;
                    block.data_size    = child_payload_size;
                    block.id           = child.type;
                    sink_emit(sink, block);
                } else if (child_payload_off + 8 <= child_payload_end
                           && r.depth + 1 <= kMaxDepth && sp < stack.size()) {
                    // Recurse only when the payload begins with a plausible BMFF
                    // box header. This keeps the scan cheap for raw payloads.
                    if (bmff_payload_may_contain_boxes(bytes, child_payload_off,
                                                       child_payload_end)) {
                        stack[sp++] = Range { child_payload_off,
                                              child_payload_end, r.depth + 1 };
                    }
                }

                off += child.size;
                if (child.size == 0) {
                    break;
                }
            }
        }
    }


    static void bmff_scan_for_meta(std::span<const std::byte> bytes,
                                   uint64_t begin, uint64_t end, uint32_t depth,
                                   ContainerFormat format,
                                   BlockSink* sink) noexcept
    {
        if (sink->result.status != ScanStatus::Ok) {
            return;
        }
        if (depth > 8) {
            return;
        }

        uint64_t offset          = begin;
        const uint32_t kMaxBoxes = 1U << 18;
        uint32_t seen            = 0;
        while (offset < end) {
            seen += 1;
            if (seen > kMaxBoxes) {
                sink->result.status = ScanStatus::Malformed;
                return;
            }

            BmffBox box;
            if (!parse_bmff_box(bytes, offset, end, &box)) {
                sink->result.status = ScanStatus::Malformed;
                return;
            }

            if (box.type == fourcc('m', 'e', 't', 'a')) {
                bmff_scan_meta_box(bytes, box, format, sink);
                if (sink->result.status != ScanStatus::Ok) {
                    return;
                }
            } else if (box.type == fourcc('u', 'u', 'i', 'd') && box.has_uuid) {
                if (box.uuid == kJp2UuidXmp) {
                    bmff_emit_uuid_payload(format, ContainerBlockKind::Xmp, box,
                                           sink);
                } else if (format == ContainerFormat::Cr3
                           && box.uuid == kCr3CanonUuid) {
                    bmff_scan_cr3_canon_uuid(bytes, box, sink);
                    if (sink->result.status != ScanStatus::Ok) {
                        return;
                    }
                }
            } else if (bmff_is_container_box(box.type)) {
                const uint64_t child_off = box.offset + box.header_size;
                const uint64_t child_end = box.offset + box.size;
                if (child_off < child_end) {
                    bmff_scan_for_meta(bytes, child_off, child_end, depth + 1,
                                       format, sink);
                    if (sink->result.status != ScanStatus::Ok) {
                        return;
                    }
                }
            }

            offset += box.size;
            if (box.size == 0) {
                break;
            }
        }
    }

}  // namespace

ScanResult
scan_bmff(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept
{
    BlockSink sink;
    sink.out = out.data();
    sink.cap = static_cast<uint32_t>(out.size());

    if (bytes.size() < 8) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }

    BmffBox ftyp;
    if (!parse_bmff_box(bytes, 0, bytes.size(), &ftyp)) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }
    if (ftyp.type != fourcc('f', 't', 'y', 'p')) {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
    }

    ContainerFormat format = ContainerFormat::Unknown;
    if (!bmff_format_from_ftyp(bytes, ftyp, &format)) {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
    }

    bmff_scan_for_meta(bytes, 0, bytes.size(), 0, format, &sink);
    return sink.result;
}


namespace {

    struct TiffConfig final {
        bool le      = true;
        bool bigtiff = false;
    };

    static bool read_tiff_u16(const TiffConfig& cfg,
                              std::span<const std::byte> bytes, uint64_t offset,
                              uint16_t* out) noexcept
    {
        if (cfg.le) {
            if (offset + 2 > bytes.size()) {
                return false;
            }
            const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 0)
                               | static_cast<uint16_t>(u8(bytes[offset + 1])
                                                       << 8);
            *out = v;
            return true;
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


    static uint64_t tiff_type_size(uint16_t type) noexcept
    {
        switch (type) {
        case 1:  // BYTE
        case 2:  // ASCII
        case 6:  // SBYTE
        case 7:  // UNDEFINED
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


    static bool push_offset(uint64_t off, std::span<uint64_t> stack,
                            uint32_t* stack_size) noexcept
    {
        if (off == 0) {
            return true;
        }
        if (*stack_size >= stack.size()) {
            return false;
        }
        stack[*stack_size] = off;
        *stack_size += 1;
        return true;
    }


    static bool already_visited(uint64_t off, std::span<const uint64_t> visited,
                                uint32_t visited_count) noexcept
    {
        for (uint32_t i = 0; i < visited_count; ++i) {
            if (visited[i] == off) {
                return true;
            }
        }
        return false;
    }

}  // namespace

ScanResult
scan_tiff(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept
{
    BlockSink sink;
    sink.out = out.data();
    sink.cap = static_cast<uint32_t>(out.size());

    if (bytes.size() < 8) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }

    TiffConfig cfg;
    const uint8_t b0 = u8(bytes[0]);
    const uint8_t b1 = u8(bytes[1]);
    if (b0 == 0x49 && b1 == 0x49) {
        cfg.le = true;
    } else if (b0 == 0x4D && b1 == 0x4D) {
        cfg.le = false;
    } else {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
    }

    uint16_t version = 0;
    if (!read_tiff_u16(cfg, bytes, 2, &version)) {
        sink.result.status = ScanStatus::Malformed;
        return sink.result;
    }
    if (version == 42) {
        cfg.bigtiff = false;
    } else if (version == 43) {
        cfg.bigtiff = true;
    } else if (version == 0x0055 || version == 0x4F52) {
        // Some TIFF-based RAW formats use a custom "version" field while still
        // storing classic TIFF IFD structures at offset 4:
        // - Panasonic RW2: "IIU\0" (0x0055 in LE form)
        // - Olympus ORF: "IIRO" (0x4F52 in LE form)
        cfg.bigtiff = false;
    } else {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
    }

    // A TIFF/DNG file is itself a TIFF-IFD stream; expose it as a logical EXIF
    // block so decoders can treat "TIFF container" and "TIFF-in-EXIF blob"
    // uniformly.
    {
        ContainerBlockRef block;
        block.format       = ContainerFormat::Tiff;
        block.kind         = ContainerBlockKind::Exif;
        block.outer_offset = 0;
        block.outer_size   = static_cast<uint64_t>(bytes.size());
        block.data_offset  = 0;
        block.data_size    = static_cast<uint64_t>(bytes.size());
        block.id           = 0;
        sink_emit(&sink, block);
    }

    uint64_t first_ifd = 0;
    if (!cfg.bigtiff) {
        uint32_t off32 = 0;
        if (!read_tiff_u32(cfg, bytes, 4, &off32)) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }
        first_ifd = off32;
    } else {
        if (bytes.size() < 16) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }
        uint16_t off_size = 0;
        uint16_t reserved = 0;
        if (!read_tiff_u16(cfg, bytes, 4, &off_size)
            || !read_tiff_u16(cfg, bytes, 6, &reserved)) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }
        if (off_size != 8 || reserved != 0) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }
        if (!read_tiff_u64(cfg, bytes, 8, &first_ifd)) {
            sink.result.status = ScanStatus::Malformed;
            return sink.result;
        }
    }

    std::array<uint64_t, 64> stack_buf {};
    std::array<uint64_t, 64> visited_buf {};
    uint32_t stack_size    = 0;
    uint32_t visited_count = 0;
    (void)push_offset(first_ifd, std::span<uint64_t>(stack_buf), &stack_size);

    while (stack_size > 0) {
        const uint64_t ifd_off = stack_buf[stack_size - 1];
        stack_size -= 1;
        if (ifd_off == 0 || ifd_off >= bytes.size()) {
            continue;
        }
        if (already_visited(ifd_off, std::span<const uint64_t>(visited_buf),
                            visited_count)) {
            continue;
        }
        if (visited_count < visited_buf.size()) {
            visited_buf[visited_count] = ifd_off;
            visited_count += 1;
        }

        uint64_t entry_count      = 0;
        uint64_t entries_off      = 0;
        uint64_t entry_size       = 0;
        uint64_t next_ifd_off_pos = 0;

        if (!cfg.bigtiff) {
            uint16_t n16 = 0;
            if (!read_tiff_u16(cfg, bytes, ifd_off, &n16)) {
                continue;
            }
            entry_count      = n16;
            entries_off      = ifd_off + 2;
            entry_size       = 12;
            next_ifd_off_pos = entries_off + entry_count * entry_size;
            if (next_ifd_off_pos + 4 > bytes.size()) {
                continue;
            }
            uint32_t next32 = 0;
            if (read_tiff_u32(cfg, bytes, next_ifd_off_pos, &next32)) {
                (void)push_offset(next32, std::span<uint64_t>(stack_buf),
                                  &stack_size);
            }
        } else {
            uint64_t n64 = 0;
            if (!read_tiff_u64(cfg, bytes, ifd_off, &n64)) {
                continue;
            }
            entry_count      = n64;
            entries_off      = ifd_off + 8;
            entry_size       = 20;
            next_ifd_off_pos = entries_off + entry_count * entry_size;
            if (next_ifd_off_pos + 8 > bytes.size()) {
                continue;
            }
            uint64_t next64 = 0;
            if (read_tiff_u64(cfg, bytes, next_ifd_off_pos, &next64)) {
                (void)push_offset(next64, std::span<uint64_t>(stack_buf),
                                  &stack_size);
            }
        }

        if (entries_off >= bytes.size()) {
            continue;
        }
        if (entry_count > 0x10000ULL) {
            continue;
        }
        if (entries_off + entry_count * entry_size > bytes.size()) {
            continue;
        }

        for (uint64_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + i * entry_size;
            uint16_t tag        = 0;
            uint16_t type       = 0;
            if (!read_tiff_u16(cfg, bytes, eoff + 0, &tag)
                || !read_tiff_u16(cfg, bytes, eoff + 2, &type)) {
                continue;
            }

            uint64_t count        = 0;
            uint64_t value_or_off = 0;
            if (!cfg.bigtiff) {
                uint32_t c32 = 0;
                uint32_t v32 = 0;
                if (!read_tiff_u32(cfg, bytes, eoff + 4, &c32)
                    || !read_tiff_u32(cfg, bytes, eoff + 8, &v32)) {
                    continue;
                }
                count        = c32;
                value_or_off = v32;
            } else {
                uint64_t c64 = 0;
                uint64_t v64 = 0;
                if (!read_tiff_u64(cfg, bytes, eoff + 4, &c64)
                    || !read_tiff_u64(cfg, bytes, eoff + 12, &v64)) {
                    continue;
                }
                count        = c64;
                value_or_off = v64;
            }

            const uint64_t unit = tiff_type_size(type);
            if (unit == 0) {
                continue;
            }
            if (count > (UINT64_MAX / unit)) {
                continue;
            }
            const uint64_t value_bytes = count * unit;

            uint64_t value_off = 0;
            if (!cfg.bigtiff) {
                if (value_bytes <= 4) {
                    value_off = eoff + 8;
                } else {
                    value_off = value_or_off;
                }
            } else {
                if (value_bytes <= 8) {
                    value_off = eoff + 12;
                } else {
                    value_off = value_or_off;
                }
            }
            if (value_off + value_bytes > bytes.size()) {
                continue;
            }

            // Follow IFD pointers.
            if (tag == 0x8769 || tag == 0x8825 || tag == 0xA005
                || tag == 0x014A) {
                // ExifIFDPointer, GPSInfoIFDPointer, InteropIFDPointer, SubIFDs
                if (tag == 0x014A && count > 1) {
                    // SubIFDs array of offsets.
                    for (uint64_t j = 0; j < count && j < 32; ++j) {
                        uint64_t sub_off = 0;
                        if (!cfg.bigtiff) {
                            uint32_t tmp = 0;
                            if (read_tiff_u32(cfg, bytes, value_off + j * 4,
                                              &tmp)) {
                                sub_off = tmp;
                            }
                        } else {
                            if (type == 4 || type == 13) {
                                uint32_t tmp = 0;
                                if (read_tiff_u32(cfg, bytes, value_off + j * 4,
                                                  &tmp)) {
                                    sub_off = tmp;
                                }
                            } else {
                                (void)read_tiff_u64(cfg, bytes,
                                                    value_off + j * 8,
                                                    &sub_off);
                            }
                        }
                        (void)push_offset(sub_off,
                                          std::span<uint64_t>(stack_buf),
                                          &stack_size);
                    }
                } else {
                    uint64_t ptr = value_or_off;
                    if (value_bytes <= (cfg.bigtiff ? 8 : 4)) {
                        // Inline pointer stored in the value field.
                        if (!cfg.bigtiff) {
                            uint32_t tmp = 0;
                            if (read_tiff_u32(cfg, bytes, value_off, &tmp)) {
                                ptr = tmp;
                            }
                        } else {
                            if (!read_tiff_u64(cfg, bytes, value_off, &ptr)) {
                                ptr = value_or_off;
                            }
                        }
                    }
                    (void)push_offset(ptr, std::span<uint64_t>(stack_buf),
                                      &stack_size);
                }
            }

            if (tag == 0x02BC) {  // XMP
                ContainerBlockRef block;
                block.format       = ContainerFormat::Tiff;
                block.kind         = ContainerBlockKind::Xmp;
                block.outer_offset = value_off;
                block.outer_size   = value_bytes;
                block.data_offset  = value_off;
                block.data_size    = value_bytes;
                block.id           = tag;
                sink_emit(&sink, block);
            } else if (tag == 0x83BB) {  // IPTC-IIM
                ContainerBlockRef block;
                block.format       = ContainerFormat::Tiff;
                block.kind         = ContainerBlockKind::IptcIim;
                block.outer_offset = value_off;
                block.outer_size   = value_bytes;
                block.data_offset  = value_off;
                block.data_size    = value_bytes;
                block.id           = tag;
                sink_emit(&sink, block);
            } else if (tag == 0x8649) {  // Photoshop IRB
                ContainerBlockRef block;
                block.format       = ContainerFormat::Tiff;
                block.kind         = ContainerBlockKind::PhotoshopIrB;
                block.chunking     = BlockChunking::PsIrB8Bim;
                block.outer_offset = value_off;
                block.outer_size   = value_bytes;
                block.data_offset  = value_off;
                block.data_size    = value_bytes;
                block.id           = tag;
                sink_emit(&sink, block);
            } else if (tag == 0x8773) {  // ICC profile
                ContainerBlockRef block;
                block.format       = ContainerFormat::Tiff;
                block.kind         = ContainerBlockKind::Icc;
                block.outer_offset = value_off;
                block.outer_size   = value_bytes;
                block.data_offset  = value_off;
                block.data_size    = value_bytes;
                block.id           = tag;
                sink_emit(&sink, block);
            } else if (tag == 0x927C) {  // MakerNote
                ContainerBlockRef block;
                block.format       = ContainerFormat::Tiff;
                block.kind         = ContainerBlockKind::MakerNote;
                block.outer_offset = value_off;
                block.outer_size   = value_bytes;
                block.data_offset  = value_off;
                block.data_size    = value_bytes;
                block.id           = tag;
                sink_emit(&sink, block);
            }
        }
    }

    return sink.result;
}


ScanResult
scan_auto(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept
{
    if (bytes.size() >= 2 && u8(bytes[0]) == 0xFF && u8(bytes[1]) == 0xD8) {
        return scan_jpeg(bytes, out);
    }
    if (bytes.size() >= kPngSignatureSize
        && match_bytes(bytes, 0, kPngSignature.data(), kPngSignatureSize)) {
        return scan_png(bytes, out);
    }
    if (bytes.size() >= 12 && match(bytes, 0, "RIFF", 4)
        && match(bytes, 8, "WEBP", 4)) {
        return scan_webp(bytes, out);
    }
    if (bytes.size() >= 6
        && (match(bytes, 0, "GIF87a", 6) || match(bytes, 0, "GIF89a", 6))) {
        return scan_gif(bytes, out);
    }
    if (bytes.size() >= 4) {
        const uint8_t b0 = u8(bytes[0]);
        const uint8_t b1 = u8(bytes[1]);
        if ((b0 == 0x49 && b1 == 0x49) || (b0 == 0x4D && b1 == 0x4D)) {
            // TIFF classic and BigTIFF share the first two bytes.
            const uint16_t v = (b0 == 0x49)
                                   ? static_cast<uint16_t>(
                                         u8(bytes[2]) | (u8(bytes[3]) << 8))
                                   : static_cast<uint16_t>((u8(bytes[2]) << 8)
                                                           | u8(bytes[3]));
            if (v == 42 || v == 43 || v == 0x0055 || v == 0x4F52) {
                return scan_tiff(bytes, out);
            }
        }
    }

    // Fujifilm RAF: fixed header, then an embedded TIFF stream.
    if (bytes.size() >= 16 && match(bytes, 0, "FUJIFILMCCD-RAW ", 16)) {
        uint64_t tiff_off = 160;
        if (tiff_off < bytes.size() && looks_like_tiff_at(bytes, tiff_off)) {
            const std::span<const std::byte> tiff = bytes.subspan(
                static_cast<size_t>(tiff_off));
            const ScanResult res   = scan_tiff(tiff, out);
            const uint32_t written = (res.written < out.size())
                                         ? res.written
                                         : static_cast<uint32_t>(out.size());
            for (uint32_t i = 0; i < written; ++i) {
                out[i].outer_offset += tiff_off;
                out[i].data_offset += tiff_off;
                out[i].format = ContainerFormat::Unknown;
            }
            return res;
        }
    }

    // Sigma X3F: the file commonly embeds an "Exif\0\0" preamble followed by a
    // classic TIFF header. Locate and scan that TIFF stream.
    if (bytes.size() >= 4 && match(bytes, 0, "FOVb", 4)) {
        const uint64_t max_search = (bytes.size() < (4ULL * 1024ULL * 1024ULL))
                                        ? static_cast<uint64_t>(bytes.size())
                                        : (4ULL * 1024ULL * 1024ULL);
        for (uint64_t off = 0; off + 10 <= max_search; ++off) {
            if (!match(bytes, off, "Exif", 4) || u8(bytes[off + 4]) != 0
                || u8(bytes[off + 5]) != 0) {
                continue;
            }
            const uint64_t tiff_off = off + 6;
            if (!looks_like_tiff_at(bytes, tiff_off)) {
                continue;
            }
            const std::span<const std::byte> tiff = bytes.subspan(
                static_cast<size_t>(tiff_off));
            const ScanResult res   = scan_tiff(tiff, out);
            const uint32_t written = (res.written < out.size())
                                         ? res.written
                                         : static_cast<uint32_t>(out.size());
            for (uint32_t i = 0; i < written; ++i) {
                out[i].outer_offset += tiff_off;
                out[i].data_offset += tiff_off;
                out[i].format = ContainerFormat::Unknown;
            }
            return res;
        }
    }

    // Canon CRW (CIFF): header with "HEAPCCDR" signature at +6.
    if (bytes.size() >= 14) {
        const uint8_t b0 = u8(bytes[0]);
        const uint8_t b1 = u8(bytes[1]);
        const bool le    = (b0 == 0x49 && b1 == 0x49);
        const bool be    = (b0 == 0x4D && b1 == 0x4D);
        if ((le || be) && match(bytes, 6, "HEAPCCDR", 8)) {
            uint32_t root_off = 0;
            if ((le && read_u32le(bytes, 2, &root_off))
                || (be && read_u32be(bytes, 2, &root_off))) {
                if (root_off >= 14U
                    && static_cast<uint64_t>(root_off) <= bytes.size()) {
                    ScanResult res;
                    res.written = 0;
                    res.needed  = 1;
                    if (out.empty()) {
                        res.status = ScanStatus::OutputTruncated;
                        return res;
                    }

                    ContainerBlockRef block;
                    block.format       = ContainerFormat::Unknown;
                    block.kind         = ContainerBlockKind::Ciff;
                    block.outer_offset = 0;
                    block.outer_size   = static_cast<uint64_t>(bytes.size());
                    block.data_offset  = 0;
                    block.data_size    = static_cast<uint64_t>(bytes.size());
                    block.id           = fourcc('C', 'R', 'W', ' ');
                    block.aux_u32      = root_off;

                    out[0]      = block;
                    res.status  = ScanStatus::Ok;
                    res.written = 1;
                    return res;
                }
            }
        }
    }

    // JP2/JXL signature boxes.
    if (bytes.size() >= 12) {
        uint32_t size = 0;
        uint32_t type = 0;
        if (read_u32be(bytes, 0, &size) && read_u32be(bytes, 4, &type)
            && size == 12 && match_bytes(bytes, 8, kJp2Signature.data(), 4)) {
            if (type == fourcc('j', 'P', ' ', ' ')) {
                return scan_jp2(bytes, out);
            }
            if (type == fourcc('J', 'X', 'L', ' ')) {
                return scan_jxl(bytes, out);
            }
        }
    }

    // ISO-BMFF (`ftyp`) image containers (HEIF/AVIF/CR3).
    if (bytes.size() >= 16) {
        uint32_t size = 0;
        uint32_t type = 0;
        if (read_u32be(bytes, 0, &size) && read_u32be(bytes, 4, &type)
            && type == fourcc('f', 't', 'y', 'p')
            && (size == 0 || size == 1 || size >= 16)) {
            return scan_bmff(bytes, out);
        }
    }

    ScanResult res;
    res.status = ScanStatus::Unsupported;
    return res;
}

}  // namespace openmeta
