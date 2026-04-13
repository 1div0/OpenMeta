// SPDX-License-Identifier: Apache-2.0

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

TEST(XmpDecodeTest, DecodesAltTextEntriesWithXmlLangPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description xmlns:dc='http://purl.org/dc/elements/1.1/'>"
          "<dc:title><rdf:Alt>"
          "<rdf:li xml:lang='x-default'>Default title</rdf:li>"
          "<rdf:li xml:lang='fr-FR'>Titre</rdf:li>"
          "</rdf:Alt></dc:title>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(
        reinterpret_cast<const std::byte*>(xmp.data()), xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 2U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns     = "http://purl.org/dc/elements/1.1/";
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

    expect_text("title[@xml:lang=x-default]", "Default title");
    expect_text("title[@xml:lang=fr-FR]", "Titre");
}

TEST(XmpDecodeTest, DecodesStructuredResourcePaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
          "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
          "<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"
          "<Iptc4xmpCore:CiUrlWork>https://example.test/contact</Iptc4xmpCore:CiUrlWork>"
          "</Iptc4xmpCore:CreatorContactInfo>"
          "<Iptc4xmpCore:LocationCreated rdf:parseType='Resource'>"
          "<Iptc4xmpCore:City>Paris</Iptc4xmpCore:City>"
          "<Iptc4xmpCore:CountryName>France</Iptc4xmpCore:CountryName>"
          "</Iptc4xmpCore:LocationCreated>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(
        reinterpret_cast<const std::byte*>(xmp.data()), xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 4U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
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

    expect_text("CreatorContactInfo/CiEmailWork", "editor@example.test");
    expect_text("CreatorContactInfo/CiUrlWork",
                "https://example.test/contact");
    expect_text("LocationCreated/City", "Paris");
    expect_text("LocationCreated/CountryName", "France");
}

TEST(XmpDecodeTest, DecodesIndexedStructuredResourcePaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:plus='http://ns.useplus.org/ldf/xmp/1.0/'>"
          "<plus:Licensee><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<plus:LicenseeName>Example Archive</plus:LicenseeName>"
          "<plus:LicenseeURL>https://example.test/archive</plus:LicenseeURL>"
          "</rdf:li>"
          "<rdf:li rdf:parseType='Resource'>"
          "<plus:LicenseeName>Editorial Partner</plus:LicenseeName>"
          "<plus:LicenseeID>lic-002</plus:LicenseeID>"
          "</rdf:li>"
          "</rdf:Seq></plus:Licensee>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(
        reinterpret_cast<const std::byte*>(xmp.data()), xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 4U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://ns.useplus.org/ldf/xmp/1.0/";
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

    expect_text("Licensee[1]/LicenseeName", "Example Archive");
    expect_text("Licensee[1]/LicenseeURL", "https://example.test/archive");
    expect_text("Licensee[2]/LicenseeName", "Editorial Partner");
    expect_text("Licensee[2]/LicenseeID", "lic-002");
}

TEST(XmpDecodeTest, DecodesIptc4xmpExtIndexedStructuredPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
          "<Iptc4xmpExt:LocationShown><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<Iptc4xmpExt:City>Paris</Iptc4xmpExt:City>"
          "<Iptc4xmpExt:CountryName>France</Iptc4xmpExt:CountryName>"
          "</rdf:li>"
          "<rdf:li rdf:parseType='Resource'>"
          "<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"
          "<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"
          "</rdf:li>"
          "</rdf:Seq></Iptc4xmpExt:LocationShown>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(
        reinterpret_cast<const std::byte*>(xmp.data()), xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 4U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
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

    expect_text("LocationShown[1]/City", "Paris");
    expect_text("LocationShown[1]/CountryName", "France");
    expect_text("LocationShown[2]/City", "Kyoto");
    expect_text("LocationShown[2]/CountryName", "Japan");
}

TEST(XmpDecodeTest, DecodesStructuredChildLangAltPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpCore='http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/'>"
          "<Iptc4xmpCore:CreatorContactInfo rdf:parseType='Resource'>"
          "<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"
          "<Iptc4xmpCore:CiAdrCity><rdf:Alt>"
          "<rdf:li xml:lang='x-default'>Tokyo</rdf:li>"
          "<rdf:li xml:lang='ja-JP'>東京</rdf:li>"
          "</rdf:Alt></Iptc4xmpCore:CiAdrCity>"
          "</Iptc4xmpCore:CreatorContactInfo>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(
        reinterpret_cast<const std::byte*>(xmp.data()), xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 3U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/";
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

    expect_text("CreatorContactInfo/CiEmailWork", "editor@example.test");
    expect_text("CreatorContactInfo/CiAdrCity[@xml:lang=x-default]", "Tokyo");
    expect_text("CreatorContactInfo/CiAdrCity[@xml:lang=ja-JP]", "東京");
}

TEST(XmpDecodeTest, DecodesIndexedStructuredChildLangAltPaths)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:Iptc4xmpExt='http://iptc.org/std/Iptc4xmpExt/2008-02-29/'>"
          "<Iptc4xmpExt:LocationShown><rdf:Seq>"
          "<rdf:li rdf:parseType='Resource'>"
          "<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"
          "<Iptc4xmpExt:Sublocation><rdf:Alt>"
          "<rdf:li xml:lang='x-default'>Gion</rdf:li>"
          "<rdf:li xml:lang='ja-JP'>祇園</rdf:li>"
          "</rdf:Alt></Iptc4xmpExt:Sublocation>"
          "</rdf:li>"
          "</rdf:Seq></Iptc4xmpExt:LocationShown>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(
        reinterpret_cast<const std::byte*>(xmp.data()), xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 3U);

    store.finalize();

    auto expect_text = [&](std::string_view path, std::string_view expected) {
        MetaKeyView key;
        key.kind                            = MetaKeyKind::XmpProperty;
        key.data.xmp_property.schema_ns
            = "http://iptc.org/std/Iptc4xmpExt/2008-02-29/";
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

    expect_text("LocationShown[1]/CountryName", "Japan");
    expect_text("LocationShown[1]/Sublocation[@xml:lang=x-default]", "Gion");
    expect_text("LocationShown[1]/Sublocation[@xml:lang=ja-JP]", "祇園");
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

TEST(XmpDecodeTest, EstimateMatchesDecodeCounters)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'>"
          "<xmp:Rating>5</xmp:Rating>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    const XmpDecodeResult estimate = measure_xmp_packet(bytes);
    EXPECT_EQ(estimate.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(estimate.entries_decoded, 2U);

    MetaStore store;
    const XmpDecodeResult decoded = decode_xmp_packet(bytes, store);
    EXPECT_EQ(decoded.status, estimate.status);
    EXPECT_EQ(decoded.entries_decoded, estimate.entries_decoded);
}

TEST(XmpDecodeTest, EstimateRespectsPropertyLimitOverride)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'>"
          "<xmp:Rating>5</xmp:Rating>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    XmpDecodeOptions options;
    options.limits.max_properties  = 1U;
    const XmpDecodeResult estimate = measure_xmp_packet(bytes, options);
    EXPECT_EQ(estimate.status, XmpDecodeStatus::LimitExceeded);

    MetaStore store;
    const XmpDecodeResult decoded
        = decode_xmp_packet(bytes, store, EntryFlags::None, options);
    EXPECT_EQ(decoded.status, XmpDecodeStatus::LimitExceeded);
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
    artist_key.kind                        = MetaKeyKind::XmpProperty;
    artist_key.data.xmp_property.schema_ns = "http://ns.adobe.com/tiff/1.0/";
    artist_key.data.xmp_property.property_path = "Artist";

    MetaKeyView copy_key;
    copy_key.kind                            = MetaKeyKind::XmpProperty;
    copy_key.data.xmp_property.schema_ns     = "http://ns.adobe.com/tiff/1.0/";
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

TEST(XmpDecodeTest, DecodesXmpToolkitOnXmpMetaRoot)
{
    const std::string xmp
        = "<?xpacket begin='' id='W5M0MpCehiHzreSzNTczkc9d' ?>"
          "<x:xmpmeta xmlns:x='adobe:ns:meta/' "
          "x:xmptk='Adobe XMP Core 5.2-c004 1.136881, 2010/06/10-18:11:35'>"
          "</x:xmpmeta>"
          "<?xpacket end='w'?>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store;
    const XmpDecodeResult r = decode_xmp_packet(bytes, store);
    EXPECT_EQ(r.status, XmpDecodeStatus::Ok);
    EXPECT_EQ(r.entries_decoded, 1U);

    store.finalize();

    MetaKeyView key;
    key.kind                            = MetaKeyKind::XmpProperty;
    key.data.xmp_property.schema_ns     = "adobe:ns:meta/";
    key.data.xmp_property.property_path = "XMPToolkit";

    const std::span<const EntryId> ids = store.find_all(key);
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    ASSERT_EQ(e.value.kind, MetaValueKind::Text);
}

TEST(XmpDecodeTest, RejectsDoctypeAndEntityDeclarations)
{
    const std::string xmp
        = "<!DOCTYPE x:xmpmeta [<!ENTITY boom 'boom'>]>"
          "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/'>"
          "<xmp:CreatorTool>&boom;</xmp:CreatorTool>"
          "</rdf:Description>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore strict_store;
    const XmpDecodeResult strict = decode_xmp_packet(bytes, strict_store);
    EXPECT_EQ(strict.status, XmpDecodeStatus::Malformed);
    EXPECT_EQ(strict.entries_decoded, 0U);

    MetaStore safe_store;
    XmpDecodeOptions options;
    options.malformed_mode     = XmpDecodeMalformedMode::OutputTruncated;
    const XmpDecodeResult safe = decode_xmp_packet(bytes, safe_store,
                                                   EntryFlags::None, options);
    EXPECT_EQ(safe.status, XmpDecodeStatus::OutputTruncated);
    EXPECT_EQ(safe.entries_decoded, 0U);
}

TEST(XmpDecodeTest, MalformedCanBeReportedAsOutputTruncated)
{
    std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'>"
          "<xmp:Rating>5";
    xmp.push_back(static_cast<char>(0x01));  // invalid XML control byte
    xmp += "</xmp:Rating>"
           "</rdf:Description>"
           "</rdf:RDF>"
           "</x:xmpmeta>";

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               xmp.data()),
                                           xmp.size());

    MetaStore store_a;
    const XmpDecodeResult strict = decode_xmp_packet(bytes, store_a);
    EXPECT_EQ(strict.status, XmpDecodeStatus::Malformed);
    EXPECT_GE(strict.entries_decoded, 1U);

    MetaStore store_b;
    XmpDecodeOptions options;
    options.malformed_mode     = XmpDecodeMalformedMode::OutputTruncated;
    const XmpDecodeResult safe = decode_xmp_packet(bytes, store_b,
                                                   EntryFlags::None, options);
    EXPECT_EQ(safe.status, XmpDecodeStatus::OutputTruncated);
    EXPECT_EQ(safe.entries_decoded, strict.entries_decoded);
}

#else

TEST(XmpDecodeTest, ExpatNotEnabled)
{
    GTEST_SKIP()
        << "OPENMETA_HAS_EXPAT is not enabled; XMP decode is unavailable.";
}

#endif

}  // namespace openmeta
