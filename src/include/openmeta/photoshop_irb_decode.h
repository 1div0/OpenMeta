#pragma once

#include "openmeta/iptc_iim_decode.h"
#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/**
 * \file photoshop_irb_decode.h
 * \brief Decoder for Photoshop Image Resource Blocks (IRB / 8BIM resources).
 */

namespace openmeta {

/// Photoshop IRB decode result status.
enum class PhotoshopIrbDecodeStatus : uint8_t {
    Ok,
    /// The bytes do not look like an IRB stream.
    Unsupported,
    /// The stream is malformed or inconsistent.
    Malformed,
    /// Resource limits were exceeded.
    LimitExceeded,
};

/// Charset policy for legacy 8-bit Photoshop IRB text payloads.
enum class PhotoshopIrbStringCharset : uint8_t {
    Latin,
    Ascii,
};

/// Resource limits applied during IRB decode to bound hostile inputs.
struct PhotoshopIrbDecodeLimits final {
    uint32_t max_resources    = 1U << 16;
    uint64_t max_total_bytes  = 64ULL * 1024ULL * 1024ULL;
    uint32_t max_resource_len = 32U * 1024U * 1024U;
};

/// Decoder options for \ref decode_photoshop_irb.
struct PhotoshopIrbDecodeOptions final {
    bool decode_iptc_iim                     = true;
    PhotoshopIrbStringCharset string_charset = PhotoshopIrbStringCharset::Latin;
    PhotoshopIrbDecodeLimits limits;
    IptcIimDecodeOptions iptc;
};

struct PhotoshopIrbDecodeResult final {
    PhotoshopIrbDecodeStatus status = PhotoshopIrbDecodeStatus::Ok;
    uint32_t resources_decoded      = 0;
    uint32_t entries_decoded        = 0;
    uint32_t iptc_entries_decoded   = 0;
};

/**
 * \brief Decodes a Photoshop IRB stream and appends resources into \p store.
 *
 * Each resource becomes one \ref Entry with:
 * - \ref MetaKeyKind::PhotoshopIrb (resource id)
 * - \ref MetaValueKind::Bytes (raw resource payload)
 *
 * A bounded interpreted subset is additionally emitted as
 * \ref MetaKeyKind::PhotoshopIrbField entries for fixed-layout resources:
 * - ResolutionInfo (0x03ED)
 * - VersionInfo (0x0421)
 * - PrintFlags (0x03F3)
 * - EffectiveBW (0x03FB)
 * - TargetLayerID (0x0400)
 * - LayersGroupInfo (0x0402)
 * - JPEG_Quality (0x0406)
 * - CopyrightFlag (0x040A)
 * - URL (0x040B)
 * - GlobalAngle (0x040D)
 * - Watermark (0x0410)
 * - ICC_Untagged (0x0411)
 * - EffectsVisible (0x0412)
 * - IDsBaseValue (0x0414)
 * - IndexedColorTableCount (0x0416)
 * - TransparentIndex (0x0417)
 * - GlobalAltitude (0x0419)
 * - SliceInfo (0x041A)
 * - WorkflowURL (0x041B)
 * - URL_List (0x041E)
 * - IPTCDigest (0x0425)
 * - PrintScaleInfo (0x0426)
 * - PixelInfo / PixelAspectRatio (0x0428)
 * - LayerSelectionIDs (0x042D)
 * - LayerGroupsEnabledID (0x0430)
 * - ChannelOptions (0x0435)
 * - PrintFlagsInfo (0x2710)
 * - ClippingPathName (0x0BB7)
 *
 * If enabled, IPTC-IIM is additionally decoded from resource id 0x0404
 * (IPTC/NAA) into separate \ref MetaKeyKind::IptcDataset entries marked as
 * \ref EntryFlags::Derived.
 */
PhotoshopIrbDecodeResult
decode_photoshop_irb(std::span<const std::byte> irb_bytes, MetaStore& store,
                     const PhotoshopIrbDecodeOptions& options
                     = PhotoshopIrbDecodeOptions {}) noexcept;

/**
 * \brief Estimates Photoshop IRB decode counts using the same limits/options.
 */
PhotoshopIrbDecodeResult
measure_photoshop_irb(std::span<const std::byte> irb_bytes,
                      const PhotoshopIrbDecodeOptions& options
                      = PhotoshopIrbDecodeOptions {}) noexcept;

std::string_view
photoshop_irb_resource_name(uint16_t resource_id) noexcept;

}  // namespace openmeta
