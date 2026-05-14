// SPDX-License-Identifier: Apache-2.0

#include "openmeta/orientation.h"

namespace openmeta {

bool
exif_orientation_is_valid(uint16_t orientation) noexcept
{
    return orientation >= 1U && orientation <= 8U;
}

bool
exif_orientation_is_mirrored(uint16_t orientation) noexcept
{
    switch (orientation) {
    case 2U:
    case 4U:
    case 5U:
    case 7U: return true;
    default: return false;
    }
}

bool
exif_orientation_swaps_width_height(uint16_t orientation) noexcept
{
    switch (orientation) {
    case 5U:
    case 6U:
    case 7U:
    case 8U: return true;
    default: return false;
    }
}

uint16_t
exif_orientation_rotation_degrees_cw(uint16_t orientation,
                                     bool* out_valid) noexcept
{
    bool valid       = true;
    uint16_t degrees = 0U;

    switch (orientation) {
    case 1U:
    case 2U: degrees = 0U; break;
    case 3U:
    case 4U: degrees = 180U; break;
    case 5U:
    case 8U: degrees = 270U; break;
    case 6U:
    case 7U: degrees = 90U; break;
    default:
        valid   = false;
        degrees = 0U;
        break;
    }

    if (out_valid) {
        *out_valid = valid;
    }
    return degrees;
}

uint16_t
exif_orientation_rotation_only(uint16_t orientation) noexcept
{
    switch (orientation) {
    case 1U:
    case 2U: return 1U;
    case 3U:
    case 4U: return 3U;
    case 5U:
    case 8U: return 8U;
    case 6U:
    case 7U: return 6U;
    default: return 0U;
    }
}

const char*
exif_orientation_name(uint16_t orientation) noexcept
{
    switch (orientation) {
    case 1U: return "Horizontal (normal)";
    case 2U: return "Mirror horizontal";
    case 3U: return "Rotate 180";
    case 4U: return "Mirror vertical";
    case 5U: return "Mirror horizontal and rotate 270 CW";
    case 6U: return "Rotate 90 CW";
    case 7U: return "Mirror horizontal and rotate 90 CW";
    case 8U: return "Rotate 270 CW";
    default: return "Invalid orientation";
    }
}

ExifOrientationInterpretation
interpret_exif_orientation(uint16_t orientation) noexcept
{
    ExifOrientationInterpretation out {};
    out.orientation = orientation;
    out.name        = exif_orientation_name(orientation);

    bool valid              = false;
    out.rotation_degrees_cw = exif_orientation_rotation_degrees_cw(orientation,
                                                                   &valid);
    if (!valid) {
        out.status                    = ExifOrientationStatus::InvalidArgument;
        out.rotation_only_orientation = 0U;
        return out;
    }

    out.status                    = ExifOrientationStatus::Ok;
    out.rotation_only_orientation = exif_orientation_rotation_only(orientation);
    out.mirrored                  = exif_orientation_is_mirrored(orientation);
    out.swaps_width_height = exif_orientation_swaps_width_height(orientation);
    return out;
}

}  // namespace openmeta
