#include "openmeta/oiio_adapter.h"

#include "openmeta/ccm_query.h"
#include "openmeta/console_format.h"

#include "interop_safety_internal.h"
#include "interop_value_format_internal.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace openmeta {
namespace {

    using interop_internal::decode_text_to_utf8_safe;
    using interop_internal::SafeTextStatus;
    using interop_internal::set_safety_error;

    static bool looks_like_numeric_unknown_name(std::string_view name) noexcept
    {
        for (size_t i = 0; i + 2 < name.size(); ++i) {
            if (name[i] == '_' && name[i + 1] == '0'
                && (name[i + 2] == 'x' || name[i + 2] == 'X')) {
                return true;
            }
        }
        return false;
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

    static void truncate_utf8_for_limit(std::string* text,
                                        uint32_t max_value_bytes) noexcept
    {
        if (!text || max_value_bytes == 0U || text->size() <= max_value_bytes) {
            return;
        }
        size_t cut = static_cast<size_t>(max_value_bytes);
        while (cut > 0U
               && (static_cast<unsigned char>((*text)[cut]) & 0xC0U) == 0x80U) {
            cut -= 1U;
        }
        text->resize(cut);
        text->append("...");
    }

    static bool exif_ifd_is_gps(const ByteArena& arena,
                                const Entry& entry) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }
        const std::span<const std::byte> raw = arena.span(
            entry.key.data.exif_tag.ifd);
        const std::string_view ifd(reinterpret_cast<const char*>(raw.data()),
                                   raw.size());
        return ifd == "gpsifd" || ifd == "gpsinfo" || ifd.ends_with("_gpsifd");
    }

    static bool exif_byte_tag_allows_safe_oiio_text(const ByteArena& arena,
                                                    const Entry& entry) noexcept
    {
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }
        switch (entry.key.data.exif_tag.tag) {
        case 0x9000U:  // ExifVersion
        case 0x9101U:  // ComponentsConfiguration
        case 0xA000U:  // FlashpixVersion
        case 0xA300U:  // FileSource
        case 0xA301U:  // SceneType
        case 0xA302U:  // CFAPattern
            return true;
        case 0x0000U: return exif_ifd_is_gps(arena, entry);  // GPSVersionID
        default: return false;
        }
    }

    static std::span<const std::byte>
    trim_trailing_nul_bytes(std::span<const std::byte> raw) noexcept
    {
        size_t size = raw.size();
        while (size > 0U && raw[size - 1U] == std::byte { 0 }) {
            size -= 1U;
        }
        return raw.first(size);
    }

    static std::span<const std::byte>
    trim_trailing_utf16_nuls(std::span<const std::byte> raw) noexcept
    {
        size_t size = raw.size();
        while (size >= 2U && raw[size - 2U] == std::byte { 0 }
               && raw[size - 1U] == std::byte { 0 }) {
            size -= 2U;
        }
        return raw.first(size);
    }

    static bool bytes_equal(std::span<const std::byte> raw, const char* text,
                            size_t text_size) noexcept
    {
        if (raw.size() != text_size) {
            return false;
        }
        return std::memcmp(raw.data(), text, text_size) == 0;
    }

    static bool format_decoded_text_bytes_for_oiio(
        std::span<const std::byte> raw, TextEncoding encoding,
        uint32_t max_value_bytes, std::string_view field_name,
        std::string_view key_path, std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (raw.empty()) {
            return false;
        }
        InteropSafetyError error {};
        const SafeTextStatus status
            = decode_text_to_utf8_safe(raw, encoding, field_name, key_path, out,
                                       &error);
        if (status != SafeTextStatus::Ok) {
            out->clear();
            return false;
        }
        truncate_utf8_for_limit(out, max_value_bytes);
        return true;
    }

    static bool
    format_user_comment_bytes_for_oiio(std::span<const std::byte> raw,
                                       uint32_t max_value_bytes,
                                       std::string* out) noexcept
    {
        if (!out || raw.size() < 8U) {
            return false;
        }

        const std::span<const std::byte> prefix = raw.first(8U);
        std::span<const std::byte> payload      = raw.subspan(8U);
        if (bytes_equal(prefix, "ASCII\0\0\0", 8U)) {
            payload = trim_trailing_nul_bytes(payload);
            return format_decoded_text_bytes_for_oiio(payload,
                                                      TextEncoding::Ascii,
                                                      max_value_bytes,
                                                      "UserComment",
                                                      "Exif:UserComment", out);
        }
        if (bytes_equal(prefix, "UTF8\0\0\0\0", 8U)) {
            payload = trim_trailing_nul_bytes(payload);
            return format_decoded_text_bytes_for_oiio(payload,
                                                      TextEncoding::Utf8,
                                                      max_value_bytes,
                                                      "UserComment",
                                                      "Exif:UserComment", out);
        }
        if (bytes_equal(prefix, "UNICODE\0", 8U)) {
            payload = trim_trailing_utf16_nuls(payload);
            if (payload.size() >= 2U) {
                const uint16_t bom_le = static_cast<uint16_t>(
                    static_cast<uint16_t>(static_cast<uint8_t>(payload[0]))
                    | static_cast<uint16_t>(
                        static_cast<uint16_t>(static_cast<uint8_t>(payload[1]))
                        << 8U));
                if (bom_le == 0xFEFFU) {
                    return format_decoded_text_bytes_for_oiio(
                        payload.subspan(2U), TextEncoding::Utf16LE,
                        max_value_bytes, "UserComment", "Exif:UserComment",
                        out);
                }
                if (bom_le == 0xFFFEU) {
                    return format_decoded_text_bytes_for_oiio(
                        payload.subspan(2U), TextEncoding::Utf16BE,
                        max_value_bytes, "UserComment", "Exif:UserComment",
                        out);
                }
            }
            return format_decoded_text_bytes_for_oiio(payload,
                                                      TextEncoding::Utf16LE,
                                                      max_value_bytes,
                                                      "UserComment",
                                                      "Exif:UserComment", out);
        }

        return false;
    }

    static bool format_text_like_bytes_for_oiio(std::string_view field_name,
                                                std::string_view key_path,
                                                const Entry& entry,
                                                std::span<const std::byte> raw,
                                                uint32_t max_value_bytes,
                                                std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (entry.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }
        if (entry.key.kind == MetaKeyKind::ExifTag
            && entry.key.data.exif_tag.tag == 0x9286U
            && format_user_comment_bytes_for_oiio(raw, max_value_bytes, out)) {
            return true;
        }

        const std::span<const std::byte> trimmed = trim_trailing_nul_bytes(raw);
        if (trimmed.empty()) {
            return false;
        }

        if (format_decoded_text_bytes_for_oiio(trimmed, TextEncoding::Ascii,
                                               max_value_bytes, field_name,
                                               key_path, out)) {
            return true;
        }
        return format_decoded_text_bytes_for_oiio(trimmed, TextEncoding::Utf8,
                                                  max_value_bytes, field_name,
                                                  key_path, out);
    }

    static bool format_small_byte_sequence_text(std::span<const std::byte> raw,
                                                std::string* out) noexcept
    {
        if (!out || raw.empty()) {
            return false;
        }
        out->clear();
        if (raw.size() == 1U) {
            *out = std::to_string(
                static_cast<unsigned>(static_cast<uint8_t>(raw[0])));
            return true;
        }
        out->push_back('[');
        for (size_t i = 0U; i < raw.size(); ++i) {
            if (i != 0U) {
                out->append(", ");
            }
            out->append(std::to_string(
                static_cast<unsigned>(static_cast<uint8_t>(raw[i]))));
        }
        out->push_back(']');
        return true;
    }

    static bool format_safe_exif_bytes_for_oiio(const ByteArena& arena,
                                                const Entry& entry,
                                                uint32_t max_value_bytes,
                                                std::string* out) noexcept
    {
        if (!out || !exif_byte_tag_allows_safe_oiio_text(arena, entry)) {
            return false;
        }

        const MetaValue& value = entry.value;
        if (value.kind != MetaValueKind::Bytes) {
            return false;
        }

        const std::span<const std::byte> raw = arena.span(value.data.span);
        if (raw.empty()) {
            return false;
        }

        const uint16_t tag = entry.key.data.exif_tag.tag;
        if (tag == 0x9000U || tag == 0xA000U) {
            out->clear();
            for (size_t i = 0U; i < raw.size(); ++i) {
                const unsigned char c = static_cast<unsigned char>(
                    static_cast<uint8_t>(raw[i]));
                if (c == 0U) {
                    break;
                }
                if (!std::isprint(c)) {
                    out->clear();
                    break;
                }
                out->push_back(static_cast<char>(c));
            }
            if (!out->empty()) {
                truncate_utf8_for_limit(out, max_value_bytes);
                return true;
            }
        }

        if (!format_small_byte_sequence_text(raw, out)) {
            return false;
        }
        truncate_utf8_for_limit(out, max_value_bytes);
        return true;
    }

    static void format_safe_bytes_hex_for_oiio(std::span<const std::byte> raw,
                                               uint32_t max_value_bytes,
                                               std::string* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        out->append("0x");
        append_hex_bytes(raw, max_value_bytes, out);
    }

    static bool copy_typed_value(const ByteArena& arena, const MetaValue& in,
                                 uint32_t max_value_bytes,
                                 OiioTypedValue* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->kind          = in.kind;
        out->elem_type     = in.elem_type;
        out->text_encoding = in.text_encoding;
        out->count         = in.count;
        out->data          = in.data;
        out->storage.clear();

        if (in.kind == MetaValueKind::Scalar
            || in.kind == MetaValueKind::Empty) {
            return in.kind != MetaValueKind::Empty;
        }

        const std::span<const std::byte> raw = arena.span(in.data.span);
        size_t to_copy                       = raw.size();
        if (max_value_bytes != 0U
            && to_copy > static_cast<size_t>(max_value_bytes)) {
            to_copy = static_cast<size_t>(max_value_bytes);
        }

        if (in.kind == MetaValueKind::Array) {
            const uint32_t elem_size = meta_element_size(in.elem_type);
            if (elem_size == 0U) {
                out->count = 0U;
                return false;
            }
            const size_t aligned = to_copy - (to_copy % elem_size);
            to_copy              = aligned;
            out->count           = static_cast<uint32_t>(to_copy / elem_size);
        } else {
            out->count = static_cast<uint32_t>(to_copy);
        }

        if (to_copy == 0U) {
            return false;
        }

        out->storage.assign(
            raw.begin(),
            raw.begin()
                + static_cast<std::span<const std::byte>::difference_type>(
                    to_copy));
        out->data.span = ByteSpan { 0U, static_cast<uint32_t>(to_copy) };
        return true;
    }

    static uint64_t double_bits(double v) noexcept
    {
        uint64_t bits = 0;
        std::memcpy(&bits, &v, sizeof(uint64_t));
        return bits;
    }

    static void set_typed_f64_value(std::span<const double> values,
                                    OiioTypedValue* out) noexcept
    {
        if (!out) {
            return;
        }
        *out               = OiioTypedValue {};
        out->elem_type     = MetaElementType::F64;
        out->text_encoding = TextEncoding::Unknown;
        out->count         = static_cast<uint32_t>(values.size());
        if (values.empty()) {
            out->kind = MetaValueKind::Empty;
            return;
        }
        if (values.size() == 1U) {
            out->kind          = MetaValueKind::Scalar;
            out->data.f64_bits = double_bits(values[0]);
            return;
        }
        out->kind = MetaValueKind::Array;
        out->storage.resize(values.size() * sizeof(double));
        std::memcpy(out->storage.data(), values.data(),
                    values.size() * sizeof(double));
        out->data.span
            = ByteSpan { 0U, static_cast<uint32_t>(out->storage.size()) };
    }

    static void
    append_normalized_ccm_typed(const MetaStore& store,
                                uint32_t max_value_bytes,
                                std::vector<OiioTypedAttribute>* out) noexcept
    {
        if (!out) {
            return;
        }
        CcmQueryOptions q;
        if (max_value_bytes != 0U) {
            const uint32_t max_values     = max_value_bytes / 8U;
            q.limits.max_values_per_field = std::max(1U, max_values);
        }
        std::vector<CcmField> fields;
        (void)collect_dng_ccm_fields(store, &fields, q);

        for (size_t i = 0; i < fields.size(); ++i) {
            const CcmField& f = fields[i];
            if (f.values.empty() || f.name.empty()) {
                continue;
            }
            OiioTypedAttribute a;
            a.name.append("DNGNorm:");
            if (!f.ifd.empty()) {
                a.name.append(f.ifd);
                a.name.push_back('.');
            }
            a.name.append(f.name);
            set_typed_f64_value(std::span<const double>(f.values.data(),
                                                        f.values.size()),
                                &a.value);
            out->push_back(std::move(a));
        }
    }

    class OiioCollectSink final : public MetadataSink {
    public:
        OiioCollectSink(const ByteArena& arena, std::vector<OiioAttribute>* out,
                        uint32_t max_value_bytes, bool include_empty) noexcept
            : arena_(arena)
            , out_(out)
            , max_value_bytes_(max_value_bytes)
            , include_empty_(include_empty)
        {
        }

        void on_item(const ExportItem& item) noexcept override
        {
            if (!out_ || !item.entry) {
                return;
            }

            std::string value_text;
            const bool has_value = interop_internal::format_value_for_text(
                arena_, item.entry->value, max_value_bytes_, &value_text);
            if (!has_value && !include_empty_
                && !looks_like_numeric_unknown_name(item.name)
                && item.name != "Exif:MakerNote") {
                return;
            }

            OiioAttribute attribute;
            attribute.name.assign(item.name.data(), item.name.size());
            attribute.value = std::move(value_text);
            out_->push_back(std::move(attribute));
        }

    private:
        const ByteArena& arena_;
        std::vector<OiioAttribute>* out_;
        uint32_t max_value_bytes_;
        bool include_empty_;
    };

    class OiioCollectTypedSink final : public MetadataSink {
    public:
        OiioCollectTypedSink(const ByteArena& arena,
                             std::vector<OiioTypedAttribute>* out,
                             uint32_t max_value_bytes,
                             bool include_empty) noexcept
            : arena_(arena)
            , out_(out)
            , max_value_bytes_(max_value_bytes)
            , include_empty_(include_empty)
        {
        }

        void on_item(const ExportItem& item) noexcept override
        {
            if (!out_ || !item.entry) {
                return;
            }

            OiioTypedAttribute attribute;
            attribute.name.assign(item.name.data(), item.name.size());
            const bool has_value = copy_typed_value(arena_, item.entry->value,
                                                    max_value_bytes_,
                                                    &attribute.value);
            if (!has_value && !include_empty_
                && !looks_like_numeric_unknown_name(item.name)
                && item.name != "Exif:MakerNote") {
                return;
            }
            out_->push_back(std::move(attribute));
        }

    private:
        const ByteArena& arena_;
        std::vector<OiioTypedAttribute>* out_;
        uint32_t max_value_bytes_;
        bool include_empty_;
    };

    class OiioCollectSafeSink final : public MetadataSink {
    public:
        OiioCollectSafeSink(const ByteArena& arena,
                            std::vector<OiioAttribute>* out,
                            uint32_t max_value_bytes, bool include_empty,
                            InteropSafetyError* error) noexcept
            : arena_(arena)
            , out_(out)
            , max_value_bytes_(max_value_bytes)
            , include_empty_(include_empty)
            , error_(error)
        {
        }

        void on_item(const ExportItem& item) noexcept override
        {
            if (status_ != InteropSafetyStatus::Ok || !out_ || !item.entry) {
                return;
            }

            std::string value_text;
            bool has_value = false;

            const MetaValue& value = item.entry->value;
            if (value.kind == MetaValueKind::Text) {
                const std::span<const std::byte> raw = arena_.span(
                    value.data.span);
                const SafeTextStatus s
                    = decode_text_to_utf8_safe(raw, value.text_encoding,
                                               item.name, item.name,
                                               &value_text, error_);
                if (s == SafeTextStatus::Error) {
                    format_safe_bytes_hex_for_oiio(raw, max_value_bytes_,
                                                   &value_text);
                    if (error_) {
                        *error_ = InteropSafetyError {};
                    }
                    has_value = !value_text.empty();
                } else {
                    has_value = (s == SafeTextStatus::Ok);
                    truncate_utf8_for_limit(&value_text, max_value_bytes_);
                }
            } else if (value.kind == MetaValueKind::Bytes) {
                const std::span<const std::byte> raw = arena_.span(
                    value.data.span);
                if (format_safe_exif_bytes_for_oiio(arena_, *item.entry,
                                                    max_value_bytes_,
                                                    &value_text)
                    || format_text_like_bytes_for_oiio(item.name, item.name,
                                                       *item.entry, raw,
                                                       max_value_bytes_,
                                                       &value_text)) {
                    has_value = !value_text.empty();
                } else {
                    format_safe_bytes_hex_for_oiio(raw, max_value_bytes_,
                                                   &value_text);
                    has_value = !value_text.empty();
                }
            } else {
                has_value = interop_internal::format_value_for_text(
                    arena_, value, max_value_bytes_, &value_text);
            }

            if (!has_value && !include_empty_
                && !looks_like_numeric_unknown_name(item.name)
                && item.name != "Exif:MakerNote") {
                return;
            }

            OiioAttribute attribute;
            attribute.name.assign(item.name.data(), item.name.size());
            attribute.value = std::move(value_text);
            out_->push_back(std::move(attribute));
        }

        InteropSafetyStatus status() const noexcept { return status_; }

    private:
        const ByteArena& arena_;
        std::vector<OiioAttribute>* out_;
        uint32_t max_value_bytes_;
        bool include_empty_;
        InteropSafetyError* error_  = nullptr;
        InteropSafetyStatus status_ = InteropSafetyStatus::Ok;
    };

    class OiioCollectTypedSafeSink final : public MetadataSink {
    public:
        OiioCollectTypedSafeSink(const ByteArena& arena,
                                 std::vector<OiioTypedAttribute>* out,
                                 uint32_t max_value_bytes, bool include_empty,
                                 InteropSafetyError* error) noexcept
            : arena_(arena)
            , out_(out)
            , max_value_bytes_(max_value_bytes)
            , include_empty_(include_empty)
            , error_(error)
        {
        }

        void on_item(const ExportItem& item) noexcept override
        {
            if (status_ != InteropSafetyStatus::Ok || !out_ || !item.entry) {
                return;
            }

            OiioTypedAttribute attribute;
            attribute.name.assign(item.name.data(), item.name.size());
            const MetaValue& value = item.entry->value;

            bool has_value = copy_typed_value(arena_, value, max_value_bytes_,
                                              &attribute.value);

            if (value.kind == MetaValueKind::Text) {
                std::string decoded;
                const std::span<const std::byte> raw = arena_.span(
                    value.data.span);
                const SafeTextStatus s
                    = decode_text_to_utf8_safe(raw, value.text_encoding,
                                               item.name, item.name, &decoded,
                                               error_);
                if (s == SafeTextStatus::Error) {
                    status_ = InteropSafetyStatus::UnsafeData;
                    return;
                }
                has_value = (s == SafeTextStatus::Ok);
                truncate_utf8_for_limit(&decoded, max_value_bytes_);

                attribute.value.kind          = MetaValueKind::Text;
                attribute.value.elem_type     = MetaElementType::U8;
                attribute.value.text_encoding = TextEncoding::Utf8;
                attribute.value.storage.assign(
                    reinterpret_cast<const std::byte*>(decoded.data()),
                    reinterpret_cast<const std::byte*>(decoded.data())
                        + decoded.size());
                attribute.value.count = static_cast<uint32_t>(
                    attribute.value.storage.size());
                attribute.value.data.span = ByteSpan { 0U,
                                                       attribute.value.count };
            } else if (value.kind == MetaValueKind::Bytes) {
                const std::span<const std::byte> raw = arena_.span(
                    value.data.span);
                std::string formatted;
                if (!format_safe_exif_bytes_for_oiio(arena_, *item.entry,
                                                     max_value_bytes_,
                                                     &formatted)
                    && !format_text_like_bytes_for_oiio(item.name, item.name,
                                                        *item.entry, raw,
                                                        max_value_bytes_,
                                                        &formatted)) {
                    set_safety_error(
                        error_, InteropSafetyReason::UnsafeBytes, item.name,
                        item.name,
                        "unsafe bytes value in typed OIIO attribute");
                    status_ = InteropSafetyStatus::UnsafeData;
                    return;
                }
                has_value                     = !formatted.empty();
                attribute.value.kind          = MetaValueKind::Text;
                attribute.value.elem_type     = MetaElementType::U8;
                attribute.value.text_encoding = TextEncoding::Utf8;
                attribute.value.storage.assign(
                    reinterpret_cast<const std::byte*>(formatted.data()),
                    reinterpret_cast<const std::byte*>(formatted.data())
                        + formatted.size());
                attribute.value.count = static_cast<uint32_t>(
                    attribute.value.storage.size());
                attribute.value.data.span = ByteSpan { 0U,
                                                       attribute.value.count };
            }

            if (!has_value && !include_empty_
                && !looks_like_numeric_unknown_name(item.name)
                && item.name != "Exif:MakerNote") {
                return;
            }

            out_->push_back(std::move(attribute));
        }

        InteropSafetyStatus status() const noexcept { return status_; }

    private:
        const ByteArena& arena_;
        std::vector<OiioTypedAttribute>* out_;
        uint32_t max_value_bytes_;
        bool include_empty_;
        InteropSafetyError* error_  = nullptr;
        InteropSafetyStatus status_ = InteropSafetyStatus::Ok;
    };

    static const char*
    oiio_transfer_semantic_name(OiioTransferPayloadKind kind) noexcept
    {
        switch (kind) {
        case OiioTransferPayloadKind::ExifBlob: return "ExifBlob";
        case OiioTransferPayloadKind::XmpPacket: return "XMPPacket";
        case OiioTransferPayloadKind::IccProfile: return "ICCProfile";
        case OiioTransferPayloadKind::IptcBlock: return "IPTCBlock";
        case OiioTransferPayloadKind::Jumbf: return "JUMBF";
        case OiioTransferPayloadKind::C2pa: return "C2PA";
        case OiioTransferPayloadKind::Unknown: break;
        }
        return "Unknown";
    }

    static OiioTransferPayloadKind
    oiio_transfer_kind_from_semantic(TransferSemanticKind kind) noexcept
    {
        switch (kind) {
        case TransferSemanticKind::Exif:
            return OiioTransferPayloadKind::ExifBlob;
        case TransferSemanticKind::Xmp:
            return OiioTransferPayloadKind::XmpPacket;
        case TransferSemanticKind::Icc:
            return OiioTransferPayloadKind::IccProfile;
        case TransferSemanticKind::Iptc:
            return OiioTransferPayloadKind::IptcBlock;
        case TransferSemanticKind::Jumbf: return OiioTransferPayloadKind::Jumbf;
        case TransferSemanticKind::C2pa: return OiioTransferPayloadKind::C2pa;
        case TransferSemanticKind::Unknown: break;
        }
        return OiioTransferPayloadKind::Unknown;
    }

    static OiioTransferPayloadView make_oiio_transfer_payload_view(
        const PreparedTransferPayloadView& payload) noexcept
    {
        OiioTransferPayloadView one;
        one.semantic_kind = oiio_transfer_kind_from_semantic(
            payload.semantic_kind);
        one.semantic_name = oiio_transfer_semantic_name(one.semantic_kind);
        one.route         = payload.route;
        one.op            = payload.op;
        one.payload       = payload.payload;
        return one;
    }

    static OiioTransferPayload
    make_oiio_transfer_payload(const PreparedTransferPayload& payload) noexcept
    {
        OiioTransferPayload one;
        one.semantic_kind = oiio_transfer_kind_from_semantic(
            payload.semantic_kind);
        one.semantic_name = oiio_transfer_semantic_name(one.semantic_kind);
        one.route         = payload.route;
        one.op            = payload.op;
        one.payload       = payload.payload;
        return one;
    }

    static OiioTransferPackageView make_oiio_transfer_package_view(
        const PreparedTransferPackageView& chunk) noexcept
    {
        OiioTransferPackageView one;
        one.semantic_kind = oiio_transfer_kind_from_semantic(
            chunk.semantic_kind);
        one.semantic_name    = oiio_transfer_semantic_name(one.semantic_kind);
        one.route            = chunk.route;
        one.package_kind     = chunk.package_kind;
        one.output_offset    = chunk.output_offset;
        one.jpeg_marker_code = chunk.jpeg_marker_code;
        one.bytes            = chunk.bytes;
        return one;
    }

    struct OiioPackageReplayAdapterState final {
        const OiioTransferPackageReplayCallbacks* callbacks = nullptr;
    };

    struct OiioPayloadReplayAdapterState final {
        const OiioTransferPayloadReplayCallbacks* callbacks = nullptr;
    };

    static TransferStatus
    oiio_replay_begin_payload(void* user, TransferTargetFormat target_format,
                              uint32_t payload_count) noexcept
    {
        if (!user) {
            return TransferStatus::InvalidArgument;
        }
        const OiioPayloadReplayAdapterState* state
            = static_cast<const OiioPayloadReplayAdapterState*>(user);
        if (!state->callbacks || !state->callbacks->begin_batch) {
            return TransferStatus::Ok;
        }
        return state->callbacks->begin_batch(state->callbacks->user,
                                             target_format, payload_count);
    }

    static TransferStatus oiio_replay_emit_payload(
        void* user, const PreparedTransferPayloadView* payload) noexcept
    {
        if (!user || !payload) {
            return TransferStatus::InvalidArgument;
        }
        const OiioPayloadReplayAdapterState* state
            = static_cast<const OiioPayloadReplayAdapterState*>(user);
        if (!state->callbacks || !state->callbacks->emit_payload) {
            return TransferStatus::InvalidArgument;
        }
        const OiioTransferPayloadView view = make_oiio_transfer_payload_view(
            *payload);
        return state->callbacks->emit_payload(state->callbacks->user, &view);
    }

    static TransferStatus
    oiio_replay_end_payload(void* user,
                            TransferTargetFormat target_format) noexcept
    {
        if (!user) {
            return TransferStatus::InvalidArgument;
        }
        const OiioPayloadReplayAdapterState* state
            = static_cast<const OiioPayloadReplayAdapterState*>(user);
        if (!state->callbacks || !state->callbacks->end_batch) {
            return TransferStatus::Ok;
        }
        return state->callbacks->end_batch(state->callbacks->user,
                                           target_format);
    }

    static TransferStatus
    oiio_replay_begin_batch(void* user, TransferTargetFormat target_format,
                            uint32_t chunk_count) noexcept
    {
        if (!user) {
            return TransferStatus::InvalidArgument;
        }
        const OiioPackageReplayAdapterState* state
            = static_cast<const OiioPackageReplayAdapterState*>(user);
        if (!state->callbacks || !state->callbacks->begin_batch) {
            return TransferStatus::Ok;
        }
        return state->callbacks->begin_batch(state->callbacks->user,
                                             target_format, chunk_count);
    }

    static TransferStatus oiio_replay_emit_chunk(
        void* user, const PreparedTransferPackageView* package_view) noexcept
    {
        if (!user || !package_view) {
            return TransferStatus::InvalidArgument;
        }
        const OiioPackageReplayAdapterState* state
            = static_cast<const OiioPackageReplayAdapterState*>(user);
        if (!state->callbacks || !state->callbacks->emit_chunk) {
            return TransferStatus::InvalidArgument;
        }
        const OiioTransferPackageView view = make_oiio_transfer_package_view(
            *package_view);
        return state->callbacks->emit_chunk(state->callbacks->user, &view);
    }

    static TransferStatus
    oiio_replay_end_batch(void* user,
                          TransferTargetFormat target_format) noexcept
    {
        if (!user) {
            return TransferStatus::InvalidArgument;
        }
        const OiioPackageReplayAdapterState* state
            = static_cast<const OiioPackageReplayAdapterState*>(user);
        if (!state->callbacks || !state->callbacks->end_batch) {
            return TransferStatus::Ok;
        }
        return state->callbacks->end_batch(state->callbacks->user,
                                           target_format);
    }

}  // namespace


void
collect_oiio_attributes(const MetaStore& store, std::vector<OiioAttribute>* out,
                        const OiioAdapterOptions& options) noexcept
{
    if (!out) {
        return;
    }
    out->clear();

    OiioCollectSink sink(store.arena(), out, options.max_value_bytes,
                         options.include_empty);
    visit_metadata(store, options.export_options, sink);
}


InteropSafetyStatus
collect_oiio_attributes_safe(const MetaStore& store,
                             std::vector<OiioAttribute>* out,
                             const OiioAdapterOptions& options,
                             InteropSafetyError* error) noexcept
{
    if (error) {
        *error = InteropSafetyError {};
    }
    if (!out) {
        set_safety_error(error, InteropSafetyReason::InternalMismatch, {}, {},
                         "null output vector");
        return InteropSafetyStatus::InvalidArgument;
    }
    out->clear();

    OiioCollectSafeSink sink(store.arena(), out, options.max_value_bytes,
                             options.include_empty, error);
    visit_metadata(store, options.export_options, sink);
    return sink.status();
}


void
collect_oiio_attributes_typed(const MetaStore& store,
                              std::vector<OiioTypedAttribute>* out,
                              const OiioAdapterOptions& options) noexcept
{
    if (!out) {
        return;
    }
    out->clear();

    OiioCollectTypedSink sink(store.arena(), out, options.max_value_bytes,
                              options.include_empty);
    visit_metadata(store, options.export_options, sink);
    if (options.include_normalized_ccm) {
        append_normalized_ccm_typed(store, options.max_value_bytes, out);
    }
}


InteropSafetyStatus
collect_oiio_attributes_typed_safe(const MetaStore& store,
                                   std::vector<OiioTypedAttribute>* out,
                                   const OiioAdapterOptions& options,
                                   InteropSafetyError* error) noexcept
{
    if (error) {
        *error = InteropSafetyError {};
    }
    if (!out) {
        set_safety_error(error, InteropSafetyReason::InternalMismatch, {}, {},
                         "null output vector");
        return InteropSafetyStatus::InvalidArgument;
    }
    out->clear();

    OiioCollectTypedSafeSink sink(store.arena(), out, options.max_value_bytes,
                                  options.include_empty, error);
    visit_metadata(store, options.export_options, sink);
    if (sink.status() == InteropSafetyStatus::Ok
        && options.include_normalized_ccm) {
        append_normalized_ccm_typed(store, options.max_value_bytes, out);
    }
    return sink.status();
}


OiioAdapterOptions
make_oiio_adapter_options(const OiioAdapterRequest& request) noexcept
{
    OiioAdapterOptions options;
    options.max_value_bytes                   = request.max_value_bytes;
    options.include_empty                     = request.include_empty;
    options.export_options.name_policy        = request.name_policy;
    options.export_options.include_makernotes = request.include_makernotes;
    options.export_options.include_origin     = request.include_origin;
    options.export_options.include_flags      = request.include_flags;
    options.include_normalized_ccm            = request.include_normalized_ccm;
    return options;
}


void
collect_oiio_attributes(const MetaStore& store, std::vector<OiioAttribute>* out,
                        const OiioAdapterRequest& request) noexcept
{
    const OiioAdapterOptions options = make_oiio_adapter_options(request);
    collect_oiio_attributes(store, out, options);
}


InteropSafetyStatus
collect_oiio_attributes_safe(const MetaStore& store,
                             std::vector<OiioAttribute>* out,
                             const OiioAdapterRequest& request,
                             InteropSafetyError* error) noexcept
{
    const OiioAdapterOptions options = make_oiio_adapter_options(request);
    return collect_oiio_attributes_safe(store, out, options, error);
}


void
collect_oiio_attributes_typed(const MetaStore& store,
                              std::vector<OiioTypedAttribute>* out,
                              const OiioAdapterRequest& request) noexcept
{
    const OiioAdapterOptions options = make_oiio_adapter_options(request);
    collect_oiio_attributes_typed(store, out, options);
}


InteropSafetyStatus
collect_oiio_attributes_typed_safe(const MetaStore& store,
                                   std::vector<OiioTypedAttribute>* out,
                                   const OiioAdapterRequest& request,
                                   InteropSafetyError* error) noexcept
{
    const OiioAdapterOptions options = make_oiio_adapter_options(request);
    return collect_oiio_attributes_typed_safe(store, out, options, error);
}


EmitTransferResult
collect_oiio_transfer_payload_views(const PreparedTransferBundle& bundle,
                                    std::vector<OiioTransferPayloadView>* out,
                                    const EmitTransferOptions& options) noexcept
{
    if (!out) {
        EmitTransferResult result;
        result.status  = TransferStatus::InvalidArgument;
        result.code    = EmitTransferCode::InvalidArgument;
        result.errors  = 1U;
        result.message = "out is null";
        return result;
    }

    std::vector<PreparedTransferPayloadView> views;
    EmitTransferResult result
        = collect_prepared_transfer_payload_views(bundle, &views, options);
    if (result.status != TransferStatus::Ok) {
        return result;
    }

    out->clear();
    out->reserve(views.size());
    for (size_t i = 0; i < views.size(); ++i) {
        out->push_back(make_oiio_transfer_payload_view(views[i]));
    }
    return result;
}


EmitTransferResult
build_oiio_transfer_payload_batch(const PreparedTransferBundle& bundle,
                                  OiioTransferPayloadBatch* out,
                                  const EmitTransferOptions& options) noexcept
{
    if (!out) {
        EmitTransferResult result;
        result.status  = TransferStatus::InvalidArgument;
        result.code    = EmitTransferCode::InvalidArgument;
        result.errors  = 1U;
        result.message = "out is null";
        return result;
    }

    PreparedTransferPayloadBatch payload_batch;
    EmitTransferResult result
        = build_prepared_transfer_payload_batch(bundle, &payload_batch,
                                                options);
    if (result.status != TransferStatus::Ok) {
        return result;
    }

    OiioTransferPayloadBatch batch;
    batch.contract_version = payload_batch.contract_version;
    batch.target_format    = payload_batch.target_format;
    batch.emit             = payload_batch.emit;
    batch.payloads.reserve(payload_batch.payloads.size());

    for (size_t i = 0; i < payload_batch.payloads.size(); ++i) {
        batch.payloads.push_back(
            make_oiio_transfer_payload(payload_batch.payloads[i]));
    }

    *out = std::move(batch);
    return result;
}

EmitTransferResult
collect_oiio_transfer_payload_views(
    const PreparedTransferPayloadBatch& batch,
    std::vector<OiioTransferPayloadView>* out) noexcept
{
    EmitTransferResult result;
    if (!out) {
        result.status  = TransferStatus::InvalidArgument;
        result.code    = EmitTransferCode::InvalidArgument;
        result.errors  = 1U;
        result.message = "out is null";
        return result;
    }

    std::vector<PreparedTransferPayloadView> views;
    result = collect_prepared_transfer_payload_views(batch, &views);
    if (result.status != TransferStatus::Ok) {
        return result;
    }

    out->clear();
    out->reserve(views.size());
    for (size_t i = 0; i < views.size(); ++i) {
        out->push_back(make_oiio_transfer_payload_view(views[i]));
    }
    return result;
}

OiioTransferPayloadReplayResult
replay_oiio_transfer_payload_batch(
    const PreparedTransferPayloadBatch& batch,
    const OiioTransferPayloadReplayCallbacks& callbacks) noexcept
{
    if (!callbacks.emit_payload) {
        OiioTransferPayloadReplayResult result;
        result.status  = TransferStatus::InvalidArgument;
        result.code    = EmitTransferCode::InvalidArgument;
        result.message = "emit_payload callback is null";
        return result;
    }

    OiioPayloadReplayAdapterState state;
    state.callbacks = &callbacks;

    PreparedTransferPayloadReplayCallbacks generic_callbacks;
    generic_callbacks.begin_batch  = oiio_replay_begin_payload;
    generic_callbacks.emit_payload = oiio_replay_emit_payload;
    generic_callbacks.end_batch    = oiio_replay_end_payload;
    generic_callbacks.user         = &state;

    const PreparedTransferPayloadReplayResult replay
        = replay_prepared_transfer_payload_batch(batch, generic_callbacks);

    OiioTransferPayloadReplayResult result;
    result.status               = replay.status;
    result.code                 = replay.code;
    result.replayed             = replay.replayed;
    result.failed_payload_index = replay.failed_payload_index;
    result.message              = replay.message;
    return result;
}


EmitTransferResult
collect_oiio_transfer_package_views(
    const PreparedTransferPackageBatch& batch,
    std::vector<OiioTransferPackageView>* out) noexcept
{
    EmitTransferResult result;
    if (!out) {
        result.status  = TransferStatus::InvalidArgument;
        result.code    = EmitTransferCode::InvalidArgument;
        result.errors  = 1U;
        result.message = "out is null";
        return result;
    }

    std::vector<PreparedTransferPackageView> package_views;
    result = collect_prepared_transfer_package_views(batch, &package_views);
    if (result.status != TransferStatus::Ok) {
        return result;
    }

    out->clear();
    out->reserve(package_views.size());
    for (size_t i = 0; i < package_views.size(); ++i) {
        out->push_back(make_oiio_transfer_package_view(package_views[i]));
    }

    result.status  = TransferStatus::Ok;
    result.code    = EmitTransferCode::None;
    result.emitted = static_cast<uint32_t>(out->size());
    return result;
}


OiioTransferPackageReplayResult
replay_oiio_transfer_package_batch(
    const PreparedTransferPackageBatch& batch,
    const OiioTransferPackageReplayCallbacks& callbacks) noexcept
{
    if (!callbacks.emit_chunk) {
        OiioTransferPackageReplayResult result;
        result.status  = TransferStatus::InvalidArgument;
        result.code    = EmitTransferCode::InvalidArgument;
        result.message = "emit_chunk callback is null";
        return result;
    }

    OiioPackageReplayAdapterState state;
    state.callbacks = &callbacks;

    PreparedTransferPackageReplayCallbacks generic_callbacks;
    generic_callbacks.begin_batch = oiio_replay_begin_batch;
    generic_callbacks.emit_chunk  = oiio_replay_emit_chunk;
    generic_callbacks.end_batch   = oiio_replay_end_batch;
    generic_callbacks.user        = &state;

    const PreparedTransferPackageReplayResult replay
        = replay_prepared_transfer_package_batch(batch, generic_callbacks);

    OiioTransferPackageReplayResult result;
    result.status             = replay.status;
    result.code               = replay.code;
    result.replayed           = replay.replayed;
    result.failed_chunk_index = replay.failed_chunk_index;
    result.message            = replay.message;
    return result;
}

}  // namespace openmeta
