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
exif_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Manual";
    case 2U: return "Auto bracket";
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
ifd_matches_context(std::string_view ifd, std::string_view decoded_prefix,
                    std::string_view registry_ifd) noexcept
{
    return ifd_has_prefix(ifd, decoded_prefix) || ifd == registry_ifd;
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

static bool
is_canon_camera_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_canon_camerainfo")
           || ifd_has_prefix(ifd, "makernote:canon:camerainfo");
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
canon_flash_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "E-TTL";
    case 3U: return "TTL";
    case 4U: return "External Auto";
    case 5U: return "External Manual";
    case 6U: return "Off";
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
canon_camera_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0015U: return canon_flash_metering_mode_name(value);
    default: return "";
    }
}

static bool
is_nikon_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_nikon0" || ifd_has_prefix(ifd, "mk_nikon_main")
           || ifd == "makernote:nikon:main";
}

static const char*
nikon_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Did Not Fire";
    case 1U: return "Fired, Manual";
    case 3U: return "Not Ready";
    case 7U: return "Fired, External";
    case 8U: return "Fired, Commander Mode";
    case 9U: return "Fired, TTL Mode";
    case 18U: return "LED Light";
    default: return "";
    }
}

static const char*
nikon_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Matrix";
    case 1U: return "Center";
    case 2U: return "Spot";
    case 3U: return "Highlight";
    default: return "";
    }
}

static const char*
nikon_movie_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Manual";
    case 1U: return "AF-S";
    case 2U: return "AF-C";
    case 4U: return "AF-F";
    default: return "";
    }
}

static const char*
nikon_menu_multiple_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    case 2U: return "On (Series)";
    default: return "";
    }
}

static bool
is_nikon_metering_ifd_tag(std::string_view ifd, uint16_t tag) noexcept
{
    if (tag == 0x0017U
        && ifd_matches_context(ifd, "mk_nikon_bracketinginfod810_",
                               "makernote:nikon:bracketinginfod810")) {
        return true;
    }
    if (tag == 0x0214U
        && ifd_matches_context(ifd, "mk_nikon_otherinfod500_",
                               "makernote:nikon:otherinfod500")) {
        return true;
    }
    if (tag == 0x02D2U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz6iii_",
                               "makernote:nikon:menusettingsz6iii")) {
        return true;
    }
    if (tag == 0x0146U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz7ii_",
                               "makernote:nikon:menusettingsz7ii")) {
        return true;
    }
    if (tag == 0x033EU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz8_",
                               "makernote:nikon:menusettingsz8")) {
        return true;
    }
    if (tag == 0x02C2U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9_",
                               "makernote:nikon:menusettingsz9")) {
        return true;
    }
    if (tag == 0x02EAU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9v3_",
                               "makernote:nikon:menusettingsz9v3")) {
        return true;
    }
    if (tag == 0x02EAU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9v4_",
                               "makernote:nikon:menusettingsz9v4")) {
        return true;
    }
    return false;
}

static bool
is_nikon_movie_focus_ifd_tag(std::string_view ifd, uint16_t tag) noexcept
{
    if (tag == 0x0248U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz7ii_",
                               "makernote:nikon:menusettingsz7ii")) {
        return true;
    }
    if (tag == 0x0340U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz8_",
                               "makernote:nikon:menusettingsz8")) {
        return true;
    }
    if (tag == 0x02C4U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9_",
                               "makernote:nikon:menusettingsz9")) {
        return true;
    }
    if (tag == 0x02ECU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9v3_",
                               "makernote:nikon:menusettingsz9v3")) {
        return true;
    }
    if (tag == 0x02ECU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9v4_",
                               "makernote:nikon:menusettingsz9v4")) {
        return true;
    }
    return false;
}

static bool
is_nikon_menu_multiple_exposure_ifd_tag(std::string_view ifd,
                                        uint16_t tag) noexcept
{
    if (tag == 0x01BCU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz6iii_",
                               "makernote:nikon:menusettingsz6iii")) {
        return true;
    }
    if (tag == 0x0098U
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz8_",
                               "makernote:nikon:menusettingsz8")) {
        return true;
    }
    if (tag == 0x008CU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9_",
                               "makernote:nikon:menusettingsz9")) {
        return true;
    }
    if (tag == 0x009AU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9v3_",
                               "makernote:nikon:menusettingsz9v3")) {
        return true;
    }
    if (tag == 0x009AU
        && ifd_matches_context(ifd, "mk_nikon_menusettingsz9v4_",
                               "makernote:nikon:menusettingsz9v4")) {
        return true;
    }
    return false;
}

static const char*
nikon_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_nikon_main_ifd(ifd) && tag == 0x0087U) {
        return nikon_flash_mode_name(value);
    }
    if (is_nikon_metering_ifd_tag(ifd, tag)) {
        return nikon_metering_mode_name(value & 0x03U);
    }
    if (is_nikon_movie_focus_ifd_tag(ifd, tag)) {
        return nikon_movie_focus_mode_name(value);
    }
    if (is_nikon_menu_multiple_exposure_ifd_tag(ifd, tag)) {
        return nikon_menu_multiple_exposure_mode_name(value);
    }
    return "";
}

static bool
is_fujifilm_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_fujifilm0" || ifd_has_prefix(ifd, "mk_fujifilm_main")
           || ifd == "makernote:fujifilm:main";
}

static const char*
fujifilm_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "On";
    case 2U: return "Off";
    case 3U: return "Red-eye reduction";
    case 4U: return "External";
    case 16U: return "Commander";
    case 0x8000U: return "Not Attached";
    case 0x8120U: return "TTL";
    case 0x8320U: return "TTL Auto - Did not fire";
    case 0x9840U: return "Manual";
    case 0x9860U: return "Flash Commander";
    case 0x9880U: return "Multi-flash";
    case 0xA920U: return "1st Curtain (front)";
    case 0xAA20U: return "TTL Slow - 1st Curtain (front)";
    case 0xAB20U: return "TTL Auto - 1st Curtain (front)";
    case 0xAD20U: return "TTL - Red-eye Flash - 1st Curtain (front)";
    case 0xAE20U: return "TTL Slow - Red-eye Flash - 1st Curtain (front)";
    case 0xAF20U: return "TTL Auto - Red-eye Flash - 1st Curtain (front)";
    case 0xC920U: return "2nd Curtain (rear)";
    case 0xCA20U: return "TTL Slow - 2nd Curtain (rear)";
    case 0xCB20U: return "TTL Auto - 2nd Curtain (rear)";
    case 0xCD20U: return "TTL - Red-eye Flash - 2nd Curtain (rear)";
    case 0xCE20U: return "TTL Slow - Red-eye Flash - 2nd Curtain (rear)";
    case 0xCF20U: return "TTL Auto - Red-eye Flash - 2nd Curtain (rear)";
    case 0xE920U: return "High Speed Sync (HSS)";
    default: return "";
    }
}

static const char*
fujifilm_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Manual";
    case 65535U: return "Movie";
    default: return "";
    }
}

static const char*
fujifilm_af_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "No";
    case 1U: return "Single Point";
    case 256U: return "Zone";
    case 512U: return "Wide/Tracking";
    default: return "";
    }
}

static const char*
fujifilm_picture_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x0000U: return "Auto";
    case 0x0001U: return "Portrait";
    case 0x0002U: return "Landscape";
    case 0x0003U: return "Macro";
    case 0x0004U: return "Sports";
    case 0x0005U: return "Night Scene";
    case 0x0006U: return "Program AE";
    case 0x0007U: return "Natural Light";
    case 0x0008U: return "Anti-blur";
    case 0x0009U: return "Beach & Snow";
    case 0x000AU: return "Sunset";
    case 0x000BU: return "Museum";
    case 0x000CU: return "Party";
    case 0x000DU: return "Flower";
    case 0x000EU: return "Text";
    case 0x000FU: return "Natural Light & Flash";
    case 0x0010U: return "Beach";
    case 0x0011U: return "Snow";
    case 0x0012U: return "Fireworks";
    case 0x0013U: return "Underwater";
    case 0x0014U: return "Portrait with Skin Correction";
    case 0x0016U: return "Panorama";
    case 0x0017U: return "Night (tripod)";
    case 0x0018U: return "Pro Low-light";
    case 0x0019U: return "Pro Focus";
    case 0x001AU: return "Portrait 2";
    case 0x001BU: return "Dog Face Detection";
    case 0x001CU: return "Cat Face Detection";
    case 0x0030U: return "HDR";
    case 0x0040U: return "Advanced Filter";
    case 0x0100U: return "Aperture-priority AE";
    case 0x0200U: return "Shutter speed priority AE";
    case 0x0300U: return "Manual";
    default: return "";
    }
}

static const char*
fujifilm_multiple_exposure_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Additive";
    case 2U: return "Average";
    case 3U: return "Light";
    case 4U: return "Dark";
    default: return "";
    }
}

static const char*
fujifilm_focus_warning_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Good";
    case 1U: return "Out of focus";
    default: return "";
    }
}

static const char*
fujifilm_exposure_warning_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Good";
    case 1U: return "Bad exposure";
    default: return "";
    }
}

static const char*
fujifilm_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (!is_fujifilm_main_ifd(ifd)) {
        return "";
    }

    switch (tag) {
    case 0x1010U: return fujifilm_flash_mode_name(value);
    case 0x1021U: return fujifilm_focus_mode_name(value);
    case 0x1022U: return fujifilm_af_mode_name(value);
    case 0x1031U: return fujifilm_picture_mode_name(value);
    case 0x1037U: return fujifilm_multiple_exposure_name(value);
    case 0x1301U: return fujifilm_focus_warning_name(value);
    case 0x1302U: return fujifilm_exposure_warning_name(value);
    default: return "";
    }
}

static bool
is_sony_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_sony0" || ifd_has_prefix(ifd, "mk_sony_main")
           || ifd == "makernote:sony:main";
}

static bool
is_sony_camera_settings_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_sony_camerasettings_")
           || ifd == "makernote:sony:camerasettings";
}

static bool
is_sony_camera_settings2_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_sony_camerasettings2_")
           || ifd == "makernote:sony:camerasettings2";
}

static bool
is_sony_camera_settings3_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_sony_camerasettings3_")
           || ifd == "makernote:sony:camerasettings3";
}

static bool
is_sony_more_settings_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_sony_moresettings_")
           || ifd == "makernote:sony:moresettings";
}

static bool
is_sony_tag2010_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_sony_tag2010")
           || ifd_has_prefix(ifd, "makernote:sony:tag2010");
}

static const char*
sony_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program AE";
    case 1U: return "Portrait";
    case 2U: return "Beach";
    case 3U: return "Sports";
    case 4U: return "Snow";
    case 5U: return "Landscape";
    case 6U: return "Auto";
    case 7U: return "Aperture-priority AE";
    case 8U: return "Shutter speed priority AE";
    case 9U: return "Night Scene / Twilight";
    case 10U: return "Hi-Speed Shutter";
    case 11U: return "Twilight Portrait";
    case 12U: return "Soft Snap/Portrait";
    case 13U: return "Fireworks";
    case 14U: return "Smile Shutter";
    case 15U: return "Manual";
    case 18U: return "High Sensitivity";
    case 19U: return "Macro";
    case 20U: return "Advanced Sports Shooting";
    case 29U: return "Underwater";
    case 33U: return "Food";
    case 34U: return "Sweep Panorama";
    case 35U: return "Handheld Night Shot";
    case 36U: return "Anti Motion Blur";
    case 37U: return "Pet";
    case 38U: return "Backlight Correction HDR";
    case 39U: return "Superior Auto";
    case 40U: return "Background Defocus";
    case 41U: return "Soft Skin";
    case 42U: return "3D Image";
    case 65535U: return "n/a";
    default: return "";
    }
}

static const char*
sony_exposure_program_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Manual";
    case 2U: return "Program AE";
    case 3U: return "Aperture-priority AE";
    case 4U: return "Shutter speed priority AE";
    case 8U: return "Program Shift A";
    case 9U: return "Program Shift S";
    case 16U: return "Portrait";
    case 17U: return "Sports";
    case 18U: return "Sunset";
    case 19U: return "Night Portrait";
    case 20U: return "Landscape";
    case 21U: return "Macro";
    case 35U: return "Auto No Flash";
    default: return "";
    }
}

static const char*
sony_exposure_program2_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Program AE";
    case 2U: return "Aperture-priority AE";
    case 3U: return "Shutter speed priority AE";
    case 4U: return "Manual";
    case 5U: return "Cont. Priority AE";
    case 16U: return "Auto";
    case 17U: return "Auto (no flash)";
    case 18U: return "Auto+";
    case 49U: return "Portrait";
    case 50U: return "Landscape";
    case 51U: return "Macro";
    case 52U: return "Sports";
    case 53U: return "Sunset";
    case 54U: return "Night view";
    case 55U: return "Night view/portrait";
    case 56U: return "Handheld Night Shot";
    case 57U: return "3D Sweep Panorama";
    case 64U: return "Auto 2";
    case 65U: return "Auto 2 (no flash)";
    case 80U: return "Sweep Panorama";
    case 96U: return "Anti Motion Blur";
    case 128U: return "Toy Camera";
    case 129U: return "Pop Color";
    case 130U: return "Posterization";
    case 131U: return "Posterization B/W";
    case 132U: return "Retro Photo";
    case 133U: return "High-key";
    case 134U: return "Partial Color Red";
    case 135U: return "Partial Color Green";
    case 136U: return "Partial Color Blue";
    case 137U: return "Partial Color Yellow";
    case 138U: return "High Contrast Monochrome";
    default: return "";
    }
}

static const char*
sony_exposure_program3_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program AE";
    case 1U: return "Aperture-priority AE";
    case 2U: return "Shutter speed priority AE";
    case 3U: return "Manual";
    case 4U: return "Auto";
    case 5U: return "iAuto";
    case 6U: return "Superior Auto";
    case 7U: return "iAuto+";
    case 8U: return "Portrait";
    case 9U: return "Landscape";
    case 10U: return "Twilight";
    case 11U: return "Twilight Portrait";
    case 12U: return "Sunset";
    case 14U: return "Action (High speed)";
    case 16U: return "Sports";
    case 17U: return "Handheld Night Shot";
    case 18U: return "Anti Motion Blur";
    case 19U: return "High Sensitivity";
    case 21U: return "Beach";
    case 22U: return "Snow";
    case 23U: return "Fireworks";
    case 26U: return "Underwater";
    case 27U: return "Gourmet";
    case 28U: return "Pet";
    case 29U: return "Macro";
    case 30U: return "Backlight Correction HDR";
    case 33U: return "Sweep Panorama";
    case 36U: return "Background Defocus";
    case 37U: return "Soft Skin";
    case 42U: return "3D Image";
    case 43U: return "Cont. Priority AE";
    case 45U: return "Document";
    case 46U: return "Party";
    default: return "";
    }
}

static const char*
sony_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Multi-segment";
    case 2U: return "Center-weighted average";
    case 4U: return "Spot";
    default: return "";
    }
}

static const char*
sony_metering_mode3_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Multi-segment";
    case 2U: return "Center-weighted average";
    case 3U: return "Spot";
    default: return "";
    }
}

static const char*
sony_metering_mode2_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x100U: return "Multi-segment";
    case 0x200U: return "Center-weighted average";
    case 0x301U: return "Spot (Standard)";
    case 0x302U: return "Spot (Large)";
    case 0x400U: return "Average";
    case 0x500U: return "Highlight";
    default: return "";
    }
}

static const char*
sony_metering_mode2010_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Multi-segment";
    case 2U: return "Center-weighted average";
    case 3U: return "Spot";
    case 4U: return "Average";
    case 5U: return "Highlight";
    default: return "";
    }
}

static const char*
sony_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Manual";
    case 1U: return "AF-S";
    case 2U: return "AF-C";
    case 3U: return "AF-A";
    default: return "";
    }
}

static const char*
sony_focus_mode_main_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Manual";
    case 2U: return "AF-S";
    case 3U: return "AF-C";
    case 4U: return "AF-A";
    case 6U: return "DMF";
    case 7U: return "AF-D";
    default: return "";
    }
}

static const char*
sony_focus_mode_setting_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Manual";
    case 1U: return "AF-S";
    case 2U: return "AF-C";
    case 3U: return "AF-A";
    case 4U: return "DMF";
    default: return "";
    }
}

static const char*
sony_focus_mode_setting_basic_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Manual";
    case 1U: return "AF-S";
    case 2U: return "AF-C";
    case 3U: return "AF-A";
    default: return "";
    }
}

static const char*
sony_focus_mode_setting2_name(uint64_t value) noexcept
{
    switch (value) {
    case 17U: return "AF-S";
    case 18U: return "AF-C";
    case 19U: return "AF-A";
    case 32U: return "Manual";
    case 48U: return "DMF";
    default: return "";
    }
}

static const char*
sony_af_area_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Wide";
    case 1U: return "Local";
    case 2U: return "Spot";
    default: return "";
    }
}

static const char*
sony_af_area_mode2_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Wide";
    case 2U: return "Spot";
    case 3U: return "Local";
    case 4U: return "Flexible";
    default: return "";
    }
}

static const char*
sony_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Autoflash";
    case 2U: return "Rear Sync";
    case 3U: return "Wireless";
    case 4U: return "Fill-flash";
    case 5U: return "Flash Off";
    case 6U: return "Slow Sync";
    default: return "";
    }
}

static const char*
sony_flash_mode2_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Flash Off";
    case 16U: return "Autoflash";
    case 17U: return "Fill-flash";
    case 18U: return "Slow Sync";
    case 19U: return "Rear Sync";
    case 20U: return "Wireless";
    default: return "";
    }
}

static const char*
sony_flash_mode2010_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Autoflash";
    case 1U: return "Fill-flash";
    case 2U: return "Flash Off";
    case 3U: return "Slow Sync";
    case 4U: return "Rear Sync";
    case 6U: return "Wireless";
    default: return "";
    }
}

static const char*
sony_drive_mode_name(uint64_t value) noexcept
{
    switch (value & 0xFFU) {
    case 0x01U: return "Single Frame";
    case 0x02U: return "Continuous High";
    case 0x04U: return "Self-timer 10 sec";
    case 0x05U: return "Self-timer 2 sec, Mirror Lock-up";
    case 0x06U: return "Single-frame Bracketing";
    case 0x07U: return "Continuous Bracketing";
    case 0x0AU: return "Remote Commander";
    case 0x0BU: return "Mirror Lock-up";
    case 0x12U: return "Continuous Low";
    case 0x18U: return "White Balance Bracketing Low";
    case 0x19U: return "D-Range Optimizer Bracketing Low";
    case 0x28U: return "White Balance Bracketing High";
    case 0x29U: return "D-Range Optimizer Bracketing High";
    default: return "";
    }
}

static const char*
sony_drive_mode2_name(uint64_t value) noexcept
{
    switch (value & 0xFFU) {
    case 0x01U: return "Single Frame";
    case 0x02U: return "Continuous High";
    case 0x04U: return "Self-timer 10 sec";
    case 0x05U: return "Self-timer 2 sec, Mirror Lock-up";
    case 0x07U: return "Continuous Bracketing";
    case 0x0AU: return "Remote Commander";
    case 0x0BU: return "Continuous Self-timer";
    default: return "";
    }
}

static const char*
sony_drive_mode3_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x10U: return "Single Frame";
    case 0x21U: return "Continuous High";
    case 0x22U: return "Continuous Low";
    case 0x30U: return "Speed Priority Continuous";
    case 0x51U: return "Self-timer 10 sec";
    case 0x52U: return "Self-timer 2 sec, Mirror Lock-up";
    case 0x71U: return "Continuous Bracketing 0.3 EV";
    case 0x75U: return "Continuous Bracketing 0.7 EV";
    case 0x91U: return "White Balance Bracketing Low";
    case 0x92U: return "White Balance Bracketing High";
    case 0xC0U: return "Remote Commander";
    case 0xD1U: return "Continuous - HDR";
    case 0xD2U: return "Continuous - Multi Frame NR";
    case 0xD3U: return "Continuous - Handheld Night Shot";
    case 0xD4U: return "Continuous - Anti Motion Blur";
    case 0xD5U: return "Continuous - Sweep Panorama";
    case 0xD6U: return "Continuous - 3D Sweep Panorama";
    default: return "";
    }
}

static const char*
sony_release_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Continuous";
    case 2U: return "Continuous - Exposure Bracketing";
    case 3U: return "DRO or White Balance Bracketing";
    case 5U: return "Continuous - Burst";
    case 6U: return "Single Frame - Capture During Movie";
    case 7U: return "Continuous - Sweep Panorama";
    case 8U: return "Continuous - Anti-Motion Blur, Hand-held Twilight";
    case 9U: return "Continuous - HDR";
    case 10U: return "Continuous - Background defocus";
    case 13U: return "Continuous - 3D Sweep Panorama";
    case 15U: return "Continuous - High Resolution Sweep Panorama";
    case 16U: return "Continuous - 3D Image";
    case 17U: return "Continuous - Burst 2";
    case 18U: return "Normal - iAuto+";
    case 19U: return "Continuous - Speed/Advance Priority";
    case 20U: return "Continuous - Multi Frame NR";
    case 23U: return "Single-frame - Exposure Bracketing";
    case 26U: return "Continuous Low";
    case 27U: return "Continuous - High Sensitivity";
    case 28U: return "Smile Shutter";
    case 29U: return "Continuous - Tele-zoom Advance Priority";
    case 146U: return "Single Frame - Movie Capture";
    default: return "";
    }
}

static const char*
sony_release_mode_main_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 2U: return "Continuous";
    case 5U: return "Exposure Bracketing";
    case 6U: return "White Balance Bracketing";
    case 8U: return "DRO Bracketing";
    case 65535U: return "n/a";
    default: return "";
    }
}

static const char*
sony_release_mode3_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Continuous";
    case 2U: return "Bracketing";
    case 4U: return "Continuous - Burst";
    case 5U: return "Continuous - Speed/Advance Priority";
    case 6U: return "Normal - Self-timer";
    case 9U: return "Single Burst Shooting";
    default: return "";
    }
}

static bool
is_sony_tag2010_release_mode(uint16_t tag) noexcept
{
    switch (tag) {
    case 0x0004U:
    case 0x0008U:
    case 0x0208U:
    case 0x0210U:
    case 0x1018U:
    case 0x1108U:
    case 0x112CU:
    case 0x1160U:
    case 0x1184U: return true;
    default: return false;
    }
}

static bool
is_sony_tag2010_release_mode3(uint16_t tag) noexcept
{
    switch (tag) {
    case 0x0204U:
    case 0x020CU:
    case 0x1014U:
    case 0x1104U:
    case 0x1128U:
    case 0x115CU:
    case 0x1180U: return true;
    default: return false;
    }
}

static bool
is_sony_tag2010_flash_mode(uint16_t tag) noexcept
{
    switch (tag) {
    case 0x0211U:
    case 0x021CU:
    case 0x1024U:
    case 0x1114U:
    case 0x1138U:
    case 0x116CU:
    case 0x1190U: return true;
    default: return false;
    }
}

static bool
is_sony_tag2010_metering_mode(uint16_t tag) noexcept
{
    switch (tag) {
    case 0x024BU:
    case 0x025CU:
    case 0x1064U:
    case 0x1154U:
    case 0x1174U:
    case 0x1178U:
    case 0x11ACU:
    case 0x11D0U: return true;
    default: return false;
    }
}

static bool
is_sony_tag2010_exposure_program(uint16_t tag) noexcept
{
    switch (tag) {
    case 0x024CU:
    case 0x025DU:
    case 0x1065U:
    case 0x1155U:
    case 0x1175U:
    case 0x1179U:
    case 0x11ADU:
    case 0x11D1U: return true;
    default: return false;
    }
}

static const char*
sony_tag2010_value_name(uint16_t tag, uint64_t value) noexcept
{
    if (is_sony_tag2010_release_mode(tag)) {
        return sony_release_mode_name(value);
    }
    if (is_sony_tag2010_release_mode3(tag)) {
        return sony_release_mode3_name(value);
    }
    if (is_sony_tag2010_flash_mode(tag)) {
        return sony_flash_mode2010_name(value);
    }
    if (is_sony_tag2010_metering_mode(tag)) {
        return sony_metering_mode2010_name(value);
    }
    if (is_sony_tag2010_exposure_program(tag)) {
        return sony_exposure_program3_name(value);
    }
    return "";
}

static const char*
sony_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x201BU: return sony_focus_mode_main_name(value);
    case 0x202CU: return sony_metering_mode2_name(value);
    case 0xB041U: return sony_exposure_mode_name(value);
    case 0xB049U: return sony_release_mode_main_name(value);
    default: return "";
    }
}

static const char*
sony_camera_settings_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0004U: return sony_drive_mode_name(value);
    case 0x0010U: return sony_focus_mode_setting_name(value);
    case 0x0011U: return sony_af_area_mode_name(value);
    case 0x0013U: return sony_flash_mode_name(value);
    case 0x0015U: return sony_metering_mode_name(value);
    case 0x003CU: return sony_exposure_program_name(value);
    case 0x004DU: return sony_focus_mode_name(value);
    default: return "";
    }
}

static const char*
sony_camera_settings2_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x000FU: return sony_focus_mode_setting_basic_name(value);
    case 0x0010U: return sony_af_area_mode_name(value);
    case 0x0013U: return sony_metering_mode_name(value);
    case 0x003CU: return sony_exposure_program_name(value);
    case 0x004DU: return sony_focus_mode_name(value);
    case 0x007EU: return sony_drive_mode2_name(value);
    case 0x007FU: return sony_flash_mode_name(value);
    default: return "";
    }
}

static const char*
sony_camera_settings3_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0004U: return sony_drive_mode3_name(value);
    case 0x0005U: return sony_exposure_program2_name(value);
    case 0x0006U: return sony_focus_mode_setting2_name(value);
    case 0x0007U: return sony_metering_mode3_name(value);
    case 0x0020U: return sony_flash_mode2_name(value);
    case 0x0024U: return sony_af_area_mode2_name(value);
    case 0x0034U: return sony_drive_mode3_name(value);
    default: return "";
    }
}

static const char*
sony_more_settings_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return sony_drive_mode3_name(value);
    case 0x0002U: return sony_exposure_program2_name(value);
    case 0x0003U: return sony_metering_mode3_name(value);
    case 0x0010U: return sony_flash_mode2_name(value);
    case 0x0013U: return sony_focus_mode_setting2_name(value);
    default: return "";
    }
}

static const char*
sony_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_sony_main_ifd(ifd)) {
        return sony_main_value_name(tag, value);
    }
    if (is_sony_camera_settings_ifd(ifd)) {
        return sony_camera_settings_value_name(tag, value);
    }
    if (is_sony_camera_settings2_ifd(ifd)) {
        return sony_camera_settings2_value_name(tag, value);
    }
    if (is_sony_camera_settings3_ifd(ifd)) {
        return sony_camera_settings3_value_name(tag, value);
    }
    if (is_sony_more_settings_ifd(ifd)) {
        return sony_more_settings_value_name(tag, value);
    }
    if (is_sony_tag2010_ifd(ifd)) {
        return sony_tag2010_value_name(tag, value);
    }
    return "";
}

static bool
is_pentax_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_pentax0" || ifd_has_prefix(ifd, "mk_pentax_main")
           || ifd == "makernote:pentax:main";
}

static bool
is_pentax_ae_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_pentax_aeinfo_")
           || ifd == "makernote:pentax:aeinfo";
}

static bool
is_pentax_flash_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_pentax_flashinfo_")
           || ifd == "makernote:pentax:flashinfo";
}

static bool
is_pentax_type2_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_pentax_type2_")
           || ifd == "makernote:pentax:type2";
}

static const char*
pentax_picture_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program";
    case 1U: return "Shutter Speed Priority";
    case 2U: return "Program AE";
    case 3U: return "Manual";
    case 5U: return "Portrait";
    case 6U: return "Landscape";
    case 8U: return "Sport";
    case 9U: return "Night Scene";
    case 11U: return "Soft";
    case 12U: return "Surf & Snow";
    case 13U: return "Candlelight";
    case 14U: return "Autumn";
    case 15U: return "Macro";
    case 17U: return "Fireworks";
    case 18U: return "Text";
    case 19U: return "Panorama";
    case 20U: return "3-D";
    case 21U: return "Black & White";
    case 22U: return "Sepia";
    case 30U: return "Self Portrait";
    case 35U: return "Night Scene Portrait";
    case 37U: return "Museum";
    case 38U: return "Food";
    case 39U: return "Underwater";
    case 40U: return "Green Mode";
    case 58U: return "Frame Composite";
    case 60U: return "Kids";
    case 61U: return "Blur Reduction";
    case 63U: return "Panorama 2";
    case 65U: return "Half-length Portrait";
    case 66U: return "Portrait 2";
    case 74U: return "Digital Microscope";
    case 75U: return "Blue Sky";
    case 80U: return "Miniature";
    case 81U: return "HDR";
    case 83U: return "Fisheye";
    case 85U: return "Digital Filter 4";
    default: return "";
    }
}

static const char*
pentax_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x000U: return "Auto, Did not fire";
    case 0x001U: return "Off, Did not fire";
    case 0x002U: return "On, Did not fire";
    case 0x003U: return "Auto, Did not fire, Red-eye reduction";
    case 0x005U: return "On, Did not fire, Wireless (Master)";
    case 0x100U: return "Auto, Fired";
    case 0x102U: return "On, Fired";
    case 0x103U: return "Auto, Fired, Red-eye reduction";
    case 0x104U: return "On, Red-eye reduction";
    case 0x105U: return "On, Wireless (Master)";
    case 0x106U: return "On, Wireless (Control)";
    case 0x108U: return "On, Soft";
    case 0x109U: return "On, Slow-sync";
    case 0x10AU: return "On, Slow-sync, Red-eye reduction";
    case 0x10BU: return "On, Trailing-curtain Sync";
    case 0x300U: return "External, Manual";
    case 0x304U: return "External, P-TTL Auto";
    case 0x306U: return "External, High-speed Sync";
    case 0x30CU: return "External, Wireless";
    default: return "";
    }
}

static const char*
pentax_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x0000U: return "Normal";
    case 0x0001U: return "Macro";
    case 0x0002U: return "Infinity";
    case 0x0003U: return "Manual";
    case 0x0004U: return "Super Macro";
    case 0x0005U: return "Pan Focus";
    case 0x0006U: return "Auto-area";
    case 0x0007U: return "Zone Select";
    case 0x0008U: return "Select";
    case 0x0009U: return "Pinpoint";
    case 0x000AU: return "Tracking";
    case 0x000BU: return "Continuous";
    case 0x000CU: return "Snap";
    case 0x0010U: return "AF-S (Focus-priority)";
    case 0x0011U: return "AF-C (Focus-priority)";
    case 0x0012U: return "AF-A (Focus-priority)";
    case 0x0020U: return "Contrast-detect (Focus-priority)";
    case 0x0021U: return "Tracking Contrast-detect (Focus-priority)";
    case 0x0110U: return "AF-S (Release-priority)";
    case 0x0111U: return "AF-C (Release-priority)";
    case 0x0112U: return "AF-A (Release-priority)";
    case 0x0120U: return "Contrast-detect (Release-priority)";
    case 0x8003U: return "Manual (Macro)";
    case 0x8006U: return "Auto-area (Macro)";
    default: return "";
    }
}

static const char*
pentax_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Multi-segment";
    case 1U: return "Center-weighted average";
    case 2U: return "Spot";
    case 6U: return "Highlight";
    default: return "";
    }
}

static const char*
pentax_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Shade";
    case 3U: return "Fluorescent";
    case 4U: return "Tungsten";
    case 5U: return "Manual";
    case 6U: return "Daylight Fluorescent";
    case 7U: return "Day White Fluorescent";
    case 8U: return "White Fluorescent";
    case 9U: return "Flash";
    case 10U: return "Cloudy";
    case 11U: return "Warm White Fluorescent";
    case 14U: return "Multi Auto";
    case 15U: return "Color Temperature Enhancement";
    case 17U: return "Kelvin";
    case 0xFFFEU: return "Unknown";
    case 0xFFFFU: return "User-Selected";
    default: return "";
    }
}

static const char*
pentax_ae_program_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "M, P or TAv";
    case 1U: return "Av, B or X";
    case 2U: return "Tv";
    case 3U: return "Sv or Green Mode";
    case 8U: return "Hi-speed Program";
    case 11U: return "Hi-speed Program (P-Shift)";
    case 16U: return "DOF Program";
    case 19U: return "DOF Program (P-Shift)";
    case 24U: return "MTF Program";
    case 27U: return "MTF Program (P-Shift)";
    case 35U: return "Standard";
    case 43U: return "Portrait";
    case 51U: return "Landscape";
    case 59U: return "Macro";
    case 67U: return "Sport";
    case 75U: return "Night Scene Portrait";
    case 83U: return "No Flash";
    case 91U: return "Night Scene";
    case 99U: return "Surf & Snow";
    case 104U: return "Night Snap";
    case 107U: return "Text";
    case 115U: return "Sunset";
    case 123U: return "Kids";
    case 131U: return "Pet";
    case 139U: return "Candlelight";
    case 144U: return "SCN";
    case 147U: return "Museum";
    case 160U: return "Program";
    case 184U: return "Shallow DOF Program";
    case 216U: return "HDR";
    default: return "";
    }
}

static const char*
pentax_ae_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Multi-segment";
    case 16U: return "Center-weighted average";
    case 32U: return "Spot";
    default: return "";
    }
}

static const char*
pentax_flash_status_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x00U: return "Off";
    case 0x01U: return "Off (1)";
    case 0x02U: return "External, Did not fire";
    case 0x06U: return "External, Fired";
    case 0x08U: return "Internal, Did not fire (0x08)";
    case 0x09U: return "Internal, Did not fire";
    case 0x0DU: return "Internal, Fired";
    default: return "";
    }
}

static const char*
pentax_internal_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x00U: return "n/a - Off-Auto-Aperture";
    case 0x86U: return "Fired, Wireless (Control)";
    case 0x95U: return "Fired, Wireless (Master)";
    case 0xC0U: return "Fired";
    case 0xC1U: return "Fired, Red-eye reduction";
    case 0xC2U: return "Fired, Auto";
    case 0xC3U: return "Fired, Auto, Red-eye reduction";
    case 0xC8U: return "Fired, Slow-sync";
    case 0xC9U: return "Fired, Slow-sync, Red-eye reduction";
    case 0xCAU: return "Fired, Trailing-curtain Sync";
    case 0xF0U: return "Did not fire, Normal";
    case 0xF1U: return "Did not fire, Red-eye reduction";
    case 0xF2U: return "Did not fire, Auto";
    case 0xF3U: return "Did not fire, Auto, Red-eye reduction";
    case 0xF5U: return "Did not fire, Wireless (Master)";
    default: return "";
    }
}

static const char*
pentax_external_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x00U: return "n/a - Off-Auto-Aperture";
    case 0x3FU: return "Off";
    case 0x40U: return "On, Auto";
    case 0xBFU: return "On, Flash Problem";
    case 0xC0U: return "On, Manual";
    case 0xC4U: return "On, P-TTL Auto";
    case 0xC5U: return "On, Contrast-control Sync";
    case 0xC6U: return "On, High-speed Sync";
    case 0xCCU: return "On, Wireless";
    case 0xCDU: return "On, Wireless, High-speed Sync";
    case 0xF0U: return "Not Connected";
    default: return "";
    }
}

static const char*
pentax_type2_recording_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Night Scene";
    case 2U: return "Manual";
    default: return "";
    }
}

static const char*
pentax_type2_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 2U: return "Custom";
    case 3U: return "Auto";
    default: return "";
    }
}

static const char*
pentax_type2_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Auto";
    case 2U: return "On";
    case 4U: return "Off";
    case 6U: return "Red-eye reduction";
    default: return "";
    }
}

static const char*
pentax_type2_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Shade";
    case 3U: return "Tungsten";
    case 4U: return "Fluorescent";
    case 5U: return "Manual";
    default: return "";
    }
}

static const char*
pentax_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x000BU: return pentax_picture_mode_name(value);
    case 0x000CU: return pentax_flash_mode_name(value);
    case 0x000DU: return pentax_focus_mode_name(value);
    case 0x0017U: return pentax_metering_mode_name(value);
    case 0x0019U: return pentax_white_balance_name(value);
    default: return "";
    }
}

static const char*
pentax_ae_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0006U: return pentax_ae_program_mode_name(value);
    case 0x000CU: return pentax_ae_metering_mode_name(value);
    default: return "";
    }
}

static const char*
pentax_flash_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0000U: return pentax_flash_status_name(value);
    case 0x0001U: return pentax_internal_flash_mode_name(value);
    case 0x0002U: return pentax_external_flash_mode_name(value);
    default: return "";
    }
}

static const char*
pentax_type2_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return pentax_type2_recording_mode_name(value);
    case 0x0003U: return pentax_type2_focus_mode_name(value);
    case 0x0004U: return pentax_type2_flash_mode_name(value);
    case 0x0007U: return pentax_type2_white_balance_name(value);
    default: return "";
    }
}

static const char*
pentax_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_pentax_main_ifd(ifd)) {
        return pentax_main_value_name(tag, value);
    }
    if (is_pentax_ae_info_ifd(ifd)) {
        return pentax_ae_info_value_name(tag, value);
    }
    if (is_pentax_flash_info_ifd(ifd)) {
        return pentax_flash_info_value_name(tag, value);
    }
    if (is_pentax_type2_ifd(ifd)) {
        return pentax_type2_value_name(tag, value);
    }
    return "";
}

static bool
is_olympus_camera_settings_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_olympus_camerasettings_")
           || ifd == "makernote:olympus:camerasettings";
}

static bool
is_olympus_raw_development_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_olympus_rawdevelopment_")
           || ifd == "makernote:olympus:rawdevelopment";
}

static bool
is_olympus_raw_development2_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_olympus_rawdevelopment2_")
           || ifd == "makernote:olympus:rawdevelopment2";
}

static bool
is_olympus_image_processing_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_olympus_imageprocessing_")
           || ifd == "makernote:olympus:imageprocessing";
}

static const char*
olympus_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Manual";
    case 2U: return "Program";
    case 3U: return "Aperture-priority AE";
    case 4U: return "Shutter speed priority AE";
    case 5U: return "Program-shift";
    default: return "";
    }
}

static const char*
olympus_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 2U: return "Center-weighted average";
    case 3U: return "Spot";
    case 5U: return "ESP";
    case 261U: return "Pattern+AF";
    case 515U: return "Spot+Highlight control";
    case 1027U: return "Spot+Shadow control";
    default: return "";
    }
}

static const char*
olympus_macro_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    case 2U: return "Super Macro";
    default: return "";
    }
}

static const char*
olympus_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single AF";
    case 1U: return "Sequential shooting AF";
    case 2U: return "Continuous AF";
    case 3U: return "Multi AF";
    case 4U: return "Face Detect";
    case 10U: return "MF";
    default: return "";
    }
}

static const char*
olympus_focus_process_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "AF Not Used";
    case 1U: return "AF Used";
    default: return "";
    }
}

static const char*
olympus_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    case 2U: return "Fill-in";
    case 4U: return "Red-eye";
    case 8U: return "Slow-sync";
    case 16U: return "Forced On";
    case 32U: return "2nd Curtain";
    default: return "";
    }
}

static const char*
olympus_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Auto (Keep Warm Color Off)";
    case 16U: return "7500K (Fine Weather with Shade)";
    case 17U: return "6000K (Cloudy)";
    case 18U: return "5300K (Fine Weather)";
    case 20U: return "3000K (Tungsten light)";
    case 21U: return "3600K (Tungsten light-like)";
    case 22U: return "Auto Setup";
    case 23U: return "5500K (Flash)";
    case 33U: return "6600K (Daylight fluorescent)";
    case 34U: return "4500K (Neutral white fluorescent)";
    case 35U: return "4000K (Cool white fluorescent)";
    case 36U: return "White Fluorescent";
    case 48U: return "3600K (Tungsten light-like)";
    default: return "";
    }
}

static const char*
olympus_scene_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 6U: return "Auto";
    case 7U: return "Sport";
    case 8U: return "Portrait";
    case 9U: return "Landscape+Portrait";
    case 10U: return "Landscape";
    case 11U: return "Night Scene";
    case 12U: return "Self Portrait";
    case 13U: return "Panorama";
    case 14U: return "2 in 1";
    case 15U: return "Movie";
    case 16U: return "Landscape+Portrait";
    case 17U: return "Night+Portrait";
    case 18U: return "Indoor";
    case 19U: return "Fireworks";
    case 20U: return "Sunset";
    case 21U: return "Beauty Skin";
    case 22U: return "Macro";
    case 23U: return "Super Macro";
    case 24U: return "Food";
    case 25U: return "Documents";
    case 26U: return "Museum";
    case 27U: return "Shoot & Select";
    case 28U: return "Beach & Snow";
    case 30U: return "Candle";
    case 31U: return "Available Light";
    case 34U: return "Pet";
    case 35U: return "Underwater Wide1";
    case 36U: return "Underwater Macro";
    case 39U: return "High Key";
    case 40U: return "Digital Image Stabilization";
    case 42U: return "Beach";
    case 43U: return "Snow";
    case 44U: return "Underwater Wide2";
    case 45U: return "Low Key";
    case 46U: return "Children";
    case 48U: return "Nature Macro";
    case 57U: return "Bulb";
    case 65U: return "Multiple Exposure";
    case 66U: return "e-Portrait";
    case 142U: return "Hand-held Starlight";
    case 154U: return "HDR";
    case 197U: return "Panning";
    case 203U: return "Light Trails";
    case 204U: return "Backlight HDR";
    case 205U: return "Silent";
    case 206U: return "Multi Focus Shot";
    default: return "";
    }
}

static const char*
olympus_picture_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Vivid";
    case 2U: return "Natural";
    case 3U: return "Muted";
    case 4U: return "Portrait";
    case 5U: return "i-Enhance";
    case 6U: return "e-Portrait";
    case 7U: return "Color Creator";
    case 8U: return "Underwater";
    case 9U: return "Color Profile 1";
    case 10U: return "Color Profile 2";
    case 11U: return "Color Profile 3";
    case 12U: return "Monochrome Profile 1";
    case 13U: return "Monochrome Profile 2";
    case 14U: return "Monochrome Profile 3";
    case 17U: return "Art Mode";
    case 18U: return "Monochrome Profile 4";
    case 256U: return "Monotone";
    case 512U: return "Sepia";
    default: return "";
    }
}

static const char*
olympus_raw_dev_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Color Temperature";
    case 2U: return "Gray Point";
    default: return "";
    }
}

static const char*
olympus_multiple_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Live Composite";
    case 2U: return "On (2 frames)";
    case 3U: return "On (3 frames)";
    default: return "";
    }
}

static const char*
olympus_camera_settings_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0200U: return olympus_exposure_mode_name(value);
    case 0x0202U: return olympus_metering_mode_name(value);
    case 0x0300U: return olympus_macro_mode_name(value);
    case 0x0301U: return olympus_focus_mode_name(value);
    case 0x0302U: return olympus_focus_process_name(value);
    case 0x0400U: return olympus_flash_mode_name(value);
    case 0x0500U: return olympus_white_balance_name(value);
    case 0x0509U: return olympus_scene_mode_name(value);
    case 0x0520U: return olympus_picture_mode_name(value);
    default: return "";
    }
}

static const char*
olympus_raw_development_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0101U: return olympus_raw_dev_white_balance_name(value);
    default: return "";
    }
}

static const char*
olympus_raw_development2_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x010CU: return olympus_picture_mode_name(value);
    default: return "";
    }
}

static const char*
olympus_image_processing_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x101CU: return olympus_multiple_exposure_mode_name(value);
    default: return "";
    }
}

static const char*
olympus_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_olympus_camera_settings_ifd(ifd)) {
        return olympus_camera_settings_value_name(tag, value);
    }
    if (is_olympus_raw_development_ifd(ifd)) {
        return olympus_raw_development_value_name(tag, value);
    }
    if (is_olympus_raw_development2_ifd(ifd)) {
        return olympus_raw_development2_value_name(tag, value);
    }
    if (is_olympus_image_processing_ifd(ifd)) {
        return olympus_image_processing_value_name(tag, value);
    }
    return "";
}

static bool
is_panasonic_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_panasonic0" || ifd_has_prefix(ifd, "mk_panasonic_main")
           || ifd == "makernote:panasonic:main";
}

static bool
is_panasonic_subdir_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_panasonic_subdir_")
           || ifd == "makernote:panasonic:subdir";
}

static const char*
panasonic_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Auto";
    case 2U: return "Daylight";
    case 3U: return "Cloudy";
    case 4U: return "Incandescent";
    case 5U: return "Manual";
    case 8U: return "Flash";
    case 10U: return "Black & White";
    case 11U: return "Manual 2";
    case 12U: return "Shade";
    case 13U: return "Kelvin";
    case 14U: return "Manual 3";
    case 15U: return "Manual 4";
    case 19U: return "Auto (cool)";
    default: return "";
    }
}

static const char*
panasonic_raw_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Cloudy";
    case 3U: return "Tungsten";
    case 4U: return "n/a";
    case 5U: return "Flash";
    case 6U: return "n/a";
    case 7U: return "n/a";
    case 8U: return "Custom#1";
    case 9U: return "Custom#2";
    case 10U: return "Custom#3";
    case 11U: return "Custom#4";
    case 12U: return "Shade";
    case 13U: return "Kelvin";
    case 16U: return "AWBc";
    default: return "";
    }
}

static const char*
panasonic_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Auto";
    case 2U: return "Manual";
    case 4U: return "Auto, Focus button";
    case 5U: return "Auto, Continuous";
    case 6U: return "AF-S";
    case 7U: return "AF-C";
    case 8U: return "AF-F";
    default: return "";
    }
}

static const char*
panasonic_macro_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "On";
    case 2U: return "Off";
    case 0x101U: return "Tele-Macro";
    case 0x201U: return "Macro Zoom";
    default: return "";
    }
}

static const char*
panasonic_shooting_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Normal";
    case 2U: return "Portrait";
    case 3U: return "Scenery";
    case 4U: return "Sports";
    case 5U: return "Night Portrait";
    case 6U: return "Program";
    case 7U: return "Aperture Priority";
    case 8U: return "Shutter Priority";
    case 9U: return "Macro";
    case 10U: return "Spot";
    case 11U: return "Manual";
    case 12U: return "Movie Preview";
    case 13U: return "Panning";
    case 14U: return "Simple";
    case 15U: return "Color Effects";
    case 16U: return "Self Portrait";
    case 17U: return "Economy";
    case 18U: return "Fireworks";
    case 19U: return "Party";
    case 20U: return "Snow";
    case 21U: return "Night Scenery";
    case 22U: return "Food";
    case 23U: return "Baby";
    case 24U: return "Soft Skin";
    case 25U: return "Candlelight";
    case 26U: return "Starry Night";
    case 27U: return "High Sensitivity";
    case 28U: return "Panorama Assist";
    case 29U: return "Underwater";
    case 30U: return "Beach";
    case 31U: return "Aerial Photo";
    case 32U: return "Sunset";
    case 33U: return "Pet";
    case 34U: return "Intelligent ISO";
    case 35U: return "Clipboard";
    case 36U: return "High Speed Continuous Shooting";
    case 37U: return "Intelligent Auto";
    case 39U: return "Multi-aspect";
    case 41U: return "Transform";
    case 42U: return "Flash Burst";
    case 43U: return "Pin Hole";
    case 44U: return "Film Grain";
    case 45U: return "My Color";
    case 46U: return "Photo Frame";
    case 48U: return "Movie";
    case 51U: return "HDR";
    case 52U: return "Peripheral Defocus";
    case 55U: return "Handheld Night Shot";
    case 57U: return "3D";
    case 59U: return "Creative Control";
    case 60U: return "Intelligent Auto Plus";
    case 62U: return "Panorama";
    case 63U: return "Glass Through";
    default: return "";
    }
}

static const char*
panasonic_burst_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    case 2U: return "Auto Exposure Bracketing (AEB)";
    case 3U: return "Focus Bracketing";
    case 4U: return "Unlimited";
    case 8U: return "White Balance Bracketing";
    case 17U: return "On (with flash)";
    case 18U: return "Aperture Bracketing";
    default: return "";
    }
}

static const char*
panasonic_color_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Natural";
    case 2U: return "Vivid";
    default: return "";
    }
}

static const char*
panasonic_optical_zoom_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Standard";
    case 2U: return "Extended";
    default: return "";
    }
}

static const char*
panasonic_film_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 1U: return "Standard (color)";
    case 2U: return "Dynamic (color)";
    case 3U: return "Nature (color)";
    case 4U: return "Smooth (color)";
    case 5U: return "Standard (B&W)";
    case 6U: return "Dynamic (B&W)";
    case 7U: return "Smooth (B&W)";
    case 10U: return "Nostalgic";
    case 11U: return "Vibrant";
    default: return "";
    }
}

static const char*
panasonic_flash_curtain_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 1U: return "1st";
    case 2U: return "2nd";
    default: return "";
    }
}

static const char*
panasonic_intelligent_exposure_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Low";
    case 2U: return "Standard";
    case 3U: return "High";
    default: return "";
    }
}

static const char*
panasonic_flash_warning_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "No";
    case 1U: return "Yes (flash required but disabled)";
    default: return "";
    }
}

static const char*
panasonic_multi_exposure_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 1U: return "Off";
    case 2U: return "On";
    default: return "";
    }
}

static const char*
panasonic_video_burst_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x01U: return "Off";
    case 0x04U: return "Post Focus";
    case 0x18U: return "4K Burst";
    case 0x28U: return "4K Burst (Start/Stop)";
    case 0x48U: return "4K Pre-burst";
    case 0x108U: return "Loop Recording";
    case 0x408U: return "Focus Stacking";
    case 0x810U: return "6K Burst";
    case 0x820U: return "6K Burst (Start/Stop)";
    case 0x1001U: return "High Resolution Mode";
    default: return "";
    }
}

static const char*
panasonic_long_exposure_nr_used_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "No";
    case 2U: return "Yes";
    default: return "";
    }
}

static const char*
panasonic_scene_mode_name(uint64_t value) noexcept
{
    if (value == 0U) {
        return "Off";
    }
    return panasonic_shooting_mode_name(value);
}

static const char*
panasonic_dark_focus_environment_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "No";
    case 2U: return "Yes";
    default: return "";
    }
}

static const char*
panasonic_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0003U: return panasonic_white_balance_name(value);
    case 0x0007U: return panasonic_focus_mode_name(value);
    case 0x001CU: return panasonic_macro_mode_name(value);
    case 0x001FU: return panasonic_shooting_mode_name(value);
    case 0x002AU: return panasonic_burst_mode_name(value);
    case 0x0032U: return panasonic_color_mode_name(value);
    case 0x0034U: return panasonic_optical_zoom_mode_name(value);
    case 0x0042U: return panasonic_film_mode_name(value);
    case 0x0048U: return panasonic_flash_curtain_name(value);
    case 0x005DU: return panasonic_intelligent_exposure_name(value);
    case 0x0062U: return panasonic_flash_warning_name(value);
    case 0x00B4U: return panasonic_multi_exposure_name(value);
    case 0x00BBU: return panasonic_video_burst_mode_name(value);
    case 0x00BEU: return panasonic_long_exposure_nr_used_name(value);
    case 0x8001U: return panasonic_scene_mode_name(value);
    case 0x8003U: return panasonic_dark_focus_environment_name(value);
    default: return "";
    }
}

static const char*
panasonic_subdir_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x3033U: return panasonic_raw_white_balance_name(value);
    default: return "";
    }
}

static const char*
panasonic_value_name(std::string_view ifd, uint16_t tag,
                     uint64_t value) noexcept
{
    if (is_panasonic_main_ifd(ifd)) {
        return panasonic_main_value_name(tag, value);
    }
    if (is_panasonic_subdir_ifd(ifd)) {
        return panasonic_subdir_value_name(tag, value);
    }
    return "";
}

static bool
is_phaseone_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_phaseone0" || ifd_has_prefix(ifd, "mk_phaseone_main")
           || ifd == "makernote:phaseone:main";
}

static const char*
phaseone_camera_orientation_name(uint64_t value) noexcept
{
    switch (value & 0x03U) {
    case 0U: return "Horizontal (normal)";
    case 1U: return "Rotate 90 CW";
    case 2U: return "Rotate 270 CW";
    case 3U: return "Rotate 180";
    default: return "";
    }
}

static const char*
phaseone_raw_format_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Uncompressed";
    case 1U: return "RAW 1";
    case 2U: return "RAW 2";
    case 3U: return "IIQ L";
    case 5U: return "IIQ S";
    case 6U: return "IIQ Sv2";
    case 8U: return "IIQ L16";
    default: return "";
    }
}

static const char*
phaseone_sequence_kind_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Bracketing: Shutter Speed";
    case 1U: return "Bracketing: Aperture";
    case 2U: return "Bracketing: ISO";
    case 3U: return "Hyperfocal";
    case 4U: return "Time Lapse";
    case 5U: return "HDR";
    case 6U: return "Focus Stacking";
    default: return "";
    }
}

static const char*
phaseone_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (!is_phaseone_main_ifd(ifd)) {
        return "";
    }
    switch (tag) {
    case 0x0100U: return phaseone_camera_orientation_name(value);
    case 0x010EU: return phaseone_raw_format_name(value);
    case 0x0263U: return phaseone_sequence_kind_name(value);
    default: return "";
    }
}

static bool
is_kodak_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_kodak0" || ifd_has_prefix(ifd, "mk_kodak_main")
           || ifd == "makernote:kodak:main";
}

static bool
is_kodak_type5_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_kodak_type5_")
           || ifd == "makernote:kodak:type5";
}

static bool
is_kodak_type11_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_kodak_type11_")
           || ifd == "makernote:kodak:type11";
}

static bool
is_kodak_subifd0_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_kodak_subifd0_")
           || ifd == "makernote:kodak:subifd0";
}

static bool
is_kodak_subifd2_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_kodak_subifd2_")
           || ifd == "makernote:kodak:subifd2";
}

static bool
is_kodak_kdc_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_kodak_kdc_ifd_")
           || ifd == "makernote:kodak:kdc_ifd";
}

static const char*
off_on_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    default: return "";
    }
}

static const char*
no_yes_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "No";
    case 1U: return "Yes";
    default: return "";
    }
}

static const char*
kodak_quality_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Fine";
    case 2U: return "Normal";
    default: return "";
    }
}

static const char*
kodak_shutter_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 8U: return "Aperture Priority";
    case 32U: return "Manual?";
    default: return "";
    }
}

static const char*
kodak_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Multi-segment";
    case 1U: return "Center-weighted average";
    case 2U: return "Spot";
    default: return "";
    }
}

static const char*
kodak_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 2U: return "Macro";
    default: return "";
    }
}

static const char*
kodak_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Flash?";
    case 2U: return "Tungsten";
    case 3U: return "Daylight";
    default: return "";
    }
}

static const char*
kodak_type5_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Daylight";
    case 2U: return "Flash";
    case 3U: return "Tungsten";
    default: return "";
    }
}

static const char*
kodak_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x00U: return "Auto";
    case 0x01U: return "Fill Flash";
    case 0x02U: return "Off";
    case 0x03U: return "Red-Eye";
    case 0x10U: return "Fill Flash";
    case 0x20U: return "Off";
    case 0x40U: return "Red-Eye?";
    default: return "";
    }
}

static const char*
kodak_color_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x01U: return "B&W";
    case 0x02U: return "Sepia";
    case 0x03U: return "B&W Yellow Filter";
    case 0x04U: return "B&W Red Filter";
    case 0x20U: return "Saturated Color";
    case 0x40U: return "Neutral Color";
    case 0x100U: return "Saturated Color";
    case 0x200U: return "Neutral Color";
    case 0x2000U: return "B&W";
    case 0x4000U: return "Sepia";
    default: return "";
    }
}

static const char*
kodak_scene_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Sport";
    case 3U: return "Portrait";
    case 4U: return "Landscape";
    case 6U: return "Beach";
    case 7U: return "Night Portrait";
    case 8U: return "Night Landscape";
    case 9U: return "Snow";
    case 10U: return "Text";
    case 11U: return "Fireworks";
    case 12U: return "Macro";
    case 13U: return "Museum";
    case 16U: return "Children";
    case 17U: return "Program";
    case 18U: return "Aperture Priority";
    case 19U: return "Shutter Priority";
    case 20U: return "Manual";
    case 25U: return "Back Light";
    case 28U: return "Candlelight";
    case 29U: return "Sunset";
    case 31U: return "Panorama Left-right";
    case 32U: return "Panorama Right-left";
    case 33U: return "Smart Scene";
    case 34U: return "High ISO";
    default: return "";
    }
}

static const char*
kodak_scene_mode_used_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program";
    case 2U: return "Aperture Priority";
    case 3U: return "Shutter Priority";
    case 4U: return "Manual";
    case 5U: return "Portrait";
    case 6U: return "Sport";
    case 7U: return "Children";
    case 8U: return "Museum";
    case 10U: return "High ISO";
    case 11U: return "Text";
    case 12U: return "Macro";
    case 13U: return "Back Light";
    case 16U: return "Landscape";
    case 17U: return "Night Landscape";
    case 18U: return "Night Portrait";
    case 19U: return "Snow";
    case 20U: return "Beach";
    case 21U: return "Fireworks";
    case 22U: return "Sunset";
    default: return "";
    }
}

static const char*
kodak_kdc_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Fluorescent";
    case 2U: return "Tungsten";
    case 3U: return "Daylight";
    case 6U: return "Shade";
    default: return "";
    }
}

static const char*
kodak_picture_effect_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "None";
    case 3U: return "Monochrome";
    case 9U: return "Kodachrome";
    default: return "";
    }
}

static const char*
kodak_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0009U: return kodak_quality_name(value);
    case 0x000AU: return off_on_name(value);
    case 0x001BU: return kodak_shutter_mode_name(value);
    case 0x001CU: return kodak_metering_mode_name(value);
    case 0x0038U: return kodak_focus_mode_name(value);
    case 0x0040U: return kodak_white_balance_name(value);
    case 0x005CU: return kodak_flash_mode_name(value);
    case 0x005DU: return no_yes_name(value);
    case 0x005EU: return value == 0U ? "Auto" : "";
    case 0x0066U: return kodak_color_mode_name(value);
    default: return "";
    }
}

static const char*
kodak_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_kodak_main_ifd(ifd)) {
        return kodak_main_value_name(tag, value);
    }
    if (is_kodak_type5_ifd(ifd)) {
        switch (tag) {
        case 0x001AU: return kodak_type5_white_balance_name(value);
        case 0x0027U: return kodak_flash_mode_name(value);
        case 0x002BU: return value == 0U ? "On" : value == 1U ? "Off" : "";
        default: return "";
        }
    }
    if (is_kodak_type11_ifd(ifd) && tag == 0x0203U) {
        return kodak_picture_effect_name(value);
    }
    if (is_kodak_subifd0_ifd(ifd) && tag == 0xFA02U) {
        return kodak_scene_mode_name(value);
    }
    if (is_kodak_subifd2_ifd(ifd) && (tag == 0x6002U || tag == 0xF002U)) {
        return kodak_scene_mode_used_name(value);
    }
    if (is_kodak_kdc_ifd(ifd) && tag == 0xFA0DU) {
        return kodak_kdc_white_balance_name(value);
    }
    return "";
}

static bool
is_minolta_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_minolta0" || ifd_has_prefix(ifd, "mk_minolta_main")
           || ifd == "makernote:minolta:main";
}

static bool
is_minolta_camera_settings_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_minolta_camerasettings_")
           || ifd == "makernote:minolta:camerasettings";
}

static bool
is_minolta_camera_settings5d_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_minolta_camerasettings5d_")
           || ifd == "makernote:minolta:camerasettings5d";
}

static bool
is_minolta_camera_settings7d_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_minolta_camerasettings7d_")
           || ifd == "makernote:minolta:camerasettings7d";
}

static bool
is_minolta_camera_settingsa100_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_minolta_camerasettingsa100_")
           || ifd == "makernote:minolta:camerasettingsa100";
}

static const char*
minolta_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program";
    case 1U: return "Aperture Priority";
    case 2U: return "Shutter Priority";
    case 3U: return "Manual";
    default: return "";
    }
}

static const char*
minolta_exposure_mode7d_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program";
    case 1U: return "Aperture Priority";
    case 2U: return "Shutter Priority";
    case 3U: return "Manual";
    case 4U: return "Auto";
    case 5U: return "Program-shift A";
    case 6U: return "Program-shift S";
    default: return "";
    }
}

static const char*
minolta_exposure_mode_a100_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Program";
    case 1U: return "Aperture Priority";
    case 2U: return "Shutter Priority";
    case 3U: return "Manual";
    case 4U: return "Auto";
    case 5U: return "Program Shift A";
    case 6U: return "Program Shift S";
    case 0x1013U: return "Portrait";
    case 0x1023U: return "Sports";
    case 0x1033U: return "Sunset";
    case 0x1043U: return "Night View/Portrait";
    case 0x1053U: return "Landscape";
    case 0x1083U: return "Macro";
    default: return "";
    }
}

static const char*
minolta_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Fill flash";
    case 1U: return "Red-eye reduction";
    case 2U: return "Rear flash sync";
    case 3U: return "Wireless";
    case 4U: return "Off?";
    default: return "";
    }
}

static const char*
minolta_flash_mode_basic_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Normal";
    case 1U: return "Red-eye reduction";
    case 2U: return "Rear flash sync";
    default: return "";
    }
}

static const char*
minolta_flash_mode_a100_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 2U: return "Rear Sync";
    case 3U: return "Wireless";
    case 4U: return "Fill Flash";
    default: return "";
    }
}

static const char*
minolta_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Cloudy";
    case 3U: return "Tungsten";
    case 5U: return "Custom";
    case 7U: return "Fluorescent";
    case 8U: return "Fluorescent 2";
    case 11U: return "Custom 2";
    case 12U: return "Custom 3";
    case 0x0800000U: return "Auto";
    case 0x1800000U: return "Daylight";
    case 0x2800000U: return "Cloudy";
    case 0x3800000U: return "Tungsten";
    case 0x4800000U: return "Flash";
    case 0x5800000U: return "Fluorescent";
    case 0x6800000U: return "Shade";
    case 0x7800000U: return "Custom1";
    case 0x8800000U: return "Custom2";
    case 0x9800000U: return "Custom3";
    default: return "";
    }
}

static const char*
minolta_white_balance_dslr_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Cloudy";
    case 3U: return "Shade";
    case 4U: return "Tungsten";
    case 5U: return "Fluorescent";
    case 6U: return "Flash";
    case 0x100U: return "Kelvin";
    case 0x200U: return "Manual";
    default: return "";
    }
}

static const char*
minolta_image_size_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Full";
    case 1U: return "1600x1200";
    case 2U: return "1280x960";
    case 3U: return "640x480";
    case 6U: return "2080x1560";
    case 7U: return "2560x1920";
    case 8U: return "3264x2176";
    default: return "";
    }
}

static const char*
minolta_image_size_basic_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Large";
    case 1U: return "Medium";
    case 2U: return "Small";
    default: return "";
    }
}

static const char*
minolta_quality_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Raw";
    case 1U: return "Super Fine";
    case 2U: return "Fine";
    case 3U: return "Standard";
    case 4U: return "Economy";
    case 5U: return "Extra Fine";
    case 16U: return "Fine";
    case 32U: return "Normal";
    case 34U: return "RAW+JPEG";
    case 48U: return "Economy";
    default: return "";
    }
}

static const char*
minolta_drive_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single";
    case 1U: return "Continuous";
    case 2U: return "Self-timer";
    case 4U: return "Bracketing";
    case 5U: return "Interval";
    case 6U: return "UHS continuous";
    case 7U: return "HS continuous";
    default: return "";
    }
}

static const char*
minolta_drive_mode_a100_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single Frame";
    case 1U: return "Continuous";
    case 2U: return "Self-timer";
    case 3U: return "Continuous Bracketing";
    case 4U: return "Single-Frame Bracketing";
    case 5U: return "White Balance Bracketing";
    default: return "";
    }
}

static const char*
minolta_drive_mode2_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x000U: return "Self-timer 10 sec";
    case 0x001U: return "Continuous";
    case 0x302U: return "Single-frame Bracketing Low";
    case 0x702U: return "Single-frame Bracketing High";
    case 0x303U: return "Continous Bracketing Low";
    case 0x703U: return "Continuous Bracketing High";
    case 0x004U: return "Self-timer 2 sec";
    case 0x005U: return "Single Frame";
    case 0x008U: return "White Balance Bracketing Low";
    case 0x009U: return "White Balance Bracketing High";
    default: return "";
    }
}

static const char*
minolta_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Multi-segment";
    case 1U: return "Center-weighted average";
    case 2U: return "Spot";
    default: return "";
    }
}

static const char*
minolta_macro_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On";
    default: return "";
    }
}

static const char*
minolta_sharpness_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Hard";
    case 1U: return "Normal";
    case 2U: return "Soft";
    default: return "";
    }
}

static const char*
minolta_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "AF";
    case 1U: return "MF";
    default: return "";
    }
}

static const char*
minolta_focus_mode7d_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "AF-S";
    case 1U: return "AF-C";
    case 3U: return "Manual";
    case 4U: return "AF-A";
    default: return "";
    }
}

static const char*
minolta_focus_mode_a100_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "AF-S";
    case 1U: return "AF-C";
    case 4U: return "AF-A";
    case 5U: return "Manual";
    case 6U: return "DMF";
    default: return "";
    }
}

static const char*
minolta_focus_area_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Wide Focus (normal)";
    case 1U: return "Spot Focus";
    default: return "";
    }
}

static const char*
minolta_af_area_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Wide";
    case 1U: return "Local";
    case 2U: return "Spot";
    default: return "";
    }
}

static const char*
minolta_color_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Natural color";
    case 1U: return "Black & White";
    case 2U: return "Vivid color";
    case 3U: return "Solarization";
    case 4U: return "Adobe RGB";
    case 5U: return "Sepia";
    case 9U: return "Natural";
    case 12U: return "Portrait";
    case 13U: return "Natural sRGB";
    case 14U: return "Natural+ sRGB";
    case 15U: return "Landscape";
    case 16U: return "Evening";
    case 17U: return "Night Scene";
    case 18U: return "Night Portrait";
    case 0x84U: return "Embed Adobe RGB";
    default: return "";
    }
}

static const char*
minolta_color_mode_a100_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Vivid";
    case 2U: return "Portrait";
    case 3U: return "Landscape";
    case 4U: return "Sunset";
    case 5U: return "Night Scene";
    case 7U: return "B&W";
    case 8U: return "Adobe RGB";
    default: return "";
    }
}

static const char*
minolta_color_space_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Natural sRGB";
    case 1U: return "Natural+ sRGB";
    case 2U: return "Monochrome";
    case 4U: return "Adobe RGB (ICC)";
    case 5U: return "Adobe RGB";
    default: return "";
    }
}

static const char*
minolta_color_space_a100_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "sRGB";
    case 2U: return "B&W";
    case 5U: return "Adobe RGB";
    default: return "";
    }
}

static const char*
minolta_scene_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Portrait";
    case 2U: return "Text";
    case 3U: return "Night Scene";
    case 4U: return "Sunset";
    case 5U: return "Sports";
    case 6U: return "Landscape";
    case 7U: return "Night Portrait";
    case 8U: return "Macro";
    case 9U: return "Super Macro";
    case 16U: return "Auto";
    case 17U: return "Night View/Portrait";
    case 18U: return "Sweep Panorama";
    case 19U: return "Handheld Night Shot";
    case 20U: return "Anti Motion Blur";
    case 21U: return "Cont. Priority AE";
    case 22U: return "Auto+";
    case 23U: return "3D Sweep Panorama";
    case 24U: return "Superior Auto";
    case 25U: return "High Sensitivity";
    case 26U: return "Fireworks";
    case 27U: return "Food";
    case 28U: return "Pet";
    case 33U: return "HDR";
    case 0xFFFFU: return "n/a";
    default: return "";
    }
}

static const char*
minolta_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0100U: return minolta_scene_mode_name(value);
    case 0x0101U: return minolta_color_mode_name(value);
    case 0x0102U: return minolta_quality_name(value);
    case 0x0103U: return minolta_image_size_name(value);
    case 0x0107U: return value == 1U ? "Off" : value == 5U ? "On" : "";
    case 0x0109U: return off_on_name(value);
    case 0x010AU:
        return value == 0U   ? "ISO Setting Used"
               : value == 1U ? "High Key"
               : value == 2U ? "Low Key"
                             : "";
    case 0x0113U: return off_on_name(value);
    case 0x0115U:
        switch (value) {
        case 0x00U: return "Auto";
        case 0x01U: return "Color Temperature/Color Filter";
        case 0x10U: return "Daylight";
        case 0x20U: return "Cloudy";
        case 0x30U: return "Shade";
        case 0x40U: return "Tungsten";
        case 0x50U: return "Flash";
        case 0x60U: return "Fluorescent";
        default: return "";
        }
    default: return "";
    }
}

static const char*
minolta_camera_settings_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0001U: return minolta_exposure_mode_name(value);
    case 0x0002U: return minolta_flash_mode_name(value);
    case 0x0003U: return minolta_white_balance_name(value);
    case 0x0004U: return minolta_image_size_name(value);
    case 0x0005U: return minolta_quality_name(value);
    case 0x0006U: return minolta_drive_mode_name(value);
    case 0x0007U: return minolta_metering_mode_name(value);
    case 0x000BU: return minolta_macro_mode_name(value);
    case 0x0014U: return no_yes_name(value);
    case 0x0021U: return minolta_sharpness_name(value);
    case 0x0024U:
        switch (value) {
        case 0U: return "100";
        case 1U: return "200";
        case 2U: return "400";
        case 3U: return "800";
        case 4U: return "Auto";
        case 5U: return "64";
        default: return "";
        }
    case 0x0028U: return minolta_color_mode_name(value);
    case 0x002BU: return value == 0U ? "No" : value == 1U ? "Fired" : "";
    case 0x0030U: return minolta_focus_mode_name(value);
    case 0x0031U: return minolta_focus_area_name(value);
    case 0x003FU:
        switch (value) {
        case 0U: return "ADI (Advanced Distance Integration)";
        case 1U: return "Pre-flash TTL";
        case 2U: return "Manual flash control";
        default: return "";
        }
    default: return "";
    }
}

static const char*
minolta_camera_settings5d_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x000AU: return minolta_exposure_mode_name(value);
    case 0x000CU: return minolta_image_size_basic_name(value);
    case 0x000DU: return minolta_quality_name(value);
    case 0x000EU: return minolta_white_balance_dslr_name(value);
    case 0x001FU:
        return value == 0U ? "Did not fire" : value == 1U ? "Fired" : "";
    case 0x0020U: return minolta_flash_mode_basic_name(value);
    case 0x0025U: return minolta_metering_mode_name(value);
    case 0x0026U:
        switch (value) {
        case 0U: return "Auto";
        case 1U: return "100";
        case 3U: return "200";
        case 4U: return "400";
        case 5U: return "800";
        case 6U: return "1600";
        case 7U: return "3200";
        case 8U: return "200 (Zone Matching High)";
        case 10U: return "80 (Zone Matching Low)";
        default: return "";
        }
    case 0x002FU: return minolta_color_space_name(value);
    default: return "";
    }
}

static const char*
minolta_camera_settings7d_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0000U: return minolta_exposure_mode7d_name(value);
    case 0x0002U: return minolta_image_size_basic_name(value);
    case 0x0003U: return minolta_quality_name(value);
    case 0x0004U: return minolta_white_balance_dslr_name(value);
    case 0x000EU: return minolta_focus_mode7d_name(value);
    case 0x0015U: return off_on_name(value);
    case 0x0016U: return minolta_flash_mode_basic_name(value);
    case 0x001CU: return minolta_camera_settings5d_value_name(0x0026U, value);
    case 0x0025U: return minolta_color_space_name(value);
    default: return "";
    }
}

static const char*
minolta_camera_settingsa100_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0000U: return minolta_exposure_mode_a100_name(value);
    case 0x000AU: return minolta_drive_mode2_name(value);
    case 0x000BU: return minolta_white_balance_dslr_name(value);
    case 0x000CU: return minolta_focus_mode_a100_name(value);
    case 0x000EU: return minolta_af_area_mode_name(value);
    case 0x000FU: return minolta_flash_mode_a100_name(value);
    case 0x0012U: return minolta_metering_mode_name(value);
    case 0x0016U: return minolta_color_mode_a100_name(value);
    case 0x0017U: return minolta_color_space_a100_name(value);
    case 0x001CU:
        return value == 0U   ? "ADI (Advanced Distance Integration)"
               : value == 1U ? "Pre-flash TTL"
                             : "";
    case 0x001EU: return minolta_drive_mode_a100_name(value);
    default: return "";
    }
}

static const char*
minolta_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_minolta_main_ifd(ifd)) {
        return minolta_main_value_name(tag, value);
    }
    if (is_minolta_camera_settings_ifd(ifd)) {
        return minolta_camera_settings_value_name(tag, value);
    }
    if (is_minolta_camera_settings5d_ifd(ifd)) {
        return minolta_camera_settings5d_value_name(tag, value);
    }
    if (is_minolta_camera_settings7d_ifd(ifd)) {
        return minolta_camera_settings7d_value_name(tag, value);
    }
    if (is_minolta_camera_settingsa100_ifd(ifd)) {
        return minolta_camera_settingsa100_value_name(tag, value);
    }
    return "";
}

static bool
is_sigma_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_sigma0" || ifd_has_prefix(ifd, "mk_sigma_main")
           || ifd == "makernote:sigma:main";
}

static const char*
sigma_exposure_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 'A': return "Aperture-priority AE";
    case 'M': return "Manual";
    case 'P': return "Program AE";
    case 'S': return "Shutter speed priority AE";
    default: return "";
    }
}

static const char*
sigma_metering_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 'A': return "Average";
    case 'C': return "Center-weighted average";
    case '8': return "Multi-segment";
    case 8U: return "Multi-segment";
    default: return "";
    }
}

static const char*
sigma_color_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "n/a";
    case 1U: return "Sepia";
    case 2U: return "B&W";
    case 3U: return "Standard";
    case 4U: return "Vivid";
    case 5U: return "Neutral";
    case 6U: return "Portrait";
    case 7U: return "Landscape";
    case 8U: return "FOV Classic Blue";
    default: return "";
    }
}

static const char*
sigma_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (!is_sigma_main_ifd(ifd)) {
        return "";
    }
    switch (tag) {
    case 0x0008U: return sigma_exposure_mode_name(value);
    case 0x0009U: return sigma_metering_mode_name(value);
    case 0x002CU: return sigma_color_mode_name(value);
    default: return "";
    }
}

static bool
is_samsung_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_samsung0" || ifd_has_prefix(ifd, "mk_samsung_ifd_")
           || ifd_has_prefix(ifd, "mk_samsung_type2_")
           || ifd == "makernote:samsung:ifd"
           || ifd == "makernote:samsung:type2";
}

static bool
is_samsung_picture_wizard_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_samsung_picturewizard_")
           || ifd == "makernote:samsung:picturewizard";
}

static const char*
samsung_device_type_name(uint64_t value) noexcept
{
    switch (value) {
    case 0x1000U: return "Compact Digital Camera";
    case 0x2000U: return "High-end NX Camera";
    case 0x3000U: return "HXM Video Camera";
    case 0x12000U: return "Cell Phone";
    case 0x300000U: return "SMX Video Camera";
    default: return "";
    }
}

static const char*
samsung_raw_data_byte_order_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Little-endian (Intel, II)";
    case 1U: return "Big-endian (Motorola, MM)";
    default: return "";
    }
}

static const char*
samsung_raw_data_cfa_pattern_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Unchanged";
    case 1U: return "Swap";
    case 65535U: return "Roll";
    default: return "";
    }
}

static const char*
samsung_color_space_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "sRGB";
    case 1U: return "Adobe RGB";
    default: return "";
    }
}

static const char*
samsung_picture_wizard_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Vivid";
    case 2U: return "Portrait";
    case 3U: return "Landscape";
    case 4U: return "Forest";
    case 5U: return "Retro";
    case 6U: return "Cool";
    case 7U: return "Calm";
    case 8U: return "Classic";
    case 9U: return "Custom1";
    case 10U: return "Custom2";
    case 11U: return "Custom3";
    case 255U: return "n/a";
    default: return "";
    }
}

static const char*
samsung_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_samsung_picture_wizard_ifd(ifd)) {
        switch (tag) {
        case 0x0000U: return samsung_picture_wizard_mode_name(value);
        default: return "";
        }
    }
    if (!is_samsung_ifd(ifd)) {
        return "";
    }
    switch (tag) {
    case 0x0002U: return samsung_device_type_name(value);
    case 0x0040U: return samsung_raw_data_byte_order_name(value);
    case 0x0041U: return value == 0U ? "Auto" : value == 1U ? "Manual" : "";
    case 0x0050U: return samsung_raw_data_cfa_pattern_name(value);
    case 0x0100U: return off_on_name(value);
    case 0x0120U: return off_on_name(value);
    case 0xA011U: return samsung_color_space_name(value);
    case 0xA012U: return off_on_name(value);
    default: return "";
    }
}

static bool
is_ricoh_main_ifd(std::string_view ifd) noexcept
{
    return ifd == "mk_ricoh0" || ifd_has_prefix(ifd, "mk_ricoh_main")
           || ifd == "makernote:ricoh:main";
}

static bool
is_ricoh_image_info_ifd(std::string_view ifd) noexcept
{
    return ifd_has_prefix(ifd, "mk_ricoh_imageinfo_")
           || ifd == "makernote:ricoh:imageinfo";
}

static const char*
ricoh_exposure_program_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Auto";
    case 2U: return "Program AE";
    case 3U: return "Aperture-priority AE";
    case 4U: return "Shutter speed priority AE";
    case 5U: return "Shutter/aperture priority AE";
    case 6U: return "Manual";
    case 7U: return "Movie";
    default: return "";
    }
}

static const char*
ricoh_drive_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Single-frame";
    case 1U: return "Continuous";
    case 8U: return "AF-priority Continuous";
    default: return "";
    }
}

static const char*
ricoh_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Multi-P Auto";
    case 2U: return "Daylight";
    case 3U: return "Cloudy";
    case 4U: return "Incandescent 1";
    case 5U: return "Incandescent 2";
    case 6U: return "Daylight Fluorescent";
    case 7U: return "Neutral White Fluorescent";
    case 8U: return "Cool White Fluorescent";
    case 9U: return "Warm White Fluorescent";
    case 10U: return "Manual";
    case 11U: return "Kelvin";
    case 12U: return "Shade";
    default: return "";
    }
}

static const char*
ricoh_focus_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 1U: return "Manual";
    case 2U: return "Multi AF";
    case 3U: return "Spot AF";
    case 4U: return "Snap";
    case 5U: return "Infinity";
    case 7U: return "Face Detect";
    case 8U: return "Subject Tracking";
    case 9U: return "Pinpoint AF";
    case 10U: return "Movie";
    default: return "";
    }
}

static const char*
ricoh_flash_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Auto, Fired";
    case 2U: return "On";
    case 3U: return "Auto, Fired, Red-eye reduction";
    case 4U: return "Slow Sync";
    case 5U: return "Manual";
    case 6U: return "On, Red-eye reduction";
    case 7U: return "Synchro, Red-eye reduction";
    case 8U: return "Auto, Did not fire";
    default: return "";
    }
}

static const char*
ricoh_manual_flash_output_name(uint64_t value) noexcept
{
    return value == 0U ? "Full" : "";
}

static const char*
ricoh_image_effects_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Standard";
    case 1U: return "Vivid";
    case 3U: return "Black & White";
    case 5U: return "B&W Toning Effect";
    case 6U: return "Setting 1";
    case 7U: return "Setting 2";
    case 9U: return "High-contrast B&W";
    case 10U: return "Cross Process";
    case 11U: return "Positive Film";
    case 12U: return "Bleach Bypass";
    case 13U: return "Retro";
    case 15U: return "Miniature";
    case 17U: return "High Key";
    default: return "";
    }
}

static const char*
ricoh_level_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "Low";
    case 2U: return "Medium";
    case 3U: return "High";
    default: return "";
    }
}

static const char*
ricoh_crop_mode_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Off";
    case 1U: return "On (35mm)";
    case 2U: return "On (47mm)";
    default: return "";
    }
}

static const char*
ricoh_image_info_sharpness_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Sharp";
    case 1U: return "Normal";
    case 2U: return "Soft";
    default: return "";
    }
}

static const char*
ricoh_image_info_white_balance_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "Daylight";
    case 2U: return "Cloudy";
    case 3U: return "Tungsten";
    case 4U: return "Fluorescent";
    case 5U: return "Manual";
    case 7U: return "Detail";
    case 9U: return "Multi-pattern Auto";
    default: return "";
    }
}

static const char*
ricoh_iso_setting_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "Auto";
    case 1U: return "64";
    case 2U: return "100";
    case 4U: return "200";
    case 6U: return "400";
    case 7U: return "800";
    case 8U: return "1600";
    case 9U: return "Auto";
    case 10U: return "3200";
    case 11U: return "100 (Low)";
    default: return "";
    }
}

static const char*
ricoh_saturation_name(uint64_t value) noexcept
{
    switch (value) {
    case 0U: return "High";
    case 1U: return "Normal";
    case 2U: return "Low";
    case 3U: return "B&W";
    case 6U: return "Toning Effect";
    case 9U: return "Vivid";
    case 10U: return "Natural";
    default: return "";
    }
}

static const char*
ricoh_main_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x1001U: return ricoh_exposure_program_name(value);
    case 0x1002U: return ricoh_drive_mode_name(value);
    case 0x1003U: return ricoh_white_balance_name(value);
    case 0x1006U: return ricoh_focus_mode_name(value);
    case 0x1009U: return off_on_name(value);
    case 0x100AU: return ricoh_flash_mode_name(value);
    case 0x100CU: return ricoh_manual_flash_output_name(value);
    case 0x100DU: return off_on_name(value);
    case 0x100EU: return ricoh_level_name(value);
    case 0x100FU: return ricoh_level_name(value);
    case 0x1010U: return ricoh_image_effects_name(value);
    case 0x1011U: return ricoh_level_name(value);
    case 0x1018U: return ricoh_crop_mode_name(value);
    case 0x1019U: return off_on_name(value);
    case 0x1205U: return value == 0U ? "Auto" : value == 2U ? "Manual" : "";
    default: return "";
    }
}

static const char*
ricoh_image_info_value_name(uint16_t tag, uint64_t value) noexcept
{
    switch (tag) {
    case 0x0020U:
        return value == 0U   ? "Off"
               : value == 1U ? "Auto"
               : value == 2U ? "On"
                             : "";
    case 0x0022U: return ricoh_image_info_sharpness_name(value);
    case 0x0026U: return ricoh_image_info_white_balance_name(value);
    case 0x0027U: return ricoh_iso_setting_name(value);
    case 0x0028U: return ricoh_saturation_name(value);
    default: return "";
    }
}

static const char*
ricoh_value_name(std::string_view ifd, uint16_t tag, uint64_t value) noexcept
{
    if (is_ricoh_main_ifd(ifd)) {
        return ricoh_main_value_name(tag, value);
    }
    if (is_ricoh_image_info_ifd(ifd)) {
        return ricoh_image_info_value_name(tag, value);
    }
    return "";
}

static const char*
makernote_tag_numeric_value_name(std::string_view ifd, uint16_t tag,
                                 uint64_t value) noexcept
{
    if (is_canon_camera_settings_ifd(ifd)) {
        return canon_camera_settings_value_name(tag, value);
    }
    if (is_canon_camera_info_ifd(ifd)) {
        return canon_camera_info_value_name(tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_nikon")
        || ifd_has_prefix(ifd, "makernote:nikon:")) {
        return nikon_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_fujifilm")
        || ifd_has_prefix(ifd, "makernote:fujifilm:")) {
        return fujifilm_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_sony")
        || ifd_has_prefix(ifd, "makernote:sony:")) {
        return sony_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_pentax")
        || ifd_has_prefix(ifd, "makernote:pentax:")) {
        return pentax_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_olympus")
        || ifd_has_prefix(ifd, "makernote:olympus:")) {
        return olympus_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_panasonic")
        || ifd_has_prefix(ifd, "makernote:panasonic:")) {
        return panasonic_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_phaseone")
        || ifd_has_prefix(ifd, "makernote:phaseone:")) {
        return phaseone_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_kodak")
        || ifd_has_prefix(ifd, "makernote:kodak:")) {
        return kodak_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_minolta")
        || ifd_has_prefix(ifd, "makernote:minolta:")) {
        return minolta_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_sigma")
        || ifd_has_prefix(ifd, "makernote:sigma:")) {
        return sigma_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_samsung")
        || ifd_has_prefix(ifd, "makernote:samsung:")) {
        return samsung_value_name(ifd, tag, value);
    }
    if (ifd_has_prefix(ifd, "mk_ricoh")
        || ifd_has_prefix(ifd, "makernote:ricoh:")) {
        return ricoh_value_name(ifd, tag, value);
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
    case 0xA402U: return exif_exposure_mode_name(value);
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
