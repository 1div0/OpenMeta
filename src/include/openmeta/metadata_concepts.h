// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "openmeta/meta_store.h"
#include "openmeta/metadata_query.h"

#include <cstdint>
#include <string>
#include <vector>

/**
 * \file metadata_concepts.h
 * \brief Experimental cross-family metadata concept resolution.
 */

namespace openmeta {

enum class MetadataConceptKind : uint8_t {
    Orientation,
    DateTime,
    ColorProfile,
    Gps,
    Geometry,
    LensCorrection,
    RawProcessing,
    Exposure,
};

enum class MetadataConceptSourceFamily : uint8_t {
    Unknown,
    Exif,
    Xmp,
    Iptc,
    Icc,
    PngText,
    InterpretationRecord,
};

enum class MetadataConceptRole : uint8_t {
    Primary,
    Orientation,
    Created,
    Digitized,
    Modified,
    MetadataDate,
    DateCreated,
    ColorSpace,
    IccProfile,
    ColorMatrix,
    WhiteBalance,
    Latitude,
    Longitude,
    Altitude,
    Timestamp,
    Crop,
    ActiveArea,
    Border,
    SensorGeometry,
    LensCorrection,
    BlackLevel,
    WhiteLevel,
    Linearization,
    CfaLayout,
    RawStorage,
    SourceProcessing,
    ExposureTime,
    Aperture,
    IsoSensitivity,
    ExposureBias,
    ExposureProgram,
    Gain,
    RawExposureAdjustment,
};

enum class MetadataConceptDateTimePrecision : uint8_t {
    Unknown,
    Date,
    DateTime,
};

enum class MetadataConceptTimeZoneKind : uint8_t {
    Unknown,
    Local,
    Utc,
    Offset,
};

enum class MetadataConceptTransferHint : uint8_t {
    Unknown,
    Safe,
    SourceBound,
    RenderedUnsafe,
    RequiresTargetImageSpec,
};

struct MetadataConceptCandidate final {
    MetadataConceptKind kind           = MetadataConceptKind::Orientation;
    MetadataConceptRole role           = MetadataConceptRole::Primary;
    MetadataConceptSourceFamily family = MetadataConceptSourceFamily::Unknown;
    MetadataQuerySemanticKind semantic = MetadataQuerySemanticKind::Unknown;
    MetadataQueryValueShape shape      = MetadataQueryValueShape::Unknown;
    EntryId entry_id                   = kInvalidEntryId;
    std::vector<EntryId> source_entries;
    uint8_t priority = 0U;
    bool preferred   = false;
    bool conflict    = false;

    MetadataConceptTransferHint transfer_hint
        = MetadataConceptTransferHint::Unknown;
    bool compatible_file_safe       = false;
    bool rendered_image_safe        = false;
    bool requires_target_image_spec = false;
    bool source_bound               = false;

    bool has_numeric      = false;
    uint8_t numeric_count = 0U;
    double numeric[4] {};

    bool has_values = false;
    std::vector<double> values;

    bool has_origin = false;
    double origin[2] {};

    bool has_size = false;
    double size[2] {};

    bool has_rect = false;
    /// Rect is normalized as x, y, width, height.
    double rect[4] {};

    bool has_margins = false;
    /// Margins are normalized as left, top, right, bottom.
    double margins[4] {};

    std::string text;
    /// Normalized value used for same-role conflict checks.
    std::string value_key;

    bool has_date_time            = false;
    bool date_time_has_time       = false;
    bool date_time_has_utc_offset = false;
    MetadataConceptDateTimePrecision date_time_precision
        = MetadataConceptDateTimePrecision::Unknown;
    MetadataConceptTimeZoneKind date_time_zone
        = MetadataConceptTimeZoneKind::Unknown;
    int16_t date_time_year           = 0;
    uint8_t date_time_month          = 0U;
    uint8_t date_time_day            = 0U;
    uint8_t date_time_hour           = 0U;
    uint8_t date_time_minute         = 0U;
    uint8_t date_time_second         = 0U;
    int16_t date_time_utc_offset_min = 0;

    bool has_gps_altitude_reference     = false;
    bool gps_altitude_below_sea_level   = false;
    uint8_t gps_altitude_reference_code = 0U;
};

struct MetadataConceptResolution final {
    MetadataConceptKind kind = MetadataConceptKind::Orientation;
    bool found               = false;
    bool conflict            = false;
    EntryId preferred_entry  = kInvalidEntryId;
    std::vector<EntryId> source_entries;
    std::vector<MetadataConceptCandidate> candidates;
};

struct MetadataConceptResult final {
    std::vector<MetadataConceptResolution> concepts;
};

MetadataConceptResolution
resolve_metadata_concept(const MetaStore& store, MetadataConceptKind kind);

MetadataConceptResult
resolve_metadata_concepts(const MetaStore& store);

const char*
metadata_concept_kind_name(MetadataConceptKind kind) noexcept;

const char*
metadata_concept_source_family_name(MetadataConceptSourceFamily family) noexcept;

const char*
metadata_concept_role_name(MetadataConceptRole role) noexcept;

const char*
metadata_concept_datetime_precision_name(
    MetadataConceptDateTimePrecision precision) noexcept;

const char*
metadata_concept_timezone_kind_name(MetadataConceptTimeZoneKind kind) noexcept;

const char*
metadata_concept_transfer_hint_name(MetadataConceptTransferHint hint) noexcept;

const char*
metadata_concept_gps_altitude_reference_name(uint8_t code) noexcept;

}  // namespace openmeta
