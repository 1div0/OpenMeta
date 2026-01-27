#include "openmeta/simple_meta.h"

namespace openmeta {
namespace {

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

    uint32_t ifd_write_pos = 0;
    bool any_exif          = false;
    for (uint32_t i = 0; i < result.scan.written; ++i) {
        const ContainerBlockRef& block = out_blocks[i];
        if (block.kind != ContainerBlockKind::Exif) {
            continue;
        }
        if (block.part_count > 1U && block.part_index != 0U) {
            continue;
        }
        any_exif = true;

        std::span<ExifIfdRef> ifd_slice;
        if (ifd_write_pos < out_ifds.size()) {
            ifd_slice = out_ifds.subspan(ifd_write_pos);
        }

        std::span<const std::byte> tiff;
        if (block.part_count <= 1U
            && block.compression == BlockCompression::None
            && block.chunking != BlockChunking::GifSubBlocks) {
            if (block.data_offset + block.data_size > file_bytes.size()) {
                merge_exif_status(&exif.status, ExifDecodeStatus::Malformed);
                continue;
            }
            tiff = file_bytes.subspan(static_cast<size_t>(block.data_offset),
                                      static_cast<size_t>(block.data_size));
        } else {
            const PayloadResult payload_res
                = extract_payload(file_bytes, out_blocks, i, payload,
                                  payload_scratch_indices, payload_options);
            merge_payload_result(&result.payload, payload_res);

            if (payload_res.status == PayloadStatus::Ok) {
                tiff = std::span<const std::byte>(payload.data(),
                                                  static_cast<size_t>(
                                                      payload_res.written));
            } else if (payload_res.status == PayloadStatus::OutputTruncated) {
                merge_exif_status(&exif.status,
                                  ExifDecodeStatus::OutputTruncated);
                continue;
            } else if (payload_res.status == PayloadStatus::LimitExceeded) {
                merge_exif_status(&exif.status,
                                  ExifDecodeStatus::LimitExceeded);
                continue;
            } else if (payload_res.status == PayloadStatus::Unsupported) {
                merge_exif_status(&exif.status, ExifDecodeStatus::Unsupported);
                continue;
            } else {
                merge_exif_status(&exif.status, ExifDecodeStatus::Malformed);
                continue;
            }
        }

        const ExifDecodeResult one = decode_exif_tiff(tiff, store, ifd_slice,
                                                      exif_options);
        merge_exif_status(&exif.status, one.status);
        exif.ifds_needed += one.ifds_needed;
        exif.entries_decoded += one.entries_decoded;

        const uint32_t room     = (ifd_write_pos < out_ifds.size())
                                      ? static_cast<uint32_t>(out_ifds.size()
                                                              - ifd_write_pos)
                                      : 0U;
        const uint32_t advanced = (one.ifds_written < room) ? one.ifds_written
                                                            : room;
        ifd_write_pos += advanced;
        exif.ifds_written = ifd_write_pos;
    }

    if (!any_exif) {
        exif.status = ExifDecodeStatus::Unsupported;
    }

    result.exif = exif;
    return result;
}

}  // namespace openmeta
