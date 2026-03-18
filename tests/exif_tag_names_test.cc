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
