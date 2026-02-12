#include "openmeta/interop_export.h"

#include "openmeta/exif_tag_names.h"

#include <array>
#include <cstdio>
#include <string>
#include <vector>

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

    static bool is_classic_tiff_ifd_token(std::string_view ifd) noexcept
    {
        return ifd.starts_with("ifd") || ifd.starts_with("subifd")
               || ifd.starts_with("mkifd") || ifd.starts_with("mk_subifd");
    }

    struct IfdPolicyHints final {
        std::string_view ifd;
        bool has_tag_zero              = false;
        bool has_panasonic_raw_version = false;
        bool has_dng_version           = false;
    };

    class ExifExportContext final {
    public:
        void scan(const ByteArena& arena, std::span<const Entry> entries) noexcept
        {
            hints_.clear();
            hints_.reserve(16);

            for (size_t i = 0; i < entries.size(); ++i) {
                const Entry& e = entries[i];
                if (e.key.kind != MetaKeyKind::ExifTag) {
                    continue;
                }
                if (any(e.flags, EntryFlags::Deleted)) {
                    continue;
                }

                const std::string_view ifd = arena_string(arena,
                                                          e.key.data.exif_tag.ifd);
                if (ifd.empty() || is_makernote_ifd(ifd)) {
                    continue;
                }
                const size_t idx = ensure_ifd(ifd);
                if (idx >= hints_.size()) {
                    continue;
                }
                if (e.key.data.exif_tag.tag == 0x0000U) {
                    hints_[idx].has_tag_zero = true;
                }
                if (e.key.data.exif_tag.tag == 0x0001U
                    && exif_tag_name(ifd, 0x0001U) == "PanasonicRawVersion") {
                    hints_[idx].has_panasonic_raw_version = true;
                }
                if (e.key.data.exif_tag.tag == 0xC612U) {
                    hints_[idx].has_dng_version = true;
                }
                if (!make_sony_ && ifd == "ifd0"
                    && e.key.data.exif_tag.tag == 0x010FU
                    && e.value.kind == MetaValueKind::Text
                    && e.value.count != 0U) {
                    const std::string_view make
                        = arena_string(arena, e.value.data.span);
                    if (make.size() >= 4
                        && (make[0] == 'S' || make[0] == 's')
                        && (make[1] == 'O' || make[1] == 'o')
                        && (make[2] == 'N' || make[2] == 'n')
                        && (make[3] == 'Y' || make[3] == 'y')) {
                        make_sony_ = true;
                    }
                }
            }
        }

        bool ifd_has_tag_zero(std::string_view ifd) const noexcept
        {
            const IfdPolicyHints* h = lookup(ifd);
            return h && h->has_tag_zero;
        }

        bool ifd_is_panasonic_raw(std::string_view ifd) const noexcept
        {
            const IfdPolicyHints* h = lookup(ifd);
            return h && h->has_panasonic_raw_version;
        }

        bool ifd_has_dng_version(std::string_view ifd) const noexcept
        {
            const IfdPolicyHints* h = lookup(ifd);
            return h && h->has_dng_version;
        }

        bool make_is_sony() const noexcept
        {
            return make_sony_;
        }

    private:
        size_t ensure_ifd(std::string_view ifd) noexcept
        {
            for (size_t i = 0; i < hints_.size(); ++i) {
                if (hints_[i].ifd == ifd) {
                    return i;
                }
            }
            hints_.push_back(IfdPolicyHints {});
            hints_.back().ifd = ifd;
            return hints_.size() - 1;
        }

        const IfdPolicyHints* lookup(std::string_view ifd) const noexcept
        {
            for (size_t i = 0; i < hints_.size(); ++i) {
                if (hints_[i].ifd == ifd) {
                    return &hints_[i];
                }
            }
            return nullptr;
        }

        std::vector<IfdPolicyHints> hints_;
        bool make_sony_ = false;
    };

    static int parse_index_with_prefix(std::string_view token,
                                       std::string_view prefix) noexcept
    {
        if (!token.starts_with(prefix)) {
            return -1;
        }
        if (token.size() <= prefix.size()) {
            return -1;
        }
        int value = 0;
        for (size_t i = prefix.size(); i < token.size(); ++i) {
            const char c = token[i];
            if (c < '0' || c > '9') {
                return -1;
            }
            value = (value * 10) + static_cast<int>(c - '0');
            if (value > 1000) {
                return -1;
            }
        }
        return value;
    }

    static bool is_preview_ifd_token(std::string_view ifd) noexcept
    {
        if (parse_index_with_prefix(ifd, "ifd") >= 1) {
            return true;
        }
        if (parse_index_with_prefix(ifd, "subifd") >= 0) {
            return true;
        }
        return false;
    }

    static bool
    alias_prefers_exif_prefix_for_tiff_tag(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x829A:  // ExposureTime
        case 0x829D:  // FNumber
        case 0x8822:  // ExposureProgram
        case 0x8827:  // ISOSpeedRatings
        case 0x9000:  // ExifVersion
        case 0x9004:  // DateTimeDigitized / CreateDate alias
        case 0x9101:  // ComponentsConfiguration
        case 0x9202:  // ApertureValue
        case 0x9204:  // ExposureBiasValue
        case 0x9209:  // Flash
        case 0x920A:  // FocalLength
        case 0xA000:  // FlashpixVersion
        case 0xA001:  // ColorSpace
        case 0xA002:  // PixelXDimension
        case 0xA003:  // PixelYDimension
        case 0xA20E:  // FocalPlaneXResolution
        case 0xA20F:  // FocalPlaneYResolution
        case 0xA210:  // FocalPlaneResolutionUnit
        case 0xA401:  // CustomRendered
        case 0xA402:  // ExposureMode
        case 0xA404:  // DigitalZoomRatio
        case 0xA430:  // CameraOwnerName / OwnerName alias
        case 0xA431:  // BodySerialNumber / SerialNumber alias
        case 0xA432:  // LensSpecification / LensInfo alias
        case 0xA405:  // FocalLengthIn35mmFilm
            return true;
        default: return false;
        }
    }

    static bool
    cr3_tiff_ifd_prefers_exif_prefix(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x829A:  // ExposureTime
        case 0x829D:  // FNumber
        case 0x8822:  // ExposureProgram
        case 0x8827:  // ISOSpeedRatings
        case 0x8830:  // SensitivityType
        case 0x8832:  // RecommendedExposureIndex
        case 0x9000:  // ExifVersion
        case 0x9003:  // DateTimeOriginal
        case 0x9004:  // DateTimeDigitized
        case 0x9010:  // OffsetTime
        case 0x9011:  // OffsetTimeOriginal
        case 0x9012:  // OffsetTimeDigitized
        case 0x9101:  // ComponentsConfiguration
        case 0x9201:  // ShutterSpeedValue
        case 0x9202:  // ApertureValue
        case 0x9204:  // ExposureBiasValue
        case 0x9207:  // MeteringMode
        case 0x9209:  // Flash
        case 0x920A:  // FocalLength
        case 0x9290:  // SubSecTime
        case 0x9291:  // SubSecTimeOriginal
        case 0x9292:  // SubSecTimeDigitized
        case 0xA000:  // FlashpixVersion
        case 0xA001:  // ColorSpace
        case 0xA002:  // PixelXDimension
        case 0xA003:  // PixelYDimension
        case 0xA20E:  // FocalPlaneXResolution
        case 0xA20F:  // FocalPlaneYResolution
        case 0xA210:  // FocalPlaneResolutionUnit
        case 0xA401:  // CustomRendered
        case 0xA402:  // ExposureMode
        case 0xA403:  // WhiteBalance
        case 0xA404:  // DigitalZoomRatio
        case 0xA406:  // SceneCaptureType
        case 0x9286:  // UserComment
        case 0x0000:  // GPSVersionID (CR3 style)
        case 0x0001:  // GPSLatitudeRef (CR3 style)
        case 0x0002:  // GPSLatitude (CR3 style)
        case 0x0003:  // GPSLongitudeRef (CR3 style)
        case 0x0004:  // GPSLongitude (CR3 style)
        case 0x0005:  // GPSAltitudeRef (CR3 style)
        case 0x0006:  // GPSAltitude (CR3 style)
        case 0x0007:  // GPSTimeStamp (CR3 style)
        case 0x0008:  // GPSSatellites (CR3 style)
        case 0x0009:  // GPSStatus (CR3 style)
        case 0x000A:  // GPSMeasureMode (CR3 style)
        case 0xA430:  // CameraOwnerName
        case 0xA431:  // BodySerialNumber
        case 0xA432:  // LensSpecification
        case 0xA434:  // LensModel
        case 0xA435:  // LensSerialNumber
            return true;
        default: return false;
        }
    }

    static bool panasonic_raw_alias_tag(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x000D:
        case 0x0027:
        case 0x0029:
        case 0x002A:
        case 0x002B:
        case 0x002C:
        case 0x0033:
        case 0x0034:
        case 0x0035:
        case 0x0036:
        case 0x0037:
        case 0x0038:
        case 0x0039:
        case 0x003A:
        case 0x003B:
        case 0x003C:
        case 0x003D:
        case 0x003E:
        case 0x003F:
        case 0x0040:
        case 0x0041:
        case 0x0042:
        case 0x0043:
        case 0x0044:
        case 0x0045:
        case 0x0046:
        case 0x0047:
        case 0x0048:
        case 0x0064:
        case 0x011A:
        case 0x011B:
        case 0x011D:
        case 0x011E:
        case 0x011F:
        case 0x0122:
        case 0x0124:
        case 0x0125:
        case 0x0126:
            return true;
        default: return false;
        }
    }

    static std::string_view cr3_tiff_gps_alias_name(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x0000: return "GPSVersionID";
        case 0x0001: return "GPSLatitudeRef";
        case 0x0002: return "GPSLatitude";
        case 0x0003: return "GPSLongitudeRef";
        case 0x0004: return "GPSLongitude";
        case 0x0005: return "GPSAltitudeRef";
        case 0x0006: return "GPSAltitude";
        case 0x0007: return "GPSTimeStamp";
        case 0x0008: return "GPSSatellites";
        case 0x0009: return "GPSStatus";
        case 0x000A: return "GPSMeasureMode";
        default: return {};
        }
    }

    static bool panasonic_raw_alias_name(uint16_t tag,
                                         std::string* out_name) noexcept
    {
        if (!out_name) {
            return false;
        }
        if (tag == 0x0027U) {
            out_name->append("Gamma");
            return true;
        }
        if (tag == 0x0118U) {
            out_name->append("RawDataOffset");
            return true;
        }
        if (!panasonic_raw_alias_tag(tag)) {
            return false;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "PanasonicRaw_0x%04x",
                      static_cast<unsigned>(tag));
        out_name->append(buf);
        return true;
    }


    static void append_u16_hex(uint16_t tag, std::string* out) noexcept
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%04X", static_cast<unsigned>(tag));
        out->append(buf);
    }


    static void append_exiftool_unknown_tag_name(uint16_t tag,
                                                 std::string* out) noexcept
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "Exif_0x%04x",
                      static_cast<unsigned>(tag));
        out->append(buf);
    }


    static void append_spec_unknown_tag_name(uint16_t tag,
                                             std::string* out) noexcept
    {
        out->append("Tag_");
        append_u16_hex(tag, out);
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


    static bool exif_tag_is_embedded_blob(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x02BC:  // XMLPacket (XMP)
        case 0x83BB:  // IPTC
        case 0x8649:  // Photoshop IRB
        case 0x8773:  // ICC profile
        case 0x927C:  // MakerNote
            return true;
        default: return false;
        }
    }


    static std::string_view
    canonical_interop_tag_name(std::string_view ifd, uint16_t tag,
                               std::string_view fallback,
                               ExportNamePolicy policy) noexcept
    {
        if (fallback.empty()) {
            return {};
        }
        if (tag == 0xC4A5U) {
            return "PrintImageMatching";
        }
        if (policy == ExportNamePolicy::Spec) {
            return fallback;
        }

        switch (tag) {
        case 0x0132: return "ModifyDate";
        case 0x8827: return "ISO";                   // ISOSpeedRatings
        case 0x9004: return "CreateDate";            // DateTimeDigitized
        case 0x9204: return "ExposureCompensation";  // ExposureBiasValue
        case 0x9400: return "AmbientTemperature";
        case 0xA002: return "ExifImageWidth";        // PixelXDimension
        case 0xA003: return "ExifImageHeight";       // PixelYDimension
        case 0xA405:
            return "FocalLengthIn35mmFormat";  // FocalLengthIn35mmFilm
        case 0xA430: return "OwnerName";     // CameraOwnerName
        case 0xA431: return "SerialNumber";  // BodySerialNumber
        case 0xA432: return "LensInfo";      // LensSpecification
        case 0xC630: return "DNGLensInfo";
        case 0xC6F3: return "CameraCalibrationSig";
        case 0xC6F4: return "ProfileCalibrationSig";
        case 0x1001: return "RelatedImageWidth";
        case 0x1002: return "RelatedImageHeight";
        case 0x9216:
        case 0xA216: return "TIFF-EPStandardID";
        case 0xC792: return "OriginalBestQualitySize";
        default: break;
        }

        const bool is_tiff_ifd = ifd.starts_with("ifd")
                                 || ifd.starts_with("subifd")
                                 || ifd.starts_with("mkifd")
                                 || ifd.starts_with("mk_subifd");
        const bool is_exif_ifd = ifd == "exififd" || ifd.ends_with("_exififd");
        const bool is_interop_ifd = ifd == "interopifd"
                                    || ifd.ends_with("_interopifd");

        if (is_tiff_ifd) {
            switch (tag) {
            case 0x00FE: return "SubfileType";         // NewSubfileType
            case 0x0101: return "ImageHeight";         // ImageLength
            case 0x0132: return "ModifyDate";          // DateTime
            case 0x0201: return "ThumbnailOffset";     // JPEGInterchangeFormat
            case 0x0202:
                return "ThumbnailLength";  // JPEGInterchangeFormatLength
            default: return fallback;
            }
        }

        if (is_exif_ifd || is_interop_ifd) {
            switch (tag) {
            case 0x0001: return "InteropIndex";
            case 0x0002: return "InteropVersion";
            default: return fallback;
            }
        }

        return fallback;
    }


    enum class ExrOiioNameMapResult : uint8_t { Unmapped = 0, Mapped, Skip };


    static ExrOiioNameMapResult
    map_exr_attribute_name_for_oiio(std::string_view attr_name,
                                    std::string_view* mapped) noexcept
    {
        if (mapped) {
            *mapped = {};
        }

        struct ExrNameMap final {
            std::string_view exr_name;
            std::string_view oiio_name;
        };

        static constexpr std::array<ExrNameMap, 12> kMap = { {
            { "cameraTransform", "worldtocamera" },
            { "capDate", "DateTime" },
            { "comments", "ImageDescription" },
            { "owner", "Copyright" },
            { "pixelAspectRatio", "PixelAspectRatio" },
            { "xDensity", "XResolution" },
            { "expTime", "ExposureTime" },
            { "wrapmodes", "wrapmodes" },
            { "aperture", "FNumber" },
            { "chunkCount", "openexr:chunkCount" },
            { "maxSamplesPerPixel", "openexr:maxSamplesPerPixel" },
            { "dwaCompressionLevel", "openexr:dwaCompressionLevel" },
        } };

        for (size_t i = 0; i < kMap.size(); ++i) {
            if (attr_name == kMap[i].exr_name) {
                if (mapped) {
                    *mapped = kMap[i].oiio_name;
                }
                return ExrOiioNameMapResult::Mapped;
            }
        }

        static constexpr std::array<std::string_view, 8> kSkip = { {
            "channels",
            "compression",
            "dataWindow",
            "displayWindow",
            "envmap",
            "tiledesc",
            "tiles",
            "type",
        } };

        for (size_t i = 0; i < kSkip.size(); ++i) {
            if (attr_name == kSkip[i]) {
                return ExrOiioNameMapResult::Skip;
            }
        }

        return ExrOiioNameMapResult::Unmapped;
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
                                        const ExifExportContext* exif_ctx,
                                        ExportNamePolicy policy,
                                        std::string* out_name) noexcept
    {
        out_name->clear();
        if (e.key.kind == MetaKeyKind::ExifTag) {
            const std::string_view ifd = arena_string(arena,
                                                      e.key.data.exif_tag.ifd);
            const bool is_cr3_style_ifd = exif_ctx
                                          && exif_ctx->ifd_has_tag_zero(ifd);
            const std::string_view cr3_gps_name
                = is_cr3_style_ifd
                      ? cr3_tiff_gps_alias_name(e.key.data.exif_tag.tag)
                      : std::string_view {};
            std::string_view prefix;
            if (!ifd_to_portable_prefix(ifd, &prefix)) {
                return false;
            }
            if (policy == ExportNamePolicy::ExifToolAlias
                && exif_tag_is_pointer(e.key.data.exif_tag.tag)) {
                return false;
            }
            if (prefix == "tiff" && is_classic_tiff_ifd_token(ifd)) {
                if ((policy == ExportNamePolicy::ExifToolAlias
                     && alias_prefers_exif_prefix_for_tiff_tag(
                         e.key.data.exif_tag.tag))
                    || (is_cr3_style_ifd
                        && cr3_tiff_ifd_prefers_exif_prefix(
                            e.key.data.exif_tag.tag))) {
                    prefix = "exif";
                }
            }
            if (!cr3_gps_name.empty()) {
                prefix = "exif";
            }
            if (policy == ExportNamePolicy::ExifToolAlias
                && exif_tag_is_embedded_blob(e.key.data.exif_tag.tag)) {
                return false;
            }

            const std::string_view tag_name
                = exif_tag_name(ifd, e.key.data.exif_tag.tag);
            out_name->append(prefix);
            out_name->append(":");

            if (policy == ExportNamePolicy::ExifToolAlias && exif_ctx
                && exif_ctx->make_is_sony()
                && parse_index_with_prefix(ifd, "ifd") >= 2) {
                if (e.key.data.exif_tag.tag == 0x0201U) {
                    out_name->append("JpgFromRawStart");
                    return true;
                }
                if (e.key.data.exif_tag.tag == 0x0202U) {
                    out_name->append("JpgFromRawLength");
                    return true;
                }
            }

            if (policy == ExportNamePolicy::ExifToolAlias
                && is_preview_ifd_token(ifd)) {
                    if (e.key.data.exif_tag.tag == 0x0111U) {
                        out_name->append("PreviewImageStart");
                        return true;
                    }
                    if (e.key.data.exif_tag.tag == 0x0117U) {
                        out_name->append("PreviewImageLength");
                        return true;
                    }
            }

            if (policy == ExportNamePolicy::ExifToolAlias && exif_ctx
                && exif_ctx->ifd_is_panasonic_raw(ifd)) {
                if (panasonic_raw_alias_name(e.key.data.exif_tag.tag,
                                             out_name)) {
                    return true;
                }
            }

            if (policy == ExportNamePolicy::ExifToolAlias && exif_ctx
                && exif_ctx->ifd_has_dng_version(ifd)
                && e.key.data.exif_tag.tag == 0x828EU) {
                out_name->append("CFAPattern2");
                return true;
            }

            if (!cr3_gps_name.empty()) {
                out_name->append(cr3_gps_name);
                return true;
            }

            if (tag_name.empty()) {
                if (policy == ExportNamePolicy::ExifToolAlias) {
                    if (exif_ctx && exif_ctx->ifd_is_panasonic_raw(ifd)) {
                        if (!panasonic_raw_alias_name(e.key.data.exif_tag.tag,
                                                      out_name)) {
                            append_exiftool_unknown_tag_name(
                                e.key.data.exif_tag.tag, out_name);
                        }
                    } else {
                        append_exiftool_unknown_tag_name(
                            e.key.data.exif_tag.tag, out_name);
                    }
                    return true;
                }
                return false;
            }

            const std::string_view mapped_name
                = canonical_interop_tag_name(ifd, e.key.data.exif_tag.tag,
                                             tag_name, policy);
            out_name->append(mapped_name);
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
                                const ExifExportContext* exif_ctx,
                                bool include_makernotes,
                                ExportNamePolicy policy,
                                std::string* out_name) noexcept
    {
        out_name->clear();

        if (e.key.kind == MetaKeyKind::ExifTag) {
            const std::string_view ifd = arena_string(arena,
                                                      e.key.data.exif_tag.ifd);
            const bool is_mk_ifd       = is_makernote_ifd(ifd);
            const bool is_cr3_style_ifd = exif_ctx
                                          && exif_ctx->ifd_has_tag_zero(ifd);
            const std::string_view cr3_gps_name
                = is_cr3_style_ifd
                      ? cr3_tiff_gps_alias_name(e.key.data.exif_tag.tag)
                      : std::string_view {};
            if (is_mk_ifd && !include_makernotes) {
                return false;
            }

            std::string_view prefix;
            if (!ifd_to_oiio_prefix(ifd, &prefix)) {
                return false;
            }

            if (policy == ExportNamePolicy::ExifToolAlias && !is_mk_ifd
                && exif_tag_is_pointer(e.key.data.exif_tag.tag)) {
                return false;
            }
            if (!is_mk_ifd && prefix.empty() && is_classic_tiff_ifd_token(ifd)) {
                if ((policy == ExportNamePolicy::ExifToolAlias
                     && alias_prefers_exif_prefix_for_tiff_tag(
                         e.key.data.exif_tag.tag))
                    || (is_cr3_style_ifd
                        && cr3_tiff_ifd_prefers_exif_prefix(
                            e.key.data.exif_tag.tag))) {
                    prefix = "Exif";
                }
            }
            if (!cr3_gps_name.empty()) {
                prefix = "GPS";
            }
            if (policy == ExportNamePolicy::ExifToolAlias && !is_mk_ifd
                && exif_tag_is_embedded_blob(e.key.data.exif_tag.tag)) {
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

            if (policy == ExportNamePolicy::ExifToolAlias && exif_ctx
                && exif_ctx->make_is_sony()
                && parse_index_with_prefix(ifd, "ifd") >= 2) {
                if (e.key.data.exif_tag.tag == 0x0201U) {
                    out_name->append("JpgFromRawStart");
                    return true;
                }
                if (e.key.data.exif_tag.tag == 0x0202U) {
                    out_name->append("JpgFromRawLength");
                    return true;
                }
            }

            if (policy == ExportNamePolicy::ExifToolAlias
                && is_preview_ifd_token(ifd)) {
                    if (e.key.data.exif_tag.tag == 0x0111U) {
                        out_name->append("PreviewImageStart");
                        return true;
                    }
                    if (e.key.data.exif_tag.tag == 0x0117U) {
                        out_name->append("PreviewImageLength");
                        return true;
                    }
            }

            if (policy == ExportNamePolicy::ExifToolAlias && exif_ctx
                && exif_ctx->ifd_is_panasonic_raw(ifd)) {
                if (panasonic_raw_alias_name(e.key.data.exif_tag.tag,
                                             out_name)) {
                    return true;
                }
            }

            if (policy == ExportNamePolicy::ExifToolAlias && exif_ctx
                && exif_ctx->ifd_has_dng_version(ifd)
                && e.key.data.exif_tag.tag == 0x828EU) {
                out_name->append("CFAPattern2");
                return true;
            }

            if (!cr3_gps_name.empty()) {
                out_name->append(cr3_gps_name);
                return true;
            }

            if (!tag_name.empty()) {
                const std::string_view mapped_name
                    = canonical_interop_tag_name(ifd, e.key.data.exif_tag.tag,
                                                 tag_name, policy);
                out_name->append(mapped_name);
            } else {
                if (policy == ExportNamePolicy::ExifToolAlias) {
                    if (exif_ctx && exif_ctx->ifd_is_panasonic_raw(ifd)) {
                        if (!panasonic_raw_alias_name(e.key.data.exif_tag.tag,
                                                      out_name)) {
                            append_exiftool_unknown_tag_name(
                                e.key.data.exif_tag.tag, out_name);
                        }
                    } else {
                        append_exiftool_unknown_tag_name(
                            e.key.data.exif_tag.tag, out_name);
                    }
                } else {
                    append_spec_unknown_tag_name(e.key.data.exif_tag.tag,
                                                 out_name);
                }
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
                std::string_view mapped;
                const ExrOiioNameMapResult map_result
                    = map_exr_attribute_name_for_oiio(attr_name, &mapped);
                if (map_result == ExrOiioNameMapResult::Skip) {
                    return false;
                }
                if (map_result == ExrOiioNameMapResult::Mapped) {
                    out_name->append(mapped);
                    return true;
                }
                out_name->append("openexr:");
                out_name->append(attr_name);
                return true;
            }
            {
                out_name->append("openexr:part:");
                append_u64_dec(e.key.data.exr_attribute.part_index, out_name);
                out_name->append(":");
                out_name->append(attr_name);
                return true;
            }
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
    ExifExportContext exif_ctx;
    exif_ctx.scan(arena, es);

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
            mapped = build_xmp_portable_name(arena, e, &exif_ctx,
                                             options.name_policy,
                                             &name);
            break;
        case ExportNameStyle::Oiio:
            mapped = build_oiio_name(arena, e, &exif_ctx,
                                     options.include_makernotes,
                                     options.name_policy, &name);
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
