// SPDX-License-Identifier: Apache-2.0

#include "openmeta/console_format.h"
#include "openmeta/interop_export.h"

#include "../src/openmeta/interop_value_format_internal.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    class NameCollectSink final : public MetadataSink {
    public:
        explicit NameCollectSink(std::vector<std::string>* out) noexcept
            : out_(out)
        {
        }

        void on_item(const ExportItem& item) noexcept override
        {
            if (!out_) {
                return;
            }
            out_->emplace_back(item.name.data(), item.name.size());
        }

    private:
        std::vector<std::string>* out_;
    };


    static bool contains_name(const std::vector<std::string>& names,
                              std::string_view target) noexcept
    {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i] == target) {
                return true;
            }
        }
        return false;
    }


    static bool contains_prefix(const std::vector<std::string>& names,
                                std::string_view prefix) noexcept
    {
        for (size_t i = 0; i < names.size(); ++i) {
            if (names[i].starts_with(prefix)) {
                return true;
            }
        }
        return false;
    }


    static uint32_t make_fourcc(char a, char b, char c, char d) noexcept
    {
        return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24)
               | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16)
               | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8)
               | static_cast<uint32_t>(static_cast<uint8_t>(d));
    }


    static MetaStore make_export_store(size_t unknown_ifd_count = 0U)
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});

        for (size_t i = 0; i < unknown_ifd_count; ++i) {
            Entry noise;
            noise.key                   = make_exif_tag_key(store.arena(),
                                                            std::string("noise_ifd_")
                                                                + std::to_string(i),
                                                            0x1234);
            noise.value                 = make_u32(static_cast<uint32_t>(i));
            noise.origin.block          = block;
            noise.origin.order_in_block = static_cast<uint32_t>(i);
            (void)store.add_entry(noise);
        }

        const uint32_t order_base = static_cast<uint32_t>(unknown_ifd_count);

        Entry make;
        make.key   = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
        make.value = make_text(store.arena(), "Canon", TextEncoding::Ascii);
        make.origin.block          = block;
        make.origin.order_in_block = order_base + 0U;
        (void)store.add_entry(make);

        Entry exposure;
        exposure.key   = make_exif_tag_key(store.arena(), "exififd", 0x829A);
        exposure.value = make_urational(1, 1250);
        exposure.origin.block          = block;
        exposure.origin.order_in_block = order_base + 1U;
        (void)store.add_entry(exposure);

        Entry modify_date;
        modify_date.key   = make_exif_tag_key(store.arena(), "ifd0", 0x0132);
        modify_date.value = make_text(store.arena(), "2026:02:11 10:00:00",
                                      TextEncoding::Ascii);
        modify_date.origin.block          = block;
        modify_date.origin.order_in_block = order_base + 2U;
        (void)store.add_entry(modify_date);

        Entry iso;
        iso.key          = make_exif_tag_key(store.arena(), "exififd", 0x8827);
        iso.value        = make_u16(200);
        iso.origin.block = block;
        iso.origin.order_in_block = order_base + 3U;
        (void)store.add_entry(iso);

        Entry exposure_bias;
        exposure_bias.key = make_exif_tag_key(store.arena(), "exififd", 0x9204);
        exposure_bias.value                 = make_srational(0, 1);
        exposure_bias.origin.block          = block;
        exposure_bias.origin.order_in_block = order_base + 4U;
        (void)store.add_entry(exposure_bias);

        Entry create_date;
        create_date.key   = make_exif_tag_key(store.arena(), "exififd", 0x9004);
        create_date.value = make_text(store.arena(), "2026:02:11 10:00:00",
                                      TextEncoding::Ascii);
        create_date.origin.block          = block;
        create_date.origin.order_in_block = order_base + 5U;
        (void)store.add_entry(create_date);

        Entry unknown_ifd0;
        unknown_ifd0.key   = make_exif_tag_key(store.arena(), "ifd0", 0xC5D8);
        unknown_ifd0.value = make_u32(1);
        unknown_ifd0.origin.block          = block;
        unknown_ifd0.origin.order_in_block = order_base + 6U;
        (void)store.add_entry(unknown_ifd0);

        Entry pointer;
        pointer.key          = make_exif_tag_key(store.arena(), "ifd0", 0x8769);
        pointer.value        = make_u32(1234);
        pointer.origin.block = block;
        pointer.origin.order_in_block = order_base + 7U;
        (void)store.add_entry(pointer);

        Entry maker_note;
        maker_note.key   = make_exif_tag_key(store.arena(), "mk_canon", 0x0001);
        maker_note.value = make_u16(9);
        maker_note.origin.block          = block;
        maker_note.origin.order_in_block = order_base + 8U;
        (void)store.add_entry(maker_note);

        Entry xmp_packet;
        xmp_packet.key   = make_exif_tag_key(store.arena(), "ifd0", 0x02BC);
        xmp_packet.value = make_text(store.arena(), "<xmpmeta/>",
                                     TextEncoding::Utf8);
        xmp_packet.origin.block          = block;
        xmp_packet.origin.order_in_block = order_base + 9U;
        (void)store.add_entry(xmp_packet);

        Entry xmp_prop;
        xmp_prop.key                   = make_xmp_property_key(store.arena(),
                                                               "http://ns.adobe.com/exif/1.0/",
                                                               "FNumber");
        xmp_prop.value                 = make_urational(28, 10);
        xmp_prop.origin.block          = block;
        xmp_prop.origin.order_in_block = order_base + 10U;
        (void)store.add_entry(xmp_prop);

        Entry exr_attr;
        exr_attr.key   = make_exr_attribute_key(store.arena(), 0U, "owner");
        exr_attr.value = make_text(store.arena(), "showA", TextEncoding::Utf8);
        exr_attr.origin.block          = block;
        exr_attr.origin.order_in_block = order_base + 11U;
        (void)store.add_entry(exr_attr);

        Entry exr_skipped;
        exr_skipped.key          = make_exr_attribute_key(store.arena(), 0U,
                                                          "compression");
        exr_skipped.value        = make_text(store.arena(), "zip",
                                             TextEncoding::Ascii);
        exr_skipped.origin.block = block;
        exr_skipped.origin.order_in_block = order_base + 12U;
        (void)store.add_entry(exr_skipped);

        const std::array<uint8_t, 4> dng_version = { 1, 6, 0, 0 };
        Entry dng_ver;
        dng_ver.key = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
        dng_ver.value
            = make_u8_array(store.arena(),
                            std::span<const uint8_t>(dng_version.data(),
                                                     dng_version.size()));
        dng_ver.origin.block          = block;
        dng_ver.origin.order_in_block = order_base + 13U;
        (void)store.add_entry(dng_ver);

        const URational cm_values[9] = {
            { 1000, 1000 }, { 0, 1000 },    { 0, 1000 },
            { 0, 1000 },    { 1000, 1000 }, { 0, 1000 },
            { 0, 1000 },    { 0, 1000 },    { 1000, 1000 },
        };
        Entry dng_cm1;
        dng_cm1.key = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
        dng_cm1.value
            = make_urational_array(store.arena(),
                                   std::span<const URational>(cm_values, 9));
        dng_cm1.origin.block          = block;
        dng_cm1.origin.order_in_block = order_base + 14U;
        (void)store.add_entry(dng_cm1);

        Entry icc_header;
        icc_header.key          = make_icc_header_field_key(4);
        icc_header.value        = make_u32(make_fourcc('a', 'p', 'p', 'l'));
        icc_header.origin.block = block;
        icc_header.origin.order_in_block = order_base + 15U;
        (void)store.add_entry(icc_header);

        const std::array<std::byte, 4> icc_tag_data = {
            std::byte { 0x64 },
            std::byte { 0x65 },
            std::byte { 0x73 },
            std::byte { 0x63 },
        };
        Entry icc_tag;
        icc_tag.key = make_icc_tag_key(make_fourcc('d', 'e', 's', 'c'));
        icc_tag.value
            = make_bytes(store.arena(),
                         std::span<const std::byte>(icc_tag_data.data(),
                                                    icc_tag_data.size()));
        icc_tag.origin.block          = block;
        icc_tag.origin.order_in_block = order_base + 16U;
        (void)store.add_entry(icc_tag);

        store.finalize();
        return store;
    }

}  // namespace


TEST(InteropExport, CanonicalStyleIncludesExpectedKeys)
{
    EXPECT_EQ(openmeta::kInteropExportContractVersion, 1U);
    EXPECT_EQ(openmeta::kFlatHostExportContractVersion, 1U);

    const MetaStore store = make_export_store();
    std::vector<std::string> names;
    NameCollectSink sink(&names);

    ExportOptions options;
    options.style = ExportNameStyle::Canonical;
    visit_metadata(store, options, sink);

    EXPECT_TRUE(contains_name(names, "exif:ifd0:0x010F"));
    EXPECT_TRUE(contains_name(names, "exif:exififd:0x829A"));
    EXPECT_TRUE(contains_name(names, "exif:mk_canon:0x0001"));
    EXPECT_TRUE(
        contains_name(names, "xmp:http://ns.adobe.com/exif/1.0/:FNumber"));
    EXPECT_TRUE(contains_name(names, "exr:part:0:owner"));
    EXPECT_TRUE(contains_name(names, "icc:header:4"));
    EXPECT_TRUE(contains_name(names, "icc:tag:0x64657363"));
}


TEST(InteropExport, PortableStyleSkipsPointersAndMakerNotes)
{
    const MetaStore store = make_export_store();
    std::vector<std::string> names;
    NameCollectSink sink(&names);

    ExportOptions options;
    options.style = ExportNameStyle::XmpPortable;
    visit_metadata(store, options, sink);

    EXPECT_TRUE(contains_name(names, "tiff:Make"));
    EXPECT_TRUE(contains_name(names, "tiff:ModifyDate"));
    EXPECT_TRUE(contains_name(names, "tiff:Exif_0xc5d8"));
    EXPECT_TRUE(contains_name(names, "exif:ExposureTime"));
    EXPECT_TRUE(contains_name(names, "exif:ISO"));
    EXPECT_TRUE(contains_name(names, "exif:ExposureCompensation"));
    EXPECT_TRUE(contains_name(names, "exif:CreateDate"));
    EXPECT_TRUE(contains_name(names, "exif:FNumber"));
    EXPECT_TRUE(contains_name(names, "tiff:ColorMatrix1"));
    EXPECT_TRUE(contains_name(names, "icc:cmm_type"));
    EXPECT_TRUE(contains_name(names, "icc:tag:0x64657363"));
    EXPECT_FALSE(contains_name(names, "tiff:DateTime"));
    EXPECT_FALSE(contains_name(names, "exif:ISOSpeedRatings"));
    EXPECT_FALSE(contains_name(names, "exif:ExposureBiasValue"));
    EXPECT_FALSE(contains_name(names, "exif:DateTimeDigitized"));
    EXPECT_FALSE(contains_name(names, "tiff:ExifIFDPointer"));
    EXPECT_FALSE(contains_name(names, "tiff:XMLPacket"));
    EXPECT_FALSE(contains_prefix(names, "MakerNote:"));
}


TEST(InteropExport, FlatHostStyleRespectsMakerNoteSwitch)
{
    const MetaStore store = make_export_store();
    std::vector<std::string> names_without_mk;
    std::vector<std::string> names_with_mk;

    {
        NameCollectSink sink(&names_without_mk);
        ExportOptions options;
        options.style              = ExportNameStyle::FlatHost;
        options.include_makernotes = false;
        visit_metadata(store, options, sink);
    }

    {
        NameCollectSink sink(&names_with_mk);
        ExportOptions options;
        options.style              = ExportNameStyle::FlatHost;
        options.include_makernotes = true;
        visit_metadata(store, options, sink);
    }

    EXPECT_TRUE(contains_name(names_without_mk, "Make"));
    EXPECT_TRUE(contains_name(names_without_mk, "ModifyDate"));
    EXPECT_TRUE(contains_name(names_without_mk, "Exif_0xc5d8"));
    EXPECT_TRUE(contains_name(names_without_mk, "Exif:ExposureTime"));
    EXPECT_TRUE(contains_name(names_without_mk, "Exif:ISO"));
    EXPECT_TRUE(contains_name(names_without_mk, "Exif:ExposureCompensation"));
    EXPECT_TRUE(contains_name(names_without_mk, "Exif:CreateDate"));
    EXPECT_TRUE(contains_name(names_without_mk, "ColorMatrix1"));
    EXPECT_TRUE(contains_name(names_without_mk, "ICC:cmm_type"));
    EXPECT_TRUE(contains_name(names_without_mk, "ICC:tag:0x64657363"));
    EXPECT_TRUE(contains_name(names_without_mk, "Copyright"));
    EXPECT_FALSE(contains_name(names_without_mk, "openexr:owner"));
    EXPECT_FALSE(contains_name(names_without_mk, "openexr:compression"));
    EXPECT_FALSE(contains_name(names_without_mk, "Exif:ISOSpeedRatings"));
    EXPECT_FALSE(contains_name(names_without_mk, "Exif:ExposureBiasValue"));
    EXPECT_FALSE(contains_name(names_without_mk, "Exif:DateTimeDigitized"));
    EXPECT_FALSE(contains_name(names_without_mk, "XMLPacket"));
    EXPECT_FALSE(contains_prefix(names_without_mk, "MakerNote:mk_canon:"));
    EXPECT_TRUE(contains_prefix(names_with_mk, "MakerNote:mk_canon:"));
}


TEST(InteropExport, FlatHostStyleRetainsKnownHintsWithUnknownIfdNoise)
{
    const MetaStore store = make_export_store(512U);
    std::vector<std::string> names;
    NameCollectSink sink(&names);

    ExportOptions options;
    options.style              = ExportNameStyle::FlatHost;
    options.include_makernotes = false;
    visit_metadata(store, options, sink);

    EXPECT_TRUE(contains_name(names, "ModifyDate"));
    EXPECT_TRUE(contains_name(names, "ColorMatrix1"));
    EXPECT_TRUE(contains_name(names, "Exif:CreateDate"));
}


TEST(InteropExport, FlatHostStylePreservesDuplicatesAndEntryOrder)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry first;
    first.key = make_xmp_property_key(store.arena(),
                                      "http://ns.adobe.com/xap/1.0/",
                                      "Rating");
    first.value                 = make_u16(1);
    first.origin.block          = block;
    first.origin.order_in_block = 0U;
    (void)store.add_entry(first);

    Entry deleted;
    deleted.key = make_xmp_property_key(store.arena(),
                                        "http://ns.adobe.com/xap/1.0/",
                                        "Label");
    deleted.value                 = make_text(store.arena(), "skip",
                                              TextEncoding::Ascii);
    deleted.flags                 = EntryFlags::Deleted;
    deleted.origin.block          = block;
    deleted.origin.order_in_block = 1U;
    (void)store.add_entry(deleted);

    Entry second;
    second.key = make_xmp_property_key(store.arena(),
                                       "http://ns.adobe.com/xap/1.0/",
                                       "Rating");
    second.value                 = make_u16(2);
    second.origin.block          = block;
    second.origin.order_in_block = 2U;
    (void)store.add_entry(second);

    Entry make;
    make.key = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    make.value                 = make_text(store.arena(), "Canon",
                                           TextEncoding::Ascii);
    make.origin.block          = block;
    make.origin.order_in_block = 3U;
    (void)store.add_entry(make);

    store.finalize();

    std::vector<std::string> names;
    NameCollectSink sink(&names);
    ExportOptions options;
    options.style = ExportNameStyle::FlatHost;
    visit_metadata(store, options, sink);

    ASSERT_EQ(names.size(), 3U);
    EXPECT_EQ(names[0], "XMP:Rating");
    EXPECT_EQ(names[1], "XMP:Rating");
    EXPECT_EQ(names[2], "Make");
}


TEST(InteropExport, ConsoleFormattingHelpersRemainDeterministic)
{
    std::string out;
    const bool dangerous = append_console_escaped_ascii("A\nB\x80", 0U, &out);
    EXPECT_TRUE(dangerous);
    EXPECT_EQ(out, "A\\nB\\x80");

    out.clear();
    const std::array<std::byte, 4> bytes = {
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
        std::byte { 0x04 },
    };
    append_hex_bytes(std::span<const std::byte>(bytes.data(), bytes.size()), 2U,
                     &out);
    EXPECT_EQ(out, "0102...");
}


TEST(InteropExport, FormatValueForTextNormalizesTruncatedArrayElementToEmpty)
{
    MetaStore store;
    const std::array<std::byte, 1> raw = { std::byte { 0x2A } };

    MetaValue value;
    value.kind      = MetaValueKind::Array;
    value.elem_type = MetaElementType::U16;
    value.count     = 1U;
    value.data.span = store.arena().append(
        std::span<const std::byte>(raw.data(), raw.size()));

    std::string out;
    EXPECT_TRUE(interop_internal::format_value_for_text(store.arena(), value,
                                                        0U, &out));
    EXPECT_EQ(out, "[]");
}


TEST(InteropExport, SpecPolicyPreservesNativeTagNames)
{
    const MetaStore store = make_export_store();
    std::vector<std::string> portable_names;
    std::vector<std::string> flat_host_names;

    {
        NameCollectSink sink(&portable_names);
        ExportOptions options;
        options.style       = ExportNameStyle::XmpPortable;
        options.name_policy = ExportNamePolicy::Spec;
        visit_metadata(store, options, sink);
    }
    {
        NameCollectSink sink(&flat_host_names);
        ExportOptions options;
        options.style              = ExportNameStyle::FlatHost;
        options.name_policy        = ExportNamePolicy::Spec;
        options.include_makernotes = false;
        visit_metadata(store, options, sink);
    }

    EXPECT_TRUE(contains_name(portable_names, "tiff:DateTime"));
    EXPECT_TRUE(contains_name(portable_names, "dng:ColorMatrix1"));
    EXPECT_TRUE(contains_name(portable_names, "exif:ISOSpeedRatings"));
    EXPECT_TRUE(contains_name(portable_names, "exif:ExposureBiasValue"));
    EXPECT_TRUE(contains_name(portable_names, "exif:DateTimeDigitized"));
    EXPECT_FALSE(contains_name(portable_names, "tiff:ModifyDate"));
    EXPECT_FALSE(contains_name(portable_names, "exif:ISO"));
    EXPECT_FALSE(contains_name(portable_names, "tiff:Exif_0xc5d8"));

    EXPECT_TRUE(contains_name(flat_host_names, "DateTime"));
    EXPECT_TRUE(contains_name(flat_host_names, "DNG:ColorMatrix1"));
    EXPECT_TRUE(contains_name(flat_host_names, "Exif:ISOSpeedRatings"));
    EXPECT_TRUE(contains_name(flat_host_names, "Exif:ExposureBiasValue"));
    EXPECT_TRUE(contains_name(flat_host_names, "Exif:DateTimeDigitized"));
    EXPECT_TRUE(contains_name(flat_host_names, "Tag_0xC5D8"));
    EXPECT_FALSE(contains_name(flat_host_names, "ModifyDate"));
    EXPECT_FALSE(contains_name(flat_host_names, "Exif:ISO"));
    EXPECT_FALSE(contains_name(flat_host_names, "Exif_0xc5d8"));
}

}  // namespace openmeta
