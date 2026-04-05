// SPDX-License-Identifier: Apache-2.0

#include "openmeta/container_scan.h"
#include "openmeta/icc_decode.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace openmeta {
namespace {

    static void write_u16be(uint16_t v, size_t off, std::vector<std::byte>* out)
    {
        (*out)[off + 0]
            = std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) };
        (*out)[off + 1]
            = std::byte { static_cast<unsigned char>((v >> 0) & 0xFF) };
    }


    static void write_u32be(uint32_t v, size_t off, std::vector<std::byte>* out)
    {
        (*out)[off + 0]
            = std::byte { static_cast<unsigned char>((v >> 24) & 0xFF) };
        (*out)[off + 1]
            = std::byte { static_cast<unsigned char>((v >> 16) & 0xFF) };
        (*out)[off + 2]
            = std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) };
        (*out)[off + 3]
            = std::byte { static_cast<unsigned char>((v >> 0) & 0xFF) };
    }


    static void write_u64be(uint64_t v, size_t off, std::vector<std::byte>* out)
    {
        (*out)[off + 0]
            = std::byte { static_cast<unsigned char>((v >> 56) & 0xFF) };
        (*out)[off + 1]
            = std::byte { static_cast<unsigned char>((v >> 48) & 0xFF) };
        (*out)[off + 2]
            = std::byte { static_cast<unsigned char>((v >> 40) & 0xFF) };
        (*out)[off + 3]
            = std::byte { static_cast<unsigned char>((v >> 32) & 0xFF) };
        (*out)[off + 4]
            = std::byte { static_cast<unsigned char>((v >> 24) & 0xFF) };
        (*out)[off + 5]
            = std::byte { static_cast<unsigned char>((v >> 16) & 0xFF) };
        (*out)[off + 6]
            = std::byte { static_cast<unsigned char>((v >> 8) & 0xFF) };
        (*out)[off + 7]
            = std::byte { static_cast<unsigned char>((v >> 0) & 0xFF) };
    }


    static void write_i32be(int32_t v, size_t off, std::vector<std::byte>* out)
    {
        const uint32_t u = static_cast<uint32_t>(v);
        write_u32be(u, off, out);
    }

}  // namespace

TEST(IccDecodeTest, DecodesHeaderAndTagTable)
{
    // Minimal ICC profile: header (128) + tag_count (4) + 1 tag entry (12)
    // + tag bytes (16) => 160 bytes.
    std::vector<std::byte> icc(160, std::byte { 0x00 });

    write_u32be(static_cast<uint32_t>(icc.size()), 0, &icc);
    write_u32be(fourcc('a', 'p', 'p', 'l'), 4, &icc);   // cmm type
    write_u32be(0x04300000U, 8, &icc);                  // version (arbitrary)
    write_u32be(fourcc('m', 'n', 't', 'r'), 12, &icc);  // class
    write_u32be(fourcc('R', 'G', 'B', ' '), 16, &icc);  // data space
    write_u32be(fourcc('X', 'Y', 'Z', ' '), 20, &icc);  // PCS

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
    write_u32be(fourcc('M', 'S', 'F', 'T'), 40, &icc);  // platform
    write_u32be(1U, 44, &icc);                          // flags
    write_u32be(fourcc('A', 'P', 'P', 'L'), 48, &icc);  // manufacturer
    write_u32be(fourcc('M', '1', '2', '3'), 52, &icc);  // model
    write_u64be(1ULL, 56, &icc);                        // attributes
    write_u32be(1U, 64, &icc);                          // intent
    write_i32be(63189, 68, &icc);  // X = 0.9642 in s15Fixed16
    write_i32be(65536, 72, &icc);  // Y = 1.0000
    write_i32be(54061, 76, &icc);  // Z = 0.8249
    write_u32be(fourcc('o', 'p', 'n', 'm'), 80, &icc);  // creator

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
    bool saw_cmm  = false;
    bool saw_attr = false;
    bool saw_ill  = false;

    for (size_t i = 0; i < store.entries().size(); ++i) {
        const Entry& e = store.entry(static_cast<EntryId>(i));
        if (e.key.kind == MetaKeyKind::IccHeaderField
            && e.key.data.icc_header_field.offset == 0) {
            saw_size = true;
            EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
            EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
            EXPECT_EQ(e.value.data.u64, static_cast<uint64_t>(icc.size()));
        }
        if (e.key.kind == MetaKeyKind::IccHeaderField
            && e.key.data.icc_header_field.offset == 4) {
            saw_cmm = true;
            EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
            EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
            EXPECT_EQ(static_cast<uint32_t>(e.value.data.u64),
                      fourcc('a', 'p', 'p', 'l'));
        }
        if (e.key.kind == MetaKeyKind::IccHeaderField
            && e.key.data.icc_header_field.offset == 56) {
            saw_attr = true;
            EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
            EXPECT_EQ(e.value.elem_type, MetaElementType::U64);
            EXPECT_EQ(e.value.data.u64, 1ULL);
        }
        if (e.key.kind == MetaKeyKind::IccHeaderField
            && e.key.data.icc_header_field.offset == 68) {
            saw_ill = true;
            EXPECT_EQ(e.value.kind, MetaValueKind::Array);
            EXPECT_EQ(e.value.elem_type, MetaElementType::SRational);
            ASSERT_EQ(e.value.count, 3U);
            const std::span<const std::byte> b = store.arena().span(
                e.value.data.span);
            ASSERT_EQ(b.size(), sizeof(SRational) * 3U);
            std::array<SRational, 3> vals {};
            std::memcpy(vals.data(), b.data(), sizeof(SRational) * vals.size());
            EXPECT_EQ(vals[0].numer, 63189);
            EXPECT_EQ(vals[0].denom, 65536);
            EXPECT_EQ(vals[1].numer, 65536);
            EXPECT_EQ(vals[1].denom, 65536);
            EXPECT_EQ(vals[2].numer, 54061);
            EXPECT_EQ(vals[2].denom, 65536);
        }
        if (e.key.kind == MetaKeyKind::IccTag
            && e.key.data.icc_tag.signature == fourcc('d', 'e', 's', 'c')) {
            saw_tag = true;
            EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
            EXPECT_EQ(e.value.count, 16U);
            const std::span<const std::byte> b = store.arena().span(
                e.value.data.span);
            ASSERT_EQ(b.size(), 16U);
            for (size_t j = 0; j < 16; ++j) {
                EXPECT_EQ(b[j], std::byte { static_cast<unsigned char>(j) });
            }
        }
    }

    EXPECT_TRUE(saw_size);
    EXPECT_TRUE(saw_cmm);
    EXPECT_TRUE(saw_attr);
    EXPECT_TRUE(saw_ill);
    EXPECT_TRUE(saw_tag);
}

TEST(IccDecodeTest, EstimateMatchesDecodeCounters)
{
    std::vector<std::byte> icc(160, std::byte { 0x00 });
    write_u32be(static_cast<uint32_t>(icc.size()), 0, &icc);
    write_u32be(fourcc('a', 'c', 's', 'p'), 36, &icc);
    write_u32be(1U, 128, &icc);
    write_u32be(fourcc('d', 'e', 's', 'c'), 132, &icc);
    write_u32be(144U, 136, &icc);
    write_u32be(16U, 140, &icc);

    const IccDecodeResult estimate = measure_icc_profile(icc);
    EXPECT_EQ(estimate.status, IccDecodeStatus::Ok);

    MetaStore store;
    const IccDecodeResult decoded = decode_icc_profile(icc, store);
    EXPECT_EQ(decoded.status, estimate.status);
    EXPECT_EQ(decoded.entries_decoded, estimate.entries_decoded);
}

}  // namespace openmeta
