#include "openmeta/simple_meta.h"

#include "exif_tiff_decode_internal.h"

#include "openmeta/icc_decode.h"
#include "openmeta/iptc_iim_decode.h"
#include "openmeta/photoshop_irb_decode.h"
#include "openmeta/xmp_decode.h"

#include <array>

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

    ExifDecodeResult exif;
    exif.status          = ExifDecodeStatus::Unsupported;
    exif.ifds_written    = 0;
    exif.ifds_needed     = 0;
    exif.entries_decoded = 0;

    XmpDecodeResult xmp;
    xmp.status          = XmpDecodeStatus::Unsupported;
    xmp.entries_decoded = 0;

    uint32_t ifd_write_pos        = 0;
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

                    ExifDecodeOptions mn_opts = exif_options;
                    mn_opts.decode_printim    = false;
                    mn_opts.decode_makernote  = false;
                    mn_opts.tokens.ifd_prefix = "mk_canon";
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

                    const ExifDecodeResult fallback = decode_exif_tiff(
                        block_bytes, store, ifd_slice, mn_opts);
                    merge_exif_status(&exif.status, fallback.status);
                    exif.ifds_needed += fallback.ifds_needed;
                    exif.entries_decoded += fallback.entries_decoded;

                    const uint32_t room
                        = (ifd_write_pos < out_ifds.size())
                              ? static_cast<uint32_t>(out_ifds.size()
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

            const ExifDecodeResult one
                = decode_exif_tiff(block_bytes, store, ifd_slice, exif_options);
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

    result.exif = exif;
    result.xmp  = xmp;
    return result;
}

}  // namespace openmeta
