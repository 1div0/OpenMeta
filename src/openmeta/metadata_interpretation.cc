// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_interpretation.h"

#include <cstddef>

namespace openmeta {
namespace {

    static void copy_pair_if_present(bool present, const double* src,
                                     bool* dst_present, double* dst) noexcept
    {
        if (!dst_present || !dst) {
            return;
        }
        *dst_present = present;
        if (!present || !src) {
            return;
        }
        dst[0] = src[0];
        dst[1] = src[1];
    }

    static void copy_quad_if_present(bool present, const double* src,
                                     bool* dst_present, double* dst) noexcept
    {
        if (!dst_present || !dst) {
            return;
        }
        *dst_present = present;
        if (!present || !src) {
            return;
        }
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
    }

    static MetadataInterpretationRecord
    make_record(MetadataQueryKind kind, const MetadataQueryCandidate& candidate)
    {
        MetadataInterpretationRecord out;
        out.query_kind     = kind;
        out.semantic       = candidate.semantic;
        out.shape          = candidate.normalized_shape;
        out.confidence     = candidate.confidence;
        out.source_entries = candidate.source_entries;

        copy_pair_if_present(candidate.has_origin, candidate.origin,
                             &out.has_origin, out.origin);
        copy_pair_if_present(candidate.has_size, candidate.size, &out.has_size,
                             out.size);
        copy_quad_if_present(candidate.has_rect, candidate.rect, &out.has_rect,
                             out.rect);
        copy_quad_if_present(candidate.has_margins, candidate.margins,
                             &out.has_margins, out.margins);

        out.has_values = candidate.has_values;
        if (candidate.has_values) {
            out.values = candidate.values;
        }
        return out;
    }

    static void append_query_records(MetadataQueryResult* query,
                                     MetadataInterpretationResult* out)
    {
        if (!query || !out) {
            return;
        }
        out->records.reserve(out->records.size() + query->candidates.size());
        for (size_t i = 0U; i < query->candidates.size(); ++i) {
            const MetadataQueryCandidate& candidate = query->candidates[i];
            if (candidate.semantic == MetadataQuerySemanticKind::Unknown) {
                continue;
            }
            if (candidate.source_entries.empty()) {
                continue;
            }
            out->records.push_back(make_record(query->kind, candidate));
        }
    }

    static void append_kind_records(const MetaStore& store,
                                    MetadataQueryKind kind,
                                    MetadataInterpretationResult* out)
    {
        MetadataQueryResult query = query_metadata(store, kind);
        append_query_records(&query, out);
    }

}  // namespace

MetadataInterpretationResult
interpret_metadata_query(const MetaStore& store, MetadataQueryKind kind)
{
    MetadataInterpretationResult out;
    append_kind_records(store, kind, &out);
    return out;
}

MetadataInterpretationResult
interpret_metadata(const MetaStore& store)
{
    MetadataInterpretationResult out;
    append_kind_records(store, MetadataQueryKind::Crop, &out);
    append_kind_records(store, MetadataQueryKind::Orientation, &out);
    append_kind_records(store, MetadataQueryKind::ExposureGain, &out);
    append_kind_records(store, MetadataQueryKind::WhiteBalance, &out);
    append_kind_records(store, MetadataQueryKind::Color, &out);
    append_kind_records(store, MetadataQueryKind::LensCorrection, &out);
    append_kind_records(store, MetadataQueryKind::RawProcessing, &out);
    return out;
}

}  // namespace openmeta
