#include "openmeta/container_payload.h"
#include "openmeta/container_scan.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
#    include <zlib.h>
#endif

namespace openmeta {
namespace {

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


    static void append_u32le(std::vector<std::byte>* out, uint32_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) });
    }


    static void append_fourcc(std::vector<std::byte>* out, uint32_t f)
    {
        append_u32be(out, f);
    }


    static void append_bytes(std::vector<std::byte>* out, std::string_view s)
    {
        for (char c : s) {
            out->push_back(std::byte { static_cast<uint8_t>(c) });
        }
    }


    static void append_jpeg_segment(std::vector<std::byte>* out,
                                    uint16_t marker,
                                    std::span<const std::byte> payload)
    {
        out->push_back(std::byte { 0xFF });
        out->push_back(std::byte { static_cast<uint8_t>(marker & 0xFF) });
        const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2);
        append_u16be(out, seg_len);
        out->insert(out->end(), payload.begin(), payload.end());
    }


    static void append_fullbox_header(std::vector<std::byte>* out,
                                      uint8_t version)
    {
        out->push_back(std::byte { version });
        out->push_back(std::byte { 0x00 });
        out->push_back(std::byte { 0x00 });
        out->push_back(std::byte { 0x00 });
    }


    static void append_bmff_box(std::vector<std::byte>* out, uint32_t type,
                                std::span<const std::byte> payload)
    {
        append_u32be(out, static_cast<uint32_t>(8 + payload.size()));
        append_fourcc(out, type);
        out->insert(out->end(), payload.begin(), payload.end());
    }


    static void append_png_chunk(std::vector<std::byte>* out, uint32_t type,
                                 std::span<const std::byte> data)
    {
        append_u32be(out, static_cast<uint32_t>(data.size()));
        append_fourcc(out, type);
        out->insert(out->end(), data.begin(), data.end());
        append_u32be(out, 0);  // CRC ignored by scanner.
    }


    TEST(ContainerPayload, GifSubBlocks)
    {
        std::vector<std::byte> gif;
        append_bytes(&gif, "GIF89a");
        // Logical Screen Descriptor: 1x1, no global color table.
        gif.push_back(std::byte { 0x01 });
        gif.push_back(std::byte { 0x00 });
        gif.push_back(std::byte { 0x01 });
        gif.push_back(std::byte { 0x00 });
        gif.push_back(std::byte { 0x00 });
        gif.push_back(std::byte { 0x00 });
        gif.push_back(std::byte { 0x00 });

        // Application Extension for XMP.
        gif.push_back(std::byte { 0x21 });
        gif.push_back(std::byte { 0xFF });
        gif.push_back(std::byte { 0x0B });
        append_bytes(&gif, "XMP Data");
        append_bytes(&gif, "XMP");
        gif.push_back(std::byte { 0x03 });
        append_bytes(&gif, "abc");
        gif.push_back(std::byte { 0x00 });
        gif.push_back(std::byte { 0x3B });

        std::array<ContainerBlockRef, 4> blocks {};
        const ScanResult scan = scan_gif(gif, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 1U);

        std::array<std::byte, 16> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(gif,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        EXPECT_EQ(res.needed, 3U);
        EXPECT_EQ(res.written, 3U);
        EXPECT_EQ(out[0], std::byte { 'a' });
        EXPECT_EQ(out[1], std::byte { 'b' });
        EXPECT_EQ(out[2], std::byte { 'c' });
    }


    TEST(ContainerPayload, JpegIccSeqTotal)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        std::vector<std::byte> icc0;
        append_bytes(&icc0, "ICC_PROFILE");
        icc0.push_back(std::byte { 0x00 });
        icc0.push_back(std::byte { 0x01 });  // seq
        icc0.push_back(std::byte { 0x02 });  // total
        append_bytes(&icc0, "AB");
        append_jpeg_segment(&jpeg, 0xFFE2,
                            std::span<const std::byte>(icc0.data(),
                                                       icc0.size()));

        std::vector<std::byte> icc1;
        append_bytes(&icc1, "ICC_PROFILE");
        icc1.push_back(std::byte { 0x00 });
        icc1.push_back(std::byte { 0x02 });  // seq
        icc1.push_back(std::byte { 0x02 });  // total
        append_bytes(&icc1, "CD");
        append_jpeg_segment(&jpeg, 0xFFE2,
                            std::span<const std::byte>(icc1.data(),
                                                       icc1.size()));

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Icc);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Icc);

        std::array<std::byte, 4> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(jpeg,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        EXPECT_EQ(res.needed, 4U);
        EXPECT_EQ(res.written, 4U);
        EXPECT_EQ(out[0], std::byte { 'A' });
        EXPECT_EQ(out[1], std::byte { 'B' });
        EXPECT_EQ(out[2], std::byte { 'C' });
        EXPECT_EQ(out[3], std::byte { 'D' });

        std::array<std::byte, 3> short_out {};
        const PayloadResult short_res
            = extract_payload(jpeg,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, short_out, scratch, opts);
        EXPECT_EQ(short_res.status, PayloadStatus::OutputTruncated);
        EXPECT_EQ(short_res.needed, 4U);
        EXPECT_EQ(short_res.written, 3U);
    }


    TEST(ContainerPayload, JpegXmpExtendedGuidOffset)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        const std::string_view guid
            = "0123456789ABCDEF0123456789ABCDEF";  // 32 bytes
        const uint32_t full_len = 6;

        std::vector<std::byte> seg1;
        append_bytes(&seg1, "http://ns.adobe.com/xmp/extension/");
        seg1.push_back(std::byte { 0x00 });
        append_bytes(&seg1, guid);
        append_u32be(&seg1, full_len);
        append_u32be(&seg1, 3);
        append_bytes(&seg1, "DEF");

        std::vector<std::byte> seg0;
        append_bytes(&seg0, "http://ns.adobe.com/xmp/extension/");
        seg0.push_back(std::byte { 0x00 });
        append_bytes(&seg0, guid);
        append_u32be(&seg0, full_len);
        append_u32be(&seg0, 0);
        append_bytes(&seg0, "ABC");

        // Deliberately store out-of-order segments (offset 3, then offset 0).
        append_jpeg_segment(&jpeg, 0xFFE1,
                            std::span<const std::byte>(seg1.data(),
                                                       seg1.size()));
        append_jpeg_segment(&jpeg, 0xFFE1,
                            std::span<const std::byte>(seg0.data(),
                                                       seg0.size()));

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::XmpExtended);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::XmpExtended);

        std::array<std::byte, 16> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(jpeg,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        EXPECT_EQ(res.needed, 6U);
        EXPECT_EQ(res.written, 6U);
        EXPECT_EQ(out[0], std::byte { 'A' });
        EXPECT_EQ(out[1], std::byte { 'B' });
        EXPECT_EQ(out[2], std::byte { 'C' });
        EXPECT_EQ(out[3], std::byte { 'D' });
        EXPECT_EQ(out[4], std::byte { 'E' });
        EXPECT_EQ(out[5], std::byte { 'F' });
    }


    TEST(ContainerPayload, BmffMetaItemExtents)
    {
        // ISO-BMFF file with a single Exif item split across two `iloc` extents.
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "exif");
        infe_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_u32be(&idat_payload, 4);  // TIFF header offset
        {
            const std::array<std::byte, 4> tiff_hdr = {
                std::byte { 'I' },
                std::byte { 'I' },
                std::byte { 0x2A },
                std::byte { 0x00 },
            };
            idat_payload.insert(idat_payload.end(), tiff_hdr.begin(),
                                tiff_hdr.end());
        }
        append_u32le(&idat_payload, 8);
        // IFD0 at offset 8: 0 entries, next=0.
        append_u16be(&idat_payload, 0);
        append_u32le(&idat_payload, 0);
        ASSERT_EQ(idat_payload.size(), 18U);
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload, 1);              // construction_method=1
        append_u16be(&iloc_payload, 0);              // data_reference_index
        append_u16be(&iloc_payload, 2);              // extent_count
        append_u32be(&iloc_payload, 0);              // extent0 offset
        append_u32be(&iloc_payload, 12);             // extent0 length
        append_u32be(&iloc_payload, 12);             // extent1 offset
        append_u32be(&iloc_payload, 6);              // extent1 length
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));

        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_bmff(file, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].part_count, 2U);

        std::array<std::byte, 64> out {};
        std::array<uint32_t, 16> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(file,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        EXPECT_EQ(res.needed, 14U);
        EXPECT_EQ(out[0], std::byte { 'I' });
        EXPECT_EQ(out[1], std::byte { 'I' });
        EXPECT_EQ(out[2], std::byte { 0x2A });
        EXPECT_EQ(out[3], std::byte { 0x00 });
    }

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
    TEST(ContainerPayload, PngItxtDeflate)
    {
        std::vector<std::byte> png = {
            std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
            std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
            std::byte { 0x1A }, std::byte { 0x0A },
        };

        const std::string_view xml = "<xmp/>";

        uLongf comp_cap = compressBound(static_cast<uLong>(xml.size()));
        std::vector<std::byte> comp(static_cast<size_t>(comp_cap));
        const int zres
            = compress2(reinterpret_cast<Bytef*>(comp.data()), &comp_cap,
                        reinterpret_cast<const Bytef*>(xml.data()),
                        static_cast<uLong>(xml.size()), Z_BEST_COMPRESSION);
        ASSERT_EQ(zres, Z_OK);
        comp.resize(static_cast<size_t>(comp_cap));

        std::vector<std::byte> itxt;
        append_bytes(&itxt, "XML:com.adobe.xmp");
        itxt.push_back(std::byte { 0x00 });
        itxt.push_back(std::byte { 0x01 });  // compressed
        itxt.push_back(std::byte { 0x00 });  // method=0 (zlib)
        itxt.push_back(std::byte { 0x00 });  // lang
        itxt.push_back(std::byte { 0x00 });  // trans
        itxt.insert(itxt.end(), comp.begin(), comp.end());
        append_png_chunk(&png, fourcc('i', 'T', 'X', 't'), itxt);

        append_png_chunk(&png, fourcc('I', 'E', 'N', 'D'), {});

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult scan = scan_png(png, blocks);
        ASSERT_EQ(scan.status, ScanStatus::Ok);
        ASSERT_EQ(scan.written, 1U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[0].compression, BlockCompression::Deflate);

        std::array<std::byte, 64> out {};
        std::array<uint32_t, 8> scratch {};
        PayloadOptions opts;
        const PayloadResult res
            = extract_payload(png,
                              std::span<const ContainerBlockRef>(blocks.data(),
                                                                 scan.written),
                              0, out, scratch, opts);
        EXPECT_EQ(res.status, PayloadStatus::Ok);
        ASSERT_EQ(res.written, xml.size());
        EXPECT_EQ(std::memcmp(out.data(), xml.data(), xml.size()), 0);
    }
#endif

}  // namespace
}  // namespace openmeta
