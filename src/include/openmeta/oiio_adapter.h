#pragma once

#include "openmeta/interop_export.h"

#include <cstddef>
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

/// Typed metadata payload for OIIO-style adapters.
struct OiioTypedValue final {
    MetaValueKind kind         = MetaValueKind::Empty;
    MetaElementType elem_type  = MetaElementType::U8;
    TextEncoding text_encoding = TextEncoding::Unknown;
    uint32_t count             = 0;
    MetaValue::Data data;
    std::vector<std::byte> storage;
};

/// Flattened typed metadata attribute used by the OIIO adapter.
struct OiioTypedAttribute final {
    std::string name;
    OiioTypedValue value;
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

/// Stable flat request for OIIO adapter export.
struct OiioAdapterRequest final {
    ExportNamePolicy name_policy = ExportNamePolicy::ExifToolAlias;
    bool include_makernotes      = true;
    bool include_origin          = false;
    bool include_flags           = false;
    uint32_t max_value_bytes     = 1024;
    bool include_empty           = false;
};

/**
 * \brief Collects OIIO-style name/value attributes from a \ref MetaStore.
 *
 */
void
collect_oiio_attributes(const MetaStore& store, std::vector<OiioAttribute>* out,
                        const OiioAdapterOptions& options) noexcept;

/**
 * \brief Strict safe export for OIIO-style name/value attributes.
 *
 * Unlike \ref collect_oiio_attributes, this API fails when unsafe payloads are
 * encountered (for example raw bytes or invalid/unsafe text sequences).
 */
InteropSafetyStatus
collect_oiio_attributes_safe(const MetaStore& store,
                             std::vector<OiioAttribute>* out,
                             const OiioAdapterOptions& options,
                             InteropSafetyError* error) noexcept;

/**
 * \brief Collects OIIO-style typed attributes from a \ref MetaStore.
 *
 */
void
collect_oiio_attributes_typed(const MetaStore& store,
                              std::vector<OiioTypedAttribute>* out,
                              const OiioAdapterOptions& options) noexcept;

/**
 * \brief Strict safe export for OIIO-style typed attributes.
 *
 * Text values are validated and normalized to UTF-8 with
 * \ref TextEncoding::Utf8. Raw bytes payloads are rejected.
 */
InteropSafetyStatus
collect_oiio_attributes_typed_safe(const MetaStore& store,
                                   std::vector<OiioTypedAttribute>* out,
                                   const OiioAdapterOptions& options,
                                   InteropSafetyError* error) noexcept;

/**
 * \brief Converts \ref OiioAdapterRequest into \ref OiioAdapterOptions.
 */
OiioAdapterOptions
make_oiio_adapter_options(const OiioAdapterRequest& request) noexcept;

/**
 * \brief Collects OIIO-style attributes via the stable request model.
 */
void
collect_oiio_attributes(const MetaStore& store, std::vector<OiioAttribute>* out,
                        const OiioAdapterRequest& request) noexcept;

/**
 * \brief Request-based strict safe export for OIIO-style name/value attributes.
 */
InteropSafetyStatus
collect_oiio_attributes_safe(const MetaStore& store,
                             std::vector<OiioAttribute>* out,
                             const OiioAdapterRequest& request,
                             InteropSafetyError* error) noexcept;

/**
 * \brief Collects OIIO-style typed attributes via the stable request model.
 */
void
collect_oiio_attributes_typed(const MetaStore& store,
                              std::vector<OiioTypedAttribute>* out,
                              const OiioAdapterRequest& request) noexcept;

/**
 * \brief Request-based strict safe export for OIIO-style typed attributes.
 */
InteropSafetyStatus
collect_oiio_attributes_typed_safe(const MetaStore& store,
                                   std::vector<OiioTypedAttribute>* out,
                                   const OiioAdapterRequest& request,
                                   InteropSafetyError* error) noexcept;

}  // namespace openmeta
