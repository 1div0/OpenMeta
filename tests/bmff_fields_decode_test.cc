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

    static void append_fullbox_header(std::vector<std::byte>* out,
                                      uint8_t version)
    {
        out->push_back(std::byte { version });
        out->push_back(std::byte { 0 });
        out->push_back(std::byte { 0 });
        out->push_back(std::byte { 0 });
    }

    static void append_bmff_box(std::vector<std::byte>* out, uint32_t type,
                                std::span<const std::byte> payload)
    {
        append_u32be(out, static_cast<uint32_t>(8 + payload.size()));
        append_fourcc(out, type);
        out->insert(out->end(), payload.begin(), payload.end());
    }

    static MetaKeyView bmff_key(std::string_view field)
    {
        MetaKeyView key;
        key.kind               = MetaKeyKind::BmffField;
        key.data.bmff_field.field = field;
        return key;
    }

    static std::vector<uint32_t>
    collect_u32_values(const MetaStore& store, std::string_view field)
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

}  // namespace

TEST(BmffDerivedFieldsDecode, EmitsFtypAndPrimaryProps)
{
    // Minimal ISO-BMFF/HEIF with:
    // - ftyp(major_brand='heic', compat=['mif1'])
    // - meta(pitm primary item id=1, iprp/ipco(ispe+irot), ipma associates props)

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

        // ipco: ispe + irot
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

        std::vector<std::byte> ipco_payload;
        ipco_payload.insert(ipco_payload.end(), ispe_box.begin(),
                            ispe_box.end());
        ipco_payload.insert(ipco_payload.end(), irot_box.begin(),
                            irot_box.end());
        std::vector<std::byte> ipco_box;
        append_bmff_box(&ipco_box, fourcc('i', 'p', 'c', 'o'), ipco_payload);

        // ipma (FullBox version 0): item 1 has properties [1,2]
        std::vector<std::byte> ipma_payload;
        append_fullbox_header(&ipma_payload, 0);
        append_u32be(&ipma_payload, 1);  // entry_count
        append_u16be(&ipma_payload, 1);  // item_ID
        ipma_payload.push_back(std::byte { 2 });  // association_count
        ipma_payload.push_back(std::byte { 1 });  // property_index=1
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
        const std::span<const EntryId> ids
            = store.find_all(bmff_key("ftyp.major_brand"));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64),
                  fourcc('h', 'e', 'i', 'c'));
    }

    // primary.width/height/rotation
    {
        const std::span<const EntryId> w
            = store.find_all(bmff_key("primary.width"));
        const std::span<const EntryId> h
            = store.find_all(bmff_key("primary.height"));
        const std::span<const EntryId> r
            = store.find_all(bmff_key("primary.rotation_degrees"));
        ASSERT_EQ(w.size(), 1U);
        ASSERT_EQ(h.size(), 1U);
        ASSERT_EQ(r.size(), 1U);
        EXPECT_EQ(static_cast<uint32_t>(store.entry(w[0]).value.data.u64), 640U);
        EXPECT_EQ(static_cast<uint32_t>(store.entry(h[0]).value.data.u64), 480U);
        EXPECT_EQ(static_cast<uint16_t>(store.entry(r[0]).value.data.u64), 90U);
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

    const std::vector<uint32_t> ref_type
        = collect_u32_values(store, "iref.ref_type");
    ASSERT_EQ(ref_type.size(), 2U);
    EXPECT_EQ(ref_type[0], fourcc('a', 'u', 'x', 'l'));
    EXPECT_EQ(ref_type[1], fourcc('a', 'u', 'x', 'l'));

    const std::vector<uint32_t> from_ids
        = collect_u32_values(store, "iref.from_item_id");
    ASSERT_EQ(from_ids.size(), 2U);
    EXPECT_EQ(from_ids[0], 1U);
    EXPECT_EQ(from_ids[1], 1U);

    const std::vector<uint32_t> to_ids
        = collect_u32_values(store, "iref.to_item_id");
    ASSERT_EQ(to_ids.size(), 2U);
    EXPECT_EQ(to_ids[0], 2U);
    EXPECT_EQ(to_ids[1], 3U);

    const std::vector<uint32_t> primary_auxl
        = collect_u32_values(store, "primary.auxl_item_id");
    ASSERT_EQ(primary_auxl.size(), 2U);
    EXPECT_EQ(primary_auxl[0], 2U);
    EXPECT_EQ(primary_auxl[1], 3U);
}

}  // namespace openmeta
