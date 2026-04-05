// SPDX-License-Identifier: Apache-2.0

#include "openmeta/oiio_adapter.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <string>
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

    struct PackageReplayState final {
        std::vector<std::string> events;
        uint32_t fail_on_chunk  = std::numeric_limits<uint32_t>::max();
        uint32_t emitted_chunks = 0;
    };

    struct PayloadReplayState final {
        std::vector<std::string> events;
        uint32_t fail_on_payload  = std::numeric_limits<uint32_t>::max();
        uint32_t emitted_payloads = 0;
    };

    static TransferStatus replay_begin_package(void* user,
                                               TransferTargetFormat target,
                                               uint32_t chunk_count) noexcept
    {
        if (!user) {
            return TransferStatus::InvalidArgument;
        }
        PackageReplayState* state = static_cast<PackageReplayState*>(user);
        state->events.push_back("begin:"
                                + std::to_string(static_cast<uint32_t>(target))
                                + ":" + std::to_string(chunk_count));
        return TransferStatus::Ok;
    }

    static TransferStatus
    replay_emit_package_chunk(void* user,
                              const OiioTransferPackageView* view) noexcept
    {
        if (!user || !view) {
            return TransferStatus::InvalidArgument;
        }
        PackageReplayState* state = static_cast<PackageReplayState*>(user);
        if (state->emitted_chunks == state->fail_on_chunk) {
            return TransferStatus::Unsupported;
        }
        state->events.push_back(std::string("chunk:") + std::string(view->route)
                                + ":" + std::to_string(view->output_offset)
                                + ":" + std::to_string(view->bytes.size()));
        state->emitted_chunks += 1U;
        return TransferStatus::Ok;
    }

    static TransferStatus
    replay_end_package(void* user, TransferTargetFormat target) noexcept
    {
        if (!user) {
            return TransferStatus::InvalidArgument;
        }
        PackageReplayState* state = static_cast<PackageReplayState*>(user);
        state->events.push_back(
            "end:" + std::to_string(static_cast<uint32_t>(target)));
        return TransferStatus::Ok;
    }

    static TransferStatus replay_begin_payload(void* user,
                                               TransferTargetFormat target,
                                               uint32_t payload_count) noexcept
    {
        if (!user) {
            return TransferStatus::InvalidArgument;
        }
        PayloadReplayState* state = static_cast<PayloadReplayState*>(user);
        state->events.push_back("begin:"
                                + std::to_string(static_cast<uint32_t>(target))
                                + ":" + std::to_string(payload_count));
        return TransferStatus::Ok;
    }

    static TransferStatus
    replay_emit_payload(void* user,
                        const OiioTransferPayloadView* view) noexcept
    {
        if (!user || !view) {
            return TransferStatus::InvalidArgument;
        }
        PayloadReplayState* state = static_cast<PayloadReplayState*>(user);
        if (state->emitted_payloads == state->fail_on_payload) {
            return TransferStatus::Unsupported;
        }
        state->events.push_back(std::string("payload:")
                                + std::string(view->route) + ":"
                                + std::string(view->semantic_name) + ":"
                                + std::to_string(view->payload.size()));
        state->emitted_payloads += 1U;
        return TransferStatus::Ok;
    }

    static TransferStatus
    replay_end_payload(void* user, TransferTargetFormat target) noexcept
    {
        if (!user) {
            return TransferStatus::InvalidArgument;
        }
        PayloadReplayState* state = static_cast<PayloadReplayState*>(user);
        state->events.push_back(
            "end:" + std::to_string(static_cast<uint32_t>(target)));
        return TransferStatus::Ok;
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
    ASSERT_EQ(safe_status, InteropSafetyStatus::Ok);
    EXPECT_TRUE(safe_error.message.empty());

    const OiioAttribute* safe_bmff = find_attr(safe_attrs, "bmff:meta.test");
    ASSERT_NE(safe_bmff, nullptr);
    EXPECT_FALSE(safe_bmff->value.empty());

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

TEST(OiioAdapter, SafeExportAllowsStandardExifByteTags)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<std::byte, 4> exif_version = {
        std::byte { '0' },
        std::byte { '2' },
        std::byte { '3' },
        std::byte { '1' },
    };
    Entry exif_ver;
    exif_ver.key          = make_exif_tag_key(store.arena(), "exififd", 0x9000);
    exif_ver.value        = make_bytes(store.arena(),
                                       std::span<const std::byte>(exif_version.data(),
                                                                  exif_version.size()));
    exif_ver.origin.block = block;
    exif_ver.origin.order_in_block = 0;
    (void)store.add_entry(exif_ver);

    const std::array<std::byte, 4> components = {
        std::byte { 1 },
        std::byte { 2 },
        std::byte { 3 },
        std::byte { 0 },
    };
    Entry components_cfg;
    components_cfg.key = make_exif_tag_key(store.arena(), "exififd", 0x9101);
    components_cfg.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(components.data(),
                                                components.size()));
    components_cfg.origin.block          = block;
    components_cfg.origin.order_in_block = 1;
    (void)store.add_entry(components_cfg);

    const std::array<std::byte, 4> flashpix_version = {
        std::byte { '0' },
        std::byte { '1' },
        std::byte { '0' },
        std::byte { '0' },
    };
    Entry flashpix_ver;
    flashpix_ver.key = make_exif_tag_key(store.arena(), "exififd", 0xA000);
    flashpix_ver.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(flashpix_version.data(),
                                                flashpix_version.size()));
    flashpix_ver.origin.block          = block;
    flashpix_ver.origin.order_in_block = 2;
    (void)store.add_entry(flashpix_ver);

    const std::array<std::byte, 4> gps_version = {
        std::byte { 2 },
        std::byte { 3 },
        std::byte { 0 },
        std::byte { 0 },
    };
    Entry gps_ver;
    gps_ver.key          = make_exif_tag_key(store.arena(), "gpsifd", 0x0000);
    gps_ver.value        = make_bytes(store.arena(),
                                      std::span<const std::byte>(gps_version.data(),
                                                                 gps_version.size()));
    gps_ver.origin.block = block;
    gps_ver.origin.order_in_block = 3;
    (void)store.add_entry(gps_ver);

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

    const OiioAttribute* exif_ver_attr = find_attr(safe_attrs,
                                                   "Exif:ExifVersion");
    ASSERT_NE(exif_ver_attr, nullptr);
    EXPECT_EQ(exif_ver_attr->value, "0231");

    const OiioAttribute* components_attr
        = find_attr(safe_attrs, "Exif:ComponentsConfiguration");
    ASSERT_NE(components_attr, nullptr);
    EXPECT_EQ(components_attr->value, "[1, 2, 3, 0]");

    const OiioAttribute* flashpix_attr = find_attr(safe_attrs,
                                                   "Exif:FlashpixVersion");
    ASSERT_NE(flashpix_attr, nullptr);
    EXPECT_EQ(flashpix_attr->value, "0100");

    const OiioAttribute* gps_ver_attr = find_attr(safe_attrs,
                                                  "GPS:GPSVersionID");
    ASSERT_NE(gps_ver_attr, nullptr);
    EXPECT_EQ(gps_ver_attr->value, "[2, 3, 0, 0]");

    std::vector<OiioTypedAttribute> safe_typed_attrs;
    const InteropSafetyStatus safe_typed_status
        = collect_oiio_attributes_typed_safe(store, &safe_typed_attrs, request,
                                             &safe_error);
    ASSERT_EQ(safe_typed_status, InteropSafetyStatus::Ok);

    const OiioTypedAttribute* typed_exif_ver
        = find_typed_attr(safe_typed_attrs, "Exif:ExifVersion");
    ASSERT_NE(typed_exif_ver, nullptr);
    EXPECT_EQ(typed_exif_ver->value.kind, MetaValueKind::Text);
    EXPECT_EQ(typed_exif_ver->value.text_encoding, TextEncoding::Utf8);
}

TEST(OiioAdapter, SafeExportDecodesByteBackedTextValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<std::byte, 6> make_raw = {
        std::byte { 'C' }, std::byte { 'a' }, std::byte { 'n' },
        std::byte { 'o' }, std::byte { 'n' }, std::byte { 0 },
    };
    Entry make;
    make.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    make.value        = make_bytes(store.arena(),
                                   std::span<const std::byte>(make_raw.data(),
                                                              make_raw.size()));
    make.origin.block = block;
    make.origin.order_in_block = 0;
    (void)store.add_entry(make);

    const std::array<std::byte, 13> user_comment_raw = {
        std::byte { 'A' }, std::byte { 'S' }, std::byte { 'C' },
        std::byte { 'I' }, std::byte { 'I' }, std::byte { 0 },
        std::byte { 0 },   std::byte { 0 },   std::byte { 'h' },
        std::byte { 'e' }, std::byte { 'l' }, std::byte { 'l' },
        std::byte { 'o' },
    };
    Entry user_comment;
    user_comment.key = make_exif_tag_key(store.arena(), "exififd", 0x9286);
    user_comment.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(user_comment_raw.data(),
                                                user_comment_raw.size()));
    user_comment.origin.block          = block;
    user_comment.origin.order_in_block = 1;
    (void)store.add_entry(user_comment);

    store.finalize();

    OiioAdapterRequest request;
    request.max_value_bytes = 256U;

    InteropSafetyError safe_error;
    std::vector<OiioAttribute> safe_attrs;
    const InteropSafetyStatus safe_status
        = collect_oiio_attributes_safe(store, &safe_attrs, request,
                                       &safe_error);
    ASSERT_EQ(safe_status, InteropSafetyStatus::Ok);

    const OiioAttribute* make_attr = find_attr(safe_attrs, "Make");
    ASSERT_NE(make_attr, nullptr);
    EXPECT_EQ(make_attr->value, "Canon");

    const OiioAttribute* user_comment_attr = find_attr(safe_attrs,
                                                       "Exif:UserComment");
    ASSERT_NE(user_comment_attr, nullptr);
    EXPECT_EQ(user_comment_attr->value, "hello");

    std::vector<OiioTypedAttribute> typed_attrs;
    const InteropSafetyStatus typed_status
        = collect_oiio_attributes_typed_safe(store, &typed_attrs, request,
                                             &safe_error);
    ASSERT_EQ(typed_status, InteropSafetyStatus::Ok);

    const OiioTypedAttribute* typed_make = find_typed_attr(typed_attrs, "Make");
    ASSERT_NE(typed_make, nullptr);
    EXPECT_EQ(typed_make->value.kind, MetaValueKind::Text);
    EXPECT_EQ(typed_make->value.text_encoding, TextEncoding::Utf8);

    const OiioTypedAttribute* typed_user_comment
        = find_typed_attr(typed_attrs, "Exif:UserComment");
    ASSERT_NE(typed_user_comment, nullptr);
    EXPECT_EQ(typed_user_comment->value.kind, MetaValueKind::Text);
    EXPECT_EQ(typed_user_comment->value.text_encoding, TextEncoding::Utf8);
}

TEST(OiioAdapter, SafeExportHexEncodesInvalidTextValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const char invalid_make_bytes[] = { 'C', 'a', 'n', 'o', 'n', '\x01' };
    Entry make;
    make.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    make.value        = make_text(store.arena(),
                                  std::string_view(invalid_make_bytes,
                                                   sizeof(invalid_make_bytes)),
                                  TextEncoding::Ascii);
    make.origin.block = block;
    make.origin.order_in_block = 0;
    (void)store.add_entry(make);

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
    EXPECT_EQ(make_attr->value, "0x43616E6F6E01");

    std::vector<OiioTypedAttribute> typed_attrs;
    const InteropSafetyStatus typed_status
        = collect_oiio_attributes_typed_safe(store, &typed_attrs, request,
                                             &safe_error);
    EXPECT_EQ(typed_status, InteropSafetyStatus::UnsafeData);
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

TEST(OiioAdapter, CollectsTransferPayloadViewsForWebp)
{
    PreparedTransferBundle bundle;
    bundle.target_format = TransferTargetFormat::Webp;

    PreparedTransferBlock exif;
    exif.route   = "webp:chunk-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    PreparedTransferBlock icc;
    icc.route   = "webp:chunk-iccp";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 },
                    std::byte { 0x43 } };
    bundle.blocks.push_back(icc);

    PreparedTransferBlock c2pa;
    c2pa.route   = "webp:chunk-c2pa";
    c2pa.payload = { std::byte { 0x10 }, std::byte { 0x11 } };
    bundle.blocks.push_back(c2pa);

    std::vector<OiioTransferPayloadView> views;
    const EmitTransferResult result
        = collect_oiio_transfer_payload_views(bundle, &views);

    ASSERT_EQ(result.status, TransferStatus::Ok);
    ASSERT_EQ(views.size(), 3U);

    EXPECT_EQ(views[0].semantic_kind, OiioTransferPayloadKind::ExifBlob);
    EXPECT_EQ(views[0].semantic_name, "ExifBlob");
    EXPECT_EQ(views[0].op.kind, TransferAdapterOpKind::WebpChunk);
    EXPECT_EQ(views[0].op.chunk_type,
              (std::array<char, 4> { 'E', 'X', 'I', 'F' }));
    EXPECT_EQ(views[0].payload.data(), bundle.blocks[0].payload.data());

    EXPECT_EQ(views[1].semantic_kind, OiioTransferPayloadKind::IccProfile);
    EXPECT_EQ(views[1].semantic_name, "ICCProfile");
    EXPECT_EQ(views[1].op.kind, TransferAdapterOpKind::WebpChunk);
    EXPECT_EQ(views[1].op.chunk_type,
              (std::array<char, 4> { 'I', 'C', 'C', 'P' }));
    EXPECT_EQ(views[1].payload.data(), bundle.blocks[1].payload.data());

    EXPECT_EQ(views[2].semantic_kind, OiioTransferPayloadKind::C2pa);
    EXPECT_EQ(views[2].semantic_name, "C2PA");
    EXPECT_EQ(views[2].op.kind, TransferAdapterOpKind::WebpChunk);
    EXPECT_EQ(views[2].op.chunk_type,
              (std::array<char, 4> { 'C', '2', 'P', 'A' }));
    EXPECT_EQ(views[2].payload.data(), bundle.blocks[2].payload.data());
}

TEST(OiioAdapter, CollectsTransferPayloadViewsForBmff)
{
    PreparedTransferBundle bundle;
    bundle.target_format = TransferTargetFormat::Heif;

    PreparedTransferBlock exif;
    exif.route   = "bmff:item-exif";
    exif.payload = { std::byte { 0x00 }, std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    PreparedTransferBlock xmp;
    xmp.route   = "bmff:item-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' } };
    bundle.blocks.push_back(xmp);

    PreparedTransferBlock c2pa;
    c2pa.route   = "bmff:item-c2pa";
    c2pa.payload = { std::byte { 0x10 }, std::byte { 0x11 } };
    bundle.blocks.push_back(c2pa);

    PreparedTransferBlock icc;
    icc.route   = "bmff:property-colr-icc";
    icc.payload = { std::byte { 'p' }, std::byte { 'r' }, std::byte { 'o' },
                    std::byte { 'f' }, std::byte { 0x20 } };
    bundle.blocks.push_back(icc);

    std::vector<OiioTransferPayloadView> views;
    const EmitTransferResult result
        = collect_oiio_transfer_payload_views(bundle, &views);

    ASSERT_EQ(result.status, TransferStatus::Ok);
    ASSERT_EQ(views.size(), 4U);

    EXPECT_EQ(views[0].semantic_kind, OiioTransferPayloadKind::ExifBlob);
    EXPECT_EQ(views[0].op.kind, TransferAdapterOpKind::BmffItem);
    EXPECT_EQ(views[0].op.bmff_item_type, fourcc('E', 'x', 'i', 'f'));
    EXPECT_FALSE(views[0].op.bmff_mime_xmp);

    EXPECT_EQ(views[1].semantic_kind, OiioTransferPayloadKind::XmpPacket);
    EXPECT_EQ(views[1].op.kind, TransferAdapterOpKind::BmffItem);
    EXPECT_EQ(views[1].op.bmff_item_type, fourcc('m', 'i', 'm', 'e'));
    EXPECT_TRUE(views[1].op.bmff_mime_xmp);

    EXPECT_EQ(views[2].semantic_kind, OiioTransferPayloadKind::C2pa);
    EXPECT_EQ(views[2].op.kind, TransferAdapterOpKind::BmffItem);
    EXPECT_EQ(views[2].op.bmff_item_type, fourcc('c', '2', 'p', 'a'));
    EXPECT_FALSE(views[2].op.bmff_mime_xmp);

    EXPECT_EQ(views[3].semantic_kind, OiioTransferPayloadKind::IccProfile);
    EXPECT_EQ(views[3].op.kind, TransferAdapterOpKind::BmffProperty);
    EXPECT_EQ(views[3].op.bmff_property_type, fourcc('c', 'o', 'l', 'r'));
    EXPECT_EQ(views[3].op.bmff_property_subtype, fourcc('p', 'r', 'o', 'f'));
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

TEST(OiioAdapter, CollectsTransferPayloadViewsFromPersistedBatch)
{
    PreparedTransferBundle bundle;
    bundle.target_format = TransferTargetFormat::Jxl;

    PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 } };
    bundle.blocks.push_back(jumbf);

    PreparedTransferPayloadBatch built_batch;
    ASSERT_EQ(build_prepared_transfer_payload_batch(bundle, &built_batch).status,
              TransferStatus::Ok);

    std::vector<std::byte> encoded;
    ASSERT_EQ(
        serialize_prepared_transfer_payload_batch(built_batch, &encoded).status,
        TransferStatus::Ok);

    PreparedTransferPayloadBatch decoded_batch;
    ASSERT_EQ(deserialize_prepared_transfer_payload_batch(
                  std::span<const std::byte>(encoded.data(), encoded.size()),
                  &decoded_batch)
                  .status,
              TransferStatus::Ok);

    std::vector<OiioTransferPayloadView> views;
    const EmitTransferResult result
        = collect_oiio_transfer_payload_views(decoded_batch, &views);

    ASSERT_EQ(result.status, TransferStatus::Ok);
    ASSERT_EQ(views.size(), 1U);
    EXPECT_EQ(views[0].semantic_kind, OiioTransferPayloadKind::Jumbf);
    EXPECT_EQ(views[0].semantic_name, "JUMBF");
    EXPECT_EQ(views[0].route, "jxl:box-jumb");
    EXPECT_EQ(views[0].op.kind, TransferAdapterOpKind::JxlBox);
    ASSERT_EQ(views[0].payload.size(), 4U);
    EXPECT_EQ(views[0].payload[0], std::byte { 0x00 });
}

TEST(OiioAdapter, ReplaysTransferPayloadBatchInOrder)
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

    PreparedTransferPayloadBatch batch;
    ASSERT_EQ(build_prepared_transfer_payload_batch(bundle, &batch).status,
              TransferStatus::Ok);

    PayloadReplayState state;
    OiioTransferPayloadReplayCallbacks callbacks;
    callbacks.begin_batch  = replay_begin_payload;
    callbacks.emit_payload = replay_emit_payload;
    callbacks.end_batch    = replay_end_payload;
    callbacks.user         = &state;

    const OiioTransferPayloadReplayResult replay
        = replay_oiio_transfer_payload_batch(batch, callbacks);

    ASSERT_EQ(replay.status, TransferStatus::Ok);
    EXPECT_EQ(replay.replayed, 2U);
    ASSERT_EQ(state.events.size(), 4U);
    EXPECT_EQ(state.events[0], "begin:0:2");
    EXPECT_EQ(state.events[1], "payload:jpeg:app1-exif:ExifBlob:2");
    EXPECT_EQ(state.events[2], "payload:jpeg:app1-xmp:XMPPacket:3");
    EXPECT_EQ(state.events[3], "end:0");
}

TEST(OiioAdapter, CollectsTransferPackageViewsForJpeg)
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

    PreparedTransferPackagePlan package;
    ASSERT_EQ(build_prepared_transfer_emit_package(bundle, &package).status,
              TransferStatus::Ok);

    const std::vector<std::byte> empty_input;
    PreparedTransferPackageBatch batch;
    ASSERT_EQ(build_prepared_transfer_package_batch(
                  std::span<const std::byte>(empty_input.data(),
                                             empty_input.size()),
                  bundle, package, &batch)
                  .status,
              TransferStatus::Ok);

    std::vector<OiioTransferPackageView> views;
    const EmitTransferResult result
        = collect_oiio_transfer_package_views(batch, &views);

    ASSERT_EQ(result.status, TransferStatus::Ok);
    ASSERT_EQ(views.size(), 2U);

    EXPECT_EQ(views[0].semantic_kind, OiioTransferPayloadKind::ExifBlob);
    EXPECT_EQ(views[0].semantic_name, "ExifBlob");
    EXPECT_EQ(views[0].route, "jpeg:app1-exif");
    EXPECT_EQ(views[0].package_kind,
              TransferPackageChunkKind::PreparedTransferBlock);
    EXPECT_EQ(views[0].output_offset, 0U);
    EXPECT_EQ(views[0].jpeg_marker_code, 0U);
    ASSERT_EQ(views[0].bytes.size(), 6U);
    EXPECT_EQ(views[0].bytes[0], std::byte { 0xFF });
    EXPECT_EQ(views[0].bytes[1], std::byte { 0xE1 });

    EXPECT_EQ(views[1].semantic_kind, OiioTransferPayloadKind::XmpPacket);
    EXPECT_EQ(views[1].semantic_name, "XMPPacket");
    EXPECT_EQ(views[1].route, "jpeg:app1-xmp");
    EXPECT_EQ(views[1].package_kind,
              TransferPackageChunkKind::PreparedTransferBlock);
    EXPECT_EQ(views[1].output_offset, 6U);
    ASSERT_EQ(views[1].bytes.size(), 7U);
    EXPECT_EQ(views[1].bytes[0], std::byte { 0xFF });
    EXPECT_EQ(views[1].bytes[1], std::byte { 0xE1 });
}

TEST(OiioAdapter, TransferPackageViewsSurviveSerializedBatchRoundTrip)
{
    PreparedTransferBundle bundle;
    bundle.target_format = TransferTargetFormat::Jxl;

    PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 } };
    bundle.blocks.push_back(jumbf);

    PreparedTransferPackagePlan package;
    ASSERT_EQ(build_prepared_transfer_emit_package(bundle, &package).status,
              TransferStatus::Ok);

    const std::vector<std::byte> empty_input;
    PreparedTransferPackageBatch built_batch;
    ASSERT_EQ(build_prepared_transfer_package_batch(
                  std::span<const std::byte>(empty_input.data(),
                                             empty_input.size()),
                  bundle, package, &built_batch)
                  .status,
              TransferStatus::Ok);

    std::vector<std::byte> encoded;
    ASSERT_EQ(
        serialize_prepared_transfer_package_batch(built_batch, &encoded).status,
        TransferStatus::Ok);

    PreparedTransferPackageBatch decoded_batch;
    ASSERT_EQ(deserialize_prepared_transfer_package_batch(
                  std::span<const std::byte>(encoded.data(), encoded.size()),
                  &decoded_batch)
                  .status,
              TransferStatus::Ok);

    std::vector<OiioTransferPackageView> views;
    const EmitTransferResult result
        = collect_oiio_transfer_package_views(decoded_batch, &views);

    ASSERT_EQ(result.status, TransferStatus::Ok);
    ASSERT_EQ(views.size(), 1U);
    EXPECT_EQ(views[0].semantic_kind, OiioTransferPayloadKind::Jumbf);
    EXPECT_EQ(views[0].semantic_name, "JUMBF");
    EXPECT_EQ(views[0].route, "jxl:box-jumb");
    EXPECT_EQ(views[0].package_kind,
              TransferPackageChunkKind::PreparedTransferBlock);
    ASSERT_EQ(views[0].bytes.size(), 12U);
    EXPECT_EQ(views[0].bytes[4], std::byte { 'j' });
    EXPECT_EQ(views[0].bytes[5], std::byte { 'u' });
    EXPECT_EQ(views[0].bytes[6], std::byte { 'm' });
    EXPECT_EQ(views[0].bytes[7], std::byte { 'b' });
}

TEST(OiioAdapter, ReplaysTransferPackageBatchInOutputOrder)
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

    PreparedTransferPackagePlan package;
    ASSERT_EQ(build_prepared_transfer_emit_package(bundle, &package).status,
              TransferStatus::Ok);

    PreparedTransferPackageBatch batch;
    const std::vector<std::byte> empty_input;
    ASSERT_EQ(build_prepared_transfer_package_batch(
                  std::span<const std::byte>(empty_input.data(),
                                             empty_input.size()),
                  bundle, package, &batch)
                  .status,
              TransferStatus::Ok);

    PackageReplayState state;
    OiioTransferPackageReplayCallbacks callbacks;
    callbacks.begin_batch = replay_begin_package;
    callbacks.emit_chunk  = replay_emit_package_chunk;
    callbacks.end_batch   = replay_end_package;
    callbacks.user        = &state;

    const OiioTransferPackageReplayResult replay
        = replay_oiio_transfer_package_batch(batch, callbacks);

    ASSERT_EQ(replay.status, TransferStatus::Ok);
    EXPECT_EQ(replay.replayed, 2U);
    ASSERT_EQ(state.events.size(), 4U);
    EXPECT_EQ(state.events[0], "begin:0:2");
    EXPECT_EQ(state.events[1], "chunk:jpeg:app1-exif:0:6");
    EXPECT_EQ(state.events[2], "chunk:jpeg:app1-xmp:6:7");
    EXPECT_EQ(state.events[3], "end:0");
}

TEST(OiioAdapter, ReplayTransferPackageBatchReportsCallbackFailure)
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

    PreparedTransferPackagePlan package;
    ASSERT_EQ(build_prepared_transfer_emit_package(bundle, &package).status,
              TransferStatus::Ok);

    PreparedTransferPackageBatch batch;
    const std::vector<std::byte> empty_input;
    ASSERT_EQ(build_prepared_transfer_package_batch(
                  std::span<const std::byte>(empty_input.data(),
                                             empty_input.size()),
                  bundle, package, &batch)
                  .status,
              TransferStatus::Ok);

    PackageReplayState state;
    state.fail_on_chunk = 1U;

    OiioTransferPackageReplayCallbacks callbacks;
    callbacks.begin_batch = replay_begin_package;
    callbacks.emit_chunk  = replay_emit_package_chunk;
    callbacks.end_batch   = replay_end_package;
    callbacks.user        = &state;

    const OiioTransferPackageReplayResult replay
        = replay_oiio_transfer_package_batch(batch, callbacks);

    ASSERT_EQ(replay.status, TransferStatus::Unsupported);
    EXPECT_EQ(replay.code, EmitTransferCode::BackendWriteFailed);
    EXPECT_EQ(replay.replayed, 1U);
    EXPECT_EQ(replay.failed_chunk_index, 1U);
    ASSERT_EQ(state.events.size(), 2U);
    EXPECT_EQ(state.events[0], "begin:0:2");
    EXPECT_EQ(state.events[1], "chunk:jpeg:app1-exif:0:6");
}

}  // namespace openmeta
