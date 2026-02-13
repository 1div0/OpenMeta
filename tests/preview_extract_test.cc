#include "openmeta/preview_extract.h"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <vector>

namespace openmeta {
namespace {

    static void append_u16le(std::vector<std::byte>* out, uint16_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte(static_cast<uint8_t>(v & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 8) & 0xFFU)));
    }

    static void append_u32le(std::vector<std::byte>* out, uint32_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte(static_cast<uint8_t>(v & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 8) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 16) & 0xFFU)));
        out->push_back(std::byte(static_cast<uint8_t>((v >> 24) & 0xFFU)));
    }

    static void append_bytes(std::vector<std::byte>* out, const char* s)
    {
        ASSERT_NE(out, nullptr);
        ASSERT_NE(s, nullptr);
        for (size_t i = 0; s[i] != '\0'; ++i) {
            out->push_back(std::byte(static_cast<uint8_t>(s[i])));
        }
    }

    static std::vector<std::byte> make_tiff_with_ifd1_jpeg_preview()
    {
        std::vector<std::byte> bytes;

        append_bytes(&bytes, "II");
        append_u16le(&bytes, 42U);
        append_u32le(&bytes, 8U);  // ifd0

        append_u16le(&bytes, 0U);   // ifd0 entries
        append_u32le(&bytes, 14U);  // next ifd -> ifd1

        append_u16le(&bytes, 2U);       // ifd1 entries
        append_u16le(&bytes, 0x0201U);  // JPEGInterchangeFormat
        append_u16le(&bytes, 4U);       // LONG
        append_u32le(&bytes, 1U);
        append_u32le(&bytes, 44U);  // preview offset

        append_u16le(&bytes, 0x0202U);  // JPEGInterchangeFormatLength
        append_u16le(&bytes, 4U);       // LONG
        append_u32le(&bytes, 1U);
        append_u32le(&bytes, 4U);  // preview length

        append_u32le(&bytes, 0U);  // no next ifd

        bytes.push_back(std::byte { 0xFF });
        bytes.push_back(std::byte { 0xD8 });
        bytes.push_back(std::byte { 0xFF });
        bytes.push_back(std::byte { 0xD9 });
        return bytes;
    }

    static std::vector<std::byte> make_tiff_with_jpg_from_raw(bool jpeg_soi)
    {
        std::vector<std::byte> bytes;

        append_bytes(&bytes, "II");
        append_u16le(&bytes, 42U);
        append_u32le(&bytes, 8U);  // ifd0

        append_u16le(&bytes, 1U);       // ifd0 entries
        append_u16le(&bytes, 0x002EU);  // JpgFromRaw
        append_u16le(&bytes, 7U);       // UNDEFINED
        append_u32le(&bytes, 6U);       // count
        append_u32le(&bytes, 26U);      // value offset
        append_u32le(&bytes, 0U);       // no next ifd

        if (jpeg_soi) {
            bytes.push_back(std::byte { 0xFF });
            bytes.push_back(std::byte { 0xD8 });
        } else {
            bytes.push_back(std::byte { 0x00 });
            bytes.push_back(std::byte { 0x11 });
        }
        bytes.push_back(std::byte { 0x01 });
        bytes.push_back(std::byte { 0x02 });
        bytes.push_back(std::byte { 0xFF });
        bytes.push_back(std::byte { 0xD9 });
        return bytes;
    }

}  // namespace

TEST(PreviewExtract, FindsExifJpegInterchangeCandidate)
{
    const std::vector<std::byte> bytes = make_tiff_with_ifd1_jpeg_preview();

    std::array<ContainerBlockRef, 8> blocks {};
    std::array<PreviewCandidate, 8> previews {};
    const PreviewScanResult res = scan_preview_candidates(
        std::span<const std::byte>(bytes.data(), bytes.size()),
        std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
        std::span<PreviewCandidate>(previews.data(), previews.size()),
        PreviewScanOptions {});

    ASSERT_EQ(res.status, PreviewScanStatus::Ok);
    ASSERT_EQ(res.written, 1U);
    const PreviewCandidate& p = previews[0];
    EXPECT_EQ(p.kind, PreviewKind::ExifJpegInterchange);
    EXPECT_EQ(p.offset_tag, 0x0201U);
    EXPECT_EQ(p.length_tag, 0x0202U);
    EXPECT_EQ(p.file_offset, 44U);
    EXPECT_EQ(p.size, 4U);
    EXPECT_TRUE(p.has_jpeg_soi_signature);

    std::array<std::byte, 4> out {};
    const PreviewExtractResult er = extract_preview_candidate(
        std::span<const std::byte>(bytes.data(), bytes.size()), p,
        std::span<std::byte>(out.data(), out.size()), PreviewExtractOptions {});
    ASSERT_EQ(er.status, PreviewExtractStatus::Ok);
    EXPECT_EQ(er.written, 4U);
    EXPECT_EQ(static_cast<uint8_t>(out[0]), 0xFFU);
    EXPECT_EQ(static_cast<uint8_t>(out[1]), 0xD8U);
}

TEST(PreviewExtract, FindsJpgFromRawCandidate)
{
    const std::vector<std::byte> bytes = make_tiff_with_jpg_from_raw(true);

    std::array<ContainerBlockRef, 8> blocks {};
    std::array<PreviewCandidate, 8> previews {};
    PreviewScanOptions options;
    options.include_exif_jpeg_interchange = false;
    options.include_jpg_from_raw          = true;

    const PreviewScanResult res = scan_preview_candidates(
        std::span<const std::byte>(bytes.data(), bytes.size()),
        std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
        std::span<PreviewCandidate>(previews.data(), previews.size()), options);

    ASSERT_EQ(res.status, PreviewScanStatus::Ok);
    ASSERT_EQ(res.written, 1U);
    const PreviewCandidate& p = previews[0];
    EXPECT_EQ(p.kind, PreviewKind::ExifJpgFromRaw);
    EXPECT_EQ(p.offset_tag, 0x002EU);
    EXPECT_EQ(p.file_offset, 26U);
    EXPECT_EQ(p.size, 6U);
    EXPECT_TRUE(p.has_jpeg_soi_signature);
}

TEST(PreviewExtract, RequireJpegSoiFiltersNonJpegCandidate)
{
    const std::vector<std::byte> bytes = make_tiff_with_jpg_from_raw(false);

    std::array<ContainerBlockRef, 8> blocks {};
    std::array<PreviewCandidate, 8> previews {};
    PreviewScanOptions options;
    options.include_exif_jpeg_interchange = false;
    options.include_jpg_from_raw          = true;
    options.require_jpeg_soi              = true;

    const PreviewScanResult res = scan_preview_candidates(
        std::span<const std::byte>(bytes.data(), bytes.size()),
        std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
        std::span<PreviewCandidate>(previews.data(), previews.size()), options);

    ASSERT_EQ(res.status, PreviewScanStatus::Ok);
    EXPECT_EQ(res.written, 0U);
}

TEST(PreviewExtract, ExtractionChecksOutputAndLimits)
{
    const std::vector<std::byte> bytes = make_tiff_with_ifd1_jpeg_preview();

    std::array<ContainerBlockRef, 8> blocks {};
    std::array<PreviewCandidate, 8> previews {};
    const PreviewScanResult scan = scan_preview_candidates(
        std::span<const std::byte>(bytes.data(), bytes.size()),
        std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
        std::span<PreviewCandidate>(previews.data(), previews.size()),
        PreviewScanOptions {});
    ASSERT_EQ(scan.status, PreviewScanStatus::Ok);
    ASSERT_EQ(scan.written, 1U);

    const PreviewCandidate& p = previews[0];
    std::array<std::byte, 2> too_small {};
    PreviewExtractOptions options;
    options.max_output_bytes         = 1024U;
    const PreviewExtractResult small = extract_preview_candidate(
        std::span<const std::byte>(bytes.data(), bytes.size()), p,
        std::span<std::byte>(too_small.data(), too_small.size()), options);
    EXPECT_EQ(small.status, PreviewExtractStatus::OutputTruncated);
    EXPECT_EQ(small.needed, 4U);

    options.max_output_bytes = 3U;
    std::array<std::byte, 8> out {};
    const PreviewExtractResult limited = extract_preview_candidate(
        std::span<const std::byte>(bytes.data(), bytes.size()), p,
        std::span<std::byte>(out.data(), out.size()), options);
    EXPECT_EQ(limited.status, PreviewExtractStatus::LimitExceeded);
}

}  // namespace openmeta
