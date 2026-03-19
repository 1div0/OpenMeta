#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_store.h"

#include <gtest/gtest.h>

#include <string_view>

TEST(ExifTagNames, MapsCommonTags)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("ifd0", 0x010F), std::string_view("Make"));
    EXPECT_EQ(exif_tag_name("ifd1", 0x0201),
              std::string_view("JPEGInterchangeFormat"));
    EXPECT_EQ(exif_tag_name("subifd0", 0x0100), std::string_view("ImageWidth"));

    EXPECT_EQ(exif_tag_name("exififd", 0x9003),
              std::string_view("DateTimeOriginal"));
    EXPECT_EQ(exif_tag_name("exififd", 0x927C), std::string_view("MakerNote"));

    EXPECT_EQ(exif_tag_name("gpsifd", 0x0001),
              std::string_view("GPSLatitudeRef"));
    EXPECT_EQ(exif_tag_name("gpsifd", 0x0002), std::string_view("GPSLatitude"));
    EXPECT_EQ(exif_tag_name("gpsifd", 0x0011),
              std::string_view("GPSImgDirection"));

    EXPECT_EQ(exif_tag_name("ifd0", 0xC4A5), std::string_view("PrintIM"));
    EXPECT_EQ(exif_tag_name("mpf0", 0xB001),
              std::string_view("NumberOfImages"));

    EXPECT_EQ(exif_tag_name("mk_nikon0", 0x0002), std::string_view("ISO"));
    EXPECT_EQ(exif_tag_name("mk_canon0", 0x0003),
              std::string_view("CanonFlashInfo"));
    EXPECT_EQ(exif_tag_name("mk_fuji0", 0x1000), std::string_view("Quality"));
    EXPECT_EQ(exif_tag_name("mk_kodak0", 0x0028),
              std::string_view("Distance1"));
    EXPECT_EQ(exif_tag_name("mk_fuji0", 0x1200),
              std::string_view("FujiFilm_0x1200"));
}


TEST(ExifTagNames, MapsNativeCrwCiffTags)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("ciff_root", 0x2005), std::string_view("RawData"));
    EXPECT_EQ(exif_tag_name("ciff_2804_0", 0x0805),
              std::string_view("CanonFileDescription"));
    EXPECT_EQ(exif_tag_name("ciff_2807_1", 0x080A),
              std::string_view("MakeModel"));
    EXPECT_EQ(exif_tag_name("ciff_2807_1", 0x0810),
              std::string_view("OwnerName"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2", 0x0816),
              std::string_view("OriginalFileName"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2", 0x1803),
              std::string_view("ImageFormat"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2", 0x1806),
              std::string_view("SelfTimerTime"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2", 0x1810),
              std::string_view("ImageInfo"));
    EXPECT_EQ(exif_tag_name("ciff_3003_3", 0x1814),
              std::string_view("MeasuredEV"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3", 0x101C),
              std::string_view("BaseISO"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3", 0x080C),
              std::string_view("ComponentVersion"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3", 0x1834),
              std::string_view("CanonModelID"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3", 0x1835),
              std::string_view("DecoderTable"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3", 0x183B),
              std::string_view("SerialNumberFormat"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4", 0x1028),
              std::string_view("CanonFlashInfo"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4", 0x1029),
              std::string_view("FocalLength"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4", 0x102A),
              std::string_view("CanonShotInfo"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4", 0x1030),
              std::string_view("WhiteSample"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4", 0x10B5),
              std::string_view("RawJpgInfo"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2_timestamp", 0x0001),
              std::string_view("TimeZoneCode"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2_imageformat", 0x0001),
              std::string_view("TargetCompressionRatio"));
    EXPECT_EQ(exif_tag_name("ciff_2807_1_makemodel", 0x0000),
              std::string_view("Make"));
    EXPECT_EQ(exif_tag_name("ciff_2807_1_makemodel", 0x0006),
              std::string_view("Model"));
    EXPECT_EQ(exif_tag_name("ciff_300A_2_imageinfo", 0x0002),
              std::string_view("PixelAspectRatio"));
    EXPECT_EQ(exif_tag_name("ciff_3002_1", 0x1813),
              std::string_view("FlashInfo"));
    EXPECT_EQ(exif_tag_name("ciff_3002_1_exposureinfo", 0x0001),
              std::string_view("ShutterSpeedValue"));
    EXPECT_TRUE(exif_tag_name("ciff_3004_3", 0x1812).empty());
    EXPECT_TRUE(exif_tag_name("ciff_300B_4", 0x1819).empty());
    EXPECT_EQ(exif_tag_name("ciff_3002_1_flashinfo", 0x0000),
              std::string_view("FlashGuideNumber"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4_focallength", 0x0002),
              std::string_view("FocalPlaneXSize"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4_shotinfo", 0x0005),
              std::string_view("TargetExposureTime"));
    EXPECT_EQ(exif_tag_name("ciff_3004_3_decodertable", 0x0002),
              std::string_view("CompressedDataOffset"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4_rawjpginfo", 0x0003),
              std::string_view("RawJpgWidth"));
    EXPECT_EQ(exif_tag_name("ciff_300B_4_whitesample", 0x0005),
              std::string_view("WhiteSampleBits"));
}


TEST(ExifTagNames, MapsFlirFffSubtables)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_flir_fff_header_0", 0x0004),
              std::string_view("CreatorSoftware"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_camerainfo_0", 0x0020),
              std::string_view("Emissivity"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_camerainfo_0", 0x00D4),
              std::string_view("CameraModel"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_paletteinfo_0", 0x0050),
              std::string_view("PaletteName"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_rawdata_0", 0x0010),
              std::string_view("RawThermalImageType"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_embeddedimage_0", 0x0010),
              std::string_view("EmbeddedImageType"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_pip_0", 0x0004),
              std::string_view("PiPX1"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_gpsinfo_0", 0x0010),
              std::string_view("GPSLatitude"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_meterlink_0", 0x0060),
              std::string_view("Reading1Value"));
}


TEST(ExifTagNames, MapsHpMainAndTypedSubtables)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_hp0", 0x0200), std::string_view("HP_0x0200"));
    EXPECT_EQ(exif_tag_name("mk_hp0", 0x0300), std::string_view("HP_0x0300"));
    EXPECT_EQ(exif_tag_name("mk_hp_type4_0", 0x000c),
              std::string_view("MaxAperture"));
    EXPECT_EQ(exif_tag_name("mk_hp_type6_0", 0x0058),
              std::string_view("SerialNumber"));
}


TEST(ExifTagNames, MapsKodakTypedTablesAndPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_kodak_type2_0", 0x0028),
              std::string_view("KodakModel"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type3_0", 0x0038),
              std::string_view("ExposureTime"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type5_0", 0x0014),
              std::string_view("ExposureTime"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type7_0", 0x0000),
              std::string_view("SerialNumber"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type10_0", 0x0002),
              std::string_view("PreviewImageSize"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type10_0", 0x0015),
              std::string_view("Kodak_Type10_0x0015"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type8_0", 0x0200),
              std::string_view("Kodak_Type8_0x0200"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type11_0", 0x0207),
              std::string_view("KodakModel"));
    EXPECT_EQ(exif_tag_name("mk_kodak_type11_0", 0x0200),
              std::string_view("Kodak_Type11_0x0200"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd1_0", 0x0001),
              std::string_view("Kodak_SubIFD1_0x0001"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd1_0", 0x1007),
              std::string_view("Kodak_SubIFD1_0x1007"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd0_0", 0xFA04),
              std::string_view("Kodak_SubIFD0_0xfa04"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd2_0", 0x6002),
              std::string_view("SceneModeUsed"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd2_0", 0x0001),
              std::string_view("Kodak_SubIFD2_0x0001"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd5_0", 0x0033),
              std::string_view("Kodak_SubIFD5_0x0033"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd5_0", 0x0047),
              std::string_view("Kodak_SubIFD5_0x0047"));
    EXPECT_EQ(exif_tag_name("mk_kodak_subifd255_0", 0xFA75),
              std::string_view("Kodak_SubIFD0_0xfa75"));
    EXPECT_EQ(exif_tag_name("mk_kodak_camerainfo_0", 0xFA01),
              std::string_view("Kodak_CameraInfo_0xfa01"));
    EXPECT_EQ(exif_tag_name("mk_kodak_camerainfo_0", 0xFF00),
              std::string_view("Kodak_CameraInfo_0xff00"));
}

TEST(ExifTagNames, MapsAppleMainTagsAndPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_apple0", 0x0001),
              std::string_view("MakerNoteVersion"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x000D),
              std::string_view("Apple_0x000d"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x001F),
              std::string_view("Apple_0x001f"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x0023),
              std::string_view("Apple_0x0023"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x002D),
              std::string_view("Apple_0x002d"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x002E),
              std::string_view("Apple_0x002e"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x0045),
              std::string_view("FrontFacingCamera"));
    EXPECT_EQ(exif_tag_name("mk_apple0", 0x0050),
              std::string_view("Apple_0x0050"));
}

TEST(ExifTagNames, MapsFujifilmMainPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_fuji0", 0x1026),
              std::string_view("FujiFilm_0x1026"));
    EXPECT_EQ(exif_tag_name("mk_fuji0", 0x1303),
              std::string_view("FujiFilm_0x1303"));
    EXPECT_EQ(exif_tag_name("mk_fuji0", 0x1430),
              std::string_view("FujiFilm_0x1430"));
}

TEST(ExifTagNames, MapsDjiMainAndThermalTables)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_dji0", 0x0001), std::string_view("Make"));
    EXPECT_EQ(exif_tag_name("mk_dji0", 0x0002), std::string_view("DJI_0x0002"));
    EXPECT_EQ(exif_tag_name("mk_dji0", 0x0010), std::string_view("DJI_0x0010"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams_0", 0x0044),
              std::string_view("ObjectDistance"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams2_0", 0x0000),
              std::string_view("AmbientTemperature"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams2_0", 0x0004),
              std::string_view("ObjectDistance"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams2_0", 0x000C),
              std::string_view("RelativeHumidity"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams2_0", 0x0010),
              std::string_view("ReflectedTemperature"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams2_0", 0x0065),
              std::string_view("IDString"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams3_0", 0x0004),
              std::string_view("RelativeHumidity"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams3_0", 0x0006),
              std::string_view("ObjectDistance"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams3_0", 0x0008),
              std::string_view("Emissivity"));
    EXPECT_EQ(exif_tag_name("mk_dji_thermalparams3_0", 0x000A),
              std::string_view("ReflectedTemperature"));
}


TEST(ExifTagNames, MapsMotorolaMainAndPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x5500),
              std::string_view("BuildNumber"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x5501),
              std::string_view("SerialNumber"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x6420),
              std::string_view("CustomRendered"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x64D0),
              std::string_view("DriveMode"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x665E),
              std::string_view("Sensor"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x6705),
              std::string_view("ManufactureDate"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x5540),
              std::string_view("Motorola_0x5540"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x6400),
              std::string_view("Motorola_0x6400"));
    EXPECT_EQ(exif_tag_name("mk_motorola0", 0x6703),
              std::string_view("Motorola_0x6703"));
}

TEST(ExifTagNames, MapsRicohMainAndSubtablePlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_ricoh0", 0x0006),
              std::string_view("Ricoh_0x0006"));
    EXPECT_EQ(exif_tag_name("mk_ricoh0", 0x1002),
              std::string_view("DriveMode"));
    EXPECT_EQ(exif_tag_name("mk_ricoh_subdir_0", 0x0007),
              std::string_view("Ricoh_Subdir_0x0007"));
    EXPECT_EQ(exif_tag_name("mk_ricoh_imageinfo_0", 0x0003),
              std::string_view("Ricoh_ImageInfo_0x0003"));
    EXPECT_EQ(exif_tag_name("mk_ricoh_thetasubdir_0", 0x0003),
              std::string_view("Accelerometer"));
    EXPECT_EQ(exif_tag_name("mk_ricoh_thetasubdir_0", 0x1001),
              std::string_view("Ricoh_ThetaSubdir_0x1001"));
    EXPECT_EQ(exif_tag_name("mk_ricoh_type2_0", 0x0104),
              std::string_view("Ricoh_Type2_0x0104"));
}

TEST(ExifTagNames, MapsPentaxMainAndSubtablePlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_pentax0", 0x003E),
              std::string_view("PreviewImageBorders"));
    EXPECT_EQ(exif_tag_name("mk_pentax0", 0x005E),
              std::string_view("Pentax_0x005e"));
    EXPECT_EQ(exif_tag_name("mk_pentax0", 0x0227),
              std::string_view("Pentax_0x0227"));
    EXPECT_EQ(exif_tag_name("mk_pentax_type2_0", 0x0005),
              std::string_view("Pentax_Type2_0x0005"));
    EXPECT_EQ(exif_tag_name("mk_pentax_faceinfo_0", 0x0001),
              std::string_view("Pentax_0x0001"));
    EXPECT_EQ(exif_tag_name("mk_pentax_facepos_0", 0x0011),
              std::string_view("Pentax_0x0011"));
    EXPECT_EQ(exif_tag_name("mk_pentax_colorinfo_0", 0x000A),
              std::string_view("Pentax_0x000a"));
}

TEST(ExifTagNames, MapsCasioType2Placeholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_casio0", 0x0E00), std::string_view("PrintIM"));
    EXPECT_EQ(exif_tag_name("mk_casio_type2_0", 0x2002),
              std::string_view("Casio_Type2_0x2002"));
    EXPECT_EQ(exif_tag_name("mk_casio_type2_0", 0x2003),
              std::string_view("Casio_Type2_0x2003"));
    EXPECT_EQ(exif_tag_name("mk_casio_type2_0", 0x3005),
              std::string_view("Casio_Type2_0x3005"));
}

TEST(ExifTagNames, MapsSamsungIfdAndType2Placeholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_samsung_ifd_0", 0x0002),
              std::string_view("Samsung_IFD_0x0002"));
    EXPECT_EQ(exif_tag_name("mk_samsung_ifd_0", 0x0006),
              std::string_view("Samsung_IFD_0x0006"));
    EXPECT_EQ(exif_tag_name("mk_samsung_type2_0", 0x0060),
              std::string_view("Samsung_Type2_0x0060"));
    EXPECT_EQ(exif_tag_name("mk_samsung_type2_0", 0x00E1),
              std::string_view("Samsung_Type2_0x00e1"));
    EXPECT_EQ(exif_tag_name("mk_samsung_type2_0", 0xA002),
              std::string_view("SerialNumber"));
}

TEST(ExifTagNames, MapsMinoltaMainPlaceholdersAndImageSizeAlias)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_minolta0", 0x0103),
              std::string_view("MinoltaImageSize"));
    EXPECT_EQ(exif_tag_name("mk_minolta0", 0x0106),
              std::string_view("Minolta_0x0106"));
    EXPECT_EQ(exif_tag_name("mk_minolta0", 0x010D),
              std::string_view("Minolta_0x010d"));
    EXPECT_EQ(exif_tag_name("mk_minolta0", 0x0200),
              std::string_view("Minolta_0x0200"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectCasioLegacyType2Compat)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry quality;
    quality.key = make_exif_tag_key(store.arena(), "mk_casio_type2_0", 0x0002);
    quality.origin.block = block;
    quality.flags |= EntryFlags::ContextualName;
    quality.origin.name_context_kind = EntryNameContextKind::CasioType2Legacy;
    quality.origin.name_context_variant = 1U;
    const EntryId quality_id            = store.add_entry(quality);
    ASSERT_NE(quality_id, openmeta::kInvalidEntryId);

    Entry placeholder;
    placeholder.key = make_exif_tag_key(store.arena(), "mk_casio_type2_0",
                                        0x0008);
    placeholder.origin.block = block;
    placeholder.flags |= EntryFlags::ContextualName;
    placeholder.origin.name_context_kind
        = EntryNameContextKind::CasioType2Legacy;
    placeholder.origin.name_context_variant = 1U;
    const EntryId placeholder_id            = store.add_entry(placeholder);
    ASSERT_NE(placeholder_id, openmeta::kInvalidEntryId);

    Entry printim;
    printim.key = make_exif_tag_key(store.arena(), "mk_casio_type2_0", 0x0E00);
    printim.origin.block = block;
    printim.flags |= EntryFlags::ContextualName;
    printim.origin.name_context_kind = EntryNameContextKind::CasioType2Legacy;
    printim.origin.name_context_variant = 1U;
    const EntryId printim_id            = store.add_entry(printim);
    ASSERT_NE(printim_id, openmeta::kInvalidEntryId);

    const Entry& quality_entry = store.entry(quality_id);
    EXPECT_EQ(exif_entry_name(store, quality_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("PreviewImageSize"));
    EXPECT_EQ(exif_entry_name(store, quality_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Quality"));

    const Entry& placeholder_entry = store.entry(placeholder_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("QualityMode"));
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Casio_0x0008"));

    const Entry& printim_entry = store.entry(printim_id);
    EXPECT_EQ(exif_entry_name(store, printim_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Casio_Type2_0x0e00"));
    EXPECT_EQ(exif_entry_name(store, printim_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("PrintIM"));
}

TEST(ExifTagNames, MapsNikonSettingsMainAndPlaceholders)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_nikonsettings_main_0", 0x0001),
              std::string_view("ISOAutoHiLimit"));
    EXPECT_EQ(exif_tag_name("mk_nikonsettings_main_0", 0x0046),
              std::string_view("OpticalVR"));
    EXPECT_EQ(exif_tag_name("mk_nikonsettings_main_0", 0x0016),
              std::string_view("NikonSettings_0x0016"));
    EXPECT_EQ(exif_tag_name("mk_nikonsettings_main_0", 0x0107),
              std::string_view("NikonSettings_0x0107"));
    EXPECT_EQ(exif_tag_name("mk_nikonsettings_main_0", 0x00FA),
              std::string_view("NikonSettings_0x00fa"));
}

TEST(ExifTagNames, MapsPanasonicMainPlaceholdersAndLegacyFallbacks)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_panasonic0", 0x0022),
              std::string_view("Panasonic_0x0022"));
    EXPECT_EQ(exif_tag_name("mk_panasonic0", 0x0037),
              std::string_view("Panasonic_0x0037"));
    EXPECT_EQ(exif_tag_name("mk_panasonic0", 0x0058),
              std::string_view("ThumbnailWidth"));
    EXPECT_EQ(exif_tag_name("mk_panasonic0", 0x00DE),
              std::string_view("AFAreaSize"));
}


TEST(ExifTagNames, FlirMainUnknownTagsUseStablePlaceholderNames)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_flir0", 0x0007),
              std::string_view("FLIR_0x0007"));
    EXPECT_EQ(exif_tag_name("mk_flir0", 0x000A),
              std::string_view("FLIR_0x000a"));
    EXPECT_EQ(exif_tag_name("mk_flir0", 0x0112),
              std::string_view("FLIR_0x0112"));
    EXPECT_EQ(exif_tag_name("mk_flir_fff_gpsinfo_0", 0x0008),
              std::string_view("GPSLatitudeRef"));
}


TEST(ExifTagNames, UnknownReturnsEmpty)
{
    using openmeta::exif_tag_name;

    EXPECT_TRUE(exif_tag_name("gpsifd", 0xFFFF).empty());
    EXPECT_TRUE(exif_tag_name("unknown_ifd", 0x010F).empty());
}


TEST(ExifTagNames, MakerNoteSubtableFallsBackToVendorMain)
{
    using openmeta::exif_tag_name;

    // This tag is known in Olympus main table but not all Olympus subtables.
    EXPECT_EQ(exif_tag_name("mk_olympus_equipment0", 0x0040),
              std::string_view("CompressedImageSize"));
}


TEST(ExifTagNames, OlympusFeTagsFallBackToKnownOlympusSubtables)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x0101),
              std::string_view("Olympus_FETags_0x0101"));
    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x0201),
              std::string_view("Olympus_FETags_0x0201"));
    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x020A),
              std::string_view("Olympus_FETags_0x020A"));
    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x0306),
              std::string_view("Olympus_FETags_0x0306"));
    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x030A),
              std::string_view("Olympus_FETags_0x030A"));
    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x0311),
              std::string_view("CoringValues"));
    EXPECT_EQ(exif_tag_name("mk_olympus_fetags_0", 0x1204),
              std::string_view("ExternalFlashBounce"));
}


TEST(ExifTagNames, OlympusMissingSubtableTagsUseStablePlaceholderNames)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_olympus_camerasettings_0", 0x0402),
              std::string_view("Olympus_CameraSettings_0x0402"));
    EXPECT_EQ(exif_tag_name("mk_olympus_focusinfo_0", 0x020B),
              std::string_view("Olympus_FocusInfo_0x020B"));
    EXPECT_EQ(exif_tag_name("mk_olympus_focusinfo_0", 0x0213),
              std::string_view("Olympus_FocusInfo_0x0213"));
    EXPECT_EQ(exif_tag_name("mk_olympus_focusinfo_0", 0x2100),
              std::string_view("Olympus_FocusInfo_0x2100"));
    EXPECT_EQ(exif_tag_name("mk_olympus_unknowninfo_0", 0x0401),
              std::string_view("Olympus_UnknownInfo_0x0401"));
    EXPECT_EQ(exif_tag_name("mk_olympus_camerasettings_0", 0x030A),
              std::string_view("Olympus_CameraSettings_0x030A"));
    EXPECT_EQ(exif_tag_name("mk_olympus_camerasettings_0", 0x030B),
              std::string_view("Olympus_CameraSettings_0x030B"));
    EXPECT_EQ(exif_tag_name("mk_olympus_camerasettings_0", 0x0821),
              std::string_view("Olympus_CameraSettings_0x0821"));
    EXPECT_EQ(exif_tag_name("mk_olympus_imageprocessing_0", 0x1000),
              std::string_view("Olympus_ImageProcessing_0x1000"));
    EXPECT_EQ(exif_tag_name("mk_olympus_imageprocessing_0", 0x2110),
              std::string_view("Olympus_ImageProcessing_0x2110"));
    EXPECT_EQ(exif_tag_name("mk_olympus_imageprocessing_0", 0x1115),
              std::string_view("Olympus_ImageProcessing_0x1115"));
    EXPECT_EQ(exif_tag_name("mk_olympus_rawdevelopment2_0", 0x0114),
              std::string_view("Olympus_RawDevelopment2_0x0114"));
    EXPECT_EQ(exif_tag_name("mk_olympus0", 0x0225),
              std::string_view("Olympus_0x0225"));
    EXPECT_EQ(exif_tag_name("mk_olympus0", 0x0400),
              std::string_view("Olympus_0x0400"));
    EXPECT_EQ(exif_tag_name("mk_olympus_unknowninfo_0", 0x2100),
              std::string_view("Olympus_UnknownInfo_0x2100"));
}


TEST(ExifTagNames, CanonMainUnknownTagsUseStablePlaceholderNames)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_canon0", 0x001F),
              std::string_view("Canon_0x001f"));
    EXPECT_EQ(exif_tag_name("mk_canon0", 0x0033),
              std::string_view("Canon_0x0033"));
    EXPECT_EQ(exif_tag_name("mk_canon0", 0x4017),
              std::string_view("Canon_0x4017"));
}


TEST(ExifTagNames, CanonModelFamiliesDoNotFallBackToMainTableNames)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_canon_colordata7_0", 0x0080),
              std::string_view("WB_RGGBLevelsDaylight"));
    EXPECT_TRUE(exif_tag_name("mk_canon_colordata7_0", 0x0095).empty());
    EXPECT_EQ(exif_tag_name("mk_canon_colordata7_0", 0x00D0),
              std::string_view("WB_RGGBLevelsUnknown20"));

    EXPECT_EQ(exif_tag_name("mk_canon_colordata12_0", 0x0073),
              std::string_view("WB_RGGBLevelsMeasured"));
    EXPECT_EQ(exif_tag_name("mk_canon_colordata12_0", 0x016B),
              std::string_view("PerChannelBlackLevel"));
    EXPECT_EQ(exif_tag_name("mk_canon_colordata12_0", 0x0280),
              std::string_view("NormalWhiteLevel"));
    EXPECT_TRUE(exif_tag_name("mk_canon_colordata12_0", 0x0083).empty());
    EXPECT_TRUE(exif_tag_name("mk_canon_colordata12_0", 0x010C).empty());
    EXPECT_EQ(exif_tag_name("mk_canon_camerainfo1100d_0", 0x019B),
              std::string_view("FirmwareVersion"));
}


TEST(ExifTagNames, CanonCustomFunctions2UnknownTagsUseStablePlaceholderNames)
{
    using openmeta::exif_tag_name;

    EXPECT_EQ(exif_tag_name("mk_canoncustom_functions2_0", 0x0115),
              std::string_view("CanonCustom_Functions2_0x0115"));
    EXPECT_EQ(exif_tag_name("mk_canoncustom_functions2_0", 0x081A),
              std::string_view("CanonCustom_Functions2_0x081a"));
    EXPECT_EQ(exif_tag_name("mk_canoncustom_functions5d_0", 0x0015),
              std::string_view("CanonCustom_Functions5D_0x0015"));
    EXPECT_EQ(exif_tag_name("mk_canoncustom_functionsd30_0", 0x0000),
              std::string_view("CanonCustom_FunctionsD30_0x0000"));
}


TEST(ExifTagNames, ContextualEntryNamesKeepCanonicalKeysStable)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry focus_placeholder;
    focus_placeholder.key          = make_exif_tag_key(store.arena(),
                                                       "mk_olympus_focusinfo_0", 0x1600);
    focus_placeholder.origin.block = block;
    focus_placeholder.flags |= EntryFlags::ContextualName;
    focus_placeholder.origin.name_context_kind
        = EntryNameContextKind::OlympusFocusInfo1600;
    focus_placeholder.origin.name_context_variant = 2U;
    const EntryId placeholder_id = store.add_entry(focus_placeholder);
    ASSERT_NE(placeholder_id, openmeta::kInvalidEntryId);

    const Entry& placeholder = store.entry(placeholder_id);
    EXPECT_EQ(exif_entry_name(store, placeholder, ExifTagNamePolicy::Canonical),
              std::string_view("ImageStabilization"));
    EXPECT_EQ(exif_entry_name(store, placeholder,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Olympus_FocusInfo_0x1600"));

    Entry focus_semantic;
    focus_semantic.key          = make_exif_tag_key(store.arena(),
                                                    "mk_olympus_focusinfo_0", 0x1600);
    focus_semantic.origin.block = block;
    focus_semantic.flags |= EntryFlags::ContextualName;
    focus_semantic.origin.name_context_kind
        = EntryNameContextKind::OlympusFocusInfo1600;
    focus_semantic.origin.name_context_variant = 1U;
    const EntryId semantic_id = store.add_entry(focus_semantic);
    ASSERT_NE(semantic_id, openmeta::kInvalidEntryId);

    const Entry& semantic = store.entry(semantic_id);
    EXPECT_EQ(exif_entry_name(store, semantic, ExifTagNamePolicy::Canonical),
              std::string_view("ImageStabilization"));
    EXPECT_EQ(exif_entry_name(store, semantic,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ImageStabilization"));
}


TEST(ExifTagNames, ContextualEntryNamesSelectCanonCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry canon_main;
    canon_main.key = make_exif_tag_key(store.arena(), "mk_canon0", 0x0038);
    canon_main.origin.block = block;
    canon_main.flags |= EntryFlags::ContextualName;
    canon_main.origin.name_context_kind = EntryNameContextKind::CanonMain0038;
    canon_main.origin.name_context_variant = 1U;
    const EntryId canon_main_id            = store.add_entry(canon_main);
    ASSERT_NE(canon_main_id, openmeta::kInvalidEntryId);

    const Entry& canon_main_entry = store.entry(canon_main_id);
    EXPECT_EQ(exif_entry_name(store, canon_main_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("BatteryType"));
    EXPECT_EQ(exif_entry_name(store, canon_main_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Canon_0x0038"));

    const struct {
        std::string_view ifd;
        uint16_t tag;
        EntryNameContextKind kind;
        std::string_view canonical;
        std::string_view compat;
    } canon_compat_cases[] = {
        { "mk_canon_shotinfo_0", 0x000E,
          EntryNameContextKind::CanonShotInfo000E, "AFPointsInFocus",
          "MinFocalLength" },
        { "mk_canon_camerasettings_0", 0x0021,
          EntryNameContextKind::CanonCameraSettings0021, "AESetting",
          "WB_RGGBLevelsKelvin" },
        { "mk_canon_colordata4_0", 0x00EA,
          EntryNameContextKind::CanonColorData400EA, "",
          "WB_RGGBLevelsUnknown7" },
        { "mk_canon_colordata4_0", 0x00EE,
          EntryNameContextKind::CanonColorData400EE, "", "MaxFocalLength" },
        { "mk_canon_colordata4_0", 0x02CF,
          EntryNameContextKind::CanonColorData402CF, "NormalWhiteLevel",
          "PerChannelBlackLevel" },
        { "mk_canon_colorcalib_0", 0x0038,
          EntryNameContextKind::CanonColorCalib0038, "CameraColorCalibration15",
          "BatteryType" },
        { "mk_canon_camerainfo1d_0", 0x0048,
          EntryNameContextKind::CanonCameraInfo1D0048, "ColorTemperature",
          "Sharpness" },
        { "mk_canon_camerainfo600d_0", 0x00EA,
          EntryNameContextKind::CanonCameraInfo600D00EA, "LensType",
          "MinFocalLength" },
    };

    for (size_t i = 0;
         i < sizeof(canon_compat_cases) / sizeof(canon_compat_cases[0]); ++i) {
        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), canon_compat_cases[i].ifd,
                                      canon_compat_cases[i].tag);
        entry.origin.block = block;
        entry.flags |= EntryFlags::ContextualName;
        entry.origin.name_context_kind    = canon_compat_cases[i].kind;
        entry.origin.name_context_variant = 1U;
        const EntryId id                  = store.add_entry(entry);
        ASSERT_NE(id, openmeta::kInvalidEntryId);

        const Entry& stored = store.entry(id);
        EXPECT_EQ(exif_entry_name(store, stored, ExifTagNamePolicy::Canonical),
                  canon_compat_cases[i].canonical);
        EXPECT_EQ(exif_entry_name(store, stored,
                                  ExifTagNamePolicy::ExifToolCompat),
                  canon_compat_cases[i].compat);
    }

    Entry iso_expansion;
    iso_expansion.key          = make_exif_tag_key(store.arena(),
                                                   "mk_canoncustom_functions2_0",
                                                   0x0103);
    iso_expansion.origin.block = block;
    iso_expansion.flags |= EntryFlags::ContextualName;
    iso_expansion.origin.name_context_kind
        = EntryNameContextKind::CanonCustomFunctions20103;
    iso_expansion.origin.name_context_variant = 1U;
    const EntryId iso_expansion_id            = store.add_entry(iso_expansion);
    ASSERT_NE(iso_expansion_id, openmeta::kInvalidEntryId);

    const Entry& iso_expansion_entry = store.entry(iso_expansion_id);
    EXPECT_EQ(exif_entry_name(store, iso_expansion_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ISOSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, iso_expansion_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ISOExpansion"));

    Entry iso_speed_range;
    iso_speed_range.key          = make_exif_tag_key(store.arena(),
                                                     "mk_canoncustom_functions2_0",
                                                     0x0103);
    iso_speed_range.origin.block = block;
    iso_speed_range.flags |= EntryFlags::ContextualName;
    iso_speed_range.origin.name_context_kind
        = EntryNameContextKind::CanonCustomFunctions20103;
    iso_speed_range.origin.name_context_variant = 2U;
    const EntryId iso_speed_range_id = store.add_entry(iso_speed_range);
    ASSERT_NE(iso_speed_range_id, openmeta::kInvalidEntryId);

    const Entry& iso_speed_range_entry = store.entry(iso_speed_range_id);
    EXPECT_EQ(exif_entry_name(store, iso_speed_range_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ISOSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, iso_speed_range_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ISOSpeedRange"));

    Entry shutter_speed_range;
    shutter_speed_range.key          = make_exif_tag_key(store.arena(),
                                                         "mk_canoncustom_functions2_0",
                                                         0x010C);
    shutter_speed_range.origin.block = block;
    shutter_speed_range.flags |= EntryFlags::ContextualName;
    shutter_speed_range.origin.name_context_kind
        = EntryNameContextKind::CanonCustomFunctions2010C;
    shutter_speed_range.origin.name_context_variant = 1U;
    const EntryId shutter_speed_range_id = store.add_entry(shutter_speed_range);
    ASSERT_NE(shutter_speed_range_id, openmeta::kInvalidEntryId);

    const Entry& shutter_speed_range_entry = store.entry(
        shutter_speed_range_id);
    EXPECT_EQ(exif_entry_name(store, shutter_speed_range_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ShutterSpeedRange"));
    EXPECT_EQ(exif_entry_name(store, shutter_speed_range_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("CanonCustom_Functions2_0x010c"));

    Entry superimposed_display;
    superimposed_display.key          = make_exif_tag_key(store.arena(),
                                                          "mk_canoncustom_functions2_0",
                                                          0x0510);
    superimposed_display.origin.block = block;
    superimposed_display.flags |= EntryFlags::ContextualName;
    superimposed_display.origin.name_context_kind
        = EntryNameContextKind::CanonCustomFunctions20510;
    superimposed_display.origin.name_context_variant = 1U;
    const EntryId superimposed_display_id            = store.add_entry(
        superimposed_display);
    ASSERT_NE(superimposed_display_id, openmeta::kInvalidEntryId);

    const Entry& superimposed_display_entry = store.entry(
        superimposed_display_id);
    EXPECT_EQ(exif_entry_name(store, superimposed_display_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("VFDisplayIllumination"));
    EXPECT_EQ(exif_entry_name(store, superimposed_display_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("SuperimposedDisplay"));

    Entry shutter_button;
    shutter_button.key          = make_exif_tag_key(store.arena(),
                                                    "mk_canoncustom_functions2_0",
                                                    0x0701);
    shutter_button.origin.block = block;
    shutter_button.flags |= EntryFlags::ContextualName;
    shutter_button.origin.name_context_kind
        = EntryNameContextKind::CanonCustomFunctions20701;
    shutter_button.origin.name_context_variant = 1U;
    const EntryId shutter_button_id = store.add_entry(shutter_button);
    ASSERT_NE(shutter_button_id, openmeta::kInvalidEntryId);

    const Entry& shutter_button_entry = store.entry(shutter_button_id);
    EXPECT_EQ(exif_entry_name(store, shutter_button_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Shutter-AELock"));
    EXPECT_EQ(exif_entry_name(store, shutter_button_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("ShutterButtonAFOnButton"));

    Entry af_and_metering;
    af_and_metering.key          = make_exif_tag_key(store.arena(),
                                                     "mk_canoncustom_functions2_0",
                                                     0x0701);
    af_and_metering.origin.block = block;
    af_and_metering.flags |= EntryFlags::ContextualName;
    af_and_metering.origin.name_context_kind
        = EntryNameContextKind::CanonCustomFunctions20701;
    af_and_metering.origin.name_context_variant = 2U;
    const EntryId af_and_metering_id = store.add_entry(af_and_metering);
    ASSERT_NE(af_and_metering_id, openmeta::kInvalidEntryId);

    const Entry& af_and_metering_entry = store.entry(af_and_metering_id);
    EXPECT_EQ(exif_entry_name(store, af_and_metering_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Shutter-AELock"));
    EXPECT_EQ(exif_entry_name(store, af_and_metering_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("AFAndMeteringButtons"));
}


TEST(ExifTagNames, ContextualEntryNamesSelectKodakCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry kodak_model;
    kodak_model.key = make_exif_tag_key(store.arena(), "mk_kodak0", 0x0028);
    kodak_model.origin.block = block;
    kodak_model.flags |= EntryFlags::ContextualName;
    kodak_model.origin.name_context_kind = EntryNameContextKind::KodakMain0028;
    kodak_model.origin.name_context_variant = 1U;
    const EntryId kodak_model_id            = store.add_entry(kodak_model);
    ASSERT_NE(kodak_model_id, openmeta::kInvalidEntryId);

    const Entry& kodak_model_entry = store.entry(kodak_model_id);
    EXPECT_EQ(exif_entry_name(store, kodak_model_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Distance1"));
    EXPECT_EQ(exif_entry_name(store, kodak_model_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("KodakModel"));
}


TEST(ExifTagNames, ContextualEntryNamesSelectMotorolaCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry placeholder;
    placeholder.key = make_exif_tag_key(store.arena(), "mk_motorola0", 0x6420);
    placeholder.origin.block = block;
    placeholder.flags |= EntryFlags::ContextualName;
    placeholder.origin.name_context_kind
        = EntryNameContextKind::MotorolaMain6420;
    placeholder.origin.name_context_variant = 1U;
    const EntryId placeholder_id            = store.add_entry(placeholder);
    ASSERT_NE(placeholder_id, openmeta::kInvalidEntryId);

    const Entry& placeholder_entry = store.entry(placeholder_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("CustomRendered"));
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Motorola_0x6420"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectFujifilmCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry placeholder;
    placeholder.key = make_exif_tag_key(store.arena(), "mk_fuji0", 0x1304);
    placeholder.origin.block = block;
    placeholder.flags |= EntryFlags::ContextualName;
    placeholder.origin.name_context_kind
        = EntryNameContextKind::FujifilmMain1304;
    placeholder.origin.name_context_variant = 1U;
    const EntryId placeholder_id            = store.add_entry(placeholder);
    ASSERT_NE(placeholder_id, openmeta::kInvalidEntryId);

    const Entry& placeholder_entry = store.entry(placeholder_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("GEImageSize"));
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FujiFilm_0x1304"));

    Entry wb_placeholder;
    wb_placeholder.key = make_exif_tag_key(store.arena(), "mk_fuji0", 0x144A);
    wb_placeholder.origin.block = block;
    wb_placeholder.flags |= EntryFlags::ContextualName;
    wb_placeholder.origin.name_context_kind
        = EntryNameContextKind::FujifilmMain1304;
    wb_placeholder.origin.name_context_variant = 1U;
    const EntryId wb_placeholder_id = store.add_entry(wb_placeholder);
    ASSERT_NE(wb_placeholder_id, openmeta::kInvalidEntryId);

    const Entry& wb_placeholder_entry = store.entry(wb_placeholder_id);
    EXPECT_EQ(exif_entry_name(store, wb_placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("WBRed"));
    EXPECT_EQ(exif_entry_name(store, wb_placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("FujiFilm_0x144a"));
}


TEST(ExifTagNames, ContextualEntryNamesSelectRicohCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry placeholder;
    placeholder.key = make_exif_tag_key(store.arena(), "mk_ricoh0", 0x1002);
    placeholder.origin.block = block;
    placeholder.flags |= EntryFlags::ContextualName;
    placeholder.origin.name_context_kind = EntryNameContextKind::RicohMainCompat;
    placeholder.origin.name_context_variant = 1U;
    const EntryId placeholder_id            = store.add_entry(placeholder);
    ASSERT_NE(placeholder_id, openmeta::kInvalidEntryId);

    Entry white_balance;
    white_balance.key = make_exif_tag_key(store.arena(), "mk_ricoh0", 0x1003);
    white_balance.origin.block = block;
    white_balance.flags |= EntryFlags::ContextualName;
    white_balance.origin.name_context_kind
        = EntryNameContextKind::RicohMainCompat;
    white_balance.origin.name_context_variant = 2U;
    const EntryId white_balance_id            = store.add_entry(white_balance);
    ASSERT_NE(white_balance_id, openmeta::kInvalidEntryId);

    const Entry& placeholder_entry = store.entry(placeholder_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("DriveMode"));
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Ricoh_0x1002"));

    const Entry& white_balance_entry = store.entry(white_balance_id);
    EXPECT_EQ(exif_entry_name(store, white_balance_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Sharpness"));
    EXPECT_EQ(exif_entry_name(store, white_balance_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("WhiteBalance"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectPentaxCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry raw_development_process;
    raw_development_process.key = make_exif_tag_key(store.arena(), "mk_pentax0",
                                                    0x0062);
    raw_development_process.origin.block = block;
    raw_development_process.flags |= EntryFlags::ContextualName;
    raw_development_process.origin.name_context_kind
        = EntryNameContextKind::PentaxMain0062;
    raw_development_process.origin.name_context_variant = 1U;
    const EntryId raw_development_process_id            = store.add_entry(
        raw_development_process);
    ASSERT_NE(raw_development_process_id, openmeta::kInvalidEntryId);

    const Entry& raw_development_process_entry = store.entry(
        raw_development_process_id);
    EXPECT_EQ(exif_entry_name(store, raw_development_process_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("RawDevelopmentProcess"));
    EXPECT_EQ(exif_entry_name(store, raw_development_process_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Pentax_0x0062"));
}

TEST(ExifTagNames, EntryNamesSelectSamsungType2A002CompatWithPeerEntries)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryId;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry serial_number;
    serial_number.key = make_exif_tag_key(store.arena(), "mk_samsung_type2_0",
                                          0xA002);
    serial_number.origin.block     = block;
    const EntryId serial_number_id = store.add_entry(serial_number);
    ASSERT_NE(serial_number_id, openmeta::kInvalidEntryId);

    Entry lens_type;
    lens_type.key = make_exif_tag_key(store.arena(), "mk_samsung_type2_0",
                                      0xA003);
    lens_type.origin.block     = block;
    const EntryId lens_type_id = store.add_entry(lens_type);
    ASSERT_NE(lens_type_id, openmeta::kInvalidEntryId);

    const Entry& serial_number_entry = store.entry(serial_number_id);
    EXPECT_EQ(exif_entry_name(store, serial_number_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("SerialNumber"));
    EXPECT_EQ(exif_entry_name(store, serial_number_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Samsung_Type2_0xa002"));

    const Entry& lens_type_entry = store.entry(lens_type_id);
    EXPECT_EQ(exif_entry_name(store, lens_type_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("LensType"));
}

TEST(ExifTagNames, EntryNamesSelectSamsungType2A002CompatWithoutPeerEntries)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryId;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry serial_number;
    serial_number.key = make_exif_tag_key(store.arena(), "mk_samsung_type2_0",
                                          0xA002);
    serial_number.origin.block     = block;
    const EntryId serial_number_id = store.add_entry(serial_number);
    ASSERT_NE(serial_number_id, openmeta::kInvalidEntryId);

    const Entry& serial_number_entry = store.entry(serial_number_id);
    EXPECT_EQ(exif_entry_name(store, serial_number_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("SerialNumber"));
    EXPECT_EQ(exif_entry_name(store, serial_number_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Samsung_Type2_0xa002"));
}

TEST(ExifTagNames, EntryNamesSelectMinoltaMainCompatPlaceholders)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryId;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry first;
    first.key = make_exif_tag_key(store.arena(), "mk_minolta0", 0x0018);
    first.origin.block     = block;
    const EntryId first_id = store.add_entry(first);
    ASSERT_NE(first_id, openmeta::kInvalidEntryId);

    Entry second;
    second.key = make_exif_tag_key(store.arena(), "mk_minolta0", 0x0113);
    second.origin.block     = block;
    const EntryId second_id = store.add_entry(second);
    ASSERT_NE(second_id, openmeta::kInvalidEntryId);

    const Entry& first_entry = store.entry(first_id);
    EXPECT_EQ(exif_entry_name(store, first_entry, ExifTagNamePolicy::Canonical),
              std::string_view("ImageStabilization"));
    EXPECT_EQ(exif_entry_name(store, first_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Minolta_0x0018"));

    const Entry& second_entry = store.entry(second_id);
    EXPECT_EQ(exif_entry_name(store, second_entry, ExifTagNamePolicy::Canonical),
              std::string_view("ImageStabilization"));
    EXPECT_EQ(exif_entry_name(store, second_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Minolta_0x0113"));
}

TEST(ExifTagNames, ContextualEntryNamesSelectNikonSettingsCompatVariants)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryFlags;
    using openmeta::EntryId;
    using openmeta::EntryNameContextKind;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry placeholder;
    placeholder.key          = make_exif_tag_key(store.arena(),
                                                 "mk_nikonsettings_main_0", 0x0001);
    placeholder.origin.block = block;
    placeholder.flags |= EntryFlags::ContextualName;
    placeholder.origin.name_context_kind
        = EntryNameContextKind::NikonSettingsMain;
    placeholder.origin.name_context_variant = 1U;
    const EntryId placeholder_id            = store.add_entry(placeholder);
    ASSERT_NE(placeholder_id, openmeta::kInvalidEntryId);

    const Entry& placeholder_entry = store.entry(placeholder_id);
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ISOAutoHiLimit"));
    EXPECT_EQ(exif_entry_name(store, placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("NikonSettings_0x0001"));
}

TEST(ExifTagNames, EntryNamesSelectPanasonicMainCompatPlaceholders)
{
    using openmeta::BlockInfo;
    using openmeta::Entry;
    using openmeta::EntryId;
    using openmeta::exif_entry_name;
    using openmeta::ExifTagNamePolicy;
    using openmeta::make_exif_tag_key;
    using openmeta::make_u16;
    using openmeta::MetaStore;

    MetaStore store;
    const openmeta::BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    Entry thumbnail_width;
    thumbnail_width.key = make_exif_tag_key(store.arena(), "mk_panasonic0",
                                            0x0058);
    thumbnail_width.origin.block     = block;
    const EntryId thumbnail_width_id = store.add_entry(thumbnail_width);
    ASSERT_NE(thumbnail_width_id, openmeta::kInvalidEntryId);

    Entry af_area_size;
    af_area_size.key = make_exif_tag_key(store.arena(), "mk_panasonic0",
                                         0x00DE);
    af_area_size.origin.block     = block;
    const EntryId af_area_size_id = store.add_entry(af_area_size);
    ASSERT_NE(af_area_size_id, openmeta::kInvalidEntryId);

    Entry lens_type;
    lens_type.key = make_exif_tag_key(store.arena(), "mk_panasonic0", 0x0051);
    lens_type.origin.block     = block;
    const EntryId lens_type_id = store.add_entry(lens_type);
    ASSERT_NE(lens_type_id, openmeta::kInvalidEntryId);

    Entry lens_type_make_placeholder;
    lens_type_make_placeholder.key          = make_exif_tag_key(store.arena(),
                                                                "mk_panasonic0", 0x00C4);
    lens_type_make_placeholder.value        = make_u16(65535U);
    lens_type_make_placeholder.origin.block = block;
    const EntryId lens_type_make_placeholder_id = store.add_entry(
        lens_type_make_placeholder);
    ASSERT_NE(lens_type_make_placeholder_id, openmeta::kInvalidEntryId);

    Entry lens_type_make_named;
    lens_type_make_named.key = make_exif_tag_key(store.arena(), "mk_panasonic0",
                                                 0x00C4);
    lens_type_make_named.value            = make_u16(0U);
    lens_type_make_named.origin.block     = block;
    const EntryId lens_type_make_named_id = store.add_entry(
        lens_type_make_named);
    ASSERT_NE(lens_type_make_named_id, openmeta::kInvalidEntryId);

    const Entry& thumbnail_width_entry = store.entry(thumbnail_width_id);
    EXPECT_EQ(exif_entry_name(store, thumbnail_width_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("ThumbnailWidth"));
    EXPECT_EQ(exif_entry_name(store, thumbnail_width_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Panasonic_0x0058"));

    const Entry& af_area_size_entry = store.entry(af_area_size_id);
    EXPECT_EQ(exif_entry_name(store, af_area_size_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("AFAreaSize"));
    EXPECT_EQ(exif_entry_name(store, af_area_size_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Panasonic_0x00de"));

    const Entry& lens_type_entry = store.entry(lens_type_id);
    EXPECT_EQ(exif_entry_name(store, lens_type_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("LensType"));
    EXPECT_EQ(exif_entry_name(store, lens_type_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("LensType"));

    const Entry& lens_type_make_placeholder_entry = store.entry(
        lens_type_make_placeholder_id);
    EXPECT_EQ(exif_entry_name(store, lens_type_make_placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("LensTypeMake"));
    EXPECT_EQ(exif_entry_name(store, lens_type_make_placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Panasonic_0x00c4"));

    const Entry& lens_type_make_named_entry = store.entry(
        lens_type_make_named_id);
    EXPECT_EQ(exif_entry_name(store, lens_type_make_named_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("LensTypeMake"));
    EXPECT_EQ(exif_entry_name(store, lens_type_make_named_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("LensTypeMake"));

    Entry legacy_model_placeholder;
    legacy_model_placeholder.key              = make_exif_tag_key(store.arena(),
                                                                  "mk_panasonic0", 0x0004);
    legacy_model_placeholder.value            = make_u16(1U);
    legacy_model_placeholder.origin.block     = block;
    const EntryId legacy_model_placeholder_id = store.add_entry(
        legacy_model_placeholder);
    ASSERT_NE(legacy_model_placeholder_id, openmeta::kInvalidEntryId);

    const Entry& legacy_model_placeholder_entry = store.entry(
        legacy_model_placeholder_id);
    EXPECT_EQ(exif_entry_name(store, legacy_model_placeholder_entry,
                              ExifTagNamePolicy::Canonical),
              std::string_view("Model"));
    EXPECT_EQ(exif_entry_name(store, legacy_model_placeholder_entry,
                              ExifTagNamePolicy::ExifToolCompat),
              std::string_view("Panasonic_0x0004"));
}
