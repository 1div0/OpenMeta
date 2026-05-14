// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/vendor_raw_processing.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace openmeta {
namespace {

    static void add_exif_u32(MetaStore* store, const char* ifd, uint16_t tag,
                             uint32_t value)
    {
        ASSERT_TRUE(store);
        Entry entry;
        entry.key   = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value = make_u32(value);
        ASSERT_NE(store->add_entry(entry), kInvalidEntryId);
    }

    static bool has_group(VendorRawProcessingGroup groups,
                          VendorRawProcessingGroup group) noexcept
    {
        return vendor_raw_processing_group_has(groups, group);
    }

}  // namespace

TEST(VendorRawProcessing, ClassifiesSonyRawProcessingFields)
{
    const VendorRawProcessingGroup sr2_groups
        = classify_vendor_raw_processing_field("mk_sony_sr2subifd_0",
                                               "WB_RGGBLevels", 0x7313U);
    EXPECT_TRUE(has_group(sr2_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(sr2_groups, VendorRawProcessingGroup::Color));
    EXPECT_FALSE(has_group(sr2_groups, VendorRawProcessingGroup::Storage));
    EXPECT_FALSE(has_group(sr2_groups, VendorRawProcessingGroup::RawData));

    const VendorRawProcessingGroup correction_groups
        = classify_vendor_raw_processing_field("mk_sony_tag9405b_0",
                                               "DistortionCorrParams", 0x0064U);
    EXPECT_TRUE(
        has_group(correction_groups, VendorRawProcessingGroup::LensCorrection));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_sony0", "LensModel",
                                               0x0100U);
    EXPECT_FALSE(
        has_group(capture_groups, VendorRawProcessingGroup::LensCorrection));
    EXPECT_FALSE(has_group(capture_groups, VendorRawProcessingGroup::Color));
}

TEST(VendorRawProcessing, ClassifiesCanonRawProcessingFields)
{
    const VendorRawProcessingGroup private_groups
        = classify_vendor_raw_processing_field("mk_canon_colordata5_0", "",
                                               0x0001U);
    EXPECT_TRUE(
        has_group(private_groups, VendorRawProcessingGroup::PrivateTable));
    EXPECT_FALSE(has_group(private_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_canon_colordata10_0",
                                               "WB_RGGBLevelsAsShot", 0x0000U);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup calibration_groups
        = classify_vendor_raw_processing_field("mk_canon_colorcalib_0",
                                               "CameraColorCalibration15",
                                               0x0038U);
    EXPECT_TRUE(has_group(calibration_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup geometry_groups
        = classify_vendor_raw_processing_field("mk_canon_aspectinfo_0",
                                               "CroppedImageWidth", 0x0004U);
    EXPECT_TRUE(has_group(geometry_groups, VendorRawProcessingGroup::Geometry));
}

TEST(VendorRawProcessing, ClassifiesNikonRawProcessingFields)
{
    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_nikon_colorbalancec_0",
                                               "WB_RGGBLevelsAuto", 0x0114U);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Sensor));

    const VendorRawProcessingGroup correction_groups
        = classify_vendor_raw_processing_field("mk_nikon_distortioninfo_0",
                                               "RadialDistortionCoefficient1",
                                               0x0014U);
    EXPECT_TRUE(
        has_group(correction_groups, VendorRawProcessingGroup::LensCorrection));

    const VendorRawProcessingGroup nef_groups
        = classify_vendor_raw_processing_field("mk_nikon_nefinfo_0",
                                               "DistortionInfo", 0x0005U);
    EXPECT_TRUE(
        has_group(nef_groups, VendorRawProcessingGroup::LensCorrection));
    EXPECT_TRUE(has_group(nef_groups, VendorRawProcessingGroup::PrivateTable));
    EXPECT_FALSE(has_group(nef_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_FALSE(has_group(nef_groups, VendorRawProcessingGroup::Storage));

    const VendorRawProcessingGroup unknown_nef_groups
        = classify_vendor_raw_processing_field("mk_nikon_nefinfo_0",
                                               "Nikon_NEFInfo_0x000b", 0x000BU);
    EXPECT_TRUE(
        has_group(unknown_nef_groups, VendorRawProcessingGroup::PrivateTable));
    EXPECT_FALSE(has_group(unknown_nef_groups,
                           VendorRawProcessingGroup::LensCorrection));
}

TEST(VendorRawProcessing, ClassifiesFujifilmRawProcessingFields)
{
    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_fuji0",
                                               "WhiteBalanceFineTune", 0x100AU);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup channel_groups
        = classify_vendor_raw_processing_field("mk_fuji0", "WBRed", 0x144AU);
    EXPECT_TRUE(
        has_group(channel_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(channel_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup raf_groups
        = classify_vendor_raw_processing_field("mk_fuji_rafdata_0",
                                               "RawImageWidth", 0x0000U);
    EXPECT_TRUE(has_group(raf_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(raf_groups, VendorRawProcessingGroup::RawData));
    EXPECT_TRUE(has_group(raf_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup raf_header_groups
        = classify_vendor_raw_processing_field("raf_header",
                                               "PreviewImageStart", 0x0054U);
    EXPECT_TRUE(
        has_group(raf_header_groups, VendorRawProcessingGroup::Storage));
    EXPECT_TRUE(
        has_group(raf_header_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup raf_native_groups
        = classify_vendor_raw_processing_field("raf_0", "RawImageCropTopLeft",
                                               0x0110U);
    EXPECT_TRUE(
        has_group(raf_native_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(
        has_group(raf_native_groups, VendorRawProcessingGroup::RawData));
    EXPECT_TRUE(
        has_group(raf_native_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup correction_groups
        = classify_vendor_raw_processing_field("mk_fuji0",
                                               "LensModulationOptimizer",
                                               0x1045U);
    EXPECT_TRUE(
        has_group(correction_groups, VendorRawProcessingGroup::LensCorrection));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_fuji0", "Quality", 0x1000U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesPentaxRawProcessingFields)
{
    const VendorRawProcessingGroup raw_size_groups
        = classify_vendor_raw_processing_field("mk_pentax0", "RawImageSize",
                                               0x0039U);
    EXPECT_TRUE(has_group(raw_size_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(raw_size_groups, VendorRawProcessingGroup::RawData));

    const VendorRawProcessingGroup matrix_groups
        = classify_vendor_raw_processing_field("mk_pentax0", "ColorMatrixA",
                                               0x0203U);
    EXPECT_TRUE(has_group(matrix_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_pentax0",
                                               "WB_RGGBLevelsDaylight",
                                               0x020DU);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup correction_groups
        = classify_vendor_raw_processing_field("mk_pentax_lenscorr_0",
                                               "DistortionCorrection", 0x0000U);
    EXPECT_TRUE(
        has_group(correction_groups, VendorRawProcessingGroup::LensCorrection));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_pentax0", "PentaxModelID",
                                               0x0005U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesPanasonicRawProcessingFields)
{
    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_panasonic_subdir_0",
                                               "WB_RGBLevels", 0x3036U);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));
    EXPECT_FALSE(has_group(wb_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup sensor_groups
        = classify_vendor_raw_processing_field("mk_panasonic_subdir_0",
                                               "SensorWidth", 0x312BU);
    EXPECT_TRUE(has_group(sensor_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(sensor_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_FALSE(
        has_group(sensor_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup image_groups
        = classify_vendor_raw_processing_field("mk_panasonic0",
                                               "PanasonicImageWidth", 0x004BU);
    EXPECT_TRUE(has_group(image_groups, VendorRawProcessingGroup::Geometry));

    const VendorRawProcessingGroup correction_groups
        = classify_vendor_raw_processing_field("mk_panasonic0",
                                               "ShadingCompensation", 0x008AU);
    EXPECT_TRUE(
        has_group(correction_groups, VendorRawProcessingGroup::LensCorrection));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_panasonic0", "LensType",
                                               0x0051U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);

    const VendorRawProcessingGroup private_groups
        = classify_vendor_raw_processing_field("mk_panasonic0", "DataDump",
                                               0x0021U);
    EXPECT_TRUE(
        has_group(private_groups, VendorRawProcessingGroup::PrivateTable));
}

TEST(VendorRawProcessing, ClassifiesOlympusRawProcessingFields)
{
    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_olympus_imageprocessing_0",
                                               "WB_RBLevelsCWB2", 0x010FU);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup matrix_groups
        = classify_vendor_raw_processing_field("mk_olympus_imageprocessing_0",
                                               "ColorMatrix", 0x0200U);
    EXPECT_TRUE(has_group(matrix_groups, VendorRawProcessingGroup::Color));
    EXPECT_TRUE(
        has_group(matrix_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup crop_groups
        = classify_vendor_raw_processing_field("mk_olympus_imageprocessing_0",
                                               "CropWidth", 0x0614U);
    EXPECT_TRUE(has_group(crop_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(crop_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup correction_groups
        = classify_vendor_raw_processing_field("mk_olympus_imageprocessing_0",
                                               "DistortionCorrection2",
                                               0x1011U);
    EXPECT_TRUE(
        has_group(correction_groups, VendorRawProcessingGroup::LensCorrection));
    EXPECT_TRUE(
        has_group(correction_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup rawdev_groups
        = classify_vendor_raw_processing_field("mk_olympus_rawdevelopment2_0",
                                               "RawDevNoiseReduction", 0x010AU);
    EXPECT_TRUE(has_group(rawdev_groups, VendorRawProcessingGroup::RawData));
    EXPECT_TRUE(has_group(rawdev_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(
        has_group(rawdev_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_olympus_equipment_0",
                                               "LensModel", 0x0203U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesKodakRawProcessingFields)
{
    const VendorRawProcessingGroup geometry_groups
        = classify_vendor_raw_processing_field("mk_kodak_ifd_0",
                                               "SensorImageWidth", 0x03EDU);
    EXPECT_TRUE(has_group(geometry_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(geometry_groups, VendorRawProcessingGroup::Sensor));

    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_kodak_ifd_0",
                                               "WB_RGBLevelsAsShot", 0x0847U);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup black_groups
        = classify_vendor_raw_processing_field("mk_kodak_ifd_0",
                                               "BlackLevelBottom", 0x03F0U);
    EXPECT_TRUE(has_group(black_groups, VendorRawProcessingGroup::Sensor));

    const VendorRawProcessingGroup raw_groups
        = classify_vendor_raw_processing_field("mk_kodak_ifd_0", "RawHistogram",
                                               0x0C4EU);
    EXPECT_TRUE(has_group(raw_groups, VendorRawProcessingGroup::RawData));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_kodak0", "KodakModel",
                                               0x0000U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesMinoltaRawProcessingFields)
{
    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_minolta_wbinfoa100_0",
                                               "WB_RGBLevels", 0x0096U);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup geometry_groups
        = classify_vendor_raw_processing_field("mk_minoltaraw_prd_0",
                                               "SensorWidth", 0x000AU);
    EXPECT_TRUE(has_group(geometry_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(geometry_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(has_group(geometry_groups, VendorRawProcessingGroup::RawData));
    EXPECT_TRUE(
        has_group(geometry_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup depth_groups
        = classify_vendor_raw_processing_field("mk_minoltaraw_prd_0",
                                               "RawDepth", 0x0010U);
    EXPECT_TRUE(has_group(depth_groups, VendorRawProcessingGroup::RawData));
    EXPECT_TRUE(has_group(depth_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(
        has_group(depth_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup storage_groups
        = classify_vendor_raw_processing_field("mk_minoltaraw_rif_0",
                                               "RawDataLength", 0x0050U);
    EXPECT_TRUE(has_group(storage_groups, VendorRawProcessingGroup::Storage));
    EXPECT_TRUE(has_group(storage_groups, VendorRawProcessingGroup::RawData));
    EXPECT_TRUE(
        has_group(storage_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_minolta0", "LensType",
                                               0x010CU);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesSigmaRawProcessingFields)
{
    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_sigma_wbsettings_0",
                                               "WB_RGBLevelsAuto", 0x0000U);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup calibration_groups
        = classify_vendor_raw_processing_field("mk_sigma0", "CameraCalibration",
                                               0x011FU);
    EXPECT_TRUE(has_group(calibration_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup vignette_groups
        = classify_vendor_raw_processing_field("mk_sigma0", "Vignette",
                                               0x0139U);
    EXPECT_TRUE(
        has_group(vignette_groups, VendorRawProcessingGroup::LensCorrection));

    const VendorRawProcessingGroup sensor_groups
        = classify_vendor_raw_processing_field("mk_sigma0", "SensorTemperature",
                                               0x0039U);
    EXPECT_TRUE(has_group(sensor_groups, VendorRawProcessingGroup::Sensor));

    const VendorRawProcessingGroup x3f_geometry_groups
        = classify_vendor_raw_processing_field("x3f_header", "ImageWidth",
                                               0x0007U);
    EXPECT_TRUE(
        has_group(x3f_geometry_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(
        has_group(x3f_geometry_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup x3f_color_groups
        = classify_vendor_raw_processing_field("x3f_header_ext",
                                               "ExposureAdjust", 0x0001U);
    EXPECT_TRUE(has_group(x3f_color_groups, VendorRawProcessingGroup::Color));
    EXPECT_TRUE(
        has_group(x3f_color_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup x3f_sensor_groups
        = classify_vendor_raw_processing_field("x3f_prop", "SensorTemperature",
                                               0x0015U);
    EXPECT_TRUE(has_group(x3f_sensor_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(
        has_group(x3f_sensor_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_sigma0", "LensType",
                                               0x0027U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesSamsungRawProcessingFields)
{
    const VendorRawProcessingGroup storage_groups
        = classify_vendor_raw_processing_field("mk_samsung_type2_0",
                                               "RawDataByteOrder", 0x0040U);
    EXPECT_TRUE(has_group(storage_groups, VendorRawProcessingGroup::Storage));
    EXPECT_TRUE(has_group(storage_groups, VendorRawProcessingGroup::RawData));

    const VendorRawProcessingGroup cfa_groups
        = classify_vendor_raw_processing_field("mk_samsung_type2_0",
                                               "RawDataCFAPattern", 0x0050U);
    EXPECT_TRUE(has_group(cfa_groups, VendorRawProcessingGroup::RawData));
    EXPECT_TRUE(has_group(cfa_groups, VendorRawProcessingGroup::Sensor));

    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_samsung_type2_0",
                                               "WB_RGGBLevelsUncorrected",
                                               0xA021U);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup matrix_groups
        = classify_vendor_raw_processing_field("mk_samsung_type2_0",
                                               "ColorMatrix", 0xA030U);
    EXPECT_TRUE(has_group(matrix_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup raw_groups
        = classify_vendor_raw_processing_field("mk_samsung_type2_0", "RawData",
                                               0xA048U);
    EXPECT_TRUE(has_group(raw_groups, VendorRawProcessingGroup::RawData));

    const VendorRawProcessingGroup correction_groups
        = classify_vendor_raw_processing_field("mk_samsung_type2_0",
                                               "Distortion", 0xA050U);
    EXPECT_TRUE(
        has_group(correction_groups, VendorRawProcessingGroup::LensCorrection));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_samsung_type2_0", "LensType",
                                               0xA003U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesRicohRawProcessingFields)
{
    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_ricoh0",
                                               "WhiteBalanceFineTune", 0x1004U);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup geometry_groups
        = classify_vendor_raw_processing_field("mk_ricoh0", "SensorWidth",
                                               0x1601U);
    EXPECT_TRUE(has_group(geometry_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(geometry_groups, VendorRawProcessingGroup::Sensor));

    const VendorRawProcessingGroup vignette_groups
        = classify_vendor_raw_processing_field("mk_ricoh0", "Vignetting",
                                               0x1011U);
    EXPECT_TRUE(
        has_group(vignette_groups, VendorRawProcessingGroup::LensCorrection));

    const VendorRawProcessingGroup noise_groups
        = classify_vendor_raw_processing_field("mk_ricoh0", "NoiseReduction",
                                               0x100FU);
    EXPECT_TRUE(has_group(noise_groups, VendorRawProcessingGroup::Sensor));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_ricoh0", "SerialNumber",
                                               0x0005U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesAppleSourceProcessingFields)
{
    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_apple0", "ColorTemperature",
                                               0x002DU);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup hdr_groups
        = classify_vendor_raw_processing_field("mk_apple0", "HDRGain", 0x0030U);
    EXPECT_TRUE(has_group(hdr_groups, VendorRawProcessingGroup::Color));
    EXPECT_TRUE(has_group(hdr_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup processing_groups
        = classify_vendor_raw_processing_field("mk_apple0",
                                               "ImageProcessingFlags", 0x0019U);
    EXPECT_TRUE(has_group(processing_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(
        has_group(processing_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup motion_groups
        = classify_vendor_raw_processing_field("mk_apple0",
                                               "AccelerationVector", 0x0008U);
    EXPECT_TRUE(has_group(motion_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(motion_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(
        has_group(motion_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup hdr_headroom_groups
        = classify_vendor_raw_processing_field("mk_apple0", "HDRHeadroom",
                                               0x0021U);
    EXPECT_TRUE(
        has_group(hdr_headroom_groups, VendorRawProcessingGroup::Color));
    EXPECT_TRUE(
        has_group(hdr_headroom_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup camera_geometry_groups
        = classify_vendor_raw_processing_field("mk_apple0", "FrontFacingCamera",
                                               0x0045U);
    EXPECT_TRUE(
        has_group(camera_geometry_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(camera_geometry_groups,
                          VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_apple0", "CameraType",
                                               0x002EU);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesDjiThermalProcessingFields)
{
    const VendorRawProcessingGroup thermal_groups
        = classify_vendor_raw_processing_field("mk_dji_thermalparams2_0",
                                               "AmbientTemperature", 0x0000U);
    EXPECT_TRUE(has_group(thermal_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(
        has_group(thermal_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup emissivity_groups
        = classify_vendor_raw_processing_field("mk_dji_thermalparams3_0",
                                               "Emissivity", 0x0008U);
    EXPECT_TRUE(has_group(emissivity_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(
        has_group(emissivity_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup pose_groups
        = classify_vendor_raw_processing_field("mk_dji0", "CameraYaw", 0x000AU);
    EXPECT_TRUE(has_group(pose_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(pose_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(has_group(pose_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup humidity_groups
        = classify_vendor_raw_processing_field("mk_dji_thermalparams_0",
                                               "RelativeHumidity", 0x0046U);
    EXPECT_TRUE(has_group(humidity_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(
        has_group(humidity_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_dji0", "Make", 0x0001U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesGoogleComputationalProcessingFields)
{
    const VendorRawProcessingGroup log_groups
        = classify_vendor_raw_processing_field("mk_google_hdrplusmakernote_0",
                                               "TimeLogText", 0x0002U);
    EXPECT_TRUE(has_group(log_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup frame_groups
        = classify_vendor_raw_processing_field("mk_google_shotlogdata_0",
                                               "OriginalPayloadFrameCount",
                                               0x0003U);
    EXPECT_TRUE(has_group(frame_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(
        has_group(frame_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup metering_groups
        = classify_vendor_raw_processing_field("mk_google_shotlogdata_0",
                                               "MeteringFrameCount", 0x0002U);
    EXPECT_TRUE(has_group(metering_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(
        has_group(metering_groups, VendorRawProcessingGroup::PrivateTable));
}

TEST(VendorRawProcessing, ClassifiesFlirThermalProcessingFields)
{
    const VendorRawProcessingGroup raw_groups
        = classify_vendor_raw_processing_field("mk_flir_fff_rawdata_0",
                                               "RawThermalImageWidth", 0x0001U);
    EXPECT_TRUE(has_group(raw_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(raw_groups, VendorRawProcessingGroup::RawData));
    EXPECT_TRUE(has_group(raw_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(has_group(raw_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup planck_groups
        = classify_vendor_raw_processing_field("mk_flir_fff_camerainfo_0",
                                               "PlanckR1", 0x0058U);
    EXPECT_TRUE(has_group(planck_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(has_group(planck_groups, VendorRawProcessingGroup::Thermal));
    EXPECT_TRUE(
        has_group(planck_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup palette_groups
        = classify_vendor_raw_processing_field("mk_flir_fff_paletteinfo_0",
                                               "PaletteName", 0x0050U);
    EXPECT_TRUE(has_group(palette_groups, VendorRawProcessingGroup::Color));
    EXPECT_TRUE(
        has_group(palette_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup raw_value_groups
        = classify_vendor_raw_processing_field("mk_flir_fff_camerainfo_0",
                                               "RawValueRangeMin", 0x0310U);
    EXPECT_TRUE(has_group(raw_value_groups, VendorRawProcessingGroup::RawData));
    EXPECT_TRUE(has_group(raw_value_groups, VendorRawProcessingGroup::Sensor));

    const VendorRawProcessingGroup fov_groups
        = classify_vendor_raw_processing_field("mk_flir_fff_camerainfo_0",
                                               "FieldOfView", 0x01B4U);
    EXPECT_TRUE(has_group(fov_groups, VendorRawProcessingGroup::Geometry));

    const VendorRawProcessingGroup focus_groups
        = classify_vendor_raw_processing_field("mk_flir_fff_camerainfo_0",
                                               "FocusDistance", 0x045CU);
    EXPECT_TRUE(has_group(focus_groups, VendorRawProcessingGroup::Geometry));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_flir_fff_camerainfo_0",
                                               "CameraModel", 0x00D4U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesSourcePrivateSubgroups)
{
    const VendorRawProcessingGroup apple_hdr
        = classify_vendor_raw_processing_field("mk_apple", "HDRHeadroom",
                                               0x0054U);
    EXPECT_TRUE(has_group(apple_hdr, VendorRawProcessingGroup::Computational));
    EXPECT_TRUE(has_group(apple_hdr, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup dji_thermal
        = classify_vendor_raw_processing_field("mk_dji_thermalparams",
                                               "Emissivity", 0x0001U);
    EXPECT_TRUE(has_group(dji_thermal, VendorRawProcessingGroup::Thermal));
    EXPECT_TRUE(has_group(dji_thermal, VendorRawProcessingGroup::Sensor));

    const VendorRawProcessingGroup google_shot
        = classify_vendor_raw_processing_field("mk_google_shotlogdata",
                                               "ShotLogData", 0x0001U);
    EXPECT_TRUE(
        has_group(google_shot, VendorRawProcessingGroup::Computational));
    EXPECT_TRUE(has_group(google_shot, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup flir_raw
        = classify_vendor_raw_processing_field("mk_flir_fff_rawdata",
                                               "RawThermalImage", 0x0001U);
    EXPECT_TRUE(has_group(flir_raw, VendorRawProcessingGroup::Thermal));
    EXPECT_TRUE(has_group(flir_raw, VendorRawProcessingGroup::RawData));

    const VendorRawProcessingGroup face
        = classify_vendor_raw_processing_field("mk_casio_faceinfo",
                                               "Face1Position", 0x0018U);
    EXPECT_TRUE(has_group(face, VendorRawProcessingGroup::FaceGeometry));

    const VendorRawProcessingGroup stitch
        = classify_vendor_raw_processing_field("mk_microsoft",
                                               "PanoramicStitchTheta0",
                                               0x0001U);
    EXPECT_TRUE(has_group(stitch, VendorRawProcessingGroup::Stitch));
    EXPECT_STREQ(vendor_raw_processing_group_name(
                     VendorRawProcessingGroup::Computational),
                 "computational");
    EXPECT_STREQ(vendor_raw_processing_group_name(
                     VendorRawProcessingGroup::Thermal),
                 "thermal");
}

TEST(VendorRawProcessing, ClassifiesCasioSourceProcessingFields)
{
    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_casio_type2_0",
                                               "WhiteBalanceBias", 0x2011U);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup preview_groups
        = classify_vendor_raw_processing_field("mk_casio_type2_0",
                                               "PreviewImageStart", 0x0004U);
    EXPECT_TRUE(has_group(preview_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(preview_groups, VendorRawProcessingGroup::Storage));
    EXPECT_TRUE(
        has_group(preview_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup face_groups
        = classify_vendor_raw_processing_field("mk_casio_faceinfo2_0",
                                               "Face1Position", 0x0018U);
    EXPECT_TRUE(has_group(face_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(face_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup color_groups
        = classify_vendor_raw_processing_field("mk_casio_type2_0", "ArtMode",
                                               0x301BU);
    EXPECT_TRUE(has_group(color_groups, VendorRawProcessingGroup::Color));
    EXPECT_TRUE(
        has_group(color_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_casio_type2_0", "Quality",
                                               0x3002U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesSanyoSourceProcessingFields)
{
    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_sanyo_mov_0", "WhiteBalance",
                                               0x0044U);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup dump_groups
        = classify_vendor_raw_processing_field("mk_sanyo0", "DataDump",
                                               0x0F00U);
    EXPECT_TRUE(has_group(dump_groups, VendorRawProcessingGroup::Storage));
    EXPECT_TRUE(has_group(dump_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup face_groups
        = classify_vendor_raw_processing_field("mk_sanyo_faceinfo_0",
                                               "FacePosition", 0x0004U);
    EXPECT_TRUE(has_group(face_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(has_group(face_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_sanyo_mov_0", "Make",
                                               0x0000U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesKyoceraRawProcessingFields)
{
    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_kyoceraraw0",
                                               "WB_RGGBLevels", 0x003CU);
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::WhiteBalance));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::Color));
    EXPECT_TRUE(has_group(wb_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_kyoceraraw0", "Model",
                                               0x000CU);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesReconyxSourceProcessingFields)
{
    const VendorRawProcessingGroup color_groups
        = classify_vendor_raw_processing_field("mk_reconyx_hyperfire2_0",
                                               "Contrast", 0x0052U);
    EXPECT_TRUE(has_group(color_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup sensor_groups
        = classify_vendor_raw_processing_field("mk_reconyx_hyperfire2_0",
                                               "AmbientTemperature", 0x0050U);
    EXPECT_TRUE(has_group(sensor_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(
        has_group(sensor_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup private_groups
        = classify_vendor_raw_processing_field("mk_reconyx_hyperfire2_0",
                                               "TriggerMode", 0x0034U);
    EXPECT_TRUE(
        has_group(private_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_reconyx_hyperfire2_0",
                                               "SerialNumber", 0x007EU);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesHpSourceProcessingFields)
{
    const VendorRawProcessingGroup private_groups
        = classify_vendor_raw_processing_field("mk_hp0", "HP_0x0200", 0x0200U);
    EXPECT_TRUE(
        has_group(private_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_hp_type6_0", "SerialNumber",
                                               0x0058U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesJvcSourceProcessingFields)
{
    const VendorRawProcessingGroup cpu_groups
        = classify_vendor_raw_processing_field("mk_jvc0", "CPUVersions",
                                               0x0002U);
    EXPECT_TRUE(has_group(cpu_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_jvc0", "Quality", 0x0003U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesGeSourceProcessingFields)
{
    const VendorRawProcessingGroup private_groups
        = classify_vendor_raw_processing_field("mk_ge0", "GE_0x0104", 0x0104U);
    EXPECT_TRUE(
        has_group(private_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_ge0", "GEModel", 0x0207U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesMotorolaSourceProcessingFields)
{
    const VendorRawProcessingGroup rendered_groups
        = classify_vendor_raw_processing_field("mk_motorola0", "CustomRendered",
                                               0x6420U);
    EXPECT_TRUE(has_group(rendered_groups, VendorRawProcessingGroup::Color));
    EXPECT_TRUE(
        has_group(rendered_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup sensor_groups
        = classify_vendor_raw_processing_field("mk_motorola0", "Sensor",
                                               0x665EU);
    EXPECT_TRUE(has_group(sensor_groups, VendorRawProcessingGroup::Sensor));
    EXPECT_TRUE(
        has_group(sensor_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_motorola0", "SerialNumber",
                                               0x5501U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesNintendoSourceProcessingFields)
{
    const VendorRawProcessingGroup geometry_groups
        = classify_vendor_raw_processing_field("mk_nintendo_camerainfo_0",
                                               "Parallax", 0x0028U);
    EXPECT_TRUE(has_group(geometry_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(
        has_group(geometry_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup private_groups
        = classify_vendor_raw_processing_field("mk_nintendo0", "CameraInfo",
                                               0x1101U);
    EXPECT_TRUE(
        has_group(private_groups, VendorRawProcessingGroup::PrivateTable));

    const VendorRawProcessingGroup capture_groups
        = classify_vendor_raw_processing_field("mk_nintendo_camerainfo_0",
                                               "ModelID", 0x0000U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesMicrosoftSourceProcessingFields)
{
    const VendorRawProcessingGroup stitch_groups
        = classify_vendor_raw_processing_field("mk_microsoft_stitch_0",
                                               "PanoramicStitchTheta0",
                                               0x0003U);
    EXPECT_TRUE(has_group(stitch_groups, VendorRawProcessingGroup::Geometry));
    EXPECT_TRUE(
        has_group(stitch_groups, VendorRawProcessingGroup::PrivateTable));
}

TEST(VendorRawProcessing, SummarizesFamiliesFromStore)
{
    MetaStore store;
    add_exif_u32(&store, "mk_sony_sr2subifd_0", 0x7313U, 1U);
    add_exif_u32(&store, "mk_sony_sr2private_0", 0x7200U, 1U);
    add_exif_u32(&store, "mk_sony_tag9405b_0", 0x0064U, 1U);
    add_exif_u32(&store, "mk_sony0", 0x0100U, 1U);

    add_exif_u32(&store, "mk_canon_colordata10_0", 0x0055U, 1U);
    add_exif_u32(&store, "mk_canon_colordata5_0", 0x0001U, 1U);
    add_exif_u32(&store, "mk_canon_colorcalib_0", 0x0038U, 1U);
    add_exif_u32(&store, "mk_canon_aspectinfo_0", 0x0004U, 1U);
    add_exif_u32(&store, "mk_canon0", 0x0006U, 1U);

    add_exif_u32(&store, "mk_nikon_colorbalancec_0", 0x0114U, 1U);
    add_exif_u32(&store, "mk_nikon_distortioninfo_0", 0x0014U, 1U);
    add_exif_u32(&store, "mk_nikon_nefinfo_0", 0x0005U, 1U);
    add_exif_u32(&store, "mk_nikon0", 0x0003U, 1U);

    add_exif_u32(&store, "mk_fuji0", 0x100AU, 1U);
    add_exif_u32(&store, "mk_fuji0", 0x1045U, 1U);
    add_exif_u32(&store, "mk_fuji_rafdata_0", 0x0000U, 1U);
    add_exif_u32(&store, "mk_fuji0", 0x1000U, 1U);

    add_exif_u32(&store, "mk_pentax0", 0x0039U, 1U);
    add_exif_u32(&store, "mk_pentax0", 0x0203U, 1U);
    add_exif_u32(&store, "mk_pentax0", 0x020DU, 1U);
    add_exif_u32(&store, "mk_pentax_lenscorr_0", 0x0000U, 1U);
    add_exif_u32(&store, "mk_pentax0", 0x0005U, 1U);

    add_exif_u32(&store, "mk_panasonic0", 0x004BU, 1U);
    add_exif_u32(&store, "mk_panasonic0", 0x008AU, 1U);
    add_exif_u32(&store, "mk_panasonic_subdir_0", 0x3036U, 1U);
    add_exif_u32(&store, "mk_panasonic_subdir_0", 0x312BU, 1U);
    add_exif_u32(&store, "mk_panasonic0", 0x0021U, 1U);
    add_exif_u32(&store, "mk_panasonic0", 0x0051U, 1U);

    add_exif_u32(&store, "mk_olympus_imageprocessing_0", 0x010FU, 1U);
    add_exif_u32(&store, "mk_olympus_imageprocessing_0", 0x0200U, 1U);
    add_exif_u32(&store, "mk_olympus_imageprocessing_0", 0x0614U, 1U);
    add_exif_u32(&store, "mk_olympus_imageprocessing_0", 0x1011U, 1U);
    add_exif_u32(&store, "mk_olympus_rawdevelopment2_0", 0x010AU, 1U);
    add_exif_u32(&store, "mk_olympus_equipment_0", 0x0203U, 1U);

    add_exif_u32(&store, "mk_kodak_ifd_0", 0x03EDU, 1U);
    add_exif_u32(&store, "mk_kodak_ifd_0", 0x0847U, 1U);
    add_exif_u32(&store, "mk_kodak_ifd_0", 0x03F0U, 1U);
    add_exif_u32(&store, "mk_kodak_ifd_0", 0x0C4EU, 1U);
    add_exif_u32(&store, "mk_kodak0", 0x0000U, 1U);

    add_exif_u32(&store, "mk_minolta_wbinfoa100_0", 0x0096U, 1U);
    add_exif_u32(&store, "mk_minoltaraw_prd_0", 0x000AU, 1U);
    add_exif_u32(&store, "mk_minoltaraw_prd_0", 0x0010U, 1U);
    add_exif_u32(&store, "mk_minoltaraw_rif_0", 0x0050U, 1U);
    add_exif_u32(&store, "mk_minolta0", 0x010CU, 1U);

    add_exif_u32(&store, "mk_sigma_wbsettings_0", 0x0000U, 1U);
    add_exif_u32(&store, "mk_sigma0", 0x011FU, 1U);
    add_exif_u32(&store, "mk_sigma0", 0x0139U, 1U);
    add_exif_u32(&store, "mk_sigma0", 0x0039U, 1U);
    add_exif_u32(&store, "mk_sigma0", 0x0027U, 1U);

    add_exif_u32(&store, "mk_samsung_type2_0", 0x0040U, 1U);
    add_exif_u32(&store, "mk_samsung_type2_0", 0x0050U, 1U);
    add_exif_u32(&store, "mk_samsung_type2_0", 0xA021U, 1U);
    add_exif_u32(&store, "mk_samsung_type2_0", 0xA030U, 1U);
    add_exif_u32(&store, "mk_samsung_type2_0", 0xA048U, 1U);
    add_exif_u32(&store, "mk_samsung_type2_0", 0xA050U, 1U);
    add_exif_u32(&store, "mk_samsung_type2_0", 0xA003U, 1U);

    add_exif_u32(&store, "mk_ricoh0", 0x1004U, 1U);
    add_exif_u32(&store, "mk_ricoh0", 0x1601U, 1U);
    add_exif_u32(&store, "mk_ricoh0", 0x1011U, 1U);
    add_exif_u32(&store, "mk_ricoh0", 0x100FU, 1U);
    add_exif_u32(&store, "mk_ricoh0", 0x0005U, 1U);

    add_exif_u32(&store, "mk_apple0", 0x002DU, 1U);
    add_exif_u32(&store, "mk_apple0", 0x003EU, 1U);
    add_exif_u32(&store, "mk_apple0", 0x0030U, 1U);
    add_exif_u32(&store, "mk_apple0", 0x0019U, 1U);
    add_exif_u32(&store, "mk_apple0", 0x002EU, 1U);

    add_exif_u32(&store, "mk_dji_thermalparams2_0", 0x0000U, 1U);
    add_exif_u32(&store, "mk_dji_thermalparams3_0", 0x0008U, 1U);
    add_exif_u32(&store, "mk_dji0", 0x0001U, 1U);

    add_exif_u32(&store, "mk_google_hdrplusmakernote_0", 0x0002U, 1U);
    add_exif_u32(&store, "mk_google_shotlogdata_0", 0x0003U, 1U);

    add_exif_u32(&store, "mk_flir_fff_rawdata_0", 0x0001U, 1U);
    add_exif_u32(&store, "mk_flir_fff_camerainfo_0", 0x0058U, 1U);
    add_exif_u32(&store, "mk_flir_fff_paletteinfo_0", 0x0050U, 1U);
    add_exif_u32(&store, "mk_flir_fff_pip_0", 0x0004U, 1U);
    add_exif_u32(&store, "mk_flir_fff_camerainfo_0", 0x00D4U, 1U);

    add_exif_u32(&store, "mk_casio_type2_0", 0x2011U, 1U);
    add_exif_u32(&store, "mk_casio_type2_0", 0x0004U, 1U);
    add_exif_u32(&store, "mk_casio_faceinfo2_0", 0x0018U, 1U);
    add_exif_u32(&store, "mk_casio_type2_0", 0x301BU, 1U);
    add_exif_u32(&store, "mk_casio_type2_0", 0x3002U, 1U);

    add_exif_u32(&store, "mk_sanyo_mov_0", 0x0044U, 1U);
    add_exif_u32(&store, "mk_sanyo0", 0x0F00U, 1U);
    add_exif_u32(&store, "mk_sanyo_faceinfo_0", 0x0004U, 1U);
    add_exif_u32(&store, "mk_sanyo_mov_0", 0x0000U, 1U);

    add_exif_u32(&store, "mk_kyoceraraw0", 0x003CU, 1U);
    add_exif_u32(&store, "mk_kyoceraraw0", 0x000CU, 1U);

    add_exif_u32(&store, "mk_reconyx_hyperfire2_0", 0x0052U, 1U);
    add_exif_u32(&store, "mk_reconyx_hyperfire2_0", 0x0050U, 1U);
    add_exif_u32(&store, "mk_reconyx_hyperfire2_0", 0x0034U, 1U);
    add_exif_u32(&store, "mk_reconyx_hyperfire2_0", 0x007EU, 1U);

    add_exif_u32(&store, "mk_hp0", 0x0200U, 1U);
    add_exif_u32(&store, "mk_hp_type6_0", 0x0058U, 1U);

    add_exif_u32(&store, "mk_jvc0", 0x0002U, 1U);
    add_exif_u32(&store, "mk_jvc0", 0x0003U, 1U);

    add_exif_u32(&store, "mk_ge0", 0x0104U, 1U);
    add_exif_u32(&store, "mk_ge0", 0x0207U, 1U);

    add_exif_u32(&store, "mk_motorola0", 0x6420U, 1U);
    add_exif_u32(&store, "mk_motorola0", 0x665EU, 1U);
    add_exif_u32(&store, "mk_motorola0", 0x5501U, 1U);

    add_exif_u32(&store, "mk_nintendo_camerainfo_0", 0x0028U, 1U);
    add_exif_u32(&store, "mk_nintendo0", 0x1101U, 1U);
    add_exif_u32(&store, "mk_nintendo_camerainfo_0", 0x0000U, 1U);

    add_exif_u32(&store, "mk_microsoft_stitch_0", 0x0003U, 1U);
    store.finalize();

    const VendorRawProcessingSummary sony
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Sony);
    EXPECT_EQ(sony.fields_seen, 3U);
    EXPECT_EQ(sony.white_balance_fields, 1U);
    EXPECT_EQ(sony.storage_fields, 1U);
    EXPECT_EQ(sony.lens_correction_fields, 1U);
    EXPECT_EQ(sony.private_table_fields, 1U);

    const VendorRawProcessingSummary canon
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Canon);
    EXPECT_EQ(canon.fields_seen, 4U);
    EXPECT_EQ(canon.color_fields, 2U);
    EXPECT_EQ(canon.white_balance_fields, 1U);
    EXPECT_EQ(canon.geometry_fields, 1U);
    EXPECT_EQ(canon.private_table_fields, 2U);

    const VendorRawProcessingSummary nikon
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Nikon);
    EXPECT_EQ(nikon.fields_seen, 3U);
    EXPECT_EQ(nikon.color_fields, 1U);
    EXPECT_EQ(nikon.white_balance_fields, 1U);
    EXPECT_EQ(nikon.sensor_fields, 1U);
    EXPECT_EQ(nikon.lens_correction_fields, 2U);
    EXPECT_EQ(nikon.private_table_fields, 1U);

    const VendorRawProcessingSummary fujifilm
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Fujifilm);
    EXPECT_EQ(fujifilm.fields_seen, 3U);
    EXPECT_EQ(fujifilm.color_fields, 1U);
    EXPECT_EQ(fujifilm.white_balance_fields, 1U);
    EXPECT_EQ(fujifilm.geometry_fields, 1U);
    EXPECT_EQ(fujifilm.lens_correction_fields, 1U);
    EXPECT_EQ(fujifilm.raw_data_fields, 1U);
    EXPECT_EQ(fujifilm.private_table_fields, 1U);

    const VendorRawProcessingSummary pentax
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Pentax);
    EXPECT_EQ(pentax.fields_seen, 4U);
    EXPECT_EQ(pentax.color_fields, 2U);
    EXPECT_EQ(pentax.white_balance_fields, 1U);
    EXPECT_EQ(pentax.geometry_fields, 1U);
    EXPECT_EQ(pentax.lens_correction_fields, 1U);
    EXPECT_EQ(pentax.raw_data_fields, 1U);

    const VendorRawProcessingSummary panasonic
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Panasonic);
    EXPECT_EQ(panasonic.fields_seen, 5U);
    EXPECT_EQ(panasonic.color_fields, 1U);
    EXPECT_EQ(panasonic.white_balance_fields, 1U);
    EXPECT_EQ(panasonic.geometry_fields, 2U);
    EXPECT_EQ(panasonic.lens_correction_fields, 1U);
    EXPECT_EQ(panasonic.sensor_fields, 1U);
    EXPECT_EQ(panasonic.private_table_fields, 1U);

    const VendorRawProcessingSummary olympus
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Olympus);
    EXPECT_EQ(olympus.fields_seen, 5U);
    EXPECT_EQ(olympus.color_fields, 2U);
    EXPECT_EQ(olympus.white_balance_fields, 1U);
    EXPECT_EQ(olympus.geometry_fields, 1U);
    EXPECT_EQ(olympus.lens_correction_fields, 1U);
    EXPECT_EQ(olympus.raw_data_fields, 1U);
    EXPECT_EQ(olympus.sensor_fields, 1U);
    EXPECT_EQ(olympus.private_table_fields, 5U);

    const VendorRawProcessingSummary kodak
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Kodak);
    EXPECT_EQ(kodak.fields_seen, 4U);
    EXPECT_EQ(kodak.color_fields, 1U);
    EXPECT_EQ(kodak.white_balance_fields, 1U);
    EXPECT_EQ(kodak.geometry_fields, 1U);
    EXPECT_EQ(kodak.raw_data_fields, 1U);
    EXPECT_EQ(kodak.sensor_fields, 2U);

    const VendorRawProcessingSummary minolta
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Minolta);
    EXPECT_EQ(minolta.fields_seen, 4U);
    EXPECT_EQ(minolta.color_fields, 1U);
    EXPECT_EQ(minolta.white_balance_fields, 1U);
    EXPECT_EQ(minolta.geometry_fields, 1U);
    EXPECT_EQ(minolta.storage_fields, 1U);
    EXPECT_EQ(minolta.raw_data_fields, 3U);
    EXPECT_EQ(minolta.sensor_fields, 2U);
    EXPECT_EQ(minolta.private_table_fields, 3U);

    const VendorRawProcessingSummary sigma
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Sigma);
    EXPECT_EQ(sigma.fields_seen, 4U);
    EXPECT_EQ(sigma.color_fields, 2U);
    EXPECT_EQ(sigma.white_balance_fields, 1U);
    EXPECT_EQ(sigma.lens_correction_fields, 1U);
    EXPECT_EQ(sigma.sensor_fields, 1U);

    const VendorRawProcessingSummary samsung
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Samsung);
    EXPECT_EQ(samsung.fields_seen, 6U);
    EXPECT_EQ(samsung.color_fields, 2U);
    EXPECT_EQ(samsung.white_balance_fields, 1U);
    EXPECT_EQ(samsung.storage_fields, 1U);
    EXPECT_EQ(samsung.lens_correction_fields, 1U);
    EXPECT_EQ(samsung.raw_data_fields, 3U);
    EXPECT_EQ(samsung.sensor_fields, 1U);

    const VendorRawProcessingSummary ricoh
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Ricoh);
    EXPECT_EQ(ricoh.fields_seen, 4U);
    EXPECT_EQ(ricoh.color_fields, 1U);
    EXPECT_EQ(ricoh.white_balance_fields, 1U);
    EXPECT_EQ(ricoh.geometry_fields, 1U);
    EXPECT_EQ(ricoh.lens_correction_fields, 1U);
    EXPECT_EQ(ricoh.sensor_fields, 2U);

    const VendorRawProcessingSummary apple
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Apple);
    EXPECT_EQ(apple.fields_seen, 4U);
    EXPECT_EQ(apple.color_fields, 3U);
    EXPECT_EQ(apple.white_balance_fields, 1U);
    EXPECT_EQ(apple.sensor_fields, 1U);
    EXPECT_EQ(apple.private_table_fields, 4U);

    const VendorRawProcessingSummary dji
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Dji);
    EXPECT_EQ(dji.fields_seen, 2U);
    EXPECT_EQ(dji.sensor_fields, 2U);
    EXPECT_EQ(dji.private_table_fields, 2U);

    const VendorRawProcessingSummary google
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Google);
    EXPECT_EQ(google.fields_seen, 2U);
    EXPECT_EQ(google.sensor_fields, 1U);
    EXPECT_EQ(google.private_table_fields, 2U);

    const VendorRawProcessingSummary flir
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Flir);
    EXPECT_EQ(flir.fields_seen, 4U);
    EXPECT_EQ(flir.color_fields, 1U);
    EXPECT_EQ(flir.geometry_fields, 2U);
    EXPECT_EQ(flir.raw_data_fields, 1U);
    EXPECT_EQ(flir.sensor_fields, 2U);
    EXPECT_EQ(flir.private_table_fields, 4U);
    EXPECT_EQ(flir.thermal_fields, 2U);

    const VendorRawProcessingSummary casio
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Casio);
    EXPECT_EQ(casio.fields_seen, 4U);
    EXPECT_EQ(casio.color_fields, 2U);
    EXPECT_EQ(casio.white_balance_fields, 1U);
    EXPECT_EQ(casio.geometry_fields, 2U);
    EXPECT_EQ(casio.storage_fields, 1U);
    EXPECT_EQ(casio.private_table_fields, 3U);

    const VendorRawProcessingSummary sanyo
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Sanyo);
    EXPECT_EQ(sanyo.fields_seen, 3U);
    EXPECT_EQ(sanyo.color_fields, 1U);
    EXPECT_EQ(sanyo.white_balance_fields, 1U);
    EXPECT_EQ(sanyo.geometry_fields, 1U);
    EXPECT_EQ(sanyo.storage_fields, 1U);
    EXPECT_EQ(sanyo.private_table_fields, 2U);

    const VendorRawProcessingSummary kyocera = vendor_raw_processing_from_store(
        store, VendorRawProcessingFamily::KyoceraRaw);
    EXPECT_EQ(kyocera.fields_seen, 1U);
    EXPECT_EQ(kyocera.color_fields, 1U);
    EXPECT_EQ(kyocera.white_balance_fields, 1U);
    EXPECT_EQ(kyocera.private_table_fields, 1U);

    const VendorRawProcessingSummary reconyx
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Reconyx);
    EXPECT_EQ(reconyx.fields_seen, 3U);
    EXPECT_EQ(reconyx.color_fields, 1U);
    EXPECT_EQ(reconyx.sensor_fields, 1U);
    EXPECT_EQ(reconyx.private_table_fields, 2U);

    const VendorRawProcessingSummary hp
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Hp);
    EXPECT_EQ(hp.fields_seen, 1U);
    EXPECT_EQ(hp.private_table_fields, 1U);

    const VendorRawProcessingSummary jvc
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Jvc);
    EXPECT_EQ(jvc.fields_seen, 1U);
    EXPECT_EQ(jvc.private_table_fields, 1U);

    const VendorRawProcessingSummary ge
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Ge);
    EXPECT_EQ(ge.fields_seen, 1U);
    EXPECT_EQ(ge.private_table_fields, 1U);

    const VendorRawProcessingSummary motorola
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Motorola);
    EXPECT_EQ(motorola.fields_seen, 2U);
    EXPECT_EQ(motorola.color_fields, 1U);
    EXPECT_EQ(motorola.sensor_fields, 1U);
    EXPECT_EQ(motorola.private_table_fields, 2U);

    const VendorRawProcessingSummary nintendo
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Nintendo);
    EXPECT_EQ(nintendo.fields_seen, 2U);
    EXPECT_EQ(nintendo.geometry_fields, 1U);
    EXPECT_EQ(nintendo.private_table_fields, 2U);

    const VendorRawProcessingSummary microsoft
        = vendor_raw_processing_from_store(store,
                                           VendorRawProcessingFamily::Microsoft);
    EXPECT_EQ(microsoft.fields_seen, 1U);
    EXPECT_EQ(microsoft.geometry_fields, 1U);
    EXPECT_EQ(microsoft.private_table_fields, 1U);
}

TEST(VendorRawProcessing, IgnoresStandardExifIfds)
{
    const VendorRawProcessingGroup groups
        = classify_vendor_raw_processing_field("ifd0", "ColorMatrix1", 0xC621U);
    EXPECT_EQ(groups, VendorRawProcessingGroup::None);
}

}  // namespace openmeta
