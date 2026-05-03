// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_query.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static EntryId add_exif_u32_array(MetaStore* store, std::string_view ifd,
                                      uint16_t tag,
                                      std::span<const uint32_t> values,
                                      EntryFlags flags = EntryFlags::None)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_u32_array(store->arena(), values);
        entry.flags      = flags;
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_u32(MetaStore* store, std::string_view ifd,
                                uint16_t tag, uint32_t value,
                                EntryFlags flags = EntryFlags::None)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_u32(value);
        entry.flags      = flags;
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_urational(MetaStore* store, std::string_view ifd,
                                      uint16_t tag, uint32_t numer,
                                      uint32_t denom)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_urational(numer, denom);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_xmp_text(MetaStore* store, std::string_view ns,
                                std::string_view path, std::string_view value)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_xmp_property_key(store->arena(), ns, path);
        entry.value      = make_text(store->arena(), value, TextEncoding::Utf8);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static const MetadataQueryCandidate*
    find_candidate(const MetadataQueryResult& result,
                   MetadataQuerySemanticKind semantic)
    {
        for (size_t i = 0U; i < result.candidates.size(); ++i) {
            if (result.candidates[i].semantic == semantic) {
                return &result.candidates[i];
            }
        }
        return nullptr;
    }

    static const MetadataQueryMatch*
    find_match_for_entry(const MetadataQueryResult& result, EntryId entry_id)
    {
        for (size_t i = 0U; i < result.matches.size(); ++i) {
            if (result.matches[i].entry_id == entry_id) {
                return &result.matches[i];
            }
        }
        return nullptr;
    }

    static bool contains_entry(const std::vector<EntryId>& entries,
                               EntryId entry_id) noexcept
    {
        for (size_t i = 0U; i < entries.size(); ++i) {
            if (entries[i] == entry_id) {
                return true;
            }
        }
        return false;
    }

    static const MetadataQueryCandidate*
    find_candidate_for_entry(const MetadataQueryResult& result,
                             EntryId entry_id)
    {
        for (size_t i = 0U; i < result.candidates.size(); ++i) {
            if (contains_entry(result.candidates[i].source_entries, entry_id)) {
                return &result.candidates[i];
            }
        }
        return nullptr;
    }

}  // namespace

TEST(MetadataQuery, BuildsDngDefaultCropCandidate)
{
    MetaStore store;
    const std::array<uint32_t, 2> origin = { 12U, 34U };
    const std::array<uint32_t, 2> size   = { 4000U, 3000U };
    const EntryId origin_id
        = add_exif_u32_array(&store, "ifd0", 0xC61FU,
                             std::span<const uint32_t>(origin.data(),
                                                       origin.size()));
    const EntryId size_id
        = add_exif_u32_array(&store, "ifd0", 0xC620U,
                             std::span<const uint32_t>(size.data(),
                                                       size.size()));
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::Crop);
    EXPECT_GE(result.matches.size(), 2U);
    const MetadataQueryCandidate* candidate
        = find_candidate(result, MetadataQuerySemanticKind::Crop);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Rect);
    EXPECT_GE(candidate->confidence, 90U);
    ASSERT_TRUE(candidate->has_origin);
    EXPECT_DOUBLE_EQ(candidate->origin[0], 12.0);
    EXPECT_DOUBLE_EQ(candidate->origin[1], 34.0);
    ASSERT_TRUE(candidate->has_size);
    EXPECT_DOUBLE_EQ(candidate->size[0], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->size[1], 3000.0);
    ASSERT_TRUE(candidate->has_rect);
    EXPECT_DOUBLE_EQ(candidate->rect[0], 12.0);
    EXPECT_DOUBLE_EQ(candidate->rect[1], 34.0);
    EXPECT_DOUBLE_EQ(candidate->rect[2], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->rect[3], 3000.0);
    EXPECT_TRUE(contains_entry(candidate->source_entries, origin_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, size_id));
}

TEST(MetadataQuery, DoesNotPairDngCropAcrossIfds)
{
    MetaStore store;
    const std::array<uint32_t, 2> origin = { 12U, 34U };
    const std::array<uint32_t, 2> size   = { 4000U, 3000U };
    add_exif_u32_array(&store, "ifd0", 0xC61FU,
                       std::span<const uint32_t>(origin.data(), origin.size()));
    add_exif_u32_array(&store, "subifd0", 0xC620U,
                       std::span<const uint32_t>(size.data(), size.size()));
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    EXPECT_EQ(find_candidate(result, MetadataQuerySemanticKind::Crop), nullptr);
}

TEST(MetadataQuery, NormalizesActiveAreaCandidate)
{
    MetaStore store;
    const std::array<uint32_t, 4> active_area = {
        10U,
        20U,
        3010U,
        4020U,
    };
    const EntryId active_id
        = add_exif_u32_array(&store, "ifd0", 0xC68DU,
                             std::span<const uint32_t>(active_area.data(),
                                                       active_area.size()));
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate(result, MetadataQuerySemanticKind::ActiveArea);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Rect);
    EXPECT_GE(candidate->confidence, 90U);
    ASSERT_TRUE(candidate->has_origin);
    EXPECT_DOUBLE_EQ(candidate->origin[0], 20.0);
    EXPECT_DOUBLE_EQ(candidate->origin[1], 10.0);
    ASSERT_TRUE(candidate->has_size);
    EXPECT_DOUBLE_EQ(candidate->size[0], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->size[1], 3000.0);
    ASSERT_TRUE(candidate->has_rect);
    EXPECT_DOUBLE_EQ(candidate->rect[0], 20.0);
    EXPECT_DOUBLE_EQ(candidate->rect[1], 10.0);
    EXPECT_DOUBLE_EQ(candidate->rect[2], 4000.0);
    EXPECT_DOUBLE_EQ(candidate->rect[3], 3000.0);
    EXPECT_TRUE(contains_entry(candidate->source_entries, active_id));
}

TEST(MetadataQuery, NormalizesPhaseOneRawGeometryCandidate)
{
    MetaStore store;
    add_exif_u32(&store, "mk_phaseone0", 0x0108U, 10560U);
    add_exif_u32(&store, "mk_phaseone0", 0x0109U, 7920U);
    add_exif_u32(&store, "mk_phaseone0", 0x010AU, 64U);
    add_exif_u32(&store, "mk_phaseone0", 0x010BU, 32U);
    add_exif_u32(&store, "mk_phaseone0", 0x010CU, 10328U);
    add_exif_u32(&store, "mk_phaseone0", 0x010DU, 7760U);
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate(result, MetadataQuerySemanticKind::ActiveArea);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Rect);
    EXPECT_EQ(candidate->confidence, 96U);
    EXPECT_EQ(candidate->source_entries.size(), 6U);
    ASSERT_TRUE(candidate->has_origin);
    EXPECT_DOUBLE_EQ(candidate->origin[0], 64.0);
    EXPECT_DOUBLE_EQ(candidate->origin[1], 32.0);
    ASSERT_TRUE(candidate->has_size);
    EXPECT_DOUBLE_EQ(candidate->size[0], 10328.0);
    EXPECT_DOUBLE_EQ(candidate->size[1], 7760.0);
    ASSERT_TRUE(candidate->has_rect);
    EXPECT_DOUBLE_EQ(candidate->rect[0], 64.0);
    EXPECT_DOUBLE_EQ(candidate->rect[1], 32.0);
    EXPECT_DOUBLE_EQ(candidate->rect[2], 10328.0);
    EXPECT_DOUBLE_EQ(candidate->rect[3], 7760.0);
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 4U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 64.0);
    EXPECT_DOUBLE_EQ(candidate->values[1], 32.0);
    EXPECT_DOUBLE_EQ(candidate->values[2], 168.0);
    EXPECT_DOUBLE_EQ(candidate->values[3], 128.0);
}

TEST(MetadataQuery, MatchesFuzzyXmpCropPath)
{
    MetaStore store;
    const EntryId entry_id
        = add_xmp_text(&store, "http://example.invalid/aux/1.0/",
                       "aux:SensorBorderPadding", "64 32 168 128");
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryMatch* match = find_match_for_entry(result, entry_id);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->key_kind, MetaKeyKind::XmpProperty);
    EXPECT_EQ(match->semantic, MetadataQuerySemanticKind::Border);
    EXPECT_EQ(match->shape, MetadataQueryValueShape::Text);
    EXPECT_GE(match->confidence, 70U);
    EXPECT_NE((match->matched_terms
               & static_cast<uint32_t>(MetadataQueryMatchTerm::Border)),
              0U);
    EXPECT_NE((match->matched_terms
               & static_cast<uint32_t>(MetadataQueryMatchTerm::Padding)),
              0U);
}

TEST(MetadataQuery, IgnoresDeletedEntries)
{
    MetaStore store;
    const std::array<uint32_t, 2> origin = { 12U, 34U };
    const std::array<uint32_t, 2> size   = { 4000U, 3000U };
    add_exif_u32_array(&store, "ifd0", 0xC61FU,
                       std::span<const uint32_t>(origin.data(), origin.size()),
                       EntryFlags::Deleted);
    add_exif_u32_array(&store, "ifd0", 0xC620U,
                       std::span<const uint32_t>(size.data(), size.size()));
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    EXPECT_EQ(find_candidate(result, MetadataQuerySemanticKind::Crop), nullptr);
    EXPECT_EQ(result.matches.size(), 1U);
}

TEST(MetadataQuery, QueryMetadataDispatchesCrop)
{
    MetaStore store;
    const std::array<uint32_t, 4> active_area = {
        10U,
        20U,
        3010U,
        4020U,
    };
    add_exif_u32_array(&store, "ifd0", 0xC68DU,
                       std::span<const uint32_t>(active_area.data(),
                                                 active_area.size()));
    store.finalize();

    const MetadataQueryResult result = query_metadata(store,
                                                      MetadataQueryKind::Crop);

    EXPECT_EQ(result.kind, MetadataQueryKind::Crop);
    EXPECT_NE(find_candidate(result, MetadataQuerySemanticKind::ActiveArea),
              nullptr);
    EXPECT_STREQ(metadata_query_kind_name(result.kind), "crop");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::ActiveArea),
                 "active_area");
    EXPECT_STREQ(metadata_query_value_shape_name(MetadataQueryValueShape::Rect),
                 "rect");
}

TEST(MetadataQuery, MatchesStandardExposureAndGain)
{
    MetaStore store;
    const EntryId exposure_id = add_exif_urational(&store, "exififd", 0x829AU,
                                                   1U, 125U);
    const EntryId gain_id     = add_exif_u32(&store, "exififd", 0xA407U, 2U);
    store.finalize();

    const MetadataQueryResult result = query_exposure_gain_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::ExposureGain);
    const MetadataQueryMatch* exposure_match
        = find_match_for_entry(result, exposure_id);
    ASSERT_NE(exposure_match, nullptr);
    EXPECT_EQ(exposure_match->semantic, MetadataQuerySemanticKind::Exposure);
    EXPECT_EQ(exposure_match->shape, MetadataQueryValueShape::Scalar);
    const MetadataQueryCandidate* exposure_candidate
        = find_candidate_for_entry(result, exposure_id);
    ASSERT_NE(exposure_candidate, nullptr);
    ASSERT_TRUE(exposure_candidate->has_values);
    ASSERT_EQ(exposure_candidate->values.size(), 1U);
    EXPECT_DOUBLE_EQ(exposure_candidate->values[0], 0.008);

    const MetadataQueryMatch* gain_match = find_match_for_entry(result,
                                                                gain_id);
    ASSERT_NE(gain_match, nullptr);
    EXPECT_EQ(gain_match->semantic, MetadataQuerySemanticKind::Gain);
    const MetadataQueryCandidate* gain_candidate
        = find_candidate_for_entry(result, gain_id);
    ASSERT_NE(gain_candidate, nullptr);
    ASSERT_TRUE(gain_candidate->has_values);
    ASSERT_EQ(gain_candidate->values.size(), 1U);
    EXPECT_DOUBLE_EQ(gain_candidate->values[0], 2.0);
}

TEST(MetadataQuery, MatchesXmpWhiteBalance)
{
    MetaStore store;
    const EntryId entry_id
        = add_xmp_text(&store, "http://ns.adobe.com/camera-raw-settings/1.0/",
                       "crs:WhiteBalance", "As Shot");
    store.finalize();

    const MetadataQueryResult result = query_white_balance_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::WhiteBalance);
    const MetadataQueryMatch* match = find_match_for_entry(result, entry_id);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->semantic, MetadataQuerySemanticKind::WhiteBalance);
    EXPECT_EQ(match->shape, MetadataQueryValueShape::Text);
    EXPECT_GE(match->confidence, 90U);
    EXPECT_NE((match->matched_terms
               & static_cast<uint32_t>(MetadataQueryMatchTerm::WhiteBalance)),
              0U);
    const MetadataQueryCandidate* candidate
        = find_candidate_for_entry(result, entry_id);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->semantic, MetadataQuerySemanticKind::WhiteBalance);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Text);
    EXPECT_FALSE(candidate->has_values);
}

TEST(MetadataQuery, MatchesDngColorMatrix)
{
    MetaStore store;
    const std::array<uint32_t, 9> matrix = {
        1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
    };
    const EntryId matrix_id
        = add_exif_u32_array(&store, "ifd0", 0xC621U,
                             std::span<const uint32_t>(matrix.data(),
                                                       matrix.size()));
    store.finalize();

    const MetadataQueryResult result = query_color_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::Color);
    const MetadataQueryMatch* match = find_match_for_entry(result, matrix_id);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->semantic, MetadataQuerySemanticKind::ColorMatrix);
    EXPECT_EQ(match->shape, MetadataQueryValueShape::Matrix3x3);
    EXPECT_GE(match->confidence, 90U);
    const MetadataQueryCandidate* candidate
        = find_candidate_for_entry(result, matrix_id);
    ASSERT_NE(candidate, nullptr);
    EXPECT_EQ(candidate->normalized_shape, MetadataQueryValueShape::Matrix3x3);
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 9U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[4], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[8], 1.0);
}

TEST(MetadataQuery, ReusesVendorLensCorrectionClassification)
{
    MetaStore store;
    const EntryId entry_id = add_exif_u32(&store, "mk_nikon_distortinfo",
                                          0x0001U, 7U);
    store.finalize();

    const MetadataQueryResult result = query_lens_correction_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::LensCorrection);
    const MetadataQueryMatch* match = find_match_for_entry(result, entry_id);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->semantic, MetadataQuerySemanticKind::LensCorrection);
    EXPECT_GE(match->confidence, 90U);
    EXPECT_NE((match->matched_terms
               & static_cast<uint32_t>(MetadataQueryMatchTerm::Lens)),
              0U);
    EXPECT_NE((match->matched_terms
               & static_cast<uint32_t>(MetadataQueryMatchTerm::Correction)),
              0U);
}

TEST(MetadataQuery, MatchesOrientationTags)
{
    MetaStore store;
    const EntryId entry_id = add_exif_u32(&store, "ifd0", 0x0112U, 6U);
    store.finalize();

    const MetadataQueryResult result = query_orientation_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::Orientation);
    const MetadataQueryMatch* match = find_match_for_entry(result, entry_id);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->semantic, MetadataQuerySemanticKind::Orientation);
    EXPECT_GE(match->confidence, 90U);
    const MetadataQueryCandidate* candidate
        = find_candidate_for_entry(result, entry_id);
    ASSERT_NE(candidate, nullptr);
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 1U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 6.0);
    EXPECT_STREQ(metadata_query_kind_name(result.kind), "orientation");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::Color),
                 "color");
    EXPECT_STREQ(metadata_query_value_shape_name(
                     MetadataQueryValueShape::Matrix3x3),
                 "matrix3x3");
}

}  // namespace openmeta
