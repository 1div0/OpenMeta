#pragma once

#include "openmeta/meta_store.h"

#include <cstddef>
#include <cstdint>
#include <span>

/**
 * \file xmp_dump.h
 * \brief XMP sidecar generation for a decoded \ref MetaStore.
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

/// Options for \ref dump_xmp_portable.
struct XmpPortableOptions final {
    XmpDumpLimits limits;
    /// Include TIFF/EXIF/GPS derived properties.
    bool include_exif = true;
    /// Include \ref MetaKeyKind::XmpProperty entries already present in the store.
    ///
    /// \note Currently only simple `property_path` values are emitted (no `/` nesting).
    bool include_existing_xmp = false;
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

/**
 * \brief Emits a portable XMP sidecar packet (standard XMP schemas).
 *
 * The output is safe-by-default:
 * - XML reserved characters are escaped.
 * - Invalid control bytes are emitted as deterministic ASCII escapes.
 *
 * This mode is intended for interoperability (e.g. XMP sidecars alongside RAW/JPEG files).
 * It emits a best-effort mapping from decoded EXIF/TIFF/GPS fields to standard XMP
 * properties (e.g. `tiff:Make`, `exif:ExposureTime`, `exif:GPSLatitude`).
 */
XmpDumpResult
dump_xmp_portable(const MetaStore& store, std::span<std::byte> out,
                  const XmpPortableOptions& options) noexcept;

}  // namespace openmeta
