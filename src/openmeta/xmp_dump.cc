#include "openmeta/xmp_dump.h"

#include "openmeta/exif_tag_names.h"
#include "openmeta/geotiff_key_names.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace openmeta {
namespace {

    static constexpr std::string_view kXmpNsX = "adobe:ns:meta/";
    static constexpr std::string_view kXmpNsRdf
        = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    static constexpr std::string_view kXmpNsOpenMetaDump
        = "urn:openmeta:dump:1.0";

    static constexpr std::string_view kXmpNsXmp = "http://ns.adobe.com/xap/1.0/";
    static constexpr std::string_view kXmpNsTiff
        = "http://ns.adobe.com/tiff/1.0/";
    static constexpr std::string_view kXmpNsExif
        = "http://ns.adobe.com/exif/1.0/";
    static constexpr std::string_view kXmpNsDc
        = "http://purl.org/dc/elements/1.1/";

    static constexpr const char* kIndent1 = "  ";
    static constexpr const char* kIndent2 = "    ";
    static constexpr const char* kIndent3 = "      ";
    static constexpr const char* kIndent4 = "        ";

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> b = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(b.data()),
                                b.size());
    }


    struct SpanWriter final {
        std::span<std::byte> out;
        uint64_t max_output = 0;
        uint64_t written    = 0;
        uint64_t needed     = 0;
        bool limit_hit      = false;

        explicit SpanWriter(std::span<std::byte> dst,
                            uint64_t max_output_bytes) noexcept
            : out(dst)
            , max_output(max_output_bytes)
        {
        }

        void note_bytes(uint64_t n) noexcept
        {
            if (limit_hit) {
                return;
            }
            if (max_output != 0U && n > (UINT64_MAX - needed)) {
                limit_hit = true;
                return;
            }
            const uint64_t next = needed + n;
            if (max_output != 0U && next > max_output) {
                limit_hit = true;
                return;
            }
            needed = next;
        }

        void append_bytes(const void* data, uint64_t n) noexcept
        {
            if (!data || n == 0U) {
                return;
            }
            if (limit_hit) {
                return;
            }

            note_bytes(n);
            if (limit_hit) {
                return;
            }

            const uint64_t cap  = static_cast<uint64_t>(out.size());
            const uint64_t w    = (written < cap) ? (cap - written) : 0U;
            const uint64_t take = (n < w) ? n : w;
            if (take > 0U) {
                std::memcpy(out.data() + static_cast<size_t>(written), data,
                            static_cast<size_t>(take));
                written += take;
            }
        }

        void append(std::string_view s) noexcept
        {
            append_bytes(s.data(), static_cast<uint64_t>(s.size()));
        }

        void append_char(char c) noexcept { append_bytes(&c, 1U); }
    };

    struct XmpNsDecl final {
        std::string_view prefix;
        std::string_view uri;
    };

    static void emit_xmp_packet_begin(SpanWriter* w,
                                      std::span<const XmpNsDecl> decls) noexcept
    {
        if (!w) {
            return;
        }

        w->append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
        w->append("<x:xmpmeta xmlns:x=\"");
        w->append(kXmpNsX);
        w->append("\" x:xmptk=\"OpenMeta\">\n");
        w->append(kIndent1);
        w->append("<rdf:RDF xmlns:rdf=\"");
        w->append(kXmpNsRdf);
        w->append("\"");
        for (size_t i = 0; i < decls.size(); ++i) {
            const XmpNsDecl& d = decls[i];
            if (d.prefix.empty() || d.uri.empty()) {
                continue;
            }
            w->append(" xmlns:");
            w->append(d.prefix);
            w->append("=\"");
            w->append(d.uri);
            w->append("\"");
        }
        w->append(">\n");
        w->append(kIndent2);
        w->append("<rdf:Description rdf:about=\"\">\n");
    }

    static void emit_xmp_packet_end(SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        w->append(kIndent2);
        w->append("</rdf:Description>\n");
        w->append(kIndent1);
        w->append("</rdf:RDF>\n");
        w->append("</x:xmpmeta>\n");
    }


    static void append_u64_dec(uint64_t v, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llu",
                      static_cast<unsigned long long>(v));
        w->append(buf);
    }

    static void append_u32_hex(uint32_t v, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(v));
        w->append(buf);
    }

    static void append_u16_hex(uint16_t v, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%04X", static_cast<unsigned>(v));
        w->append(buf);
    }


    static void append_xml_safe_ascii(std::string_view s,
                                      SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        for (size_t i = 0; i < s.size(); ++i) {
            const uint8_t c = static_cast<uint8_t>(s[i]);
            if (c == static_cast<uint8_t>('&')) {
                w->append("&amp;");
                continue;
            }
            if (c == static_cast<uint8_t>('<')) {
                w->append("&lt;");
                continue;
            }
            if (c == static_cast<uint8_t>('>')) {
                w->append("&gt;");
                continue;
            }
            if (c >= 0x20U && c <= 0x7EU) {
                w->append_char(static_cast<char>(c));
                continue;
            }

            // Emit a deterministic ASCII escape for anything non-printable or non-ASCII.
            char buf[5];
            static constexpr char hex[] = "0123456789ABCDEF";
            buf[0]                      = '\\';
            buf[1]                      = 'x';
            buf[2]                      = hex[(c >> 4) & 0x0F];
            buf[3]                      = hex[(c >> 0) & 0x0F];
            buf[4]                      = '\0';
            w->append(buf);
        }
    }

    static void append_xml_safe_utf8(std::string_view s, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }

        for (size_t i = 0; i < s.size(); ++i) {
            const uint8_t c = static_cast<uint8_t>(s[i]);
            if (c == static_cast<uint8_t>('&')) {
                w->append("&amp;");
                continue;
            }
            if (c == static_cast<uint8_t>('<')) {
                w->append("&lt;");
                continue;
            }
            if (c == static_cast<uint8_t>('>')) {
                w->append("&gt;");
                continue;
            }
            if (c == static_cast<uint8_t>('\"')) {
                w->append("&quot;");
                continue;
            }
            if (c == static_cast<uint8_t>('\'')) {
                w->append("&apos;");
                continue;
            }

            // XML 1.0 allows TAB/CR/LF and 0x20..; escape other control bytes.
            if (c == 0x09U || c == 0x0AU || c == 0x0DU
                || (c >= 0x20U && c != 0x7FU)) {
                w->append_char(static_cast<char>(c));
                continue;
            }

            char buf[5];
            static constexpr char hex[] = "0123456789ABCDEF";
            buf[0]                      = '\\';
            buf[1]                      = 'x';
            buf[2]                      = hex[(c >> 4) & 0x0F];
            buf[3]                      = hex[(c >> 0) & 0x0F];
            buf[4]                      = '\0';
            w->append(buf);
        }
    }


    struct Base64Encoder final {
        SpanWriter* w     = nullptr;
        uint8_t buf[3]    = { 0, 0, 0 };
        uint32_t buffered = 0;

        explicit Base64Encoder(SpanWriter* writer) noexcept
            : w(writer)
        {
        }

        void emit_triplet(uint8_t a, uint8_t b, uint8_t c) noexcept
        {
            static constexpr char kEnc[]
                = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            char out4[4];
            out4[0] = kEnc[(a >> 2) & 0x3F];
            out4[1] = kEnc[((a & 0x03) << 4) | ((b >> 4) & 0x0F)];
            out4[2] = kEnc[((b & 0x0F) << 2) | ((c >> 6) & 0x03)];
            out4[3] = kEnc[c & 0x3F];
            w->append_bytes(out4, 4);
        }

        void append_u8(uint8_t v) noexcept
        {
            if (!w || w->limit_hit) {
                return;
            }
            buf[buffered] = v;
            buffered += 1;
            if (buffered == 3U) {
                emit_triplet(buf[0], buf[1], buf[2]);
                buffered = 0;
            }
        }

        void append(std::span<const std::byte> bytes) noexcept
        {
            if (!w || w->limit_hit) {
                return;
            }
            for (size_t i = 0; i < bytes.size(); ++i) {
                append_u8(static_cast<uint8_t>(bytes[i]));
                if (w->limit_hit) {
                    return;
                }
            }
        }

        void finish() noexcept
        {
            if (!w || w->limit_hit) {
                return;
            }
            static constexpr char kEnc[]
                = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            if (buffered == 0U) {
                return;
            }
            if (buffered == 1U) {
                const uint8_t a = buf[0];
                char out4[4];
                out4[0] = kEnc[(a >> 2) & 0x3F];
                out4[1] = kEnc[(a & 0x03) << 4];
                out4[2] = '=';
                out4[3] = '=';
                w->append_bytes(out4, 4);
            } else if (buffered == 2U) {
                const uint8_t a = buf[0];
                const uint8_t b = buf[1];
                char out4[4];
                out4[0] = kEnc[(a >> 2) & 0x3F];
                out4[1] = kEnc[((a & 0x03) << 4) | ((b >> 4) & 0x0F)];
                out4[2] = kEnc[(b & 0x0F) << 2];
                out4[3] = '=';
                w->append_bytes(out4, 4);
            }
            buffered = 0;
        }
    };


    static void le16(uint16_t v, uint8_t out[2]) noexcept
    {
        out[0] = static_cast<uint8_t>((v >> 0) & 0xFF);
        out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    }
    static void le32(uint32_t v, uint8_t out[4]) noexcept
    {
        out[0] = static_cast<uint8_t>((v >> 0) & 0xFF);
        out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        out[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        out[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
    }
    static void le64(uint64_t v, uint8_t out[8]) noexcept
    {
        out[0] = static_cast<uint8_t>((v >> 0) & 0xFF);
        out[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
        out[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
        out[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
        out[4] = static_cast<uint8_t>((v >> 32) & 0xFF);
        out[5] = static_cast<uint8_t>((v >> 40) & 0xFF);
        out[6] = static_cast<uint8_t>((v >> 48) & 0xFF);
        out[7] = static_cast<uint8_t>((v >> 56) & 0xFF);
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


    static const char* key_kind_name(MetaKeyKind k) noexcept
    {
        switch (k) {
        case MetaKeyKind::ExifTag: return "ExifTag";
        case MetaKeyKind::ExrAttribute: return "ExrAttribute";
        case MetaKeyKind::IptcDataset: return "IptcDataset";
        case MetaKeyKind::XmpProperty: return "XmpProperty";
        case MetaKeyKind::IccHeaderField: return "IccHeaderField";
        case MetaKeyKind::IccTag: return "IccTag";
        case MetaKeyKind::PhotoshopIrb: return "PhotoshopIrb";
        case MetaKeyKind::GeotiffKey: return "GeotiffKey";
        case MetaKeyKind::PrintImField: return "PrintImField";
        case MetaKeyKind::BmffField: return "BmffField";
        case MetaKeyKind::JumbfField: return "JumbfField";
        case MetaKeyKind::JumbfCborKey: return "JumbfCborKey";
        }
        return "Unknown";
    }

    static const char* value_kind_name(MetaValueKind k) noexcept
    {
        switch (k) {
        case MetaValueKind::Empty: return "Empty";
        case MetaValueKind::Scalar: return "Scalar";
        case MetaValueKind::Array: return "Array";
        case MetaValueKind::Bytes: return "Bytes";
        case MetaValueKind::Text: return "Text";
        }
        return "Unknown";
    }

    static const char* elem_type_name(MetaElementType t) noexcept
    {
        switch (t) {
        case MetaElementType::U8: return "U8";
        case MetaElementType::I8: return "I8";
        case MetaElementType::U16: return "U16";
        case MetaElementType::I16: return "I16";
        case MetaElementType::U32: return "U32";
        case MetaElementType::I32: return "I32";
        case MetaElementType::U64: return "U64";
        case MetaElementType::I64: return "I64";
        case MetaElementType::F32: return "F32";
        case MetaElementType::F64: return "F64";
        case MetaElementType::URational: return "URational";
        case MetaElementType::SRational: return "SRational";
        }
        return "Unknown";
    }

    static const char* text_encoding_name(TextEncoding e) noexcept
    {
        switch (e) {
        case TextEncoding::Unknown: return "Unknown";
        case TextEncoding::Ascii: return "Ascii";
        case TextEncoding::Utf8: return "Utf8";
        case TextEncoding::Utf16LE: return "Utf16LE";
        case TextEncoding::Utf16BE: return "Utf16BE";
        }
        return "Unknown";
    }

    static const char* wire_family_name(WireFamily f) noexcept
    {
        switch (f) {
        case WireFamily::None: return "None";
        case WireFamily::Tiff: return "Tiff";
        case WireFamily::Other: return "Other";
        }
        return "Unknown";
    }

    static std::string_view exr_wire_type_name(uint16_t code) noexcept
    {
        switch (code) {
        case 1: return "box2i";
        case 2: return "box2f";
        case 3: return "bytes";
        case 4: return "chlist";
        case 5: return "chromaticities";
        case 6: return "compression";
        case 7: return "double";
        case 8: return "envmap";
        case 9: return "float";
        case 10: return "floatvector";
        case 11: return "int";
        case 12: return "keycode";
        case 13: return "lineOrder";
        case 14: return "m33f";
        case 15: return "m33d";
        case 16: return "m44f";
        case 17: return "m44d";
        case 18: return "preview";
        case 19: return "rational";
        case 20: return "string";
        case 21: return "stringvector";
        case 22: return "tiledesc";
        case 23: return "timecode";
        case 24: return "v2i";
        case 25: return "v2f";
        case 26: return "v2d";
        case 27: return "v3i";
        case 28: return "v3f";
        case 29: return "v3d";
        case 30: return "deepImageState";
        default: return {};
        }
    }


    static void emit_open(SpanWriter* w, const char* indent,
                          std::string_view name) noexcept
    {
        w->append(indent);
        w->append("<");
        w->append(name);
        w->append(">");
    }

    static void emit_text_element(SpanWriter* w, const char* indent,
                                  std::string_view name,
                                  std::string_view value) noexcept
    {
        emit_open(w, indent, name);
        append_xml_safe_ascii(value, w);
        w->append("</");
        w->append(name);
        w->append(">\n");
    }

    static void emit_u64_element(SpanWriter* w, const char* indent,
                                 std::string_view name, uint64_t value) noexcept
    {
        emit_open(w, indent, name);
        append_u64_dec(value, w);
        w->append("</");
        w->append(name);
        w->append(">\n");
    }


    static void emit_value_base64(const ByteArena& arena, const MetaValue& v,
                                  SpanWriter* w, uint64_t* out_bytes) noexcept
    {
        if (out_bytes) {
            *out_bytes = 0;
        }
        if (!w || w->limit_hit) {
            return;
        }

        Base64Encoder b64(w);

        if (v.kind == MetaValueKind::Empty) {
            return;
        }

        if (v.kind == MetaValueKind::Bytes || v.kind == MetaValueKind::Text) {
            const std::span<const std::byte> raw = arena.span(v.data.span);
            if (out_bytes) {
                *out_bytes = static_cast<uint64_t>(raw.size());
            }
            b64.append(raw);
            b64.finish();
            return;
        }

        if (v.kind == MetaValueKind::Scalar) {
            switch (v.elem_type) {
            case MetaElementType::U8: {
                const uint8_t x = static_cast<uint8_t>(v.data.u64 & 0xFFU);
                if (out_bytes) {
                    *out_bytes = 1;
                }
                b64.append_u8(x);
                b64.finish();
                return;
            }
            case MetaElementType::I8: {
                const uint8_t x = static_cast<uint8_t>(
                    static_cast<int8_t>(v.data.i64));
                if (out_bytes) {
                    *out_bytes = 1;
                }
                b64.append_u8(x);
                b64.finish();
                return;
            }
            case MetaElementType::U16: {
                uint8_t tmp[2];
                le16(static_cast<uint16_t>(v.data.u64 & 0xFFFFU), tmp);
                if (out_bytes) {
                    *out_bytes = 2;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 2));
                b64.finish();
                return;
            }
            case MetaElementType::I16: {
                uint8_t tmp[2];
                le16(static_cast<uint16_t>(static_cast<int16_t>(v.data.i64)),
                     tmp);
                if (out_bytes) {
                    *out_bytes = 2;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 2));
                b64.finish();
                return;
            }
            case MetaElementType::U32: {
                uint8_t tmp[4];
                le32(static_cast<uint32_t>(v.data.u64 & 0xFFFFFFFFU), tmp);
                if (out_bytes) {
                    *out_bytes = 4;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 4));
                b64.finish();
                return;
            }
            case MetaElementType::I32: {
                uint8_t tmp[4];
                le32(static_cast<uint32_t>(static_cast<int32_t>(v.data.i64)),
                     tmp);
                if (out_bytes) {
                    *out_bytes = 4;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 4));
                b64.finish();
                return;
            }
            case MetaElementType::U64: {
                uint8_t tmp[8];
                le64(v.data.u64, tmp);
                if (out_bytes) {
                    *out_bytes = 8;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 8));
                b64.finish();
                return;
            }
            case MetaElementType::I64: {
                uint8_t tmp[8];
                le64(static_cast<uint64_t>(v.data.i64), tmp);
                if (out_bytes) {
                    *out_bytes = 8;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 8));
                b64.finish();
                return;
            }
            case MetaElementType::F32: {
                uint8_t tmp[4];
                le32(v.data.f32_bits, tmp);
                if (out_bytes) {
                    *out_bytes = 4;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 4));
                b64.finish();
                return;
            }
            case MetaElementType::F64: {
                uint8_t tmp[8];
                le64(v.data.f64_bits, tmp);
                if (out_bytes) {
                    *out_bytes = 8;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 8));
                b64.finish();
                return;
            }
            case MetaElementType::URational: {
                uint8_t tmp[8];
                le32(v.data.ur.numer, tmp + 0);
                le32(v.data.ur.denom, tmp + 4);
                if (out_bytes) {
                    *out_bytes = 8;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 8));
                b64.finish();
                return;
            }
            case MetaElementType::SRational: {
                uint8_t tmp[8];
                le32(static_cast<uint32_t>(v.data.sr.numer), tmp + 0);
                le32(static_cast<uint32_t>(v.data.sr.denom), tmp + 4);
                if (out_bytes) {
                    *out_bytes = 8;
                }
                b64.append(std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(tmp), 8));
                b64.finish();
                return;
            }
            }
        }

        if (v.kind == MetaValueKind::Array) {
            const std::span<const std::byte> raw = arena.span(v.data.span);
            const uint32_t n                     = safe_array_count(arena, v);
            const uint32_t elem_size = meta_element_size(v.elem_type);
            if (elem_size == 0U || n == 0U) {
                return;
            }
            if (out_bytes) {
                *out_bytes = static_cast<uint64_t>(n) * elem_size;
            }

            if (v.elem_type == MetaElementType::U8
                || v.elem_type == MetaElementType::I8) {
                b64.append(raw.subspan(0, static_cast<size_t>(n)));
                b64.finish();
                return;
            }

            const uint64_t take_bytes = static_cast<uint64_t>(n) * elem_size;
            if (take_bytes > raw.size()) {
                return;
            }

            for (uint32_t i = 0; i < n; ++i) {
                const size_t off = static_cast<size_t>(i) * elem_size;
                const std::span<const std::byte> elem = raw.subspan(off,
                                                                    elem_size);

                if (v.elem_type == MetaElementType::U16) {
                    uint16_t x = 0;
                    std::memcpy(&x, elem.data(), 2);
                    uint8_t tmp[2];
                    le16(x, tmp);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 2));
                } else if (v.elem_type == MetaElementType::I16) {
                    int16_t x = 0;
                    std::memcpy(&x, elem.data(), 2);
                    uint8_t tmp[2];
                    le16(static_cast<uint16_t>(x), tmp);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 2));
                } else if (v.elem_type == MetaElementType::U32
                           || v.elem_type == MetaElementType::F32) {
                    uint32_t x = 0;
                    std::memcpy(&x, elem.data(), 4);
                    uint8_t tmp[4];
                    le32(x, tmp);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 4));
                } else if (v.elem_type == MetaElementType::I32) {
                    int32_t x = 0;
                    std::memcpy(&x, elem.data(), 4);
                    uint8_t tmp[4];
                    le32(static_cast<uint32_t>(x), tmp);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 4));
                } else if (v.elem_type == MetaElementType::U64
                           || v.elem_type == MetaElementType::F64) {
                    uint64_t x = 0;
                    std::memcpy(&x, elem.data(), 8);
                    uint8_t tmp[8];
                    le64(x, tmp);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 8));
                } else if (v.elem_type == MetaElementType::I64) {
                    int64_t x = 0;
                    std::memcpy(&x, elem.data(), 8);
                    uint8_t tmp[8];
                    le64(static_cast<uint64_t>(x), tmp);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 8));
                } else if (v.elem_type == MetaElementType::URational) {
                    URational r;
                    std::memcpy(&r, elem.data(), sizeof(URational));
                    uint8_t tmp[8];
                    le32(r.numer, tmp + 0);
                    le32(r.denom, tmp + 4);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 8));
                } else if (v.elem_type == MetaElementType::SRational) {
                    SRational r;
                    std::memcpy(&r, elem.data(), sizeof(SRational));
                    uint8_t tmp[8];
                    le32(static_cast<uint32_t>(r.numer), tmp + 0);
                    le32(static_cast<uint32_t>(r.denom), tmp + 4);
                    b64.append(std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(tmp), 8));
                }

                if (w->limit_hit) {
                    return;
                }
            }

            b64.finish();
            return;
        }
    }


    static void emit_entry_key_fields(const MetaStore& store, const Entry& e,
                                      SpanWriter* w,
                                      const XmpDumpOptions& options) noexcept
    {
        const ByteArena& arena = store.arena();
        emit_text_element(w, kIndent4, "omd:keyKind",
                          key_kind_name(e.key.kind));

        // Canonical key text (stable, one-line, kind-specific).
        w->append(kIndent4);
        w->append("<omd:key>");
        switch (e.key.kind) {
        case MetaKeyKind::ExifTag: {
            const std::string_view ifd = arena_string(arena,
                                                      e.key.data.exif_tag.ifd);
            w->append("exif:");
            append_xml_safe_ascii(ifd, w);
            w->append(":");
            char buf[16];
            std::snprintf(buf, sizeof(buf), "0x%04X",
                          static_cast<unsigned>(e.key.data.exif_tag.tag));
            w->append(buf);
            break;
        }
        case MetaKeyKind::ExrAttribute: {
            const std::string_view name
                = arena_string(arena, e.key.data.exr_attribute.name);
            w->append("exr:part:");
            append_u64_dec(e.key.data.exr_attribute.part_index, w);
            w->append(":");
            append_xml_safe_ascii(name, w);
            break;
        }
        case MetaKeyKind::IptcDataset: {
            w->append("iptc:");
            append_u64_dec(e.key.data.iptc_dataset.record, w);
            w->append(":");
            append_u64_dec(e.key.data.iptc_dataset.dataset, w);
            break;
        }
        case MetaKeyKind::XmpProperty: {
            const std::string_view ns
                = arena_string(arena, e.key.data.xmp_property.schema_ns);
            const std::string_view path
                = arena_string(arena, e.key.data.xmp_property.property_path);
            w->append("xmp:");
            append_xml_safe_ascii(ns, w);
            w->append(":");
            append_xml_safe_ascii(path, w);
            break;
        }
        case MetaKeyKind::IccHeaderField:
            w->append("icc:header:");
            append_u64_dec(e.key.data.icc_header_field.offset, w);
            break;
        case MetaKeyKind::IccTag:
            w->append("icc:tag:");
            append_u32_hex(e.key.data.icc_tag.signature, w);
            break;
        case MetaKeyKind::PhotoshopIrb:
            w->append("psirb:");
            append_u16_hex(e.key.data.photoshop_irb.resource_id, w);
            break;
        case MetaKeyKind::GeotiffKey:
            w->append("geotiff:");
            append_u64_dec(e.key.data.geotiff_key.key_id, w);
            break;
        case MetaKeyKind::PrintImField: {
            const std::string_view field
                = arena_string(arena, e.key.data.printim_field.field);
            w->append("printim:");
            append_xml_safe_ascii(field, w);
            break;
        }
        case MetaKeyKind::BmffField: {
            const std::string_view field
                = arena_string(arena, e.key.data.bmff_field.field);
            w->append("bmff:");
            append_xml_safe_ascii(field, w);
            break;
        }
        case MetaKeyKind::JumbfField: {
            const std::string_view field
                = arena_string(arena, e.key.data.jumbf_field.field);
            w->append("jumbf:");
            append_xml_safe_ascii(field, w);
            break;
        }
        case MetaKeyKind::JumbfCborKey: {
            const std::string_view key
                = arena_string(arena, e.key.data.jumbf_cbor_key.key);
            w->append("jumbf_cbor:");
            append_xml_safe_ascii(key, w);
            break;
        }
        }
        w->append("</omd:key>\n");

        if (e.key.kind == MetaKeyKind::ExifTag) {
            const std::string_view ifd = arena_string(arena,
                                                      e.key.data.exif_tag.ifd);
            emit_text_element(w, kIndent4, "omd:ifd", ifd);
            w->append(kIndent4);
            w->append("<omd:tag>");
            append_u16_hex(e.key.data.exif_tag.tag, w);
            w->append("</omd:tag>\n");
            if (options.include_names) {
                const std::string_view n
                    = exif_tag_name(ifd, e.key.data.exif_tag.tag);
                if (!n.empty()) {
                    emit_text_element(w, kIndent4, "omd:tagName", n);
                }
            }
        } else if (e.key.kind == MetaKeyKind::GeotiffKey
                   && options.include_names) {
            const std::string_view n = geotiff_key_name(
                e.key.data.geotiff_key.key_id);
            if (!n.empty()) {
                emit_text_element(w, kIndent4, "omd:tagName", n);
            }
        } else if (e.key.kind == MetaKeyKind::ExrAttribute) {
            emit_u64_element(w, kIndent4, "omd:part",
                             static_cast<uint64_t>(
                                 e.key.data.exr_attribute.part_index));
            const std::string_view name
                = arena_string(arena, e.key.data.exr_attribute.name);
            emit_text_element(w, kIndent4, "omd:attrName", name);
        }
    }

}  // namespace


XmpDumpResult
dump_xmp_lossless(const MetaStore& store, std::span<std::byte> out,
                  const XmpDumpOptions& options) noexcept
{
    XmpDumpResult r;

    SpanWriter w(out, options.limits.max_output_bytes);

    static constexpr std::array<XmpNsDecl, 1> kDecls = {
        XmpNsDecl { "omd", kXmpNsOpenMetaDump },
    };
    emit_xmp_packet_begin(&w, std::span<const XmpNsDecl>(kDecls.data(),
                                                         kDecls.size()));

    emit_u64_element(&w, kIndent3, "omd:formatVersion", 1);
    emit_u64_element(&w, kIndent3, "omd:blockCount",
                     static_cast<uint64_t>(store.block_count()));

    w.append(kIndent3);
    w.append("<omd:entries>\n");
    w.append(kIndent4);
    w.append("<rdf:Seq>\n");

    const ByteArena& arena = store.arena();

    uint32_t emitted = 0;
    for (BlockId block = 0; block < store.block_count(); ++block) {
        const std::span<const EntryId> ids = store.entries_in_block(block);
        for (size_t i = 0; i < ids.size(); ++i) {
            const Entry& e = store.entry(ids[i]);
            if (any(e.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (options.limits.max_entries != 0U
                && emitted >= options.limits.max_entries) {
                w.limit_hit = true;
                break;
            }

            w.append(kIndent4);
            w.append("<rdf:li rdf:parseType=\"Resource\">\n");

            emit_entry_key_fields(store, e, &w, options);

            emit_text_element(&w, kIndent4, "omd:valueKind",
                              value_kind_name(e.value.kind));
            emit_text_element(&w, kIndent4, "omd:elemType",
                              elem_type_name(e.value.elem_type));
            emit_text_element(&w, kIndent4, "omd:textEncoding",
                              text_encoding_name(e.value.text_encoding));
            emit_u64_element(&w, kIndent4, "omd:count",
                             static_cast<uint64_t>(e.value.count));

            // Lossless payload (base64).
            {
                uint64_t value_bytes = 0;
                w.append(kIndent4);
                w.append("<omd:valueBase64>");
                emit_value_base64(arena, e.value, &w, &value_bytes);
                w.append("</omd:valueBase64>\n");
                emit_u64_element(&w, kIndent4, "omd:valueBytes", value_bytes);
                if (e.value.kind == MetaValueKind::Bytes
                    || e.value.kind == MetaValueKind::Text) {
                    emit_text_element(&w, kIndent4, "omd:valueEncoding", "raw");
                } else if (e.value.kind == MetaValueKind::Array
                           || e.value.kind == MetaValueKind::Scalar) {
                    emit_text_element(&w, kIndent4, "omd:valueEncoding", "le");
                }
            }

            if (options.include_origin) {
                emit_u64_element(&w, kIndent4, "omd:originBlock",
                                 static_cast<uint64_t>(e.origin.block));
                emit_u64_element(&w, kIndent4, "omd:originOrder",
                                 static_cast<uint64_t>(e.origin.order_in_block));
            }
            if (options.include_wire) {
                emit_text_element(&w, kIndent4, "omd:wireFamily",
                                  wire_family_name(e.origin.wire_type.family));
                emit_u64_element(&w, kIndent4, "omd:wireTypeCode",
                                 static_cast<uint64_t>(e.origin.wire_type.code));
                emit_u64_element(&w, kIndent4, "omd:wireCount",
                                 static_cast<uint64_t>(e.origin.wire_count));
                if (e.key.kind == MetaKeyKind::ExrAttribute) {
                    std::string_view type_name;
                    if (e.origin.wire_type_name.size > 0U) {
                        type_name = arena_string(arena,
                                                 e.origin.wire_type_name);
                    } else {
                        type_name = exr_wire_type_name(e.origin.wire_type.code);
                    }
                    if (!type_name.empty()) {
                        emit_text_element(&w, kIndent4, "omd:exrTypeName",
                                          type_name);
                    }
                }
            }
            if (options.include_flags) {
                emit_u64_element(&w, kIndent4, "omd:flags",
                                 static_cast<uint64_t>(
                                     static_cast<uint32_t>(e.flags)));
            }

            w.append(kIndent4);
            w.append("</rdf:li>\n");

            emitted += 1;
            if (w.limit_hit) {
                break;
            }
        }
        if (w.limit_hit) {
            break;
        }
    }

    w.append(kIndent4);
    w.append("</rdf:Seq>\n");
    w.append(kIndent3);
    w.append("</omd:entries>\n");
    emit_u64_element(&w, kIndent3, "omd:entriesWritten",
                     static_cast<uint64_t>(emitted));
    emit_xmp_packet_end(&w);

    r.entries = emitted;

    if (w.limit_hit) {
        r.status = XmpDumpStatus::LimitExceeded;
    } else if (w.needed > static_cast<uint64_t>(out.size())) {
        r.status = XmpDumpStatus::OutputTruncated;
    } else {
        r.status = XmpDumpStatus::Ok;
    }

    r.written = (w.written < w.needed) ? w.written : w.needed;
    r.needed  = w.needed;
    return r;
}


namespace {

    static bool is_simple_xmp_property_name(std::string_view s) noexcept
    {
        if (s.empty()) {
            return false;
        }
        if (s.find('/') != std::string_view::npos) {
            return false;
        }
        if (s.find('[') != std::string_view::npos
            || s.find(']') != std::string_view::npos) {
            return false;
        }
        for (size_t i = 0; i < s.size(); ++i) {
            const char c  = s[i];
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
                            || (c >= '0' && c <= '9') || c == '_' || c == '-';
            if (!ok) {
                return false;
            }
        }
        return true;
    }


    static bool is_makernote_ifd(std::string_view ifd) noexcept
    {
        return ifd.starts_with("mk_");
    }


    static bool ifd_to_portable_prefix(std::string_view ifd,
                                       std::string_view* out_prefix) noexcept
    {
        if (!out_prefix) {
            return false;
        }
        *out_prefix = {};

        if (ifd.empty() || is_makernote_ifd(ifd)) {
            return false;
        }
        if (ifd == "exififd" || ifd.ends_with("_exififd")) {
            *out_prefix = "exif";
            return true;
        }
        if (ifd == "gpsifd" || ifd.ends_with("_gpsifd")) {
            *out_prefix = "exif";
            return true;
        }
        if (ifd == "interopifd" || ifd.ends_with("_interopifd")) {
            *out_prefix = "exif";
            return true;
        }
        if (ifd.starts_with("ifd") || ifd.starts_with("subifd")
            || ifd.starts_with("mkifd") || ifd.starts_with("mk_subifd")) {
            *out_prefix = "tiff";
            return true;
        }
        return false;
    }

    static std::string_view
    canonical_portable_property_name(std::string_view prefix,
                                     std::string_view name) noexcept
    {
        if (name.empty()) {
            return {};
        }

        if (prefix == "tiff") {
            if (name == "ImageLength") {
                return "ImageHeight";
            }
            return name;
        }

        if (prefix != "exif") {
            return name;
        }

        if (name == "ExposureBiasValue") {
            return "ExposureCompensation";
        }
        if (name == "ISOSpeedRatings") {
            return "ISO";
        }
        if (name == "PixelXDimension") {
            return "ExifImageWidth";
        }
        if (name == "PixelYDimension") {
            return "ExifImageHeight";
        }
        if (name == "FocalLengthIn35mmFilm") {
            return "FocalLengthIn35mmFormat";
        }
        return name;
    }

    static std::string_view
    portable_property_name_for_exif_tag(std::string_view prefix,
                                        std::string_view ifd, uint16_t tag,
                                        std::string_view fallback_name) noexcept
    {
        (void)ifd;
        (void)tag;
        return canonical_portable_property_name(prefix, fallback_name);
    }


    static bool xmp_ns_to_portable_prefix(std::string_view ns,
                                          std::string_view* out_prefix) noexcept
    {
        if (!out_prefix) {
            return false;
        }
        *out_prefix = {};
        if (ns == kXmpNsXmp) {
            *out_prefix = "xmp";
            return true;
        }
        if (ns == kXmpNsTiff) {
            *out_prefix = "tiff";
            return true;
        }
        if (ns == kXmpNsExif) {
            *out_prefix = "exif";
            return true;
        }
        if (ns == kXmpNsDc) {
            *out_prefix = "dc";
            return true;
        }
        return false;
    }

    static std::string_view
    portable_property_name_for_existing_xmp(std::string_view prefix,
                                            std::string_view name) noexcept
    {
        return canonical_portable_property_name(prefix, name);
    }


    static bool parse_indexed_xmp_property_name(std::string_view path,
                                                std::string_view* out_base,
                                                uint32_t* out_index) noexcept
    {
        if (!out_base || !out_index) {
            return false;
        }
        *out_base  = {};
        *out_index = 0U;

        const size_t lb = path.rfind('[');
        if (lb == std::string_view::npos || lb + 2U >= path.size()
            || path.back() != ']') {
            return false;
        }

        const std::string_view base = path.substr(0, lb);
        if (!is_simple_xmp_property_name(base)) {
            return false;
        }

        const std::string_view idx = path.substr(lb + 1U,
                                                 path.size() - lb - 2U);
        uint64_t parsed_idx        = 0U;
        for (size_t i = 0; i < idx.size(); ++i) {
            const char c = idx[i];
            if (c < '0' || c > '9') {
                return false;
            }
            parsed_idx = parsed_idx * 10U + static_cast<uint64_t>(c - '0');
            if (parsed_idx > UINT32_MAX) {
                return false;
            }
        }
        if (parsed_idx == 0U) {
            return false;
        }

        *out_base  = base;
        *out_index = static_cast<uint32_t>(parsed_idx);
        return true;
    }


    static bool bytes_are_ascii_text(std::span<const std::byte> raw) noexcept
    {
        for (size_t i = 0; i < raw.size(); ++i) {
            const uint8_t c = static_cast<uint8_t>(raw[i]);
            if (c == 0U) {
                return false;
            }
            if (c < 0x20U || c > 0x7EU) {
                return false;
            }
        }
        return true;
    }


    static void append_i64_dec(int64_t v, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        w->append(buf);
    }


    static void append_f64_dec(double v, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }
        if (!std::isfinite(v)) {
            w->append("0");
            return;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", v);
        w->append(buf);
    }


    static void append_rational_text(const URational& r, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }

        if (r.denom == 0U) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%u/%u",
                          static_cast<unsigned>(r.numer),
                          static_cast<unsigned>(r.denom));
            w->append(buf);
            return;
        }

        uint32_t n = r.numer;
        uint32_t d = r.denom;
        while (d != 0U) {
            const uint32_t t = n % d;
            n                = d;
            d                = t;
        }
        const uint32_t gcd = (n == 0U) ? 1U : n;
        const uint32_t rn  = r.numer / gcd;
        const uint32_t rd  = r.denom / gcd;

        if (rd == 1U) {
            append_u64_dec(static_cast<uint64_t>(rn), w);
            return;
        }

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%u/%u", static_cast<unsigned>(rn),
                      static_cast<unsigned>(rd));
        w->append(buf);
    }


    static void append_rational_text(const SRational& r, SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }

        const int64_t n0 = static_cast<int64_t>(r.numer);
        const int64_t d0 = static_cast<int64_t>(r.denom);
        if (d0 == 0) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%lld/%lld",
                          static_cast<long long>(n0),
                          static_cast<long long>(d0));
            w->append(buf);
            return;
        }

        int64_t n = n0;
        int64_t d = d0;
        if (d < 0) {
            n = -n;
            d = -d;
        }

        uint64_t an = (n < 0) ? static_cast<uint64_t>(-n)
                              : static_cast<uint64_t>(n);
        uint64_t ad = static_cast<uint64_t>(d);
        while (ad != 0U) {
            const uint64_t t = an % ad;
            an               = ad;
            ad               = t;
        }
        const uint64_t gcd = (an == 0U) ? 1U : an;
        const int64_t rn   = n / static_cast<int64_t>(gcd);
        const int64_t rd   = d / static_cast<int64_t>(gcd);

        if (rd == 1) {
            append_i64_dec(rn, w);
            return;
        }

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%lld/%lld", static_cast<long long>(rn),
                      static_cast<long long>(rd));
        w->append(buf);
    }


    static void emit_portable_scalar_text(const MetaValue& v,
                                          SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }

        switch (v.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64: append_u64_dec(v.data.u64, w); return;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64: append_i64_dec(v.data.i64, w); return;
        case MetaElementType::F32: {
            float f = 0.0f;
            std::memcpy(&f, &v.data.f32_bits, sizeof(f));
            append_f64_dec(static_cast<double>(f), w);
            return;
        }
        case MetaElementType::F64: {
            double d = 0.0;
            std::memcpy(&d, &v.data.f64_bits, sizeof(d));
            append_f64_dec(d, w);
            return;
        }
        case MetaElementType::URational:
            append_rational_text(v.data.ur, w);
            return;
        case MetaElementType::SRational:
            append_rational_text(v.data.sr, w);
            return;
        }
    }


    static bool emit_portable_value_inline(const ByteArena& arena,
                                           const MetaValue& v,
                                           SpanWriter* w) noexcept
    {
        if (!w) {
            return false;
        }

        switch (v.kind) {
        case MetaValueKind::Empty: return false;
        case MetaValueKind::Text: {
            const std::string_view s = arena_string(arena, v.data.span);
            if (v.text_encoding == TextEncoding::Utf8) {
                append_xml_safe_utf8(s, w);
            } else {
                append_xml_safe_ascii(s, w);
            }
            return true;
        }
        case MetaValueKind::Bytes: {
            const std::span<const std::byte> raw = arena.span(v.data.span);
            if (!bytes_are_ascii_text(raw)) {
                return false;
            }
            const std::string_view s(reinterpret_cast<const char*>(raw.data()),
                                     raw.size());
            append_xml_safe_ascii(s, w);
            return true;
        }
        case MetaValueKind::Scalar:
            emit_portable_scalar_text(v, w);
            return true;
        case MetaValueKind::Array: return false;
        }
        return false;
    }


    static bool portable_scalar_like_value_supported(const ByteArena& arena,
                                                     const MetaValue& v) noexcept
    {
        if (v.kind == MetaValueKind::Text || v.kind == MetaValueKind::Scalar) {
            return true;
        }
        if (v.kind == MetaValueKind::Bytes) {
            return bytes_are_ascii_text(arena.span(v.data.span));
        }
        return false;
    }


    static void emit_portable_array_as_seq(const ByteArena& arena,
                                           const MetaValue& v,
                                           SpanWriter* w) noexcept
    {
        if (!w) {
            return;
        }

        const std::span<const std::byte> raw = arena.span(v.data.span);
        const uint32_t elem_size             = meta_element_size(v.elem_type);
        if (elem_size == 0U) {
            return;
        }
        const uint32_t count = safe_array_count(arena, v);
        if (count == 0U) {
            return;
        }

        w->append(kIndent4);
        w->append("<rdf:Seq>\n");
        for (uint32_t i = 0; i < count; ++i) {
            const size_t off = static_cast<size_t>(i) * elem_size;
            if (off + elem_size > raw.size()) {
                break;
            }
            w->append(kIndent4);
            w->append(kIndent1);
            w->append("<rdf:li>");

            switch (v.elem_type) {
            case MetaElementType::U8: {
                const uint8_t x = static_cast<uint8_t>(raw[off]);
                append_u64_dec(x, w);
                break;
            }
            case MetaElementType::I8: {
                const int8_t x = static_cast<int8_t>(
                    static_cast<uint8_t>(raw[off]));
                append_i64_dec(x, w);
                break;
            }
            case MetaElementType::U16: {
                uint16_t x = 0;
                std::memcpy(&x, raw.data() + off, sizeof(x));
                append_u64_dec(x, w);
                break;
            }
            case MetaElementType::I16: {
                int16_t x = 0;
                std::memcpy(&x, raw.data() + off, sizeof(x));
                append_i64_dec(x, w);
                break;
            }
            case MetaElementType::U32: {
                uint32_t x = 0;
                std::memcpy(&x, raw.data() + off, sizeof(x));
                append_u64_dec(x, w);
                break;
            }
            case MetaElementType::I32: {
                int32_t x = 0;
                std::memcpy(&x, raw.data() + off, sizeof(x));
                append_i64_dec(x, w);
                break;
            }
            case MetaElementType::U64: {
                uint64_t x = 0;
                std::memcpy(&x, raw.data() + off, sizeof(x));
                append_u64_dec(x, w);
                break;
            }
            case MetaElementType::I64: {
                int64_t x = 0;
                std::memcpy(&x, raw.data() + off, sizeof(x));
                append_i64_dec(x, w);
                break;
            }
            case MetaElementType::F32: {
                uint32_t bits = 0;
                std::memcpy(&bits, raw.data() + off, sizeof(bits));
                float f = 0.0f;
                std::memcpy(&f, &bits, sizeof(f));
                append_f64_dec(static_cast<double>(f), w);
                break;
            }
            case MetaElementType::F64: {
                uint64_t bits = 0;
                std::memcpy(&bits, raw.data() + off, sizeof(bits));
                double d = 0.0;
                std::memcpy(&d, &bits, sizeof(d));
                append_f64_dec(d, w);
                break;
            }
            case MetaElementType::URational: {
                URational r;
                std::memcpy(&r, raw.data() + off, sizeof(r));
                append_rational_text(r, w);
                break;
            }
            case MetaElementType::SRational: {
                SRational r;
                std::memcpy(&r, raw.data() + off, sizeof(r));
                append_rational_text(r, w);
                break;
            }
            }

            w->append("</rdf:li>\n");
        }
        w->append(kIndent4);
        w->append("</rdf:Seq>\n");
    }


    static bool emit_portable_property(SpanWriter* w, std::string_view prefix,
                                       std::string_view name,
                                       const ByteArena& arena,
                                       const MetaValue& v) noexcept
    {
        if (!w) {
            return false;
        }
        if (prefix.empty() || name.empty()) {
            return false;
        }
        if (!is_simple_xmp_property_name(name)) {
            return false;
        }

        if (v.kind == MetaValueKind::Array) {
            w->append(kIndent3);
            w->append("<");
            w->append(prefix);
            w->append(":");
            w->append(name);
            w->append(">\n");

            emit_portable_array_as_seq(arena, v, w);

            w->append(kIndent3);
            w->append("</");
            w->append(prefix);
            w->append(":");
            w->append(name);
            w->append(">\n");
            return true;
        }

        // Skip bytes that can't be represented safely as portable XMP text.
        if (v.kind == MetaValueKind::Bytes) {
            const std::span<const std::byte> raw = arena.span(v.data.span);
            if (!bytes_are_ascii_text(raw)) {
                return false;
            }
        }
        if (v.kind != MetaValueKind::Text && v.kind != MetaValueKind::Bytes
            && v.kind != MetaValueKind::Scalar) {
            return false;
        }

        w->append(kIndent3);
        w->append("<");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">");
        (void)emit_portable_value_inline(arena, v, w);
        w->append("</");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_property_text(SpanWriter* w,
                                            std::string_view prefix,
                                            std::string_view name,
                                            std::string_view value) noexcept
    {
        if (!w || prefix.empty() || name.empty()) {
            return false;
        }
        w->append(kIndent3);
        w->append("<");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">");
        append_xml_safe_ascii(value, w);
        w->append("</");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static bool scalar_u64_value(const MetaValue& v, uint64_t* out) noexcept
    {
        if (!out || v.kind != MetaValueKind::Scalar) {
            return false;
        }
        switch (v.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64: *out = v.data.u64; return true;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            if (v.data.i64 < 0) {
                return false;
            }
            *out = static_cast<uint64_t>(v.data.i64);
            return true;
        default: return false;
        }
    }

    static std::string_view portable_enum_text_override(std::string_view prefix,
                                                        uint16_t tag,
                                                        uint64_t value) noexcept
    {
        if (prefix == "tiff") {
            switch (tag) {
            case 0x0103U:  // Compression
                switch (value) {
                case 1U: return "Uncompressed";
                case 6U: return "JPEG (old-style)";
                case 7U: return "JPEG";
                case 8U: return "Adobe Deflate";
                case 32773U: return "PackBits";
                default: return {};
                }
            case 0x011CU:  // PlanarConfiguration
                switch (value) {
                case 1U: return "Chunky";
                case 2U: return "Planar";
                default: return {};
                }
            case 0x0106U:  // PhotometricInterpretation
                switch (value) {
                case 0U: return "WhiteIsZero";
                case 1U: return "BlackIsZero";
                case 2U: return "RGB";
                case 3U: return "RGB Palette";
                case 4U: return "Transparency Mask";
                case 5U: return "CMYK";
                case 6U: return "YCbCr";
                case 8U: return "CIELab";
                case 9U: return "ICCLab";
                case 10U: return "ITULab";
                default: return {};
                }
            case 0x0213U:  // YCbCrPositioning
                switch (value) {
                case 1U: return "Centered";
                case 2U: return "Co-sited";
                default: return {};
                }
            default: return {};
            }
        }

        if (prefix == "exif") {
            switch (tag) {
            case 0xA406U:  // SceneCaptureType
                switch (value) {
                case 0U: return "Standard";
                case 1U: return "Landscape";
                case 2U: return "Portrait";
                case 3U: return "Night scene";
                default: return {};
                }
            case 0x9208U:  // LightSource
                switch (value) {
                case 0U: return "Unknown";
                case 1U: return "Daylight";
                case 2U: return "Fluorescent";
                case 3U: return "Tungsten (incandescent)";
                case 4U: return "Flash";
                case 9U: return "Fine weather";
                case 10U: return "Cloudy";
                case 11U: return "Shade";
                case 12U: return "Daylight fluorescent";
                case 13U: return "Day white fluorescent";
                case 14U: return "Cool white fluorescent";
                case 15U: return "White fluorescent";
                case 17U: return "Standard light A";
                case 18U: return "Standard light B";
                case 19U: return "Standard light C";
                case 20U: return "D55";
                case 21U: return "D65";
                case 22U: return "D75";
                case 23U: return "D50";
                case 24U: return "ISO studio tungsten";
                case 255U: return "Other";
                default: return {};
                }
            case 0xA40AU:  // Sharpness
                switch (value) {
                case 0U: return "Normal";
                case 1U: return "Soft";
                case 2U: return "Hard";
                default: return {};
                }
            case 0xA408U:  // Contrast
                switch (value) {
                case 0U: return "Normal";
                case 1U: return "Low";
                case 2U: return "High";
                default: return {};
                }
            case 0xA409U:  // Saturation
                switch (value) {
                case 0U: return "Normal";
                case 1U: return "Low";
                case 2U: return "High";
                default: return {};
                }
            case 0xA407U:  // GainControl
                switch (value) {
                case 0U: return "None";
                case 1U: return "Low gain up";
                case 2U: return "High gain up";
                case 3U: return "Low gain down";
                case 4U: return "High gain down";
                default: return {};
                }
            case 0xA40CU:  // SubjectDistanceRange
                switch (value) {
                case 0U: return "Unknown";
                case 1U: return "Macro";
                case 2U: return "Close";
                case 3U: return "Distant";
                default: return {};
                }
            default: return {};
            }
        }

        return {};
    }

    static bool scalar_urational_value(const MetaValue& v,
                                       URational* out) noexcept
    {
        if (!out || v.kind != MetaValueKind::Scalar
            || v.elem_type != MetaElementType::URational) {
            return false;
        }
        *out = v.data.ur;
        return true;
    }

    static bool has_invalid_urational_value(const ByteArena& arena,
                                            const MetaValue& v) noexcept
    {
        if (v.kind == MetaValueKind::Scalar
            && v.elem_type == MetaElementType::URational) {
            return v.data.ur.denom == 0U;
        }

        if (v.kind != MetaValueKind::Array
            || v.elem_type != MetaElementType::URational) {
            return false;
        }

        const std::span<const std::byte> raw = arena.span(v.data.span);
        const uint32_t count                 = safe_array_count(arena, v);
        for (uint32_t i = 0U; i < count; ++i) {
            const size_t off = static_cast<size_t>(i) * sizeof(URational);
            if (off + sizeof(URational) > raw.size()) {
                break;
            }
            URational r {};
            std::memcpy(&r, raw.data() + off, sizeof(r));
            if (r.denom == 0U) {
                return true;
            }
        }
        return false;
    }

    static bool scalar_srational_value(const MetaValue& v,
                                       SRational* out) noexcept
    {
        if (!out || v.kind != MetaValueKind::Scalar
            || v.elem_type != MetaElementType::SRational) {
            return false;
        }
        *out = v.data.sr;
        return true;
    }

    static bool urational_to_double(const URational& r, double* out) noexcept
    {
        if (!out || r.denom == 0U) {
            return false;
        }
        const double d = static_cast<double>(r.numer)
                         / static_cast<double>(r.denom);
        if (!std::isfinite(d)) {
            return false;
        }
        *out = d;
        return true;
    }

    static bool srational_to_double(const SRational& r, double* out) noexcept
    {
        if (!out || r.denom == 0) {
            return false;
        }
        const double d = static_cast<double>(r.numer)
                         / static_cast<double>(r.denom);
        if (!std::isfinite(d)) {
            return false;
        }
        *out = d;
        return true;
    }

    static bool emit_exif_lens_specification_decimal_seq(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena, const MetaValue& v) noexcept
    {
        if (!w || prefix.empty() || name.empty()
            || v.kind != MetaValueKind::Array
            || v.elem_type != MetaElementType::URational) {
            return false;
        }
        const std::span<const std::byte> raw = arena.span(v.data.span);
        const uint32_t count                 = safe_array_count(arena, v);
        if (count == 0U) {
            return false;
        }

        w->append(kIndent3);
        w->append("<");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        w->append(kIndent4);
        w->append("<rdf:Seq>\n");

        for (uint32_t i = 0; i < count; ++i) {
            const size_t off = static_cast<size_t>(i) * sizeof(URational);
            if (off + sizeof(URational) > raw.size()) {
                break;
            }
            URational r {};
            std::memcpy(&r, raw.data() + off, sizeof(r));
            w->append(kIndent4);
            w->append(kIndent1);
            w->append("<rdf:li>");
            double d = 0.0;
            if (urational_to_double(r, &d)) {
                append_f64_dec(d, w);
            } else {
                append_rational_text(r, w);
            }
            w->append("</rdf:li>\n");
        }

        w->append(kIndent4);
        w->append("</rdf:Seq>\n");
        w->append(kIndent3);
        w->append("</");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static bool emit_portable_exif_tag_property_override(
        SpanWriter* w, std::string_view prefix, std::string_view ifd,
        uint16_t tag, std::string_view name, const ByteArena& arena,
        const MetaValue& v) noexcept
    {
        (void)ifd;
        if (!w || prefix.empty() || name.empty()) {
            return false;
        }

        uint64_t u = 0U;
        if (scalar_u64_value(v, &u)) {
            const std::string_view enum_text
                = portable_enum_text_override(prefix, tag, u);
            if (!enum_text.empty()) {
                return emit_portable_property_text(w, prefix, name, enum_text);
            }
        }

        if (prefix != "exif") {
            return false;
        }

        if (tag == 0xA432U) {  // LensSpecification
            return emit_exif_lens_specification_decimal_seq(w, prefix, name,
                                                            arena, v);
        }

        if (tag == 0x0000U && v.kind == MetaValueKind::Array
            && v.elem_type == MetaElementType::U8 && v.count > 0U) {
            const std::span<const std::byte> raw = arena.span(v.data.span);
            if (!raw.empty()) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%u",
                              static_cast<unsigned>(
                                  static_cast<uint8_t>(raw[0])));
                return emit_portable_property_text(w, prefix, name, buf);
            }
        }

        URational ur {};
        SRational sr {};
        char buf[96];

        if (tag == 0x920AU && scalar_urational_value(v, &ur)) {  // FocalLength
            double d = 0.0;
            if (urational_to_double(ur, &d)) {
                std::snprintf(buf, sizeof(buf), "%.1f mm", d);
                return emit_portable_property_text(w, prefix, name, buf);
            }
        }
        if (tag == 0x829DU && scalar_urational_value(v, &ur)) {  // FNumber
            double d = 0.0;
            if (urational_to_double(ur, &d)) {
                std::snprintf(buf, sizeof(buf), "%.1f", d);
                return emit_portable_property_text(w, prefix, name, buf);
            }
        }
        if ((tag == 0x9202U || tag == 0x9205U)
            && scalar_urational_value(v,
                                      &ur)) {  // ApertureValue/MaxApertureValue
            double apex = 0.0;
            if (urational_to_double(ur, &apex)) {
                const double fnum = std::pow(2.0, apex * 0.5);
                if (std::isfinite(fnum)) {
                    std::snprintf(buf, sizeof(buf), "%.1f", fnum);
                    return emit_portable_property_text(w, prefix, name, buf);
                }
            }
        }
        if (tag == 0x9201U
            && scalar_srational_value(v, &sr)) {  // ShutterSpeedValue
            double apex = 0.0;
            if (srational_to_double(sr, &apex)) {
                const double sec = std::pow(2.0, -apex);
                if (std::isfinite(sec) && sec > 0.0) {
                    if (sec < 1.0) {
                        const double den    = 1.0 / sec;
                        const uint64_t rden = static_cast<uint64_t>(
                            std::llround(den));
                        if (rden > 0U) {
                            std::snprintf(buf, sizeof(buf), "1/%llu",
                                          static_cast<unsigned long long>(rden));
                            return emit_portable_property_text(w, prefix, name,
                                                               buf);
                        }
                    } else {
                        std::snprintf(buf, sizeof(buf), "%.1f", sec);
                        return emit_portable_property_text(w, prefix, name,
                                                           buf);
                    }
                }
            }
        }
        if ((tag == 0xA20EU || tag == 0xA20FU)
            && scalar_urational_value(v, &ur)) {  // FocalPlaneX/YResolution
            double d = 0.0;
            if (urational_to_double(ur, &d)) {
                std::snprintf(buf, sizeof(buf), "%.15g", d);
                return emit_portable_property_text(w, prefix, name, buf);
            }
        }

        return false;
    }


    struct PortableIndexedProperty final {
        std::string_view prefix;
        std::string_view base;
        uint32_t index         = 0U;
        uint32_t order         = 0U;
        const MetaValue* value = nullptr;
    };

    struct PortablePropertyKey final {
        std::string_view prefix;
        std::string_view name;
    };

    struct PortablePropertyKeyHash final {
        size_t operator()(const PortablePropertyKey& key) const noexcept
        {
            const size_t h1 = std::hash<std::string_view> {}(key.prefix);
            const size_t h2 = std::hash<std::string_view> {}(key.name);
            return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6U) + (h1 >> 2U));
        }
    };

    struct PortablePropertyKeyEq final {
        bool operator()(const PortablePropertyKey& a,
                        const PortablePropertyKey& b) const noexcept
        {
            return a.prefix == b.prefix && a.name == b.name;
        }
    };

    using PortablePropertyKeySet
        = std::unordered_set<PortablePropertyKey, PortablePropertyKeyHash,
                             PortablePropertyKeyEq>;

    static bool add_portable_property_key(PortablePropertyKeySet* keys,
                                          std::string_view prefix,
                                          std::string_view name) noexcept
    {
        if (!keys || prefix.empty() || name.empty()) {
            return false;
        }
        return keys->insert(PortablePropertyKey { prefix, name }).second;
    }

    static bool process_portable_existing_xmp_entry(
        const ByteArena& arena, const Entry& e, uint32_t order, SpanWriter* w,
        PortablePropertyKeySet* emitted_keys,
        std::vector<PortableIndexedProperty>* indexed) noexcept
    {
        if (!w || !emitted_keys || !indexed
            || e.key.kind != MetaKeyKind::XmpProperty) {
            return false;
        }

        const std::string_view ns
            = arena_string(arena, e.key.data.xmp_property.schema_ns);
        const std::string_view name
            = arena_string(arena, e.key.data.xmp_property.property_path);
        std::string_view prefix;
        if (!xmp_ns_to_portable_prefix(ns, &prefix)) {
            return false;
        }

        if (is_simple_xmp_property_name(name)) {
            const std::string_view portable_name
                = portable_property_name_for_existing_xmp(prefix, name);
            if (portable_name.empty() || portable_name == "XMLPacket") {
                return false;
            }
            if (!add_portable_property_key(emitted_keys, prefix,
                                           portable_name)) {
                return false;
            }
            return emit_portable_property(w, prefix, portable_name, arena,
                                          e.value);
        }

        std::string_view base_name;
        uint32_t index = 0U;
        if (!parse_indexed_xmp_property_name(name, &base_name, &index)) {
            return false;
        }

        const std::string_view portable_base
            = portable_property_name_for_existing_xmp(prefix, base_name);
        if (portable_base.empty() || portable_base == "XMLPacket") {
            return false;
        }

        PortableIndexedProperty item;
        item.prefix = prefix;
        item.base   = portable_base;
        item.index  = index;
        item.order  = order;
        item.value  = &e.value;
        indexed->push_back(item);
        return false;
    }

    static bool
    process_portable_exif_entry(const ByteArena& arena, const Entry& e,
                                SpanWriter* w,
                                PortablePropertyKeySet* emitted_keys) noexcept
    {
        if (!w || !emitted_keys || e.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }

        const std::string_view ifd = arena_string(arena,
                                                  e.key.data.exif_tag.ifd);
        std::string_view prefix;
        if (!ifd_to_portable_prefix(ifd, &prefix)) {
            return false;
        }

        if ((ifd == "gpsifd" || ifd.ends_with("_gpsifd"))
            && has_invalid_urational_value(arena, e.value)) {
            return false;
        }

        const std::string_view tag_name
            = exif_tag_name(ifd, e.key.data.exif_tag.tag);
        if (tag_name.empty()) {
            return false;
        }

        const std::string_view portable_tag_name
            = portable_property_name_for_exif_tag(prefix, ifd,
                                                  e.key.data.exif_tag.tag,
                                                  tag_name);
        if (portable_tag_name.empty()) {
            return false;
        }

        if (tag_name.ends_with("IFDPointer") || tag_name == "SubIFDs"
            || tag_name == "XMLPacket") {
            return false;
        }

        if (!add_portable_property_key(emitted_keys, prefix,
                                       portable_tag_name)) {
            return false;
        }

        if (emit_portable_exif_tag_property_override(w, prefix, ifd,
                                                     e.key.data.exif_tag.tag,
                                                     portable_tag_name, arena,
                                                     e.value)) {
            return true;
        }

        return emit_portable_property(w, prefix, portable_tag_name, arena,
                                      e.value);
    }

    static bool
    portable_indexed_property_less(const PortableIndexedProperty& a,
                                   const PortableIndexedProperty& b) noexcept
    {
        if (a.prefix != b.prefix) {
            return a.prefix < b.prefix;
        }
        if (a.base != b.base) {
            return a.base < b.base;
        }
        if (a.index != b.index) {
            return a.index < b.index;
        }
        return a.order < b.order;
    }


    static bool emit_portable_indexed_property_seq(
        SpanWriter* w, std::string_view prefix, std::string_view name,
        const ByteArena& arena,
        std::span<const PortableIndexedProperty> items) noexcept
    {
        if (!w || prefix.empty() || name.empty() || items.empty()) {
            return false;
        }

        uint32_t valid = 0U;
        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value) {
                continue;
            }
            if (portable_scalar_like_value_supported(arena, *items[i].value)) {
                valid += 1U;
            }
        }
        if (valid == 0U) {
            return false;
        }

        w->append(kIndent3);
        w->append("<");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        w->append(kIndent4);
        w->append("<rdf:Seq>\n");

        for (size_t i = 0; i < items.size(); ++i) {
            if (!items[i].value
                || !portable_scalar_like_value_supported(arena,
                                                         *items[i].value)) {
                continue;
            }
            w->append(kIndent4);
            w->append(kIndent1);
            w->append("<rdf:li>");
            (void)emit_portable_value_inline(arena, *items[i].value, w);
            w->append("</rdf:li>\n");
        }

        w->append(kIndent4);
        w->append("</rdf:Seq>\n");
        w->append(kIndent3);
        w->append("</");
        w->append(prefix);
        w->append(":");
        w->append(name);
        w->append(">\n");
        return true;
    }

    static void
    emit_portable_indexed_groups(SpanWriter* w, const ByteArena& arena,
                                 std::vector<PortableIndexedProperty>* indexed,
                                 PortablePropertyKeySet* emitted_keys,
                                 uint32_t max_entries,
                                 uint32_t* emitted) noexcept
    {
        if (!w || !indexed || !emitted_keys || !emitted || w->limit_hit
            || indexed->empty()) {
            return;
        }

        std::stable_sort(indexed->begin(), indexed->end(),
                         portable_indexed_property_less);

        size_t i = 0U;
        while (i < indexed->size()) {
            if (max_entries != 0U && *emitted >= max_entries) {
                w->limit_hit = true;
                break;
            }

            size_t j = i + 1U;
            while (j < indexed->size()
                   && (*indexed)[j].prefix == (*indexed)[i].prefix
                   && (*indexed)[j].base == (*indexed)[i].base) {
                j += 1U;
            }

            if (!add_portable_property_key(emitted_keys, (*indexed)[i].prefix,
                                           (*indexed)[i].base)) {
                i = j;
                continue;
            }

            if (emit_portable_indexed_property_seq(
                    w, (*indexed)[i].prefix, (*indexed)[i].base, arena,
                    std::span<const PortableIndexedProperty>(indexed->data() + i,
                                                             j - i))) {
                *emitted += 1U;
            }

            i = j;
        }
    }

}  // namespace


XmpDumpResult
dump_xmp_portable(const MetaStore& store, std::span<std::byte> out,
                  const XmpPortableOptions& options) noexcept
{
    XmpDumpResult r;
    SpanWriter w(out, options.limits.max_output_bytes);

    static constexpr std::array<XmpNsDecl, 4> kDecls = {
        XmpNsDecl { "xmp", kXmpNsXmp },
        XmpNsDecl { "tiff", kXmpNsTiff },
        XmpNsDecl { "exif", kXmpNsExif },
        XmpNsDecl { "dc", kXmpNsDc },
    };
    emit_xmp_packet_begin(&w, std::span<const XmpNsDecl>(kDecls.data(),
                                                         kDecls.size()));

    const ByteArena& arena          = store.arena();
    const std::span<const Entry> es = store.entries();

    std::vector<PortableIndexedProperty> indexed;
    indexed.reserve(128);
    PortablePropertyKeySet emitted_keys;
    emitted_keys.reserve(256);

    uint32_t emitted = 0;
    for (size_t i = 0; i < es.size(); ++i) {
        if (options.limits.max_entries != 0U
            && emitted >= options.limits.max_entries) {
            w.limit_hit = true;
            break;
        }

        const Entry& e = es[i];
        if (any(e.flags, EntryFlags::Deleted)) {
            continue;
        }

        if (e.key.kind == MetaKeyKind::ExifTag) {
            if (!options.include_exif) {
                continue;
            }
            if (process_portable_exif_entry(arena, e, &w, &emitted_keys)) {
                emitted += 1U;
            }
            continue;
        }

        if (e.key.kind == MetaKeyKind::XmpProperty
            && options.include_existing_xmp) {
            if (process_portable_existing_xmp_entry(arena, e,
                                                    static_cast<uint32_t>(i),
                                                    &w, &emitted_keys,
                                                    &indexed)) {
                emitted += 1U;
            }
            continue;
        }
    }

    emit_portable_indexed_groups(&w, arena, &indexed, &emitted_keys,
                                 options.limits.max_entries, &emitted);

    emit_xmp_packet_end(&w);

    r.entries = emitted;
    if (w.limit_hit) {
        r.status = XmpDumpStatus::LimitExceeded;
    } else if (w.needed > static_cast<uint64_t>(out.size())) {
        r.status = XmpDumpStatus::OutputTruncated;
    } else {
        r.status = XmpDumpStatus::Ok;
    }

    r.written = (w.written < w.needed) ? w.written : w.needed;
    r.needed  = w.needed;
    return r;
}


XmpDumpResult
dump_xmp_sidecar(const MetaStore& store, std::vector<std::byte>* out,
                 const XmpSidecarOptions& options) noexcept
{
    XmpDumpResult r;
    if (!out) {
        r.status = XmpDumpStatus::LimitExceeded;
        return r;
    }

    uint64_t initial_bytes = options.initial_output_bytes;
    if (initial_bytes == 0U) {
        initial_bytes = 1024ULL * 1024ULL;
    }
    if (initial_bytes > static_cast<uint64_t>(SIZE_MAX)) {
        r.status = XmpDumpStatus::LimitExceeded;
        return r;
    }
    out->assign(static_cast<size_t>(initial_bytes), std::byte { 0 });

    for (;;) {
        const std::span<std::byte> span(out->data(), out->size());
        if (options.format == XmpSidecarFormat::Portable) {
            r = dump_xmp_portable(store, span, options.portable);
        } else {
            r = dump_xmp_lossless(store, span, options.lossless);
        }

        if (r.status == XmpDumpStatus::OutputTruncated
            && r.needed > out->size()) {
            if (r.needed > static_cast<uint64_t>(SIZE_MAX)) {
                r.status = XmpDumpStatus::LimitExceeded;
                return r;
            }
            out->resize(static_cast<size_t>(r.needed));
            continue;
        }
        break;
    }

    if (r.status == XmpDumpStatus::Ok && r.written <= out->size()) {
        out->resize(static_cast<size_t>(r.written));
    }
    return r;
}

}  // namespace openmeta
