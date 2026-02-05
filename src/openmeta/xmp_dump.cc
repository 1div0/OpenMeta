#include "openmeta/xmp_dump.h"

#include "openmeta/exif_tag_names.h"
#include "openmeta/geotiff_key_names.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

namespace openmeta {
namespace {

    static constexpr std::string_view kXmpNsX
        = "adobe:ns:meta/";
    static constexpr std::string_view kXmpNsRdf
        = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    static constexpr std::string_view kXmpNsOpenMetaDump
        = "urn:openmeta:dump:1.0";

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

            const uint64_t cap = static_cast<uint64_t>(out.size());
            const uint64_t w   = (written < cap) ? (cap - written) : 0U;
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


    struct Base64Encoder final {
        SpanWriter* w      = nullptr;
        uint8_t buf[3]     = { 0, 0, 0 };
        uint32_t buffered  = 0;

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
        const uint32_t elem_size             = meta_element_size(value.elem_type);
        if (elem_size == 0U) {
            return 0U;
        }
        const uint32_t available
            = static_cast<uint32_t>(raw.size() / elem_size);
        return (value.count < available) ? value.count : available;
    }


    static const char* key_kind_name(MetaKeyKind k) noexcept
    {
        switch (k) {
        case MetaKeyKind::ExifTag: return "ExifTag";
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
                                 std::string_view name,
                                 uint64_t value) noexcept
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
                const uint8_t x = static_cast<uint8_t>(static_cast<int8_t>(
                    v.data.i64));
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
            const uint32_t elem_size             = meta_element_size(v.elem_type);
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

            const uint64_t take_bytes
                = static_cast<uint64_t>(n) * elem_size;
            if (take_bytes > raw.size()) {
                return;
            }

            for (uint32_t i = 0; i < n; ++i) {
                const size_t off = static_cast<size_t>(i) * elem_size;
                const std::span<const std::byte> elem
                    = raw.subspan(off, elem_size);

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
            const std::string_view ifd = arena_string(arena, e.key.data.exif_tag.ifd);
            w->append("exif:");
            append_xml_safe_ascii(ifd, w);
            w->append(":");
            char buf[16];
            std::snprintf(buf, sizeof(buf), "0x%04X",
                          static_cast<unsigned>(e.key.data.exif_tag.tag));
            w->append(buf);
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
            const std::string_view ifd = arena_string(arena, e.key.data.exif_tag.ifd);
            emit_text_element(w, kIndent4, "omd:ifd", ifd);
            w->append(kIndent4);
            w->append("<omd:tag>");
            append_u16_hex(e.key.data.exif_tag.tag, w);
            w->append("</omd:tag>\n");
            if (options.include_names) {
                const std::string_view n = exif_tag_name(ifd, e.key.data.exif_tag.tag);
                if (!n.empty()) {
                    emit_text_element(w, kIndent4, "omd:tagName", n);
                }
            }
        } else if (e.key.kind == MetaKeyKind::GeotiffKey && options.include_names) {
            const std::string_view n = geotiff_key_name(e.key.data.geotiff_key.key_id);
            if (!n.empty()) {
                emit_text_element(w, kIndent4, "omd:tagName", n);
            }
        }
    }

}  // namespace


XmpDumpResult
dump_xmp_lossless(const MetaStore& store, std::span<std::byte> out,
                  const XmpDumpOptions& options) noexcept
{
    XmpDumpResult r;

    SpanWriter w(out, options.limits.max_output_bytes);

    w.append("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n");
    w.append("<x:xmpmeta xmlns:x=\"");
    w.append(kXmpNsX);
    w.append("\" x:xmptk=\"OpenMeta\">\n");
    w.append(kIndent1);
    w.append("<rdf:RDF xmlns:rdf=\"");
    w.append(kXmpNsRdf);
    w.append("\" xmlns:omd=\"");
    w.append(kXmpNsOpenMetaDump);
    w.append("\">\n");
    w.append(kIndent2);
    w.append("<rdf:Description rdf:about=\"\">\n");

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

    w.append(kIndent2);
    w.append("</rdf:Description>\n");
    w.append(kIndent1);
    w.append("</rdf:RDF>\n");
    w.append("</x:xmpmeta>\n");

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

}  // namespace openmeta
