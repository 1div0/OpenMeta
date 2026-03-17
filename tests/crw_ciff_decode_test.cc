#include "openmeta/simple_meta.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
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


    static uint32_t f32_bits(float value)
    {
        uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }


    static std::vector<std::byte> make_padded_ascii(std::string_view text,
                                                    size_t width)
    {
        std::vector<std::byte> out(width, std::byte { 0 });
        const size_t n = (text.size() < width) ? text.size() : width;
        std::memcpy(out.data(), text.data(), n);
        return out;
    }


    static std::vector<std::byte> make_padded_u16_scalar(uint16_t value)
    {
        std::vector<std::byte> out(8U, std::byte { 0 });
        out[0] = std::byte { static_cast<uint8_t>((value >> 0) & 0xFFU) };
        out[1] = std::byte { static_cast<uint8_t>((value >> 8) & 0xFFU) };
        return out;
    }


    static std::vector<std::byte> make_padded_u32_scalar(uint32_t value)
    {
        std::vector<std::byte> out(8U, std::byte { 0 });
        out[0] = std::byte { static_cast<uint8_t>((value >> 0) & 0xFFU) };
        out[1] = std::byte { static_cast<uint8_t>((value >> 8) & 0xFFU) };
        out[2] = std::byte { static_cast<uint8_t>((value >> 16) & 0xFFU) };
        out[3] = std::byte { static_cast<uint8_t>((value >> 24) & 0xFFU) };
        return out;
    }


    static std::vector<std::byte> make_padded_f32_scalar(float value)
    {
        return make_padded_u32_scalar(f32_bits(value));
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


    struct CiffValueEntry final {
        uint16_t tag = 0;
        std::vector<std::byte> value;
    };


    static std::vector<std::byte>
    make_ciff_directory(const std::vector<CiffValueEntry>& entries)
    {
        std::vector<std::byte> out;
        append_u16le(&out, static_cast<uint16_t>(entries.size()));

        const uint32_t table_bytes
            = 2U + static_cast<uint32_t>(entries.size()) * 10U;
        uint32_t data_off = table_bytes;

        for (size_t i = 0; i < entries.size(); ++i) {
            append_u16le(&out, entries[i].tag);
            append_u32le(&out, static_cast<uint32_t>(entries[i].value.size()));
            append_u32le(&out, data_off);
            data_off += static_cast<uint32_t>(entries[i].value.size());
        }

        for (size_t i = 0; i < entries.size(); ++i) {
            out.insert(out.end(), entries[i].value.begin(),
                       entries[i].value.end());
        }

        append_u32le(&out, 0U);
        return out;
    }


    static std::vector<std::byte>
    make_ciff_inline_directory(const std::vector<CiffValueEntry>& entries)
    {
        std::vector<std::byte> out;
        append_u16le(&out, static_cast<uint16_t>(entries.size()));

        for (size_t i = 0; i < entries.size(); ++i) {
            append_u16le(&out, entries[i].tag);
            std::vector<std::byte> value = entries[i].value;
            value.resize(8U, std::byte { 0 });
            out.insert(out.end(), value.begin(), value.begin() + 8U);
        }

        append_u32le(&out, 0U);
        return out;
    }


    static std::vector<std::byte> make_crw_with_derived_exif_sources()
    {
        std::vector<std::byte> make_model;
        append_bytes(&make_model, "Canon");
        make_model.push_back(std::byte { 0 });
        append_bytes(&make_model, "PowerShot Pro70");
        make_model.push_back(std::byte { 0 });

        std::vector<std::byte> subject_distance;
        append_u32le(&subject_distance, 123U);

        std::vector<std::byte> exposure_info;
        append_u32le(&exposure_info, f32_bits(0.33333334f));
        append_u32le(&exposure_info, f32_bits(6.875f));
        append_u32le(&exposure_info, f32_bits(3.0f));

        std::vector<std::byte> datetime_original;
        append_u32le(&datetime_original, 1700000000U);
        append_u32le(&datetime_original, 0xFFFFFFFDU);
        append_u32le(&datetime_original, 0x0000007BU);

        std::vector<std::byte> dimensions_orientation;
        append_u32le(&dimensions_orientation, 1536U);  // PixelXDimension
        append_u32le(&dimensions_orientation, 1024U);  // PixelYDimension
        append_u32le(&dimensions_orientation, f32_bits(1.0f));
        append_u32le(&dimensions_orientation, 90U);  // rotation -> orient=6

        const std::vector<std::byte> dir2807 = make_ciff_directory(
            std::vector<CiffValueEntry> { { 0x080AU, make_model } });
        const std::vector<std::byte> dir3002 = make_ciff_directory(
            std::vector<CiffValueEntry> {
                { 0x1807U, subject_distance },
                { 0x1818U, exposure_info },
            });
        const std::vector<std::byte> dir300a = make_ciff_directory(
            std::vector<CiffValueEntry> { { 0x180EU, datetime_original },
                                          { 0x1810U, dimensions_orientation } });

        const std::vector<std::byte> root = make_ciff_directory(
            std::vector<CiffValueEntry> {
                { 0x2807U, dir2807 },
                { 0x3002U, dir3002 },
                { 0x300AU, dir300a },
            });

        std::vector<std::byte> file;
        append_bytes(&file, "II");
        append_u32le(&file, 14U);
        append_bytes(&file, "HEAPCCDR");
        file.insert(file.end(), root.begin(), root.end());
        return file;
    }


    static std::vector<std::byte> make_crw_with_textual_ciff_fields()
    {
        const std::vector<std::byte> dir2804 = make_ciff_directory(
            std::vector<CiffValueEntry> {
                { 0x0805U, make_padded_ascii("High definition camera", 32U) },
            });
        const std::vector<std::byte> dir2807 = make_ciff_directory(
            std::vector<CiffValueEntry> {
                { 0x0810U, make_padded_ascii("Alice", 32U) },
            });
        const std::vector<std::byte> dir300a = make_ciff_directory(
            std::vector<CiffValueEntry> {
                { 0x0816U, make_padded_ascii("IMG_0001.CRW", 32U) },
            });

        const std::vector<std::byte> root = make_ciff_directory(
            std::vector<CiffValueEntry> {
                { 0x2804U, dir2804 },
                { 0x2807U, dir2807 },
                { 0x300AU, dir300a },
            });

        std::vector<std::byte> file;
        append_bytes(&file, "II");
        append_u32le(&file, 14U);
        append_bytes(&file, "HEAPCCDR");
        file.insert(file.end(), root.begin(), root.end());
        return file;
    }


    static std::vector<std::byte> make_crw_with_semantic_native_scalars()
    {
        const std::vector<std::byte> dir3002 = make_ciff_inline_directory(
            std::vector<CiffValueEntry> {
                { 0x5010U, make_padded_u16_scalar(2U) },
                { 0x5011U, make_padded_u16_scalar(1U) },
                { 0x5807U, make_padded_f32_scalar(12.5f) },
            });
        const std::vector<std::byte> dir3003 = make_ciff_inline_directory(
            std::vector<CiffValueEntry> {
                { 0x5814U, make_padded_f32_scalar(9.5f) },
            });
        const std::vector<std::byte> dir3004 = make_ciff_inline_directory(
            std::vector<CiffValueEntry> {
                { 0x501CU, make_padded_u16_scalar(100U) },
            });
        const std::vector<std::byte> dir300a = make_ciff_inline_directory(
            std::vector<CiffValueEntry> {
                { 0x500AU, make_padded_u16_scalar(7U) },
                { 0x5804U, make_padded_u32_scalar(42U) },
                { 0x5817U, make_padded_u32_scalar(162U) },
            });

        const std::vector<std::byte> root = make_ciff_directory(
            std::vector<CiffValueEntry> {
                { 0x3002U, dir3002 },
                { 0x3003U, dir3003 },
                { 0x3004U, dir3004 },
                { 0x300AU, dir300a },
            });

        std::vector<std::byte> file;
        append_bytes(&file, "II");
        append_u32le(&file, 14U);
        append_bytes(&file, "HEAPCCDR");
        file.insert(file.end(), root.begin(), root.end());
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

    const SimpleMetaResult res = simple_meta_read(file, store, blocks, ifds,
                                                  payload, payload_scratch,
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


TEST(CrwCiffDecode, AddsDerivedExifEntriesForKnownCiffTags)
{
    const std::vector<std::byte> file = make_crw_with_derived_exif_sources();

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 16> ifds {};
    std::array<std::byte, 4096> payload {};
    std::array<uint32_t, 64> payload_scratch {};

    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    const SimpleMetaResult res = simple_meta_read(file, store, blocks, ifds,
                                                  payload, payload_scratch,
                                                  exif_opts, payload_opts);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);

    store.finalize();

    ASSERT_EQ(store.find_all(exif_key("ifd0", 0x010F)).size(), 1U);
    ASSERT_EQ(store.find_all(exif_key("ifd0", 0x0110)).size(), 1U);
    ASSERT_EQ(store.find_all(exif_key("ifd0", 0x0112)).size(), 1U);
    ASSERT_EQ(store.find_all(exif_key("exififd", 0x9003)).size(), 1U);
    ASSERT_EQ(store.find_all(exif_key("exififd", 0x9206)).size(), 1U);
    ASSERT_EQ(store.find_all(exif_key("exififd", 0xA002)).size(), 1U);
    ASSERT_EQ(store.find_all(exif_key("exififd", 0xA003)).size(), 1U);
}


TEST(CrwCiffDecode, DecodesNamedPaddedAsciiFieldsAndDerivedExifText)
{
    const std::vector<std::byte> file = make_crw_with_textual_ciff_fields();

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 16> ifds {};
    std::array<std::byte, 4096> payload {};
    std::array<uint32_t, 64> payload_scratch {};

    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    const SimpleMetaResult res = simple_meta_read(file, store, blocks, ifds,
                                                  payload, payload_scratch,
                                                  exif_opts, payload_opts);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);

    store.finalize();

    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_2804_0", 0x0805));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        EXPECT_EQ(arena_string(store.arena(), e.value),
                  "High definition camera");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_2807_1", 0x0810));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        EXPECT_EQ(arena_string(store.arena(), e.value), "Alice");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_300A_2", 0x0816));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        EXPECT_EQ(arena_string(store.arena(), e.value), "IMG_0001.CRW");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ifd0", 0x010E));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        EXPECT_EQ(arena_string(store.arena(), e.value),
                  "High definition camera");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("exififd", 0xA430));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Text);
        EXPECT_EQ(arena_string(store.arena(), e.value), "Alice");
    }
}


TEST(CrwCiffDecode, ProjectsNativeCiffSubtables)
{
    const std::vector<std::byte> file = make_crw_with_derived_exif_sources();

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 16> ifds {};
    std::array<std::byte, 4096> payload {};
    std::array<uint32_t, 64> payload_scratch {};

    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    const SimpleMetaResult res = simple_meta_read(file, store, blocks, ifds,
                                                  payload, payload_scratch,
                                                  exif_opts, payload_opts);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);

    store.finalize();

    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_300A_2_timestamp", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 1700000000U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_300A_2_timestamp", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I32);
        EXPECT_EQ(static_cast<int32_t>(e.value.data.i64), -3);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_300A_2_imageinfo", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::F32);
        EXPECT_EQ(e.value.data.f32_bits, f32_bits(1.0f));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_300A_2_imageinfo", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I32);
        EXPECT_EQ(static_cast<int32_t>(e.value.data.i64), 90);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_3002_1_exposureinfo", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::F32);
        EXPECT_EQ(e.value.data.f32_bits, f32_bits(0.33333334f));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_3002_1_exposureinfo", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::F32);
        EXPECT_EQ(e.value.data.f32_bits, f32_bits(3.0f));
    }
}


TEST(CrwCiffDecode, DecodesKnownNativeCiffScalarFieldsSemantically)
{
    const std::vector<std::byte> file = make_crw_with_semantic_native_scalars();

    MetaStore store;
    std::array<ContainerBlockRef, 16> blocks {};
    std::array<ExifIfdRef, 16> ifds {};
    std::array<std::byte, 4096> payload {};
    std::array<uint32_t, 64> payload_scratch {};

    ExifDecodeOptions exif_opts;
    PayloadOptions payload_opts;

    const SimpleMetaResult res = simple_meta_read(file, store, blocks, ifds,
                                                  payload, payload_scratch,
                                                  exif_opts, payload_opts);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);
    EXPECT_EQ(res.exif.status, ExifDecodeStatus::Ok);

    store.finalize();

    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_3002_0", 0x1010));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 2U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_3002_0", 0x1011));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 1U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_3002_0", 0x1807));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::F32);
        EXPECT_EQ(e.value.data.f32_bits, f32_bits(12.5f));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_3003_1", 0x1814));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::F32);
        EXPECT_EQ(e.value.data.f32_bits, f32_bits(9.5f));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_3004_2", 0x101C));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 100U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_300A_3", 0x100A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 7U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_300A_3", 0x1804));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 42U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("ciff_300A_3", 0x1817));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 162U);
    }
}

}  // namespace openmeta
