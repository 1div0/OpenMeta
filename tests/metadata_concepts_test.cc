// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/metadata_concepts.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static EntryId add_exif_u16(MetaStore* store, std::string_view ifd,
                                uint16_t tag, uint16_t value)
    {
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_u16(value);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_u32(MetaStore* store, std::string_view ifd,
                                uint16_t tag, uint32_t value)
    {
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_u32(value);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_text(MetaStore* store, std::string_view ifd,
                                 uint16_t tag, std::string_view value)
    {
        Entry entry;
        entry.key   = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value = make_text(store->arena(), value, TextEncoding::Ascii);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_urational_array(MetaStore* store,
                                            std::string_view ifd, uint16_t tag,
                                            std::span<const URational> values)
    {
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_urational_array(store->arena(), values);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_u32_array(MetaStore* store, std::string_view ifd,
                                      uint16_t tag,
                                      std::span<const uint32_t> values)
    {
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_u32_array(store->arena(), values);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_xmp_text(MetaStore* store, std::string_view ns,
                                std::string_view path, std::string_view value)
    {
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
        Entry entry;
        entry.key   = make_iptc_dataset_key(record, dataset);
        entry.value = make_text(store->arena(), value, TextEncoding::Ascii);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_icc_header_u32(MetaStore* store, uint32_t offset,
                                      uint32_t value)
    {
        Entry entry;
        entry.key        = make_icc_header_field_key(offset);
        entry.value      = make_u32(value);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static const MetadataConceptResolution*
    find_concept(const MetadataConceptResult& result, MetadataConceptKind kind)
    {
        for (size_t i = 0U; i < result.concepts.size(); ++i) {
            if (result.concepts[i].kind == kind) {
                return &result.concepts[i];
            }
        }
        return nullptr;
    }

    static const MetadataConceptCandidate*
    find_role(const MetadataConceptResolution& resolution,
              MetadataConceptRole role)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            if (resolution.candidates[i].role == role) {
                return &resolution.candidates[i];
            }
        }
        return nullptr;
    }

    static const MetadataConceptCandidate*
    find_role_family(const MetadataConceptResolution& resolution,
                     MetadataConceptRole role,
                     MetadataConceptSourceFamily family)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = resolution.candidates[i];
            if (candidate.role == role && candidate.family == family) {
                return &candidate;
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

    TEST(MetadataConcepts, ResolvesCoreCrossFamilyConcepts)
    {
        MetaStore store;
        const EntryId exif_orientation = add_exif_u16(&store, "ifd0", 0x0112U,
                                                      6U);
        const EntryId xmp_orientation
            = add_xmp_text(&store, "http://ns.adobe.com/tiff/1.0/",
                           "tiff:Orientation", "8");

        const EntryId exif_created = add_exif_text(&store, "exififd", 0x9003U,
                                                   "2024:04:19 12:34:56");
        (void)add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/",
                           "xmp:CreateDate", "2024-04-19T12:34:56Z");
        (void)add_xmp_text(&store, "http://ns.adobe.com/xap/1.0/",
                           "xmp:ModifyDate", "2024-04-20T01:02:03Z");
        (void)add_iptc_text(&store, 2U, 55U, "20240419");
        (void)add_iptc_text(&store, 2U, 60U, "123456+0000");

        const EntryId exif_colorspace = add_exif_u16(&store, "exififd", 0xA001U,
                                                     1U);
        const EntryId icc_colorspace  = add_icc_header_u32(&store, 16U,
                                                           0x52474220U);
        (void)add_xmp_text(&store, "http://ns.adobe.com/photoshop/1.0/",
                           "photoshop:ICCProfile", "sRGB IEC61966-2.1");

        (void)add_exif_text(&store, "gpsifd", 0x0001U, "N");
        const std::array<URational, 3> lat = {
            URational { 41U, 1U },
            URational { 24U, 1U },
            URational { 30U, 1U },
        };
        const EntryId latitude
            = add_exif_urational_array(&store, "gpsifd", 0x0002U,
                                       std::span<const URational>(lat.data(),
                                                                  lat.size()));
        (void)add_exif_text(&store, "gpsifd", 0x0003U, "E");
        const std::array<URational, 3> lon = {
            URational { 2U, 1U },
            URational { 9U, 1U },
            URational { 0U, 1U },
        };
        const EntryId longitude
            = add_exif_urational_array(&store, "gpsifd", 0x0004U,
                                       std::span<const URational>(lon.data(),
                                                                  lon.size()));
        (void)add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/",
                           "exif:GPSLatitude", "41,24.500N");
        const EntryId altitude_ref = add_exif_u16(&store, "gpsifd", 0x0005U,
                                                  1U);
        const std::array<URational, 1> alt = {
            URational { 100U, 1U },
        };
        const EntryId altitude
            = add_exif_urational_array(&store, "gpsifd", 0x0006U,
                                       std::span<const URational>(alt.data(),
                                                                  alt.size()));
        const EntryId gps_date = add_exif_text(&store, "gpsifd", 0x001DU,
                                               "2024:04:19");
        const std::array<URational, 3> gps_time = {
            URational { 12U, 1U },
            URational { 34U, 1U },
            URational { 56U, 1U },
        };
        const EntryId gps_time_id = add_exif_urational_array(
            &store, "gpsifd", 0x0007U,
            std::span<const URational>(gps_time.data(), gps_time.size()));
        const std::array<uint32_t, 2> crop_origin_values = { 12U, 34U };
        const std::array<uint32_t, 2> crop_size_values   = { 4000U, 3000U };
        const std::span<const uint32_t> crop_origin_span(
            crop_origin_values.data(), crop_origin_values.size());
        const std::span<const uint32_t> crop_size_span(crop_size_values.data(),
                                                       crop_size_values.size());
        const EntryId crop_origin = add_exif_u32_array(&store, "ifd0", 0xC61FU,
                                                       crop_origin_span);
        const EntryId crop_size   = add_exif_u32_array(&store, "ifd0", 0xC620U,
                                                       crop_size_span);
        const std::array<uint32_t, 4> active_area_values = {
            10U,
            20U,
            3010U,
            4020U,
        };
        const EntryId active_area = add_exif_u32_array(
            &store, "ifd0", 0xC68DU,
            std::span<const uint32_t>(active_area_values.data(),
                                      active_area_values.size()));
        const EntryId border_padding
            = add_xmp_text(&store, "http://example.invalid/aux/1.0/",
                           "aux:SensorBorderPadding", "64 32 168 128");
        store.finalize();

        const MetadataConceptResult result = resolve_metadata_concepts(store);

        const MetadataConceptResolution* orientation
            = find_concept(result, MetadataConceptKind::Orientation);
        ASSERT_NE(orientation, nullptr);
        EXPECT_TRUE(orientation->found);
        EXPECT_TRUE(orientation->conflict);
        EXPECT_EQ(orientation->preferred_entry, exif_orientation);
        ASSERT_NE(find_role_family(*orientation,
                                   MetadataConceptRole::Orientation,
                                   MetadataConceptSourceFamily::Exif),
                  nullptr);
        ASSERT_NE(find_role_family(*orientation,
                                   MetadataConceptRole::Orientation,
                                   MetadataConceptSourceFamily::Xmp),
                  nullptr);
        EXPECT_NE(xmp_orientation, kInvalidEntryId);

        const MetadataConceptResolution* datetime
            = find_concept(result, MetadataConceptKind::DateTime);
        ASSERT_NE(datetime, nullptr);
        EXPECT_TRUE(datetime->found);
        EXPECT_FALSE(datetime->conflict);
        EXPECT_EQ(datetime->preferred_entry, exif_created);
        const MetadataConceptCandidate* created
            = find_role(*datetime, MetadataConceptRole::Created);
        ASSERT_NE(created, nullptr);
        EXPECT_TRUE(created->has_date_time);
        EXPECT_TRUE(created->date_time_has_time);
        EXPECT_EQ(created->date_time_year, 2024);
        EXPECT_EQ(created->date_time_month, 4U);
        EXPECT_EQ(created->date_time_day, 19U);
        EXPECT_EQ(created->date_time_precision,
                  MetadataConceptDateTimePrecision::DateTime);
        EXPECT_EQ(created->date_time_zone, MetadataConceptTimeZoneKind::Local);
        const MetadataConceptCandidate* xmp_created
            = find_role_family(*datetime, MetadataConceptRole::Created,
                               MetadataConceptSourceFamily::Xmp);
        ASSERT_NE(xmp_created, nullptr);
        EXPECT_TRUE(xmp_created->date_time_has_utc_offset);
        EXPECT_EQ(xmp_created->date_time_utc_offset_min, 0);
        EXPECT_EQ(xmp_created->date_time_zone,
                  MetadataConceptTimeZoneKind::Utc);
        ASSERT_NE(find_role(*datetime, MetadataConceptRole::Modified), nullptr);
        const MetadataConceptCandidate* iptc_created
            = find_role(*datetime, MetadataConceptRole::DateCreated);
        ASSERT_NE(iptc_created, nullptr);
        EXPECT_TRUE(iptc_created->has_date_time);
        EXPECT_TRUE(iptc_created->date_time_has_time);
        EXPECT_EQ(iptc_created->date_time_zone,
                  MetadataConceptTimeZoneKind::Utc);

        const MetadataConceptResolution* color
            = find_concept(result, MetadataConceptKind::ColorProfile);
        ASSERT_NE(color, nullptr);
        EXPECT_TRUE(color->found);
        ASSERT_NE(find_role_family(*color, MetadataConceptRole::ColorSpace,
                                   MetadataConceptSourceFamily::Exif),
                  nullptr);
        ASSERT_NE(find_role_family(*color, MetadataConceptRole::ColorSpace,
                                   MetadataConceptSourceFamily::Icc),
                  nullptr);
        EXPECT_NE(exif_colorspace, kInvalidEntryId);
        EXPECT_NE(icc_colorspace, kInvalidEntryId);

        const MetadataConceptResolution* gps
            = find_concept(result, MetadataConceptKind::Gps);
        ASSERT_NE(gps, nullptr);
        EXPECT_TRUE(gps->found);
        const MetadataConceptCandidate* lat_candidate
            = find_role(*gps, MetadataConceptRole::Latitude);
        ASSERT_NE(lat_candidate, nullptr);
        ASSERT_TRUE(lat_candidate->has_numeric);
        EXPECT_NEAR(lat_candidate->numeric[0], 41.408333333, 0.000001);
        const MetadataConceptCandidate* lon_candidate
            = find_role(*gps, MetadataConceptRole::Longitude);
        ASSERT_NE(lon_candidate, nullptr);
        ASSERT_TRUE(lon_candidate->has_numeric);
        EXPECT_NEAR(lon_candidate->numeric[0], 2.15, 0.000001);
        const MetadataConceptCandidate* altitude_candidate
            = find_role(*gps, MetadataConceptRole::Altitude);
        ASSERT_NE(altitude_candidate, nullptr);
        ASSERT_TRUE(altitude_candidate->has_numeric);
        EXPECT_NEAR(altitude_candidate->numeric[0], -100.0, 0.000001);
        EXPECT_TRUE(altitude_candidate->has_gps_altitude_reference);
        EXPECT_TRUE(altitude_candidate->gps_altitude_below_sea_level);
        EXPECT_EQ(altitude_candidate->gps_altitude_reference_code, 1U);
        EXPECT_TRUE(
            contains_entry(altitude_candidate->source_entries, altitude_ref));
        EXPECT_TRUE(
            contains_entry(altitude_candidate->source_entries, altitude));
        const MetadataConceptCandidate* gps_timestamp
            = find_role(*gps, MetadataConceptRole::Timestamp);
        ASSERT_NE(gps_timestamp, nullptr);
        EXPECT_TRUE(gps_timestamp->has_date_time);
        EXPECT_TRUE(gps_timestamp->date_time_has_time);
        EXPECT_TRUE(gps_timestamp->date_time_has_utc_offset);
        EXPECT_EQ(gps_timestamp->date_time_hour, 12U);
        EXPECT_EQ(gps_timestamp->date_time_minute, 34U);
        EXPECT_EQ(gps_timestamp->date_time_second, 56U);
        EXPECT_EQ(gps_timestamp->date_time_precision,
                  MetadataConceptDateTimePrecision::DateTime);
        EXPECT_EQ(gps_timestamp->date_time_zone,
                  MetadataConceptTimeZoneKind::Utc);
        EXPECT_TRUE(contains_entry(gps_timestamp->source_entries, gps_date));
        EXPECT_TRUE(contains_entry(gps_timestamp->source_entries, gps_time_id));
        EXPECT_EQ(gps->preferred_entry, latitude);
        EXPECT_NE(longitude, kInvalidEntryId);

        const MetadataConceptResolution* geometry
            = find_concept(result, MetadataConceptKind::Geometry);
        ASSERT_NE(geometry, nullptr);
        EXPECT_TRUE(geometry->found);
        const MetadataConceptCandidate* crop
            = find_role(*geometry, MetadataConceptRole::Crop);
        ASSERT_NE(crop, nullptr);
        ASSERT_TRUE(crop->has_origin);
        EXPECT_DOUBLE_EQ(crop->origin[0], 12.0);
        EXPECT_DOUBLE_EQ(crop->origin[1], 34.0);
        ASSERT_TRUE(crop->has_size);
        EXPECT_DOUBLE_EQ(crop->size[0], 4000.0);
        EXPECT_DOUBLE_EQ(crop->size[1], 3000.0);
        ASSERT_TRUE(crop->has_rect);
        EXPECT_DOUBLE_EQ(crop->rect[0], 12.0);
        EXPECT_DOUBLE_EQ(crop->rect[1], 34.0);
        EXPECT_DOUBLE_EQ(crop->rect[2], 4000.0);
        EXPECT_DOUBLE_EQ(crop->rect[3], 3000.0);
        EXPECT_TRUE(contains_entry(crop->source_entries, crop_origin));
        EXPECT_TRUE(contains_entry(crop->source_entries, crop_size));

        const MetadataConceptCandidate* active
            = find_role(*geometry, MetadataConceptRole::ActiveArea);
        ASSERT_NE(active, nullptr);
        ASSERT_TRUE(active->has_rect);
        EXPECT_DOUBLE_EQ(active->rect[0], 20.0);
        EXPECT_DOUBLE_EQ(active->rect[1], 10.0);
        EXPECT_DOUBLE_EQ(active->rect[2], 4000.0);
        EXPECT_DOUBLE_EQ(active->rect[3], 3000.0);
        EXPECT_TRUE(contains_entry(active->source_entries, active_area));

        const MetadataConceptCandidate* border
            = find_role(*geometry, MetadataConceptRole::Border);
        ASSERT_NE(border, nullptr);
        ASSERT_TRUE(border->has_margins);
        EXPECT_DOUBLE_EQ(border->margins[0], 64.0);
        EXPECT_DOUBLE_EQ(border->margins[1], 32.0);
        EXPECT_DOUBLE_EQ(border->margins[2], 168.0);
        EXPECT_DOUBLE_EQ(border->margins[3], 128.0);
        EXPECT_TRUE(contains_entry(border->source_entries, border_padding));
    }

    TEST(MetadataConcepts, FlagsConflictingCreatedDatesAcrossFamilies)
    {
        MetaStore store;
        (void)add_exif_text(&store, "exififd", 0x9003U, "2024:04:19 12:34:56");
        (void)add_iptc_text(&store, 2U, 55U, "20250419");
        store.finalize();

        const MetadataConceptResolution datetime
            = resolve_metadata_concept(store, MetadataConceptKind::DateTime);

        EXPECT_TRUE(datetime.found);
        EXPECT_TRUE(datetime.conflict);
        const MetadataConceptCandidate* created
            = find_role(datetime, MetadataConceptRole::Created);
        ASSERT_NE(created, nullptr);
        EXPECT_TRUE(created->conflict);
        const MetadataConceptCandidate* date_created
            = find_role(datetime, MetadataConceptRole::DateCreated);
        ASSERT_NE(date_created, nullptr);
        EXPECT_TRUE(date_created->conflict);
    }

    TEST(MetadataConcepts, ResolvesSingleConcept)
    {
        MetaStore store;
        const EntryId xmp_color
            = add_xmp_text(&store, "http://ns.adobe.com/photoshop/1.0/",
                           "photoshop:ICCProfile", "Display P3");
        store.finalize();

        const MetadataConceptResolution color
            = resolve_metadata_concept(store,
                                       MetadataConceptKind::ColorProfile);

        EXPECT_TRUE(color.found);
        EXPECT_FALSE(color.conflict);
        EXPECT_EQ(color.preferred_entry, xmp_color);
        const MetadataConceptCandidate* candidate
            = find_role(color, MetadataConceptRole::IccProfile);
        ASSERT_NE(candidate, nullptr);
        EXPECT_TRUE(candidate->preferred);
        EXPECT_EQ(candidate->family, MetadataConceptSourceFamily::Xmp);
        EXPECT_EQ(candidate->text, "Display P3");
    }

}  // namespace
}  // namespace openmeta
