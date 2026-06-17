// SPDX-License-Identifier: Apache-2.0

#include "openmeta/photoshop_irb_decode.h"

#include "openmeta/container_scan.h"

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

    static void write_u16be(uint16_t v, size_t off, std::vector<std::byte>* out)
    {
        (*out)[off + 0]
            = std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) };
        (*out)[off + 1]
            = std::byte { static_cast<unsigned char>((v >> 0) & 0xFF) };
    }

    static void write_u32be(uint32_t v, size_t off, std::vector<std::byte>* out)
    {
        (*out)[off + 0]
            = std::byte { static_cast<unsigned char>((v >> 24) & 0xFF) };
        (*out)[off + 1]
            = std::byte { static_cast<unsigned char>((v >> 16) & 0xFF) };
        (*out)[off + 2]
            = std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) };
        (*out)[off + 3]
            = std::byte { static_cast<unsigned char>((v >> 0) & 0xFF) };
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

    static void append_descriptor_key4(char a, char b, char c, char d,
                                       std::vector<std::byte>* out)
    {
        append_u32be(0U, out);
        out->push_back(std::byte { static_cast<unsigned char>(a) });
        out->push_back(std::byte { static_cast<unsigned char>(b) });
        out->push_back(std::byte { static_cast<unsigned char>(c) });
        out->push_back(std::byte { static_cast<unsigned char>(d) });
    }

    static void append_descriptor_type(char a, char b, char c, char d,
                                       std::vector<std::byte>* out)
    {
        out->push_back(std::byte { static_cast<unsigned char>(a) });
        out->push_back(std::byte { static_cast<unsigned char>(b) });
        out->push_back(std::byte { static_cast<unsigned char>(c) });
        out->push_back(std::byte { static_cast<unsigned char>(d) });
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

    static std::vector<std::byte> make_minimal_icc_profile()
    {
        std::vector<std::byte> icc(132U, std::byte { 0x00 });
        write_u32be(static_cast<uint32_t>(icc.size()), 0U, &icc);
        write_u32be(0x04300000U, 8U, &icc);
        write_u32be(fourcc('m', 'n', 't', 'r'), 12U, &icc);
        write_u32be(fourcc('R', 'G', 'B', ' '), 16U, &icc);
        write_u32be(fourcc('X', 'Y', 'Z', ' '), 20U, &icc);
        write_u16be(2026U, 24U, &icc);
        write_u16be(5U, 26U, &icc);
        write_u16be(24U, 28U, &icc);
        icc[36] = std::byte { 'a' };
        icc[37] = std::byte { 'c' };
        icc[38] = std::byte { 's' };
        icc[39] = std::byte { 'p' };
        write_u32be(fourcc('o', 'm', 'e', 't'), 80U, &icc);
        write_u32be(0U, 128U, &icc);
        return icc;
    }

    static const Entry* find_icc_header_field(const MetaStore& store,
                                              uint32_t offset) noexcept
    {
        for (size_t i = 0; i < store.entries().size(); ++i) {
            const Entry& e = store.entry(static_cast<EntryId>(i));
            if (e.key.kind == MetaKeyKind::IccHeaderField
                && e.key.data.icc_header_field.offset == offset) {
                return &e;
            }
        }
        return nullptr;
    }

    static const Entry* find_xmp_property(const MetaStore& store,
                                          std::string_view schema_ns,
                                          std::string_view path) noexcept
    {
        for (size_t i = 0; i < store.entries().size(); ++i) {
            const Entry& e = store.entry(static_cast<EntryId>(i));
            if (e.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }
            if (arena_string(store, e.key.data.xmp_property.schema_ns)
                    == schema_ns
                && arena_string(store, e.key.data.xmp_property.property_path)
                       == path) {
                return &e;
            }
        }
        return nullptr;
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

    static std::vector<int32_t> collect_photoshop_irb_i32_fields(
        const MetaStore& store, uint16_t resource_id, std::string_view field)
    {
        std::vector<int32_t> out;
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
                || e.value.elem_type != MetaElementType::I32) {
                continue;
            }
            out.push_back(static_cast<int32_t>(e.value.data.i64));
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
    ASSERT_EQ(store.entries().size(), 4U);

    uint32_t irb_entries       = 0;
    uint32_t iptc_entries      = 0;
    uint32_t irb_field_entries = 0;
    for (size_t i = 0; i < store.entries().size(); ++i) {
        const Entry& e = store.entry(static_cast<EntryId>(i));
        if (e.key.kind == MetaKeyKind::PhotoshopIrb) {
            irb_entries += 1;
            continue;
        }
        if (e.key.kind == MetaKeyKind::PhotoshopIrbField) {
            irb_field_entries += 1;
            EXPECT_EQ(e.value.data.u64, 9U);
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
    EXPECT_EQ(irb_field_entries, 1U);
}

TEST(PhotoshopIrbDecodeTest, DecodesEmbeddedXmpAndIccResources)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'/>"
          "</rdf:RDF>"
          "</x:xmpmeta>";
    const std::span<const std::byte> xmp_bytes(
        reinterpret_cast<const std::byte*>(xmp.data()), xmp.size());

    const std::vector<std::byte> icc = make_minimal_icc_profile();

    std::vector<std::byte> irb;
    append_irb_resource(0x0424U, xmp_bytes, &irb);
    append_irb_resource(0x040FU,
                        std::span<const std::byte>(icc.data(), icc.size()),
                        &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 2U);
    EXPECT_GT(r.icc_entries_decoded, 0U);

    const Entry* xmp_bytes_field = find_photoshop_irb_field(store, 0x0424U,
                                                            "XMPPacketBytes");
    ASSERT_NE(xmp_bytes_field, nullptr);
    EXPECT_EQ(xmp_bytes_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(xmp_bytes_field->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(xmp_bytes_field->value.data.u64, xmp.size());

    const Entry* icc_bytes_field = find_photoshop_irb_field(store, 0x040FU,
                                                            "ICCProfileBytes");
    ASSERT_NE(icc_bytes_field, nullptr);
    EXPECT_EQ(icc_bytes_field->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(icc_bytes_field->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(icc_bytes_field->value.data.u64, icc.size());

    const Entry* icc_size = find_icc_header_field(store, 0U);
    ASSERT_NE(icc_size, nullptr);
    EXPECT_EQ(icc_size->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(icc_size->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(icc_size->value.data.u64, icc.size());

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT
    EXPECT_EQ(r.xmp_entries_decoded, 1U);
    const Entry* creator_tool
        = find_xmp_property(store, "http://ns.adobe.com/xap/1.0/",
                            "CreatorTool");
    ASSERT_NE(creator_tool, nullptr);
    EXPECT_TRUE(any(creator_tool->flags, EntryFlags::Derived));
    EXPECT_EQ(creator_tool->value.kind, MetaValueKind::Text);
    EXPECT_EQ(arena_string(store, creator_tool->value.data.span), "OpenMeta");
#else
    EXPECT_EQ(r.xmp_entries_decoded, 0U);
#endif
}

TEST(PhotoshopIrbDecodeTest, DecodesEmbeddedExifResourceByteCounts)
{
    const std::array<std::byte, 4> exif_a = {
        std::byte { 'E' },
        std::byte { 'x' },
        std::byte { 'i' },
        std::byte { 'f' },
    };
    const std::array<std::byte, 2> exif_b = {
        std::byte { 'I' },
        std::byte { 'I' },
    };

    std::vector<std::byte> irb;
    append_irb_resource(0x0422U, exif_a, &irb);
    append_irb_resource(0x0423U, exif_b, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 2U);

    const Entry* exif_info = find_photoshop_irb_field(store, 0x0422U,
                                                      "EXIFInfoBytes");
    ASSERT_NE(exif_info, nullptr);
    EXPECT_EQ(exif_info->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(exif_info->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(exif_info->value.data.u64, 4U);

    const Entry* exif_info2 = find_photoshop_irb_field(store, 0x0423U,
                                                       "EXIFInfo2Bytes");
    ASSERT_NE(exif_info2, nullptr);
    EXPECT_EQ(exif_info2->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(exif_info2->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(exif_info2->value.data.u64, 2U);
}

TEST(PhotoshopIrbDecodeTest, DecodesThumbnailHeaderResources)
{
    std::vector<std::byte> thumbnail;
    append_u32be(1U, &thumbnail);
    append_u32be(160U, &thumbnail);
    append_u32be(90U, &thumbnail);
    append_u32be(480U, &thumbnail);
    append_u32be(43200U, &thumbnail);
    append_u32be(1234U, &thumbnail);
    append_u16be(24U, &thumbnail);
    append_u16be(1U, &thumbnail);
    thumbnail.push_back(std::byte { 0xFF });
    thumbnail.push_back(std::byte { 0xD8 });
    thumbnail.push_back(std::byte { 0xFF });

    std::vector<std::byte> irb;
    append_irb_resource(0x040CU,
                        std::span<const std::byte>(thumbnail.data(),
                                                   thumbnail.size()),
                        &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 1U);

    const std::vector<uint32_t> format
        = collect_photoshop_irb_u32_fields(store, 0x040CU, "ThumbnailFormat");
    const std::vector<uint32_t> width
        = collect_photoshop_irb_u32_fields(store, 0x040CU, "ThumbnailWidth");
    const std::vector<uint32_t> height
        = collect_photoshop_irb_u32_fields(store, 0x040CU, "ThumbnailHeight");
    const std::vector<uint32_t> compressed
        = collect_photoshop_irb_u32_fields(store, 0x040CU,
                                           "ThumbnailCompressedBytes");
    const std::vector<uint32_t> data_bytes
        = collect_photoshop_irb_u32_fields(store, 0x040CU,
                                           "ThumbnailDataBytes");
    const std::vector<uint16_t> bits
        = collect_photoshop_irb_u16_fields(store, 0x040CU,
                                           "ThumbnailBitsPerPixel");
    const std::vector<uint16_t> planes
        = collect_photoshop_irb_u16_fields(store, 0x040CU, "ThumbnailPlanes");

    ASSERT_EQ(format.size(), 1U);
    ASSERT_EQ(width.size(), 1U);
    ASSERT_EQ(height.size(), 1U);
    ASSERT_EQ(compressed.size(), 1U);
    ASSERT_EQ(data_bytes.size(), 1U);
    ASSERT_EQ(bits.size(), 1U);
    ASSERT_EQ(planes.size(), 1U);
    EXPECT_EQ(format[0], 1U);
    EXPECT_EQ(width[0], 160U);
    EXPECT_EQ(height[0], 90U);
    EXPECT_EQ(compressed[0], 1234U);
    EXPECT_EQ(data_bytes[0], 3U);
    EXPECT_EQ(bits[0], 24U);
    EXPECT_EQ(planes[0], 1U);
}

TEST(PhotoshopIrbDecodeTest, DecodesDisplayInfoAndGridGuidesResources)
{
    std::vector<std::byte> display;
    append_u16be(1U, &display);
    append_u16be(100U, &display);
    append_u16be(200U, &display);
    append_u16be(300U, &display);
    append_u16be(400U, &display);
    append_u16be(85U, &display);
    display.push_back(std::byte { 2U });
    display.push_back(std::byte { 0U });

    std::vector<std::byte> grid_guides;
    append_u32be(1U, &grid_guides);
    append_u32be(576U, &grid_guides);
    append_u32be(720U, &grid_guides);
    append_u32be(2U, &grid_guides);
    append_u32be(1000U, &grid_guides);
    grid_guides.push_back(std::byte { 0U });
    append_u32be(2000U, &grid_guides);
    grid_guides.push_back(std::byte { 1U });

    std::vector<std::byte> irb;
    append_irb_resource(0x03EFU,
                        std::span<const std::byte>(display.data(),
                                                   display.size()),
                        &irb);
    append_irb_resource(0x0408U,
                        std::span<const std::byte>(grid_guides.data(),
                                                   grid_guides.size()),
                        &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 2U);

    const std::vector<uint32_t> display_count
        = collect_photoshop_irb_u32_fields(store, 0x03EFU, "DisplayInfoCount");
    const std::vector<uint16_t> display_space
        = collect_photoshop_irb_u16_fields(store, 0x03EFU, "DisplayColorSpace");
    const std::vector<uint16_t> display_data
        = collect_photoshop_irb_u16_fields(store, 0x03EFU, "DisplayColorData");
    const std::vector<uint16_t> opacity
        = collect_photoshop_irb_u16_fields(store, 0x03EFU, "DisplayOpacity");
    const std::vector<uint8_t> kind
        = collect_photoshop_irb_u8_fields(store, 0x03EFU, "DisplayKind");
    ASSERT_EQ(display_count.size(), 1U);
    ASSERT_EQ(display_space.size(), 1U);
    ASSERT_EQ(display_data.size(), 4U);
    ASSERT_EQ(opacity.size(), 1U);
    ASSERT_EQ(kind.size(), 1U);
    EXPECT_EQ(display_count[0], 1U);
    EXPECT_EQ(display_space[0], 1U);
    EXPECT_EQ(display_data[0], 100U);
    EXPECT_EQ(display_data[3], 400U);
    EXPECT_EQ(opacity[0], 85U);
    EXPECT_EQ(kind[0], 2U);

    const std::vector<uint32_t> guide_count
        = collect_photoshop_irb_u32_fields(store, 0x0408U, "GuideCount");
    const std::vector<uint32_t> grid_h
        = collect_photoshop_irb_u32_fields(store, 0x0408U,
                                           "GridHorizontalCycle");
    const std::vector<uint32_t> grid_v
        = collect_photoshop_irb_u32_fields(store, 0x0408U, "GridVerticalCycle");
    const std::vector<uint32_t> locations
        = collect_photoshop_irb_u32_fields(store, 0x0408U, "GuideLocation");
    const std::vector<uint8_t> directions
        = collect_photoshop_irb_u8_fields(store, 0x0408U, "GuideDirection");
    ASSERT_EQ(guide_count.size(), 1U);
    ASSERT_EQ(grid_h.size(), 1U);
    ASSERT_EQ(grid_v.size(), 1U);
    ASSERT_EQ(locations.size(), 2U);
    ASSERT_EQ(directions.size(), 2U);
    EXPECT_EQ(guide_count[0], 2U);
    EXPECT_EQ(grid_h[0], 576U);
    EXPECT_EQ(grid_v[0], 720U);
    EXPECT_EQ(locations[0], 1000U);
    EXPECT_EQ(locations[1], 2000U);
    EXPECT_EQ(directions[0], 0U);
    EXPECT_EQ(directions[1], 1U);
}

TEST(PhotoshopIrbDecodeTest, DecodesColorSamplerAndDescriptorResources)
{
    std::vector<std::byte> border;
    append_u32be(0x00018000U, &border);
    append_u16be(2U, &border);

    std::vector<std::byte> background;
    append_u16be(0U, &background);
    append_u16be(65535U, &background);
    append_u16be(32768U, &background);
    append_u16be(1024U, &background);
    append_u16be(0U, &background);

    std::vector<std::byte> effective_bw;
    effective_bw.push_back(std::byte { 12U });
    effective_bw.push_back(std::byte { 240U });

    std::vector<std::byte> color_samplers_v2;
    append_u32be(2U, &color_samplers_v2);
    append_u32be(1U, &color_samplers_v2);
    append_u32be(f32_bits(12.5f), &color_samplers_v2);
    append_u32be(f32_bits(34.25f), &color_samplers_v2);
    append_u16be(0U, &color_samplers_v2);
    append_u16be(16U, &color_samplers_v2);

    std::vector<std::byte> color_samplers_v3;
    append_u32be(3U, &color_samplers_v3);
    append_u32be(1U, &color_samplers_v3);
    append_u32be(1U, &color_samplers_v3);
    append_u32be(111U, &color_samplers_v3);
    append_u32be(222U, &color_samplers_v3);
    append_u16be(18U, &color_samplers_v3);
    append_u16be(32U, &color_samplers_v3);

    std::vector<std::byte> layer_comps;
    append_u32be(16U, &layer_comps);
    append_utf16be_string32("LayerComp", &layer_comps);
    append_u32be(4U, &layer_comps);
    layer_comps.push_back(std::byte { 'L' });
    layer_comps.push_back(std::byte { 'y' });
    layer_comps.push_back(std::byte { 'r' });
    layer_comps.push_back(std::byte { 'C' });
    append_u32be(5U, &layer_comps);
    append_descriptor_key4('e', 'n', 'a', 'b', &layer_comps);
    append_descriptor_type('b', 'o', 'o', 'l', &layer_comps);
    layer_comps.push_back(std::byte { 1U });
    append_descriptor_key4('i', 'n', 'd', 'x', &layer_comps);
    append_descriptor_type('l', 'o', 'n', 'g', &layer_comps);
    append_u32be(42U, &layer_comps);
    append_descriptor_key4('m', 'o', 'd', 'e', &layer_comps);
    append_descriptor_type('e', 'n', 'u', 'm', &layer_comps);
    append_descriptor_key4('M', 'o', 'd', 'e', &layer_comps);
    append_descriptor_key4('N', 'r', 'm', 'l', &layer_comps);
    append_descriptor_key4('l', 'i', 's', 't', &layer_comps);
    append_descriptor_type('V', 'l', 'L', 's', &layer_comps);
    append_u32be(0U, &layer_comps);
    append_descriptor_key4('o', 'b', 'j', '0', &layer_comps);
    append_descriptor_type('O', 'b', 'j', 'c', &layer_comps);
    append_utf16be_string32("SubObj", &layer_comps);
    append_descriptor_key4('S', 'u', 'b', 'O', &layer_comps);
    append_u32be(0U, &layer_comps);

    std::vector<std::byte> measurement_scale;
    append_u32be(16U, &measurement_scale);
    measurement_scale.push_back(std::byte { 1U });
    measurement_scale.push_back(std::byte { 2U });

    std::vector<std::byte> timeline_info;
    append_u32be(16U, &timeline_info);
    timeline_info.push_back(std::byte { 7U });

    std::vector<std::byte> sheet_disclosure;
    append_u32be(16U, &sheet_disclosure);
    sheet_disclosure.push_back(std::byte { 8U });
    sheet_disclosure.push_back(std::byte { 9U });

    std::vector<std::byte> path_selection;
    append_u32be(16U, &path_selection);
    path_selection.push_back(std::byte { 3U });

    std::vector<std::byte> hdr_toning;
    append_u32be(16U, &hdr_toning);
    hdr_toning.push_back(std::byte { 4U });

    std::vector<std::byte> print_info;
    append_u32be(17U, &print_info);
    print_info.push_back(std::byte { 5U });
    print_info.push_back(std::byte { 6U });

    std::vector<std::byte> onion_skins;
    append_u32be(16U, &onion_skins);
    onion_skins.push_back(std::byte { 10U });
    onion_skins.push_back(std::byte { 11U });
    onion_skins.push_back(std::byte { 12U });

    std::vector<std::byte> count_info;
    append_u32be(16U, &count_info);
    count_info.push_back(std::byte { 13U });
    count_info.push_back(std::byte { 14U });

    std::vector<std::byte> print_info2;
    append_u32be(16U, &print_info2);
    print_info2.push_back(std::byte { 15U });

    std::vector<std::byte> print_style;
    append_u32be(16U, &print_style);
    print_style.push_back(std::byte { 16U });
    print_style.push_back(std::byte { 17U });

    std::vector<std::byte> origin_path;
    append_u32be(17U, &origin_path);
    origin_path.push_back(std::byte { 18U });
    origin_path.push_back(std::byte { 19U });
    origin_path.push_back(std::byte { 20U });

    std::vector<std::byte> irb;
    append_irb_resource(0x03F1U, border, &irb);
    append_irb_resource(0x03F2U, background, &irb);
    append_irb_resource(0x03FBU, effective_bw, &irb);
    append_irb_resource(0x040EU, color_samplers_v2, &irb);
    append_irb_resource(0x0431U, color_samplers_v3, &irb);
    append_irb_resource(0x0429U, layer_comps, &irb);
    append_irb_resource(0x0432U, measurement_scale, &irb);
    append_irb_resource(0x0433U, timeline_info, &irb);
    append_irb_resource(0x0434U, sheet_disclosure, &irb);
    append_irb_resource(0x0440U, path_selection, &irb);
    append_irb_resource(0x042EU, hdr_toning, &irb);
    append_irb_resource(0x042FU, print_info, &irb);
    append_irb_resource(0x0436U, onion_skins, &irb);
    append_irb_resource(0x0438U, count_info, &irb);
    append_irb_resource(0x043AU, print_info2, &irb);
    append_irb_resource(0x043BU, print_style, &irb);
    append_irb_resource(0x0BB8U, origin_path, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 17U);

    const Entry* border_width = find_photoshop_irb_field(store, 0x03F1U,
                                                         "BorderWidth");
    ASSERT_NE(border_width, nullptr);
    EXPECT_EQ(border_width->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(border_width->value.elem_type, MetaElementType::F64);
    EXPECT_DOUBLE_EQ(std::bit_cast<double>(border_width->value.data.f64_bits),
                     1.5);

    const std::vector<uint16_t> border_units
        = collect_photoshop_irb_u16_fields(store, 0x03F1U, "BorderUnits");
    ASSERT_EQ(border_units.size(), 1U);
    EXPECT_EQ(border_units[0], 2U);

    const std::vector<uint16_t> background_space
        = collect_photoshop_irb_u16_fields(store, 0x03F2U,
                                           "BackgroundColorSpace");
    const std::vector<uint16_t> background_data
        = collect_photoshop_irb_u16_fields(store, 0x03F2U,
                                           "BackgroundColorData");
    ASSERT_EQ(background_space.size(), 1U);
    ASSERT_EQ(background_data.size(), 4U);
    EXPECT_EQ(background_space[0], 0U);
    EXPECT_EQ(background_data[0], 65535U);
    EXPECT_EQ(background_data[1], 32768U);
    EXPECT_EQ(background_data[2], 1024U);
    EXPECT_EQ(background_data[3], 0U);

    const std::vector<uint8_t> effective_black
        = collect_photoshop_irb_u8_fields(store, 0x03FBU, "EffectiveBlack");
    const std::vector<uint8_t> effective_white
        = collect_photoshop_irb_u8_fields(store, 0x03FBU, "EffectiveWhite");
    ASSERT_EQ(effective_black.size(), 1U);
    ASSERT_EQ(effective_white.size(), 1U);
    EXPECT_EQ(effective_black[0], 12U);
    EXPECT_EQ(effective_white[0], 240U);

    const std::vector<uint32_t> sampler_v2_version
        = collect_photoshop_irb_u32_fields(store, 0x040EU,
                                           "ColorSamplerVersion");
    const std::vector<uint32_t> sampler_v2_horizontal
        = collect_photoshop_irb_u32_fields(store, 0x040EU,
                                           "ColorSamplerHorizontalRaw");
    const std::vector<uint32_t> sampler_v2_vertical
        = collect_photoshop_irb_u32_fields(store, 0x040EU,
                                           "ColorSamplerVerticalRaw");
    const std::vector<uint16_t> sampler_v2_depth
        = collect_photoshop_irb_u16_fields(store, 0x040EU, "ColorSamplerDepth");
    ASSERT_EQ(sampler_v2_version.size(), 1U);
    ASSERT_EQ(sampler_v2_horizontal.size(), 1U);
    ASSERT_EQ(sampler_v2_vertical.size(), 1U);
    ASSERT_EQ(sampler_v2_depth.size(), 1U);
    EXPECT_EQ(sampler_v2_version[0], 2U);
    EXPECT_EQ(sampler_v2_horizontal[0], f32_bits(12.5f));
    EXPECT_EQ(sampler_v2_vertical[0], f32_bits(34.25f));
    EXPECT_EQ(sampler_v2_depth[0], 16U);

    const std::vector<uint32_t> sampler_v3_record_version
        = collect_photoshop_irb_u32_fields(store, 0x0431U,
                                           "ColorSamplerRecordVersion");
    const std::vector<uint32_t> sampler_v3_horizontal
        = collect_photoshop_irb_u32_fields(store, 0x0431U,
                                           "ColorSamplerHorizontalRaw");
    const std::vector<uint16_t> sampler_v3_color_space
        = collect_photoshop_irb_u16_fields(store, 0x0431U,
                                           "ColorSamplerColorSpace");
    ASSERT_EQ(sampler_v3_record_version.size(), 1U);
    ASSERT_EQ(sampler_v3_horizontal.size(), 1U);
    ASSERT_EQ(sampler_v3_color_space.size(), 1U);
    EXPECT_EQ(sampler_v3_record_version[0], 1U);
    EXPECT_EQ(sampler_v3_horizontal[0], 111U);
    EXPECT_EQ(sampler_v3_color_space[0], 18U);

    const std::vector<uint32_t> layer_comp_version
        = collect_photoshop_irb_u32_fields(store, 0x0429U, "DescriptorVersion");
    const std::vector<uint32_t> layer_comp_bytes
        = collect_photoshop_irb_u32_fields(store, 0x0429U, "DescriptorBytes");
    const std::vector<std::string_view> layer_comp_class_name
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorClassName");
    const std::vector<std::string_view> layer_comp_class_id
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorClassID");
    const std::vector<uint32_t> layer_comp_item_count
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorItemCount");
    const std::vector<uint32_t> layer_comp_parsed_item_count
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorParsedItemCount");
    const std::vector<std::string_view> layer_comp_item_keys
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorItemKey");
    const std::vector<std::string_view> layer_comp_item_type_names
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorItemTypeName");
    const std::vector<uint8_t> layer_comp_boolean
        = collect_photoshop_irb_u8_fields(store, 0x0429U,
                                          "DescriptorItemBoolean");
    const std::vector<int32_t> layer_comp_integer
        = collect_photoshop_irb_i32_fields(store, 0x0429U,
                                           "DescriptorItemInteger");
    const std::vector<std::string_view> layer_comp_enum_type
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorItemEnumType");
    const std::vector<std::string_view> layer_comp_enum_value
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorItemEnumValue");
    const std::vector<uint32_t> layer_comp_list_count
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorItemListCount");
    const std::vector<std::string_view> layer_comp_object_class_name
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorItemObjectClassName");
    const std::vector<std::string_view> layer_comp_object_class_id
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorItemObjectClassID");
    const std::vector<uint32_t> layer_comp_object_item_count
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorItemObjectItemCount");
    const std::vector<uint8_t> layer_comp_truncated
        = collect_photoshop_irb_u8_fields(store, 0x0429U,
                                          "DescriptorItemParseTruncated");
    const std::vector<uint32_t> measurement_bytes
        = collect_photoshop_irb_u32_fields(store, 0x0432U, "DescriptorBytes");
    const std::vector<uint32_t> timeline_bytes
        = collect_photoshop_irb_u32_fields(store, 0x0433U, "DescriptorBytes");
    const std::vector<uint32_t> sheet_bytes
        = collect_photoshop_irb_u32_fields(store, 0x0434U, "DescriptorBytes");
    const std::vector<uint32_t> path_bytes
        = collect_photoshop_irb_u32_fields(store, 0x0440U, "DescriptorBytes");
    const std::vector<uint32_t> hdr_toning_version
        = collect_photoshop_irb_u32_fields(store, 0x042EU, "DescriptorVersion");
    const std::vector<uint32_t> print_info_bytes
        = collect_photoshop_irb_u32_fields(store, 0x042FU, "DescriptorBytes");
    const std::vector<uint32_t> onion_bytes
        = collect_photoshop_irb_u32_fields(store, 0x0436U, "DescriptorBytes");
    const std::vector<uint32_t> count_bytes
        = collect_photoshop_irb_u32_fields(store, 0x0438U, "DescriptorBytes");
    const std::vector<uint32_t> print_info2_bytes
        = collect_photoshop_irb_u32_fields(store, 0x043AU, "DescriptorBytes");
    const std::vector<uint32_t> print_style_bytes
        = collect_photoshop_irb_u32_fields(store, 0x043BU, "DescriptorBytes");
    const std::vector<uint32_t> origin_path_version
        = collect_photoshop_irb_u32_fields(store, 0x0BB8U, "DescriptorVersion");
    ASSERT_EQ(layer_comp_version.size(), 1U);
    ASSERT_EQ(layer_comp_bytes.size(), 1U);
    ASSERT_EQ(layer_comp_class_name.size(), 1U);
    ASSERT_EQ(layer_comp_class_id.size(), 1U);
    ASSERT_EQ(layer_comp_item_count.size(), 1U);
    ASSERT_EQ(layer_comp_parsed_item_count.size(), 1U);
    ASSERT_EQ(layer_comp_item_keys.size(), 5U);
    ASSERT_EQ(layer_comp_item_type_names.size(), 5U);
    ASSERT_EQ(layer_comp_boolean.size(), 1U);
    ASSERT_EQ(layer_comp_integer.size(), 1U);
    ASSERT_EQ(layer_comp_enum_type.size(), 1U);
    ASSERT_EQ(layer_comp_enum_value.size(), 1U);
    ASSERT_EQ(layer_comp_list_count.size(), 1U);
    ASSERT_EQ(layer_comp_object_class_name.size(), 1U);
    ASSERT_EQ(layer_comp_object_class_id.size(), 1U);
    ASSERT_EQ(layer_comp_object_item_count.size(), 1U);
    EXPECT_TRUE(layer_comp_truncated.empty());
    ASSERT_EQ(measurement_bytes.size(), 1U);
    ASSERT_EQ(timeline_bytes.size(), 1U);
    ASSERT_EQ(sheet_bytes.size(), 1U);
    ASSERT_EQ(path_bytes.size(), 1U);
    ASSERT_EQ(hdr_toning_version.size(), 1U);
    ASSERT_EQ(print_info_bytes.size(), 1U);
    ASSERT_EQ(onion_bytes.size(), 1U);
    ASSERT_EQ(count_bytes.size(), 1U);
    ASSERT_EQ(print_info2_bytes.size(), 1U);
    ASSERT_EQ(print_style_bytes.size(), 1U);
    ASSERT_EQ(origin_path_version.size(), 1U);
    EXPECT_EQ(layer_comp_version[0], 16U);
    EXPECT_EQ(layer_comp_bytes[0], 147U);
    EXPECT_EQ(layer_comp_class_name[0], "LayerComp");
    EXPECT_EQ(layer_comp_class_id[0], "LyrC");
    EXPECT_EQ(layer_comp_item_count[0], 5U);
    EXPECT_EQ(layer_comp_parsed_item_count[0], 5U);
    EXPECT_EQ(layer_comp_item_keys[0], "enab");
    EXPECT_EQ(layer_comp_item_keys[1], "indx");
    EXPECT_EQ(layer_comp_item_keys[2], "mode");
    EXPECT_EQ(layer_comp_item_keys[3], "list");
    EXPECT_EQ(layer_comp_item_keys[4], "obj0");
    EXPECT_EQ(layer_comp_item_type_names[0], "boolean");
    EXPECT_EQ(layer_comp_item_type_names[1], "integer");
    EXPECT_EQ(layer_comp_item_type_names[2], "enum");
    EXPECT_EQ(layer_comp_item_type_names[3], "list");
    EXPECT_EQ(layer_comp_item_type_names[4], "object");
    EXPECT_EQ(layer_comp_boolean[0], 1U);
    EXPECT_EQ(layer_comp_integer[0], 42);
    EXPECT_EQ(layer_comp_enum_type[0], "Mode");
    EXPECT_EQ(layer_comp_enum_value[0], "Nrml");
    EXPECT_EQ(layer_comp_list_count[0], 0U);
    EXPECT_EQ(layer_comp_object_class_name[0], "SubObj");
    EXPECT_EQ(layer_comp_object_class_id[0], "SubO");
    EXPECT_EQ(layer_comp_object_item_count[0], 0U);
    EXPECT_EQ(measurement_bytes[0], 2U);
    EXPECT_EQ(timeline_bytes[0], 1U);
    EXPECT_EQ(sheet_bytes[0], 2U);
    EXPECT_EQ(path_bytes[0], 1U);
    EXPECT_EQ(hdr_toning_version[0], 16U);
    EXPECT_EQ(print_info_bytes[0], 2U);
    EXPECT_EQ(onion_bytes[0], 3U);
    EXPECT_EQ(count_bytes[0], 2U);
    EXPECT_EQ(print_info2_bytes[0], 1U);
    EXPECT_EQ(print_style_bytes[0], 2U);
    EXPECT_EQ(origin_path_version[0], 17U);
}

TEST(PhotoshopIrbDecodeTest, DecodesNestedDescriptorItems)
{
    std::vector<std::byte> descriptor;
    append_u32be(16U, &descriptor);
    append_utf16be_string32("RootDescriptor", &descriptor);
    append_descriptor_key4('R', 'o', 'o', 't', &descriptor);
    append_u32be(1U, &descriptor);

    append_descriptor_key4('n', 'e', 's', 't', &descriptor);
    append_descriptor_type('O', 'b', 'j', 'c', &descriptor);
    append_utf16be_string32("Nested", &descriptor);
    append_descriptor_key4('N', 's', 't', 'D', &descriptor);
    append_u32be(2U, &descriptor);

    append_descriptor_key4('f', 'l', 'a', 'g', &descriptor);
    append_descriptor_type('b', 'o', 'o', 'l', &descriptor);
    descriptor.push_back(std::byte { 1U });

    append_descriptor_key4('n', 'u', 'm', 's', &descriptor);
    append_descriptor_type('V', 'l', 'L', 's', &descriptor);
    append_u32be(2U, &descriptor);
    append_descriptor_type('l', 'o', 'n', 'g', &descriptor);
    append_u32be(7U, &descriptor);
    append_descriptor_type('T', 'E', 'X', 'T', &descriptor);
    append_utf16be_string32("hello", &descriptor);

    std::vector<std::byte> irb;
    append_irb_resource(0x0429U, descriptor, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 1U);

    const std::vector<uint32_t> item_count
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorItemCount");
    const std::vector<uint32_t> parsed_item_count
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorParsedItemCount");
    const std::vector<uint32_t> parsed_value_count
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorParsedValueCount");
    const std::vector<std::string_view> keys
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorItemKey");
    const std::vector<std::string_view> paths
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorItemPath");
    const std::vector<uint32_t> depths
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorItemDepth");
    const std::vector<uint32_t> list_indices
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorItemListIndex");
    const std::vector<std::string_view> type_names
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorItemTypeName");
    const std::vector<uint8_t> bool_values
        = collect_photoshop_irb_u8_fields(store, 0x0429U,
                                          "DescriptorItemBoolean");
    const std::vector<int32_t> int_values
        = collect_photoshop_irb_i32_fields(store, 0x0429U,
                                           "DescriptorItemInteger");
    const std::vector<std::string_view> text_values
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorItemText");
    const std::vector<uint32_t> object_item_counts
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorItemObjectItemCount");
    const std::vector<uint32_t> list_counts
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorItemListCount");
    const std::vector<uint32_t> raw_data_bytes
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorItemRawDataBytes");
    const std::vector<uint8_t> truncated
        = collect_photoshop_irb_u8_fields(store, 0x0429U,
                                          "DescriptorItemParseTruncated");

    ASSERT_EQ(item_count.size(), 1U);
    ASSERT_EQ(parsed_item_count.size(), 1U);
    ASSERT_EQ(parsed_value_count.size(), 1U);
    ASSERT_EQ(keys.size(), 3U);
    ASSERT_EQ(paths.size(), 5U);
    ASSERT_EQ(depths.size(), 5U);
    ASSERT_EQ(list_indices.size(), 2U);
    ASSERT_EQ(type_names.size(), 5U);
    ASSERT_EQ(bool_values.size(), 1U);
    ASSERT_EQ(int_values.size(), 1U);
    ASSERT_EQ(text_values.size(), 1U);
    ASSERT_EQ(object_item_counts.size(), 1U);
    ASSERT_EQ(list_counts.size(), 1U);
    EXPECT_TRUE(raw_data_bytes.empty());
    EXPECT_TRUE(truncated.empty());

    EXPECT_EQ(item_count[0], 1U);
    EXPECT_EQ(parsed_item_count[0], 1U);
    EXPECT_EQ(parsed_value_count[0], 5U);
    EXPECT_EQ(keys[0], "nest");
    EXPECT_EQ(keys[1], "flag");
    EXPECT_EQ(keys[2], "nums");
    EXPECT_EQ(paths[0], "nest");
    EXPECT_EQ(paths[1], "nest.flag");
    EXPECT_EQ(paths[2], "nest.nums");
    EXPECT_EQ(paths[3], "nest.nums[0]");
    EXPECT_EQ(paths[4], "nest.nums[1]");
    EXPECT_EQ(depths[0], 0U);
    EXPECT_EQ(depths[1], 1U);
    EXPECT_EQ(depths[2], 1U);
    EXPECT_EQ(depths[3], 2U);
    EXPECT_EQ(depths[4], 2U);
    EXPECT_EQ(list_indices[0], 0U);
    EXPECT_EQ(list_indices[1], 1U);
    EXPECT_EQ(type_names[0], "object");
    EXPECT_EQ(type_names[1], "boolean");
    EXPECT_EQ(type_names[2], "list");
    EXPECT_EQ(type_names[3], "integer");
    EXPECT_EQ(type_names[4], "text");
    EXPECT_EQ(bool_values[0], 1U);
    EXPECT_EQ(int_values[0], 7);
    EXPECT_EQ(text_values[0], "hello");
    EXPECT_EQ(object_item_counts[0], 2U);
    EXPECT_EQ(list_counts[0], 2U);
}

TEST(PhotoshopIrbDecodeTest, SummarizesDescriptorRawDataItems)
{
    std::vector<std::byte> descriptor;
    append_u32be(16U, &descriptor);
    append_utf16be_string32("RawDataDescriptor", &descriptor);
    append_descriptor_key4('R', 'a', 'w', 'D', &descriptor);
    append_u32be(1U, &descriptor);

    append_descriptor_key4('d', 'a', 't', 'a', &descriptor);
    append_descriptor_type('t', 'd', 't', 'a', &descriptor);
    append_u32be(3U, &descriptor);
    descriptor.push_back(std::byte { 0xAAU });
    descriptor.push_back(std::byte { 0xBBU });
    descriptor.push_back(std::byte { 0xCCU });

    std::vector<std::byte> irb;
    append_irb_resource(0x0429U, descriptor, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 1U);

    const std::vector<uint32_t> item_count
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorItemCount");
    const std::vector<uint32_t> parsed_item_count
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorParsedItemCount");
    const std::vector<uint32_t> parsed_value_count
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorParsedValueCount");
    const std::vector<std::string_view> item_type_names
        = collect_photoshop_irb_text_fields(store, 0x0429U,
                                            "DescriptorItemTypeName");
    const std::vector<uint32_t> raw_data_bytes
        = collect_photoshop_irb_u32_fields(store, 0x0429U,
                                           "DescriptorItemRawDataBytes");
    const std::vector<uint8_t> truncated
        = collect_photoshop_irb_u8_fields(store, 0x0429U,
                                          "DescriptorItemParseTruncated");

    ASSERT_EQ(item_count.size(), 1U);
    ASSERT_EQ(parsed_item_count.size(), 1U);
    ASSERT_EQ(parsed_value_count.size(), 1U);
    ASSERT_EQ(item_type_names.size(), 1U);
    ASSERT_EQ(raw_data_bytes.size(), 1U);
    EXPECT_TRUE(truncated.empty());
    EXPECT_EQ(item_count[0], 1U);
    EXPECT_EQ(parsed_item_count[0], 1U);
    EXPECT_EQ(parsed_value_count[0], 1U);
    EXPECT_EQ(item_type_names[0], "raw_data");
    EXPECT_EQ(raw_data_bytes[0], 3U);
}

TEST(PhotoshopIrbDecodeTest, DecodesLegacyFixedHeaderAndPathResources)
{
    std::vector<std::byte> photoshop2_info;
    append_u16be(3U, &photoshop2_info);
    append_u16be(200U, &photoshop2_info);
    append_u16be(300U, &photoshop2_info);
    append_u16be(8U, &photoshop2_info);
    append_u16be(3U, &photoshop2_info);

    const std::array<std::byte, 6> photoshop2_color_table = {
        std::byte { 0U },   std::byte { 0U },   std::byte { 0U },
        std::byte { 255U }, std::byte { 255U }, std::byte { 255U },
    };

    std::vector<std::byte> raw_image_mode;
    append_u16be(3U, &raw_image_mode);

    std::vector<std::byte> spot_halftone;
    append_u32be(1U, &spot_halftone);
    append_u32be(4U, &spot_halftone);
    append_u32be(0x01020304U, &spot_halftone);

    std::vector<std::byte> jump_to_xpep;
    append_u16be(2U, &jump_to_xpep);
    append_u16be(1U, &jump_to_xpep);
    append_u32be(7U, &jump_to_xpep);

    std::vector<std::byte> auto_save_path;
    append_utf16be_string32("autosave.psd", &auto_save_path);

    std::vector<std::byte> auto_save_format;
    append_utf16be_string32("PSD", &auto_save_format);

    const std::array<std::byte, 7> image_ready_variables = {
        std::byte { '<' }, std::byte { 'v' }, std::byte { 'a' },
        std::byte { 'r' }, std::byte { 's' }, std::byte { '/' },
        std::byte { '>' },
    };

    const std::array<std::byte, 7> image_ready_data_sets = {
        std::byte { '<' }, std::byte { 's' }, std::byte { 'e' },
        std::byte { 't' }, std::byte { 's' }, std::byte { '/' },
        std::byte { '>' },
    };

    const std::array<std::byte, 6> xml_data = {
        std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' },
        std::byte { 'l' }, std::byte { '/' }, std::byte { '>' },
    };

    const std::array<std::byte, 5> iptc_data = {
        std::byte { 0x1c }, std::byte { 0x02 }, std::byte { 0x19 },
        std::byte { 0x00 }, std::byte { 0x00 },
    };

    const std::array<std::byte, 11> lightroom_workflow = {
        std::byte { 'l' }, std::byte { 'r' }, std::byte { ':' },
        std::byte { 'w' }, std::byte { 'o' }, std::byte { 'r' },
        std::byte { 'k' }, std::byte { 'f' }, std::byte { 'l' },
        std::byte { 'o' }, std::byte { 'w' },
    };

    std::vector<std::byte> working_path(26U, std::byte { 0x00 });
    working_path[1] = std::byte { 0x01 };

    std::vector<std::byte> halftone;
    append_u16be(0x1234U, &halftone);

    std::vector<std::byte> transfer_function(28U, std::byte { 0x00 });
    transfer_function[0] = std::byte { 0x12 };
    transfer_function[1] = std::byte { 0x34 };

    const std::array<std::byte, 4> duotone_info = {
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
        std::byte { 0x04 },
    };
    const std::array<std::byte, 2> eps_options = {
        std::byte { 0xAA },
        std::byte { 0x55 },
    };

    std::vector<std::byte> irb;
    append_irb_resource(0x03E8U, photoshop2_info, &irb);
    append_irb_resource(0x03EBU, photoshop2_color_table, &irb);
    append_irb_resource(0x0405U, raw_image_mode, &irb);
    append_irb_resource(0x0413U, spot_halftone, &irb);
    append_irb_resource(0x041CU, jump_to_xpep, &irb);
    append_irb_resource(0x043EU, auto_save_path, &irb);
    append_irb_resource(0x043FU, auto_save_format, &irb);
    append_irb_resource(0x1B58U, image_ready_variables, &irb);
    append_irb_resource(0x1B59U, image_ready_data_sets, &irb);
    append_irb_resource(0x03EAU, xml_data, &irb);
    append_irb_resource(0x0404U, iptc_data, &irb);
    append_irb_resource(0x1F40U, lightroom_workflow, &irb);
    append_irb_resource(0x0401U, working_path, &irb);
    append_irb_resource(0x03F4U, halftone, &irb);
    append_irb_resource(0x03F7U, transfer_function, &irb);
    append_irb_resource(0x03FAU, duotone_info, &irb);
    append_irb_resource(0x03FDU, eps_options, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 17U);
    EXPECT_EQ(r.entries_decoded, 47U);

    const Entry* channels = find_photoshop_irb_field(store, 0x03E8U,
                                                     "Photoshop2ChannelCount");
    ASSERT_NE(channels, nullptr);
    EXPECT_EQ(channels->value.elem_type, MetaElementType::U16);
    EXPECT_EQ(channels->value.data.u64, 3U);

    const Entry* rows = find_photoshop_irb_field(store, 0x03E8U,
                                                 "Photoshop2Rows");
    ASSERT_NE(rows, nullptr);
    EXPECT_EQ(rows->value.data.u64, 200U);

    const Entry* color_bytes
        = find_photoshop_irb_field(store, 0x03EBU, "Photoshop2ColorTableBytes");
    ASSERT_NE(color_bytes, nullptr);
    EXPECT_EQ(color_bytes->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(color_bytes->value.data.u64, 6U);

    const Entry* color_count = find_photoshop_irb_field(store, 0x03EBU,
                                                        "Photoshop2ColorCount");
    ASSERT_NE(color_count, nullptr);
    EXPECT_EQ(color_count->value.data.u64, 2U);

    const Entry* raw_mode = find_photoshop_irb_field(store, 0x0405U,
                                                     "RawImageMode");
    ASSERT_NE(raw_mode, nullptr);
    EXPECT_EQ(raw_mode->value.data.u64, 3U);

    const Entry* spot_version = find_photoshop_irb_field(store, 0x0413U,
                                                         "SpotHalftoneVersion");
    ASSERT_NE(spot_version, nullptr);
    EXPECT_EQ(spot_version->value.data.u64, 1U);

    const Entry* jump_count = find_photoshop_irb_field(store, 0x041CU,
                                                       "JumpToXPEPJumpCount");
    ASSERT_NE(jump_count, nullptr);
    EXPECT_EQ(jump_count->value.data.u64, 7U);

    const std::vector<std::string_view> save_paths
        = collect_photoshop_irb_text_fields(store, 0x043EU, "AutoSaveFilePath");
    ASSERT_EQ(save_paths.size(), 1U);
    EXPECT_EQ(save_paths[0], "autosave.psd");

    const std::vector<std::string_view> save_formats
        = collect_photoshop_irb_text_fields(store, 0x043FU, "AutoSaveFormat");
    ASSERT_EQ(save_formats.size(), 1U);
    EXPECT_EQ(save_formats[0], "PSD");

    const std::vector<std::string_view> variables
        = collect_photoshop_irb_text_fields(store, 0x1B58U,
                                            "ImageReadyVariables");
    const std::vector<std::string_view> data_sets
        = collect_photoshop_irb_text_fields(store, 0x1B59U,
                                            "ImageReadyDataSets");
    ASSERT_EQ(variables.size(), 1U);
    ASSERT_EQ(data_sets.size(), 1U);
    EXPECT_EQ(variables[0], "<vars/>");
    EXPECT_EQ(data_sets[0], "<sets/>");
    const std::vector<std::string_view> xml_values
        = collect_photoshop_irb_text_fields(store, 0x03EAU, "XMLData");
    ASSERT_EQ(xml_values.size(), 1U);
    EXPECT_EQ(xml_values[0], "<xml/>");
    const Entry* iptc_bytes = find_photoshop_irb_field(store, 0x0404U,
                                                       "IPTCDataBytes");
    ASSERT_NE(iptc_bytes, nullptr);
    EXPECT_EQ(iptc_bytes->value.data.u64, 5U);
    const std::vector<std::string_view> lightroom_values
        = collect_photoshop_irb_text_fields(store, 0x1F40U,
                                            "LightroomWorkflow");
    ASSERT_EQ(lightroom_values.size(), 1U);
    EXPECT_EQ(lightroom_values[0], "lr:workflow");

    const Entry* path_count = find_photoshop_irb_field(store, 0x0401U,
                                                       "PathRecordCount");
    ASSERT_NE(path_count, nullptr);
    EXPECT_EQ(path_count->value.data.u64, 1U);
    const std::vector<uint16_t> selectors
        = collect_photoshop_irb_u16_fields(store, 0x0401U,
                                           "PathRecordSelector");
    ASSERT_EQ(selectors.size(), 1U);
    EXPECT_EQ(selectors[0], 1U);

    const Entry* halftone_word
        = find_photoshop_irb_field(store, 0x03F4U, "HalftoneInfoFirstWord");
    ASSERT_NE(halftone_word, nullptr);
    EXPECT_EQ(halftone_word->value.data.u64, 0x1234U);

    const Entry* transfer_curves
        = find_photoshop_irb_field(store, 0x03F7U,
                                   "TransferFunctionCurveCount");
    ASSERT_NE(transfer_curves, nullptr);
    EXPECT_EQ(transfer_curves->value.data.u64, 1U);

    const Entry* duotone_bytes
        = find_photoshop_irb_field(store, 0x03FAU, "DuotoneImageInfoBytes");
    ASSERT_NE(duotone_bytes, nullptr);
    EXPECT_EQ(duotone_bytes->value.data.u64, 4U);

    const Entry* eps_bytes = find_photoshop_irb_field(store, 0x03FDU,
                                                      "EPSOptionsBytes");
    ASSERT_NE(eps_bytes, nullptr);
    EXPECT_EQ(eps_bytes->value.data.u64, 2U);
}

TEST(PhotoshopIrbDecodeTest, AvoidsStructuredFieldsForShortAdditionalResources)
{
    const std::array<std::byte, 8> short_photoshop2_info = {};
    const std::array<std::byte, 1> short_raw_image_mode  = {};
    const std::array<std::byte, 7> short_spot_halftone   = {};
    const std::array<std::byte, 7> short_jump_to_xpep    = {};
    const std::array<std::byte, 3> short_unicode_text    = {};
    const std::array<std::byte, 3> non_ascii_xml         = {
        std::byte { '<' },
        std::byte { 0x80 },
        std::byte { '>' },
    };
    const std::array<std::byte, 0> empty_xml      = {};
    const std::array<std::byte, 25> short_path    = {};
    const std::array<std::byte, 1> short_halftone = {
        std::byte { 0x12 },
    };
    const std::array<std::byte, 1> short_transfer = {
        std::byte { 0x12 },
    };

    std::vector<std::byte> irb;
    append_irb_resource(0x03E8U, short_photoshop2_info, &irb);
    append_irb_resource(0x0405U, short_raw_image_mode, &irb);
    append_irb_resource(0x0413U, short_spot_halftone, &irb);
    append_irb_resource(0x041CU, short_jump_to_xpep, &irb);
    append_irb_resource(0x043EU, short_unicode_text, &irb);
    append_irb_resource(0x043FU, short_unicode_text, &irb);
    append_irb_resource(0x1B58U, non_ascii_xml, &irb);
    append_irb_resource(0x1B59U, empty_xml, &irb);
    append_irb_resource(0x03EAU, non_ascii_xml, &irb);
    append_irb_resource(0x0401U, short_path, &irb);
    append_irb_resource(0x03F4U, short_halftone, &irb);
    append_irb_resource(0x03F7U, short_transfer, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 12U);

    EXPECT_EQ(find_photoshop_irb_field(store, 0x03E8U, "Photoshop2ChannelCount"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0405U, "RawImageMode"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0413U, "SpotHalftoneVersion"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x041CU, "JumpToXPEPJumpCount"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x043EU, "AutoSaveFilePath"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x043FU, "AutoSaveFormat"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x1B58U, "ImageReadyVariables"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x1B59U, "ImageReadyDataSets"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03EAU, "XMLData"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0401U, "PathRecordCount"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03F4U, "HalftoneInfoFirstWord"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03F7U,
                                       "TransferFunctionFirstWord"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03F7U,
                                       "TransferFunctionCurveCount"),
              nullptr);
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

    std::vector<std::byte> alpha_names;
    const std::array<std::byte, 4> alpha_name_a = {
        std::byte { 'M' },
        std::byte { 'a' },
        std::byte { 's' },
        std::byte { 'k' },
    };
    const std::array<std::byte, 4> alpha_name_b = {
        std::byte { 'S' },
        std::byte { 'o' },
        std::byte { 'f' },
        std::byte { 't' },
    };
    append_pascal_string(alpha_name_a, &alpha_names);
    alpha_names.push_back(std::byte { 0x00 });
    append_pascal_string(alpha_name_b, &alpha_names);
    alpha_names.push_back(std::byte { 0x00 });
    append_irb_resource(0x03EEU, alpha_names, &irb);

    std::vector<std::byte> caption;
    const std::array<std::byte, 7> caption_text = {
        std::byte { 'C' }, std::byte { 'a' }, std::byte { 'p' },
        std::byte { 't' }, std::byte { 'i' }, std::byte { 'o' },
        std::byte { 'n' },
    };
    append_pascal_string(caption_text, &caption);
    append_irb_resource(0x03F0U, caption, &irb);

    std::vector<std::byte> quick_mask;
    append_u16be(5U, &quick_mask);
    quick_mask.push_back(std::byte { 0x01 });
    append_irb_resource(0x03FEU, quick_mask, &irb);

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

    std::vector<std::byte> unicode_alpha_names;
    append_utf16be_string32("Matte", &unicode_alpha_names);
    append_utf16be_string32("Depth", &unicode_alpha_names);
    append_irb_resource(0x0415U, unicode_alpha_names, &irb);

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

    std::vector<std::byte> alpha_identifiers;
    append_u32be(101U, &alpha_identifiers);
    append_u32be(202U, &alpha_identifiers);
    append_irb_resource(0x041DU, alpha_identifiers, &irb);

    const std::array<std::byte, 1> layer_groups_enabled_id = {
        std::byte { 0x01 },
    };
    append_irb_resource(0x0430U, layer_groups_enabled_id, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 30U);
    EXPECT_EQ(r.entries_decoded, 87U);

    const Entry* x_resolution = find_photoshop_irb_field(store, 0x03EDU,
                                                         "XResolution");
    ASSERT_NE(x_resolution, nullptr);
    EXPECT_EQ(x_resolution->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(x_resolution->value.elem_type, MetaElementType::F64);
    EXPECT_DOUBLE_EQ(std::bit_cast<double>(x_resolution->value.data.f64_bits),
                     72.5);
    EXPECT_TRUE(any(x_resolution->flags, EntryFlags::Derived));

    const Entry* alpha_name_count
        = find_photoshop_irb_field(store, 0x03EEU, "AlphaChannelNameCount");
    ASSERT_NE(alpha_name_count, nullptr);
    EXPECT_EQ(alpha_name_count->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(alpha_name_count->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(alpha_name_count->value.data.u64, 2U);

    const std::vector<std::string_view> alpha_name_values
        = collect_photoshop_irb_text_fields(store, 0x03EEU, "AlphaChannelName");
    ASSERT_EQ(alpha_name_values.size(), 2U);
    EXPECT_EQ(alpha_name_values[0], "Mask");
    EXPECT_EQ(alpha_name_values[1], "Soft");

    const Entry* caption_field = find_photoshop_irb_field(store, 0x03F0U,
                                                          "Caption");
    ASSERT_NE(caption_field, nullptr);
    EXPECT_EQ(caption_field->value.kind, MetaValueKind::Text);
    EXPECT_EQ(caption_field->value.text_encoding, TextEncoding::Utf8);
    EXPECT_EQ(arena_string(store, caption_field->value.data.span), "Caption");

    const Entry* quick_mask_channel_id
        = find_photoshop_irb_field(store, 0x03FEU, "QuickMaskChannelID");
    ASSERT_NE(quick_mask_channel_id, nullptr);
    EXPECT_EQ(quick_mask_channel_id->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(quick_mask_channel_id->value.elem_type, MetaElementType::U16);
    EXPECT_EQ(quick_mask_channel_id->value.data.u64, 5U);

    const Entry* quick_mask_was_empty
        = find_photoshop_irb_field(store, 0x03FEU, "QuickMaskWasEmpty");
    ASSERT_NE(quick_mask_was_empty, nullptr);
    EXPECT_EQ(quick_mask_was_empty->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(quick_mask_was_empty->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(quick_mask_was_empty->value.data.u64, 1U);

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

    const Entry* print_flag_labels
        = find_photoshop_irb_field(store, 0x03F3U, "PrintFlagLabels");
    ASSERT_NE(print_flag_labels, nullptr);
    EXPECT_EQ(print_flag_labels->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(print_flag_labels->value.elem_type, MetaElementType::U8);
    EXPECT_EQ(print_flag_labels->value.data.u64, 7U);

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

    const Entry* unicode_alpha_name_count
        = find_photoshop_irb_field(store, 0x0415U, "UnicodeAlphaNameCount");
    ASSERT_NE(unicode_alpha_name_count, nullptr);
    EXPECT_EQ(unicode_alpha_name_count->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(unicode_alpha_name_count->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(unicode_alpha_name_count->value.data.u64, 2U);

    const std::vector<std::string_view> unicode_alpha_name_values
        = collect_photoshop_irb_text_fields(store, 0x0415U, "UnicodeAlphaName");
    ASSERT_EQ(unicode_alpha_name_values.size(), 2U);
    EXPECT_EQ(unicode_alpha_name_values[0], "Matte");
    EXPECT_EQ(unicode_alpha_name_values[1], "Depth");

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

    const Entry* alpha_identifier_count
        = find_photoshop_irb_field(store, 0x041DU, "AlphaIdentifierCount");
    ASSERT_NE(alpha_identifier_count, nullptr);
    EXPECT_EQ(alpha_identifier_count->value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(alpha_identifier_count->value.elem_type, MetaElementType::U32);
    EXPECT_EQ(alpha_identifier_count->value.data.u64, 2U);

    const std::vector<uint32_t> alpha_identifier_values
        = collect_photoshop_irb_u32_fields(store, 0x041DU, "AlphaIdentifier");
    ASSERT_EQ(alpha_identifier_values.size(), 2U);
    EXPECT_EQ(alpha_identifier_values[0], 101U);
    EXPECT_EQ(alpha_identifier_values[1], 202U);

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
    const std::array<std::byte, 4> short_alpha_names = {
        std::byte { 0x01 },
        std::byte { 'A' },
        std::byte { 0x03 },
        std::byte { 'B' },
    };
    const std::array<std::byte, 2> short_caption = {
        std::byte { 0x05 },
        std::byte { 'x' },
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
    const std::array<std::byte, 2> short_quick_mask = {
        std::byte { 0x00 },
        std::byte { 0x05 },
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
    const std::array<std::byte, 3> short_unicode_alpha_names = {
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
    const std::array<std::byte, 5> short_alpha_identifiers = {
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x01 }, std::byte { 0xFF },
    };
    const std::array<std::byte, 1> short_layer_selection_ids = {
        std::byte { 0x00 },
    };
    const std::array<std::byte, 0> empty_layer_groups_enabled_id = {};
    std::vector<std::byte> irb;
    append_irb_resource(0x03EDU, short_resolution, &irb);
    append_irb_resource(0x03EEU, short_alpha_names, &irb);
    append_irb_resource(0x03F0U, short_caption, &irb);
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
    append_irb_resource(0x03FEU, short_quick_mask, &irb);
    append_irb_resource(0x0406U, short_jpeg_quality, &irb);
    append_irb_resource(0x0410U, empty_watermark, &irb);
    append_irb_resource(0x0411U, empty_icc_untagged, &irb);
    append_irb_resource(0x0414U, short_ids_base_value, &irb);
    append_irb_resource(0x0415U, short_unicode_alpha_names, &irb);
    append_irb_resource(0x0426U, short_print_scale_info, &irb);
    append_irb_resource(0x041AU, short_slice_info, &irb);
    append_irb_resource(0x041BU, short_workflow_url, &irb);
    append_irb_resource(0x041EU, short_url_list, &irb);
    append_irb_resource(0x041DU, short_alpha_identifiers, &irb);
    append_irb_resource(0x042DU, short_layer_selection_ids, &irb);
    append_irb_resource(0x0416U, short_indexed_color_table_count, &irb);
    append_irb_resource(0x0417U, short_transparent_index, &irb);
    append_irb_resource(0x0419U, short_global_altitude, &irb);
    append_irb_resource(0x0430U, empty_layer_groups_enabled_id, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 29U);
    EXPECT_EQ(r.entries_decoded, 32U);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03EDU, "XResolution"), nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03EEU, "AlphaChannelNameCount"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03EEU, "AlphaChannelName"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03F0U, "Caption"), nullptr);
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
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03FEU, "QuickMaskChannelID"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x03FEU, "QuickMaskWasEmpty"),
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
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0415U, "UnicodeAlphaNameCount"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x0415U, "UnicodeAlphaName"),
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
    EXPECT_EQ(find_photoshop_irb_field(store, 0x041DU, "AlphaIdentifierCount"),
              nullptr);
    EXPECT_EQ(find_photoshop_irb_field(store, 0x041DU, "AlphaIdentifier"),
              nullptr);
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
    EXPECT_EQ(photoshop_irb_resource_name(0x03E8U), "Photoshop2Info");
    EXPECT_EQ(photoshop_irb_resource_name(0x03E9U), "MacintoshPrintInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x03EAU), "XMLData");
    EXPECT_EQ(photoshop_irb_resource_name(0x03EBU), "Photoshop2ColorTable");
    EXPECT_EQ(photoshop_irb_resource_name(0x0421U), "VersionInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x0408U), "GridGuidesInfo");
    EXPECT_EQ(photoshop_irb_resource_name(0x040AU), "CopyrightFlag");
    EXPECT_EQ(photoshop_irb_resource_name(0x040BU), "URL");
    EXPECT_EQ(photoshop_irb_resource_name(0x040FU), "ICC_Profile");
    EXPECT_EQ(photoshop_irb_resource_name(0x040DU), "GlobalAngle");
    EXPECT_EQ(photoshop_irb_resource_name(0x0405U), "RawImageMode");
    EXPECT_EQ(photoshop_irb_resource_name(0x0BB7U), "ClippingPathName");
    EXPECT_EQ(photoshop_irb_resource_name(0x0412U), "EffectsVisible");
    EXPECT_EQ(photoshop_irb_resource_name(0x0413U), "SpotHalftone");
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
    EXPECT_EQ(photoshop_irb_resource_name(0x041CU), "JumpToXPEP");
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
