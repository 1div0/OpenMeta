#include "openmeta/interop_export.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

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

        void on_item(const ExportItem& item) override
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


    static MetaStore make_export_store()
    {
        MetaStore store;
        const BlockId block = store.add_block(BlockInfo {});

        Entry make;
        make.key   = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
        make.value = make_text(store.arena(), "Canon", TextEncoding::Ascii);
        make.origin.block          = block;
        make.origin.order_in_block = 0;
        (void)store.add_entry(make);

        Entry exposure;
        exposure.key   = make_exif_tag_key(store.arena(), "exififd", 0x829A);
        exposure.value = make_urational(1, 1250);
        exposure.origin.block          = block;
        exposure.origin.order_in_block = 1;
        (void)store.add_entry(exposure);

        Entry modify_date;
        modify_date.key   = make_exif_tag_key(store.arena(), "ifd0", 0x0132);
        modify_date.value = make_text(store.arena(), "2026:02:11 10:00:00",
                                      TextEncoding::Ascii);
        modify_date.origin.block          = block;
        modify_date.origin.order_in_block = 2;
        (void)store.add_entry(modify_date);

        Entry iso;
        iso.key          = make_exif_tag_key(store.arena(), "exififd", 0x8827);
        iso.value        = make_u16(200);
        iso.origin.block = block;
        iso.origin.order_in_block = 3;
        (void)store.add_entry(iso);

        Entry exposure_bias;
        exposure_bias.key = make_exif_tag_key(store.arena(), "exififd", 0x9204);
        exposure_bias.value                 = make_srational(0, 1);
        exposure_bias.origin.block          = block;
        exposure_bias.origin.order_in_block = 4;
        (void)store.add_entry(exposure_bias);

        Entry create_date;
        create_date.key   = make_exif_tag_key(store.arena(), "exififd", 0x9004);
        create_date.value = make_text(store.arena(), "2026:02:11 10:00:00",
                                      TextEncoding::Ascii);
        create_date.origin.block          = block;
        create_date.origin.order_in_block = 5;
        (void)store.add_entry(create_date);

        Entry unknown_ifd0;
        unknown_ifd0.key   = make_exif_tag_key(store.arena(), "ifd0", 0xC5D8);
        unknown_ifd0.value = make_u32(1);
        unknown_ifd0.origin.block          = block;
        unknown_ifd0.origin.order_in_block = 6;
        (void)store.add_entry(unknown_ifd0);

        Entry pointer;
        pointer.key          = make_exif_tag_key(store.arena(), "ifd0", 0x8769);
        pointer.value        = make_u32(1234);
        pointer.origin.block = block;
        pointer.origin.order_in_block = 7;
        (void)store.add_entry(pointer);

        Entry maker_note;
        maker_note.key   = make_exif_tag_key(store.arena(), "mk_canon", 0x0001);
        maker_note.value = make_u16(9);
        maker_note.origin.block          = block;
        maker_note.origin.order_in_block = 8;
        (void)store.add_entry(maker_note);

        Entry xmp_packet;
        xmp_packet.key   = make_exif_tag_key(store.arena(), "ifd0", 0x02BC);
        xmp_packet.value = make_text(store.arena(), "<xmpmeta/>",
                                     TextEncoding::Utf8);
        xmp_packet.origin.block          = block;
        xmp_packet.origin.order_in_block = 9;
        (void)store.add_entry(xmp_packet);

        Entry xmp_prop;
        xmp_prop.key                   = make_xmp_property_key(store.arena(),
                                                               "http://ns.adobe.com/exif/1.0/",
                                                               "FNumber");
        xmp_prop.value                 = make_urational(28, 10);
        xmp_prop.origin.block          = block;
        xmp_prop.origin.order_in_block = 10;
        (void)store.add_entry(xmp_prop);

        Entry exr_attr;
        exr_attr.key   = make_exr_attribute_key(store.arena(), 0U, "owner");
        exr_attr.value = make_text(store.arena(), "showA", TextEncoding::Utf8);
        exr_attr.origin.block          = block;
        exr_attr.origin.order_in_block = 11;
        (void)store.add_entry(exr_attr);

        Entry exr_skipped;
        exr_skipped.key          = make_exr_attribute_key(store.arena(), 0U,
                                                          "compression");
        exr_skipped.value        = make_text(store.arena(), "zip",
                                             TextEncoding::Ascii);
        exr_skipped.origin.block = block;
        exr_skipped.origin.order_in_block = 12;
        (void)store.add_entry(exr_skipped);

        store.finalize();
        return store;
    }

}  // namespace


TEST(InteropExport, CanonicalStyleIncludesExpectedKeys)
{
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
    EXPECT_FALSE(contains_name(names, "tiff:DateTime"));
    EXPECT_FALSE(contains_name(names, "exif:ISOSpeedRatings"));
    EXPECT_FALSE(contains_name(names, "exif:ExposureBiasValue"));
    EXPECT_FALSE(contains_name(names, "exif:DateTimeDigitized"));
    EXPECT_FALSE(contains_name(names, "tiff:ExifIFDPointer"));
    EXPECT_FALSE(contains_name(names, "tiff:XMLPacket"));
    EXPECT_FALSE(contains_prefix(names, "MakerNote:"));
}


TEST(InteropExport, OiioStyleRespectsMakerNoteSwitch)
{
    const MetaStore store = make_export_store();
    std::vector<std::string> names_without_mk;
    std::vector<std::string> names_with_mk;

    {
        NameCollectSink sink(&names_without_mk);
        ExportOptions options;
        options.style              = ExportNameStyle::Oiio;
        options.include_makernotes = false;
        visit_metadata(store, options, sink);
    }

    {
        NameCollectSink sink(&names_with_mk);
        ExportOptions options;
        options.style              = ExportNameStyle::Oiio;
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


TEST(InteropExport, SpecPolicyPreservesNativeTagNames)
{
    const MetaStore store = make_export_store();
    std::vector<std::string> portable_names;
    std::vector<std::string> oiio_names;

    {
        NameCollectSink sink(&portable_names);
        ExportOptions options;
        options.style       = ExportNameStyle::XmpPortable;
        options.name_policy = ExportNamePolicy::Spec;
        visit_metadata(store, options, sink);
    }
    {
        NameCollectSink sink(&oiio_names);
        ExportOptions options;
        options.style              = ExportNameStyle::Oiio;
        options.name_policy        = ExportNamePolicy::Spec;
        options.include_makernotes = false;
        visit_metadata(store, options, sink);
    }

    EXPECT_TRUE(contains_name(portable_names, "tiff:DateTime"));
    EXPECT_TRUE(contains_name(portable_names, "exif:ISOSpeedRatings"));
    EXPECT_TRUE(contains_name(portable_names, "exif:ExposureBiasValue"));
    EXPECT_TRUE(contains_name(portable_names, "exif:DateTimeDigitized"));
    EXPECT_FALSE(contains_name(portable_names, "tiff:ModifyDate"));
    EXPECT_FALSE(contains_name(portable_names, "exif:ISO"));
    EXPECT_FALSE(contains_name(portable_names, "tiff:Exif_0xc5d8"));

    EXPECT_TRUE(contains_name(oiio_names, "DateTime"));
    EXPECT_TRUE(contains_name(oiio_names, "Exif:ISOSpeedRatings"));
    EXPECT_TRUE(contains_name(oiio_names, "Exif:ExposureBiasValue"));
    EXPECT_TRUE(contains_name(oiio_names, "Exif:DateTimeDigitized"));
    EXPECT_TRUE(contains_name(oiio_names, "Tag_0xC5D8"));
    EXPECT_FALSE(contains_name(oiio_names, "ModifyDate"));
    EXPECT_FALSE(contains_name(oiio_names, "Exif:ISO"));
    EXPECT_FALSE(contains_name(oiio_names, "Exif_0xc5d8"));
}

}  // namespace openmeta
