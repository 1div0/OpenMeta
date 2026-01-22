#pragma once

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace openmeta {

enum class ExifDecodeStatus : uint8_t {
    Ok,
    OutputTruncated,
    Unsupported,
    Malformed,
    LimitExceeded,
};

enum class ExifIfdKind : uint8_t {
    Ifd,
    ExifIfd,
    GpsIfd,
    InteropIfd,
    SubIfd,
};

struct ExifIfdRef final {
    ExifIfdKind kind = ExifIfdKind::Ifd;
    uint32_t index   = 0;  // For Ifd/SubIfd; otherwise 0.
    uint64_t offset  = 0;
    BlockId block    = kInvalidBlockId;
};

struct ExifDecodeLimits final {
    uint32_t max_ifds            = 128;
    uint32_t max_entries_per_ifd = 4096;
    uint32_t max_total_entries   = 200000;
    uint64_t max_value_bytes     = 16ULL * 1024ULL * 1024ULL;
};

struct ExifDecodeOptions final {
    bool include_pointer_tags = true;
    ExifDecodeLimits limits;
};

struct ExifDecodeResult final {
    ExifDecodeStatus status  = ExifDecodeStatus::Ok;
    uint32_t ifds_written    = 0;
    uint32_t ifds_needed     = 0;
    uint32_t entries_decoded = 0;
};

// Decodes a TIFF header + IFD chain (typically from an EXIF blob) and appends
// tags as `exif.tag` entries into `store`. Pointer tags are preserved as normal
// entries and also followed to decode referenced IFDs.
ExifDecodeResult
decode_exif_tiff(std::span<const std::byte> tiff_bytes, MetaStore& store,
                 std::span<ExifIfdRef> out_ifds,
                 const ExifDecodeOptions& options) noexcept;

}  // namespace openmeta
