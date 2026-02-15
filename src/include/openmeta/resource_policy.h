#pragma once

#include "openmeta/container_payload.h"
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/exr_decode.h"
#include "openmeta/icc_decode.h"
#include "openmeta/iptc_iim_decode.h"
#include "openmeta/jumbf_decode.h"
#include "openmeta/photoshop_irb_decode.h"
#include "openmeta/preview_extract.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

#include <cstdint>

/**
 * \file resource_policy.h
 * \brief Draft resource-budget policy for OpenMeta read/dump workflows.
 */

namespace openmeta {

/**
 * \brief Draft, storage-agnostic resource limits for untrusted metadata input.
 *
 * This policy intentionally favors parser/output budgets over hard file-size
 * caps, so large legitimate assets (for example RAW/EXR) can still be
 * processed when decode limits are respected.
 */
struct OpenMetaResourcePolicy final {
    /// Optional file mapping cap (0 = unlimited).
    uint64_t max_file_bytes = 0;

    /// Reassembly/decompression budgets.
    PayloadLimits payload_limits;

    /// EXIF/TIFF decode budgets.
    ExifDecodeLimits exif_limits;

    /// XMP RDF/XML decode budgets.
    XmpDecodeLimits xmp_limits;

    /// OpenEXR header decode budgets.
    ExrDecodeLimits exr_limits;

    /// JUMBF/C2PA decode budgets.
    JumbfDecodeLimits jumbf_limits;

    /// ICC profile decode budgets.
    IccDecodeLimits icc_limits;

    /// IPTC-IIM decode budgets.
    IptcIimDecodeLimits iptc_limits;

    /// Photoshop IRB decode budgets.
    PhotoshopIrbDecodeLimits photoshop_irb_limits;

    /// Embedded preview discovery/extraction budgets.
    PreviewScanLimits preview_scan_limits;
    uint64_t max_preview_output_bytes = 128ULL * 1024ULL * 1024ULL;

    /// XMP sidecar dump budgets.
    XmpDumpLimits xmp_dump_limits;

    /// Draft future budget hooks (not enforced yet).
    uint32_t max_decode_millis           = 0;
    uint32_t max_decompression_ratio     = 0;
    uint64_t max_total_decode_work_bytes = 0;
};

inline void
apply_resource_policy(const OpenMetaResourcePolicy& policy,
                      ExifDecodeOptions* exif, PayloadOptions* payload) noexcept
{
    if (exif) {
        exif->limits = policy.exif_limits;
    }
    if (payload) {
        payload->limits = policy.payload_limits;
    }
}

inline void
apply_resource_policy(const OpenMetaResourcePolicy& policy,
                      XmpDecodeOptions* xmp, ExrDecodeOptions* exr,
                      JumbfDecodeOptions* jumbf, IccDecodeOptions* icc,
                      IptcIimDecodeOptions* iptc,
                      PhotoshopIrbDecodeOptions* irb) noexcept
{
    if (xmp) {
        xmp->limits = policy.xmp_limits;
    }
    if (exr) {
        exr->limits = policy.exr_limits;
    }
    if (jumbf) {
        jumbf->limits = policy.jumbf_limits;
    }
    if (icc) {
        icc->limits = policy.icc_limits;
    }
    if (iptc) {
        iptc->limits = policy.iptc_limits;
    }
    if (irb) {
        irb->limits      = policy.photoshop_irb_limits;
        irb->iptc.limits = policy.iptc_limits;
    }
}

inline void
apply_resource_policy(const OpenMetaResourcePolicy& policy,
                      XmpDecodeOptions* xmp, ExrDecodeOptions* exr,
                      IccDecodeOptions* icc, IptcIimDecodeOptions* iptc,
                      PhotoshopIrbDecodeOptions* irb) noexcept
{
    apply_resource_policy(policy, xmp, exr, nullptr, icc, iptc, irb);
}

inline void
apply_resource_policy(const OpenMetaResourcePolicy& policy,
                      PreviewScanOptions* scan,
                      PreviewExtractOptions* extract) noexcept
{
    if (scan) {
        scan->limits = policy.preview_scan_limits;
    }
    if (extract) {
        extract->max_output_bytes = policy.max_preview_output_bytes;
    }
}

inline void
apply_resource_policy(const OpenMetaResourcePolicy& policy,
                      XmpSidecarRequest* request) noexcept
{
    if (request) {
        request->limits = policy.xmp_dump_limits;
    }
}

}  // namespace openmeta
