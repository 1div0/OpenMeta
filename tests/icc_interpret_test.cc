#include "openmeta/icc_interpret.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace openmeta {
namespace {

    static constexpr uint32_t make_fourcc(char a, char b, char c,
                                          char d) noexcept
    {
        return (static_cast<uint32_t>(static_cast<uint8_t>(a)) << 24)
               | (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 16)
               | (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 8)
               | static_cast<uint32_t>(static_cast<uint8_t>(d));
    }

    static void write_u16be(uint16_t v, size_t off, std::vector<std::byte>* out)
    {
        (*out)[off + 0]
            = std::byte { static_cast<unsigned char>((v >> 8) & 0xFFU) };
        (*out)[off + 1]
            = std::byte { static_cast<unsigned char>((v >> 0) & 0xFFU) };
    }

    static void write_u32be(uint32_t v, size_t off, std::vector<std::byte>* out)
    {
        (*out)[off + 0]
            = std::byte { static_cast<unsigned char>((v >> 24) & 0xFFU) };
        (*out)[off + 1]
            = std::byte { static_cast<unsigned char>((v >> 16) & 0xFFU) };
        (*out)[off + 2]
            = std::byte { static_cast<unsigned char>((v >> 8) & 0xFFU) };
        (*out)[off + 3]
            = std::byte { static_cast<unsigned char>((v >> 0) & 0xFFU) };
    }

}  // namespace


TEST(IccInterpret, DecodesDescText)
{
    std::vector<std::byte> tag(18, std::byte { 0x00 });
    write_u32be(make_fourcc('d', 'e', 's', 'c'), 0, &tag);
    write_u32be(6U, 8, &tag);  // "hello\0"
    tag[12] = std::byte { 'h' };
    tag[13] = std::byte { 'e' };
    tag[14] = std::byte { 'l' };
    tag[15] = std::byte { 'l' };
    tag[16] = std::byte { 'o' };
    tag[17] = std::byte { 0x00 };

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('d', 'e', 's', 'c'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.name, "ProfileDescription");
    EXPECT_EQ(out.type, "desc");
    EXPECT_EQ(out.text, "hello");
    EXPECT_TRUE(out.values.empty());
}


TEST(IccInterpret, DecodesXyzTriplet)
{
    std::vector<std::byte> tag(20, std::byte { 0x00 });
    write_u32be(make_fourcc('X', 'Y', 'Z', ' '), 0, &tag);
    write_u32be(63189U, 8, &tag);   // 0.9642
    write_u32be(65536U, 12, &tag);  // 1.0
    write_u32be(54061U, 16, &tag);  // 0.8249

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('w', 't', 'p', 't'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.name, "MediaWhitePoint");
    EXPECT_EQ(out.type, "XYZ ");
    EXPECT_EQ(out.rows, 1U);
    EXPECT_EQ(out.cols, 3U);
    ASSERT_EQ(out.values.size(), 3U);
    EXPECT_NEAR(out.values[0], 0.9642, 1e-4);
    EXPECT_NEAR(out.values[1], 1.0, 1e-9);
    EXPECT_NEAR(out.values[2], 0.8249, 1e-4);
}


TEST(IccInterpret, DecodesCurveGammaAndAppliesLimits)
{
    std::vector<std::byte> tag(14, std::byte { 0x00 });
    write_u32be(make_fourcc('c', 'u', 'r', 'v'), 0, &tag);
    write_u32be(1U, 8, &tag);
    write_u16be(563U, 12, &tag);  // about gamma 2.199

    IccTagInterpretation out;
    IccTagInterpretOptions opts;
    opts.limits.max_values = 1U;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('r', 'T', 'R', 'C'), tag, &out, opts);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.name, "RedTRC");
    EXPECT_EQ(out.type, "curv");
    ASSERT_EQ(out.values.size(), 1U);
    EXPECT_NEAR(out.values[0], 563.0 / 256.0, 1e-9);
}

TEST(IccInterpret, DecodesTextType)
{
    std::vector<std::byte> tag(17, std::byte { 0x00 });
    write_u32be(make_fourcc('t', 'e', 'x', 't'), 0, &tag);
    tag[8]  = std::byte { 'A' };
    tag[9]  = std::byte { 'C' };
    tag[10] = std::byte { 'M' };
    tag[11] = std::byte { 'E' };
    tag[12] = std::byte { ' ' };
    tag[13] = std::byte { 'R' };
    tag[14] = std::byte { 'G' };
    tag[15] = std::byte { 'B' };
    tag[16] = std::byte { 0x00 };

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('c', 'p', 'r', 't'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "text");
    EXPECT_EQ(out.text, "ACME RGB");
    EXPECT_TRUE(out.values.empty());
}

TEST(IccInterpret, DecodesSignatureType)
{
    std::vector<std::byte> tag(12, std::byte { 0x00 });
    write_u32be(make_fourcc('s', 'i', 'g', ' '), 0, &tag);
    write_u32be(make_fourcc('s', 'c', 'n', 'r'), 8, &tag);

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('t', 'e', 'c', 'h'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "sig ");
    EXPECT_EQ(out.text, "scnr");
    EXPECT_TRUE(out.values.empty());
}

}  // namespace openmeta
