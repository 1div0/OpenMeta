#pragma once

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file exif_tiff_decode.h
 * \brief Decoder for TIFF-IFD tag streams (used by EXIF and TIFF/DNG).
 */

namespace openmeta {

/// EXIF/TIFF decode result status.
enum class ExifDecodeStatus : uint8_t {
    Ok,
    OutputTruncated,
    Unsupported,
    Malformed,
    LimitExceeded,
};

/// Logical IFD kinds exposed by decode_exif_tiff().
enum class ExifIfdKind : uint8_t {
    Ifd,
    ExifIfd,
    GpsIfd,
    InteropIfd,
    SubIfd,
};

/// Reference to a decoded IFD within the input TIFF byte stream.
struct ExifIfdRef final {
    ExifIfdKind kind = ExifIfdKind::Ifd;
    uint32_t index   = 0;  // For Ifd/SubIfd; otherwise 0.
    uint64_t offset  = 0;
    BlockId block    = kInvalidBlockId;
};

/// Resource limits applied during decode to bound hostile inputs.
struct ExifDecodeLimits final {
    uint32_t max_ifds            = 128;
    uint32_t max_entries_per_ifd = 4096;
    uint32_t max_total_entries   = 200000;
    uint64_t max_value_bytes     = 16ULL * 1024ULL * 1024ULL;
};

/// Decoder options for \ref decode_exif_tiff.
struct ExifDecodeOptions final {
    /// If true, pointer tags are preserved as entries in addition to being followed.
    bool include_pointer_tags = true;
    ExifDecodeLimits limits;
};

/// Aggregated decode statistics.
struct ExifDecodeResult final {
    ExifDecodeStatus status  = ExifDecodeStatus::Ok;
    uint32_t ifds_written    = 0;
    uint32_t ifds_needed     = 0;
    uint32_t entries_decoded = 0;
};

/**
 * \brief Decodes a TIFF header + IFD chain and appends tags into \p store.
 *
 * The decoded entries use:
 * - \ref MetaKeyKind::ExifTag
 * - an IFD token string such as `"ifd0"`, `"exififd"`, `"gpsifd"`, `"subifd0"`
 * - the numeric TIFF tag id.
 *
 * Provenance is recorded in \ref Origin (block + order + wire type/count).
 *
 * \param tiff_bytes TIFF header + IFD stream (from an EXIF blob or a TIFF/DNG file).
 * \param store Destination \ref MetaStore (entries are appended).
 * \param out_ifds Optional output array for decoded IFD references (may be empty).
 * \param options Decode options + limits.
 */
ExifDecodeResult
decode_exif_tiff(std::span<const std::byte> tiff_bytes, MetaStore& store,
                 std::span<ExifIfdRef> out_ifds,
                 const ExifDecodeOptions& options) noexcept;

}  // namespace openmeta
