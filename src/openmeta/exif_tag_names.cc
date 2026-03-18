#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_store.h"

#include <cstdint>

namespace openmeta {
std::string_view
makernote_tag_name(std::string_view ifd, uint16_t tag) noexcept;

namespace {

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool parse_ciff_dir_id(std::string_view ifd, uint16_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (ifd == "ciff_root") {
            *out = 0xFFFFU;
            return true;
        }
        if (!ifd.starts_with("ciff_") || ifd.size() < 10U || ifd[9] != '_') {
            return false;
        }

        uint16_t dir_id = 0U;
        for (size_t i = 5U; i < 9U; ++i) {
            const char c    = ifd[i];
            uint16_t nibble = 0U;
            if (c >= '0' && c <= '9') {
                nibble = static_cast<uint16_t>(c - '0');
            } else if (c >= 'A' && c <= 'F') {
                nibble = static_cast<uint16_t>(10 + (c - 'A'));
            } else if (c >= 'a' && c <= 'f') {
                nibble = static_cast<uint16_t>(10 + (c - 'a'));
            } else {
                return false;
            }
            dir_id = static_cast<uint16_t>((dir_id << 4U) | nibble);
        }

        *out = dir_id;
        return true;
    }


    static std::string_view
    ciff_synthetic_table_suffix(std::string_view ifd) noexcept
    {
        if (!ifd.starts_with("ciff_") || ifd.size() < 12U) {
            return {};
        }
        size_t pos = 10U;
        while (pos < ifd.size() && ifd[pos] != '_') {
            const char c = ifd[pos];
            if (c < '0' || c > '9') {
                return {};
            }
            ++pos;
        }
        if (pos >= ifd.size() || ifd[pos] != '_') {
            return {};
        }
        return ifd.substr(pos + 1U);
    }

    enum class ExifIfdGroup : uint8_t {
        TiffIfd,
        ExifIfd,
        GpsIfd,
        InteropIfd,
        MpfIfd,
        Unknown,
    };

    static ExifIfdGroup exif_ifd_group(std::string_view ifd) noexcept
    {
        if (ifd == "exififd" || ifd.ends_with("_exififd")) {
            return ExifIfdGroup::ExifIfd;
        }
        if (ifd == "gpsifd" || ifd.ends_with("_gpsifd")) {
            return ExifIfdGroup::GpsIfd;
        }
        if (ifd == "interopifd" || ifd.ends_with("_interopifd")) {
            return ExifIfdGroup::InteropIfd;
        }
        if (ifd.starts_with("ifd") || ifd.starts_with("subifd")) {
            return ExifIfdGroup::TiffIfd;
        }
        if (ifd.starts_with("mkifd") || ifd.starts_with("mk_subifd")) {
            return ExifIfdGroup::TiffIfd;
        }
        if (ifd.starts_with("mpf")) {
            return ExifIfdGroup::MpfIfd;
        }
        return ExifIfdGroup::Unknown;
    }

    struct StandardTagNameEntry final {
        uint16_t tag     = 0;
        const char* name = nullptr;
    };

#include "exif_standard_tag_names_generated.inc"

    static std::string_view find_tag_name(const StandardTagNameEntry* entries,
                                          uint32_t count, uint16_t tag) noexcept
    {
        if (!entries || count == 0) {
            return {};
        }

        uint32_t lo = 0;
        uint32_t hi = count;
        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2;
            const uint16_t cur = entries[mid].tag;
            if (cur < tag) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        if (lo < count && entries[lo].tag == tag && entries[lo].name) {
            return entries[lo].name;
        }
        return {};
    }


    static std::string_view tiff_ifd_tag_name(uint16_t tag) noexcept
    {
        return find_tag_name(kStandardIfdTags,
                             sizeof(kStandardIfdTags)
                                 / sizeof(kStandardIfdTags[0]),
                             tag);
    }


    static std::string_view exif_ifd_tag_name(uint16_t tag) noexcept
    {
        return find_tag_name(kStandardExifIfdTags,
                             sizeof(kStandardExifIfdTags)
                                 / sizeof(kStandardExifIfdTags[0]),
                             tag);
    }


    static std::string_view gps_ifd_tag_name(uint16_t tag) noexcept
    {
        return find_tag_name(kStandardGpsIfdTags,
                             sizeof(kStandardGpsIfdTags)
                                 / sizeof(kStandardGpsIfdTags[0]),
                             tag);
    }


    static std::string_view interop_ifd_tag_name(uint16_t tag) noexcept
    {
        return find_tag_name(kStandardInteropIfdTags,
                             sizeof(kStandardInteropIfdTags)
                                 / sizeof(kStandardInteropIfdTags[0]),
                             tag);
    }


    static std::string_view mpf_ifd_tag_name(uint16_t tag) noexcept
    {
        return find_tag_name(kStandardMpfTags,
                             sizeof(kStandardMpfTags)
                                 / sizeof(kStandardMpfTags[0]),
                             tag);
    }


    static std::string_view ciff_tag_name(std::string_view ifd,
                                          uint16_t tag) noexcept
    {
        uint16_t dir_id = 0U;
        if (!parse_ciff_dir_id(ifd, &dir_id)) {
            return {};
        }
        const std::string_view synthetic = ciff_synthetic_table_suffix(ifd);

        if (synthetic == "timestamp") {
            switch (tag) {
            case 0x0000U: return "DateTimeOriginal";
            case 0x0001U: return "TimeZoneCode";
            case 0x0002U: return "TimeZoneInfo";
            default: return {};
            }
        }

        if (synthetic == "imageinfo") {
            switch (tag) {
            case 0x0000U: return "ImageWidth";
            case 0x0001U: return "ImageHeight";
            case 0x0002U: return "PixelAspectRatio";
            case 0x0003U: return "Rotation";
            case 0x0004U: return "ComponentBitDepth";
            case 0x0005U: return "ColorBitDepth";
            case 0x0006U: return "ColorBW";
            default: return {};
            }
        }

        if (synthetic == "imageformat") {
            switch (tag) {
            case 0x0000U: return "FileFormat";
            case 0x0001U: return "TargetCompressionRatio";
            default: return {};
            }
        }

        if (synthetic == "makemodel") {
            switch (tag) {
            case 0x0000U: return "Make";
            case 0x0006U: return "Model";
            default: return {};
            }
        }

        if (synthetic == "exposureinfo") {
            switch (tag) {
            case 0x0000U: return "ExposureCompensation";
            case 0x0001U: return "ShutterSpeedValue";
            case 0x0002U: return "ApertureValue";
            default: return {};
            }
        }

        if (synthetic == "flashinfo") {
            switch (tag) {
            case 0x0000U: return "FlashGuideNumber";
            case 0x0001U: return "FlashThreshold";
            default: return {};
            }
        }

        if (synthetic == "focallength") {
            switch (tag) {
            case 0x0000U: return "FocalType";
            case 0x0001U: return "FocalLength";
            case 0x0002U: return "FocalPlaneXSize";
            case 0x0003U: return "FocalPlaneYSize";
            default: return {};
            }
        }

        if (synthetic == "shotinfo") {
            switch (tag) {
            case 0x0001U: return "AutoISO";
            case 0x0002U: return "BaseISO";
            case 0x0003U: return "MeasuredEV";
            case 0x0004U: return "TargetAperture";
            case 0x0005U: return "TargetExposureTime";
            case 0x0006U: return "ExposureCompensation";
            case 0x0007U: return "WhiteBalance";
            case 0x0008U: return "SlowShutter";
            case 0x0009U: return "SequenceNumber";
            case 0x000AU: return "OpticalZoomCode";
            default: return {};
            }
        }

        if (synthetic == "decodertable") {
            switch (tag) {
            case 0x0000U: return "DecoderTableNumber";
            case 0x0002U: return "CompressedDataOffset";
            case 0x0003U: return "CompressedDataLength";
            default: return {};
            }
        }

        if (synthetic == "rawjpginfo") {
            switch (tag) {
            case 0x0001U: return "RawJpgQuality";
            case 0x0002U: return "RawJpgSize";
            case 0x0003U: return "RawJpgWidth";
            case 0x0004U: return "RawJpgHeight";
            default: return {};
            }
        }

        if (synthetic == "whitesample") {
            switch (tag) {
            case 0x0001U: return "WhiteSampleWidth";
            case 0x0002U: return "WhiteSampleHeight";
            case 0x0003U: return "WhiteSampleLeftBorder";
            case 0x0004U: return "WhiteSampleTopBorder";
            case 0x0005U: return "WhiteSampleBits";
            default: return {};
            }
        }

        if (dir_id == 0xFFFFU) {
            switch (tag) {
            case 0x2005U: return "RawData";
            default: return {};
            }
        }

        switch (dir_id) {
        case 0x2804U:
            switch (tag) {
            case 0x0805U: return "CanonFileDescription";
            case 0x0815U: return "CanonImageType";
            default: return {};
            }
        case 0x2807U:
            switch (tag) {
            case 0x080AU: return "MakeModel";
            case 0x0810U: return "OwnerName";
            default: return {};
            }
        case 0x3002U:
            switch (tag) {
            case 0x1010U: return "ShutterReleaseMethod";
            case 0x1011U: return "ShutterReleaseTiming";
            case 0x1016U: return "ReleaseSetting";
            case 0x1807U: return "TargetDistanceSetting";
            case 0x1813U: return "FlashInfo";
            default: return {};
            }
        case 0x3003U:
            switch (tag) {
            case 0x1814U: return "MeasuredEV";
            default: return {};
            }
        case 0x3004U:
            switch (tag) {
            case 0x080BU: return "CanonFirmwareVersion";
            case 0x080CU: return "ComponentVersion";
            case 0x080DU: return "ROMOperationMode";
            case 0x101CU: return "BaseISO";
            case 0x180BU: return "UnknownNumber";
            case 0x1834U: return "CanonModelID";
            case 0x1835U: return "DecoderTable";
            case 0x183BU: return "SerialNumberFormat";
            default: return {};
            }
        case 0x300AU:
            switch (tag) {
            case 0x0816U: return "OriginalFileName";
            case 0x0817U: return "ThumbnailFileName";
            case 0x100AU: return "TargetImageType";
            case 0x1803U: return "ImageFormat";
            case 0x1804U: return "RecordID";
            case 0x1806U: return "SelfTimerTime";
            case 0x180EU: return "TimeStamp";
            case 0x1810U: return "ImageInfo";
            case 0x1817U: return "FileNumber";
            default: return {};
            }
        case 0x300BU:
            switch (tag) {
            case 0x1030U: return "WhiteSample";
            case 0x1028U: return "CanonFlashInfo";
            case 0x1029U: return "FocalLength";
            case 0x102AU: return "CanonShotInfo";
            case 0x10B5U: return "RawJpgInfo";
            default: return {};
            }
        default: return {};
        }
    }


    static std::string_view
    contextual_exif_entry_name(const Entry& entry, ExifTagNamePolicy policy,
                               std::string_view canonical) noexcept
    {
        if (policy != ExifTagNamePolicy::ExifToolCompat
            || !any(entry.flags, EntryFlags::ContextualName)) {
            return canonical;
        }

        switch (entry.origin.name_context_kind) {
        case EntryNameContextKind::None: return canonical;
        case EntryNameContextKind::OlympusFocusInfo1600:
            switch (entry.origin.name_context_variant) {
            case 1: return "ImageStabilization";
            case 2: return "Olympus_FocusInfo_0x1600";
            default: return canonical;
            }
        case EntryNameContextKind::KodakMain0028:
            switch (entry.origin.name_context_variant) {
            case 1: return "KodakModel";
            default: return canonical;
            }
        case EntryNameContextKind::CanonMain0038:
            switch (entry.origin.name_context_variant) {
            case 1: return "Canon_0x0038";
            default: return canonical;
            }
        case EntryNameContextKind::CanonCustomFunctions20103:
            switch (entry.origin.name_context_variant) {
            case 1: return "ISOExpansion";
            case 2: return "ISOSpeedRange";
            default: return canonical;
            }
        case EntryNameContextKind::CanonCustomFunctions2010C:
            switch (entry.origin.name_context_variant) {
            case 1: return "CanonCustom_Functions2_0x010c";
            default: return canonical;
            }
        case EntryNameContextKind::CanonCustomFunctions20510:
            switch (entry.origin.name_context_variant) {
            case 1: return "SuperimposedDisplay";
            default: return canonical;
            }
        case EntryNameContextKind::CanonCustomFunctions20701:
            switch (entry.origin.name_context_variant) {
            case 1: return "ShutterButtonAFOnButton";
            case 2: return "AFAndMeteringButtons";
            default: return canonical;
            }
        }
        return canonical;
    }

}  // namespace

std::string_view
exif_tag_name(std::string_view ifd, uint16_t tag) noexcept
{
    const ExifIfdGroup group = exif_ifd_group(ifd);
    if (group == ExifIfdGroup::Unknown) {
        if (ifd == "ciff_root" || ifd.starts_with("ciff_")) {
            return ciff_tag_name(ifd, tag);
        }
        if (ifd.starts_with("mk_")) {
            return makernote_tag_name(ifd, tag);
        }
        return {};
    }

    switch (group) {
    case ExifIfdGroup::TiffIfd: {
        std::string_view name = tiff_ifd_tag_name(tag);
        if (name.empty()) {
            name = exif_ifd_tag_name(tag);
        }
        return name;
    }
    case ExifIfdGroup::ExifIfd: {
        std::string_view name = exif_ifd_tag_name(tag);
        if (name.empty()) {
            name = tiff_ifd_tag_name(tag);
        }
        return name;
    }
    case ExifIfdGroup::GpsIfd: return gps_ifd_tag_name(tag);
    case ExifIfdGroup::InteropIfd: return interop_ifd_tag_name(tag);
    case ExifIfdGroup::MpfIfd: return mpf_ifd_tag_name(tag);
    case ExifIfdGroup::Unknown: return {};
    }
    return {};
}


std::string_view
exif_entry_name(const MetaStore& store, const Entry& entry,
                ExifTagNamePolicy policy) noexcept
{
    if (entry.key.kind != MetaKeyKind::ExifTag) {
        return {};
    }

    const std::string_view ifd = arena_string(store.arena(),
                                              entry.key.data.exif_tag.ifd);
    const std::string_view canonical
        = exif_tag_name(ifd, entry.key.data.exif_tag.tag);
    return contextual_exif_entry_name(entry, policy, canonical);
}

}  // namespace openmeta
