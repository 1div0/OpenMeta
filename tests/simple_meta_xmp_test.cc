#include "openmeta/simple_meta.h"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace openmeta {

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT

TEST(SimpleMetaRead, DecodesStandaloneXmpPacket)
{
    const std::string xmp
        = "<x:xmpmeta xmlns:x='adobe:ns:meta/'>"
          "<rdf:RDF xmlns:rdf='http://www.w3.org/1999/02/22-rdf-syntax-ns#'>"
          "<rdf:Description "
          "xmlns:xmp='http://ns.adobe.com/xap/1.0/' "
          "xmp:CreatorTool='OpenMeta'/>"
          "</rdf:RDF>"
          "</x:xmpmeta>";

    const std::span<const std::byte> file_bytes(
        reinterpret_cast<const std::byte*>(xmp.data()), xmp.size());

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 16> ifds {};
    std::vector<std::byte> payload(1024);
    std::vector<uint32_t> payload_parts(64);

    ExifDecodeOptions exif_options;
    PayloadOptions payload_options;
    const SimpleMetaResult read
        = simple_meta_read(file_bytes, store, blocks, ifds, payload,
                           payload_parts, exif_options, payload_options);

    EXPECT_EQ(read.xmp.status, XmpDecodeStatus::Ok);
    EXPECT_GT(read.xmp.entries_decoded, 0U);

    store.finalize();
    uint32_t xmp_props = 0;
    for (const Entry& e : store.entries()) {
        if (e.key.kind == MetaKeyKind::XmpProperty
            && !any(e.flags, EntryFlags::Deleted)) {
            xmp_props += 1;
        }
    }
    EXPECT_EQ(xmp_props, read.xmp.entries_decoded);
}

#else

TEST(SimpleMetaRead, ExpatNotEnabled)
{
    GTEST_SKIP()
        << "OPENMETA_HAS_EXPAT is not enabled; standalone XMP decode is unavailable.";
}

#endif

}  // namespace openmeta
