#include "openmeta/exif_tiff_decode.h"
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

    static void write_u32be_at(std::vector<std::byte>* out, size_t off,
                               uint32_t v)
    {
        ASSERT_TRUE(out);
        ASSERT_GE(out->size(), off + 4U);
        (*out)[off + 0] = std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) };
        (*out)[off + 1] = std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) };
        (*out)[off + 2] = std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) };
        (*out)[off + 3] = std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) };
    }


    static void write_u16le_at(std::vector<std::byte>* out, size_t off,
                               uint16_t v)
    {
        ASSERT_TRUE(out);
        ASSERT_GE(out->size(), off + 2U);
        (*out)[off + 0] = std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) };
        (*out)[off + 1] = std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) };
    }

    static uint8_t sony_encipher_byte(uint8_t b) noexcept
    {
        if (b >= 249U) {
            return b;
        }
        const uint32_t m  = 249U;
        const uint32_t x  = b;
        const uint32_t x2 = (x * x) % m;
        const uint32_t x3 = (x2 * x) % m;
        return static_cast<uint8_t>(x3);
    }


    static MetaKeyView exif_key(std::string_view ifd, uint16_t tag)
    {
        MetaKeyView key;
        key.kind              = MetaKeyKind::ExifTag;
        key.data.exif_tag.ifd = ifd;
        key.data.exif_tag.tag = tag;
        return key;
    }

    static bool read_u16le_at(std::span<const std::byte> bytes, size_t off,
                              uint16_t* out) noexcept
    {
        if (!out || off + 2U > bytes.size()) {
            return false;
        }
        const uint16_t b0 = static_cast<uint16_t>(bytes[off + 0]);
        const uint16_t b1 = static_cast<uint16_t>(bytes[off + 1]);
        *out              = static_cast<uint16_t>(b0 | (b1 << 8));
        return true;
    }

    static bool read_u32le_at(std::span<const std::byte> bytes, size_t off,
                              uint32_t* out) noexcept
    {
        if (!out || off + 4U > bytes.size()) {
            return false;
        }
        const uint32_t b0 = static_cast<uint32_t>(bytes[off + 0]);
        const uint32_t b1 = static_cast<uint32_t>(bytes[off + 1]);
        const uint32_t b2 = static_cast<uint32_t>(bytes[off + 2]);
        const uint32_t b3 = static_cast<uint32_t>(bytes[off + 3]);
        *out              = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
        return true;
    }

    static bool patch_sony_makernote_value_offset_in_tiff(
        std::vector<std::byte>* tiff) noexcept
    {
        if (!tiff || tiff->size() < 8U) {
            return false;
        }
        const std::span<const std::byte> bytes(tiff->data(), tiff->size());
        if (bytes[0] != std::byte { 'I' } || bytes[1] != std::byte { 'I' }) {
            return false;
        }

        uint32_t ifd0_off = 0;
        if (!read_u32le_at(bytes, 4, &ifd0_off)) {
            return false;
        }
        if (ifd0_off + 2U > bytes.size()) {
            return false;
        }
        uint16_t ifd0_count = 0;
        if (!read_u16le_at(bytes, ifd0_off, &ifd0_count) || ifd0_count == 0) {
            return false;
        }

        uint32_t exif_ifd_off            = 0;
        const size_t ifd0_entries_off    = size_t(ifd0_off) + 2U;
        const size_t ifd0_entries_bytes  = size_t(ifd0_count) * 12U;
        const size_t ifd0_entries_needed = ifd0_entries_off
                                           + ifd0_entries_bytes;
        if (ifd0_entries_needed > bytes.size()) {
            return false;
        }

        for (uint16_t i = 0; i < ifd0_count; ++i) {
            const size_t eoff = ifd0_entries_off + size_t(i) * 12U;
            uint16_t tag      = 0;
            if (!read_u16le_at(bytes, eoff + 0, &tag)) {
                return false;
            }
            if (tag == 0x8769) {  // ExifIFDPointer
                if (!read_u32le_at(bytes, eoff + 8, &exif_ifd_off)) {
                    return false;
                }
                break;
            }
        }
        if (exif_ifd_off == 0 || exif_ifd_off + 2U > bytes.size()) {
            return false;
        }

        uint16_t exif_count = 0;
        if (!read_u16le_at(bytes, exif_ifd_off, &exif_count)
            || exif_count == 0) {
            return false;
        }

        uint32_t maker_note_off          = 0;
        const size_t exif_entries_off    = size_t(exif_ifd_off) + 2U;
        const size_t exif_entries_bytes  = size_t(exif_count) * 12U;
        const size_t exif_entries_needed = exif_entries_off
                                           + exif_entries_bytes;
        if (exif_entries_needed > bytes.size()) {
            return false;
        }

        for (uint16_t i = 0; i < exif_count; ++i) {
            const size_t eoff = exif_entries_off + size_t(i) * 12U;
            uint16_t tag      = 0;
            if (!read_u16le_at(bytes, eoff + 0, &tag)) {
                return false;
            }
            if (tag == 0x927C) {  // MakerNote
                if (!read_u32le_at(bytes, eoff + 8, &maker_note_off)) {
                    return false;
                }
                break;
            }
        }
        if (maker_note_off == 0 || maker_note_off + 18U > bytes.size()) {
            return false;
        }

        // Sony MakerNote: out-of-line value offsets are absolute (relative to
        // the outer TIFF stream). Our test MakerNote consists of:
        //   u16(entry_count=1) + IFD entry(12) + next_ifd(4) + payload...
        // so the payload starts at +18 bytes.
        const uint32_t value_off_abs = maker_note_off + 18U;
        const size_t patch_off       = size_t(maker_note_off) + 10U;
        if (patch_off + 4U > tiff->size()) {
            return false;
        }
        (*tiff)[patch_off + 0]
            = std::byte { static_cast<uint8_t>((value_off_abs >> 0) & 0xFF) };
        (*tiff)[patch_off + 1]
            = std::byte { static_cast<uint8_t>((value_off_abs >> 8) & 0xFF) };
        (*tiff)[patch_off + 2]
            = std::byte { static_cast<uint8_t>((value_off_abs >> 16) & 0xFF) };
        (*tiff)[patch_off + 3]
            = std::byte { static_cast<uint8_t>((value_off_abs >> 24) & 0xFF) };
        return true;
    }

    static bool patch_makernote_count_in_tiff(std::vector<std::byte>* tiff,
                                              uint32_t new_count) noexcept
    {
        if (!tiff || tiff->size() < 8U) {
            return false;
        }
        const std::span<const std::byte> bytes(tiff->data(), tiff->size());
        if (bytes[0] != std::byte { 'I' } || bytes[1] != std::byte { 'I' }) {
            return false;
        }

        uint32_t ifd0_off = 0;
        if (!read_u32le_at(bytes, 4, &ifd0_off)) {
            return false;
        }
        if (ifd0_off + 2U > bytes.size()) {
            return false;
        }
        uint16_t ifd0_count = 0;
        if (!read_u16le_at(bytes, ifd0_off, &ifd0_count) || ifd0_count == 0) {
            return false;
        }

        uint32_t exif_ifd_off            = 0;
        const size_t ifd0_entries_off    = size_t(ifd0_off) + 2U;
        const size_t ifd0_entries_bytes  = size_t(ifd0_count) * 12U;
        const size_t ifd0_entries_needed = ifd0_entries_off
                                           + ifd0_entries_bytes;
        if (ifd0_entries_needed > bytes.size()) {
            return false;
        }

        for (uint16_t i = 0; i < ifd0_count; ++i) {
            const size_t eoff = ifd0_entries_off + size_t(i) * 12U;
            uint16_t tag      = 0;
            if (!read_u16le_at(bytes, eoff + 0, &tag)) {
                return false;
            }
            if (tag == 0x8769) {  // ExifIFDPointer
                if (!read_u32le_at(bytes, eoff + 8, &exif_ifd_off)) {
                    return false;
                }
                break;
            }
        }
        if (exif_ifd_off == 0 || exif_ifd_off + 2U > bytes.size()) {
            return false;
        }

        uint16_t exif_count = 0;
        if (!read_u16le_at(bytes, exif_ifd_off, &exif_count)
            || exif_count == 0) {
            return false;
        }

        const size_t exif_entries_off    = size_t(exif_ifd_off) + 2U;
        const size_t exif_entries_bytes  = size_t(exif_count) * 12U;
        const size_t exif_entries_needed = exif_entries_off
                                           + exif_entries_bytes;
        if (exif_entries_needed > bytes.size()) {
            return false;
        }

        for (uint16_t i = 0; i < exif_count; ++i) {
            const size_t eoff = exif_entries_off + size_t(i) * 12U;
            uint16_t tag      = 0;
            if (!read_u16le_at(bytes, eoff + 0, &tag)) {
                return false;
            }
            if (tag == 0x927C) {  // MakerNote
                write_u32le_at(tiff, eoff + 4, new_count);
                return true;
            }
        }

        return false;
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

    static std::vector<std::byte> make_test_tiff_with_makernote_and_model(
        std::string_view make, std::string_view model,
        std::span<const std::byte> maker_note)
    {
        // TIFF header (8 bytes) + IFD0 (3 entries: Make, Model, ExifIFDPointer)
        // + Make string + Model string + ExifIFD (1 entry) + MakerNote bytes.
        const uint32_t ifd0_off       = 8;
        const uint32_t ifd0_entry_cnt = 3;
        const uint32_t ifd0_size      = 2U + ifd0_entry_cnt * 12U + 4U;
        const uint32_t make_off       = ifd0_off + ifd0_size;
        const uint32_t make_count     = static_cast<uint32_t>(make.size() + 1U);
        const uint32_t model_off      = make_off + make_count;
        const uint32_t model_count  = static_cast<uint32_t>(model.size() + 1U);
        const uint32_t exif_ifd_off = model_off + model_count;
        const uint32_t exif_entry_cnt   = 1;
        const uint32_t exif_ifd_size    = 2U + exif_entry_cnt * 12U + 4U;
        const uint32_t maker_note_off   = exif_ifd_off + exif_ifd_size;
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

        // Model (0x0110) ASCII at model_off.
        append_u16le(&tiff, 0x0110);
        append_u16le(&tiff, 2);
        append_u32le(&tiff, model_count);
        append_u32le(&tiff, model_off);

        // ExifIFDPointer (0x8769) LONG -> exif_ifd_off.
        append_u16le(&tiff, 0x8769);
        append_u16le(&tiff, 4);
        append_u32le(&tiff, 1);
        append_u32le(&tiff, exif_ifd_off);

        append_u32le(&tiff, 0);  // next IFD

        EXPECT_EQ(tiff.size(), make_off);
        append_bytes(&tiff, make);
        tiff.push_back(std::byte { 0 });

        EXPECT_EQ(tiff.size(), model_off);
        append_bytes(&tiff, model);
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

    static std::vector<std::byte> make_sony_makernote()
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "SONY");

        // Classic IFD starting at offset 4, with offsets relative to the outer
        // TIFF stream (patched by the test after placement).
        append_u16le(&mn, 2);       // entry count
        append_u16le(&mn, 0x0102);  // Quality
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 1);       // count
        append_u32le(&mn, 7);       // value (inline)

        append_u16le(&mn, 0xB020);  // CreativeStyle
        append_u16le(&mn, 2);       // ASCII
        append_u32le(&mn, 9);       // count ("Standard\0")
        append_u32le(&mn, 0);       // value offset placeholder (absolute)

        append_u32le(&mn, 0);  // next IFD
        EXPECT_EQ(mn.size(), 34U);

        append_bytes(&mn, "Standard");
        mn.push_back(std::byte { 0 });
        return mn;
    }

    static std::vector<std::byte> make_minolta_makernote()
    {
        // Minolta/Konica-Minolta MakerNote is often a classic TIFF IFD.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0100);  // SceneMode
        append_u16le(&mn, 4);       // LONG
        append_u32le(&mn, 1);       // count
        append_u32le(&mn, 13);      // value (inline)
        append_u32le(&mn, 0);       // next IFD
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

    static std::vector<std::byte>
    make_canon_camera_settings_makernote_adjusted_base()
    {
        // Canon MakerNote where the out-of-line value offset is not relative to
        // the MakerNote start, but to an adjusted base. This occurs in some
        // real-world Canon JPEGs where ExifTool reports:
        //   "Adjusted MakerNotes base by <N>".
        //
        // The value bytes are placed immediately after the IFD table (at +18),
        // but the offset field stores a larger value (50). The decoder should
        // infer a base shift so that base+50 == maker_note_off+18.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0001);  // CanonCameraSettings
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 3);       // count
        append_u32le(&mn, 50);      // value offset (adjusted base)
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
        append_u32le(&mn, 18);      // value offset (MakerNote-relative)
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

    static std::vector<std::byte> make_canon_custom_functions1d_makernote()
    {
        // Canon MakerNote with:
        // - CustomFunctions1D (0x0090): u16 array where each u16 encodes
        //   (tag_id << 8) | value, preceded by a byte-size header.
        // - PersonalFunctions (0x0091): u16 values preceded by size header.
        // - PersonalFunctionValues (0x0092): u16 values preceded by size header.
        std::vector<std::byte> mn;
        append_u16le(&mn, 3);       // entry count
        append_u16le(&mn, 0x0090);  // CustomFunctions1D
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 6);       // count
        append_u32le(&mn, 42);      // value offset (MakerNote-relative)
        append_u16le(&mn, 0x0091);  // PersonalFunctions
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 4);       // count
        append_u32le(&mn, 54);      // value offset
        append_u16le(&mn, 0x0092);  // PersonalFunctionValues
        append_u16le(&mn, 3);       // SHORT
        append_u32le(&mn, 4);       // count
        append_u32le(&mn, 62);      // value offset
        append_u32le(&mn, 0);       // next IFD
        EXPECT_EQ(mn.size(), 42U);

        // CustomFunctions1D (6*2 = 12 bytes).
        append_u16le(&mn, 12);      // size in bytes
        append_u16le(&mn, 0x0001);  // tag0=1
        append_u16le(&mn, 0x0100);  // tag1=0
        append_u16le(&mn, 0x0200);  // tag2=0
        append_u16le(&mn, 0x0301);  // tag3=1
        append_u16le(&mn, 0x0400);  // tag4=0

        // PersonalFunctions (4*2 = 8 bytes).
        append_u16le(&mn, 8);
        append_u16le(&mn, 0);
        append_u16le(&mn, 0);
        append_u16le(&mn, 0);

        // PersonalFunctionValues (4*2 = 8 bytes).
        append_u16le(&mn, 8);
        append_u16le(&mn, 63);
        append_u16le(&mn, 240);
        append_u16le(&mn, 16);

        EXPECT_EQ(mn.size(), 70U);
        return mn;
    }

    static std::vector<std::byte> make_canon_truncated_count_makernote()
    {
        // Canon MakerNote with a classic IFD that stores an out-of-line ASCII
        // value immediately after the directory. Used to test cases where the
        // declared MakerNote byte count is smaller than the directory size.
        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x0006);  // CanonImageType
        append_u16le(&mn, 2);       // ASCII
        append_u32le(&mn, 13);      // count
        append_u32le(&mn, 18);      // value offset (MakerNote-relative)
        append_u32le(&mn, 0);       // next IFD
        EXPECT_EQ(mn.size(), 18U);

        append_bytes(&mn, "IMG:TEST JPEG");
        mn.push_back(std::byte { 0 });
        EXPECT_GE(mn.size(), 18U + 13U);
        return mn;
    }


    static std::vector<std::byte> make_canon_camera_info_psinfo_makernote()
    {
        // Canon MakerNote with a single CameraInfo blob tag (0x000d) that
        // contains a PictureStyleInfo table at offset 0x025b.
        const size_t cam_bytes = 0x025b + 0x0100;
        std::vector<std::byte> cam(cam_bytes, std::byte { 0 });

        // Minimal embedded classic IFD at the start of the blob:
        // tag 0x0003 SHORT value 42.
        write_u16le_at(&cam, 0, 1);       // entry count
        write_u16le_at(&cam, 2, 0x0003);  // tag
        write_u16le_at(&cam, 4, 3);       // type SHORT
        write_u32le_at(&cam, 6, 1);       // count
        write_u16le_at(&cam, 10, 42);     // value (inline)
        write_u16le_at(&cam, 12, 0);      // padding
        write_u32le_at(&cam, 14, 0);      // next IFD

        write_u32le_at(&cam, 0x025b + 0x0000, 0);
        write_u32le_at(&cam, 0x025b + 0x0004, 3);
        write_u16le_at(&cam, 0x025b + 0x00d8, 129);

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);       // entry count
        append_u16le(&mn, 0x000d);  // CanonCameraInfo* blob
        append_u16le(&mn, 7);       // UNDEFINED bytes
        append_u32le(&mn, static_cast<uint32_t>(cam_bytes));
        append_u32le(&mn, 18);  // value offset (MakerNote-relative)
        append_u32le(&mn, 0);   // next IFD
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
        append_u32le(&mn, 18);      // value offset (MakerNote-relative)
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

    static std::vector<std::byte> make_casio_type2_makernote_fr10_variant()
    {
        // Casio MakerNote "QVC" directory variant observed in some models:
        // "QVC\0" + u16le version + u16le entry_count, then little-endian
        // IFD-style entries (12 bytes each).
        std::vector<std::byte> mn;
        append_bytes(&mn, "QVC");
        mn.push_back(std::byte { 0 });
        append_u16le(&mn, 0);  // version
        append_u16le(&mn, 1);  // entry count

        // Tag 0x0002 (PreviewImageSize), SHORT[2] stored inline (LE).
        append_u16le(&mn, 0x0002);
        append_u16le(&mn, 3);
        append_u32le(&mn, 2);
        append_u16le(&mn, 320);
        append_u16le(&mn, 240);

        return mn;
    }

    static std::vector<std::byte>
    make_casio_type2_makernote_out_of_line(uint32_t payload_off_tiff_relative)
    {
        // One out-of-line UNDEFINED value to validate TIFF-relative offsets.
        std::vector<std::byte> mn;
        append_bytes(&mn, "QVC");
        mn.push_back(std::byte { 0 });
        append_u32be(&mn, 1);  // entry count

        // Tag 0x2000 (PreviewImage), UNDEFINED[6] stored out-of-line.
        append_u16be(&mn, 0x2000);
        append_u16be(&mn, 7);
        append_u32be(&mn, 6);
        append_u32be(&mn, payload_off_tiff_relative);
        return mn;
    }

    static std::vector<std::byte>
    make_casio_type2_makernote_faceinfo1(uint32_t payload_off_tiff_relative,
                                         uint32_t payload_bytes)
    {
        std::vector<std::byte> mn;
        append_bytes(&mn, "QVC");
        mn.push_back(std::byte { 0 });
        append_u32be(&mn, 1);  // entry count

        // Tag 0x2089 (FaceInfo1), UNDEFINED stored out-of-line.
        append_u16be(&mn, 0x2089);
        append_u16be(&mn, 7);
        append_u32be(&mn, payload_bytes);
        append_u32be(&mn, payload_off_tiff_relative);
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
        append_u16le(&mn, 2);
        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x01020304U);

        // VRInfo (0x001f) UNDEFINED[8] stored out-of-line at offset 38.
        append_u16le(&mn, 0x001f);
        append_u16le(&mn, 7);
        append_u32le(&mn, 8);
        append_u32le(&mn, 38);

        append_u32le(&mn, 0);

        EXPECT_EQ(mn.size(), 48U);
        append_bytes(&mn, "0101");
        mn.push_back(std::byte { 1 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });

        return mn;
    }

    static std::vector<std::byte> make_nikon_makernote_with_binary_subdirs()
    {
        // Nikon MakerNote with an embedded TIFF header, and a number of
        // BinaryData subdirectory tags (UNDEFINED byte blobs) commonly seen in
        // real-world Nikon RAW files.
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

        static constexpr uint16_t kEntryCount = 8;
        append_u16le(&mn, kEntryCount);

        uint32_t value_off = 8U + 2U + static_cast<uint32_t>(kEntryCount) * 12U
                             + 4U;

        // Tag 0x0001 LONG value 0x01020304 (inline).
        append_u16le(&mn, 0x0001);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, 0x01020304U);

        // VRInfo (0x001f) UNDEFINED[8].
        append_u16le(&mn, 0x001f);
        append_u16le(&mn, 7);
        append_u32le(&mn, 8);
        append_u32le(&mn, value_off);
        value_off += 8;

        // DistortInfo (0x002b) UNDEFINED[16].
        append_u16le(&mn, 0x002b);
        append_u16le(&mn, 7);
        append_u32le(&mn, 16);
        append_u32le(&mn, value_off);
        value_off += 16;

        // FlashInfo (0x00a8) UNDEFINED[49].
        append_u16le(&mn, 0x00a8);
        append_u16le(&mn, 7);
        append_u32le(&mn, 49);
        append_u32le(&mn, value_off);
        value_off += 49;

        // MultiExposure (0x00b0) UNDEFINED[16].
        append_u16le(&mn, 0x00b0);
        append_u16le(&mn, 7);
        append_u32le(&mn, 16);
        append_u32le(&mn, value_off);
        value_off += 16;

        // AFInfo2 (0x00b7) UNDEFINED[30].
        append_u16le(&mn, 0x00b7);
        append_u16le(&mn, 7);
        append_u32le(&mn, 30);
        append_u32le(&mn, value_off);
        value_off += 30;

        // FileInfo (0x00b8) UNDEFINED[10].
        append_u16le(&mn, 0x00b8);
        append_u16le(&mn, 7);
        append_u32le(&mn, 10);
        append_u32le(&mn, value_off);
        value_off += 10;

        // RetouchInfo (0x00bb) UNDEFINED[8].
        append_u16le(&mn, 0x00bb);
        append_u16le(&mn, 7);
        append_u32le(&mn, 8);
        append_u32le(&mn, value_off);
        value_off += 8;

        append_u32le(&mn, 0);  // next IFD

        const uint32_t expected_value_start
            = 10U + 8U + 2U + static_cast<uint32_t>(kEntryCount) * 12U + 4U;
        EXPECT_EQ(mn.size(), expected_value_start);

        // VRInfo value bytes (version "0101", vr_enabled=1, vr_mode=2).
        append_bytes(&mn, "0101");
        mn.push_back(std::byte { 1 });
        mn.push_back(std::byte { 0 });
        mn.push_back(std::byte { 2 });
        mn.push_back(std::byte { 0 });

        // DistortInfo value bytes (version "0100", AutoDistortionControl=1).
        {
            std::array<std::byte, 16> raw {};
            raw[0] = std::byte { '0' };
            raw[1] = std::byte { '1' };
            raw[2] = std::byte { '0' };
            raw[3] = std::byte { '0' };
            raw[4] = std::byte { 1 };
            mn.insert(mn.end(), raw.begin(), raw.end());
        }

        // FlashInfo0106 value bytes (mostly zeros, but include a few fields).
        {
            std::array<std::byte, 49> raw {};
            raw[0]      = std::byte { '0' };
            raw[1]      = std::byte { '1' };
            raw[2]      = std::byte { '0' };
            raw[3]      = std::byte { '6' };
            raw[4]      = std::byte { 1 };     // FlashSource
            raw[6]      = std::byte { 0x11 };  // ExternalFlashFirmware[0]
            raw[7]      = std::byte { 0x22 };  // ExternalFlashFirmware[1]
            raw[8]      = std::byte { 0x33 };  // ExternalFlashFlags
            raw[0x27]   = std::byte { 0x80 };  // FlashCompensation (-128)
            raw[0x002a] = std::byte { 0xFF };  // FlashGroupCCompensation (-1)
            mn.insert(mn.end(), raw.begin(), raw.end());
        }

        // MultiExposure value bytes (version "0100", mode=1, shots=2, gain=3).
        append_bytes(&mn, "0100");
        append_u32le(&mn, 1);
        append_u32le(&mn, 2);
        append_u32le(&mn, 3);

        // AFInfo2 value bytes (version "0100", some fields + offsets).
        {
            std::array<std::byte, 30> raw {};
            raw[0]    = std::byte { '0' };
            raw[1]    = std::byte { '1' };
            raw[2]    = std::byte { '0' };
            raw[3]    = std::byte { '0' };
            raw[4]    = std::byte { 1 };  // ContrastDetectAF
            raw[5]    = std::byte { 8 };  // AFAreaMode
            raw[6]    = std::byte { 3 };  // PhaseDetectAF
            raw[7]    = std::byte { 2 };  // PrimaryAFPoint
            raw[8]    = std::byte { 0xaa };
            raw[9]    = std::byte { 0xbb };
            raw[10]   = std::byte { 0xcc };
            raw[11]   = std::byte { 0xdd };
            raw[12]   = std::byte { 0xee };
            raw[0x10] = std::byte { 0x34 };  // AFImageWidth (0x1234 le)
            raw[0x11] = std::byte { 0x12 };
            raw[0x12] = std::byte { 0x78 };  // AFImageHeight (0x5678 le)
            raw[0x13] = std::byte { 0x56 };
            raw[0x1c] = std::byte { 1 };  // ContrastDetectAFInFocus
            mn.insert(mn.end(), raw.begin(), raw.end());
        }

        // FileInfo value bytes (version "0100", card=2, dir=99, file=8).
        append_bytes(&mn, "0100");
        append_u16le(&mn, 2);
        append_u16le(&mn, 99);
        append_u16le(&mn, 8);

        // RetouchInfo value bytes (version "0200", processing=-1).
        {
            std::array<std::byte, 8> raw {};
            raw[0] = std::byte { '0' };
            raw[1] = std::byte { '2' };
            raw[2] = std::byte { '0' };
            raw[3] = std::byte { '0' };
            raw[5] = std::byte { 0xFF };
            mn.insert(mn.end(), raw.begin(), raw.end());
        }

        EXPECT_EQ(mn.size(), static_cast<size_t>(10U + value_off));
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

    static std::vector<std::byte> make_olympus_makernote_olympus_signature()
    {
        // Olympus MakerNote variant:
        // "OLYMPUS\0" + byte order marker + u16 + classic IFD at +12.
        // Sub-IFD offsets are relative to the MakerNote start, and are often
        // stored as LONG (not IFD) pointers.
        std::vector<std::byte> mn;
        append_bytes(&mn, "OLYMPUS");
        mn.push_back(std::byte { 0 });
        append_bytes(&mn, "II");
        append_u16le(&mn, 3);
        EXPECT_EQ(mn.size(), 12U);

        // Root IFD at +12.
        append_u16le(&mn, 1);  // entry count

        const uint32_t sub_ifd_off = 12U + 18U;  // directly after this IFD

        // Tag 0x4000 (Main) LONG offset to sub-IFD (MakerNote-relative).
        append_u16le(&mn, 0x4000);
        append_u16le(&mn, 4);
        append_u32le(&mn, 1);
        append_u32le(&mn, sub_ifd_off);

        append_u32le(&mn, 0);  // next IFD
        EXPECT_EQ(mn.size(), sub_ifd_off);

        // Sub-IFD containing a single main tag (Quality).
        append_u16le(&mn, 1);  // entry count
        append_u16le(&mn, 0x0201);
        append_u16le(&mn, 3);
        append_u32le(&mn, 1);
        append_u16le(&mn, 2);
        append_u16le(&mn, 0);
        append_u32le(&mn, 0);  // next IFD

        return mn;
    }

    static std::vector<std::byte> make_panasonic_makernote_with_subdirs()
    {
        // Minimal Panasonic MakerNote sample:
        // classic IFD at offset 0 with UNDEFINED binary sub-blocks:
        // - FaceDetInfo (0x004e)
        // - FaceRecInfo (0x0061)
        // - TimeInfo (0x2003)

        // FaceDetInfo: int16u format (word offsets).
        std::vector<std::byte> facedet;
        append_u16le(&facedet, 1);   // NumFacePositions
        append_u16le(&facedet, 10);  // X
        append_u16le(&facedet, 20);  // Y
        append_u16le(&facedet, 30);  // W
        append_u16le(&facedet, 40);  // H
        EXPECT_EQ(facedet.size(), 10U);

        // FaceRecInfo: byte offsets.
        std::vector<std::byte> facerec(52, std::byte { 0 });
        write_u16le_at(&facerec, 0, 1);  // FacesRecognized
        {
            const std::string_view name = "Bob";
            for (size_t i = 0; i < name.size() && (4U + i) < facerec.size();
                 ++i) {
                facerec[4U + i]
                    = std::byte { static_cast<uint8_t>(name[i]) };
            }
        }
        write_u16le_at(&facerec, 24U + 0, 1);
        write_u16le_at(&facerec, 24U + 2, 2);
        write_u16le_at(&facerec, 24U + 4, 3);
        write_u16le_at(&facerec, 24U + 6, 4);
        {
            const std::string_view age = "25";
            for (size_t i = 0; i < age.size() && (32U + i) < facerec.size();
                 ++i) {
                facerec[32U + i]
                    = std::byte { static_cast<uint8_t>(age[i]) };
            }
        }
        EXPECT_EQ(facerec.size(), 52U);

        // TimeInfo: BCD date-time at 0..7 and shot number at 16..19.
        std::vector<std::byte> timeinfo(20, std::byte { 0xFF });
        // 2024:06:27 12:53:52.54 (BCD nibbles).
        timeinfo[0] = std::byte { 0x20 };
        timeinfo[1] = std::byte { 0x24 };
        timeinfo[2] = std::byte { 0x06 };
        timeinfo[3] = std::byte { 0x27 };
        timeinfo[4] = std::byte { 0x12 };
        timeinfo[5] = std::byte { 0x53 };
        timeinfo[6] = std::byte { 0x52 };
        timeinfo[7] = std::byte { 0x54 };
        write_u32le_at(&timeinfo, 16U, 123);
        EXPECT_EQ(timeinfo.size(), 20U);

        std::vector<std::byte> mn;
        append_u16le(&mn, 3);  // entry count

        // FaceDetInfo (UNDEFINED, out-of-line value offset patched by caller).
        append_u16le(&mn, 0x004e);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(facedet.size()));
        append_u32le(&mn, 0);

        // FaceRecInfo (UNDEFINED, out-of-line value offset patched by caller).
        append_u16le(&mn, 0x0061);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(facerec.size()));
        append_u32le(&mn, 0);

        // TimeInfo (UNDEFINED, out-of-line value offset patched by caller).
        append_u16le(&mn, 0x2003);
        append_u16le(&mn, 7);
        append_u32le(&mn, static_cast<uint32_t>(timeinfo.size()));
        append_u32le(&mn, 0);

        append_u32le(&mn, 0);  // next IFD

        mn.insert(mn.end(), facedet.begin(), facedet.end());
        mn.insert(mn.end(), facerec.begin(), facerec.end());
        mn.insert(mn.end(), timeinfo.begin(), timeinfo.end());
        return mn;
    }

    static std::vector<std::byte> make_panasonic_type2_makernote()
    {
        // Minimal Panasonic Type2 MakerNote sample (fixed-layout binary).
        std::vector<std::byte> mn;
        append_bytes(&mn, "TEST");  // MakerNoteType (string[4])
        append_u16le(&mn, 0);
        append_u16le(&mn, 42);  // Gain (word offset 3 -> byte offset 6)
        EXPECT_EQ(mn.size(), 8U);
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

    static std::vector<std::byte> make_sony_makernote_tag9050b_ciphered()
    {
        // Minimal Sony MakerNote:
        // Classic IFD at offset 0, but out-of-line value offsets are absolute
        // (relative to outer TIFF stream).
        //
        // We include only Tag9050 (UNDEFINED bytes), containing the ciphered
        // BinaryData sub-block that OpenMeta decodes into a derived IFD.
        const size_t payload_size = 0x90;
        std::vector<std::byte> plain(payload_size, std::byte { 0 });

        // Shutter (u16[3]) at 0x0026.
        plain[0x0026] = std::byte { 0x01 };
        plain[0x0027] = std::byte { 0x00 };
        plain[0x0028] = std::byte { 0x02 };
        plain[0x0029] = std::byte { 0x00 };
        plain[0x002A] = std::byte { 0x03 };
        plain[0x002B] = std::byte { 0x00 };

        // ShutterCount (u32) at 0x003A.
        plain[0x003A] = std::byte { 0x11 };
        plain[0x003B] = std::byte { 0x22 };
        plain[0x003C] = std::byte { 0x33 };
        plain[0x003D] = std::byte { 0x44 };

        // InternalSerialNumber (6 bytes) at 0x0088.
        plain[0x0088] = std::byte { 0x01 };
        plain[0x0089] = std::byte { 0x02 };
        plain[0x008A] = std::byte { 0x03 };
        plain[0x008B] = std::byte { 0x04 };
        plain[0x008C] = std::byte { 0x05 };
        plain[0x008D] = std::byte { 0x06 };

        std::vector<std::byte> cipher(payload_size, std::byte { 0 });
        for (size_t i = 0; i < plain.size(); ++i) {
            const uint8_t p = static_cast<uint8_t>(plain[i]);
            cipher[i]       = std::byte { sony_encipher_byte(p) };
        }

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);  // entry count
        append_u16le(&mn, 0x9050);
        append_u16le(&mn, 7);  // UNDEFINED
        append_u32le(&mn, static_cast<uint32_t>(cipher.size()));
        append_u32le(&mn, 0);  // value offset placeholder (patched by caller)
        append_u32le(&mn, 0);  // next IFD

        mn.insert(mn.end(), cipher.begin(), cipher.end());
        return mn;
    }

    static std::vector<std::byte> make_sony_makernote_tag2010i_ciphered()
    {
        // Minimal Sony MakerNote containing Tag2010 (UNDEFINED bytes) for the
        // ciphered Tag2010i subtable.
        //
        // We keep the payload small enough to cover fields asserted in the
        // test; out-of-range fields are skipped by the decoder.
        const size_t payload_size = 0x1800;
        std::vector<std::byte> plain(payload_size, std::byte { 0 });

        // I16 tag 0x0217 = 0x1234.
        plain[0x0217] = std::byte { 0x34 };
        plain[0x0218] = std::byte { 0x12 };

        // WB_RGBLevels u16[3] at 0x0252 = [1, 2, 3].
        plain[0x0252] = std::byte { 0x01 };
        plain[0x0253] = std::byte { 0x00 };
        plain[0x0254] = std::byte { 0x02 };
        plain[0x0255] = std::byte { 0x00 };
        plain[0x0256] = std::byte { 0x03 };
        plain[0x0257] = std::byte { 0x00 };

        // SonyISO u16 at 0x0320 = 100.
        plain[0x0320] = std::byte { 0x64 };
        plain[0x0321] = std::byte { 0x00 };

        // DistortionCorrParams bytes[32] at 0x17D0.
        for (size_t i = 0; i < 32; ++i) {
            plain[0x17D0 + i] = std::byte { static_cast<uint8_t>(i) };
        }

        std::vector<std::byte> cipher(payload_size, std::byte { 0 });
        for (size_t i = 0; i < plain.size(); ++i) {
            const uint8_t p = static_cast<uint8_t>(plain[i]);
            cipher[i]       = std::byte { sony_encipher_byte(p) };
        }

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);  // entry count
        append_u16le(&mn, 0x2010);
        append_u16le(&mn, 7);  // UNDEFINED
        append_u32le(&mn, static_cast<uint32_t>(cipher.size()));
        append_u32le(&mn, 0);  // value offset placeholder (absolute)
        append_u32le(&mn, 0);  // next IFD

        mn.insert(mn.end(), cipher.begin(), cipher.end());
        return mn;
    }

    static std::vector<std::byte> make_sony_makernote_tag3000_shotinfo()
    {
        // Minimal Sony MakerNote containing ShotInfo (tag 0x3000). Within the
        // ShotInfo block, values are stored at offsets equal to the tag id.
        std::vector<std::byte> blob(0x44, std::byte { 0 });
        blob[0] = std::byte { 'I' };
        blob[1] = std::byte { 'I' };

        write_u16le_at(&blob, 0x0002, 94);

        const std::string_view dt = "2017:02:08 07:07:08";
        for (size_t i = 0; i < dt.size() && (0x0006 + i) < blob.size(); ++i) {
            blob[0x0006 + i] = std::byte { static_cast<uint8_t>(dt[i]) };
        }

        write_u16le_at(&blob, 0x001A, 5304);
        write_u16le_at(&blob, 0x001C, 7952);
        write_u16le_at(&blob, 0x0030, 2);
        write_u16le_at(&blob, 0x0032, 37);

        const std::string_view ver = "DC7303320222000";
        for (size_t i = 0; i < ver.size() && (0x0034 + i) < blob.size(); ++i) {
            blob[0x0034 + i] = std::byte { static_cast<uint8_t>(ver[i]) };
        }

        std::vector<std::byte> mn;
        append_u16le(&mn, 1);  // entry count
        append_u16le(&mn, 0x3000);
        append_u16le(&mn, 7);  // UNDEFINED
        append_u32le(&mn, static_cast<uint32_t>(blob.size()));
        append_u32le(&mn, 0);  // value offset placeholder (patched by caller)
        append_u32le(&mn, 0);  // next IFD

        mn.insert(mn.end(), blob.begin(), blob.end());
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

TEST(MakerNoteDecode, DecodesSonyMakerNoteByMakeString)
{
    std::vector<std::byte> mn   = make_sony_makernote();
    const std::string_view make = "Sony";
    const uint32_t maker_note_off
        = 57U + static_cast<uint32_t>(make.size());  // see builder layout
    const uint32_t value_off_abs = maker_note_off + 34U;
    write_u32le_at(&mn, 26U, value_off_abs);

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
            exif_key("mk_sony0", 0x0102));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 7U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony0", 0xB020));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "Standard");
    }
}

TEST(MakerNoteDecode, DecodesSonyCipheredTag9050IntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag9050b_ciphered();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050b_0", 0x0026));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        ASSERT_EQ(e.value.kind, MetaValueKind::Array);
        ASSERT_EQ(e.value.elem_type, MetaElementType::U16);
        ASSERT_EQ(e.value.count, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050b_0", 0x003A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x44332211U);
    }
}

TEST(MakerNoteDecode, DecodesSonyCipheredTag2010IntoDerivedIfd)
{
    const std::string_view make = "Sony";
    std::vector<std::byte> mn   = make_sony_makernote_tag2010i_ciphered();
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010i_0", 0x0217));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I16);
        EXPECT_EQ(e.value.data.u64, 0x1234U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010i_0", 0x0252));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag2010i_0", 0x17D0));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 32U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag9050cWhenModelMatchesNewerBodies)
{
    std::vector<std::byte> mn = make_sony_makernote_tag9050b_ciphered();

    const std::string_view make  = "Sony";
    const std::string_view model = "ILCE-7M4";
    std::vector<std::byte> tiff
        = make_test_tiff_with_makernote_and_model(make, model, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050c_0", 0x0026));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 3U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_tag9050c_0", 0x003A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 0x44332211U);
    }
}

TEST(MakerNoteDecode, DecodesSonyTag3000ShotInfoIntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_sony_makernote_tag3000_shotinfo();
    const std::string_view make = "Sony";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_TRUE(patch_sony_makernote_value_offset_in_tiff(&tiff));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x0002));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 94U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x0006));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "2017:02:08 07:07:08");
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x001A));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 5304U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x001C));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 7952U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x0030));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 2U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x0032));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 37U);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_sony_shotinfo_0", 0x0034));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "DC7303320222000");
    }
}

TEST(MakerNoteDecode, DecodesKonicaMinoltaMakerNoteByMakeString)
{
    const std::vector<std::byte> mn = make_minolta_makernote();
    const std::vector<std::byte> tiff
        = make_test_tiff_with_makernote("KONICA MINOLTA", mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_minolta0", 0x0100));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
    EXPECT_EQ(e.value.data.u64, 13U);
}


TEST(MakerNoteDecode, DecodesCanonBinaryDataCameraSettingsIntoDerivedIfd)
{
    std::vector<std::byte> mn         = make_canon_camera_settings_makernote();
    const std::string_view make       = "Canon";
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

TEST(MakerNoteDecode, DecodesCanonBinaryDataCameraSettingsWithAdjustedBase)
{
    std::vector<std::byte> mn
        = make_canon_camera_settings_makernote_adjusted_base();
    const std::string_view make       = "Canon";
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

TEST(MakerNoteDecode,
     DecodesCanonCustomFunctions1DAndPersonalFunctionsIntoDerivedIfds)
{
    std::vector<std::byte> mn   = make_canon_custom_functions1d_makernote();
    const std::string_view make = "Canon";
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
            exif_key("mk_canoncustom_functions1d_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canoncustom_functions1d_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canoncustom_personalfuncs_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 0U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_canoncustom_personalfuncvalues_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 63U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}


TEST(MakerNoteDecode, DecodesCanonMakernoteWithTruncatedDeclaredByteCount)
{
    std::vector<std::byte> mn   = make_canon_truncated_count_makernote();
    const std::string_view make = "Canon";
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    // Declare an unrealistically small MakerNote count to force the decoder
    // to rely on bounds checks against the full EXIF/TIFF buffer.
    ASSERT_TRUE(patch_makernote_count_in_tiff(&tiff, 10));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_canon0", 0x0006));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Text);
    EXPECT_EQ(e.value.text_encoding, TextEncoding::Ascii);
    EXPECT_FALSE(any(e.flags, EntryFlags::Derived));
}


TEST(MakerNoteDecode, DecodesCanonCameraInfoPictureStyleIntoDerivedIfd)
{
    std::vector<std::byte> mn   = make_canon_camera_info_psinfo_makernote();
    const std::string_view make = "Canon";
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
            exif_key("mk_canon_camerainfo_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 42U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
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
    std::vector<std::byte> mn         = make_canon_afinfo2_makernote();
    const std::string_view make       = "Canon";
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

TEST(MakerNoteDecode, DecodesCasioType2MakerNoteFr10VariantLeDirectory)
{
    const std::vector<std::byte> mn = make_casio_type2_makernote_fr10_variant();
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


TEST(MakerNoteDecode, DecodesCasioType2MakerNoteOutOfLineTiffRelativeOffsets)
{
    // Layout is deterministic based on make_test_tiff_with_makernote():
    // maker_note_off = 57 + strlen(make), maker_note_size = 8 + 12 = 20.
    const std::string_view make    = "CASIO";
    const uint32_t maker_note_off  = 57U + static_cast<uint32_t>(make.size());
    const uint32_t maker_note_size = 20U;
    const uint32_t payload_off     = maker_note_off + maker_note_size;

    std::vector<std::byte> mn = make_casio_type2_makernote_out_of_line(
        payload_off);
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_EQ(tiff.size(), payload_off);

    const std::byte payload[6] = {
        std::byte { 0xAA }, std::byte { 0xBB }, std::byte { 0xCC },
        std::byte { 0xDD }, std::byte { 0xEE }, std::byte { 0xFF },
    };
    tiff.insert(tiff.end(), payload, payload + 6);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_casio_type2_0", 0x2000));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Bytes);
    const std::span<const std::byte> got = store.arena().span(
        e.value.data.span);
    ASSERT_GE(got.size(), 6U);
    EXPECT_EQ(got[0], payload[0]);
    EXPECT_EQ(got[1], payload[1]);
    EXPECT_EQ(got[2], payload[2]);
    EXPECT_EQ(got[3], payload[3]);
    EXPECT_EQ(got[4], payload[4]);
    EXPECT_EQ(got[5], payload[5]);
}


TEST(MakerNoteDecode, DecodesCasioFaceInfo1IntoDerivedIfd)
{
    const std::string_view make    = "CASIO";
    const uint32_t maker_note_off  = 57U + static_cast<uint32_t>(make.size());
    const uint32_t maker_note_size = 20U;
    const uint32_t payload_off     = maker_note_off + maker_note_size;
    const uint32_t payload_bytes   = 32U;

    std::vector<std::byte> mn
        = make_casio_type2_makernote_faceinfo1(payload_off, payload_bytes);
    std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);
    ASSERT_EQ(tiff.size(), payload_off);

    std::vector<std::byte> payload(payload_bytes, std::byte { 0 });
    // FaceInfo1: [0]=FacesDetected, [1..4]=big-endian frame size (640x480).
    payload[0] = std::byte { 1 };
    payload[1] = std::byte { 0x02 };
    payload[2] = std::byte { 0x80 };
    payload[3] = std::byte { 0x01 };
    payload[4] = std::byte { 0xE0 };
    tiff.insert(tiff.end(), payload.begin(), payload.end());

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_faceinfo1_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_casio_faceinfo1_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 2U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
        const std::span<const std::byte> dim_bytes = store.arena().span(
            e.value.data.span);
        ASSERT_GE(dim_bytes.size(), 4U);
        uint16_t w = 0;
        uint16_t h = 0;
        std::memcpy(&w, dim_bytes.data() + 0, 2);
        std::memcpy(&h, dim_bytes.data() + 2, 2);
        EXPECT_EQ(w, 640U);
        EXPECT_EQ(h, 480U);
    }
}


TEST(MakerNoteDecode, DecodesCasioQvciApp1Block)
{
    // Minimal JPEG with a single APP1 "QVCI" segment and EOI.
    std::vector<std::byte> jpg;
    jpg.push_back(std::byte { 0xFF });
    jpg.push_back(std::byte { 0xD8 });  // SOI
    jpg.push_back(std::byte { 0xFF });
    jpg.push_back(std::byte { 0xE1 });  // APP1

    std::vector<std::byte> payload(0x90, std::byte { 0 });
    payload[0]    = std::byte { 'Q' };
    payload[1]    = std::byte { 'V' };
    payload[2]    = std::byte { 'C' };
    payload[3]    = std::byte { 'I' };
    payload[0x2c] = std::byte { 3 };  // CasioQuality
    // DateTimeOriginal at 0x4d, "YYYY.MM.DD.HH.MM.SS" (20 bytes).
    const char dt[20] = "2025.01.02.03.04.05";
    std::memcpy(payload.data() + 0x4d, dt, 20);
    const char model[7] = "QV7000";
    std::memcpy(payload.data() + 0x62, model, 6);

    const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
    append_u16be(&jpg, seg_len);
    jpg.insert(jpg.end(), payload.begin(), payload.end());
    jpg.push_back(std::byte { 0xFF });
    jpg.push_back(std::byte { 0xD9 });  // EOI

    MetaStore store;
    std::array<ContainerBlockRef, 8> blocks {};
    std::array<ExifIfdRef, 8> ifds {};
    std::array<std::byte, 1024> scratch {};
    std::array<uint32_t, 64> scratch_idx {};

    ExifDecodeOptions exif_options;
    exif_options.decode_makernote = true;
    PayloadOptions payload_options;
    const SimpleMetaResult res
        = simple_meta_read(std::span<const std::byte>(jpg.data(), jpg.size()),
                           store, blocks, ifds, scratch, scratch_idx,
                           exif_options, payload_options);
    EXPECT_EQ(res.scan.status, ScanStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_casio_qvci_0", 0x002c));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
    EXPECT_EQ(e.value.data.u64, 3U);
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

TEST(MakerNoteDecode, DecodesEmbeddedTiffMakerNoteValuesBeyondDeclaredCount)
{
    // Embedded TIFF header with an out-of-line value stored beyond the declared
    // MakerNote byte count.
    std::vector<std::byte> mn;
    append_bytes(&mn, "II");
    append_u16le(&mn, 42);
    append_u32le(&mn, 8);  // IFD0 offset
    append_u16le(&mn, 1);  // entry count
    append_u16le(&mn, 0x0E22);
    append_u16le(&mn, 3);    // SHORT
    append_u32le(&mn, 4);    // count
    append_u32le(&mn, 100);  // value offset
    append_u32le(&mn, 0);

    while (mn.size() < 100U) {
        mn.push_back(std::byte { 0 });
    }
    append_u16le(&mn, 1);
    append_u16le(&mn, 2);
    append_u16le(&mn, 3);
    append_u16le(&mn, 4);
    EXPECT_GE(mn.size(), 108U);

    std::vector<std::byte> tiff = make_test_tiff_with_makernote("Nikon", mn);
    ASSERT_TRUE(patch_makernote_count_in_tiff(&tiff, 32));

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_nikon0", 0x0E22));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Array);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.count, 4U);
    EXPECT_FALSE(any(e.flags, EntryFlags::Derived));
}

TEST(MakerNoteDecode, DecodesNikonBinarySubdirectories)
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
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_vrinfo_0", 0x0006));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 2U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_vrinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
        const std::string_view v(
            reinterpret_cast<const char*>(
                store.arena().span(e.value.data.span).data()),
            store.arena().span(e.value.data.span).size());
        EXPECT_EQ(v, "0101");
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
}

TEST(MakerNoteDecode, DecodesNikonBinarySubdirectoriesExtended)
{
    const std::vector<std::byte> mn = make_nikon_makernote_with_binary_subdirs();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Canon",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_distortinfo_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_flashinfo0106_0", 0x0006));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.count, 2U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_multiexposure_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U32);
        EXPECT_EQ(e.value.data.u64, 3U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_afinfo2v0100_0", 0x001c));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U8);
        EXPECT_EQ(e.value.data.u64, 1U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_fileinfo_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.data.u64, 99U);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_nikon_retouchinfo_0", 0x0005));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
        EXPECT_EQ(e.value.elem_type, MetaElementType::I8);
        EXPECT_EQ(static_cast<int64_t>(e.value.data.i64), -1);
        EXPECT_TRUE(any(e.flags, EntryFlags::Derived));
    }
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

TEST(MakerNoteDecode, DecodesOlympusMakerNoteWithOlympusSignatureSubIfdOffsets)
{
    const std::vector<std::byte> mn = make_olympus_makernote_olympus_signature();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("OLYMPUS",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 16> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    const std::span<const EntryId> ids = store.find_all(
        exif_key("mk_olympus_main_0", 0x0201));
    ASSERT_EQ(ids.size(), 1U);
    const Entry& e = store.entry(ids[0]);
    EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
    EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
    EXPECT_EQ(e.value.data.u64, 2U);
}

TEST(MakerNoteDecode, DecodesPanasonicBinarySubDirs)
{
    std::vector<std::byte> mn = make_panasonic_makernote_with_subdirs();
    const std::string_view make = "Panasonic";
    const uint32_t maker_note_off
        = 57U + static_cast<uint32_t>(make.size());  // see builder layout

    // MakerNote IFD layout:
    // u16(count=3) + 3*12 bytes + u32(next_ifd) = 42 bytes header.
    const uint32_t header_size = 42U;
    const uint32_t facedet_off = maker_note_off + header_size;
    const uint32_t facerec_off = facedet_off + 10U;
    const uint32_t time_off    = facerec_off + 52U;

    // Patch UNDEFINED value offsets to be absolute (outer TIFF-relative).
    write_u32le_at(&mn, 10U, facedet_off);
    write_u32le_at(&mn, 22U, facerec_off);
    write_u32le_at(&mn, 34U, time_off);

    const std::vector<std::byte> tiff = make_test_tiff_with_makernote(make, mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();

    {
        const std::span<const EntryId> ids
            = store.find_all(exif_key("mk_panasonic_facedetinfo_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 1U);
    }
    {
        const std::span<const EntryId> ids
            = store.find_all(exif_key("mk_panasonic_facedetinfo_0", 0x0001));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Array);
        EXPECT_EQ(e.value.elem_type, MetaElementType::U16);
        EXPECT_EQ(e.value.count, 4U);
    }
    {
        const std::span<const EntryId> ids
            = store.find_all(exif_key("mk_panasonic_facerecinfo_0", 0x0004));
        ASSERT_EQ(ids.size(), 1U);
        const Entry& e = store.entry(ids[0]);
        EXPECT_EQ(e.value.kind, MetaValueKind::Text);
    }
    {
        const std::span<const EntryId> ids
            = store.find_all(exif_key("mk_panasonic_timeinfo_0", 0x0010));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 123U);
    }
}

TEST(MakerNoteDecode, DecodesPanasonicType2MakerNote)
{
    const std::vector<std::byte> mn   = make_panasonic_type2_makernote();
    const std::vector<std::byte> tiff = make_test_tiff_with_makernote("Panasonic",
                                                                      mn);

    MetaStore store;
    std::array<ExifIfdRef, 8> ifds {};
    ExifDecodeOptions options;
    options.decode_makernote   = true;
    const ExifDecodeResult res = decode_exif_tiff(tiff, store, ifds, options);
    EXPECT_EQ(res.status, ExifDecodeStatus::Ok);

    store.finalize();
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_type2_0", 0x0000));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.kind, MetaValueKind::Text);
    }
    {
        const std::span<const EntryId> ids = store.find_all(
            exif_key("mk_panasonic_type2_0", 0x0003));
        ASSERT_EQ(ids.size(), 1U);
        EXPECT_EQ(store.entry(ids[0]).value.data.u64, 42U);
    }
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
