#pragma once

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file jumbf_decode.h
 * \brief Decoder for JUMBF/C2PA payload blocks.
 */

namespace openmeta {

/// JUMBF decode result status.
enum class JumbfDecodeStatus : uint8_t {
    Ok,
    /// Input does not look like a JUMBF payload.
    Unsupported,
    /// Input is truncated or structurally invalid.
    Malformed,
    /// Refused due to configured resource limits.
    LimitExceeded,
};

/// Resource limits for JUMBF/C2PA decode.
struct JumbfDecodeLimits final {
    /// Maximum input bytes to accept (0 = unlimited).
    uint64_t max_input_bytes = 64ULL * 1024ULL * 1024ULL;
    /// Maximum BMFF box depth.
    uint32_t max_box_depth = 32;
    /// Maximum BMFF boxes to traverse.
    uint32_t max_boxes = 1U << 16;
    /// Maximum emitted entries.
    uint32_t max_entries = 200000;
    /// Maximum CBOR recursion depth.
    uint32_t max_cbor_depth = 64;
    /// Maximum CBOR items to parse.
    uint32_t max_cbor_items = 200000;
    /// Maximum CBOR string key bytes.
    uint32_t max_cbor_key_bytes = 1024;
    /// Maximum CBOR text value bytes.
    uint32_t max_cbor_text_bytes = 8U * 1024U * 1024U;
    /// Maximum CBOR byte-string value bytes.
    uint32_t max_cbor_bytes_bytes = 8U * 1024U * 1024U;
};

/// Decoder options for \ref decode_jumbf_payload.
struct JumbfDecodeOptions final {
    /// If true, traverse `cbor` boxes and emit decoded CBOR key/value entries.
    bool decode_cbor = true;
    /// If true, emit a `c2pa.detected` marker when C2PA-like payload is seen.
    bool detect_c2pa = true;
    JumbfDecodeLimits limits;
};

/// JUMBF decode result summary.
struct JumbfDecodeResult final {
    JumbfDecodeStatus status = JumbfDecodeStatus::Unsupported;
    uint32_t boxes_decoded   = 0;
    uint32_t cbor_items      = 0;
    uint32_t entries_decoded = 0;
};

/**
 * \brief Decodes a JUMBF/C2PA payload and appends entries into \p store.
 *
 * Emitted entries use:
 * - \ref MetaKeyKind::JumbfField for structural fields
 * - \ref MetaKeyKind::JumbfCborKey for decoded CBOR keys/values
 *
 * Duplicate keys are preserved.
 */
JumbfDecodeResult
decode_jumbf_payload(std::span<const std::byte> bytes, MetaStore& store,
                     EntryFlags flags = EntryFlags::None,
                     const JumbfDecodeOptions& options
                     = JumbfDecodeOptions {}) noexcept;

}  // namespace openmeta
