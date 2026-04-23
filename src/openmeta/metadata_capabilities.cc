// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_capabilities.h"

namespace openmeta {
namespace {

static bool
is_tiff_family(TransferTargetFormat format) noexcept
{
    return format == TransferTargetFormat::Tiff
           || format == TransferTargetFormat::Dng;
}

static bool
is_bmff_family(TransferTargetFormat format) noexcept
{
    return format == TransferTargetFormat::Heif
           || format == TransferTargetFormat::Avif
           || format == TransferTargetFormat::Cr3;
}

static bool
is_primary_still_transfer_target(TransferTargetFormat format) noexcept
{
    return format == TransferTargetFormat::Jpeg || is_tiff_family(format)
           || format == TransferTargetFormat::Png
           || format == TransferTargetFormat::Webp
           || format == TransferTargetFormat::Jp2
           || format == TransferTargetFormat::Jxl || is_bmff_family(format);
}

static bool
has_native_iptc_carrier(TransferTargetFormat format) noexcept
{
    return format == TransferTargetFormat::Jpeg || is_tiff_family(format)
           || format == TransferTargetFormat::Jp2;
}

static bool
has_container_c2pa_jumbf_lane(TransferTargetFormat format) noexcept
{
    return format == TransferTargetFormat::Jpeg
           || format == TransferTargetFormat::Png
           || format == TransferTargetFormat::Webp
           || format == TransferTargetFormat::Jxl || is_bmff_family(format)
           || is_tiff_family(format);
}

static MetadataCapabilitySupport
xmp_support() noexcept
{
#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT
    return MetadataCapabilitySupport::Supported;
#else
    return MetadataCapabilitySupport::Disabled;
#endif
}

static void
set_exif_capability(MetadataCapability* cap) noexcept
{
    if (!cap || !is_primary_still_transfer_target(cap->format)) {
        return;
    }
    cap->read              = MetadataCapabilitySupport::Supported;
    cap->structured_decode = MetadataCapabilitySupport::Supported;
    cap->transfer_prepare  = MetadataCapabilitySupport::Supported;
    cap->target_edit       = is_bmff_family(cap->format)
                                 ? MetadataCapabilitySupport::Bounded
                                 : MetadataCapabilitySupport::Supported;
    cap->raw_preservation  = MetadataCapabilitySupport::Bounded;
}

static void
set_xmp_capability(MetadataCapability* cap) noexcept
{
    if (!cap || !is_primary_still_transfer_target(cap->format)) {
        return;
    }
    const MetadataCapabilitySupport support = xmp_support();
    cap->read              = support;
    cap->structured_decode = support;
    cap->transfer_prepare  = support;
    cap->target_edit       = is_bmff_family(cap->format)
                                 ? MetadataCapabilitySupport::Bounded
                                 : support;
    cap->raw_preservation  = MetadataCapabilitySupport::Bounded;
}

static void
set_icc_capability(MetadataCapability* cap) noexcept
{
    if (!cap || !is_primary_still_transfer_target(cap->format)) {
        return;
    }
    cap->read              = MetadataCapabilitySupport::Supported;
    cap->structured_decode = MetadataCapabilitySupport::Supported;
    cap->transfer_prepare  = MetadataCapabilitySupport::Supported;
    cap->target_edit       = is_bmff_family(cap->format)
                                 ? MetadataCapabilitySupport::Bounded
                                 : MetadataCapabilitySupport::Supported;
    cap->raw_preservation  = MetadataCapabilitySupport::Bounded;
}

static void
set_iptc_capability(MetadataCapability* cap) noexcept
{
    if (!cap || !is_primary_still_transfer_target(cap->format)) {
        return;
    }
    cap->read = has_native_iptc_carrier(cap->format)
                    ? MetadataCapabilitySupport::Supported
                    : MetadataCapabilitySupport::Unsupported;
    cap->structured_decode = cap->read;
    cap->transfer_prepare  = MetadataCapabilitySupport::Bounded;
    cap->target_edit       = MetadataCapabilitySupport::Bounded;
    cap->raw_preservation  = has_native_iptc_carrier(cap->format)
                                  ? MetadataCapabilitySupport::Bounded
                                  : MetadataCapabilitySupport::Unsupported;
}

static void
set_makernote_capability(MetadataCapability* cap) noexcept
{
    if (!cap) {
        return;
    }
    if (cap->format == TransferTargetFormat::Jpeg || is_tiff_family(cap->format)
        || cap->format == TransferTargetFormat::Cr3) {
        cap->read              = MetadataCapabilitySupport::Bounded;
        cap->structured_decode = MetadataCapabilitySupport::Bounded;
        cap->transfer_prepare  = MetadataCapabilitySupport::Bounded;
        cap->target_edit       = MetadataCapabilitySupport::Bounded;
        cap->raw_preservation  = MetadataCapabilitySupport::Bounded;
    }
}

static void
set_photoshop_irb_capability(MetadataCapability* cap) noexcept
{
    if (!cap) {
        return;
    }
    if (cap->format == TransferTargetFormat::Jpeg || is_tiff_family(cap->format)) {
        cap->read              = MetadataCapabilitySupport::Bounded;
        cap->structured_decode = MetadataCapabilitySupport::Bounded;
        cap->transfer_prepare  = MetadataCapabilitySupport::Bounded;
        cap->target_edit       = MetadataCapabilitySupport::Bounded;
        cap->raw_preservation  = MetadataCapabilitySupport::Bounded;
    }
}

static void
set_jumbf_capability(MetadataCapability* cap) noexcept
{
    if (!cap || !has_container_c2pa_jumbf_lane(cap->format)) {
        return;
    }
    cap->read              = MetadataCapabilitySupport::Bounded;
    cap->structured_decode = MetadataCapabilitySupport::Bounded;
    cap->transfer_prepare  = MetadataCapabilitySupport::Bounded;
    cap->target_edit       = MetadataCapabilitySupport::Bounded;
    cap->raw_preservation  = MetadataCapabilitySupport::Bounded;
}

static void
set_c2pa_capability(MetadataCapability* cap) noexcept
{
    set_jumbf_capability(cap);
}

static void
set_bmff_fields_capability(MetadataCapability* cap) noexcept
{
    if (!cap || !is_bmff_family(cap->format)) {
        return;
    }
    cap->read              = MetadataCapabilitySupport::Supported;
    cap->structured_decode = MetadataCapabilitySupport::Supported;
}

static void
set_geotiff_capability(MetadataCapability* cap) noexcept
{
    if (!cap) {
        return;
    }
    if (is_tiff_family(cap->format) || cap->format == TransferTargetFormat::Jp2) {
        cap->read              = MetadataCapabilitySupport::Supported;
        cap->structured_decode = MetadataCapabilitySupport::Supported;
        cap->transfer_prepare  = MetadataCapabilitySupport::Unsupported;
        cap->target_edit       = MetadataCapabilitySupport::Unsupported;
        cap->raw_preservation  = MetadataCapabilitySupport::Unsupported;
    }
}

static void
set_exr_attribute_capability(MetadataCapability* cap) noexcept
{
    if (!cap || cap->format != TransferTargetFormat::Exr) {
        return;
    }
    cap->read              = MetadataCapabilitySupport::Supported;
    cap->structured_decode = MetadataCapabilitySupport::Supported;
    cap->transfer_prepare  = MetadataCapabilitySupport::Bounded;
    cap->target_edit       = MetadataCapabilitySupport::Bounded;
    cap->raw_preservation  = MetadataCapabilitySupport::Bounded;
}

}  // namespace

const char*
metadata_capability_family_name(MetadataCapabilityFamily family) noexcept
{
    switch (family) {
    case MetadataCapabilityFamily::Exif: return "exif";
    case MetadataCapabilityFamily::Xmp: return "xmp";
    case MetadataCapabilityFamily::Icc: return "icc";
    case MetadataCapabilityFamily::Iptc: return "iptc";
    case MetadataCapabilityFamily::MakerNote: return "makernote";
    case MetadataCapabilityFamily::PhotoshopIrb: return "photoshop_irb";
    case MetadataCapabilityFamily::Jumbf: return "jumbf";
    case MetadataCapabilityFamily::C2pa: return "c2pa";
    case MetadataCapabilityFamily::BmffFields: return "bmff_fields";
    case MetadataCapabilityFamily::GeoTiff: return "geotiff";
    case MetadataCapabilityFamily::ExrAttribute: return "exr_attribute";
    }
    return "unknown";
}

const char*
metadata_capability_support_name(MetadataCapabilitySupport support) noexcept
{
    switch (support) {
    case MetadataCapabilitySupport::Unsupported: return "unsupported";
    case MetadataCapabilitySupport::Supported: return "supported";
    case MetadataCapabilitySupport::Bounded: return "bounded";
    case MetadataCapabilitySupport::Disabled: return "disabled";
    }
    return "unknown";
}

bool
metadata_capability_available(MetadataCapabilitySupport support) noexcept
{
    return support == MetadataCapabilitySupport::Supported
           || support == MetadataCapabilitySupport::Bounded;
}

MetadataCapability
metadata_capability(TransferTargetFormat format,
                    MetadataCapabilityFamily family) noexcept
{
    MetadataCapability cap;
    cap.format = format;
    cap.family = family;

    switch (family) {
    case MetadataCapabilityFamily::Exif: set_exif_capability(&cap); break;
    case MetadataCapabilityFamily::Xmp: set_xmp_capability(&cap); break;
    case MetadataCapabilityFamily::Icc: set_icc_capability(&cap); break;
    case MetadataCapabilityFamily::Iptc: set_iptc_capability(&cap); break;
    case MetadataCapabilityFamily::MakerNote:
        set_makernote_capability(&cap);
        break;
    case MetadataCapabilityFamily::PhotoshopIrb:
        set_photoshop_irb_capability(&cap);
        break;
    case MetadataCapabilityFamily::Jumbf: set_jumbf_capability(&cap); break;
    case MetadataCapabilityFamily::C2pa: set_c2pa_capability(&cap); break;
    case MetadataCapabilityFamily::BmffFields:
        set_bmff_fields_capability(&cap);
        break;
    case MetadataCapabilityFamily::GeoTiff:
        set_geotiff_capability(&cap);
        break;
    case MetadataCapabilityFamily::ExrAttribute:
        set_exr_attribute_capability(&cap);
        break;
    }

    return cap;
}

}  // namespace openmeta
