#include "openmeta/simple_meta.h"

#include "bmff_fields_decode_internal.h"
#include "crw_ciff_decode_internal.h"
#include "exif_tiff_decode_internal.h"

#include "openmeta/exr_decode.h"
#include "openmeta/icc_decode.h"
#include "openmeta/iptc_iim_decode.h"
#include "openmeta/photoshop_irb_decode.h"
#include "openmeta/xmp_decode.h"

#include <array>
#include <cmath>
#include <cstring>

namespace openmeta {
namespace {

    static uint8_t u8(std::byte b) noexcept { return static_cast<uint8_t>(b); }


    static void merge_payload_result(PayloadResult* out,
                                     const PayloadResult& in) noexcept
    {
        if (in.status == PayloadStatus::Ok) {
            return;
        }
        if (in.needed > out->needed) {
            out->needed = in.needed;
        }

        // Prefer actionable outcomes:
        // LimitExceeded > OutputTruncated > Unsupported > Malformed.
        if (out->status == PayloadStatus::LimitExceeded) {
            return;
        }
        if (in.status == PayloadStatus::LimitExceeded) {
            out->status = in.status;
            return;
        }
        if (out->status == PayloadStatus::OutputTruncated) {
            return;
        }
        if (in.status == PayloadStatus::OutputTruncated) {
            out->status = in.status;
            return;
        }
        if (out->status == PayloadStatus::Unsupported) {
            return;
        }
        if (in.status == PayloadStatus::Unsupported) {
            out->status = in.status;
            return;
        }
        if (out->status == PayloadStatus::Malformed) {
            return;
        }
        if (in.status == PayloadStatus::Malformed) {
            out->status = in.status;
            return;
        }
    }


    static bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4 > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 0])) << 24;
        v |= static_cast<uint32_t>(u8(bytes[offset + 1])) << 16;
        v |= static_cast<uint32_t>(u8(bytes[offset + 2])) << 8;
        v |= static_cast<uint32_t>(u8(bytes[offset + 3])) << 0;
        *out = v;
        return true;
    }

    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 8)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 0);
        *out = v;
        return true;
    }

    static bool read_u16le(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 0)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 8);
        *out = v;
        return true;
    }

    static bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4 > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 0])) << 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 1])) << 8;
        v |= static_cast<uint32_t>(u8(bytes[offset + 2])) << 16;
        v |= static_cast<uint32_t>(u8(bytes[offset + 3])) << 24;
        *out = v;
        return true;
    }

    static uint32_t f32_bits_from_float(float v) noexcept
    {
        uint32_t bits = 0;
        static_assert(sizeof(bits) == sizeof(v));
        std::memcpy(&bits, &v, sizeof(bits));
        return bits;
    }

    static float f32_from_bits(uint32_t bits) noexcept
    {
        float v = 0.0f;
        static_assert(sizeof(bits) == sizeof(v));
        std::memcpy(&v, &bits, sizeof(v));
        return v;
    }

    static bool float_plausible(float v, float lo, float hi) noexcept
    {
        return std::isfinite(v) && v >= lo && v <= hi;
    }

    static uint64_t find_magic_u32be(std::span<const std::byte> bytes,
                                     uint32_t magic) noexcept
    {
        if (bytes.size() < 4) {
            return UINT64_MAX;
        }
        for (uint64_t i = 0; i + 4U <= bytes.size(); ++i) {
            uint32_t v = 0;
            if (read_u32be(bytes, i, &v) && v == magic) {
                return i;
            }
        }
        return UINT64_MAX;
    }

    static bool parse_dji_thermal_params(std::span<const std::byte> app4,
                                         MetaStore& store,
                                         const ExifDecodeLimits& limits,
                                         ExifDecodeResult* status_out) noexcept
    {
        // ExifTool reference tables:
        // - ThermalParams:  magic 0xaa551206, u16 values at offsets 0x44..0x4c
        // - ThermalParams2: float values (ambient/dist/emiss/rh/refl) + IDString
        // - ThermalParams3: magic 0xaa553800, u16 values at offsets 0x04..0x0a
        //
        // Real files often store these blocks at offset 32 within APP4.

        bool any = false;

        // 1) ThermalParams3 (magic AA 55 38 00).
        const uint64_t m3 = find_magic_u32be(app4, 0xAA553800U);
        if (m3 != UINT64_MAX && m3 + 0x0cU <= app4.size()) {
            uint16_t rh_raw = 0;
            uint16_t od_raw = 0;
            uint16_t em_raw = 0;
            uint16_t rt_raw = 0;
            if (read_u16le(app4, m3 + 0x04U, &rh_raw)
                && read_u16le(app4, m3 + 0x06U, &od_raw)
                && read_u16le(app4, m3 + 0x08U, &em_raw)
                && read_u16le(app4, m3 + 0x0aU, &rt_raw)) {
                const float od = float(od_raw) / 10.0f;
                const float em = float(em_raw) / 100.0f;
                const float rt = float(rt_raw) / 10.0f;

                char scratch[64];
                const std::string_view ifd_name
                    = exif_internal::make_mk_subtable_ifd_token(
                        "mk_dji", "thermalparams3", 0,
                        std::span<char>(scratch));
                if (!ifd_name.empty()) {
                    const uint16_t tags_out[4] = { 0x0004, 0x0006, 0x0008,
                                                   0x000a };
                    const MetaValue vals_out[4]
                        = { make_u16(rh_raw),
                            make_f32_bits(f32_bits_from_float(od)),
                            make_f32_bits(f32_bits_from_float(em)),
                            make_f32_bits(f32_bits_from_float(rt)) };
                    exif_internal::emit_bin_dir_entries(
                        ifd_name, store, std::span<const uint16_t>(tags_out, 4),
                        std::span<const MetaValue>(vals_out, 4), limits,
                        status_out);
                    any = true;
                }
            }
        }

        // 2) ThermalParams (magic AA 55 12 06).
        const uint64_t m1 = find_magic_u32be(app4, 0xAA551206U);
        if (m1 != UINT64_MAX && m1 + 0x4eU <= app4.size()) {
            uint16_t od = 0;
            uint16_t rh = 0;
            uint16_t em = 0;
            uint16_t rf = 0;
            uint16_t at = 0;
            if (read_u16le(app4, m1 + 0x44U, &od)
                && read_u16le(app4, m1 + 0x46U, &rh)
                && read_u16le(app4, m1 + 0x48U, &em)
                && read_u16le(app4, m1 + 0x4aU, &rf)
                && read_u16le(app4, m1 + 0x4cU, &at)) {
                char scratch[64];
                const std::string_view ifd_name
                    = exif_internal::make_mk_subtable_ifd_token(
                        "mk_dji", "thermalparams", 0, std::span<char>(scratch));
                if (!ifd_name.empty()) {
                    const uint16_t tags_out[5]  = { 0x0044, 0x0046, 0x0048,
                                                    0x004a, 0x004c };
                    const MetaValue vals_out[5] = { make_u16(od), make_u16(rh),
                                                    make_u16(em), make_u16(rf),
                                                    make_u16(at) };
                    exif_internal::emit_bin_dir_entries(
                        ifd_name, store, std::span<const uint16_t>(tags_out, 5),
                        std::span<const MetaValue>(vals_out, 5), limits,
                        status_out);
                    any = true;
                }
            }
        }

        // 3) ThermalParams2 (float fields + IDString, no magic in observed files).
        // Try base offsets commonly seen in the wild.
        const uint64_t bases[2] = { 0U, 32U };
        for (uint32_t bi = 0; bi < 2; ++bi) {
            const uint64_t base = bases[bi];
            if (base + 0x14U > app4.size()) {
                continue;
            }

            uint32_t bits_at = 0;
            uint32_t bits_od = 0;
            uint32_t bits_em = 0;
            uint32_t bits_rh = 0;
            uint32_t bits_rt = 0;
            if (!read_u32le(app4, base + 0x00U, &bits_at)
                || !read_u32le(app4, base + 0x04U, &bits_od)
                || !read_u32le(app4, base + 0x08U, &bits_em)
                || !read_u32le(app4, base + 0x0cU, &bits_rh)
                || !read_u32le(app4, base + 0x10U, &bits_rt)) {
                continue;
            }

            const float at = f32_from_bits(bits_at);
            const float od = f32_from_bits(bits_od);
            const float em = f32_from_bits(bits_em);
            const float rh = f32_from_bits(bits_rh);
            const float rt = f32_from_bits(bits_rt);

            // Plausibility gates to avoid false positives on unrelated APP4 data.
            if (!float_plausible(at, -100.0f, 300.0f)
                || !float_plausible(rt, -100.0f, 300.0f)
                || !float_plausible(od, 0.0f, 10000.0f)
                || !float_plausible(em, 0.0f, 2.0f)
                || !float_plausible(rh, 0.0f, 1.0f)) {
                continue;
            }

            char scratch[64];
            const std::string_view ifd_name
                = exif_internal::make_mk_subtable_ifd_token(
                    "mk_dji", "thermalparams2", 0, std::span<char>(scratch));
            if (ifd_name.empty()) {
                break;
            }

            uint16_t tags_out[6] = { 0x0000, 0x0004, 0x0008,
                                     0x000c, 0x0010, 0x0065 };
            MetaValue vals_out[6]
                = { make_f32_bits(bits_at), make_f32_bits(bits_od),
                    make_f32_bits(bits_em), make_f32_bits(bits_rh),
                    make_f32_bits(bits_rt), MetaValue {} };

            if (base + 0x65U + 16U <= app4.size()) {
                const std::span<const std::byte> raw
                    = app4.subspan(static_cast<size_t>(base + 0x65U), 16U);
                size_t n = 0;
                while (n < raw.size() && raw[n] != std::byte { 0 }) {
                    n += 1;
                }
                const std::string_view s(reinterpret_cast<const char*>(
                                             raw.data()),
                                         n);
                vals_out[5] = make_text(store.arena(), s, TextEncoding::Ascii);
            }

            exif_internal::emit_bin_dir_entries(
                ifd_name, store, std::span<const uint16_t>(tags_out, 6),
                std::span<const MetaValue>(vals_out, 6), limits, status_out);
            any = true;
            break;
        }

        return any;
    }

    static bool parse_classic_tiff_header(std::span<const std::byte> bytes,
                                          TiffConfig* out_cfg,
                                          uint64_t* out_ifd0_off) noexcept
    {
        if (!out_cfg || !out_ifd0_off) {
            return false;
        }
        if (bytes.size() < 8) {
            return false;
        }

        const uint8_t b0 = u8(bytes[0]);
        const uint8_t b1 = u8(bytes[1]);
        const bool le    = (b0 == 'I' && b1 == 'I');
        const bool be    = (b0 == 'M' && b1 == 'M');
        if (!le && !be) {
            return false;
        }

        out_cfg->le      = le;
        out_cfg->bigtiff = false;

        uint16_t magic = 0;
        if (le) {
            if (!read_u16le(bytes, 2, &magic)) {
                return false;
            }
        } else {
            if (!read_u16be(bytes, 2, &magic)) {
                return false;
            }
        }
        if (magic != 42) {
            return false;
        }

        uint32_t ifd0_off = 0;
        if (le) {
            if (!read_u32le(bytes, 4, &ifd0_off)) {
                return false;
            }
        } else {
            if (!read_u32be(bytes, 4, &ifd0_off)) {
                return false;
            }
        }

        if (ifd0_off > static_cast<uint64_t>(bytes.size())) {
            return false;
        }
        *out_ifd0_off = static_cast<uint64_t>(ifd0_off);
        return true;
    }


    static void merge_exif_status(ExifDecodeStatus* out,
                                  ExifDecodeStatus in) noexcept
    {
        // Aggregate results across multiple EXIF blocks:
        // - Treat `Unsupported` as "no usable EXIF in this block".
        // - Promote to the worst non-Unsupported status seen.
        if (*out == ExifDecodeStatus::LimitExceeded) {
            return;
        }
        if (in == ExifDecodeStatus::LimitExceeded) {
            *out = in;
            return;
        }
        if (*out == ExifDecodeStatus::Malformed) {
            return;
        }
        if (in == ExifDecodeStatus::Malformed) {
            *out = in;
            return;
        }
        if (*out == ExifDecodeStatus::OutputTruncated) {
            return;
        }
        if (in == ExifDecodeStatus::OutputTruncated) {
            *out = in;
            return;
        }
        if (*out == ExifDecodeStatus::Ok) {
            return;
        }
        if (in == ExifDecodeStatus::Ok) {
            *out = in;
            return;
        }
    }


    static void merge_xmp_status(XmpDecodeStatus* out,
                                 XmpDecodeStatus in) noexcept
    {
        if (!out) {
            return;
        }
        if (*out == XmpDecodeStatus::LimitExceeded) {
            return;
        }
        if (in == XmpDecodeStatus::LimitExceeded) {
            *out = in;
            return;
        }
        if (*out == XmpDecodeStatus::Malformed) {
            return;
        }
        if (in == XmpDecodeStatus::Malformed) {
            *out = in;
            return;
        }
        if (*out == XmpDecodeStatus::OutputTruncated) {
            return;
        }
        if (in == XmpDecodeStatus::OutputTruncated) {
            *out = in;
            return;
        }
        // `Unsupported` means "no usable XMP in this block".
        // Promote to the best status seen across all XMP blocks.
        if (*out == XmpDecodeStatus::Ok) {
            return;
        }
        if (in == XmpDecodeStatus::Ok) {
            *out = in;
            return;
        }
        if (*out == XmpDecodeStatus::Unsupported) {
            return;
        }
        if (in == XmpDecodeStatus::Unsupported) {
            *out = in;
        }
    }


    static PayloadResult
    get_block_bytes(std::span<const std::byte> file_bytes,
                    std::span<const ContainerBlockRef> blocks,
                    uint32_t block_index, std::span<std::byte> payload,
                    std::span<uint32_t> payload_scratch_indices,
                    const PayloadOptions& payload_options,
                    std::span<const std::byte>* out) noexcept
    {
        PayloadResult res;
        if (!out || block_index >= blocks.size()) {
            res.status = PayloadStatus::Malformed;
            return res;
        }
        const ContainerBlockRef& block = blocks[block_index];

        if (block.part_count <= 1U
            && block.compression == BlockCompression::None
            && block.chunking != BlockChunking::GifSubBlocks) {
            const uint64_t end = static_cast<uint64_t>(file_bytes.size());
            if (block.data_offset > end
                || block.data_size > end - block.data_offset) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
            *out = file_bytes.subspan(static_cast<size_t>(block.data_offset),
                                      static_cast<size_t>(block.data_size));
            res.status  = PayloadStatus::Ok;
            res.written = block.data_size;
            res.needed  = block.data_size;
            return res;
        }

        const PayloadResult payload_res
            = extract_payload(file_bytes, blocks, block_index, payload,
                              payload_scratch_indices, payload_options);
        if (payload_res.status != PayloadStatus::Ok) {
            return payload_res;
        }
        *out = std::span<const std::byte>(payload.data(),
                                          static_cast<size_t>(
                                              payload_res.written));
        return payload_res;
    }

}  // namespace

SimpleMetaResult
simple_meta_read(std::span<const std::byte> file_bytes, MetaStore& store,
                 std::span<ContainerBlockRef> out_blocks,
                 std::span<ExifIfdRef> out_ifds, std::span<std::byte> payload,
                 std::span<uint32_t> payload_scratch_indices,
                 const ExifDecodeOptions& exif_options,
                 const PayloadOptions& payload_options) noexcept
{
    SimpleMetaResult result;
    result.scan            = scan_auto(file_bytes, out_blocks);
    result.payload.status  = PayloadStatus::Ok;
    result.payload.written = 0;
    result.payload.needed  = 0;

    // Container-derived fields (currently: ISO-BMFF/HEIF/AVIF/CR3).
    bmff_internal::decode_bmff_derived_fields(file_bytes, store);

    ExifDecodeResult exif;
    exif.status          = ExifDecodeStatus::Unsupported;
    exif.ifds_written    = 0;
    exif.ifds_needed     = 0;
    exif.entries_decoded = 0;

    XmpDecodeResult xmp;
    xmp.status          = XmpDecodeStatus::Unsupported;
    xmp.entries_decoded = 0;

    ExrDecodeResult exr;
    exr.status          = ExrDecodeStatus::Unsupported;
    exr.parts_decoded   = 0;
    exr.entries_decoded = 0;

    uint32_t ifd_write_pos        = 0;
    uint32_t casio_qvci_index     = 0;
    bool any_exif                 = false;
    bool any_xmp                  = false;
    const uint32_t blocks_written = (result.scan.written < out_blocks.size())
                                        ? result.scan.written
                                        : static_cast<uint32_t>(
                                              out_blocks.size());
    const std::span<const ContainerBlockRef> blocks_view(out_blocks.data(),
                                                         static_cast<size_t>(
                                                             blocks_written));
    for (uint32_t i = 0; i < blocks_written; ++i) {
        const ContainerBlockRef& block = out_blocks[i];
        if (block.part_count > 1U && block.part_index != 0U) {
            continue;
        }

        std::span<const std::byte> block_bytes;
        const PayloadResult payload_one
            = get_block_bytes(file_bytes, blocks_view, i, payload,
                              payload_scratch_indices, payload_options,
                              &block_bytes);
        merge_payload_result(&result.payload, payload_one);
        if (payload_one.status != PayloadStatus::Ok) {
            if (block.kind == ContainerBlockKind::Exif
                || (block.kind == ContainerBlockKind::CompressedMetadata
                    && block.compression == BlockCompression::Brotli
                    && block.aux_u32 == fourcc('E', 'x', 'i', 'f'))) {
                switch (payload_one.status) {
                case PayloadStatus::Ok: break;
                case PayloadStatus::OutputTruncated:
                    merge_exif_status(&exif.status,
                                      ExifDecodeStatus::OutputTruncated);
                    break;
                case PayloadStatus::Unsupported:
                    merge_exif_status(&exif.status,
                                      ExifDecodeStatus::Unsupported);
                    break;
                case PayloadStatus::Malformed:
                    merge_exif_status(&exif.status,
                                      ExifDecodeStatus::Malformed);
                    break;
                case PayloadStatus::LimitExceeded:
                    merge_exif_status(&exif.status,
                                      ExifDecodeStatus::LimitExceeded);
                    break;
                }
            }
            continue;
        }

        if (block.kind == ContainerBlockKind::Exif) {
            // CR3: some Canon metadata is stored in a dedicated TIFF stream
            // (`CMT3`) rather than in the standard MakerNote tag (0x927C).
            // When MakerNote decoding is enabled, decode that directory as a
            // Canon MakerNote block and expand known BinaryData subtables.
            if (block.format == ContainerFormat::Cr3
                && block.id == fourcc('C', 'M', 'T', '3')) {
                if (!exif_options.decode_makernote) {
                    continue;
                }

                any_exif = true;

                TiffConfig cfg;
                uint64_t ifd0_off = 0;
                if (parse_classic_tiff_header(block_bytes, &cfg, &ifd0_off)
                    && ifd0_off < block_bytes.size()) {
                    ExifDecodeResult one;
                    one.status          = ExifDecodeStatus::Ok;
                    one.ifds_written    = 0;
                    one.ifds_needed     = 0;
                    one.entries_decoded = 0;

                    ExifDecodeOptions mn_opts        = exif_options;
                    mn_opts.decode_printim           = false;
                    mn_opts.decode_makernote         = false;
                    mn_opts.tokens.ifd_prefix        = "mk_canon";
                    mn_opts.tokens.subifd_prefix     = "mk_canon_subifd";
                    mn_opts.tokens.exif_ifd_token    = "mk_canon_exififd";
                    mn_opts.tokens.gps_ifd_token     = "mk_canon_gpsifd";
                    mn_opts.tokens.interop_ifd_token = "mk_canon_interopifd";

                    const uint64_t bytes_rem = static_cast<uint64_t>(
                        block_bytes.size() - static_cast<size_t>(ifd0_off));
                    if (exif_internal::decode_canon_makernote(
                            cfg, block_bytes, ifd0_off, bytes_rem, "mk_canon0",
                            store, mn_opts, &one)) {
                        merge_exif_status(&exif.status, one.status);
                        exif.entries_decoded += one.entries_decoded;
                        continue;
                    }

                    // Fallback: decode the TIFF stream into mk_canon* tags
                    // without BinaryData expansion.
                    std::span<ExifIfdRef> ifd_slice;
                    if (ifd_write_pos < out_ifds.size()) {
                        ifd_slice = out_ifds.subspan(ifd_write_pos);
                    }

                    const ExifDecodeResult fallback
                        = decode_exif_tiff(block_bytes, store, ifd_slice,
                                           mn_opts);
                    merge_exif_status(&exif.status, fallback.status);
                    exif.ifds_needed += fallback.ifds_needed;
                    exif.entries_decoded += fallback.entries_decoded;

                    const uint32_t room     = (ifd_write_pos < out_ifds.size())
                                                  ? static_cast<uint32_t>(
                                                    out_ifds.size()
                                                    - ifd_write_pos)
                                                  : 0U;
                    const uint32_t advanced = (fallback.ifds_written < room)
                                                  ? fallback.ifds_written
                                                  : room;
                    ifd_write_pos += advanced;
                    exif.ifds_written = ifd_write_pos;
                }
                continue;
            }

            any_exif = true;

            std::span<ExifIfdRef> ifd_slice;
            if (ifd_write_pos < out_ifds.size()) {
                ifd_slice = out_ifds.subspan(ifd_write_pos);
            }

            const size_t entry_start = store.entries().size();
            const ExifDecodeResult one
                = decode_exif_tiff(block_bytes, store, ifd_slice, exif_options);
            const size_t entry_end = store.entries().size();
            merge_exif_status(&exif.status, one.status);
            exif.ifds_needed += one.ifds_needed;
            exif.entries_decoded += one.entries_decoded;

            const uint32_t room     = (ifd_write_pos < out_ifds.size())
                                          ? static_cast<uint32_t>(out_ifds.size()
                                                                  - ifd_write_pos)
                                          : 0U;
            const uint32_t advanced = (one.ifds_written < room)
                                          ? one.ifds_written
                                          : room;
            ifd_write_pos += advanced;
            exif.ifds_written = ifd_write_pos;

            // Some TIFF-based RAW formats store an embedded JPEG preview as a
            // byte blob within a TIFF tag (for example, Panasonic RW2
            // `JpgFromRaw` tag 0x002E). ExifTool reports many common EXIF tags
            // from this preview; decode best-effort when enabled.
            if (exif_options.decode_embedded_containers
                && entry_end > entry_start) {
                // Phase 1: collect candidate blobs without mutating the arena.
                std::array<ByteSpan, 8> candidates {};
                uint32_t cand_count                  = 0;
                const std::span<const Entry> entries = store.entries();
                const size_t scan_end = (entry_end < entries.size())
                                            ? entry_end
                                            : entries.size();
                for (size_t ei = entry_start;
                     ei < scan_end && cand_count < candidates.size(); ++ei) {
                    const Entry& e = entries[ei];
                    if (e.key.kind != MetaKeyKind::ExifTag) {
                        continue;
                    }
                    if (e.key.data.exif_tag.tag != 0x002EU) {
                        continue;
                    }
                    if (any(e.flags,
                            EntryFlags::Truncated | EntryFlags::Unreadable)) {
                        continue;
                    }
                    const bool ok_kind
                        = (e.value.kind == MetaValueKind::Bytes)
                          || (e.value.kind == MetaValueKind::Array
                              && e.value.elem_type == MetaElementType::U8);
                    if (!ok_kind || e.value.count < 2) {
                        continue;
                    }
                    candidates[cand_count++] = e.value.data.span;
                }

                // Phase 2: copy + decode each embedded JPEG.
                for (uint32_t ci = 0; ci < cand_count; ++ci) {
                    const std::span<const std::byte> blob = store.arena().span(
                        candidates[ci]);
                    if (blob.size() < 2 || u8(blob[0]) != 0xFFU
                        || u8(blob[1]) != 0xD8U) {
                        continue;
                    }
                    if (blob.size() > payload.size()) {
                        merge_exif_status(&exif.status,
                                          ExifDecodeStatus::OutputTruncated);
                        continue;
                    }

                    std::memcpy(payload.data(), blob.data(), blob.size());
                    const std::span<const std::byte> jpeg_bytes(payload.data(),
                                                                blob.size());

                    std::array<ContainerBlockRef, 64> embed_blocks {};
                    const ScanResult scan_embed = scan_jpeg(jpeg_bytes,
                                                            embed_blocks);
                    if (scan_embed.status == ScanStatus::Malformed) {
                        merge_exif_status(&exif.status,
                                          ExifDecodeStatus::Malformed);
                        continue;
                    }
                    if (scan_embed.status == ScanStatus::OutputTruncated) {
                        merge_exif_status(&exif.status,
                                          ExifDecodeStatus::OutputTruncated);
                    }

                    const uint32_t embed_written
                        = (scan_embed.written < embed_blocks.size())
                              ? scan_embed.written
                              : static_cast<uint32_t>(embed_blocks.size());
                    for (uint32_t bi = 0; bi < embed_written; ++bi) {
                        const ContainerBlockRef& b = embed_blocks[bi];
                        if (b.part_count > 1U && b.part_index != 0U) {
                            continue;
                        }
                        if (b.data_offset > jpeg_bytes.size()
                            || b.data_size
                                   > jpeg_bytes.size() - b.data_offset) {
                            merge_exif_status(&exif.status,
                                              ExifDecodeStatus::Malformed);
                            continue;
                        }
                        const std::span<const std::byte> inner
                            = jpeg_bytes.subspan(
                                static_cast<size_t>(b.data_offset),
                                static_cast<size_t>(b.data_size));

                        if (b.kind == ContainerBlockKind::Exif) {
                            any_exif = true;

                            ExifDecodeOptions embed_opts = exif_options;
                            embed_opts.decode_makernote  = false;
                            embed_opts.decode_printim    = false;
                            embed_opts.decode_embedded_containers = false;

                            std::span<ExifIfdRef> embed_ifds;
                            if (ifd_write_pos < out_ifds.size()) {
                                embed_ifds = out_ifds.subspan(ifd_write_pos);
                            }

                            const ExifDecodeResult inner_res
                                = decode_exif_tiff(inner, store, embed_ifds,
                                                   embed_opts);
                            merge_exif_status(&exif.status, inner_res.status);
                            exif.ifds_needed += inner_res.ifds_needed;
                            exif.entries_decoded += inner_res.entries_decoded;

                            const uint32_t inner_room
                                = (ifd_write_pos < out_ifds.size())
                                      ? static_cast<uint32_t>(out_ifds.size()
                                                              - ifd_write_pos)
                                      : 0U;
                            const uint32_t inner_advanced
                                = (inner_res.ifds_written < inner_room)
                                      ? inner_res.ifds_written
                                      : inner_room;
                            ifd_write_pos += inner_advanced;
                            exif.ifds_written = ifd_write_pos;
                        } else if (b.kind == ContainerBlockKind::Xmp) {
                            any_xmp                  = true;
                            const XmpDecodeResult xr = decode_xmp_packet(inner,
                                                                         store);
                            merge_xmp_status(&xmp.status, xr.status);
                            xmp.entries_decoded += xr.entries_decoded;
                        }
                    }
                }
            }
        } else if (block.kind == ContainerBlockKind::Mpf) {
            // JPEG APP2 MPF: TIFF-IFD stream used by MPO (multi-picture) files.
            // Decode as EXIF/TIFF tags into a separate IFD token namespace.
            std::array<ExifIfdRef, 64> mpf_ifds;
            ExifDecodeOptions mpf_options        = exif_options;
            mpf_options.tokens.ifd_prefix        = "mpf";
            mpf_options.tokens.subifd_prefix     = "mpf_subifd";
            mpf_options.tokens.exif_ifd_token    = "mpf_exififd";
            mpf_options.tokens.gps_ifd_token     = "mpf_gpsifd";
            mpf_options.tokens.interop_ifd_token = "mpf_interopifd";
            (void)decode_exif_tiff(block_bytes, store,
                                   std::span<ExifIfdRef>(mpf_ifds.data(),
                                                         mpf_ifds.size()),
                                   mpf_options);
        } else if (block.kind == ContainerBlockKind::Ciff) {
            any_exif = true;

            ExifDecodeResult one;
            one.status          = ExifDecodeStatus::Ok;
            one.ifds_written    = 0;
            one.ifds_needed     = 0;
            one.entries_decoded = 0;

            if (ciff_internal::decode_crw_ciff(block_bytes, store,
                                               exif_options.limits, &one)) {
                merge_exif_status(&exif.status, one.status);
                exif.entries_decoded += one.entries_decoded;
            } else {
                merge_exif_status(&exif.status, one.status);
            }
        } else if (block.kind == ContainerBlockKind::Xmp
                   || block.kind == ContainerBlockKind::XmpExtended) {
            any_xmp                   = true;
            const XmpDecodeResult one = decode_xmp_packet(block_bytes, store);
            merge_xmp_status(&xmp.status, one.status);
            xmp.entries_decoded += one.entries_decoded;
        } else if (block.kind == ContainerBlockKind::Icc) {
            (void)decode_icc_profile(block_bytes, store);
        } else if (block.kind == ContainerBlockKind::PhotoshopIrB) {
            (void)decode_photoshop_irb(block_bytes, store);
        } else if (block.kind == ContainerBlockKind::IptcIim) {
            (void)decode_iptc_iim(block_bytes, store);
        } else if (block.kind == ContainerBlockKind::MakerNote) {
            if (!exif_options.decode_makernote) {
                continue;
            }

            // JPEG APP4: DJI thermal parameter blocks (and potentially other
            // vendor-specific metadata). Decode best-effort when recognized.
            if (block.format == ContainerFormat::Jpeg && block.id == 0xFFE4U) {
                ExifDecodeResult one;
                one.status          = ExifDecodeStatus::Ok;
                one.ifds_written    = 0;
                one.ifds_needed     = 0;
                one.entries_decoded = 0;

                if (parse_dji_thermal_params(block_bytes, store,
                                             exif_options.limits, &one)) {
                    any_exif = true;
                    merge_exif_status(&exif.status, one.status);
                    exif.entries_decoded += one.entries_decoded;
                }
            }

            // JPEG APP1 "QVCI" block found in some Casio files (QV-7000SX).
            if (block.format == ContainerFormat::Jpeg
                && block.aux_u32 == fourcc('Q', 'V', 'C', 'I')) {
                any_exif = true;

                ExifDecodeResult one;
                one.status          = ExifDecodeStatus::Ok;
                one.ifds_written    = 0;
                one.ifds_needed     = 0;
                one.entries_decoded = 0;

                char scratch[64];
                const std::string_view ifd_name
                    = exif_internal::make_mk_subtable_ifd_token(
                        "mk_casio", "qvci", casio_qvci_index++,
                        std::span<char>(scratch));
                if (ifd_name.empty()) {
                    continue;
                }

                (void)exif_internal::decode_casio_qvci(block_bytes, ifd_name,
                                                       store,
                                                       exif_options.limits,
                                                       &one);
                merge_exif_status(&exif.status, one.status);
                exif.entries_decoded += one.entries_decoded;
            }

            // JPEG APP1 "FLIR" multi-part stream containing an FFF/AFF payload.
            if (block.format == ContainerFormat::Jpeg
                && block.aux_u32 == fourcc('F', 'L', 'I', 'R')) {
                any_exif = true;

                ExifDecodeResult one;
                one.status          = ExifDecodeStatus::Ok;
                one.ifds_written    = 0;
                one.ifds_needed     = 0;
                one.entries_decoded = 0;

                if (exif_internal::decode_flir_fff(block_bytes, store,
                                                   exif_options.limits, &one)) {
                    merge_exif_status(&exif.status, one.status);
                    exif.entries_decoded += one.entries_decoded;
                }
            }
        } else if (block.kind == ContainerBlockKind::CompressedMetadata
                   && block.compression == BlockCompression::Brotli
                   && block.aux_u32 == fourcc('E', 'x', 'i', 'f')) {
            // JPEG XL "brob" box containing Brotli-compressed Exif box payload.
            // Exif box payload begins with a big-endian u32 TIFF offset.
            if (!payload_options.decompress) {
                continue;
            }
            if (block_bytes.size() < 4) {
                merge_exif_status(&exif.status, ExifDecodeStatus::Malformed);
                continue;
            }
            uint32_t off = 0;
            if (!read_u32be(block_bytes, 0, &off)
                || static_cast<uint64_t>(off) >= block_bytes.size()) {
                merge_exif_status(&exif.status, ExifDecodeStatus::Malformed);
                continue;
            }
            const std::span<const std::byte> tiff = block_bytes.subspan(
                static_cast<size_t>(off),
                static_cast<size_t>(block_bytes.size()
                                    - static_cast<size_t>(off)));

            any_exif = true;

            std::span<ExifIfdRef> ifd_slice;
            if (ifd_write_pos < out_ifds.size()) {
                ifd_slice = out_ifds.subspan(ifd_write_pos);
            }

            const ExifDecodeResult one
                = decode_exif_tiff(tiff, store, ifd_slice, exif_options);
            merge_exif_status(&exif.status, one.status);
            exif.ifds_needed += one.ifds_needed;
            exif.entries_decoded += one.entries_decoded;

            const uint32_t room     = (ifd_write_pos < out_ifds.size())
                                          ? static_cast<uint32_t>(out_ifds.size()
                                                                  - ifd_write_pos)
                                          : 0U;
            const uint32_t advanced = (one.ifds_written < room)
                                          ? one.ifds_written
                                          : room;
            ifd_write_pos += advanced;
            exif.ifds_written = ifd_write_pos;
        }
    }

    if (!any_exif) {
        exif.status = ExifDecodeStatus::Unsupported;
    }
    if (!any_xmp) {
        xmp.status = XmpDecodeStatus::Unsupported;
    }

    exr = decode_exr_header(file_bytes, store);

    // If EXR decode succeeded, preserve "unsupported" EXIF/XMP statuses: EXR
    // metadata is a separate key space and may be the only metadata in file.
    result.exif = exif;
    result.exr  = exr;
    result.xmp  = xmp;
    return result;
}

}  // namespace openmeta
