#pragma once

#include "exif_tiff_decode_internal.h"

namespace openmeta::exif_internal {

struct GeoTiffTagRef final {
    uint16_t type      = 0;
    uint32_t count32   = 0;
    uint64_t value_off = 0;
    uint64_t value_bytes = 0;
    bool present       = false;
};

void decode_geotiff_keys(const TiffConfig& cfg,
                         std::span<const std::byte> tiff_bytes,
                         const GeoTiffTagRef& key_directory,
                         const GeoTiffTagRef& double_params,
                         const GeoTiffTagRef& ascii_params,
                         MetaStore& store,
                         const ExifDecodeLimits& limits) noexcept;

}  // namespace openmeta::exif_internal

