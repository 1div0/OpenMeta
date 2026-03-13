#pragma once

#include "openmeta/interop_export.h"
#include "openmeta/metadata_transfer.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <string_view>
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
    /// Append derived normalized DNG CCM fields (e.g. `DNGNorm:*`) to typed exports.
    bool include_normalized_ccm = true;

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
    bool include_normalized_ccm  = true;
};

/// Semantic block kind for OIIO-facing transfer payload export.
enum class OiioTransferPayloadKind : uint8_t {
    Unknown = 0,
    ExifBlob,
    XmpPacket,
    IccProfile,
    IptcBlock,
    Jumbf,
    C2pa,
};

/// One zero-copy prepared transfer payload view for OIIO-style hosts.
struct OiioTransferPayloadView final {
    OiioTransferPayloadKind semantic_kind = OiioTransferPayloadKind::Unknown;
    std::string_view semantic_name;
    std::string_view route;
    PreparedTransferAdapterOp op;
    std::span<const std::byte> payload;
};

/// One owned prepared transfer payload for OIIO-style hosts.
struct OiioTransferPayload final {
    OiioTransferPayloadKind semantic_kind = OiioTransferPayloadKind::Unknown;
    std::string semantic_name;
    std::string route;
    PreparedTransferAdapterOp op;
    std::vector<std::byte> payload;
};

/// One owned OIIO-facing transfer payload batch.
struct OiioTransferPayloadBatch final {
    uint32_t contract_version          = 0;
    TransferTargetFormat target_format = TransferTargetFormat::Jpeg;
    EmitTransferOptions emit;
    std::vector<OiioTransferPayload> payloads;
};

/// One zero-copy packaged transfer view for OIIO-style hosts.
struct OiioTransferPackageView final {
    OiioTransferPayloadKind semantic_kind = OiioTransferPayloadKind::Unknown;
    std::string_view semantic_name;
    std::string_view route;
    TransferPackageChunkKind package_kind
        = TransferPackageChunkKind::SourceRange;
    uint64_t output_offset   = 0;
    uint8_t jpeg_marker_code = 0U;
    std::span<const std::byte> bytes;
};

/// Replay callbacks for \ref replay_oiio_transfer_package_batch.
struct OiioTransferPackageReplayCallbacks final {
    TransferStatus (*begin_batch)(void* user,
                                  TransferTargetFormat target_format,
                                  uint32_t chunk_count) noexcept
        = nullptr;
    TransferStatus (*emit_chunk)(void* user,
                                 const OiioTransferPackageView* view) noexcept
        = nullptr;
    TransferStatus (*end_batch)(void* user,
                                TransferTargetFormat target_format) noexcept
        = nullptr;
    void* user = nullptr;
};

/// Result for OIIO package-batch replay.
struct OiioTransferPackageReplayResult final {
    TransferStatus status       = TransferStatus::Ok;
    EmitTransferCode code       = EmitTransferCode::None;
    uint32_t replayed           = 0;
    uint32_t failed_chunk_index = std::numeric_limits<uint32_t>::max();
    std::string message;
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

/**
 * \brief Builds one OIIO-facing zero-copy transfer payload list from a
 *        prepared bundle.
 *
 * The returned payload views borrow both route strings and payload bytes from
 * \p bundle. The caller must keep \p bundle alive while consuming the views.
 */
EmitTransferResult
collect_oiio_transfer_payload_views(const PreparedTransferBundle& bundle,
                                    std::vector<OiioTransferPayloadView>* out,
                                    const EmitTransferOptions& options
                                    = EmitTransferOptions {}) noexcept;

/**
 * \brief Builds one owned OIIO-facing transfer payload batch from a prepared
 *        bundle.
 *
 * Unlike \ref collect_oiio_transfer_payload_views, the resulting payloads own
 * their bytes and can outlive \p bundle.
 */
EmitTransferResult
build_oiio_transfer_payload_batch(const PreparedTransferBundle& bundle,
                                  OiioTransferPayloadBatch* out,
                                  const EmitTransferOptions& options
                                  = EmitTransferOptions {}) noexcept;

/**
 * \brief Builds one zero-copy OIIO-facing package view list from a persisted
 *        transfer package batch.
 *
 * The returned views borrow route strings and chunk bytes from \p batch. This
 * is the host-facing path for stable packaged transfer output that no longer
 * depends on the original prepared bundle lifetime.
 */
EmitTransferResult
collect_oiio_transfer_package_views(
    const PreparedTransferPackageBatch& batch,
    std::vector<OiioTransferPackageView>* out) noexcept;

/**
 * \brief Replays one persisted transfer package batch through explicit host
 *        callbacks.
 *
 * \ref OiioTransferPackageReplayCallbacks::emit_chunk must be non-null.
 * \ref OiioTransferPackageReplayCallbacks::begin_batch and
 * \ref OiioTransferPackageReplayCallbacks::end_batch are optional.
 */
OiioTransferPackageReplayResult
replay_oiio_transfer_package_batch(
    const PreparedTransferPackageBatch& batch,
    const OiioTransferPackageReplayCallbacks& callbacks) noexcept;

}  // namespace openmeta
