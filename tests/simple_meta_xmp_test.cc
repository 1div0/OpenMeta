// SPDX-License-Identifier: Apache-2.0

#include "openmeta/simple_meta.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT
namespace {

    static void append_bytes(std::string_view s, std::vector<std::byte>* out)
    {
        for (size_t i = 0; i < s.size(); ++i) {
            out->push_back(std::byte { static_cast<unsigned char>(s[i]) });
        }
    }


    static void append_u16be(uint16_t v, std::vector<std::byte>* out)
    {
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<unsigned char>(v & 0xFF) });
    }


    static void put_u16le(uint16_t v, size_t off, std::vector<std::byte>* out)
    {
        (*out)[off + 0U] = std::byte { static_cast<unsigned char>(v & 0xFF) };
        (*out)[off + 1U]
            = std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) };
    }


    static void put_u32le(uint32_t v, size_t off, std::vector<std::byte>* out)
    {
        (*out)[off + 0U] = std::byte { static_cast<unsigned char>(v & 0xFF) };
        (*out)[off + 1U]
            = std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) };
        (*out)[off + 2U]
            = std::byte { static_cast<unsigned char>((v >> 16) & 0xFF) };
        (*out)[off + 3U]
            = std::byte { static_cast<unsigned char>((v >> 24) & 0xFF) };
    }


    static void put_tiff_entry(uint16_t tag, uint16_t type, uint32_t count,
                               uint32_t value_or_offset, size_t off,
                               std::vector<std::byte>* out)
    {
        put_u16le(tag, off + 0U, out);
        put_u16le(type, off + 2U, out);
        put_u32le(count, off + 4U, out);
        put_u32le(value_or_offset, off + 8U, out);
    }


    static void append_jpeg_segment(uint8_t marker,
                                    std::span<const std::byte> payload,
                                    std::vector<std::byte>* out)
    {
        out->push_back(std::byte { 0xFF });
        out->push_back(std::byte { marker });
        append_u16be(static_cast<uint16_t>(payload.size() + 2U), out);
        out->insert(out->end(), payload.begin(), payload.end());
    }


    static std::vector<std::byte> make_minimal_icc()
    {
        std::vector<std::byte> icc(132, std::byte { 0x00 });
        icc[36] = std::byte { 'a' };
        icc[37] = std::byte { 'c' };
        icc[38] = std::byte { 's' };
        icc[39] = std::byte { 'p' };
        put_u32le(0U, 128U, &icc);
        // The ICC header is big-endian; only the size field matters for this
        // smoke path, and a zero size is accepted by the decoder.
        return icc;
    }


    static std::vector<std::byte>
    make_simple_meta_jpeg(std::span<const std::byte> exif_payload)
    {
        std::vector<std::byte> out;
        out.push_back(std::byte { 0xFF });
        out.push_back(std::byte { 0xD8 });
        append_jpeg_segment(0xE1U, exif_payload, &out);
        out.push_back(std::byte { 0xFF });
        out.push_back(std::byte { 0xD9 });
        return out;
    }


    static SimpleMetaResult run_simple_meta(std::span<const std::byte> bytes,
                                            MetaStore* store)
    {
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 32> ifds {};
        std::vector<std::byte> payload(4096);
        std::vector<uint32_t> payload_parts(64);

        ExifDecodeOptions exif_options;
        PayloadOptions payload_options;
        return simple_meta_read(bytes, *store, blocks, ifds, payload,
                                payload_parts, exif_options, payload_options);
    }

}  // namespace

TEST(SimpleMetaRead, DecodesStandaloneXmpPacket)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'/>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> file_bytes(
        reinterpret_cast<const std::byte*>(xmp.data()), xmp.size());

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 16> ifds {};
    std::vector<std::byte> payload(1024);
    std::vector<uint32_t> payload_parts(64);

    ExifDecodeOptions exif_options;
    PayloadOptions payload_options;
    const SimpleMetaResult read
        = simple_meta_read(file_bytes, store, blocks, ifds, payload,
                           payload_parts, exif_options, payload_options);

    EXPECT_EQ(read.xmp.status, XmpDecodeStatus::Ok);
    EXPECT_GT(read.xmp.entries_decoded, 0U);

    store.finalize();
    uint32_t xmp_props = 0;
    for (const Entry& e : store.entries()) {
        if (e.key.kind == MetaKeyKind::XmpProperty
            && !any(e.flags, EntryFlags::Deleted)) {
            xmp_props += 1;
        }
    }
    EXPECT_EQ(xmp_props, read.xmp.entries_decoded);
}

TEST(SimpleMetaRead, DecodesXmpPacketInsideExifPayload)
{
    const std::string xml
        = "<xmp:xmpmeta xmlns:xmp='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'>"
          "<xmp:Rating>0</xmp:Rating>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</xmp:xmpmeta>";

    std::vector<std::byte> app1;
    append_bytes(std::string_view("Exif\0\0", 6U), &app1);
    std::vector<std::byte> tiff(14U, std::byte { 0x00 });
    tiff[0] = std::byte { 'I' };
    tiff[1] = std::byte { 'I' };
    put_u16le(42U, 2U, &tiff);
    put_u32le(8U, 4U, &tiff);
    put_u16le(0U, 8U, &tiff);
    put_u32le(0U, 10U, &tiff);
    app1.insert(app1.end(), tiff.begin(), tiff.end());
    append_bytes(std::string_view("http://ns.adobe.com/xap/1.0/\0", 29U),
                 &app1);
    append_bytes(xml, &app1);

    const std::vector<std::byte> jpg = make_simple_meta_jpeg(app1);
    MetaStore store;
    const SimpleMetaResult read = run_simple_meta(jpg, &store);

    EXPECT_EQ(read.xmp.status, XmpDecodeStatus::Ok);
    EXPECT_GT(read.xmp.entries_decoded, 0U);
}

TEST(SimpleMetaRead, DecodesIccAndIptcStoredInExifTags)
{
    const std::vector<std::byte> icc = make_minimal_icc();
    const std::byte iptc_raw[]       = {
        std::byte { 0x1C }, std::byte { 0x02 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x02 }, std::byte { 0x00 },
        std::byte { 0x02 },
    };
    const std::span<const std::byte> iptc(iptc_raw, sizeof(iptc_raw));

    std::vector<std::byte> tiff(8U + 2U + 2U * 12U + 4U, std::byte { 0x00 });
    tiff[0] = std::byte { 'I' };
    tiff[1] = std::byte { 'I' };
    put_u16le(42U, 2U, &tiff);
    put_u32le(8U, 4U, &tiff);
    put_u16le(2U, 8U, &tiff);

    const uint32_t data_off = static_cast<uint32_t>(tiff.size());
    put_tiff_entry(0x83BBU, 1U, static_cast<uint32_t>(iptc.size()), data_off,
                   10U, &tiff);
    put_tiff_entry(0x8773U, 7U, static_cast<uint32_t>(icc.size()),
                   data_off + static_cast<uint32_t>(iptc.size()), 22U, &tiff);

    tiff.insert(tiff.end(), iptc.begin(), iptc.end());
    tiff.insert(tiff.end(), icc.begin(), icc.end());

    std::vector<std::byte> app1;
    append_bytes(std::string_view("Exif\0\0", 6U), &app1);
    app1.insert(app1.end(), tiff.begin(), tiff.end());

    const std::vector<std::byte> jpg = make_simple_meta_jpeg(app1);
    MetaStore store;
    const SimpleMetaResult read = run_simple_meta(jpg, &store);

    EXPECT_EQ(read.exif.status, ExifDecodeStatus::Ok);

    uint32_t icc_entries  = 0;
    uint32_t iptc_entries = 0;
    for (const Entry& e : store.entries()) {
        if (e.key.kind == MetaKeyKind::IccHeaderField) {
            icc_entries += 1U;
        } else if (e.key.kind == MetaKeyKind::IptcDataset) {
            iptc_entries += 1U;
        }
    }
    EXPECT_GT(icc_entries, 0U);
    EXPECT_GT(iptc_entries, 0U);
}

#else

TEST(SimpleMetaRead, ExpatNotEnabled)
{
    GTEST_SKIP()
        << "OPENMETA_HAS_EXPAT is not enabled; standalone XMP decode is unavailable.";
}

#endif

}  // namespace openmeta
