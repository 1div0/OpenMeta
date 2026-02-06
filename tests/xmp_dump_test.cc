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
    e.key                   = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    e.value                 = make_text(store.arena(), "Canon",
                                        TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    e.origin.wire_type      = WireType { WireFamily::Tiff, 2 /*ASCII*/ };
    e.origin.wire_count     = 5;
    (void)store.add_entry(e);

    store.finalize();

    XmpDumpOptions opts;
    std::vector<std::byte> out(64);

    XmpDumpResult r1 = dump_xmp_lossless(
        store, std::span<std::byte>(out.data(), out.size()), opts);
    ASSERT_EQ(r1.status, XmpDumpStatus::OutputTruncated);
    ASSERT_GT(r1.needed, out.size());

    out.resize(static_cast<size_t>(r1.needed));
    const XmpDumpResult r2 = dump_xmp_lossless(
        store, std::span<std::byte>(out.data(), out.size()), opts);
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
    make.key                   = make_exif_tag_key(store.arena(), "ifd0", 0x010F);
    make.value                 = make_text(store.arena(), "Canon",
                                           TextEncoding::Ascii);
    make.origin.block          = block;
    make.origin.order_in_block = 0;
    (void)store.add_entry(make);

    Entry exp;
    exp.key                   = make_exif_tag_key(store.arena(), "exififd", 0x829A);
    exp.value                 = make_urational(1, 1250);
    exp.origin.block          = block;
    exp.origin.order_in_block = 1;
    (void)store.add_entry(exp);

    store.finalize();

    XmpPortableOptions opts;
    std::vector<std::byte> out(64);

    XmpDumpResult r1 = dump_xmp_portable(
        store, std::span<std::byte>(out.data(), out.size()), opts);
    ASSERT_EQ(r1.status, XmpDumpStatus::OutputTruncated);
    ASSERT_GT(r1.needed, out.size());

    out.resize(static_cast<size_t>(r1.needed));
    const XmpDumpResult r2 = dump_xmp_portable(
        store, std::span<std::byte>(out.data(), out.size()), opts);
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

}  // namespace openmeta
