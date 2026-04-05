// SPDX-License-Identifier: Apache-2.0

#include "openmeta/photoshop_irb_decode.h"

#include <bit>
#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static uint32_t f32_bits(float value) noexcept
    {
        return std::bit_cast<uint32_t>(value);
    }

    static void append_u16be(uint16_t v, std::vector<std::byte>* out)
    {
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 0) & 0xFF) });
    }


    static void append_u32be(uint32_t v, std::vector<std::byte>* out)
    {
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 24) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 16) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 0) & 0xFF) });
    }

    static void append_utf16be_string32(const char* s,
                                        std::vector<std::byte>* out)
    {
        uint32_t len = 0;
        while (s[len] != '\0') {
            len += 1U;
        }
        append_u32be(len, out);
        for (uint32_t i = 0; i < len; ++i) {
            out->push_back(std::byte { 0x00 });
            out->push_back(std::byte { static_cast<unsigned char>(s[i]) });
        }
    }

    static void append_pascal_string(std::span<const std::byte> text,
                                     std::vector<std::byte>* out)
    {
        out->push_back(
            std::byte { static_cast<unsigned char>(text.size() & 0xFFU) });
        out->insert(out->end(), text.begin(), text.end());
    }

    static void append_u64be(uint64_t v, std::vector<std::byte>* out)
    {
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 56) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 48) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 40) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 32) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 24) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 16) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 0) & 0xFF) });
    }


    static void append_irb_resource(uint16_t id,
                                    std::span<const std::byte> payload,
                                    std::vector<std::byte>* out)
    {
        // Signature.
        out->push_back(std::byte { '8' });
        out->push_back(std::byte { 'B' });
        out->push_back(std::byte { 'I' });
        out->push_back(std::byte { 'M' });
        append_u16be(id, out);

        // Pascal name (len=0) + pad => 2 bytes total.
        out->push_back(std::byte { 0x00 });
        out->push_back(std::byte { 0x00 });

        append_u32be(static_cast<uint32_t>(payload.size()), out);
        out->insert(out->end(), payload.begin(), payload.end());

        if ((payload.size() & 1U) != 0U) {
            out->push_back(std::byte { 0x00 });
        }
    }


    static std::string_view arena_string(const MetaStore& store,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = store.arena().span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }


    static const Entry*
    find_photoshop_irb_field(const MetaStore& store, uint16_t resource_id,
                             std::string_view field) noexcept
    {
        for (size_t i = 0; i < store.entries().size(); ++i) {
            const Entry& e = store.entry(static_cast<EntryId>(i));
            if (e.key.kind != MetaKeyKind::PhotoshopIrbField) {
                continue;
            }
            if (e.key.data.photoshop_irb_field.resource_id != resource_id) {
                continue;
            }
            if (arena_string(store, e.key.data.photoshop_irb_field.field)
                == field) {
                return &e;
            }
        }
        return nullptr;
    }

    static std::vector<uint32_t> collect_photoshop_irb_u32_fields(
        const MetaStore& store, uint16_t resource_id, std::string_view field)
    {
        std::vector<uint32_t> out;
        for (size_t i = 0; i < store.entries().size(); ++i) {
            const Entry& e = store.entry(static_cast<EntryId>(i));
            if (e.key.kind != MetaKeyKind::PhotoshopIrbField) {
                continue;
            }
            if (e.key.data.photoshop_irb_field.resource_id != resource_id) {
                continue;
            }
            if (arena_string(store, e.key.data.photoshop_irb_field.field)
                != field) {
                continue;
            }
            if (e.value.kind != MetaValueKind::Scalar
                || e.value.elem_type != MetaElementType::U32) {
                continue;
            }
            out.push_back(static_cast<uint32_t>(e.value.data.u64));
        }
        return out;
    }

    static std::vector<uint16_t> collect_photoshop_irb_u16_fields(
        const MetaStore& store, uint16_t resource_id, std::string_view field)
    {
        std::vector<uint16_t> out;
        for (size_t i = 0; i < store.entries().size(); ++i) {
            const Entry& e = store.entry(static_cast<EntryId>(i));
            if (e.key.kind != MetaKeyKind::PhotoshopIrbField) {
                continue;
            }
            if (e.key.data.photoshop_irb_field.resource_id != resource_id) {
                continue;
            }
            if (arena_string(store, e.key.data.photoshop_irb_field.field)
                != field) {
                continue;
            }
            if (e.value.kind != MetaValueKind::Scalar
                || e.value.elem_type != MetaElementType::U16) {
                continue;
            }
            out.push_back(static_cast<uint16_t>(e.value.data.u64));
        }
        return out;
    }

    static std::vector<uint8_t> collect_photoshop_irb_u8_fields(
        const MetaStore& store, uint16_t resource_id, std::string_view field)
    {
        std::vector<uint8_t> out;
        for (size_t i = 0; i < store.entries().size(); ++i) {
            const Entry& e = store.entry(static_cast<EntryId>(i));
            if (e.key.kind != MetaKeyKind::PhotoshopIrbField) {
                continue;
            }
            if (e.key.data.photoshop_irb_field.resource_id != resource_id) {
                continue;
            }
            if (arena_string(store, e.key.data.photoshop_irb_field.field)
                != field) {
                continue;
            }
            if (e.value.kind != MetaValueKind::Scalar
                || e.value.elem_type != MetaElementType::U8) {
                continue;
            }
            out.push_back(static_cast<uint8_t>(e.value.data.u64));
        }
        return out;
    }

    static std::vector<std::string_view> collect_photoshop_irb_text_fields(
        const MetaStore& store, uint16_t resource_id, std::string_view field)
    {
        std::vector<std::string_view> out;
        for (size_t i = 0; i < store.entries().size(); ++i) {
            const Entry& e = store.entry(static_cast<EntryId>(i));
            if (e.key.kind != MetaKeyKind::PhotoshopIrbField) {
                continue;
            }
            if (e.key.data.photoshop_irb_field.resource_id != resource_id) {
                continue;
            }
            if (arena_string(store, e.key.data.photoshop_irb_field.field)
                != field) {
                continue;
            }
            if (e.value.kind != MetaValueKind::Text) {
                continue;
            }
            out.push_back(arena_string(store, e.value.data.span));
        }
        return out;
    }

}  // namespace

TEST(PhotoshopIrbDecodeTest, DecodesResourcesAndOptionalIptc)
{
    // One IPTC dataset to embed in resource 0x0404.
    const std::array<std::byte, 9> iptc = {
        std::byte { 0x1C }, std::byte { 0x02 }, std::byte { 0x19 },
        std::byte { 0x00 }, std::byte { 0x04 }, std::byte { 't' },
        std::byte { 'e' },  std::byte { 's' },  std::byte { 't' },
    };

    std::vector<std::byte> irb;
    append_irb_resource(0x0404, iptc, &irb);

    const std::array<std::byte, 3> other = {
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
    };
    append_irb_resource(0x1234, other, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 2U);
    EXPECT_EQ(r.iptc_entries_decoded, 1U);

    // One block for IRB resources, plus one for derived IPTC datasets.
    ASSERT_EQ(store.block_count(), 2U);
    ASSERT_EQ(store.entries().size(), 3U);

    uint32_t irb_entries  = 0;
    uint32_t iptc_entries = 0;
    for (size_t i = 0; i < store.entries().size(); ++i) {
        const Entry& e = store.entry(static_cast<EntryId>(i));
        if (e.key.kind == MetaKeyKind::PhotoshopIrb) {
            irb_entries += 1;
            continue;
        }
        if (e.key.kind == MetaKeyKind::IptcDataset) {
            iptc_entries += 1;
            EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
            EXPECT_EQ(e.key.data.iptc_dataset.record, 2U);
            EXPECT_EQ(e.key.data.iptc_dataset.dataset, 25U);
        }
    }
    EXPECT_EQ(irb_entries, 2U);
    EXPECT_EQ(iptc_entries, 1U);
}

TEST(PhotoshopIrbDecodeTest, EstimateMatchesDecodeCounters)
{
    const std::array<std::byte, 3> payload = {
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
    };
    std::vector<std::byte> irb;
    append_irb_resource(0x1234, payload, &irb);

    const PhotoshopIrbDecodeResult estimate = measure_photoshop_irb(irb);
    EXPECT_EQ(estimate.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(estimate.resources_decoded, 1U);

    MetaStore store;
    const PhotoshopIrbDecodeResult decoded = decode_photoshop_irb(irb, store);
    EXPECT_EQ(decoded.status, estimate.status);
    EXPECT_EQ(decoded.resources_decoded, estimate.resources_decoded);
    EXPECT_EQ(decoded.entries_decoded, estimate.entries_decoded);
}

TEST(PhotoshopIrbDecodeTest, AcceptsTrailingZeroPadding)
{
    const std::array<std::byte, 3> payload = {
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
    };
    std::vector<std::byte> irb;
    append_irb_resource(0x1234, payload, &irb);
    irb.push_back(std::byte { 0x00 });
    irb.push_back(std::byte { 0x00 });
    irb.push_back(std::byte { 0x00 });
    irb.push_back(std::byte { 0x00 });

    const PhotoshopIrbDecodeResult estimate = measure_photoshop_irb(irb);
    EXPECT_EQ(estimate.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(estimate.resources_decoded, 1U);

    MetaStore store;
    const PhotoshopIrbDecodeResult decoded = decode_photoshop_irb(irb, store);
    EXPECT_EQ(decoded.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(decoded.resources_decoded, 1U);
    EXPECT_EQ(decoded.entries_decoded, 1U);
}

TEST(PhotoshopIrbDecodeTest, DecodesBoundedDerivedResourceFields)
{
    std::vector<std::byte> irb;

    std::vector<std::byte> resolution;
    append_u32be(0x00488000U, &resolution);  // 72.5
    append_u16be(1U, &resolution);           // inches
    append_u16be(1U, &resolution);           // width unit (unused here)
    append_u32be(0x00904000U, &resolution);  // 144.25
    append_u16be(2U, &resolution);           // cm
    append_u16be(1U, &resolution);           // height unit (unused here)
    append_irb_resource(0x03EDU, resolution, &irb);

    const std::array<std::byte, 16> digest = {
        std::byte { 0x00 }, std::byte { 0x01 }, std::byte { 0x02 },
        std::byte { 0x03 }, std::byte { 0x04 }, std::byte { 0x05 },
        std::byte { 0x06 }, std::byte { 0x07 }, std::byte { 0x08 },
        std::byte { 0x09 }, std::byte { 0x0A }, std::byte { 0x0B },
        std::byte { 0x0C }, std::byte { 0x0D }, std::byte { 0x0E },
        std::byte { 0x0F },
    };
    append_irb_resource(0x0425U, digest, &irb);

    std::vector<std::byte> pixel_info;
    append_u32be(1U, &pixel_info);
    append_u64be(0x3FF8000000000000ULL, &pixel_info);  // 1.5
    append_irb_resource(0x0428U, pixel_info, &irb);

    std::vector<std::byte> version_info;
    append_u32be(1U, &version_info);
    version_info.push_back(std::byte { 0x01 });
    append_utf16be_string32("Writer", &version_info);
    append_utf16be_string32("Reader", &version_info);
    append_u32be(1U, &version_info);
    append_irb_resource(0x0421U, version_info, &irb);

    const std::array<std::byte, 1> copyright_flag = {
        std::byte { 0x01 },
    };
    append_irb_resource(0x040AU, copyright_flag, &irb);

    std::vector<std::byte> global_angle;
    append_u32be(30U, &global_angle);
    append_irb_resource(0x040DU, global_angle, &irb);

    const std::array<std::byte, 25> url = {
        std::byte { 'h' },  std::byte { 't' }, std::byte { 't' },
        std::byte { 'p' },  std::byte { 's' }, std::byte { ':' },
        std::byte { '/' },  std::byte { '/' }, std::byte { 'e' },
        std::byte { 'x' },  std::byte { 'a' }, std::byte { 'm' },
        std::byte { 'p' },  std::byte { 'l' }, std::byte { 'e' },
        std::byte { '.' },  std::byte { 'c' }, std::byte { 'o' },
        std::byte { 'm' },  std::byte { '/' }, std::byte { 'i' },
        std::byte { 'r' },  std::byte { 'b' }, std::byte { 0x00 },
        std::byte { 0x00 },
    };
    append_irb_resource(0x040BU, url, &irb);

    const std::array<std::byte, 1> effects_visible = {
        std::byte { 0x00 },
    };
    append_irb_resource(0x0412U, effects_visible, &irb);

    const std::array<std::byte, 1> print_flags = {
        std::byte { 0x07 },
    };
    append_irb_resource(0x03F3U, print_flags, &irb);

    const std::array<std::byte, 1> effective_bw = {
        std::byte { 0x01 },
    };
    append_irb_resource(0x03FBU, effective_bw, &irb);

    std::vector<std::byte> target_layer_id;
    append_u16be(42U, &target_layer_id);
    append_irb_resource(0x0400U, target_layer_id, &irb);

    std::vector<std::byte> layers_group_info;
    append_u16be(7U, &layers_group_info);
    append_u16be(8U, &layers_group_info);
    append_u16be(9U, &layers_group_info);
    append_irb_resource(0x0402U, layers_group_info, &irb);

    std::vector<std::byte> jpeg_quality;
    append_u16be(2U, &jpeg_quality);
    append_u16be(0x0101U, &jpeg_quality);
    append_u16be(3U, &jpeg_quality);
    append_irb_resource(0x0406U, jpeg_quality, &irb);

    const std::array<std::byte, 1> watermark = {
        std::byte { 0x01 },
    };
    append_irb_resource(0x0410U, watermark, &irb);

    const std::array<std::byte, 1> icc_untagged = {
        std::byte { 0x00 },
    };
    append_irb_resource(0x0411U, icc_untagged, &irb);

    std::vector<std::byte> ids_base_value;
    append_u32be(1234U, &ids_base_value);
    append_irb_resource(0x0414U, ids_base_value, &irb);

    std::vector<std::byte> print_scale_info;
    append_u16be(2U, &print_scale_info);
    append_u32be(f32_bits(10.0f), &print_scale_info);
    append_u32be(f32_bits(20.0f), &print_scale_info);
    append_u32be(f32_bits(1.5f), &print_scale_info);
    append_irb_resource(0x0426U, print_scale_info, &irb);

    std::vector<std::byte> layer_selection_ids;
    append_u16be(3U, &layer_selection_ids);
    append_u32be(11U, &layer_selection_ids);
    append_u32be(22U, &layer_selection_ids);
    append_u32be(33U, &layer_selection_ids);
    append_irb_resource(0x042DU, layer_selection_ids, &irb);

    std::vector<std::byte> slice_info(20U, std::byte { 0x00 });
    append_utf16be_string32("Group", &slice_info);
    append_u32be(4U, &slice_info);
    append_irb_resource(0x041AU, slice_info, &irb);

    std::vector<std::byte> workflow_url;
    append_utf16be_string32("https://workflow.example", &workflow_url);
    append_irb_resource(0x041BU, workflow_url, &irb);

    std::vector<std::byte> url_list;
    append_u32be(2U, &url_list);
    append_u32be(0U, &url_list);
    append_u32be(1U, &url_list);
    append_utf16be_string32("https://list-1.example", &url_list);
    append_u32be(0U, &url_list);
    append_u32be(2U, &url_list);
    append_utf16be_string32("https://list-2.example", &url_list);
    append_irb_resource(0x041EU, url_list, &irb);

    std::vector<std::byte> indexed_color_table_count;
    append_u16be(256U, &indexed_color_table_count);
    append_irb_resource(0x0416U, indexed_color_table_count, &irb);

    std::vector<std::byte> transparent_index;
    append_u16be(17U, &transparent_index);
    append_irb_resource(0x0417U, transparent_index, &irb);

    std::vector<std::byte> global_altitude;
    append_u32be(99U, &global_altitude);
    append_irb_resource(0x0419U, global_altitude, &irb);

    const std::array<std::byte, 1> layer_groups_enabled_id = {
        std::byte { 0x01 },
    };
    append_irb_resource(0x0430U, layer_groups_enabled_id, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 25U);
    EXPECT_EQ(r.entries_decoded, 69U);

    const Entry* x_resolution = find_photoshop_irb_field(store, 0x03EDU,
                                                         "XResolution");
    ASSERT_NE(x_resolution, nullptr);
    EXPECT_EQ(x_resolution->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(x_resolution->value.elem_type, MetaElementType::F64);
    EXPECT_DOUBLE_EQ(std::bit_cast<double>(x_resolution->value.data.f64_bits),
                     72.5);
    EXPECT_TRUE(any(x_resolution->flags, EntryFlags::Derived));

    const Entry* units_x = find_photoshop_irb_field(store, 0x03EDU,
                                                    "DisplayedUnitsX");
    ASSERT_NE(units_x, nullptr);
    EXPECT_EQ(units_x->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(units_x->value.elem_type, MetaElementType::U16);
    EXPECT_EQ(units_x->value.data.u64, 1U);

    const Entry* y_resolution = find_photoshop_irb_field(store, 0x03EDU,
                                                         "YResolution");
    ASSERT_NE(y_resolution, nullptr);
    EXPECT_DOUBLE_EQ(std::bit_cast<double>(y_resolution->value.data.f64_bits),
                     144.25);

    const Entry* units_y = find_photoshop_irb_field(store, 0x03EDU,
                                                    "DisplayedUnitsY");
    ASSERT_NE(units_y, nullptr);
    EXPECT_EQ(units_y->value.data.u64, 2U);

    const Entry* iptc_digest = find_photoshop_irb_field(store, 0x0425U,
                                                        "IPTCDigest");
    ASSERT_NE(iptc_digest, nullptr);
    EXPECT_EQ(iptc_digest->value.kind, MetaValueKind::Text);
    EXPECT_EQ(iptc_digest->value.text_encoding, TextEncoding::Ascii);
    EXPECT_EQ(arena_string(store, iptc_digest->value.data.span),
              "000102030405060708090a0b0c0d0e0f");

    const Entry* pixel_aspect = find_photoshop_irb_field(store, 0x0428U,
                                                         "PixelAspectRatio");
    ASSERT_NE(pixel_aspect, nullptr);
    EXPECT_EQ(pixel_aspect->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(pixel_aspect->value.elem_type, MetaElementType::F64);
    EXPECT_DOUBLE_EQ(std::bit_cast<double>(pixel_aspect->value.data.f64_bits),
                     1.5);

    const Entry* copyright = find_photoshop_irb_field(store, 0x040AU,
                                                      "CopyrightFlag");
    ASSERT_NE(copyright, nullptr);
    EXPECT_EQ(copyright->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(copyright->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(copyright->value.data.u64, 1U);

    const Entry* has_real_merged_data
        = find_photoshop_irb_field(store, 0x0421U, "HasRealMergedData");
    ASSERT_NE(has_real_merged_data, nullptr);
    EXPECT_EQ(has_real_merged_data->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(has_real_merged_data->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(has_real_merged_data->value.data.u64, 1U);

    const Entry* writer_name = find_photoshop_irb_field(store, 0x0421U,
                                                        "WriterName");
    ASSERT_NE(writer_name, nullptr);
    EXPECT_EQ(writer_name->value.kind, MetaValueKind::Text);
    EXPECT_EQ(writer_name->value.text_encoding, TextEncoding::Utf8);
    EXPECT_EQ(arena_string(store, writer_name->value.data.span), "Writer");

    const Entry* reader_name = find_photoshop_irb_field(store, 0x0421U,
                                                        "ReaderName");
    ASSERT_NE(reader_name, nullptr);
    EXPECT_EQ(reader_name->value.kind, MetaValueKind::Text);
    EXPECT_EQ(reader_name->value.text_encoding, TextEncoding::Utf8);
    EXPECT_EQ(arena_string(store, reader_name->value.data.span), "Reader");

    const Entry* angle = find_photoshop_irb_field(store, 0x040DU,
                                                  "GlobalAngle");
    ASSERT_NE(angle, nullptr);
    EXPECT_EQ(angle->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(angle->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(angle->value.data.u64, 30U);

    const Entry* url_field = find_photoshop_irb_field(store, 0x040BU, "URL");
    ASSERT_NE(url_field, nullptr);
    EXPECT_EQ(url_field->value.kind, MetaValueKind::Text);
    EXPECT_EQ(url_field->value.text_encoding, TextEncoding::Ascii);
    EXPECT_EQ(arena_string(store, url_field->value.data.span),
              "https://example.com/irb");

    const Entry* effects = find_photoshop_irb_field(store, 0x0412U,
                                                    "EffectsVisible");
    ASSERT_NE(effects, nullptr);
    EXPECT_EQ(effects->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(effects->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(effects->value.data.u64, 0U);

    const Entry* print_flags_field = find_photoshop_irb_field(store, 0x03F3U,
                                                              "PrintFlags");
    ASSERT_NE(print_flags_field, nullptr);
    EXPECT_EQ(print_flags_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(print_flags_field->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(print_flags_field->value.data.u64, 7U);

    const Entry* effective_bw_field = find_photoshop_irb_field(store, 0x03FBU,
                                                               "EffectiveBW");
    ASSERT_NE(effective_bw_field, nullptr);
    EXPECT_EQ(effective_bw_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(effective_bw_field->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(effective_bw_field->value.data.u64, 1U);

    const Entry* target_layer = find_photoshop_irb_field(store, 0x0400U,
                                                         "TargetLayerID");
    ASSERT_NE(target_layer, nullptr);
    EXPECT_EQ(target_layer->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(target_layer->value.elem_type, MetaElementType::U16);
    EXPECT_EQ(target_layer->value.data.u64, 42U);

    const Entry* layers_group_info_count
        = find_photoshop_irb_field(store, 0x0402U, "LayersGroupInfoCount");
    ASSERT_NE(layers_group_info_count, nullptr);
    EXPECT_EQ(layers_group_info_count->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(layers_group_info_count->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(layers_group_info_count->value.data.u64, 3U);

    const std::vector<uint16_t> layers_group_info_values
        = collect_photoshop_irb_u16_fields(store, 0x0402U, "LayersGroupInfo");
    ASSERT_EQ(layers_group_info_values.size(), 3U);
    EXPECT_EQ(layers_group_info_values[0], 7U);
    EXPECT_EQ(layers_group_info_values[1], 8U);
    EXPECT_EQ(layers_group_info_values[2], 9U);

    const Entry* photoshop_quality
        = find_photoshop_irb_field(store, 0x0406U, "PhotoshopQuality");
    ASSERT_NE(photoshop_quality, nullptr);
    EXPECT_EQ(photoshop_quality->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(photoshop_quality->value.elem_type, MetaElementType::I16);
    EXPECT_EQ(photoshop_quality->value.data.i64, 2);

    const Entry* photoshop_format = find_photoshop_irb_field(store, 0x0406U,
                                                             "PhotoshopFormat");
    ASSERT_NE(photoshop_format, nullptr);
    EXPECT_EQ(photoshop_format->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(photoshop_format->value.elem_type, MetaElementType::I16);
    EXPECT_EQ(photoshop_format->value.data.i64, 0x0101);

    const Entry* progressive_scans
        = find_photoshop_irb_field(store, 0x0406U, "ProgressiveScans");
    ASSERT_NE(progressive_scans, nullptr);
    EXPECT_EQ(progressive_scans->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(progressive_scans->value.elem_type, MetaElementType::I16);
    EXPECT_EQ(progressive_scans->value.data.i64, 3);

    const Entry* watermark_field = find_photoshop_irb_field(store, 0x0410U,
                                                            "Watermark");
    ASSERT_NE(watermark_field, nullptr);
    EXPECT_EQ(watermark_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(watermark_field->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(watermark_field->value.data.u64, 1U);

    const Entry* icc_untagged_field = find_photoshop_irb_field(store, 0x0411U,
                                                               "ICC_Untagged");
    ASSERT_NE(icc_untagged_field, nullptr);
    EXPECT_EQ(icc_untagged_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(icc_untagged_field->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(icc_untagged_field->value.data.u64, 0U);

    const Entry* ids_base_value_field
        = find_photoshop_irb_field(store, 0x0414U, "IDsBaseValue");
    ASSERT_NE(ids_base_value_field, nullptr);
    EXPECT_EQ(ids_base_value_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(ids_base_value_field->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(ids_base_value_field->value.data.u64, 1234U);

    const Entry* print_style_field = find_photoshop_irb_field(store, 0x0426U,
                                                              "PrintStyle");
    ASSERT_NE(print_style_field, nullptr);
    EXPECT_EQ(print_style_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(print_style_field->value.elem_type, MetaElementType::U16);
    EXPECT_EQ(print_style_field->value.data.u64, 2U);

    const Entry* print_position_x_field
        = find_photoshop_irb_field(store, 0x0426U, "PrintPositionX");
    ASSERT_NE(print_position_x_field, nullptr);
    EXPECT_EQ(print_position_x_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(print_position_x_field->value.elem_type, MetaElementType::F32);
    EXPECT_EQ(print_position_x_field->value.data.f32_bits, f32_bits(10.0f));

    const Entry* print_position_y_field
        = find_photoshop_irb_field(store, 0x0426U, "PrintPositionY");
    ASSERT_NE(print_position_y_field, nullptr);
    EXPECT_EQ(print_position_y_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(print_position_y_field->value.elem_type, MetaElementType::F32);
    EXPECT_EQ(print_position_y_field->value.data.f32_bits, f32_bits(20.0f));

    const Entry* print_scale_field = find_photoshop_irb_field(store, 0x0426U,
                                                              "PrintScale");
    ASSERT_NE(print_scale_field, nullptr);
    EXPECT_EQ(print_scale_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(print_scale_field->value.elem_type, MetaElementType::F32);
    EXPECT_EQ(print_scale_field->value.data.f32_bits, f32_bits(1.5f));

    const Entry* layer_selection_count
        = find_photoshop_irb_field(store, 0x042DU, "LayerSelectionIDCount");
    ASSERT_NE(layer_selection_count, nullptr);
    EXPECT_EQ(layer_selection_count->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(layer_selection_count->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(layer_selection_count->value.data.u64, 3U);

    const std::vector<uint32_t> layer_selection_values
        = collect_photoshop_irb_u32_fields(store, 0x042DU, "LayerSelectionID");
    ASSERT_EQ(layer_selection_values.size(), 3U);
    EXPECT_EQ(layer_selection_values[0], 11U);
    EXPECT_EQ(layer_selection_values[1], 22U);
    EXPECT_EQ(layer_selection_values[2], 33U);

    const Entry* slices_group_name
        = find_photoshop_irb_field(store, 0x041AU, "SlicesGroupName");
    ASSERT_NE(slices_group_name, nullptr);
    EXPECT_EQ(slices_group_name->value.kind, MetaValueKind::Text);
    EXPECT_EQ(slices_group_name->value.text_encoding, TextEncoding::Utf8);
    EXPECT_EQ(arena_string(store, slices_group_name->value.data.span), "Group");

    const Entry* num_slices = find_photoshop_irb_field(store, 0x041AU,
                                                       "NumSlices");
    ASSERT_NE(num_slices, nullptr);
    EXPECT_EQ(num_slices->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(num_slices->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(num_slices->value.data.u64, 4U);

    const Entry* workflow_url_field = find_photoshop_irb_field(store, 0x041BU,
                                                               "WorkflowURL");
    ASSERT_NE(workflow_url_field, nullptr);
    EXPECT_EQ(workflow_url_field->value.kind, MetaValueKind::Text);
    EXPECT_EQ(workflow_url_field->value.text_encoding, TextEncoding::Utf8);
    EXPECT_EQ(arena_string(store, workflow_url_field->value.data.span),
              "https://workflow.example");

    const Entry* url_list_count = find_photoshop_irb_field(store, 0x041EU,
                                                           "URLListCount");
    ASSERT_NE(url_list_count, nullptr);
    EXPECT_EQ(url_list_count->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(url_list_count->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(url_list_count->value.data.u64, 2U);

    const std::vector<std::string_view> url_list_values
        = collect_photoshop_irb_text_fields(store, 0x041EU, "URL");
    ASSERT_EQ(url_list_values.size(), 2U);
    EXPECT_EQ(url_list_values[0], "https://list-1.example");
    EXPECT_EQ(url_list_values[1], "https://list-2.example");

    const Entry* indexed_color_table_count_field
        = find_photoshop_irb_field(store, 0x0416U, "IndexedColorTableCount");
    ASSERT_NE(indexed_color_table_count_field, nullptr);
    EXPECT_EQ(indexed_color_table_count_field->value.kind,
              MetaValueKind::Scalar);
    EXPECT_EQ(indexed_color_table_count_field->value.elem_type,
              MetaElementType::U16);
    EXPECT_EQ(indexed_color_table_count_field->value.data.u64, 256U);

    const Entry* transparent_index_field
        = find_photoshop_irb_field(store, 0x0417U, "TransparentIndex");
    ASSERT_NE(transparent_index_field, nullptr);
    EXPECT_EQ(transparent_index_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(transparent_index_field->value.elem_type, MetaElementType::U16);
    EXPECT_EQ(transparent_index_field->value.data.u64, 17U);

    const Entry* global_altitude_field
        = find_photoshop_irb_field(store, 0x0419U, "GlobalAltitude");
    ASSERT_NE(global_altitude_field, nullptr);
    EXPECT_EQ(global_altitude_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(global_altitude_field->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(global_altitude_field->value.data.u64, 99U);

    const Entry* layer_groups_enabled_id_field
        = find_photoshop_irb_field(store, 0x0430U, "LayerGroupsEnabledID");
    ASSERT_NE(layer_groups_enabled_id_field, nullptr);
    EXPECT_EQ(layer_groups_enabled_id_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(layer_groups_enabled_id_field->value.elem_type,
              MetaElementType::U8);
    EXPECT_EQ(layer_groups_enabled_id_field->value.data.u64, 1U);
}

TEST(PhotoshopIrbDecodeTest, KeepsShortKnownResourcesRawOnly)
{
    const std::array<std::byte, 3> short_resolution = {
        std::byte { 0x00 },
        std::byte { 0x01 },
        std::byte { 0x02 },
    };
    const std::array<std::byte, 10> short_pixel_info = {
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x01 }, std::byte { 0x3F }, std::byte { 0xF8 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 },
    };
    const std::array<std::byte, 3> short_version_info = {
        std::byte { 0x00 },
        std::byte { 0x00 },
        std::byte { 0x00 },
    };
    const std::array<std::byte, 0> empty_copyright    = {};
    const std::array<std::byte, 0> empty_url          = {};
    const std::array<std::byte, 3> short_global_angle = {
        std::byte { 0x00 },
        std::byte { 0x00 },
        std::byte { 0x00 },
    };
    const std::array<std::byte, 0> empty_effects_visible = {};
    const std::array<std::byte, 0> empty_print_flags     = {};
    const std::array<std::byte, 0> empty_effective_bw    = {};
    const std::array<std::byte, 1> short_target_layer_id = {
        std::byte { 0x00 },
    };
    const std::array<std::byte, 1> short_layers_group_info = {
        std::byte { 0x00 },
    };
    const std::array<std::byte, 5> short_jpeg_quality = {
        std::byte { 0x00 }, std::byte { 0x02 }, std::byte { 0x01 },
        std::byte { 0x01 }, std::byte { 0x00 },
    };
    const std::array<std::byte, 0> empty_watermark      = {};
    const std::array<std::byte, 0> empty_icc_untagged   = {};
    const std::array<std::byte, 3> short_ids_base_value = {
        std::byte { 0x00 },
        std::byte { 0x00 },
        std::byte { 0x00 },
    };
    const std::array<std::byte, 11> short_print_scale_info = {
        std::byte { 0x00 }, std::byte { 0x02 }, std::byte { 0x41 },
        std::byte { 0x20 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x41 }, std::byte { 0xA0 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x3F },
    };
    const std::array<std::byte, 1> short_indexed_color_table_count = {
        std::byte { 0x00 },
    };
    const std::array<std::byte, 1> short_transparent_index = {
        std::byte { 0x00 },
    };
    const std::array<std::byte, 3> short_global_altitude = {
        std::byte { 0x00 },
        std::byte { 0x00 },
        std::byte { 0x00 },
    };
    const std::array<std::byte, 20> short_slice_info  = {};
    const std::array<std::byte, 3> short_workflow_url = {
        std::byte { 0x00 },
        std::byte { 0x00 },
        std::byte { 0x00 },
    };
    const std::array<std::byte, 7> short_url_list = {
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x01 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 },
    };
    const std::array<std::byte, 1> short_layer_selection_ids = {
        std::byte { 0x00 },
    };
    const std::array<std::byte, 0> empty_layer_groups_enabled_id = {};
    std::vector<std::byte> irb;
    append_irb_resource(0x03EDU, short_resolution, &irb);
    append_irb_resource(0x0428U, short_pixel_info, &irb);
    append_irb_resource(0x0421U, short_version_info, &irb);
    append_irb_resource(0x040AU, empty_copyright, &irb);
    append_irb_resource(0x040BU, empty_url, &irb);
    append_irb_resource(0x040DU, short_global_angle, &irb);
    append_irb_resource(0x0412U, empty_effects_visible, &irb);
    append_irb_resource(0x03F3U, empty_print_flags, &irb);
    append_irb_resource(0x03FBU, empty_effective_bw, &irb);
    append_irb_resource(0x0400U, short_target_layer_id, &irb);
    append_irb_resource(0x0402U, short_layers_group_info, &irb);
    append_irb_resource(0x0406U, short_jpeg_quality, &irb);
    append_irb_resource(0x0410U, empty_watermark, &irb);
    append_irb_resource(0x0411U, empty_icc_untagged, &irb);
    append_irb_resource(0x0414U, short_ids_base_value, &irb);
    append_irb_resource(0x0426U, short_print_scale_info, &irb);
    append_irb_resource(0x041AU, short_slice_info, &irb);
    append_irb_resource(0x041BU, short_workflow_url, &irb);
    append_irb_resource(0x041EU, short_url_list, &irb);
    append_irb_resource(0x042DU, short_layer_selection_ids, &irb);
    append_irb_resource(0x0416U, short_indexed_color_table_count, &irb);
    append_irb_resource(0x0417U, short_transparent_index, &irb);
    append_irb_resource(0x0419U, short_global_altitude, &irb);
    append_irb_resource(0x0430U, empty_layer_groups_enabled_id, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 24U);
    EXPECT_EQ(r.entries_decoded, 27U);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03EDU, "XResolution"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0428U, "PixelAspectRatio"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0421U, "HasRealMergedData"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0421U, "WriterName"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0421U, "ReaderName"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x040AU, "CopyrightFlag"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x040BU, "URL"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x040DU, "GlobalAngle"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0412U, "EffectsVisible"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03F3U, "PrintFlags"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03FBU, "EffectiveBW"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0400U, "TargetLayerID"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0402U, "LayersGroupInfoCount"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0402U, "LayersGroupInfo"),
              nullptr);
    const Entry* short_photoshop_quality
        = find_photoshop_irb_field(store, 0x0406U, "PhotoshopQuality");
    ASSERT_NE(short_photoshop_quality, nullptr);
    EXPECT_EQ(short_photoshop_quality->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(short_photoshop_quality->value.elem_type, MetaElementType::I16);
    EXPECT_EQ(short_photoshop_quality->value.data.i64, 2);

    const Entry* short_photoshop_format
        = find_photoshop_irb_field(store, 0x0406U, "PhotoshopFormat");
    ASSERT_NE(short_photoshop_format, nullptr);
    EXPECT_EQ(short_photoshop_format->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(short_photoshop_format->value.elem_type, MetaElementType::I16);
    EXPECT_EQ(short_photoshop_format->value.data.i64, 0x0101);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0406U, "ProgressiveScans"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0410U, "Watermark"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0411U, "ICC_Untagged"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0414U, "IDsBaseValue"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0426U, "PrintStyle"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0426U, "PrintPositionX"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0426U, "PrintPositionY"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0426U, "PrintScale"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x041AU, "SlicesGroupName"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x041AU, "NumSlices"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x041BU, "WorkflowURL"), nullptr);
    const Entry* short_url_list_count
        = find_photoshop_irb_field(store, 0x041EU, "URLListCount");
    ASSERT_NE(short_url_list_count, nullptr);
    EXPECT_EQ(short_url_list_count->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(short_url_list_count->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(short_url_list_count->value.data.u64, 0U);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x041EU, "URL"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x042DU, "LayerSelectionIDCount"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x042DU, "LayerSelectionID"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0416U, "IndexedColorTableCount"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0417U, "TransparentIndex"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0419U, "GlobalAltitude"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0430U, "LayerGroupsEnabledID"),
              nullptr);
}

TEST(PhotoshopIrbDecodeTest, DecodesClippingPathNameWithLatinCharsetByDefault)
{
    std::vector<std::byte> clipping_path_name;
    const std::array<std::byte, 5> text = {
        std::byte { 'C' },  std::byte { 'a' }, std::byte { 'f' },
        std::byte { 0xE9 }, std::byte { '!' },
    };
    append_pascal_string(text, &clipping_path_name);
    clipping_path_name.insert(clipping_path_name.end(), 6U, std::byte { 0x00 });

    std::vector<std::byte> irb;
    append_irb_resource(0x0BB7U, clipping_path_name, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 1U);
    EXPECT_EQ(r.entries_decoded, 2U);

    const Entry* clipping_path = find_photoshop_irb_field(store, 0x0BB7U,
                                                          "ClippingPathName");
    ASSERT_NE(clipping_path, nullptr);
    EXPECT_EQ(clipping_path->value.kind, MetaValueKind::Text);
    EXPECT_EQ(clipping_path->value.text_encoding, TextEncoding::Utf8);
    EXPECT_EQ(arena_string(store, clipping_path->value.data.span),
              "Caf\xC3\xA9!");
}

TEST(PhotoshopIrbDecodeTest, RespectsAsciiPolicyForClippingPathName)
{
    std::vector<std::byte> clipping_path_name;
    const std::array<std::byte, 5> text = {
        std::byte { 'C' },  std::byte { 'a' }, std::byte { 'f' },
        std::byte { 0xE9 }, std::byte { '!' },
    };
    append_pascal_string(text, &clipping_path_name);

    std::vector<std::byte> irb;
    append_irb_resource(0x0BB7U, clipping_path_name, &irb);

    MetaStore store;
    PhotoshopIrbDecodeOptions options;
    options.string_charset           = PhotoshopIrbStringCharset::Ascii;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store,
                                                            options);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 1U);
    EXPECT_EQ(r.entries_decoded, 1U);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0BB7U, "ClippingPathName"),
              nullptr);
}

TEST(PhotoshopIrbDecodeTest, DecodesBoundedChannelOptions)
{
    std::vector<std::byte> channel_options;
    append_u16be(0U, &channel_options);
    append_u16be(1U, &channel_options);
    append_u16be(2U, &channel_options);
    append_u16be(3U, &channel_options);
    append_u16be(4U, &channel_options);
    channel_options.push_back(std::byte { 0U });
    channel_options.push_back(std::byte { 75U });
    channel_options.push_back(std::byte { 2U });
    append_u16be(7U, &channel_options);
    append_u16be(10U, &channel_options);
    append_u16be(20U, &channel_options);
    append_u16be(30U, &channel_options);
    append_u16be(40U, &channel_options);
    channel_options.push_back(std::byte { 0U });
    channel_options.push_back(std::byte { 50U });
    channel_options.push_back(std::byte { 1U });

    std::vector<std::byte> irb;
    append_irb_resource(0x0435U, channel_options, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 1U);
    EXPECT_EQ(r.entries_decoded, 18U);

    const Entry* count = find_photoshop_irb_field(store, 0x0435U,
                                                  "ChannelOptionsCount");
    ASSERT_NE(count, nullptr);
    EXPECT_EQ(count->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(count->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(count->value.data.u64, 2U);

    const std::vector<uint32_t> indexes
        = collect_photoshop_irb_u32_fields(store, 0x0435U, "ChannelIndex");
    ASSERT_EQ(indexes.size(), 2U);
    EXPECT_EQ(indexes[0], 0U);
    EXPECT_EQ(indexes[1], 1U);

    const std::vector<uint16_t> color_spaces
        = collect_photoshop_irb_u16_fields(store, 0x0435U, "ChannelColorSpace");
    ASSERT_EQ(color_spaces.size(), 2U);
    EXPECT_EQ(color_spaces[0], 0U);
    EXPECT_EQ(color_spaces[1], 7U);

    const std::vector<uint16_t> color_data
        = collect_photoshop_irb_u16_fields(store, 0x0435U, "ChannelColorData");
    ASSERT_EQ(color_data.size(), 8U);
    EXPECT_EQ(color_data[0], 1U);
    EXPECT_EQ(color_data[1], 2U);
    EXPECT_EQ(color_data[2], 3U);
    EXPECT_EQ(color_data[3], 4U);
    EXPECT_EQ(color_data[4], 10U);
    EXPECT_EQ(color_data[5], 20U);
    EXPECT_EQ(color_data[6], 30U);
    EXPECT_EQ(color_data[7], 40U);

    const std::vector<uint8_t> opacity
        = collect_photoshop_irb_u8_fields(store, 0x0435U, "ChannelOpacity");
    ASSERT_EQ(opacity.size(), 2U);
    EXPECT_EQ(opacity[0], 75U);
    EXPECT_EQ(opacity[1], 50U);

    const std::vector<uint8_t> color_indicates
        = collect_photoshop_irb_u8_fields(store, 0x0435U,
                                          "ChannelColorIndicates");
    ASSERT_EQ(color_indicates.size(), 2U);
    EXPECT_EQ(color_indicates[0], 2U);
    EXPECT_EQ(color_indicates[1], 1U);
}

TEST(PhotoshopIrbDecodeTest, DecodesBoundedPrintFlagsInfo)
{
    std::vector<std::byte> print_flags_info;
    append_u16be(1U, &print_flags_info);
    print_flags_info.push_back(std::byte { 1U });
    print_flags_info.push_back(std::byte { 0U });
    append_u32be(144U, &print_flags_info);
    append_u16be(2U, &print_flags_info);

    std::vector<std::byte> irb;
    append_irb_resource(0x2710U, print_flags_info, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 1U);
    EXPECT_EQ(r.entries_decoded, 5U);

    const Entry* version = find_photoshop_irb_field(store, 0x2710U,
                                                    "PrintFlagsInfoVersion");
    ASSERT_NE(version, nullptr);
    EXPECT_EQ(version->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(version->value.elem_type, MetaElementType::U16);
    EXPECT_EQ(version->value.data.u64, 1U);

    const Entry* center_crop_marks
        = find_photoshop_irb_field(store, 0x2710U, "CenterCropMarks");
    ASSERT_NE(center_crop_marks, nullptr);
    EXPECT_EQ(center_crop_marks->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(center_crop_marks->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(center_crop_marks->value.data.u64, 1U);

    const Entry* bleed_width_value
        = find_photoshop_irb_field(store, 0x2710U, "BleedWidthValue");
    ASSERT_NE(bleed_width_value, nullptr);
    EXPECT_EQ(bleed_width_value->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(bleed_width_value->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(bleed_width_value->value.data.u64, 144U);

    const Entry* bleed_width_scale
        = find_photoshop_irb_field(store, 0x2710U, "BleedWidthScale");
    ASSERT_NE(bleed_width_scale, nullptr);
    EXPECT_EQ(bleed_width_scale->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(bleed_width_scale->value.elem_type, MetaElementType::U16);
    EXPECT_EQ(bleed_width_scale->value.data.u64, 2U);
}

TEST(PhotoshopIrbDecodeTest, KeepsShortPrintFlagsInfoRawOnly)
{
    const std::array<std::byte, 9> short_print_flags_info = {
        std::byte { 0x00 }, std::byte { 0x01 }, std::byte { 0x01 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x90 }, std::byte { 0x00 },
    };
    std::vector<std::byte> irb;
    append_irb_resource(0x2710U, short_print_flags_info, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 1U);
    EXPECT_EQ(r.entries_decoded, 1U);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x2710U, "PrintFlagsInfoVersion"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x2710U, "CenterCropMarks"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x2710U, "BleedWidthValue"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x2710U, "BleedWidthScale"),
              nullptr);
}

TEST(PhotoshopIrbDecodeTest, NamesAdditionalKnownResources)
{
    EXPECT_EQ(photoshop_irb_resource_name(0x03E9U), "MacintoshPrintInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x03EAU), "XMLData");
    EXPECT_EQ(photoshop_irb_resource_name(0x0421U), "VersionInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x0408U), "GridGuidesInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x040AU), "CopyrightFlag");
    EXPECT_EQ(photoshop_irb_resource_name(0x040BU), "URL");
    EXPECT_EQ(photoshop_irb_resource_name(0x040FU), "ICC_Profile");
    EXPECT_EQ(photoshop_irb_resource_name(0x040DU), "GlobalAngle");
    EXPECT_EQ(photoshop_irb_resource_name(0x0BB7U), "ClippingPathName");
    EXPECT_EQ(photoshop_irb_resource_name(0x0412U), "EffectsVisible");
    EXPECT_EQ(photoshop_irb_resource_name(0x03F3U), "PrintFlags");
    EXPECT_EQ(photoshop_irb_resource_name(0x03FBU), "EffectiveBW");
    EXPECT_EQ(photoshop_irb_resource_name(0x0400U), "TargetLayerID");
    EXPECT_EQ(photoshop_irb_resource_name(0x0402U), "LayersGroupInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x0406U), "JPEG_Quality");
    EXPECT_EQ(photoshop_irb_resource_name(0x0410U), "Watermark");
    EXPECT_EQ(photoshop_irb_resource_name(0x0411U), "ICC_Untagged");
    EXPECT_EQ(photoshop_irb_resource_name(0x0414U), "IDsBaseValue");
    EXPECT_EQ(photoshop_irb_resource_name(0x0416U), "IndexedColorTableCount");
    EXPECT_EQ(photoshop_irb_resource_name(0x0417U), "TransparentIndex");
    EXPECT_EQ(photoshop_irb_resource_name(0x0419U), "GlobalAltitude");
    EXPECT_EQ(photoshop_irb_resource_name(0x041AU), "SliceInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x041BU), "WorkflowURL");
    EXPECT_EQ(photoshop_irb_resource_name(0x041EU), "URL_List");
    EXPECT_EQ(photoshop_irb_resource_name(0x0424U), "XMP");
    EXPECT_EQ(photoshop_irb_resource_name(0x0426U), "PrintScaleInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x0429U), "LayerComps");
    EXPECT_EQ(photoshop_irb_resource_name(0x042DU), "LayerSelectionIDs");
    EXPECT_EQ(photoshop_irb_resource_name(0x042FU), "PrintInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x0430U), "LayerGroupsEnabledID");
    EXPECT_EQ(photoshop_irb_resource_name(0x0432U), "MeasurementScale");
    EXPECT_EQ(photoshop_irb_resource_name(0x0433U), "TimelineInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x0435U), "ChannelOptions");
    EXPECT_EQ(photoshop_irb_resource_name(0x0436U), "OnionSkins");
    EXPECT_EQ(photoshop_irb_resource_name(0x0438U), "CountInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x043AU), "PrintInfo2");
    EXPECT_EQ(photoshop_irb_resource_name(0x043CU), "MacintoshNSPrintInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x043EU), "AutoSaveFilePath");
    EXPECT_EQ(photoshop_irb_resource_name(0x043FU), "AutoSaveFormat");
    EXPECT_EQ(photoshop_irb_resource_name(0x0440U), "PathSelectionState");
    EXPECT_EQ(photoshop_irb_resource_name(0x1B58U), "ImageReadyVariables");
    EXPECT_EQ(photoshop_irb_resource_name(0x1B59U), "ImageReadyDataSets");
    EXPECT_EQ(photoshop_irb_resource_name(0x2710U), "PrintFlagsInfo");
}

}  // namespace openmeta
