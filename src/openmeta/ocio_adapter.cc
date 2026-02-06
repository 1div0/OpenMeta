#include "openmeta/ocio_adapter.h"

#include "interop_value_format_internal.h"

namespace openmeta {
namespace {

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

        void on_item(const ExportItem& item) override
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
}

}  // namespace openmeta
