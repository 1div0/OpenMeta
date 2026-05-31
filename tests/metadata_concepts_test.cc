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

    static EntryId add_exif_urational(MetaStore* store, std::string_view ifd,
                                      uint16_t tag, uint32_t numer,
                                      uint32_t denom)
    {
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_urational(numer, denom);
        const EntryId id = store->add_entry(entry);
        EXPECT_NE(id, kInvalidEntryId);
        return id;
    }

    static EntryId add_exif_srational(MetaStore* store, std::string_view ifd,
                                      uint16_t tag, int32_t numer,
                                      int32_t denom)
    {
        Entry entry;
        entry.key        = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value      = make_srational(numer, denom);
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
    find_role_shape(const MetadataConceptResolution& resolution,
                    MetadataConceptRole role, MetadataQueryValueShape shape)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = resolution.candidates[i];
            if (candidate.role == role && candidate.shape == shape) {
                return &candidate;
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

    static const MetadataConceptCandidate*
    find_role_entries(const MetadataConceptResolution& resolution,
                      MetadataConceptRole role, EntryId first, EntryId second)
    {
        for (size_t i = 0U; i < resolution.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = resolution.candidates[i];
            if (candidate.role == role
                && contains_entry(candidate.source_entries, first)
                && contains_entry(candidate.source_entries, second)) {
                return &candidate;
            }
        }
        return nullptr;
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
        const std::array<uint32_t, 9> color_matrix_values = {
            1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
        };
        const EntryId color_matrix = add_exif_u32_array(
            &store, "ifd0", 0xC621U,
            std::span<const uint32_t>(color_matrix_values.data(),
                                      color_matrix_values.size()));
        const std::array<uint32_t, 3> wb_neutral_values = { 1U, 2U, 3U };
        const std::array<uint32_t, 3> wb_analog_values  = { 10U, 20U, 30U };
        const EntryId wb_neutral                        = add_exif_u32_array(
            &store, "ifd0", 0xC628U,
            std::span<const uint32_t>(wb_neutral_values.data(),
                                                             wb_neutral_values.size()));
        const EntryId wb_analog = add_exif_u32_array(
            &store, "ifd0", 0xC627U,
            std::span<const uint32_t>(wb_analog_values.data(),
                                      wb_analog_values.size()));

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
        const EntryId lens_distort
            = add_exif_u32(&store, "mk_nikon_distortinfo", 0x0001U, 7U);
        const EntryId lens_vignette = add_exif_u32(&store, "mk_nikon_vignette",
                                                   0x0001U, 3U);
        const std::array<uint32_t, 2> linearization_values = { 0U, 65535U };
        const EntryId black_level = add_exif_u32(&store, "ifd0", 0xC61AU, 512U);
        const EntryId linearization = add_exif_u32_array(
            &store, "ifd0", 0xC618U,
            std::span<const uint32_t>(linearization_values.data(),
                                      linearization_values.size()));
        const std::array<uint32_t, 4> raw_id_values = { 1U, 2U, 3U, 4U };
        const EntryId raw_id                        = add_exif_u32_array(
            &store, "ifd0", 0xC65DU,
            std::span<const uint32_t>(raw_id_values.data(),
                                                             raw_id_values.size()));
        const EntryId raw_name = add_exif_text(&store, "ifd0", 0xC68BU,
                                               "source.raw");
        const EntryId source_processing
            = add_exif_u32(&store, "mk_google_shotlogdata", 0x0001U, 7U);
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
        const MetadataConceptCandidate* matrix_candidate
            = find_role(*color, MetadataConceptRole::ColorMatrix);
        ASSERT_NE(matrix_candidate, nullptr);
        EXPECT_EQ(matrix_candidate->shape, MetadataQueryValueShape::Matrix3x3);
        ASSERT_TRUE(matrix_candidate->has_values);
        ASSERT_EQ(matrix_candidate->values.size(), 9U);
        EXPECT_DOUBLE_EQ(matrix_candidate->values[0], 1.0);
        EXPECT_DOUBLE_EQ(matrix_candidate->values[4], 1.0);
        EXPECT_DOUBLE_EQ(matrix_candidate->values[8], 1.0);
        EXPECT_TRUE(
            contains_entry(matrix_candidate->source_entries, color_matrix));
        const MetadataConceptCandidate* wb_candidate
            = find_role_shape(*color, MetadataConceptRole::WhiteBalance,
                              MetadataQueryValueShape::VectorSet);
        ASSERT_NE(wb_candidate, nullptr);
        EXPECT_EQ(wb_candidate->shape, MetadataQueryValueShape::VectorSet);
        ASSERT_TRUE(wb_candidate->has_values);
        ASSERT_EQ(wb_candidate->values.size(), 6U);
        EXPECT_DOUBLE_EQ(wb_candidate->values[0], 1.0);
        EXPECT_DOUBLE_EQ(wb_candidate->values[3], 10.0);
        EXPECT_TRUE(contains_entry(wb_candidate->source_entries, wb_neutral));
        EXPECT_TRUE(contains_entry(wb_candidate->source_entries, wb_analog));
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

        const MetadataConceptResolution* lens
            = find_concept(result, MetadataConceptKind::LensCorrection);
        ASSERT_NE(lens, nullptr);
        EXPECT_TRUE(lens->found);
        const MetadataConceptCandidate* lens_candidate
            = find_role_shape(*lens, MetadataConceptRole::LensCorrection,
                              MetadataQueryValueShape::Table);
        ASSERT_NE(lens_candidate, nullptr);
        EXPECT_EQ(lens_candidate->shape, MetadataQueryValueShape::Table);
        ASSERT_TRUE(lens_candidate->has_values);
        ASSERT_EQ(lens_candidate->values.size(), 2U);
        EXPECT_DOUBLE_EQ(lens_candidate->values[0], 7.0);
        EXPECT_DOUBLE_EQ(lens_candidate->values[1], 3.0);
        EXPECT_TRUE(
            contains_entry(lens_candidate->source_entries, lens_distort));
        EXPECT_TRUE(
            contains_entry(lens_candidate->source_entries, lens_vignette));

        const MetadataConceptResolution* raw
            = find_concept(result, MetadataConceptKind::RawProcessing);
        ASSERT_NE(raw, nullptr);
        EXPECT_TRUE(raw->found);
        const MetadataConceptCandidate* black
            = find_role(*raw, MetadataConceptRole::BlackLevel);
        ASSERT_NE(black, nullptr);
        ASSERT_TRUE(black->has_values);
        EXPECT_DOUBLE_EQ(black->values[0], 512.0);
        EXPECT_TRUE(contains_entry(black->source_entries, black_level));
        const MetadataConceptCandidate* linear
            = find_role(*raw, MetadataConceptRole::RawValueCurve);
        ASSERT_NE(linear, nullptr);
        ASSERT_TRUE(linear->has_values);
        EXPECT_DOUBLE_EQ(linear->values[1], 65535.0);
        EXPECT_TRUE(contains_entry(linear->source_entries, linearization));
        const MetadataConceptCandidate* storage
            = find_role_shape(*raw, MetadataConceptRole::RawStorage,
                              MetadataQueryValueShape::Table);
        ASSERT_NE(storage, nullptr);
        EXPECT_EQ(storage->shape, MetadataQueryValueShape::Table);
        EXPECT_TRUE(contains_entry(storage->source_entries, raw_id));
        EXPECT_TRUE(contains_entry(storage->source_entries, raw_name));
        const MetadataConceptCandidate* source
            = find_role(*raw, MetadataConceptRole::ComputationalProcessing);
        ASSERT_NE(source, nullptr);
        ASSERT_TRUE(source->has_values);
        EXPECT_DOUBLE_EQ(source->values[0], 7.0);
        EXPECT_TRUE(contains_entry(source->source_entries, source_processing));
    }

    TEST(MetadataConcepts, ResolvesFujifilmRafRawCropAsTargetOwnedGeometry)
    {
        MetaStore store;
        const std::array<uint32_t, 2> full_size = { 4032U, 3024U };
        const std::array<uint32_t, 2> top_left  = { 16U, 8U };
        const std::array<uint32_t, 2> crop_size = { 4000U, 3000U };
        const EntryId full_id
            = add_exif_u32_array(&store, "raf_0", 0x0100U,
                                 std::span<const uint32_t>(full_size.data(),
                                                           full_size.size()));
        const EntryId top_left_id
            = add_exif_u32_array(&store, "raf_0", 0x0110U,
                                 std::span<const uint32_t>(top_left.data(),
                                                           top_left.size()));
        const EntryId size_id
            = add_exif_u32_array(&store, "raf_0", 0x0111U,
                                 std::span<const uint32_t>(crop_size.data(),
                                                           crop_size.size()));
        store.finalize();

        const MetadataConceptResult result = resolve_metadata_concepts(store);

        const MetadataConceptResolution* geometry
            = find_concept(result, MetadataConceptKind::Geometry);
        ASSERT_NE(geometry, nullptr);
        EXPECT_TRUE(geometry->found);
        const MetadataConceptCandidate* active
            = find_role(*geometry, MetadataConceptRole::ActiveArea);
        ASSERT_NE(active, nullptr);
        EXPECT_EQ(active->shape, MetadataQueryValueShape::Rect);
        EXPECT_EQ(active->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        EXPECT_TRUE(active->compatible_file_safe);
        EXPECT_FALSE(active->rendered_image_safe);
        EXPECT_TRUE(active->requires_target_image_spec);
        EXPECT_FALSE(active->source_bound);
        EXPECT_TRUE(contains_entry(active->source_entries, full_id));
        EXPECT_TRUE(contains_entry(active->source_entries, top_left_id));
        EXPECT_TRUE(contains_entry(active->source_entries, size_id));
        ASSERT_TRUE(active->has_rect);
        EXPECT_DOUBLE_EQ(active->rect[0], 16.0);
        EXPECT_DOUBLE_EQ(active->rect[1], 8.0);
        EXPECT_DOUBLE_EQ(active->rect[2], 4000.0);
        EXPECT_DOUBLE_EQ(active->rect[3], 3000.0);
        ASSERT_TRUE(active->has_margins);
        EXPECT_DOUBLE_EQ(active->margins[0], 16.0);
        EXPECT_DOUBLE_EQ(active->margins[1], 8.0);
        EXPECT_DOUBLE_EQ(active->margins[2], 16.0);
        EXPECT_DOUBLE_EQ(active->margins[3], 16.0);
    }

    TEST(MetadataConcepts, ResolvesVendorRawGeometryAsTargetOwnedConcepts)
    {
        MetaStore store;
        const EntryId canon_width
            = add_exif_u32(&store, "mk_canon_aspectinfo_0", 0x0001U, 4000U);
        const EntryId canon_height
            = add_exif_u32(&store, "mk_canon_aspectinfo_0", 0x0002U, 3000U);
        const EntryId canon_left = add_exif_u32(&store, "mk_canon_aspectinfo_0",
                                                0x0003U, 12U);
        const EntryId canon_top  = add_exif_u32(&store, "mk_canon_aspectinfo_0",
                                                0x0004U, 8U);

        const EntryId margin_left  = add_exif_u32(&store, "mk_canon_cropinfo_0",
                                                  0x0000U, 16U);
        const EntryId margin_right = add_exif_u32(&store, "mk_canon_cropinfo_0",
                                                  0x0001U, 20U);
        const EntryId margin_top   = add_exif_u32(&store, "mk_canon_cropinfo_0",
                                                  0x0002U, 4U);
        const EntryId margin_bottom
            = add_exif_u32(&store, "mk_canon_cropinfo_0", 0x0003U, 6U);

        const EntryId nikon_left
            = add_exif_u32(&store, "mk_nikoncapture_cropdata_0", 0x001EU, 10U);
        const EntryId nikon_top
            = add_exif_u32(&store, "mk_nikoncapture_cropdata_0", 0x0026U, 20U);
        const EntryId nikon_right  = add_exif_u32(&store,
                                                  "mk_nikoncapture_cropdata_0",
                                                  0x002EU, 4010U);
        const EntryId nikon_bottom = add_exif_u32(&store,
                                                  "mk_nikoncapture_cropdata_0",
                                                  0x0036U, 3020U);

        const EntryId sony_left   = add_exif_u32(&store, "mk_sony_panorama_0",
                                                 0x0004U, 100U);
        const EntryId sony_top    = add_exif_u32(&store, "mk_sony_panorama_0",
                                                 0x0005U, 20U);
        const EntryId sony_right  = add_exif_u32(&store, "mk_sony_panorama_0",
                                                 0x0006U, 120U);
        const EntryId sony_bottom = add_exif_u32(&store, "mk_sony_panorama_0",
                                                 0x0007U, 30U);
        store.finalize();

        const MetadataConceptResult result = resolve_metadata_concepts(store);

        const MetadataConceptResolution* geometry
            = find_concept(result, MetadataConceptKind::Geometry);
        ASSERT_NE(geometry, nullptr);
        EXPECT_TRUE(geometry->found);

        const MetadataConceptCandidate* canon_crop
            = find_role_entries(*geometry, MetadataConceptRole::Crop,
                                canon_left, canon_width);
        ASSERT_NE(canon_crop, nullptr);
        EXPECT_EQ(canon_crop->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        EXPECT_TRUE(canon_crop->requires_target_image_spec);
        ASSERT_TRUE(canon_crop->has_rect);
        EXPECT_DOUBLE_EQ(canon_crop->rect[0], 12.0);
        EXPECT_DOUBLE_EQ(canon_crop->rect[1], 8.0);
        EXPECT_DOUBLE_EQ(canon_crop->rect[2], 4000.0);
        EXPECT_DOUBLE_EQ(canon_crop->rect[3], 3000.0);
        EXPECT_TRUE(contains_entry(canon_crop->source_entries, canon_height));
        EXPECT_TRUE(contains_entry(canon_crop->source_entries, canon_top));

        const MetadataConceptCandidate* canon_border
            = find_role_entries(*geometry, MetadataConceptRole::Border,
                                margin_left, margin_right);
        ASSERT_NE(canon_border, nullptr);
        EXPECT_EQ(canon_border->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        ASSERT_TRUE(canon_border->has_margins);
        EXPECT_DOUBLE_EQ(canon_border->margins[0], 16.0);
        EXPECT_DOUBLE_EQ(canon_border->margins[1], 4.0);
        EXPECT_DOUBLE_EQ(canon_border->margins[2], 20.0);
        EXPECT_DOUBLE_EQ(canon_border->margins[3], 6.0);
        EXPECT_TRUE(contains_entry(canon_border->source_entries, margin_top));
        EXPECT_TRUE(
            contains_entry(canon_border->source_entries, margin_bottom));

        const MetadataConceptCandidate* nikon_crop
            = find_role_entries(*geometry, MetadataConceptRole::Crop,
                                nikon_left, nikon_right);
        ASSERT_NE(nikon_crop, nullptr);
        EXPECT_EQ(nikon_crop->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        ASSERT_TRUE(nikon_crop->has_rect);
        EXPECT_DOUBLE_EQ(nikon_crop->rect[0], 10.0);
        EXPECT_DOUBLE_EQ(nikon_crop->rect[1], 20.0);
        EXPECT_DOUBLE_EQ(nikon_crop->rect[2], 4000.0);
        EXPECT_DOUBLE_EQ(nikon_crop->rect[3], 3000.0);
        EXPECT_TRUE(contains_entry(nikon_crop->source_entries, nikon_top));
        EXPECT_TRUE(contains_entry(nikon_crop->source_entries, nikon_bottom));

        const MetadataConceptCandidate* sony_border
            = find_role_entries(*geometry, MetadataConceptRole::Border,
                                sony_left, sony_right);
        ASSERT_NE(sony_border, nullptr);
        EXPECT_EQ(sony_border->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        ASSERT_TRUE(sony_border->has_margins);
        EXPECT_DOUBLE_EQ(sony_border->margins[0], 100.0);
        EXPECT_DOUBLE_EQ(sony_border->margins[1], 20.0);
        EXPECT_DOUBLE_EQ(sony_border->margins[2], 120.0);
        EXPECT_DOUBLE_EQ(sony_border->margins[3], 30.0);
        EXPECT_TRUE(contains_entry(sony_border->source_entries, sony_top));
        EXPECT_TRUE(contains_entry(sony_border->source_entries, sony_bottom));
    }

    TEST(MetadataConcepts, ResolvesGroupedVendorRecordsForInspectionHints)
    {
        MetaStore store;
        const std::array<uint32_t, 9> color_matrix_a = {
            1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
        };
        const std::array<uint32_t, 9> color_matrix_b = {
            2U, 0U, 0U, 0U, 2U, 0U, 0U, 0U, 2U,
        };
        const EntryId matrix_a = add_exif_u32_array(
            &store, "mk_phaseone0", 0x0106U,
            std::span<const uint32_t>(color_matrix_a.data(),
                                      color_matrix_a.size()));
        const EntryId matrix_b = add_exif_u32_array(
            &store, "mk_phaseone0", 0x0226U,
            std::span<const uint32_t>(color_matrix_b.data(),
                                      color_matrix_b.size()));

        const std::array<uint32_t, 4> daylight = { 110U, 256U, 256U, 144U };
        const std::array<uint32_t, 4> cloudy   = { 120U, 256U, 256U, 136U };
        const EntryId daylight_id
            = add_exif_u32_array(&store, "mk_nikon_colorbalancec_0", 0x0114U,
                                 std::span<const uint32_t>(daylight.data(),
                                                           daylight.size()));
        const EntryId cloudy_id
            = add_exif_u32_array(&store, "mk_nikon_colorbalancec_0", 0x0115U,
                                 std::span<const uint32_t>(cloudy.data(),
                                                           cloudy.size()));

        const EntryId distort_id  = add_exif_u32(&store, "mk_nikon_distortinfo",
                                                 0x0001U, 7U);
        const EntryId vignette_id = add_exif_u32(&store, "mk_nikon_vignette",
                                                 0x0001U, 3U);

        const EntryId source_a = add_exif_u32(&store, "mk_google_shotlogdata",
                                              0x0001U, 1U);
        const EntryId source_b = add_exif_u32(&store, "mk_google_shotlogdata",
                                              0x0002U, 2U);
        store.finalize();

        const MetadataConceptResult result = resolve_metadata_concepts(store);

        const MetadataConceptResolution* color
            = find_concept(result, MetadataConceptKind::ColorProfile);
        ASSERT_NE(color, nullptr);
        const MetadataConceptCandidate* matrix
            = find_role_shape(*color, MetadataConceptRole::ColorMatrix,
                              MetadataQueryValueShape::MatrixSet);
        ASSERT_NE(matrix, nullptr);
        EXPECT_EQ(matrix->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(matrix->compatible_file_safe);
        EXPECT_FALSE(matrix->rendered_image_safe);
        EXPECT_TRUE(matrix->source_bound);
        EXPECT_TRUE(contains_entry(matrix->source_entries, matrix_a));
        EXPECT_TRUE(contains_entry(matrix->source_entries, matrix_b));
        ASSERT_TRUE(matrix->has_values);
        ASSERT_EQ(matrix->values.size(), 18U);
        EXPECT_DOUBLE_EQ(matrix->values[0], 1.0);
        EXPECT_DOUBLE_EQ(matrix->values[9], 2.0);

        const MetadataConceptCandidate* wb
            = find_role_shape(*color, MetadataConceptRole::WhiteBalance,
                              MetadataQueryValueShape::VectorSet);
        ASSERT_NE(wb, nullptr);
        EXPECT_EQ(wb->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(wb->compatible_file_safe);
        EXPECT_FALSE(wb->rendered_image_safe);
        EXPECT_TRUE(wb->source_bound);
        EXPECT_TRUE(contains_entry(wb->source_entries, daylight_id));
        EXPECT_TRUE(contains_entry(wb->source_entries, cloudy_id));
        ASSERT_TRUE(wb->has_values);
        ASSERT_EQ(wb->values.size(), 8U);
        EXPECT_DOUBLE_EQ(wb->values[0], 110.0);
        EXPECT_DOUBLE_EQ(wb->values[4], 120.0);

        const MetadataConceptResolution* lens
            = find_concept(result, MetadataConceptKind::LensCorrection);
        ASSERT_NE(lens, nullptr);
        const MetadataConceptCandidate* lens_table
            = find_role_shape(*lens, MetadataConceptRole::LensCorrection,
                              MetadataQueryValueShape::Table);
        ASSERT_NE(lens_table, nullptr);
        EXPECT_EQ(lens_table->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(lens_table->compatible_file_safe);
        EXPECT_FALSE(lens_table->rendered_image_safe);
        EXPECT_TRUE(lens_table->source_bound);
        EXPECT_TRUE(contains_entry(lens_table->source_entries, distort_id));
        EXPECT_TRUE(contains_entry(lens_table->source_entries, vignette_id));

        const MetadataConceptResolution* raw
            = find_concept(result, MetadataConceptKind::RawProcessing);
        ASSERT_NE(raw, nullptr);
        const MetadataConceptCandidate* source
            = find_role_entries(*raw,
                                MetadataConceptRole::ComputationalProcessing,
                                source_a, source_b);
        ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->shape, MetadataQueryValueShape::Table);
        EXPECT_EQ(source->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(source->source_bound);
        EXPECT_TRUE(contains_entry(source->source_entries, source_a));
        EXPECT_TRUE(contains_entry(source->source_entries, source_b));
        ASSERT_TRUE(source->has_values);
        ASSERT_EQ(source->values.size(), 2U);
        EXPECT_DOUBLE_EQ(source->values[0], 1.0);
        EXPECT_DOUBLE_EQ(source->values[1], 2.0);
    }

    TEST(MetadataConcepts, ResolvesLongTailVendorAliasGroupsForSafetyHints)
    {
        MetaStore store;
        const std::array<uint32_t, 9> matrix_a = {
            1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
        };
        const std::array<uint32_t, 9> matrix_b = {
            2U, 0U, 0U, 0U, 2U, 0U, 0U, 0U, 2U,
        };
        const EntryId matrix_a_id
            = add_exif_u32_array(&store, "mk_samsung_type2_0", 0xA030U,
                                 std::span<const uint32_t>(matrix_a.data(),
                                                           matrix_a.size()));
        const EntryId matrix_b_id
            = add_exif_u32_array(&store, "mk_samsung_type2_0", 0xA031U,
                                 std::span<const uint32_t>(matrix_b.data(),
                                                           matrix_b.size()));

        const std::array<uint32_t, 4> wb_a = { 110U, 256U, 256U, 144U };
        const std::array<uint32_t, 4> wb_b = { 120U, 256U, 256U, 136U };
        const EntryId wb_a_id
            = add_exif_u32_array(&store, "mk_samsung_type2_0", 0xA021U,
                                 std::span<const uint32_t>(wb_a.data(),
                                                           wb_a.size()));
        const EntryId wb_b_id
            = add_exif_u32_array(&store, "mk_samsung_type2_0", 0xA022U,
                                 std::span<const uint32_t>(wb_b.data(),
                                                           wb_b.size()));

        const EntryId lens_a  = add_exif_u32(&store, "mk_samsung_type2_0",
                                             0xA052U, 7U);
        const EntryId lens_b  = add_exif_u32(&store, "mk_samsung_type2_0",
                                             0xA053U, 3U);
        const EntryId style_a = add_exif_u32(&store, "mk_sony0", 0xB020U, 1U);
        const EntryId style_b = add_exif_u32(&store, "mk_sony_camerasettings_0",
                                             0x001AU, 2U);
        store.finalize();

        const MetadataConceptResult result = resolve_metadata_concepts(store);

        const MetadataConceptResolution* color
            = find_concept(result, MetadataConceptKind::ColorProfile);
        ASSERT_NE(color, nullptr);
        const MetadataConceptCandidate* matrix
            = find_role_shape(*color, MetadataConceptRole::ColorMatrix,
                              MetadataQueryValueShape::MatrixSet);
        ASSERT_NE(matrix, nullptr);
        EXPECT_EQ(matrix->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(matrix->source_bound);
        EXPECT_FALSE(matrix->rendered_image_safe);
        EXPECT_TRUE(contains_entry(matrix->source_entries, matrix_a_id));
        EXPECT_TRUE(contains_entry(matrix->source_entries, matrix_b_id));

        const MetadataConceptCandidate* wb
            = find_role_shape(*color, MetadataConceptRole::WhiteBalance,
                              MetadataQueryValueShape::VectorSet);
        ASSERT_NE(wb, nullptr);
        EXPECT_EQ(wb->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(wb->source_bound);
        EXPECT_FALSE(wb->rendered_image_safe);
        EXPECT_TRUE(contains_entry(wb->source_entries, wb_a_id));
        EXPECT_TRUE(contains_entry(wb->source_entries, wb_b_id));

        const MetadataConceptResolution* lens
            = find_concept(result, MetadataConceptKind::LensCorrection);
        ASSERT_NE(lens, nullptr);
        const MetadataConceptCandidate* lens_table
            = find_role_shape(*lens, MetadataConceptRole::LensCorrection,
                              MetadataQueryValueShape::Table);
        ASSERT_NE(lens_table, nullptr);
        EXPECT_EQ(lens_table->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(lens_table->source_bound);
        EXPECT_FALSE(lens_table->rendered_image_safe);
        EXPECT_TRUE(contains_entry(lens_table->source_entries, lens_a));
        EXPECT_TRUE(contains_entry(lens_table->source_entries, lens_b));

        const MetadataConceptResolution* raw
            = find_concept(result, MetadataConceptKind::RawProcessing);
        ASSERT_NE(raw, nullptr);
        const MetadataConceptCandidate* source
            = find_role_entries(*raw,
                                MetadataConceptRole::ComputationalProcessing,
                                style_a, style_b);
        ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->shape, MetadataQueryValueShape::Table);
        EXPECT_EQ(source->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(source->source_bound);
        EXPECT_FALSE(source->rendered_image_safe);
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

    TEST(MetadataConcepts, UsesToleranceForGpsCoordinateConflicts)
    {
        {
            MetaStore store;
            (void)add_exif_text(&store, "gpsifd", 0x0001U, "N");
            const std::array<URational, 3> lat = {
                URational { 41U, 1U },
                URational { 24U, 1U },
                URational { 30U, 1U },
            };
            (void)add_exif_urational_array(
                &store, "gpsifd", 0x0002U,
                std::span<const URational>(lat.data(), lat.size()));
            (void)add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/",
                               "exif:GPSLatitude", "41,24.5000001N");
            store.finalize();

            const MetadataConceptResolution gps
                = resolve_metadata_concept(store, MetadataConceptKind::Gps);

            EXPECT_TRUE(gps.found);
            EXPECT_FALSE(gps.conflict);
        }

        {
            MetaStore store;
            (void)add_exif_text(&store, "gpsifd", 0x0001U, "N");
            const std::array<URational, 3> lat = {
                URational { 41U, 1U },
                URational { 24U, 1U },
                URational { 30U, 1U },
            };
            (void)add_exif_urational_array(
                &store, "gpsifd", 0x0002U,
                std::span<const URational>(lat.data(), lat.size()));
            (void)add_xmp_text(&store, "http://ns.adobe.com/exif/1.0/",
                               "exif:GPSLatitude", "41,25.500N");
            store.finalize();

            const MetadataConceptResolution gps
                = resolve_metadata_concept(store, MetadataConceptKind::Gps);

            EXPECT_TRUE(gps.found);
            EXPECT_TRUE(gps.conflict);
            const MetadataConceptCandidate* lat_candidate
                = find_role(gps, MetadataConceptRole::Latitude);
            ASSERT_NE(lat_candidate, nullptr);
            EXPECT_TRUE(lat_candidate->conflict);
        }
    }

    TEST(MetadataConcepts, ResolvesExposureConceptRoles)
    {
        MetaStore store;
        const EntryId exposure_time = add_exif_urational(&store, "exififd",
                                                         0x829AU, 1U, 125U);
        const EntryId aperture = add_exif_urational(&store, "exififd", 0x829DU,
                                                    56U, 10U);
        const EntryId iso      = add_exif_u16(&store, "exififd", 0x8827U, 200U);
        const EntryId bias = add_exif_srational(&store, "exififd", 0x9204U, -1,
                                                3);
        const EntryId program = add_exif_u16(&store, "exififd", 0x8822U, 3U);
        const EntryId gain    = add_exif_u16(&store, "exififd", 0xA407U, 1U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);
        EXPECT_FALSE(exposure.conflict);

        const MetadataConceptCandidate* time
            = find_role(exposure, MetadataConceptRole::ExposureTime);
        ASSERT_NE(time, nullptr);
        EXPECT_EQ(time->transfer_hint, MetadataConceptTransferHint::Safe);
        EXPECT_TRUE(time->rendered_image_safe);
        EXPECT_TRUE(contains_entry(time->source_entries, exposure_time));
        ASSERT_TRUE(time->has_values);
        EXPECT_NEAR(time->values[0], 0.008, 0.0000001);

        const MetadataConceptCandidate* f_number
            = find_role(exposure, MetadataConceptRole::Aperture);
        ASSERT_NE(f_number, nullptr);
        EXPECT_TRUE(contains_entry(f_number->source_entries, aperture));
        ASSERT_TRUE(f_number->has_values);
        EXPECT_NEAR(f_number->values[0], 5.6, 0.0000001);

        const MetadataConceptCandidate* sensitivity
            = find_role(exposure, MetadataConceptRole::IsoSensitivity);
        ASSERT_NE(sensitivity, nullptr);
        EXPECT_TRUE(contains_entry(sensitivity->source_entries, iso));
        ASSERT_TRUE(sensitivity->has_values);
        EXPECT_DOUBLE_EQ(sensitivity->values[0], 200.0);

        const MetadataConceptCandidate* exposure_bias
            = find_role(exposure, MetadataConceptRole::ExposureBias);
        ASSERT_NE(exposure_bias, nullptr);
        EXPECT_TRUE(contains_entry(exposure_bias->source_entries, bias));
        ASSERT_TRUE(exposure_bias->has_values);
        EXPECT_NEAR(exposure_bias->values[0], -0.333333333333, 0.0000001);

        const MetadataConceptCandidate* exposure_program
            = find_role(exposure, MetadataConceptRole::ExposureProgram);
        ASSERT_NE(exposure_program, nullptr);
        EXPECT_TRUE(contains_entry(exposure_program->source_entries, program));
        ASSERT_TRUE(exposure_program->has_values);
        EXPECT_DOUBLE_EQ(exposure_program->values[0], 3.0);
        EXPECT_EQ(exposure_program->text, "Aperture-priority AE");
        EXPECT_EQ(exposure_program->value_key, "aperturepriorityae");

        const MetadataConceptCandidate* gain_value
            = find_role(exposure, MetadataConceptRole::Gain);
        ASSERT_NE(gain_value, nullptr);
        EXPECT_TRUE(contains_entry(gain_value->source_entries, gain));
        ASSERT_TRUE(gain_value->has_values);
        EXPECT_DOUBLE_EQ(gain_value->values[0], 1.0);
        EXPECT_EQ(gain_value->text, "Low gain up");
        EXPECT_EQ(gain_value->value_key, "lowgainup");
    }

    TEST(MetadataConcepts, ResolvesVendorExposureNamesIntoRoles)
    {
        MetaStore store;
        const EntryId exposure_time = add_exif_urational(&store,
                                                         "mk_canon_shotinfo_0",
                                                         0x0005U, 1U, 125U);
        const EntryId aperture      = add_exif_urational(&store,
                                                         "mk_canon_shotinfo_0",
                                                         0x0004U, 56U, 10U);
        const EntryId bias = add_exif_srational(&store, "mk_canon_shotinfo_0",
                                                0x0006U, -2, 3);
        const EntryId iso  = add_exif_u16(&store, "mk_ricoh_imageinfo_0",
                                          0x0027U, 400U);
        const EntryId program
            = add_exif_u16(&store, "mk_canon_camerasettings_0", 0x0014U, 4U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);

        const MetadataConceptCandidate* time
            = find_role(exposure, MetadataConceptRole::ExposureTime);
        ASSERT_NE(time, nullptr);
        EXPECT_TRUE(contains_entry(time->source_entries, exposure_time));
        ASSERT_TRUE(time->has_values);
        EXPECT_NEAR(time->values[0], 0.008, 0.0000001);

        const MetadataConceptCandidate* f_number
            = find_role(exposure, MetadataConceptRole::Aperture);
        ASSERT_NE(f_number, nullptr);
        EXPECT_TRUE(contains_entry(f_number->source_entries, aperture));
        ASSERT_TRUE(f_number->has_values);
        EXPECT_NEAR(f_number->values[0], 5.6, 0.0000001);

        const MetadataConceptCandidate* exposure_bias
            = find_role(exposure, MetadataConceptRole::ExposureBias);
        ASSERT_NE(exposure_bias, nullptr);
        EXPECT_TRUE(contains_entry(exposure_bias->source_entries, bias));
        ASSERT_TRUE(exposure_bias->has_values);
        EXPECT_NEAR(exposure_bias->values[0], -0.666666666666, 0.0000001);

        const MetadataConceptCandidate* sensitivity
            = find_role(exposure, MetadataConceptRole::IsoSensitivity);
        ASSERT_NE(sensitivity, nullptr);
        EXPECT_TRUE(contains_entry(sensitivity->source_entries, iso));
        ASSERT_TRUE(sensitivity->has_values);
        EXPECT_DOUBLE_EQ(sensitivity->values[0], 400.0);

        const MetadataConceptCandidate* exposure_program
            = find_role(exposure, MetadataConceptRole::ExposureProgram);
        ASSERT_NE(exposure_program, nullptr);
        EXPECT_TRUE(contains_entry(exposure_program->source_entries, program));
        ASSERT_TRUE(exposure_program->has_values);
        EXPECT_DOUBLE_EQ(exposure_program->values[0], 4.0);
        EXPECT_EQ(exposure_program->text, "Manual");
        EXPECT_EQ(exposure_program->value_key, "manual");
    }

    TEST(MetadataConcepts, ResolvesStandardExposureModeNameIntoRoles)
    {
        MetaStore store;
        const EntryId exposure_mode = add_exif_u16(&store, "exififd", 0xA402U,
                                                   1U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);

        const MetadataConceptCandidate* exposure_program
            = find_role(exposure, MetadataConceptRole::ExposureProgram);
        ASSERT_NE(exposure_program, nullptr);
        EXPECT_TRUE(
            contains_entry(exposure_program->source_entries, exposure_mode));
        ASSERT_TRUE(exposure_program->has_values);
        EXPECT_DOUBLE_EQ(exposure_program->values[0], 1.0);
        EXPECT_EQ(exposure_program->text, "Manual");
        EXPECT_EQ(exposure_program->value_key, "manual");
    }

    TEST(MetadataConcepts, ResolvesSonyMakerNoteExposureNameLabels)
    {
        MetaStore store;
        const EntryId program = add_exif_u16(&store, "mk_sony_tag2010i_0",
                                             0x024CU, 5U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);

        const MetadataConceptCandidate* exposure_program
            = find_role(exposure, MetadataConceptRole::ExposureProgram);
        ASSERT_NE(exposure_program, nullptr);
        EXPECT_TRUE(contains_entry(exposure_program->source_entries, program));
        ASSERT_TRUE(exposure_program->has_values);
        EXPECT_DOUBLE_EQ(exposure_program->values[0], 5.0);
        EXPECT_EQ(exposure_program->text, "iAuto");
        EXPECT_EQ(exposure_program->value_key, "iauto");
    }

    TEST(MetadataConcepts, ResolvesAdditionalMakerNoteExposureNameLabels)
    {
        MetaStore store;
        const EntryId pentax_program
            = add_exif_u16(&store, "mk_pentax_aeinfo_0", 0x0006U, 216U);
        const EntryId olympus_program
            = add_exif_u16(&store, "mk_olympus_camerasettings_0", 0x0200U, 3U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);

        const MetadataConceptCandidate* pentax  = nullptr;
        const MetadataConceptCandidate* olympus = nullptr;
        for (size_t i = 0U; i < exposure.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = exposure.candidates[i];
            if (contains_entry(candidate.source_entries, pentax_program)) {
                pentax = &candidate;
            }
            if (contains_entry(candidate.source_entries, olympus_program)) {
                olympus = &candidate;
            }
        }

        ASSERT_NE(pentax, nullptr);
        EXPECT_EQ(pentax->role, MetadataConceptRole::ExposureProgram);
        ASSERT_TRUE(pentax->has_values);
        EXPECT_DOUBLE_EQ(pentax->values[0], 216.0);
        EXPECT_EQ(pentax->text, "HDR");
        EXPECT_EQ(pentax->value_key, "hdr");

        ASSERT_NE(olympus, nullptr);
        EXPECT_EQ(olympus->role, MetadataConceptRole::ExposureProgram);
        ASSERT_TRUE(olympus->has_values);
        EXPECT_DOUBLE_EQ(olympus->values[0], 3.0);
        EXPECT_EQ(olympus->text, "Aperture-priority AE");
        EXPECT_EQ(olympus->value_key, "aperturepriorityae");
    }

    TEST(MetadataConcepts, ResolvesLongTailMakerNoteExposureNameLabels)
    {
        MetaStore store;
        const EntryId ricoh_program = add_exif_u16(&store, "mk_ricoh0", 0x1001U,
                                                   5U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);

        const MetadataConceptCandidate* exposure_program
            = find_role(exposure, MetadataConceptRole::ExposureProgram);
        ASSERT_NE(exposure_program, nullptr);
        EXPECT_TRUE(
            contains_entry(exposure_program->source_entries, ricoh_program));
        ASSERT_TRUE(exposure_program->has_values);
        EXPECT_DOUBLE_EQ(exposure_program->values[0], 5.0);
        EXPECT_EQ(exposure_program->text, "Shutter/aperture priority AE");
        EXPECT_EQ(exposure_program->value_key, "shutteraperturepriorityae");
    }

    TEST(MetadataConcepts, MarksDngExposureAdjustmentsRenderedUnsafe)
    {
        MetaStore store;
        const EntryId baseline = add_exif_srational(&store, "ifd0", 0xC62AU, 1,
                                                    2);
        const EntryId preview_gain = add_exif_urational(&store, "ifd0", 0xC7A8U,
                                                        3U, 2U);
        store.finalize();

        const MetadataConceptResolution exposure
            = resolve_metadata_concept(store, MetadataConceptKind::Exposure);

        EXPECT_TRUE(exposure.found);
        uint32_t raw_adjustments = 0U;
        bool saw_baseline        = false;
        bool saw_preview_gain    = false;
        for (size_t i = 0U; i < exposure.candidates.size(); ++i) {
            const MetadataConceptCandidate& candidate = exposure.candidates[i];
            if (candidate.role != MetadataConceptRole::RawExposureAdjustment) {
                continue;
            }
            raw_adjustments += 1U;
            EXPECT_EQ(candidate.transfer_hint,
                      MetadataConceptTransferHint::RenderedUnsafe);
            EXPECT_TRUE(candidate.compatible_file_safe);
            EXPECT_FALSE(candidate.rendered_image_safe);
            EXPECT_TRUE(candidate.source_bound);
            saw_baseline = saw_baseline
                           || contains_entry(candidate.source_entries,
                                             baseline);
            saw_preview_gain = saw_preview_gain
                               || contains_entry(candidate.source_entries,
                                                 preview_gain);
        }
        EXPECT_GE(raw_adjustments, 2U);
        EXPECT_TRUE(saw_baseline);
        EXPECT_TRUE(saw_preview_gain);
    }

    TEST(MetadataConcepts, ExposesTransferHintsForHostPolicy)
    {
        MetaStore store;
        (void)add_exif_u16(&store, "ifd0", 0x0112U, 6U);
        (void)add_exif_text(&store, "exififd", 0x9003U, "2024:04:19 12:34:56");
        (void)add_exif_urational(&store, "exififd", 0x829AU, 1U, 125U);
        (void)add_exif_srational(&store, "ifd0", 0xC62AU, 1, 2);
        (void)add_exif_text(&store, "gpsifd", 0x0001U, "N");
        const std::array<URational, 3> lat = {
            URational { 41U, 1U },
            URational { 24U, 1U },
            URational { 30U, 1U },
        };
        (void)add_exif_urational_array(&store, "gpsifd", 0x0002U,
                                       std::span<const URational>(lat.data(),
                                                                  lat.size()));
        (void)add_exif_u16(&store, "exififd", 0xA001U, 1U);
        const std::array<uint32_t, 9> color_matrix_values = {
            1U, 0U, 0U, 0U, 1U, 0U, 0U, 0U, 1U,
        };
        (void)add_exif_u32_array(
            &store, "ifd0", 0xC621U,
            std::span<const uint32_t>(color_matrix_values.data(),
                                      color_matrix_values.size()));
        const std::array<uint32_t, 4> active_area_values = {
            10U,
            20U,
            3010U,
            4020U,
        };
        (void)add_exif_u32_array(
            &store, "ifd0", 0xC68DU,
            std::span<const uint32_t>(active_area_values.data(),
                                      active_area_values.size()));
        (void)add_exif_u32(&store, "mk_nikon_distortinfo", 0x0001U, 7U);
        (void)add_exif_u32(&store, "ifd0", 0xC61AU, 512U);
        const std::array<uint32_t, 2> linearization_values = {
            0U,
            65535U,
        };
        (void)add_exif_u32_array(
            &store, "ifd0", 0xC618U,
            std::span<const uint32_t>(linearization_values.data(),
                                      linearization_values.size()));
        (void)add_exif_u32(&store, "mk_google_shotlogdata", 0x0001U, 7U);
        store.finalize();

        const MetadataConceptResult result = resolve_metadata_concepts(store);

        const MetadataConceptResolution* datetime
            = find_concept(result, MetadataConceptKind::DateTime);
        ASSERT_NE(datetime, nullptr);
        const MetadataConceptCandidate* created
            = find_role(*datetime, MetadataConceptRole::Created);
        ASSERT_NE(created, nullptr);
        EXPECT_EQ(created->transfer_hint, MetadataConceptTransferHint::Safe);
        EXPECT_TRUE(created->compatible_file_safe);
        EXPECT_TRUE(created->rendered_image_safe);

        const MetadataConceptResolution* gps
            = find_concept(result, MetadataConceptKind::Gps);
        ASSERT_NE(gps, nullptr);
        const MetadataConceptCandidate* gps_lat
            = find_role(*gps, MetadataConceptRole::Latitude);
        ASSERT_NE(gps_lat, nullptr);
        EXPECT_EQ(gps_lat->transfer_hint, MetadataConceptTransferHint::Safe);
        EXPECT_TRUE(gps_lat->rendered_image_safe);

        const MetadataConceptResolution* exposure
            = find_concept(result, MetadataConceptKind::Exposure);
        ASSERT_NE(exposure, nullptr);
        const MetadataConceptCandidate* exposure_time
            = find_role(*exposure, MetadataConceptRole::ExposureTime);
        ASSERT_NE(exposure_time, nullptr);
        EXPECT_EQ(exposure_time->transfer_hint,
                  MetadataConceptTransferHint::Safe);
        EXPECT_TRUE(exposure_time->rendered_image_safe);
        const MetadataConceptCandidate* raw_adjustment
            = find_role(*exposure, MetadataConceptRole::RawExposureAdjustment);
        ASSERT_NE(raw_adjustment, nullptr);
        EXPECT_EQ(raw_adjustment->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_FALSE(raw_adjustment->rendered_image_safe);

        const MetadataConceptResolution* orientation
            = find_concept(result, MetadataConceptKind::Orientation);
        ASSERT_NE(orientation, nullptr);
        const MetadataConceptCandidate* orientation_value
            = find_role(*orientation, MetadataConceptRole::Orientation);
        ASSERT_NE(orientation_value, nullptr);
        EXPECT_EQ(orientation_value->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        EXPECT_TRUE(orientation_value->compatible_file_safe);
        EXPECT_FALSE(orientation_value->rendered_image_safe);
        EXPECT_TRUE(orientation_value->requires_target_image_spec);

        const MetadataConceptResolution* color
            = find_concept(result, MetadataConceptKind::ColorProfile);
        ASSERT_NE(color, nullptr);
        const MetadataConceptCandidate* color_space
            = find_role(*color, MetadataConceptRole::ColorSpace);
        ASSERT_NE(color_space, nullptr);
        EXPECT_EQ(color_space->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        const MetadataConceptCandidate* matrix
            = find_role(*color, MetadataConceptRole::ColorMatrix);
        ASSERT_NE(matrix, nullptr);
        EXPECT_EQ(matrix->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(matrix->source_bound);
        EXPECT_FALSE(matrix->rendered_image_safe);

        const MetadataConceptResolution* geometry
            = find_concept(result, MetadataConceptKind::Geometry);
        ASSERT_NE(geometry, nullptr);
        const MetadataConceptCandidate* active
            = find_role(*geometry, MetadataConceptRole::ActiveArea);
        ASSERT_NE(active, nullptr);
        EXPECT_EQ(active->transfer_hint,
                  MetadataConceptTransferHint::RequiresTargetImageSpec);
        EXPECT_FALSE(active->rendered_image_safe);

        const MetadataConceptResolution* lens
            = find_concept(result, MetadataConceptKind::LensCorrection);
        ASSERT_NE(lens, nullptr);
        const MetadataConceptCandidate* lens_value
            = find_role(*lens, MetadataConceptRole::LensCorrection);
        ASSERT_NE(lens_value, nullptr);
        EXPECT_EQ(lens_value->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(lens_value->source_bound);

        const MetadataConceptResolution* raw
            = find_concept(result, MetadataConceptKind::RawProcessing);
        ASSERT_NE(raw, nullptr);
        const MetadataConceptCandidate* black
            = find_role(*raw, MetadataConceptRole::BlackLevel);
        ASSERT_NE(black, nullptr);
        EXPECT_EQ(black->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_EQ(black->raw_applicability,
                  MetadataRawApplicabilityState::AppliesToStoredRaw);
        EXPECT_FALSE(black->raw_applicability_requires_storage_context);
        EXPECT_TRUE(black->raw_applicability_can_affect_decode);
        const MetadataConceptCandidate* curve
            = find_role(*raw, MetadataConceptRole::RawValueCurve);
        ASSERT_NE(curve, nullptr);
        EXPECT_EQ(curve->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(curve->compatible_file_safe);
        EXPECT_FALSE(curve->rendered_image_safe);
        EXPECT_EQ(curve->raw_applicability,
                  MetadataRawApplicabilityState::ConditionalOnRawEncoding);
        EXPECT_TRUE(curve->raw_applicability_requires_storage_context);
        EXPECT_TRUE(curve->raw_applicability_can_affect_decode);
        const MetadataConceptCandidate* source
            = find_role(*raw, MetadataConceptRole::ComputationalProcessing);
        ASSERT_NE(source, nullptr);
        EXPECT_EQ(source->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(source->compatible_file_safe);
        EXPECT_FALSE(source->rendered_image_safe);
        EXPECT_STREQ(metadata_concept_transfer_hint_name(
                         MetadataConceptTransferHint::SourceBound),
                     "source_bound");
        EXPECT_STREQ(metadata_concept_role_name(
                         MetadataConceptRole::RawValueCurve),
                     "raw_value_curve");
        EXPECT_STREQ(metadata_raw_data_encoding_name(
                         MetadataRawDataEncoding::LosslessCompressed),
                     "lossless_compressed");
        EXPECT_STREQ(
            metadata_raw_applicability_state_name(
                MetadataRawApplicabilityState::ConditionalOnRawEncoding),
            "conditional_on_raw_encoding");
    }

    TEST(MetadataConcepts, SurfacesColorAndGeometryConflicts)
    {
        {
            MetaStore store;
            (void)add_exif_u16(&store, "exififd", 0xA001U, 1U);
            (void)add_exif_u16(&store, "ifd0", 0xA001U, 2U);
            store.finalize();

            const MetadataConceptResolution color
                = resolve_metadata_concept(store,
                                           MetadataConceptKind::ColorProfile);

            EXPECT_TRUE(color.found);
            EXPECT_TRUE(color.conflict);
            uint32_t conflicts = 0U;
            for (size_t i = 0U; i < color.candidates.size(); ++i) {
                if (color.candidates[i].role == MetadataConceptRole::ColorSpace
                    && color.candidates[i].conflict) {
                    conflicts += 1U;
                }
            }
            EXPECT_GE(conflicts, 2U);
        }

        {
            MetaStore store;
            (void)add_xmp_text(&store, "http://example.invalid/aux/1.0/",
                               "aux:SensorBorderPadding", "64 32 168 128");
            (void)add_xmp_text(&store, "http://example.invalid/aux/1.0/",
                               "aux:OutputBorderPadding", "32 32 168 128");
            store.finalize();

            const MetadataConceptResolution geometry
                = resolve_metadata_concept(store,
                                           MetadataConceptKind::Geometry);

            EXPECT_TRUE(geometry.found);
            EXPECT_TRUE(geometry.conflict);
            uint32_t conflicts = 0U;
            for (size_t i = 0U; i < geometry.candidates.size(); ++i) {
                if (geometry.candidates[i].role == MetadataConceptRole::Border
                    && geometry.candidates[i].conflict) {
                    conflicts += 1U;
                }
            }
            EXPECT_GE(conflicts, 2U);
        }
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

    TEST(MetadataConcepts, ResolvesSourceColorTransformAsRenderedUnsafe)
    {
        MetaStore store;
        const EntryId profile
            = add_xmp_text(&store,
                           "http://ns.adobe.com/camera-raw-settings/1.0/",
                           "crs:CameraProfile", "Adobe Color");
        store.finalize();

        const MetadataConceptResolution color
            = resolve_metadata_concept(store,
                                       MetadataConceptKind::ColorProfile);

        EXPECT_TRUE(color.found);
        EXPECT_FALSE(color.conflict);
        EXPECT_EQ(color.preferred_entry, profile);
        const MetadataConceptCandidate* candidate
            = find_role(color, MetadataConceptRole::SourceColorTransform);
        ASSERT_NE(candidate, nullptr);
        EXPECT_TRUE(candidate->preferred);
        EXPECT_EQ(candidate->family, MetadataConceptSourceFamily::Xmp);
        EXPECT_EQ(candidate->semantic,
                  MetadataQuerySemanticKind::SourceColorTransform);
        EXPECT_EQ(candidate->shape, MetadataQueryValueShape::Text);
        EXPECT_EQ(candidate->transfer_hint,
                  MetadataConceptTransferHint::RenderedUnsafe);
        EXPECT_TRUE(candidate->compatible_file_safe);
        EXPECT_FALSE(candidate->rendered_image_safe);
        EXPECT_TRUE(candidate->source_bound);
        EXPECT_TRUE(contains_entry(candidate->source_entries, profile));
    }

    TEST(MetadataConcepts, ResolvesSourceProcessingSubrolesAsSourceBound)
    {
        MetaStore store;
        const EntryId computational
            = add_exif_u32(&store, "mk_google_shotlogdata", 0x0001U, 7U);
        const EntryId thermal = add_exif_u32(&store, "mk_dji_thermalparams",
                                             0x0048U, 98U);
        const EntryId stitch  = add_exif_u32(&store, "mk_microsoft_stitch",
                                             0x0003U, 12U);
        store.finalize();

        const MetadataConceptResolution raw
            = resolve_metadata_concept(store,
                                       MetadataConceptKind::RawProcessing);

        EXPECT_TRUE(raw.found);
        const MetadataConceptCandidate* computational_candidate
            = find_role(raw, MetadataConceptRole::ComputationalProcessing);
        ASSERT_NE(computational_candidate, nullptr);
        EXPECT_EQ(computational_candidate->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(computational_candidate->source_bound);
        EXPECT_TRUE(contains_entry(computational_candidate->source_entries,
                                   computational));

        const MetadataConceptCandidate* thermal_candidate
            = find_role(raw, MetadataConceptRole::ThermalProcessing);
        ASSERT_NE(thermal_candidate, nullptr);
        EXPECT_EQ(thermal_candidate->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(thermal_candidate->source_bound);
        EXPECT_TRUE(contains_entry(thermal_candidate->source_entries, thermal));

        const MetadataConceptCandidate* stitch_candidate
            = find_role(raw, MetadataConceptRole::StitchProcessing);
        ASSERT_NE(stitch_candidate, nullptr);
        EXPECT_EQ(stitch_candidate->transfer_hint,
                  MetadataConceptTransferHint::SourceBound);
        EXPECT_TRUE(stitch_candidate->source_bound);
        EXPECT_TRUE(contains_entry(stitch_candidate->source_entries, stitch));
    }

}  // namespace
}  // namespace openmeta
