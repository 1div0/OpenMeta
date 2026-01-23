#pragma once

#include <cstdint>
#include <string_view>

namespace openmeta {

// Returns a human-readable EXIF/TIFF tag name for a given IFD token and tag id.
// Returns an empty view for unknown tags.
std::string_view exif_tag_name(std::string_view ifd, uint16_t tag) noexcept;

}  // namespace openmeta

