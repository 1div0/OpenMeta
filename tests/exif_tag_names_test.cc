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
