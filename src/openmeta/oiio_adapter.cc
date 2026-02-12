#include "openmeta/oiio_adapter.h"

#include "interop_safety_internal.h"
#include "interop_value_format_internal.h"

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
               && (static_cast<unsigned char>((*text)[cut]) & 0xC0U)
                      == 0x80U) {
            cut -= 1U;
        }
        text->resize(cut);
        text->append("...");
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

        void on_item(const ExportItem& item) override
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

        void on_item(const ExportItem& item) override
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

        void on_item(const ExportItem& item) override
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
                    status_ = InteropSafetyStatus::UnsafeData;
                    return;
                }
                has_value = (s == SafeTextStatus::Ok);
                truncate_utf8_for_limit(&value_text, max_value_bytes_);
            } else if (value.kind == MetaValueKind::Bytes) {
                set_safety_error(error_, InteropSafetyReason::UnsafeBytes,
                                 item.name, item.name,
                                 "unsafe bytes value in OIIO attribute");
                status_ = InteropSafetyStatus::UnsafeData;
                return;
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

        void on_item(const ExportItem& item) override
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
                set_safety_error(error_, InteropSafetyReason::UnsafeBytes,
                                 item.name, item.name,
                                 "unsafe bytes value in typed OIIO attribute");
                status_ = InteropSafetyStatus::UnsafeData;
                return;
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

}  // namespace openmeta
