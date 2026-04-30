// SPDX-License-Identifier: Apache-2.0

#include "openmeta/vendor_raw_processing.h"

#include "openmeta/byte_arena.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/meta_key.h"

#include <cstddef>
#include <span>

namespace openmeta {
namespace {

    static constexpr uint32_t kVendorRawColor = static_cast<uint32_t>(
        VendorRawProcessingGroup::Color);
    static constexpr uint32_t kVendorRawWhiteBalance = static_cast<uint32_t>(
        VendorRawProcessingGroup::WhiteBalance);
    static constexpr uint32_t kVendorRawGeometry = static_cast<uint32_t>(
        VendorRawProcessingGroup::Geometry);
    static constexpr uint32_t kVendorRawStorage = static_cast<uint32_t>(
        VendorRawProcessingGroup::Storage);
    static constexpr uint32_t kVendorRawLensCorrection = static_cast<uint32_t>(
        VendorRawProcessingGroup::LensCorrection);
    static constexpr uint32_t kVendorRawRawData = static_cast<uint32_t>(
        VendorRawProcessingGroup::RawData);
    static constexpr uint32_t kVendorRawSensor = static_cast<uint32_t>(
        VendorRawProcessingGroup::Sensor);
    static constexpr uint32_t kVendorRawPrivateTable = static_cast<uint32_t>(
        VendorRawProcessingGroup::PrivateTable);

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool starts_with(std::string_view text,
                            std::string_view prefix) noexcept
    {
        return text.size() >= prefix.size()
               && text.substr(0U, prefix.size()) == prefix;
    }

    static bool contains(std::string_view text,
                         std::string_view fragment) noexcept
    {
        return text.find(fragment) != std::string_view::npos;
    }

    static uint32_t classify_canon_ifd(std::string_view ifd) noexcept
    {
        uint32_t groups = 0U;
        if (contains(ifd, "colorbalance") || contains(ifd, "colorcoefs")) {
            groups |= kVendorRawColor | kVendorRawWhiteBalance;
        }
        if (contains(ifd, "colorcalib")) {
            groups |= kVendorRawColor;
        }
        if (contains(ifd, "colordata")) {
            groups |= kVendorRawPrivateTable;
        }
        if (contains(ifd, "measuredcolor")) {
            groups |= kVendorRawColor | kVendorRawWhiteBalance;
        }
        if (contains(ifd, "cropinfo") || contains(ifd, "aspectinfo")
            || contains(ifd, "cmp1")) {
            groups |= kVendorRawGeometry;
        }
        return groups;
    }

    static uint32_t classify_nikon_ifd(std::string_view ifd) noexcept
    {
        uint32_t groups = 0U;
        if (contains(ifd, "colorbalance") || contains(ifd, "wbadj")) {
            groups |= kVendorRawColor | kVendorRawWhiteBalance
                      | kVendorRawSensor;
        }
        if (contains(ifd, "distort") || contains(ifd, "vignette")) {
            groups |= kVendorRawLensCorrection;
        }
        if (contains(ifd, "nefinfo")) {
            groups |= kVendorRawPrivateTable;
        }
        if (contains(ifd, "cropdata")) {
            groups |= kVendorRawGeometry;
        }
        if (contains(ifd, "noisereduction")) {
            groups |= kVendorRawSensor;
        }
        return groups;
    }

    static uint32_t classify_sony_ifd(std::string_view ifd) noexcept
    {
        uint32_t groups = 0U;
        if (contains(ifd, "sr2private") || contains(ifd, "srf")) {
            groups |= kVendorRawStorage | kVendorRawPrivateTable;
        }
        return groups;
    }

    static uint32_t classify_fujifilm_ifd(std::string_view ifd) noexcept
    {
        uint32_t groups = 0U;
        if (contains(ifd, "rafdata")) {
            groups |= kVendorRawGeometry | kVendorRawRawData
                      | kVendorRawPrivateTable;
        }
        return groups;
    }

    static uint32_t classify_pentax_ifd(std::string_view ifd) noexcept
    {
        uint32_t groups = 0U;
        if (contains(ifd, "wblevels") || contains(ifd, "kelvinwb")
            || contains(ifd, "awbinfo")) {
            groups |= kVendorRawColor | kVendorRawWhiteBalance;
        }
        if (contains(ifd, "colorinfo")) {
            groups |= kVendorRawColor;
        }
        if (contains(ifd, "lenscorr")) {
            groups |= kVendorRawLensCorrection;
        }
        return groups;
    }

    static uint32_t classify_panasonic_ifd(std::string_view ifd) noexcept
    {
        (void)ifd;
        uint32_t groups = 0U;
        return groups;
    }

    static uint32_t classify_olympus_ifd(std::string_view ifd) noexcept
    {
        uint32_t groups = 0U;
        if (contains(ifd, "imageprocessing") || contains(ifd, "rawinfo")
            || contains(ifd, "rawdevelopment")) {
            groups |= kVendorRawPrivateTable;
        }
        return groups;
    }

    static uint32_t classify_kodak_ifd(std::string_view ifd) noexcept
    {
        (void)ifd;
        uint32_t groups = 0U;
        return groups;
    }

    static uint32_t classify_minolta_ifd(std::string_view ifd) noexcept
    {
        uint32_t groups = 0U;
        if (contains(ifd, "minoltaraw")) {
            groups |= kVendorRawRawData | kVendorRawPrivateTable;
        }
        if (contains(ifd, "wbinfo")) {
            groups |= kVendorRawColor | kVendorRawWhiteBalance;
        }
        return groups;
    }

    static uint32_t classify_sigma_ifd(std::string_view ifd) noexcept
    {
        uint32_t groups = 0U;
        if (contains(ifd, "wbsettings")) {
            groups |= kVendorRawColor | kVendorRawWhiteBalance;
        }
        return groups;
    }

    static uint32_t classify_sigma_tag(std::string_view ifd,
                                       uint16_t tag) noexcept
    {
        if (!starts_with(ifd, "mk_sigma")) {
            return 0U;
        }
        switch (tag) {
        case 0x0039U:
        case 0x0055U: return kVendorRawSensor;
        default: break;
        }
        return 0U;
    }

    static uint32_t classify_samsung_ifd(std::string_view ifd) noexcept
    {
        (void)ifd;
        uint32_t groups = 0U;
        return groups;
    }

    static uint32_t classify_ricoh_ifd(std::string_view ifd) noexcept
    {
        (void)ifd;
        uint32_t groups = 0U;
        return groups;
    }

    static uint32_t classify_name(std::string_view name) noexcept
    {
        uint32_t groups = 0U;
        if (contains(name, "ColorMatrix") || contains(name, "ColorCalibration")
            || contains(name, "CameraColorCalibration")
            || contains(name, "CameraCalibration")
            || contains(name, "CbCrMatrix") || contains(name, "CbCrGain")
            || contains(name, "ToneCurve")) {
            groups |= kVendorRawColor;
        }
        if (contains(name, "WB_") || contains(name, "WhiteBalance")
            || contains(name, "ColorTemp") || contains(name, "RGGBLevels")
            || contains(name, "MeasuredRGGB") || contains(name, "WBRed")
            || contains(name, "WBGreen") || contains(name, "WBBlue")
            || contains(name, "WB_RB") || contains(name, "WB_G")
            || contains(name, "WBMode") || contains(name, "WBScale")
            || contains(name, "WB_RGBMul") || contains(name, "WB_RGBCoeffs")
            || contains(name, "WB_Red") || contains(name, "WB_Blue")) {
            groups |= kVendorRawColor | kVendorRawWhiteBalance;
        }
        if (contains(name, "ImageWidth") || contains(name, "ImageHeight")
            || contains(name, "ImageSize") || contains(name, "CroppedImage")
            || contains(name, "Crop") || contains(name, "ActiveArea")
            || contains(name, "SensorWidth")
            || contains(name, "SensorHeight") || contains(name, "SensorFull")
            || contains(name, "SensorLeft") || contains(name, "SensorTop")
            || contains(name, "SensorAreas")) {
            groups |= kVendorRawGeometry;
        }
        if (contains(name, "Strip") || contains(name, "ByteCount")
            || contains(name, "FileOffset") || contains(name, "DataOffset")
            || contains(name, "DataLength") || contains(name, "DecoderTable")
            || contains(name, "CompressedData") || contains(name, "ByteOrder")
            || contains(name, "StorageMethod")) {
            groups |= kVendorRawStorage;
        }
        if (contains(name, "RawData") || contains(name, "RawFile")
            || contains(name, "OriginalRaw") || contains(name, "NEFInfo")
            || contains(name, "RawFormat") || contains(name, "RawDev")
            || contains(name, "RawImage") || contains(name, "RawDepth")
            || contains(name, "RawHistogram") || contains(name, "RawCFA")
            || contains(name, "SR2") || contains(name, "SRF")) {
            groups |= kVendorRawRawData;
        }
        if (contains(name, "Distort") || contains(name, "Vignette")
            || contains(name, "Vignet") || contains(name, "LensCorrection")
            || contains(name, "Aberration") || contains(name, "Shading")
            || contains(name, "Peripheral") || contains(name, "Diffraction")
            || contains(name, "LensModulationOptimizer")) {
            groups |= kVendorRawLensCorrection;
        }
        if (contains(name, "BlackLevel") || contains(name, "WhiteLevel")
            || contains(name, "Linearization") || contains(name, "Noise")
            || contains(name, "CFA") || contains(name, "Sensor")
            || contains(name, "ValidBits") || contains(name, "RawValueRange")
            || contains(name, "RawValueMedian")
            || contains(name, "CameraTemperature")
            || contains(name, "RawDepth") || contains(name, "BitDepth")
            || contains(name, "BayerPattern") || contains(name, "Linearity")) {
            groups |= kVendorRawSensor;
        }
        if (contains(name, "DataDump") || contains(name, "UnknownBlock")
            || contains(name, "CameraParameters")) {
            groups |= kVendorRawPrivateTable;
        }
        return groups;
    }

    static bool family_matches_ifd(std::string_view ifd,
                                   VendorRawProcessingFamily family) noexcept
    {
        switch (family) {
        case VendorRawProcessingFamily::Sony:
            return starts_with(ifd, "mk_sony");
        case VendorRawProcessingFamily::Canon:
            return starts_with(ifd, "mk_canon");
        case VendorRawProcessingFamily::Nikon:
            return starts_with(ifd, "mk_nikon");
        case VendorRawProcessingFamily::Fujifilm:
            return starts_with(ifd, "mk_fuji");
        case VendorRawProcessingFamily::Pentax:
            return starts_with(ifd, "mk_pentax");
        case VendorRawProcessingFamily::Panasonic:
            return starts_with(ifd, "mk_panasonic");
        case VendorRawProcessingFamily::Olympus:
            return starts_with(ifd, "mk_olympus");
        case VendorRawProcessingFamily::Kodak:
            return starts_with(ifd, "mk_kodak");
        case VendorRawProcessingFamily::Minolta:
            return starts_with(ifd, "mk_minolta");
        case VendorRawProcessingFamily::Sigma:
            return starts_with(ifd, "mk_sigma");
        case VendorRawProcessingFamily::Samsung:
            return starts_with(ifd, "mk_samsung");
        case VendorRawProcessingFamily::Ricoh:
            return starts_with(ifd, "mk_ricoh");
        }
        return false;
    }

    static void
    add_summary_group_counts(uint32_t groups,
                             VendorRawProcessingSummary* out) noexcept
    {
        if (!out || groups == 0U) {
            return;
        }
        out->fields_seen += 1U;
        if ((groups & kVendorRawColor) != 0U) {
            out->color_fields += 1U;
        }
        if ((groups & kVendorRawWhiteBalance) != 0U) {
            out->white_balance_fields += 1U;
        }
        if ((groups & kVendorRawGeometry) != 0U) {
            out->geometry_fields += 1U;
        }
        if ((groups & kVendorRawStorage) != 0U) {
            out->storage_fields += 1U;
        }
        if ((groups & kVendorRawLensCorrection) != 0U) {
            out->lens_correction_fields += 1U;
        }
        if ((groups & kVendorRawRawData) != 0U) {
            out->raw_data_fields += 1U;
        }
        if ((groups & kVendorRawSensor) != 0U) {
            out->sensor_fields += 1U;
        }
        if ((groups & kVendorRawPrivateTable) != 0U) {
            out->private_table_fields += 1U;
        }
    }

}  // namespace

VendorRawProcessingGroup
classify_vendor_raw_processing_field(std::string_view ifd,
                                     std::string_view name,
                                     uint16_t tag) noexcept
{
    uint32_t groups = classify_name(name);
    if (starts_with(ifd, "mk_canon")) {
        groups |= classify_canon_ifd(ifd);
    } else if (starts_with(ifd, "mk_nikon")) {
        groups |= classify_nikon_ifd(ifd);
    } else if (starts_with(ifd, "mk_sony")) {
        groups |= classify_sony_ifd(ifd);
    } else if (starts_with(ifd, "mk_fuji")) {
        groups |= classify_fujifilm_ifd(ifd);
    } else if (starts_with(ifd, "mk_pentax")) {
        groups |= classify_pentax_ifd(ifd);
    } else if (starts_with(ifd, "mk_panasonic")) {
        groups |= classify_panasonic_ifd(ifd);
    } else if (starts_with(ifd, "mk_olympus")) {
        groups |= classify_olympus_ifd(ifd);
    } else if (starts_with(ifd, "mk_kodak")) {
        groups |= classify_kodak_ifd(ifd);
    } else if (starts_with(ifd, "mk_minolta")) {
        groups |= classify_minolta_ifd(ifd);
    } else if (starts_with(ifd, "mk_sigma")) {
        groups |= classify_sigma_ifd(ifd);
        groups |= classify_sigma_tag(ifd, tag);
    } else if (starts_with(ifd, "mk_samsung")) {
        groups |= classify_samsung_ifd(ifd);
    } else if (starts_with(ifd, "mk_ricoh")) {
        groups |= classify_ricoh_ifd(ifd);
    } else {
        return VendorRawProcessingGroup::None;
    }
    return static_cast<VendorRawProcessingGroup>(groups);
}

VendorRawProcessingSummary
vendor_raw_processing_from_store(const MetaStore& store,
                                 VendorRawProcessingFamily family) noexcept
{
    VendorRawProcessingSummary out;
    const std::span<const Entry> entries = store.entries();
    for (size_t i = 0U; i < entries.size(); ++i) {
        const Entry& entry = entries[i];
        if (entry.key.kind != MetaKeyKind::ExifTag
            || any(entry.flags, EntryFlags::Deleted)) {
            continue;
        }

        const std::string_view ifd = arena_string(store.arena(),
                                                  entry.key.data.exif_tag.ifd);
        if (!family_matches_ifd(ifd, family)) {
            continue;
        }
        const std::string_view name
            = exif_entry_name(store, entry, ExifTagNamePolicy::ExifToolCompat);
        const VendorRawProcessingGroup groups
            = classify_vendor_raw_processing_field(ifd, name,
                                                   entry.key.data.exif_tag.tag);
        add_summary_group_counts(static_cast<uint32_t>(groups), &out);
    }
    return out;
}

bool
vendor_raw_processing_group_has(VendorRawProcessingGroup groups,
                                VendorRawProcessingGroup group) noexcept
{
    const uint32_t group_bits = static_cast<uint32_t>(group);
    return (static_cast<uint32_t>(groups) & group_bits) != 0U;
}

const char*
vendor_raw_processing_family_name(VendorRawProcessingFamily family) noexcept
{
    switch (family) {
    case VendorRawProcessingFamily::Sony: return "sony";
    case VendorRawProcessingFamily::Canon: return "canon";
    case VendorRawProcessingFamily::Nikon: return "nikon";
    case VendorRawProcessingFamily::Fujifilm: return "fujifilm";
    case VendorRawProcessingFamily::Pentax: return "pentax";
    case VendorRawProcessingFamily::Panasonic: return "panasonic";
    case VendorRawProcessingFamily::Olympus: return "olympus";
    case VendorRawProcessingFamily::Kodak: return "kodak";
    case VendorRawProcessingFamily::Minolta: return "minolta";
    case VendorRawProcessingFamily::Sigma: return "sigma";
    case VendorRawProcessingFamily::Samsung: return "samsung";
    case VendorRawProcessingFamily::Ricoh: return "ricoh";
    }
    return "unknown";
}

const char*
vendor_raw_processing_group_name(VendorRawProcessingGroup group) noexcept
{
    switch (group) {
    case VendorRawProcessingGroup::None: return "none";
    case VendorRawProcessingGroup::Color: return "color";
    case VendorRawProcessingGroup::WhiteBalance: return "white_balance";
    case VendorRawProcessingGroup::Geometry: return "geometry";
    case VendorRawProcessingGroup::Storage: return "storage";
    case VendorRawProcessingGroup::LensCorrection: return "lens_correction";
    case VendorRawProcessingGroup::RawData: return "raw_data";
    case VendorRawProcessingGroup::Sensor: return "sensor";
    case VendorRawProcessingGroup::PrivateTable: return "private_table";
    }
    return "mixed";
}

}  // namespace openmeta
