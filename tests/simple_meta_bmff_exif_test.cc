#include "openmeta/simple_meta.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace openmeta {
namespace {

    static void append_u16be(std::vector<std::byte>* out, uint16_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFFU) });
    }

    static void append_u32be(std::vector<std::byte>* out, uint32_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFFU) });
    }

    static void append_u16le(std::vector<std::byte>* out, uint16_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
    }

    static void append_u32le(std::vector<std::byte>* out, uint32_t v)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFFU) });
    }

    static void append_fourcc(std::vector<std::byte>* out, uint32_t v)
    {
        append_u32be(out, v);
    }

    static void append_bytes(std::vector<std::byte>* out, const char* s)
    {
        ASSERT_NE(out, nullptr);
        ASSERT_NE(s, nullptr);
        for (size_t i = 0; s[i] != '\0'; ++i) {
            out->push_back(std::byte { static_cast<uint8_t>(s[i]) });
        }
    }

    static void append_fullbox_header(std::vector<std::byte>* out,
                                      uint8_t version)
    {
        ASSERT_NE(out, nullptr);
        out->push_back(std::byte { version });
        out->push_back(std::byte { 0 });
        out->push_back(std::byte { 0 });
        out->push_back(std::byte { 0 });
    }

    static void append_bmff_box(std::vector<std::byte>* out, uint32_t type,
                                std::span<const std::byte> payload)
    {
        ASSERT_NE(out, nullptr);
        append_u32be(out, static_cast<uint32_t>(8U + payload.size()));
        append_fourcc(out, type);
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

    static std::vector<std::byte> make_tiff_ifd0_imagewidth_u32(uint32_t width)
    {
        // Classic TIFF LE header + IFD0 with a single ImageWidth entry.
        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42U);
        append_u32le(&tiff, 8U);  // ifd0

        append_u16le(&tiff, 1U);       // entry count
        append_u16le(&tiff, 0x0100U);  // ImageWidth
        append_u16le(&tiff, 4U);       // LONG
        append_u32le(&tiff, 1U);
        append_u32le(&tiff, width);
        append_u32le(&tiff, 0U);  // next IFD
        return tiff;
    }

    static std::vector<std::byte>
    make_bmff_exif_item_with_preamble(std::span<const std::byte> tiff_bytes)
    {
        // ISO-BMFF Exif item: u32be offset to TIFF header after this field.
        // Common layout: offset=6 + "Exif\0\0" + TIFF.
        std::vector<std::byte> exif;
        append_u32be(&exif, 6U);
        append_bytes(&exif, "Exif");
        exif.push_back(std::byte { 0 });
        exif.push_back(std::byte { 0 });
        exif.insert(exif.end(), tiff_bytes.begin(), tiff_bytes.end());
        return exif;
    }

}  // namespace

TEST(SimpleMetaRead, BmffMetaExifItemFromIdatDecodes)
{
    struct Case final {
        uint32_t major_brand = 0;
    };
    const std::array<Case, 3> cases = {
        Case { fourcc('h', 'e', 'i', 'c') },
        Case { fourcc('a', 'v', 'i', 'f') },
        Case { fourcc('c', 'r', 'x', ' ') },
    };

    for (const Case& c : cases) {
        const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(640U);
        const std::vector<std::byte> exif_item
            = make_bmff_exif_item_with_preamble(tiff);

        // infe (v2): item 1 is Exif.
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "exif");
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        // iinf (v2): 1 entry.
        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        // idat payload: Exif item bytes.
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), exif_item);

        // iloc (v1): construction_method=1 (idat), extent points to offset 0 in idat.
        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload,
                     1);                 // construction_method=1 (idat)
        append_u16be(&iloc_payload, 0);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload,
                     0);  // extent_offset (within idat)
        append_u32be(&iloc_payload, static_cast<uint32_t>(exif_item.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        // meta (FullBox): iinf + iloc + idat.
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

        // ftyp.
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, c.major_brand);
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 4096> payload {};
        std::array<uint32_t, 64> scratch {};
        const SimpleMetaResult res
            = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                               ExifDecodeOptions {}, PayloadOptions {});
        store.finalize();

        ASSERT_EQ(res.scan.status, ScanStatus::Ok);
        ASSERT_EQ(res.exif.status, ExifDecodeStatus::Ok);

        const std::span<const EntryId> ids = store.find_all(
            exif_key("ifd0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64), 640U);
    }
}

TEST(SimpleMetaRead, BmffMetaExifItemFromIdatWithDrefSelfContainedDecodes)
{
    // Some BMFF files use `iloc.data_reference_index=1` with `dref/url ` set to
    // self-contained (flags=1) to indicate the item data is stored in the same file.
    struct Case final {
        uint32_t major_brand = 0;
    };
    const std::array<Case, 3> cases = {
        Case { fourcc('h', 'e', 'i', 'c') },
        Case { fourcc('a', 'v', 'i', 'f') },
        Case { fourcc('c', 'r', 'x', ' ') },
    };

    for (const Case& c : cases) {
        const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(640U);
        const std::vector<std::byte> exif_item
            = make_bmff_exif_item_with_preamble(tiff);

        // infe (v2): item 1 is Exif.
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "exif");
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        // iinf (v2): 1 entry.
        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        // idat payload: Exif item bytes.
        std::vector<std::byte> idat_box;
        append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), exif_item);

        // dref: one self-contained `url ` entry (flags=1).
        std::vector<std::byte> url_payload;
        url_payload.push_back(std::byte { 0 });  // version
        url_payload.push_back(std::byte { 0 });
        url_payload.push_back(std::byte { 0 });
        url_payload.push_back(std::byte { 1 });  // flags (self-contained)
        std::vector<std::byte> url_box;
        append_bmff_box(&url_box, fourcc('u', 'r', 'l', ' '), url_payload);

        std::vector<std::byte> dref_payload;
        append_fullbox_header(&dref_payload, 0);
        append_u32be(&dref_payload, 1);  // entry_count
        dref_payload.insert(dref_payload.end(), url_box.begin(), url_box.end());
        std::vector<std::byte> dref_box;
        append_bmff_box(&dref_box, fourcc('d', 'r', 'e', 'f'), dref_payload);

        std::vector<std::byte> dinf_payload;
        dinf_payload.insert(dinf_payload.end(), dref_box.begin(),
                            dref_box.end());
        std::vector<std::byte> dinf_box;
        append_bmff_box(&dinf_box, fourcc('d', 'i', 'n', 'f'), dinf_payload);

        // iloc (v1): construction_method=1 (idat), data_reference_index=1.
        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload,
                     1);                 // construction_method=1 (idat)
        append_u16be(&iloc_payload, 1);  // data_reference_index
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload,
                     0);  // extent_offset (within idat)
        append_u32be(&iloc_payload, static_cast<uint32_t>(exif_item.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        // meta (FullBox): iinf + iloc + dinf + idat.
        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        meta_payload.insert(meta_payload.end(), dinf_box.begin(),
                            dinf_box.end());
        meta_payload.insert(meta_payload.end(), idat_box.begin(),
                            idat_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        // ftyp.
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, c.major_brand);
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 4096> payload {};
        std::array<uint32_t, 64> scratch {};
        const SimpleMetaResult res
            = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                               ExifDecodeOptions {}, PayloadOptions {});
        store.finalize();

        ASSERT_EQ(res.scan.status, ScanStatus::Ok);
        ASSERT_EQ(res.exif.status, ExifDecodeStatus::Ok);

        const std::span<const EntryId> ids = store.find_all(
            exif_key("ifd0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64), 640U);
    }
}

TEST(SimpleMetaRead, BmffMetaExifItemFromFileOffsetDecodes)
{
    struct Case final {
        uint32_t major_brand = 0;
    };
    const std::array<Case, 3> cases = {
        Case { fourcc('h', 'e', 'i', 'c') },
        Case { fourcc('a', 'v', 'i', 'f') },
        Case { fourcc('c', 'r', 'x', ' ') },
    };

    for (const Case& c : cases) {
        const std::vector<std::byte> tiff = make_tiff_ifd0_imagewidth_u32(
            4032U);
        const std::vector<std::byte> exif_item
            = make_bmff_exif_item_with_preamble(tiff);

        // infe (v2): item 1 is Exif.
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);
        append_u16be(&infe_payload, 0);
        append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe_payload, "exif");
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        // iinf (v2): 1 entry.
        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        // iloc (v1): construction_method=0 (file), base_offset patched later.
        std::vector<std::byte> iloc_payload;
        append_fullbox_header(&iloc_payload, 1);
        iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
        iloc_payload.push_back(std::byte { 0x40 });  // base=4, idx=0
        append_u16be(&iloc_payload, 1);              // item_count
        append_u16be(&iloc_payload, 1);              // item_ID
        append_u16be(&iloc_payload, 0);              // construction_method=0
        append_u16be(&iloc_payload, 0);              // data_reference_index
        const size_t base_off_pos = iloc_payload.size();
        append_u32be(&iloc_payload, 0);  // base_offset placeholder
        append_u16be(&iloc_payload, 1);  // extent_count
        append_u32be(&iloc_payload, 0);  // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(exif_item.size()));
        std::vector<std::byte> iloc_box;
        append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

        // meta (FullBox): iinf + iloc.
        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iloc_box.begin(),
                            iloc_box.end());
        std::vector<std::byte> meta_box;
        append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

        // mdat containing Exif item bytes.
        std::vector<std::byte> mdat_box;
        append_bmff_box(&mdat_box, fourcc('m', 'd', 'a', 't'), exif_item);

        // ftyp.
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, c.major_brand);
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));

        std::vector<std::byte> file;
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
        file.insert(file.end(), meta_box.begin(), meta_box.end());

        const uint64_t mdat_payload_off = static_cast<uint64_t>(file.size())
                                          + 8U;
        file.insert(file.end(), mdat_box.begin(), mdat_box.end());

        // Patch base_offset in-place (big-endian u32) to point at the mdat payload.
        const uint64_t meta_box_start_off
            = 8U + static_cast<uint64_t>(ftyp_payload.size());
        const uint64_t iloc_box_start_in_meta_payload
            = 4U + static_cast<uint64_t>(iinf_box.size());
        const uint64_t base_word_off = meta_box_start_off + 8U
                                       + iloc_box_start_in_meta_payload + 8U
                                       + base_off_pos;
        ASSERT_LE(base_word_off + 4U, static_cast<uint64_t>(file.size()));
        file[static_cast<size_t>(base_word_off + 0U)] = std::byte {
            static_cast<uint8_t>((mdat_payload_off >> 24) & 0xFFU)
        };
        file[static_cast<size_t>(base_word_off + 1U)] = std::byte {
            static_cast<uint8_t>((mdat_payload_off >> 16) & 0xFFU)
        };
        file[static_cast<size_t>(base_word_off + 2U)] = std::byte {
            static_cast<uint8_t>((mdat_payload_off >> 8) & 0xFFU)
        };
        file[static_cast<size_t>(base_word_off + 3U)]
            = std::byte { static_cast<uint8_t>(mdat_payload_off & 0xFFU) };

        MetaStore store;
        std::array<ContainerBlockRef, 32> blocks {};
        std::array<ExifIfdRef, 8> ifds {};
        std::array<std::byte, 4096> payload {};
        std::array<uint32_t, 64> scratch {};
        const SimpleMetaResult res
            = simple_meta_read(file, store, blocks, ifds, payload, scratch,
                               ExifDecodeOptions {}, PayloadOptions {});
        store.finalize();

        ASSERT_EQ(res.scan.status, ScanStatus::Ok);
        ASSERT_EQ(res.exif.status, ExifDecodeStatus::Ok);

        const std::span<const EntryId> ids = store.find_all(
            exif_key("ifd0", 0x0100));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64), 4032U);
    }
}

}  // namespace openmeta
