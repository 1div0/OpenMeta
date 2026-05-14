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

const char*
exif_tag_numeric_value_name(std::string_view ifd, uint16_t tag,
                            uint64_t value) noexcept
{
    (void)ifd;
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
