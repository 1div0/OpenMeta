#include "openmeta/simple_meta.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static void append_bytes(std::vector<std::byte>* out, std::string_view s)
    {
        out->insert(out->end(), reinterpret_cast<const std::byte*>(s.data()),
                    reinterpret_cast<const std::byte*>(s.data() + s.size()));
    }


    static void append_u16le(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
    }


    static void append_u32le(std::vector<std::byte>* out, uint32_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) });
    }


    static std::string_view arena_string(const ByteArena& arena,
                                         const MetaValue& v)
    {
        const std::span<const std::byte> bytes = arena.span(v.data.span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }


    static MetaKeyView exif_key(std::string_view ifd, uint16_t tag)
    {
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = ifd;
        key.data.exif_tag.tag = tag;
        return key;
    }


    static std::vector<std::byte> make_minimal_crw_ciff()
    {
        // Minimal CRW (CIFF) container:
        //   0: "II" (LE)
        //   2: u32 root_dir_offset
        //   6: "HEAPCCDR"
        // Root directory contains 1 entry (directoryData) with inline asciiString.
        std::vector<std::byte> file;
        append_bytes(&file, "II");
        append_u32le(&file, 14);  // root dir begins immediately after header.
        append_bytes(&file, "HEAPCCDR");
        EXPECT_EQ(file.size(), 14U);

        // Directory bytes (16 bytes total):
        //   u16 entry_count=1
        //   entry[0]: u16 tag=0x4801 (loc=0x4000 directoryData, type=0x0800 asciiString, id=1)
        //             8 bytes value: "CIFFTEST"
        //   u32 entry_table_offset=0
        append_u16le(&file, 1);
        append_u16le(&file, 0x4801);
        append_bytes(&file, "CIFFTEST");
        append_u32le(&file, 0);
        return file;
    }

}  // namespace

TEST(CrwCiffDecode, DecodesMinimalDirectory)
{
    const std::vector<std::byte> file = make_minimal_crw_ciff();

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> payload {};
    std::array<uint32_t, 32> payload_scratch {};

    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    const SimpleMetaResult res
        = simple_meta_read(file, store, blocks, ifds, payload, payload_scratch,
                           exif_opts, payload_opts);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("ciff_root", 0x0801));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Text);
    EXPECT_EQ(e.value.text_encoding, TextEncoding::Ascii);
    EXPECT_EQ(arena_string(store.arena(), e.value), "CIFFTEST");
}

}  // namespace openmeta
