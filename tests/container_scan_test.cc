#include "openmeta/container_scan.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

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
        out->push_back(std::byte { static_cast<uint8_t>((f >> 24) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((f >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((f >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((f >> 0) & 0xFF) });
    }


    static void append_bytes(std::vector<std::byte>* out, std::string_view s)
    {
        for (char c : s) {
            out->push_back(std::byte { static_cast<uint8_t>(c) });
        }
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


    TEST(ContainerScan, JpegSegments)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        const std::array<std::byte, 14> exif_payload = {
            std::byte { 'E' },  std::byte { 'x' },  std::byte { 'i' },
            std::byte { 'f' },  std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 'I' },  std::byte { 'I' },  std::byte { 0x2A },
            std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 },
        };
        append_jpeg_segment(&jpeg, 0xFFE1, exif_payload);

        std::vector<std::byte> xmp_payload;
        append_bytes(&xmp_payload, "http://ns.adobe.com/xap/1.0/");
        xmp_payload.push_back(std::byte { 0x00 });
        append_bytes(&xmp_payload, "<xmp/>");
        append_jpeg_segment(&jpeg, 0xFFE1, xmp_payload);

        std::vector<std::byte> icc_payload;
        append_bytes(&icc_payload, "ICC_PROFILE");
        icc_payload.push_back(std::byte { 0x00 });
        icc_payload.push_back(std::byte { 0x01 });
        icc_payload.push_back(std::byte { 0x01 });
        append_bytes(&icc_payload, "ICC");
        append_jpeg_segment(&jpeg, 0xFFE2, icc_payload);

        std::vector<std::byte> ps_payload;
        append_bytes(&ps_payload, "Photoshop 3.0");
        ps_payload.push_back(std::byte { 0x00 });
        append_bytes(&ps_payload, "DATA");
        append_jpeg_segment(&jpeg, 0xFFED, ps_payload);

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_jpeg(jpeg, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 4U);
        ASSERT_EQ(res.needed, 4U);

        EXPECT_EQ(blocks[0].format, ContainerFormat::Jpeg);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].id, 0xFFE1U);
        ASSERT_GE(blocks[0].data_size, 4U);
        EXPECT_EQ(jpeg[blocks[0].data_offset + 0], std::byte { 'I' });
        EXPECT_EQ(jpeg[blocks[0].data_offset + 1], std::byte { 'I' });

        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[1].id, 0xFFE1U);
        ASSERT_GE(blocks[1].data_size, 5U);
        EXPECT_EQ(jpeg[blocks[1].data_offset + 0], std::byte { '<' });

        EXPECT_EQ(blocks[2].kind, ContainerBlockKind::Icc);
        EXPECT_EQ(blocks[2].chunking, BlockChunking::JpegApp2SeqTotal);
        EXPECT_EQ(blocks[2].part_index, 0U);
        EXPECT_EQ(blocks[2].part_count, 1U);

        EXPECT_EQ(blocks[3].kind, ContainerBlockKind::PhotoshopIrB);
        EXPECT_EQ(blocks[3].chunking, BlockChunking::PsIrB8Bim);

        const ScanResult auto_res = scan_auto(jpeg, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 4U);
    }


    static void append_png_chunk(std::vector<std::byte>* out, uint32_t type,
                                 std::span<const std::byte> data)
    {
        append_u32be(out, static_cast<uint32_t>(data.size()));
        append_fourcc(out, type);
        out->insert(out->end(), data.begin(), data.end());
        append_u32be(out, 0);  // crc ignored by scanner
    }


    TEST(ContainerScan, PngChunks)
    {
        std::vector<std::byte> png = {
            std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
            std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
            std::byte { 0x1A }, std::byte { 0x0A },
        };

        // Uncompressed XMP iTXt.
        std::vector<std::byte> itxt0;
        append_bytes(&itxt0, "XML:com.adobe.xmp");
        itxt0.push_back(std::byte { 0x00 });
        itxt0.push_back(std::byte { 0x00 });  // comp flag
        itxt0.push_back(std::byte { 0x00 });  // comp method
        itxt0.push_back(std::byte { 0x00 });  // lang
        itxt0.push_back(std::byte { 0x00 });  // trans
        append_bytes(&itxt0, "<xmp/>");
        append_png_chunk(&png, fourcc('i', 'T', 'X', 't'), itxt0);

        // Compressed XMP iTXt (still dummy data).
        std::vector<std::byte> itxt1;
        append_bytes(&itxt1, "XML:com.adobe.xmp");
        itxt1.push_back(std::byte { 0x00 });
        itxt1.push_back(std::byte { 0x01 });  // comp flag
        itxt1.push_back(std::byte { 0x00 });  // comp method
        itxt1.push_back(std::byte { 0x00 });  // lang
        itxt1.push_back(std::byte { 0x00 });  // trans
        append_bytes(&itxt1, "Z");
        append_png_chunk(&png, fourcc('i', 'T', 'X', 't'), itxt1);

        // iCCP with dummy compressed bytes.
        std::vector<std::byte> iccp;
        append_bytes(&iccp, "icc");
        iccp.push_back(std::byte { 0x00 });
        iccp.push_back(std::byte { 0x00 });
        append_bytes(&iccp, "Z");
        append_png_chunk(&png, fourcc('i', 'C', 'C', 'P'), iccp);

        // eXIf with TIFF header.
        const std::array<std::byte, 8> exif
            = { std::byte { 'I' },  std::byte { 'I' },  std::byte { 0x2A },
                std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
                std::byte { 0x00 }, std::byte { 0x00 } };
        append_png_chunk(&png, fourcc('e', 'X', 'I', 'f'), exif);

        append_png_chunk(&png, fourcc('I', 'E', 'N', 'D'), {});

        std::array<ContainerBlockRef, 16> blocks {};
        const ScanResult res = scan_png(png, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 4U);

        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[0].compression, BlockCompression::None);
        EXPECT_EQ(png[blocks[0].data_offset], std::byte { '<' });

        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[1].compression, BlockCompression::Deflate);

        EXPECT_EQ(blocks[2].kind, ContainerBlockKind::Icc);
        EXPECT_EQ(blocks[2].compression, BlockCompression::Deflate);

        EXPECT_EQ(blocks[3].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(png[blocks[3].data_offset + 0], std::byte { 'I' });

        const ScanResult auto_res = scan_auto(png, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 4U);
    }


    TEST(ContainerScan, WebpRiffChunks)
    {
        std::vector<std::byte> webp;
        append_bytes(&webp, "RIFF");
        append_u32le(&webp, 0);  // placeholder
        append_bytes(&webp, "WEBP");

        std::vector<std::byte> exif;
        append_bytes(&exif, "Exif");
        exif.push_back(std::byte { 0x00 });
        exif.push_back(std::byte { 0x00 });
        append_bytes(&exif, "II");
        exif.push_back(std::byte { 0x2A });
        exif.push_back(std::byte { 0x00 });
        append_u32le(&exif, 8);

        append_fourcc(&webp, fourcc('E', 'X', 'I', 'F'));
        append_u32le(&webp, static_cast<uint32_t>(exif.size()));
        webp.insert(webp.end(), exif.begin(), exif.end());
        if ((exif.size() & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        std::vector<std::byte> xmp;
        append_bytes(&xmp, "<xmp/>");
        append_fourcc(&webp, fourcc('X', 'M', 'P', ' '));
        append_u32le(&webp, static_cast<uint32_t>(xmp.size()));
        webp.insert(webp.end(), xmp.begin(), xmp.end());
        if ((xmp.size() & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        std::vector<std::byte> icc;
        append_bytes(&icc, "ICC");
        append_fourcc(&webp, fourcc('I', 'C', 'C', 'P'));
        append_u32le(&webp, static_cast<uint32_t>(icc.size()));
        webp.insert(webp.end(), icc.begin(), icc.end());
        if ((icc.size() & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        const uint32_t riff_size = static_cast<uint32_t>(webp.size() - 8);
        webp[4] = std::byte { static_cast<uint8_t>((riff_size >> 0) & 0xFF) };
        webp[5] = std::byte { static_cast<uint8_t>((riff_size >> 8) & 0xFF) };
        webp[6] = std::byte { static_cast<uint8_t>((riff_size >> 16) & 0xFF) };
        webp[7] = std::byte { static_cast<uint8_t>((riff_size >> 24) & 0xFF) };

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_webp(webp, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 3U);

        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(webp[blocks[0].data_offset], std::byte { 'I' });
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[2].kind, ContainerBlockKind::Icc);
    }


    TEST(ContainerScan, GifApplicationExtensions)
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
        const ScanResult res = scan_gif(gif, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[0].chunking, BlockChunking::GifSubBlocks);
        EXPECT_EQ(gif[blocks[0].data_offset], std::byte { 0x03 });
    }


    TEST(ContainerScan, Jp2AndJxlBoxes)
    {
        std::vector<std::byte> jp2;
        append_u32be(&jp2, 12);
        append_fourcc(&jp2, fourcc('j', 'P', ' ', ' '));
        append_u32be(&jp2, 0x0D0A870A);

        // jp2h with colr (method=2 + ICC bytes).
        std::vector<std::byte> colr;
        colr.push_back(std::byte { 0x02 });
        colr.push_back(std::byte { 0x00 });
        colr.push_back(std::byte { 0x00 });
        append_bytes(&colr, "ICC");

        std::vector<std::byte> colr_box;
        append_u32be(&colr_box, static_cast<uint32_t>(8 + colr.size()));
        append_fourcc(&colr_box, fourcc('c', 'o', 'l', 'r'));
        colr_box.insert(colr_box.end(), colr.begin(), colr.end());

        std::vector<std::byte> jp2h_box;
        append_u32be(&jp2h_box, static_cast<uint32_t>(8 + colr_box.size()));
        append_fourcc(&jp2h_box, fourcc('j', 'p', '2', 'h'));
        jp2h_box.insert(jp2h_box.end(), colr_box.begin(), colr_box.end());
        jp2.insert(jp2.end(), jp2h_box.begin(), jp2h_box.end());

        // uuid (XMP) box.
        const std::array<std::byte, 16> xmp_uuid = {
            std::byte { 0xbe }, std::byte { 0x7a }, std::byte { 0xcf },
            std::byte { 0xcb }, std::byte { 0x97 }, std::byte { 0xa9 },
            std::byte { 0x42 }, std::byte { 0xe8 }, std::byte { 0x9c },
            std::byte { 0x71 }, std::byte { 0x99 }, std::byte { 0x94 },
            std::byte { 0x91 }, std::byte { 0xe3 }, std::byte { 0xaf },
            std::byte { 0xac },
        };
        std::vector<std::byte> uuid_payload;
        uuid_payload.insert(uuid_payload.end(), xmp_uuid.begin(),
                            xmp_uuid.end());
        append_bytes(&uuid_payload, "<xmp/>");
        append_u32be(&jp2, static_cast<uint32_t>(8 + uuid_payload.size()));
        append_fourcc(&jp2, fourcc('u', 'u', 'i', 'd'));
        jp2.insert(jp2.end(), uuid_payload.begin(), uuid_payload.end());

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult jp2_res = scan_jp2(jp2, blocks);
        ASSERT_EQ(jp2_res.status, ScanStatus::Ok);
        ASSERT_EQ(jp2_res.written, 2U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Icc);
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);

        // JXL container: signature + Exif + xml + brob(xml).
        std::vector<std::byte> jxl;
        append_u32be(&jxl, 12);
        append_fourcc(&jxl, fourcc('J', 'X', 'L', ' '));
        append_u32be(&jxl, 0x0D0A870A);

        std::vector<std::byte> exif_box_payload;
        append_u32be(&exif_box_payload, 0);
        append_bytes(&exif_box_payload, "II");
        exif_box_payload.push_back(std::byte { 0x2A });
        exif_box_payload.push_back(std::byte { 0x00 });
        append_u32le(&exif_box_payload, 8);
        append_u32be(&jxl, static_cast<uint32_t>(8 + exif_box_payload.size()));
        append_fourcc(&jxl, fourcc('E', 'x', 'i', 'f'));
        jxl.insert(jxl.end(), exif_box_payload.begin(), exif_box_payload.end());

        std::vector<std::byte> xml_payload;
        append_bytes(&xml_payload, "<xmp/>");
        append_u32be(&jxl, static_cast<uint32_t>(8 + xml_payload.size()));
        append_fourcc(&jxl, fourcc('x', 'm', 'l', ' '));
        jxl.insert(jxl.end(), xml_payload.begin(), xml_payload.end());

        std::vector<std::byte> brob_payload;
        append_fourcc(&brob_payload, fourcc('x', 'm', 'l', ' '));
        append_bytes(&brob_payload, "zzz");
        append_u32be(&jxl, static_cast<uint32_t>(8 + brob_payload.size()));
        append_fourcc(&jxl, fourcc('b', 'r', 'o', 'b'));
        jxl.insert(jxl.end(), brob_payload.begin(), brob_payload.end());

        const ScanResult jxl_res = scan_jxl(jxl, blocks);
        ASSERT_EQ(jxl_res.status, ScanStatus::Ok);
        ASSERT_EQ(jxl_res.written, 3U);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].chunking, BlockChunking::BmffExifTiffOffsetU32Be);
        EXPECT_EQ(blocks[0].aux_u32, 0U);
        EXPECT_EQ(jxl[blocks[0].data_offset], std::byte { 'I' });
        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[2].kind, ContainerBlockKind::CompressedMetadata);
        EXPECT_EQ(blocks[2].compression, BlockCompression::Brotli);
        EXPECT_EQ(blocks[2].aux_u32, fourcc('x', 'm', 'l', ' '));
    }


    TEST(ContainerScan, BmffMetaItems)
    {
        // Build a tiny ISO-BMFF file with Exif + XMP items stored in `meta`/`idat`.
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "exif");
        infe_payload.push_back(std::byte { 0x00 });

        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> infe_xmp_payload;
        append_fullbox_header(&infe_xmp_payload, 2);
        append_u16be(&infe_xmp_payload, 2);  // item_ID
        append_u16be(&infe_xmp_payload, 0);  // protection
        append_fourcc(&infe_xmp_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_xmp_payload, "xmp");
        infe_xmp_payload.push_back(std::byte { 0x00 });
        append_bytes(&infe_xmp_payload, "application/rdf+xml");
        infe_xmp_payload.push_back(std::byte { 0x00 });
        // Some real-world files omit the encoding terminator (treat as empty).

        std::vector<std::byte> infe_xmp_box;
        append_bmff_box(&infe_xmp_box, fourcc('i', 'n', 'f', 'e'),
                        infe_xmp_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 2);  // entry_count
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        iinf_payload.insert(iinf_payload.end(), infe_xmp_box.begin(),
                            infe_xmp_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> idat_payload;
        append_u32be(&idat_payload, 0);  // TIFF header offset
        append_bytes(&idat_payload, "II");
        idat_payload.push_back(std::byte { 0x2A });
        idat_payload.push_back(std::byte { 0x00 });
        append_u32le(&idat_payload, 8);
        const uint32_t xmp_off = static_cast<uint32_t>(idat_payload.size());
        append_bytes(&idat_payload, "<xmp/>");
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 2);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload, 1);  // construction_method=1 (idat)
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset (within idat)
        append_u32be(&iloc_payload, xmp_off);

        append_u16be(&iloc_payload, 2);        // item_ID
        append_u16be(&iloc_payload, 1);        // construction_method=1 (idat)
        append_u16be(&iloc_payload, 0);        // data_reference_index
        append_u16be(&iloc_payload, 1);        // extent_count
        append_u32be(&iloc_payload, xmp_off);  // extent_offset (within idat)
        append_u32be(&iloc_payload,
                     static_cast<uint32_t>(idat_payload.size() - xmp_off));
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

        struct Case final {
            uint32_t major_brand       = 0;
            ContainerFormat expect_fmt = ContainerFormat::Unknown;
        };

        const std::array<Case, 3> cases = {
            Case { fourcc('h', 'e', 'i', 'c'), ContainerFormat::Heif },
            Case { fourcc('a', 'v', 'i', 'f'), ContainerFormat::Avif },
            Case { fourcc('c', 'r', 'x', ' '), ContainerFormat::Cr3 },
        };

        for (const Case& c : cases) {
            std::vector<std::byte> ftyp_payload;
            append_fourcc(&ftyp_payload, c.major_brand);
            append_u32be(&ftyp_payload, 0);
            append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
            std::vector<std::byte> file;
            append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
            file.insert(file.end(), meta_box.begin(), meta_box.end());

            std::array<ContainerBlockRef, 8> blocks {};
            const ScanResult res = scan_bmff(file, blocks);
            ASSERT_EQ(res.status, ScanStatus::Ok);
            ASSERT_EQ(res.written, 2U);

            const ContainerBlockRef* exif_block = nullptr;
            const ContainerBlockRef* xmp_block  = nullptr;
            for (uint32_t bi = 0; bi < res.written; ++bi) {
                if (blocks[bi].kind == ContainerBlockKind::Exif) {
                    exif_block = &blocks[bi];
                } else if (blocks[bi].kind == ContainerBlockKind::Xmp) {
                    xmp_block = &blocks[bi];
                }
            }
            ASSERT_NE(exif_block, nullptr);
            ASSERT_NE(xmp_block, nullptr);

            EXPECT_EQ(exif_block->format, c.expect_fmt);
            EXPECT_EQ(exif_block->chunking,
                      BlockChunking::BmffExifTiffOffsetU32Be);
            EXPECT_EQ(exif_block->aux_u32, 0U);
            EXPECT_EQ(file[exif_block->data_offset], std::byte { 'I' });

            EXPECT_EQ(xmp_block->format, c.expect_fmt);
            EXPECT_EQ(file[xmp_block->data_offset], std::byte { '<' });

            const ScanResult auto_res = scan_auto(file, blocks);
            EXPECT_EQ(auto_res.status, ScanStatus::Ok);
            EXPECT_EQ(auto_res.written, 2U);
        }
    }


    TEST(ContainerScan, Cr3CanonUuidCmtBoxes)
    {
        // Minimal CR3-style BMFF: `ftyp` + `moov/uuid(Canon)` containing `CMT1` TIFF.
        std::vector<std::byte> file;

        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('c', 'r', 'x', ' '));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('i', 's', 'o', 'm'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'),
                        std::span<const std::byte>(ftyp_payload.data(),
                                                   ftyp_payload.size()));

        std::vector<std::byte> cmt_payload;
        const std::array<std::byte, 4> tiff_hdr = {
            std::byte { 'I' },
            std::byte { 'I' },
            std::byte { 0x2A },
            std::byte { 0x00 },
        };
        cmt_payload.insert(cmt_payload.end(), tiff_hdr.begin(), tiff_hdr.end());
        append_u32le(&cmt_payload, 8);
        std::vector<std::byte> cmt_box;
        append_bmff_box(&cmt_box, fourcc('C', 'M', 'T', '1'),
                        std::span<const std::byte>(cmt_payload.data(),
                                                   cmt_payload.size()));

        std::vector<std::byte> uuid_box;
        const std::array<std::byte, 16> canon_uuid = {
            std::byte { 0x85 }, std::byte { 0xc0 }, std::byte { 0xb6 },
            std::byte { 0x87 }, std::byte { 0x82 }, std::byte { 0x0f },
            std::byte { 0x11 }, std::byte { 0xe0 }, std::byte { 0x81 },
            std::byte { 0x11 }, std::byte { 0xf4 }, std::byte { 0xce },
            std::byte { 0x46 }, std::byte { 0x2b }, std::byte { 0x6a },
            std::byte { 0x48 },
        };
        append_u32be(&uuid_box, static_cast<uint32_t>(8 + 16 + cmt_box.size()));
        append_fourcc(&uuid_box, fourcc('u', 'u', 'i', 'd'));
        uuid_box.insert(uuid_box.end(), canon_uuid.begin(), canon_uuid.end());
        uuid_box.insert(uuid_box.end(), cmt_box.begin(), cmt_box.end());

        std::vector<std::byte> moov_payload;
        moov_payload.insert(moov_payload.end(), uuid_box.begin(),
                            uuid_box.end());
        append_bmff_box(&file, fourcc('m', 'o', 'o', 'v'),
                        std::span<const std::byte>(moov_payload.data(),
                                                   moov_payload.size()));

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_bmff(file, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 1U);
        EXPECT_EQ(blocks[0].format, ContainerFormat::Cr3);
        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].id, fourcc('C', 'M', 'T', '1'));
        ASSERT_GE(blocks[0].data_size, 4U);
        EXPECT_EQ(file[blocks[0].data_offset], std::byte { 'I' });

        const ScanResult auto_res = scan_auto(file, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 1U);
    }


    TEST(ContainerScan, TiffTagValues)
    {
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        tiff.push_back(std::byte { 0x2A });
        tiff.push_back(std::byte { 0x00 });
        append_u32le(&tiff, 8);

        // IFD0 at offset 8 with two entries.
        tiff.push_back(std::byte { 0x02 });
        tiff.push_back(std::byte { 0x00 });

        // XMP tag 0x02BC, type BYTE(1), count 5, offset to value (38).
        tiff.push_back(std::byte { 0xBC });
        tiff.push_back(std::byte { 0x02 });
        tiff.push_back(std::byte { 0x01 });
        tiff.push_back(std::byte { 0x00 });
        append_u32le(&tiff, 5);
        append_u32le(&tiff, 38);

        // ICC tag 0x8773, type UNDEFINED(7), count 4 inline ("ABCD").
        tiff.push_back(std::byte { 0x73 });
        tiff.push_back(std::byte { 0x87 });
        tiff.push_back(std::byte { 0x07 });
        tiff.push_back(std::byte { 0x00 });
        append_u32le(&tiff, 4);
        append_bytes(&tiff, "ABCD");

        // Next IFD offset = 0.
        append_u32le(&tiff, 0);

        // XMP value bytes at offset 38.
        ASSERT_EQ(tiff.size(), 38U);
        append_bytes(&tiff, "<xmp>");

        std::array<ContainerBlockRef, 8> blocks {};
        const ScanResult res = scan_tiff(tiff, blocks);
        ASSERT_EQ(res.status, ScanStatus::Ok);
        ASSERT_EQ(res.written, 3U);

        EXPECT_EQ(blocks[0].kind, ContainerBlockKind::Exif);
        EXPECT_EQ(blocks[0].data_offset, 0U);
        EXPECT_EQ(blocks[0].data_size, tiff.size());

        EXPECT_EQ(blocks[1].kind, ContainerBlockKind::Xmp);
        EXPECT_EQ(blocks[1].data_size, 5U);
        EXPECT_EQ(tiff[blocks[1].data_offset + 0], std::byte { '<' });
        EXPECT_EQ(blocks[2].kind, ContainerBlockKind::Icc);
        EXPECT_EQ(tiff[blocks[2].data_offset + 0], std::byte { 'A' });

        const ScanResult auto_res = scan_auto(tiff, blocks);
        EXPECT_EQ(auto_res.status, ScanStatus::Ok);
        EXPECT_EQ(auto_res.written, 3U);
    }

}  // namespace
}  // namespace openmeta
