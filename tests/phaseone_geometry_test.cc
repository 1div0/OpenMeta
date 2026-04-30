// SPDX-License-Identifier: Apache-2.0

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/phaseone_geometry.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace openmeta {
namespace {

    static void add_phaseone_u32(MetaStore* store, uint16_t tag,
                                 uint32_t value)
    {
        ASSERT_TRUE(store);
        Entry entry;
        entry.key   = make_exif_tag_key(store->arena(), "mk_phaseone0", tag);
        entry.value = make_u32(value);
        store->add_entry(entry);
    }

    static void append_u32le(std::vector<std::byte>* out, uint32_t value)
    {
        ASSERT_TRUE(out);
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 0U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 16U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 24U) & 0xFFU) });
    }

    static std::vector<std::byte>
    make_f32_bits_bytes(std::span<const uint32_t> bits)
    {
        std::vector<std::byte> out;
        out.reserve(bits.size() * 4U);
        for (size_t i = 0; i < bits.size(); ++i) {
            append_u32le(&out, bits[i]);
        }
        return out;
    }

    static void add_phaseone_bytes(MetaStore* store, std::string_view ifd,
                                   uint16_t tag,
                                   std::span<const std::byte> bytes,
                                   uint32_t wire_count)
    {
        ASSERT_TRUE(store);
        Entry entry;
        entry.key = make_exif_tag_key(store->arena(), ifd, tag);
        entry.value = make_bytes(store->arena(), bytes);
        entry.origin.wire_type = WireType { WireFamily::Other, 4U };
        entry.origin.wire_count = wire_count;
        ASSERT_NE(store->add_entry(entry), kInvalidEntryId);
    }

}  // namespace

TEST(PhaseOneGeometry, ComputesActiveRectAndMarginsFromValues)
{
    const PhaseOneRawGeometryResult result = phaseone_raw_geometry_from_values(
        10560U, 7920U, 64U, 32U, 10328U, 7760U);

    EXPECT_EQ(result.status, PhaseOneRawGeometryStatus::Ok);
    EXPECT_EQ(result.geometry.active_x, 64U);
    EXPECT_EQ(result.geometry.active_y, 32U);
    EXPECT_EQ(result.geometry.active_width, 10328U);
    EXPECT_EQ(result.geometry.active_height, 7760U);
    EXPECT_EQ(result.geometry.right_margin, 168U);
    EXPECT_EQ(result.geometry.bottom_margin, 128U);
}

TEST(PhaseOneGeometry, ReadsActiveRectAndMarginsFromStore)
{
    MetaStore store;
    add_phaseone_u32(&store, 0x0108U, 10560U);
    add_phaseone_u32(&store, 0x0109U, 7920U);
    add_phaseone_u32(&store, 0x010AU, 64U);
    add_phaseone_u32(&store, 0x010BU, 32U);
    add_phaseone_u32(&store, 0x010CU, 10328U);
    add_phaseone_u32(&store, 0x010DU, 7760U);
    store.finalize();

    const PhaseOneRawGeometryResult result
        = phaseone_raw_geometry_from_store(store);

    EXPECT_EQ(result.status, PhaseOneRawGeometryStatus::Ok);
    EXPECT_EQ(result.geometry.sensor_width, 10560U);
    EXPECT_EQ(result.geometry.sensor_height, 7920U);
    EXPECT_EQ(result.geometry.active_x, 64U);
    EXPECT_EQ(result.geometry.active_y, 32U);
    EXPECT_EQ(result.geometry.right_margin, 168U);
    EXPECT_EQ(result.geometry.bottom_margin, 128U);
}

TEST(PhaseOneGeometry, ReportsMissingField)
{
    MetaStore store;
    add_phaseone_u32(&store, 0x0108U, 10560U);
    store.finalize();

    const PhaseOneRawGeometryResult result
        = phaseone_raw_geometry_from_store(store);

    EXPECT_EQ(result.status, PhaseOneRawGeometryStatus::MissingField);
    EXPECT_STREQ(phaseone_raw_geometry_status_name(result.status),
                 "missing_field");
}

TEST(PhaseOneGeometry, RejectsOutOfBoundsActiveRect)
{
    const PhaseOneRawGeometryResult result = phaseone_raw_geometry_from_values(
        100U, 80U, 20U, 10U, 90U, 60U);

    EXPECT_EQ(result.status, PhaseOneRawGeometryStatus::OutOfBounds);
    EXPECT_STREQ(phaseone_raw_geometry_status_name(result.status),
                 "out_of_bounds");
}

TEST(PhaseOneGeometry, ReadsRawProcessingFieldsFromStore)
{
    MetaStore store;

    const std::array<uint32_t, 9> matrix1 = {
        0x3F800000U, 0x00000000U, 0x00000000U,
        0x00000000U, 0x3F800000U, 0x00000000U,
        0x00000000U, 0x00000000U, 0x3F800000U,
    };
    const std::array<uint32_t, 9> matrix2 = {
        0x40000000U, 0x00000000U, 0x00000000U,
        0x00000000U, 0x40000000U, 0x00000000U,
        0x00000000U, 0x00000000U, 0x40000000U,
    };
    const std::array<uint32_t, 3> wb = {
        0x3FC00000U, 0x3F800000U, 0x40000000U,
    };
    const std::vector<std::byte> matrix1_bytes = make_f32_bits_bytes(
        std::span<const uint32_t>(matrix1.data(), matrix1.size()));
    const std::vector<std::byte> matrix2_bytes = make_f32_bits_bytes(
        std::span<const uint32_t>(matrix2.data(), matrix2.size()));
    const std::vector<std::byte> wb_bytes = make_f32_bits_bytes(
        std::span<const uint32_t>(wb.data(), wb.size()));
    add_phaseone_bytes(&store, "mk_phaseone0", 0x0106U, matrix1_bytes, 9U);
    add_phaseone_bytes(&store, "mk_phaseone0", 0x0107U, wb_bytes, 3U);
    add_phaseone_u32(&store, 0x010EU, 3U);
    {
        const std::array<std::byte, 12> raw_data = {
            std::byte { 1 }, std::byte { 2 }, std::byte { 3 },
            std::byte { 4 }, std::byte { 5 }, std::byte { 6 },
            std::byte { 7 }, std::byte { 8 }, std::byte { 9 },
            std::byte { 10 }, std::byte { 11 }, std::byte { 12 },
        };
        add_phaseone_bytes(&store, "mk_phaseone0", 0x010FU, raw_data, 6U);
    }
    add_phaseone_u32(&store, 0x0210U, 0x41CC0000U);
    add_phaseone_u32(&store, 0x0211U, 0x41FA0000U);
    {
        const std::array<std::byte, 8> strip_offsets = {
            std::byte { 0 }, std::byte { 1 }, std::byte { 2 },
            std::byte { 3 }, std::byte { 4 }, std::byte { 5 },
            std::byte { 6 }, std::byte { 7 },
        };
        add_phaseone_bytes(&store, "mk_phaseone0", 0x021CU, strip_offsets,
                           2U);
    }
    add_phaseone_u32(&store, 0x021DU, 1024U);
    {
        const std::array<std::byte, 6> black_level_data = {
            std::byte { 1 }, std::byte { 2 }, std::byte { 3 },
            std::byte { 4 }, std::byte { 5 }, std::byte { 6 },
        };
        add_phaseone_bytes(&store, "mk_phaseone0", 0x0223U, black_level_data,
                           3U);
    }
    add_phaseone_bytes(&store, "mk_phaseone0", 0x0226U, matrix2_bytes, 9U);
    {
        const std::array<std::byte, 8> defects = {
            std::byte { 1 }, std::byte { 2 }, std::byte { 3 },
            std::byte { 4 }, std::byte { 5 }, std::byte { 6 },
            std::byte { 7 }, std::byte { 8 },
        };
        add_phaseone_bytes(&store, "mk_phaseone_sensorcalibration_0", 0x0400U,
                           defects, 2U);
    }
    {
        const std::array<std::byte, 12> flat = {
            std::byte { 1 }, std::byte { 2 }, std::byte { 3 },
            std::byte { 4 }, std::byte { 5 }, std::byte { 6 },
            std::byte { 7 }, std::byte { 8 }, std::byte { 9 },
            std::byte { 10 }, std::byte { 11 }, std::byte { 12 },
        };
        add_phaseone_bytes(&store, "mk_phaseone_sensorcalibration_0", 0x0401U,
                           flat, 3U);
    }
    {
        const std::array<uint32_t, 2> linear = {
            0x3F800000U, 0x40000000U,
        };
        const std::vector<std::byte> linear_bytes = make_f32_bits_bytes(
            std::span<const uint32_t>(linear.data(), linear.size()));
        add_phaseone_bytes(&store, "mk_phaseone_sensorcalibration_0", 0x0419U,
                           linear_bytes, 2U);
    }
    store.finalize();

    const PhaseOneRawProcessingResult result
        = phaseone_raw_processing_from_store(store);

    EXPECT_EQ(result.status, PhaseOneRawProcessingStatus::Ok);
    EXPECT_EQ(result.invalid_fields, 0U);
    EXPECT_EQ(result.fields_seen, result.fields_decoded);
    EXPECT_TRUE(result.info.has_color_matrix1);
    EXPECT_DOUBLE_EQ(result.info.color_matrix1[0], 1.0);
    EXPECT_DOUBLE_EQ(result.info.color_matrix1[4], 1.0);
    EXPECT_TRUE(result.info.has_color_matrix2);
    EXPECT_DOUBLE_EQ(result.info.color_matrix2[0], 2.0);
    EXPECT_TRUE(result.info.has_wb_rgb_levels);
    EXPECT_DOUBLE_EQ(result.info.wb_rgb_levels[0], 1.5);
    EXPECT_DOUBLE_EQ(result.info.wb_rgb_levels[1], 1.0);
    EXPECT_DOUBLE_EQ(result.info.wb_rgb_levels[2], 2.0);
    EXPECT_TRUE(result.info.has_raw_format);
    EXPECT_EQ(result.info.raw_format, 3U);
    EXPECT_TRUE(result.info.has_raw_data);
    EXPECT_EQ(result.info.raw_data_bytes, 12U);
    EXPECT_TRUE(result.info.has_sensor_temperature_c);
    EXPECT_DOUBLE_EQ(result.info.sensor_temperature_c, 25.5);
    EXPECT_TRUE(result.info.has_sensor_temperature2_c);
    EXPECT_DOUBLE_EQ(result.info.sensor_temperature2_c, 31.25);
    EXPECT_TRUE(result.info.has_strip_offsets);
    EXPECT_EQ(result.info.strip_offsets_bytes, 8U);
    EXPECT_TRUE(result.info.has_black_level);
    EXPECT_EQ(result.info.black_level, 1024U);
    EXPECT_TRUE(result.info.has_black_level_data);
    EXPECT_EQ(result.info.black_level_data_bytes, 6U);
    EXPECT_TRUE(result.info.has_sensor_calibration);
    EXPECT_EQ(result.info.sensor_calibration_entry_count, 3U);
    EXPECT_EQ(result.info.sensor_calibration_payload_bytes, 28U);
    EXPECT_TRUE(result.info.has_sensor_defects);
    EXPECT_EQ(result.info.sensor_defects_bytes, 8U);
    EXPECT_TRUE(result.info.has_flat_field);
    EXPECT_EQ(result.info.flat_field_bytes, 12U);
    EXPECT_TRUE(result.info.has_linearization_coefficients);
    EXPECT_EQ(result.info.linearization_coefficients_count, 2U);
}

TEST(PhaseOneGeometry, ReportsMissingRawProcessingFields)
{
    MetaStore store;
    store.finalize();

    const PhaseOneRawProcessingResult result
        = phaseone_raw_processing_from_store(store);

    EXPECT_EQ(result.status, PhaseOneRawProcessingStatus::MissingField);
    EXPECT_STREQ(phaseone_raw_processing_status_name(result.status),
                 "missing_field");
    EXPECT_EQ(result.fields_seen, 0U);
}

TEST(PhaseOneGeometry, ReportsPartialRawProcessingDecode)
{
    MetaStore store;
    const std::array<std::byte, 2> malformed = {
        std::byte { 1 }, std::byte { 2 },
    };
    add_phaseone_bytes(&store, "mk_phaseone0", 0x0106U, malformed, 0U);
    add_phaseone_u32(&store, 0x021DU, 1024U);
    store.finalize();

    const PhaseOneRawProcessingResult result
        = phaseone_raw_processing_from_store(store);

    EXPECT_EQ(result.status, PhaseOneRawProcessingStatus::Partial);
    EXPECT_STREQ(phaseone_raw_processing_status_name(result.status), "partial");
    EXPECT_EQ(result.fields_seen, 2U);
    EXPECT_EQ(result.fields_decoded, 1U);
    EXPECT_EQ(result.invalid_fields, 1U);
    EXPECT_TRUE(result.info.has_black_level);
    EXPECT_FALSE(result.info.has_color_matrix1);
}

}  // namespace openmeta
