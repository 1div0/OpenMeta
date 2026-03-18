#include "openmeta/simple_meta.h"

#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <span>
#include <string>
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

    static void append_fourcc(std::vector<std::byte>* out, uint32_t fourcc_v)
    {
        append_u32be(out, fourcc_v);
    }

    static void append_bytes(std::vector<std::byte>* out, const char* s)
    {
        for (size_t i = 0; s[i] != '\0'; ++i) {
            out->push_back(std::byte { static_cast<uint8_t>(s[i]) });
        }
    }

    static void append_fullbox_header(std::vector<std::byte>* out,
                                      uint8_t version)
    {
        out->push_back(std::byte { version });
        out->push_back(std::byte { 0 });
        out->push_back(std::byte { 0 });
        out->push_back(std::byte { 0 });
    }

    static void append_auxc_payload(std::vector<std::byte>* out,
                                    const char* aux_type,
                                    std::span<const std::byte> subtype)
    {
        append_fullbox_header(out, 0);
        append_bytes(out, aux_type);
        out->push_back(std::byte { 0x00 });
        out->insert(out->end(), subtype.begin(), subtype.end());
    }

    static void append_bmff_box(std::vector<std::byte>* out, uint32_t type,
                                std::span<const std::byte> payload)
    {
        append_u32be(out, static_cast<uint32_t>(8 + payload.size()));
        append_fourcc(out, type);
        out->insert(out->end(), payload.begin(), payload.end());
    }

    static void append_infe_v2(std::vector<std::byte>* out, uint16_t item_id,
                               uint16_t protection_index, uint32_t item_type,
                               const char* name)
    {
        std::vector<std::byte> payload;
        append_fullbox_header(&payload, 2);
        append_u16be(&payload, item_id);
        append_u16be(&payload, protection_index);
        append_u32be(&payload, item_type);
        append_bytes(&payload, name);
        payload.push_back(std::byte { 0 });
        append_bmff_box(out, fourcc('i', 'n', 'f', 'e'), payload);
    }

    static MetaKeyView bmff_key(std::string_view field)
    {
        MetaKeyView key;
        key.kind                  = MetaKeyKind::BmffField;
        key.data.bmff_field.field = field;
        return key;
    }

    static std::vector<uint32_t> collect_u32_values(const MetaStore& store,
                                                    std::string_view field)
    {
        std::vector<uint32_t> out;
        const std::span<const EntryId> ids = store.find_all(bmff_key(field));
        out.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& e = store.entry(ids[i]);
            if (e.value.kind != MetaValueKind::Scalar
                || e.value.elem_type != MetaElementType::U32) {
                continue;
            }
            out.push_back(static_cast<uint32_t>(e.value.data.u64));
        }
        return out;
    }

    static std::vector<uint8_t> collect_u8_values(const MetaStore& store,
                                                  std::string_view field)
    {
        std::vector<uint8_t> out;
        const std::span<const EntryId> ids = store.find_all(bmff_key(field));
        out.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& e = store.entry(ids[i]);
            if (e.value.kind != MetaValueKind::Scalar
                || e.value.elem_type != MetaElementType::U8) {
                continue;
            }
            out.push_back(static_cast<uint8_t>(e.value.data.u64));
        }
        return out;
    }

    static std::vector<uint64_t> collect_u64_values(const MetaStore& store,
                                                    std::string_view field)
    {
        std::vector<uint64_t> out;
        const std::span<const EntryId> ids = store.find_all(bmff_key(field));
        out.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& e = store.entry(ids[i]);
            if (e.value.kind != MetaValueKind::Scalar
                || e.value.elem_type != MetaElementType::U64) {
                continue;
            }
            out.push_back(e.value.data.u64);
        }
        return out;
    }

    static std::vector<std::string> collect_text_values(const MetaStore& store,
                                                        std::string_view field)
    {
        std::vector<std::string> out;
        const std::span<const EntryId> ids = store.find_all(bmff_key(field));
        out.reserve(ids.size());
        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& e = store.entry(ids[i]);
            if (e.value.kind != MetaValueKind::Text) {
                continue;
            }
            const std::span<const std::byte> text = store.arena().span(
                e.value.data.span);
            out.emplace_back(reinterpret_cast<const char*>(text.data()),
                             text.size());
        }
        return out;
    }

}  // namespace

TEST(BmffDerivedFieldsDecode, EmitsFtypAndPrimaryProps)
{
    // Minimal ISO-BMFF/HEIF with:
    // - ftyp(major_brand='heic', compat=['mif1'])
    // - meta(pitm primary item id=1, iprp/ipco(ispe+irot+imir), ipma associates props)

    std::vector<std::byte> file;

    // ftyp
    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);  // minor_version
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    // meta
    {
        // pitm (FullBox version 0): primary item id=1 (u16)
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        // ipco: ispe + irot + imir
        std::vector<std::byte> ispe_payload;
        append_fullbox_header(&ispe_payload, 0);
        append_u32be(&ispe_payload, 640);
        append_u32be(&ispe_payload, 480);
        std::vector<std::byte> ispe_box;
        append_bmff_box(&ispe_box, fourcc('i', 's', 'p', 'e'), ispe_payload);

        std::vector<std::byte> irot_payload;
        irot_payload.push_back(std::byte { 1 });  // 90 degrees
        std::vector<std::byte> irot_box;
        append_bmff_box(&irot_box, fourcc('i', 'r', 'o', 't'), irot_payload);

        std::vector<std::byte> imir_payload;
        imir_payload.push_back(std::byte { 1 });
        std::vector<std::byte> imir_box;
        append_bmff_box(&imir_box, fourcc('i', 'm', 'i', 'r'), imir_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), ispe_box.begin(),
                            ispe_box.end());
        ipco_payload.insert(ipco_payload.end(), irot_box.begin(),
                            irot_box.end());
        ipco_payload.insert(ipco_payload.end(), imir_box.begin(),
                            imir_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        // ipma (FullBox version 0): item 1 has properties [1,2,3]
        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 1);           // entry_count
        append_u16be(&ipma_payload, 1);           // item_ID
        ipma_payload.push_back(std::byte { 3 });  // association_count
        ipma_payload.push_back(std::byte { 1 });  // property_index=1
        ipma_payload.push_back(std::byte { 2 });  // property_index=2
        ipma_payload.push_back(std::byte { 3 });  // property_index=3
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    // ftyp.major_brand == 'heic'
    {
        const std::span<const EntryId> ids = store.find_all(
            bmff_key("ftyp.major_brand"));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64),
                  fourcc('h', 'e', 'i', 'c'));
    }

    // primary.width/height/rotation/mirror
    {
        const std::span<const EntryId> w = store.find_all(
            bmff_key("primary.width"));
        const std::span<const EntryId> h = store.find_all(
            bmff_key("primary.height"));
        const std::span<const EntryId> r = store.find_all(
            bmff_key("primary.rotation_degrees"));
        const std::vector<uint8_t> m = collect_u8_values(store,
                                                         "primary.mirror");
        ASSERT_EQ(w.size(), 1U);
        ASSERT_EQ(h.size(), 1U);
        ASSERT_EQ(r.size(), 1U);
        ASSERT_EQ(m.size(), 1U);
        EXPECT_EQ(static_cast<uint32_t>(store.entry(w[0]).value.data.u64),
                  640U);
        EXPECT_EQ(static_cast<uint32_t>(store.entry(h[0]).value.data.u64),
                  480U);
        EXPECT_EQ(static_cast<uint16_t>(store.entry(r[0]).value.data.u64), 90U);
        EXPECT_EQ(m[0], 1U);
    }
}

TEST(BmffDerivedFieldsDecode, EmitsIrefEdgesAndPrimaryAuxLinks)
{
    // Minimal HEIF-like BMFF:
    // - ftyp(heic)
    // - meta(pitm primary item id=1)
    // - iref with one auxl edge box: from=1 -> [2,3]

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> auxl_payload;
        append_u16be(&auxl_payload, 1);  // from item id
        append_u16be(&auxl_payload, 2);  // ref count
        append_u16be(&auxl_payload, 2);  // to item id
        append_u16be(&auxl_payload, 3);  // to item id
        std::vector<std::byte> auxl_box;
        append_bmff_box(&auxl_box, fourcc('a', 'u', 'x', 'l'), auxl_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), auxl_box.begin(),
                            auxl_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> edge_count
        = collect_u32_values(store, "iref.edge_count");
    ASSERT_EQ(edge_count.size(), 1U);
    EXPECT_EQ(edge_count[0], 2U);

    const std::vector<uint32_t> ref_type = collect_u32_values(store,
                                                              "iref.ref_type");
    ASSERT_EQ(ref_type.size(), 2U);
    EXPECT_EQ(ref_type[0], fourcc('a', 'u', 'x', 'l'));
    EXPECT_EQ(ref_type[1], fourcc('a', 'u', 'x', 'l'));

    const std::vector<uint32_t> from_ids
        = collect_u32_values(store, "iref.from_item_id");
    ASSERT_EQ(from_ids.size(), 2U);
    EXPECT_EQ(from_ids[0], 1U);
    EXPECT_EQ(from_ids[1], 1U);

    const std::vector<uint32_t> to_ids = collect_u32_values(store,
                                                            "iref.to_item_id");
    ASSERT_EQ(to_ids.size(), 2U);
    EXPECT_EQ(to_ids[0], 2U);
    EXPECT_EQ(to_ids[1], 3U);

    const std::vector<uint32_t> item_count
        = collect_u32_values(store, "iref.item_count");
    ASSERT_EQ(item_count.size(), 1U);
    EXPECT_EQ(item_count[0], 3U);
    const std::vector<uint32_t> from_unique_count
        = collect_u32_values(store, "iref.from_item_unique_count");
    ASSERT_EQ(from_unique_count.size(), 1U);
    EXPECT_EQ(from_unique_count[0], 1U);
    const std::vector<uint32_t> to_unique_count
        = collect_u32_values(store, "iref.to_item_unique_count");
    ASSERT_EQ(to_unique_count.size(), 1U);
    EXPECT_EQ(to_unique_count[0], 2U);
    const std::vector<uint32_t> item_ids = collect_u32_values(store,
                                                              "iref.item_id");
    ASSERT_EQ(item_ids.size(), 3U);
    EXPECT_EQ(item_ids[0], 1U);
    EXPECT_EQ(item_ids[1], 2U);
    EXPECT_EQ(item_ids[2], 3U);
    const std::vector<uint32_t> item_out_counts
        = collect_u32_values(store, "iref.item_out_edge_count");
    ASSERT_EQ(item_out_counts.size(), 3U);
    EXPECT_EQ(item_out_counts[0], 2U);
    EXPECT_EQ(item_out_counts[1], 0U);
    EXPECT_EQ(item_out_counts[2], 0U);
    const std::vector<uint32_t> item_in_counts
        = collect_u32_values(store, "iref.item_in_edge_count");
    ASSERT_EQ(item_in_counts.size(), 3U);
    EXPECT_EQ(item_in_counts[0], 0U);
    EXPECT_EQ(item_in_counts[1], 1U);
    EXPECT_EQ(item_in_counts[2], 1U);

    const std::vector<uint32_t> primary_auxl
        = collect_u32_values(store, "primary.auxl_item_id");
    ASSERT_EQ(primary_auxl.size(), 2U);
    EXPECT_EQ(primary_auxl[0], 2U);
    EXPECT_EQ(primary_auxl[1], 3U);
    const std::vector<uint32_t> primary_auxl_count
        = collect_u32_values(store, "primary.auxl_count");
    ASSERT_EQ(primary_auxl_count.size(), 1U);
    EXPECT_EQ(primary_auxl_count[0], 2U);

    const std::vector<uint32_t> auxl_from_unique
        = collect_u32_values(store, "iref.auxl.from_item_unique_count");
    ASSERT_EQ(auxl_from_unique.size(), 1U);
    EXPECT_EQ(auxl_from_unique[0], 1U);
    const std::vector<uint32_t> auxl_to_unique
        = collect_u32_values(store, "iref.auxl.to_item_unique_count");
    ASSERT_EQ(auxl_to_unique.size(), 1U);
    EXPECT_EQ(auxl_to_unique[0], 2U);
}

TEST(BmffDerivedFieldsDecode, EmitsIrefEdgesForVersion1ItemIds)
{
    // Same auxl edge semantics as the v0 test, but with 32-bit item IDs in
    // pitm/iref (version=1 fullboxes).
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        const uint32_t kPrimary = 0x10001U;
        const uint32_t kAuxA    = 0x10002U;
        const uint32_t kAuxB    = 0x10003U;

        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 1);
        append_u32be(&pitm_payload, kPrimary);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> auxl_payload;
        append_u32be(&auxl_payload, kPrimary);  // from item id
        append_u16be(&auxl_payload, 2);         // ref count
        append_u32be(&auxl_payload, kAuxA);     // to item id
        append_u32be(&auxl_payload, kAuxB);     // to item id
        std::vector<std::byte> auxl_box;
        append_bmff_box(&auxl_box, fourcc('a', 'u', 'x', 'l'), auxl_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 1);
        iref_payload.insert(iref_payload.end(), auxl_box.begin(),
                            auxl_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> edge_count
        = collect_u32_values(store, "iref.edge_count");
    ASSERT_EQ(edge_count.size(), 1U);
    EXPECT_EQ(edge_count[0], 2U);

    const std::vector<uint32_t> primary_auxl
        = collect_u32_values(store, "primary.auxl_item_id");
    ASSERT_EQ(primary_auxl.size(), 2U);
    EXPECT_EQ(primary_auxl[0], 0x10002U);
    EXPECT_EQ(primary_auxl[1], 0x10003U);
    const std::vector<uint32_t> primary_auxl_count
        = collect_u32_values(store, "primary.auxl_count");
    ASSERT_EQ(primary_auxl_count.size(), 1U);
    EXPECT_EQ(primary_auxl_count[0], 2U);
}

TEST(BmffDerivedFieldsDecode, EmitsNonPrimaryIrefTypedEdgesForVersion1ItemIds)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        const uint32_t kPrimary  = 0x10001U;
        const uint32_t kFromDimg = 0x20002U;
        const uint32_t kFromThmb = 0x20003U;
        const uint32_t kFromCdsc = 0x20004U;
        const uint32_t kToDimgA  = 0x30005U;
        const uint32_t kToDimgB  = 0x30006U;
        const uint32_t kToThmb   = 0x30007U;
        const uint32_t kToCdsc   = 0x30008U;

        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 1);
        append_u32be(&pitm_payload, kPrimary);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> dimg_payload;
        append_u32be(&dimg_payload, kFromDimg);
        append_u16be(&dimg_payload, 2);
        append_u32be(&dimg_payload, kToDimgA);
        append_u32be(&dimg_payload, kToDimgB);
        std::vector<std::byte> dimg_box;
        append_bmff_box(&dimg_box, fourcc('d', 'i', 'm', 'g'), dimg_payload);

        std::vector<std::byte> thmb_payload;
        append_u32be(&thmb_payload, kFromThmb);
        append_u16be(&thmb_payload, 1);
        append_u32be(&thmb_payload, kToThmb);
        std::vector<std::byte> thmb_box;
        append_bmff_box(&thmb_box, fourcc('t', 'h', 'm', 'b'), thmb_payload);

        std::vector<std::byte> cdsc_payload;
        append_u32be(&cdsc_payload, kFromCdsc);
        append_u16be(&cdsc_payload, 1);
        append_u32be(&cdsc_payload, kToCdsc);
        std::vector<std::byte> cdsc_box;
        append_bmff_box(&cdsc_box, fourcc('c', 'd', 's', 'c'), cdsc_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 1);
        iref_payload.insert(iref_payload.end(), dimg_box.begin(),
                            dimg_box.end());
        iref_payload.insert(iref_payload.end(), thmb_box.begin(),
                            thmb_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_box.begin(),
                            cdsc_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> edge_count
        = collect_u32_values(store, "iref.edge_count");
    ASSERT_EQ(edge_count.size(), 1U);
    EXPECT_EQ(edge_count[0], 4U);

    const std::vector<uint32_t> dimg_count
        = collect_u32_values(store, "iref.dimg.edge_count");
    ASSERT_EQ(dimg_count.size(), 1U);
    EXPECT_EQ(dimg_count[0], 2U);
    const std::vector<uint32_t> thmb_count
        = collect_u32_values(store, "iref.thmb.edge_count");
    ASSERT_EQ(thmb_count.size(), 1U);
    EXPECT_EQ(thmb_count[0], 1U);
    const std::vector<uint32_t> cdsc_count
        = collect_u32_values(store, "iref.cdsc.edge_count");
    ASSERT_EQ(cdsc_count.size(), 1U);
    EXPECT_EQ(cdsc_count[0], 1U);

    const std::vector<uint32_t> dimg_from
        = collect_u32_values(store, "iref.dimg.from_item_id");
    ASSERT_EQ(dimg_from.size(), 2U);
    EXPECT_EQ(dimg_from[0], 0x20002U);
    EXPECT_EQ(dimg_from[1], 0x20002U);
    const std::vector<uint32_t> dimg_to
        = collect_u32_values(store, "iref.dimg.to_item_id");
    ASSERT_EQ(dimg_to.size(), 2U);
    EXPECT_EQ(dimg_to[0], 0x30005U);
    EXPECT_EQ(dimg_to[1], 0x30006U);

    const std::vector<uint32_t> thmb_from
        = collect_u32_values(store, "iref.thmb.from_item_id");
    ASSERT_EQ(thmb_from.size(), 1U);
    EXPECT_EQ(thmb_from[0], 0x20003U);
    const std::vector<uint32_t> thmb_to
        = collect_u32_values(store, "iref.thmb.to_item_id");
    ASSERT_EQ(thmb_to.size(), 1U);
    EXPECT_EQ(thmb_to[0], 0x30007U);

    const std::vector<uint32_t> cdsc_from
        = collect_u32_values(store, "iref.cdsc.from_item_id");
    ASSERT_EQ(cdsc_from.size(), 1U);
    EXPECT_EQ(cdsc_from[0], 0x20004U);
    const std::vector<uint32_t> cdsc_to
        = collect_u32_values(store, "iref.cdsc.to_item_id");
    ASSERT_EQ(cdsc_to.size(), 1U);
    EXPECT_EQ(cdsc_to[0], 0x30008U);

    EXPECT_TRUE(collect_u32_values(store, "primary.dimg_item_id").empty());
    EXPECT_TRUE(collect_u32_values(store, "primary.thmb_item_id").empty());
    EXPECT_TRUE(collect_u32_values(store, "primary.cdsc_item_id").empty());
}

TEST(BmffDerivedFieldsDecode, EmitsPrimaryAuxSemanticsFromAuxC)
{
    // Minimal HEIF-like BMFF:
    // - primary item id = 1
    // - iref auxl edges: 1 -> 2,3
    // - ipco has auxC properties:
    //   - prop #1: urn:mpeg:hevc:2015:auxid:2 (depth)
    //   - prop #2: urn:mpeg:hevc:2015:auxid:1 (alpha)
    // - ipma maps item 2 -> prop #1, item 3 -> prop #2

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> auxl_payload;
        append_u16be(&auxl_payload, 1);  // from item id
        append_u16be(&auxl_payload, 2);  // ref count
        append_u16be(&auxl_payload, 2);  // to item id
        append_u16be(&auxl_payload, 3);  // to item id
        std::vector<std::byte> auxl_box;
        append_bmff_box(&auxl_box, fourcc('a', 'u', 'x', 'l'), auxl_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), auxl_box.begin(),
                            auxl_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> auxc_depth_payload;
        static constexpr char kDepth[] = "urn:mpeg:hevc:2015:auxid:2";
        append_auxc_payload(&auxc_depth_payload, kDepth, {});
        auxc_depth_payload.push_back(std::byte { 0xAA });
        auxc_depth_payload.push_back(std::byte { 0xBB });
        std::vector<std::byte> auxc_depth_box;
        append_bmff_box(&auxc_depth_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_depth_payload);

        std::vector<std::byte> auxc_alpha_payload;
        static constexpr char kAlpha[] = "urn:mpeg:hevc:2015:auxid:1";
        append_auxc_payload(&auxc_alpha_payload, kAlpha, {});
        auxc_alpha_payload.push_back(std::byte { 0x11 });
        std::vector<std::byte> auxc_alpha_box;
        append_bmff_box(&auxc_alpha_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_alpha_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), auxc_depth_box.begin(),
                            auxc_depth_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_alpha_box.begin(),
                            auxc_alpha_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 3);  // entry_count

        append_u16be(&ipma_payload, 1);           // item id (primary)
        ipma_payload.push_back(std::byte { 0 });  // association_count

        append_u16be(&ipma_payload, 2);           // item id (aux depth)
        ipma_payload.push_back(std::byte { 1 });  // association_count
        ipma_payload.push_back(std::byte { 1 });  // property_index=1

        append_u16be(&ipma_payload, 3);           // item id (aux alpha)
        ipma_payload.push_back(std::byte { 1 });  // association_count
        ipma_payload.push_back(std::byte { 2 });  // property_index=2

        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 6);
        append_infe_v2(&iinf_payload, 2, 0, fourcc('a', 'u', 'x', 'l'),
                       "depth_aux");
        append_infe_v2(&iinf_payload, 3, 0, fourcc('a', 'u', 'x', 'l'),
                       "alpha_aux");
        append_infe_v2(&iinf_payload, 4, 0, fourcc('d', 'e', 'r', 'v'),
                       "derived");
        append_infe_v2(&iinf_payload, 5, 0, fourcc('t', 'h', 'm', 'b'),
                       "thumb");
        append_infe_v2(&iinf_payload, 6, 0, fourcc('c', 'd', 's', 'c'),
                       "caption");
        append_infe_v2(&iinf_payload, 7, 0, fourcc('a', 'u', 'x', 'l'),
                       "other_aux");
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> primary_auxl
        = collect_u32_values(store, "primary.auxl_item_id");
    ASSERT_EQ(primary_auxl.size(), 2U);
    EXPECT_EQ(primary_auxl[0], 2U);
    EXPECT_EQ(primary_auxl[1], 3U);
    const std::vector<uint32_t> primary_auxl_count
        = collect_u32_values(store, "primary.auxl_count");
    ASSERT_EQ(primary_auxl_count.size(), 1U);
    EXPECT_EQ(primary_auxl_count[0], 2U);

    const std::vector<std::string> primary_auxl_semantic
        = collect_text_values(store, "primary.auxl_semantic");
    ASSERT_EQ(primary_auxl_semantic.size(), 2U);
    EXPECT_EQ(primary_auxl_semantic[0], "depth");
    EXPECT_EQ(primary_auxl_semantic[1], "alpha");

    const std::vector<uint32_t> depth_ids
        = collect_u32_values(store, "primary.depth_item_id");
    ASSERT_EQ(depth_ids.size(), 1U);
    EXPECT_EQ(depth_ids[0], 2U);
    const std::vector<uint32_t> depth_count
        = collect_u32_values(store, "primary.depth_count");
    ASSERT_EQ(depth_count.size(), 1U);
    EXPECT_EQ(depth_count[0], 1U);

    const std::vector<uint32_t> alpha_ids
        = collect_u32_values(store, "primary.alpha_item_id");
    ASSERT_EQ(alpha_ids.size(), 1U);
    EXPECT_EQ(alpha_ids[0], 3U);
    const std::vector<uint32_t> alpha_count
        = collect_u32_values(store, "primary.alpha_count");
    ASSERT_EQ(alpha_count.size(), 1U);
    EXPECT_EQ(alpha_count[0], 1U);

    const std::vector<uint32_t> aux_item_ids
        = collect_u32_values(store, "aux.item_id");
    ASSERT_EQ(aux_item_ids.size(), 2U);
    EXPECT_EQ(aux_item_ids[0], 2U);
    EXPECT_EQ(aux_item_ids[1], 3U);
    const std::vector<uint32_t> aux_item_count
        = collect_u32_values(store, "aux.item_count");
    ASSERT_EQ(aux_item_count.size(), 1U);
    EXPECT_EQ(aux_item_count[0], 2U);
    const std::vector<uint32_t> aux_depth_count
        = collect_u32_values(store, "aux.depth_count");
    ASSERT_EQ(aux_depth_count.size(), 1U);
    EXPECT_EQ(aux_depth_count[0], 1U);
    const std::vector<uint32_t> aux_alpha_count
        = collect_u32_values(store, "aux.alpha_count");
    ASSERT_EQ(aux_alpha_count.size(), 1U);
    EXPECT_EQ(aux_alpha_count[0], 1U);
    EXPECT_TRUE(collect_u32_values(store, "aux.disparity_count").empty());
    EXPECT_TRUE(collect_u32_values(store, "aux.matte_count").empty());

    const std::vector<std::string> aux_semantic
        = collect_text_values(store, "aux.semantic");
    ASSERT_EQ(aux_semantic.size(), 2U);
    EXPECT_EQ(aux_semantic[0], "depth");
    EXPECT_EQ(aux_semantic[1], "alpha");

    const std::vector<std::string> aux_type = collect_text_values(store,
                                                                  "aux.type");
    ASSERT_EQ(aux_type.size(), 2U);
    EXPECT_EQ(aux_type[0], "urn:mpeg:hevc:2015:auxid:2");
    EXPECT_EQ(aux_type[1], "urn:mpeg:hevc:2015:auxid:1");

    const std::vector<std::string> aux_subtype
        = collect_text_values(store, "aux.subtype_hex");
    ASSERT_EQ(aux_subtype.size(), 2U);
    EXPECT_EQ(aux_subtype[0], "0xAABB");
    EXPECT_EQ(aux_subtype[1], "0x11");

    const std::vector<uint32_t> aux_subtype_len
        = collect_u32_values(store, "aux.subtype_len");
    ASSERT_EQ(aux_subtype_len.size(), 2U);
    EXPECT_EQ(aux_subtype_len[0], 2U);
    EXPECT_EQ(aux_subtype_len[1], 1U);

    const std::vector<std::string> aux_subtype_kind
        = collect_text_values(store, "aux.subtype_kind");
    ASSERT_EQ(aux_subtype_kind.size(), 2U);
    EXPECT_EQ(aux_subtype_kind[0], "u16be");
    EXPECT_EQ(aux_subtype_kind[1], "u8");

    const std::vector<uint32_t> aux_subtype_u32
        = collect_u32_values(store, "aux.subtype_u32");
    ASSERT_EQ(aux_subtype_u32.size(), 2U);
    EXPECT_EQ(aux_subtype_u32[0], 43707U);
    EXPECT_EQ(aux_subtype_u32[1], 17U);

    const std::vector<uint32_t> auxl_from
        = collect_u32_values(store, "iref.auxl.from_item_id");
    ASSERT_EQ(auxl_from.size(), 2U);
    EXPECT_EQ(auxl_from[0], 1U);
    EXPECT_EQ(auxl_from[1], 1U);

    const std::vector<uint32_t> auxl_to
        = collect_u32_values(store, "iref.auxl.to_item_id");
    ASSERT_EQ(auxl_to.size(), 2U);
    EXPECT_EQ(auxl_to[0], 2U);
    EXPECT_EQ(auxl_to[1], 3U);

    const std::vector<std::string> auxl_semantic
        = collect_text_values(store, "iref.auxl.semantic");
    ASSERT_EQ(auxl_semantic.size(), 2U);
    EXPECT_EQ(auxl_semantic[0], "depth");
    EXPECT_EQ(auxl_semantic[1], "alpha");

    const std::vector<std::string> auxl_type
        = collect_text_values(store, "iref.auxl.type");
    ASSERT_EQ(auxl_type.size(), 2U);
    EXPECT_EQ(auxl_type[0], "urn:mpeg:hevc:2015:auxid:2");
    EXPECT_EQ(auxl_type[1], "urn:mpeg:hevc:2015:auxid:1");

    const std::vector<std::string> auxl_subtype
        = collect_text_values(store, "iref.auxl.subtype_hex");
    ASSERT_EQ(auxl_subtype.size(), 2U);
    EXPECT_EQ(auxl_subtype[0], "0xAABB");
    EXPECT_EQ(auxl_subtype[1], "0x11");

    const std::vector<std::string> auxl_subtype_kind
        = collect_text_values(store, "iref.auxl.subtype_kind");
    ASSERT_EQ(auxl_subtype_kind.size(), 2U);
    EXPECT_EQ(auxl_subtype_kind[0], "u16be");
    EXPECT_EQ(auxl_subtype_kind[1], "u8");

    const std::vector<uint32_t> auxl_subtype_u32
        = collect_u32_values(store, "iref.auxl.subtype_u32");
    ASSERT_EQ(auxl_subtype_u32.size(), 2U);
    EXPECT_EQ(auxl_subtype_u32[0], 43707U);
    EXPECT_EQ(auxl_subtype_u32[1], 17U);
}

TEST(BmffDerivedFieldsDecode, EmitsPrimaryLinkedItemRoles)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> auxl_payload;
        append_u16be(&auxl_payload, 1);
        append_u16be(&auxl_payload, 3);
        append_u16be(&auxl_payload, 2);
        append_u16be(&auxl_payload, 3);
        append_u16be(&auxl_payload, 7);
        std::vector<std::byte> auxl_box;
        append_bmff_box(&auxl_box, fourcc('a', 'u', 'x', 'l'), auxl_payload);

        std::vector<std::byte> dimg_payload;
        append_u16be(&dimg_payload, 1);
        append_u16be(&dimg_payload, 1);
        append_u16be(&dimg_payload, 4);
        std::vector<std::byte> dimg_box;
        append_bmff_box(&dimg_box, fourcc('d', 'i', 'm', 'g'), dimg_payload);

        std::vector<std::byte> thmb_payload;
        append_u16be(&thmb_payload, 1);
        append_u16be(&thmb_payload, 1);
        append_u16be(&thmb_payload, 5);
        std::vector<std::byte> thmb_box;
        append_bmff_box(&thmb_box, fourcc('t', 'h', 'm', 'b'), thmb_payload);

        std::vector<std::byte> cdsc_payload;
        append_u16be(&cdsc_payload, 1);
        append_u16be(&cdsc_payload, 1);
        append_u16be(&cdsc_payload, 6);
        std::vector<std::byte> cdsc_box;
        append_bmff_box(&cdsc_box, fourcc('c', 'd', 's', 'c'), cdsc_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), auxl_box.begin(),
                            auxl_box.end());
        iref_payload.insert(iref_payload.end(), dimg_box.begin(),
                            dimg_box.end());
        iref_payload.insert(iref_payload.end(), thmb_box.begin(),
                            thmb_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_box.begin(),
                            cdsc_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> auxc_depth_payload;
        static constexpr char kDepth[] = "urn:mpeg:hevc:2015:auxid:2";
        append_auxc_payload(&auxc_depth_payload, kDepth, {});
        std::vector<std::byte> auxc_depth_box;
        append_bmff_box(&auxc_depth_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_depth_payload);

        std::vector<std::byte> auxc_alpha_payload;
        static constexpr char kAlpha[] = "urn:mpeg:hevc:2015:auxid:1";
        append_auxc_payload(&auxc_alpha_payload, kAlpha, {});
        std::vector<std::byte> auxc_alpha_box;
        append_bmff_box(&auxc_alpha_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_alpha_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), auxc_depth_box.begin(),
                            auxc_depth_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_alpha_box.begin(),
                            auxc_alpha_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 4);

        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte { 0 });

        append_u16be(&ipma_payload, 2);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 1 });

        append_u16be(&ipma_payload, 3);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 2 });

        append_u16be(&ipma_payload, 7);
        ipma_payload.push_back(std::byte { 0 });

        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 6);
        append_infe_v2(&iinf_payload, 2, 0, fourcc('a', 'u', 'x', 'l'),
                       "depth_aux");
        append_infe_v2(&iinf_payload, 3, 0, fourcc('a', 'u', 'x', 'l'),
                       "alpha_aux");
        append_infe_v2(&iinf_payload, 4, 0, fourcc('d', 'e', 'r', 'v'),
                       "derived");
        append_infe_v2(&iinf_payload, 5, 0, fourcc('t', 'h', 'm', 'b'),
                       "thumb");
        append_infe_v2(&iinf_payload, 6, 0, fourcc('c', 'd', 's', 'c'),
                       "caption");
        append_infe_v2(&iinf_payload, 7, 0, fourcc('a', 'u', 'x', 'l'),
                       "other_aux");
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> role_count
        = collect_u32_values(store, "primary.linked_item_role_count");
    ASSERT_EQ(role_count.size(), 1U);
    EXPECT_EQ(role_count[0], 6U);

    const std::vector<uint32_t> role_item_ids
        = collect_u32_values(store, "primary.linked_item_id");
    ASSERT_EQ(role_item_ids.size(), 6U);
    EXPECT_EQ(role_item_ids[0], 2U);
    EXPECT_EQ(role_item_ids[1], 3U);
    EXPECT_EQ(role_item_ids[2], 7U);
    EXPECT_EQ(role_item_ids[3], 4U);
    EXPECT_EQ(role_item_ids[4], 5U);
    EXPECT_EQ(role_item_ids[5], 6U);

    const std::vector<uint32_t> role_item_types
        = collect_u32_values(store, "primary.linked_item_type");
    ASSERT_EQ(role_item_types.size(), 6U);
    EXPECT_EQ(role_item_types[0], fourcc('a', 'u', 'x', 'l'));
    EXPECT_EQ(role_item_types[1], fourcc('a', 'u', 'x', 'l'));
    EXPECT_EQ(role_item_types[2], fourcc('a', 'u', 'x', 'l'));
    EXPECT_EQ(role_item_types[3], fourcc('d', 'e', 'r', 'v'));
    EXPECT_EQ(role_item_types[4], fourcc('t', 'h', 'm', 'b'));
    EXPECT_EQ(role_item_types[5], fourcc('c', 'd', 's', 'c'));

    const std::vector<std::string> role_item_names
        = collect_text_values(store, "primary.linked_item_name");
    ASSERT_EQ(role_item_names.size(), 6U);
    EXPECT_EQ(role_item_names[0], "depth_aux");
    EXPECT_EQ(role_item_names[1], "alpha_aux");
    EXPECT_EQ(role_item_names[2], "other_aux");
    EXPECT_EQ(role_item_names[3], "derived");
    EXPECT_EQ(role_item_names[4], "thumb");
    EXPECT_EQ(role_item_names[5], "caption");

    const std::vector<std::string> roles
        = collect_text_values(store, "primary.linked_item_role");
    ASSERT_EQ(roles.size(), 6U);
    EXPECT_EQ(roles[0], "depth");
    EXPECT_EQ(roles[1], "alpha");
    EXPECT_EQ(roles[2], "auxiliary");
    EXPECT_EQ(roles[3], "derived");
    EXPECT_EQ(roles[4], "thumbnail");
    EXPECT_EQ(roles[5], "content_description");
}

TEST(BmffDerivedFieldsDecode, EmitsDisparityAndMatteAuxCountsFromAuxC)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> auxl_payload;
        append_u16be(&auxl_payload, 1);
        append_u16be(&auxl_payload, 2);
        append_u16be(&auxl_payload, 2);
        append_u16be(&auxl_payload, 3);
        std::vector<std::byte> auxl_box;
        append_bmff_box(&auxl_box, fourcc('a', 'u', 'x', 'l'), auxl_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), auxl_box.begin(),
                            auxl_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> auxc_disparity_payload;
        static constexpr char kDisparity[]
            = "urn:mpeg:mpegB:cicp:systems:auxiliary:disparity";
        append_auxc_payload(&auxc_disparity_payload, kDisparity, {});
        auxc_disparity_payload.push_back(std::byte { 0x44 });
        std::vector<std::byte> auxc_disparity_box;
        append_bmff_box(&auxc_disparity_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_disparity_payload);

        std::vector<std::byte> auxc_matte_payload;
        static constexpr char kMatte[]
            = "urn:mpeg:mpegB:cicp:systems:auxiliary:matte";
        append_auxc_payload(&auxc_matte_payload, kMatte, {});
        auxc_matte_payload.push_back(std::byte { 0x55 });
        std::vector<std::byte> auxc_matte_box;
        append_bmff_box(&auxc_matte_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_matte_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), auxc_disparity_box.begin(),
                            auxc_disparity_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_matte_box.begin(),
                            auxc_matte_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 3);

        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte { 0 });

        append_u16be(&ipma_payload, 2);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 1 });

        append_u16be(&ipma_payload, 3);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 2 });

        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> aux_item_count
        = collect_u32_values(store, "aux.item_count");
    ASSERT_EQ(aux_item_count.size(), 1U);
    EXPECT_EQ(aux_item_count[0], 2U);

    const std::vector<uint32_t> disparity_count
        = collect_u32_values(store, "aux.disparity_count");
    ASSERT_EQ(disparity_count.size(), 1U);
    EXPECT_EQ(disparity_count[0], 1U);
    const std::vector<uint32_t> primary_disparity_count
        = collect_u32_values(store, "primary.disparity_count");
    ASSERT_EQ(primary_disparity_count.size(), 1U);
    EXPECT_EQ(primary_disparity_count[0], 1U);
    const std::vector<uint32_t> primary_disparity_ids
        = collect_u32_values(store, "primary.disparity_item_id");
    ASSERT_EQ(primary_disparity_ids.size(), 1U);
    EXPECT_EQ(primary_disparity_ids[0], 2U);

    const std::vector<uint32_t> matte_count
        = collect_u32_values(store, "aux.matte_count");
    ASSERT_EQ(matte_count.size(), 1U);
    EXPECT_EQ(matte_count[0], 1U);
    const std::vector<uint32_t> primary_matte_count
        = collect_u32_values(store, "primary.matte_count");
    ASSERT_EQ(primary_matte_count.size(), 1U);
    EXPECT_EQ(primary_matte_count[0], 1U);
    const std::vector<uint32_t> primary_matte_ids
        = collect_u32_values(store, "primary.matte_item_id");
    ASSERT_EQ(primary_matte_ids.size(), 1U);
    EXPECT_EQ(primary_matte_ids[0], 3U);

    EXPECT_TRUE(collect_u32_values(store, "aux.alpha_count").empty());
    EXPECT_TRUE(collect_u32_values(store, "aux.depth_count").empty());

    const std::vector<std::string> aux_semantic
        = collect_text_values(store, "aux.semantic");
    ASSERT_EQ(aux_semantic.size(), 2U);
    EXPECT_EQ(aux_semantic[0], "disparity");
    EXPECT_EQ(aux_semantic[1], "matte");

    const std::vector<std::string> primary_auxl_semantic
        = collect_text_values(store, "primary.auxl_semantic");
    ASSERT_EQ(primary_auxl_semantic.size(), 2U);
    EXPECT_EQ(primary_auxl_semantic[0], "disparity");
    EXPECT_EQ(primary_auxl_semantic[1], "matte");
}

TEST(BmffDerivedFieldsDecode, EmitsNonPrimaryIrefTypedEdges)
{
    // Minimal HEIF-like BMFF:
    // - primary item id = 1
    // - iref edges on non-primary items:
    //   dimg: 2 -> [5,6]
    //   thmb: 3 -> [7]
    //   cdsc: 4 -> [8]

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> dimg_payload;
        append_u16be(&dimg_payload, 2);  // from item id
        append_u16be(&dimg_payload, 2);  // ref count
        append_u16be(&dimg_payload, 5);  // to item id
        append_u16be(&dimg_payload, 6);  // to item id
        std::vector<std::byte> dimg_box;
        append_bmff_box(&dimg_box, fourcc('d', 'i', 'm', 'g'), dimg_payload);

        std::vector<std::byte> thmb_payload;
        append_u16be(&thmb_payload, 3);  // from item id
        append_u16be(&thmb_payload, 1);  // ref count
        append_u16be(&thmb_payload, 7);  // to item id
        std::vector<std::byte> thmb_box;
        append_bmff_box(&thmb_box, fourcc('t', 'h', 'm', 'b'), thmb_payload);

        std::vector<std::byte> cdsc_payload;
        append_u16be(&cdsc_payload, 4);  // from item id
        append_u16be(&cdsc_payload, 1);  // ref count
        append_u16be(&cdsc_payload, 8);  // to item id
        std::vector<std::byte> cdsc_box;
        append_bmff_box(&cdsc_box, fourcc('c', 'd', 's', 'c'), cdsc_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), dimg_box.begin(),
                            dimg_box.end());
        iref_payload.insert(iref_payload.end(), thmb_box.begin(),
                            thmb_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_box.begin(),
                            cdsc_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> edge_count
        = collect_u32_values(store, "iref.edge_count");
    ASSERT_EQ(edge_count.size(), 1U);
    EXPECT_EQ(edge_count[0], 4U);

    const std::vector<uint32_t> dimg_count
        = collect_u32_values(store, "iref.dimg.edge_count");
    ASSERT_EQ(dimg_count.size(), 1U);
    EXPECT_EQ(dimg_count[0], 2U);
    const std::vector<uint32_t> dimg_graph_count
        = collect_u32_values(store, "iref.graph.dimg.edge_count");
    ASSERT_EQ(dimg_graph_count.size(), 1U);
    EXPECT_EQ(dimg_graph_count[0], 2U);
    const std::vector<uint32_t> dimg_from_unique
        = collect_u32_values(store, "iref.dimg.from_item_unique_count");
    ASSERT_EQ(dimg_from_unique.size(), 1U);
    EXPECT_EQ(dimg_from_unique[0], 1U);
    const std::vector<uint32_t> dimg_graph_from_unique
        = collect_u32_values(store, "iref.graph.dimg.from_item_unique_count");
    ASSERT_EQ(dimg_graph_from_unique.size(), 1U);
    EXPECT_EQ(dimg_graph_from_unique[0], 1U);
    const std::vector<uint32_t> dimg_to_unique
        = collect_u32_values(store, "iref.dimg.to_item_unique_count");
    ASSERT_EQ(dimg_to_unique.size(), 1U);
    EXPECT_EQ(dimg_to_unique[0], 2U);
    const std::vector<uint32_t> dimg_graph_to_unique
        = collect_u32_values(store, "iref.graph.dimg.to_item_unique_count");
    ASSERT_EQ(dimg_graph_to_unique.size(), 1U);
    EXPECT_EQ(dimg_graph_to_unique[0], 2U);

    const std::vector<uint32_t> thmb_count
        = collect_u32_values(store, "iref.thmb.edge_count");
    ASSERT_EQ(thmb_count.size(), 1U);
    EXPECT_EQ(thmb_count[0], 1U);
    const std::vector<uint32_t> thmb_graph_count
        = collect_u32_values(store, "iref.graph.thmb.edge_count");
    ASSERT_EQ(thmb_graph_count.size(), 1U);
    EXPECT_EQ(thmb_graph_count[0], 1U);
    const std::vector<uint32_t> thmb_from_unique
        = collect_u32_values(store, "iref.thmb.from_item_unique_count");
    ASSERT_EQ(thmb_from_unique.size(), 1U);
    EXPECT_EQ(thmb_from_unique[0], 1U);
    const std::vector<uint32_t> thmb_graph_from_unique
        = collect_u32_values(store, "iref.graph.thmb.from_item_unique_count");
    ASSERT_EQ(thmb_graph_from_unique.size(), 1U);
    EXPECT_EQ(thmb_graph_from_unique[0], 1U);
    const std::vector<uint32_t> thmb_to_unique
        = collect_u32_values(store, "iref.thmb.to_item_unique_count");
    ASSERT_EQ(thmb_to_unique.size(), 1U);
    EXPECT_EQ(thmb_to_unique[0], 1U);
    const std::vector<uint32_t> thmb_graph_to_unique
        = collect_u32_values(store, "iref.graph.thmb.to_item_unique_count");
    ASSERT_EQ(thmb_graph_to_unique.size(), 1U);
    EXPECT_EQ(thmb_graph_to_unique[0], 1U);

    const std::vector<uint32_t> cdsc_count
        = collect_u32_values(store, "iref.cdsc.edge_count");
    ASSERT_EQ(cdsc_count.size(), 1U);
    EXPECT_EQ(cdsc_count[0], 1U);
    const std::vector<uint32_t> cdsc_graph_count
        = collect_u32_values(store, "iref.graph.cdsc.edge_count");
    ASSERT_EQ(cdsc_graph_count.size(), 1U);
    EXPECT_EQ(cdsc_graph_count[0], 1U);
    const std::vector<uint32_t> cdsc_from_unique
        = collect_u32_values(store, "iref.cdsc.from_item_unique_count");
    ASSERT_EQ(cdsc_from_unique.size(), 1U);
    EXPECT_EQ(cdsc_from_unique[0], 1U);
    const std::vector<uint32_t> cdsc_graph_from_unique
        = collect_u32_values(store, "iref.graph.cdsc.from_item_unique_count");
    ASSERT_EQ(cdsc_graph_from_unique.size(), 1U);
    EXPECT_EQ(cdsc_graph_from_unique[0], 1U);
    const std::vector<uint32_t> cdsc_to_unique
        = collect_u32_values(store, "iref.cdsc.to_item_unique_count");
    ASSERT_EQ(cdsc_to_unique.size(), 1U);
    EXPECT_EQ(cdsc_to_unique[0], 1U);
    const std::vector<uint32_t> cdsc_graph_to_unique
        = collect_u32_values(store, "iref.graph.cdsc.to_item_unique_count");
    ASSERT_EQ(cdsc_graph_to_unique.size(), 1U);
    EXPECT_EQ(cdsc_graph_to_unique[0], 1U);

    EXPECT_TRUE(
        collect_u32_values(store, "iref.graph.auxl.edge_count").empty());

    const std::vector<uint32_t> item_count
        = collect_u32_values(store, "iref.item_count");
    ASSERT_EQ(item_count.size(), 1U);
    EXPECT_EQ(item_count[0], 7U);
    const std::vector<uint32_t> from_unique_count
        = collect_u32_values(store, "iref.from_item_unique_count");
    ASSERT_EQ(from_unique_count.size(), 1U);
    EXPECT_EQ(from_unique_count[0], 3U);
    const std::vector<uint32_t> to_unique_count
        = collect_u32_values(store, "iref.to_item_unique_count");
    ASSERT_EQ(to_unique_count.size(), 1U);
    EXPECT_EQ(to_unique_count[0], 4U);

    const std::vector<uint32_t> dimg_from
        = collect_u32_values(store, "iref.dimg.from_item_id");
    ASSERT_EQ(dimg_from.size(), 2U);
    EXPECT_EQ(dimg_from[0], 2U);
    EXPECT_EQ(dimg_from[1], 2U);

    const std::vector<uint32_t> dimg_to
        = collect_u32_values(store, "iref.dimg.to_item_id");
    ASSERT_EQ(dimg_to.size(), 2U);
    EXPECT_EQ(dimg_to[0], 5U);
    EXPECT_EQ(dimg_to[1], 6U);

    const std::vector<uint32_t> thmb_from
        = collect_u32_values(store, "iref.thmb.from_item_id");
    ASSERT_EQ(thmb_from.size(), 1U);
    EXPECT_EQ(thmb_from[0], 3U);

    const std::vector<uint32_t> thmb_to
        = collect_u32_values(store, "iref.thmb.to_item_id");
    ASSERT_EQ(thmb_to.size(), 1U);
    EXPECT_EQ(thmb_to[0], 7U);

    const std::vector<uint32_t> cdsc_from
        = collect_u32_values(store, "iref.cdsc.from_item_id");
    ASSERT_EQ(cdsc_from.size(), 1U);
    EXPECT_EQ(cdsc_from[0], 4U);

    const std::vector<uint32_t> cdsc_to
        = collect_u32_values(store, "iref.cdsc.to_item_id");
    ASSERT_EQ(cdsc_to.size(), 1U);
    EXPECT_EQ(cdsc_to[0], 8U);

    EXPECT_TRUE(collect_u32_values(store, "primary.dimg_item_id").empty());
    EXPECT_TRUE(collect_u32_values(store, "primary.thmb_item_id").empty());
    EXPECT_TRUE(collect_u32_values(store, "primary.cdsc_item_id").empty());
}

TEST(BmffDerivedFieldsDecode, EmitsDynamicIrefTypedEdgesForUnknownAsciiFourcc)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> pred_payload;
        append_u16be(&pred_payload, 9);   // from item id
        append_u16be(&pred_payload, 2);   // ref count
        append_u16be(&pred_payload, 10);  // to item id
        append_u16be(&pred_payload, 11);  // to item id
        std::vector<std::byte> pred_box;
        append_bmff_box(&pred_box, fourcc('p', 'r', 'e', 'd'), pred_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), pred_box.begin(),
                            pred_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<std::string> ref_type_names
        = collect_text_values(store, "iref.ref_type_name");
    ASSERT_EQ(ref_type_names.size(), 2U);
    EXPECT_EQ(ref_type_names[0], "pred");
    EXPECT_EQ(ref_type_names[1], "pred");

    const std::vector<uint32_t> pred_count
        = collect_u32_values(store, "iref.pred.edge_count");
    ASSERT_EQ(pred_count.size(), 1U);
    EXPECT_EQ(pred_count[0], 2U);
    const std::vector<uint32_t> pred_graph_count
        = collect_u32_values(store, "iref.graph.pred.edge_count");
    ASSERT_EQ(pred_graph_count.size(), 1U);
    EXPECT_EQ(pred_graph_count[0], 2U);

    const std::vector<uint32_t> pred_from
        = collect_u32_values(store, "iref.pred.from_item_id");
    ASSERT_EQ(pred_from.size(), 2U);
    EXPECT_EQ(pred_from[0], 9U);
    EXPECT_EQ(pred_from[1], 9U);
    const std::vector<uint32_t> pred_to
        = collect_u32_values(store, "iref.pred.to_item_id");
    ASSERT_EQ(pred_to.size(), 2U);
    EXPECT_EQ(pred_to[0], 10U);
    EXPECT_EQ(pred_to[1], 11U);

    const std::vector<uint32_t> pred_item_count
        = collect_u32_values(store, "iref.pred.item_count");
    ASSERT_EQ(pred_item_count.size(), 1U);
    EXPECT_EQ(pred_item_count[0], 3U);
    const std::vector<uint32_t> pred_item_ids
        = collect_u32_values(store, "iref.pred.item_id");
    ASSERT_EQ(pred_item_ids.size(), 3U);
    EXPECT_EQ(pred_item_ids[0], 9U);
    EXPECT_EQ(pred_item_ids[1], 10U);
    EXPECT_EQ(pred_item_ids[2], 11U);
}

TEST(BmffDerivedFieldsDecode, EmitsAuxSubtypeU64AndAsciiZFromAuxC)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> auxl_payload;
        append_u16be(&auxl_payload, 1);
        append_u16be(&auxl_payload, 3);
        append_u16be(&auxl_payload, 2);
        append_u16be(&auxl_payload, 3);
        append_u16be(&auxl_payload, 4);
        std::vector<std::byte> auxl_box;
        append_bmff_box(&auxl_box, fourcc('a', 'u', 'x', 'l'), auxl_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), auxl_box.begin(),
                            auxl_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> auxc_depth_payload;
        append_fullbox_header(&auxc_depth_payload, 0);
        static constexpr char kDepth[] = "urn:mpeg:hevc:2015:auxid:2";
        for (size_t i = 0; i < sizeof(kDepth) - 1; ++i) {
            auxc_depth_payload.push_back(
                std::byte { static_cast<uint8_t>(kDepth[i]) });
        }
        auxc_depth_payload.push_back(std::byte { 0x00 });
        static constexpr char kAsciiZ[] = "profile";
        for (size_t i = 0; i < sizeof(kAsciiZ) - 1; ++i) {
            auxc_depth_payload.push_back(
                std::byte { static_cast<uint8_t>(kAsciiZ[i]) });
        }
        auxc_depth_payload.push_back(std::byte { 0x00 });
        std::vector<std::byte> auxc_depth_box;
        append_bmff_box(&auxc_depth_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_depth_payload);

        std::vector<std::byte> auxc_alpha_payload;
        append_fullbox_header(&auxc_alpha_payload, 0);
        static constexpr char kAlpha[] = "urn:mpeg:hevc:2015:auxid:1";
        for (size_t i = 0; i < sizeof(kAlpha) - 1; ++i) {
            auxc_alpha_payload.push_back(
                std::byte { static_cast<uint8_t>(kAlpha[i]) });
        }
        auxc_alpha_payload.push_back(std::byte { 0x00 });
        auxc_alpha_payload.push_back(std::byte { 0x11 });
        auxc_alpha_payload.push_back(std::byte { 0x22 });
        auxc_alpha_payload.push_back(std::byte { 0x33 });
        auxc_alpha_payload.push_back(std::byte { 0x44 });
        auxc_alpha_payload.push_back(std::byte { 0x55 });
        auxc_alpha_payload.push_back(std::byte { 0x66 });
        auxc_alpha_payload.push_back(std::byte { 0x77 });
        auxc_alpha_payload.push_back(std::byte { 0x88 });
        std::vector<std::byte> auxc_alpha_box;
        append_bmff_box(&auxc_alpha_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_alpha_payload);

        std::vector<std::byte> auxc_uuid_payload;
        append_fullbox_header(&auxc_uuid_payload, 0);
        for (size_t i = 0; i < sizeof(kAlpha) - 1; ++i) {
            auxc_uuid_payload.push_back(
                std::byte { static_cast<uint8_t>(kAlpha[i]) });
        }
        auxc_uuid_payload.push_back(std::byte { 0x00 });
        for (uint8_t i = 0; i < 16U; ++i) {
            auxc_uuid_payload.push_back(std::byte { i });
        }
        std::vector<std::byte> auxc_uuid_box;
        append_bmff_box(&auxc_uuid_box, fourcc('a', 'u', 'x', 'C'),
                        auxc_uuid_payload);

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), auxc_depth_box.begin(),
                            auxc_depth_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_alpha_box.begin(),
                            auxc_alpha_box.end());
        ipco_payload.insert(ipco_payload.end(), auxc_uuid_box.begin(),
                            auxc_uuid_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 4);

        append_u16be(&ipma_payload, 1);
        ipma_payload.push_back(std::byte { 0 });

        append_u16be(&ipma_payload, 2);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 1 });

        append_u16be(&ipma_payload, 3);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 2 });

        append_u16be(&ipma_payload, 4);
        ipma_payload.push_back(std::byte { 1 });
        ipma_payload.push_back(std::byte { 3 });
        std::vector<std::byte> ipma_box;
        append_bmff_box(&ipma_box, fourcc('i', 'p', 'm', 'a'), ipma_payload);

        std::vector<std::byte> iprp_payload;
        iprp_payload.insert(iprp_payload.end(), ipco_box.begin(),
                            ipco_box.end());
        iprp_payload.insert(iprp_payload.end(), ipma_box.begin(),
                            ipma_box.end());
        std::vector<std::byte> iprp_box;
        append_bmff_box(&iprp_box, fourcc('i', 'p', 'r', 'p'), iprp_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        meta_payload.insert(meta_payload.end(), iprp_box.begin(),
                            iprp_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<std::string> aux_subtype_kind
        = collect_text_values(store, "aux.subtype_kind");
    ASSERT_EQ(aux_subtype_kind.size(), 3U);
    EXPECT_EQ(aux_subtype_kind[0], "ascii_z");
    EXPECT_EQ(aux_subtype_kind[1], "u64be");
    EXPECT_EQ(aux_subtype_kind[2], "uuid");

    const std::vector<std::string> aux_subtype_text
        = collect_text_values(store, "aux.subtype_text");
    ASSERT_EQ(aux_subtype_text.size(), 2U);
    EXPECT_EQ(aux_subtype_text[0], "profile");
    EXPECT_EQ(aux_subtype_text[1], "00010203-0405-0607-0809-0A0B0C0D0E0F");

    const std::vector<std::string> aux_subtype_uuid
        = collect_text_values(store, "aux.subtype_uuid");
    ASSERT_EQ(aux_subtype_uuid.size(), 1U);
    EXPECT_EQ(aux_subtype_uuid[0], "00010203-0405-0607-0809-0A0B0C0D0E0F");

    const std::vector<uint64_t> aux_subtype_u64
        = collect_u64_values(store, "aux.subtype_u64");
    ASSERT_EQ(aux_subtype_u64.size(), 1U);
    EXPECT_EQ(aux_subtype_u64[0], 0x1122334455667788ULL);

    const std::vector<std::string> iref_auxl_subtype_kind
        = collect_text_values(store, "iref.auxl.subtype_kind");
    ASSERT_EQ(iref_auxl_subtype_kind.size(), 3U);
    EXPECT_EQ(iref_auxl_subtype_kind[0], "ascii_z");
    EXPECT_EQ(iref_auxl_subtype_kind[1], "u64be");
    EXPECT_EQ(iref_auxl_subtype_kind[2], "uuid");

    const std::vector<uint64_t> iref_auxl_subtype_u64
        = collect_u64_values(store, "iref.auxl.subtype_u64");
    ASSERT_EQ(iref_auxl_subtype_u64.size(), 1U);
    EXPECT_EQ(iref_auxl_subtype_u64[0], 0x1122334455667788ULL);

    const std::vector<std::string> iref_auxl_subtype_uuid
        = collect_text_values(store, "iref.auxl.subtype_uuid");
    ASSERT_EQ(iref_auxl_subtype_uuid.size(), 1U);
    EXPECT_EQ(iref_auxl_subtype_uuid[0],
              "00010203-0405-0607-0809-0A0B0C0D0E0F");
}

TEST(BmffDerivedFieldsDecode, EmitsPerTypeUniqueCountsWithDuplicateEdges)
{
    // Minimal HEIF-like BMFF with duplicate iref edges so per-type
    // unique counters can be distinguished from edge counters.
    //
    // auxl: 1 -> [2,2,3] and 1 -> [3]        => edge=4, from_unique=1, to=2
    // dimg: 2 -> [5,5] and 4 -> [5]          => edge=3, from_unique=2, to=1
    // thmb: 3 -> [7,7] and 3 -> [8]          => edge=3, from_unique=1, to=2
    // cdsc: 4 -> [8,9,9] and 5 -> [8]        => edge=4, from_unique=2, to=2

    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> auxl_a_payload;
        append_u16be(&auxl_a_payload, 1);
        append_u16be(&auxl_a_payload, 3);
        append_u16be(&auxl_a_payload, 2);
        append_u16be(&auxl_a_payload, 2);
        append_u16be(&auxl_a_payload, 3);
        std::vector<std::byte> auxl_a_box;
        append_bmff_box(&auxl_a_box, fourcc('a', 'u', 'x', 'l'),
                        auxl_a_payload);

        std::vector<std::byte> auxl_b_payload;
        append_u16be(&auxl_b_payload, 1);
        append_u16be(&auxl_b_payload, 1);
        append_u16be(&auxl_b_payload, 3);
        std::vector<std::byte> auxl_b_box;
        append_bmff_box(&auxl_b_box, fourcc('a', 'u', 'x', 'l'),
                        auxl_b_payload);

        std::vector<std::byte> dimg_a_payload;
        append_u16be(&dimg_a_payload, 2);
        append_u16be(&dimg_a_payload, 2);
        append_u16be(&dimg_a_payload, 5);
        append_u16be(&dimg_a_payload, 5);
        std::vector<std::byte> dimg_a_box;
        append_bmff_box(&dimg_a_box, fourcc('d', 'i', 'm', 'g'),
                        dimg_a_payload);

        std::vector<std::byte> dimg_b_payload;
        append_u16be(&dimg_b_payload, 4);
        append_u16be(&dimg_b_payload, 1);
        append_u16be(&dimg_b_payload, 5);
        std::vector<std::byte> dimg_b_box;
        append_bmff_box(&dimg_b_box, fourcc('d', 'i', 'm', 'g'),
                        dimg_b_payload);

        std::vector<std::byte> thmb_a_payload;
        append_u16be(&thmb_a_payload, 3);
        append_u16be(&thmb_a_payload, 2);
        append_u16be(&thmb_a_payload, 7);
        append_u16be(&thmb_a_payload, 7);
        std::vector<std::byte> thmb_a_box;
        append_bmff_box(&thmb_a_box, fourcc('t', 'h', 'm', 'b'),
                        thmb_a_payload);

        std::vector<std::byte> thmb_b_payload;
        append_u16be(&thmb_b_payload, 3);
        append_u16be(&thmb_b_payload, 1);
        append_u16be(&thmb_b_payload, 8);
        std::vector<std::byte> thmb_b_box;
        append_bmff_box(&thmb_b_box, fourcc('t', 'h', 'm', 'b'),
                        thmb_b_payload);

        std::vector<std::byte> cdsc_a_payload;
        append_u16be(&cdsc_a_payload, 4);
        append_u16be(&cdsc_a_payload, 3);
        append_u16be(&cdsc_a_payload, 8);
        append_u16be(&cdsc_a_payload, 9);
        append_u16be(&cdsc_a_payload, 9);
        std::vector<std::byte> cdsc_a_box;
        append_bmff_box(&cdsc_a_box, fourcc('c', 'd', 's', 'c'),
                        cdsc_a_payload);

        std::vector<std::byte> cdsc_b_payload;
        append_u16be(&cdsc_b_payload, 5);
        append_u16be(&cdsc_b_payload, 1);
        append_u16be(&cdsc_b_payload, 8);
        std::vector<std::byte> cdsc_b_box;
        append_bmff_box(&cdsc_b_box, fourcc('c', 'd', 's', 'c'),
                        cdsc_b_payload);

        std::vector<std::byte> iref_payload;
        append_fullbox_header(&iref_payload, 0);
        iref_payload.insert(iref_payload.end(), auxl_a_box.begin(),
                            auxl_a_box.end());
        iref_payload.insert(iref_payload.end(), auxl_b_box.begin(),
                            auxl_b_box.end());
        iref_payload.insert(iref_payload.end(), dimg_a_box.begin(),
                            dimg_a_box.end());
        iref_payload.insert(iref_payload.end(), dimg_b_box.begin(),
                            dimg_b_box.end());
        iref_payload.insert(iref_payload.end(), thmb_a_box.begin(),
                            thmb_a_box.end());
        iref_payload.insert(iref_payload.end(), thmb_b_box.begin(),
                            thmb_b_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_a_box.begin(),
                            cdsc_a_box.end());
        iref_payload.insert(iref_payload.end(), cdsc_b_box.begin(),
                            cdsc_b_box.end());
        std::vector<std::byte> iref_box;
        append_bmff_box(&iref_box, fourcc('i', 'r', 'e', 'f'), iref_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iref_box.begin(),
                            iref_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> edge_count
        = collect_u32_values(store, "iref.edge_count");
    ASSERT_EQ(edge_count.size(), 1U);
    EXPECT_EQ(edge_count[0], 14U);

    const std::vector<uint32_t> auxl_edge
        = collect_u32_values(store, "iref.auxl.edge_count");
    ASSERT_EQ(auxl_edge.size(), 1U);
    EXPECT_EQ(auxl_edge[0], 4U);
    const std::vector<uint32_t> auxl_graph_edge
        = collect_u32_values(store, "iref.graph.auxl.edge_count");
    ASSERT_EQ(auxl_graph_edge.size(), 1U);
    EXPECT_EQ(auxl_graph_edge[0], 4U);
    const std::vector<uint32_t> auxl_from_unique
        = collect_u32_values(store, "iref.auxl.from_item_unique_count");
    ASSERT_EQ(auxl_from_unique.size(), 1U);
    EXPECT_EQ(auxl_from_unique[0], 1U);
    const std::vector<uint32_t> auxl_graph_from_unique
        = collect_u32_values(store, "iref.graph.auxl.from_item_unique_count");
    ASSERT_EQ(auxl_graph_from_unique.size(), 1U);
    EXPECT_EQ(auxl_graph_from_unique[0], 1U);
    const std::vector<uint32_t> auxl_to_unique
        = collect_u32_values(store, "iref.auxl.to_item_unique_count");
    ASSERT_EQ(auxl_to_unique.size(), 1U);
    EXPECT_EQ(auxl_to_unique[0], 2U);
    const std::vector<uint32_t> auxl_graph_to_unique
        = collect_u32_values(store, "iref.graph.auxl.to_item_unique_count");
    ASSERT_EQ(auxl_graph_to_unique.size(), 1U);
    EXPECT_EQ(auxl_graph_to_unique[0], 2U);

    const std::vector<uint32_t> dimg_edge
        = collect_u32_values(store, "iref.dimg.edge_count");
    ASSERT_EQ(dimg_edge.size(), 1U);
    EXPECT_EQ(dimg_edge[0], 3U);
    const std::vector<uint32_t> dimg_graph_edge
        = collect_u32_values(store, "iref.graph.dimg.edge_count");
    ASSERT_EQ(dimg_graph_edge.size(), 1U);
    EXPECT_EQ(dimg_graph_edge[0], 3U);
    const std::vector<uint32_t> dimg_from_unique
        = collect_u32_values(store, "iref.dimg.from_item_unique_count");
    ASSERT_EQ(dimg_from_unique.size(), 1U);
    EXPECT_EQ(dimg_from_unique[0], 2U);
    const std::vector<uint32_t> dimg_graph_from_unique
        = collect_u32_values(store, "iref.graph.dimg.from_item_unique_count");
    ASSERT_EQ(dimg_graph_from_unique.size(), 1U);
    EXPECT_EQ(dimg_graph_from_unique[0], 2U);
    const std::vector<uint32_t> dimg_to_unique
        = collect_u32_values(store, "iref.dimg.to_item_unique_count");
    ASSERT_EQ(dimg_to_unique.size(), 1U);
    EXPECT_EQ(dimg_to_unique[0], 1U);
    const std::vector<uint32_t> dimg_graph_to_unique
        = collect_u32_values(store, "iref.graph.dimg.to_item_unique_count");
    ASSERT_EQ(dimg_graph_to_unique.size(), 1U);
    EXPECT_EQ(dimg_graph_to_unique[0], 1U);

    const std::vector<uint32_t> thmb_edge
        = collect_u32_values(store, "iref.thmb.edge_count");
    ASSERT_EQ(thmb_edge.size(), 1U);
    EXPECT_EQ(thmb_edge[0], 3U);
    const std::vector<uint32_t> thmb_graph_edge
        = collect_u32_values(store, "iref.graph.thmb.edge_count");
    ASSERT_EQ(thmb_graph_edge.size(), 1U);
    EXPECT_EQ(thmb_graph_edge[0], 3U);
    const std::vector<uint32_t> thmb_from_unique
        = collect_u32_values(store, "iref.thmb.from_item_unique_count");
    ASSERT_EQ(thmb_from_unique.size(), 1U);
    EXPECT_EQ(thmb_from_unique[0], 1U);
    const std::vector<uint32_t> thmb_graph_from_unique
        = collect_u32_values(store, "iref.graph.thmb.from_item_unique_count");
    ASSERT_EQ(thmb_graph_from_unique.size(), 1U);
    EXPECT_EQ(thmb_graph_from_unique[0], 1U);
    const std::vector<uint32_t> thmb_to_unique
        = collect_u32_values(store, "iref.thmb.to_item_unique_count");
    ASSERT_EQ(thmb_to_unique.size(), 1U);
    EXPECT_EQ(thmb_to_unique[0], 2U);
    const std::vector<uint32_t> thmb_graph_to_unique
        = collect_u32_values(store, "iref.graph.thmb.to_item_unique_count");
    ASSERT_EQ(thmb_graph_to_unique.size(), 1U);
    EXPECT_EQ(thmb_graph_to_unique[0], 2U);

    const std::vector<uint32_t> cdsc_edge
        = collect_u32_values(store, "iref.cdsc.edge_count");
    ASSERT_EQ(cdsc_edge.size(), 1U);
    EXPECT_EQ(cdsc_edge[0], 4U);
    const std::vector<uint32_t> cdsc_graph_edge
        = collect_u32_values(store, "iref.graph.cdsc.edge_count");
    ASSERT_EQ(cdsc_graph_edge.size(), 1U);
    EXPECT_EQ(cdsc_graph_edge[0], 4U);
    const std::vector<uint32_t> cdsc_from_unique
        = collect_u32_values(store, "iref.cdsc.from_item_unique_count");
    ASSERT_EQ(cdsc_from_unique.size(), 1U);
    EXPECT_EQ(cdsc_from_unique[0], 2U);
    const std::vector<uint32_t> cdsc_graph_from_unique
        = collect_u32_values(store, "iref.graph.cdsc.from_item_unique_count");
    ASSERT_EQ(cdsc_graph_from_unique.size(), 1U);
    EXPECT_EQ(cdsc_graph_from_unique[0], 2U);
    const std::vector<uint32_t> cdsc_to_unique
        = collect_u32_values(store, "iref.cdsc.to_item_unique_count");
    ASSERT_EQ(cdsc_to_unique.size(), 1U);
    EXPECT_EQ(cdsc_to_unique[0], 2U);
    const std::vector<uint32_t> cdsc_graph_to_unique
        = collect_u32_values(store, "iref.graph.cdsc.to_item_unique_count");
    ASSERT_EQ(cdsc_graph_to_unique.size(), 1U);
    EXPECT_EQ(cdsc_graph_to_unique[0], 2U);

    const std::vector<uint32_t> item_count
        = collect_u32_values(store, "iref.item_count");
    ASSERT_EQ(item_count.size(), 1U);
    EXPECT_EQ(item_count[0], 8U);
    const std::vector<uint32_t> from_unique_count
        = collect_u32_values(store, "iref.from_item_unique_count");
    ASSERT_EQ(from_unique_count.size(), 1U);
    EXPECT_EQ(from_unique_count[0], 5U);
    const std::vector<uint32_t> to_unique_count
        = collect_u32_values(store, "iref.to_item_unique_count");
    ASSERT_EQ(to_unique_count.size(), 1U);
    EXPECT_EQ(to_unique_count[0], 6U);

    const std::vector<uint32_t> auxl_item_count
        = collect_u32_values(store, "iref.auxl.item_count");
    ASSERT_EQ(auxl_item_count.size(), 1U);
    EXPECT_EQ(auxl_item_count[0], 3U);
    const std::vector<uint32_t> auxl_item_ids
        = collect_u32_values(store, "iref.auxl.item_id");
    ASSERT_EQ(auxl_item_ids.size(), 3U);
    EXPECT_EQ(auxl_item_ids[0], 1U);
    EXPECT_EQ(auxl_item_ids[1], 2U);
    EXPECT_EQ(auxl_item_ids[2], 3U);
    const std::vector<uint32_t> auxl_out
        = collect_u32_values(store, "iref.auxl.item_out_edge_count");
    ASSERT_EQ(auxl_out.size(), 3U);
    EXPECT_EQ(auxl_out[0], 4U);
    EXPECT_EQ(auxl_out[1], 0U);
    EXPECT_EQ(auxl_out[2], 0U);
    const std::vector<uint32_t> auxl_in
        = collect_u32_values(store, "iref.auxl.item_in_edge_count");
    ASSERT_EQ(auxl_in.size(), 3U);
    EXPECT_EQ(auxl_in[0], 0U);
    EXPECT_EQ(auxl_in[1], 2U);
    EXPECT_EQ(auxl_in[2], 2U);

    const std::vector<uint32_t> dimg_item_count
        = collect_u32_values(store, "iref.dimg.item_count");
    ASSERT_EQ(dimg_item_count.size(), 1U);
    EXPECT_EQ(dimg_item_count[0], 3U);
    const std::vector<uint32_t> dimg_item_ids
        = collect_u32_values(store, "iref.dimg.item_id");
    ASSERT_EQ(dimg_item_ids.size(), 3U);
    EXPECT_EQ(dimg_item_ids[0], 2U);
    EXPECT_EQ(dimg_item_ids[1], 5U);
    EXPECT_EQ(dimg_item_ids[2], 4U);
    const std::vector<uint32_t> dimg_out
        = collect_u32_values(store, "iref.dimg.item_out_edge_count");
    ASSERT_EQ(dimg_out.size(), 3U);
    EXPECT_EQ(dimg_out[0], 2U);
    EXPECT_EQ(dimg_out[1], 0U);
    EXPECT_EQ(dimg_out[2], 1U);
    const std::vector<uint32_t> dimg_in
        = collect_u32_values(store, "iref.dimg.item_in_edge_count");
    ASSERT_EQ(dimg_in.size(), 3U);
    EXPECT_EQ(dimg_in[0], 0U);
    EXPECT_EQ(dimg_in[1], 3U);
    EXPECT_EQ(dimg_in[2], 0U);

    const std::vector<uint32_t> thmb_item_count
        = collect_u32_values(store, "iref.thmb.item_count");
    ASSERT_EQ(thmb_item_count.size(), 1U);
    EXPECT_EQ(thmb_item_count[0], 3U);
    const std::vector<uint32_t> thmb_item_ids
        = collect_u32_values(store, "iref.thmb.item_id");
    ASSERT_EQ(thmb_item_ids.size(), 3U);
    EXPECT_EQ(thmb_item_ids[0], 3U);
    EXPECT_EQ(thmb_item_ids[1], 7U);
    EXPECT_EQ(thmb_item_ids[2], 8U);

    const std::vector<uint32_t> cdsc_item_count
        = collect_u32_values(store, "iref.cdsc.item_count");
    ASSERT_EQ(cdsc_item_count.size(), 1U);
    EXPECT_EQ(cdsc_item_count[0], 4U);
    const std::vector<uint32_t> cdsc_item_ids
        = collect_u32_values(store, "iref.cdsc.item_id");
    ASSERT_EQ(cdsc_item_ids.size(), 4U);
    EXPECT_EQ(cdsc_item_ids[0], 4U);
    EXPECT_EQ(cdsc_item_ids[1], 8U);
    EXPECT_EQ(cdsc_item_ids[2], 9U);
    EXPECT_EQ(cdsc_item_ids[3], 5U);
}

TEST(BmffDerivedFieldsDecode, EmitsItemInfoRowsAndPrimaryAliases)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        const uint32_t kMimeItem = 0x10001U;
        const uint32_t kExifItem = 0x10002U;

        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 1);
        append_u32be(&pitm_payload, kExifItem);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> infe1_payload;
        append_fullbox_header(&infe1_payload, 3);
        append_u32be(&infe1_payload, kMimeItem);
        append_u16be(&infe1_payload, 0);
        append_fourcc(&infe1_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe1_payload, "preview");
        infe1_payload.push_back(std::byte { 0 });
        append_bytes(&infe1_payload, "image/png");
        infe1_payload.push_back(std::byte { 0 });
        append_bytes(&infe1_payload, "gzip");
        infe1_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe1_box;
        append_bmff_box(&infe1_box, fourcc('i', 'n', 'f', 'e'), infe1_payload);

        std::vector<std::byte> infe2_payload;
        append_fullbox_header(&infe2_payload, 3);
        append_u32be(&infe2_payload, kExifItem);
        append_u16be(&infe2_payload, 0);
        append_fourcc(&infe2_payload, fourcc('E', 'x', 'i', 'f'));
        append_bytes(&infe2_payload, "exif");
        infe2_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe2_box;
        append_bmff_box(&infe2_box, fourcc('i', 'n', 'f', 'e'), infe2_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 2);
        iinf_payload.insert(iinf_payload.end(), infe1_box.begin(),
                            infe1_box.end());
        iinf_payload.insert(iinf_payload.end(), infe2_box.begin(),
                            infe2_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> info_count
        = collect_u32_values(store, "item.info_count");
    ASSERT_EQ(info_count.size(), 1U);
    EXPECT_EQ(info_count[0], 2U);

    const std::vector<uint32_t> item_ids = collect_u32_values(store, "item.id");
    ASSERT_EQ(item_ids.size(), 2U);
    EXPECT_EQ(item_ids[0], 0x10001U);
    EXPECT_EQ(item_ids[1], 0x10002U);

    const std::vector<uint32_t> item_types = collect_u32_values(store,
                                                                "item.type");
    ASSERT_EQ(item_types.size(), 2U);
    EXPECT_EQ(item_types[0], fourcc('m', 'i', 'm', 'e'));
    EXPECT_EQ(item_types[1], fourcc('E', 'x', 'i', 'f'));

    const std::vector<std::string> item_names
        = collect_text_values(store, "item.name");
    ASSERT_EQ(item_names.size(), 2U);
    EXPECT_EQ(item_names[0], "preview");
    EXPECT_EQ(item_names[1], "exif");

    const std::vector<std::string> content_types
        = collect_text_values(store, "item.content_type");
    ASSERT_EQ(content_types.size(), 1U);
    EXPECT_EQ(content_types[0], "image/png");

    const std::vector<std::string> content_encoding
        = collect_text_values(store, "item.content_encoding");
    ASSERT_EQ(content_encoding.size(), 1U);
    EXPECT_EQ(content_encoding[0], "gzip");

    const std::vector<uint32_t> primary_type
        = collect_u32_values(store, "primary.item_type");
    ASSERT_EQ(primary_type.size(), 1U);
    EXPECT_EQ(primary_type[0], fourcc('E', 'x', 'i', 'f'));

    const std::vector<std::string> primary_name
        = collect_text_values(store, "primary.item_name");
    ASSERT_EQ(primary_name.size(), 1U);
    EXPECT_EQ(primary_name[0], "exif");

    EXPECT_TRUE(collect_text_values(store, "primary.content_type").empty());
}

TEST(BmffDerivedFieldsDecode, EmitsPrimaryMimeItemInfoFromInfeV2)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);
        append_u16be(&infe_payload, 7);
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "payload");
        infe_payload.push_back(std::byte { 0 });
        append_bytes(&infe_payload, "application/rdf+xml");
        infe_payload.push_back(std::byte { 0 });
        append_bytes(&infe_payload, "gzip");
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> primary_type
        = collect_u32_values(store, "primary.item_type");
    ASSERT_EQ(primary_type.size(), 1U);
    EXPECT_EQ(primary_type[0], fourcc('m', 'i', 'm', 'e'));

    const std::vector<std::string> primary_name
        = collect_text_values(store, "primary.item_name");
    ASSERT_EQ(primary_name.size(), 1U);
    EXPECT_EQ(primary_name[0], "payload");

    const std::vector<std::string> primary_content_type
        = collect_text_values(store, "primary.content_type");
    ASSERT_EQ(primary_content_type.size(), 1U);
    EXPECT_EQ(primary_content_type[0], "application/rdf+xml");

    const std::vector<std::string> primary_content_encoding
        = collect_text_values(store, "primary.content_encoding");
    ASSERT_EQ(primary_content_encoding.size(), 1U);
    EXPECT_EQ(primary_content_encoding[0], "gzip");
}

TEST(BmffDerivedFieldsDecode, EmitsItemInfoRowsWithoutPitm)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 3);
        append_u16be(&infe_payload, 0);
        append_fourcc(&infe_payload, fourcc('m', 'i', 'm', 'e'));
        append_bytes(&infe_payload, "sidecar");
        infe_payload.push_back(std::byte { 0 });
        append_bytes(&infe_payload, "application/json");
        infe_payload.push_back(std::byte { 0 });
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> info_count
        = collect_u32_values(store, "item.info_count");
    ASSERT_EQ(info_count.size(), 1U);
    EXPECT_EQ(info_count[0], 1U);

    const std::vector<uint32_t> item_ids = collect_u32_values(store, "item.id");
    ASSERT_EQ(item_ids.size(), 1U);
    EXPECT_EQ(item_ids[0], 3U);

    const std::vector<uint32_t> item_types = collect_u32_values(store,
                                                                "item.type");
    ASSERT_EQ(item_types.size(), 1U);
    EXPECT_EQ(item_types[0], fourcc('m', 'i', 'm', 'e'));

    const std::vector<std::string> item_names
        = collect_text_values(store, "item.name");
    ASSERT_EQ(item_names.size(), 1U);
    EXPECT_EQ(item_names[0], "sidecar");

    const std::vector<std::string> content_types
        = collect_text_values(store, "item.content_type");
    ASSERT_EQ(content_types.size(), 1U);
    EXPECT_EQ(content_types[0], "application/json");

    EXPECT_TRUE(collect_u32_values(store, "meta.primary_item_id").empty());
    EXPECT_TRUE(collect_u32_values(store, "primary.item_type").empty());
    EXPECT_TRUE(collect_text_values(store, "primary.item_name").empty());
}

TEST(BmffDerivedFieldsDecode, EmitsPrimaryUriItemInfoFromInfeV2)
{
    std::vector<std::byte> file;

    {
        std::vector<std::byte> ftyp_payload;
        append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
        append_u32be(&ftyp_payload, 0);
        append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));
        append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    }

    {
        std::vector<std::byte> pitm_payload;
        append_fullbox_header(&pitm_payload, 0);
        append_u16be(&pitm_payload, 1);
        std::vector<std::byte> pitm_box;
        append_bmff_box(&pitm_box, fourcc('p', 'i', 't', 'm'), pitm_payload);

        std::vector<std::byte> infe_payload;
        append_fullbox_header(&infe_payload, 2);
        append_u16be(&infe_payload, 1);
        append_u16be(&infe_payload, 3);
        append_fourcc(&infe_payload, fourcc('u', 'r', 'i', ' '));
        append_bytes(&infe_payload, "link");
        infe_payload.push_back(std::byte { 0 });
        append_bytes(&infe_payload, "https://ns.example/item");
        infe_payload.push_back(std::byte { 0 });
        std::vector<std::byte> infe_box;
        append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

        std::vector<std::byte> iinf_payload;
        append_fullbox_header(&iinf_payload, 2);
        append_u32be(&iinf_payload, 1);
        iinf_payload.insert(iinf_payload.end(), infe_box.begin(),
                            infe_box.end());
        std::vector<std::byte> iinf_box;
        append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

        std::vector<std::byte> meta_payload;
        append_fullbox_header(&meta_payload, 0);
        meta_payload.insert(meta_payload.end(), pitm_box.begin(),
                            pitm_box.end());
        meta_payload.insert(meta_payload.end(), iinf_box.begin(),
                            iinf_box.end());
        append_bmff_box(&file, fourcc('m', 'e', 't', 'a'), meta_payload);
    }

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};
    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    (void)simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    store.finalize();

    const std::vector<uint32_t> item_types = collect_u32_values(store,
                                                                "item.type");
    ASSERT_EQ(item_types.size(), 1U);
    EXPECT_EQ(item_types[0], fourcc('u', 'r', 'i', ' '));

    const std::vector<std::string> item_names
        = collect_text_values(store, "item.name");
    ASSERT_EQ(item_names.size(), 1U);
    EXPECT_EQ(item_names[0], "link");

    const std::vector<std::string> item_uri_type
        = collect_text_values(store, "item.uri_type");
    ASSERT_EQ(item_uri_type.size(), 1U);
    EXPECT_EQ(item_uri_type[0], "https://ns.example/item");

    const std::vector<uint32_t> primary_type
        = collect_u32_values(store, "primary.item_type");
    ASSERT_EQ(primary_type.size(), 1U);
    EXPECT_EQ(primary_type[0], fourcc('u', 'r', 'i', ' '));

    const std::vector<std::string> primary_name
        = collect_text_values(store, "primary.item_name");
    ASSERT_EQ(primary_name.size(), 1U);
    EXPECT_EQ(primary_name[0], "link");

    const std::vector<std::string> primary_uri_type
        = collect_text_values(store, "primary.uri_type");
    ASSERT_EQ(primary_uri_type.size(), 1U);
    EXPECT_EQ(primary_uri_type[0], "https://ns.example/item");

    EXPECT_TRUE(collect_text_values(store, "primary.content_type").empty());
}

}  // namespace openmeta
