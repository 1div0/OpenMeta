#include "openmeta/jumbf_decode.h"

#include "openmeta/meta_key.h"
#include "openmeta/simple_meta.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static void append_u16be(std::vector<std::byte>* out, uint16_t value)
    {
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 0U) & 0xFFU) });
    }


    static void append_u32be(std::vector<std::byte>* out, uint32_t value)
    {
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 24U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 16U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 8U) & 0xFFU) });
        out->push_back(
            std::byte { static_cast<uint8_t>((value >> 0U) & 0xFFU) });
    }


    static void append_fourcc(std::vector<std::byte>* out, uint32_t value)
    {
        append_u32be(out, value);
    }


    static void append_bytes(std::vector<std::byte>* out, std::string_view text)
    {
        for (char c : text) {
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
        append_u32be(out, static_cast<uint32_t>(8U + payload.size()));
        append_fourcc(out, type);
        out->insert(out->end(), payload.begin(), payload.end());
    }

    static std::vector<std::byte> make_jumbf_payload_with_cbor(
        std::span<const std::byte> cbor_payload);


    static std::vector<std::byte> make_sample_jumbf_payload()
    {
        const std::vector<std::byte> cbor_payload = {
            std::byte { 0xA1 },
            std::byte { 0x61 },
            std::byte { 0x61 },
            std::byte { 0x01 },
        };
        return make_jumbf_payload_with_cbor(cbor_payload);
    }


    static std::vector<std::byte> make_jumbf_payload_with_cbor(
        std::span<const std::byte> cbor_payload)
    {
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


    static std::vector<std::byte> make_heif_with_jumbf_item()
    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);  // item_ID
        append_u16be(&infe_payload, 0);  // protection
        append_fourcc(&infe_payload, fourcc('j', 'u', 'm', 'b'));
        append_bytes(&infe_payload, "manifest");
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

        const std::vector<std::byte> jumbf = make_sample_jumbf_payload();
        std::vector<std::byte> idat_payload(jumbf.begin(), jumbf.end());
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
        append_u16be(&iloc_payload, 1);              // extent_count
        append_u32be(&iloc_payload, 0);              // extent_offset
        append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));
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
        return file;
    }

}  // namespace

TEST(JumbfDecode, DecodesStructureAndCborMap)
{
    const std::vector<std::byte> payload = make_sample_jumbf_payload();

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);
    EXPECT_GE(result.boxes_decoded, 3U);
    EXPECT_GT(result.entries_decoded, 0U);

    store.finalize();

    MetaKeyView c2pa_key;
    c2pa_key.kind                       = MetaKeyKind::JumbfField;
    c2pa_key.data.jumbf_field.field     = "c2pa.detected";
    const std::span<const EntryId> c2pa = store.find_all(c2pa_key);
    ASSERT_EQ(c2pa.size(), 1U);
    const Entry& c2pa_entry = store.entry(c2pa[0]);
    ASSERT_EQ(c2pa_entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(c2pa_entry.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(static_cast<uint8_t>(c2pa_entry.value.data.u64), 1U);

    MetaKeyView cbor_key;
    cbor_key.kind                       = MetaKeyKind::JumbfCborKey;
    cbor_key.data.jumbf_cbor_key.key    = "box.0.1.cbor.a";
    const std::span<const EntryId> cbor = store.find_all(cbor_key);
    ASSERT_EQ(cbor.size(), 1U);
    const Entry& cbor_entry = store.entry(cbor[0]);
    ASSERT_EQ(cbor_entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(cbor_entry.value.elem_type, MetaElementType::U64);
    EXPECT_EQ(cbor_entry.value.data.u64, 1U);
}

TEST(JumbfDecode, IntegratedViaSimpleMetaRead)
{
    const std::vector<std::byte> file = make_heif_with_jumbf_item();

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 16> ifds {};
    std::array<std::byte, 4096> payload {};
    std::array<uint32_t, 64> payload_parts {};
    SimpleMetaDecodeOptions options;

    const SimpleMetaResult read = simple_meta_read(file, store, blocks, ifds,
                                                   payload, payload_parts,
                                                   options);
    EXPECT_EQ(read.scan.status, ScanStatus::Ok);
    EXPECT_EQ(read.jumbf.status, JumbfDecodeStatus::Ok);
    EXPECT_GT(read.jumbf.entries_decoded, 0U);

    store.finalize();
    MetaKeyView cbor_key;
    cbor_key.kind                       = MetaKeyKind::JumbfCborKey;
    cbor_key.data.jumbf_cbor_key.key    = "box.0.1.cbor.a";
    const std::span<const EntryId> cbor = store.find_all(cbor_key);
    ASSERT_EQ(cbor.size(), 1U);
}

TEST(JumbfDecode, UnsupportedForNonBmffPayload)
{
    const std::array<std::byte, 4> bad = {
        std::byte { 0xDE },
        std::byte { 0xAD },
        std::byte { 0xBE },
        std::byte { 0xEF },
    };
    MetaStore store;
    const JumbfDecodeResult res = decode_jumbf_payload(
        std::span<const std::byte>(bad.data(), bad.size()), store);
    EXPECT_EQ(res.status, JumbfDecodeStatus::Unsupported);
}

TEST(JumbfDecode, CborCompositeKeyFallbackUsesStableName)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xA1 },  // map(1)
        std::byte { 0x82 },  // key: array(2)
        std::byte { 0x01 },  // key[0]
        std::byte { 0x02 },  // key[1]
        std::byte { 0x03 },  // value
    };
    const std::vector<std::byte> payload
        = make_jumbf_payload_with_cbor(cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

    store.finalize();
    MetaKeyView key;
    key.kind                        = MetaKeyKind::JumbfCborKey;
    key.data.jumbf_cbor_key.key     = "box.0.1.cbor.k0_arr";
    const std::span<const EntryId> values = store.find_all(key);
    ASSERT_EQ(values.size(), 1U);
    const Entry& entry = store.entry(values[0]);
    ASSERT_EQ(entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(entry.value.elem_type, MetaElementType::U64);
    EXPECT_EQ(entry.value.data.u64, 3U);
}

TEST(JumbfDecode, CborHalfAndSimpleScalarsDecode)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xA2 },  // map(2)
        std::byte { 0x61 },  // text key "h"
        std::byte { 0x68 },
        std::byte { 0xF9 },  // half float
        std::byte { 0x3E },
        std::byte { 0x00 },  // 1.5f
        std::byte { 0x61 },  // text key "s"
        std::byte { 0x73 },
        std::byte { 0xF0 },  // simple(16)
    };
    const std::vector<std::byte> payload
        = make_jumbf_payload_with_cbor(cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

    store.finalize();

    MetaKeyView half_key;
    half_key.kind                    = MetaKeyKind::JumbfCborKey;
    half_key.data.jumbf_cbor_key.key = "box.0.1.cbor.h";
    const std::span<const EntryId> half_values = store.find_all(half_key);
    ASSERT_EQ(half_values.size(), 1U);
    const Entry& half_entry = store.entry(half_values[0]);
    ASSERT_EQ(half_entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(half_entry.value.elem_type, MetaElementType::F32);
    EXPECT_EQ(half_entry.value.data.f32_bits, 0x3FC00000U);

    MetaKeyView simple_key;
    simple_key.kind                    = MetaKeyKind::JumbfCborKey;
    simple_key.data.jumbf_cbor_key.key = "box.0.1.cbor.s";
    const std::span<const EntryId> simple_values = store.find_all(simple_key);
    ASSERT_EQ(simple_values.size(), 1U);
    const Entry& simple_entry = store.entry(simple_values[0]);
    ASSERT_EQ(simple_entry.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(simple_entry.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(static_cast<uint8_t>(simple_entry.value.data.u64), 16U);
}

TEST(JumbfDecode, CborIndefiniteTextAndBytesDecode)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xA2 },  // map(2)
        std::byte { 0x61 },  // "t"
        std::byte { 0x74 },
        std::byte { 0x7F },  // text(*)
        std::byte { 0x62 },  // "hi"
        std::byte { 0x68 },
        std::byte { 0x69 },
        std::byte { 0x63 },  // "!!!"
        std::byte { 0x21 },
        std::byte { 0x21 },
        std::byte { 0x21 },
        std::byte { 0xFF },  // break
        std::byte { 0x61 },  // "b"
        std::byte { 0x62 },
        std::byte { 0x5F },  // bytes(*)
        std::byte { 0x42 },  // 0x01 0x02
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x41 },  // 0x03
        std::byte { 0x03 },
        std::byte { 0xFF },  // break
    };
    const std::vector<std::byte> payload
        = make_jumbf_payload_with_cbor(cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

    store.finalize();

    MetaKeyView text_key;
    text_key.kind                    = MetaKeyKind::JumbfCborKey;
    text_key.data.jumbf_cbor_key.key = "box.0.1.cbor.t";
    const std::span<const EntryId> text_values = store.find_all(text_key);
    ASSERT_EQ(text_values.size(), 1U);
    const Entry& text_entry = store.entry(text_values[0]);
    ASSERT_EQ(text_entry.value.kind, MetaValueKind::Text);
    EXPECT_EQ(text_entry.value.text_encoding, TextEncoding::Utf8);
    const std::span<const std::byte> text_bytes = store.arena().span(
        text_entry.value.data.span);
    EXPECT_EQ(std::string_view(
                  reinterpret_cast<const char*>(text_bytes.data()),
                  text_bytes.size()),
              "hi!!!");

    MetaKeyView bytes_key;
    bytes_key.kind                    = MetaKeyKind::JumbfCborKey;
    bytes_key.data.jumbf_cbor_key.key = "box.0.1.cbor.b";
    const std::span<const EntryId> bytes_values = store.find_all(bytes_key);
    ASSERT_EQ(bytes_values.size(), 1U);
    const Entry& bytes_entry = store.entry(bytes_values[0]);
    ASSERT_EQ(bytes_entry.value.kind, MetaValueKind::Bytes);
    const std::span<const std::byte> bytes = store.arena().span(
        bytes_entry.value.data.span);
    ASSERT_EQ(bytes.size(), 3U);
    EXPECT_EQ(static_cast<uint8_t>(bytes[0]), 1U);
    EXPECT_EQ(static_cast<uint8_t>(bytes[1]), 2U);
    EXPECT_EQ(static_cast<uint8_t>(bytes[2]), 3U);
}

TEST(JumbfDecode, CborIndefiniteArrayAndMapDecode)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xA2 },  // map(2)
        std::byte { 0x63 },  // "arr"
        std::byte { 0x61 },
        std::byte { 0x72 },
        std::byte { 0x72 },
        std::byte { 0x9F },  // array(*)
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0xFF },  // break
        std::byte { 0x63 },  // "map"
        std::byte { 0x6D },
        std::byte { 0x61 },
        std::byte { 0x70 },
        std::byte { 0xBF },  // map(*)
        std::byte { 0x01 },  // key=1
        std::byte { 0x61 },  // value="x"
        std::byte { 0x78 },
        std::byte { 0xFF },  // break
    };
    const std::vector<std::byte> payload
        = make_jumbf_payload_with_cbor(cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

    store.finalize();

    MetaKeyView arr0_key;
    arr0_key.kind                    = MetaKeyKind::JumbfCborKey;
    arr0_key.data.jumbf_cbor_key.key = "box.0.1.cbor.arr[0]";
    const std::span<const EntryId> arr0 = store.find_all(arr0_key);
    ASSERT_EQ(arr0.size(), 1U);
    EXPECT_EQ(store.entry(arr0[0]).value.elem_type, MetaElementType::U64);
    EXPECT_EQ(store.entry(arr0[0]).value.data.u64, 1U);

    MetaKeyView arr1_key;
    arr1_key.kind                    = MetaKeyKind::JumbfCborKey;
    arr1_key.data.jumbf_cbor_key.key = "box.0.1.cbor.arr[1]";
    const std::span<const EntryId> arr1 = store.find_all(arr1_key);
    ASSERT_EQ(arr1.size(), 1U);
    EXPECT_EQ(store.entry(arr1[0]).value.elem_type, MetaElementType::U64);
    EXPECT_EQ(store.entry(arr1[0]).value.data.u64, 2U);

    MetaKeyView map_key;
    map_key.kind                    = MetaKeyKind::JumbfCborKey;
    map_key.data.jumbf_cbor_key.key = "box.0.1.cbor.map.1";
    const std::span<const EntryId> map_values = store.find_all(map_key);
    ASSERT_EQ(map_values.size(), 1U);
    const Entry& map_entry = store.entry(map_values[0]);
    ASSERT_EQ(map_entry.value.kind, MetaValueKind::Text);
    const std::span<const std::byte> map_text = store.arena().span(
        map_entry.value.data.span);
    EXPECT_EQ(std::string_view(
                  reinterpret_cast<const char*>(map_text.data()),
                  map_text.size()),
              "x");
}

TEST(JumbfDecode, EmitsDraftC2paSemanticProjectionFields)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xA1 },  // map(1)
        std::byte { 0x69 },  // "manifests"
        std::byte { 0x6D },
        std::byte { 0x61 },
        std::byte { 0x6E },
        std::byte { 0x69 },
        std::byte { 0x66 },
        std::byte { 0x65 },
        std::byte { 0x73 },
        std::byte { 0x74 },
        std::byte { 0x73 },
        std::byte { 0xA1 },  // map(1)
        std::byte { 0x6F },  // "active_manifest"
        std::byte { 0x61 },
        std::byte { 0x63 },
        std::byte { 0x74 },
        std::byte { 0x69 },
        std::byte { 0x76 },
        std::byte { 0x65 },
        std::byte { 0x5F },
        std::byte { 0x6D },
        std::byte { 0x61 },
        std::byte { 0x6E },
        std::byte { 0x69 },
        std::byte { 0x66 },
        std::byte { 0x65 },
        std::byte { 0x73 },
        std::byte { 0x74 },
        std::byte { 0xA4 },  // map(4)
        std::byte { 0x6F },  // "claim_generator"
        std::byte { 0x63 },
        std::byte { 0x6C },
        std::byte { 0x61 },
        std::byte { 0x69 },
        std::byte { 0x6D },
        std::byte { 0x5F },
        std::byte { 0x67 },
        std::byte { 0x65 },
        std::byte { 0x6E },
        std::byte { 0x65 },
        std::byte { 0x72 },
        std::byte { 0x61 },
        std::byte { 0x74 },
        std::byte { 0x6F },
        std::byte { 0x72 },
        std::byte { 0x68 },  // "OpenMeta"
        std::byte { 0x4F },
        std::byte { 0x70 },
        std::byte { 0x65 },
        std::byte { 0x6E },
        std::byte { 0x4D },
        std::byte { 0x65 },
        std::byte { 0x74 },
        std::byte { 0x61 },
        std::byte { 0x6A },  // "assertions"
        std::byte { 0x61 },
        std::byte { 0x73 },
        std::byte { 0x73 },
        std::byte { 0x65 },
        std::byte { 0x72 },
        std::byte { 0x74 },
        std::byte { 0x69 },
        std::byte { 0x6F },
        std::byte { 0x6E },
        std::byte { 0x73 },
        std::byte { 0x82 },  // [1,2]
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x69 },  // "signature"
        std::byte { 0x73 },
        std::byte { 0x69 },
        std::byte { 0x67 },
        std::byte { 0x6E },
        std::byte { 0x61 },
        std::byte { 0x74 },
        std::byte { 0x75 },
        std::byte { 0x72 },
        std::byte { 0x65 },
        std::byte { 0x62 },  // "ok"
        std::byte { 0x6F },
        std::byte { 0x6B },
        std::byte { 0x65 },  // "claim"
        std::byte { 0x63 },
        std::byte { 0x6C },
        std::byte { 0x61 },
        std::byte { 0x69 },
        std::byte { 0x6D },
        std::byte { 0x64 },  // "test"
        std::byte { 0x74 },
        std::byte { 0x65 },
        std::byte { 0x73 },
        std::byte { 0x74 },
    };
    const std::vector<std::byte> payload
        = make_jumbf_payload_with_cbor(cbor_payload);

    MetaStore store;
    const JumbfDecodeResult result = decode_jumbf_payload(payload, store);
    EXPECT_EQ(result.status, JumbfDecodeStatus::Ok);

    store.finalize();

    auto read_u8_field = [&](std::string_view field_name) -> uint8_t {
        MetaKeyView key;
        key.kind                   = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        return static_cast<uint8_t>(e.value.data.u64);
    };

    auto read_u64_field = [&](std::string_view field_name) -> uint64_t {
        MetaKeyView key;
        key.kind                   = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        EXPECT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
        return e.value.data.u64;
    };

    EXPECT_EQ(read_u8_field("c2pa.detected"), 1U);
    EXPECT_EQ(read_u8_field("c2pa.semantic.manifest_present"), 1U);
    EXPECT_EQ(read_u8_field("c2pa.semantic.claim_present"), 1U);
    EXPECT_EQ(read_u8_field("c2pa.semantic.assertion_present"), 1U);
    EXPECT_EQ(read_u8_field("c2pa.semantic.signature_present"), 1U);
    EXPECT_GE(read_u64_field("c2pa.semantic.cbor_key_count"), 5U);
    EXPECT_GE(read_u64_field("c2pa.semantic.assertion_key_hits"), 1U);

    MetaKeyView cg_key;
    cg_key.kind                   = MetaKeyKind::JumbfField;
    cg_key.data.jumbf_field.field = "c2pa.semantic.claim_generator";
    const std::span<const EntryId> cg_ids = store.find_all(cg_key);
    ASSERT_EQ(cg_ids.size(), 1U);
    const Entry& cg = store.entry(cg_ids[0]);
    ASSERT_EQ(cg.value.kind, MetaValueKind::Text);
    const std::span<const std::byte> cg_text = store.arena().span(
        cg.value.data.span);
    EXPECT_EQ(std::string_view(
                  reinterpret_cast<const char*>(cg_text.data()),
                  cg_text.size()),
              "OpenMeta");
}

}  // namespace openmeta
