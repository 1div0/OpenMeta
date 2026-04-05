// SPDX-License-Identifier: Apache-2.0

#include "openmeta/exr_adapter.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <span>
#include <string_view>

namespace openmeta {
namespace {

    static constexpr uint16_t kExrAttrOpaque = 31U;

    struct ExrTypeCodeMap final {
        std::string_view name;
        uint16_t code = 0;
    };

    static constexpr std::array<ExrTypeCodeMap, 30> kExrTypeCodeMap = {
        ExrTypeCodeMap { "box2i", 1U },
        ExrTypeCodeMap { "box2f", 2U },
        ExrTypeCodeMap { "bytes", 3U },
        ExrTypeCodeMap { "chlist", 4U },
        ExrTypeCodeMap { "chromaticities", 5U },
        ExrTypeCodeMap { "compression", 6U },
        ExrTypeCodeMap { "double", 7U },
        ExrTypeCodeMap { "envmap", 8U },
        ExrTypeCodeMap { "float", 9U },
        ExrTypeCodeMap { "floatvector", 10U },
        ExrTypeCodeMap { "int", 11U },
        ExrTypeCodeMap { "keycode", 12U },
        ExrTypeCodeMap { "lineOrder", 13U },
        ExrTypeCodeMap { "m33f", 14U },
        ExrTypeCodeMap { "m33d", 15U },
        ExrTypeCodeMap { "m44f", 16U },
        ExrTypeCodeMap { "m44d", 17U },
        ExrTypeCodeMap { "preview", 18U },
        ExrTypeCodeMap { "rational", 19U },
        ExrTypeCodeMap { "string", 20U },
        ExrTypeCodeMap { "stringvector", 21U },
        ExrTypeCodeMap { "tiledesc", 22U },
        ExrTypeCodeMap { "timecode", 23U },
        ExrTypeCodeMap { "v2i", 24U },
        ExrTypeCodeMap { "v2f", 25U },
        ExrTypeCodeMap { "v2d", 26U },
        ExrTypeCodeMap { "v3i", 27U },
        ExrTypeCodeMap { "v3f", 28U },
        ExrTypeCodeMap { "v3d", 29U },
        ExrTypeCodeMap { "deepImageState", 30U },
    };

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static std::string_view exr_type_name_from_code(uint16_t code) noexcept
    {
        for (size_t i = 0; i < kExrTypeCodeMap.size(); ++i) {
            if (kExrTypeCodeMap[i].code == code) {
                return kExrTypeCodeMap[i].name;
            }
        }
        return {};
    }

    static bool exr_type_uses_raw_bytes(std::string_view type_name) noexcept
    {
        return type_name == "bytes" || type_name == "chlist"
               || type_name == "preview" || type_name == "stringvector";
    }

    static void append_u32le(std::vector<std::byte>* out, uint32_t v) noexcept
    {
        out->push_back(std::byte { static_cast<uint8_t>(v & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFFU) });
        out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFFU) });
    }

    static void append_i32le(std::vector<std::byte>* out, int32_t v) noexcept
    {
        append_u32le(out, static_cast<uint32_t>(v));
    }

    static void append_u64le(std::vector<std::byte>* out, uint64_t v) noexcept
    {
        append_u32le(out, static_cast<uint32_t>(v & 0xFFFFFFFFULL));
        append_u32le(out, static_cast<uint32_t>((v >> 32) & 0xFFFFFFFFULL));
    }

    static bool read_u64le(std::span<const std::byte> bytes, size_t* off,
                           uint64_t* out) noexcept
    {
        if (!off || !out || *off > bytes.size() || bytes.size() - *off < 8U) {
            return false;
        }
        const size_t i = *off;
        uint64_t v     = 0U;
        for (size_t b = 0; b < 8U; ++b) {
            v |= (static_cast<uint64_t>(
                      static_cast<uint8_t>(bytes[i + b]))
                  << (b * 8U));
        }
        *off += 8U;
        *out = v;
        return true;
    }

    static bool parse_prepared_exr_string_attribute_payload(
        std::span<const std::byte> bytes, std::string* out_name,
        std::vector<std::byte>* out_value) noexcept
    {
        if (!out_name || !out_value) {
            return false;
        }
        size_t off        = 0U;
        uint64_t name_len = 0U;
        if (!read_u64le(bytes, &off, &name_len)
            || name_len > bytes.size() - off) {
            return false;
        }
        const size_t name_size = static_cast<size_t>(name_len);
        out_name->assign(reinterpret_cast<const char*>(bytes.data() + off),
                         name_size);
        off += name_size;

        uint64_t value_len = 0U;
        if (!read_u64le(bytes, &off, &value_len)
            || value_len > bytes.size() - off) {
            return false;
        }
        const size_t value_size = static_cast<size_t>(value_len);
        out_value->assign(bytes.begin() + static_cast<std::ptrdiff_t>(off),
                          bytes.begin()
                              + static_cast<std::ptrdiff_t>(off + value_size));
        off += value_size;
        return off == bytes.size();
    }

    template<typename T>
    static bool value_array_bytes(const ByteArena& arena,
                                  const MetaValue& value,
                                  std::span<const std::byte>* out) noexcept
    {
        if (!out || value.kind != MetaValueKind::Array) {
            return false;
        }
        const std::span<const std::byte> raw = arena.span(value.data.span);
        if (raw.size() != value.count * sizeof(T)) {
            return false;
        }
        *out = raw;
        return true;
    }

    template<typename T>
    static bool append_host_array_i32(const ByteArena& arena,
                                      const MetaValue& value,
                                      std::vector<std::byte>* out) noexcept
    {
        std::span<const std::byte> raw;
        if (!out || !value_array_bytes<T>(arena, value, &raw)) {
            return false;
        }
        out->clear();
        out->reserve(raw.size());
        for (uint32_t i = 0; i < value.count; ++i) {
            T v {};
            std::memcpy(&v, raw.data() + (i * sizeof(T)), sizeof(T));
            append_i32le(out, static_cast<int32_t>(v));
        }
        return true;
    }

    template<typename T>
    static bool append_host_array_u32(const ByteArena& arena,
                                      const MetaValue& value,
                                      std::vector<std::byte>* out) noexcept
    {
        std::span<const std::byte> raw;
        if (!out || !value_array_bytes<T>(arena, value, &raw)) {
            return false;
        }
        out->clear();
        out->reserve(raw.size());
        for (uint32_t i = 0; i < value.count; ++i) {
            T v {};
            std::memcpy(&v, raw.data() + (i * sizeof(T)), sizeof(T));
            append_u32le(out, static_cast<uint32_t>(v));
        }
        return true;
    }

    template<typename T>
    static bool append_host_array_u64(const ByteArena& arena,
                                      const MetaValue& value,
                                      std::vector<std::byte>* out) noexcept
    {
        std::span<const std::byte> raw;
        if (!out || !value_array_bytes<T>(arena, value, &raw)) {
            return false;
        }
        out->clear();
        out->reserve(raw.size());
        for (uint32_t i = 0; i < value.count; ++i) {
            T v {};
            std::memcpy(&v, raw.data() + (i * sizeof(T)), sizeof(T));
            append_u64le(out, static_cast<uint64_t>(v));
        }
        return true;
    }

    static bool infer_scalar_type_name(const MetaValue& value,
                                       std::string_view* out_type) noexcept
    {
        if (!out_type) {
            return false;
        }
        if (value.kind == MetaValueKind::Text) {
            *out_type = "string";
            return true;
        }
        if (value.kind != MetaValueKind::Scalar) {
            return false;
        }
        switch (value.elem_type) {
        case MetaElementType::I32: *out_type = "int"; return true;
        case MetaElementType::F32: *out_type = "float"; return true;
        case MetaElementType::F64: *out_type = "double"; return true;
        case MetaElementType::SRational: *out_type = "rational"; return true;
        default: break;
        }
        return false;
    }

    static bool resolve_exr_type_name(const MetaStore& store,
                                      const Entry& entry,
                                      std::string_view* out_type,
                                      bool* out_is_opaque) noexcept
    {
        if (!out_type || !out_is_opaque) {
            return false;
        }
        *out_is_opaque = false;

        if (entry.origin.wire_type_name.size > 0U) {
            *out_type      = arena_string(store.arena(),
                                          entry.origin.wire_type_name);
            *out_is_opaque = true;
            return !out_type->empty();
        }

        if (entry.origin.wire_type.family == WireFamily::Other
            && entry.origin.wire_type.code != 0U) {
            *out_type = exr_type_name_from_code(entry.origin.wire_type.code);
            if (!out_type->empty()) {
                *out_is_opaque = (entry.origin.wire_type.code
                                  == kExrAttrOpaque);
                return true;
            }
        }

        return infer_scalar_type_name(entry.value, out_type);
    }

    static bool has_nul(std::span<const std::byte> bytes) noexcept
    {
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (bytes[i] == std::byte { 0 }) {
                return true;
            }
        }
        return false;
    }

    static bool encode_exr_attribute_value(const MetaStore& store,
                                           const Entry& entry,
                                           std::string_view type_name,
                                           bool is_opaque,
                                           std::vector<std::byte>* out) noexcept
    {
        if (!out) {
            return false;
        }

        const ByteArena& arena = store.arena();
        const MetaValue& value = entry.value;

        if ((is_opaque || exr_type_uses_raw_bytes(type_name))
            && value.kind == MetaValueKind::Bytes) {
            const std::span<const std::byte> raw = arena.span(value.data.span);
            out->assign(raw.begin(), raw.end());
            return true;
        }

        if (type_name == "string" && value.kind == MetaValueKind::Text) {
            const std::span<const std::byte> raw = arena.span(value.data.span);
            if (has_nul(raw)) {
                return false;
            }
            out->assign(raw.begin(), raw.end());
            return true;
        }

        if (type_name == "int" && value.kind == MetaValueKind::Scalar
            && value.elem_type == MetaElementType::I32) {
            out->clear();
            out->reserve(4U);
            append_i32le(out, static_cast<int32_t>(value.data.i64));
            return true;
        }

        if (type_name == "float" && value.kind == MetaValueKind::Scalar
            && value.elem_type == MetaElementType::F32) {
            out->clear();
            out->reserve(4U);
            append_u32le(out, value.data.f32_bits);
            return true;
        }

        if (type_name == "double" && value.kind == MetaValueKind::Scalar
            && value.elem_type == MetaElementType::F64) {
            out->clear();
            out->reserve(8U);
            append_u64le(out, value.data.f64_bits);
            return true;
        }

        if ((type_name == "compression" || type_name == "envmap"
             || type_name == "lineOrder" || type_name == "deepImageState")
            && value.kind == MetaValueKind::Scalar
            && value.elem_type == MetaElementType::U8) {
            out->assign(1U, std::byte { static_cast<uint8_t>(value.data.u64) });
            return true;
        }

        if (type_name == "rational" && value.kind == MetaValueKind::Scalar
            && value.elem_type == MetaElementType::SRational
            && value.data.sr.denom >= 0) {
            out->clear();
            out->reserve(8U);
            append_i32le(out, value.data.sr.numer);
            append_u32le(out, static_cast<uint32_t>(value.data.sr.denom));
            return true;
        }

        if (type_name == "floatvector"
            && value.elem_type == MetaElementType::F32) {
            return append_host_array_u32<uint32_t>(arena, value, out);
        }

        if (type_name == "box2i" && value.elem_type == MetaElementType::I32
            && value.count == 4U) {
            return append_host_array_i32<int32_t>(arena, value, out);
        }

        if ((type_name == "box2f" && value.count == 4U)
            || (type_name == "v2f" && value.count == 2U)
            || (type_name == "v3f" && value.count == 3U)
            || (type_name == "m33f" && value.count == 9U)
            || (type_name == "m44f" && value.count == 16U)
            || (type_name == "chromaticities" && value.count == 8U)) {
            if (value.elem_type != MetaElementType::F32) {
                return false;
            }
            return append_host_array_u32<uint32_t>(arena, value, out);
        }

        if ((type_name == "v2i" && value.count == 2U)
            || (type_name == "v3i" && value.count == 3U)
            || (type_name == "keycode" && value.count == 7U)) {
            if (value.elem_type != MetaElementType::I32) {
                return false;
            }
            return append_host_array_i32<int32_t>(arena, value, out);
        }

        if (type_name == "timecode" && value.count == 2U) {
            if (value.elem_type != MetaElementType::U32) {
                return false;
            }
            return append_host_array_u32<uint32_t>(arena, value, out);
        }

        if ((type_name == "v2d" && value.count == 2U)
            || (type_name == "v3d" && value.count == 3U)
            || (type_name == "m33d" && value.count == 9U)
            || (type_name == "m44d" && value.count == 16U)) {
            if (value.elem_type != MetaElementType::F64) {
                return false;
            }
            return append_host_array_u64<uint64_t>(arena, value, out);
        }

        if (type_name == "tiledesc" && value.kind == MetaValueKind::Array
            && value.elem_type == MetaElementType::U32 && value.count == 3U) {
            std::span<const std::byte> raw;
            if (!value_array_bytes<uint32_t>(arena, value, &raw)) {
                return false;
            }
            uint32_t v[3] = {};
            for (uint32_t i = 0; i < 3U; ++i) {
                std::memcpy(&v[i], raw.data() + (i * sizeof(uint32_t)),
                            sizeof(uint32_t));
            }
            if (v[2] > 255U) {
                return false;
            }
            out->clear();
            out->reserve(9U);
            append_u32le(out, v[0]);
            append_u32le(out, v[1]);
            out->push_back(std::byte { static_cast<uint8_t>(v[2]) });
            return true;
        }

        return false;
    }

    static bool exr_attribute_part_less(const ExrAdapterAttribute& a,
                                        const ExrAdapterAttribute& b) noexcept
    {
        return a.part_index < b.part_index;
    }

}  // namespace


ExrAdapterResult
build_exr_attribute_batch(const MetaStore& store, ExrAdapterBatch* out,
                          const ExrAdapterOptions& options) noexcept
{
    ExrAdapterResult result;
    if (!out) {
        result.status  = ExrAdapterStatus::InvalidArgument;
        result.errors  = 1U;
        result.message = "out is null";
        return result;
    }

    ExrAdapterBatch batch;
    batch.encoding_version = kExrCanonicalEncodingVersion;
    batch.options          = options;

    const std::span<const Entry> entries = store.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        if (entry.key.kind != MetaKeyKind::ExrAttribute) {
            continue;
        }

        std::string_view type_name;
        bool is_opaque = false;
        if (!resolve_exr_type_name(store, entry, &type_name, &is_opaque)) {
            result.failed_entry = static_cast<EntryId>(i);
            result.errors += 1U;
            result.message = "unable to resolve EXR type name";
            if (options.fail_on_unencodable) {
                result.status = ExrAdapterStatus::Unsupported;
                return result;
            }
            result.skipped += 1U;
            continue;
        }

        if (is_opaque && !options.include_opaque) {
            result.skipped += 1U;
            continue;
        }

        ExrAdapterAttribute attr;
        attr.part_index = entry.key.data.exr_attribute.part_index;
        attr.name.assign(
            arena_string(store.arena(), entry.key.data.exr_attribute.name));
        attr.type_name.assign(type_name.data(), type_name.size());
        attr.is_opaque = is_opaque;
        if (!encode_exr_attribute_value(store, entry, type_name, is_opaque,
                                        &attr.value)) {
            result.failed_entry = static_cast<EntryId>(i);
            result.errors += 1U;
            result.message = "unable to encode EXR attribute value";
            if (options.fail_on_unencodable) {
                result.status = ExrAdapterStatus::Unsupported;
                return result;
            }
            result.skipped += 1U;
            continue;
        }

        batch.attributes.push_back(std::move(attr));
        result.exported += 1U;
    }

    std::stable_sort(batch.attributes.begin(), batch.attributes.end(),
                     exr_attribute_part_less);

    *out          = std::move(batch);
    result.status = ExrAdapterStatus::Ok;
    return result;
}

ExrAdapterResult
build_prepared_exr_attribute_batch(const PreparedTransferBundle& bundle,
                                   ExrAdapterBatch* out,
                                   const ExrAdapterOptions& options) noexcept
{
    ExrAdapterResult result;
    if (!out) {
        result.status  = ExrAdapterStatus::InvalidArgument;
        result.errors  = 1U;
        result.message = "out batch is null";
        return result;
    }

    out->encoding_version = kExrCanonicalEncodingVersion;
    out->options          = options;
    out->attributes.clear();

    if (bundle.target_format != TransferTargetFormat::Exr) {
        result.status  = ExrAdapterStatus::Unsupported;
        result.errors  = 1U;
        result.message = "prepared bundle target format is not exr";
        return result;
    }

    out->attributes.reserve(bundle.blocks.size());
    for (size_t i = 0; i < bundle.blocks.size(); ++i) {
        const PreparedTransferBlock& block = bundle.blocks[i];
        if (block.kind != TransferBlockKind::ExrAttribute
            || block.route != "exr:attribute-string") {
            result.status  = ExrAdapterStatus::Unsupported;
            result.errors  = 1U;
            result.message = "prepared bundle contains unsupported exr block";
            return result;
        }

        std::string name;
        std::vector<std::byte> value;
        if (!parse_prepared_exr_string_attribute_payload(
                std::span<const std::byte>(block.payload.data(),
                                           block.payload.size()),
                &name, &value)) {
            result.status  = ExrAdapterStatus::Unsupported;
            result.errors  = 1U;
            result.message = "prepared exr attribute payload is malformed";
            return result;
        }

        ExrAdapterAttribute attr;
        attr.part_index = 0U;
        attr.name       = std::move(name);
        attr.type_name  = "string";
        attr.value      = std::move(value);
        attr.is_opaque  = false;
        out->attributes.push_back(std::move(attr));
    }

    result.status   = ExrAdapterStatus::Ok;
    result.exported = static_cast<uint32_t>(out->attributes.size());
    return result;
}


BuildExrAttributeBatchFileResult
build_exr_attribute_batch_from_file(
    const char* path, ExrAdapterBatch* out,
    const BuildExrAttributeBatchFileOptions& options) noexcept
{
    BuildExrAttributeBatchFileResult result;

    PrepareTransferFileOptions prepare_options = options.prepare;
    prepare_options.prepare.target_format      = TransferTargetFormat::Exr;
    result.prepared = prepare_metadata_for_target_file(path, prepare_options);
    if (result.prepared.file_status != TransferFileStatus::Ok
        || result.prepared.prepare.status != TransferStatus::Ok) {
        if (result.prepared.prepare.status == TransferStatus::InvalidArgument) {
            result.adapter.status = ExrAdapterStatus::InvalidArgument;
        } else {
            result.adapter.status = ExrAdapterStatus::Unsupported;
        }
        result.adapter.errors = 1U;
        result.adapter.message = result.prepared.prepare.message;
        return result;
    }

    result.adapter = build_prepared_exr_attribute_batch(result.prepared.bundle,
                                                        out,
                                                        options.adapter);
    return result;
}


ExrAdapterStatus
build_exr_attribute_part_spans(const ExrAdapterBatch& batch,
                               std::vector<ExrAdapterPartSpan>* out) noexcept
{
    if (!out) {
        return ExrAdapterStatus::InvalidArgument;
    }

    out->clear();
    const std::span<const ExrAdapterAttribute> attrs(batch.attributes.data(),
                                                     batch.attributes.size());
    if (attrs.empty()) {
        return ExrAdapterStatus::Ok;
    }

    out->reserve(attrs.size());
    uint32_t first = 0U;
    while (first < attrs.size()) {
        const uint32_t part_index = attrs[first].part_index;
        uint32_t last             = first + 1U;
        while (last < attrs.size()) {
            if (attrs[last].part_index < part_index) {
                out->clear();
                return ExrAdapterStatus::Unsupported;
            }
            if (attrs[last].part_index != part_index) {
                break;
            }
            last += 1U;
        }
        ExrAdapterPartSpan span;
        span.part_index      = part_index;
        span.first_attribute = first;
        span.attribute_count = last - first;
        out->push_back(span);
        first = last;
    }

    return ExrAdapterStatus::Ok;
}


ExrAdapterStatus
build_exr_attribute_part_views(const ExrAdapterBatch& batch,
                               std::vector<ExrAdapterPartView>* out) noexcept
{
    if (!out) {
        return ExrAdapterStatus::InvalidArgument;
    }

    std::vector<ExrAdapterPartSpan> spans;
    const ExrAdapterStatus status = build_exr_attribute_part_spans(batch,
                                                                   &spans);
    if (status != ExrAdapterStatus::Ok) {
        out->clear();
        return status;
    }

    out->clear();
    out->reserve(spans.size());
    const std::span<const ExrAdapterAttribute> attrs(batch.attributes.data(),
                                                     batch.attributes.size());
    for (size_t i = 0; i < spans.size(); ++i) {
        ExrAdapterPartView view;
        view.part_index = spans[i].part_index;
        view.attributes = attrs.subspan(spans[i].first_attribute,
                                        spans[i].attribute_count);
        out->push_back(view);
    }
    return ExrAdapterStatus::Ok;
}


ExrAdapterReplayResult
replay_exr_attribute_batch(const ExrAdapterBatch& batch,
                           const ExrAdapterReplayCallbacks& callbacks) noexcept
{
    ExrAdapterReplayResult result;
    if (!callbacks.emit_attribute) {
        result.status  = ExrAdapterStatus::InvalidArgument;
        result.message = "emit_attribute callback is null";
        return result;
    }

    std::vector<ExrAdapterPartSpan> spans;
    const ExrAdapterStatus span_status = build_exr_attribute_part_spans(batch,
                                                                        &spans);
    if (span_status != ExrAdapterStatus::Ok) {
        result.status  = span_status;
        result.message = "attribute batch is not grouped by part";
        return result;
    }

    const std::span<const ExrAdapterAttribute> attrs(batch.attributes.data(),
                                                     batch.attributes.size());
    for (size_t i = 0; i < spans.size(); ++i) {
        const ExrAdapterPartSpan& span = spans[i];
        if (callbacks.begin_part) {
            const ExrAdapterStatus status
                = callbacks.begin_part(callbacks.user, span.part_index,
                                       span.attribute_count);
            if (status != ExrAdapterStatus::Ok) {
                result.status            = status;
                result.failed_part_index = span.part_index;
                result.message           = "begin_part callback failed";
                return result;
            }
        }

        for (uint32_t j = 0; j < span.attribute_count; ++j) {
            const uint32_t attr_index = span.first_attribute + j;
            const ExrAdapterStatus status
                = callbacks.emit_attribute(callbacks.user, span.part_index,
                                           &attrs[attr_index]);
            if (status != ExrAdapterStatus::Ok) {
                result.status                 = status;
                result.failed_part_index      = span.part_index;
                result.failed_attribute_index = attr_index;
                result.message = "emit_attribute callback failed";
                return result;
            }
            result.replayed_attributes += 1U;
        }

        if (callbacks.end_part) {
            const ExrAdapterStatus status = callbacks.end_part(callbacks.user,
                                                               span.part_index);
            if (status != ExrAdapterStatus::Ok) {
                result.status            = status;
                result.failed_part_index = span.part_index;
                result.message           = "end_part callback failed";
                return result;
            }
        }

        result.replayed_parts += 1U;
    }

    result.status = ExrAdapterStatus::Ok;
    return result;
}

}  // namespace openmeta
