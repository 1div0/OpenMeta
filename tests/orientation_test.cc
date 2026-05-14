// SPDX-License-Identifier: Apache-2.0

#include "openmeta/orientation.h"

#include <gtest/gtest.h>

namespace openmeta {
namespace {

    TEST(Orientation, InterpretsAllExifOrientationValues)
    {
        struct Case final {
            uint16_t orientation;
            uint16_t degrees;
            uint16_t rotation_only;
            bool mirrored;
            bool swaps_width_height;
            const char* name;
        };

        const Case cases[] = {
            { 1U, 0U, 1U, false, false, "Horizontal (normal)" },
            { 2U, 0U, 1U, true, false, "Mirror horizontal" },
            { 3U, 180U, 3U, false, false, "Rotate 180" },
            { 4U, 180U, 3U, true, false, "Mirror vertical" },
            { 5U, 270U, 8U, true, true, "Mirror horizontal and rotate 270 CW" },
            { 6U, 90U, 6U, false, true, "Rotate 90 CW" },
            { 7U, 90U, 6U, true, true, "Mirror horizontal and rotate 90 CW" },
            { 8U, 270U, 8U, false, true, "Rotate 270 CW" },
        };

        for (const Case& item : cases) {
            bool valid = false;
            EXPECT_TRUE(exif_orientation_is_valid(item.orientation));
            EXPECT_EQ(exif_orientation_rotation_degrees_cw(item.orientation,
                                                           &valid),
                      item.degrees);
            EXPECT_TRUE(valid);
            EXPECT_EQ(exif_orientation_rotation_only(item.orientation),
                      item.rotation_only);
            EXPECT_EQ(exif_orientation_is_mirrored(item.orientation),
                      item.mirrored);
            EXPECT_EQ(exif_orientation_swaps_width_height(item.orientation),
                      item.swaps_width_height);
            EXPECT_STREQ(exif_orientation_name(item.orientation), item.name);

            const ExifOrientationInterpretation interpreted
                = interpret_exif_orientation(item.orientation);
            EXPECT_EQ(interpreted.status, ExifOrientationStatus::Ok);
            EXPECT_EQ(interpreted.orientation, item.orientation);
            EXPECT_EQ(interpreted.rotation_degrees_cw, item.degrees);
            EXPECT_EQ(interpreted.rotation_only_orientation,
                      item.rotation_only);
            EXPECT_EQ(interpreted.mirrored, item.mirrored);
            EXPECT_EQ(interpreted.swaps_width_height, item.swaps_width_height);
            EXPECT_STREQ(interpreted.name, item.name);
        }
    }

    TEST(Orientation, RejectsInvalidOrientationValues)
    {
        bool valid = true;

        EXPECT_FALSE(exif_orientation_is_valid(0U));
        EXPECT_EQ(exif_orientation_rotation_degrees_cw(0U, &valid), 0U);
        EXPECT_FALSE(valid);
        EXPECT_EQ(exif_orientation_rotation_only(0U), 0U);
        EXPECT_STREQ(exif_orientation_name(0U), "Invalid orientation");

        const ExifOrientationInterpretation zero = interpret_exif_orientation(
            0U);
        EXPECT_EQ(zero.status, ExifOrientationStatus::InvalidArgument);
        EXPECT_EQ(zero.orientation, 0U);
        EXPECT_EQ(zero.rotation_only_orientation, 0U);
        EXPECT_FALSE(zero.mirrored);
        EXPECT_FALSE(zero.swaps_width_height);

        valid = true;
        EXPECT_FALSE(exif_orientation_is_valid(9U));
        EXPECT_EQ(exif_orientation_rotation_degrees_cw(9U, &valid), 0U);
        EXPECT_FALSE(valid);
        EXPECT_EQ(exif_orientation_rotation_only(9U), 0U);
        EXPECT_STREQ(exif_orientation_name(9U), "Invalid orientation");
    }

}  // namespace
}  // namespace openmeta
