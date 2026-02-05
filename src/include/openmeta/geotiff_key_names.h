#pragma once

#include <cstdint>
#include <string_view>

/**
 * \file geotiff_key_names.h
 * \brief GeoTIFF GeoKey name lookup.
 */

namespace openmeta {

/// Returns a best-effort GeoTIFF key name for a numeric GeoKey id.
std::string_view
geotiff_key_name(uint16_t key_id) noexcept;

}  // namespace openmeta

