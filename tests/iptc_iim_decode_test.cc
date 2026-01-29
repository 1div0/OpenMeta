#include "openmeta/iptc_iim_decode.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openmeta {

TEST(IptcIimDecodeTest, DecodesDatasetsAndPreservesDuplicates)
{
    std::vector<std::byte> iptc;

    // Dataset 1: 0x1C 0x02 0x19 len=5 "hello"
    const std::array<std::byte, 10> d1 = {
        std::byte { 0x1C }, std::byte { 0x02 }, std::byte { 0x19 },
        std::byte { 0x00 }, std::byte { 0x05 }, std::byte { 'h' },
        std::byte { 'e' },  std::byte { 'l' },  std::byte { 'l' },
        std::byte { 'o' },
    };
    iptc.insert(iptc.end(), d1.begin(), d1.end());

    // Dataset 2 (extended length): 0x1C 0x02 0x78 len=0x8002 + 0x0003 "abc"
    const std::array<std::byte, 10> d2 = {
        std::byte { 0x1C }, std::byte { 0x02 }, std::byte { 0x78 },
        std::byte { 0x80 }, std::byte { 0x02 }, std::byte { 0x00 },
        std::byte { 0x03 }, std::byte { 'a' },  std::byte { 'b' },
        std::byte { 'c' },
    };
    iptc.insert(iptc.end(), d2.begin(), d2.end());

    MetaStore store;
    const IptcIimDecodeResult r
        = decode_iptc_iim(std::span<const std::byte>(iptc.data(), iptc.size()),
                          store, EntryFlags::None);
    EXPECT_EQ(r.status, IptcIimDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    ASSERT_EQ(store.block_count(), 1U);
    ASSERT_EQ(store.entries().size(), 2U);

    const Entry& e0 = store.entry(0);
    EXPECT_EQ(e0.key.kind, MetaKeyKind::IptcDataset);
    EXPECT_EQ(e0.key.data.iptc_dataset.record, 2U);
    EXPECT_EQ(e0.key.data.iptc_dataset.dataset, 25U);
    EXPECT_EQ(e0.value.kind, MetaValueKind::Bytes);
    EXPECT_EQ(e0.value.count, 5U);
    {
        const std::span<const std::byte> b = store.arena().span(
            e0.value.data.span);
        ASSERT_EQ(b.size(), 5U);
        EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(b[0])), 'h');
        EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(b[1])), 'e');
        EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(b[2])), 'l');
        EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(b[3])), 'l');
        EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(b[4])), 'o');
    }

    const Entry& e1 = store.entry(1);
    EXPECT_EQ(e1.key.kind, MetaKeyKind::IptcDataset);
    EXPECT_EQ(e1.key.data.iptc_dataset.record, 2U);
    EXPECT_EQ(e1.key.data.iptc_dataset.dataset, 120U);
    EXPECT_EQ(e1.value.kind, MetaValueKind::Bytes);
    EXPECT_EQ(e1.value.count, 3U);
    {
        const std::span<const std::byte> b = store.arena().span(
            e1.value.data.span);
        ASSERT_EQ(b.size(), 3U);
        EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(b[0])), 'a');
        EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(b[1])), 'b');
        EXPECT_EQ(static_cast<char>(std::to_integer<unsigned char>(b[2])), 'c');
    }
}


TEST(IptcIimDecodeTest, ReturnsUnsupportedWhenNoIptcMarker)
{
    const std::array<std::byte, 4> bytes = {
        std::byte { 0x00 },
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
    };

    MetaStore store;
    const IptcIimDecodeResult r = decode_iptc_iim(bytes, store);
    EXPECT_EQ(r.status, IptcIimDecodeStatus::Unsupported);
    EXPECT_TRUE(store.entries().empty());
}


TEST(IptcIimDecodeTest, EnforcesMaxDatasetsLimit)
{
    // Two small datasets.
    const std::array<std::byte, 12> bytes = {
        std::byte { 0x1C }, std::byte { 0x02 }, std::byte { 0x19 },
        std::byte { 0x00 }, std::byte { 0x01 }, std::byte { 'a' },
        std::byte { 0x1C }, std::byte { 0x02 }, std::byte { 0x1A },
        std::byte { 0x00 }, std::byte { 0x01 }, std::byte { 'b' },
    };

    IptcIimDecodeOptions options;
    options.limits.max_datasets = 1;

    MetaStore store;
    const IptcIimDecodeResult r = decode_iptc_iim(bytes, store,
                                                  EntryFlags::None, options);
    EXPECT_EQ(r.status, IptcIimDecodeStatus::LimitExceeded);
}

}  // namespace openmeta
