#include "openmeta/oiio_adapter.h"

#include "interop_value_format_internal.h"

namespace openmeta {
namespace {

    static bool
    looks_like_numeric_unknown_name(std::string_view name) noexcept
    {
        for (size_t i = 0; i + 2 < name.size(); ++i) {
            if (name[i] == '_' && name[i + 1] == '0'
                && (name[i + 2] == 'x' || name[i + 2] == 'X')) {
                return true;
            }
        }
        return false;
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

}  // namespace openmeta
