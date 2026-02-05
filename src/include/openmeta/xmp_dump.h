#pragma once

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file xmp_dump.h
 * \brief Lossless XMP sidecar dump of a decoded \ref MetaStore.
 */

namespace openmeta {

/// XMP dump result status.
enum class XmpDumpStatus : uint8_t {
    Ok,
    /// Output buffer was too small; \ref XmpDumpResult::needed reports required size.
    OutputTruncated,
    /// Caller-specified limits prevented generating a complete dump.
    LimitExceeded,
};

/// Resource limits applied during dump to bound output generation.
struct XmpDumpLimits final {
    /// If non-zero, refuse to generate output larger than this many bytes.
    uint64_t max_output_bytes = 0;
    /// If non-zero, refuse to emit more than this many entries.
    uint32_t max_entries = 0;
};

/// Dump options for \ref dump_xmp_lossless.
struct XmpDumpOptions final {
    XmpDumpLimits limits;
    bool include_origin = true;
    bool include_wire   = true;
    bool include_flags  = true;
    bool include_names  = true;
};

/// Dump result (size stats + how many entries were emitted).
struct XmpDumpResult final {
    XmpDumpStatus status = XmpDumpStatus::Ok;
    uint64_t written     = 0;
    uint64_t needed      = 0;
    uint32_t entries     = 0;
};

/**
 * \brief Emits a lossless OpenMeta dump as a valid XMP RDF/XML packet.
 *
 * The output is safe-by-default:
 * - Text fields are XML-escaped and additionally restricted to a safe ASCII subset.
 * - Binary payloads (bytes/text/arrays/scalars) are stored as base64.
 *
 * This dump is intended as a storage-agnostic sidecar format for debugging and
 * offline workflows. It uses a private namespace (`urn:openmeta:dump:1.0`) and
 * is not meant to replace standard, interoperable XMP mappings.
 */
XmpDumpResult
dump_xmp_lossless(const MetaStore& store, std::span<std::byte> out,
                  const XmpDumpOptions& options) noexcept;

}  // namespace openmeta

