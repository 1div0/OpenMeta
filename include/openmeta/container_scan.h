#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace openmeta {

enum class ScanStatus : uint8_t {
    Ok,
    OutputTruncated,
    Unsupported,
    Malformed,
};

enum class ContainerFormat : uint8_t {
    Unknown,
    Jpeg,
    Png,
    Webp,
    Gif,
    Tiff,
    Jp2,
    Jxl,
    Heif,
};

enum class ContainerBlockKind : uint8_t {
    Unknown,
    Exif,
    MakerNote,
    Xmp,
    XmpExtended,
    Icc,
    IptcIim,
    PhotoshopIrB,
    Mpf,
    Comment,
    Text,
    CompressedMetadata,
};

enum class BlockCompression : uint8_t {
    None,
    Deflate,
    Brotli,
};

enum class BlockChunking : uint8_t {
    None,
    JpegApp2SeqTotal,
    JpegXmpExtendedGuidOffset,
    GifSubBlocks,
    BmffExifTiffOffsetU32Be,
    BrobU32BeRealTypePrefix,
    Jp2UuidPayload,
    PsIrB8Bim,
};

struct ContainerBlockRef final {
    ContainerFormat format       = ContainerFormat::Unknown;
    ContainerBlockKind kind      = ContainerBlockKind::Unknown;
    BlockCompression compression = BlockCompression::None;
    BlockChunking chunking       = BlockChunking::None;

    // The outer container block (e.g. JPEG segment, PNG chunk, BMFF box).
    uint64_t outer_offset = 0;
    uint64_t outer_size   = 0;

    // The metadata bytes inside the block (after signatures/prefix fields).
    uint64_t data_offset = 0;
    uint64_t data_size   = 0;

    // Container-specific identifier:
    // - JPEG: marker (0xFFEx)
    // - PNG: chunk type (FourCC)
    // - RIFF/WebP: chunk type (FourCC)
    // - BMFF/JP2/JXL: box type (FourCC)
    // - TIFF: tag id (u16)
    uint32_t id = 0;

    // Optional logical chunking info for reassembly.
    uint32_t part_index     = 0;  // 0-based
    uint32_t part_count     = 0;  // 0 if unknown
    uint64_t logical_offset = 0;  // byte offset within the logical stream
    uint64_t logical_size   = 0;  // total logical size (0 if unknown)
    uint64_t group          = 0;  // stable group id/hash (0 if none)

    // Extra container-specific data (e.g. brob wrapped type, BMFF Exif offset).
    uint32_t aux_u32 = 0;
};

struct ScanResult final {
    ScanStatus status = ScanStatus::Ok;
    uint32_t written  = 0;
    uint32_t needed   = 0;
};

static constexpr uint32_t
fourcc(char a, char b, char c, char d) noexcept
{
    return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24)
           | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16)
           | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8)
           | (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 0);
}

ScanResult
scan_auto(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept;

ScanResult
scan_jpeg(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept;
ScanResult
scan_png(std::span<const std::byte> bytes,
         std::span<ContainerBlockRef> out) noexcept;
ScanResult
scan_webp(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept;
ScanResult
scan_gif(std::span<const std::byte> bytes,
         std::span<ContainerBlockRef> out) noexcept;
ScanResult
scan_tiff(std::span<const std::byte> bytes,
          std::span<ContainerBlockRef> out) noexcept;
ScanResult
scan_jp2(std::span<const std::byte> bytes,
         std::span<ContainerBlockRef> out) noexcept;
ScanResult
scan_jxl(std::span<const std::byte> bytes,
         std::span<ContainerBlockRef> out) noexcept;

}  // namespace openmeta
