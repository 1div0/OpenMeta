#include "openmeta/photoshop_irb_decode.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openmeta {
namespace {

    static void append_u16be(uint16_t v, std::vector<std::byte>* out)
    {
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 0) & 0xFF) });
    }


    static void append_u32be(uint32_t v, std::vector<std::byte>* out)
    {
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 24) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 16) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) });
        out->push_back(
            std::byte { static_cast<unsigned char>((v >> 0) & 0xFF) });
    }


    static void append_irb_resource(uint16_t id,
                                    std::span<const std::byte> payload,
                                    std::vector<std::byte>* out)
    {
        // Signature.
        out->push_back(std::byte { '8' });
        out->push_back(std::byte { 'B' });
        out->push_back(std::byte { 'I' });
        out->push_back(std::byte { 'M' });
        append_u16be(id, out);

        // Pascal name (len=0) + pad => 2 bytes total.
        out->push_back(std::byte { 0x00 });
        out->push_back(std::byte { 0x00 });

        append_u32be(static_cast<uint32_t>(payload.size()), out);
        out->insert(out->end(), payload.begin(), payload.end());

        if ((payload.size() & 1U) != 0U) {
            out->push_back(std::byte { 0x00 });
        }
    }

}  // namespace

TEST(PhotoshopIrbDecodeTest, DecodesResourcesAndOptionalIptc)
{
    // One IPTC dataset to embed in resource 0x0404.
    const std::array<std::byte, 9> iptc = {
        std::byte { 0x1C }, std::byte { 0x02 }, std::byte { 0x19 },
        std::byte { 0x00 }, std::byte { 0x04 }, std::byte { 't' },
        std::byte { 'e' },  std::byte { 's' },  std::byte { 't' },
    };

    std::vector<std::byte> irb;
    append_irb_resource(0x0404, iptc, &irb);

    const std::array<std::byte, 3> other = {
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
    };
    append_irb_resource(0x1234, other, &irb);

    MetaStore store;
    const PhotoshopIrbDecodeResult r = decode_photoshop_irb(irb, store);
    EXPECT_EQ(r.status, PhotoshopIrbDecodeStatus::Ok);
    EXPECT_EQ(r.resources_decoded, 2U);
    EXPECT_EQ(r.iptc_entries_decoded, 1U);

    // One block for IRB resources, plus one for derived IPTC datasets.
    ASSERT_EQ(store.block_count(), 2U);
    ASSERT_EQ(store.entries().size(), 3U);

    uint32_t irb_entries  = 0;
    uint32_t iptc_entries = 0;
    for (size_t i = 0; i < store.entries().size(); ++i) {
        const Entry& e = store.entry(static_cast<EntryId>(i));
        if (e.key.kind == MetaKeyKind::PhotoshopIrb) {
            irb_entries += 1;
            continue;
        }
        if (e.key.kind == MetaKeyKind::IptcDataset) {
            iptc_entries += 1;
            EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
            EXPECT_EQ(e.key.data.iptc_dataset.record, 2U);
            EXPECT_EQ(e.key.data.iptc_dataset.dataset, 25U);
        }
    }
    EXPECT_EQ(irb_entries, 2U);
    EXPECT_EQ(iptc_entries, 1U);
}

}  // namespace openmeta
