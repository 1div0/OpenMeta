#pragma once

#include "openmeta/iptc_iim_decode.h"
#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

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

/// Resource limits applied during IRB decode to bound hostile inputs.
struct PhotoshopIrbDecodeLimits final {
    uint32_t max_resources    = 1U << 16;
    uint64_t max_total_bytes  = 64ULL * 1024ULL * 1024ULL;
    uint32_t max_resource_len = 32U * 1024U * 1024U;
};

/// Decoder options for \ref decode_photoshop_irb.
struct PhotoshopIrbDecodeOptions final {
    bool decode_iptc_iim = true;
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
 * If enabled, IPTC-IIM is additionally decoded from resource id 0x0404
 * (IPTC/NAA) into separate \ref MetaKeyKind::IptcDataset entries marked as
 * \ref EntryFlags::Derived.
 */
PhotoshopIrbDecodeResult
decode_photoshop_irb(std::span<const std::byte> irb_bytes, MetaStore& store,
                     const PhotoshopIrbDecodeOptions& options = PhotoshopIrbDecodeOptions {}) noexcept;

}  // namespace openmeta

