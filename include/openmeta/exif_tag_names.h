#pragma once

#include <cstdint>
#include <string_view>

/**
 * \file exif_tag_names.h
 * \brief Human-readable names for common EXIF/TIFF tags.
 */

namespace openmeta {

/**
 * \brief Returns a human-readable EXIF/TIFF tag name for a given IFD token and tag id.
 *
 * The IFD token matches the decoder output (e.g. `"ifd0"`, `"exififd"`, `"gpsifd"`).
 *
 * \return An empty view for unknown tags.
 */
std::string_view
exif_tag_name(std::string_view ifd, uint16_t tag) noexcept;

}  // namespace openmeta
