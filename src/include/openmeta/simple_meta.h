#pragma once

#include "openmeta/container_payload.h"
#include "openmeta/container_scan.h"
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/exr_decode.h"
#include "openmeta/meta_store.h"
#include "openmeta/xmp_decode.h"

#include <cstddef>
#include <span>

/**
 * \file simple_meta.h
 * \brief High-level "read" helper for scanning containers and decoding metadata.
 */

namespace openmeta {

struct SimpleMetaResult final {
    ScanResult scan;
    PayloadResult payload;
    ExrDecodeResult exr;
    ExifDecodeResult exif;
    XmpDecodeResult xmp;
};

/**
 * \brief Scans a file container and decodes supported metadata payloads.
 *
 * Current decode support:
 * - EXIF/TIFF-IFD tags (\ref decode_exif_tiff) from:
 *   - JPEG/PNG/WebP/etc. EXIF blocks
 *   - TIFF/DNG containers (whole file treated as a TIFF-IFD stream)
 *   - ISO-BMFF containers (HEIF/AVIF/CR3) Exif items
 * - ICC profiles (\ref decode_icc_profile)
 * - Photoshop IRB / 8BIM resources (\ref decode_photoshop_irb)
 * - IPTC-IIM dataset streams (\ref decode_iptc_iim)
 * - OpenEXR header attributes (\ref decode_exr_header)
 * - XMP packets (\ref decode_xmp_packet)
 *
 * Caller provides the scratch buffers (blocks + decoded IFD list) to keep the
 * data flow explicit and allocation-free.
 *
 * \param file_bytes Full file bytes in memory.
 * \param store Destination \ref MetaStore (entries are appended).
 * \param out_blocks Scratch buffer for block scanning results.
 * \param out_ifds Scratch buffer for decoded IFD references.
 * \param payload Scratch buffer for reassembled EXIF payload bytes.
 * \param payload_scratch_indices Scratch buffer for payload part indices.
 * \param exif_options EXIF/TIFF decode options and limits.
 * \param payload_options Payload extraction options and limits.
 */
SimpleMetaResult
simple_meta_read(std::span<const std::byte> file_bytes, MetaStore& store,
                 std::span<ContainerBlockRef> out_blocks,
                 std::span<ExifIfdRef> out_ifds, std::span<std::byte> payload,
                 std::span<uint32_t> payload_scratch_indices,
                 const ExifDecodeOptions& exif_options,
                 const PayloadOptions& payload_options) noexcept;

}  // namespace openmeta
