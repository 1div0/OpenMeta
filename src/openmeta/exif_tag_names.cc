#include "openmeta/exif_tag_names.h"

#include <cstdint>

namespace openmeta {
std::string_view
makernote_tag_name(std::string_view ifd, uint16_t tag) noexcept;

namespace {

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
                                          uint32_t count,
                                          uint16_t tag) noexcept
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

}  // namespace

std::string_view
exif_tag_name(std::string_view ifd, uint16_t tag) noexcept
{
    const ExifIfdGroup group = exif_ifd_group(ifd);
    if (group == ExifIfdGroup::Unknown) {
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

}  // namespace openmeta
