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

    static EntryId add_exif_text(MetaStore* store, std::string_view ifd,
                                 uint16_t tag, std::string_view value)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
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

    static const MetadataQueryCandidate* find_candidate_with_shape(
        const MetadataQueryResult& result, MetadataQuerySemanticKind semantic,
        MetadataQueryValueShape shape, size_t min_source_entries)
    {
        for (size_t i = 0U; i < result.candidates.size(); ++i) {
            const MetadataQueryCandidate& candidate = result.candidates[i];
            if (candidate.semantic == semantic
                && candidate.normalized_shape == shape
                && candidate.source_entries.size() >= min_source_entries) {
                return &candidate;
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

TEST(MetadataQuery, ReportsRapidFuzzAvailability)
{
#if defined(OPENMETA_HAS_RAPIDFUZZ) && OPENMETA_HAS_RAPIDFUZZ
    EXPECT_TRUE(metadata_query_fuzzy_search_available());
#else
    EXPECT_FALSE(metadata_query_fuzzy_search_available());
#endif
}

TEST(MetadataQuery, RapidFuzzMatchesMisspelledXmpCropPath)
{
    if (!metadata_query_fuzzy_search_available()) {
        GTEST_SKIP() << "RapidFuzz metadata query matching is not enabled";
    }

    MetaStore store;
    const EntryId entry_id
        = add_xmp_text(&store, "http://example.invalid/aux/1.0/",
                       "aux:SensrBordrPading", "64 32 168 128");
    store.finalize();

    const MetadataQueryResult result = query_crop_metadata(store);

    const MetadataQueryMatch* match = find_match_for_entry(result, entry_id);
    ASSERT_NE(match, nullptr);
    EXPECT_EQ(match->semantic, MetadataQuerySemanticKind::Border);
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

TEST(MetadataQuery, GroupsDngExposureGainTable)
{
    MetaStore store;
    const EntryId baseline_id     = add_exif_u32(&store, "ifd0", 0xC62AU, 1U);
    const EntryId preview_gain_id = add_exif_u32(&store, "ifd0", 0xC7A8U, 2U);
    store.finalize();

    const MetadataQueryResult result = query_exposure_gain_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::ExposureGain,
                                    MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_GE(candidate->confidence, 90U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, baseline_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, preview_gain_id));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 2U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[1], 2.0);
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

TEST(MetadataQuery, GroupsDngColorMatrixSet)
{
    MetaStore store;
    const std::array<uint32_t, 9> matrix1 = {
        1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
    };
    const std::array<uint32_t, 9> matrix2 = {
        2U, 0U, 0U, 0U, 2U, 0U, 0U, 0U, 2U,
    };
    const EntryId matrix1_id
        = add_exif_u32_array(&store, "ifd0", 0xC621U,
                             std::span<const uint32_t>(matrix1.data(),
                                                       matrix1.size()));
    const EntryId matrix2_id
        = add_exif_u32_array(&store, "ifd0", 0xC622U,
                             std::span<const uint32_t>(matrix2.data(),
                                                       matrix2.size()));
    store.finalize();

    const MetadataQueryResult result = query_color_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::ColorMatrix,
                                    MetadataQueryValueShape::MatrixSet, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_GE(candidate->confidence, 90U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, matrix1_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, matrix2_id));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 18U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[4], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[8], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[9], 2.0);
    EXPECT_DOUBLE_EQ(candidate->values[13], 2.0);
    EXPECT_DOUBLE_EQ(candidate->values[17], 2.0);
    EXPECT_STREQ(metadata_query_value_shape_name(
                     MetadataQueryValueShape::MatrixSet),
                 "matrix_set");
}

TEST(MetadataQuery, GroupsDngWhiteBalanceVectorSet)
{
    MetaStore store;
    const std::array<uint32_t, 3> neutral = { 1U, 2U, 3U };
    const std::array<uint32_t, 3> analog  = { 10U, 20U, 30U };
    const EntryId neutral_id
        = add_exif_u32_array(&store, "ifd0", 0xC628U,
                             std::span<const uint32_t>(neutral.data(),
                                                       neutral.size()));
    const EntryId analog_id
        = add_exif_u32_array(&store, "ifd0", 0xC627U,
                             std::span<const uint32_t>(analog.data(),
                                                       analog.size()));
    store.finalize();

    const MetadataQueryResult result = query_white_balance_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::WhiteBalance,
                                    MetadataQueryValueShape::VectorSet, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_GE(candidate->confidence, 90U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, neutral_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, analog_id));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 6U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[1], 2.0);
    EXPECT_DOUBLE_EQ(candidate->values[2], 3.0);
    EXPECT_DOUBLE_EQ(candidate->values[3], 10.0);
    EXPECT_DOUBLE_EQ(candidate->values[4], 20.0);
    EXPECT_DOUBLE_EQ(candidate->values[5], 30.0);
    EXPECT_STREQ(metadata_query_value_shape_name(
                     MetadataQueryValueShape::VectorSet),
                 "vector_set");
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

TEST(MetadataQuery, GroupsVendorLensCorrectionTable)
{
    MetaStore store;
    const EntryId distort_id  = add_exif_u32(&store, "mk_nikon_distortinfo",
                                             0x0001U, 7U);
    const EntryId vignette_id = add_exif_u32(&store, "mk_nikon_vignette",
                                             0x0001U, 3U);
    store.finalize();

    const MetadataQueryResult result = query_lens_correction_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::LensCorrection,
                                    MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_GE(candidate->confidence, 90U);
    EXPECT_TRUE(contains_entry(candidate->source_entries, distort_id));
    EXPECT_TRUE(contains_entry(candidate->source_entries, vignette_id));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 2U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 7.0);
    EXPECT_DOUBLE_EQ(candidate->values[1], 3.0);
    EXPECT_STREQ(metadata_query_value_shape_name(MetadataQueryValueShape::Table),
                 "table");
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

TEST(MetadataQuery, MatchesDngRawProcessingLevels)
{
    MetaStore store;
    const std::array<uint32_t, 2> linearization = { 0U, 65535U };
    const EntryId black_id = add_exif_u32(&store, "ifd0", 0xC61AU, 512U);
    const EntryId white_id = add_exif_u32(&store, "ifd0", 0xC61DU, 16383U);
    const EntryId linearization_id
        = add_exif_u32_array(&store, "ifd0", 0xC618U,
                             std::span<const uint32_t>(linearization.data(),
                                                       linearization.size()));
    store.finalize();

    const MetadataQueryResult result = query_raw_processing_metadata(store);

    EXPECT_EQ(result.kind, MetadataQueryKind::RawProcessing);
    const MetadataQueryMatch* black_match = find_match_for_entry(result,
                                                                 black_id);
    ASSERT_NE(black_match, nullptr);
    EXPECT_EQ(black_match->semantic, MetadataQuerySemanticKind::BlackLevel);
    const MetadataQueryMatch* white_match = find_match_for_entry(result,
                                                                 white_id);
    ASSERT_NE(white_match, nullptr);
    EXPECT_EQ(white_match->semantic, MetadataQuerySemanticKind::WhiteLevel);
    const MetadataQueryMatch* linearization_match
        = find_match_for_entry(result, linearization_id);
    ASSERT_NE(linearization_match, nullptr);
    EXPECT_EQ(linearization_match->semantic,
              MetadataQuerySemanticKind::Linearization);

    const MetadataQueryCandidate* black_candidate
        = find_candidate_for_entry(result, black_id);
    ASSERT_NE(black_candidate, nullptr);
    ASSERT_TRUE(black_candidate->has_values);
    ASSERT_EQ(black_candidate->values.size(), 1U);
    EXPECT_DOUBLE_EQ(black_candidate->values[0], 512.0);
}

TEST(MetadataQuery, GroupsDngBlackLevelAndCfaTables)
{
    MetaStore store;
    const std::array<uint32_t, 2> repeat_dim = { 2U, 2U };
    const std::array<uint32_t, 4> black      = { 512U, 513U, 514U, 515U };
    const std::array<uint32_t, 2> cfa_dim    = { 2U, 2U };
    const std::array<uint32_t, 4> cfa        = { 0U, 1U, 1U, 2U };
    const EntryId repeat_id
        = add_exif_u32_array(&store, "ifd0", 0xC619U,
                             std::span<const uint32_t>(repeat_dim.data(),
                                                       repeat_dim.size()));
    const EntryId black_id
        = add_exif_u32_array(&store, "ifd0", 0xC61AU,
                             std::span<const uint32_t>(black.data(),
                                                       black.size()));
    const EntryId cfa_dim_id
        = add_exif_u32_array(&store, "ifd0", 0x828DU,
                             std::span<const uint32_t>(cfa_dim.data(),
                                                       cfa_dim.size()));
    const EntryId cfa_id
        = add_exif_u32_array(&store, "ifd0", 0x828EU,
                             std::span<const uint32_t>(cfa.data(), cfa.size()));
    store.finalize();

    const MetadataQueryResult result = query_raw_processing_metadata(store);

    const MetadataQueryCandidate* black_candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::BlackLevel,
                                    MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(black_candidate, nullptr);
    EXPECT_TRUE(contains_entry(black_candidate->source_entries, repeat_id));
    EXPECT_TRUE(contains_entry(black_candidate->source_entries, black_id));
    ASSERT_TRUE(black_candidate->has_values);
    ASSERT_EQ(black_candidate->values.size(), 6U);
    EXPECT_DOUBLE_EQ(black_candidate->values[0], 2.0);
    EXPECT_DOUBLE_EQ(black_candidate->values[2], 512.0);
    EXPECT_DOUBLE_EQ(black_candidate->values[5], 515.0);

    const MetadataQueryCandidate* cfa_candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::CfaLayout,
                                    MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(cfa_candidate, nullptr);
    EXPECT_TRUE(contains_entry(cfa_candidate->source_entries, cfa_dim_id));
    EXPECT_TRUE(contains_entry(cfa_candidate->source_entries, cfa_id));
    ASSERT_TRUE(cfa_candidate->has_values);
    ASSERT_EQ(cfa_candidate->values.size(), 6U);
    EXPECT_DOUBLE_EQ(cfa_candidate->values[0], 2.0);
    EXPECT_DOUBLE_EQ(cfa_candidate->values[2], 0.0);
    EXPECT_DOUBLE_EQ(cfa_candidate->values[5], 2.0);
}

TEST(MetadataQuery, GroupsDngRawStorageTable)
{
    MetaStore store;
    const std::array<uint32_t, 4> raw_id = { 1U, 2U, 3U, 4U };
    const EntryId raw_id_entry
        = add_exif_u32_array(&store, "ifd0", 0xC65DU,
                             std::span<const uint32_t>(raw_id.data(),
                                                       raw_id.size()));
    const EntryId raw_name_entry = add_exif_text(&store, "ifd0", 0xC68BU,
                                                 "source.raw");
    store.finalize();

    const MetadataQueryResult result = query_raw_processing_metadata(store);

    const MetadataQueryCandidate* candidate
        = find_candidate_with_shape(result,
                                    MetadataQuerySemanticKind::RawStorage,
                                    MetadataQueryValueShape::Table, 2U);
    ASSERT_NE(candidate, nullptr);
    EXPECT_TRUE(contains_entry(candidate->source_entries, raw_id_entry));
    EXPECT_TRUE(contains_entry(candidate->source_entries, raw_name_entry));
    ASSERT_TRUE(candidate->has_values);
    ASSERT_EQ(candidate->values.size(), 4U);
    EXPECT_DOUBLE_EQ(candidate->values[0], 1.0);
    EXPECT_DOUBLE_EQ(candidate->values[3], 4.0);
    EXPECT_STREQ(metadata_query_kind_name(result.kind), "raw_processing");
    EXPECT_STREQ(metadata_query_semantic_kind_name(
                     MetadataQuerySemanticKind::CfaLayout),
                 "cfa_layout");
}

}  // namespace openmeta
