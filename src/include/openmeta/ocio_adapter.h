#pragma once

#include "openmeta/interop_export.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * \file ocio_adapter.h
 * \brief Adapter helpers for OCIO-style metadata trees.
 */

namespace openmeta {

/// Minimal tree node similar to OCIO FormatMetadata composition.
struct OcioMetadataNode final {
    std::string name;
    std::string value;
    std::vector<OcioMetadataNode> children;
};

/// Options for \ref build_ocio_metadata_tree.
struct OcioAdapterOptions final {
    ExportOptions export_options;
    uint32_t max_value_bytes = 1024;
    bool include_empty       = false;

    OcioAdapterOptions() noexcept
        : export_options()
    {
        export_options.style              = ExportNameStyle::XmpPortable;
        export_options.include_makernotes = false;
    }
};

/**
 * \brief Builds a deterministic metadata tree for OCIO-style consumers.
 *
 * Mapping rules:
 * - Item names with a `prefix:name` form become namespace nodes (`prefix`)
 *   with leaf children (`name=value`).
 * - Other names are added as direct leaves under the root.
 */
void
build_ocio_metadata_tree(const MetaStore& store, OcioMetadataNode* root,
                         const OcioAdapterOptions& options) noexcept;

}  // namespace openmeta
