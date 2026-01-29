#include "openmeta/simple_meta.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static void append_bytes(std::vector<std::byte>* out, std::string_view s)
    {
        out->insert(out->end(), reinterpret_cast<const std::byte*>(s.data()),
                    reinterpret_cast<const std::byte*>(s.data() + s.size()));
    }


    static void append_u16be(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }


    static void append_u16le(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
    }


    static void append_u32le(std::vector<std::byte>* out, uint32_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) });
    }


    static MetaKeyView exif_key(std::string_view ifd, uint16_t tag)
    {
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = ifd;
        key.data.exif_tag.tag = tag;
        return key;
    }


    static std::vector<std::byte> make_mpf_tiff()
    {
        // Minimal MPF TIFF stream:
        // - MPFVersion (0xB000) ASCII "0100" (no terminator)
        // - NumberOfImages (0xB001) LONG 3
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42);
        append_u32le(&tiff, 8);

        append_u16le(&tiff, 2);

        append_u16le(&tiff, 0xB000);
        append_u16le(&tiff, 2);
        append_u32le(&tiff, 4);
        append_bytes(&tiff, "0100");

        append_u16le(&tiff, 0xB001);
        append_u16le(&tiff, 4);
        append_u32le(&tiff, 1);
        append_u32le(&tiff, 3);

        append_u32le(&tiff, 0);
        return tiff;
    }

}  // namespace

TEST(MpfDecode, SimpleMetaDecodesMpfBlocks)
{
    const std::vector<std::byte> mpf_tiff = make_mpf_tiff();

    std::vector<std::byte> jpg;
    jpg.push_back(std::byte { 0xFF });
    jpg.push_back(std::byte { 0xD8 });  // SOI

    std::vector<std::byte> seg_payload;
    append_bytes(&seg_payload, "MPF");
    seg_payload.push_back(std::byte { 0 });
    seg_payload.insert(seg_payload.end(), mpf_tiff.begin(), mpf_tiff.end());

    jpg.push_back(std::byte { 0xFF });
    jpg.push_back(std::byte { 0xE2 });  // APP2
    const uint16_t seg_len = static_cast<uint16_t>(seg_payload.size() + 2U);
    append_u16be(&jpg, seg_len);
    jpg.insert(jpg.end(), seg_payload.begin(), seg_payload.end());

    jpg.push_back(std::byte { 0xFF });
    jpg.push_back(std::byte { 0xD9 });  // EOI

    MetaStore store;
    std::array<ContainerBlockRef, 32> blocks {};
    std::array<ExifIfdRef, 32> ifds {};
    std::array<std::byte, 4096> payload {};
    std::array<uint32_t, 512> indices {};

    ExifDecodeOptions exif_options;
    PayloadOptions payload_options;

    (void)simple_meta_read(jpg, store, blocks, ifds, payload, indices,
                           exif_options, payload_options);

    store.finalize();

    const std::span<const EntryId> ids = store.find_all(
        exif_key("mpf0", 0xB001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 3U);
}

}  // namespace openmeta
