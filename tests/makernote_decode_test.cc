#include "openmeta/exif_tiff_decode.h"

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


    static void append_u16be(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }


    static void append_u32be(std::vector<std::byte>* out, uint32_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }


    static void write_u32le_at(std::vector<std::byte>* out, size_t off,
                               uint32_t v)
    {
        ASSERT_TRUE(out);
        ASSERT_GE(out->size(), off + 4U);
        (*out)[off + 0] = std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) };
        (*out)[off + 1] = std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) };
        (*out)[off + 2] = std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) };
        (*out)[off + 3] = std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) };
    }


    static void write_u16le_at(std::vector<std::byte>* out, size_t off,
                               uint16_t v)
    {
        ASSERT_TRUE(out);
        ASSERT_GE(out->size(), off + 2U);
        (*out)[off + 0] = std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) };
        (*out)[off + 1] = std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) };
    }


    static MetaKeyView exif_key(std::string_view ifd, uint16_t tag)
    {
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = ifd;
        key.data.exif_tag.tag = tag;
        return key;
    }


    static std::vector<std::byte>
    make_test_tiff_with_makernote(std::string_view make,
                                  std::span<const std::byte> maker_note)
    {
        // TIFF header (8 bytes) + IFD0 (2 entries) + Make string + ExifIFD (1 entry)
        // + MakerNote bytes.
        const uint32_t ifd0_off       = 8;
        const uint32_t ifd0_entry_cnt = 2;
        const uint32_t ifd0_size      = 2U + ifd0_entry_cnt * 12U + 4U;
        const uint32_t make_off       = ifd0_off + ifd0_size;
        const uint32_t make_count     = static_cast<uint32_t>(make.size() + 1U);
        const uint32_t exif_ifd_off   = make_off + make_count;
        const uint32_t exif_entry_cnt = 1;
        const uint32_t exif_ifd_size  = 2U + exif_entry_cnt * 12U + 4U;
        const uint32_t maker_note_off = exif_ifd_off + exif_ifd_size;
        const uint32_t maker_note_count = (maker_note.size() > UINT32_MAX)
                                              ? UINT32_MAX
                                              : static_cast<uint32_t>(
                                                    maker_note.size());

        std::vector<std::byte> tiff;
        append_bytes(&tiff, "II");
        append_u16le(&tiff, 42);
        append_u32le(&tiff, ifd0_off);

        // IFD0.
        append_u16le(&tiff, static_cast<uint16_t>(ifd0_entry_cnt));

        // Make (0x010F) ASCII at make_off.
        append_u16le(&tiff, 0x010F);
        append_u16le(&tiff, 2);
        append_u32le(&tiff, make_count);
        append_u32le(&tiff, make_off);

        // ExifIFDPointer (0x8769) LONG -> exif_ifd_off.
        append_u16le(&tiff, 0x8769);
        append_u16le(&tiff, 4);
        append_u32le(&tiff, 1);
        append_u32le(&tiff, exif_ifd_off);

        append_u32le(&tiff, 0);  // next IFD

        EXPECT_EQ(tiff.size(), make_off);
        append_bytes(&tiff, make);
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), exif_ifd_off);

        // ExifIFD.
        append_u16le(&tiff, static_cast<uint16_t>(exif_entry_cnt));

        // MakerNote (0x927C) UNDEFINED bytes at maker_note_off.
        append_u16le(&tiff, 0x927C);
        append_u16le(&tiff, 7);
        append_u32le(&tiff, maker_note_count);
        append_u32le(&tiff, maker_note_off);

        append_u32le(&tiff, 0);  // next IFD

        EXPECT_EQ(tiff.size(), maker_note_off);
        tiff.insert(tiff.end(), maker_note.begin(), maker_note.end());

        return tiff;
    }


    static std::vector<std::byte> make_canon_makernote()
    {
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x12345678U);
        append_u32le(&mn, 0);
        return mn;
    }


    static std::vector<std::byte> make_canon_camera_settings_makernote()
    {
        // Canon MakerNote with a single SHORT array tag (0x0001) stored
        // out-of-line, to exercise Canon BinaryData subdirectory decoding.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0001);  // CanonCameraSettings
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 3);       // count
        append_u32le(&mn, 18);      // value offset (MakerNote-relative)
        append_u32le(&mn, 0);       // next IFD
        EXPECT_EQ(mn.size(), 18U);
        append_u16le(&mn, 0);
        append_u16le(&mn, 11);
        append_u16le(&mn, 22);
        return mn;
    }


    static std::vector<std::byte> make_canon_custom_functions2_makernote()
    {
        // Canon MakerNote with a minimal CustomFunctions2 blob (0x0099),
        // following the CanonCustom2 group record structure.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0099);  // CustomFunctions2
        append_u16le(&mn, 4);       // LONG
        append_u32le(&mn, 8);       // count (32 bytes / 4)
        append_u32le(&mn, 0);       // value offset placeholder (absolute)
        append_u32le(&mn, 0);       // next IFD
        EXPECT_EQ(mn.size(), 18U);

        // CanonCustom2 blob (32 bytes total):
        // u16 size, u16 reserved, u32 group_count, then 1 group with 1 entry.
        append_u16le(&mn, 32);
        append_u16le(&mn, 0);
        append_u32le(&mn, 1);   // group count
        append_u32le(&mn, 1);   // recNum
        append_u32le(&mn, 20);  // recLen (excludes recCount)
        append_u32le(&mn, 1);   // recCount
        append_u32le(&mn, 0x0101);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0);

        EXPECT_EQ(mn.size(), 18U + 32U);
        return mn;
    }


    static std::vector<std::byte> make_canon_camera_info_psinfo_makernote()
    {
        // Canon MakerNote with a single CameraInfo blob tag (0x000d) that
        // contains a PictureStyleInfo table at offset 0x025b.
        const size_t cam_bytes = 0x025b + 0x0100;
        std::vector<std::byte> cam(cam_bytes, std::byte { 0 });
        write_u32le_at(&cam, 0x025b + 0x0000, 0);
        write_u32le_at(&cam, 0x025b + 0x0004, 3);
        write_u16le_at(&cam, 0x025b + 0x00d8, 129);

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x000d);  // CanonCameraInfo* blob
        append_u16le(&mn, 7);       // UNDEFINED bytes
        append_u32le(&mn, static_cast<uint32_t>(cam_bytes));
        append_u32le(&mn, 0);  // value offset placeholder (absolute)
        append_u32le(&mn, 0);  // next IFD
        EXPECT_EQ(mn.size(), 18U);
        mn.insert(mn.end(), cam.begin(), cam.end());
        return mn;
    }


    static std::vector<std::byte> make_canon_afinfo2_makernote()
    {
        // Canon MakerNote with CanonAFInfo2 (0x0026), stored out-of-line.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0026);  // CanonAFInfo2
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 48);      // count (96 bytes)
        append_u32le(&mn, 0);       // value offset placeholder (absolute)
        append_u32le(&mn, 0);       // next IFD
        EXPECT_EQ(mn.size(), 18U);

        // Word layout:
        // [0]=size(bytes), [1]=AFAreaMode, [2]=NumAFPoints, [3]=ValidAFPoints,
        // [4..7]=image dimensions, then 4 arrays of length NumAFPoints,
        // then two scalar fields.
        append_u16le(&mn, 96);    // size
        append_u16le(&mn, 2);     // AFAreaMode
        append_u16le(&mn, 9);     // NumAFPoints
        append_u16le(&mn, 9);     // ValidAFPoints
        append_u16le(&mn, 3888);  // CanonImageWidth
        append_u16le(&mn, 2592);  // CanonImageHeight
        append_u16le(&mn, 3888);  // AFImageWidth
        append_u16le(&mn, 2592);  // AFImageHeight

        for (int i = 0; i < 9; ++i)
            append_u16le(&mn, 97);  // widths
        for (int i = 0; i < 9; ++i)
            append_u16le(&mn, 98);  // heights

        const int16_t x_pos[9] = { 0, -649, 649, -1034, 0, 1034, -649, 649, 0 };
        for (int i = 0; i < 9; ++i) {
            append_u16le(&mn, static_cast<uint16_t>(x_pos[i]));
        }
        const int16_t y_pos[9] = { 562, 298, 298, 0, 0, 0, -298, -298, -562 };
        for (int i = 0; i < 9; ++i) {
            append_u16le(&mn, static_cast<uint16_t>(y_pos[i]));
        }

        append_u16le(&mn, 4);  // AFPointsInFocus
        append_u16le(&mn, 4);  // AFPointsSelected
        append_u16le(&mn, 0);  // padding
        append_u16le(&mn, 0);  // padding

        EXPECT_EQ(mn.size(), 18U + 96U);
        return mn;
    }


    static std::vector<std::byte> make_casio_type2_makernote()
    {
        // Casio MakerNote type2: "QVC\0" header + big-endian entry table.
        std::vector<std::byte> mn;
        append_bytes(&mn, "QVC");
        mn.push_back(std::byte { 0 });
        append_u32be(&mn, 1);  // entry count

        // Tag 0x0002 (PreviewImageSize), SHORT[2] stored inline.
        append_u16be(&mn, 0x0002);
        append_u16be(&mn, 3);
        append_u32be(&mn, 2);
        append_u16be(&mn, 320);
        append_u16be(&mn, 240);

        return mn;
    }


    static std::vector<std::byte> make_fuji_makernote()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "FUJIFILM");
        append_u32le(&mn, 12);
        EXPECT_EQ(mn.size(), 12U);

        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x00000042U);
        append_u32le(&mn, 0);
        return mn;
    }


    static std::vector<std::byte> make_nikon_makernote()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "Nikon");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        EXPECT_EQ(mn.size(), 10U);

        append_bytes(&mn, "II");
        append_u16le(&mn, 42);
        append_u32le(&mn, 8);

        // IFD0 at offset 8 (relative to TIFF header start).
        append_u16le(&mn, 1);
        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x01020304U);
        append_u32le(&mn, 0);

        return mn;
    }


    static std::vector<std::byte> make_apple_makernote()
    {
        // Minimal "Apple iOS" MakerNote sample:
        // prefix + endian marker + classic big-endian IFD.
        std::vector<std::byte> mn;
        append_bytes(&mn, "Apple iOS");
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 1 });
        append_bytes(&mn, "MM");
        EXPECT_EQ(mn.size(), 14U);

        // IFD at offset 14, big-endian.
        append_u16be(&mn, 2);  // entry count

        // Tag 0x0001 LONG value 17 (inline).
        append_u16be(&mn, 0x0001);
        append_u16be(&mn, 4);
        append_u32be(&mn, 1);
        append_u32be(&mn, 17);

        // Tag 0x0004 SHORT value 2 (inline).
        append_u16be(&mn, 0x0004);
        append_u16be(&mn, 3);
        append_u32be(&mn, 1);
        append_u16be(&mn, 2);
        append_u16be(&mn, 0);

        append_u32be(&mn, 0);  // next IFD
        return mn;
    }


    static std::vector<std::byte> make_olympus_makernote()
    {
        // Minimal Olympus MakerNote sample:
        // "OLYMP\0" + u16(version) + classic IFD at +8.
        //
        // Note: Many Olympus MakerNotes use offsets relative to the outer EXIF
        // TIFF header, so we patch the value offset in the test to be an
        // absolute offset in the generated TIFF bytes.
        std::vector<std::byte> mn;
        append_bytes(&mn, "OLYMP");
        mn.push_back(std::byte { 0 });
        append_u16le(&mn, 1);  // version
        EXPECT_EQ(mn.size(), 8U);

        // IFD0 at offset +8.
        append_u16le(&mn, 1);  // entry count

        // Tag 0x0200 LONG[3] stored out-of-line (absolute offset patched later).
        append_u16le(&mn, 0x0200);
        append_u16le(&mn, 4);
        append_u32le(&mn, 3);
        append_u32le(&mn, 0);  // value offset placeholder

        append_u32le(&mn, 0);  // next IFD
        return mn;
    }


    static std::vector<std::byte> make_pentax_makernote()
    {
        // Minimal Pentax MakerNote sample:
        // "AOC\0II" + u16le(count) + classic IFD entries at +8.
        std::vector<std::byte> mn;
        append_bytes(&mn, "AOC");
        mn.push_back(std::byte { 0 });
        append_bytes(&mn, "II");
        append_u16le(&mn, 1);
        EXPECT_EQ(mn.size(), 8U);

        // Tag 0x0001 SHORT value 2 (inline).
        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 2);
        append_u16le(&mn, 0);

        append_u32le(&mn, 0);  // next IFD
        return mn;
    }

}  // namespace

TEST(MakerNoteDecode, DecodesCanonStyleMakerNoteIfdAtOffset0)
{
    const std::vector<std::byte> mn   = make_canon_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 0x12345678U);
}


TEST(MakerNoteDecode, DecodesCanonBinaryDataCameraSettingsIntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_canon_camera_settings_makernote();
    const std::string_view make = "Canon";
    const uint32_t maker_note_off
        = 57U + static_cast<uint32_t>(make.size());  // see builder layout
    const uint32_t value_off_abs = maker_note_off + 18U;
    write_u32le_at(&mn, 10U, value_off_abs);

    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon_camerasettings_0", 0x0002));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 22U);
    EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
}


TEST(MakerNoteDecode, DecodesCanonCustomFunctions2IntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_canon_custom_functions2_makernote();
    const std::string_view make = "Canon";
    const uint32_t maker_note_off
        = 57U + static_cast<uint32_t>(make.size());  // see builder layout
    const uint32_t value_off_abs = maker_note_off + 18U;
    write_u32le_at(&mn, 10U, value_off_abs);

    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canoncustom_functions2_0", 0x0101));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 0U);
    EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
}


TEST(MakerNoteDecode, DecodesCanonCameraInfoPictureStyleIntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_canon_camera_info_psinfo_makernote();
    const std::string_view make = "Canon";
    const uint32_t maker_note_off
        = 57U + static_cast<uint32_t>(make.size());  // see builder layout
    const uint32_t value_off_abs = maker_note_off + 18U;
    write_u32le_at(&mn, 10U, value_off_abs);

    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_psinfo_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I32);
        EXPECT_EQ(e.value.data.i64, 3);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_psinfo_0", 0x00d8));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 129U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}


TEST(MakerNoteDecode, DecodesCanonAfInfo2IntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_canon_afinfo2_makernote();
    const std::string_view make = "Canon";
    const uint32_t maker_note_off
        = 57U + static_cast<uint32_t>(make.size());  // see builder layout
    const uint32_t value_off_abs = maker_note_off + 18U;
    write_u32le_at(&mn, 10U, value_off_abs);

    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_afinfo2_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 9U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_afinfo2_0", 0x0008));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 9U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canon_afinfo2_0", 0x000a));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.count, 9U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}


TEST(MakerNoteDecode, DecodesCasioType2MakerNoteQvcDirectory)
{
    const std::vector<std::byte> mn   = make_casio_type2_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("CASIO",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_casio_type2_0", 0x0002));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Array);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.count, 2U);
}


TEST(MakerNoteDecode, DecodesFujiMakerNoteWithSignatureAndOffset)
{
    const std::vector<std::byte> mn   = make_fuji_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_fuji0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 0x42U);
}


TEST(MakerNoteDecode, DecodesNikonMakerNoteWithEmbeddedTiffHeader)
{
    const std::vector<std::byte> mn   = make_nikon_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikon0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 0x01020304U);
}


TEST(MakerNoteDecode, DecodesAppleMakerNoteWithBigEndianIfdAtOffset14)
{
    const std::vector<std::byte> mn   = make_apple_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Apple",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_apple0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 17U);
}


TEST(MakerNoteDecode, DecodesOlympusMakerNoteWithOuterTiffOffsets)
{
    std::vector<std::byte> mn = make_olympus_makernote();

    const std::string_view make   = "OLYMPUS";
    const uint32_t maker_note_off = 57U + static_cast<uint32_t>(make.size());
    const uint32_t value_off_abs  = maker_note_off
                                   + static_cast<uint32_t>(mn.size());

    // Patch the out-of-line value offset in the MakerNote entry.
    // Layout: header(8) + entry_count(2) + tag(2) + type(2) + count(4) = 18.
    write_u32le_at(&mn, 18U, value_off_abs);

    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    append_u32le(&tiff, 1);
    append_u32le(&tiff, 2);
    append_u32le(&tiff, 3);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_olympus0", 0x0200));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Array);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.count, 3U);
}


TEST(MakerNoteDecode, DecodesPentaxMakerNoteWithAocHeaderAndCount)
{
    const std::vector<std::byte> mn   = make_pentax_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("PENTAX",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_pentax0", 0x0001));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 2U);
}

}  // namespace openmeta
