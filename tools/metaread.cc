#include "openmeta/container_scan.h"
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static const char* scan_status_name(ScanStatus status) noexcept
    {
        switch (status) {
        case ScanStatus::Ok: return "ok";
        case ScanStatus::OutputTruncated: return "output_truncated";
        case ScanStatus::Unsupported: return "unsupported";
        case ScanStatus::Malformed: return "malformed";
        }
        return "unknown";
    }

    static const char* exif_status_name(ExifDecodeStatus status) noexcept
    {
        switch (status) {
        case ExifDecodeStatus::Ok: return "ok";
        case ExifDecodeStatus::OutputTruncated: return "output_truncated";
        case ExifDecodeStatus::Unsupported: return "unsupported";
        case ExifDecodeStatus::Malformed: return "malformed";
        case ExifDecodeStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }

    static const char* format_name(ContainerFormat format) noexcept
    {
        switch (format) {
        case ContainerFormat::Unknown: return "unknown";
        case ContainerFormat::Jpeg: return "jpeg";
        case ContainerFormat::Png: return "png";
        case ContainerFormat::Webp: return "webp";
        case ContainerFormat::Gif: return "gif";
        case ContainerFormat::Tiff: return "tiff";
        case ContainerFormat::Jp2: return "jp2";
        case ContainerFormat::Jxl: return "jxl";
        case ContainerFormat::Heif: return "heif";
        }
        return "unknown";
    }

    static const char* block_kind_name(ContainerBlockKind kind) noexcept
    {
        switch (kind) {
        case ContainerBlockKind::Unknown: return "unknown";
        case ContainerBlockKind::Exif: return "exif";
        case ContainerBlockKind::MakerNote: return "makernote";
        case ContainerBlockKind::Xmp: return "xmp";
        case ContainerBlockKind::XmpExtended: return "xmp_extended";
        case ContainerBlockKind::Icc: return "icc";
        case ContainerBlockKind::IptcIim: return "iptc_iim";
        case ContainerBlockKind::PhotoshopIrB: return "photoshop_irb";
        case ContainerBlockKind::Mpf: return "mpf";
        case ContainerBlockKind::Comment: return "comment";
        case ContainerBlockKind::Text: return "text";
        case ContainerBlockKind::CompressedMetadata:
            return "compressed_metadata";
        }
        return "unknown";
    }

    static const char* compression_name(BlockCompression compression) noexcept
    {
        switch (compression) {
        case BlockCompression::None: return "none";
        case BlockCompression::Deflate: return "deflate";
        case BlockCompression::Brotli: return "brotli";
        }
        return "unknown";
    }

    static const char* chunking_name(BlockChunking chunking) noexcept
    {
        switch (chunking) {
        case BlockChunking::None: return "none";
        case BlockChunking::JpegApp2SeqTotal: return "jpeg_app2_seq_total";
        case BlockChunking::JpegXmpExtendedGuidOffset:
            return "jpeg_xmp_extended_guid_offset";
        case BlockChunking::GifSubBlocks: return "gif_sub_blocks";
        case BlockChunking::BmffExifTiffOffsetU32Be:
            return "bmff_exif_tiff_offset_u32be";
        case BlockChunking::BrobU32BeRealTypePrefix:
            return "brob_u32be_real_type_prefix";
        case BlockChunking::Jp2UuidPayload: return "jp2_uuid_payload";
        case BlockChunking::PsIrB8Bim: return "ps_irb_8bim";
        }
        return "unknown";
    }

    static const char* tiff_type_name(uint16_t code) noexcept
    {
        switch (code) {
        case 1: return "BYTE";
        case 2: return "ASCII";
        case 3: return "SHORT";
        case 4: return "LONG";
        case 5: return "RATIONAL";
        case 6: return "SBYTE";
        case 7: return "UNDEFINED";
        case 8: return "SSHORT";
        case 9: return "SLONG";
        case 10: return "SRATIONAL";
        case 11: return "FLOAT";
        case 12: return "DOUBLE";
        case 13: return "IFD";
        case 16: return "LONG8";
        case 17: return "SLONG8";
        case 18: return "IFD8";
        case 129: return "UTF8";
        default: return "UNKNOWN";
        }
    }

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static void print_escaped(std::string_view s, uint32_t max_bytes)
    {
        const uint32_t n = (s.size() < max_bytes)
                               ? static_cast<uint32_t>(s.size())
                               : max_bytes;
        std::putchar('"');
        for (uint32_t i = 0; i < n; ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == '\\' || c == '"') {
                std::putchar('\\');
                std::putchar(static_cast<int>(c));
                continue;
            }
            if (c == '\n') {
                std::fputs("\\n", stdout);
                continue;
            }
            if (c == '\r') {
                std::fputs("\\r", stdout);
                continue;
            }
            if (c == '\t') {
                std::fputs("\\t", stdout);
                continue;
            }
            if (c < 0x20U || c == 0x7FU) {
                std::printf("\\x%02X", static_cast<unsigned>(c));
                continue;
            }
            std::putchar(static_cast<int>(c));
        }
        if (n < s.size()) {
            std::fputs("...", stdout);
        }
        std::putchar('"');
    }

    static void print_hex(std::span<const std::byte> bytes, uint32_t max_bytes)
    {
        const uint32_t n = (bytes.size() < max_bytes)
                               ? static_cast<uint32_t>(bytes.size())
                               : max_bytes;
        for (uint32_t i = 0; i < n; ++i) {
            const unsigned v = static_cast<unsigned>(
                static_cast<uint8_t>(bytes[i]));
            std::printf("%02X", v);
        }
        if (n < bytes.size()) {
            std::fputs("...", stdout);
        }
    }

    static bool read_file_bytes(const char* path, std::vector<std::byte>* out)
    {
        out->clear();
        std::FILE* f = std::fopen(path, "rb");
        if (!f) {
            return false;
        }

        if (std::fseek(f, 0, SEEK_END) != 0) {
            std::fclose(f);
            return false;
        }
        const long end = std::ftell(f);
        if (end < 0) {
            std::fclose(f);
            return false;
        }
        if (std::fseek(f, 0, SEEK_SET) != 0) {
            std::fclose(f);
            return false;
        }

        const size_t size = static_cast<size_t>(end);
        out->resize(size);
        if (size > 0) {
            const size_t read = std::fread(out->data(), 1, size, f);
            if (read != size) {
                std::fclose(f);
                out->clear();
                return false;
            }
        }
        std::fclose(f);
        return true;
    }

    static bool looks_like_tiff(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.size() < 4) {
            return false;
        }
        const uint8_t b0 = static_cast<uint8_t>(bytes[0]);
        const uint8_t b1 = static_cast<uint8_t>(bytes[1]);
        if (!((b0 == 0x49 && b1 == 0x49) || (b0 == 0x4D && b1 == 0x4D))) {
            return false;
        }
        const uint16_t v = (b0 == 0x49)
                               ? static_cast<uint16_t>(
                                     static_cast<uint8_t>(bytes[2])
                                     | (static_cast<uint8_t>(bytes[3]) << 8))
                               : static_cast<uint16_t>(
                                     (static_cast<uint8_t>(bytes[2]) << 8)
                                     | static_cast<uint8_t>(bytes[3]));
        return v == 42 || v == 43;
    }

    static void print_value(const MetaStore& store, const MetaValue& value,
                            uint32_t max_elements, uint32_t max_bytes)
    {
        const ByteArena& arena = store.arena();
        switch (value.kind) {
        case MetaValueKind::Empty: std::fputs("empty", stdout); return;
        case MetaValueKind::Scalar: break;
        case MetaValueKind::Text: {
            const std::string_view s = arena_string(arena, value.data.span);
            std::fputs("text=", stdout);
            print_escaped(s, max_bytes);
            return;
        }
        case MetaValueKind::Bytes: {
            const std::span<const std::byte> b = arena.span(value.data.span);
            std::printf("bytes[%zu]=", b.size());
            print_hex(b, max_bytes);
            return;
        }
        case MetaValueKind::Array: break;
        }

        if (value.kind == MetaValueKind::Scalar) {
            switch (value.elem_type) {
            case MetaElementType::U8:
            case MetaElementType::U16:
            case MetaElementType::U32:
            case MetaElementType::U64:
                std::printf("u=%llu",
                            static_cast<unsigned long long>(value.data.u64));
                return;
            case MetaElementType::I8:
            case MetaElementType::I16:
            case MetaElementType::I32:
            case MetaElementType::I64:
                std::printf("i=%lld", static_cast<long long>(value.data.i64));
                return;
            case MetaElementType::F32: {
                float f = 0.0F;
                std::memcpy(&f, &value.data.f32_bits, sizeof(float));
                std::printf("f32=%g", static_cast<double>(f));
                return;
            }
            case MetaElementType::F64: {
                double d = 0.0;
                std::memcpy(&d, &value.data.f64_bits, sizeof(double));
                std::printf("f64=%g", d);
                return;
            }
            case MetaElementType::URational:
                std::printf("urational=%u/%u", value.data.ur.numer,
                            value.data.ur.denom);
                return;
            case MetaElementType::SRational:
                std::printf("srational=%d/%d", value.data.sr.numer,
                            value.data.sr.denom);
                return;
            }
            std::fputs("scalar", stdout);
            return;
        }

        const std::span<const std::byte> raw = arena.span(value.data.span);
        uint32_t elem_size                   = 1;
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::I8: elem_size = 1; break;
        case MetaElementType::U16:
        case MetaElementType::I16: elem_size = 2; break;
        case MetaElementType::U32:
        case MetaElementType::I32:
        case MetaElementType::F32: elem_size = 4; break;
        case MetaElementType::U64:
        case MetaElementType::I64:
        case MetaElementType::F64: elem_size = 8; break;
        case MetaElementType::URational:
        case MetaElementType::SRational: elem_size = 8; break;
        }

        const uint32_t available
            = (elem_size > 0U) ? static_cast<uint32_t>(raw.size() / elem_size)
                               : 0U;
        const uint32_t n = (value.count < available) ? value.count : available;
        const uint32_t shown = (n < max_elements) ? n : max_elements;

        std::printf("array[%u]=", n);
        std::putchar('[');
        for (uint32_t i = 0; i < shown; ++i) {
            if (i != 0) {
                std::fputs(", ", stdout);
            }
            const std::span<const std::byte> elem
                = raw.subspan(static_cast<size_t>(i) * elem_size, elem_size);
            switch (value.elem_type) {
            case MetaElementType::U8:
                std::printf("%u", static_cast<unsigned>(
                                      static_cast<uint8_t>(elem[0])));
                break;
            case MetaElementType::I8:
                std::printf("%d", static_cast<int>(static_cast<int8_t>(
                                      static_cast<uint8_t>(elem[0]))));
                break;
            case MetaElementType::U16: {
                uint16_t v = 0;
                std::memcpy(&v, elem.data(), 2);
                std::printf("%u", static_cast<unsigned>(v));
                break;
            }
            case MetaElementType::I16: {
                int16_t v = 0;
                std::memcpy(&v, elem.data(), 2);
                std::printf("%d", static_cast<int>(v));
                break;
            }
            case MetaElementType::U32: {
                uint32_t v = 0;
                std::memcpy(&v, elem.data(), 4);
                std::printf("%u", v);
                break;
            }
            case MetaElementType::I32: {
                int32_t v = 0;
                std::memcpy(&v, elem.data(), 4);
                std::printf("%d", v);
                break;
            }
            case MetaElementType::U64: {
                uint64_t v = 0;
                std::memcpy(&v, elem.data(), 8);
                std::printf("%llu", static_cast<unsigned long long>(v));
                break;
            }
            case MetaElementType::I64: {
                int64_t v = 0;
                std::memcpy(&v, elem.data(), 8);
                std::printf("%lld", static_cast<long long>(v));
                break;
            }
            case MetaElementType::F32: {
                uint32_t bits = 0;
                std::memcpy(&bits, elem.data(), 4);
                float f = 0.0F;
                std::memcpy(&f, &bits, sizeof(float));
                std::printf("%g", static_cast<double>(f));
                break;
            }
            case MetaElementType::F64: {
                uint64_t bits = 0;
                std::memcpy(&bits, elem.data(), 8);
                double d = 0.0;
                std::memcpy(&d, &bits, sizeof(double));
                std::printf("%g", d);
                break;
            }
            case MetaElementType::URational: {
                URational r;
                std::memcpy(&r, elem.data(), sizeof(URational));
                std::printf("%u/%u", r.numer, r.denom);
                break;
            }
            case MetaElementType::SRational: {
                SRational r;
                std::memcpy(&r, elem.data(), sizeof(SRational));
                std::printf("%d/%d", r.numer, r.denom);
                break;
            }
            }
        }
        if (shown < n) {
            std::fputs(", ...", stdout);
        }
        std::putchar(']');
    }

    static void print_entry(const MetaStore& store, const Entry& entry,
                            uint32_t max_elements, uint32_t max_bytes)
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return;
        }

        const std::string_view ifd = arena_string(store.arena(),
                                                  entry.key.data.exif_tag.ifd);
        const uint16_t tag         = entry.key.data.exif_tag.tag;
        const uint16_t wire_type   = entry.origin.wire_type.code;

        std::printf("  [%u] %.*s tag=0x%04X type=%u(%s) count=%u ",
                    entry.origin.order_in_block, static_cast<int>(ifd.size()),
                    ifd.data(), static_cast<unsigned>(tag),
                    static_cast<unsigned>(wire_type), tiff_type_name(wire_type),
                    static_cast<unsigned>(entry.origin.wire_count));
        print_value(store, entry.value, max_elements, max_bytes);
        std::putchar('\n');
    }

    static bool parse_u32_arg(const char* s, uint32_t* out)
    {
        if (!s || !*s) {
            return false;
        }
        char* end       = nullptr;
        unsigned long v = std::strtoul(s, &end, 10);
        if (!end || *end != '\0') {
            return false;
        }
        if (v > 0xFFFFFFFFUL) {
            return false;
        }
        *out = static_cast<uint32_t>(v);
        return true;
    }

    static void usage(const char* argv0)
    {
        std::printf("usage: %s [options] <file> [file...]\n", argv0);
        std::printf("options:\n");
        std::printf("  --no-blocks           hide container block summary\n");
        std::printf(
            "  --no-pointer-tags     do not store pointer tags (0x8769/0x8825/0xA005/0x014A)\n");
        std::printf(
            "  --max-elements N      max array elements to print (default: 16)\n");
        std::printf(
            "  --max-bytes N         max bytes to print for text/bytes (default: 256)\n");
    }

}  // namespace
}  // namespace openmeta

int
main(int argc, char** argv)
{
    using namespace openmeta;

    bool show_blocks = true;
    ExifDecodeOptions exif_options;
    exif_options.include_pointer_tags = true;
    uint32_t max_elements             = 16;
    uint32_t max_bytes                = 256;

    int first_path = 1;
    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!arg) {
            continue;
        }
        if (std::strcmp(arg, "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (std::strcmp(arg, "--no-blocks") == 0) {
            show_blocks = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--no-pointer-tags") == 0) {
            exif_options.include_pointer_tags = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-elements") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --max-elements value\n");
                return 2;
            }
            max_elements = v;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-bytes") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --max-bytes value\n");
                return 2;
            }
            max_bytes = v;
            i += 1;
            first_path += 2;
            continue;
        }
        break;
    }

    if (argc <= first_path) {
        usage(argv[0]);
        return 2;
    }

    int exit_code = 0;
    for (int argi = first_path; argi < argc; ++argi) {
        const char* path = argv[argi];
        if (!path || !*path) {
            continue;
        }

        std::vector<std::byte> bytes;
        if (!read_file_bytes(path, &bytes)) {
            std::fprintf(stderr, "openmeta_metaread: failed to read `%s`\n",
                         path);
            exit_code = 1;
            continue;
        }

        std::printf("== %s\n", path);
        std::printf("size=%zu\n", bytes.size());

        std::vector<ContainerBlockRef> blocks;
        blocks.resize(128);
        ScanResult scan;
        for (;;) {
            scan = scan_auto(bytes,
                             std::span<ContainerBlockRef>(blocks.data(),
                                                          blocks.size()));
            if (scan.status != ScanStatus::OutputTruncated) {
                break;
            }
            if (scan.needed <= blocks.size()) {
                break;
            }
            blocks.resize(scan.needed);
        }

        std::printf("scan=%s written=%u needed=%u\n",
                    scan_status_name(scan.status), scan.written, scan.needed);

        if (show_blocks) {
            for (uint32_t i = 0; i < scan.written; ++i) {
                const ContainerBlockRef& b = blocks[i];
                std::printf(
                    "block[%u] format=%s kind=%s comp=%s chunking=%s id=0x%08X outer=(%llu,%llu) data=(%llu,%llu)\n",
                    i, format_name(b.format), block_kind_name(b.kind),
                    compression_name(b.compression), chunking_name(b.chunking),
                    static_cast<unsigned>(b.id),
                    static_cast<unsigned long long>(b.outer_offset),
                    static_cast<unsigned long long>(b.outer_size),
                    static_cast<unsigned long long>(b.data_offset),
                    static_cast<unsigned long long>(b.data_size));
            }
        }

        MetaStore store;
        ExifDecodeResult exif_total;
        exif_total.status          = ExifDecodeStatus::Unsupported;
        exif_total.ifds_written    = 0;
        exif_total.ifds_needed     = 0;
        exif_total.entries_decoded = 0;

        bool any_exif = false;
        std::vector<ExifIfdRef> ifd_refs;
        ifd_refs.resize(256);

        for (uint32_t i = 0; i < scan.written; ++i) {
            const ContainerBlockRef& b = blocks[i];
            if (b.kind != ContainerBlockKind::Exif) {
                continue;
            }
            if (b.data_offset + b.data_size > bytes.size()) {
                continue;
            }

            const std::span<const std::byte> tiff
                = std::span<const std::byte>(bytes.data(), bytes.size())
                      .subspan(static_cast<size_t>(b.data_offset),
                               static_cast<size_t>(b.data_size));

            ExifDecodeResult one
                = decode_exif_tiff(tiff, store,
                                   std::span<ExifIfdRef>(ifd_refs.data(),
                                                         ifd_refs.size()),
                                   exif_options);

            const bool first = !any_exif;
            any_exif         = true;
            if (first) {
                exif_total = one;
            } else {
                if (one.status == ExifDecodeStatus::LimitExceeded) {
                    exif_total.status = one.status;
                } else if (one.status == ExifDecodeStatus::Malformed
                           && exif_total.status
                                  != ExifDecodeStatus::LimitExceeded) {
                    exif_total.status = one.status;
                } else if (one.status == ExifDecodeStatus::OutputTruncated
                           && exif_total.status == ExifDecodeStatus::Ok) {
                    exif_total.status = one.status;
                } else if (one.status == ExifDecodeStatus::Ok
                           && exif_total.status
                                  == ExifDecodeStatus::Unsupported) {
                    exif_total.status = one.status;
                }
            }
            exif_total.ifds_needed += one.ifds_needed;
            exif_total.entries_decoded += one.entries_decoded;
        }

        if (!any_exif && looks_like_tiff(bytes)) {
            ExifDecodeResult one
                = decode_exif_tiff(bytes, store,
                                   std::span<ExifIfdRef>(ifd_refs.data(),
                                                         ifd_refs.size()),
                                   exif_options);
            any_exif   = true;
            exif_total = one;
        }

        if (!any_exif) {
            std::printf("exif=%s\n",
                        exif_status_name(ExifDecodeStatus::Unsupported));
            continue;
        }

        store.finalize();
        std::printf("exif=%s ifds_decoded=%u entries=%zu\n",
                    exif_status_name(exif_total.status), store.block_count(),
                    store.entries().size());

        for (BlockId block = 0; block < store.block_count(); ++block) {
            const std::span<const EntryId> ids = store.entries_in_block(block);
            if (ids.empty()) {
                continue;
            }
            const Entry& first = store.entry(ids[0]);
            if (first.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            const std::string_view ifd
                = arena_string(store.arena(), first.key.data.exif_tag.ifd);
            std::printf("ifd=%.*s block=%u entries=%zu\n",
                        static_cast<int>(ifd.size()), ifd.data(),
                        static_cast<unsigned>(block), ids.size());
            for (size_t i = 0; i < ids.size(); ++i) {
                const Entry& e = store.entry(ids[i]);
                print_entry(store, e, max_elements, max_bytes);
            }
        }
    }

    return exit_code;
}
