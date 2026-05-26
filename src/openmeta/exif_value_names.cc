// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_value_names.h"

namespace openmeta {

const char*
tiff_compression_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Uncompressed";
    case 2U: return "CCITT 1D";
    case 3U: return "T4/Group 3 Fax";
    case 4U: return "T6/Group 4 Fax";
    case 5U: return "LZW";
    case 6U: return "JPEG (old-style)";
    case 7U: return "JPEG";
    case 8U: return "Adobe Deflate";
    case 9U: return "JBIG B&W";
    case 10U: return "JBIG Color";
    case 32770U: return "Samsung SRW Compressed";
    case 32773U: return "PackBits";
    default: return "";
    }
}

const char*
tiff_photometric_interpretation_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "WhiteIsZero";
    case 1U: return "BlackIsZero";
    case 2U: return "RGB";
    case 3U: return "RGB Palette";
    case 4U: return "Transparency Mask";
    case 5U: return "CMYK";
    case 6U: return "YCbCr";
    case 8U: return "CIELab";
    case 9U: return "ICCLab";
    case 10U: return "ITULab";
    case 32803U: return "Color Filter Array";
    case 32844U: return "Pixar LogL";
    case 32845U: return "Pixar LogLuv";
    case 34892U: return "Linear Raw";
    default: return "";
    }
}

const char*
tiff_planar_configuration_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Chunky";
    case 2U: return "Planar";
    default: return "";
    }
}

const char*
tiff_resolution_unit_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "None";
    case 2U: return "inches";
    case 3U: return "cm";
    default: return "";
    }
}

const char*
exif_exposure_program_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Not defined";
    case 1U: return "Manual";
    case 2U: return "Program AE";
    case 3U: return "Aperture-priority AE";
    case 4U: return "Shutter speed priority AE";
    case 5U: return "Creative (Slow speed)";
    case 6U: return "Action (High speed)";
    case 7U: return "Portrait";
    case 8U: return "Landscape";
    case 9U: return "Bulb";
    default: return "";
    }
}

const char*
exif_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Unknown";
    case 1U: return "Average";
    case 2U: return "Center-weighted average";
    case 3U: return "Spot";
    case 4U: return "Multi-spot";
    case 5U: return "Multi-segment";
    case 6U: return "Partial";
    case 255U: return "Other";
    default: return "";
    }
}

const char*
exif_light_source_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Unknown";
    case 1U: return "Daylight";
    case 2U: return "Fluorescent";
    case 3U: return "Tungsten (incandescent)";
    case 4U: return "Flash";
    case 9U: return "Fine weather";
    case 10U: return "Cloudy";
    case 11U: return "Shade";
    case 12U: return "Daylight fluorescent";
    case 13U: return "Day white fluorescent";
    case 14U: return "Cool white fluorescent";
    case 15U: return "White fluorescent";
    case 16U: return "Warm white fluorescent";
    case 17U: return "Standard light A";
    case 18U: return "Standard light B";
    case 19U: return "Standard light C";
    case 20U: return "D55";
    case 21U: return "D65";
    case 22U: return "D75";
    case 23U: return "D50";
    case 24U: return "ISO studio tungsten";
    case 255U: return "Other";
    default: return "";
    }
}

const char*
exif_flash_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "No flash";
    case 1U: return "Fired";
    case 5U: return "Fired, return not detected";
    case 7U: return "Fired, return detected";
    case 8U: return "On, did not fire";
    case 9U: return "On, fired";
    case 13U: return "On, return not detected";
    case 15U: return "On, return detected";
    case 16U: return "Off, did not fire";
    case 24U: return "Auto, did not fire";
    case 25U: return "Auto, fired";
    case 29U: return "Auto, fired, return not detected";
    case 31U: return "Auto, fired, return detected";
    case 32U: return "No flash function";
    case 65U: return "Fired, red-eye reduction";
    case 89U: return "Auto, fired, red-eye reduction";
    case 93U: return "Auto, fired, red-eye reduction, return not detected";
    case 95U: return "Auto, fired, red-eye reduction, return detected";
    default: return "";
    }
}

const char*
exif_color_space_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "sRGB";
    case 2U: return "Adobe RGB";
    case 0xFFFFU: return "Uncalibrated";
    default: return "";
    }
}

const char*
exif_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Manual";
    default: return "";
    }
}

const char*
exif_scene_capture_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Landscape";
    case 2U: return "Portrait";
    case 3U: return "Night";
    default: return "";
    }
}

const char*
exif_gain_control_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "None";
    case 1U: return "Low gain up";
    case 2U: return "High gain up";
    case 3U: return "Low gain down";
    case 4U: return "High gain down";
    default: return "";
    }
}

const char*
dng_cfa_layout_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Rectangular";
    case 2U: return "Even columns offset down 1/2 row";
    case 3U: return "Even columns offset up 1/2 row";
    case 4U: return "Even rows offset right 1/2 column";
    case 5U: return "Even rows offset left 1/2 column";
    default: return "";
    }
}

const char*
dng_calibration_illuminant_name(uint64_t value) noexcept
{
    return exif_light_source_name(value);
}

static bool
ifd_has_prefix(std::string_view ifd, std::string_view prefix) noexcept
{
    return ifd.size() >= prefix.size()
           && ifd.substr(0U, prefix.size()) == prefix;
}

static bool
is_makernote_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_") || ifd_has_prefix(ifd, "makernote:");
}

static bool
is_canon_camera_settings_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_camerasettings")
           || ifd == "makernote:canon:camerasettings";
}

static const char*
canon_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Auto";
    case 2U: return "On";
    case 3U: return "Red-eye reduction";
    case 4U: return "Slow-sync";
    case 5U: return "Red-eye reduction (Auto)";
    case 6U: return "Red-eye reduction (On)";
    case 16U: return "External flash";
    default: return "";
    }
}

static const char*
canon_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "One-shot AF";
    case 1U: return "AI Servo AF";
    case 2U: return "AI Focus AF";
    case 3U: return "Manual Focus (3)";
    case 4U: return "Single";
    case 5U: return "Continuous";
    case 6U: return "Manual Focus (6)";
    case 16U: return "Pan Focus";
    case 256U: return "One-shot AF (Live View)";
    case 257U: return "AI Servo AF (Live View)";
    case 258U: return "AI Focus AF (Live View)";
    case 512U: return "Movie Snap Focus";
    case 519U: return "Movie Servo AF";
    default: return "";
    }
}

static const char*
canon_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Default";
    case 1U: return "Spot";
    case 2U: return "Average";
    case 3U: return "Evaluative";
    case 4U: return "Partial";
    case 5U: return "Center-weighted average";
    default: return "";
    }
}

static const char*
canon_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Easy";
    case 1U: return "Program AE";
    case 2U: return "Shutter speed priority AE";
    case 3U: return "Aperture-priority AE";
    case 4U: return "Manual";
    case 5U: return "Depth-of-field AE";
    case 6U: return "M-Dep";
    case 7U: return "Bulb";
    case 8U: return "Flexible-priority AE";
    default: return "";
    }
}

static const char*
canon_spot_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Center";
    case 1U: return "AF Point";
    default: return "";
    }
}

static const char*
canon_camera_settings_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0004U: return canon_flash_mode_name(value);
    case 0x0007U: return canon_focus_mode_name(value);
    case 0x0011U: return canon_metering_mode_name(value);
    case 0x0014U: return canon_exposure_mode_name(value);
    case 0x0027U: return canon_spot_metering_mode_name(value);
    default: return "";
    }
}

static const char*
makernote_tag_numeric_value_name(std::string_view ifd, uint16_t tag,
                                 uint64_t value) noexcept
{
    if (is_canon_camera_settings_ifd(ifd)) {
        return canon_camera_settings_value_name(tag, value);
    }
    return "";
}

const char*
exif_tag_numeric_value_name(std::string_view ifd, uint16_t tag,
                            uint64_t value) noexcept
{
    if (is_makernote_ifd(ifd)) {
        return makernote_tag_numeric_value_name(ifd, tag, value);
    }
    switch (tag) {
    case 0x0103U: return tiff_compression_name(value);
    case 0x0106U: return tiff_photometric_interpretation_name(value);
    case 0x011CU: return tiff_planar_configuration_name(value);
    case 0x0128U: return tiff_resolution_unit_name(value);
    case 0x8822U: return exif_exposure_program_name(value);
    case 0x9207U: return exif_metering_mode_name(value);
    case 0x9208U: return exif_light_source_name(value);
    case 0x9209U: return exif_flash_name(value);
    case 0xA001U: return exif_color_space_name(value);
    case 0xA403U: return exif_white_balance_name(value);
    case 0xA406U: return exif_scene_capture_type_name(value);
    case 0xA407U: return exif_gain_control_name(value);
    case 0xC617U: return dng_cfa_layout_name(value);
    case 0xC65AU:
    case 0xC65BU:
    case 0xCD31U: return dng_calibration_illuminant_name(value);
    default: return "";
    }
}

}  // namespace openmeta
