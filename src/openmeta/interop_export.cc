#include "openmeta/interop_export.h"

#include "openmeta/exif_tag_names.h"

#include <array>
#include <cstdio>

namespace openmeta {
namespace {

    static constexpr std::string_view kXmpNsXmp = "http://ns.adobe.com/xap/1.0/";
    static constexpr std::string_view kXmpNsTiff
        = "http://ns.adobe.com/tiff/1.0/";
    static constexpr std::string_view kXmpNsExif
        = "http://ns.adobe.com/exif/1.0/";
    static constexpr std::string_view kXmpNsDc
        = "http://purl.org/dc/elements/1.1/";


    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }


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


    static bool exif_tag_is_pointer(uint16_t tag) noexcept
    {
        static constexpr std::array<uint16_t, 4> kPointerTags = {
            0x8769,  // ExifIFDPointer
            0x8825,  // GPSInfoIFDPointer
            0xA005,  // InteropIFDPointer
            0x014A,  // SubIFDs
        };
        for (size_t i = 0; i < kPointerTags.size(); ++i) {
            if (kPointerTags[i] == tag) {
                return true;
            }
        }
        return false;
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


    static bool ifd_to_oiio_prefix(std::string_view ifd,
                                   std::string_view* out_prefix) noexcept
    {
        if (!out_prefix) {
            return false;
        }
        *out_prefix = {};

        if (ifd.empty()) {
            return false;
        }
        if (is_makernote_ifd(ifd)) {
            *out_prefix = "MakerNote";
            return true;
        }
        if (ifd == "exififd" || ifd.ends_with("_exififd") || ifd == "interopifd"
            || ifd.ends_with("_interopifd")) {
            *out_prefix = "Exif";
            return true;
        }
        if (ifd == "gpsifd" || ifd.ends_with("_gpsifd")) {
            *out_prefix = "GPS";
            return true;
        }
        if (ifd.starts_with("ifd") || ifd.starts_with("subifd")
            || ifd.starts_with("mkifd") || ifd.starts_with("mk_subifd")) {
            *out_prefix = {};
            return true;
        }
        return false;
    }


    static void append_u16_hex(uint16_t tag, std::string* out) noexcept
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%04X", static_cast<unsigned>(tag));
        out->append(buf);
    }


    static void append_u32_hex(uint32_t v, std::string* out) noexcept
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08X", static_cast<unsigned>(v));
        out->append(buf);
    }


    static void append_u64_dec(uint64_t v, std::string* out) noexcept
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%llu",
                      static_cast<unsigned long long>(v));
        out->append(buf);
    }


    static bool build_canonical_name(const ByteArena& arena, const Entry& e,
                                     std::string* out_name) noexcept
    {
        out_name->clear();
        switch (e.key.kind) {
        case MetaKeyKind::ExifTag: {
            out_name->append("exif:");
            out_name->append(arena_string(arena, e.key.data.exif_tag.ifd));
            out_name->append(":");
            append_u16_hex(e.key.data.exif_tag.tag, out_name);
            return true;
        }
        case MetaKeyKind::ExrAttribute: {
            out_name->append("exr:part:");
            append_u64_dec(e.key.data.exr_attribute.part_index, out_name);
            out_name->append(":");
            out_name->append(
                arena_string(arena, e.key.data.exr_attribute.name));
            return true;
        }
        case MetaKeyKind::IptcDataset: {
            out_name->append("iptc:");
            append_u64_dec(e.key.data.iptc_dataset.record, out_name);
            out_name->append(":");
            append_u64_dec(e.key.data.iptc_dataset.dataset, out_name);
            return true;
        }
        case MetaKeyKind::XmpProperty: {
            out_name->append("xmp:");
            out_name->append(
                arena_string(arena, e.key.data.xmp_property.schema_ns));
            out_name->append(":");
            out_name->append(
                arena_string(arena, e.key.data.xmp_property.property_path));
            return true;
        }
        case MetaKeyKind::IccHeaderField: {
            out_name->append("icc:header:");
            append_u64_dec(e.key.data.icc_header_field.offset, out_name);
            return true;
        }
        case MetaKeyKind::IccTag: {
            out_name->append("icc:tag:");
            append_u32_hex(e.key.data.icc_tag.signature, out_name);
            return true;
        }
        case MetaKeyKind::PhotoshopIrb: {
            out_name->append("psirb:");
            append_u16_hex(e.key.data.photoshop_irb.resource_id, out_name);
            return true;
        }
        case MetaKeyKind::GeotiffKey: {
            out_name->append("geotiff:");
            append_u64_dec(e.key.data.geotiff_key.key_id, out_name);
            return true;
        }
        case MetaKeyKind::PrintImField: {
            out_name->append("printim:");
            out_name->append(
                arena_string(arena, e.key.data.printim_field.field));
            return true;
        }
        case MetaKeyKind::BmffField: {
            out_name->append("bmff:");
            out_name->append(arena_string(arena, e.key.data.bmff_field.field));
            return true;
        }
        case MetaKeyKind::JumbfField: {
            out_name->append("jumbf:");
            out_name->append(arena_string(arena, e.key.data.jumbf_field.field));
            return true;
        }
        case MetaKeyKind::JumbfCborKey: {
            out_name->append("jumbf_cbor:");
            out_name->append(
                arena_string(arena, e.key.data.jumbf_cbor_key.key));
            return true;
        }
        }
        return false;
    }


    static bool build_xmp_portable_name(const ByteArena& arena, const Entry& e,
                                        std::string* out_name) noexcept
    {
        out_name->clear();
        if (e.key.kind == MetaKeyKind::ExifTag) {
            const std::string_view ifd = arena_string(arena,
                                                      e.key.data.exif_tag.ifd);
            std::string_view prefix;
            if (!ifd_to_portable_prefix(ifd, &prefix)) {
                return false;
            }
            if (exif_tag_is_pointer(e.key.data.exif_tag.tag)) {
                return false;
            }
            const std::string_view tag_name
                = exif_tag_name(ifd, e.key.data.exif_tag.tag);
            if (tag_name.empty()) {
                return false;
            }
            out_name->append(prefix);
            out_name->append(":");
            out_name->append(tag_name);
            return true;
        }

        if (e.key.kind == MetaKeyKind::XmpProperty) {
            const std::string_view ns
                = arena_string(arena, e.key.data.xmp_property.schema_ns);
            const std::string_view prop
                = arena_string(arena, e.key.data.xmp_property.property_path);
            if (!is_simple_xmp_property_name(prop)) {
                return false;
            }

            std::string_view prefix;
            if (ns == kXmpNsXmp) {
                prefix = "xmp";
            } else if (ns == kXmpNsTiff) {
                prefix = "tiff";
            } else if (ns == kXmpNsExif) {
                prefix = "exif";
            } else if (ns == kXmpNsDc) {
                prefix = "dc";
            } else {
                return false;
            }

            out_name->append(prefix);
            out_name->append(":");
            out_name->append(prop);
            return true;
        }

        return false;
    }


    static bool build_oiio_name(const ByteArena& arena, const Entry& e,
                                bool include_makernotes,
                                std::string* out_name) noexcept
    {
        out_name->clear();

        if (e.key.kind == MetaKeyKind::ExifTag) {
            const std::string_view ifd = arena_string(arena,
                                                      e.key.data.exif_tag.ifd);
            const bool is_mk_ifd       = is_makernote_ifd(ifd);
            if (is_mk_ifd && !include_makernotes) {
                return false;
            }

            std::string_view prefix;
            if (!ifd_to_oiio_prefix(ifd, &prefix)) {
                return false;
            }

            if (!is_mk_ifd && exif_tag_is_pointer(e.key.data.exif_tag.tag)) {
                return false;
            }

            const std::string_view tag_name
                = exif_tag_name(ifd, e.key.data.exif_tag.tag);

            if (is_mk_ifd) {
                out_name->append("MakerNote:");
                out_name->append(ifd);
                out_name->append(":");
                if (!tag_name.empty()) {
                    out_name->append(tag_name);
                } else {
                    append_u16_hex(e.key.data.exif_tag.tag, out_name);
                }
                return true;
            }

            if (!prefix.empty()) {
                out_name->append(prefix);
                out_name->append(":");
            }

            if (!tag_name.empty()) {
                out_name->append(tag_name);
            } else {
                out_name->append("Tag_");
                append_u16_hex(e.key.data.exif_tag.tag, out_name);
            }
            return true;
        }

        if (e.key.kind == MetaKeyKind::XmpProperty) {
            const std::string_view ns
                = arena_string(arena, e.key.data.xmp_property.schema_ns);
            const std::string_view prop
                = arena_string(arena, e.key.data.xmp_property.property_path);
            if (!is_simple_xmp_property_name(prop)) {
                return false;
            }

            std::string_view prefix;
            if (ns == kXmpNsXmp) {
                prefix = "XMP";
            } else if (ns == kXmpNsTiff) {
                prefix = "TIFF";
            } else if (ns == kXmpNsExif) {
                prefix = "Exif";
            } else if (ns == kXmpNsDc) {
                prefix = "DC";
            } else {
                return false;
            }

            out_name->append(prefix);
            out_name->append(":");
            out_name->append(prop);
            return true;
        }

        if (e.key.kind == MetaKeyKind::ExrAttribute) {
            const std::string_view attr_name
                = arena_string(arena, e.key.data.exr_attribute.name);
            if (e.key.data.exr_attribute.part_index == 0U) {
                out_name->append("openexr:");
                out_name->append(attr_name);
            } else {
                out_name->append("openexr:part:");
                append_u64_dec(e.key.data.exr_attribute.part_index, out_name);
                out_name->append(":");
                out_name->append(attr_name);
            }
            return true;
        }

        return build_canonical_name(arena, e, out_name);
    }

}  // namespace


void
visit_metadata(const MetaStore& store, const ExportOptions& options,
               MetadataSink& sink) noexcept
{
    const ByteArena& arena          = store.arena();
    const std::span<const Entry> es = store.entries();

    std::string name;
    name.reserve(128);

    for (size_t i = 0; i < es.size(); ++i) {
        const Entry& e = es[i];
        if (any(e.flags, EntryFlags::Deleted)) {
            continue;
        }

        if (!options.include_makernotes && e.key.kind == MetaKeyKind::ExifTag) {
            const std::string_view ifd = arena_string(arena,
                                                      e.key.data.exif_tag.ifd);
            if (is_makernote_ifd(ifd)) {
                continue;
            }
        }

        bool mapped = false;
        switch (options.style) {
        case ExportNameStyle::Canonical:
            mapped = build_canonical_name(arena, e, &name);
            break;
        case ExportNameStyle::XmpPortable:
            mapped = build_xmp_portable_name(arena, e, &name);
            break;
        case ExportNameStyle::Oiio:
            mapped = build_oiio_name(arena, e, options.include_makernotes,
                                     &name);
            break;
        }

        if (!mapped || name.empty()) {
            continue;
        }

        ExportItem item;
        item.name  = std::string_view(name.data(), name.size());
        item.entry = &e;
        if (options.include_origin) {
            item.origin = &e.origin;
        }
        if (options.include_flags) {
            item.flags = e.flags;
        }
        sink.on_item(item);
    }
}

}  // namespace openmeta
