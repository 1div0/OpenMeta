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

    bool has_numeric      = false;
    uint8_t numeric_count = 0U;
    double numeric[4] {};

    std::string text;
    /// Normalized value used for same-role conflict checks.
    std::string value_key;

    bool has_date_time               = false;
    bool date_time_has_time          = false;
    bool date_time_has_utc_offset    = false;
    int16_t date_time_year           = 0;
    uint8_t date_time_month          = 0U;
    uint8_t date_time_day            = 0U;
    uint8_t date_time_hour           = 0U;
    uint8_t date_time_minute         = 0U;
    uint8_t date_time_second         = 0U;
    int16_t date_time_utc_offset_min = 0;
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

}  // namespace openmeta
