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

    static const OiioTypedAttribute*
    find_typed_attr(const std::vector<OiioTypedAttribute>& attrs,
                    std::string_view name) noexcept
    {
        for (size_t i = 0; i < attrs.size(); ++i) {
            if (attrs[i].name == name) {
                return &attrs[i];
            }
        }
        return nullptr;
    }


    static void expect_same_attributes(const std::vector<OiioAttribute>& a,
                                       const std::vector<OiioAttribute>& b)
    {
        ASSERT_EQ(a.size(), b.size());
        for (size_t i = 0; i < a.size(); ++i) {
            EXPECT_EQ(a[i].name, b[i].name);
            EXPECT_EQ(a[i].value, b[i].value);
        }
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
    empty_makernote.key   = make_exif_tag_key(store.arena(), "exififd", 0x927C);
    empty_makernote.value = MetaValue {};
    empty_makernote.origin.block          = block;
    empty_makernote.origin.order_in_block = 7;
    (void)store.add_entry(empty_makernote);

    store.finalize();

    OiioAdapterOptions options;
    options.max_value_bytes = 256;

    std::vector<OiioAttribute> attrs;
    collect_oiio_attributes(store, &attrs, options);

    OiioAdapterRequest request;
    request.max_value_bytes = 256;
    std::vector<OiioAttribute> request_attrs;
    collect_oiio_attributes(store, &request_attrs, request);
    expect_same_attributes(attrs, request_attrs);

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

    OiioAdapterOptions spec_options         = options;
    spec_options.export_options.name_policy = ExportNamePolicy::Spec;
    std::vector<OiioAttribute> spec_attrs;
    collect_oiio_attributes(store, &spec_attrs, spec_options);

    OiioAdapterRequest spec_request;
    spec_request.max_value_bytes = 256;
    spec_request.name_policy     = ExportNamePolicy::Spec;
    std::vector<OiioAttribute> spec_request_attrs;
    collect_oiio_attributes(store, &spec_request_attrs, spec_request);
    expect_same_attributes(spec_attrs, spec_request_attrs);

    const OiioAttribute* a_empty_makernote = find_attr(spec_attrs,
                                                       "Exif:MakerNote");
    ASSERT_NE(a_empty_makernote, nullptr);
    EXPECT_TRUE(a_empty_makernote->value.empty());

    std::vector<OiioTypedAttribute> typed_attrs;
    collect_oiio_attributes_typed(store, &typed_attrs, request);

    const OiioTypedAttribute* t_make = find_typed_attr(typed_attrs, "Make");
    ASSERT_NE(t_make, nullptr);
    ASSERT_EQ(t_make->value.kind, MetaValueKind::Text);
    ASSERT_EQ(t_make->value.text_encoding, TextEncoding::Ascii);
    ASSERT_EQ(t_make->value.storage.size(), 5U);
    EXPECT_EQ(static_cast<char>(t_make->value.storage[0]), 'C');

    const OiioTypedAttribute* t_exp = find_typed_attr(typed_attrs,
                                                      "Exif:ExposureTime");
    ASSERT_NE(t_exp, nullptr);
    ASSERT_EQ(t_exp->value.kind, MetaValueKind::Scalar);
    ASSERT_EQ(t_exp->value.elem_type, MetaElementType::URational);
    EXPECT_EQ(t_exp->value.data.ur.numer, 1U);
    EXPECT_EQ(t_exp->value.data.ur.denom, 1250U);

    const OiioTypedAttribute* t_exr = find_typed_attr(typed_attrs,
                                                      "openexr:v2");
    ASSERT_NE(t_exr, nullptr);
    ASSERT_EQ(t_exr->value.kind, MetaValueKind::Array);
    ASSERT_EQ(t_exr->value.elem_type, MetaElementType::U16);
    ASSERT_EQ(t_exr->value.count, 3U);

    std::vector<OiioTypedAttribute> typed_spec_attrs;
    collect_oiio_attributes_typed(store, &typed_spec_attrs, spec_request);
    const OiioTypedAttribute* t_empty_makernote
        = find_typed_attr(typed_spec_attrs, "Exif:MakerNote");
    ASSERT_NE(t_empty_makernote, nullptr);
    EXPECT_EQ(t_empty_makernote->value.kind, MetaValueKind::Empty);

    InteropSafetyError safe_error;
    std::vector<OiioAttribute> safe_attrs;
    const InteropSafetyStatus safe_status
        = collect_oiio_attributes_safe(store, &safe_attrs, request,
                                       &safe_error);
    EXPECT_EQ(safe_status, InteropSafetyStatus::UnsafeData);
    EXPECT_EQ(safe_error.reason, InteropSafetyReason::UnsafeBytes);
    EXPECT_EQ(safe_error.field_name, "bmff:meta.test");

    std::vector<OiioTypedAttribute> safe_typed_attrs;
    const InteropSafetyStatus safe_typed_status
        = collect_oiio_attributes_typed_safe(store, &safe_typed_attrs, request,
                                             &safe_error);
    EXPECT_EQ(safe_typed_status, InteropSafetyStatus::UnsafeData);
    EXPECT_EQ(safe_error.reason, InteropSafetyReason::UnsafeBytes);
    EXPECT_EQ(safe_error.field_name, "bmff:meta.test");
}

TEST(OiioAdapter, SafeExportSucceedsWithoutBytesValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    Entry make;
    make.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    make.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    make.origin.block = block;
    make.origin.order_in_block = 0;
    (void)store.add_entry(make);

    Entry owner;
    owner.key          = make_exr_attribute_key(store.arena(), 0U, "owner");
    owner.value        = make_text(store.arena(), "showA", TextEncoding::Utf8);
    owner.origin.block = block;
    owner.origin.order_in_block = 1;
    (void)store.add_entry(owner);

    store.finalize();

    OiioAdapterRequest request;
    request.max_value_bytes = 256U;

    InteropSafetyError safe_error;
    std::vector<OiioAttribute> safe_attrs;
    const InteropSafetyStatus safe_status
        = collect_oiio_attributes_safe(store, &safe_attrs, request,
                                       &safe_error);
    ASSERT_EQ(safe_status, InteropSafetyStatus::Ok);
    EXPECT_TRUE(safe_error.message.empty());

    const OiioAttribute* make_attr = find_attr(safe_attrs, "Make");
    ASSERT_NE(make_attr, nullptr);
    EXPECT_EQ(make_attr->value, "Canon");

    std::vector<OiioTypedAttribute> safe_typed_attrs;
    const InteropSafetyStatus safe_typed_status
        = collect_oiio_attributes_typed_safe(store, &safe_typed_attrs, request,
                                             &safe_error);
    ASSERT_EQ(safe_typed_status, InteropSafetyStatus::Ok);
    const OiioTypedAttribute* owner_attr = find_typed_attr(safe_typed_attrs,
                                                           "Copyright");
    ASSERT_NE(owner_attr, nullptr);
    EXPECT_EQ(owner_attr->value.kind, MetaValueKind::Text);
    EXPECT_EQ(owner_attr->value.text_encoding, TextEncoding::Utf8);
}

TEST(OiioAdapter, TypedExportIncludesNormalizedDngCcm)
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

    OiioAdapterRequest request;
    request.max_value_bytes = 1024U;

    std::vector<OiioTypedAttribute> typed_attrs;
    collect_oiio_attributes_typed(store, &typed_attrs, request);

    const OiioTypedAttribute* cm = find_typed_attr(typed_attrs,
                                                   "DNGNorm:ifd0.ColorMatrix1");
    ASSERT_NE(cm, nullptr);
    EXPECT_EQ(cm->value.kind, MetaValueKind::Array);
    EXPECT_EQ(cm->value.elem_type, MetaElementType::F64);
    EXPECT_EQ(cm->value.count, 9U);

    request.include_normalized_ccm = false;
    typed_attrs.clear();
    collect_oiio_attributes_typed(store, &typed_attrs, request);
    EXPECT_EQ(find_typed_attr(typed_attrs, "DNGNorm:ifd0.ColorMatrix1"),
              nullptr);
}

TEST(OiioAdapter, TypedExportNormalizedDngCcmRequiresDngContext)
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

    OiioAdapterRequest request;
    std::vector<OiioTypedAttribute> typed_attrs;
    collect_oiio_attributes_typed(store, &typed_attrs, request);
    EXPECT_EQ(find_typed_attr(typed_attrs, "DNGNorm:ifd0.ColorMatrix1"),
              nullptr);
}

TEST(OiioAdapter, SafeTypedExportIncludesNormalizedDngCcm)
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

    OiioAdapterRequest request;
    InteropSafetyError error;
    std::vector<OiioTypedAttribute> typed_attrs;
    const InteropSafetyStatus status
        = collect_oiio_attributes_typed_safe(store, &typed_attrs, request,
                                             &error);
    ASSERT_EQ(status, InteropSafetyStatus::Ok);
    EXPECT_TRUE(error.message.empty());

    const OiioTypedAttribute* cm = find_typed_attr(typed_attrs,
                                                   "DNGNorm:ifd0.ColorMatrix1");
    ASSERT_NE(cm, nullptr);
    EXPECT_EQ(cm->value.kind, MetaValueKind::Array);
    EXPECT_EQ(cm->value.elem_type, MetaElementType::F64);
}

TEST(OiioAdapter, ExportsBmffAuxSemanticInSafeMode)
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

    OiioAdapterRequest request;
    request.max_value_bytes = 256U;

    InteropSafetyError safe_error;
    std::vector<OiioAttribute> safe_attrs;
    const InteropSafetyStatus safe_status
        = collect_oiio_attributes_safe(store, &safe_attrs, request,
                                       &safe_error);
    ASSERT_EQ(safe_status, InteropSafetyStatus::Ok);

    const OiioAttribute* semantic_attr
        = find_attr(safe_attrs, "bmff:primary.auxl_semantic");
    ASSERT_NE(semantic_attr, nullptr);
    EXPECT_EQ(semantic_attr->value, "depth");

    const OiioAttribute* depth_attr = find_attr(safe_attrs,
                                                "bmff:primary.depth_item_id");
    ASSERT_NE(depth_attr, nullptr);
    EXPECT_EQ(depth_attr->value, "2");

    std::vector<OiioTypedAttribute> safe_typed_attrs;
    const InteropSafetyStatus safe_typed_status
        = collect_oiio_attributes_typed_safe(store, &safe_typed_attrs, request,
                                             &safe_error);
    ASSERT_EQ(safe_typed_status, InteropSafetyStatus::Ok);

    const OiioTypedAttribute* semantic_typed
        = find_typed_attr(safe_typed_attrs, "bmff:primary.auxl_semantic");
    ASSERT_NE(semantic_typed, nullptr);
    EXPECT_EQ(semantic_typed->value.kind, MetaValueKind::Text);
    EXPECT_EQ(semantic_typed->value.text_encoding, TextEncoding::Utf8);

    const OiioTypedAttribute* depth_typed
        = find_typed_attr(safe_typed_attrs, "bmff:primary.depth_item_id");
    ASSERT_NE(depth_typed, nullptr);
    EXPECT_EQ(depth_typed->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(depth_typed->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(static_cast<uint32_t>(depth_typed->value.data.u64), 2U);
}

TEST(OiioAdapter, CollectsTransferPayloadViewsForJpeg)
{
    PreparedTransferBundle bundle;
    bundle.target_format = TransferTargetFormat::Jpeg;

    PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    PreparedTransferBlock xmp;
    xmp.route   = "jpeg:app1-xmp";
    xmp.payload = { std::byte { 'x' }, std::byte { 'm' }, std::byte { 'p' } };
    bundle.blocks.push_back(xmp);

    std::vector<OiioTransferPayloadView> views;
    const EmitTransferResult result
        = collect_oiio_transfer_payload_views(bundle, &views);

    ASSERT_EQ(result.status, TransferStatus::Ok);
    ASSERT_EQ(views.size(), 2U);

    EXPECT_EQ(views[0].semantic_kind, OiioTransferPayloadKind::ExifBlob);
    EXPECT_EQ(views[0].semantic_name, "ExifBlob");
    EXPECT_EQ(views[0].route, "jpeg:app1-exif");
    EXPECT_EQ(views[0].op.kind, TransferAdapterOpKind::JpegMarker);
    EXPECT_EQ(views[0].op.jpeg_marker_code, 0xE1U);
    EXPECT_EQ(views[0].payload.size(), 2U);
    EXPECT_EQ(views[0].payload.data(), bundle.blocks[0].payload.data());

    EXPECT_EQ(views[1].semantic_kind, OiioTransferPayloadKind::XmpPacket);
    EXPECT_EQ(views[1].semantic_name, "XMPPacket");
    EXPECT_EQ(views[1].route, "jpeg:app1-xmp");
    EXPECT_EQ(views[1].op.kind, TransferAdapterOpKind::JpegMarker);
    EXPECT_EQ(views[1].op.jpeg_marker_code, 0xE1U);
    EXPECT_EQ(views[1].payload.size(), 3U);
    EXPECT_EQ(views[1].payload.data(), bundle.blocks[1].payload.data());
}

TEST(OiioAdapter, CollectsTransferPayloadViewsForTiff)
{
    PreparedTransferBundle bundle;
    bundle.target_format = TransferTargetFormat::Tiff;

    PreparedTransferBlock icc;
    icc.route   = "tiff:tag-34675-icc";
    icc.payload = { std::byte { 0xAA }, std::byte { 0xBB } };
    bundle.blocks.push_back(icc);

    PreparedTransferBlock iptc;
    iptc.route   = "tiff:tag-33723-iptc";
    iptc.payload = { std::byte { 0xCC } };
    bundle.blocks.push_back(iptc);

    std::vector<OiioTransferPayloadView> views;
    const EmitTransferResult result
        = collect_oiio_transfer_payload_views(bundle, &views);

    ASSERT_EQ(result.status, TransferStatus::Ok);
    ASSERT_EQ(views.size(), 2U);

    EXPECT_EQ(views[0].semantic_kind, OiioTransferPayloadKind::IccProfile);
    EXPECT_EQ(views[0].semantic_name, "ICCProfile");
    EXPECT_EQ(views[0].op.kind, TransferAdapterOpKind::TiffTagBytes);
    EXPECT_EQ(views[0].op.tiff_tag, 34675U);
    EXPECT_EQ(views[0].payload.data(), bundle.blocks[0].payload.data());

    EXPECT_EQ(views[1].semantic_kind, OiioTransferPayloadKind::IptcBlock);
    EXPECT_EQ(views[1].semantic_name, "IPTCBlock");
    EXPECT_EQ(views[1].op.kind, TransferAdapterOpKind::TiffTagBytes);
    EXPECT_EQ(views[1].op.tiff_tag, 33723U);
    EXPECT_EQ(views[1].payload.data(), bundle.blocks[1].payload.data());
}

TEST(OiioAdapter, CollectsTransferPayloadViewsForJxl)
{
    PreparedTransferBundle bundle;
    bundle.target_format = TransferTargetFormat::Jxl;

    PreparedTransferBlock icc;
    icc.route   = "jxl:icc-profile";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 },
                    std::byte { 0x43 } };
    bundle.blocks.push_back(icc);

    PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 } };
    bundle.blocks.push_back(jumbf);

    PreparedTransferBlock c2pa;
    c2pa.route    = "jxl:box-c2pa";
    c2pa.box_type = { 'c', '2', 'p', 'a' };
    c2pa.payload  = { std::byte { 0x10 }, std::byte { 0x11 } };
    bundle.blocks.push_back(c2pa);

    std::vector<OiioTransferPayloadView> views;
    const EmitTransferResult result
        = collect_oiio_transfer_payload_views(bundle, &views);

    ASSERT_EQ(result.status, TransferStatus::Ok);
    ASSERT_EQ(views.size(), 3U);

    EXPECT_EQ(views[0].semantic_kind, OiioTransferPayloadKind::IccProfile);
    EXPECT_EQ(views[0].semantic_name, "ICCProfile");
    EXPECT_EQ(views[0].op.kind, TransferAdapterOpKind::JxlIccProfile);
    EXPECT_EQ(views[0].payload.data(), bundle.blocks[0].payload.data());

    EXPECT_EQ(views[1].semantic_kind, OiioTransferPayloadKind::Jumbf);
    EXPECT_EQ(views[1].semantic_name, "JUMBF");
    EXPECT_EQ(views[1].op.kind, TransferAdapterOpKind::JxlBox);
    EXPECT_EQ(views[1].op.box_type,
              (std::array<char, 4> { 'j', 'u', 'm', 'b' }));
    EXPECT_EQ(views[1].payload.data(), bundle.blocks[1].payload.data());

    EXPECT_EQ(views[2].semantic_kind, OiioTransferPayloadKind::C2pa);
    EXPECT_EQ(views[2].semantic_name, "C2PA");
    EXPECT_EQ(views[2].op.kind, TransferAdapterOpKind::JxlBox);
    EXPECT_EQ(views[2].op.box_type,
              (std::array<char, 4> { 'c', '2', 'p', 'a' }));
    EXPECT_EQ(views[2].payload.data(), bundle.blocks[2].payload.data());
}

TEST(OiioAdapter, BuildsTransferPayloadBatchForJpeg)
{
    PreparedTransferBundle bundle;
    bundle.target_format = TransferTargetFormat::Jpeg;

    PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    PreparedTransferBlock xmp;
    xmp.route   = "jpeg:app1-xmp";
    xmp.payload = { std::byte { 'x' }, std::byte { 'm' }, std::byte { 'p' } };
    bundle.blocks.push_back(xmp);

    EmitTransferOptions options;
    options.skip_empty_payloads = false;

    OiioTransferPayloadBatch batch;
    const EmitTransferResult result
        = build_oiio_transfer_payload_batch(bundle, &batch, options);

    ASSERT_EQ(result.status, TransferStatus::Ok);
    EXPECT_EQ(batch.contract_version, bundle.contract_version);
    EXPECT_EQ(batch.target_format, TransferTargetFormat::Jpeg);
    EXPECT_FALSE(batch.emit.skip_empty_payloads);
    ASSERT_EQ(batch.payloads.size(), 2U);

    EXPECT_EQ(batch.payloads[0].semantic_kind,
              OiioTransferPayloadKind::ExifBlob);
    EXPECT_EQ(batch.payloads[0].semantic_name, "ExifBlob");
    EXPECT_EQ(batch.payloads[0].route, "jpeg:app1-exif");
    EXPECT_EQ(batch.payloads[0].op.kind, TransferAdapterOpKind::JpegMarker);
    EXPECT_EQ(batch.payloads[0].op.jpeg_marker_code, 0xE1U);
    EXPECT_EQ(batch.payloads[0].payload, bundle.blocks[0].payload);
    EXPECT_NE(batch.payloads[0].payload.data(),
              bundle.blocks[0].payload.data());

    EXPECT_EQ(batch.payloads[1].semantic_kind,
              OiioTransferPayloadKind::XmpPacket);
    EXPECT_EQ(batch.payloads[1].semantic_name, "XMPPacket");
    EXPECT_EQ(batch.payloads[1].route, "jpeg:app1-xmp");
    EXPECT_EQ(batch.payloads[1].op.kind, TransferAdapterOpKind::JpegMarker);
    EXPECT_EQ(batch.payloads[1].op.jpeg_marker_code, 0xE1U);
    EXPECT_EQ(batch.payloads[1].payload, bundle.blocks[1].payload);
    EXPECT_NE(batch.payloads[1].payload.data(),
              bundle.blocks[1].payload.data());
}

TEST(OiioAdapter, TransferPayloadBatchOwnsBytesAfterSourceMutation)
{
    PreparedTransferBundle bundle;
    bundle.target_format = TransferTargetFormat::Jxl;

    PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x10 }, std::byte { 0x11 } };
    bundle.blocks.push_back(jumbf);

    OiioTransferPayloadBatch batch;
    ASSERT_EQ(build_oiio_transfer_payload_batch(bundle, &batch).status,
              TransferStatus::Ok);
    ASSERT_EQ(batch.payloads.size(), 1U);
    ASSERT_EQ(batch.payloads[0].payload.size(), 2U);
    EXPECT_EQ(batch.payloads[0].payload[0], std::byte { 0x10 });

    bundle.blocks[0].payload[0] = std::byte { 0x7F };

    EXPECT_EQ(batch.payloads[0].semantic_kind, OiioTransferPayloadKind::Jumbf);
    EXPECT_EQ(batch.payloads[0].payload[0], std::byte { 0x10 });
    EXPECT_EQ(bundle.blocks[0].payload[0], std::byte { 0x7F });
}

}  // namespace openmeta
