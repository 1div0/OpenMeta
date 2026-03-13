#include "openmeta/oiio_adapter.h"

#include "openmeta/ccm_query.h"

#include "interop_safety_internal.h"
#include "interop_value_format_internal.h"

#include <algorithm>
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
    EmitTransferResult result;
    if (!out) {
        result.status  = TransferStatus::InvalidArgument;
        result.code    = EmitTransferCode::InvalidArgument;
        result.errors  = 1U;
        result.message = "out is null";
        return result;
    }

    out->clear();

    PreparedTransferAdapterView view;
    result = build_prepared_transfer_adapter_view(bundle, &view, options);
    if (result.status != TransferStatus::Ok) {
        return result;
    }

    out->reserve(view.ops.size());
    for (size_t i = 0; i < view.ops.size(); ++i) {
        const PreparedTransferAdapterOp& op = view.ops[i];
        const PreparedTransferBlock& block  = bundle.blocks[op.block_index];
        OiioTransferPayloadView payload_view;
        payload_view.semantic_kind = oiio_transfer_kind_from_semantic(
            classify_transfer_route_semantic_kind(block.route));
        payload_view.semantic_name = oiio_transfer_semantic_name(
            payload_view.semantic_kind);
        payload_view.route   = block.route;
        payload_view.op      = op;
        payload_view.payload = std::span<const std::byte>(block.payload.data(),
                                                          block.payload.size());
        out->push_back(payload_view);
    }

    result.emitted = static_cast<uint32_t>(out->size());
    return result;
}


EmitTransferResult
build_oiio_transfer_payload_batch(const PreparedTransferBundle& bundle,
                                  OiioTransferPayloadBatch* out,
                                  const EmitTransferOptions& options) noexcept
{
    EmitTransferResult result;
    if (!out) {
        result.status  = TransferStatus::InvalidArgument;
        result.code    = EmitTransferCode::InvalidArgument;
        result.errors  = 1U;
        result.message = "out is null";
        return result;
    }

    std::vector<OiioTransferPayloadView> views;
    result = collect_oiio_transfer_payload_views(bundle, &views, options);
    if (result.status != TransferStatus::Ok) {
        return result;
    }

    OiioTransferPayloadBatch batch;
    batch.contract_version = bundle.contract_version;
    batch.target_format    = bundle.target_format;
    batch.emit             = options;
    batch.payloads.reserve(views.size());

    for (size_t i = 0; i < views.size(); ++i) {
        const OiioTransferPayloadView& view = views[i];
        OiioTransferPayload payload;
        payload.semantic_kind = view.semantic_kind;
        payload.semantic_name.assign(view.semantic_name.data(),
                                     view.semantic_name.size());
        payload.route.assign(view.route.data(), view.route.size());
        payload.op = view.op;
        payload.payload.assign(view.payload.begin(), view.payload.end());
        batch.payloads.push_back(std::move(payload));
    }

    *out           = std::move(batch);
    result.emitted = static_cast<uint32_t>(out->payloads.size());
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
