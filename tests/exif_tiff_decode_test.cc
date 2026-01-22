#include "openmeta/exif_tiff_decode.h"

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

    static void append_u16be(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }

    static void append_u32be(std::vector<std::byte>* out, uint32_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }

    static std::string_view arena_string(const ByteArena& arena,
                                         const MetaValue& v)
    {
        const std::span<const std::byte> bytes = arena.span(v.data.span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static MetaKeyView exif_key(std::string_view ifd, uint16_t tag)
    {
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = ifd;
        key.data.exif_tag.tag = tag;
        return key;
    }

    static std::vector<std::byte> make_test_tiff_le()
    {
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42);
        append_u32le(&tiff, 8);

        // IFD0 (offset 8).
        append_u16le(&tiff, 2);

        // Make (0x010F) ASCII "Canon\0" at offset 38.
        append_u16le(&tiff, 0x010F);
        append_u16le(&tiff, 2);
        append_u32le(&tiff, 6);
        append_u32le(&tiff, 38);

        // ExifIFDPointer (0x8769) LONG offset 44.
        append_u16le(&tiff, 0x8769);
        append_u16le(&tiff, 4);
        append_u32le(&tiff, 1);
        append_u32le(&tiff, 44);

        // next IFD offset.
        append_u32le(&tiff, 0);

        EXPECT_EQ(tiff.size(), 38U);
        append_bytes(&tiff, "Canon");
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), 44U);

        // ExifIFD (offset 44).
        append_u16le(&tiff, 1);

        // DateTimeOriginal (0x9003) ASCII at offset 62.
        append_u16le(&tiff, 0x9003);
        append_u16le(&tiff, 2);
        append_u32le(&tiff, 20);
        append_u32le(&tiff, 62);

        append_u32le(&tiff, 0);

        EXPECT_EQ(tiff.size(), 62U);
        append_bytes(&tiff, "2024:01:01 00:00:00");
        tiff.push_back(std::byte { 0 });
        return tiff;
    }

    static std::vector<std::byte> make_test_tiff_be()
    {
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "MM");
        append_u16be(&tiff, 42);
        append_u32be(&tiff, 8);

        // IFD0 (offset 8).
        append_u16be(&tiff, 2);

        // Make (0x010F) ASCII "Canon\0" at offset 38.
        append_u16be(&tiff, 0x010F);
        append_u16be(&tiff, 2);
        append_u32be(&tiff, 6);
        append_u32be(&tiff, 38);

        // ExifIFDPointer (0x8769) LONG offset 44.
        append_u16be(&tiff, 0x8769);
        append_u16be(&tiff, 4);
        append_u32be(&tiff, 1);
        append_u32be(&tiff, 44);

        // next IFD offset.
        append_u32be(&tiff, 0);

        EXPECT_EQ(tiff.size(), 38U);
        append_bytes(&tiff, "Canon");
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), 44U);

        // ExifIFD (offset 44).
        append_u16be(&tiff, 1);

        // DateTimeOriginal (0x9003) ASCII at offset 62.
        append_u16be(&tiff, 0x9003);
        append_u16be(&tiff, 2);
        append_u32be(&tiff, 20);
        append_u32be(&tiff, 62);

        append_u32be(&tiff, 0);

        EXPECT_EQ(tiff.size(), 62U);
        append_bytes(&tiff, "2024:01:01 00:00:00");
        tiff.push_back(std::byte { 0 });
        return tiff;
    }

}  // namespace

TEST(ExifTiffDecode, DecodesIfd0AndExifIfd_LittleEndian)
{
    const std::vector<std::byte> tiff = make_test_tiff_le();

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.include_pointer_tags = true;
    const ExifDecodeResult res   = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);
    EXPECT_EQ(res.ifds_written, 2U);
    EXPECT_EQ(res.entries_decoded, 3U);

    store.finalize();

    const std::span<const EntryId> make_ids = store.find_all(
        exif_key("ifd0", 0x010F));
    ASSERT_EQ(make_ids.size(), 1U);
    const Entry& make = store.entry(make_ids[0]);
    EXPECT_EQ(make.origin.wire_type.family, WireFamily::Tiff);
    EXPECT_EQ(make.origin.wire_type.code, 2U);
    EXPECT_EQ(make.origin.wire_count, 6U);
    EXPECT_EQ(make.value.kind, MetaValueKind::Text);
    EXPECT_EQ(make.value.text_encoding, TextEncoding::Ascii);
    EXPECT_EQ(arena_string(store.arena(), make.value), "Canon");

    const std::span<const EntryId> ptr_ids = store.find_all(
        exif_key("ifd0", 0x8769));
    ASSERT_EQ(ptr_ids.size(), 1U);
    const Entry& ptr = store.entry(ptr_ids[0]);
    EXPECT_EQ(ptr.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(ptr.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(ptr.value.data.u64, 44U);

    const std::span<const EntryId> dt_ids = store.find_all(
        exif_key("exififd", 0x9003));
    ASSERT_EQ(dt_ids.size(), 1U);
    const Entry& dt = store.entry(dt_ids[0]);
    EXPECT_EQ(dt.origin.wire_count, 20U);
    EXPECT_EQ(dt.value.kind, MetaValueKind::Text);
    EXPECT_EQ(arena_string(store.arena(), dt.value), "2024:01:01 00:00:00");
}

TEST(ExifTiffDecode, DecodesIfd0AndExifIfd_BigEndian)
{
    const std::vector<std::byte> tiff = make_test_tiff_be();

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.include_pointer_tags = true;
    const ExifDecodeResult res   = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);
    EXPECT_EQ(res.ifds_written, 2U);
    EXPECT_EQ(res.entries_decoded, 3U);

    store.finalize();

    const std::span<const EntryId> make_ids = store.find_all(
        exif_key("ifd0", 0x010F));
    ASSERT_EQ(make_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(make_ids[0]).value),
              "Canon");

    const std::span<const EntryId> dt_ids = store.find_all(
        exif_key("exififd", 0x9003));
    ASSERT_EQ(dt_ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(dt_ids[0]).value),
              "2024:01:01 00:00:00");
}

TEST(ExifTiffDecode, PreservesUtf8Type129)
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42);
    append_u32le(&tiff, 8);

    // IFD0 with a single UTF-8 tag (type 129) stored inline.
    append_u16le(&tiff, 1);
    append_u16le(&tiff, 0x010E);  // ImageDescription
    append_u16le(&tiff, 129);     // UTF-8
    append_u32le(&tiff, 3);       // "Hi\0"
    append_bytes(&tiff, "Hi");
    tiff.push_back(std::byte { 0 });
    tiff.push_back(std::byte { 0 });  // pad to 4
    append_u32le(&tiff, 0);

    MetaStore store;
    std::array<ExifIfdRef, 4> ifds {};
    ExifDecodeOptions options;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("ifd0", 0x010E));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.origin.wire_type.code, 129U);
    EXPECT_EQ(e.value.kind, MetaValueKind::Text);
    EXPECT_EQ(e.value.text_encoding, TextEncoding::Utf8);
    EXPECT_EQ(arena_string(store.arena(), e.value), "Hi");
}

TEST(SimpleMetaRead, ScansAndDecodesJpegApp1Exif)
{
    const std::vector<std::byte> tiff = make_test_tiff_le();

    std::vector<std::byte> jpeg;
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD8 });

    std::vector<std::byte> payload;
    append_bytes(&payload, "Exif");
    payload.push_back(std::byte { 0 });
    payload.push_back(std::byte { 0 });
    payload.insert(payload.end(), tiff.begin(), tiff.end());

    const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xE1 });
    jpeg.push_back(std::byte { static_cast<uint8_t>((seg_len >> 8) & 0xFF) });
    jpeg.push_back(std::byte { static_cast<uint8_t>((seg_len >> 0) & 0xFF) });
    jpeg.insert(jpeg.end(), payload.begin(), payload.end());

    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD9 });

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions exif_options;
    const SimpleMetaResult res = simple_meta_read(jpeg, store, blocks, ifds,
                                                  exif_options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("exififd", 0x9003));
    ASSERT_EQ(ids.size(), 1U);
    EXPECT_EQ(arena_string(store.arena(), store.entry(ids[0]).value),
              "2024:01:01 00:00:00");
}

}  // namespace openmeta
