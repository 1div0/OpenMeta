#include "openmeta/build_info.h"
#include "openmeta/console_format.h"
#include "openmeta/container_payload.h"
#include "openmeta/container_scan.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/simple_meta.h"
#include "openmeta/xmp_decode.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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

    static const char* xmp_status_name(XmpDecodeStatus status) noexcept
    {
        switch (status) {
        case XmpDecodeStatus::Ok: return "ok";
        case XmpDecodeStatus::OutputTruncated: return "output_truncated";
        case XmpDecodeStatus::Unsupported: return "unsupported";
        case XmpDecodeStatus::Malformed: return "malformed";
        case XmpDecodeStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
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
        if (*out == XmpDecodeStatus::Ok) {
            return;
        }
        if (in == XmpDecodeStatus::Ok) {
            *out = in;
            return;
        }
    }

    static void xmp_sidecar_candidates(const char* path, std::string* out_a,
                                       std::string* out_b)
    {
        if (out_a) {
            out_a->clear();
        }
        if (out_b) {
            out_b->clear();
        }
        if (!path || !*path || !out_a || !out_b) {
            return;
        }

        const std::string s(path);
        *out_b = s;
        out_b->append(".xmp");

        const size_t sep = s.find_last_of("/\\");
        const size_t dot = s.find_last_of('.');
        if (dot != std::string::npos && (sep == std::string::npos || dot > sep)) {
            *out_a = s.substr(0, dot);
            out_a->append(".xmp");
        } else {
            *out_a = *out_b;
        }

        if (*out_a == *out_b) {
            out_b->clear();
        }
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
        case ContainerFormat::Avif: return "avif";
        case ContainerFormat::Cr3: return "cr3";
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

    enum class ReadFileStatus : uint8_t {
        Ok,
        OpenFailed,
        IoFailed,
        TooLarge,
    };

    static ReadFileStatus read_file_bytes(const char* path,
                                          std::vector<std::byte>* out,
                                          uint64_t max_file_bytes,
                                          uint64_t* out_size)
    {
        out->clear();
        if (out_size) {
            *out_size = 0;
        }
        std::FILE* f = std::fopen(path, "rb");
        if (!f) {
            return ReadFileStatus::OpenFailed;
        }

        if (std::fseek(f, 0, SEEK_END) != 0) {
            std::fclose(f);
            return ReadFileStatus::IoFailed;
        }
        const long end = std::ftell(f);
        if (end < 0) {
            std::fclose(f);
            return ReadFileStatus::IoFailed;
        }
        if (std::fseek(f, 0, SEEK_SET) != 0) {
            std::fclose(f);
            return ReadFileStatus::IoFailed;
        }

        const uint64_t size_u64 = static_cast<uint64_t>(end);
        if (out_size) {
            *out_size = size_u64;
        }
        if (max_file_bytes != 0U && size_u64 > max_file_bytes) {
            std::fclose(f);
            return ReadFileStatus::TooLarge;
        }

        const size_t size = static_cast<size_t>(size_u64);
        out->resize(size);
        if (size != 0) {
            const size_t read = std::fread(out->data(), 1, size, f);
            if (read != size) {
                std::fclose(f);
                out->clear();
                return ReadFileStatus::IoFailed;
            }
        }
        std::fclose(f);
        return ReadFileStatus::Ok;
    }

    static bool parse_u64_arg(const char* s, uint64_t* out)
    {
        if (!s || !*s) {
            return false;
        }
        char* end            = nullptr;
        unsigned long long v = std::strtoull(s, &end, 10);
        if (!end || *end != '\0') {
            return false;
        }
        *out = static_cast<uint64_t>(v);
        return true;
    }

    static void append_double_fixed6_trim(double d, std::string* out)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%.6f", d);
        size_t len = std::strlen(buf);
        while (len > 0 && buf[len - 1] == '0') {
            len -= 1;
        }
        if (len > 0 && buf[len - 1] == '.') {
            len -= 1;
        }
        out->append(buf, len);
    }

    static uint32_t meta_element_size(MetaElementType type) noexcept
    {
        switch (type) {
        case MetaElementType::U8:
        case MetaElementType::I8: return 1U;
        case MetaElementType::U16:
        case MetaElementType::I16: return 2U;
        case MetaElementType::U32:
        case MetaElementType::I32:
        case MetaElementType::F32: return 4U;
        case MetaElementType::U64:
        case MetaElementType::I64:
        case MetaElementType::F64: return 8U;
        case MetaElementType::URational:
        case MetaElementType::SRational: return 8U;
        }
        return 0U;
    }

    static void append_u64(uint64_t v, std::string* out)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llu",
                      static_cast<unsigned long long>(v));
        out->append(buf);
    }

    static void append_i64(int64_t v, std::string* out)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        out->append(buf);
    }

    static void append_element_raw(MetaElementType type,
                                   std::span<const std::byte> elem,
                                   std::string* out)
    {
        switch (type) {
        case MetaElementType::U8:
            append_u64(static_cast<uint64_t>(static_cast<uint8_t>(elem[0])),
                       out);
            return;
        case MetaElementType::I8:
            append_i64(static_cast<int64_t>(
                           static_cast<int8_t>(static_cast<uint8_t>(elem[0]))),
                       out);
            return;
        case MetaElementType::U16: {
            uint16_t v = 0;
            std::memcpy(&v, elem.data(), 2);
            append_u64(static_cast<uint64_t>(v), out);
            return;
        }
        case MetaElementType::I16: {
            int16_t v = 0;
            std::memcpy(&v, elem.data(), 2);
            append_i64(static_cast<int64_t>(v), out);
            return;
        }
        case MetaElementType::U32: {
            uint32_t v = 0;
            std::memcpy(&v, elem.data(), 4);
            append_u64(static_cast<uint64_t>(v), out);
            return;
        }
        case MetaElementType::I32: {
            int32_t v = 0;
            std::memcpy(&v, elem.data(), 4);
            append_i64(static_cast<int64_t>(v), out);
            return;
        }
        case MetaElementType::U64: {
            uint64_t v = 0;
            std::memcpy(&v, elem.data(), 8);
            append_u64(v, out);
            return;
        }
        case MetaElementType::I64: {
            int64_t v = 0;
            std::memcpy(&v, elem.data(), 8);
            append_i64(v, out);
            return;
        }
        case MetaElementType::F32: {
            uint32_t bits = 0;
            std::memcpy(&bits, elem.data(), 4);
            float f = 0.0F;
            std::memcpy(&f, &bits, sizeof(float));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(f));
            out->append(buf);
            return;
        }
        case MetaElementType::F64: {
            uint64_t bits = 0;
            std::memcpy(&bits, elem.data(), 8);
            double d = 0.0;
            std::memcpy(&d, &bits, sizeof(double));
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%g", d);
            out->append(buf);
            return;
        }
        case MetaElementType::URational: {
            URational r;
            std::memcpy(&r, elem.data(), sizeof(URational));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%u/%u",
                          static_cast<unsigned>(r.numer),
                          static_cast<unsigned>(r.denom));
            out->append(buf);
            return;
        }
        case MetaElementType::SRational: {
            SRational r;
            std::memcpy(&r, elem.data(), sizeof(SRational));
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d/%d", static_cast<int>(r.numer),
                          static_cast<int>(r.denom));
            out->append(buf);
            return;
        }
        }
    }

    static void append_element_value(MetaElementType type,
                                     std::span<const std::byte> elem,
                                     std::string* out)
    {
        if (type == MetaElementType::URational) {
            URational r;
            std::memcpy(&r, elem.data(), sizeof(URational));
            if (r.denom == 0U) {
                out->append("-");
                return;
            }
            append_double_fixed6_trim(static_cast<double>(r.numer)
                                          / static_cast<double>(r.denom),
                                      out);
            return;
        }
        if (type == MetaElementType::SRational) {
            SRational r;
            std::memcpy(&r, elem.data(), sizeof(SRational));
            if (r.denom == 0) {
                out->append("-");
                return;
            }
            append_double_fixed6_trim(static_cast<double>(r.numer)
                                          / static_cast<double>(r.denom),
                                      out);
            return;
        }
        append_element_raw(type, elem, out);
    }

    static uint32_t safe_array_count(const ByteArena& arena,
                                     const MetaValue& value) noexcept
    {
        if (value.kind != MetaValueKind::Array) {
            return value.count;
        }
        const std::span<const std::byte> raw = arena.span(value.data.span);
        const uint32_t elem_size = meta_element_size(value.elem_type);
        if (elem_size == 0U) {
            return 0U;
        }
        const uint32_t available = static_cast<uint32_t>(raw.size()
                                                         / elem_size);
        return (value.count < available) ? value.count : available;
    }

    static std::string value_type_string(const ByteArena& arena,
                                         const MetaValue& value)
    {
        switch (value.kind) {
        case MetaValueKind::Empty: return "empty";
        case MetaValueKind::Text: return "text";
        case MetaValueKind::Bytes: {
            const std::span<const std::byte> b = arena.span(value.data.span);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "bytes[%zu]", b.size());
            return std::string(buf);
        }
        case MetaValueKind::Array: {
            const uint32_t n = safe_array_count(arena, value);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "array[%u]",
                          static_cast<unsigned>(n));
            return std::string(buf);
        }
        case MetaValueKind::Scalar: break;
        }

        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64: return "u";
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64: return "i";
        case MetaElementType::F32: return "f32";
        case MetaElementType::F64: return "f64";
        case MetaElementType::URational: return "urational";
        case MetaElementType::SRational: return "srational";
        }
        return "scalar";
    }

    static void format_value_pair(const MetaStore& store,
                                  const MetaValue& value, uint32_t max_elements,
                                  uint32_t max_bytes, std::string* raw_out,
                                  std::string* val_out)
    {
        raw_out->clear();
        val_out->clear();

        const ByteArena& arena = store.arena();
        switch (value.kind) {
        case MetaValueKind::Empty:
            raw_out->append("-");
            val_out->append("-");
            return;
        case MetaValueKind::Text: {
            const std::string_view s = arena_string(arena, value.data.span);
            const bool dangerous = append_console_escaped_ascii(s, max_bytes,
                                                                raw_out);
            if (dangerous) {
                val_out->append("(DANGEROUS) ");
                val_out->append(*raw_out);
            } else {
                *val_out = *raw_out;
            }
            return;
        }
        case MetaValueKind::Bytes: {
            const std::span<const std::byte> b = arena.span(value.data.span);
            raw_out->append("0x");
            append_hex_bytes(b, max_bytes, raw_out);
            *val_out = *raw_out;
            return;
        }
        case MetaValueKind::Scalar: break;
        case MetaValueKind::Array: break;
        }

        if (value.kind == MetaValueKind::Scalar) {
            switch (value.elem_type) {
            case MetaElementType::U8:
            case MetaElementType::U16:
            case MetaElementType::U32:
            case MetaElementType::U64:
                append_u64(value.data.u64, raw_out);
                *val_out = *raw_out;
                return;
            case MetaElementType::I8:
            case MetaElementType::I16:
            case MetaElementType::I32:
            case MetaElementType::I64:
                append_i64(value.data.i64, raw_out);
                *val_out = *raw_out;
                return;
            case MetaElementType::F32: {
                float f = 0.0F;
                std::memcpy(&f, &value.data.f32_bits, sizeof(float));
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(f));
                raw_out->append(buf);
                *val_out = *raw_out;
                return;
            }
            case MetaElementType::F64: {
                double d = 0.0;
                std::memcpy(&d, &value.data.f64_bits, sizeof(double));
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%g", d);
                raw_out->append(buf);
                *val_out = *raw_out;
                return;
            }
            case MetaElementType::URational: {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%u/%u",
                              static_cast<unsigned>(value.data.ur.numer),
                              static_cast<unsigned>(value.data.ur.denom));
                raw_out->append(buf);
                if (value.data.ur.denom == 0U) {
                    val_out->append("-");
                } else {
                    append_double_fixed6_trim(
                        static_cast<double>(value.data.ur.numer)
                            / static_cast<double>(value.data.ur.denom),
                        val_out);
                }
                return;
            }
            case MetaElementType::SRational: {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%d/%d",
                              static_cast<int>(value.data.sr.numer),
                              static_cast<int>(value.data.sr.denom));
                raw_out->append(buf);
                if (value.data.sr.denom == 0) {
                    val_out->append("-");
                } else {
                    append_double_fixed6_trim(
                        static_cast<double>(value.data.sr.numer)
                            / static_cast<double>(value.data.sr.denom),
                        val_out);
                }
                return;
            }
            }
        }

        const std::span<const std::byte> raw = arena.span(value.data.span);
        const uint32_t elem_size = meta_element_size(value.elem_type);
        const uint32_t n         = safe_array_count(arena, value);
        const uint32_t shown     = (n < max_elements) ? n : max_elements;

        for (uint32_t i = 0; i < shown; ++i) {
            if (i != 0) {
                raw_out->append(", ");
                val_out->append(", ");
            }
            const std::span<const std::byte> elem
                = raw.subspan(static_cast<size_t>(i) * elem_size, elem_size);
            append_element_raw(value.elem_type, elem, raw_out);
            append_element_value(value.elem_type, elem, val_out);
        }
        if (shown < n) {
            raw_out->append(", ...");
            val_out->append(", ...");
        }
    }

    struct TableRow final {
        uint32_t idx = 0;
        std::string idx_s;
        std::string tag_s;
        std::string name_s;
        std::string tag_type_s;
        std::string count_s;
        std::string type_s;
        std::string raw_s;
        std::string val_s;
    };

    static bool row_less_by_idx(const TableRow& a, const TableRow& b) noexcept
    {
        return a.idx < b.idx;
    }

    static void print_line(char ch, size_t count)
    {
        for (size_t i = 0; i < count; ++i) {
            std::putchar(static_cast<int>(ch));
        }
        std::putchar('\n');
    }

    static void print_cell(std::string_view text, size_t width)
    {
        const size_t maxp = (text.size() < width) ? text.size() : width;
        const int w       = static_cast<int>(width);
        const int p       = static_cast<int>(maxp);
        std::printf("%-*.*s", w, p, text.data());
    }

    static void truncate_cell(std::string* s, uint32_t max_chars)
    {
        const size_t max_len = static_cast<size_t>(max_chars);
        if (max_len == 0 || s->size() <= max_len) {
            return;
        }
        if (max_len <= 3) {
            s->resize(max_len);
            return;
        }
        s->resize(max_len - 3U);
        s->append("...");
    }

    static void print_exif_block_table(const MetaStore& store, BlockId block,
                                       std::string_view ifd,
                                       std::span<const EntryId> ids,
                                       uint32_t max_elements,
                                       uint32_t max_bytes,
                                       uint32_t max_cell_chars)
    {
        std::vector<TableRow> rows;
        rows.reserve(ids.size());

        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& entry = store.entry(ids[i]);
            if (entry.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            const uint16_t tag       = entry.key.data.exif_tag.tag;
            const uint16_t wire_type = entry.origin.wire_type.code;

            TableRow row;
            row.idx = entry.origin.order_in_block;
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%u",
                              static_cast<unsigned>(row.idx));
                row.idx_s = buf;
            }
            {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "0x%04X",
                              static_cast<unsigned>(tag));
                row.tag_s = buf;
            }
            {
                const std::string_view name = exif_tag_name(ifd, tag);
                if (name.empty()) {
                    row.name_s = "-";
                } else {
                    row.name_s.assign(name.data(), name.size());
                }
            }
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%u(%s)",
                              static_cast<unsigned>(wire_type),
                              tiff_type_name(wire_type));
                row.tag_type_s = buf;
            }
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%u",
                              static_cast<unsigned>(entry.origin.wire_count));
                row.count_s = buf;
            }

            row.type_s = value_type_string(store.arena(), entry.value);
            format_value_pair(store, entry.value, max_elements, max_bytes,
                              &row.raw_s, &row.val_s);
            truncate_cell(&row.raw_s, max_cell_chars);
            truncate_cell(&row.val_s, max_cell_chars);

            rows.push_back(std::move(row));
        }

        std::sort(rows.begin(), rows.end(), row_less_by_idx);

        size_t w_idx      = std::strlen("idx");
        size_t w_tag      = ifd.size();
        size_t w_name     = std::strlen("name");
        size_t w_tag_type = std::strlen("tag type");
        size_t w_count    = std::strlen("count");
        size_t w_type     = std::strlen("type");
        size_t w_raw      = std::strlen("raw val");
        size_t w_val      = std::strlen("val");

        for (size_t i = 0; i < rows.size(); ++i) {
            const TableRow& r = rows[i];
            w_idx      = (r.idx_s.size() > w_idx) ? r.idx_s.size() : w_idx;
            w_tag      = (r.tag_s.size() > w_tag) ? r.tag_s.size() : w_tag;
            w_name     = (r.name_s.size() > w_name) ? r.name_s.size() : w_name;
            w_tag_type = (r.tag_type_s.size() > w_tag_type)
                             ? r.tag_type_s.size()
                             : w_tag_type;
            w_count = (r.count_s.size() > w_count) ? r.count_s.size() : w_count;
            w_type  = (r.type_s.size() > w_type) ? r.type_s.size() : w_type;
            w_raw   = (r.raw_s.size() > w_raw) ? r.raw_s.size() : w_raw;
            w_val   = (r.val_s.size() > w_val) ? r.val_s.size() : w_val;
        }

        const size_t total_width = 1U + w_idx + 3U + w_tag + 3U + w_name + 3U
                                   + w_tag_type + 3U + w_count + 3U + w_type
                                   + 3U + w_raw + 3U + w_val;

        print_line('=', total_width);
        std::printf(" ifd=%.*s block=%u entries=%zu\n",
                    static_cast<int>(ifd.size()), ifd.data(),
                    static_cast<unsigned>(block), rows.size());
        print_line('=', total_width);

        std::putchar(' ');
        print_cell("idx", w_idx);
        std::fputs(" | ", stdout);
        print_cell(ifd, w_tag);
        std::fputs(" | ", stdout);
        print_cell("name", w_name);
        std::fputs(" | ", stdout);
        print_cell("tag type", w_tag_type);
        std::fputs(" | ", stdout);
        print_cell("count", w_count);
        std::fputs(" | ", stdout);
        print_cell("type", w_type);
        std::fputs(" | ", stdout);
        print_cell("raw val", w_raw);
        std::fputs(" | ", stdout);
        print_cell("val", w_val);
        std::putchar('\n');

        print_line('-', total_width);

        for (size_t i = 0; i < rows.size(); ++i) {
            const TableRow& r = rows[i];
            std::putchar(' ');
            print_cell(r.idx_s, w_idx);
            std::fputs(" | ", stdout);
            print_cell(r.tag_s, w_tag);
            std::fputs(" | ", stdout);
            print_cell(r.name_s, w_name);
            std::fputs(" | ", stdout);
            print_cell(r.tag_type_s, w_tag_type);
            std::fputs(" | ", stdout);
            print_cell(r.count_s, w_count);
            std::fputs(" | ", stdout);
            print_cell(r.type_s, w_type);
            std::fputs(" | ", stdout);
            print_cell(r.raw_s, w_raw);
            std::fputs(" | ", stdout);
            print_cell(r.val_s, w_val);
            std::putchar('\n');
        }

        print_line('=', total_width);
    }

    static const char* icc_header_field_name(uint32_t offset) noexcept
    {
        switch (offset) {
        case 0: return "profile_size";
        case 4: return "cmm_type";
        case 8: return "version";
        case 12: return "class";
        case 16: return "data_space";
        case 20: return "pcs";
        case 24: return "date_time";
        case 36: return "signature";
        case 40: return "platform";
        case 44: return "flags";
        case 48: return "manufacturer";
        case 52: return "model";
        case 56: return "attributes";
        case 64: return "rendering_intent";
        case 68: return "pcs_illuminant";
        case 80: return "creator";
        case 84: return "profile_id";
        default: return "-";
        }
    }

    static std::string fourcc_string(uint32_t v)
    {
        char s[5];
        s[0] = static_cast<char>((v >> 24) & 0xFF);
        s[1] = static_cast<char>((v >> 16) & 0xFF);
        s[2] = static_cast<char>((v >> 8) & 0xFF);
        s[3] = static_cast<char>((v >> 0) & 0xFF);
        s[4] = '\0';

        bool printable = true;
        for (int i = 0; i < 4; ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c < 0x20U || c > 0x7EU) {
                printable = false;
                break;
            }
        }
        if (printable) {
            return std::string(s, 4);
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X",
                      static_cast<unsigned>(v));
        return std::string(buf);
    }

    static const char* photoshop_resource_name(uint16_t id) noexcept
    {
        switch (id) {
        case 0x0404: return "IPTC_NAA";
        case 0x0422: return "EXIF_DATA_1";
        case 0x0423: return "EXIF_DATA_3";
        default: return "-";
        }
    }

    static bool bytes_look_ascii(std::span<const std::byte> bytes) noexcept
    {
        if (bytes.empty()) {
            return false;
        }
        for (size_t i = 0; i < bytes.size(); ++i) {
            const unsigned char c
                = static_cast<unsigned char>(std::to_integer<uint8_t>(bytes[i]));
            if (c == 0) {
                return false;
            }
            if (c < 0x09U || (c > 0x0DU && c < 0x20U) || c > 0x7EU) {
                return false;
            }
        }
        return true;
    }

    static void print_generic_block_table(const MetaStore& store, BlockId block,
                                          std::string_view block_name,
                                          std::span<const EntryId> ids,
                                          uint32_t max_elements,
                                          uint32_t max_bytes,
                                          uint32_t max_cell_chars)
    {
        struct Row final {
            uint32_t idx = 0;
            std::string idx_s;
            std::string key_s;
            std::string name_s;
            std::string type_s;
            std::string raw_s;
            std::string val_s;
        };

        std::vector<Row> rows;
        rows.reserve(ids.size());

        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& entry = store.entry(ids[i]);

            Row row;
            row.idx = entry.origin.order_in_block;
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%u",
                              static_cast<unsigned>(row.idx));
                row.idx_s = buf;
            }

            switch (entry.key.kind) {
            case MetaKeyKind::IptcDataset: {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%u:%u",
                              static_cast<unsigned>(entry.key.data.iptc_dataset.record),
                              static_cast<unsigned>(entry.key.data.iptc_dataset.dataset));
                row.key_s = buf;
                row.name_s = "-";
                break;
            }
            case MetaKeyKind::PhotoshopIrb: {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "0x%04X",
                              static_cast<unsigned>(entry.key.data.photoshop_irb.resource_id));
                row.key_s  = buf;
                row.name_s = photoshop_resource_name(
                    entry.key.data.photoshop_irb.resource_id);
                break;
            }
            case MetaKeyKind::IccHeaderField: {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "0x%X",
                              static_cast<unsigned>(entry.key.data.icc_header_field.offset));
                row.key_s  = buf;
                row.name_s = icc_header_field_name(
                    entry.key.data.icc_header_field.offset);
                break;
            }
            case MetaKeyKind::IccTag: {
                row.key_s  = fourcc_string(entry.key.data.icc_tag.signature);
                row.name_s = "-";
                break;
            }
            default:
                row.key_s  = "-";
                row.name_s = "-";
                break;
            }

            row.type_s = value_type_string(store.arena(), entry.value);
            format_value_pair(store, entry.value, max_elements, max_bytes,
                              &row.raw_s, &row.val_s);

            if (entry.key.kind == MetaKeyKind::IccHeaderField
                && entry.value.kind == MetaValueKind::Bytes) {
                const std::span<const std::byte> b
                    = store.arena().span(entry.value.data.span);
                if (b.size() == 4 && bytes_look_ascii(b)) {
                    row.val_s.clear();
                    row.val_s.append(reinterpret_cast<const char*>(b.data()),
                                     b.size());
                }
            }

            if (entry.key.kind == MetaKeyKind::IptcDataset
                && entry.value.kind == MetaValueKind::Bytes) {
                const std::span<const std::byte> b
                    = store.arena().span(entry.value.data.span);
                if (bytes_look_ascii(b)) {
                    row.val_s.clear();
                    const std::string_view s(
                        reinterpret_cast<const char*>(b.data()), b.size());
                    const bool dangerous
                        = append_console_escaped_ascii(s, max_bytes, &row.val_s);
                    if (dangerous) {
                        row.val_s.insert(0, "(DANGEROUS) ");
                    }
                }
            }

            truncate_cell(&row.raw_s, max_cell_chars);
            truncate_cell(&row.val_s, max_cell_chars);
            rows.push_back(std::move(row));
        }

        struct RowLess final {
            bool operator()(const Row& a, const Row& b) const noexcept
            {
                return a.idx < b.idx;
            }
        };
        std::sort(rows.begin(), rows.end(), RowLess {});

        size_t w_idx  = std::strlen("idx");
        size_t w_key  = std::strlen("key");
        size_t w_name = std::strlen("name");
        size_t w_type = std::strlen("type");
        size_t w_raw  = std::strlen("raw val");
        size_t w_val  = std::strlen("val");

        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            w_idx  = (r.idx_s.size() > w_idx) ? r.idx_s.size() : w_idx;
            w_key  = (r.key_s.size() > w_key) ? r.key_s.size() : w_key;
            w_name = (r.name_s.size() > w_name) ? r.name_s.size() : w_name;
            w_type = (r.type_s.size() > w_type) ? r.type_s.size() : w_type;
            w_raw  = (r.raw_s.size() > w_raw) ? r.raw_s.size() : w_raw;
            w_val  = (r.val_s.size() > w_val) ? r.val_s.size() : w_val;
        }

        const size_t total_width = 1U + w_idx + 3U + w_key + 3U + w_name + 3U
                                   + w_type + 3U + w_raw + 3U + w_val;

        print_line('=', total_width);
        std::printf(" %.*s block=%u entries=%zu\n",
                    static_cast<int>(block_name.size()), block_name.data(),
                    static_cast<unsigned>(block), rows.size());
        print_line('=', total_width);

        std::putchar(' ');
        print_cell("idx", w_idx);
        std::fputs(" | ", stdout);
        print_cell("key", w_key);
        std::fputs(" | ", stdout);
        print_cell("name", w_name);
        std::fputs(" | ", stdout);
        print_cell("type", w_type);
        std::fputs(" | ", stdout);
        print_cell("raw val", w_raw);
        std::fputs(" | ", stdout);
        print_cell("val", w_val);
        std::putchar('\n');

        print_line('-', total_width);

        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            std::putchar(' ');
            print_cell(r.idx_s, w_idx);
            std::fputs(" | ", stdout);
            print_cell(r.key_s, w_key);
            std::fputs(" | ", stdout);
            print_cell(r.name_s, w_name);
            std::fputs(" | ", stdout);
            print_cell(r.type_s, w_type);
            std::fputs(" | ", stdout);
            print_cell(r.raw_s, w_raw);
            std::fputs(" | ", stdout);
            print_cell(r.val_s, w_val);
            std::putchar('\n');
        }

        print_line('=', total_width);
    }

    static void print_xmp_block_table(const MetaStore& store, BlockId block,
                                      std::span<const EntryId> ids,
                                      uint32_t max_elements,
                                      uint32_t max_bytes,
                                      uint32_t max_cell_chars)
    {
        struct Row final {
            uint32_t idx = 0;
            std::string idx_s;
            std::string schema_s;
            std::string path_s;
            std::string type_s;
            std::string raw_s;
            std::string val_s;
        };

        std::vector<Row> rows;
        rows.reserve(ids.size());

        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& entry = store.entry(ids[i]);
            if (entry.key.kind != MetaKeyKind::XmpProperty) {
                continue;
            }

            Row row;
            row.idx = entry.origin.order_in_block;
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%u",
                              static_cast<unsigned>(row.idx));
                row.idx_s = buf;
            }

            const std::string_view schema
                = arena_string(store.arena(), entry.key.data.xmp_property.schema_ns);
            const std::string_view path
                = arena_string(store.arena(),
                               entry.key.data.xmp_property.property_path);

            {
                const bool dangerous = append_console_escaped_ascii(
                    schema, max_bytes, &row.schema_s);
                if (dangerous) {
                    row.schema_s.insert(0, "(DANGEROUS) ");
                }
            }
            {
                const bool dangerous = append_console_escaped_ascii(
                    path, max_bytes, &row.path_s);
                if (dangerous) {
                    row.path_s.insert(0, "(DANGEROUS) ");
                }
            }

            row.type_s = value_type_string(store.arena(), entry.value);
            format_value_pair(store, entry.value, max_elements, max_bytes,
                              &row.raw_s, &row.val_s);

            truncate_cell(&row.schema_s, max_cell_chars);
            truncate_cell(&row.path_s, max_cell_chars);
            truncate_cell(&row.raw_s, max_cell_chars);
            truncate_cell(&row.val_s, max_cell_chars);

            rows.push_back(std::move(row));
        }

        struct RowLess final {
            bool operator()(const Row& a, const Row& b) const noexcept
            {
                return a.idx < b.idx;
            }
        };
        std::sort(rows.begin(), rows.end(), RowLess {});

        size_t w_idx    = std::strlen("idx");
        size_t w_schema = std::strlen("schema");
        size_t w_path   = std::strlen("path");
        size_t w_type   = std::strlen("type");
        size_t w_raw    = std::strlen("raw val");
        size_t w_val    = std::strlen("val");

        for (size_t i = 0; i < rows.size(); ++i) {
            w_idx    = (rows[i].idx_s.size() > w_idx) ? rows[i].idx_s.size() : w_idx;
            w_schema = (rows[i].schema_s.size() > w_schema) ? rows[i].schema_s.size() : w_schema;
            w_path   = (rows[i].path_s.size() > w_path) ? rows[i].path_s.size() : w_path;
            w_type   = (rows[i].type_s.size() > w_type) ? rows[i].type_s.size() : w_type;
            w_raw    = (rows[i].raw_s.size() > w_raw) ? rows[i].raw_s.size() : w_raw;
            w_val    = (rows[i].val_s.size() > w_val) ? rows[i].val_s.size() : w_val;
        }

        const size_t total_width = 1U + w_idx + 3U + w_schema + 3U + w_path
                                   + 3U + w_type + 3U + w_raw + 3U + w_val;

        print_line('=', total_width);
        std::printf(" xmp block=%u entries=%zu\n", static_cast<unsigned>(block),
                    rows.size());
        print_line('=', total_width);

        std::fputs(" ", stdout);
        print_cell("idx", w_idx);
        std::fputs(" | ", stdout);
        print_cell("schema", w_schema);
        std::fputs(" | ", stdout);
        print_cell("path", w_path);
        std::fputs(" | ", stdout);
        print_cell("type", w_type);
        std::fputs(" | ", stdout);
        print_cell("raw val", w_raw);
        std::fputs(" | ", stdout);
        print_cell("val", w_val);
        std::putchar('\n');

        print_line('-', total_width);

        for (size_t i = 0; i < rows.size(); ++i) {
            const Row& r = rows[i];
            std::fputs(" ", stdout);
            print_cell(r.idx_s, w_idx);
            std::fputs(" | ", stdout);
            print_cell(r.schema_s, w_schema);
            std::fputs(" | ", stdout);
            print_cell(r.path_s, w_path);
            std::fputs(" | ", stdout);
            print_cell(r.type_s, w_type);
            std::fputs(" | ", stdout);
            print_cell(r.raw_s, w_raw);
            std::fputs(" | ", stdout);
            print_cell(r.val_s, w_val);
            std::putchar('\n');
        }

        print_line('=', total_width);
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
        std::printf("  --version            print build info and exit\n");
        std::printf("  --no-build-info      hide build info header\n");
        std::printf("  --no-blocks           hide container block summary\n");
        std::printf("  --xmp-sidecar         also read sidecar XMP (<file>.xmp and <basename>.xmp)\n");
        std::printf(
            "  --no-pointer-tags     do not store pointer tags (0x8769/0x8825/0xA005/0x014A)\n");
        std::printf(
            "  --max-elements N      max array elements to print (default: 16)\n");
        std::printf(
            "  --max-bytes N         max bytes to print for text/bytes (default: 256)\n");
        std::printf(
            "  --max-cell-chars N    max chars per table cell (default: 32)\n");
        std::printf(
            "  --max-file-bytes N    refuse to read files larger than N bytes (default: 536870912; 0=unlimited)\n");
    }

    static void print_build_info_header()
    {
        std::string line1;
        std::string line2;
        format_build_info_lines(&line1, &line2);
        std::printf("%s\n", line1.c_str());
        std::printf("%s\n", line2.c_str());
    }

}  // namespace
}  // namespace openmeta

int
main(int argc, char** argv)
{
    using namespace openmeta;

    bool show_blocks     = true;
    bool show_build_info = true;
    bool xmp_sidecar     = false;
    ExifDecodeOptions exif_options;
    exif_options.include_pointer_tags = true;
    uint32_t max_elements             = 16;
    uint32_t max_bytes                = 256;
    uint32_t max_cell_chars           = 32;
    uint64_t max_file_bytes           = 512ULL * 1024ULL * 1024ULL;

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
        if (std::strcmp(arg, "--version") == 0) {
            print_build_info_header();
            return 0;
        }
        if (std::strcmp(arg, "--no-build-info") == 0) {
            show_build_info = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--no-blocks") == 0) {
            show_blocks = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--xmp-sidecar") == 0) {
            xmp_sidecar = true;
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
        if (std::strcmp(arg, "--max-cell-chars") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --max-cell-chars value\n");
                return 2;
            }
            max_cell_chars = v;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-file-bytes") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (!parse_u64_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --max-file-bytes value\n");
                return 2;
            }
            max_file_bytes = v;
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

    if (show_build_info) {
        print_build_info_header();
    }

    int exit_code = 0;
    for (int argi = first_path; argi < argc; ++argi) {
        const char* path = argv[argi];
        if (!path || !*path) {
            continue;
        }

        std::vector<std::byte> bytes;
        uint64_t file_size      = 0;
        const ReadFileStatus st = read_file_bytes(path, &bytes, max_file_bytes,
                                                  &file_size);
        if (st != ReadFileStatus::Ok) {
            if (st == ReadFileStatus::TooLarge) {
                std::fprintf(
                    stderr,
                    "metaread: refusing to read `%s` (size=%llu > --max-file-bytes=%llu)\n",
                    path, static_cast<unsigned long long>(file_size),
                    static_cast<unsigned long long>(max_file_bytes));
            } else if (st == ReadFileStatus::OpenFailed) {
                std::fprintf(stderr, "metaread: failed to open `%s`\n", path);
            } else {
                std::fprintf(stderr, "metaread: failed to read `%s`\n", path);
            }
            exit_code = 1;
            continue;
        }

        std::printf("== %s\n", path);
        std::printf("size=%zu\n", bytes.size());

        std::vector<ContainerBlockRef> blocks(128);
        std::vector<ExifIfdRef> ifd_refs(256);
        std::vector<std::byte> payload(1024 * 1024);
        std::vector<uint32_t> payload_parts(16384);

        MetaStore store;
        SimpleMetaResult read;
        PayloadOptions payload_options;
        for (;;) {
            store = MetaStore();
            read  = simple_meta_read(
                bytes, store,
                std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
                std::span<ExifIfdRef>(ifd_refs.data(), ifd_refs.size()),
                std::span<std::byte>(payload.data(), payload.size()),
                std::span<uint32_t>(payload_parts.data(), payload_parts.size()),
                exif_options, payload_options);

            if (read.scan.status == ScanStatus::OutputTruncated
                && read.scan.needed > blocks.size()) {
                blocks.resize(read.scan.needed);
                continue;
            }
            if (read.payload.status == PayloadStatus::OutputTruncated
                && read.payload.needed > payload.size()) {
                payload.resize(static_cast<size_t>(read.payload.needed));
                continue;
            }
            break;
        }

        if (xmp_sidecar) {
            std::string sidecar_a;
            std::string sidecar_b;
            xmp_sidecar_candidates(path, &sidecar_a, &sidecar_b);

            const char* candidates[2] = { sidecar_a.c_str(),
                                          sidecar_b.empty() ? nullptr
                                                            : sidecar_b.c_str() };
            for (size_t i = 0; i < 2; ++i) {
                const char* sp = candidates[i];
                if (!sp || !*sp) {
                    continue;
                }
                std::vector<std::byte> xmp_bytes;
                uint64_t sc_size = 0;
                const ReadFileStatus sc_st
                    = read_file_bytes(sp, &xmp_bytes, max_file_bytes, &sc_size);
                if (sc_st == ReadFileStatus::OpenFailed) {
                    continue;
                }
                if (sc_st != ReadFileStatus::Ok) {
                    if (sc_st == ReadFileStatus::TooLarge) {
                        std::fprintf(
                            stderr,
                            "metaread: refusing to read sidecar `%s` (size=%llu > --max-file-bytes=%llu)\n",
                            sp, static_cast<unsigned long long>(sc_size),
                            static_cast<unsigned long long>(max_file_bytes));
                    } else {
                        std::fprintf(stderr,
                                     "metaread: failed to read sidecar `%s`\n",
                                     sp);
                    }
                    exit_code = 1;
                    continue;
                }

                const XmpDecodeResult one = decode_xmp_packet(xmp_bytes, store);
                merge_xmp_status(&read.xmp.status, one.status);
                read.xmp.entries_decoded += one.entries_decoded;
                std::printf("xmp_sidecar=%s status=%s entries=%u\n", sp,
                            xmp_status_name(one.status),
                            static_cast<unsigned>(one.entries_decoded));
            }
        }

        std::printf("scan=%s written=%u needed=%u\n",
                    scan_status_name(read.scan.status), read.scan.written,
                    read.scan.needed);

        if (show_blocks) {
            for (uint32_t i = 0; i < read.scan.written; ++i) {
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

        store.finalize();
        std::printf("exif=%s ifds_decoded=%u xmp=%s xmp_entries=%u entries=%zu blocks=%u\n",
                    exif_status_name(read.exif.status),
                    static_cast<unsigned>(read.exif.ifds_written),
                    xmp_status_name(read.xmp.status),
                    static_cast<unsigned>(read.xmp.entries_decoded),
                    store.entries().size(),
                    static_cast<unsigned>(store.block_count()));

        for (BlockId block = 0; block < store.block_count(); ++block) {
            const std::span<const EntryId> ids = store.entries_in_block(block);
            if (ids.empty()) {
                continue;
            }
            const Entry& first = store.entry(ids[0]);
            if (first.key.kind == MetaKeyKind::ExifTag) {
                const std::string_view ifd
                    = arena_string(store.arena(), first.key.data.exif_tag.ifd);
                print_exif_block_table(store, block, ifd, ids, max_elements,
                                       max_bytes, max_cell_chars);
            } else if (first.key.kind == MetaKeyKind::IccHeaderField
                       || first.key.kind == MetaKeyKind::IccTag) {
                print_generic_block_table(store, block, "icc", ids,
                                          max_elements, max_bytes,
                                          max_cell_chars);
            } else if (first.key.kind == MetaKeyKind::IptcDataset) {
                print_generic_block_table(store, block, "iptc_iim", ids,
                                          max_elements, max_bytes,
                                          max_cell_chars);
            } else if (first.key.kind == MetaKeyKind::XmpProperty) {
                print_xmp_block_table(store, block, ids, max_elements,
                                      max_bytes, max_cell_chars);
            } else if (first.key.kind == MetaKeyKind::PhotoshopIrb) {
                print_generic_block_table(store, block, "photoshop_irb", ids,
                                          max_elements, max_bytes,
                                          max_cell_chars);
            }
        }
    }

    return exit_code;
}
