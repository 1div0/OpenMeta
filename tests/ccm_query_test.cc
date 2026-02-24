#include "openmeta/ccm_query.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <gtest/gtest.h>

#include <array>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static const CcmField* find_field(const std::vector<CcmField>& fields,
                                      std::string_view ifd,
                                      std::string_view name) noexcept
    {
        for (size_t i = 0; i < fields.size(); ++i) {
            if (fields[i].ifd == ifd && fields[i].name == name) {
                return &fields[i];
            }
        }
        return nullptr;
    }

    static bool has_issue(const std::vector<CcmIssue>& issues,
                          CcmIssueCode code) noexcept
    {
        for (size_t i = 0; i < issues.size(); ++i) {
            if (issues[i].code == code) {
                return true;
            }
        }
        return false;
    }

    static bool has_issue(const std::vector<CcmIssue>& issues,
                          CcmIssueSeverity severity, CcmIssueCode code) noexcept
    {
        for (size_t i = 0; i < issues.size(); ++i) {
            if (issues[i].severity == severity && issues[i].code == code) {
                return true;
            }
        }
        return false;
    }

}  // namespace


TEST(CcmQuery, CollectsNormalizedFieldsInDngContext)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1, 6, 0, 0 };
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

    const URational as_shot_neutral[3] = {
        { 1, 1 },
        { 1, 1 },
        { 1, 1 },
    };
    Entry asn;
    asn.key = make_exif_tag_key(store.arena(), "ifd0", 0xC628);
    asn.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(as_shot_neutral, 3));
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

    std::vector<CcmField> fields;
    CcmQueryOptions opts;
    opts.require_dng_context = true;
    CcmQueryResult r         = collect_dng_ccm_fields(store, &fields, opts);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_TRUE(fields.empty());

    opts.require_dng_context         = false;
    opts.limits.max_values_per_field = 4U;
    r = collect_dng_ccm_fields(store, &fields, opts);
    EXPECT_EQ(r.status, CcmQueryStatus::LimitExceeded);
    ASSERT_EQ(fields.size(), 1U);
    EXPECT_EQ(fields[0].values.size(), 4U);
}


TEST(CcmQuery, ValidationWarnsWithoutRejectingStructuredMismatches)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1, 7, 0, 0 };
    Entry dng_ver;
    dng_ver.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
    dng_ver.value        = make_u8_array(store.arena(),
                                         std::span<const uint8_t>(dng_version.data(),
                                                                  dng_version.size()));
    dng_ver.origin.block = block;
    dng_ver.origin.order_in_block = 0;
    (void)store.add_entry(dng_ver);

    const URational cm_values[8] = {
        { 1, 1 }, { 0, 1 }, { 0, 1 }, { 0, 1 },
        { 1, 1 }, { 0, 1 }, { 0, 1 }, { 1, 1 },
    };
    Entry cm1;
    cm1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value        = make_urational_array(store.arena(),
                                            std::span<const URational>(cm_values, 8));
    cm1.origin.block = block;
    cm1.origin.order_in_block = 1;
    (void)store.add_entry(cm1);

    const URational as_shot_neutral[3] = {
        { 1, 1 },
        { 1, 1 },
        { 1, 1 },
    };
    Entry asn;
    asn.key = make_exif_tag_key(store.arena(), "ifd0", 0xC628);
    asn.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(as_shot_neutral, 3));
    asn.origin.block          = block;
    asn.origin.order_in_block = 2;
    (void)store.add_entry(asn);

    const URational as_shot_xy[2] = { { 3127, 10000 }, { 3290, 10000 } };
    Entry asxy;
    asxy.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC629);
    asxy.value        = make_urational_array(store.arena(),
                                             std::span<const URational>(as_shot_xy, 2));
    asxy.origin.block = block;
    asxy.origin.order_in_block = 3;
    (void)store.add_entry(asxy);

    store.finalize();

    std::vector<CcmField> fields;
    std::vector<CcmIssue> issues;
    CcmQueryOptions opts;
    opts.validation_mode = CcmValidationMode::DngSpecWarnings;

    const CcmQueryResult r = collect_dng_ccm_fields(store, &fields, opts,
                                                    &issues);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_EQ(r.fields_found, fields.size());
    EXPECT_EQ(r.fields_dropped, 0U);
    ASSERT_FALSE(fields.empty());
    EXPECT_TRUE(has_issue(issues, CcmIssueCode::MatrixCountNotDivisibleBy3));
    EXPECT_TRUE(has_issue(issues, CcmIssueCode::AsShotConflict));
    EXPECT_GT(r.issues_reported, 0U);
}


TEST(CcmQuery, NonFiniteValuesAreDroppedAndReported)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1, 7, 0, 0 };
    Entry dng_ver;
    dng_ver.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
    dng_ver.value        = make_u8_array(store.arena(),
                                         std::span<const uint8_t>(dng_version.data(),
                                                                  dng_version.size()));
    dng_ver.origin.block = block;
    dng_ver.origin.order_in_block = 0;
    (void)store.add_entry(dng_ver);

    const uint64_t nan_bits[9] = {
        0x7FF8000000000000ULL, 0x3FF0000000000000ULL, 0x3FF0000000000000ULL,
        0x3FF0000000000000ULL, 0x3FF0000000000000ULL, 0x3FF0000000000000ULL,
        0x3FF0000000000000ULL, 0x3FF0000000000000ULL, 0x3FF0000000000000ULL,
    };
    Entry cm1;
    cm1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC621);
    cm1.value        = make_f64_bits_array(store.arena(),
                                           std::span<const uint64_t>(nan_bits, 9));
    cm1.origin.block = block;
    cm1.origin.order_in_block = 1;
    (void)store.add_entry(cm1);

    store.finalize();

    std::vector<CcmField> fields;
    std::vector<CcmIssue> issues;
    CcmQueryOptions opts;
    opts.validation_mode = CcmValidationMode::DngSpecWarnings;

    const CcmQueryResult r = collect_dng_ccm_fields(store, &fields, opts,
                                                    &issues);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_TRUE(fields.empty());
    EXPECT_EQ(r.fields_found, 0U);
    EXPECT_EQ(r.fields_dropped, 1U);
    EXPECT_TRUE(has_issue(issues, CcmIssueSeverity::Error,
                          CcmIssueCode::NonFiniteValue));
}

TEST(CcmQuery, ValidationWarnsOnIlluminantCodeAndWhiteXYRange)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});

    const std::array<uint8_t, 4> dng_version = { 1, 7, 0, 0 };
    Entry dng_ver;
    dng_ver.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC612);
    dng_ver.value        = make_u8_array(store.arena(),
                                         std::span<const uint8_t>(dng_version.data(),
                                                                  dng_version.size()));
    dng_ver.origin.block = block;
    dng_ver.origin.order_in_block = 0;
    (void)store.add_entry(dng_ver);

    Entry cal1;
    cal1.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC65A);
    cal1.value        = make_u16(500U);
    cal1.origin.block = block;
    cal1.origin.order_in_block = 1;
    (void)store.add_entry(cal1);

    const URational as_shot_xy[2] = { { 11, 10 }, { 1, 10 } };
    Entry asxy;
    asxy.key          = make_exif_tag_key(store.arena(), "ifd0", 0xC629);
    asxy.value        = make_urational_array(store.arena(),
                                             std::span<const URational>(as_shot_xy, 2));
    asxy.origin.block = block;
    asxy.origin.order_in_block = 2;
    (void)store.add_entry(asxy);

    store.finalize();

    std::vector<CcmField> fields;
    std::vector<CcmIssue> issues;
    CcmQueryOptions opts;
    opts.validation_mode = CcmValidationMode::DngSpecWarnings;

    const CcmQueryResult r = collect_dng_ccm_fields(store, &fields, opts,
                                                    &issues);
    EXPECT_EQ(r.status, CcmQueryStatus::Ok);
    EXPECT_EQ(r.fields_found, fields.size());
    EXPECT_TRUE(has_issue(issues, CcmIssueCode::InvalidIlluminantCode));
    EXPECT_TRUE(has_issue(issues, CcmIssueCode::WhiteXYOutOfRange));
    EXPECT_GT(r.issues_reported, 0U);
}

}  // namespace openmeta
