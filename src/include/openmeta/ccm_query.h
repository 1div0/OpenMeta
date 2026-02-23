#pragma once

#include "openmeta/meta_store.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * \file ccm_query.h
 * \brief Query helpers for normalized DNG/RAW color matrix metadata.
 */

namespace openmeta {

/// Query status for \ref collect_dng_ccm_fields.
enum class CcmQueryStatus : uint8_t {
    Ok,
    /// Input was valid but query limits were exceeded.
    LimitExceeded,
};

/// Limits for CCM query extraction.
struct CcmQueryLimits final {
    uint32_t max_fields           = 128U;
    uint32_t max_values_per_field = 256U;
};

/// Options for \ref collect_dng_ccm_fields.
struct CcmQueryOptions final {
    /// Require DNG context marker (`DNGVersion`) in the source IFD.
    bool require_dng_context = true;
    /// Include `ReductionMatrix*` tags.
    bool include_reduction_matrices = true;
    CcmQueryLimits limits;
};

/// One normalized CCM/white-balance/calibration field.
struct CcmField final {
    /// Canonical field name (e.g. `ColorMatrix1`).
    std::string name;
    /// Source EXIF IFD token (e.g. `ifd0`).
    std::string ifd;
    /// Source EXIF tag id.
    uint16_t tag = 0;
    /// Logical row count (matrix/vector layout hint).
    uint32_t rows = 0;
    /// Logical column count (matrix/vector layout hint).
    uint32_t cols = 0;
    /// Normalized numeric values (row-major for matrices).
    std::vector<double> values;
};

/// Query result for \ref collect_dng_ccm_fields.
struct CcmQueryResult final {
    CcmQueryStatus status = CcmQueryStatus::Ok;
    uint32_t fields_found = 0;
};

/**
 * \brief Extracts normalized DNG/RAW CCM-related fields from a \ref MetaStore.
 *
 * The query scans EXIF tags and emits stable field names for:
 * - `ColorMatrix*`, `ForwardMatrix*`, `CameraCalibration*`
 * - `ReductionMatrix*` (optional)
 * - `AsShotNeutral`, `AsShotWhiteXY`, `AnalogBalance`
 * - `CalibrationIlluminant*`
 */
CcmQueryResult
collect_dng_ccm_fields(const MetaStore& store, std::vector<CcmField>* out,
                       const CcmQueryOptions& options
                       = CcmQueryOptions {}) noexcept;

}  // namespace openmeta

