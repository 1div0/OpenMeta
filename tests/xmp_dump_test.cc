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

}  // namespace openmeta
