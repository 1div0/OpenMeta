// SPDX-License-Identifier: Apache-2.0

#include "openmeta/ocio_adapter.h"

#include "openmeta/ccm_query.h"

#include "interop_safety_internal.h"
#include "interop_value_format_internal.h"

#include <cstdio>

namespace openmeta {
namespace {

    using interop_internal::decode_text_to_utf8_safe;
    using interop_internal::SafeTextStatus;
    using interop_internal::set_safety_error;

    static size_t find_child_node(const std::vector<OcioMetadataNode>& nodes,
                                  std::string_view name) noexcept
    {
        for (size_t i = 0; i < nodes.size(); ++i) {
            if (nodes[i].name == name) {
                return i;
            }
        }
        return static_cast<size_t>(-1);
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

    static void append_double_text(double v, std::string* out) noexcept
    {
        if (!out) {
            return;
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.9g", v);
        out->append(buf);
    }

    static void format_ccm_field_value(const CcmField& field,
                                       uint32_t max_value_bytes,
                                       std::string* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        if (field.values.empty()) {
            return;
        }
        if (field.rows > 1U && field.cols > 0U) {
            char dims[32];
            std::snprintf(dims, sizeof(dims), "%ux%u ",
                          static_cast<unsigned>(field.rows),
                          static_cast<unsigned>(field.cols));
            out->append(dims);
        }
        out->push_back('[');
        for (size_t i = 0; i < field.values.size(); ++i) {
            if (i != 0U) {
                out->append(", ");
            }
            append_double_text(field.values[i], out);
        }
        out->push_back(']');
        truncate_utf8_for_limit(out, max_value_bytes);
    }

    static void append_normalized_ccm_tree(const MetaStore& store,
                                           OcioMetadataNode* root,
                                           uint32_t max_value_bytes) noexcept
    {
        if (!root) {
            return;
        }
        CcmQueryOptions q;
        if (max_value_bytes != 0U) {
            const uint32_t max_values     = max_value_bytes / 8U;
            q.limits.max_values_per_field = (max_values == 0U) ? 1U
                                                               : max_values;
        }
        std::vector<CcmField> fields;
        (void)collect_dng_ccm_fields(store, &fields, q);
        if (fields.empty()) {
            return;
        }

        size_t ns_index = find_child_node(root->children, "dngnorm");
        if (ns_index == static_cast<size_t>(-1)) {
            OcioMetadataNode ns;
            ns.name = "dngnorm";
            root->children.push_back(std::move(ns));
            ns_index = root->children.size() - 1U;
        }

        for (size_t i = 0; i < fields.size(); ++i) {
            const CcmField& f = fields[i];
            if (f.values.empty() || f.name.empty()) {
                continue;
            }
            OcioMetadataNode leaf;
            if (!f.ifd.empty()) {
                leaf.name.append(f.ifd);
                leaf.name.push_back('.');
            }
            leaf.name.append(f.name);
            format_ccm_field_value(f, max_value_bytes, &leaf.value);
            root->children[ns_index].children.push_back(std::move(leaf));
        }
    }


    class OcioTreeSink final : public MetadataSink {
    public:
        OcioTreeSink(const ByteArena& arena, OcioMetadataNode* root,
                     uint32_t max_value_bytes, bool include_empty) noexcept
            : arena_(arena)
            , root_(root)
            , max_value_bytes_(max_value_bytes)
            , include_empty_(include_empty)
        {
        }

        void on_item(const ExportItem& item) noexcept override
        {
            if (!root_ || !item.entry) {
                return;
            }

            std::string value_text;
            const bool has_value = interop_internal::format_value_for_text(
                arena_, item.entry->value, max_value_bytes_, &value_text);
            if (!has_value && !include_empty_) {
                return;
            }

            const std::string_view full_name = item.name;
            const size_t sep                 = full_name.find(':');
            if (sep == std::string_view::npos || sep == 0U
                || (sep + 1U) >= full_name.size()) {
                OcioMetadataNode leaf;
                leaf.name.assign(full_name.data(), full_name.size());
                leaf.value = std::move(value_text);
                root_->children.push_back(std::move(leaf));
                return;
            }

            const std::string_view ns_name(full_name.data(), sep);
            const std::string_view leaf_name(full_name.data() + sep + 1U,
                                             full_name.size() - sep - 1U);

            size_t ns_index = find_child_node(root_->children, ns_name);
            if (ns_index == static_cast<size_t>(-1)) {
                OcioMetadataNode ns;
                ns.name.assign(ns_name.data(), ns_name.size());
                root_->children.push_back(std::move(ns));
                ns_index = root_->children.size() - 1U;
            }

            OcioMetadataNode leaf;
            leaf.name.assign(leaf_name.data(), leaf_name.size());
            leaf.value = std::move(value_text);
            root_->children[ns_index].children.push_back(std::move(leaf));
        }

    private:
        const ByteArena& arena_;
        OcioMetadataNode* root_;
        uint32_t max_value_bytes_;
        bool include_empty_;
    };

    class OcioTreeSafeSink final : public MetadataSink {
    public:
        OcioTreeSafeSink(const ByteArena& arena, OcioMetadataNode* root,
                         uint32_t max_value_bytes, bool include_empty,
                         InteropSafetyError* error) noexcept
            : arena_(arena)
            , root_(root)
            , max_value_bytes_(max_value_bytes)
            , include_empty_(include_empty)
            , error_(error)
        {
        }

        void on_item(const ExportItem& item) noexcept override
        {
            if (status_ != InteropSafetyStatus::Ok || !root_ || !item.entry) {
                return;
            }

            std::string value_text;
            bool has_value         = false;
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
                                 "unsafe bytes value in OCIO metadata");
                status_ = InteropSafetyStatus::UnsafeData;
                return;
            } else {
                has_value = interop_internal::format_value_for_text(
                    arena_, value, max_value_bytes_, &value_text);
            }

            if (!has_value && !include_empty_) {
                return;
            }

            const std::string_view full_name = item.name;
            const size_t sep                 = full_name.find(':');
            if (sep == std::string_view::npos || sep == 0U
                || (sep + 1U) >= full_name.size()) {
                OcioMetadataNode leaf;
                leaf.name.assign(full_name.data(), full_name.size());
                leaf.value = std::move(value_text);
                root_->children.push_back(std::move(leaf));
                return;
            }

            const std::string_view ns_name(full_name.data(), sep);
            const std::string_view leaf_name(full_name.data() + sep + 1U,
                                             full_name.size() - sep - 1U);

            size_t ns_index = find_child_node(root_->children, ns_name);
            if (ns_index == static_cast<size_t>(-1)) {
                OcioMetadataNode ns;
                ns.name.assign(ns_name.data(), ns_name.size());
                root_->children.push_back(std::move(ns));
                ns_index = root_->children.size() - 1U;
            }

            OcioMetadataNode leaf;
            leaf.name.assign(leaf_name.data(), leaf_name.size());
            leaf.value = std::move(value_text);
            root_->children[ns_index].children.push_back(std::move(leaf));
        }

        InteropSafetyStatus status() const noexcept { return status_; }

    private:
        const ByteArena& arena_;
        OcioMetadataNode* root_;
        uint32_t max_value_bytes_;
        bool include_empty_;
        InteropSafetyError* error_  = nullptr;
        InteropSafetyStatus status_ = InteropSafetyStatus::Ok;
    };

}  // namespace


void
build_ocio_metadata_tree(const MetaStore& store, OcioMetadataNode* root,
                         const OcioAdapterOptions& options) noexcept
{
    if (!root) {
        return;
    }

    root->name     = "OpenMeta";
    root->value    = {};
    root->children = {};

    OcioTreeSink sink(store.arena(), root, options.max_value_bytes,
                      options.include_empty);
    visit_metadata(store, options.export_options, sink);
    if (options.include_normalized_ccm) {
        append_normalized_ccm_tree(store, root, options.max_value_bytes);
    }
}


InteropSafetyStatus
build_ocio_metadata_tree_safe(const MetaStore& store, OcioMetadataNode* root,
                              const OcioAdapterOptions& options,
                              InteropSafetyError* error) noexcept
{
    if (error) {
        *error = InteropSafetyError {};
    }
    if (!root) {
        set_safety_error(error, InteropSafetyReason::InternalMismatch, {}, {},
                         "null root node");
        return InteropSafetyStatus::InvalidArgument;
    }

    root->name     = "OpenMeta";
    root->value    = {};
    root->children = {};

    OcioTreeSafeSink sink(store.arena(), root, options.max_value_bytes,
                          options.include_empty, error);
    visit_metadata(store, options.export_options, sink);
    if (sink.status() == InteropSafetyStatus::Ok
        && options.include_normalized_ccm) {
        append_normalized_ccm_tree(store, root, options.max_value_bytes);
    }
    return sink.status();
}


OcioAdapterOptions
make_ocio_adapter_options(const OcioAdapterRequest& request) noexcept
{
    OcioAdapterOptions options;
    options.max_value_bytes                   = request.max_value_bytes;
    options.include_empty                     = request.include_empty;
    options.export_options.style              = request.style;
    options.export_options.name_policy        = request.name_policy;
    options.export_options.include_makernotes = request.include_makernotes;
    options.export_options.include_origin     = request.include_origin;
    options.export_options.include_flags      = request.include_flags;
    options.include_normalized_ccm            = request.include_normalized_ccm;
    return options;
}


void
build_ocio_metadata_tree(const MetaStore& store, OcioMetadataNode* root,
                         const OcioAdapterRequest& request) noexcept
{
    const OcioAdapterOptions options = make_ocio_adapter_options(request);
    build_ocio_metadata_tree(store, root, options);
}


InteropSafetyStatus
build_ocio_metadata_tree_safe(const MetaStore& store, OcioMetadataNode* root,
                              const OcioAdapterRequest& request,
                              InteropSafetyError* error) noexcept
{
    const OcioAdapterOptions options = make_ocio_adapter_options(request);
    return build_ocio_metadata_tree_safe(store, root, options, error);
}

}  // namespace openmeta
