#pragma once

#include "openmeta/container_scan.h"
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/meta_store.h"

#include <cstddef>
#include <span>

/**
 * \file simple_meta.h
 * \brief High-level "read" helper for scanning containers and decoding metadata.
 */

namespace openmeta {

struct SimpleMetaResult final {
    ScanResult scan;
    ExifDecodeResult exif;
};

/**
 * \brief Scans a file container and decodes supported metadata payloads.
 *
 * Current decode support:
 * - EXIF/TIFF-IFD tags (\ref decode_exif_tiff) from:
 *   - JPEG/PNG/WebP/etc. EXIF blocks
 *   - TIFF/DNG containers (whole file treated as a TIFF-IFD stream)
 *
 * Caller provides the scratch buffers (blocks + decoded IFD list) to keep the
 * data flow explicit and allocation-free.
 *
 * \param file_bytes Full file bytes in memory.
 * \param store Destination \ref MetaStore (entries are appended).
 * \param out_blocks Scratch buffer for block scanning results.
 * \param out_ifds Scratch buffer for decoded IFD references.
 * \param exif_options EXIF/TIFF decode options and limits.
 */
SimpleMetaResult
simple_meta_read(std::span<const std::byte> file_bytes, MetaStore& store,
                 std::span<ContainerBlockRef> out_blocks,
                 std::span<ExifIfdRef> out_ifds,
                 const ExifDecodeOptions& exif_options) noexcept;

}  // namespace openmeta
