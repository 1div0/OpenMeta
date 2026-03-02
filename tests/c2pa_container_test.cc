#include "openmeta/simple_meta.h"

#include "openmeta/meta_key.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI \
    && defined(OPENMETA_HAS_BROTLI_ENCODER) && OPENMETA_HAS_BROTLI_ENCODER
#    include <brotli/encode.h>
#endif

namespace openmeta {
namespace {

    static void append_u16be(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFFU) });
    }

    static void append_u32be(std::vector<std::byte>* out, uint32_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFFU) });
    }

    static void append_u32le(std::vector<std::byte>* out, uint32_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFFU) });
    }

    static void append_fourcc(std::vector<std::byte>* out, uint32_t f)
    {
        out->push_back(std::byte { static_cast<uint8_t>((f >> 24) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((f >> 16) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((f >> 8) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((f >> 0) & 0xFFU) });
    }

    static void append_bytes(std::vector<std::byte>* out, std::string_view s)
    {
        for (char c : s) {
            out->push_back(std::byte { static_cast<uint8_t>(c) });
        }
    }

    static void append_bmff_box(std::vector<std::byte>* out, uint32_t type,
                                std::span<const std::byte> payload)
    {
        append_u32be(out, static_cast<uint32_t>(8U + payload.size()));
        append_fourcc(out, type);
        out->insert(out->end(), payload.begin(), payload.end());
    }

    static void append_jpeg_segment(std::vector<std::byte>* out,
                                    uint16_t marker,
                                    std::span<const std::byte> payload)
    {
        out->push_back(std::byte { 0xFF });
        out->push_back(std::byte { static_cast<uint8_t>(marker & 0xFFU) });
        const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
        append_u16be(out, seg_len);
        out->insert(out->end(), payload.begin(), payload.end());
    }

    static void append_png_chunk(std::vector<std::byte>* out, uint32_t type,
                                 std::span<const std::byte> data)
    {
        append_u32be(out, static_cast<uint32_t>(data.size()));
        append_fourcc(out, type);
        out->insert(out->end(), data.begin(), data.end());
        append_u32be(out, 0);  // crc ignored by scanner
    }

    static std::vector<std::byte> make_sample_jumbf_payload()
    {
        // cbor payload: { "a": 1 }
        const std::vector<std::byte> cbor_payload = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { 0x01 },
        };

        std::vector<std::byte> jumd_payload;
        append_bytes(&jumd_payload, "c2pa");
        jumd_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> jumd_box;
        append_bmff_box(&jumd_box, fourcc('j', 'u', 'm', 'd'), jumd_payload);

        std::vector<std::byte> cbor_box;
        append_bmff_box(&cbor_box, fourcc('c', 'b', 'o', 'r'), cbor_payload);

        std::vector<std::byte> jumb_payload;
        jumb_payload.insert(jumb_payload.end(), jumd_box.begin(),
                            jumd_box.end());
        jumb_payload.insert(jumb_payload.end(), cbor_box.begin(),
                            cbor_box.end());

        std::vector<std::byte> jumb_box;
        append_bmff_box(&jumb_box, fourcc('j', 'u', 'm', 'b'), jumb_payload);
        return jumb_box;
    }

    static bool store_has_jumbf_cbor_key(const MetaStore& store,
                                         std::string_view key)
    {
        MetaKeyView k;
        k.kind                    = MetaKeyKind::JumbfCborKey;
        k.data.jumbf_cbor_key.key = key;
        return !store.find_all(k).empty();
    }

    static bool store_has_jumbf_field(const MetaStore& store,
                                      std::string_view field)
    {
        MetaKeyView k;
        k.kind                      = MetaKeyKind::JumbfField;
        k.data.jumbf_field.field    = field;
        return !store.find_all(k).empty();
    }


    TEST(C2paContainers, JpegApp11MultiPartJumbfIntegrated)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        const std::vector<std::byte> jumb_box = make_sample_jumbf_payload();
        ASSERT_GT(jumb_box.size(), 8U);

        const std::span<const std::byte> jumb_payload(jumb_box.data() + 8U,
                                                      jumb_box.size() - 8U);
        const size_t mid = jumb_payload.size() / 2U;

        for (uint32_t part = 0; part < 2U; ++part) {
            std::vector<std::byte> seg;
            append_bytes(&seg, "JP");
            seg.push_back(std::byte { 0x00 });
            seg.push_back(std::byte { 0x00 });
            // 1-based sequence numbering: 1, 2.
            append_u32be(&seg, part + 1U);
            // BMFF box header (size/type) repeated per segment.
            seg.insert(seg.end(), jumb_box.begin(), jumb_box.begin() + 8);

            const size_t start = (part == 0U) ? 0U : mid;
            const size_t end   = (part == 0U) ? mid : jumb_payload.size();
            seg.insert(seg.end(), jumb_payload.begin() + static_cast<ptrdiff_t>(start),
                       jumb_payload.begin() + static_cast<ptrdiff_t>(end));

            append_jpeg_segment(&jpeg, 0xFFEB, seg);
        }

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 16> ifds {};
        std::array<std::byte, 8192> payload {};
        std::array<uint32_t, 128> payload_parts {};
        SimpleMetaDecodeOptions options;

        const SimpleMetaResult read
            = simple_meta_read(jpeg, store, blocks, ifds, payload,
                               payload_parts, options);
        EXPECT_EQ(read.scan.status, ScanStatus::Ok);
        EXPECT_EQ(read.jumbf.status, JumbfDecodeStatus::Ok);
        EXPECT_GT(read.jumbf.entries_decoded, 0U);

        store.finalize();
        EXPECT_TRUE(store_has_jumbf_field(store, "c2pa.detected"));
        EXPECT_TRUE(store_has_jumbf_cbor_key(store, "box.0.1.cbor.a"));
    }

    TEST(C2paContainers, JpegApp11HeaderOnlyFirstSegmentIntegrated)
    {
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        const std::vector<std::byte> jumb_box = make_sample_jumbf_payload();
        ASSERT_GT(jumb_box.size(), 8U);

        const std::span<const std::byte> jumb_payload(jumb_box.data() + 8U,
                                                      jumb_box.size() - 8U);

        // Part 1: APP11 preamble + BMFF header only.
        std::vector<std::byte> seg1;
        append_bytes(&seg1, "JP");
        seg1.push_back(std::byte { 0x00 });
        seg1.push_back(std::byte { 0x00 });
        append_u32be(&seg1, 1U);
        seg1.insert(seg1.end(), jumb_box.begin(), jumb_box.begin() + 8);
        append_jpeg_segment(&jpeg, 0xFFEB, seg1);

        // Part 2: APP11 preamble + same BMFF header + full payload bytes.
        std::vector<std::byte> seg2;
        append_bytes(&seg2, "JP");
        seg2.push_back(std::byte { 0x00 });
        seg2.push_back(std::byte { 0x00 });
        append_u32be(&seg2, 2U);
        seg2.insert(seg2.end(), jumb_box.begin(), jumb_box.begin() + 8);
        seg2.insert(seg2.end(), jumb_payload.begin(), jumb_payload.end());
        append_jpeg_segment(&jpeg, 0xFFEB, seg2);

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 16> ifds {};
        std::array<std::byte, 8192> payload {};
        std::array<uint32_t, 128> payload_parts {};
        SimpleMetaDecodeOptions options;

        const SimpleMetaResult read
            = simple_meta_read(jpeg, store, blocks, ifds, payload,
                               payload_parts, options);
        EXPECT_EQ(read.scan.status, ScanStatus::Ok);
        EXPECT_EQ(read.jumbf.status, JumbfDecodeStatus::Ok);
        EXPECT_GT(read.jumbf.entries_decoded, 0U);

        store.finalize();
        EXPECT_TRUE(store_has_jumbf_field(store, "c2pa.detected"));
        EXPECT_TRUE(store_has_jumbf_cbor_key(store, "box.0.1.cbor.a"));
    }


    TEST(C2paContainers, PngCaBxJumbfIntegrated)
    {
        std::vector<std::byte> png = {
            std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
            std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
            std::byte { 0x1A }, std::byte { 0x0A },
        };

        const std::vector<std::byte> jumb_box = make_sample_jumbf_payload();
        append_png_chunk(&png, fourcc('c', 'a', 'B', 'X'), jumb_box);
        append_png_chunk(&png, fourcc('I', 'E', 'N', 'D'),
                         std::span<const std::byte> {});

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 16> ifds {};
        std::array<std::byte, 8192> payload {};
        std::array<uint32_t, 128> payload_parts {};
        SimpleMetaDecodeOptions options;

        const SimpleMetaResult read = simple_meta_read(
            png, store, blocks, ifds, payload, payload_parts, options);
        EXPECT_EQ(read.scan.status, ScanStatus::Ok);
        EXPECT_EQ(read.jumbf.status, JumbfDecodeStatus::Ok);

        store.finalize();
        EXPECT_TRUE(store_has_jumbf_field(store, "c2pa.detected"));
        EXPECT_TRUE(store_has_jumbf_cbor_key(store, "box.0.1.cbor.a"));
    }

    TEST(C2paContainers, PngCaBxSplitChunksIntegrated)
    {
        std::vector<std::byte> png = {
            std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
            std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
            std::byte { 0x1A }, std::byte { 0x0A },
        };

        const std::vector<std::byte> jumb_box = make_sample_jumbf_payload();
        ASSERT_GT(jumb_box.size(), 8U);
        const size_t mid = jumb_box.size() / 2U;

        append_png_chunk(&png, fourcc('c', 'a', 'B', 'X'),
                         std::span<const std::byte>(jumb_box.data(), mid));
        append_png_chunk(
            &png, fourcc('c', 'a', 'B', 'X'),
            std::span<const std::byte>(jumb_box.data() + static_cast<ptrdiff_t>(mid),
                                       jumb_box.size() - mid));
        append_png_chunk(&png, fourcc('I', 'E', 'N', 'D'),
                         std::span<const std::byte> {});

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 16> ifds {};
        std::array<std::byte, 8192> payload {};
        std::array<uint32_t, 128> payload_parts {};
        SimpleMetaDecodeOptions options;

        const SimpleMetaResult read = simple_meta_read(
            png, store, blocks, ifds, payload, payload_parts, options);
        EXPECT_EQ(read.scan.status, ScanStatus::Ok);
        EXPECT_EQ(read.jumbf.status, JumbfDecodeStatus::Ok);

        store.finalize();
        EXPECT_TRUE(store_has_jumbf_field(store, "c2pa.detected"));
        EXPECT_TRUE(store_has_jumbf_cbor_key(store, "box.0.1.cbor.a"));
    }


    TEST(C2paContainers, WebpC2paChunkIntegrated)
    {
        // Minimal RIFF/WEBP with a single "C2PA" chunk containing JUMBF bytes.
        const std::vector<std::byte> jumb_box = make_sample_jumbf_payload();

        std::vector<std::byte> webp;
        append_bytes(&webp, "RIFF");
        append_u32le(&webp, 0);  // filled later
        append_bytes(&webp, "WEBP");

        append_bytes(&webp, "C2PA");
        append_u32le(&webp, static_cast<uint32_t>(jumb_box.size()));
        webp.insert(webp.end(), jumb_box.begin(), jumb_box.end());
        if ((jumb_box.size() & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        const uint32_t riff_size
            = static_cast<uint32_t>((webp.size() >= 8U) ? (webp.size() - 8U)
                                                        : 0U);
        webp[4] = std::byte { static_cast<uint8_t>((riff_size >> 0) & 0xFFU) };
        webp[5] = std::byte { static_cast<uint8_t>((riff_size >> 8) & 0xFFU) };
        webp[6] = std::byte { static_cast<uint8_t>((riff_size >> 16) & 0xFFU) };
        webp[7] = std::byte { static_cast<uint8_t>((riff_size >> 24) & 0xFFU) };

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 16> ifds {};
        std::array<std::byte, 8192> payload {};
        std::array<uint32_t, 128> payload_parts {};
        SimpleMetaDecodeOptions options;

        const SimpleMetaResult read = simple_meta_read(
            webp, store, blocks, ifds, payload, payload_parts, options);
        EXPECT_EQ(read.scan.status, ScanStatus::Ok);
        EXPECT_EQ(read.jumbf.status, JumbfDecodeStatus::Ok);

        store.finalize();
        EXPECT_TRUE(store_has_jumbf_field(store, "c2pa.detected"));
        EXPECT_TRUE(store_has_jumbf_cbor_key(store, "box.0.1.cbor.a"));
    }

    TEST(C2paContainers, WebpC2paSplitChunksIntegrated)
    {
        const std::vector<std::byte> jumb_box = make_sample_jumbf_payload();
        ASSERT_GT(jumb_box.size(), 8U);
        const size_t mid = jumb_box.size() / 2U;

        std::vector<std::byte> webp;
        append_bytes(&webp, "RIFF");
        append_u32le(&webp, 0);
        append_bytes(&webp, "WEBP");

        append_bytes(&webp, "C2PA");
        append_u32le(&webp, static_cast<uint32_t>(mid));
        webp.insert(webp.end(), jumb_box.begin(),
                    jumb_box.begin() + static_cast<ptrdiff_t>(mid));
        if ((mid & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        append_bytes(&webp, "C2PA");
        append_u32le(&webp, static_cast<uint32_t>(jumb_box.size() - mid));
        webp.insert(
            webp.end(), jumb_box.begin() + static_cast<ptrdiff_t>(mid),
            jumb_box.end());
        if (((jumb_box.size() - mid) & 1U) != 0U) {
            webp.push_back(std::byte { 0x00 });
        }

        const uint32_t riff_size
            = static_cast<uint32_t>((webp.size() >= 8U) ? (webp.size() - 8U)
                                                        : 0U);
        webp[4] = std::byte { static_cast<uint8_t>((riff_size >> 0) & 0xFFU) };
        webp[5] = std::byte { static_cast<uint8_t>((riff_size >> 8) & 0xFFU) };
        webp[6] = std::byte { static_cast<uint8_t>((riff_size >> 16) & 0xFFU) };
        webp[7] = std::byte { static_cast<uint8_t>((riff_size >> 24) & 0xFFU) };

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 16> ifds {};
        std::array<std::byte, 8192> payload {};
        std::array<uint32_t, 128> payload_parts {};
        SimpleMetaDecodeOptions options;

        const SimpleMetaResult read = simple_meta_read(
            webp, store, blocks, ifds, payload, payload_parts, options);
        EXPECT_EQ(read.scan.status, ScanStatus::Ok);
        EXPECT_EQ(read.jumbf.status, JumbfDecodeStatus::Ok);

        store.finalize();
        EXPECT_TRUE(store_has_jumbf_field(store, "c2pa.detected"));
        EXPECT_TRUE(store_has_jumbf_cbor_key(store, "box.0.1.cbor.a"));
    }


#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI \
    && defined(OPENMETA_HAS_BROTLI_ENCODER) && OPENMETA_HAS_BROTLI_ENCODER

    static std::vector<std::byte>
    brotli_compress(std::span<const std::byte> input)
    {
        const size_t max_out = BrotliEncoderMaxCompressedSize(input.size());
        std::vector<uint8_t> out(max_out);
        size_t out_size = out.size();

        const int quality = 4;
        const int lgwin   = 22;
        const BrotliEncoderMode mode = BROTLI_MODE_GENERIC;

        const auto* in_u8 = reinterpret_cast<const uint8_t*>(input.data());
        const bool ok = BrotliEncoderCompress(
            quality, lgwin, mode, input.size(), in_u8, &out_size, out.data());
        if (!ok) {
            return {};
        }
        std::vector<std::byte> bytes(out_size);
        for (size_t i = 0; i < out_size; ++i) {
            bytes[i] = std::byte { out[i] };
        }
        return bytes;
    }

    TEST(C2paContainers, JxlBrobDispatchesJumbf)
    {
        // brob(realtype=jumb) where the compressed bytes contain the *payload*
        // of a jumb box (i.e. nested boxes starting with jumd).
        const std::vector<std::byte> jumb_box = make_sample_jumbf_payload();
        ASSERT_GT(jumb_box.size(), 8U);

        const std::span<const std::byte> jumb_payload(jumb_box.data() + 8U,
                                                      jumb_box.size() - 8U);
        const std::vector<std::byte> brotli = brotli_compress(jumb_payload);
        ASSERT_FALSE(brotli.empty());

        std::vector<std::byte> brob_payload;
        append_fourcc(&brob_payload, fourcc('j', 'u', 'm', 'b'));
        brob_payload.insert(brob_payload.end(), brotli.begin(), brotli.end());

        std::vector<std::byte> jxl;
        append_u32be(&jxl, 12);
        append_fourcc(&jxl, fourcc('J', 'X', 'L', ' '));
        append_u32be(&jxl, 0x0D0A870A);
        append_bmff_box(&jxl, fourcc('b', 'r', 'o', 'b'), brob_payload);

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 16> ifds {};
        std::array<std::byte, 65536> payload {};
        std::array<uint32_t, 128> payload_parts {};
        SimpleMetaDecodeOptions options;

        const SimpleMetaResult read
            = simple_meta_read(jxl, store, blocks, ifds, payload,
                               payload_parts, options);
        EXPECT_EQ(read.scan.status, ScanStatus::Ok);
        EXPECT_EQ(read.jumbf.status, JumbfDecodeStatus::Ok);

        store.finalize();
        EXPECT_TRUE(store_has_jumbf_field(store, "c2pa.detected"));
        EXPECT_TRUE(store_has_jumbf_cbor_key(store, "box.1.cbor.a"));
    }

#else

    TEST(C2paContainers, JxlBrobDispatchesJumbf)
    {
        GTEST_SKIP() << "Brotli decode+encode support is not enabled.";
    }

#endif


#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI \
    && defined(OPENMETA_HAS_BROTLI_ENCODER) && OPENMETA_HAS_BROTLI_ENCODER \
    && defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT

    TEST(C2paContainers, JxlBrobDispatchesXmp)
    {
        const std::string xmp
            = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
              "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
              "<rdf:Description "
              "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
              "xmp:CreatorTool='OpenMeta'/>"
              "</rdf:RDF>"
              "</x:xmpmeta>";

        const std::span<const std::byte> xmp_bytes(
            reinterpret_cast<const std::byte*>(xmp.data()), xmp.size());
        const std::vector<std::byte> brotli = brotli_compress(xmp_bytes);
        ASSERT_FALSE(brotli.empty());

        std::vector<std::byte> brob_payload;
        append_fourcc(&brob_payload, fourcc('x', 'm', 'l', ' '));
        brob_payload.insert(brob_payload.end(), brotli.begin(), brotli.end());

        std::vector<std::byte> jxl;
        append_u32be(&jxl, 12);
        append_fourcc(&jxl, fourcc('J', 'X', 'L', ' '));
        append_u32be(&jxl, 0x0D0A870A);
        append_bmff_box(&jxl, fourcc('b', 'r', 'o', 'b'), brob_payload);

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 16> ifds {};
        std::array<std::byte, 65536> payload {};
        std::array<uint32_t, 128> payload_parts {};
        SimpleMetaDecodeOptions options;

        const SimpleMetaResult read
            = simple_meta_read(jxl, store, blocks, ifds, payload,
                               payload_parts, options);
        EXPECT_EQ(read.scan.status, ScanStatus::Ok);
        EXPECT_EQ(read.xmp.status, XmpDecodeStatus::Ok);
        EXPECT_GT(read.xmp.entries_decoded, 0U);
    }

#endif

}  // namespace
}  // namespace openmeta
