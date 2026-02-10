#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static uint32_t count_substring(std::string_view s,
                                    std::string_view needle) noexcept
    {
        if (needle.empty()) {
            return 0U;
        }
        uint32_t count = 0U;
        size_t pos     = 0U;
        while (pos < s.size()) {
            const size_t found = s.find(needle, pos);
            if (found == std::string_view::npos) {
                break;
            }
            count += 1U;
            pos = found + needle.size();
        }
        return count;
    }

}  // namespace

TEST(XmpDump, EmitsValidPacketAndKey)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry e;
    e.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    e.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    e.origin.block = block;
    e.origin.order_in_block = 0;
    e.origin.wire_type      = WireType { WireFamily::Tiff, 2 /*ASCII*/ };
    e.origin.wire_count     = 5;
    (void)store.add_entry(e);

    store.finalize();

    XmpDumpOptions opts;
    std::vector<std::byte> out(64);

    XmpDumpResult r1
        = dump_xmp_lossless(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r1.status, XmpDumpStatus::OutputTruncated);
    ASSERT_GT(r1.needed, out.size());

    out.resize(static_cast<size_t>(r1.needed));
    const XmpDumpResult r2
        = dump_xmp_lossless(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r2.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r2.entries, 1U);
    ASSERT_EQ(r2.written, r2.needed);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r2.written));
    EXPECT_NE(s.find("<x:xmpmeta"), std::string_view::npos);
    EXPECT_NE(s.find("urn:openmeta:dump:1.0"), std::string_view::npos);
    EXPECT_NE(s.find("exif:ifd0:0x010F"), std::string_view::npos);
    EXPECT_NE(s.find("Q2Fub24="), std::string_view::npos);  // base64("Canon")
}


TEST(XmpDump, EmitsPortablePacketWithExifAndTiff)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry make;
    make.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    make.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    make.origin.block = block;
    make.origin.order_in_block = 0;
    (void)store.add_entry(make);

    Entry exp;
    exp.key          = make_exif_tag_key(store.arena(), "exififd", 0x829A);
    exp.value        = make_urational(1, 1250);
    exp.origin.block = block;
    exp.origin.order_in_block = 1;
    (void)store.add_entry(exp);

    store.finalize();

    XmpPortableOptions opts;
    std::vector<std::byte> out(64);

    XmpDumpResult r1
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r1.status, XmpDumpStatus::OutputTruncated);
    ASSERT_GT(r1.needed, out.size());

    out.resize(static_cast<size_t>(r1.needed));
    const XmpDumpResult r2
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r2.status, XmpDumpStatus::Ok);
    ASSERT_GE(r2.entries, 2U);
    ASSERT_EQ(r2.written, r2.needed);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r2.written));
    EXPECT_NE(s.find("http://ns.adobe.com/exif/1.0/"), std::string_view::npos);
    EXPECT_NE(s.find("http://ns.adobe.com/tiff/1.0/"), std::string_view::npos);
    EXPECT_NE(s.find("<tiff:Make>Canon</tiff:Make>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExposureTime>1/1250</exif:ExposureTime>"),
              std::string_view::npos);
}

TEST(XmpDump, EmitsExrTypeNameInLosslessDump)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry e;
    e.key = make_exr_attribute_key(store.arena(), 0, "customA");
    const std::array<std::byte, 3> raw { std::byte { 0xAA }, std::byte { 0xBB },
                                         std::byte { 0xCC } };
    e.value                 = make_bytes(store.arena(),
                                         std::span<const std::byte>(raw.data(), raw.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    e.origin.wire_type      = WireType { WireFamily::Other, 31U };
    e.origin.wire_count     = 3U;
    e.origin.wire_type_name = store.arena().append_string("myVendorFoo");
    (void)store.add_entry(e);

    store.finalize();

    XmpDumpOptions opts;
    std::vector<std::byte> out(2048);
    const XmpDumpResult r
        = dump_xmp_lossless(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("exr:part:0:customA"), std::string_view::npos);
    EXPECT_NE(s.find("<omd:exrTypeName>myVendorFoo</omd:exrTypeName>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableIncludeExistingXmpSwitch)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry exif_make;
    exif_make.key   = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    exif_make.value = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    exif_make.origin.block          = block;
    exif_make.origin.order_in_block = 0;
    (void)store.add_entry(exif_make);

    Entry xmp_rating;
    xmp_rating.key                   = make_xmp_property_key(store.arena(),
                                                             "http://ns.adobe.com/xap/1.0/",
                                                             "Rating");
    xmp_rating.value                 = make_u16(5);
    xmp_rating.origin.block          = block;
    xmp_rating.origin.order_in_block = 1;
    (void)store.add_entry(xmp_rating);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(1024);
    XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 0U);

    std::string_view s(reinterpret_cast<const char*>(out.data()),
                       static_cast<size_t>(r.written));
    EXPECT_EQ(s.find("<tiff:Make>"), std::string_view::npos);
    EXPECT_EQ(s.find("<xmp:Rating>"), std::string_view::npos);

    opts.include_existing_xmp = true;
    r = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                          opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    s = std::string_view(reinterpret_cast<const char*>(out.data()),
                         static_cast<size_t>(r.written));
    EXPECT_EQ(s.find("<tiff:Make>"), std::string_view::npos);
    EXPECT_NE(s.find("<xmp:Rating>5</xmp:Rating>"), std::string_view::npos);
}


TEST(XmpDump, PortableExistingXmpIndexedPathEmitsSeq)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry li1;
    li1.key          = make_xmp_property_key(store.arena(),
                                             "http://ns.adobe.com/exif/1.0/",
                                             "GPSLatitude[1]");
    li1.value        = make_text(store.arena(), "41", TextEncoding::Ascii);
    li1.origin.block = block;
    li1.origin.order_in_block = 0;
    (void)store.add_entry(li1);

    Entry li2;
    li2.key          = make_xmp_property_key(store.arena(),
                                             "http://ns.adobe.com/exif/1.0/",
                                             "GPSLatitude[2]");
    li2.value        = make_text(store.arena(), "24", TextEncoding::Ascii);
    li2.origin.block = block;
    li2.origin.order_in_block = 1;
    (void)store.add_entry(li2);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(1024);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<exif:GPSLatitude>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:Seq>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>41</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>24</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortableDeduplicatesSamePropertyName)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry width0;
    width0.key          = make_exif_tag_key(store.arena(), "ifd0", 0x0100);
    width0.value        = make_u32(5184);
    width0.origin.block = block;
    width0.origin.order_in_block = 0;
    (void)store.add_entry(width0);

    Entry width1;
    width1.key          = make_exif_tag_key(store.arena(), "ifd1", 0x0100);
    width1.value        = make_u32(668);
    width1.origin.block = block;
    width1.origin.order_in_block = 1;
    (void)store.add_entry(width1);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(2048);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_EQ(count_substring(s, "<tiff:ImageWidth>"), 1U);
    EXPECT_NE(s.find("<tiff:ImageWidth>5184</tiff:ImageWidth>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableUsesCanonicalXmpPropertyNames)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry image_length;
    image_length.key = make_exif_tag_key(store.arena(), "ifd0", 0x0101);
    image_length.value                 = make_u32(3456);
    image_length.origin.block          = block;
    image_length.origin.order_in_block = 0;
    (void)store.add_entry(image_length);

    Entry exposure_bias;
    exposure_bias.key = make_exif_tag_key(store.arena(), "exififd", 0x9204);
    exposure_bias.value                 = make_srational(0, 1);
    exposure_bias.origin.block          = block;
    exposure_bias.origin.order_in_block = 1;
    (void)store.add_entry(exposure_bias);

    Entry iso;
    iso.key   = make_exif_tag_key(store.arena(), "exififd", 0x8827);
    iso.value = make_u16(400);
    iso.origin.block          = block;
    iso.origin.order_in_block = 2;
    (void)store.add_entry(iso);

    Entry pixel_x;
    pixel_x.key   = make_exif_tag_key(store.arena(), "exififd", 0xA002);
    pixel_x.value = make_u16(6000);
    pixel_x.origin.block          = block;
    pixel_x.origin.order_in_block = 3;
    (void)store.add_entry(pixel_x);

    Entry pixel_y;
    pixel_y.key   = make_exif_tag_key(store.arena(), "exififd", 0xA003);
    pixel_y.value = make_u16(4000);
    pixel_y.origin.block          = block;
    pixel_y.origin.order_in_block = 4;
    (void)store.add_entry(pixel_y);

    Entry focal35;
    focal35.key = make_exif_tag_key(store.arena(), "exififd", 0xA405);
    focal35.value                 = make_u16(50);
    focal35.origin.block          = block;
    focal35.origin.order_in_block = 5;
    (void)store.add_entry(focal35);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<tiff:ImageHeight>3456</tiff:ImageHeight>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExposureCompensation>0</exif:ExposureCompensation>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ISO>400</exif:ISO>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExifImageWidth>6000</exif:ExifImageWidth>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExifImageHeight>4000</exif:ExifImageHeight>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<exif:FocalLengthIn35mmFormat>50</exif:FocalLengthIn35mmFormat>"),
        std::string_view::npos);

    EXPECT_EQ(s.find("<tiff:ImageLength>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ExposureBiasValue>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ISOSpeedRatings>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:PixelXDimension>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:PixelYDimension>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:FocalLengthIn35mmFilm>"), std::string_view::npos);
}

TEST(XmpDump, PortableNormalizesRationalAndSkipsXmlPacket)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry xres;
    xres.key   = make_exif_tag_key(store.arena(), "ifd0", 0x011A);
    xres.value = make_urational(72, 1);
    xres.origin.block          = block;
    xres.origin.order_in_block = 0;
    (void)store.add_entry(xres);

    Entry exp_comp;
    exp_comp.key   = make_exif_tag_key(store.arena(), "exififd", 0x9204);
    exp_comp.value = make_srational(10, 20);
    exp_comp.origin.block          = block;
    exp_comp.origin.order_in_block = 1;
    (void)store.add_entry(exp_comp);

    Entry xml_packet;
    xml_packet.key = make_exif_tag_key(store.arena(), "ifd0", 0x02BC);
    xml_packet.value = make_text(store.arena(), "<x:xmpmeta/>",
                                 TextEncoding::Ascii);
    xml_packet.origin.block          = block;
    xml_packet.origin.order_in_block = 2;
    (void)store.add_entry(xml_packet);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<tiff:XResolution>72</tiff:XResolution>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<exif:ExposureCompensation>1/2</exif:ExposureCompensation>"),
        std::string_view::npos);
    EXPECT_EQ(s.find("<tiff:XMLPacket>"), std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesExistingXmpPropertyNames)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry image_length;
    image_length.key = make_xmp_property_key(store.arena(),
                                             "http://ns.adobe.com/tiff/1.0/",
                                             "ImageLength");
    image_length.value                 = make_u32(3456);
    image_length.origin.block          = block;
    image_length.origin.order_in_block = 0;
    (void)store.add_entry(image_length);

    Entry exposure_bias;
    exposure_bias.key = make_xmp_property_key(store.arena(),
                                              "http://ns.adobe.com/exif/1.0/",
                                              "ExposureBiasValue");
    exposure_bias.value                 = make_u16(0);
    exposure_bias.origin.block          = block;
    exposure_bias.origin.order_in_block = 1;
    (void)store.add_entry(exposure_bias);

    Entry iso;
    iso.key = make_xmp_property_key(store.arena(),
                                    "http://ns.adobe.com/exif/1.0/",
                                    "ISOSpeedRatings");
    iso.value                 = make_u16(400);
    iso.origin.block          = block;
    iso.origin.order_in_block = 2;
    (void)store.add_entry(iso);

    Entry pixel_x;
    pixel_x.key = make_xmp_property_key(store.arena(),
                                        "http://ns.adobe.com/exif/1.0/",
                                        "PixelXDimension");
    pixel_x.value                 = make_u16(6000);
    pixel_x.origin.block          = block;
    pixel_x.origin.order_in_block = 3;
    (void)store.add_entry(pixel_x);

    Entry pixel_y;
    pixel_y.key = make_xmp_property_key(store.arena(),
                                        "http://ns.adobe.com/exif/1.0/",
                                        "PixelYDimension");
    pixel_y.value                 = make_u16(4000);
    pixel_y.origin.block          = block;
    pixel_y.origin.order_in_block = 4;
    (void)store.add_entry(pixel_y);

    Entry focal35;
    focal35.key = make_xmp_property_key(store.arena(),
                                        "http://ns.adobe.com/exif/1.0/",
                                        "FocalLengthIn35mmFilm");
    focal35.value                 = make_u16(50);
    focal35.origin.block          = block;
    focal35.origin.order_in_block = 5;
    (void)store.add_entry(focal35);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = false;
    opts.include_existing_xmp = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(r.entries, 6U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find("<tiff:ImageHeight>3456</tiff:ImageHeight>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExposureCompensation>0</exif:ExposureCompensation>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ISO>400</exif:ISO>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExifImageWidth>6000</exif:ExifImageWidth>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExifImageHeight>4000</exif:ExifImageHeight>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<exif:FocalLengthIn35mmFormat>50</exif:FocalLengthIn35mmFormat>"),
        std::string_view::npos);

    EXPECT_EQ(s.find("<tiff:ImageLength>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ExposureBiasValue>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ISOSpeedRatings>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:PixelXDimension>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:PixelYDimension>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:FocalLengthIn35mmFilm>"), std::string_view::npos);
}

TEST(XmpDump, PortablePrintConvertsCommonExifEnumsAndValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry orientation;
    orientation.key   = make_exif_tag_key(store.arena(), "ifd0", 0x0112);
    orientation.value = make_u16(6);
    orientation.origin.block          = block;
    orientation.origin.order_in_block = 0;
    (void)store.add_entry(orientation);

    Entry resolution_unit;
    resolution_unit.key = make_exif_tag_key(store.arena(), "ifd0", 0x0128);
    resolution_unit.value                 = make_u16(2);
    resolution_unit.origin.block          = block;
    resolution_unit.origin.order_in_block = 1;
    (void)store.add_entry(resolution_unit);

    Entry metering_mode;
    metering_mode.key = make_exif_tag_key(store.arena(), "exififd", 0x9207);
    metering_mode.value                 = make_u16(5);
    metering_mode.origin.block          = block;
    metering_mode.origin.order_in_block = 2;
    (void)store.add_entry(metering_mode);

    Entry exposure_program;
    exposure_program.key = make_exif_tag_key(store.arena(), "exififd", 0x8822);
    exposure_program.value                 = make_u16(2);
    exposure_program.origin.block          = block;
    exposure_program.origin.order_in_block = 3;
    (void)store.add_entry(exposure_program);

    Entry focal_length;
    focal_length.key = make_exif_tag_key(store.arena(), "exififd", 0x920A);
    focal_length.value                 = make_urational(66, 1);
    focal_length.origin.block          = block;
    focal_length.origin.order_in_block = 4;
    (void)store.add_entry(focal_length);

    Entry shutter_speed;
    shutter_speed.key = make_exif_tag_key(store.arena(), "exififd", 0x9201);
    shutter_speed.value                 = make_srational(6, 1);
    shutter_speed.origin.block          = block;
    shutter_speed.origin.order_in_block = 5;
    (void)store.add_entry(shutter_speed);

    const std::array<URational, 4> lens_spec = {
        URational { 24, 1 },
        URational { 70, 1 },
        URational { 0, 1 },
        URational { 0, 1 },
    };
    Entry lens_spec_entry;
    lens_spec_entry.key = make_exif_tag_key(store.arena(), "exififd", 0xA432);
    lens_spec_entry.value = make_urational_array(
        store.arena(), std::span<const URational>(lens_spec.data(),
                                                  lens_spec.size()));
    lens_spec_entry.origin.block          = block;
    lens_spec_entry.origin.order_in_block = 6;
    (void)store.add_entry(lens_spec_entry);

    const std::array<uint8_t, 4> gps_ver = { 2, 3, 0, 0 };
    Entry gps_version;
    gps_version.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0000);
    gps_version.value = make_u8_array(store.arena(),
                                      std::span<const uint8_t>(gps_ver.data(),
                                                               gps_ver.size()));
    gps_version.origin.block          = block;
    gps_version.origin.order_in_block = 7;
    (void)store.add_entry(gps_version);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif         = true;
    opts.include_existing_xmp = false;

    std::vector<std::byte> out(8192);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));

    EXPECT_NE(s.find("<tiff:Orientation>6</tiff:Orientation>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<tiff:ResolutionUnit>2</tiff:ResolutionUnit>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:MeteringMode>5</exif:MeteringMode>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExposureProgram>2</exif:ExposureProgram>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:FocalLength>66.0 mm</exif:FocalLength>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ShutterSpeedValue>1/64</exif:ShutterSpeedValue>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSVersionID>2</exif:GPSVersionID>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>24</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>70</rdf:li>"), std::string_view::npos);
}

}  // namespace openmeta
