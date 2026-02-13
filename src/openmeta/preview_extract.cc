#include "openmeta/preview_extract.h"

#include <array>
#include <cstring>

namespace openmeta {
namespace {

    struct TiffConfig final {
        bool little_endian = true;
    };

    struct ClassicIfdEntry final {
        uint16_t tag          = 0;
        uint16_t type         = 0;
        uint32_t count        = 0;
        uint32_t value_or_off = 0;
    };

    static uint8_t u8(std::byte b) noexcept { return static_cast<uint8_t>(b); }

    static bool read_u16le(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2U > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 0)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 8);
        *out = v;
        return true;
    }

    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2U > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 8)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 0);
        *out = v;
        return true;
    }

    static bool read_u32le(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4U > bytes.size()) {
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

    static bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4U > bytes.size()) {
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

    static bool read_tiff_u16(const TiffConfig& cfg,
                              std::span<const std::byte> bytes, uint64_t offset,
                              uint16_t* out) noexcept
    {
        if (cfg.little_endian) {
            return read_u16le(bytes, offset, out);
        }
        return read_u16be(bytes, offset, out);
    }

    static bool read_tiff_u32(const TiffConfig& cfg,
                              std::span<const std::byte> bytes, uint64_t offset,
                              uint32_t* out) noexcept
    {
        if (cfg.little_endian) {
            return read_u32le(bytes, offset, out);
        }
        return read_u32be(bytes, offset, out);
    }

    static bool parse_tiff_header(std::span<const std::byte> bytes,
                                  TiffConfig* cfg, uint64_t* first_ifd) noexcept
    {
        if (!cfg || !first_ifd || bytes.size() < 8U) {
            return false;
        }

        const uint8_t b0 = u8(bytes[0]);
        const uint8_t b1 = u8(bytes[1]);
        if (b0 == 'I' && b1 == 'I') {
            cfg->little_endian = true;
        } else if (b0 == 'M' && b1 == 'M') {
            cfg->little_endian = false;
        } else {
            return false;
        }

        uint16_t magic = 0;
        uint32_t ifd0  = 0;
        if (!read_tiff_u16(*cfg, bytes, 2, &magic)
            || !read_tiff_u32(*cfg, bytes, 4, &ifd0)) {
            return false;
        }
        if (magic != 42U) {
            return false;
        }
        *first_ifd = static_cast<uint64_t>(ifd0);
        return true;
    }

    static uint64_t tiff_type_size(uint16_t type) noexcept
    {
        switch (type) {
        case 1:  // BYTE
        case 2:  // ASCII
        case 6:  // SBYTE
        case 7:  // UNDEFINED
            return 1U;
        case 3:  // SHORT
        case 8:  // SSHORT
            return 2U;
        case 4:   // LONG
        case 9:   // SLONG
        case 11:  // FLOAT
            return 4U;
        case 5:   // RATIONAL
        case 10:  // SRATIONAL
        case 12:  // DOUBLE
            return 8U;
        default: return 0U;
        }
    }

    static bool read_classic_ifd_entry(const TiffConfig& cfg,
                                       std::span<const std::byte> bytes,
                                       uint64_t entry_off,
                                       ClassicIfdEntry* out) noexcept
    {
        if (!out || entry_off + 12U > bytes.size()) {
            return false;
        }
        ClassicIfdEntry e;
        if (!read_tiff_u16(cfg, bytes, entry_off + 0U, &e.tag)
            || !read_tiff_u16(cfg, bytes, entry_off + 2U, &e.type)
            || !read_tiff_u32(cfg, bytes, entry_off + 4U, &e.count)
            || !read_tiff_u32(cfg, bytes, entry_off + 8U, &e.value_or_off)) {
            return false;
        }
        *out = e;
        return true;
    }

    static bool entry_scalar_u32(const TiffConfig& cfg,
                                 const ClassicIfdEntry& e,
                                 uint32_t* out) noexcept
    {
        if (!out || e.count == 0U) {
            return false;
        }
        if (e.type == 4U) {  // LONG
            *out = e.value_or_off;
            return true;
        }
        if (e.type == 3U) {  // SHORT
            if (cfg.little_endian) {
                *out = e.value_or_off & 0xFFFFU;
            } else {
                *out = (e.value_or_off >> 16) & 0xFFFFU;
            }
            return true;
        }
        return false;
    }

    static bool is_jpeg_soi(std::span<const std::byte> file_bytes,
                            uint64_t offset, uint64_t size) noexcept
    {
        if (size < 2U || offset + 2U > file_bytes.size()) {
            return false;
        }
        return u8(file_bytes[static_cast<size_t>(offset + 0U)]) == 0xFFU
               && u8(file_bytes[static_cast<size_t>(offset + 1U)]) == 0xD8U;
    }

    static bool contains_ifd_offset(std::span<const uint64_t> values,
                                    uint32_t count, uint64_t off) noexcept
    {
        for (uint32_t i = 0; i < count; ++i) {
            if (values[i] == off) {
                return true;
            }
        }
        return false;
    }

    static bool push_ifd_offset(std::span<uint64_t> queue, uint32_t* count,
                                uint64_t off) noexcept
    {
        if (!count || *count >= queue.size()) {
            return false;
        }
        if (off == 0U || contains_ifd_offset(queue, *count, off)) {
            return true;
        }
        queue[*count] = off;
        *count += 1U;
        return true;
    }

    static PreviewScanStatus
    add_candidate(std::span<const std::byte> file_bytes,
                  std::span<PreviewCandidate> out, uint32_t* written,
                  uint32_t* needed, const PreviewScanOptions& options,
                  const PreviewCandidate& in) noexcept
    {
        if (!written || !needed) {
            return PreviewScanStatus::Malformed;
        }
        PreviewCandidate candidate = in;
        candidate.has_jpeg_soi_signature
            = is_jpeg_soi(file_bytes, candidate.file_offset, candidate.size);
        if (options.require_jpeg_soi && !candidate.has_jpeg_soi_signature) {
            return PreviewScanStatus::Ok;
        }
        if (*written < out.size()) {
            out[*written] = candidate;
            *written += 1U;
            *needed += 1U;
            return PreviewScanStatus::Ok;
        }
        *needed += 1U;
        return PreviewScanStatus::OutputTruncated;
    }

    static PreviewScanStatus collect_tiff_preview_candidates(
        std::span<const std::byte> file_bytes, const ContainerBlockRef& block,
        uint32_t block_index, std::span<PreviewCandidate> out,
        uint32_t* written, uint32_t* needed,
        const PreviewScanOptions& options) noexcept
    {
        if (block.data_offset > file_bytes.size()
            || block.data_size > file_bytes.size() - block.data_offset) {
            return PreviewScanStatus::Malformed;
        }

        const std::span<const std::byte> tiff
            = file_bytes.subspan(static_cast<size_t>(block.data_offset),
                                 static_cast<size_t>(block.data_size));
        TiffConfig cfg;
        uint64_t ifd0 = 0;
        if (!parse_tiff_header(tiff, &cfg, &ifd0)) {
            return PreviewScanStatus::Unsupported;
        }
        if (ifd0 == 0U || ifd0 > tiff.size()) {
            return PreviewScanStatus::Malformed;
        }

        std::array<uint64_t, 256> ifd_queue {};
        const uint32_t ifd_cap = (options.limits.max_ifds < ifd_queue.size())
                                     ? options.limits.max_ifds
                                     : static_cast<uint32_t>(ifd_queue.size());
        std::span<uint64_t> ifds(ifd_queue.data(), ifd_cap);
        uint32_t ifd_count = 0;
        uint32_t ifd_index = 0;
        if (!push_ifd_offset(ifds, &ifd_count, ifd0)) {
            return PreviewScanStatus::LimitExceeded;
        }

        uint32_t total_entries = 0;
        bool truncated         = false;
        while (ifd_index < ifd_count) {
            const uint64_t ifd_off = ifds[ifd_index++];
            if (ifd_off + 2U > tiff.size()) {
                return PreviewScanStatus::Malformed;
            }

            uint16_t entry_count = 0;
            if (!read_tiff_u16(cfg, tiff, ifd_off, &entry_count)) {
                return PreviewScanStatus::Malformed;
            }
            const uint64_t ifd_bytes
                = 2ULL + static_cast<uint64_t>(entry_count) * 12ULL + 4ULL;
            if (ifd_off + ifd_bytes > tiff.size()) {
                return PreviewScanStatus::Malformed;
            }

            if (total_entries + static_cast<uint32_t>(entry_count)
                > options.limits.max_total_entries) {
                return PreviewScanStatus::LimitExceeded;
            }
            total_entries += static_cast<uint32_t>(entry_count);

            bool have_jif_off = false;
            bool have_jif_len = false;
            uint32_t jif_off  = 0;
            uint32_t jif_len  = 0;

            for (uint16_t ei = 0; ei < entry_count; ++ei) {
                const uint64_t entry_off = ifd_off + 2ULL
                                           + static_cast<uint64_t>(ei) * 12ULL;
                ClassicIfdEntry e;
                if (!read_classic_ifd_entry(cfg, tiff, entry_off, &e)) {
                    return PreviewScanStatus::Malformed;
                }

                if (e.tag == 0x0201U && options.include_exif_jpeg_interchange) {
                    have_jif_off = entry_scalar_u32(cfg, e, &jif_off);
                } else if (e.tag == 0x0202U
                           && options.include_exif_jpeg_interchange) {
                    have_jif_len = entry_scalar_u32(cfg, e, &jif_len);
                } else if ((e.tag == 0x002EU || e.tag == 0x0127U)
                           && options.include_jpg_from_raw) {
                    const uint64_t elem_size = tiff_type_size(e.type);
                    if (elem_size == 0U) {
                        continue;
                    }
                    if (e.count > 0U
                        && elem_size
                               > options.limits.max_preview_bytes / e.count) {
                        return PreviewScanStatus::LimitExceeded;
                    }
                    const uint64_t byte_count = elem_size * e.count;
                    if (byte_count == 0U
                        || byte_count > options.limits.max_preview_bytes) {
                        continue;
                    }
                    if (byte_count <= 4U) {
                        continue;
                    }
                    const uint64_t local_off = static_cast<uint64_t>(
                        e.value_or_off);
                    if (local_off > tiff.size()
                        || byte_count > tiff.size() - local_off) {
                        return PreviewScanStatus::Malformed;
                    }
                    if (block.data_offset > file_bytes.size()
                        || local_off > file_bytes.size() - block.data_offset
                        || byte_count > file_bytes.size() - block.data_offset
                                            - local_off) {
                        return PreviewScanStatus::Malformed;
                    }
                    PreviewCandidate c;
                    c.kind   = (e.tag == 0x002EU) ? PreviewKind::ExifJpgFromRaw
                                                  : PreviewKind::ExifJpgFromRaw2;
                    c.format = block.format;
                    c.block_index             = block_index;
                    c.offset_tag              = e.tag;
                    c.length_tag              = 0U;
                    c.file_offset             = block.data_offset + local_off;
                    c.size                    = byte_count;
                    const PreviewScanStatus s = add_candidate(file_bytes, out,
                                                              written, needed,
                                                              options, c);
                    if (s == PreviewScanStatus::OutputTruncated) {
                        truncated = true;
                    } else if (s != PreviewScanStatus::Ok) {
                        return s;
                    }
                }

                if (e.tag == 0x8769U || e.tag == 0x8825U || e.tag == 0xA005U) {
                    uint32_t child = 0;
                    if (entry_scalar_u32(cfg, e, &child) && child != 0U) {
                        if (!push_ifd_offset(ifds, &ifd_count, child)) {
                            return PreviewScanStatus::LimitExceeded;
                        }
                    }
                } else if (e.tag == 0x014AU) {
                    const uint64_t elem_size = tiff_type_size(e.type);
                    if (elem_size != 4U || e.count == 0U) {
                        continue;
                    }
                    const uint64_t bytes_needed = static_cast<uint64_t>(e.count)
                                                  * elem_size;
                    if (bytes_needed <= 4U) {
                        uint32_t one = e.value_or_off;
                        if (one != 0U
                            && !push_ifd_offset(ifds, &ifd_count,
                                                static_cast<uint64_t>(one))) {
                            return PreviewScanStatus::LimitExceeded;
                        }
                    } else {
                        const uint64_t off = static_cast<uint64_t>(
                            e.value_or_off);
                        if (off > tiff.size()
                            || bytes_needed > tiff.size() - off) {
                            return PreviewScanStatus::Malformed;
                        }
                        for (uint32_t ai = 0; ai < e.count; ++ai) {
                            uint32_t one = 0;
                            const uint64_t v_off
                                = off + static_cast<uint64_t>(ai) * 4U;
                            if (!read_tiff_u32(cfg, tiff, v_off, &one)) {
                                return PreviewScanStatus::Malformed;
                            }
                            if (one != 0U
                                && !push_ifd_offset(ifds, &ifd_count,
                                                    static_cast<uint64_t>(
                                                        one))) {
                                return PreviewScanStatus::LimitExceeded;
                            }
                        }
                    }
                }
            }

            if (have_jif_off && have_jif_len && jif_len > 0U) {
                const uint64_t off64 = static_cast<uint64_t>(jif_off);
                const uint64_t len64 = static_cast<uint64_t>(jif_len);
                if (len64 > options.limits.max_preview_bytes) {
                    return PreviewScanStatus::LimitExceeded;
                }
                if (off64 > tiff.size() || len64 > tiff.size() - off64) {
                    return PreviewScanStatus::Malformed;
                }
                if (block.data_offset > file_bytes.size()
                    || off64 > file_bytes.size() - block.data_offset
                    || len64 > file_bytes.size() - block.data_offset - off64) {
                    return PreviewScanStatus::Malformed;
                }

                PreviewCandidate c;
                c.kind                    = PreviewKind::ExifJpegInterchange;
                c.format                  = block.format;
                c.block_index             = block_index;
                c.offset_tag              = 0x0201U;
                c.length_tag              = 0x0202U;
                c.file_offset             = block.data_offset + off64;
                c.size                    = len64;
                const PreviewScanStatus s = add_candidate(file_bytes, out,
                                                          written, needed,
                                                          options, c);
                if (s == PreviewScanStatus::OutputTruncated) {
                    truncated = true;
                } else if (s != PreviewScanStatus::Ok) {
                    return s;
                }
            }

            uint32_t next_ifd = 0;
            if (!read_tiff_u32(cfg, tiff,
                               ifd_off + 2ULL
                                   + static_cast<uint64_t>(entry_count) * 12ULL,
                               &next_ifd)) {
                return PreviewScanStatus::Malformed;
            }
            if (next_ifd != 0U
                && !push_ifd_offset(ifds, &ifd_count,
                                    static_cast<uint64_t>(next_ifd))) {
                return PreviewScanStatus::LimitExceeded;
            }
        }

        return truncated ? PreviewScanStatus::OutputTruncated
                         : PreviewScanStatus::Ok;
    }

}  // namespace

PreviewScanResult
find_preview_candidates(std::span<const std::byte> file_bytes,
                        std::span<const ContainerBlockRef> blocks,
                        std::span<PreviewCandidate> out,
                        const PreviewScanOptions& options) noexcept
{
    PreviewScanResult result;
    result.status  = PreviewScanStatus::Unsupported;
    result.written = 0U;
    result.needed  = 0U;

    bool saw_tiff  = false;
    bool truncated = false;
    for (uint32_t i = 0; i < blocks.size(); ++i) {
        const ContainerBlockRef& block = blocks[i];
        if (block.kind != ContainerBlockKind::Exif) {
            continue;
        }
        if (block.part_count > 1U && block.part_index != 0U) {
            continue;
        }

        saw_tiff = true;
        const PreviewScanStatus one
            = collect_tiff_preview_candidates(file_bytes, block, i, out,
                                              &result.written, &result.needed,
                                              options);
        if (one == PreviewScanStatus::OutputTruncated) {
            truncated = true;
            continue;
        }
        if (one == PreviewScanStatus::LimitExceeded
            || one == PreviewScanStatus::Malformed) {
            result.status = one;
            return result;
        }
    }

    if (truncated) {
        result.status = PreviewScanStatus::OutputTruncated;
    } else if (!saw_tiff) {
        result.status = PreviewScanStatus::Unsupported;
    } else {
        result.status = PreviewScanStatus::Ok;
    }
    return result;
}

PreviewScanResult
scan_preview_candidates(std::span<const std::byte> file_bytes,
                        std::span<ContainerBlockRef> blocks_scratch,
                        std::span<PreviewCandidate> out,
                        const PreviewScanOptions& options) noexcept
{
    const ScanResult scan = scan_auto(file_bytes, blocks_scratch);
    PreviewScanResult result;
    result.status  = PreviewScanStatus::Unsupported;
    result.written = 0U;
    result.needed  = 0U;
    if (scan.status == ScanStatus::Unsupported) {
        result.status = PreviewScanStatus::Unsupported;
        return result;
    }
    if (scan.status == ScanStatus::Malformed) {
        result.status = PreviewScanStatus::Malformed;
        return result;
    }

    const uint32_t written = (scan.written < blocks_scratch.size())
                                 ? scan.written
                                 : static_cast<uint32_t>(blocks_scratch.size());
    result                 = find_preview_candidates(
        file_bytes,
        std::span<const ContainerBlockRef>(blocks_scratch.data(), written), out,
        options);
    if (scan.status == ScanStatus::OutputTruncated
        && result.status == PreviewScanStatus::Ok) {
        result.status = PreviewScanStatus::OutputTruncated;
    }
    return result;
}

PreviewExtractResult
extract_preview_candidate(std::span<const std::byte> file_bytes,
                          const PreviewCandidate& candidate,
                          std::span<std::byte> out,
                          const PreviewExtractOptions& options) noexcept
{
    PreviewExtractResult result;
    result.status  = PreviewExtractStatus::Ok;
    result.written = 0U;
    result.needed  = candidate.size;

    if (candidate.size > options.max_output_bytes) {
        result.status = PreviewExtractStatus::LimitExceeded;
        return result;
    }
    if (candidate.file_offset > file_bytes.size()
        || candidate.size > file_bytes.size() - candidate.file_offset) {
        result.status = PreviewExtractStatus::Malformed;
        return result;
    }
    if (options.require_jpeg_soi
        && !is_jpeg_soi(file_bytes, candidate.file_offset, candidate.size)) {
        result.status = PreviewExtractStatus::Malformed;
        return result;
    }

    if (candidate.size > out.size()) {
        result.status = PreviewExtractStatus::OutputTruncated;
        return result;
    }

    const std::span<const std::byte> src
        = file_bytes.subspan(static_cast<size_t>(candidate.file_offset),
                             static_cast<size_t>(candidate.size));
    if (!src.empty()) {
        std::memcpy(out.data(), src.data(), src.size());
    }
    result.written = candidate.size;
    return result;
}

}  // namespace openmeta
