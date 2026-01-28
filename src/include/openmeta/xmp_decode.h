#pragma once

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file xmp_decode.h
 * \brief Decoder for XMP packets (RDF/XML).
 */

namespace openmeta {

/// XMP decode result status.
enum class XmpDecodeStatus : uint8_t {
    Ok,
    OutputTruncated,
    Unsupported,
    Malformed,
    LimitExceeded,
};

/// Resource limits applied during XMP decode to bound hostile inputs.
struct XmpDecodeLimits final {
    uint32_t max_depth      = 128;
    uint32_t max_properties = 200000;

    /// Caps the input XMP packet size (0 = unlimited).
    uint64_t max_input_bytes = 64ULL * 1024ULL * 1024ULL;

    /// Max bytes per decoded property path string.
    uint32_t max_path_bytes = 1024;

    /// Max text bytes per decoded value (element/attribute).
    uint32_t max_value_bytes = 8U * 1024U * 1024U;

    /// Max total text bytes accumulated across values (0 = unlimited).
    uint64_t max_total_value_bytes = 64ULL * 1024ULL * 1024ULL;
};

/// Decoder options for \ref decode_xmp_packet.
struct XmpDecodeOptions final {
    /// If true, decodes attributes on `rdf:Description` as XMP properties.
    bool decode_description_attributes = true;
    XmpDecodeLimits limits;
};

struct XmpDecodeResult final {
    XmpDecodeStatus status = XmpDecodeStatus::Ok;
    uint32_t entries_decoded = 0;
};

/**
 * \brief Decodes an XMP packet and appends properties into \p store.
 *
 * The decoder emits one \ref Entry per decoded property value with:
 * - \ref MetaKeyKind::XmpProperty (`schema_ns` URI + `property_path`)
 * - \ref MetaValueKind::Text (UTF-8)
 *
 * Duplicate properties are preserved.
 */
XmpDecodeResult
decode_xmp_packet(std::span<const std::byte> xmp_bytes, MetaStore& store,
                  EntryFlags flags = EntryFlags::None,
                  const XmpDecodeOptions& options = XmpDecodeOptions {}) noexcept;

}  // namespace openmeta

