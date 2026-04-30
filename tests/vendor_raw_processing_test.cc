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
                                               "WhiteBalanceFineTune",
                                               0x100AU);
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
    EXPECT_TRUE(
        has_group(raw_size_groups, VendorRawProcessingGroup::Geometry));
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
                                               "DistortionCorrection",
                                               0x0000U);
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
                                               "RawDevNoiseReduction",
                                               0x010AU);
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
        = classify_vendor_raw_processing_field("mk_kodak_ifd_0",
                                               "RawHistogram", 0x0C4EU);
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
        = classify_vendor_raw_processing_field("mk_sigma0",
                                               "CameraCalibration", 0x011FU);
    EXPECT_TRUE(has_group(calibration_groups, VendorRawProcessingGroup::Color));

    const VendorRawProcessingGroup vignette_groups
        = classify_vendor_raw_processing_field("mk_sigma0", "Vignette",
                                               0x0139U);
    EXPECT_TRUE(
        has_group(vignette_groups, VendorRawProcessingGroup::LensCorrection));

    const VendorRawProcessingGroup sensor_groups
        = classify_vendor_raw_processing_field("mk_sigma0",
                                               "SensorTemperature", 0x0039U);
    EXPECT_TRUE(has_group(sensor_groups, VendorRawProcessingGroup::Sensor));

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
        = classify_vendor_raw_processing_field("mk_samsung_type2_0",
                                               "LensType", 0xA003U);
    EXPECT_EQ(capture_groups, VendorRawProcessingGroup::None);
}

TEST(VendorRawProcessing, ClassifiesRicohRawProcessingFields)
{
    const VendorRawProcessingGroup wb_groups
        = classify_vendor_raw_processing_field("mk_ricoh0",
                                               "WhiteBalanceFineTune",
                                               0x1004U);
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
        = vendor_raw_processing_from_store(
            store, VendorRawProcessingFamily::Fujifilm);
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
        = vendor_raw_processing_from_store(
            store, VendorRawProcessingFamily::Panasonic);
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
}

TEST(VendorRawProcessing, IgnoresStandardExifIfds)
{
    const VendorRawProcessingGroup groups
        = classify_vendor_raw_processing_field("ifd0", "ColorMatrix1", 0xC621U);
    EXPECT_EQ(groups, VendorRawProcessingGroup::None);
}

}  // namespace openmeta
