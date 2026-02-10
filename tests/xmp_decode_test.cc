#include "openmeta/xmp_decode.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace openmeta {

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT

TEST(XmpDecodeTest, DecodesAttributesArraysAndRdfResource)
{
    const std::string xmp
        = "<?xpacket begin='\\xEF\\xBB\\xBF' id='W5M0MpCehiHzreSzNTczkc9d'?>"
          "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:dc='http://purl.org/dc/elements/1.1/' "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmlns:xmpMM='http://ns.adobe.com/xap/1.0/mm/' "
          "xmp:CreatorTool='OpenMeta'>"
          "<dc:creator><rdf:Seq>"
          "<rdf:li>John</rdf:li><rdf:li>Jane</rdf:li>"
          "</rdf:Seq></dc:creator>"
          "<xmp:Rating> 5 </xmp:Rating>"
          "<xmpMM:InstanceID rdf:resource='uuid:123'/>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>"
          "<?xpacket end='w'?>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 5U);

    store.finalize();

    auto expect_text = [&](std::string_view schema_ns, std::string_view path,
                           std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = schema_ns;
        key.data.xmp_property.property_path = path;

        const std::span<const EntryId> ids = store.find_all(key);
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        const std::span<const std::byte> vb = store.arena().span(
            e.value.data.span);
        const std::string_view val(reinterpret_cast<const char*>(vb.data()),
                                   vb.size());
        EXPECT_EQ(val, expected);
    };

    expect_text("http://ns.adobe.com/xap/1.0/", "CreatorTool", "OpenMeta");
    expect_text("http://purl.org/dc/elements/1.1/", "creator[1]", "John");
    expect_text("http://purl.org/dc/elements/1.1/", "creator[2]", "Jane");
    expect_text("http://ns.adobe.com/xap/1.0/", "Rating", "5");
    expect_text("http://ns.adobe.com/xap/1.0/mm/", "InstanceID", "uuid:123");
}

TEST(XmpDecodeTest, TrimsTrailingNulPadding)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'/>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    std::string padded = xmp;
    padded.append(16, '\0');

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               padded.data()),
                                           padded.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 1U);
}

TEST(XmpDecodeTest, PreservesExplicitEmptyLeafValues)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:tiff='http://ns.adobe.com/tiff/1.0/'>"
          "<tiff:Artist/>"
          "<tiff:Copyright>   </tiff:Copyright>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    MetaKeyView artist_key;
    artist_key.kind = MetaKeyKind::XmpProperty;
    artist_key.data.xmp_property.schema_ns
        = "http://ns.adobe.com/tiff/1.0/";
    artist_key.data.xmp_property.property_path = "Artist";

    MetaKeyView copy_key;
    copy_key.kind = MetaKeyKind::XmpProperty;
    copy_key.data.xmp_property.schema_ns
        = "http://ns.adobe.com/tiff/1.0/";
    copy_key.data.xmp_property.property_path = "Copyright";

    const std::span<const EntryId> artist_ids = store.find_all(artist_key);
    const std::span<const EntryId> copy_ids   = store.find_all(copy_key);
    ASSERT_EQ(artist_ids.size(), 1U);
    ASSERT_EQ(copy_ids.size(), 1U);

    const Entry& artist = store.entry(artist_ids[0]);
    const Entry& copy   = store.entry(copy_ids[0]);
    ASSERT_EQ(artist.value.kind, MetaValueKind::Text);
    ASSERT_EQ(copy.value.kind, MetaValueKind::Text);

    const std::span<const std::byte> artist_val = store.arena().span(
        artist.value.data.span);
    const std::span<const std::byte> copy_val = store.arena().span(
        copy.value.data.span);
    EXPECT_TRUE(artist_val.empty());
    EXPECT_TRUE(copy_val.empty());
}

TEST(XmpDecodeTest, SkipsLeadingMimePrefix)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'/>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    std::string blob = "application/rdf+xml";
    blob.push_back('\0');
    blob.append(xmp);
    blob.append(8, '\0');

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               blob.data()),
                                           blob.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 1U);
}

#else

TEST(XmpDecodeTest, ExpatNotEnabled)
{
    GTEST_SKIP()
        << "OPENMETA_HAS_EXPAT is not enabled; XMP decode is unavailable.";
}

#endif

}  // namespace openmeta
