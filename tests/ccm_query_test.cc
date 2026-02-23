#include "openmeta/ccm_query.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static const CcmField*
    find_field(const std::vector<CcmField>& fields, std::string_view ifd,
               std::string_view name) noexcept
    {
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].ifd == ifd && fields[i].name == name) {
                return &fields[i];
            }
        }
        return nullptr;
    }

}  // namespace


TEST(CcmQuery, CollectsNormalizedFieldsInDngContext)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1, 6, 0, 0 };
    Entry dng_ver;
    dng_ver.key = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
    dng_ver.value = make_u8_array(
        store.arena(),
        std::span<const uint8_t>(dng_version.data(), dng_version.size()));
    dng_ver.origin.block          = block;
    dng_ver.origin.order_in_block = 0;
    (void)store.add_entry(dng_ver);

    const URational cm_values[9] = {
        { 1000, 1000 },
        { 0, 1000 },
        { 0, 1000 },
        { 0, 1000 },
        { 1000, 1000 },
        { 0, 1000 },
        { 0, 1000 },
        { 0, 1000 },
        { 1000, 1000 },
    };
    Entry cm1;
    cm1.key = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value = make_urational_array(store.arena(),
                                     std::span<const URational>(cm_values, 9));
    cm1.origin.block          = block;
    cm1.origin.order_in_block = 1;
    (void)store.add_entry(cm1);

    const URational as_shot_neutral[3] = {
        { 1, 1 },
        { 1, 1 },
        { 1, 1 },
    };
    Entry asn;
    asn.key = make_exif_tag_key(store.arena(), "ifd0", 0xC628);
    asn.value = make_urational_array(
        store.arena(), std::span<const URational>(as_shot_neutral, 3));
    asn.origin.block          = block;
    asn.origin.order_in_block = 2;
    (void)store.add_entry(asn);

    store.finalize();

    std::vector<CcmField> fields;
    const CcmQueryResult r = collect_dng_ccm_fields(store, &fields);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_EQ(r.fields_found, fields.size());

    const CcmField* cm = find_field(fields, "ifd0", "ColorMatrix1");
    ASSERT_NE(cm, nullptr);
    EXPECT_EQ(cm->rows, 3U);
    EXPECT_EQ(cm->cols, 3U);
    ASSERT_EQ(cm->values.size(), 9U);
    EXPECT_DOUBLE_EQ(cm->values[0], 1.0);
    EXPECT_DOUBLE_EQ(cm->values[4], 1.0);
    EXPECT_DOUBLE_EQ(cm->values[8], 1.0);

    const CcmField* neutral = find_field(fields, "ifd0", "AsShotNeutral");
    ASSERT_NE(neutral, nullptr);
    EXPECT_EQ(neutral->rows, 1U);
    EXPECT_EQ(neutral->cols, 3U);
    ASSERT_EQ(neutral->values.size(), 3U);
    EXPECT_DOUBLE_EQ(neutral->values[0], 1.0);
    EXPECT_DOUBLE_EQ(neutral->values[1], 1.0);
    EXPECT_DOUBLE_EQ(neutral->values[2], 1.0);
}


TEST(CcmQuery, RespectsRequireContextAndLimits)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const URational cm_values[9] = {
        { 1000, 1000 },
        { 0, 1000 },
        { 0, 1000 },
        { 0, 1000 },
        { 1000, 1000 },
        { 0, 1000 },
        { 0, 1000 },
        { 0, 1000 },
        { 1000, 1000 },
    };
    Entry cm1;
    cm1.key = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value = make_urational_array(store.arena(),
                                     std::span<const URational>(cm_values, 9));
    cm1.origin.block          = block;
    cm1.origin.order_in_block = 0;
    (void)store.add_entry(cm1);
    store.finalize();

    std::vector<CcmField> fields;
    CcmQueryOptions opts;
    opts.require_dng_context = true;
    CcmQueryResult r         = collect_dng_ccm_fields(store, &fields, opts);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_TRUE(fields.empty());

    opts.require_dng_context            = false;
    opts.limits.max_values_per_field    = 4U;
    r                                   = collect_dng_ccm_fields(store, &fields,
                                                                  opts);
    EXPECT_EQ(r.status, CcmQueryStatus::LimitExceeded);
    ASSERT_EQ(fields.size(), 1U);
    EXPECT_EQ(fields[0].values.size(), 4U);
}

}  // namespace openmeta

