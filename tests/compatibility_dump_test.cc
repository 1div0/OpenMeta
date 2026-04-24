// SPDX-License-Identifier: Apache-2.0

#include "openmeta/compatibility_dump.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

namespace openmeta {
namespace {

    static bool contains_text(std::string_view haystack,
                              std::string_view needle) noexcept
    {
        return haystack.find(needle) != std::string_view::npos;
    }


    static MetaStore make_metadata_dump_store()
    {
        MetaStore store;
        BlockInfo info;
        info.format    = 1U;
        info.container = 2U;
        info.id        = 3U;
        const BlockId block = store.add_block(info);

        Entry make;
        make.key = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
        make.value = make_text(store.arena(), "Canon", TextEncoding::Ascii);
        make.origin.block          = block;
        make.origin.order_in_block = 7U;
        make.origin.wire_type.family = WireFamily::Tiff;
        make.origin.wire_type.code   = 2U;
        make.origin.wire_count       = 6U;
        (void)store.add_entry(make);

        Entry rating;
        rating.key = make_xmp_property_key(store.arena(),
                                           "http://ns.adobe.com/xap/1.0/",
                                           "Rating");
        rating.value                 = make_i32(-2);
        rating.flags                 = EntryFlags::Derived;
        rating.origin.block          = block;
        rating.origin.order_in_block = 8U;
        (void)store.add_entry(rating);

        const std::array<std::byte, 3> raw = {
            std::byte { 0x01 },
            std::byte { 0x02 },
            std::byte { 0x03 },
        };
        Entry bytes;
        bytes.key = make_icc_tag_key(0x64657363U);
        bytes.value
            = make_bytes(store.arena(),
                         std::span<const std::byte>(raw.data(), raw.size()));
        bytes.origin.block          = block;
        bytes.origin.order_in_block = 9U;
        (void)store.add_entry(bytes);

        store.finalize();
        return store;
    }

}  // namespace


TEST(CompatibilityDump, MetadataDumpIncludesNamesValuesTypesAndOrigins)
{
    const MetaStore store = make_metadata_dump_store();
    std::string dump;

    MetadataCompatibilityDumpOptions options;
    options.style = ExportNameStyle::FlatHost;
    ASSERT_TRUE(dump_metadata_compatibility(store, options, &dump));

    EXPECT_TRUE(contains_text(
        dump, "openmeta.compat.metadata version=1 interop_version=1 "
              "flat_host_version=1 style=\"flat_host\""));
    EXPECT_TRUE(contains_text(
        dump, "entry index=0 name=\"Make\" key_kind=\"exif_tag\" "
              "value_kind=\"text\" elem_type=\"u8\" text_encoding=\"ascii\" "
              "count=5 value=\"Canon\" origin_block=0 origin_order=7 "
              "wire_family=\"tiff\" wire_code=2 wire_count=6"));
    EXPECT_TRUE(contains_text(
        dump, "entry index=1 name=\"XMP:Rating\" key_kind=\"xmp_property\" "
              "value_kind=\"scalar\" elem_type=\"i32\""));
    EXPECT_TRUE(contains_text(dump, "value=\"-2\""));
    EXPECT_TRUE(contains_text(dump, "flag_derived=true"));
    EXPECT_TRUE(contains_text(
        dump, "entry index=2 name=\"ICC:tag:0x64657363\""));
    EXPECT_TRUE(contains_text(dump, "value=\"0x010203\""));
}


TEST(CompatibilityDump, TransferDumpIncludesPolicyBlocksAndWriteback)
{
    ExecutePreparedTransferFileResult result;
    result.prepared.file_status          = TransferFileStatus::Ok;
    result.prepared.prepare.status       = TransferStatus::Ok;
    result.prepared.bundle.target_format = TransferTargetFormat::Png;

    PreparedTransferPolicyDecision decision;
    decision.subject         = TransferPolicySubject::XmpIptcProjection;
    decision.requested       = TransferPolicyAction::Keep;
    decision.effective       = TransferPolicyAction::Rewrite;
    decision.reason          = TransferPolicyReason::ProjectedPayload;
    decision.matched_entries = 2U;
    decision.message         = "projected";
    result.prepared.bundle.policy_decisions.push_back(decision);

    PreparedTransferBlock block;
    block.kind  = TransferBlockKind::Xmp;
    block.order = 3U;
    block.route = "png:itxt-xmp";
    block.payload.resize(12U);
    result.prepared.bundle.blocks.push_back(block);

    result.execute.edit_requested       = true;
    result.execute.compile.status       = TransferStatus::Ok;
    result.execute.emit.status          = TransferStatus::Ok;
    result.execute.edit_plan_status     = TransferStatus::Ok;
    result.execute.edit_apply.status    = TransferStatus::Ok;
    result.execute.edit_input_size      = 10U;
    result.execute.edit_output_size     = 22U;
    result.xmp_sidecar_requested        = true;
    result.xmp_sidecar_status           = TransferStatus::Ok;
    result.xmp_sidecar_path             = "target.xmp";
    result.xmp_sidecar_output.resize(5U);
    result.xmp_sidecar_cleanup_requested = true;
    result.xmp_sidecar_cleanup_status    = TransferStatus::Ok;
    result.xmp_sidecar_cleanup_path      = "stale.xmp";

    PersistPreparedTransferFileResult persisted;
    persisted.status                      = TransferStatus::Ok;
    persisted.output_status               = TransferStatus::Ok;
    persisted.output_path                 = "target.png";
    persisted.output_bytes                = 22U;
    persisted.xmp_sidecar_status          = TransferStatus::Ok;
    persisted.xmp_sidecar_path            = "target.xmp";
    persisted.xmp_sidecar_bytes           = 5U;
    persisted.xmp_sidecar_cleanup_status  = TransferStatus::Ok;
    persisted.xmp_sidecar_cleanup_path    = "stale.xmp";
    persisted.xmp_sidecar_cleanup_removed = true;

    std::string dump;
    TransferCompatibilityDumpOptions options;
    ASSERT_TRUE(dump_transfer_compatibility(result, &persisted, options,
                                            &dump));

    EXPECT_TRUE(contains_text(
        dump, "openmeta.compat.transfer version=1 target_format=\"png\" "
              "file_status=\"ok\" prepare_status=\"ok\" prepared_blocks=1"));
    EXPECT_TRUE(contains_text(
        dump, "policy index=0 subject=\"xmp_iptc_projection\" "
              "requested=\"keep\" effective=\"rewrite\" "
              "reason=\"projected_payload\" matched_entries=2"));
    EXPECT_TRUE(contains_text(
        dump, "block index=0 order=3 kind=\"xmp\" route=\"png:itxt-xmp\" "
              "payload_bytes=12"));
    EXPECT_TRUE(contains_text(
        dump, "execute edit_requested=true compile_status=\"ok\" "
              "emit_status=\"ok\" edit_plan_status=\"ok\""));
    EXPECT_TRUE(contains_text(
        dump, "writeback destination_embedded_loaded=false "
              "destination_embedded_status=\"unsupported\" "
              "xmp_sidecar_requested=true xmp_sidecar_status=\"ok\" "
              "xmp_sidecar_path=\"target.xmp\" xmp_sidecar_bytes=5"));
    EXPECT_TRUE(contains_text(
        dump, "persist status=\"ok\" message=\"\" output_status=\"ok\" "
              "output_path=\"target.png\" output_bytes=22"));
    EXPECT_TRUE(contains_text(dump, "xmp_sidecar_cleanup_removed=true"));
}

}  // namespace openmeta
