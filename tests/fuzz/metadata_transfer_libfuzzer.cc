// SPDX-License-Identifier: Apache-2.0

#include "openmeta/metadata_transfer.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace openmeta {
namespace {

    struct ReplayState final {
        uint32_t count = 0U;
        uint64_t bytes = 0U;
    };


    static TransferStatus replay_begin_payload(void* user, TransferTargetFormat,
                                               uint32_t) noexcept
    {
        return user ? TransferStatus::Ok : TransferStatus::InvalidArgument;
    }


    static TransferStatus
    replay_emit_payload(void* user,
                        const PreparedTransferPayloadView* view) noexcept
    {
        if (!user || !view) {
            return TransferStatus::InvalidArgument;
        }
        ReplayState* state = static_cast<ReplayState*>(user);
        state->count += 1U;
        state->bytes += static_cast<uint64_t>(view->payload.size());
        return TransferStatus::Ok;
    }


    static TransferStatus replay_end_payload(void* user,
                                             TransferTargetFormat) noexcept
    {
        return user ? TransferStatus::Ok : TransferStatus::InvalidArgument;
    }


    static TransferStatus replay_begin_package(void* user, TransferTargetFormat,
                                               uint32_t) noexcept
    {
        return user ? TransferStatus::Ok : TransferStatus::InvalidArgument;
    }


    static TransferStatus
    replay_emit_package(void* user,
                        const PreparedTransferPackageView* view) noexcept
    {
        if (!user || !view) {
            return TransferStatus::InvalidArgument;
        }
        ReplayState* state = static_cast<ReplayState*>(user);
        state->count += 1U;
        state->bytes += static_cast<uint64_t>(view->bytes.size());
        return TransferStatus::Ok;
    }


    static TransferStatus replay_end_package(void* user,
                                             TransferTargetFormat) noexcept
    {
        return user ? TransferStatus::Ok : TransferStatus::InvalidArgument;
    }


    static void fuzz_payload_batch(const PreparedTransferPayloadBatch& batch)
    {
        std::vector<PreparedTransferPayloadView> views;
        (void)collect_prepared_transfer_payload_views(batch, &views);

        ReplayState state;
        PreparedTransferPayloadReplayCallbacks callbacks;
        callbacks.begin_batch  = replay_begin_payload;
        callbacks.emit_payload = replay_emit_payload;
        callbacks.end_batch    = replay_end_payload;
        callbacks.user         = &state;
        (void)replay_prepared_transfer_payload_batch(batch, callbacks);

        std::vector<std::byte> encoded;
        (void)serialize_prepared_transfer_payload_batch(batch, &encoded);
    }


    static void fuzz_package_batch(const PreparedTransferPackageBatch& batch)
    {
        std::vector<PreparedTransferPackageView> views;
        (void)collect_prepared_transfer_package_views(batch, &views);

        ReplayState state;
        PreparedTransferPackageReplayCallbacks callbacks;
        callbacks.begin_batch = replay_begin_package;
        callbacks.emit_chunk  = replay_emit_package;
        callbacks.end_batch   = replay_end_package;
        callbacks.user        = &state;
        (void)replay_prepared_transfer_package_batch(batch, callbacks);

        std::vector<std::byte> encoded;
        (void)serialize_prepared_transfer_package_batch(batch, &encoded);
    }


    static void fuzz_artifact_bytes(std::span<const std::byte> bytes)
    {
        PreparedTransferArtifactInfo info;
        (void)inspect_prepared_transfer_artifact(bytes, &info);

        PreparedTransferPayloadBatch payload_batch;
        const PreparedTransferPayloadIoResult payload_result
            = deserialize_prepared_transfer_payload_batch(bytes,
                                                          &payload_batch);
        if (payload_result.status == TransferStatus::Ok) {
            fuzz_payload_batch(payload_batch);
        }

        PreparedTransferPackageBatch package_batch;
        const PreparedTransferPackageIoResult package_result
            = deserialize_prepared_transfer_package_batch(bytes,
                                                          &package_batch);
        if (package_result.status == TransferStatus::Ok) {
            fuzz_package_batch(package_batch);
        }
    }


    static void fuzz_generated_jpeg_transfer(std::span<const std::byte> bytes)
    {
        PreparedTransferBundle bundle;
        bundle.target_format = TransferTargetFormat::Jpeg;

        PreparedTransferBlock block;
        block.kind = TransferBlockKind::Exif;
        block.route.assign("jpeg:app1-exif");

        if (!bytes.empty() && (std::to_integer<uint8_t>(bytes[0]) & 1U) != 0U) {
            block.kind = TransferBlockKind::Xmp;
            block.route.assign("jpeg:app1-xmp");
        }

        const size_t payload_off  = bytes.empty() ? 0U : 1U;
        const size_t payload_size = bytes.size() - payload_off;
        const size_t clamped_size = payload_size > 1024U ? 1024U : payload_size;
        block.payload.assign(
            bytes.begin() + static_cast<std::ptrdiff_t>(payload_off),
            bytes.begin()
                + static_cast<std::ptrdiff_t>(payload_off + clamped_size));
        bundle.blocks.push_back(std::move(block));

        PreparedTransferPayloadBatch payload_batch;
        const EmitTransferResult payload_result
            = build_prepared_transfer_payload_batch(bundle, &payload_batch);
        if (payload_result.status == TransferStatus::Ok) {
            std::vector<std::byte> encoded;
            const PreparedTransferPayloadIoResult serialized
                = serialize_prepared_transfer_payload_batch(payload_batch,
                                                            &encoded);
            if (serialized.status == TransferStatus::Ok) {
                PreparedTransferPayloadBatch decoded;
                const PreparedTransferPayloadIoResult parsed
                    = deserialize_prepared_transfer_payload_batch(encoded,
                                                                  &decoded);
                if (parsed.status == TransferStatus::Ok) {
                    fuzz_payload_batch(decoded);
                }
            }
        }

        PreparedTransferPackagePlan plan;
        const EmitTransferResult planned
            = build_prepared_transfer_emit_package(bundle, &plan);
        if (planned.status != TransferStatus::Ok) {
            return;
        }

        const std::vector<std::byte> empty_input;
        PreparedTransferPackageBatch package_batch;
        const EmitTransferResult built
            = build_prepared_transfer_package_batch(empty_input, bundle, plan,
                                                    &package_batch);
        if (built.status != TransferStatus::Ok) {
            return;
        }

        std::vector<std::byte> encoded;
        const PreparedTransferPackageIoResult serialized
            = serialize_prepared_transfer_package_batch(package_batch,
                                                        &encoded);
        if (serialized.status != TransferStatus::Ok) {
            return;
        }

        PreparedTransferPackageBatch decoded;
        const PreparedTransferPackageIoResult parsed
            = deserialize_prepared_transfer_package_batch(encoded, &decoded);
        if (parsed.status == TransferStatus::Ok) {
            fuzz_package_batch(decoded);
        }
    }

}  // namespace
}  // namespace openmeta

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               data),
                                           size);
    if (bytes.size() > 1024U * 1024U) {
        return 0;
    }

    openmeta::fuzz_artifact_bytes(bytes);
    openmeta::fuzz_generated_jpeg_transfer(bytes);
    return 0;
}
