#include "openmeta/container_payload.h"

#include <array>
#include <cstring>

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
#    include <zlib.h>
#endif

#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI
#    include <brotli/decode.h>
#endif

namespace openmeta {
namespace {

    static uint8_t u8(std::byte b) noexcept { return static_cast<uint8_t>(b); }

    static bool validate_range(std::span<const std::byte> bytes,
                               uint64_t offset, uint64_t size) noexcept
    {
        const uint64_t bytes_size = static_cast<uint64_t>(bytes.size());
        if (offset > bytes_size) {
            return false;
        }
        const uint64_t cap = bytes_size - offset;
        return size <= cap;
    }

    static uint32_t safe_u32(uint64_t v) noexcept
    {
        return (v > 0xFFFFFFFFULL) ? 0xFFFFFFFFU : static_cast<uint32_t>(v);
    }

    static void insertion_sort_by_part_index(
        std::span<uint32_t> indices,
        std::span<const ContainerBlockRef> blocks) noexcept
    {
        for (size_t i = 1; i < indices.size(); ++i) {
            const uint32_t key = indices[i];
            const uint32_t key_part
                = blocks[key].part_index;  // stable, no bounds checks here.
            size_t j = i;
            while (j > 0) {
                const uint32_t prev = indices[j - 1];
                if (blocks[prev].part_index <= key_part) {
                    break;
                }
                indices[j] = prev;
                j -= 1;
            }
            indices[j] = key;
        }
    }

    static void insertion_sort_by_logical_offset(
        std::span<uint32_t> indices,
        std::span<const ContainerBlockRef> blocks) noexcept
    {
        for (size_t i = 1; i < indices.size(); ++i) {
            const uint32_t key     = indices[i];
            const uint64_t key_off = blocks[key].logical_offset;
            size_t j               = i;
            while (j > 0) {
                const uint32_t prev = indices[j - 1];
                if (blocks[prev].logical_offset <= key_off) {
                    break;
                }
                indices[j] = prev;
                j -= 1;
            }
            indices[j] = key;
        }
    }

    static void copy_bytes(std::span<std::byte> dst, uint64_t dst_off,
                           std::span<const std::byte> src,
                           uint64_t* io_written) noexcept
    {
        const uint64_t dst_size = static_cast<uint64_t>(dst.size());
        if (dst_off >= dst_size) {
            return;
        }
        const uint64_t room = dst_size - dst_off;
        const uint64_t n    = (static_cast<uint64_t>(src.size()) < room)
                                  ? static_cast<uint64_t>(src.size())
                                  : room;
        if (n == 0) {
            return;
        }
        std::memcpy(dst.data() + static_cast<size_t>(dst_off), src.data(),
                    static_cast<size_t>(n));
        if (io_written) {
            *io_written += n;
        }
    }

    static PayloadResult
    extract_gif_subblocks(std::span<const std::byte> bytes,
                          std::span<std::byte> out,
                          const PayloadOptions& options) noexcept
    {
        PayloadResult res;
        uint64_t needed  = 0;
        uint64_t written = 0;

        const uint64_t max_out = options.limits.max_output_bytes;
        uint64_t p             = 0;
        while (p < bytes.size()) {
            const uint8_t sub = u8(bytes[static_cast<size_t>(p)]);
            p += 1;
            if (sub == 0) {
                break;
            }
            if (p > bytes.size() || sub > bytes.size() - p) {
                res.status = PayloadStatus::Malformed;
                return res;
            }

            needed += sub;
            if (max_out != 0U && needed > max_out) {
                res.status  = PayloadStatus::LimitExceeded;
                res.needed  = needed;
                res.written = written;
                return res;
            }

            const std::span<const std::byte> part
                = bytes.subspan(static_cast<size_t>(p),
                                static_cast<size_t>(sub));
            copy_bytes(out, written, part, &written);
            p += sub;
        }

        res.needed  = needed;
        res.written = written;
        if (written < needed) {
            res.status = PayloadStatus::OutputTruncated;
        }
        return res;
    }

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
    static PayloadResult inflate_zlib(std::span<const std::byte> in,
                                      std::span<std::byte> out,
                                      const PayloadOptions& options) noexcept
    {
        PayloadResult res;

        z_stream strm {};
        strm.zalloc = Z_NULL;
        strm.zfree  = Z_NULL;
        strm.opaque = Z_NULL;

        int ret = inflateInit(&strm);
        if (ret != Z_OK) {
            res.status = PayloadStatus::Malformed;
            return res;
        }

        std::array<std::byte, 32768> discard {};

        uint64_t in_off   = 0;
        uint64_t written  = 0;
        uint64_t produced = 0;

        const uint64_t max_out = options.limits.max_output_bytes;

        for (;;) {
            if (strm.avail_in == 0) {
                if (in_off >= in.size()) {
                    (void)inflateEnd(&strm);
                    res.status  = PayloadStatus::Malformed;
                    res.written = written;
                    res.needed  = produced;
                    return res;
                }
                const uint64_t remaining = static_cast<uint64_t>(in.size())
                                           - in_off;
                const uint64_t chunk64
                    = (remaining < static_cast<uint64_t>(0xFFFFFFFFU))
                          ? remaining
                          : static_cast<uint64_t>(0xFFFFFFFFU);
                const uint32_t chunk = safe_u32(chunk64);
                strm.next_in  = reinterpret_cast<Bytef*>(const_cast<std::byte*>(
                    in.data() + static_cast<size_t>(in_off)));
                strm.avail_in = static_cast<uInt>(chunk);
                in_off += chunk;
            }

            std::byte* out_ptr = nullptr;
            uint64_t out_room  = 0;
            if (written < out.size()) {
                out_ptr  = out.data() + static_cast<size_t>(written);
                out_room = static_cast<uint64_t>(out.size()) - written;
            } else {
                out_ptr  = discard.data();
                out_room = discard.size();
            }
            const uint64_t out_chunk64
                = (out_room < static_cast<uint64_t>(0xFFFFFFFFU))
                      ? out_room
                      : static_cast<uint64_t>(0xFFFFFFFFU);
            const uint32_t out_chunk = safe_u32(out_chunk64);
            strm.next_out            = reinterpret_cast<Bytef*>(
                reinterpret_cast<void*>(out_ptr));
            strm.avail_out = static_cast<uInt>(out_chunk);

            const uInt avail_before = strm.avail_out;
            ret                     = inflate(&strm, Z_NO_FLUSH);
            const uInt used_out     = avail_before - strm.avail_out;

            produced += used_out;
            if (written < out.size()) {
                written += used_out;
                if (written > out.size()) {
                    written = out.size();
                }
            }

            if (max_out != 0U && produced > max_out) {
                (void)inflateEnd(&strm);
                res.status  = PayloadStatus::LimitExceeded;
                res.written = written;
                res.needed  = produced;
                return res;
            }

            if (ret == Z_STREAM_END) {
                break;
            }
            if (ret != Z_OK) {
                (void)inflateEnd(&strm);
                res.status  = PayloadStatus::Malformed;
                res.written = written;
                res.needed  = produced;
                return res;
            }
        }

        (void)inflateEnd(&strm);
        res.written = written;
        res.needed  = produced;
        if (written < produced) {
            res.status = PayloadStatus::OutputTruncated;
        }
        return res;
    }
#endif

#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI
    static PayloadResult
    brotli_decompress(std::span<const std::byte> in, std::span<std::byte> out,
                      const PayloadOptions& options) noexcept
    {
        PayloadResult res;

        BrotliDecoderState* st = BrotliDecoderCreateInstance(nullptr, nullptr,
                                                             nullptr);
        if (!st) {
            res.status = PayloadStatus::LimitExceeded;
            return res;
        }

        std::array<std::byte, 32768> discard {};

        const uint8_t* next_in = reinterpret_cast<const uint8_t*>(in.data());
        size_t avail_in        = in.size();

        uint64_t written  = 0;
        uint64_t produced = 0;

        const uint64_t max_out = options.limits.max_output_bytes;

        for (;;) {
            uint8_t* next_out = nullptr;
            size_t avail_out  = 0;
            if (written < out.size()) {
                next_out = reinterpret_cast<uint8_t*>(
                    out.data() + static_cast<size_t>(written));
                avail_out = out.size() - static_cast<size_t>(written);
            } else {
                next_out  = reinterpret_cast<uint8_t*>(discard.data());
                avail_out = discard.size();
            }
            const size_t avail_before = avail_out;
            const BrotliDecoderResult r
                = BrotliDecoderDecompressStream(st, &avail_in, &next_in,
                                                &avail_out, &next_out, nullptr);
            const size_t used_out = avail_before - avail_out;
            produced += used_out;
            if (written < out.size()) {
                written += used_out;
                if (written > out.size()) {
                    written = out.size();
                }
            }

            if (max_out != 0U && produced > max_out) {
                BrotliDecoderDestroyInstance(st);
                res.status  = PayloadStatus::LimitExceeded;
                res.written = written;
                res.needed  = produced;
                return res;
            }

            if (r == BROTLI_DECODER_RESULT_SUCCESS) {
                break;
            }
            if (r == BROTLI_DECODER_RESULT_ERROR) {
                BrotliDecoderDestroyInstance(st);
                res.status  = PayloadStatus::Malformed;
                res.written = written;
                res.needed  = produced;
                return res;
            }

            if (r == BROTLI_DECODER_RESULT_NEEDS_MORE_INPUT) {
                if (avail_in == 0) {
                    BrotliDecoderDestroyInstance(st);
                    res.status  = PayloadStatus::Malformed;
                    res.written = written;
                    res.needed  = produced;
                    return res;
                }
            }
        }

        BrotliDecoderDestroyInstance(st);
        res.written = written;
        res.needed  = produced;
        if (written < produced) {
            res.status = PayloadStatus::OutputTruncated;
        }
        return res;
    }
#endif

    static PayloadResult extract_single_block(
        std::span<const std::byte> file_bytes, const ContainerBlockRef& block,
        std::span<std::byte> out, const PayloadOptions& options) noexcept
    {
        PayloadResult res;
        if (!validate_range(file_bytes, block.data_offset, block.data_size)) {
            res.status = PayloadStatus::Malformed;
            return res;
        }

        const std::span<const std::byte> src
            = file_bytes.subspan(static_cast<size_t>(block.data_offset),
                                 static_cast<size_t>(block.data_size));

        if (block.chunking == BlockChunking::GifSubBlocks) {
            return extract_gif_subblocks(src, out, options);
        }

        if (!options.decompress
            || block.compression == BlockCompression::None) {
            const uint64_t max_out = options.limits.max_output_bytes;
            if (max_out != 0U && block.data_size > max_out) {
                res.status  = PayloadStatus::LimitExceeded;
                res.needed  = block.data_size;
                res.written = 0;
                return res;
            }
            res.needed       = block.data_size;
            uint64_t written = 0;
            copy_bytes(out, 0, src, &written);
            res.written = written;
            if (written < block.data_size) {
                res.status = PayloadStatus::OutputTruncated;
            }
            return res;
        }

        if (block.compression == BlockCompression::Deflate) {
#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
            return inflate_zlib(src, out, options);
#else
            res.status = PayloadStatus::Unsupported;
            return res;
#endif
        }
        if (block.compression == BlockCompression::Brotli) {
#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI
            return brotli_decompress(src, out, options);
#else
            res.status = PayloadStatus::Unsupported;
            return res;
#endif
        }

        res.status = PayloadStatus::Unsupported;
        return res;
    }

    static bool blocks_match_jpeg_icc(const ContainerBlockRef& seed,
                                      const ContainerBlockRef& b) noexcept
    {
        if (b.format != seed.format || b.kind != seed.kind) {
            return false;
        }
        if (b.chunking != BlockChunking::JpegApp2SeqTotal) {
            return false;
        }
        if (seed.part_count != 0U && b.part_count != 0U
            && b.part_count != seed.part_count) {
            return false;
        }
        return true;
    }

    static bool blocks_match_jpeg_xmp_ext(const ContainerBlockRef& seed,
                                          const ContainerBlockRef& b) noexcept
    {
        if (b.format != seed.format || b.kind != seed.kind) {
            return false;
        }
        if (b.chunking != BlockChunking::JpegXmpExtendedGuidOffset) {
            return false;
        }
        if (b.group != seed.group) {
            return false;
        }
        if (seed.logical_size != 0U && b.logical_size != 0U
            && b.logical_size != seed.logical_size) {
            return false;
        }
        return true;
    }

    static bool blocks_match_multipart(const ContainerBlockRef& seed,
                                       const ContainerBlockRef& b) noexcept
    {
        if (b.format != seed.format || b.kind != seed.kind) {
            return false;
        }
        if (b.group != seed.group) {
            return false;
        }
        if (b.id != seed.id) {
            return false;
        }
        if (seed.part_count != 0U && b.part_count != 0U
            && b.part_count != seed.part_count) {
            return false;
        }
        return true;
    }

    static PayloadResult
    extract_concat_parts(std::span<const std::byte> file_bytes,
                         std::span<const ContainerBlockRef> blocks,
                         std::span<const uint32_t> part_indices,
                         std::span<std::byte> out,
                         const PayloadOptions& options) noexcept
    {
        PayloadResult res;

        uint64_t needed = 0;
        for (size_t i = 0; i < part_indices.size(); ++i) {
            const ContainerBlockRef& b = blocks[part_indices[i]];
            if (!validate_range(file_bytes, b.data_offset, b.data_size)) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
            needed += b.data_size;
            const uint64_t max_out = options.limits.max_output_bytes;
            if (max_out != 0U && needed > max_out) {
                res.status = PayloadStatus::LimitExceeded;
                res.needed = needed;
                return res;
            }
        }

        uint64_t written = 0;
        for (size_t i = 0; i < part_indices.size(); ++i) {
            const ContainerBlockRef& b = blocks[part_indices[i]];
            const std::span<const std::byte> src
                = file_bytes.subspan(static_cast<size_t>(b.data_offset),
                                     static_cast<size_t>(b.data_size));
            copy_bytes(out, written, src, &written);
        }

        res.needed  = needed;
        res.written = written;
        if (written < needed) {
            res.status = PayloadStatus::OutputTruncated;
        }
        return res;
    }

    static PayloadResult
    extract_offset_parts(std::span<const std::byte> file_bytes,
                         std::span<const ContainerBlockRef> blocks,
                         std::span<const uint32_t> part_indices,
                         uint64_t logical_size, std::span<std::byte> out,
                         const PayloadOptions& options) noexcept
    {
        PayloadResult res;

        if (logical_size == 0U) {
            res.status = PayloadStatus::Malformed;
            return res;
        }

        const uint64_t max_out = options.limits.max_output_bytes;
        if (max_out != 0U && logical_size > max_out) {
            res.status = PayloadStatus::LimitExceeded;
            res.needed = logical_size;
            return res;
        }

        uint64_t expected = 0;
        uint64_t written  = 0;
        for (size_t i = 0; i < part_indices.size(); ++i) {
            const ContainerBlockRef& b = blocks[part_indices[i]];
            if (!validate_range(file_bytes, b.data_offset, b.data_size)) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
            if (b.logical_offset != expected) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
            if (b.data_size > logical_size - expected) {
                res.status = PayloadStatus::Malformed;
                return res;
            }

            const std::span<const std::byte> src
                = file_bytes.subspan(static_cast<size_t>(b.data_offset),
                                     static_cast<size_t>(b.data_size));
            copy_bytes(out, expected, src, &written);
            expected += b.data_size;
        }

        if (expected != logical_size) {
            res.status = PayloadStatus::Malformed;
            return res;
        }

        res.needed  = logical_size;
        res.written = written;
        if (written < logical_size) {
            res.status = PayloadStatus::OutputTruncated;
        }
        return res;
    }

}  // namespace

PayloadResult
extract_payload(std::span<const std::byte> file_bytes,
                std::span<const ContainerBlockRef> blocks, uint32_t seed_index,
                std::span<std::byte> out_payload,
                std::span<uint32_t> scratch_indices,
                const PayloadOptions& options) noexcept
{
    PayloadResult res;
    if (static_cast<size_t>(seed_index) >= blocks.size()) {
        res.status = PayloadStatus::Malformed;
        return res;
    }

    const ContainerBlockRef& seed = blocks[seed_index];

    if (seed.chunking == BlockChunking::GifSubBlocks) {
        return extract_single_block(file_bytes, seed, out_payload, options);
    }

    if (seed.part_count <= 1U
        && seed.chunking != BlockChunking::JpegApp2SeqTotal
        && seed.chunking != BlockChunking::JpegXmpExtendedGuidOffset) {
        return extract_single_block(file_bytes, seed, out_payload, options);
    }

    // Multi-part logical streams.
    size_t count = 0;
    for (size_t i = 0; i < blocks.size(); ++i) {
        const ContainerBlockRef& b = blocks[i];
        bool match                 = false;
        if (seed.chunking == BlockChunking::JpegApp2SeqTotal) {
            match = blocks_match_jpeg_icc(seed, b);
        } else if (seed.chunking == BlockChunking::JpegXmpExtendedGuidOffset) {
            match = blocks_match_jpeg_xmp_ext(seed, b);
        } else if (seed.part_count > 1U) {
            match = blocks_match_multipart(seed, b);
        }
        if (!match) {
            continue;
        }
        if (count >= static_cast<size_t>(options.limits.max_parts)) {
            res.status = PayloadStatus::LimitExceeded;
            res.needed = static_cast<uint64_t>(count);
            return res;
        }
        if (count >= scratch_indices.size()) {
            res.status = PayloadStatus::LimitExceeded;
            res.needed = static_cast<uint64_t>(count + 1U);
            return res;
        }
        if (i > static_cast<size_t>(0xFFFFFFFFU)) {
            res.status = PayloadStatus::LimitExceeded;
            return res;
        }
        scratch_indices[count] = static_cast<uint32_t>(i);
        count += 1;
    }

    if (count == 0) {
        res.status = PayloadStatus::Malformed;
        return res;
    }

    const std::span<uint32_t> parts = scratch_indices.first(count);

    if (seed.chunking == BlockChunking::JpegApp2SeqTotal) {
        insertion_sort_by_part_index(parts, blocks);
        const uint32_t expected_total = (seed.part_count != 0U)
                                            ? seed.part_count
                                            : static_cast<uint32_t>(count);
        if (expected_total == 0U || expected_total > options.limits.max_parts) {
            res.status = PayloadStatus::LimitExceeded;
            return res;
        }
        if (static_cast<uint32_t>(count) != expected_total) {
            res.status = PayloadStatus::Malformed;
            return res;
        }
        for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
            const ContainerBlockRef& b = blocks[parts[i]];
            if (b.part_index != i) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
        }
        return extract_concat_parts(file_bytes, blocks, parts, out_payload,
                                    options);
    }

    if (seed.chunking == BlockChunking::JpegXmpExtendedGuidOffset) {
        insertion_sort_by_logical_offset(parts, blocks);

        uint64_t logical_size = seed.logical_size;
        if (logical_size == 0U) {
            uint64_t max_end = 0;
            for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
                const ContainerBlockRef& b = blocks[parts[i]];
                const uint64_t end         = b.logical_offset + b.data_size;
                max_end                    = (end > max_end) ? end : max_end;
            }
            logical_size = max_end;
        }
        return extract_offset_parts(file_bytes, blocks, parts, logical_size,
                                    out_payload, options);
    }

    if (seed.part_count > 1U) {
        insertion_sort_by_part_index(parts, blocks);

        bool any_offsets = false;
        for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
            if (blocks[parts[i]].logical_offset != 0U) {
                any_offsets = true;
                break;
            }
        }
        if (any_offsets) {
            insertion_sort_by_logical_offset(parts, blocks);
            uint64_t logical_size = 0;
            uint64_t max_end      = 0;
            for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
                const ContainerBlockRef& b = blocks[parts[i]];
                if (b.logical_size != 0U) {
                    logical_size = b.logical_size;
                }
                const uint64_t end = b.logical_offset + b.data_size;
                max_end            = (end > max_end) ? end : max_end;
            }
            if (logical_size == 0U) {
                logical_size = max_end;
            }
            return extract_offset_parts(file_bytes, blocks, parts, logical_size,
                                        out_payload, options);
        }

        const uint32_t expected_total = (seed.part_count != 0U)
                                            ? seed.part_count
                                            : static_cast<uint32_t>(count);
        if (expected_total == 0U || expected_total > options.limits.max_parts) {
            res.status = PayloadStatus::LimitExceeded;
            return res;
        }
        if (static_cast<uint32_t>(count) != expected_total) {
            res.status = PayloadStatus::Malformed;
            return res;
        }
        for (uint32_t i = 0; i < static_cast<uint32_t>(count); ++i) {
            const ContainerBlockRef& b = blocks[parts[i]];
            if (b.part_index != i) {
                res.status = PayloadStatus::Malformed;
                return res;
            }
        }
        return extract_concat_parts(file_bytes, blocks, parts, out_payload,
                                    options);
    }

    res.status = PayloadStatus::Unsupported;
    return res;
}

}  // namespace openmeta
