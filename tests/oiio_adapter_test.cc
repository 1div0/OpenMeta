#include "openmeta/oiio_adapter.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static const OiioAttribute*
    find_attr(const std::vector<OiioAttribute>& attrs,
              std::string_view name) noexcept
    {
        for (size_t i = 0; i < attrs.size(); ++i) {
            if (attrs[i].name == name) {
                return &attrs[i];
            }
        }
        return nullptr;
    }

}  // namespace


TEST(OiioAdapter, CollectsOiioNamedAttributes)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry make;
    make.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    make.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    make.origin.block = block;
    make.origin.order_in_block = 0;
    (void)store.add_entry(make);

    Entry exposure;
    exposure.key          = make_exif_tag_key(store.arena(), "exififd", 0x829A);
    exposure.value        = make_urational(1, 1250);
    exposure.origin.block = block;
    exposure.origin.order_in_block = 1;
    (void)store.add_entry(exposure);

    const std::array<uint16_t, 3> arr = { 1U, 2U, 3U };
    Entry exr_vec;
    exr_vec.key = make_exr_attribute_key(store.arena(), 0U, "v2");
    exr_vec.value
        = make_u16_array(store.arena(),
                         std::span<const uint16_t>(arr.data(), arr.size()));
    exr_vec.origin.block          = block;
    exr_vec.origin.order_in_block = 2;
    (void)store.add_entry(exr_vec);

    const std::array<std::byte, 2> bytes = { std::byte { 0xDE },
                                             std::byte { 0xAD } };

    Entry exr_owner;
    exr_owner.key   = make_exr_attribute_key(store.arena(), 0U, "owner");
    exr_owner.value = make_text(store.arena(), "showA", TextEncoding::Utf8);
    exr_owner.origin.block          = block;
    exr_owner.origin.order_in_block = 3;
    (void)store.add_entry(exr_owner);

    Entry exr_compression;
    exr_compression.key          = make_exr_attribute_key(store.arena(), 0U,
                                                          "compression");
    exr_compression.value        = make_text(store.arena(), "zip",
                                             TextEncoding::Ascii);
    exr_compression.origin.block = block;
    exr_compression.origin.order_in_block = 4;
    (void)store.add_entry(exr_compression);

    Entry bmff;
    bmff.key = make_bmff_field_key(store.arena(), "meta.test");
    bmff.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(bytes.data(), bytes.size()));
    bmff.origin.block          = block;
    bmff.origin.order_in_block = 5;
    (void)store.add_entry(bmff);

    Entry empty_unknown;
    empty_unknown.key   = make_exif_tag_key(store.arena(), "ifd0", 0xC5D8);
    empty_unknown.value = MetaValue {};
    empty_unknown.origin.block          = block;
    empty_unknown.origin.order_in_block = 6;
    (void)store.add_entry(empty_unknown);

    Entry empty_makernote;
    empty_makernote.key = make_exif_tag_key(store.arena(), "exififd", 0x927C);
    empty_makernote.value                 = MetaValue {};
    empty_makernote.origin.block          = block;
    empty_makernote.origin.order_in_block = 7;
    (void)store.add_entry(empty_makernote);

    store.finalize();

    OiioAdapterOptions options;
    options.max_value_bytes = 256;

    std::vector<OiioAttribute> attrs;
    collect_oiio_attributes(store, &attrs, options);

    const OiioAttribute* a_make = find_attr(attrs, "Make");
    ASSERT_NE(a_make, nullptr);
    EXPECT_EQ(a_make->value, "Canon");

    const OiioAttribute* a_exp = find_attr(attrs, "Exif:ExposureTime");
    ASSERT_NE(a_exp, nullptr);
    EXPECT_EQ(a_exp->value, "1/1250");

    const OiioAttribute* a_exr = find_attr(attrs, "openexr:v2");
    ASSERT_NE(a_exr, nullptr);
    EXPECT_EQ(a_exr->value, "[1, 2, 3]");

    const OiioAttribute* a_owner = find_attr(attrs, "Copyright");
    ASSERT_NE(a_owner, nullptr);
    EXPECT_EQ(a_owner->value, "showA");

    EXPECT_EQ(find_attr(attrs, "openexr:compression"), nullptr);

    const OiioAttribute* a_bmff = find_attr(attrs, "bmff:meta.test");
    ASSERT_NE(a_bmff, nullptr);
    EXPECT_EQ(a_bmff->value, "0xDEAD");

    const OiioAttribute* a_empty_unknown = find_attr(attrs, "Exif_0xc5d8");
    ASSERT_NE(a_empty_unknown, nullptr);
    EXPECT_TRUE(a_empty_unknown->value.empty());

    OiioAdapterOptions spec_options = options;
    spec_options.export_options.name_policy = ExportNamePolicy::Spec;
    std::vector<OiioAttribute> spec_attrs;
    collect_oiio_attributes(store, &spec_attrs, spec_options);

    const OiioAttribute* a_empty_makernote
        = find_attr(spec_attrs, "Exif:MakerNote");
    ASSERT_NE(a_empty_makernote, nullptr);
    EXPECT_TRUE(a_empty_makernote->value.empty());
}

}  // namespace openmeta
