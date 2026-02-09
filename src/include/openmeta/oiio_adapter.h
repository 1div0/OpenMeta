#pragma once

#include "openmeta/interop_export.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * \file oiio_adapter.h
 * \brief Adapter helpers for OIIO-style metadata export.
 */

namespace openmeta {

/// Flattened metadata attribute used by the OIIO adapter.
struct OiioAttribute final {
    std::string name;
    std::string value;
};

/// Options for \ref collect_oiio_attributes.
struct OiioAdapterOptions final {
    ExportOptions export_options;
    uint32_t max_value_bytes = 1024;
    bool include_empty       = false;

    OiioAdapterOptions() noexcept
        : export_options()
    {
        export_options.style = ExportNameStyle::Oiio;
    }
};

/**
 * \brief Collects OIIO-style name/value attributes from a \ref MetaStore.
 *
 */
void
collect_oiio_attributes(const MetaStore& store, std::vector<OiioAttribute>* out,
                        const OiioAdapterOptions& options) noexcept;

}  // namespace openmeta
