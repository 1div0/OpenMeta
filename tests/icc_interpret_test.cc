#include "openmeta/icc_interpret.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
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

TEST(IccInterpret, ProvidesExtendedTagNames)
{
    EXPECT_EQ(icc_tag_name(make_fourcc('c', 'h', 'r', 'm')), "Chromaticity");
    EXPECT_EQ(icc_tag_name(make_fourcc('d', 'm', 'n', 'd')), "DeviceMfgDesc");
    EXPECT_EQ(icc_tag_name(make_fourcc('d', 'm', 'd', 'd')), "DeviceModelDesc");
    EXPECT_EQ(icc_tag_name(make_fourcc('g', 'a', 'm', 't')), "Gamut");
    EXPECT_EQ(icc_tag_name(make_fourcc('n', 'c', 'l', '2')), "NamedColor2");
    EXPECT_EQ(icc_tag_name(make_fourcc('r', 'e', 's', 'p')), "OutputResponse");
    EXPECT_EQ(icc_tag_name(make_fourcc('t', 'a', 'r', 'g')), "CharTarget");
}


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

TEST(IccInterpret, DecodesDataTypeAsciiPayload)
{
    std::vector<std::byte> tag(17, std::byte { 0x00 });
    write_u32be(make_fourcc('d', 'a', 't', 'a'), 0, &tag);
    write_u32be(0U, 8, &tag);  // ascii
    tag[12] = std::byte { 'h' };
    tag[13] = std::byte { 'e' };
    tag[14] = std::byte { 'l' };
    tag[15] = std::byte { 'l' };
    tag[16] = std::byte { 'o' };

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('c', 'p', 'r', 't'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "data");
    EXPECT_EQ(out.text, "hello");
}

TEST(IccInterpret, DecodesDataTypeBinarySummary)
{
    std::vector<std::byte> tag(16, std::byte { 0x00 });
    write_u32be(make_fourcc('d', 'a', 't', 'a'), 0, &tag);
    write_u32be(1U, 8, &tag);  // binary
    tag[12] = std::byte { 0x01 };
    tag[13] = std::byte { 0x02 };
    tag[14] = std::byte { 0x03 };
    tag[15] = std::byte { 0x04 };

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('c', 'p', 'r', 't'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "data");
    EXPECT_NE(out.text.find("flags=1"), std::string::npos);
    EXPECT_NE(out.text.find("bytes=4"), std::string::npos);
}

TEST(IccInterpret, DecodesNamedColor2Summary)
{
    // Header(84) + one color entry (32 + 6 + 2) = 124 bytes
    std::vector<std::byte> tag(124, std::byte { 0x00 });
    write_u32be(make_fourcc('n', 'c', 'l', '2'), 0, &tag);
    write_u32be(1U, 12, &tag);  // named color count
    write_u32be(1U, 16, &tag);  // device coords per color

    const char* name = "FirstColor";
    for (size_t i = 0U; name[i] != '\0'; ++i) {
        tag[84U + i] = std::byte { static_cast<unsigned char>(name[i]) };
    }

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('n', 'c', 'l', '2'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "ncl2");
    EXPECT_NE(out.text.find("count=1"), std::string::npos);
    EXPECT_NE(out.text.find("device_coords=1"), std::string::npos);
    EXPECT_NE(out.text.find("first=\"FirstColor\""), std::string::npos);
}

TEST(IccInterpret, DecodesDateTimeType)
{
    std::vector<std::byte> tag(20, std::byte { 0x00 });
    write_u32be(make_fourcc('d', 't', 'i', 'm'), 0, &tag);
    write_u16be(2024U, 8, &tag);
    write_u16be(2U, 10, &tag);
    write_u16be(29U, 12, &tag);
    write_u16be(17U, 14, &tag);
    write_u16be(45U, 16, &tag);
    write_u16be(3U, 18, &tag);

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('m', 'e', 'a', 's'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "dtim");
    EXPECT_EQ(out.text, "2024-02-29T17:45:03");
    EXPECT_TRUE(out.values.empty());
}

TEST(IccInterpret, DecodesViewingConditions)
{
    std::vector<std::byte> tag(36, std::byte { 0x00 });
    write_u32be(make_fourcc('v', 'i', 'e', 'w'), 0, &tag);
    write_u32be(65536U, 8, &tag);   // illum X = 1.0
    write_u32be(32768U, 12, &tag);  // illum Y = 0.5
    write_u32be(16384U, 16, &tag);  // illum Z = 0.25
    write_u32be(6554U, 20, &tag);   // sur X = 0.100006
    write_u32be(13107U, 24, &tag);  // sur Y = 0.199997
    write_u32be(19661U, 28, &tag);  // sur Z = 0.300003
    write_u32be(2U, 32, &tag);      // D65

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('v', 'i', 'e', 'w'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "view");
    EXPECT_EQ(out.rows, 2U);
    EXPECT_EQ(out.cols, 3U);
    ASSERT_EQ(out.values.size(), 6U);
    EXPECT_NEAR(out.values[0], 1.0, 1e-9);
    EXPECT_NEAR(out.values[1], 0.5, 1e-9);
    EXPECT_NEAR(out.values[2], 0.25, 1e-9);
    EXPECT_NEAR(out.values[3], 6554.0 / 65536.0, 1e-9);
    EXPECT_NEAR(out.values[4], 13107.0 / 65536.0, 1e-9);
    EXPECT_NEAR(out.values[5], 19661.0 / 65536.0, 1e-9);
    EXPECT_NE(out.text.find("illuminant_type=2(D65)"), std::string::npos);
}

TEST(IccInterpret, DecodesMeasurementType)
{
    std::vector<std::byte> tag(36, std::byte { 0x00 });
    write_u32be(make_fourcc('m', 'e', 'a', 's'), 0, &tag);
    write_u32be(1U, 8, &tag);       // observer
    write_u32be(6554U, 12, &tag);   // backing X = 0.100006
    write_u32be(13107U, 16, &tag);  // backing Y = 0.199997
    write_u32be(19661U, 20, &tag);  // backing Z = 0.300003
    write_u32be(1U, 24, &tag);      // geometry
    write_u32be(32768U, 28, &tag);  // flare = 0.5
    write_u32be(2U, 32, &tag);      // illuminant (D65)

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('m', 'e', 'a', 's'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "meas");
    EXPECT_EQ(out.rows, 1U);
    EXPECT_EQ(out.cols, 4U);
    ASSERT_EQ(out.values.size(), 4U);
    EXPECT_NEAR(out.values[0], 6554.0 / 65536.0, 1e-9);
    EXPECT_NEAR(out.values[1], 13107.0 / 65536.0, 1e-9);
    EXPECT_NEAR(out.values[2], 19661.0 / 65536.0, 1e-9);
    EXPECT_NEAR(out.values[3], 0.5, 1e-9);
    EXPECT_NE(out.text.find("observer=1(CIE1931_2deg)"), std::string::npos);
    EXPECT_NE(out.text.find("geometry=1(0_45_or_45_0)"), std::string::npos);
    EXPECT_NE(out.text.find("illuminant=2(D65)"), std::string::npos);
}

TEST(IccInterpret, DecodesChromaticityType)
{
    std::vector<std::byte> tag(36, std::byte { 0x00 });
    write_u32be(make_fourcc('c', 'h', 'r', 'm'), 0, &tag);
    write_u16be(3U, 8, &tag);   // channels
    write_u16be(1U, 10, &tag);  // BT.709

    // 3 x (x,y), u16Fixed16
    write_u32be(0x0000A666U, 12, &tag);  // 0.64999
    write_u32be(0x00005555U, 16, &tag);  // 0.33333
    write_u32be(0x00004CCC, 20, &tag);   // 0.29999
    write_u32be(0x0000999A, 24, &tag);   // 0.60001
    write_u32be(0x00002666, 28, &tag);   // 0.14999
    write_u32be(0x0000099A, 32, &tag);   // 0.03751

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('r', 'X', 'Y', 'Z'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "chrm");
    EXPECT_EQ(out.rows, 3U);
    EXPECT_EQ(out.cols, 2U);
    ASSERT_EQ(out.values.size(), 6U);
    EXPECT_NEAR(out.values[0], 0.65, 1e-4);
    EXPECT_NEAR(out.values[1], 0.3333, 1e-4);
    EXPECT_NEAR(out.values[2], 0.3, 1e-4);
    EXPECT_NEAR(out.values[3], 0.6, 1e-4);
    EXPECT_NEAR(out.values[4], 0.15, 1e-4);
    EXPECT_NEAR(out.values[5], 0.0375, 1e-4);
    EXPECT_NE(out.text.find("channels=3"), std::string::npos);
    EXPECT_NE(out.text.find("colorant=1(ITU-R_BT.709)"), std::string::npos);
}

TEST(IccInterpret, RejectsMalformedChromaticityType)
{
    std::vector<std::byte> tag(20, std::byte { 0x00 });
    write_u32be(make_fourcc('c', 'h', 'r', 'm'), 0, &tag);
    write_u16be(3U, 8, &tag);  // channels=3 requires 24 payload bytes
    write_u16be(0U, 10, &tag);
    write_u32be(0x00010000U, 12, &tag);
    write_u32be(0x00010000U, 16, &tag);

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('g', 'X', 'Y', 'Z'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Malformed);
}

TEST(IccInterpret, DecodesSf32Array)
{
    std::vector<std::byte> tag(16, std::byte { 0x00 });
    write_u32be(make_fourcc('s', 'f', '3', '2'), 0, &tag);
    write_u32be(0x00010000U, 8, &tag);   // 1.0
    write_u32be(0xFFFF8000U, 12, &tag);  // -0.5

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('b', 'k', 'p', 't'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "sf32");
    EXPECT_EQ(out.rows, 1U);
    EXPECT_EQ(out.cols, 2U);
    ASSERT_EQ(out.values.size(), 2U);
    EXPECT_NEAR(out.values[0], 1.0, 1e-9);
    EXPECT_NEAR(out.values[1], -0.5, 1e-9);
}

TEST(IccInterpret, DecodesUf32Array)
{
    std::vector<std::byte> tag(16, std::byte { 0x00 });
    write_u32be(make_fourcc('u', 'f', '3', '2'), 0, &tag);
    write_u32be(0x00018000U, 8, &tag);   // 1.5
    write_u32be(0x00004000U, 12, &tag);  // 0.25

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('c', 'h', 'a', 'd'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "uf32");
    EXPECT_EQ(out.rows, 1U);
    EXPECT_EQ(out.cols, 2U);
    ASSERT_EQ(out.values.size(), 2U);
    EXPECT_NEAR(out.values[0], 1.5, 1e-9);
    EXPECT_NEAR(out.values[1], 0.25, 1e-9);
}

TEST(IccInterpret, DecodesUi16Array)
{
    std::vector<std::byte> tag(14, std::byte { 0x00 });
    write_u32be(make_fourcc('u', 'i', '1', '6'), 0, &tag);
    write_u16be(100U, 8, &tag);
    write_u16be(200U, 10, &tag);
    write_u16be(65535U, 12, &tag);

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('c', 'h', 'a', 'd'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "ui16");
    EXPECT_EQ(out.rows, 1U);
    EXPECT_EQ(out.cols, 3U);
    ASSERT_EQ(out.values.size(), 3U);
    EXPECT_EQ(out.values[0], 100.0);
    EXPECT_EQ(out.values[1], 200.0);
    EXPECT_EQ(out.values[2], 65535.0);
}

TEST(IccInterpret, DecodesUi32ArrayWithValueLimit)
{
    std::vector<std::byte> tag(20, std::byte { 0x00 });
    write_u32be(make_fourcc('u', 'i', '3', '2'), 0, &tag);
    write_u32be(1U, 8, &tag);
    write_u32be(65536U, 12, &tag);
    write_u32be(4294967295U, 16, &tag);

    IccTagInterpretation out;
    IccTagInterpretOptions opts;
    opts.limits.max_values = 2U;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('c', 'h', 'a', 'd'), tag, &out, opts);
    EXPECT_EQ(st, IccTagInterpretStatus::LimitExceeded);
    EXPECT_EQ(out.type, "ui32");
    EXPECT_EQ(out.rows, 1U);
    EXPECT_EQ(out.cols, 2U);
    ASSERT_EQ(out.values.size(), 2U);
    EXPECT_EQ(out.values[0], 1.0);
    EXPECT_EQ(out.values[1], 65536.0);
}

TEST(IccInterpret, SummarizesMft1)
{
    // mft1 with in=3, out=3, clut_points=2 => need=48+768+24+768 = 1608.
    std::vector<std::byte> tag(1608, std::byte { 0x00 });
    write_u32be(make_fourcc('m', 'f', 't', '1'), 0, &tag);
    tag[8]  = std::byte { 3 };
    tag[9]  = std::byte { 3 };
    tag[10] = std::byte { 2 };

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('A', '2', 'B', '0'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "mft1");
    EXPECT_TRUE(out.values.empty());
    EXPECT_NE(out.text.find("mft1 in=3 out=3 clut_points=2"),
              std::string::npos);
    EXPECT_NE(out.text.find("in_tbl=768"), std::string::npos);
    EXPECT_NE(out.text.find("clut=24"), std::string::npos);
    EXPECT_NE(out.text.find("out_tbl=768"), std::string::npos);
}

TEST(IccInterpret, SummarizesMft2)
{
    // mft2 with in=3, out=3, clut_points=2, in_entries=4, out_entries=4
    // values=12+24+12=48, bytes=96, need=52+96=148.
    std::vector<std::byte> tag(148, std::byte { 0x00 });
    write_u32be(make_fourcc('m', 'f', 't', '2'), 0, &tag);
    tag[8]  = std::byte { 3 };
    tag[9]  = std::byte { 3 };
    tag[10] = std::byte { 2 };
    write_u16be(4U, 48, &tag);
    write_u16be(4U, 50, &tag);

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('B', '2', 'A', '0'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "mft2");
    EXPECT_TRUE(out.values.empty());
    EXPECT_NE(out.text.find("mft2 in=3 out=3 clut_points=2"),
              std::string::npos);
    EXPECT_NE(out.text.find("in_tbl=12"), std::string::npos);
    EXPECT_NE(out.text.find("clut=24"), std::string::npos);
    EXPECT_NE(out.text.find("out_tbl=12"), std::string::npos);
    EXPECT_NE(out.text.find("in_entries=4"), std::string::npos);
    EXPECT_NE(out.text.find("out_entries=4"), std::string::npos);
}

TEST(IccInterpret, SummarizesMabStructure)
{
    std::vector<std::byte> tag(512, std::byte { 0x00 });
    write_u32be(make_fourcc('m', 'A', 'B', ' '), 0, &tag);
    tag[8] = std::byte { 3 };
    tag[9] = std::byte { 3 };
    write_u32be(64U, 12, &tag);   // B curves
    write_u32be(128U, 16, &tag);  // matrix
    write_u32be(192U, 20, &tag);  // M curves
    write_u32be(256U, 24, &tag);  // CLUT
    write_u32be(320U, 28, &tag);  // A curves

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('A', '2', 'B', '0'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "mAB ");
    EXPECT_NE(out.text.find("mAB in=3 out=3"), std::string::npos);
    EXPECT_NE(out.text.find("blocks=B,matrix,M,CLUT,A"), std::string::npos);
    EXPECT_NE(out.text.find("offs=B:64"), std::string::npos);
}

TEST(IccInterpret, SummarizesMbaStructure)
{
    std::vector<std::byte> tag(256, std::byte { 0x00 });
    write_u32be(make_fourcc('m', 'B', 'A', ' '), 0, &tag);
    tag[8] = std::byte { 4 };
    tag[9] = std::byte { 3 };
    write_u32be(80U, 12, &tag);   // B curves
    write_u32be(0U, 16, &tag);    // matrix absent
    write_u32be(0U, 20, &tag);    // M curves absent
    write_u32be(144U, 24, &tag);  // CLUT
    write_u32be(0U, 28, &tag);    // A curves absent

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('B', '2', 'A', '0'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "mBA ");
    EXPECT_NE(out.text.find("mBA in=4 out=3"), std::string::npos);
    EXPECT_NE(out.text.find("blocks=B,CLUT"), std::string::npos);
    EXPECT_NE(out.text.find("offs=B:80"), std::string::npos);
    EXPECT_NE(out.text.find("CLUT:144"), std::string::npos);
}

TEST(IccInterpret, MabMalformedOffsetOutOfRange)
{
    std::vector<std::byte> tag(128, std::byte { 0x00 });
    write_u32be(make_fourcc('m', 'A', 'B', ' '), 0, &tag);
    tag[8] = std::byte { 3 };
    tag[9] = std::byte { 3 };
    write_u32be(200U, 12, &tag);  // out of range for size 128

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('A', '2', 'B', '0'), tag, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Malformed);
}

TEST(IccInterpret, DecodesMlucPreferredEnUs)
{
    // mluc with 2 records, prefers enUS over frFR.
    std::vector<std::byte> tag(56, std::byte { 0x00 });
    write_u32be(make_fourcc('m', 'l', 'u', 'c'), 0, &tag);
    write_u32be(2U, 8, &tag);    // record count
    write_u32be(12U, 12, &tag);  // record size

    // Record 0: frFR -> "Salut"
    tag[16] = std::byte { 'f' };
    tag[17] = std::byte { 'r' };
    tag[18] = std::byte { 'F' };
    tag[19] = std::byte { 'R' };
    write_u32be(10U, 20, &tag);  // len bytes (UTF-16BE)
    write_u32be(40U, 24, &tag);  // offset

    // Record 1: enUS -> "Hello"
    tag[28] = std::byte { 'e' };
    tag[29] = std::byte { 'n' };
    tag[30] = std::byte { 'U' };
    tag[31] = std::byte { 'S' };
    write_u32be(10U, 32, &tag);
    write_u32be(50U, 36, &tag);

    // UTF-16BE "Salut"
    tag[40] = std::byte { 0x00 };
    tag[41] = std::byte { 'S' };
    tag[42] = std::byte { 0x00 };
    tag[43] = std::byte { 'a' };
    tag[44] = std::byte { 0x00 };
    tag[45] = std::byte { 'l' };
    tag[46] = std::byte { 0x00 };
    tag[47] = std::byte { 'u' };
    tag[48] = std::byte { 0x00 };
    tag[49] = std::byte { 't' };

    // UTF-16BE "Hello"
    tag[50] = std::byte { 0x00 };
    tag[51] = std::byte { 'H' };
    tag[52] = std::byte { 0x00 };
    tag[53] = std::byte { 'e' };
    tag[54] = std::byte { 0x00 };
    tag[55] = std::byte { 'l' };
    // "lo" gets truncated by tag size intentionally in this test to ensure
    // malformed tables are rejected below; create full-good table first.

    std::vector<std::byte> good = tag;
    good.resize(60, std::byte { 0x00 });
    good[56] = std::byte { 0x00 };
    good[57] = std::byte { 'l' };
    good[58] = std::byte { 0x00 };
    good[59] = std::byte { 'o' };

    IccTagInterpretation out;
    const IccTagInterpretStatus st
        = interpret_icc_tag(make_fourcc('c', 'p', 'r', 't'), good, &out);
    EXPECT_EQ(st, IccTagInterpretStatus::Ok);
    EXPECT_EQ(out.type, "mluc");
    EXPECT_EQ(out.text, "Hello");

    std::vector<std::byte> bad = good;
    // Invalidate both record lengths (odd UTF-16 byte counts).
    write_u32be(11U, 20, &bad);
    write_u32be(11U, 32, &bad);

    const IccTagInterpretStatus malformed
        = interpret_icc_tag(make_fourcc('c', 'p', 'r', 't'), bad, &out);
    EXPECT_EQ(malformed, IccTagInterpretStatus::Malformed);
}

TEST(IccInterpret, FormatsDisplayValueForCliPython)
{
    std::vector<std::byte> sig_tag(12, std::byte { 0x00 });
    write_u32be(make_fourcc('s', 'i', 'g', ' '), 0, &sig_tag);
    write_u32be(make_fourcc('s', 'c', 'n', 'r'), 8, &sig_tag);

    std::string text;
    EXPECT_TRUE(format_icc_tag_display_value(make_fourcc('t', 'e', 'c', 'h'),
                                             sig_tag, 16U, 256U, &text));
    EXPECT_EQ(text, "scnr");

    std::vector<std::byte> xyz_tag(20, std::byte { 0x00 });
    write_u32be(make_fourcc('X', 'Y', 'Z', ' '), 0, &xyz_tag);
    write_u32be(65536U, 8, &xyz_tag);
    write_u32be(0U, 12, &xyz_tag);
    write_u32be(32768U, 16, &xyz_tag);
    EXPECT_TRUE(format_icc_tag_display_value(make_fourcc('w', 't', 'p', 't'),
                                             xyz_tag, 16U, 256U, &text));
    EXPECT_EQ(text, "[1, 0, 0.5]");

    std::vector<std::byte> sf32_tag(16, std::byte { 0x00 });
    write_u32be(make_fourcc('s', 'f', '3', '2'), 0, &sf32_tag);
    write_u32be(0x00010000U, 8, &sf32_tag);
    write_u32be(0xFFFF8000U, 12, &sf32_tag);
    EXPECT_TRUE(format_icc_tag_display_value(make_fourcc('b', 'k', 'p', 't'),
                                             sf32_tag, 16U, 256U, &text));
    EXPECT_EQ(text, "[1, -0.5]");

    std::vector<std::byte> mft1_tag(1608, std::byte { 0x00 });
    write_u32be(make_fourcc('m', 'f', 't', '1'), 0, &mft1_tag);
    mft1_tag[8]  = std::byte { 3 };
    mft1_tag[9]  = std::byte { 3 };
    mft1_tag[10] = std::byte { 2 };
    EXPECT_TRUE(format_icc_tag_display_value(make_fourcc('A', '2', 'B', '0'),
                                             mft1_tag, 16U, 256U, &text));
    EXPECT_NE(text.find("mft1 in=3 out=3 clut_points=2"), std::string::npos);

    std::vector<std::byte> mab_tag(512, std::byte { 0x00 });
    write_u32be(make_fourcc('m', 'A', 'B', ' '), 0, &mab_tag);
    mab_tag[8] = std::byte { 3 };
    mab_tag[9] = std::byte { 3 };
    write_u32be(64U, 12, &mab_tag);
    write_u32be(128U, 16, &mab_tag);
    write_u32be(192U, 20, &mab_tag);
    write_u32be(256U, 24, &mab_tag);
    write_u32be(320U, 28, &mab_tag);
    EXPECT_TRUE(format_icc_tag_display_value(make_fourcc('A', '2', 'B', '0'),
                                             mab_tag, 16U, 256U, &text));
    EXPECT_NE(text.find("mAB in=3 out=3"), std::string::npos);
    EXPECT_NE(text.find("blocks=B,matrix,M,CLUT,A"), std::string::npos);

    std::vector<std::byte> view_tag(36, std::byte { 0x00 });
    write_u32be(make_fourcc('v', 'i', 'e', 'w'), 0, &view_tag);
    write_u32be(2U, 32, &view_tag);
    EXPECT_TRUE(format_icc_tag_display_value(make_fourcc('v', 'i', 'e', 'w'),
                                             view_tag, 16U, 256U, &text));
    EXPECT_NE(text.find("view illuminant_type=2(D65)"), std::string::npos);

    std::vector<std::byte> chrm_tag(36, std::byte { 0x00 });
    write_u32be(make_fourcc('c', 'h', 'r', 'm'), 0, &chrm_tag);
    write_u16be(3U, 8, &chrm_tag);
    write_u16be(1U, 10, &chrm_tag);
    write_u32be(0x0000A666U, 12, &chrm_tag);
    write_u32be(0x00005555U, 16, &chrm_tag);
    write_u32be(0x00004CCCU, 20, &chrm_tag);
    write_u32be(0x0000999AU, 24, &chrm_tag);
    write_u32be(0x00002666U, 28, &chrm_tag);
    write_u32be(0x0000099AU, 32, &chrm_tag);
    EXPECT_TRUE(format_icc_tag_display_value(make_fourcc('r', 'X', 'Y', 'Z'),
                                             chrm_tag, 16U, 256U, &text));
    EXPECT_NE(text.find("chrm channels=3 colorant=1(ITU-R_BT.709)"),
              std::string::npos);
}

}  // namespace openmeta
