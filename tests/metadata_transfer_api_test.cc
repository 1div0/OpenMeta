#include "openmeta/metadata_transfer.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

class FakeJpegEmitter final : public openmeta::JpegTransferEmitter {
public:
    openmeta::TransferStatus
    write_app_marker(uint8_t marker_code,
                     std::span<const std::byte> payload) noexcept override
    {
        calls.emplace_back(marker_code, payload.size());
        if (fail_after_calls != 0U && calls.size() >= fail_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    std::vector<std::pair<uint8_t, size_t>> calls;
    size_t fail_after_calls = 0;
    openmeta::TransferStatus fail_status
        = openmeta::TransferStatus::InternalError;
};

class FakeTiffEmitter final : public openmeta::TiffTransferEmitter {
public:
    openmeta::TransferStatus set_tag_u32(uint16_t tag,
                                         uint32_t value) noexcept override
    {
        u32_calls.emplace_back(tag, value);
        if (fail_set_after_calls != 0U
            && (u32_calls.size() + bytes_calls.size())
                   >= fail_set_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus
    set_tag_bytes(uint16_t tag,
                  std::span<const std::byte> payload) noexcept override
    {
        bytes_calls.emplace_back(tag, payload.size());
        if (fail_set_after_calls != 0U
            && (u32_calls.size() + bytes_calls.size())
                   >= fail_set_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus
    commit_exif_directory(uint64_t* out_ifd_offset) noexcept override
    {
        commit_calls += 1U;
        if (out_ifd_offset) {
            *out_ifd_offset = 0U;
        }
        if (fail_commit) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    std::vector<std::pair<uint16_t, uint32_t>> u32_calls;
    std::vector<std::pair<uint16_t, size_t>> bytes_calls;
    size_t fail_set_after_calls = 0;
    bool fail_commit            = false;
    size_t commit_calls         = 0;
    openmeta::TransferStatus fail_status
        = openmeta::TransferStatus::InternalError;
};

static uint16_t
read_u16le(std::span<const std::byte> bytes, size_t off) noexcept
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[off + 0])) << 0)
        | (static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[off + 1]))
           << 8));
}

static uint32_t
read_u32le(std::span<const std::byte> bytes, size_t off) noexcept
{
    return static_cast<uint32_t>(
        (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 0])) << 0)
        | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 1])) << 8)
        | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 2])) << 16)
        | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 3]))
           << 24));
}

static std::vector<std::byte>
ascii_z(const char* s)
{
    std::vector<std::byte> out;
    if (!s) {
        out.push_back(std::byte { 0x00 });
        return out;
    }
    const size_t n = std::strlen(s);
    out.reserve(n + 1U);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<std::byte>(static_cast<uint8_t>(s[i])));
    }
    out.push_back(std::byte { 0x00 });
    return out;
}

static void
append_u16be(std::vector<std::byte>* out, uint16_t v)
{
    out->push_back(static_cast<std::byte>((v >> 8U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 0U) & 0xFFU));
}

static std::vector<std::byte>
make_jpeg_with_segment(uint8_t marker, std::span<const std::byte> payload)
{
    std::vector<std::byte> out;
    out.push_back(std::byte { 0xFF });
    out.push_back(std::byte { 0xD8 });
    out.push_back(std::byte { 0xFF });
    out.push_back(static_cast<std::byte>(marker));
    append_u16be(&out, static_cast<uint16_t>(payload.size() + 2U));
    out.insert(out.end(), payload.begin(), payload.end());
    out.push_back(std::byte { 0xFF });
    out.push_back(std::byte { 0xD9 });
    return out;
}

static std::vector<std::byte>
make_minimal_tiff_little_endian()
{
    return {
        std::byte { 'I' },  std::byte { 'I' },  std::byte { 0x2A },
        std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 },
    };
}

TEST(MetadataTransferApi, ContractDefaultsAreStable)
{
    openmeta::PreparedTransferBundle bundle;
    EXPECT_EQ(bundle.contract_version,
              openmeta::kMetadataTransferContractVersion);
    EXPECT_EQ(bundle.target_format, openmeta::TransferTargetFormat::Jpeg);
    EXPECT_TRUE(bundle.profile.preserve_makernotes);
    EXPECT_TRUE(bundle.profile.preserve_jumbf);
    EXPECT_TRUE(bundle.profile.preserve_c2pa);
    EXPECT_TRUE(bundle.profile.allow_time_patch);
    EXPECT_TRUE(bundle.blocks.empty());
    EXPECT_TRUE(bundle.time_patch_map.empty());
}

TEST(MetadataTransferApi, PreparedBlockCarriesRouteAndPayload)
{
    openmeta::PreparedTransferBlock b;
    b.kind     = openmeta::TransferBlockKind::Exif;
    b.order    = 7U;
    b.route    = "jpeg:app1-exif";
    b.box_type = { 'E', 'x', 'i', 'f' };
    b.payload  = { std::byte { 0x45 }, std::byte { 0x78 }, std::byte { 0x69 },
                   std::byte { 0x66 } };

    EXPECT_EQ(b.kind, openmeta::TransferBlockKind::Exif);
    EXPECT_EQ(b.order, 7U);
    EXPECT_EQ(b.route, "jpeg:app1-exif");
    ASSERT_EQ(b.payload.size(), 4U);
    EXPECT_EQ(b.payload[0], std::byte { 0x45 });
    EXPECT_EQ(b.box_type[0], 'E');
}

TEST(MetadataTransferApi, PrepareFileRejectsEmptyPath)
{
    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file("");
    EXPECT_EQ(result.file_status,
              openmeta::TransferFileStatus::InvalidArgument);
    EXPECT_EQ(result.code, openmeta::PrepareTransferFileCode::EmptyPath);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.prepare.code, openmeta::PrepareTransferCode::None);
    EXPECT_EQ(result.prepare.errors, 1U);
}

TEST(MetadataTransferApi, PrepareBuildsJpegXmpApp1Block)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key                   = openmeta::make_xmp_property_key(store.arena(),
                                                              "http://purl.org/dc/elements/1.1/",
                                                              "title");
    e.value                 = openmeta::make_text(store.arena(), "OpenMeta",
                                                  openmeta::TextEncoding::Utf8);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(result.warnings, 0U);
    ASSERT_EQ(bundle.target_format, openmeta::TransferTargetFormat::Jpeg);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    ASSERT_GE(bundle.blocks[0].payload.size(), 30U);
    EXPECT_EQ(bundle.blocks[0].payload[0], std::byte { 'h' });
}

TEST(MetadataTransferApi, PrepareBuildsJpegExifApp1Block)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x010FU);
    e.value = openmeta::make_text(store.arena(), "Vendor",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-exif");

    const std::span<const std::byte> p(bundle.blocks[0].payload.data(),
                                       bundle.blocks[0].payload.size());
    ASSERT_GE(p.size(), 20U);
    EXPECT_EQ(p[0], std::byte { 'E' });
    EXPECT_EQ(p[1], std::byte { 'x' });
    EXPECT_EQ(p[2], std::byte { 'i' });
    EXPECT_EQ(p[3], std::byte { 'f' });
    EXPECT_EQ(p[6], std::byte { 'I' });
    EXPECT_EQ(p[7], std::byte { 'I' });
    EXPECT_EQ(read_u16le(p, 8U), 42U);

    const uint32_t ifd0_off = read_u32le(p, 10U);
    const size_t ifd0_pos   = 6U + static_cast<size_t>(ifd0_off);
    ASSERT_LT(ifd0_pos + 2U, p.size());
    EXPECT_EQ(read_u16le(p, ifd0_pos), 1U);
    ASSERT_LT(ifd0_pos + 14U, p.size());
    EXPECT_EQ(read_u16le(p, ifd0_pos + 2U), 0x010FU);
    EXPECT_EQ(read_u16le(p, ifd0_pos + 4U), 2U);
}

TEST(MetadataTransferApi, PrepareBuildsTiffExifTransferBlockAndTimePatchSlots)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e0;
    e0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e0.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                   openmeta::TextEncoding::Ascii);
    e0.origin.block          = block;
    e0.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e0), openmeta::kInvalidEntryId);

    openmeta::Entry e1;
    e1.key   = openmeta::make_exif_tag_key(store.arena(), "exififd", 0x9003U);
    e1.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                   openmeta::TextEncoding::Ascii);
    e1.origin.block          = block;
    e1.origin.order_in_block = 1;
    ASSERT_NE(store.add_entry(e1), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.target_format, openmeta::TransferTargetFormat::Tiff);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "tiff:ifd-exif-app1");
    EXPECT_FALSE(bundle.time_patch_map.empty());
    for (size_t i = 0; i < bundle.time_patch_map.size(); ++i) {
        EXPECT_EQ(bundle.time_patch_map[i].block_index, 0U);
    }
}

TEST(MetadataTransferApi, PrepareBuildsTimePatchSlotsForExifDateFields)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e0;
    e0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e0.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                   openmeta::TextEncoding::Ascii);
    e0.origin.block          = block;
    e0.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e0), openmeta::kInvalidEntryId);

    openmeta::Entry e1;
    e1.key   = openmeta::make_exif_tag_key(store.arena(), "exififd", 0x9003U);
    e1.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                   openmeta::TextEncoding::Ascii);
    e1.origin.block          = block;
    e1.origin.order_in_block = 1;
    ASSERT_NE(store.add_entry(e1), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    bool found_ifd0_dt   = false;
    bool found_exififd_o = false;
    for (const openmeta::TimePatchSlot& slot : bundle.time_patch_map) {
        if (slot.field == openmeta::TimePatchField::DateTime) {
            found_ifd0_dt = true;
            EXPECT_EQ(slot.block_index, 0U);
            EXPECT_EQ(slot.width, 20U);
        }
        if (slot.field == openmeta::TimePatchField::DateTimeOriginal) {
            found_exififd_o = true;
            EXPECT_EQ(slot.block_index, 0U);
            EXPECT_EQ(slot.width, 20U);
        }
    }
    EXPECT_TRUE(found_ifd0_dt);
    EXPECT_TRUE(found_exififd_o);
}

TEST(MetadataTransferApi, ApplyTimePatchesUpdatesPreparedPayload)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e0;
    e0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e0.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                   openmeta::TextEncoding::Ascii);
    e0.origin.block          = block;
    e0.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e0), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    openmeta::TimePatchSlot dt_slot;
    bool have_slot = false;
    for (const openmeta::TimePatchSlot& slot : bundle.time_patch_map) {
        if (slot.field == openmeta::TimePatchField::DateTime) {
            dt_slot   = slot;
            have_slot = true;
            break;
        }
    }
    ASSERT_TRUE(have_slot);
    ASSERT_EQ(dt_slot.width, 20U);

    const std::vector<std::byte> v = ascii_z("2030:12:31 23:59:59");
    ASSERT_EQ(v.size(), 20U);
    openmeta::TimePatchUpdate u;
    u.field = openmeta::TimePatchField::DateTime;
    u.value = v;
    const std::array<openmeta::TimePatchUpdate, 1> updates = { u };

    const openmeta::ApplyTimePatchResult patch_result
        = openmeta::apply_time_patches(&bundle, updates);
    EXPECT_EQ(patch_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(patch_result.errors, 0U);
    EXPECT_GE(patch_result.patched_slots, 1U);

    const std::vector<std::byte>& payload
        = bundle.blocks[dt_slot.block_index].payload;
    ASSERT_LE(static_cast<size_t>(dt_slot.byte_offset + dt_slot.width),
              payload.size());
    EXPECT_EQ(std::memcmp(payload.data() + dt_slot.byte_offset, v.data(),
                          v.size()),
              0);
}

TEST(MetadataTransferApi, ApplyTimePatchesStrictWidthMismatchFails)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e0;
    e0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e0.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                   openmeta::TextEncoding::Ascii);
    e0.origin.block          = block;
    e0.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e0), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(result.status, openmeta::TransferStatus::Ok);

    openmeta::TimePatchUpdate u;
    u.field = openmeta::TimePatchField::DateTime;
    u.value = ascii_z("2030:12:31 23:59");  // too short
    const std::array<openmeta::TimePatchUpdate, 1> updates = { u };

    openmeta::ApplyTimePatchOptions patch_opts;
    patch_opts.strict_width = true;
    const openmeta::ApplyTimePatchResult patch_result
        = openmeta::apply_time_patches(&bundle, updates, patch_opts);
    EXPECT_EQ(patch_result.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_GT(patch_result.errors, 0U);
    EXPECT_EQ(patch_result.patched_slots, 0U);
}

TEST(MetadataTransferApi, PlanJpegEditAutoFallsBackToMetadataRewrite)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    const std::vector<std::byte> input = {
        std::byte { 0xFF },
        std::byte { 0xD8 },
        std::byte { 0xFF },
        std::byte { 0xD9 },
    };
    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    EXPECT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(plan.selected_mode, openmeta::JpegEditMode::MetadataRewrite);
    EXPECT_FALSE(plan.in_place_possible);
    EXPECT_EQ(plan.emitted_segments, 1U);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::Ok);
    ASSERT_GT(out.size(), input.size());
    EXPECT_EQ(out[0], std::byte { 0xFF });
    EXPECT_EQ(out[1], std::byte { 0xD8 });
    EXPECT_EQ(out[2], std::byte { 0xFF });
    EXPECT_EQ(out[3], std::byte { 0xE1 });
}

TEST(MetadataTransferApi, PlanJpegEditAutoSelectsInPlaceWhenPossible)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    ASSERT_EQ(bundle.blocks[0].route, "jpeg:app1-exif");

    const std::vector<std::byte> input = make_jpeg_with_segment(
        0xE1U, std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                          bundle.blocks[0].payload.size()));

    openmeta::TimePatchUpdate u;
    u.field = openmeta::TimePatchField::DateTime;
    u.value = ascii_z("2033:03:04 05:06:07");
    const std::array<openmeta::TimePatchUpdate, 1> updates = { u };
    const openmeta::ApplyTimePatchResult patched
        = openmeta::apply_time_patches(&bundle, updates);
    ASSERT_EQ(patched.status, openmeta::TransferStatus::Ok);

    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    EXPECT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(plan.selected_mode, openmeta::JpegEditMode::InPlace);
    EXPECT_TRUE(plan.in_place_possible);
    EXPECT_EQ(plan.output_size, input.size());

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(out.size(), input.size());
    EXPECT_NE(out, input);

    const std::vector<std::byte> needle = ascii_z("2033:03:04 05:06:07");
    const auto it = std::search(out.begin(), out.end(), needle.begin(),
                                needle.end());
    EXPECT_NE(it, out.end());
}

TEST(MetadataTransferApi, PlanAndApplyTiffEditMetadataRewrite)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "title");
    xmp.value                 = openmeta::make_text(store.arena(), "OpenMeta",
                                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1;
    ASSERT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format        = openmeta::TransferTargetFormat::Tiff;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.include_xmp_app1     = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    EXPECT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_GE(plan.tag_updates, 1U);
    EXPECT_GT(plan.output_size, plan.input_size);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::Ok);
    ASSERT_GT(out.size(), input.size());
    EXPECT_EQ(out[0], std::byte { 'I' });
    EXPECT_EQ(out[1], std::byte { 'I' });
    EXPECT_EQ(read_u16le(std::span<const std::byte>(out.data(), out.size()), 2U),
              42U);
    EXPECT_GT(read_u32le(std::span<const std::byte>(out.data(), out.size()), 4U),
              8U);
}

TEST(MetadataTransferApi, ExecutePreparedTransferJpegEditAndEmit)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    const std::vector<std::byte> input = make_jpeg_with_segment(
        0xE1U, std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                          bundle.blocks[0].payload.size()));

    openmeta::TransferTimePatchInput patch;
    patch.field      = openmeta::TimePatchField::DateTime;
    patch.value      = ascii_z("2033:03:04 05:06:07");
    patch.text_value = true;

    openmeta::ExecutePreparedTransferOptions options;
    options.time_patches.push_back(patch);
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.time_patch.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.compiled_ops, 1U);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.marker_summary.size(), 1U);
    EXPECT_EQ(result.marker_summary[0].marker, 0xE1U);
    EXPECT_EQ(result.marker_summary[0].count, 1U);

    EXPECT_TRUE(result.edit_requested);
    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.jpeg_edit_plan.selected_mode,
              openmeta::JpegEditMode::InPlace);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_output_size, input.size());
    ASSERT_EQ(result.edited_output.size(), input.size());

    const std::vector<std::byte> needle = ascii_z("2033:03:04 05:06:07");
    const auto it = std::search(result.edited_output.begin(),
                                result.edited_output.end(), needle.begin(),
                                needle.end());
    EXPECT_NE(it, result.edited_output.end());
}

TEST(MetadataTransferApi, ExecutePreparedTransferTiffEditAndEmit)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "title");
    xmp.value                 = openmeta::make_text(store.arena(), "OpenMeta",
                                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1;
    ASSERT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format        = openmeta::TransferTargetFormat::Tiff;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.include_xmp_app1     = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();

    openmeta::TransferTimePatchInput patch;
    patch.field      = openmeta::TimePatchField::DateTime;
    patch.value      = ascii_z("2030:12:31 23:59:59");
    patch.text_value = true;

    openmeta::ExecutePreparedTransferOptions options;
    options.time_patches.push_back(patch);
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.time_patch.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_GE(result.compiled_ops, 1U);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_TRUE(result.tiff_commit);
    ASSERT_FALSE(result.tiff_tag_summary.empty());

    bool found_xmp  = false;
    bool found_exif = false;
    for (size_t i = 0; i < result.tiff_tag_summary.size(); ++i) {
        if (result.tiff_tag_summary[i].tag == 700U) {
            found_xmp = true;
        }
        if (result.tiff_tag_summary[i].tag == 34665U) {
            found_exif = true;
        }
    }
    EXPECT_TRUE(found_xmp);
    EXPECT_TRUE(found_exif);

    EXPECT_TRUE(result.edit_requested);
    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_GE(result.tiff_edit_plan.tag_updates, 1U);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_GT(result.edit_output_size, input.size());
    ASSERT_GT(result.edited_output.size(), input.size());
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileRejectsEmptyPath)
{
    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file("");

    EXPECT_EQ(result.prepared.file_status,
              openmeta::TransferFileStatus::InvalidArgument);
    EXPECT_EQ(result.prepared.code,
              openmeta::PrepareTransferFileCode::EmptyPath);
    EXPECT_EQ(result.execute.compile.status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.execute.emit.status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(result.execute.compile.message.find("read/prepare failure")
                != std::string::npos);
}

TEST(MetadataTransferApi, ApplyTiffEditRejectsPlanMismatch)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;
    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 'x' } };
    bundle.blocks.push_back(xmp);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferBundle changed = bundle;
    changed.blocks.clear();

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), changed,
            plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(applied.code, openmeta::EmitTransferCode::PlanMismatch);
    EXPECT_EQ(applied.errors, 1U);
}

TEST(MetadataTransferApi, PlanTiffEditRejectsNoUpdates)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    EXPECT_EQ(plan.status, openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(plan.message.find("no tiff updates") != std::string::npos);
}

TEST(MetadataTransferApi, PrepareUnsupportedWhenExifOnlyUnsupportedIfd)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "mk_test", 0x0001U);
    e.value = openmeta::make_text(store.arena(), "v",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::PrepareTransferCode::ExifPackFailed);
    EXPECT_EQ(result.warnings, 1U);
    EXPECT_TRUE(result.message.find("exif app1 packer could not serialize")
                != std::string::npos);
    EXPECT_TRUE(bundle.blocks.empty());
}

TEST(MetadataTransferApi, PrepareBuildsJpegIccApp2Block)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    std::vector<std::byte> icc_tag_bytes(24U, std::byte { 0x11 });
    openmeta::Entry e;
    e.key   = openmeta::make_icc_tag_key(0x64657363U);  // 'desc'
    e.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(icc_tag_bytes.data(), icc_tag_bytes.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_iptc_app13 = false;
    request.include_icc_app2   = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app2-icc");
    ASSERT_GE(bundle.blocks[0].payload.size(), 20U);
    EXPECT_EQ(bundle.blocks[0].payload[0], std::byte { 'I' });
    EXPECT_EQ(bundle.blocks[0].payload[1], std::byte { 'C' });
    EXPECT_EQ(bundle.blocks[0].payload[2], std::byte { 'C' });
    EXPECT_EQ(bundle.blocks[0].payload[11], std::byte { 0x00 });
    EXPECT_EQ(bundle.blocks[0].payload[12], std::byte { 0x01 });
    EXPECT_EQ(bundle.blocks[0].payload[13], std::byte { 0x01 });
}

TEST(MetadataTransferApi, PrepareBuildsMultiChunkIccApp2Blocks)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    std::vector<std::byte> icc_tag_bytes(90000U, std::byte { 0x22 });
    openmeta::Entry e;
    e.key   = openmeta::make_icc_tag_key(0x64657363U);  // 'desc'
    e.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(icc_tag_bytes.data(), icc_tag_bytes.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_iptc_app13 = false;
    request.include_icc_app2   = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_GE(bundle.blocks.size(), 2U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app2-icc");
    EXPECT_EQ(bundle.blocks[1].route, "jpeg:app2-icc");
    EXPECT_EQ(bundle.blocks[0].payload[12], std::byte { 0x01 });
    EXPECT_EQ(bundle.blocks[1].payload[12], std::byte { 0x02 });
    EXPECT_EQ(bundle.blocks[0].payload[13], bundle.blocks[1].payload[13]);
}

TEST(MetadataTransferApi, PrepareBuildsJpegIptcApp13FromDatasets)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    const std::array<std::byte, 5> iptc_value = {
        std::byte { 'H' }, std::byte { 'e' }, std::byte { 'l' },
        std::byte { 'l' }, std::byte { 'o' },
    };

    openmeta::Entry e;
    e.key = openmeta::make_iptc_dataset_key(2U, 5U);
    e.value
        = openmeta::make_bytes(store.arena(),
                               std::span<const std::byte>(iptc_value.data(),
                                                          iptc_value.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app13-iptc");
    const std::span<const std::byte> p(bundle.blocks[0].payload.data(),
                                       bundle.blocks[0].payload.size());
    ASSERT_GE(p.size(), 32U);
    EXPECT_EQ(p[0], std::byte { 'P' });
    EXPECT_EQ(p[12], std::byte { '0' });
    EXPECT_EQ(p[13], std::byte { 0x00 });
    EXPECT_EQ(p[14], std::byte { '8' });
    EXPECT_EQ(p[15], std::byte { 'B' });
    EXPECT_EQ(p[16], std::byte { 'I' });
    EXPECT_EQ(p[17], std::byte { 'M' });
    EXPECT_EQ(p[18], std::byte { 0x04 });
    EXPECT_EQ(p[19], std::byte { 0x04 });
}

TEST(MetadataTransferApi, PrepareBuildsJpegIptcApp13FromPhotoshopIrb)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    const std::array<std::byte, 8> iptc_iim = {
        std::byte { 0x1C }, std::byte { 0x02 }, std::byte { 0x05 },
        std::byte { 0x00 }, std::byte { 0x03 }, std::byte { 'A' },
        std::byte { 'B' },  std::byte { 'C' },
    };

    openmeta::Entry e;
    e.key                   = openmeta::make_photoshop_irb_key(0x0404U);
    e.value                 = openmeta::make_bytes(store.arena(),
                                                   std::span<const std::byte>(iptc_iim.data(),
                                                                              iptc_iim.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    const std::span<const std::byte> p(bundle.blocks[0].payload.data(),
                                       bundle.blocks[0].payload.size());
    ASSERT_GE(p.size(), 8U + iptc_iim.size());
    bool found = false;
    for (size_t i = 0; i + iptc_iim.size() <= p.size(); ++i) {
        if (std::memcmp(p.data() + i, iptc_iim.data(), iptc_iim.size()) == 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(MetadataTransferApi, EmitJpegKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jpeg:app2-icc";
    icc.payload = { std::byte { 0x02 }, std::byte { 0x03 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferBlock iptc;
    iptc.route   = "jpeg:app13-iptc";
    iptc.payload = { std::byte { 0x04 } };
    bundle.blocks.push_back(iptc);

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 3U);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(emitter.calls.size(), 3U);
    EXPECT_EQ(emitter.calls[0].first, 0xE1U);
    EXPECT_EQ(emitter.calls[1].first, 0xE2U);
    EXPECT_EQ(emitter.calls[2].first, 0xEDU);
}

TEST(MetadataTransferApi, CompileJpegPlanKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock com;
    com.route   = "jpeg:com";
    com.payload = { std::byte { 0x02 } };
    bundle.blocks.push_back(com);

    openmeta::PreparedJpegEmitPlan plan;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_jpeg(bundle, &plan);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    ASSERT_EQ(plan.ops.size(), 2U);
    EXPECT_EQ(plan.ops[0].block_index, 0U);
    EXPECT_EQ(plan.ops[0].marker_code, 0xE1U);
    EXPECT_EQ(plan.ops[1].block_index, 1U);
    EXPECT_EQ(plan.ops[1].marker_code, 0xFEU);
}

TEST(MetadataTransferApi, EmitJpegCompiledPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jpeg:app2-icc";
    icc.payload = { std::byte { 0x02 }, std::byte { 0x03 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedJpegEmitPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_bundle_jpeg(bundle, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.ops.size(), 2U);

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult emit_result
        = openmeta::emit_prepared_bundle_jpeg_compiled(bundle, plan, emitter);
    EXPECT_EQ(emit_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(emit_result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(emit_result.emitted, 2U);
    ASSERT_EQ(emitter.calls.size(), 2U);
    EXPECT_EQ(emitter.calls[0].first, 0xE1U);
    EXPECT_EQ(emitter.calls[1].first, 0xE2U);
}

TEST(MetadataTransferApi, CompileRejectsNullPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_jpeg(bundle, nullptr);
    EXPECT_EQ(result.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(result.errors, 1U);
}

TEST(MetadataTransferApi, EmitJpegGenericAppRoutesAndCom)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock app0;
    app0.route   = "jpeg:app0-jfif";
    app0.payload = { std::byte { 0xAA } };
    bundle.blocks.push_back(app0);

    openmeta::PreparedTransferBlock app15;
    app15.route   = "jpeg:app15-vendor";
    app15.payload = { std::byte { 0xBB } };
    bundle.blocks.push_back(app15);

    openmeta::PreparedTransferBlock com;
    com.route   = "jpeg:com";
    com.payload = { std::byte { 0xCC } };
    bundle.blocks.push_back(com);

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(emitter.calls.size(), 3U);
    EXPECT_EQ(emitter.calls[0].first, 0xE0U);
    EXPECT_EQ(emitter.calls[1].first, 0xEFU);
    EXPECT_EQ(emitter.calls[2].first, 0xFEU);
}

TEST(MetadataTransferApi, EmitJpegSkipsEmptyPayloadByDefault)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock empty;
    empty.route = "jpeg:app1-xmp";
    bundle.blocks.push_back(empty);

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 0U);
    EXPECT_EQ(result.skipped, 1U);
    EXPECT_TRUE(emitter.calls.empty());
}

TEST(MetadataTransferApi, EmitJpegRejectsUnknownRoute)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock bad;
    bad.route   = "jpeg:app16-oob";
    bad.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(bad);

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::UnsupportedRoute);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(result.failed_block_index, 0U);
    EXPECT_TRUE(emitter.calls.empty());
}

TEST(MetadataTransferApi, EmitJpegPropagatesBackendError)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    FakeJpegEmitter emitter;
    emitter.fail_after_calls = 1U;
    emitter.fail_status      = openmeta::TransferStatus::Malformed;

    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::BackendWriteFailed);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(result.emitted, 0U);
    EXPECT_EQ(result.failed_block_index, 0U);
}

TEST(MetadataTransferApi, EmitJpegRejectsNonJpegBundle)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::BundleTargetNotJpeg);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_TRUE(emitter.calls.empty());
}

TEST(MetadataTransferApi, EmitTiffEmitsKnownRoutesAndCommits)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock exif;
    exif.route = "tiff:ifd-exif-app1";
    exif.payload
        = { std::byte { 'E' },  std::byte { 'x' },  std::byte { 'i' },
            std::byte { 'f' },  std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 'I' },  std::byte { 'I' },  std::byte { 42 },
            std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "tiff:tag-34675-icc";
    icc.payload = { std::byte { 0x03 } };
    bundle.blocks.push_back(icc);

    FakeTiffEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_tiff(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(result.emitted, 3U);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(emitter.commit_calls, 1U);
    ASSERT_EQ(emitter.bytes_calls.size(), 3U);
    EXPECT_EQ(emitter.bytes_calls[0].first, 34665U);
    EXPECT_EQ(emitter.bytes_calls[1].first, 700U);
    EXPECT_EQ(emitter.bytes_calls[2].first, 34675U);
}

TEST(MetadataTransferApi, CompileTiffPlanKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock exif;
    exif.route = "tiff:ifd-exif-app1";
    exif.payload
        = { std::byte { 'E' },  std::byte { 'x' },  std::byte { 'i' },
            std::byte { 'f' },  std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 'I' },  std::byte { 'I' },  std::byte { 42 },
            std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTiffEmitPlan plan;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_tiff(bundle, &plan);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    ASSERT_EQ(plan.ops.size(), 2U);
    EXPECT_EQ(plan.ops[0].block_index, 0U);
    EXPECT_EQ(plan.ops[0].tiff_tag, 34665U);
    EXPECT_EQ(plan.ops[1].block_index, 1U);
    EXPECT_EQ(plan.ops[1].tiff_tag, 700U);
}

TEST(MetadataTransferApi, EmitTiffCompiledPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock iptc;
    iptc.route   = "tiff:tag-33723-iptc";
    iptc.payload = { std::byte { 0x03 }, std::byte { 0x04 } };
    bundle.blocks.push_back(iptc);

    openmeta::PreparedTiffEmitPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_bundle_tiff(bundle, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.ops.size(), 2U);

    FakeTiffEmitter emitter;
    const openmeta::EmitTransferResult emit_result
        = openmeta::emit_prepared_bundle_tiff_compiled(bundle, plan, emitter);
    EXPECT_EQ(emit_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(emit_result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(emit_result.emitted, 2U);
    EXPECT_EQ(emit_result.errors, 0U);
    EXPECT_EQ(emitter.commit_calls, 1U);
    ASSERT_EQ(emitter.bytes_calls.size(), 2U);
    EXPECT_EQ(emitter.bytes_calls[0].first, 700U);
    EXPECT_EQ(emitter.bytes_calls[1].first, 33723U);
}

TEST(MetadataTransferApi, CompileTiffRejectsNullPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_tiff(bundle, nullptr);
    EXPECT_EQ(result.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(result.errors, 1U);
}

TEST(MetadataTransferApi, EmitTiffRejectsUnknownRoute)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock bad;
    bad.route   = "tiff:tag-1234-unknown";
    bad.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(bad);

    FakeTiffEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_tiff(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::UnsupportedRoute);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(result.failed_block_index, 0U);
    EXPECT_EQ(emitter.commit_calls, 0U);
}

TEST(MetadataTransferApi, EmitTiffCompiledRejectsPlanMismatch)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;
    bundle.contract_version += 1U;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTiffEmitPlan plan;
    plan.contract_version = openmeta::kMetadataTransferContractVersion;
    plan.ops.push_back(openmeta::PreparedTiffEmitOp { 0U, 700U });

    FakeTiffEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_tiff_compiled(bundle, plan, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::PlanMismatch);
    EXPECT_EQ(result.errors, 1U);
}

TEST(MetadataTransferApi, EmitTiffRejectsNonTiffBundle)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(xmp);

    FakeTiffEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_tiff(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(emitter.commit_calls, 0U);
}

TEST(MetadataTransferApi, EmitTiffPropagatesBackendWriteError)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(xmp);

    FakeTiffEmitter emitter;
    emitter.fail_set_after_calls = 1U;
    emitter.fail_status          = openmeta::TransferStatus::Malformed;

    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_tiff(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::BackendWriteFailed);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(result.failed_block_index, 0U);
    EXPECT_EQ(emitter.commit_calls, 0U);
}

TEST(MetadataTransferApi, EmitCompiledRejectsPlanMismatch)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;
    bundle.contract_version += 1U;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedJpegEmitPlan plan;
    plan.contract_version = openmeta::kMetadataTransferContractVersion;
    plan.ops.push_back(openmeta::PreparedJpegEmitOp { 0U, 0xE1U });

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg_compiled(bundle, plan, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::PlanMismatch);
    EXPECT_EQ(result.errors, 1U);
}

}  // namespace
