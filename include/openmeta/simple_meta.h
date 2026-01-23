#pragma once

#include "openmeta/container_scan.h"
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/meta_store.h"

#include <cstddef>
#include <span>

namespace openmeta {

struct SimpleMetaResult final {
    ScanResult scan;
    ExifDecodeResult exif;
};

// High-level helper that scans a file container and decodes supported payloads.
// Currently: EXIF/TIFF-IFD tags only (including TIFF/DNG containers).
//
// Caller controls all scratch buffers (blocks + decoded IFD list) to keep data
// flow explicit and allocation-free.
SimpleMetaResult
simple_meta_read(std::span<const std::byte> file_bytes, MetaStore& store,
                 std::span<ContainerBlockRef> out_blocks,
                 std::span<ExifIfdRef> out_ifds,
                 const ExifDecodeOptions& exif_options) noexcept;

}  // namespace openmeta
