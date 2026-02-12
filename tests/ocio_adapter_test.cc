#include "openmeta/ocio_adapter.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <array>
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


    static void expect_same_tree(const OcioMetadataNode& a,
                                 const OcioMetadataNode& b)
    {
        EXPECT_EQ(a.name, b.name);
        EXPECT_EQ(a.value, b.value);
        ASSERT_EQ(a.children.size(), b.children.size());
        for (size_t i = 0; i < a.children.size(); ++i) {
            expect_same_tree(a.children[i], b.children[i]);
        }
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

    OcioAdapterRequest request;
    OcioMetadataNode request_root;
    build_ocio_metadata_tree(store, &request_root, request);
    expect_same_tree(root, request_root);

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

    InteropSafetyError safe_error;
    OcioMetadataNode safe_root;
    const InteropSafetyStatus safe_status
        = build_ocio_metadata_tree_safe(store, &safe_root, options,
                                        &safe_error);
    ASSERT_EQ(safe_status, InteropSafetyStatus::Ok);
    EXPECT_TRUE(safe_error.message.empty());
    EXPECT_EQ(safe_root.name, "OpenMeta");
}

TEST(OcioAdapter, SafeTreeRejectsBytesValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<std::byte, 4> raw
        = { std::byte { 0x30 }, std::byte { 0x31 }, std::byte { 0x32 },
            std::byte { 0x33 } };
    Entry bmff;
    bmff.key          = make_bmff_field_key(store.arena(), "meta.test");
    bmff.value        = make_bytes(store.arena(),
                                   std::span<const std::byte>(raw.data(), raw.size()));
    bmff.origin.block = block;
    bmff.origin.order_in_block = 0;
    (void)store.add_entry(bmff);
    store.finalize();

    OcioAdapterOptions options;
    options.export_options.style = ExportNameStyle::Canonical;
    OcioMetadataNode root;
    InteropSafetyError safe_error;
    const InteropSafetyStatus safe_status
        = build_ocio_metadata_tree_safe(store, &root, options, &safe_error);
    EXPECT_EQ(safe_status, InteropSafetyStatus::UnsafeData);
    EXPECT_EQ(safe_error.reason, InteropSafetyReason::UnsafeBytes);
    EXPECT_EQ(safe_error.field_name, "bmff:meta.test");
}

}  // namespace openmeta
