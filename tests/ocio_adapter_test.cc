// SPDX-License-Identifier: Apache-2.0

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

TEST(OcioAdapter, IncludesNormalizedDngCcmNamespace)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1U, 6U, 0U, 0U };
    Entry dng_ver;
    dng_ver.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
    dng_ver.value        = make_u8_array(store.arena(),
                                         std::span<const uint8_t>(dng_version.data(),
                                                                  dng_version.size()));
    dng_ver.origin.block = block;
    dng_ver.origin.order_in_block = 0;
    (void)store.add_entry(dng_ver);

    const URational cm_values[9] = {
        { 1000, 1000 }, { 0, 1000 },    { 0, 1000 },
        { 0, 1000 },    { 1000, 1000 }, { 0, 1000 },
        { 0, 1000 },    { 0, 1000 },    { 1000, 1000 },
    };
    Entry cm1;
    cm1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value        = make_urational_array(store.arena(),
                                            std::span<const URational>(cm_values, 9));
    cm1.origin.block = block;
    cm1.origin.order_in_block = 1;
    (void)store.add_entry(cm1);
    store.finalize();

    OcioAdapterRequest request;
    OcioMetadataNode root;
    build_ocio_metadata_tree(store, &root, request);

    const OcioMetadataNode* ns = find_child(root, "dngnorm");
    ASSERT_NE(ns, nullptr);
    const OcioMetadataNode* cm = find_child(*ns, "ifd0.ColorMatrix1");
    ASSERT_NE(cm, nullptr);
    EXPECT_FALSE(cm->value.empty());

    request.include_normalized_ccm = false;
    root                           = OcioMetadataNode {};
    build_ocio_metadata_tree(store, &root, request);
    EXPECT_EQ(find_child(root, "dngnorm"), nullptr);
}

TEST(OcioAdapter, NormalizedDngCcmRequiresDngContext)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const URational cm_values[9] = {
        { 1000, 1000 }, { 0, 1000 },    { 0, 1000 },
        { 0, 1000 },    { 1000, 1000 }, { 0, 1000 },
        { 0, 1000 },    { 0, 1000 },    { 1000, 1000 },
    };
    Entry cm1;
    cm1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value        = make_urational_array(store.arena(),
                                            std::span<const URational>(cm_values, 9));
    cm1.origin.block = block;
    cm1.origin.order_in_block = 0;
    (void)store.add_entry(cm1);
    store.finalize();

    OcioAdapterRequest request;
    OcioMetadataNode root;
    build_ocio_metadata_tree(store, &root, request);
    EXPECT_EQ(find_child(root, "dngnorm"), nullptr);
}

TEST(OcioAdapter, IncludesBmffAuxSemanticFields)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry semantic;
    semantic.key = make_bmff_field_key(store.arena(), "primary.auxl_semantic");
    semantic.value = make_text(store.arena(), "depth", TextEncoding::Ascii);
    semantic.origin.block          = block;
    semantic.origin.order_in_block = 0;
    (void)store.add_entry(semantic);

    Entry depth_id;
    depth_id.key = make_bmff_field_key(store.arena(), "primary.depth_item_id");
    depth_id.value                 = make_u32(2U);
    depth_id.origin.block          = block;
    depth_id.origin.order_in_block = 1;
    (void)store.add_entry(depth_id);

    store.finalize();

    OcioAdapterRequest request;
    request.style = ExportNameStyle::Canonical;
    OcioMetadataNode root;
    build_ocio_metadata_tree(store, &root, request);

    const OcioMetadataNode* bmff = find_child(root, "bmff");
    ASSERT_NE(bmff, nullptr);

    const OcioMetadataNode* sem_leaf = find_child(*bmff,
                                                  "primary.auxl_semantic");
    ASSERT_NE(sem_leaf, nullptr);
    EXPECT_EQ(sem_leaf->value, "depth");

    const OcioMetadataNode* depth_leaf = find_child(*bmff,
                                                    "primary.depth_item_id");
    ASSERT_NE(depth_leaf, nullptr);
    EXPECT_EQ(depth_leaf->value, "2");

    InteropSafetyError safe_error;
    OcioMetadataNode safe_root;
    const InteropSafetyStatus safe_status
        = build_ocio_metadata_tree_safe(store, &safe_root, request,
                                        &safe_error);
    ASSERT_EQ(safe_status, InteropSafetyStatus::Ok);
    EXPECT_TRUE(safe_error.message.empty());
}

}  // namespace openmeta
