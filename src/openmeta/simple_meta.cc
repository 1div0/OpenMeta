#include "openmeta/simple_meta.h"

namespace openmeta {
namespace {

    static void merge_exif_status(ExifDecodeStatus* out,
                                  ExifDecodeStatus in) noexcept
    {
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
        if (*out == ExifDecodeStatus::Unsupported) {
            return;
        }
        if (in == ExifDecodeStatus::Unsupported) {
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
    }

}  // namespace

SimpleMetaResult
simple_meta_read(std::span<const std::byte> file_bytes, MetaStore& store,
                 std::span<ContainerBlockRef> out_blocks,
                 std::span<ExifIfdRef> out_ifds,
                 const ExifDecodeOptions& exif_options) noexcept
{
    SimpleMetaResult result;
    result.scan = scan_auto(file_bytes, out_blocks);

    ExifDecodeResult exif;
    exif.status          = ExifDecodeStatus::Ok;
    exif.ifds_written    = 0;
    exif.ifds_needed     = 0;
    exif.entries_decoded = 0;

    uint32_t ifd_write_pos = 0;
    for (uint32_t i = 0; i < result.scan.written; ++i) {
        const ContainerBlockRef& block = out_blocks[i];
        if (block.kind != ContainerBlockKind::Exif) {
            continue;
        }
        if (block.data_offset + block.data_size > file_bytes.size()) {
            merge_exif_status(&exif.status, ExifDecodeStatus::Malformed);
            continue;
        }

        std::span<ExifIfdRef> ifd_slice;
        if (ifd_write_pos < out_ifds.size()) {
            ifd_slice = out_ifds.subspan(ifd_write_pos);
        }

        const std::span<const std::byte> tiff
            = file_bytes.subspan(static_cast<size_t>(block.data_offset),
                                 static_cast<size_t>(block.data_size));

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

    result.exif = exif;
    return result;
}

}  // namespace openmeta
