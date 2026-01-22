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
        if (block->data_size < 6) {
            return;
        }
        if (match(bytes, block->data_offset, "Exif\0\0", 6)) {
            block->data_offset += 6;
            block->data_size -= 6;
        }
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
        if (static_cast<uint64_t>(tiff_off) >= block->data_size) {
            return;
        }
        block->chunking = BlockChunking::BmffExifTiffOffsetU32Be;
        block->aux_u32  = tiff_off;
        block->data_offset += static_cast<uint64_t>(tiff_off);
        block->data_size -= static_cast<uint64_t>(tiff_off);
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
            if (seg_payload_size >= 6
                && match(bytes, seg_payload_off, "Exif\0\0", 6)) {
                ContainerBlockRef block;
                block.format       = ContainerFormat::Jpeg;
                block.kind         = ContainerBlockKind::Exif;
                block.outer_offset = seg_total_off;
                block.outer_size   = seg_total_size;
                block.data_offset  = seg_payload_off + 6;
                block.data_size    = seg_payload_size - 6;
                block.id           = marker;
                sink_emit(&sink, block);
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
    } else {
        sink.result.status = ScanStatus::Unsupported;
        return sink.result;
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
            if (v == 42 || v == 43) {
                return scan_tiff(bytes, out);
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

    ScanResult res;
    res.status = ScanStatus::Unsupported;
    return res;
}

}  // namespace openmeta
