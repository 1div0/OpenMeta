#include "openmeta/icc_decode.h"
#include "openmeta/container_scan.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openmeta {
namespace {

static void write_u16be(uint16_t v, size_t off, std::vector<std::byte>* out)
{
    (*out)[off + 0] = std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) };
    (*out)[off + 1] = std::byte { static_cast<unsigned char>((v >> 0) & 0xFF) };
}

static void write_u32be(uint32_t v, size_t off, std::vector<std::byte>* out)
{
    (*out)[off + 0] = std::byte { static_cast<unsigned char>((v >> 24) & 0xFF) };
    (*out)[off + 1] = std::byte { static_cast<unsigned char>((v >> 16) & 0xFF) };
    (*out)[off + 2] = std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) };
    (*out)[off + 3] = std::byte { static_cast<unsigned char>((v >> 0) & 0xFF) };
}

}  // namespace

TEST(IccDecodeTest, DecodesHeaderAndTagTable)
{
    // Minimal ICC profile: header (128) + tag_count (4) + 1 tag entry (12)
    // + tag bytes (16) => 160 bytes.
    std::vector<std::byte> icc(160, std::byte { 0x00 });

    write_u32be(static_cast<uint32_t>(icc.size()), 0, &icc);
    write_u32be(0x04300000U, 8, &icc);  // version (arbitrary)

    // Date/time: 2026-01-28 00:00:00
    write_u16be(2026, 24, &icc);
    write_u16be(1, 26, &icc);
    write_u16be(28, 28, &icc);
    write_u16be(0, 30, &icc);
    write_u16be(0, 32, &icc);
    write_u16be(0, 34, &icc);

    // Signature.
    icc[36] = std::byte { 'a' };
    icc[37] = std::byte { 'c' };
    icc[38] = std::byte { 's' };
    icc[39] = std::byte { 'p' };

    // Tag table.
    write_u32be(1U, 128, &icc);
    write_u32be(fourcc('d', 'e', 's', 'c'), 132, &icc);
    write_u32be(144U, 136, &icc);
    write_u32be(16U, 140, &icc);

    for (size_t i = 0; i < 16; ++i) {
        icc[144 + i] = std::byte { static_cast<unsigned char>(i) };
    }

    MetaStore store;
    const IccDecodeResult r = decode_icc_profile(icc, store);
    EXPECT_EQ(r.status, IccDecodeStatus::Ok);

    bool saw_size = false;
    bool saw_tag  = false;

    for (size_t i = 0; i < store.entries().size(); ++i) {
        const Entry& e = store.entry(static_cast<EntryId>(i));
        if (e.key.kind == MetaKeyKind::IccHeaderField
            && e.key.data.icc_header_field.offset == 0) {
            saw_size = true;
            EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
            EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
            EXPECT_EQ(e.value.data.u64, static_cast<uint64_t>(icc.size()));
        }
        if (e.key.kind == MetaKeyKind::IccTag
            && e.key.data.icc_tag.signature == fourcc('d', 'e', 's', 'c')) {
            saw_tag = true;
            EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
            EXPECT_EQ(e.value.count, 16U);
            const std::span<const std::byte> b = store.arena().span(e.value.data.span);
            ASSERT_EQ(b.size(), 16U);
            for (size_t j = 0; j < 16; ++j) {
                EXPECT_EQ(b[j], std::byte { static_cast<unsigned char>(j) });
            }
        }
    }

    EXPECT_TRUE(saw_size);
    EXPECT_TRUE(saw_tag);
}

}  // namespace openmeta
