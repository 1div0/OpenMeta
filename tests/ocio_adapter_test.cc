#include "openmeta/ocio_adapter.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <string_view>

namespace openmeta {
namespace {

    static const OcioMetadataNode* find_child(const OcioMetadataNode& node,
                                              std::string_view name) noexcept
    {
        for (size_t i = 0; i < node.children.size(); ++i) {
            if (node.children[i].name == name) {
                return &node.children[i];
            }
        }
        return nullptr;
    }

}  // namespace


TEST(OcioAdapter, BuildsDeterministicNamespaceTree)
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

    Entry xmp_label;
    xmp_label.key   = make_xmp_property_key(store.arena(),
                                            "http://ns.adobe.com/xap/1.0/",
                                            "Label");
    xmp_label.value = make_text(store.arena(), "shotA", TextEncoding::Utf8);
    xmp_label.origin.block          = block;
    xmp_label.origin.order_in_block = 2;
    (void)store.add_entry(xmp_label);

    store.finalize();

    OcioAdapterOptions options;
    OcioMetadataNode root;
    build_ocio_metadata_tree(store, &root, options);

    EXPECT_EQ(root.name, "OpenMeta");

    const OcioMetadataNode* tiff = find_child(root, "tiff");
    ASSERT_NE(tiff, nullptr);
    const OcioMetadataNode* make_leaf = find_child(*tiff, "Make");
    ASSERT_NE(make_leaf, nullptr);
    EXPECT_EQ(make_leaf->value, "Canon");

    const OcioMetadataNode* exif = find_child(root, "exif");
    ASSERT_NE(exif, nullptr);
    const OcioMetadataNode* exp_leaf = find_child(*exif, "ExposureTime");
    ASSERT_NE(exp_leaf, nullptr);
    EXPECT_EQ(exp_leaf->value, "1/1250");

    const OcioMetadataNode* xmp = find_child(root, "xmp");
    ASSERT_NE(xmp, nullptr);
    const OcioMetadataNode* label_leaf = find_child(*xmp, "Label");
    ASSERT_NE(label_leaf, nullptr);
    EXPECT_EQ(label_leaf->value, "shotA");
}

}  // namespace openmeta
