// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_interpretation.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static EntryId add_exif_u32(MetaStore* store, std::string_view ifd,
                                uint16_t tag, uint32_t value)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_u32(value);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_u32_array(MetaStore* store, std::string_view ifd,
                                      uint16_t tag,
                                      std::span<const uint32_t> values)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_u32_array(store->arena(), values);
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

    static EntryId add_iptc_text(MetaStore* store, uint16_t record,
                                 uint16_t dataset, std::string_view value)
    {
        if (!store) {
            return kInvalidEntryId;
        }
        Entry entry;
        entry.key        = make_iptc_dataset_key(record, dataset);
        entry.value      = make_text(store->arena(), value, TextEncoding::Utf8);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
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

    static const MetadataInterpretationRecord*
    find_record(const MetadataInterpretationResult& result,
                MetadataQuerySemanticKind semantic,
                MetadataQueryValueShape shape)
    {
        for (size_t i = 0U; i < result.records.size(); ++i) {
            const MetadataInterpretationRecord& record = result.records[i];
            if (record.semantic == semantic && record.shape == shape) {
                return &record;
            }
        }
        return nullptr;
    }

    TEST(MetadataInterpretation, BuildsStructuredRecordsForCoreClasses)
    {
        MetaStore store;
        const std::array<uint32_t, 2> crop_origin_values = { 4U, 8U };
        const std::array<uint32_t, 2> crop_size_values   = { 640U, 480U };
        const std::span<const uint32_t> crop_origin_span(
            crop_origin_values.data(), crop_origin_values.size());
        const std::span<const uint32_t> crop_size_span(crop_size_values.data(),
                                                       crop_size_values.size());
        const EntryId crop_origin = add_exif_u32_array(&store, "ifd0", 0xC61FU,
                                                       crop_origin_span);
        const EntryId crop_size   = add_exif_u32_array(&store, "ifd0", 0xC620U,
                                                       crop_size_span);
        const EntryId orientation = add_exif_u32(&store, "ifd0", 0x0112U, 6U);
        const EntryId exposure = add_exif_urational(&store, "exififd", 0x829AU,
                                                    1U, 125U);
        const EntryId wb
            = add_xmp_text(&store,
                           "http://ns.adobe.com/camera-raw-settings/1.0/",
                           "crs:WhiteBalance", "As Shot");
        const std::array<uint32_t, 9> matrix = {
            1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
        };
        const EntryId color
            = add_exif_u32_array(&store, "ifd0", 0xC621U,
                                 std::span<const uint32_t>(matrix.data(),
                                                           matrix.size()));
        const EntryId color_profile
            = add_xmp_text(&store, "http://ns.adobe.com/photoshop/1.0/",
                           "photoshop:ICCProfile", "sRGB IEC61966-2.1");
        const EntryId raw     = add_exif_u32(&store, "ifd0", 0xC61AU, 512U);
        const EntryId source  = add_exif_u32(&store, "mk_google_shotlogdata",
                                             0x0001U, 7U);
        const EntryId caption = add_iptc_text(&store, 2U, 120U,
                                              "Street after rain");
        store.finalize();

        const MetadataInterpretationResult result = interpret_metadata(store);

        const MetadataInterpretationRecord* crop
            = find_record(result, MetadataQuerySemanticKind::Crop,
                          MetadataQueryValueShape::Rect);
        ASSERT_NE(crop, nullptr);
        EXPECT_EQ(crop->query_kind, MetadataQueryKind::Crop);
        EXPECT_TRUE(contains_entry(crop->source_entries, crop_origin));
        EXPECT_TRUE(contains_entry(crop->source_entries, crop_size));
        ASSERT_TRUE(crop->has_rect);
        EXPECT_DOUBLE_EQ(crop->rect[0], 4.0);
        EXPECT_DOUBLE_EQ(crop->rect[1], 8.0);
        EXPECT_DOUBLE_EQ(crop->rect[2], 640.0);
        EXPECT_DOUBLE_EQ(crop->rect[3], 480.0);

        const MetadataInterpretationRecord* orient
            = find_record(result, MetadataQuerySemanticKind::Orientation,
                          MetadataQueryValueShape::Scalar);
        ASSERT_NE(orient, nullptr);
        EXPECT_TRUE(contains_entry(orient->source_entries, orientation));
        ASSERT_TRUE(orient->has_values);
        ASSERT_EQ(orient->values.size(), 1U);
        EXPECT_DOUBLE_EQ(orient->values[0], 6.0);

        const MetadataInterpretationRecord* exp
            = find_record(result, MetadataQuerySemanticKind::Exposure,
                          MetadataQueryValueShape::Scalar);
        ASSERT_NE(exp, nullptr);
        EXPECT_TRUE(contains_entry(exp->source_entries, exposure));
        ASSERT_TRUE(exp->has_values);
        EXPECT_DOUBLE_EQ(exp->values[0], 0.008);

        const MetadataInterpretationRecord* white_balance
            = find_record(result, MetadataQuerySemanticKind::WhiteBalance,
                          MetadataQueryValueShape::Text);
        ASSERT_NE(white_balance, nullptr);
        EXPECT_TRUE(contains_entry(white_balance->source_entries, wb));

        const MetadataInterpretationRecord* color_matrix
            = find_record(result, MetadataQuerySemanticKind::ColorMatrix,
                          MetadataQueryValueShape::Matrix3x3);
        ASSERT_NE(color_matrix, nullptr);
        EXPECT_TRUE(contains_entry(color_matrix->source_entries, color));
        ASSERT_TRUE(color_matrix->has_values);
        ASSERT_EQ(color_matrix->values.size(), 9U);
        EXPECT_DOUBLE_EQ(color_matrix->values[0], 1.0);
        EXPECT_DOUBLE_EQ(color_matrix->values[4], 1.0);
        EXPECT_DOUBLE_EQ(color_matrix->values[8], 1.0);

        const MetadataInterpretationRecord* profile
            = find_record(result, MetadataQuerySemanticKind::ColorProfile,
                          MetadataQueryValueShape::Text);
        ASSERT_NE(profile, nullptr);
        EXPECT_EQ(profile->query_kind, MetadataQueryKind::Color);
        EXPECT_TRUE(contains_entry(profile->source_entries, color_profile));

        const MetadataInterpretationRecord* black
            = find_record(result, MetadataQuerySemanticKind::BlackLevel,
                          MetadataQueryValueShape::Scalar);
        ASSERT_NE(black, nullptr);
        EXPECT_TRUE(contains_entry(black->source_entries, raw));
        ASSERT_TRUE(black->has_values);
        EXPECT_DOUBLE_EQ(black->values[0], 512.0);

        const MetadataInterpretationRecord* source_processing
            = find_record(result, MetadataQuerySemanticKind::SourceProcessing,
                          MetadataQueryValueShape::Scalar);
        ASSERT_NE(source_processing, nullptr);
        EXPECT_TRUE(contains_entry(source_processing->source_entries, source));
        ASSERT_TRUE(source_processing->has_values);
        EXPECT_DOUBLE_EQ(source_processing->values[0], 7.0);

        const MetadataInterpretationRecord* description
            = find_record(result, MetadataQuerySemanticKind::Description,
                          MetadataQueryValueShape::Text);
        ASSERT_NE(description, nullptr);
        EXPECT_EQ(description->query_kind, MetadataQueryKind::Descriptive);
        EXPECT_TRUE(contains_entry(description->source_entries, caption));
    }

    TEST(MetadataInterpretation, CanInterpretSingleQueryClass)
    {
        MetaStore store;
        const EntryId orientation = add_exif_u32(&store, "ifd0", 0x0112U, 8U);
        (void)add_exif_u32(&store, "ifd0", 0xC61AU, 512U);
        store.finalize();

        const MetadataInterpretationResult result
            = interpret_metadata_query(store, MetadataQueryKind::Orientation);

        ASSERT_EQ(result.records.size(), 1U);
        const MetadataInterpretationRecord& record = result.records[0];
        EXPECT_EQ(record.query_kind, MetadataQueryKind::Orientation);
        EXPECT_EQ(record.semantic, MetadataQuerySemanticKind::Orientation);
        EXPECT_TRUE(contains_entry(record.source_entries, orientation));
        ASSERT_TRUE(record.has_values);
        EXPECT_DOUBLE_EQ(record.values[0], 8.0);
    }

    TEST(MetadataInterpretation, PreservesGroupedVendorRawProcessingRecords)
    {
        MetaStore store;
        const EntryId shot_log_a = add_exif_u32(&store, "mk_google_shotlogdata",
                                                0x0001U, 1U);
        const EntryId shot_log_b = add_exif_u32(&store, "mk_google_shotlogdata",
                                                0x0002U, 2U);
        store.finalize();

        const MetadataInterpretationResult result
            = interpret_metadata_query(store, MetadataQueryKind::RawProcessing);

        const MetadataInterpretationRecord* record
            = find_record(result, MetadataQuerySemanticKind::SourceProcessing,
                          MetadataQueryValueShape::Table);
        ASSERT_NE(record, nullptr);
        EXPECT_EQ(record->query_kind, MetadataQueryKind::RawProcessing);
        EXPECT_TRUE(contains_entry(record->source_entries, shot_log_a));
        EXPECT_TRUE(contains_entry(record->source_entries, shot_log_b));
        ASSERT_TRUE(record->has_values);
        ASSERT_EQ(record->values.size(), 2U);
        EXPECT_DOUBLE_EQ(record->values[0], 1.0);
        EXPECT_DOUBLE_EQ(record->values[1], 2.0);
    }

}  // namespace
}  // namespace openmeta
