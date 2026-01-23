#include "openmeta/exif_tag_names.h"

namespace openmeta {
namespace {

    enum class ExifIfdGroup : uint8_t {
        TiffIfd,
        ExifIfd,
        GpsIfd,
        InteropIfd,
        Unknown,
    };

    static ExifIfdGroup exif_ifd_group(std::string_view ifd) noexcept
    {
        if (ifd == "exififd") {
            return ExifIfdGroup::ExifIfd;
        }
        if (ifd == "gpsifd") {
            return ExifIfdGroup::GpsIfd;
        }
        if (ifd == "interopifd") {
            return ExifIfdGroup::InteropIfd;
        }
        if (ifd.starts_with("ifd") || ifd.starts_with("subifd")) {
            return ExifIfdGroup::TiffIfd;
        }
        return ExifIfdGroup::Unknown;
    }

    static std::string_view tiff_ifd_tag_name(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x00FE: return "NewSubfileType";
        case 0x00FF: return "SubfileType";
        case 0x0100: return "ImageWidth";
        case 0x0101: return "ImageLength";
        case 0x0102: return "BitsPerSample";
        case 0x0103: return "Compression";
        case 0x0106: return "PhotometricInterpretation";
        case 0x010E: return "ImageDescription";
        case 0x010F: return "Make";
        case 0x0110: return "Model";
        case 0x0111: return "StripOffsets";
        case 0x0112: return "Orientation";
        case 0x0115: return "SamplesPerPixel";
        case 0x0116: return "RowsPerStrip";
        case 0x0117: return "StripByteCounts";
        case 0x011A: return "XResolution";
        case 0x011B: return "YResolution";
        case 0x011C: return "PlanarConfiguration";
        case 0x0128: return "ResolutionUnit";
        case 0x012D: return "TransferFunction";
        case 0x0131: return "Software";
        case 0x0132: return "DateTime";
        case 0x013B: return "Artist";
        case 0x013C: return "HostComputer";
        case 0x014A: return "SubIFDs";
        case 0x0201: return "JPEGInterchangeFormat";
        case 0x0202: return "JPEGInterchangeFormatLength";
        case 0x8298: return "Copyright";
        case 0x8769: return "ExifIFDPointer";
        case 0x8825: return "GPSInfoIFDPointer";
        default: return {};
        }
    }

    static std::string_view exif_ifd_tag_name(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x829A: return "ExposureTime";
        case 0x829D: return "FNumber";
        case 0x8822: return "ExposureProgram";
        case 0x8827: return "ISOSpeedRatings";
        case 0x9000: return "ExifVersion";
        case 0x9003: return "DateTimeOriginal";
        case 0x9004: return "DateTimeDigitized";
        case 0x9101: return "ComponentsConfiguration";
        case 0x9102: return "CompressedBitsPerPixel";
        case 0x9201: return "ShutterSpeedValue";
        case 0x9202: return "ApertureValue";
        case 0x9204: return "ExposureBiasValue";
        case 0x9207: return "MeteringMode";
        case 0x9208: return "LightSource";
        case 0x9209: return "Flash";
        case 0x920A: return "FocalLength";
        case 0x927C: return "MakerNote";
        case 0x9286: return "UserComment";
        case 0x9290: return "SubSecTime";
        case 0x9291: return "SubSecTimeOriginal";
        case 0x9292: return "SubSecTimeDigitized";
        case 0xA000: return "FlashpixVersion";
        case 0xA001: return "ColorSpace";
        case 0xA002: return "PixelXDimension";
        case 0xA003: return "PixelYDimension";
        case 0xA004: return "RelatedSoundFile";
        case 0xA005: return "InteroperabilityIFDPointer";
        case 0xA420: return "ImageUniqueID";
        default: return {};
        }
    }

    static std::string_view gps_ifd_tag_name(uint16_t tag) noexcept
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
        case 0x000B: return "GPSDOP";
        case 0x000C: return "GPSSpeedRef";
        case 0x000D: return "GPSSpeed";
        case 0x000E: return "GPSTrackRef";
        case 0x000F: return "GPSTrack";
        case 0x0010: return "GPSImgDirectionRef";
        case 0x0011: return "GPSImgDirection";
        case 0x0012: return "GPSMapDatum";
        case 0x001B: return "GPSProcessingMethod";
        case 0x001C: return "GPSAreaInformation";
        case 0x001D: return "GPSDateStamp";
        case 0x001E: return "GPSDifferential";
        case 0x001F: return "GPSHPositioningError";
        default: return {};
        }
    }

    static std::string_view interop_ifd_tag_name(uint16_t tag) noexcept
    {
        switch (tag) {
        case 0x0001: return "InteroperabilityIndex";
        case 0x0002: return "InteroperabilityVersion";
        case 0x1001: return "RelatedImageWidth";
        case 0x1002: return "RelatedImageLength";
        default: return {};
        }
    }

}  // namespace

std::string_view exif_tag_name(std::string_view ifd, uint16_t tag) noexcept
{
    switch (exif_ifd_group(ifd)) {
    case ExifIfdGroup::TiffIfd: return tiff_ifd_tag_name(tag);
    case ExifIfdGroup::ExifIfd: return exif_ifd_tag_name(tag);
    case ExifIfdGroup::GpsIfd: return gps_ifd_tag_name(tag);
    case ExifIfdGroup::InteropIfd: return interop_ifd_tag_name(tag);
    case ExifIfdGroup::Unknown: return {};
    }
    return {};
}

}  // namespace openmeta

