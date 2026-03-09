#pragma once

#include "openmeta/meta_store.h"
#include "openmeta/resource_policy.h"
#include "openmeta/simple_meta.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

/**
 * \file metadata_transfer.h
 * \brief Draft metadata transfer bundle and backend emitter contracts.
 *
 * This header defines a stable draft contract for "prepare once, emit many"
 * metadata transfer workflows.
 */

namespace openmeta {

/// Stable metadata transfer contract version.
inline constexpr uint32_t kMetadataTransferContractVersion = 1U;

/// Target container family for prepared transfer bundles.
enum class TransferTargetFormat : uint8_t {
    Jpeg,
    Tiff,
    Jxl,
    Exr,
};

/// Prepared payload block category.
enum class TransferBlockKind : uint8_t {
    Exif,
    Xmp,
    IptcIim,
    PhotoshopIrb,
    Icc,
    Jumbf,
    C2pa,
    ExrAttribute,
    Other,
};

/// Status for transfer preparation and emit APIs.
enum class TransferStatus : uint8_t {
    Ok,
    InvalidArgument,
    Unsupported,
    LimitExceeded,
    Malformed,
    UnsafeData,
    InternalError,
};

/// Stable preparation error code for \ref PrepareTransferResult.
enum class PrepareTransferCode : uint16_t {
    None = 0,
    NullOutBundle,
    UnsupportedTargetFormat,
    ExifPackFailed,
    XmpPackFailed,
    IccPackFailed,
    IptcPackFailed,
    RequestedMetadataNotSerializable,
};

/// Stable emit error code for \ref EmitTransferResult.
enum class EmitTransferCode : uint16_t {
    None = 0,
    InvalidArgument,
    BundleTargetNotJpeg,
    UnsupportedRoute,
    BackendWriteFailed,
    PlanMismatch,
};

/// Stable file/read/prepare error code for \ref PrepareTransferFileResult.
enum class PrepareTransferFileCode : uint16_t {
    None = 0,
    EmptyPath,
    MapFailed,
    PayloadBufferPlatformLimit,
    DecodeFailed,
};

/// Status for high-level file-to-bundle transfer preparation.
enum class TransferFileStatus : uint8_t {
    Ok,
    InvalidArgument,
    OpenFailed,
    StatFailed,
    TooLarge,
    MapFailed,
    ReadFailed,
};

/// Transfer-policy subject handled during bundle preparation.
enum class TransferPolicySubject : uint8_t {
    MakerNote,
    Jumbf,
    C2pa,
};

/// Requested/effective action for a metadata family during transfer.
enum class TransferPolicyAction : uint8_t {
    Keep,
    Drop,
    Invalidate,
    Rewrite,
};

/// Reason attached to one prepared transfer policy decision.
enum class TransferPolicyReason : uint8_t {
    Default,
    NotPresent,
    ExplicitDrop,
    CarrierDisabled,
    PortableInvalidationUnavailable,
    RewriteUnavailablePreservedRaw,
    TargetSerializationUnavailable,
};

/// Transfer policy profile for initial no-edits workflows.
struct TransferProfile final {
    TransferPolicyAction makernote = TransferPolicyAction::Keep;
    TransferPolicyAction jumbf     = TransferPolicyAction::Keep;
    TransferPolicyAction c2pa      = TransferPolicyAction::Keep;
    bool allow_time_patch          = true;
};

/// Effective policy decision captured during bundle preparation.
struct PreparedTransferPolicyDecision final {
    TransferPolicySubject subject  = TransferPolicySubject::MakerNote;
    TransferPolicyAction requested = TransferPolicyAction::Keep;
    TransferPolicyAction effective = TransferPolicyAction::Keep;
    TransferPolicyReason reason    = TransferPolicyReason::Default;
    uint32_t matched_entries       = 0;
    std::string message;
};

/// Optional fixed-width patch field identifiers for per-frame updates.
enum class TimePatchField : uint8_t {
    DateTime,
    DateTimeOriginal,
    DateTimeDigitized,
    SubSecTime,
    SubSecTimeOriginal,
    SubSecTimeDigitized,
    OffsetTime,
    OffsetTimeOriginal,
    OffsetTimeDigitized,
    GpsDateStamp,
    GpsTimeStamp,
};

/// One fixed-width patch location in a prepared payload block.
struct TimePatchSlot final {
    TimePatchField field = TimePatchField::DateTime;
    uint32_t block_index = 0;
    uint32_t byte_offset = 0;
    uint16_t width       = 0;
};

/// One container-ready payload in emit order.
struct PreparedTransferBlock final {
    TransferBlockKind kind = TransferBlockKind::Other;
    uint32_t order         = 0;
    /// Route token for backend dispatch, for example: `jpeg:app1-exif`.
    std::string route;
    /// Optional 4CC when route is box-based (`Exif`, `xml `, `jumb`...).
    std::array<char, 4> box_type = { '\0', '\0', '\0', '\0' };
    /// Payload bytes as prepared by the transfer packager.
    std::vector<std::byte> payload;
};

/// Immutable prepared metadata transfer artifact.
struct PreparedTransferBundle final {
    uint32_t contract_version          = kMetadataTransferContractVersion;
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    TransferProfile profile;
    std::vector<PreparedTransferPolicyDecision> policy_decisions;
    std::vector<PreparedTransferBlock> blocks;
    std::vector<TimePatchSlot> time_patch_map;
};

/// Request options for preparation.
struct PrepareTransferRequest final {
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    TransferProfile profile;
    bool include_exif_app1              = true;
    bool include_xmp_app1               = true;
    bool include_icc_app2               = true;
    bool include_iptc_app13             = true;
    bool xmp_portable                   = true;
    bool xmp_include_existing           = true;
    bool xmp_exiftool_gpsdatetime_alias = false;
};

/// Result details for preparation.
struct PrepareTransferResult final {
    TransferStatus status    = TransferStatus::Ok;
    PrepareTransferCode code = PrepareTransferCode::None;
    uint32_t warnings        = 0;
    uint32_t errors          = 0;
    std::string message;
};

/// Result details for bundle emission.
struct EmitTransferResult final {
    TransferStatus status       = TransferStatus::Ok;
    EmitTransferCode code       = EmitTransferCode::None;
    uint32_t emitted            = 0;
    uint32_t skipped            = 0;
    uint32_t errors             = 0;
    uint32_t failed_block_index = 0xFFFFFFFFU;
    std::string message;
};

/// Draft JPEG edit strategy selection.
enum class JpegEditMode : uint8_t {
    Auto,
    InPlace,
    MetadataRewrite,
};

/// Options for JPEG edit planning.
struct PlanJpegEditOptions final {
    JpegEditMode mode        = JpegEditMode::Auto;
    bool require_in_place    = false;
    bool skip_empty_payloads = true;
};

/// Planned JPEG edit summary (draft API).
struct JpegEditPlan final {
    TransferStatus status       = TransferStatus::Ok;
    JpegEditMode requested_mode = JpegEditMode::Auto;
    JpegEditMode selected_mode  = JpegEditMode::MetadataRewrite;
    bool in_place_possible      = false;
    uint32_t emitted_segments   = 0;
    uint32_t replaced_segments  = 0;
    uint32_t appended_segments  = 0;
    uint64_t input_size         = 0;
    uint64_t output_size        = 0;
    uint64_t leading_scan_end   = 0;
    std::string message;
};

/// Options for TIFF edit planning.
struct PlanTiffEditOptions final {
    /// If true, fail planning when the bundle has no TIFF-applicable updates.
    bool require_updates = true;
};

/// Planned TIFF edit summary (draft API).
struct TiffEditPlan final {
    TransferStatus status = TransferStatus::Ok;
    uint32_t tag_updates  = 0;
    bool has_exif_ifd     = false;
    uint64_t input_size   = 0;
    uint64_t output_size  = 0;
    std::string message;
};

/// One precompiled JPEG emit operation (route -> marker mapping).
struct PreparedJpegEmitOp final {
    uint32_t block_index = 0;
    uint8_t marker_code  = 0;
};

/// Reusable precompiled JPEG emit plan for a prepared transfer bundle.
struct PreparedJpegEmitPlan final {
    uint32_t contract_version = kMetadataTransferContractVersion;
    std::vector<PreparedJpegEmitOp> ops;
};

/// One precompiled TIFF emit operation (route -> TIFF tag mapping).
struct PreparedTiffEmitOp final {
    uint32_t block_index = 0;
    uint16_t tiff_tag    = 0;
};

/// Reusable precompiled TIFF emit plan for a prepared transfer bundle.
struct PreparedTiffEmitPlan final {
    uint32_t contract_version = kMetadataTransferContractVersion;
    std::vector<PreparedTiffEmitOp> ops;
};

/// Emission options shared by backend emit entry points.
struct EmitTransferOptions final {
    bool skip_empty_payloads = true;
    bool stop_on_error       = true;
};

/// Reusable compiled execution plan for high-throughput transfer workflows.
struct PreparedTransferExecutionPlan final {
    uint32_t contract_version          = kMetadataTransferContractVersion;
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    EmitTransferOptions emit;
    PreparedJpegEmitPlan jpeg_emit;
    PreparedTiffEmitPlan tiff_emit;
};

/// File-read + decode options for \ref prepare_metadata_for_target_file.
struct PrepareTransferFileOptions final {
    bool include_pointer_tags       = true;
    bool decode_makernote           = false;
    bool decode_embedded_containers = true;
    bool decompress                 = true;

    OpenMetaResourcePolicy policy;
    PrepareTransferRequest prepare;
};

/// High-level file read/prepare result.
struct PrepareTransferFileResult final {
    TransferFileStatus file_status = TransferFileStatus::Ok;
    PrepareTransferFileCode code   = PrepareTransferFileCode::None;
    uint64_t file_size             = 0;
    uint32_t entry_count           = 0;

    SimpleMetaResult read;
    PrepareTransferResult prepare;
    PreparedTransferBundle bundle;
};

/// One time patch update payload for \ref apply_time_patches.
struct TimePatchUpdate final {
    TimePatchField field = TimePatchField::DateTime;
    std::vector<std::byte> value;
};

/// Non-owning time patch view for hot-path transfer execution.
struct TimePatchView final {
    TimePatchField field = TimePatchField::DateTime;
    std::span<const std::byte> value;
};

/// Options for \ref apply_time_patches.
struct ApplyTimePatchOptions final {
    /// If true, update value size must match slot width exactly.
    bool strict_width = true;
    /// If true, each requested field must have at least one slot.
    bool require_slot = false;
};

/// Result for \ref apply_time_patches.
struct ApplyTimePatchResult final {
    TransferStatus status  = TransferStatus::Ok;
    uint32_t patched_slots = 0;
    uint32_t skipped_slots = 0;
    uint32_t errors        = 0;
    std::string message;
};

/// One high-level time patch input for transfer execution helpers.
struct TransferTimePatchInput final {
    TimePatchField field = TimePatchField::DateTime;
    std::vector<std::byte> value;
    bool text_value = false;
};

/// One emitted JPEG marker summary entry.
struct EmittedJpegMarkerSummary final {
    uint8_t marker = 0;
    uint32_t count = 0;
    uint64_t bytes = 0;
};

/// One emitted TIFF tag summary entry.
struct EmittedTiffTagSummary final {
    uint16_t tag   = 0;
    uint32_t count = 0;
    uint64_t bytes = 0;
};

/// Streaming byte sink for edit/write transfer paths.
class TransferByteWriter {
public:
    virtual ~TransferByteWriter() = default;
    virtual TransferStatus write(std::span<const std::byte> bytes) noexcept = 0;
    virtual uint64_t remaining_capacity_hint() const noexcept
    {
        return UINT64_MAX;
    }
};

/// Fixed-buffer writer for encoder integrations with preallocated memory.
class SpanTransferByteWriter final : public TransferByteWriter {
public:
    explicit SpanTransferByteWriter(std::span<std::byte> buffer) noexcept
        : buffer_(buffer)
    {
    }

    TransferStatus write(std::span<const std::byte> bytes) noexcept override
    {
        if (status_ != TransferStatus::Ok) {
            return status_;
        }
        if (bytes.empty()) {
            return TransferStatus::Ok;
        }
        if (bytes.size() > remaining()) {
            status_ = TransferStatus::LimitExceeded;
            return status_;
        }
        std::memcpy(buffer_.data() + written_, bytes.data(), bytes.size());
        written_ += bytes.size();
        return TransferStatus::Ok;
    }

    void reset() noexcept
    {
        written_ = 0U;
        status_  = TransferStatus::Ok;
    }

    size_t capacity() const noexcept { return buffer_.size(); }
    size_t bytes_written() const noexcept { return written_; }
    size_t remaining() const noexcept { return buffer_.size() - written_; }
    TransferStatus status() const noexcept { return status_; }
    uint64_t remaining_capacity_hint() const noexcept override
    {
        return static_cast<uint64_t>(remaining());
    }

    std::span<const std::byte> written_bytes() const noexcept
    {
        return std::span<const std::byte>(buffer_.data(), written_);
    }

private:
    std::span<std::byte> buffer_;
    size_t written_        = 0U;
    TransferStatus status_ = TransferStatus::Ok;
};

/// Options for \ref execute_prepared_transfer.
struct ExecutePreparedTransferOptions final {
    std::vector<TransferTimePatchInput> time_patches;
    ApplyTimePatchOptions time_patch;
    bool time_patch_auto_nul = true;

    EmitTransferOptions emit;
    uint32_t emit_repeat                   = 1;
    TransferByteWriter* emit_output_writer = nullptr;

    bool edit_requested                    = false;
    bool edit_apply                        = false;
    TransferByteWriter* edit_output_writer = nullptr;
    PlanJpegEditOptions jpeg_edit;
    PlanTiffEditOptions tiff_edit;
};

/// Result for \ref execute_prepared_transfer.
struct ExecutePreparedTransferResult final {
    ApplyTimePatchResult time_patch;

    EmitTransferResult compile;
    uint32_t compiled_ops = 0;

    EmitTransferResult emit;
    std::vector<EmittedJpegMarkerSummary> marker_summary;
    std::vector<EmittedTiffTagSummary> tiff_tag_summary;
    bool tiff_commit          = false;
    uint64_t emit_output_size = 0;

    bool edit_requested             = false;
    TransferStatus edit_plan_status = TransferStatus::Unsupported;
    std::string edit_plan_message;
    JpegEditPlan jpeg_edit_plan;
    TiffEditPlan tiff_edit_plan;
    EmitTransferResult edit_apply;
    uint64_t edit_input_size  = 0;
    uint64_t edit_output_size = 0;
    std::vector<std::byte> edited_output;
};

/// Options for \ref execute_prepared_transfer_file.
struct ExecutePreparedTransferFileOptions final {
    PrepareTransferFileOptions prepare;
    ExecutePreparedTransferOptions execute;
    std::string edit_target_path;
};

/// Result for \ref execute_prepared_transfer_file.
struct ExecutePreparedTransferFileResult final {
    PrepareTransferFileResult prepared;
    ExecutePreparedTransferResult execute;
};

/**
 * \brief Backend contract for JPEG metadata emission.
 */
class JpegTransferEmitter {
public:
    virtual ~JpegTransferEmitter() = default;
    virtual TransferStatus
    write_app_marker(uint8_t marker_code,
                     std::span<const std::byte> payload) noexcept
        = 0;
};

/**
 * \brief Backend contract for TIFF metadata emission.
 */
class TiffTransferEmitter {
public:
    virtual ~TiffTransferEmitter() = default;
    virtual TransferStatus set_tag_u32(uint16_t tag, uint32_t value) noexcept
        = 0;
    virtual TransferStatus
    set_tag_bytes(uint16_t tag, std::span<const std::byte> payload) noexcept
        = 0;
    virtual TransferStatus
    commit_exif_directory(uint64_t* out_ifd_offset) noexcept
        = 0;
};

/**
 * \brief Backend contract for JPEG XL metadata box emission.
 */
class JxlTransferEmitter {
public:
    virtual ~JxlTransferEmitter() = default;
    virtual TransferStatus add_box(std::array<char, 4> type,
                                   std::span<const std::byte> payload,
                                   bool compress) noexcept
        = 0;
    virtual TransferStatus close_boxes() noexcept = 0;
};

/// EXR attribute payload for transfer emission.
struct ExrPreparedAttribute final {
    std::string name;
    std::string type_name;
    std::vector<std::byte> value;
    bool is_opaque = false;
};

/**
 * \brief Backend contract for OpenEXR header attribute emission.
 */
class ExrTransferEmitter {
public:
    virtual ~ExrTransferEmitter() = default;
    virtual TransferStatus
    set_attribute(const ExrPreparedAttribute& attr) noexcept
        = 0;
};

/// \brief Draft bundle preparation entry point.
PrepareTransferResult
prepare_metadata_for_target(const MetaStore&, const PrepareTransferRequest&,
                            PreparedTransferBundle* out_bundle) noexcept;

/**
 * \brief Emit prepared metadata blocks into a JPEG backend.
 *
 * Route mapping:
 * - `jpeg:app1-exif` -> APP1 (0xE1)
 * - `jpeg:app1-xmp` -> APP1 (0xE1)
 * - `jpeg:app2-icc` -> APP2 (0xE2)
 * - `jpeg:app13-iptc` -> APP13 (0xED)
 * - `jpeg:appN` / `jpeg:appN-*` where `N` in [0,15]
 * - `jpeg:com` -> COM (0xFE)
 */
EmitTransferResult
emit_prepared_bundle_jpeg(const PreparedTransferBundle& bundle,
                          JpegTransferEmitter& emitter,
                          const EmitTransferOptions& options
                          = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit prepared metadata blocks into a TIFF backend.
 *
 * Route mapping:
 * - `tiff:tag-700-xmp` -> TIFF tag 700 (XMP packet)
 * - `tiff:ifd-exif-app1` -> serialized EXIF APP1 input for ExifIFD materialization
 * - `tiff:tag-34675-icc` -> TIFF tag 34675 (ICC profile)
 * - `tiff:tag-33723-iptc` -> TIFF tag 33723 (IPTC IIM stream)
 */
EmitTransferResult
emit_prepared_bundle_tiff(const PreparedTransferBundle& bundle,
                          TiffTransferEmitter& emitter,
                          const EmitTransferOptions& options
                          = EmitTransferOptions {}) noexcept;

/**
 * \brief Compile a reusable TIFF emit plan from a prepared bundle.
 *
 * This maps route strings to TIFF tags once for high-throughput
 * "prepare once, emit many" workflows.
 */
EmitTransferResult
compile_prepared_bundle_tiff(const PreparedTransferBundle& bundle,
                             PreparedTiffEmitPlan* out_plan,
                             const EmitTransferOptions& options
                             = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit a prepared bundle using a precompiled TIFF emit plan.
 */
EmitTransferResult
emit_prepared_bundle_tiff_compiled(const PreparedTransferBundle& bundle,
                                   const PreparedTiffEmitPlan& plan,
                                   TiffTransferEmitter& emitter,
                                   const EmitTransferOptions& options
                                   = EmitTransferOptions {}) noexcept;

/**
 * \brief Compile a reusable JPEG emit plan from a prepared bundle.
 *
 * This maps route strings to marker codes once for high-throughput
 * "prepare once, emit many" workflows.
 */
EmitTransferResult
compile_prepared_bundle_jpeg(const PreparedTransferBundle& bundle,
                             PreparedJpegEmitPlan* out_plan,
                             const EmitTransferOptions& options
                             = EmitTransferOptions {}) noexcept;

/**
 * \brief Emit a prepared bundle using a precompiled JPEG emit plan.
 */
EmitTransferResult
emit_prepared_bundle_jpeg_compiled(const PreparedTransferBundle& bundle,
                                   const PreparedJpegEmitPlan& plan,
                                   JpegTransferEmitter& emitter,
                                   const EmitTransferOptions& options
                                   = EmitTransferOptions {}) noexcept;

/**
 * \brief Write prepared JPEG metadata marker bytes directly to a byte sink.
 *
 * This emits serialized APP/COM marker records only. It does not write SOI,
 * SOS/image data, or EOI.
 */
EmitTransferResult
write_prepared_bundle_jpeg(const PreparedTransferBundle& bundle,
                           TransferByteWriter& writer,
                           const EmitTransferOptions& options
                           = EmitTransferOptions {}) noexcept;

/**
 * \brief Write prepared JPEG metadata marker bytes using a precompiled plan.
 */
EmitTransferResult
write_prepared_bundle_jpeg_compiled(const PreparedTransferBundle& bundle,
                                    const PreparedJpegEmitPlan& plan,
                                    TransferByteWriter& writer,
                                    const EmitTransferOptions& options
                                    = EmitTransferOptions {}) noexcept;

/**
 * \brief High-level helper: read file, decode metadata, and prepare transfer bundle.
 *
 * This helper is intended for thin wrappers (CLI/Python) that need
 * `read -> prepare` with consistent resource policies.
 */
PrepareTransferFileResult
prepare_metadata_for_target_file(const char* path,
                                 const PrepareTransferFileOptions& options
                                 = PrepareTransferFileOptions {}) noexcept;

/**
 * \brief Compile a reusable execution plan for \ref execute_prepared_transfer_compiled.
 *
 * This performs route-to-backend compilation once and stores the selected
 * emit options inside the plan for repeated runtime execution.
 */
EmitTransferResult
compile_prepared_transfer_execution(
    const PreparedTransferBundle& bundle, const EmitTransferOptions& options,
    PreparedTransferExecutionPlan* out_plan) noexcept;

/**
 * \brief Apply fixed-width time patch updates in-place on a prepared bundle.
 *
 * Patches are applied to byte ranges listed in `bundle.time_patch_map`
 * (typically filled for EXIF APP1 during prepare).
 */
ApplyTimePatchResult
apply_time_patches(PreparedTransferBundle* bundle,
                   std::span<const TimePatchUpdate> updates,
                   const ApplyTimePatchOptions& options
                   = ApplyTimePatchOptions {}) noexcept;

/**
 * \brief Apply fixed-width time patch views in-place on a prepared bundle.
 *
 * This overload avoids owned patch buffers and is intended for high-throughput
 * `prepare once -> compile once -> patch/emit many` integrations.
 */
ApplyTimePatchResult
apply_time_patches_view(PreparedTransferBundle* bundle,
                        std::span<const TimePatchView> updates,
                        const ApplyTimePatchOptions& options
                        = ApplyTimePatchOptions {}) noexcept;

/**
 * \brief Execute the high-level transfer flow on a prepared bundle.
 *
 * This helper applies optional time patches, compiles/emits prepared blocks,
 * and optionally plans/applies a target-stream edit using \p edit_input.
 * It is intended for thin wrappers and high-throughput "prepare once, reuse"
 * integrations.
 */
ExecutePreparedTransferResult
execute_prepared_transfer(PreparedTransferBundle* bundle,
                          std::span<const std::byte> edit_input = {},
                          const ExecutePreparedTransferOptions& options
                          = ExecutePreparedTransferOptions {}) noexcept;

/**
 * \brief Execute the transfer flow using a precompiled execution plan.
 *
 * This is intended for high-throughput integrations that want
 * `prepare once -> compile once -> patch/emit many`.
 */
ExecutePreparedTransferResult
execute_prepared_transfer_compiled(PreparedTransferBundle* bundle,
                                   const PreparedTransferExecutionPlan& plan,
                                   std::span<const std::byte> edit_input = {},
                                   const ExecutePreparedTransferOptions& options
                                   = ExecutePreparedTransferOptions {}) noexcept;

/**
 * \brief Hot-path helper: apply non-owning time patches and stream emit bytes.
 *
 * This is a thin encoder-integration helper for
 * `prepare once -> compile once -> patch -> write` workflows. It reuses
 * \ref execute_prepared_transfer_compiled for plan validation and emission,
 * but accepts non-owning patch views to avoid per-call owned patch buffers.
 */
ExecutePreparedTransferResult
write_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    TransferByteWriter& writer,
    std::span<const TimePatchView> time_patches = {},
    const ApplyTimePatchOptions& time_patch     = ApplyTimePatchOptions {},
    uint32_t emit_repeat                        = 1U) noexcept;

/**
 * \brief Hot-path helper: apply non-owning time patches and emit through a
 * JPEG backend.
 *
 * This is the direct backend-emitter path for `prepare once -> compile once ->
 * patch -> emit` integrations that already own a JPEG encoder/backend.
 */
ExecutePreparedTransferResult
emit_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    JpegTransferEmitter& emitter,
    std::span<const TimePatchView> time_patches = {},
    const ApplyTimePatchOptions& time_patch     = ApplyTimePatchOptions {},
    uint32_t emit_repeat                        = 1U) noexcept;

/**
 * \brief Hot-path helper: apply non-owning time patches and emit through a
 * TIFF backend.
 *
 * TIFF intentionally uses backend-emitter or rewrite/edit paths instead of a
 * metadata-only byte-writer emit contract.
 */
ExecutePreparedTransferResult
emit_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    TiffTransferEmitter& emitter,
    std::span<const TimePatchView> time_patches = {},
    const ApplyTimePatchOptions& time_patch     = ApplyTimePatchOptions {},
    uint32_t emit_repeat                        = 1U) noexcept;

/**
 * \brief High-level helper: read file, prepare a bundle, then execute transfer.
 *
 * This wraps \ref prepare_metadata_for_target_file plus
 * \ref execute_prepared_transfer for CLI/Python entry points that want a single
 * file-based execution path.
 */
ExecutePreparedTransferFileResult
execute_prepared_transfer_file(const char* path,
                               const ExecutePreparedTransferFileOptions& options
                               = ExecutePreparedTransferFileOptions {}) noexcept;

/**
 * \brief Plan JPEG metadata injection/edit strategy for a prepared bundle.
 *
 * `Auto` selects `InPlace` when all emitted payloads can replace existing
 * leading JPEG metadata segments with exact size match; otherwise it selects
 * `MetadataRewrite`.
 */
JpegEditPlan
plan_prepared_bundle_jpeg_edit(std::span<const std::byte> input_jpeg,
                               const PreparedTransferBundle& bundle,
                               const PlanJpegEditOptions& options
                               = PlanJpegEditOptions {}) noexcept;

/**
 * \brief Apply a planned JPEG metadata edit and produce edited output bytes.
 *
 * For `InPlace`, this replaces matched existing segment payload bytes in a
 * copy of the input stream. For `MetadataRewrite`, this rewrites leading
 * metadata segments and preserves the remaining codestream bytes unchanged.
 */
EmitTransferResult
apply_prepared_bundle_jpeg_edit(std::span<const std::byte> input_jpeg,
                                const PreparedTransferBundle& bundle,
                                const JpegEditPlan& plan,
                                std::vector<std::byte>* out_jpeg) noexcept;

/**
 * \brief Apply a planned JPEG metadata edit and stream output bytes to a writer.
 */
EmitTransferResult
write_prepared_bundle_jpeg_edit(std::span<const std::byte> input_jpeg,
                                const PreparedTransferBundle& bundle,
                                const JpegEditPlan& plan,
                                TransferByteWriter& writer) noexcept;

/**
 * \brief Plan TIFF metadata rewrite for a prepared bundle.
 *
 * This computes the expected output size and validates that TIFF-applicable
 * prepared blocks can be materialized on the target stream.
 */
TiffEditPlan
plan_prepared_bundle_tiff_edit(std::span<const std::byte> input_tiff,
                               const PreparedTransferBundle& bundle,
                               const PlanTiffEditOptions& options
                               = PlanTiffEditOptions {}) noexcept;

/**
 * \brief Apply a planned TIFF metadata rewrite and produce edited output bytes.
 */
EmitTransferResult
apply_prepared_bundle_tiff_edit(std::span<const std::byte> input_tiff,
                                const PreparedTransferBundle& bundle,
                                const TiffEditPlan& plan,
                                std::vector<std::byte>* out_tiff) noexcept;

/**
 * \brief Apply a planned TIFF metadata rewrite and stream output bytes to a writer.
 *
 * Current implementation uses the same rewrite path as
 * \ref apply_prepared_bundle_tiff_edit and then writes the final bytes to the
 * supplied sink.
 */
EmitTransferResult
write_prepared_bundle_tiff_edit(std::span<const std::byte> input_tiff,
                                const PreparedTransferBundle& bundle,
                                const TiffEditPlan& plan,
                                TransferByteWriter& writer) noexcept;

}  // namespace openmeta
