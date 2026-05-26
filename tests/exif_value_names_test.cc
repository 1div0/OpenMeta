// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exif_value_names.h"

#include <gtest/gtest.h>

namespace openmeta {
namespace {

    TEST(ExifValueNames, MapsTiffImageLayoutEnums)
    {
        EXPECT_STREQ(tiff_compression_name(1U), "Uncompressed");
        EXPECT_STREQ(tiff_compression_name(8U), "Adobe Deflate");
        EXPECT_STREQ(tiff_photometric_interpretation_name(2U), "RGB");
        EXPECT_STREQ(tiff_photometric_interpretation_name(32803U),
                     "Color Filter Array");
        EXPECT_STREQ(tiff_planar_configuration_name(1U), "Chunky");
        EXPECT_STREQ(tiff_planar_configuration_name(2U), "Planar");
        EXPECT_STREQ(tiff_resolution_unit_name(3U), "cm");
        EXPECT_STREQ(tiff_compression_name(65535U), "");
    }

    TEST(ExifValueNames, MapsStandardExifEnums)
    {
        EXPECT_STREQ(exif_exposure_program_name(3U), "Aperture-priority AE");
        EXPECT_STREQ(exif_exposure_mode_name(1U), "Manual");
        EXPECT_STREQ(exif_exposure_mode_name(2U), "Auto bracket");
        EXPECT_STREQ(exif_metering_mode_name(5U), "Multi-segment");
        EXPECT_STREQ(exif_light_source_name(21U), "D65");
        EXPECT_STREQ(exif_flash_name(25U), "Auto, fired");
        EXPECT_STREQ(exif_color_space_name(1U), "sRGB");
        EXPECT_STREQ(exif_color_space_name(0xFFFFU), "Uncalibrated");
        EXPECT_STREQ(exif_white_balance_name(1U), "Manual");
        EXPECT_STREQ(exif_exposure_program_name(6U), "Action (High speed)");
        EXPECT_STREQ(exif_scene_capture_type_name(2U), "Portrait");
        EXPECT_STREQ(exif_scene_capture_type_name(3U), "Night");
        EXPECT_STREQ(exif_gain_control_name(2U), "High gain up");
        EXPECT_STREQ(exif_exposure_mode_name(42U), "");
        EXPECT_STREQ(exif_metering_mode_name(42U), "");
    }

    TEST(ExifValueNames, MapsDngEnums)
    {
        EXPECT_STREQ(dng_cfa_layout_name(1U), "Rectangular");
        EXPECT_STREQ(dng_cfa_layout_name(4U),
                     "Even rows offset right 1/2 column");
        EXPECT_STREQ(dng_calibration_illuminant_name(23U), "D50");
    }

    TEST(ExifValueNames, DispatchesByTag)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("ifd0", 0x0106U, 2U), "RGB");
        EXPECT_STREQ(exif_tag_numeric_value_name("exififd", 0x9208U, 4U),
                     "Flash");
        EXPECT_STREQ(exif_tag_numeric_value_name("exififd", 0xA402U, 2U),
                     "Auto bracket");
        EXPECT_STREQ(exif_tag_numeric_value_name("ifd0", 0xC617U, 1U),
                     "Rectangular");
        EXPECT_STREQ(exif_tag_numeric_value_name("ifd0", 0x9999U, 1U), "");
    }

    TEST(ExifValueNames, DispatchesCanonMakerNoteCameraSettingsEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0004U, 16U),
                     "External flash");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0007U, 0U),
                     "One-shot AF");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0011U, 3U),
                     "Evaluative");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0014U, 4U),
                     "Manual");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0027U, 1U),
                     "AF Point");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerasettings_0",
                                                 0x0103U, 1U),
                     "");
        EXPECT_STREQ(exif_tag_numeric_value_name(
                         "makernote:canon:camerasettings", 0x0014U, 8U),
                     "Flexible-priority AE");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_canon_camerainfo5d_0",
                                                 0x0015U, 4U),
                     "External Auto");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:canon:camerainfo7d",
                                                 0x0015U, 6U),
                     "Off");
    }

    TEST(ExifValueNames, DispatchesNikonMakerNoteEnums)
    {
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon0", 0x0087U, 9U),
                     "Fired, TTL Mode");
        EXPECT_STREQ(exif_tag_numeric_value_name("makernote:nikon:main",
                                                 0x0087U, 18U),
                     "LED Light");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_bracketinginfod810_0",
                                                 0x0017U, 3U),
                     "Highlight");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_otherinfod500_0",
                                                 0x0214U, 6U),
                     "Spot");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz8_0",
                                                 0x033EU, 1U),
                     "Center");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz9_0",
                                                 0x02C2U, 0U),
                     "Matrix");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz9v4_0",
                                                 0x02EAU, 3U),
                     "Highlight");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz8_0",
                                                 0x0340U, 4U),
                     "AF-F");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz9v3_0",
                                                 0x02ECU, 2U),
                     "AF-C");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon_menusettingsz9_0",
                                                 0x008CU, 2U),
                     "On (Series)");
        EXPECT_STREQ(exif_tag_numeric_value_name("mk_nikon0", 0x0103U, 1U), "");
    }

}  // namespace
}  // namespace openmeta
