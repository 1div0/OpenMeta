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


    static void append_u16be(std::vector<std::byte>* out, uint16_t v)
    {
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
    }


    static void append_jpeg_segment(std::vector<std::byte>* out,
                                    uint16_t marker,
                                    std::span<const std::byte> payload)
    {
        out->push_back(std::byte { 0xFF });
        out->push_back(std::byte { static_cast<uint8_t>(marker & 0xFF) });
        const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2);
        append_u16be(out, seg_len);
        out->insert(out->end(), payload.begin(), payload.end());
    }


    static std::string_view arena_str(const ByteArena& arena, ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }


    TEST(Flir, SimpleMetaReadJpegFff)
    {
        // Minimal JPEG with a single APP1 FLIR segment containing:
        // - FLIR preamble (8 bytes)
        // - FFF header (0x40)
        // - 1 record directory entry (0x20) pointing to a CameraInfo record.
        std::vector<std::byte> jpeg;
        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD8 });

        std::vector<std::byte> flir;
        append_bytes(&flir, "FLIR");
        flir.push_back(std::byte { 0x00 });
        flir.push_back(std::byte { 0x01 });
        flir.push_back(std::byte { 0x00 });  // part index
        flir.push_back(std::byte { 0x00 });  // total-1 (1 part)

        std::vector<std::byte> fff;
        fff.resize(0x90, std::byte { 0x00 });
        fff[0x00] = std::byte { 'F' };
        fff[0x01] = std::byte { 'F' };
        fff[0x02] = std::byte { 'F' };
        fff[0x03] = std::byte { 0x00 };
        // CreatorSoftware at 0x04 (string[16]).
        fff[0x04] = std::byte { 'F' };
        fff[0x05] = std::byte { 'L' };
        fff[0x06] = std::byte { 'I' };
        fff[0x07] = std::byte { 'R' };
        // Version (u32) at 0x14, big-endian.
        fff[0x14] = std::byte { 0x00 };
        fff[0x15] = std::byte { 0x00 };
        fff[0x16] = std::byte { 0x00 };
        fff[0x17] = std::byte { 0x64 };  // 100
        // Directory offset at 0x18 -> 0x40.
        fff[0x18] = std::byte { 0x00 };
        fff[0x19] = std::byte { 0x00 };
        fff[0x1A] = std::byte { 0x00 };
        fff[0x1B] = std::byte { 0x40 };
        // Directory count at 0x1c -> 1.
        fff[0x1C] = std::byte { 0x00 };
        fff[0x1D] = std::byte { 0x00 };
        fff[0x1E] = std::byte { 0x00 };
        fff[0x1F] = std::byte { 0x01 };

        // Record directory entry at 0x40.
        // rec_type = 0x20 (CameraInfo)
        fff[0x40] = std::byte { 0x00 };
        fff[0x41] = std::byte { 0x20 };
        // rec_subtype = 1
        fff[0x42] = std::byte { 0x00 };
        fff[0x43] = std::byte { 0x01 };
        // rec_version = 0x64
        fff[0x44] = std::byte { 0x00 };
        fff[0x45] = std::byte { 0x00 };
        fff[0x46] = std::byte { 0x00 };
        fff[0x47] = std::byte { 0x64 };
        // index id = 1
        fff[0x48] = std::byte { 0x00 };
        fff[0x49] = std::byte { 0x00 };
        fff[0x4A] = std::byte { 0x00 };
        fff[0x4B] = std::byte { 0x01 };
        // record offset = 0x60
        fff[0x4C] = std::byte { 0x00 };
        fff[0x4D] = std::byte { 0x00 };
        fff[0x4E] = std::byte { 0x00 };
        fff[0x4F] = std::byte { 0x60 };
        // record length = 0x30
        fff[0x50] = std::byte { 0x00 };
        fff[0x51] = std::byte { 0x00 };
        fff[0x52] = std::byte { 0x00 };
        fff[0x53] = std::byte { 0x30 };

        // CameraInfo record (0x30 bytes) at 0x60.
        // Byte-order check u16 at offset 0: 0x0002 (big-endian).
        fff[0x60] = std::byte { 0x00 };
        fff[0x61] = std::byte { 0x02 };
        // Emissivity float bits at offset 0x20 within record. Leave as 0.

        flir.insert(flir.end(), fff.begin(), fff.end());
        append_jpeg_segment(&jpeg, 0xFFE1, flir);

        jpeg.push_back(std::byte { 0xFF });
        jpeg.push_back(std::byte { 0xD9 });

        MetaStore store;

        std::array<ContainerBlockRef, 16> blocks {};
        std::array<ExifIfdRef, 16> ifds {};
        std::array<std::byte, 4096> payload {};
        std::array<uint32_t, 64> scratch_indices {};

        ExifDecodeOptions exif_opts;
        exif_opts.decode_makernote = true;
        PayloadOptions payload_opts;

        const SimpleMetaResult res = simple_meta_read(
            std::span<const std::byte>(jpeg.data(), jpeg.size()), store, blocks,
            ifds, payload, scratch_indices, exif_opts, payload_opts);

        EXPECT_EQ(res.scan.status, ScanStatus::Ok);

        bool found = false;
        for (const Entry& e : store.entries()) {
            if (e.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            if (e.key.data.exif_tag.tag != 0x0020u) {
                continue;
            }
            const std::string_view ifd
                = arena_str(store.arena(), e.key.data.exif_tag.ifd);
            if (ifd == "mk_flir_fff_camerainfo_0") {
                found = true;
                EXPECT_EQ(e.value.kind, MetaValueKind::Scalar);
                EXPECT_EQ(e.value.elem_type, MetaElementType::F32);
                break;
            }
        }
        EXPECT_TRUE(found);
    }

}  // namespace
}  // namespace openmeta
