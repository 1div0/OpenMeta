#include "openmeta/simple_meta.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace openmeta {
namespace {

    static void append_u16be(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }


    static void append_jpeg_segment(std::vector<std::byte>* out, uint16_t marker,
                                    std::span<const std::byte> payload)
    {
        ASSERT_TRUE(out);
        out->push_back(std::byte { 0xFF });
        out->push_back(
            std::byte { static_cast<uint8_t>(marker & 0xFFU) });
        const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
        append_u16be(out, seg_len);
        out->insert(out->end(), payload.begin(), payload.end());
    }


    static MetaKeyView exif_key(std::string_view ifd, uint16_t tag)
    {
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = ifd;
        key.data.exif_tag.tag = tag;
        return key;
    }


    static float f32_from_bits(uint32_t bits) noexcept
    {
        float v = 0.0f;
        static_assert(sizeof(bits) == sizeof(v));
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

}  // namespace

TEST(DjiApp4Decode, DecodesThermalParams3FromJpegApp4)
{
    std::vector<std::byte> payload(32, std::byte { 0x00 });
    // magic: AA 55 38 00 (big-endian u32)
    payload.push_back(std::byte { 0xAA });
    payload.push_back(std::byte { 0x55 });
    payload.push_back(std::byte { 0x38 });
    payload.push_back(std::byte { 0x00 });

    // u16le fields at +4/+6/+8/+0a (relative to magic):
    // RH=60, ObjectDistance=50 (-> 5.0), Emissivity=98 (-> 0.98), ReflectedTemp=230 (-> 23.0)
    payload.push_back(std::byte { 0x3C });
    payload.push_back(std::byte { 0x00 });
    payload.push_back(std::byte { 0x32 });
    payload.push_back(std::byte { 0x00 });
    payload.push_back(std::byte { 0x62 });
    payload.push_back(std::byte { 0x00 });
    payload.push_back(std::byte { 0xE6 });
    payload.push_back(std::byte { 0x00 });

    std::vector<std::byte> jpeg;
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD8 });
    append_jpeg_segment(&jpeg, 0xFFE4, payload);
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD9 });

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 4096> scratch_payload {};
    std::array<uint32_t, 16> scratch_indices {};

    ExifDecodeOptions exif_options;
    exif_options.decode_makernote = true;

    PayloadOptions payload_options;
    payload_options.decompress = true;

    (void)simple_meta_read(jpeg, store, blocks, ifds, scratch_payload,
                           scratch_indices, exif_options, payload_options);
    store.finalize();

    {
        const std::span<const EntryId> ids
            = store.find_all(exif_key("mk_dji_thermalparams3_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 60U);
    }
    {
        const std::span<const EntryId> ids
            = store.find_all(exif_key("mk_dji_thermalparams3_0", 0x0006));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.elem_type, MetaElementType::F32);
        EXPECT_NEAR(f32_from_bits(e.value.data.f32_bits), 5.0f, 1e-6f);
    }
    {
        const std::span<const EntryId> ids
            = store.find_all(exif_key("mk_dji_thermalparams3_0", 0x0008));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.elem_type, MetaElementType::F32);
        EXPECT_NEAR(f32_from_bits(e.value.data.f32_bits), 0.98f, 1e-6f);
    }
    {
        const std::span<const EntryId> ids
            = store.find_all(exif_key("mk_dji_thermalparams3_0", 0x000a));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.elem_type, MetaElementType::F32);
        EXPECT_NEAR(f32_from_bits(e.value.data.f32_bits), 23.0f, 1e-6f);
    }
}

}  // namespace openmeta

