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

        Entry pointer;
        pointer.key          = make_exif_tag_key(store.arena(), "ifd0", 0x8769);
        pointer.value        = make_u32(1234);
        pointer.origin.block = block;
        pointer.origin.order_in_block = 2;
        (void)store.add_entry(pointer);

        Entry maker_note;
        maker_note.key   = make_exif_tag_key(store.arena(), "mk_canon", 0x0001);
        maker_note.value = make_u16(9);
        maker_note.origin.block          = block;
        maker_note.origin.order_in_block = 3;
        (void)store.add_entry(maker_note);

        Entry xmp_prop;
        xmp_prop.key                   = make_xmp_property_key(store.arena(),
                                                               "http://ns.adobe.com/exif/1.0/",
                                                               "FNumber");
        xmp_prop.value                 = make_urational(28, 10);
        xmp_prop.origin.block          = block;
        xmp_prop.origin.order_in_block = 4;
        (void)store.add_entry(xmp_prop);

        Entry exr_attr;
        exr_attr.key   = make_exr_attribute_key(store.arena(), 0U, "owner");
        exr_attr.value = make_text(store.arena(), "showA", TextEncoding::Utf8);
        exr_attr.origin.block          = block;
        exr_attr.origin.order_in_block = 5;
        (void)store.add_entry(exr_attr);

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
    EXPECT_TRUE(contains_name(names, "exif:ExposureTime"));
    EXPECT_TRUE(contains_name(names, "exif:FNumber"));
    EXPECT_FALSE(contains_name(names, "tiff:ExifIFDPointer"));
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
    EXPECT_TRUE(contains_name(names_without_mk, "Exif:ExposureTime"));
    EXPECT_FALSE(contains_prefix(names_without_mk, "MakerNote:mk_canon:"));
    EXPECT_TRUE(contains_prefix(names_with_mk, "MakerNote:mk_canon:"));
}

}  // namespace openmeta
