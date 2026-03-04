#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"
#include "openmeta/xmp_dump.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>
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


TEST(XmpDump, SidecarApiLosslessAutoResizesOutput)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry e;
    e.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    e.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    e.origin.block = block;
    e.origin.order_in_block = 0;
    (void)store.add_entry(e);
    store.finalize();

    XmpSidecarOptions opts;
    opts.format               = XmpSidecarFormat::Lossless;
    opts.initial_output_bytes = 16U;

    std::vector<std::byte> out;
    const XmpDumpResult r = dump_xmp_sidecar(store, &out, opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(out.size(), static_cast<size_t>(r.written));
    ASSERT_GE(r.entries, 1U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             out.size());
    EXPECT_NE(s.find("<x:xmpmeta"), std::string_view::npos);
    EXPECT_NE(s.find("exif:ifd0:0x010F"), std::string_view::npos);
}


TEST(XmpDump, SidecarApiPortableUsesFormatSwitch)
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

    XmpSidecarOptions opts;
    opts.format                = XmpSidecarFormat::Portable;
    opts.initial_output_bytes  = 32U;
    opts.portable.include_exif = true;

    std::vector<std::byte> out;
    const XmpDumpResult r = dump_xmp_sidecar(store, &out, opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);
    ASSERT_EQ(out.size(), static_cast<size_t>(r.written));
    ASSERT_GE(r.entries, 2U);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             out.size());
    EXPECT_NE(s.find("<tiff:Make>Canon</tiff:Make>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:ExposureTime>1/1250</exif:ExposureTime>"),
              std::string_view::npos);
}


TEST(XmpDump, SidecarRequestApiPortableIsDeterministic)
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

    Entry model;
    model.key   = make_exif_tag_key(store.arena(), "ifd0", 0x0110);
    model.value = make_text(store.arena(), "EOS R6", TextEncoding::Ascii);
    model.origin.block          = block;
    model.origin.order_in_block = 1;
    (void)store.add_entry(model);

    store.finalize();

    XmpSidecarRequest request;
    request.format               = XmpSidecarFormat::Portable;
    request.initial_output_bytes = 32U;

    std::vector<std::byte> out_a;
    std::vector<std::byte> out_b;
    const XmpDumpResult ra = dump_xmp_sidecar(store, &out_a, request);
    const XmpDumpResult rb = dump_xmp_sidecar(store, &out_b, request);

    ASSERT_EQ(ra.status, XmpDumpStatus::Ok);
    ASSERT_EQ(rb.status, XmpDumpStatus::Ok);
    ASSERT_EQ(ra.entries, rb.entries);
    ASSERT_EQ(ra.written, rb.written);
    ASSERT_EQ(out_a, out_b);
}


TEST(XmpDump, SidecarRequestApiRespectsOutputLimit)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry e;
    e.key          = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    e.value        = make_text(store.arena(), "Canon", TextEncoding::Ascii);
    e.origin.block = block;
    e.origin.order_in_block = 0;
    (void)store.add_entry(e);
    store.finalize();

    XmpSidecarRequest request;
    request.format                  = XmpSidecarFormat::Lossless;
    request.initial_output_bytes    = 32U;
    request.limits.max_output_bytes = 32U;

    std::vector<std::byte> out;
    const XmpDumpResult r = dump_xmp_sidecar(store, &out, request);
    ASSERT_EQ(r.status, XmpDumpStatus::LimitExceeded);
}


TEST(XmpDump, SidecarRequestPortableEscapesUnsafeText)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    std::string unsafe;
    unsafe.push_back('A');
    unsafe.push_back(static_cast<char>(0x01));
    unsafe.append("<>&\"'");

    Entry xmp_label;
    xmp_label.key   = make_xmp_property_key(store.arena(),
                                            "http://ns.adobe.com/xap/1.0/",
                                            "Label");
    xmp_label.value = make_text(store.arena(), unsafe, TextEncoding::Utf8);
    xmp_label.origin.block          = block;
    xmp_label.origin.order_in_block = 0;
    (void)store.add_entry(xmp_label);
    store.finalize();

    XmpSidecarRequest request;
    request.format               = XmpSidecarFormat::Portable;
    request.initial_output_bytes = 64U;
    request.include_exif         = false;
    request.include_existing_xmp = true;

    std::vector<std::byte> out;
    const XmpDumpResult r = dump_xmp_sidecar(store, &out, request);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             out.size());
    EXPECT_NE(s.find("<xmp:Label>A\\x01&lt;&gt;&amp;&quot;&apos;</xmp:Label>"),
              std::string_view::npos);
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
    image_length.key   = make_exif_tag_key(store.arena(), "ifd0", 0x0101);
    image_length.value = make_u32(3456);
    image_length.origin.block          = block;
    image_length.origin.order_in_block = 0;
    (void)store.add_entry(image_length);

    Entry exposure_bias;
    exposure_bias.key   = make_exif_tag_key(store.arena(), "exififd", 0x9204);
    exposure_bias.value = make_srational(0, 1);
    exposure_bias.origin.block          = block;
    exposure_bias.origin.order_in_block = 1;
    (void)store.add_entry(exposure_bias);

    Entry iso;
    iso.key          = make_exif_tag_key(store.arena(), "exififd", 0x8827);
    iso.value        = make_u16(400);
    iso.origin.block = block;
    iso.origin.order_in_block = 2;
    (void)store.add_entry(iso);

    Entry pixel_x;
    pixel_x.key          = make_exif_tag_key(store.arena(), "exififd", 0xA002);
    pixel_x.value        = make_u16(6000);
    pixel_x.origin.block = block;
    pixel_x.origin.order_in_block = 3;
    (void)store.add_entry(pixel_x);

    Entry pixel_y;
    pixel_y.key          = make_exif_tag_key(store.arena(), "exififd", 0xA003);
    pixel_y.value        = make_u16(4000);
    pixel_y.origin.block = block;
    pixel_y.origin.order_in_block = 4;
    (void)store.add_entry(pixel_y);

    Entry focal35;
    focal35.key          = make_exif_tag_key(store.arena(), "exififd", 0xA405);
    focal35.value        = make_u16(50);
    focal35.origin.block = block;
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
        s.find(
            "<exif:FocalLengthIn35mmFormat>50</exif:FocalLengthIn35mmFormat>"),
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
    xres.key          = make_exif_tag_key(store.arena(), "ifd0", 0x011A);
    xres.value        = make_urational(72, 1);
    xres.origin.block = block;
    xres.origin.order_in_block = 0;
    (void)store.add_entry(xres);

    Entry exp_comp;
    exp_comp.key          = make_exif_tag_key(store.arena(), "exififd", 0x9204);
    exp_comp.value        = make_srational(10, 20);
    exp_comp.origin.block = block;
    exp_comp.origin.order_in_block = 1;
    (void)store.add_entry(exp_comp);

    Entry xml_packet;
    xml_packet.key          = make_exif_tag_key(store.arena(), "ifd0", 0x02BC);
    xml_packet.value        = make_text(store.arena(), "<x:xmpmeta/>",
                                        TextEncoding::Ascii);
    xml_packet.origin.block = block;
    xml_packet.origin.order_in_block = 2;
    (void)store.add_entry(xml_packet);

    Entry maker_note;
    maker_note.key = make_exif_tag_key(store.arena(), "exififd", 0x927C);
    const std::array<std::byte, 4> maker_note_bytes = {
        std::byte { 0xDE },
        std::byte { 0xAD },
        std::byte { 0xBE },
        std::byte { 0xEF },
    };
    maker_note.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(maker_note_bytes.data(),
                                                maker_note_bytes.size()));
    maker_note.origin.block          = block;
    maker_note.origin.order_in_block = 3;
    (void)store.add_entry(maker_note);

    Entry dng_private;
    dng_private.key = make_exif_tag_key(store.arena(), "ifd0", 0xC634);
    const std::array<std::byte, 4> dng_private_bytes = {
        std::byte { 0xAA },
        std::byte { 0xBB },
        std::byte { 0xCC },
        std::byte { 0xDD },
    };
    dng_private.value
        = make_bytes(store.arena(),
                     std::span<const std::byte>(dng_private_bytes.data(),
                                                dng_private_bytes.size()));
    dng_private.origin.block          = block;
    dng_private.origin.order_in_block = 4;
    (void)store.add_entry(dng_private);

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
    EXPECT_NE(s.find(
                  "<exif:ExposureCompensation>0.5</exif:ExposureCompensation>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<tiff:XMLPacket>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:MakerNote>"), std::string_view::npos);
    EXPECT_EQ(s.find("<tiff:DNGPrivateData>"), std::string_view::npos);
}

TEST(XmpDump, PortableCanonicalizesExistingXmpPropertyNames)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry image_length;
    image_length.key                   = make_xmp_property_key(store.arena(),
                                                               "http://ns.adobe.com/tiff/1.0/",
                                                               "ImageLength");
    image_length.value                 = make_u32(3456);
    image_length.origin.block          = block;
    image_length.origin.order_in_block = 0;
    (void)store.add_entry(image_length);

    Entry exposure_bias;
    exposure_bias.key                   = make_xmp_property_key(store.arena(),
                                                                "http://ns.adobe.com/exif/1.0/",
                                                                "ExposureBiasValue");
    exposure_bias.value                 = make_u16(0);
    exposure_bias.origin.block          = block;
    exposure_bias.origin.order_in_block = 1;
    (void)store.add_entry(exposure_bias);

    Entry iso;
    iso.key                   = make_xmp_property_key(store.arena(),
                                                      "http://ns.adobe.com/exif/1.0/",
                                                      "ISOSpeedRatings");
    iso.value                 = make_u16(400);
    iso.origin.block          = block;
    iso.origin.order_in_block = 2;
    (void)store.add_entry(iso);

    Entry pixel_x;
    pixel_x.key                   = make_xmp_property_key(store.arena(),
                                                          "http://ns.adobe.com/exif/1.0/",
                                                          "PixelXDimension");
    pixel_x.value                 = make_u16(6000);
    pixel_x.origin.block          = block;
    pixel_x.origin.order_in_block = 3;
    (void)store.add_entry(pixel_x);

    Entry pixel_y;
    pixel_y.key                   = make_xmp_property_key(store.arena(),
                                                          "http://ns.adobe.com/exif/1.0/",
                                                          "PixelYDimension");
    pixel_y.value                 = make_u16(4000);
    pixel_y.origin.block          = block;
    pixel_y.origin.order_in_block = 4;
    (void)store.add_entry(pixel_y);

    Entry focal35;
    focal35.key                   = make_xmp_property_key(store.arena(),
                                                          "http://ns.adobe.com/exif/1.0/",
                                                          "FocalLengthIn35mmFilm");
    focal35.value                 = make_u16(50);
    focal35.origin.block          = block;
    focal35.origin.order_in_block = 5;
    (void)store.add_entry(focal35);

    Entry maker_note;
    maker_note.key   = make_xmp_property_key(store.arena(),
                                             "http://ns.adobe.com/exif/1.0/",
                                             "MakerNote");
    maker_note.value = make_text(store.arena(), "AAAA", TextEncoding::Ascii);
    maker_note.origin.block          = block;
    maker_note.origin.order_in_block = 6;
    (void)store.add_entry(maker_note);

    Entry dng_private;
    dng_private.key   = make_xmp_property_key(store.arena(),
                                              "http://ns.adobe.com/tiff/1.0/",
                                              "DNGPrivateData");
    dng_private.value = make_text(store.arena(), "BBBB", TextEncoding::Ascii);
    dng_private.origin.block          = block;
    dng_private.origin.order_in_block = 7;
    (void)store.add_entry(dng_private);

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
        s.find(
            "<exif:FocalLengthIn35mmFormat>50</exif:FocalLengthIn35mmFormat>"),
        std::string_view::npos);

    EXPECT_EQ(s.find("<tiff:ImageLength>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ExposureBiasValue>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ISOSpeedRatings>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:PixelXDimension>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:PixelYDimension>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:FocalLengthIn35mmFilm>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:MakerNote>"), std::string_view::npos);
    EXPECT_EQ(s.find("<tiff:DNGPrivateData>"), std::string_view::npos);
}

TEST(XmpDump, PortablePrintConvertsCommonExifEnumsAndValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry orientation;
    orientation.key          = make_exif_tag_key(store.arena(), "ifd0", 0x0112);
    orientation.value        = make_u16(6);
    orientation.origin.block = block;
    orientation.origin.order_in_block = 0;
    (void)store.add_entry(orientation);

    Entry resolution_unit;
    resolution_unit.key   = make_exif_tag_key(store.arena(), "ifd0", 0x0128);
    resolution_unit.value = make_u16(2);
    resolution_unit.origin.block          = block;
    resolution_unit.origin.order_in_block = 1;
    (void)store.add_entry(resolution_unit);

    Entry metering_mode;
    metering_mode.key   = make_exif_tag_key(store.arena(), "exififd", 0x9207);
    metering_mode.value = make_u16(5);
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
    focal_length.key   = make_exif_tag_key(store.arena(), "exififd", 0x920A);
    focal_length.value = make_urational(66, 1);
    focal_length.origin.block          = block;
    focal_length.origin.order_in_block = 4;
    (void)store.add_entry(focal_length);

    Entry shutter_speed;
    shutter_speed.key   = make_exif_tag_key(store.arena(), "exififd", 0x9201);
    shutter_speed.value = make_srational(6, 1);
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
    lens_spec_entry.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(lens_spec.data(),
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

    Entry gps_lat_ref;
    gps_lat_ref.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0001);
    gps_lat_ref.value = make_text(store.arena(), "N", TextEncoding::Ascii);
    gps_lat_ref.origin.block          = block;
    gps_lat_ref.origin.order_in_block = 8;
    (void)store.add_entry(gps_lat_ref);

    const std::array<URational, 3> gps_lat_triplet = {
        URational { 41, 1 },
        URational { 24, 1 },
        URational { 30, 1 },
    };
    Entry gps_lat;
    gps_lat.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0002);
    gps_lat.value = make_urational_array(
        store.arena(), std::span<const URational>(gps_lat_triplet.data(),
                                                  gps_lat_triplet.size()));
    gps_lat.origin.block          = block;
    gps_lat.origin.order_in_block = 9;
    (void)store.add_entry(gps_lat);

    Entry gps_lon_ref;
    gps_lon_ref.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0003);
    gps_lon_ref.value = make_text(store.arena(), "E", TextEncoding::Ascii);
    gps_lon_ref.origin.block          = block;
    gps_lon_ref.origin.order_in_block = 10;
    (void)store.add_entry(gps_lon_ref);

    const std::array<URational, 3> gps_lon_triplet = {
        URational { 2, 1 },
        URational { 9, 1 },
        URational { 0, 1 },
    };
    Entry gps_lon;
    gps_lon.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0004);
    gps_lon.value = make_urational_array(
        store.arena(), std::span<const URational>(gps_lon_triplet.data(),
                                                  gps_lon_triplet.size()));
    gps_lon.origin.block          = block;
    gps_lon.origin.order_in_block = 11;
    (void)store.add_entry(gps_lon);

    Entry gps_date;
    gps_date.key          = make_exif_tag_key(store.arena(), "gpsifd", 0x001D);
    gps_date.value        = make_text(store.arena(), "2024:04:19",
                                      TextEncoding::Ascii);
    gps_date.origin.block = block;
    gps_date.origin.order_in_block = 12;
    (void)store.add_entry(gps_date);

    const std::array<URational, 3> gps_time_triplet = {
        URational { 12, 1 },
        URational { 11, 1 },
        URational { 13, 1 },
    };
    Entry gps_time;
    gps_time.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0007);
    gps_time.value = make_urational_array(
        store.arena(), std::span<const URational>(gps_time_triplet.data(),
                                                  gps_time_triplet.size()));
    gps_time.origin.block          = block;
    gps_time.origin.order_in_block = 13;
    (void)store.add_entry(gps_time);

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
    EXPECT_NE(s.find("<exif:GPSLatitude>41,24.5N</exif:GPSLatitude>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSLongitude>2,9E</exif:GPSLongitude>"),
              std::string_view::npos);
    EXPECT_NE(
        s.find("<exif:GPSTimeStamp>2024-04-19T12:11:13Z</exif:GPSTimeStamp>"),
        std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>24</rdf:li>"), std::string_view::npos);
    EXPECT_NE(s.find("<rdf:li>70</rdf:li>"), std::string_view::npos);
}

TEST(XmpDump, PortableSkipsInvalidGpsRationalValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry gps_lat_ref;
    gps_lat_ref.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0001);
    gps_lat_ref.value = make_text(store.arena(), "N", TextEncoding::Ascii);
    gps_lat_ref.origin.block          = block;
    gps_lat_ref.origin.order_in_block = 0;
    (void)store.add_entry(gps_lat_ref);

    const std::array<URational, 3> invalid_triplet = {
        URational { 0, 0 },
        URational { 0, 0 },
        URational { 0, 0 },
    };
    Entry gps_lat;
    gps_lat.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0002);
    gps_lat.value = make_urational_array(
        store.arena(), std::span<const URational>(invalid_triplet.data(),
                                                  invalid_triplet.size()));
    gps_lat.origin.block          = block;
    gps_lat.origin.order_in_block = 1;
    (void)store.add_entry(gps_lat);

    Entry gps_time;
    gps_time.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0007);
    gps_time.value = make_urational_array(
        store.arena(), std::span<const URational>(invalid_triplet.data(),
                                                  invalid_triplet.size()));
    gps_time.origin.block          = block;
    gps_time.origin.order_in_block = 2;
    (void)store.add_entry(gps_time);

    Entry gps_alt;
    gps_alt.key          = make_exif_tag_key(store.arena(), "gpsifd", 0x0006);
    gps_alt.value        = make_urational(0, 0);
    gps_alt.origin.block = block;
    gps_alt.origin.order_in_block = 3;
    (void)store.add_entry(gps_alt);

    Entry gps_img_dir;
    gps_img_dir.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0011);
    gps_img_dir.value = make_urational(42889, 241);
    gps_img_dir.origin.block          = block;
    gps_img_dir.origin.order_in_block = 4;
    (void)store.add_entry(gps_img_dir);

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
    EXPECT_NE(s.find("<exif:GPSLatitudeRef>N</exif:GPSLatitudeRef>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:GPSImgDirection>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:GPSLatitude>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:GPSTimeStamp>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:GPSAltitude>"), std::string_view::npos);
}


TEST(XmpDump, PortableSkipsInvalidApexRationalValues)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry fnumber;
    fnumber.key          = make_exif_tag_key(store.arena(), "exififd", 0x829D);
    fnumber.value        = make_urational(0, 0);
    fnumber.origin.block = block;
    fnumber.origin.order_in_block = 0;
    (void)store.add_entry(fnumber);

    Entry aperture;
    aperture.key          = make_exif_tag_key(store.arena(), "exififd", 0x9202);
    aperture.value        = make_urational(0, 0);
    aperture.origin.block = block;
    aperture.origin.order_in_block = 1;
    (void)store.add_entry(aperture);

    Entry shutter;
    shutter.key          = make_exif_tag_key(store.arena(), "exififd", 0x9201);
    shutter.value        = make_srational(0, 0);
    shutter.origin.block = block;
    shutter.origin.order_in_block = 2;
    (void)store.add_entry(shutter);

    Entry exposure_comp;
    exposure_comp.key   = make_exif_tag_key(store.arena(), "exififd", 0x9204);
    exposure_comp.value = make_srational(0, 0);
    exposure_comp.origin.block          = block;
    exposure_comp.origin.order_in_block = 3;
    (void)store.add_entry(exposure_comp);

    Entry focal_length;
    focal_length.key   = make_exif_tag_key(store.arena(), "exififd", 0x920A);
    focal_length.value = make_urational(66, 1);
    focal_length.origin.block          = block;
    focal_length.origin.order_in_block = 4;
    (void)store.add_entry(focal_length);

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
    EXPECT_EQ(s.find("<exif:FNumber>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ApertureValue>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ShutterSpeedValue>"), std::string_view::npos);
    EXPECT_EQ(s.find("<exif:ExposureCompensation>"), std::string_view::npos);
    EXPECT_NE(s.find("<exif:FocalLength>66.0 mm</exif:FocalLength>"),
              std::string_view::npos);
}


TEST(XmpDump, PortableApexTagsUseFirstValidArrayElement)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::array<URational, 2> fnumber_vals = {
        URational { 139, 50 },
        URational { 0, 0 },
    };
    Entry fnumber;
    fnumber.key = make_exif_tag_key(store.arena(), "exififd", 0x829D);
    fnumber.value
        = make_urational_array(store.arena(),
                               std::span<const URational>(fnumber_vals.data(),
                                                          fnumber_vals.size()));
    fnumber.origin.block          = block;
    fnumber.origin.order_in_block = 0;
    (void)store.add_entry(fnumber);

    const std::array<URational, 2> aperture_vals = {
        URational { 0, 0 }, URational { 4, 1 },  // APEX 4 -> f/4.0
    };
    Entry aperture;
    aperture.key   = make_exif_tag_key(store.arena(), "exififd", 0x9202);
    aperture.value = make_urational_array(
        store.arena(),
        std::span<const URational>(aperture_vals.data(), aperture_vals.size()));
    aperture.origin.block          = block;
    aperture.origin.order_in_block = 1;
    (void)store.add_entry(aperture);

    const std::array<SRational, 2> shutter_vals = {
        SRational { 0, 0 }, SRational { 6, 1 },  // APEX 6 -> 1/64 s
    };
    Entry shutter;
    shutter.key = make_exif_tag_key(store.arena(), "exififd", 0x9201);
    shutter.value
        = make_srational_array(store.arena(),
                               std::span<const SRational>(shutter_vals.data(),
                                                          shutter_vals.size()));
    shutter.origin.block          = block;
    shutter.origin.order_in_block = 2;
    (void)store.add_entry(shutter);

    const std::array<SRational, 2> exp_comp_vals = {
        SRational { 0, 0 },
        SRational { 1, 2 },
    };
    Entry exp_comp;
    exp_comp.key   = make_exif_tag_key(store.arena(), "exififd", 0x9204);
    exp_comp.value = make_srational_array(
        store.arena(),
        std::span<const SRational>(exp_comp_vals.data(), exp_comp_vals.size()));
    exp_comp.origin.block          = block;
    exp_comp.origin.order_in_block = 3;
    (void)store.add_entry(exp_comp);

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
    EXPECT_NE(s.find("<exif:FNumber>2.8</exif:FNumber>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ApertureValue>4.0</exif:ApertureValue>"),
              std::string_view::npos);
    EXPECT_NE(s.find("<exif:ShutterSpeedValue>1/64</exif:ShutterSpeedValue>"),
              std::string_view::npos);
    EXPECT_NE(s.find(
                  "<exif:ExposureCompensation>0.5</exif:ExposureCompensation>"),
              std::string_view::npos);
}

TEST(XmpDump, PortableSkipsGpsTimeStampWithoutGpsDateStamp)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    const std::array<URational, 3> gps_time_triplet = {
        URational { 12, 1 },
        URational { 11, 1 },
        URational { 13, 1 },
    };
    Entry gps_time;
    gps_time.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0007);
    gps_time.value = make_urational_array(
        store.arena(), std::span<const URational>(gps_time_triplet.data(),
                                                  gps_time_triplet.size()));
    gps_time.origin.block          = block;
    gps_time.origin.order_in_block = 0;
    (void)store.add_entry(gps_time);

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
    EXPECT_EQ(s.find("<exif:GPSTimeStamp>"), std::string_view::npos);
}

TEST(XmpDump, PortableGpsTimeStampSupportsExifToolAlias)
{
    MetaStore store;
    const BlockId block = store.add_block(BlockInfo {});
    ASSERT_NE(block, kInvalidBlockId);

    Entry gps_date;
    gps_date.key          = make_exif_tag_key(store.arena(), "gpsifd", 0x001D);
    gps_date.value        = make_text(store.arena(), "2024:04:19",
                                      TextEncoding::Ascii);
    gps_date.origin.block = block;
    gps_date.origin.order_in_block = 0;
    (void)store.add_entry(gps_date);

    const std::array<URational, 3> gps_time_triplet = {
        URational { 12, 1 },
        URational { 11, 1 },
        URational { 13, 1 },
    };
    Entry gps_time;
    gps_time.key   = make_exif_tag_key(store.arena(), "gpsifd", 0x0007);
    gps_time.value = make_urational_array(
        store.arena(), std::span<const URational>(gps_time_triplet.data(),
                                                  gps_time_triplet.size()));
    gps_time.origin.block          = block;
    gps_time.origin.order_in_block = 1;
    (void)store.add_entry(gps_time);

    store.finalize();

    XmpPortableOptions opts;
    opts.include_exif               = true;
    opts.include_existing_xmp       = false;
    opts.exiftool_gpsdatetime_alias = true;

    std::vector<std::byte> out(4096);
    const XmpDumpResult r
        = dump_xmp_portable(store, std::span<std::byte>(out.data(), out.size()),
                            opts);
    ASSERT_EQ(r.status, XmpDumpStatus::Ok);

    const std::string_view s(reinterpret_cast<const char*>(out.data()),
                             static_cast<size_t>(r.written));
    EXPECT_NE(s.find(
                  "<exif:GPSDateTime>2024-04-19T12:11:13Z</exif:GPSDateTime>"),
              std::string_view::npos);
    EXPECT_EQ(s.find("<exif:GPSTimeStamp>"), std::string_view::npos);
}

}  // namespace openmeta
