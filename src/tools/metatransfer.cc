#include "cli_parse.h"
#include "openmeta/build_info.h"
#include "openmeta/mapped_file.h"
#include "openmeta/metadata_transfer.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    struct PendingTimePatch final {
        TimePatchField field = TimePatchField::DateTime;
        std::string value;
    };

    static void usage(const char* argv0)
    {
        std::printf(
            "Usage: %s [options] <file> [file...]\n"
            "\n"
            "Transfer smoke tool (thin wrapper):\n"
            "  read/decode -> prepare_metadata_for_target_file -> execute_prepared_transfer_file\n"
            "\n"
            "Options:\n"
            "  --help                 Show this help\n"
            "  --version              Print OpenMeta build info\n"
            "  --no-build-info        Hide build info header\n"
            "  -i, --input <path>     Input file (repeatable)\n"
            "  --format <portable|lossless>\n"
            "                         XMP mode for transfer-prepared APP1 XMP\n"
            "  --portable             Alias for --format portable\n"
            "  --lossless             Alias for --format lossless\n"
            "  --xmp-include-existing Include existing decoded XMP in generated XMP\n"
            "  --xmp-exiftool-gpsdatetime-alias\n"
            "                         Emit exif:GPSDateTime alias in portable mode\n"
            "  --no-exif              Skip EXIF APP1 preparation\n"
            "  --no-xmp               Skip XMP APP1 preparation\n"
            "  --no-icc               Skip ICC APP2 preparation\n"
            "  --no-iptc              Skip IPTC APP13 preparation\n"
            "  --makernotes           Enable best-effort MakerNote decode in read phase\n"
            "  --makernote-policy <keep|drop|invalidate|rewrite>\n"
            "                         Transfer policy for MakerNote payloads\n"
            "  --jumbf-policy <keep|drop|invalidate|rewrite>\n"
            "                         Transfer policy for JUMBF payloads\n"
            "  --c2pa-policy <keep|drop|invalidate|rewrite>\n"
            "                         Transfer policy for C2PA payloads\n"
            "  --jpeg-jumbf <path>   Append one logical raw JUMBF payload to a JPEG bundle\n"
            "  --replace-jpeg-jumbf  Remove prepared jpeg:app11-jumbf blocks before append\n"
            "  --jpeg-c2pa-signed <path>\n"
            "                         Stage one externally signed logical C2PA payload\n"
            "  --c2pa-manifest-output <path>\n"
            "                         External manifest-builder output bytes for signed C2PA staging\n"
            "  --c2pa-certificate-chain <path>\n"
            "                         External certificate chain bytes for signed C2PA staging\n"
            "  --c2pa-key-ref <text> Private-key reference string for signed C2PA staging\n"
            "  --c2pa-signing-time <text>\n"
            "                         Signing time for signed C2PA staging\n"
            "  --dump-c2pa-binding <path>\n"
            "                         Write exact C2PA content-binding bytes for external signing\n"
            "  --dump-c2pa-handoff <path>\n"
            "                         Write one persisted C2PA handoff package\n"
            "  --dump-c2pa-signed-package <path>\n"
            "                         Write one persisted signed C2PA package from current signer inputs\n"
            "  --load-c2pa-signed-package <path>\n"
            "                         Load one persisted signed C2PA package for staging\n"
            "  --dump-transfer-payload-batch <path>\n"
            "                         Write one persisted semantic transfer payload batch\n"
            "  --load-transfer-payload-batch <path>\n"
            "                         Load and inspect one persisted semantic transfer payload batch\n"
            "  --no-decompress        Disable payload decompression in read phase\n"
            "  --unsafe-write-payloads\n"
            "                         Write prepared raw block payload bytes to files\n"
            "  --write-payloads       Deprecated alias for --unsafe-write-payloads\n"
            "  --out-dir <dir>        Output directory for --write-payloads\n"
            "  --source-meta <path>   Metadata source for prepare phase\n"
            "  --target-jpeg <path>   Target JPEG stream for edit/apply phase\n"
            "  --target-tiff <path>   Target TIFF stream for edit/apply phase\n"
            "  --target-jxl           Target JPEG XL metadata emit summary\n"
            "  --target-webp          Target WebP metadata chunk emit summary\n"
            "  --target-heif          Target HEIF metadata item emit summary\n"
            "  --target-avif          Target AVIF metadata item emit summary\n"
            "  --target-cr3           Target CR3 metadata item emit summary\n"
            "  -o, --output <path>    Write edited output file\n"
            "  --force                Overwrite existing payload files\n"
            "  --dry-run              Plan edit only; do not write output\n"
            "  --mode <auto|in_place|metadata_rewrite>\n"
            "                         JPEG-only edit mode selection for output writer\n"
            "  --require-in-place     Fail if selected plan is not in_place\n"
            "  --emit-repeat N        Emit same prepared bundle N times (default: 1)\n"
            "  --time-patch F=V       Apply time patch update (repeatable)\n"
            "                         F: DateTime|DateTimeOriginal|DateTimeDigitized|\n"
            "                            SubSecTime|SubSecTimeOriginal|SubSecTimeDigitized|\n"
            "                            OffsetTime|OffsetTimeOriginal|OffsetTimeDigitized|\n"
            "                            GpsDateStamp|GpsTimeStamp\n"
            "  --time-patch-lax-width Disable strict patch width checks\n"
            "  --time-patch-require-slot\n"
            "                         Fail if requested patch field is not present\n"
            "  --time-patch-no-auto-nul\n"
            "                         Disable auto NUL append for text patch values\n"
            "  --max-file-bytes N     Optional mapped file cap in bytes (0=unlimited)\n"
            "  --max-payload-bytes N  Max reassembled/decompressed payload bytes\n"
            "  --max-payload-parts N  Max payload part count\n"
            "  --max-exif-ifds N      Max EXIF/TIFF IFD count\n"
            "  --max-exif-entries N   Max EXIF/TIFF entries per IFD\n"
            "  --max-exif-total N     Max EXIF/TIFF total entries\n",
            argv0 ? argv0 : "metatransfer");
    }


    static std::string canonical_time_patch_name(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (std::isalnum(c) == 0) {
                continue;
            }
            const int lower = std::tolower(c);
            out.push_back(static_cast<char>(lower));
        }
        return out;
    }


    static bool parse_time_patch_field(std::string_view name,
                                       TimePatchField* out)
    {
        if (!out) {
            return false;
        }
        const std::string n = canonical_time_patch_name(name);
        if (n == "datetime") {
            *out = TimePatchField::DateTime;
            return true;
        }
        if (n == "datetimeoriginal") {
            *out = TimePatchField::DateTimeOriginal;
            return true;
        }
        if (n == "datetimedigitized") {
            *out = TimePatchField::DateTimeDigitized;
            return true;
        }
        if (n == "subsectime") {
            *out = TimePatchField::SubSecTime;
            return true;
        }
        if (n == "subsectimeoriginal") {
            *out = TimePatchField::SubSecTimeOriginal;
            return true;
        }
        if (n == "subsectimedigitized") {
            *out = TimePatchField::SubSecTimeDigitized;
            return true;
        }
        if (n == "offsettime") {
            *out = TimePatchField::OffsetTime;
            return true;
        }
        if (n == "offsettimeoriginal") {
            *out = TimePatchField::OffsetTimeOriginal;
            return true;
        }
        if (n == "offsettimedigitized") {
            *out = TimePatchField::OffsetTimeDigitized;
            return true;
        }
        if (n == "gpsdatestamp") {
            *out = TimePatchField::GpsDateStamp;
            return true;
        }
        if (n == "gpstimestamp") {
            *out = TimePatchField::GpsTimeStamp;
            return true;
        }
        return false;
    }


    static bool parse_time_patch_arg(const char* arg, PendingTimePatch* out,
                                     std::string* err)
    {
        if (!arg || !out) {
            if (err) {
                *err = "invalid --time-patch argument";
            }
            return false;
        }
        const std::string_view s(arg);
        const size_t eq = s.find('=');
        if (eq == std::string_view::npos || eq == 0U) {
            if (err) {
                *err = "expected --time-patch Field=Value";
            }
            return false;
        }

        const std::string_view f = s.substr(0U, eq);
        const std::string_view v = s.substr(eq + 1U);
        TimePatchField field     = TimePatchField::DateTime;
        if (!parse_time_patch_field(f, &field)) {
            if (err) {
                *err = "unknown --time-patch field";
            }
            return false;
        }

        out->field = field;
        out->value.assign(v.data(), v.size());
        return true;
    }

    static std::vector<TransferTimePatchInput> build_transfer_time_patch_inputs(
        const std::vector<PendingTimePatch>& pending)
    {
        std::vector<TransferTimePatchInput> updates;
        updates.reserve(pending.size());
        for (size_t i = 0; i < pending.size(); ++i) {
            TransferTimePatchInput one;
            one.field      = pending[i].field;
            one.text_value = true;
            one.value.resize(pending[i].value.size());
            for (size_t bi = 0; bi < pending[i].value.size(); ++bi) {
                const unsigned char c = static_cast<unsigned char>(
                    pending[i].value[bi]);
                one.value[bi] = static_cast<std::byte>(c);
            }
            updates.push_back(std::move(one));
        }
        return updates;
    }


    static bool parse_u64_arg(const char* s, uint64_t* out)
    {
        return tool_parse::parse_decimal_u64(s, out);
    }


    static bool parse_u32_arg(const char* s, uint32_t* out)
    {
        return tool_parse::parse_decimal_u32(s, out);
    }

    static const char* jpeg_edit_mode_name(JpegEditMode mode) noexcept
    {
        switch (mode) {
        case JpegEditMode::Auto: return "auto";
        case JpegEditMode::InPlace: return "in_place";
        case JpegEditMode::MetadataRewrite: return "metadata_rewrite";
        }
        return "unknown";
    }

    static bool parse_jpeg_edit_mode(const char* s, JpegEditMode* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "auto") == 0) {
            *out = JpegEditMode::Auto;
            return true;
        }
        if (std::strcmp(s, "in_place") == 0) {
            *out = JpegEditMode::InPlace;
            return true;
        }
        if (std::strcmp(s, "metadata_rewrite") == 0) {
            *out = JpegEditMode::MetadataRewrite;
            return true;
        }
        return false;
    }


    static bool parse_transfer_policy_action(const char* s,
                                             TransferPolicyAction* out) noexcept
    {
        if (!s || !out) {
            return false;
        }
        if (std::strcmp(s, "keep") == 0) {
            *out = TransferPolicyAction::Keep;
            return true;
        }
        if (std::strcmp(s, "drop") == 0) {
            *out = TransferPolicyAction::Drop;
            return true;
        }
        if (std::strcmp(s, "invalidate") == 0) {
            *out = TransferPolicyAction::Invalidate;
            return true;
        }
        if (std::strcmp(s, "rewrite") == 0) {
            *out = TransferPolicyAction::Rewrite;
            return true;
        }
        return false;
    }


    static const char*
    transfer_policy_subject_name(TransferPolicySubject subject) noexcept
    {
        switch (subject) {
        case TransferPolicySubject::MakerNote: return "makernote";
        case TransferPolicySubject::Jumbf: return "jumbf";
        case TransferPolicySubject::C2pa: return "c2pa";
        }
        return "unknown";
    }


    static const char*
    transfer_policy_action_name(TransferPolicyAction action) noexcept
    {
        switch (action) {
        case TransferPolicyAction::Keep: return "keep";
        case TransferPolicyAction::Drop: return "drop";
        case TransferPolicyAction::Invalidate: return "invalidate";
        case TransferPolicyAction::Rewrite: return "rewrite";
        }
        return "unknown";
    }


    static const char*
    transfer_policy_reason_name(TransferPolicyReason reason) noexcept
    {
        switch (reason) {
        case TransferPolicyReason::Default: return "default";
        case TransferPolicyReason::NotPresent: return "not_present";
        case TransferPolicyReason::ExplicitDrop: return "explicit_drop";
        case TransferPolicyReason::CarrierDisabled: return "carrier_disabled";
        case TransferPolicyReason::ProjectedPayload: return "projected_payload";
        case TransferPolicyReason::DraftInvalidationPayload:
            return "draft_invalidation_payload";
        case TransferPolicyReason::ExternalSignedPayload:
            return "external_signed_payload";
        case TransferPolicyReason::ContentBoundTransferUnavailable:
            return "content_bound_transfer_unavailable";
        case TransferPolicyReason::SignedRewriteUnavailable:
            return "signed_rewrite_unavailable";
        case TransferPolicyReason::PortableInvalidationUnavailable:
            return "portable_invalidation_unavailable";
        case TransferPolicyReason::RewriteUnavailablePreservedRaw:
            return "rewrite_unavailable_preserved_raw";
        case TransferPolicyReason::TargetSerializationUnavailable:
            return "target_serialization_unavailable";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_mode_name(TransferC2paMode mode) noexcept
    {
        switch (mode) {
        case TransferC2paMode::NotApplicable: return "not_applicable";
        case TransferC2paMode::NotPresent: return "not_present";
        case TransferC2paMode::Drop: return "drop";
        case TransferC2paMode::DraftUnsignedInvalidation:
            return "draft_unsigned_invalidation";
        case TransferC2paMode::PreserveRaw: return "preserve_raw";
        case TransferC2paMode::SignedRewrite: return "signed_rewrite";
        }
        return "unknown";
    }

    static const char*
    transfer_c2pa_source_kind_name(TransferC2paSourceKind kind) noexcept
    {
        switch (kind) {
        case TransferC2paSourceKind::NotApplicable: return "not_applicable";
        case TransferC2paSourceKind::NotPresent: return "not_present";
        case TransferC2paSourceKind::DecodedOnly: return "decoded_only";
        case TransferC2paSourceKind::ContentBound: return "content_bound";
        case TransferC2paSourceKind::DraftUnsignedInvalidation:
            return "draft_unsigned_invalidation";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_prepared_output_name(
        TransferC2paPreparedOutput output) noexcept
    {
        switch (output) {
        case TransferC2paPreparedOutput::NotApplicable: return "not_applicable";
        case TransferC2paPreparedOutput::NotPresent: return "not_present";
        case TransferC2paPreparedOutput::Dropped: return "dropped";
        case TransferC2paPreparedOutput::PreservedRaw: return "preserved_raw";
        case TransferC2paPreparedOutput::GeneratedDraftUnsignedInvalidation:
            return "generated_draft_unsigned_invalidation";
        case TransferC2paPreparedOutput::SignedRewrite: return "signed_rewrite";
        }
        return "unknown";
    }

    static const char*
    transfer_c2pa_rewrite_state_name(TransferC2paRewriteState state) noexcept
    {
        switch (state) {
        case TransferC2paRewriteState::NotApplicable: return "not_applicable";
        case TransferC2paRewriteState::NotRequested: return "not_requested";
        case TransferC2paRewriteState::SigningMaterialRequired:
            return "signing_material_required";
        case TransferC2paRewriteState::Ready: return "ready";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_rewrite_chunk_kind_name(
        TransferC2paRewriteChunkKind kind) noexcept
    {
        switch (kind) {
        case TransferC2paRewriteChunkKind::SourceRange: return "source_range";
        case TransferC2paRewriteChunkKind::PreparedJpegSegment:
            return "prepared_jpeg_segment";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_signed_payload_kind_name(
        TransferC2paSignedPayloadKind kind) noexcept
    {
        switch (kind) {
        case TransferC2paSignedPayloadKind::NotApplicable:
            return "not_applicable";
        case TransferC2paSignedPayloadKind::GenericJumbf:
            return "generic_jumbf";
        case TransferC2paSignedPayloadKind::DraftUnsignedInvalidation:
            return "draft_unsigned_invalidation";
        case TransferC2paSignedPayloadKind::ContentBound:
            return "content_bound";
        }
        return "unknown";
    }

    static const char* transfer_c2pa_semantic_status_name(
        TransferC2paSemanticStatus status) noexcept
    {
        switch (status) {
        case TransferC2paSemanticStatus::NotChecked: return "not_checked";
        case TransferC2paSemanticStatus::Ok: return "ok";
        case TransferC2paSemanticStatus::Invalid: return "invalid";
        }
        return "unknown";
    }

    static const char*
    transfer_target_format_name(TransferTargetFormat format) noexcept
    {
        switch (format) {
        case TransferTargetFormat::Jpeg: return "jpeg";
        case TransferTargetFormat::Tiff: return "tiff";
        case TransferTargetFormat::Jxl: return "jxl";
        case TransferTargetFormat::Webp: return "webp";
        case TransferTargetFormat::Heif: return "heif";
        case TransferTargetFormat::Avif: return "avif";
        case TransferTargetFormat::Cr3: return "cr3";
        case TransferTargetFormat::Exr: return "exr";
        }
        return "unknown";
    }

    static const char*
    transfer_adapter_op_kind_name(TransferAdapterOpKind kind) noexcept
    {
        switch (kind) {
        case TransferAdapterOpKind::JpegMarker: return "jpeg_marker";
        case TransferAdapterOpKind::TiffTagBytes: return "tiff_tag";
        case TransferAdapterOpKind::JxlBox: return "jxl_box";
        case TransferAdapterOpKind::JxlIccProfile: return "jxl_icc_profile";
        case TransferAdapterOpKind::WebpChunk: return "webp_chunk";
        case TransferAdapterOpKind::BmffItem: return "bmff_item";
        case TransferAdapterOpKind::BmffProperty: return "bmff_property";
        }
        return "unknown";
    }

    static void print_build_info_header()
    {
        std::string line1;
        std::string line2;
        format_build_info_lines(&line1, &line2);
        std::printf("%s\n%s\n", line1.c_str(), line2.c_str());
    }


    static const char* transfer_status_name(TransferStatus status) noexcept
    {
        switch (status) {
        case TransferStatus::Ok: return "ok";
        case TransferStatus::InvalidArgument: return "invalid_argument";
        case TransferStatus::Unsupported: return "unsupported";
        case TransferStatus::LimitExceeded: return "limit_exceeded";
        case TransferStatus::Malformed: return "malformed";
        case TransferStatus::UnsafeData: return "unsafe_data";
        case TransferStatus::InternalError: return "internal_error";
        }
        return "unknown";
    }


    static const char*
    transfer_file_status_name(TransferFileStatus status) noexcept
    {
        switch (status) {
        case TransferFileStatus::Ok: return "ok";
        case TransferFileStatus::InvalidArgument: return "invalid_argument";
        case TransferFileStatus::OpenFailed: return "open_failed";
        case TransferFileStatus::StatFailed: return "stat_failed";
        case TransferFileStatus::TooLarge: return "too_large";
        case TransferFileStatus::MapFailed: return "map_failed";
        case TransferFileStatus::ReadFailed: return "read_failed";
        }
        return "unknown";
    }

    static const char*
    prepare_transfer_code_name(PrepareTransferCode code) noexcept
    {
        switch (code) {
        case PrepareTransferCode::None: return "none";
        case PrepareTransferCode::NullOutBundle: return "null_out_bundle";
        case PrepareTransferCode::UnsupportedTargetFormat:
            return "unsupported_target_format";
        case PrepareTransferCode::ExifPackFailed: return "exif_pack_failed";
        case PrepareTransferCode::XmpPackFailed: return "xmp_pack_failed";
        case PrepareTransferCode::IccPackFailed: return "icc_pack_failed";
        case PrepareTransferCode::IptcPackFailed: return "iptc_pack_failed";
        case PrepareTransferCode::RequestedMetadataNotSerializable:
            return "requested_metadata_not_serializable";
        }
        return "unknown";
    }

    static const char* emit_transfer_code_name(EmitTransferCode code) noexcept
    {
        switch (code) {
        case EmitTransferCode::None: return "none";
        case EmitTransferCode::InvalidArgument: return "invalid_argument";
        case EmitTransferCode::BundleTargetNotJpeg:
            return "bundle_target_not_jpeg";
        case EmitTransferCode::UnsupportedRoute: return "unsupported_route";
        case EmitTransferCode::InvalidPayload: return "invalid_payload";
        case EmitTransferCode::ContentBoundPayloadUnsupported:
            return "content_bound_payload_unsupported";
        case EmitTransferCode::BackendWriteFailed:
            return "backend_write_failed";
        case EmitTransferCode::PlanMismatch: return "plan_mismatch";
        }
        return "unknown";
    }

    static const char*
    prepare_transfer_file_code_name(PrepareTransferFileCode code) noexcept
    {
        switch (code) {
        case PrepareTransferFileCode::None: return "none";
        case PrepareTransferFileCode::EmptyPath: return "empty_path";
        case PrepareTransferFileCode::MapFailed: return "map_failed";
        case PrepareTransferFileCode::PayloadBufferPlatformLimit:
            return "payload_buffer_platform_limit";
        case PrepareTransferFileCode::DecodeFailed: return "decode_failed";
        }
        return "unknown";
    }


    static const char* transfer_kind_name(TransferBlockKind kind) noexcept
    {
        switch (kind) {
        case TransferBlockKind::Exif: return "exif";
        case TransferBlockKind::Xmp: return "xmp";
        case TransferBlockKind::IptcIim: return "iptc_iim";
        case TransferBlockKind::PhotoshopIrb: return "photoshop_irb";
        case TransferBlockKind::Icc: return "icc";
        case TransferBlockKind::Jumbf: return "jumbf";
        case TransferBlockKind::C2pa: return "c2pa";
        case TransferBlockKind::ExrAttribute: return "exr_attribute";
        case TransferBlockKind::Other: return "other";
        }
        return "unknown";
    }


    static const char* scan_status_name(ScanStatus status) noexcept
    {
        switch (status) {
        case ScanStatus::Ok: return "ok";
        case ScanStatus::OutputTruncated: return "output_truncated";
        case ScanStatus::Unsupported: return "unsupported";
        case ScanStatus::Malformed: return "malformed";
        }
        return "unknown";
    }


    static const char* payload_status_name(PayloadStatus status) noexcept
    {
        switch (status) {
        case PayloadStatus::Ok: return "ok";
        case PayloadStatus::OutputTruncated: return "output_truncated";
        case PayloadStatus::Unsupported: return "unsupported";
        case PayloadStatus::Malformed: return "malformed";
        case PayloadStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static const char* exif_status_name(ExifDecodeStatus status) noexcept
    {
        switch (status) {
        case ExifDecodeStatus::Ok: return "ok";
        case ExifDecodeStatus::OutputTruncated: return "output_truncated";
        case ExifDecodeStatus::Unsupported: return "unsupported";
        case ExifDecodeStatus::Malformed: return "malformed";
        case ExifDecodeStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static const char* xmp_status_name(XmpDecodeStatus status) noexcept
    {
        switch (status) {
        case XmpDecodeStatus::Ok: return "ok";
        case XmpDecodeStatus::OutputTruncated: return "output_truncated";
        case XmpDecodeStatus::Unsupported: return "unsupported";
        case XmpDecodeStatus::Malformed: return "malformed";
        case XmpDecodeStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static const char* jumbf_status_name(JumbfDecodeStatus status) noexcept
    {
        switch (status) {
        case JumbfDecodeStatus::Ok: return "ok";
        case JumbfDecodeStatus::Unsupported: return "unsupported";
        case JumbfDecodeStatus::Malformed: return "malformed";
        case JumbfDecodeStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static std::string basename_only(const std::string& path)
    {
        const size_t sep = path.find_last_of("/\\");
        if (sep == std::string::npos) {
            return path;
        }
        return path.substr(sep + 1U);
    }


    static std::string join_path(const std::string& dir,
                                 const std::string& name)
    {
        if (dir.empty()) {
            return name;
        }
        const char back = dir.back();
        if (back == '/' || back == '\\') {
            return dir + name;
        }
        return dir + "/" + name;
    }


    static std::string sanitize_filename(std::string s)
    {
        for (size_t i = 0; i < s.size(); ++i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            const bool ok         = std::isalnum(c) != 0 || c == '.' || c == '_'
                            || c == '-';
            if (!ok) {
                s[i] = '_';
            }
        }
        if (s.empty()) {
            return "file";
        }
        return s;
    }


    static std::string format_index(uint32_t idx)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%03u", idx);
        return std::string(buf);
    }


    static bool file_exists(const std::string& path)
    {
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        std::fclose(f);
        return true;
    }


    static bool write_file_bytes(const std::string& path,
                                 std::span<const std::byte> bytes)
    {
        std::FILE* f = std::fopen(path.c_str(), "wb");
        if (!f) {
            return false;
        }
        size_t written = 0;
        if (!bytes.empty()) {
            written = std::fwrite(bytes.data(), 1, bytes.size(), f);
        }
        std::fclose(f);
        return written == bytes.size();
    }


    static bool read_file_bytes(const std::string& path,
                                std::vector<std::byte>* out)
    {
        if (!out) {
            return false;
        }
        out->clear();
        std::FILE* f = std::fopen(path.c_str(), "rb");
        if (!f) {
            return false;
        }
        if (std::fseek(f, 0, SEEK_END) != 0) {
            std::fclose(f);
            return false;
        }
        const long end = std::ftell(f);
        if (end < 0) {
            std::fclose(f);
            return false;
        }
        if (std::fseek(f, 0, SEEK_SET) != 0) {
            std::fclose(f);
            return false;
        }
        out->resize(static_cast<size_t>(end));
        size_t read = 0U;
        if (!out->empty()) {
            read = std::fread(out->data(), 1, out->size(), f);
        }
        std::fclose(f);
        return read == out->size();
    }


    class FileByteWriter final : public TransferByteWriter {
    public:
        explicit FileByteWriter(std::string path)
            : path_(std::move(path))
        {
        }

        ~FileByteWriter() override { close_handle(); }

        TransferStatus write(std::span<const std::byte> bytes) noexcept override
        {
            if (bytes.empty()) {
                return TransferStatus::Ok;
            }
            if (!ensure_open()) {
                return status_;
            }
            const size_t written = std::fwrite(bytes.data(), 1, bytes.size(),
                                               file_);
            if (written != bytes.size()) {
                status_ = TransferStatus::InternalError;
                return status_;
            }
            bytes_written_ += static_cast<uint64_t>(written);
            return TransferStatus::Ok;
        }


        TransferStatus finish() noexcept
        {
            if (!file_) {
                return status_;
            }
            if (std::fclose(file_) != 0) {
                file_   = nullptr;
                status_ = TransferStatus::InternalError;
                return status_;
            }
            file_   = nullptr;
            status_ = TransferStatus::Ok;
            return status_;
        }


        uint64_t bytes_written() const noexcept { return bytes_written_; }

    private:
        bool ensure_open() noexcept
        {
            if (file_) {
                return true;
            }
            file_ = std::fopen(path_.c_str(), "wb");
            if (!file_) {
                status_ = TransferStatus::InternalError;
                return false;
            }
            return true;
        }


        void close_handle() noexcept
        {
            if (file_) {
                std::fclose(file_);
                file_ = nullptr;
            }
        }


        std::string path_;
        std::FILE* file_        = nullptr;
        TransferStatus status_  = TransferStatus::Ok;
        uint64_t bytes_written_ = 0;
    };


    static std::string payload_dump_path(const std::string& in_path,
                                         const std::string& out_dir,
                                         const PreparedTransferBlock& block,
                                         uint32_t idx)
    {
        std::string base  = sanitize_filename(basename_only(in_path));
        std::string route = sanitize_filename(block.route);
        std::string out   = base;
        out.append(".meta.");
        out.append(format_index(idx));
        out.push_back('.');
        out.append(route);
        out.append(".bin");
        return join_path(out_dir, out);
    }


    static std::string marker_name(uint8_t marker)
    {
        if (marker >= 0xE0U && marker <= 0xEFU) {
            const uint32_t n = static_cast<uint32_t>(marker - 0xE0U);
            char b[24];
            std::snprintf(b, sizeof(b), "APP%u(0x%02X)", n,
                          static_cast<unsigned>(marker));
            return std::string(b);
        }
        if (marker == 0xFEU) {
            return std::string("COM(0xFE)");
        }
        char b[24];
        std::snprintf(b, sizeof(b), "0x%02X", static_cast<unsigned>(marker));
        return std::string(b);
    }

    static std::string jxl_box_name(const std::array<char, 4>& type)
    {
        return std::string(type.data(), type.size());
    }

    static std::string webp_chunk_name(const std::array<char, 4>& type)
    {
        return std::string(type.data(), type.size());
    }

    static std::string bmff_fourcc_name(uint32_t v) noexcept
    {
        char out[5];
        out[0] = static_cast<char>((v >> 24) & 0xFFU);
        out[1] = static_cast<char>((v >> 16) & 0xFFU);
        out[2] = static_cast<char>((v >> 8) & 0xFFU);
        out[3] = static_cast<char>(v & 0xFFU);
        out[4] = '\0';
        return std::string(out, 4U);
    }

    static std::string bmff_item_name(uint32_t item_type,
                                      bool mime_xmp) noexcept
    {
        if (mime_xmp) {
            return "mime/xmp";
        }
        return bmff_fourcc_name(item_type);
    }

    static std::string bmff_property_name(uint32_t property_type,
                                          uint32_t property_subtype) noexcept
    {
        return bmff_fourcc_name(property_type) + "/"
               + bmff_fourcc_name(property_subtype);
    }

    static bool target_format_is_bmff(TransferTargetFormat format) noexcept
    {
        return format == TransferTargetFormat::Heif
               || format == TransferTargetFormat::Avif
               || format == TransferTargetFormat::Cr3;
    }

    static void
    print_transfer_payload_view_summary(const PreparedTransferPayloadView& view,
                                        uint32_t index)
    {
        std::printf("  [%u] semantic=%s route=%s size=%llu op=%s", index,
                    view.semantic_name.data(), view.route.data(),
                    static_cast<unsigned long long>(view.payload.size()),
                    transfer_adapter_op_kind_name(view.op.kind));
        switch (view.op.kind) {
        case TransferAdapterOpKind::JpegMarker:
            std::printf(" marker=%s",
                        marker_name(view.op.jpeg_marker_code).c_str());
            break;
        case TransferAdapterOpKind::TiffTagBytes:
            std::printf(" tag=0x%04X", static_cast<unsigned>(view.op.tiff_tag));
            break;
        case TransferAdapterOpKind::JxlBox:
            std::printf(" type=%s", jxl_box_name(view.op.box_type).c_str());
            break;
        case TransferAdapterOpKind::JxlIccProfile: break;
        case TransferAdapterOpKind::WebpChunk:
            std::printf(" type=%s",
                        webp_chunk_name(view.op.chunk_type).c_str());
            break;
        case TransferAdapterOpKind::BmffItem:
            std::printf(" type=%s", bmff_item_name(view.op.bmff_item_type,
                                                   view.op.bmff_mime_xmp)
                                        .c_str());
            break;
        case TransferAdapterOpKind::BmffProperty:
            std::printf(" type=%s",
                        bmff_property_name(view.op.bmff_property_type,
                                           view.op.bmff_property_subtype)
                            .c_str());
            break;
        }
        std::printf("\n");
    }

}  // namespace
}  // namespace openmeta


int
main(int argc, char** argv)
{
    using namespace openmeta;

    bool show_build_info      = true;
    bool force                = false;
    bool write_payloads       = false;
    bool dry_run              = false;
    bool replace_jpeg_jumbf   = false;
    bool c2pa_stage_requested = false;
    uint32_t emit_repeat      = 1U;
    bool time_patch_auto_nul  = true;
    std::string out_dir;
    std::string source_meta_path;
    std::string target_jpeg_path;
    std::string target_tiff_path;
    bool target_jxl  = false;
    bool target_webp = false;
    bool target_heif = false;
    bool target_avif = false;
    bool target_cr3  = false;
    std::string jpeg_jumbf_path;
    std::string jpeg_c2pa_signed_path;
    std::string c2pa_manifest_output_path;
    std::string c2pa_certificate_chain_path;
    std::string c2pa_key_ref;
    std::string c2pa_signing_time;
    std::string c2pa_binding_output_path;
    std::string c2pa_handoff_output_path;
    std::string c2pa_signed_package_output_path;
    std::string c2pa_signed_package_input_path;
    std::string transfer_payload_batch_output_path;
    std::string transfer_payload_batch_input_path;
    std::string output_path;
    std::vector<std::string> input_paths;
    std::vector<PendingTimePatch> pending_time_patches;
    ApplyTimePatchOptions time_patch_opts;
    PlanJpegEditOptions edit_plan_opts;

    PrepareTransferFileOptions options;
    options.prepare.target_format = TransferTargetFormat::Jpeg;
    options.prepare.xmp_portable  = true;

    for (int i = 1; i < argc; ++i) {
        const char* arg = argv[i];
        if (!arg) {
            continue;
        }
        if (std::strcmp(arg, "--help") == 0) {
            usage(argv[0]);
            return 0;
        }
        if (std::strcmp(arg, "--version") == 0) {
            print_build_info_header();
            return 0;
        }
        if (std::strcmp(arg, "--no-build-info") == 0) {
            show_build_info = false;
            continue;
        }
        if ((std::strcmp(arg, "-i") == 0 || std::strcmp(arg, "--input") == 0)
            && i + 1 < argc) {
            input_paths.emplace_back(argv[i + 1]);
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--format") == 0 && i + 1 < argc) {
            const char* v = argv[i + 1];
            if (!v) {
                std::fprintf(stderr, "invalid --format value\n");
                return 2;
            }
            if (std::strcmp(v, "portable") == 0) {
                options.prepare.xmp_portable = true;
            } else if (std::strcmp(v, "lossless") == 0) {
                options.prepare.xmp_portable = false;
            } else {
                std::fprintf(
                    stderr,
                    "invalid --format value (expected portable|lossless)\n");
                return 2;
            }
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--portable") == 0) {
            options.prepare.xmp_portable = true;
            continue;
        }
        if (std::strcmp(arg, "--lossless") == 0) {
            options.prepare.xmp_portable = false;
            continue;
        }
        if (std::strcmp(arg, "--xmp-include-existing") == 0) {
            options.prepare.xmp_include_existing = true;
            continue;
        }
        if (std::strcmp(arg, "--xmp-exiftool-gpsdatetime-alias") == 0) {
            options.prepare.xmp_exiftool_gpsdatetime_alias = true;
            continue;
        }
        if (std::strcmp(arg, "--no-exif") == 0) {
            options.prepare.include_exif_app1 = false;
            continue;
        }
        if (std::strcmp(arg, "--no-xmp") == 0) {
            options.prepare.include_xmp_app1 = false;
            continue;
        }
        if (std::strcmp(arg, "--no-icc") == 0) {
            options.prepare.include_icc_app2 = false;
            continue;
        }
        if (std::strcmp(arg, "--no-iptc") == 0) {
            options.prepare.include_iptc_app13 = false;
            continue;
        }
        if (std::strcmp(arg, "--makernotes") == 0) {
            options.decode_makernote = true;
            continue;
        }
        if (std::strcmp(arg, "--makernote-policy") == 0 && i + 1 < argc) {
            TransferPolicyAction action = TransferPolicyAction::Keep;
            if (!parse_transfer_policy_action(argv[i + 1], &action)) {
                std::fprintf(
                    stderr,
                    "invalid --makernote-policy value (expected keep|drop|invalidate|rewrite)\n");
                return 2;
            }
            options.prepare.profile.makernote = action;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--jumbf-policy") == 0 && i + 1 < argc) {
            TransferPolicyAction action = TransferPolicyAction::Keep;
            if (!parse_transfer_policy_action(argv[i + 1], &action)) {
                std::fprintf(
                    stderr,
                    "invalid --jumbf-policy value (expected keep|drop|invalidate|rewrite)\n");
                return 2;
            }
            options.prepare.profile.jumbf = action;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-policy") == 0 && i + 1 < argc) {
            TransferPolicyAction action = TransferPolicyAction::Keep;
            if (!parse_transfer_policy_action(argv[i + 1], &action)) {
                std::fprintf(
                    stderr,
                    "invalid --c2pa-policy value (expected keep|drop|invalidate|rewrite)\n");
                return 2;
            }
            options.prepare.profile.c2pa = action;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--jpeg-jumbf") == 0 && i + 1 < argc) {
            jpeg_jumbf_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--replace-jpeg-jumbf") == 0) {
            replace_jpeg_jumbf = true;
            continue;
        }
        if (std::strcmp(arg, "--jpeg-c2pa-signed") == 0 && i + 1 < argc) {
            jpeg_c2pa_signed_path = argv[i + 1];
            c2pa_stage_requested  = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-manifest-output") == 0 && i + 1 < argc) {
            c2pa_manifest_output_path = argv[i + 1];
            c2pa_stage_requested      = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-certificate-chain") == 0 && i + 1 < argc) {
            c2pa_certificate_chain_path = argv[i + 1];
            c2pa_stage_requested        = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-key-ref") == 0 && i + 1 < argc) {
            c2pa_key_ref         = argv[i + 1];
            c2pa_stage_requested = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--c2pa-signing-time") == 0 && i + 1 < argc) {
            c2pa_signing_time    = argv[i + 1];
            c2pa_stage_requested = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--dump-c2pa-binding") == 0 && i + 1 < argc) {
            c2pa_binding_output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--dump-c2pa-handoff") == 0 && i + 1 < argc) {
            c2pa_handoff_output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--dump-c2pa-signed-package") == 0
            && i + 1 < argc) {
            c2pa_signed_package_output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--load-c2pa-signed-package") == 0
            && i + 1 < argc) {
            c2pa_signed_package_input_path = argv[i + 1];
            c2pa_stage_requested           = true;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--dump-transfer-payload-batch") == 0
            && i + 1 < argc) {
            transfer_payload_batch_output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--load-transfer-payload-batch") == 0
            && i + 1 < argc) {
            transfer_payload_batch_input_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--no-decompress") == 0) {
            options.decompress = false;
            continue;
        }
        if (std::strcmp(arg, "--unsafe-write-payloads") == 0
            || std::strcmp(arg, "--write-payloads") == 0) {
            write_payloads = true;
            continue;
        }
        if (std::strcmp(arg, "--out-dir") == 0 && i + 1 < argc) {
            out_dir = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--source-meta") == 0 && i + 1 < argc) {
            source_meta_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-jpeg") == 0 && i + 1 < argc) {
            target_jpeg_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-tiff") == 0 && i + 1 < argc) {
            target_tiff_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--target-jxl") == 0) {
            target_jxl = true;
            continue;
        }
        if (std::strcmp(arg, "--target-webp") == 0) {
            target_webp = true;
            continue;
        }
        if (std::strcmp(arg, "--target-heif") == 0) {
            target_heif = true;
            continue;
        }
        if (std::strcmp(arg, "--target-avif") == 0) {
            target_avif = true;
            continue;
        }
        if (std::strcmp(arg, "--target-cr3") == 0) {
            target_cr3 = true;
            continue;
        }
        if ((std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--output") == 0)
            && i + 1 < argc) {
            output_path = argv[i + 1];
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--force") == 0) {
            force = true;
            continue;
        }
        if (std::strcmp(arg, "--dry-run") == 0) {
            dry_run = true;
            continue;
        }
        if (std::strcmp(arg, "--mode") == 0 && i + 1 < argc) {
            JpegEditMode m = JpegEditMode::Auto;
            if (!parse_jpeg_edit_mode(argv[i + 1], &m)) {
                std::fprintf(
                    stderr,
                    "invalid --mode value (expected auto|in_place|metadata_rewrite)\n");
                return 2;
            }
            edit_plan_opts.mode = m;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--require-in-place") == 0) {
            edit_plan_opts.require_in_place = true;
            continue;
        }
        if (std::strcmp(arg, "--emit-repeat") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --emit-repeat value\n");
                return 2;
            }
            emit_repeat = v;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--time-patch") == 0 && i + 1 < argc) {
            PendingTimePatch p;
            std::string err;
            if (!parse_time_patch_arg(argv[i + 1], &p, &err)) {
                std::fprintf(stderr, "%s: %s\n", err.c_str(), argv[i + 1]);
                return 2;
            }
            pending_time_patches.push_back(std::move(p));
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--time-patch-lax-width") == 0) {
            time_patch_opts.strict_width = false;
            continue;
        }
        if (std::strcmp(arg, "--time-patch-require-slot") == 0) {
            time_patch_opts.require_slot = true;
            continue;
        }
        if (std::strcmp(arg, "--time-patch-no-auto-nul") == 0) {
            time_patch_auto_nul = false;
            continue;
        }
        if (std::strcmp(arg, "--max-file-bytes") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (!parse_u64_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --max-file-bytes value\n");
                return 2;
            }
            options.policy.max_file_bytes = v;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-payload-bytes") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (!parse_u64_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-payload-bytes value\n");
                return 2;
            }
            options.policy.payload_limits.max_output_bytes = v;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-payload-parts") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-payload-parts value\n");
                return 2;
            }
            options.policy.payload_limits.max_parts = v;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-exif-ifds") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-exif-ifds value\n");
                return 2;
            }
            options.policy.exif_limits.max_ifds = v;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-exif-entries") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-exif-entries value\n");
                return 2;
            }
            options.policy.exif_limits.max_entries_per_ifd = v;
            i += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-exif-total") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v) || v == 0U) {
                std::fprintf(stderr, "invalid --max-exif-total value\n");
                return 2;
            }
            options.policy.exif_limits.max_total_entries = v;
            i += 1;
            continue;
        }
        if (arg[0] == '-') {
            std::fprintf(stderr, "unknown option: %s\n", arg);
            return 2;
        }
        input_paths.emplace_back(arg);
    }

    if (input_paths.empty()) {
        if (!source_meta_path.empty()) {
            input_paths.push_back(source_meta_path);
        } else if (!target_jpeg_path.empty()) {
            input_paths.push_back(target_jpeg_path);
        } else if (!target_tiff_path.empty()) {
            input_paths.push_back(target_tiff_path);
        }
    }
    if (input_paths.empty() && transfer_payload_batch_input_path.empty()) {
        usage(argv[0]);
        return 2;
    }
    if ((!source_meta_path.empty() || !target_jpeg_path.empty()
         || !target_tiff_path.empty())
        && input_paths.size() != 1U) {
        std::fprintf(
            stderr,
            "--source-meta/--target-jpeg/--target-tiff support exactly one job per run\n");
        return 2;
    }
    if (!output_path.empty() && input_paths.size() != 1U) {
        std::fprintf(stderr,
                     "--output supports exactly one input path per run\n");
        return 2;
    }
    if (!c2pa_binding_output_path.empty() && input_paths.size() != 1U) {
        std::fprintf(
            stderr,
            "--dump-c2pa-binding supports exactly one input path per run\n");
        return 2;
    }
    if ((!c2pa_handoff_output_path.empty()
         || !c2pa_signed_package_output_path.empty()
         || !c2pa_signed_package_input_path.empty())
        && input_paths.size() != 1U) {
        std::fprintf(
            stderr,
            "C2PA package options support exactly one input path per run\n");
        return 2;
    }
    if (!transfer_payload_batch_output_path.empty()
        && input_paths.size() != 1U) {
        std::fprintf(
            stderr,
            "--dump-transfer-payload-batch supports exactly one input path per run\n");
        return 2;
    }
    const uint32_t target_count
        = static_cast<uint32_t>(!target_jpeg_path.empty())
          + static_cast<uint32_t>(!target_tiff_path.empty())
          + static_cast<uint32_t>(target_jxl)
          + static_cast<uint32_t>(target_webp)
          + static_cast<uint32_t>(target_heif)
          + static_cast<uint32_t>(target_avif)
          + static_cast<uint32_t>(target_cr3);
    if (!transfer_payload_batch_input_path.empty()) {
        if (!input_paths.empty() || !source_meta_path.empty()
            || !target_jpeg_path.empty() || !target_tiff_path.empty()
            || target_count != 0U || !jpeg_jumbf_path.empty()
            || c2pa_stage_requested || !c2pa_binding_output_path.empty()
            || !c2pa_handoff_output_path.empty()
            || !c2pa_signed_package_output_path.empty()
            || !c2pa_signed_package_input_path.empty()
            || !transfer_payload_batch_output_path.empty()
            || !output_path.empty() || dry_run || write_payloads) {
            std::fprintf(
                stderr,
                "--load-transfer-payload-batch is an inspect-only mode and is mutually exclusive with source transfer/edit options\n");
            return 2;
        }

        if (show_build_info) {
            print_build_info_header();
        }

        std::vector<std::byte> batch_bytes;
        if (!read_file_bytes(transfer_payload_batch_input_path, &batch_bytes)) {
            std::fprintf(stderr, "failed to read transfer payload batch: %s\n",
                         transfer_payload_batch_input_path.c_str());
            return 1;
        }

        PreparedTransferPayloadBatch batch;
        const PreparedTransferPayloadIoResult batch_io
            = deserialize_prepared_transfer_payload_batch(
                std::span<const std::byte>(batch_bytes.data(),
                                           batch_bytes.size()),
                &batch);
        std::vector<PreparedTransferPayloadView> payload_views;
        EmitTransferResult inspect;
        if (batch_io.status == TransferStatus::Ok) {
            inspect = collect_prepared_transfer_payload_views(batch,
                                                              &payload_views);
        } else {
            inspect.status  = batch_io.status;
            inspect.code    = batch_io.code;
            inspect.errors  = batch_io.errors;
            inspect.message = batch_io.message;
        }

        std::printf("== transfer_payload_batch=%s\n",
                    transfer_payload_batch_input_path.c_str());
        std::printf(
            "  transfer_payload_batch: status=%s code=%s bytes=%llu payloads=%u target=%s\n",
            transfer_status_name(batch_io.status),
            emit_transfer_code_name(batch_io.code),
            static_cast<unsigned long long>(batch_io.bytes),
            static_cast<unsigned>(payload_views.size()),
            transfer_target_format_name(batch.target_format));
        if (!batch_io.message.empty()) {
            std::printf("  transfer_payload_batch_message=%s\n",
                        batch_io.message.c_str());
        }
        if (inspect.status != TransferStatus::Ok) {
            std::printf("  inspect: status=%s code=%s errors=%u\n",
                        transfer_status_name(inspect.status),
                        emit_transfer_code_name(inspect.code),
                        static_cast<unsigned>(inspect.errors));
            if (!inspect.message.empty()) {
                std::printf("  inspect_message=%s\n", inspect.message.c_str());
            }
            return 1;
        }
        for (size_t i = 0; i < payload_views.size(); ++i) {
            print_transfer_payload_view_summary(payload_views[i],
                                                static_cast<uint32_t>(i));
        }
        return 0;
    }
    if (target_count > 1U) {
        std::fprintf(
            stderr,
            "--target-jpeg, --target-tiff, --target-jxl, --target-webp, --target-heif, --target-avif, and --target-cr3 are mutually exclusive\n");
        return 2;
    }
    if (target_jxl) {
        options.prepare.target_format = TransferTargetFormat::Jxl;
    } else if (target_webp) {
        options.prepare.target_format = TransferTargetFormat::Webp;
    } else if (target_heif) {
        options.prepare.target_format = TransferTargetFormat::Heif;
    } else if (target_avif) {
        options.prepare.target_format = TransferTargetFormat::Avif;
    } else if (target_cr3) {
        options.prepare.target_format = TransferTargetFormat::Cr3;
    } else if (!target_tiff_path.empty()) {
        options.prepare.target_format = TransferTargetFormat::Tiff;
    } else {
        options.prepare.target_format = TransferTargetFormat::Jpeg;
    }
    if (!jpeg_jumbf_path.empty()
        && options.prepare.target_format != TransferTargetFormat::Jpeg) {
        std::fprintf(stderr,
                     "--jpeg-jumbf is only supported for JPEG targets\n");
        return 2;
    }
    if (c2pa_stage_requested
        && options.prepare.target_format != TransferTargetFormat::Jpeg) {
        std::fprintf(stderr,
                     "signed c2pa staging is only supported for JPEG targets\n");
        return 2;
    }
    if (!c2pa_binding_output_path.empty()
        && options.prepare.target_format != TransferTargetFormat::Jpeg) {
        std::fprintf(stderr,
                     "--dump-c2pa-binding is only supported for JPEG targets\n");
        return 2;
    }
    if ((!c2pa_handoff_output_path.empty()
         || !c2pa_signed_package_output_path.empty()
         || !c2pa_signed_package_input_path.empty())
        && options.prepare.target_format != TransferTargetFormat::Jpeg) {
        std::fprintf(
            stderr,
            "C2PA package options are only supported for JPEG targets\n");
        return 2;
    }
    if (!c2pa_signed_package_input_path.empty()
        && (!jpeg_c2pa_signed_path.empty() || !c2pa_manifest_output_path.empty()
            || !c2pa_certificate_chain_path.empty() || !c2pa_key_ref.empty()
            || !c2pa_signing_time.empty())) {
        std::fprintf(
            stderr,
            "--load-c2pa-signed-package is mutually exclusive with individual signed C2PA inputs\n");
        return 2;
    }
    if (c2pa_stage_requested) {
        options.prepare.profile.c2pa = TransferPolicyAction::Rewrite;
    }
    if (options.prepare.target_format == TransferTargetFormat::Tiff
        && (edit_plan_opts.mode != JpegEditMode::Auto
            || edit_plan_opts.require_in_place)) {
        std::fprintf(
            stderr,
            "--mode/--require-in-place are only valid for JPEG targets\n");
        return 2;
    }
    if (!output_path.empty()
        && (options.prepare.target_format == TransferTargetFormat::Jxl
            || options.prepare.target_format == TransferTargetFormat::Webp
            || target_format_is_bmff(options.prepare.target_format))) {
        std::fprintf(stderr,
                     options.prepare.target_format == TransferTargetFormat::Jxl
                         ? "--output is not supported for JXL targets yet\n"
                     : options.prepare.target_format
                             == TransferTargetFormat::Webp
                         ? "--output is not supported for WebP targets yet\n"
                         : "--output is not supported for BMFF targets yet\n");
        return 2;
    }

    if (show_build_info) {
        print_build_info_header();
    }

    bool any_failed = false;
    for (size_t i = 0; i < input_paths.size(); ++i) {
        const std::string& job_path   = input_paths[i];
        const std::string source_path = source_meta_path.empty()
                                            ? job_path
                                            : source_meta_path;
        std::string target_path;
        if (!target_jpeg_path.empty()) {
            target_path = target_jpeg_path;
        } else if (!target_tiff_path.empty()) {
            target_path = target_tiff_path;
        } else {
            target_path = job_path;
        }

        std::printf("== source=%s\n", source_path.c_str());
        if (target_path != source_path) {
            std::printf("   target=%s\n", target_path.c_str());
        }

        const bool need_jpeg_edit
            = options.prepare.target_format == TransferTargetFormat::Jpeg
              && (dry_run || !output_path.empty()
                  || edit_plan_opts.mode != JpegEditMode::Auto
                  || edit_plan_opts.require_in_place
                  || !target_jpeg_path.empty());
        const bool need_tiff_edit
            = options.prepare.target_format == TransferTargetFormat::Tiff
              && (dry_run || !output_path.empty() || !target_tiff_path.empty());

        const bool output_exists = !output_path.empty()
                                   && file_exists(output_path);
        FileByteWriter output_writer(output_path);
        const bool use_output_writer = !output_path.empty() && !dry_run
                                       && (!output_exists || force);
        PrepareTransferFileResult prepared
            = prepare_metadata_for_target_file(source_path.c_str(), options);
        EmitTransferResult append_jumbf_result;
        EmitTransferResult c2pa_stage_result;
        ValidatePreparedC2paSignResult c2pa_stage_validation;
        PreparedTransferC2paHandoffPackage c2pa_handoff;
        PreparedTransferC2paSignedPackage c2pa_signed_package;
        PreparedTransferC2paPackageIoResult c2pa_handoff_io;
        PreparedTransferC2paPackageIoResult c2pa_signed_package_io;
        PreparedTransferPayloadBatch transfer_payload_batch;
        PreparedTransferPayloadIoResult transfer_payload_batch_io;
        bool did_append_jumbf                = false;
        bool did_stage_c2pa                  = false;
        bool did_dump_c2pa_binding           = false;
        bool did_dump_c2pa_handoff           = false;
        bool did_dump_c2pa_signed_package    = false;
        bool did_dump_transfer_payload_batch = false;
        if (prepared.file_status == TransferFileStatus::Ok
            && prepared.prepare.status == TransferStatus::Ok
            && !jpeg_jumbf_path.empty()) {
            std::vector<std::byte> jpeg_jumbf_bytes;
            if (!read_file_bytes(jpeg_jumbf_path, &jpeg_jumbf_bytes)) {
                std::fprintf(stderr, "  append_jumbf: read_failed: %s\n",
                             jpeg_jumbf_path.c_str());
                any_failed = true;
                continue;
            }
            AppendPreparedJpegJumbfOptions append_options;
            append_options.replace_existing = replace_jpeg_jumbf;
            append_jumbf_result             = append_prepared_bundle_jpeg_jumbf(
                &prepared.bundle,
                std::span<const std::byte>(jpeg_jumbf_bytes.data(),
                                                       jpeg_jumbf_bytes.size()),
                append_options);
            did_append_jumbf = true;
        }
        if (prepared.file_status == TransferFileStatus::Ok
            && c2pa_stage_requested) {
            if (!c2pa_signed_package_input_path.empty()) {
                std::vector<std::byte> signed_package_bytes;
                if (!read_file_bytes(c2pa_signed_package_input_path,
                                     &signed_package_bytes)) {
                    std::fprintf(stderr, "  c2pa_stage: read_failed: %s\n",
                                 c2pa_signed_package_input_path.c_str());
                    any_failed = true;
                    continue;
                }
                c2pa_signed_package_io
                    = deserialize_prepared_c2pa_signed_package(
                        std::span<const std::byte>(signed_package_bytes.data(),
                                                   signed_package_bytes.size()),
                        &c2pa_signed_package);
                if (c2pa_signed_package_io.status != TransferStatus::Ok) {
                    c2pa_stage_result.status  = c2pa_signed_package_io.status;
                    c2pa_stage_result.code    = c2pa_signed_package_io.code;
                    c2pa_stage_result.errors  = c2pa_signed_package_io.errors;
                    c2pa_stage_result.message = c2pa_signed_package_io.message;
                } else {
                    c2pa_stage_validation
                        = validate_prepared_c2pa_signed_package(
                            prepared.bundle, c2pa_signed_package);
                    c2pa_stage_result = apply_prepared_c2pa_signed_package(
                        &prepared.bundle, c2pa_signed_package);
                }
            } else {
                std::vector<std::byte> signed_bytes;
                std::vector<std::byte> manifest_bytes;
                std::vector<std::byte> certificate_bytes;
                if (!jpeg_c2pa_signed_path.empty()
                    && !read_file_bytes(jpeg_c2pa_signed_path, &signed_bytes)) {
                    std::fprintf(stderr, "  c2pa_stage: read_failed: %s\n",
                                 jpeg_c2pa_signed_path.c_str());
                    any_failed = true;
                    continue;
                }
                if (!c2pa_manifest_output_path.empty()
                    && !read_file_bytes(c2pa_manifest_output_path,
                                        &manifest_bytes)) {
                    std::fprintf(stderr, "  c2pa_stage: read_failed: %s\n",
                                 c2pa_manifest_output_path.c_str());
                    any_failed = true;
                    continue;
                }
                if (!c2pa_certificate_chain_path.empty()
                    && !read_file_bytes(c2pa_certificate_chain_path,
                                        &certificate_bytes)) {
                    std::fprintf(stderr, "  c2pa_stage: read_failed: %s\n",
                                 c2pa_certificate_chain_path.c_str());
                    any_failed = true;
                    continue;
                }

                PreparedTransferC2paSignerInput signer_input;
                signer_input.signing_time            = c2pa_signing_time;
                signer_input.certificate_chain_bytes = std::move(
                    certificate_bytes);
                signer_input.private_key_reference   = c2pa_key_ref;
                signer_input.manifest_builder_output = std::move(
                    manifest_bytes);
                signer_input.signed_c2pa_logical_payload = std::move(
                    signed_bytes);

                const TransferStatus signed_package_status
                    = build_prepared_c2pa_signed_package(prepared.bundle,
                                                         signer_input,
                                                         &c2pa_signed_package);
                if (signed_package_status != TransferStatus::Ok) {
                    c2pa_stage_result.status = signed_package_status;
                    c2pa_stage_result.code = EmitTransferCode::InvalidArgument;
                    c2pa_stage_result.errors = 1U;
                    c2pa_stage_result.message
                        = c2pa_signed_package.request.message.empty()
                              ? "failed to build c2pa sign request"
                              : c2pa_signed_package.request.message;
                } else {
                    c2pa_stage_validation
                        = validate_prepared_c2pa_signed_package(
                            prepared.bundle, c2pa_signed_package);
                    c2pa_stage_result = apply_prepared_c2pa_signed_package(
                        &prepared.bundle, c2pa_signed_package);
                }
            }
            did_stage_c2pa = true;
        }

        PreparedTransferC2paSignRequest sign_request;
        const TransferStatus sign_request_status
            = build_prepared_c2pa_sign_request(prepared.bundle, &sign_request);
        if (prepared.file_status == TransferFileStatus::Ok
            && (!c2pa_binding_output_path.empty()
                || !c2pa_handoff_output_path.empty())) {
            did_dump_c2pa_binding = !c2pa_binding_output_path.empty();
            did_dump_c2pa_handoff = !c2pa_handoff_output_path.empty();
            std::vector<std::byte> binding_input;
            if (!read_file_bytes(target_path, &binding_input)) {
                c2pa_handoff.binding.status = TransferStatus::InvalidArgument;
                c2pa_handoff.binding.code   = EmitTransferCode::InvalidArgument;
                c2pa_handoff.binding.errors = 1U;
                c2pa_handoff.binding.message
                    = "failed to read c2pa binding target bytes";
            } else {
                build_prepared_c2pa_handoff_package(
                    prepared.bundle,
                    std::span<const std::byte>(binding_input.data(),
                                               binding_input.size()),
                    &c2pa_handoff);
                if (c2pa_handoff.binding.status == TransferStatus::Ok
                    && did_dump_c2pa_binding) {
                    if (!force && file_exists(c2pa_binding_output_path)) {
                        c2pa_handoff.binding.status
                            = TransferStatus::InvalidArgument;
                        c2pa_handoff.binding.code
                            = EmitTransferCode::InvalidArgument;
                        c2pa_handoff.binding.errors = 1U;
                        c2pa_handoff.binding.message
                            = "c2pa binding output exists (use --force)";
                        c2pa_handoff.binding.written = 0U;
                    } else if (!write_file_bytes(
                                   c2pa_binding_output_path,
                                   std::span<const std::byte>(
                                       c2pa_handoff.binding_bytes.data(),
                                       c2pa_handoff.binding_bytes.size()))) {
                        c2pa_handoff.binding.status
                            = TransferStatus::InternalError;
                        c2pa_handoff.binding.code
                            = EmitTransferCode::BackendWriteFailed;
                        c2pa_handoff.binding.errors = 1U;
                        c2pa_handoff.binding.message
                            = "failed to write c2pa binding output";
                        c2pa_handoff.binding.written = 0U;
                    }
                }
                if (c2pa_handoff.binding.status == TransferStatus::Ok
                    && did_dump_c2pa_handoff) {
                    std::vector<std::byte> handoff_bytes;
                    c2pa_handoff_io = serialize_prepared_c2pa_handoff_package(
                        c2pa_handoff, &handoff_bytes);
                    if (c2pa_handoff_io.status == TransferStatus::Ok) {
                        if (!force && file_exists(c2pa_handoff_output_path)) {
                            c2pa_handoff_io.status
                                = TransferStatus::InvalidArgument;
                            c2pa_handoff_io.code
                                = EmitTransferCode::InvalidArgument;
                            c2pa_handoff_io.errors = 1U;
                            c2pa_handoff_io.message
                                = "c2pa handoff output exists (use --force)";
                            c2pa_handoff_io.bytes = 0U;
                        } else if (!write_file_bytes(c2pa_handoff_output_path,
                                                     std::span<const std::byte>(
                                                         handoff_bytes.data(),
                                                         handoff_bytes.size()))) {
                            c2pa_handoff_io.status
                                = TransferStatus::InternalError;
                            c2pa_handoff_io.code
                                = EmitTransferCode::BackendWriteFailed;
                            c2pa_handoff_io.errors = 1U;
                            c2pa_handoff_io.message
                                = "failed to write c2pa handoff output";
                            c2pa_handoff_io.bytes = 0U;
                        }
                    }
                }
            }
        }
        if (prepared.file_status == TransferFileStatus::Ok
            && !c2pa_signed_package_output_path.empty()
            && c2pa_signed_package.request.status == TransferStatus::Ok) {
            did_dump_c2pa_signed_package = true;
            std::vector<std::byte> signed_package_bytes;
            c2pa_signed_package_io
                = serialize_prepared_c2pa_signed_package(c2pa_signed_package,
                                                         &signed_package_bytes);
            if (c2pa_signed_package_io.status == TransferStatus::Ok) {
                if (!force && file_exists(c2pa_signed_package_output_path)) {
                    c2pa_signed_package_io.status
                        = TransferStatus::InvalidArgument;
                    c2pa_signed_package_io.code
                        = EmitTransferCode::InvalidArgument;
                    c2pa_signed_package_io.errors = 1U;
                    c2pa_signed_package_io.message
                        = "signed c2pa package output exists (use --force)";
                    c2pa_signed_package_io.bytes = 0U;
                } else if (!write_file_bytes(c2pa_signed_package_output_path,
                                             std::span<const std::byte>(
                                                 signed_package_bytes.data(),
                                                 signed_package_bytes.size()))) {
                    c2pa_signed_package_io.status
                        = TransferStatus::InternalError;
                    c2pa_signed_package_io.code
                        = EmitTransferCode::BackendWriteFailed;
                    c2pa_signed_package_io.errors = 1U;
                    c2pa_signed_package_io.message
                        = "failed to write signed c2pa package output";
                    c2pa_signed_package_io.bytes = 0U;
                }
            }
        }
        if (prepared.file_status == TransferFileStatus::Ok
            && !transfer_payload_batch_output_path.empty()) {
            did_dump_transfer_payload_batch = true;
            const EmitTransferResult payload_batch_status
                = build_prepared_transfer_payload_batch(prepared.bundle,
                                                        &transfer_payload_batch);
            if (payload_batch_status.status != TransferStatus::Ok) {
                transfer_payload_batch_io.status = payload_batch_status.status;
                transfer_payload_batch_io.code   = payload_batch_status.code;
                transfer_payload_batch_io.errors = payload_batch_status.errors;
                transfer_payload_batch_io.message = payload_batch_status.message;
            } else {
                std::vector<std::byte> payload_batch_bytes;
                transfer_payload_batch_io
                    = serialize_prepared_transfer_payload_batch(
                        transfer_payload_batch, &payload_batch_bytes);
                if (transfer_payload_batch_io.status == TransferStatus::Ok) {
                    if (!force
                        && file_exists(transfer_payload_batch_output_path)) {
                        transfer_payload_batch_io.status
                            = TransferStatus::InvalidArgument;
                        transfer_payload_batch_io.code
                            = EmitTransferCode::InvalidArgument;
                        transfer_payload_batch_io.errors = 1U;
                        transfer_payload_batch_io.message
                            = "transfer payload batch output exists (use --force)";
                        transfer_payload_batch_io.bytes = 0U;
                    } else if (!write_file_bytes(
                                   transfer_payload_batch_output_path,
                                   std::span<const std::byte>(
                                       payload_batch_bytes.data(),
                                       payload_batch_bytes.size()))) {
                        transfer_payload_batch_io.status
                            = TransferStatus::InternalError;
                        transfer_payload_batch_io.code
                            = EmitTransferCode::BackendWriteFailed;
                        transfer_payload_batch_io.errors = 1U;
                        transfer_payload_batch_io.message
                            = "failed to write transfer payload batch output";
                        transfer_payload_batch_io.bytes = 0U;
                    }
                }
            }
        }

        ExecutePreparedTransferOptions exec_options;
        exec_options.time_patches = build_transfer_time_patch_inputs(
            pending_time_patches);
        exec_options.time_patch          = time_patch_opts;
        exec_options.time_patch_auto_nul = time_patch_auto_nul;
        exec_options.emit_repeat         = emit_repeat;
        exec_options.jpeg_edit           = edit_plan_opts;
        exec_options.edit_requested      = need_jpeg_edit || need_tiff_edit;
        exec_options.edit_apply          = !dry_run && !output_path.empty();
        if (use_output_writer) {
            exec_options.edit_output_writer = &output_writer;
        }

        ExecutePreparedTransferResult exec;
        MappedFile mapped_edit_file;
        if (prepared.file_status == TransferFileStatus::Ok
            && (prepared.prepare.status == TransferStatus::Ok
                || (did_stage_c2pa
                    && c2pa_stage_result.status == TransferStatus::Ok))
            && (!did_append_jumbf
                || append_jumbf_result.status == TransferStatus::Ok)) {
            std::span<const std::byte> edit_input;
            if (exec_options.edit_requested) {
                const MappedFileStatus map_status
                    = mapped_edit_file.open(target_path.c_str(),
                                            options.policy.max_file_bytes);
                if (map_status != MappedFileStatus::Ok) {
                    std::fprintf(stderr, "  edit_input: map_failed: %s\n",
                                 target_path.c_str());
                    any_failed = true;
                    continue;
                }
                edit_input = mapped_edit_file.bytes();
            }
            exec = execute_prepared_transfer(&prepared.bundle, edit_input,
                                             exec_options);
        }

        std::printf("  file_status=%s code=%s size=%llu entries=%u\n",
                    transfer_file_status_name(prepared.file_status),
                    prepare_transfer_file_code_name(prepared.code),
                    static_cast<unsigned long long>(prepared.file_size),
                    prepared.entry_count);
        std::printf("  read: scan=%s payload=%s exif=%s xmp=%s jumbf=%s\n",
                    scan_status_name(prepared.read.scan.status),
                    payload_status_name(prepared.read.payload.status),
                    exif_status_name(prepared.read.exif.status),
                    xmp_status_name(prepared.read.xmp.status),
                    jumbf_status_name(prepared.read.jumbf.status));
        std::printf(
            "  prepare: status=%s code=%s blocks=%u warnings=%u errors=%u\n",
            transfer_status_name(prepared.prepare.status),
            prepare_transfer_code_name(prepared.prepare.code),
            static_cast<unsigned>(prepared.bundle.blocks.size()),
            prepared.prepare.warnings, prepared.prepare.errors);
        if (!prepared.prepare.message.empty()) {
            std::printf("  prepare_message=%s\n",
                        prepared.prepare.message.c_str());
        }
        if (did_append_jumbf) {
            std::printf(
                "  append_jumbf: status=%s code=%s emitted=%u removed=%u errors=%u\n",
                transfer_status_name(append_jumbf_result.status),
                emit_transfer_code_name(append_jumbf_result.code),
                append_jumbf_result.emitted, append_jumbf_result.skipped,
                append_jumbf_result.errors);
            if (!append_jumbf_result.message.empty()) {
                std::printf("  append_jumbf_message=%s\n",
                            append_jumbf_result.message.c_str());
            }
        }
        if (did_stage_c2pa) {
            std::printf(
                "  c2pa_stage_validate: status=%s code=%s kind=%s payload_bytes=%llu carrier_bytes=%llu segments=%u errors=%u\n",
                transfer_status_name(c2pa_stage_validation.status),
                emit_transfer_code_name(c2pa_stage_validation.code),
                transfer_c2pa_signed_payload_kind_name(
                    c2pa_stage_validation.payload_kind),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.logical_payload_bytes),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.staged_payload_bytes),
                c2pa_stage_validation.staged_segments,
                c2pa_stage_validation.errors);
            std::printf(
                "  c2pa_stage_semantics: status=%s reason=%s manifest=%llu manifests=%llu claim_generator=%llu assertions=%llu claims=%llu signatures=%llu linked=%llu orphan=%llu explicit_refs=%llu unresolved=%llu ambiguous=%llu\n",
                transfer_c2pa_semantic_status_name(
                    c2pa_stage_validation.semantic_status),
                c2pa_stage_validation.semantic_reason.c_str(),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_manifest_present),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_manifest_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_claim_generator_present),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_assertion_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_claim_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_signature_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_signature_linked),
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_signature_orphan),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_explicit_reference_signature_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_explicit_reference_unresolved_signature_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_explicit_reference_ambiguous_signature_count));
            std::printf(
                "  c2pa_stage_linkage: claim0_assertions=%llu claim0_refs=%llu sig0_links=%llu\n",
                static_cast<unsigned long long>(
                    c2pa_stage_validation.semantic_primary_claim_assertion_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_primary_claim_referenced_by_signature_count),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_primary_signature_linked_claim_count));
            std::printf(
                "  c2pa_stage_references: sig0_keys=%llu sig0_present=%llu sig0_resolved=%llu\n",
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_primary_signature_reference_key_hits),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_primary_signature_explicit_reference_present),
                static_cast<unsigned long long>(
                    c2pa_stage_validation
                        .semantic_primary_signature_explicit_reference_resolved_claim_count));
            if (!c2pa_stage_validation.message.empty()) {
                std::printf("  c2pa_stage_validate_message=%s\n",
                            c2pa_stage_validation.message.c_str());
            }
            std::printf(
                "  c2pa_stage: status=%s code=%s emitted=%u removed=%u errors=%u\n",
                transfer_status_name(c2pa_stage_result.status),
                emit_transfer_code_name(c2pa_stage_result.code),
                c2pa_stage_result.emitted, c2pa_stage_result.skipped,
                c2pa_stage_result.errors);
            if (!c2pa_stage_result.message.empty()) {
                std::printf("  c2pa_stage_message=%s\n",
                            c2pa_stage_result.message.c_str());
            }
        }
        for (size_t pi = 0; pi < prepared.bundle.policy_decisions.size();
             ++pi) {
            const PreparedTransferPolicyDecision& decision
                = prepared.bundle.policy_decisions[pi];
            if (decision.subject == TransferPolicySubject::C2pa) {
                std::printf(
                    "  policy[%s]: requested=%s effective=%s reason=%s mode=%s source=%s output=%s matched=%u\n",
                    transfer_policy_subject_name(decision.subject),
                    transfer_policy_action_name(decision.requested),
                    transfer_policy_action_name(decision.effective),
                    transfer_policy_reason_name(decision.reason),
                    transfer_c2pa_mode_name(decision.c2pa_mode),
                    transfer_c2pa_source_kind_name(decision.c2pa_source_kind),
                    transfer_c2pa_prepared_output_name(
                        decision.c2pa_prepared_output),
                    decision.matched_entries);
            } else {
                std::printf(
                    "  policy[%s]: requested=%s effective=%s reason=%s matched=%u\n",
                    transfer_policy_subject_name(decision.subject),
                    transfer_policy_action_name(decision.requested),
                    transfer_policy_action_name(decision.effective),
                    transfer_policy_reason_name(decision.reason),
                    decision.matched_entries);
            }
            if (!decision.message.empty()) {
                std::printf("  policy[%s]_message=%s\n",
                            transfer_policy_subject_name(decision.subject),
                            decision.message.c_str());
            }
        }
        if (prepared.bundle.c2pa_rewrite.state
                != TransferC2paRewriteState::NotApplicable
            || prepared.bundle.c2pa_rewrite.matched_entries > 0U) {
            const PreparedTransferC2paRewriteRequirements& rewrite
                = prepared.bundle.c2pa_rewrite;
            std::printf(
                "  c2pa_rewrite: state=%s target=%s source=%s matched=%u existing_segments=%u carrier_available=%s invalidates_existing=%s\n",
                transfer_c2pa_rewrite_state_name(rewrite.state),
                transfer_target_format_name(rewrite.target_format),
                transfer_c2pa_source_kind_name(rewrite.source_kind),
                rewrite.matched_entries, rewrite.existing_carrier_segments,
                rewrite.target_carrier_available ? "yes" : "no",
                rewrite.content_change_invalidates_existing ? "yes" : "no");
            if (rewrite.requires_manifest_builder
                || rewrite.requires_content_binding
                || rewrite.requires_certificate_chain
                || rewrite.requires_private_key
                || rewrite.requires_signing_time) {
                std::printf(
                    "  c2pa_rewrite_requirements: manifest_builder=%s content_binding=%s certificate_chain=%s private_key=%s signing_time=%s\n",
                    rewrite.requires_manifest_builder ? "yes" : "no",
                    rewrite.requires_content_binding ? "yes" : "no",
                    rewrite.requires_certificate_chain ? "yes" : "no",
                    rewrite.requires_private_key ? "yes" : "no",
                    rewrite.requires_signing_time ? "yes" : "no");
            }
            if (!rewrite.message.empty()) {
                std::printf("  c2pa_rewrite_message=%s\n",
                            rewrite.message.c_str());
            }
            if (!rewrite.content_binding_chunks.empty()) {
                std::printf("  c2pa_rewrite_binding: chunks=%u bytes=%llu\n",
                            static_cast<unsigned>(
                                rewrite.content_binding_chunks.size()),
                            static_cast<unsigned long long>(
                                rewrite.content_binding_bytes));
                for (size_t ci = 0; ci < rewrite.content_binding_chunks.size();
                     ++ci) {
                    const PreparedTransferC2paRewriteChunk& chunk
                        = rewrite.content_binding_chunks[ci];
                    if (chunk.kind
                        == TransferC2paRewriteChunkKind::SourceRange) {
                        std::printf(
                            "  c2pa_rewrite_chunk[%u]: kind=%s offset=%llu size=%llu\n",
                            static_cast<unsigned>(ci),
                            transfer_c2pa_rewrite_chunk_kind_name(chunk.kind),
                            static_cast<unsigned long long>(chunk.source_offset),
                            static_cast<unsigned long long>(chunk.size));
                    } else {
                        std::printf(
                            "  c2pa_rewrite_chunk[%u]: kind=%s block=%u marker=0x%02X size=%llu route=%s\n",
                            static_cast<unsigned>(ci),
                            transfer_c2pa_rewrite_chunk_kind_name(chunk.kind),
                            chunk.block_index, chunk.jpeg_marker_code,
                            static_cast<unsigned long long>(chunk.size),
                            chunk.block_index < prepared.bundle.blocks.size()
                                ? prepared.bundle.blocks[chunk.block_index]
                                      .route.c_str()
                                : "invalid");
                    }
                }
            }
        }
        if (prepared.bundle.c2pa_rewrite.state
                != TransferC2paRewriteState::NotApplicable
            || prepared.bundle.c2pa_rewrite.matched_entries > 0U) {
            std::printf(
                "  c2pa_sign_request: status=%s carrier=%s manifest_label=%s source_ranges=%u prepared_segments=%u bytes=%llu\n",
                transfer_status_name(sign_request_status),
                sign_request.carrier_route.empty()
                    ? "-"
                    : sign_request.carrier_route.c_str(),
                sign_request.manifest_label.empty()
                    ? "-"
                    : sign_request.manifest_label.c_str(),
                sign_request.source_range_chunks,
                sign_request.prepared_segment_chunks,
                static_cast<unsigned long long>(
                    sign_request.content_binding_bytes));
            if (!sign_request.message.empty()) {
                std::printf("  c2pa_sign_request_message=%s\n",
                            sign_request.message.c_str());
            }
        }
        if (did_dump_c2pa_binding) {
            std::printf(
                "  c2pa_binding: status=%s code=%s bytes=%llu errors=%u path=%s\n",
                transfer_status_name(c2pa_handoff.binding.status),
                emit_transfer_code_name(c2pa_handoff.binding.code),
                static_cast<unsigned long long>(c2pa_handoff.binding.written),
                c2pa_handoff.binding.errors, c2pa_binding_output_path.c_str());
            if (!c2pa_handoff.request.carrier_route.empty()) {
                std::printf(
                    "  c2pa_handoff: carrier=%s manifest_label=%s bytes=%llu source_ranges=%u prepared_segments=%u\n",
                    c2pa_handoff.request.carrier_route.c_str(),
                    c2pa_handoff.request.manifest_label.c_str(),
                    static_cast<unsigned long long>(
                        c2pa_handoff.binding.written),
                    c2pa_handoff.request.source_range_chunks,
                    c2pa_handoff.request.prepared_segment_chunks);
            }
            if (!c2pa_handoff.binding.message.empty()) {
                std::printf("  c2pa_binding_message=%s\n",
                            c2pa_handoff.binding.message.c_str());
            }
        }
        if (did_dump_c2pa_handoff) {
            std::printf(
                "  c2pa_handoff_package: status=%s code=%s bytes=%llu errors=%u path=%s\n",
                transfer_status_name(c2pa_handoff_io.status),
                emit_transfer_code_name(c2pa_handoff_io.code),
                static_cast<unsigned long long>(c2pa_handoff_io.bytes),
                c2pa_handoff_io.errors, c2pa_handoff_output_path.c_str());
            if (!c2pa_handoff_io.message.empty()) {
                std::printf("  c2pa_handoff_package_message=%s\n",
                            c2pa_handoff_io.message.c_str());
            }
        }
        if (!c2pa_signed_package_input_path.empty()) {
            std::printf(
                "  c2pa_signed_package_input: status=%s code=%s bytes=%llu errors=%u path=%s\n",
                transfer_status_name(c2pa_signed_package_io.status),
                emit_transfer_code_name(c2pa_signed_package_io.code),
                static_cast<unsigned long long>(c2pa_signed_package_io.bytes),
                c2pa_signed_package_io.errors,
                c2pa_signed_package_input_path.c_str());
            if (!c2pa_signed_package_io.message.empty()) {
                std::printf("  c2pa_signed_package_input_message=%s\n",
                            c2pa_signed_package_io.message.c_str());
            }
        }
        if (did_dump_c2pa_signed_package) {
            std::printf(
                "  c2pa_signed_package: status=%s code=%s bytes=%llu errors=%u path=%s\n",
                transfer_status_name(c2pa_signed_package_io.status),
                emit_transfer_code_name(c2pa_signed_package_io.code),
                static_cast<unsigned long long>(c2pa_signed_package_io.bytes),
                c2pa_signed_package_io.errors,
                c2pa_signed_package_output_path.c_str());
            if (!c2pa_signed_package_io.message.empty()) {
                std::printf("  c2pa_signed_package_message=%s\n",
                            c2pa_signed_package_io.message.c_str());
            }
        }
        if (did_dump_transfer_payload_batch) {
            std::printf(
                "  transfer_payload_batch: status=%s code=%s bytes=%llu errors=%u path=%s\n",
                transfer_status_name(transfer_payload_batch_io.status),
                emit_transfer_code_name(transfer_payload_batch_io.code),
                static_cast<unsigned long long>(transfer_payload_batch_io.bytes),
                transfer_payload_batch_io.errors,
                transfer_payload_batch_output_path.c_str());
            if (!transfer_payload_batch_io.message.empty()) {
                std::printf("  transfer_payload_batch_message=%s\n",
                            transfer_payload_batch_io.message.c_str());
            }
        }

        if (prepared.file_status != TransferFileStatus::Ok
            || (prepared.prepare.status != TransferStatus::Ok
                && (!did_stage_c2pa
                    || c2pa_stage_result.status != TransferStatus::Ok)
                && (!did_dump_c2pa_binding
                    || c2pa_handoff.binding.status != TransferStatus::Ok)
                && (!did_dump_c2pa_handoff
                    || c2pa_handoff_io.status != TransferStatus::Ok)
                && (!did_dump_transfer_payload_batch
                    || transfer_payload_batch_io.status
                           != TransferStatus::Ok))) {
            any_failed = true;
            continue;
        }
        if (did_append_jumbf
            && append_jumbf_result.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (did_stage_c2pa && c2pa_stage_result.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (did_dump_c2pa_binding
            && c2pa_handoff.binding.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (did_dump_c2pa_handoff
            && c2pa_handoff_io.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (!c2pa_signed_package_input_path.empty()
            && c2pa_signed_package_io.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (did_dump_c2pa_signed_package
            && c2pa_signed_package_io.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (did_dump_transfer_payload_batch
            && transfer_payload_batch_io.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }
        if (prepared.prepare.status != TransferStatus::Ok
            && (!did_stage_c2pa
                || c2pa_stage_result.status != TransferStatus::Ok)
            && ((did_dump_c2pa_binding
                 && c2pa_handoff.binding.status == TransferStatus::Ok)
                || (did_dump_c2pa_handoff
                    && c2pa_handoff_io.status == TransferStatus::Ok)
                || (did_dump_transfer_payload_batch
                    && transfer_payload_batch_io.status == TransferStatus::Ok))
            && !exec_options.edit_requested) {
            continue;
        }

        std::printf("  time_patch: status=%s patched=%u skipped=%u errors=%u\n",
                    transfer_status_name(exec.time_patch.status),
                    exec.time_patch.patched_slots,
                    exec.time_patch.skipped_slots, exec.time_patch.errors);
        if (!exec.time_patch.message.empty()) {
            std::printf("  time_patch_message=%s\n",
                        exec.time_patch.message.c_str());
        }
        if (exec.time_patch.status != TransferStatus::Ok) {
            any_failed = true;
            continue;
        }

        for (uint32_t bi = 0; bi < prepared.bundle.blocks.size(); ++bi) {
            const PreparedTransferBlock& block = prepared.bundle.blocks[bi];
            std::printf("  [%u] route=%s kind=%s size=%u\n", bi,
                        block.route.c_str(), transfer_kind_name(block.kind),
                        static_cast<unsigned>(block.payload.size()));
            if (!write_payloads) {
                continue;
            }
            const std::string out_path = payload_dump_path(source_path, out_dir,
                                                           block, bi);
            if (!force && file_exists(out_path)) {
                std::fprintf(stderr, "  [%u] exists: %s (use --force)\n", bi,
                             out_path.c_str());
                any_failed = true;
                continue;
            }
            if (!write_file_bytes(out_path, std::span<const std::byte>(
                                                block.payload.data(),
                                                block.payload.size()))) {
                std::fprintf(stderr, "  [%u] write_failed: %s\n", bi,
                             out_path.c_str());
                any_failed = true;
                continue;
            }
            std::printf("  [%u] wrote=%s\n", bi, out_path.c_str());
        }

        if (prepared.bundle.target_format == TransferTargetFormat::Jpeg) {
            if (exec.edit_requested) {
                if (exec.edit_plan_status == TransferStatus::Ok) {
                    std::printf(
                        "  edit_plan: status=%s requested=%s selected=%s "
                        "in_place_possible=%s emitted=%u replaced=%u appended=%u "
                        "removed=%u removed_jumbf=%u removed_c2pa=%u "
                        "input=%llu output=%llu scan_end=%llu\n",
                        transfer_status_name(exec.edit_plan_status),
                        jpeg_edit_mode_name(exec.jpeg_edit_plan.requested_mode),
                        jpeg_edit_mode_name(exec.jpeg_edit_plan.selected_mode),
                        exec.jpeg_edit_plan.in_place_possible ? "true"
                                                              : "false",
                        exec.jpeg_edit_plan.emitted_segments,
                        exec.jpeg_edit_plan.replaced_segments,
                        exec.jpeg_edit_plan.appended_segments,
                        exec.jpeg_edit_plan.removed_existing_segments,
                        exec.jpeg_edit_plan.removed_existing_jumbf_segments,
                        exec.jpeg_edit_plan.removed_existing_c2pa_segments,
                        static_cast<unsigned long long>(exec.edit_input_size),
                        static_cast<unsigned long long>(exec.edit_output_size),
                        static_cast<unsigned long long>(
                            exec.jpeg_edit_plan.leading_scan_end));
                } else {
                    std::printf("  edit_plan: status=%s\n",
                                transfer_status_name(exec.edit_plan_status));
                }
                if (!exec.edit_plan_message.empty()) {
                    std::printf("  edit_plan_message=%s\n",
                                exec.edit_plan_message.c_str());
                }
                if (exec.edit_plan_status != TransferStatus::Ok) {
                    any_failed = true;
                }
            }

            if (!output_path.empty()) {
                if (!force && output_exists) {
                    std::fprintf(stderr,
                                 "  edit_apply: exists: %s (use --force)\n",
                                 output_path.c_str());
                    any_failed = true;
                    continue;
                }
                if (!dry_run) {
                    std::printf(
                        "  edit_apply: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                        transfer_status_name(exec.edit_apply.status),
                        emit_transfer_code_name(exec.edit_apply.code),
                        exec.edit_apply.emitted, exec.edit_apply.skipped,
                        exec.edit_apply.errors);
                    if (!exec.edit_apply.message.empty()) {
                        std::printf("  edit_apply_message=%s\n",
                                    exec.edit_apply.message.c_str());
                    }
                    if (exec.edit_apply.status != TransferStatus::Ok) {
                        any_failed = true;
                        continue;
                    }
                    if (use_output_writer) {
                        if (output_writer.finish() != TransferStatus::Ok) {
                            std::fprintf(stderr,
                                         "  edit_apply: write_failed: %s\n",
                                         output_path.c_str());
                            any_failed = true;
                            continue;
                        }
                    } else if (!write_file_bytes(
                                   output_path,
                                   std::span<const std::byte>(
                                       exec.edited_output.data(),
                                       exec.edited_output.size()))) {
                        std::fprintf(stderr, "  edit_apply: write_failed: %s\n",
                                     output_path.c_str());
                        any_failed = true;
                        continue;
                    }
                    std::printf("  output=%s bytes=%llu\n", output_path.c_str(),
                                static_cast<unsigned long long>(
                                    use_output_writer
                                        ? output_writer.bytes_written()
                                        : exec.edited_output.size()));
                }
            }

            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t mi = 0; mi < exec.marker_summary.size(); ++mi) {
                const EmittedJpegMarkerSummary& one = exec.marker_summary[mi];
                std::printf("  marker %s count=%u bytes=%llu\n",
                            marker_name(one.marker).c_str(), one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            continue;
        }

        if (prepared.bundle.target_format == TransferTargetFormat::Tiff) {
            if (exec.edit_requested) {
                if (exec.edit_plan_status == TransferStatus::Ok) {
                    std::printf(
                        "  tiff_edit: status=%s updates=%u exif=%s input=%llu output=%llu\n",
                        transfer_status_name(exec.edit_plan_status),
                        static_cast<unsigned>(exec.tiff_edit_plan.tag_updates),
                        exec.tiff_edit_plan.has_exif_ifd ? "on" : "off",
                        static_cast<unsigned long long>(exec.edit_input_size),
                        static_cast<unsigned long long>(exec.edit_output_size));
                } else {
                    std::printf("  tiff_edit: status=%s\n",
                                transfer_status_name(exec.edit_plan_status));
                }
                if (!exec.edit_plan_message.empty()) {
                    std::printf("  tiff_edit_message=%s\n",
                                exec.edit_plan_message.c_str());
                }
                if (exec.edit_plan_status != TransferStatus::Ok) {
                    any_failed = true;
                }
            }

            if (!output_path.empty()) {
                if (!force && output_exists) {
                    std::fprintf(stderr,
                                 "  tiff_edit_apply: exists: %s (use --force)\n",
                                 output_path.c_str());
                    any_failed = true;
                    continue;
                }
                if (!dry_run) {
                    std::printf(
                        "  tiff_edit_apply: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                        transfer_status_name(exec.edit_apply.status),
                        emit_transfer_code_name(exec.edit_apply.code),
                        exec.edit_apply.emitted, exec.edit_apply.skipped,
                        exec.edit_apply.errors);
                    if (!exec.edit_apply.message.empty()) {
                        std::printf("  tiff_edit_apply_message=%s\n",
                                    exec.edit_apply.message.c_str());
                    }
                    if (exec.edit_apply.status != TransferStatus::Ok) {
                        any_failed = true;
                        continue;
                    }
                    if (use_output_writer) {
                        if (output_writer.finish() != TransferStatus::Ok) {
                            std::fprintf(stderr,
                                         "  tiff_edit_apply: write_failed: %s\n",
                                         output_path.c_str());
                            any_failed = true;
                            continue;
                        }
                    } else if (!write_file_bytes(
                                   output_path,
                                   std::span<const std::byte>(
                                       exec.edited_output.data(),
                                       exec.edited_output.size()))) {
                        std::fprintf(stderr,
                                     "  tiff_edit_apply: write_failed: %s\n",
                                     output_path.c_str());
                        any_failed = true;
                        continue;
                    }
                    std::printf("  output=%s bytes=%llu\n", output_path.c_str(),
                                static_cast<unsigned long long>(
                                    use_output_writer
                                        ? output_writer.bytes_written()
                                        : exec.edited_output.size()));
                }
            }

            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            std::printf("  tiff_commit=%s\n",
                        exec.tiff_commit ? "true" : "false");
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t si = 0; si < exec.tiff_tag_summary.size(); ++si) {
                const EmittedTiffTagSummary& one = exec.tiff_tag_summary[si];
                std::printf("  tiff_tag %u count=%u bytes=%llu\n",
                            static_cast<unsigned>(one.tag), one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            continue;
        }

        if (prepared.bundle.target_format == TransferTargetFormat::Jxl) {
            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t bi = 0; bi < exec.jxl_box_summary.size(); ++bi) {
                const EmittedJxlBoxSummary& one = exec.jxl_box_summary[bi];
                std::printf("  jxl_box %s count=%u bytes=%llu\n",
                            jxl_box_name(one.type).c_str(), one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            continue;
        }

        if (prepared.bundle.target_format == TransferTargetFormat::Webp) {
            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t ci = 0; ci < exec.webp_chunk_summary.size(); ++ci) {
                const EmittedWebpChunkSummary& one = exec.webp_chunk_summary[ci];
                std::printf("  webp_chunk %s count=%u bytes=%llu\n",
                            webp_chunk_name(one.type).c_str(), one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            continue;
        }

        if (target_format_is_bmff(prepared.bundle.target_format)) {
            std::printf(
                "  compile: status=%s code=%s ops=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.compile.status),
                emit_transfer_code_name(exec.compile.code),
                static_cast<unsigned>(exec.compiled_ops), exec.compile.skipped,
                exec.compile.errors);
            if (!exec.compile.message.empty()) {
                std::printf("  compile_message=%s\n",
                            exec.compile.message.c_str());
            }
            if (exec.compile.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            std::printf(
                "  emit: status=%s code=%s emitted=%u skipped=%u errors=%u\n",
                transfer_status_name(exec.emit.status),
                emit_transfer_code_name(exec.emit.code), exec.emit.emitted,
                exec.emit.skipped, exec.emit.errors);
            if (emit_repeat > 1U) {
                std::printf("  emit_repeat=%u\n", emit_repeat);
            }
            if (!exec.emit.message.empty()) {
                std::printf("  emit_message=%s\n", exec.emit.message.c_str());
            }
            if (exec.emit.status != TransferStatus::Ok) {
                any_failed = true;
                continue;
            }

            for (size_t bi = 0; bi < exec.bmff_item_summary.size(); ++bi) {
                const EmittedBmffItemSummary& one = exec.bmff_item_summary[bi];
                std::printf("  bmff_item %s count=%u bytes=%llu\n",
                            bmff_item_name(one.item_type, one.mime_xmp).c_str(),
                            one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            for (size_t bi = 0; bi < exec.bmff_property_summary.size(); ++bi) {
                const EmittedBmffPropertySummary& one
                    = exec.bmff_property_summary[bi];
                std::printf("  bmff_property %s count=%u bytes=%llu\n",
                            bmff_property_name(one.property_type,
                                               one.property_subtype)
                                .c_str(),
                            one.count,
                            static_cast<unsigned long long>(one.bytes));
            }
            continue;
        }

        std::fprintf(stderr, "  emit: unsupported bundle target format\n");
        any_failed = true;
    }

    return any_failed ? 1 : 0;
}
