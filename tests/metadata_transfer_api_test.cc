// SPDX-License-Identifier: Apache-2.0

#include "openmeta/container_scan.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/simple_meta.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI \
    && defined(OPENMETA_HAS_BROTLI_ENCODER) && OPENMETA_HAS_BROTLI_ENCODER
#    include <brotli/encode.h>
#endif

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
#    include <zlib.h>
#endif

namespace {

class FakeJpegEmitter final : public openmeta::JpegTransferEmitter {
public:
    openmeta::TransferStatus
    write_app_marker(uint8_t marker_code,
                     std::span<const std::byte> payload) noexcept override
    {
        calls.emplace_back(marker_code, payload.size());
        if (fail_after_calls != 0U && calls.size() >= fail_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    std::vector<std::pair<uint8_t, size_t>> calls;
    size_t fail_after_calls = 0;
    openmeta::TransferStatus fail_status
        = openmeta::TransferStatus::InternalError;
};

class FakeTiffEmitter final : public openmeta::TiffTransferEmitter {
public:
    openmeta::TransferStatus set_tag_u32(uint16_t tag,
                                         uint32_t value) noexcept override
    {
        u32_calls.emplace_back(tag, value);
        if (fail_set_after_calls != 0U
            && (u32_calls.size() + bytes_calls.size())
                   >= fail_set_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus
    set_tag_bytes(uint16_t tag,
                  std::span<const std::byte> payload) noexcept override
    {
        bytes_calls.emplace_back(tag, payload.size());
        if (fail_set_after_calls != 0U
            && (u32_calls.size() + bytes_calls.size())
                   >= fail_set_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus
    commit_exif_directory(uint64_t* out_ifd_offset) noexcept override
    {
        commit_calls += 1U;
        if (out_ifd_offset) {
            *out_ifd_offset = 0U;
        }
        if (fail_commit) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    std::vector<std::pair<uint16_t, uint32_t>> u32_calls;
    std::vector<std::pair<uint16_t, size_t>> bytes_calls;
    size_t fail_set_after_calls = 0;
    bool fail_commit            = false;
    size_t commit_calls         = 0;
    openmeta::TransferStatus fail_status
        = openmeta::TransferStatus::InternalError;
};

class FakeJxlEmitter final : public openmeta::JxlTransferEmitter {
public:
    openmeta::TransferStatus
    set_icc_profile(std::span<const std::byte> payload) noexcept override
    {
        icc_calls += 1U;
        icc_bytes = payload.size();
        if (fail_icc) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus add_box(std::array<char, 4> type,
                                     std::span<const std::byte> payload,
                                     bool compress) noexcept override
    {
        calls.push_back({ type, payload.size(), compress });
        if (fail_after_calls != 0U && calls.size() >= fail_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus close_boxes() noexcept override
    {
        close_calls += 1U;
        if (fail_close) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    struct Call final {
        std::array<char, 4> type = { '\0', '\0', '\0', '\0' };
        size_t bytes             = 0U;
        bool compress            = false;
    };

    std::vector<Call> calls;
    size_t icc_calls        = 0U;
    size_t icc_bytes        = 0U;
    size_t close_calls      = 0U;
    size_t fail_after_calls = 0U;
    bool fail_icc           = false;
    bool fail_close         = false;
    openmeta::TransferStatus fail_status
        = openmeta::TransferStatus::InternalError;
};

class FakeWebpEmitter final : public openmeta::WebpTransferEmitter {
public:
    openmeta::TransferStatus
    add_chunk(std::array<char, 4> type,
              std::span<const std::byte> payload) noexcept override
    {
        calls.push_back({ type, payload.size() });
        if (fail_after_calls != 0U && calls.size() >= fail_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus close_chunks() noexcept override
    {
        close_calls += 1U;
        if (fail_close) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    struct Call final {
        std::array<char, 4> type = { '\0', '\0', '\0', '\0' };
        size_t bytes             = 0U;
    };

    std::vector<Call> calls;
    size_t close_calls      = 0U;
    size_t fail_after_calls = 0U;
    bool fail_close         = false;
    openmeta::TransferStatus fail_status
        = openmeta::TransferStatus::InternalError;
};

class FakePngEmitter final : public openmeta::PngTransferEmitter {
public:
    openmeta::TransferStatus
    add_chunk(std::array<char, 4> type,
              std::span<const std::byte> payload) noexcept override
    {
        calls.push_back({ type, payload.size() });
        if (fail_after_calls != 0U && calls.size() >= fail_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus close_chunks() noexcept override
    {
        close_calls += 1U;
        if (fail_close) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    struct Call final {
        std::array<char, 4> type = { '\0', '\0', '\0', '\0' };
        size_t bytes             = 0U;
    };

    std::vector<Call> calls;
    size_t close_calls      = 0U;
    size_t fail_after_calls = 0U;
    bool fail_close         = false;
    openmeta::TransferStatus fail_status
        = openmeta::TransferStatus::InternalError;
};

class FakeJp2Emitter final : public openmeta::Jp2TransferEmitter {
public:
    openmeta::TransferStatus
    add_box(std::array<char, 4> type,
            std::span<const std::byte> payload) noexcept override
    {
        calls.push_back({ type, payload.size() });
        if (fail_after_calls != 0U && calls.size() >= fail_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus close_boxes() noexcept override
    {
        close_calls += 1U;
        if (fail_close) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    struct Call final {
        std::array<char, 4> type = { '\0', '\0', '\0', '\0' };
        size_t bytes             = 0U;
    };

    std::vector<Call> calls;
    size_t close_calls      = 0U;
    size_t fail_after_calls = 0U;
    bool fail_close         = false;
    openmeta::TransferStatus fail_status
        = openmeta::TransferStatus::InternalError;
};

class FakeExrEmitter final : public openmeta::ExrTransferEmitter {
public:
    openmeta::TransferStatus
    set_attribute(const openmeta::ExrPreparedAttribute& attr) noexcept override
    {
        Call one;
        one.name      = attr.name;
        one.type_name = attr.type_name;
        one.bytes     = attr.value.size();
        calls.push_back(std::move(one));
        if (fail_after_calls != 0U && calls.size() >= fail_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus
    set_attribute_view(const openmeta::ExrPreparedAttributeView& attr) noexcept
        override
    {
        Call one;
        one.name.assign(attr.name.data(), attr.name.size());
        one.type_name.assign(attr.type_name.data(), attr.type_name.size());
        one.bytes = attr.value.size();
        calls.push_back(std::move(one));
        if (fail_after_calls != 0U && calls.size() >= fail_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    struct Call final {
        std::string name;
        std::string type_name;
        size_t bytes = 0U;
    };

    std::vector<Call> calls;
    size_t fail_after_calls = 0U;
    openmeta::TransferStatus fail_status
        = openmeta::TransferStatus::InternalError;
};

class FakeBmffEmitter final : public openmeta::BmffTransferEmitter {
public:
    openmeta::TransferStatus
    add_item(uint32_t item_type,
             std::span<const std::byte> payload) noexcept override
    {
        calls.push_back({ item_type, 0U, 0U, payload.size(), false, false });
        if (fail_after_calls != 0U && calls.size() >= fail_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus
    add_mime_xmp_item(std::span<const std::byte> payload) noexcept override
    {
        calls.push_back({ openmeta::fourcc('m', 'i', 'm', 'e'), 0U, 0U,
                          payload.size(), true, false });
        if (fail_after_calls != 0U && calls.size() >= fail_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus
    add_property(uint32_t property_type,
                 std::span<const std::byte> payload) noexcept override
    {
        uint32_t property_subtype = 0U;
        if (payload.size() >= 4U) {
            property_subtype
                = (static_cast<uint32_t>(std::to_integer<uint8_t>(payload[0]))
                   << 24)
                  | (static_cast<uint32_t>(std::to_integer<uint8_t>(payload[1]))
                     << 16)
                  | (static_cast<uint32_t>(std::to_integer<uint8_t>(payload[2]))
                     << 8)
                  | static_cast<uint32_t>(std::to_integer<uint8_t>(payload[3]));
        }
        calls.push_back({ 0U, property_type, property_subtype, payload.size(),
                          false, true });
        if (fail_after_calls != 0U && calls.size() >= fail_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    openmeta::TransferStatus close_items() noexcept override
    {
        close_calls += 1U;
        if (fail_close) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    struct Call final {
        uint32_t item_type        = 0U;
        uint32_t property_type    = 0U;
        uint32_t property_subtype = 0U;
        size_t bytes              = 0U;
        bool mime_xmp             = false;
        bool property             = false;
    };

    std::vector<Call> calls;
    size_t close_calls      = 0U;
    size_t fail_after_calls = 0U;
    bool fail_close         = false;
    openmeta::TransferStatus fail_status
        = openmeta::TransferStatus::InternalError;
};

class FakeTransferAdapterSink final : public openmeta::TransferAdapterSink {
public:
    openmeta::TransferStatus
    emit_op(const openmeta::PreparedTransferAdapterOp& op,
            std::span<const std::byte> payload) noexcept override
    {
        Call one;
        one.kind                  = op.kind;
        one.block_index           = op.block_index;
        one.payload_size          = payload.size();
        one.serialized_size       = op.serialized_size;
        one.jpeg_marker_code      = op.jpeg_marker_code;
        one.tiff_tag              = op.tiff_tag;
        one.box_type              = op.box_type;
        one.chunk_type            = op.chunk_type;
        one.bmff_item_type        = op.bmff_item_type;
        one.bmff_property_type    = op.bmff_property_type;
        one.bmff_property_subtype = op.bmff_property_subtype;
        one.bmff_mime_xmp         = op.bmff_mime_xmp;
        one.compress              = op.compress;
        calls.push_back(one);
        if (fail_after_calls != 0U && calls.size() >= fail_after_calls) {
            return fail_status;
        }
        return openmeta::TransferStatus::Ok;
    }

    struct Call final {
        openmeta::TransferAdapterOpKind kind
            = openmeta::TransferAdapterOpKind::JpegMarker;
        uint32_t block_index           = 0U;
        size_t payload_size            = 0U;
        uint64_t serialized_size       = 0U;
        uint8_t jpeg_marker_code       = 0U;
        uint16_t tiff_tag              = 0U;
        std::array<char, 4> box_type   = { '\0', '\0', '\0', '\0' };
        std::array<char, 4> chunk_type = { '\0', '\0', '\0', '\0' };
        uint32_t bmff_item_type        = 0U;
        uint32_t bmff_property_type    = 0U;
        uint32_t bmff_property_subtype = 0U;
        bool bmff_mime_xmp             = false;
        bool compress                  = false;
    };

    std::vector<Call> calls;
    size_t fail_after_calls = 0U;
    openmeta::TransferStatus fail_status
        = openmeta::TransferStatus::InternalError;
};

class BufferByteWriter final : public openmeta::TransferByteWriter {
public:
    openmeta::TransferStatus
    write(std::span<const std::byte> bytes) noexcept override
    {
        writes += 1U;
        if (fail_after_writes != 0U && writes >= fail_after_writes) {
            return fail_status;
        }
        out.insert(out.end(), bytes.begin(), bytes.end());
        return openmeta::TransferStatus::Ok;
    }

    std::vector<std::byte> out;
    size_t writes            = 0;
    size_t fail_after_writes = 0;
    openmeta::TransferStatus fail_status
        = openmeta::TransferStatus::InternalError;
};

struct PackageReplayState final {
    std::vector<std::string> events;
    uint32_t fail_on_chunk  = 0xFFFFFFFFU;
    uint32_t emitted_chunks = 0U;
};

static openmeta::TransferStatus
replay_begin_package(void* user, openmeta::TransferTargetFormat target,
                     uint32_t chunk_count) noexcept
{
    if (!user) {
        return openmeta::TransferStatus::InvalidArgument;
    }
    PackageReplayState* state = static_cast<PackageReplayState*>(user);
    state->events.push_back("begin:"
                            + std::to_string(static_cast<uint32_t>(target))
                            + ":" + std::to_string(chunk_count));
    return openmeta::TransferStatus::Ok;
}

static openmeta::TransferStatus
replay_emit_package_chunk(
    void* user, const openmeta::PreparedTransferPackageView* view) noexcept
{
    if (!user || !view) {
        return openmeta::TransferStatus::InvalidArgument;
    }
    PackageReplayState* state = static_cast<PackageReplayState*>(user);
    if (state->emitted_chunks == state->fail_on_chunk) {
        return openmeta::TransferStatus::Unsupported;
    }
    state->events.push_back(std::string("chunk:") + std::string(view->route)
                            + ":" + std::to_string(view->output_offset) + ":"
                            + std::to_string(view->bytes.size()));
    state->emitted_chunks += 1U;
    return openmeta::TransferStatus::Ok;
}

static openmeta::TransferStatus
replay_end_package(void* user, openmeta::TransferTargetFormat target) noexcept
{
    if (!user) {
        return openmeta::TransferStatus::InvalidArgument;
    }
    PackageReplayState* state = static_cast<PackageReplayState*>(user);
    state->events.push_back("end:"
                            + std::to_string(static_cast<uint32_t>(target)));
    return openmeta::TransferStatus::Ok;
}

static uint16_t
read_u16le(std::span<const std::byte> bytes, size_t off) noexcept
{
    return static_cast<uint16_t>(
        (static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[off + 0])) << 0)
        | (static_cast<uint16_t>(std::to_integer<uint8_t>(bytes[off + 1]))
           << 8));
}

static uint32_t
read_u32le(std::span<const std::byte> bytes, size_t off) noexcept
{
    return static_cast<uint32_t>(
        (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 0])) << 0)
        | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 1])) << 8)
        | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 2])) << 16)
        | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 3]))
           << 24));
}

static uint64_t
read_u64le(std::span<const std::byte> bytes, size_t off) noexcept
{
    return static_cast<uint64_t>(
        (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 0])) << 0)
        | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 1]))
           << 8)
        | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 2]))
           << 16)
        | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 3]))
           << 24)
        | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 4]))
           << 32)
        | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 5]))
           << 40)
        | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 6]))
           << 48)
        | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 7]))
           << 56));
}

static bool
find_tiff_tag_entry_le(std::span<const std::byte> bytes, uint32_t ifd_off,
                       uint16_t tag, uint16_t* out_type, uint32_t* out_count,
                       uint32_t* out_value_or_offset) noexcept
{
    const size_t base = static_cast<size_t>(ifd_off);
    if (base + 2U > bytes.size()) {
        return false;
    }
    const uint16_t count = read_u16le(bytes, base);
    size_t entry_pos     = base + 2U;
    for (uint16_t i = 0; i < count; ++i) {
        if (entry_pos + 12U > bytes.size()) {
            return false;
        }
        if (read_u16le(bytes, entry_pos + 0U) == tag) {
            if (out_type) {
                *out_type = read_u16le(bytes, entry_pos + 2U);
            }
            if (out_count) {
                *out_count = read_u32le(bytes, entry_pos + 4U);
            }
            if (out_value_or_offset) {
                *out_value_or_offset = read_u32le(bytes, entry_pos + 8U);
            }
            return true;
        }
        entry_pos += 12U;
    }
    return false;
}

static bool
find_bigtiff_tag_entry_le(std::span<const std::byte> bytes, uint64_t ifd_off,
                          uint16_t tag, uint16_t* out_type,
                          uint64_t* out_count,
                          uint64_t* out_value_or_offset) noexcept
{
    const size_t base = static_cast<size_t>(ifd_off);
    if (base + 8U > bytes.size()) {
        return false;
    }
    const uint64_t count = read_u64le(bytes, base);
    size_t entry_pos     = base + 8U;
    for (uint64_t i = 0U; i < count; ++i) {
        if (entry_pos + 20U > bytes.size()) {
            return false;
        }
        if (read_u16le(bytes, entry_pos + 0U) == tag) {
            if (out_type) {
                *out_type = read_u16le(bytes, entry_pos + 2U);
            }
            if (out_count) {
                *out_count = read_u64le(bytes, entry_pos + 4U);
            }
            if (out_value_or_offset) {
                *out_value_or_offset = read_u64le(bytes, entry_pos + 12U);
            }
            return true;
        }
        entry_pos += 20U;
    }
    return false;
}

static bool
exif_app1_contains_exififd_tag(std::span<const std::byte> payload,
                               uint16_t tag) noexcept
{
    if (payload.size() < 14U) {
        return false;
    }
    if (payload[0] != std::byte { 'E' } || payload[1] != std::byte { 'x' }
        || payload[2] != std::byte { 'i' } || payload[3] != std::byte { 'f' }) {
        return false;
    }
    const uint32_t ifd0_off = read_u32le(payload, 10U);
    uint32_t exififd_off    = 0U;
    if (!find_tiff_tag_entry_le(payload, 6U + ifd0_off, 0x8769U, nullptr,
                                nullptr, &exififd_off)) {
        return false;
    }
    return find_tiff_tag_entry_le(payload, 6U + exififd_off, tag, nullptr,
                                  nullptr, nullptr);
}

static const openmeta::PreparedTransferPolicyDecision*
find_policy_decision(const openmeta::PreparedTransferBundle& bundle,
                     openmeta::TransferPolicySubject subject) noexcept
{
    for (size_t i = 0; i < bundle.policy_decisions.size(); ++i) {
        if (bundle.policy_decisions[i].subject == subject) {
            return &bundle.policy_decisions[i];
        }
    }
    return nullptr;
}

static std::vector<std::byte>
ascii_z(const char* s)
{
    std::vector<std::byte> out;
    if (!s) {
        out.push_back(std::byte { 0x00 });
        return out;
    }
    const size_t n = std::strlen(s);
    out.reserve(n + 1U);
    for (size_t i = 0; i < n; ++i) {
        out.push_back(static_cast<std::byte>(static_cast<uint8_t>(s[i])));
    }
    out.push_back(std::byte { 0x00 });
    return out;
}

static void
append_u16be(std::vector<std::byte>* out, uint16_t v)
{
    out->push_back(static_cast<std::byte>((v >> 8U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 0U) & 0xFFU));
}

static void
append_u32be(std::vector<std::byte>* out, uint32_t v)
{
    out->push_back(static_cast<std::byte>((v >> 24U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 16U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 8U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 0U) & 0xFFU));
}

static void
append_u16le(std::vector<std::byte>* out, uint16_t v)
{
    out->push_back(static_cast<std::byte>((v >> 0U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 8U) & 0xFFU));
}

static void
append_u32le(std::vector<std::byte>* out, uint32_t v)
{
    out->push_back(static_cast<std::byte>((v >> 0U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 8U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 16U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 24U) & 0xFFU));
}

static void
append_u64le(std::vector<std::byte>* out, uint64_t v)
{
    out->push_back(static_cast<std::byte>((v >> 0U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 8U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 16U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 24U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 32U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 40U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 48U) & 0xFFU));
    out->push_back(static_cast<std::byte>((v >> 56U) & 0xFFU));
}

static void
append_png_chunk(std::vector<std::byte>* out, uint32_t type,
                 std::span<const std::byte> data)
{
    append_u32be(out, static_cast<uint32_t>(data.size()));
    append_u32be(out, type);
    out->insert(out->end(), data.begin(), data.end());
    append_u32be(out, 0U);
}

static std::vector<std::byte>
build_minimal_png_file(std::span<const std::byte> metadata_chunks)
{
    std::vector<std::byte> png = {
        std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
        std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
        std::byte { 0x1A }, std::byte { 0x0A },
    };

    std::vector<std::byte> ihdr;
    append_u32be(&ihdr, 1U);
    append_u32be(&ihdr, 1U);
    ihdr.push_back(std::byte { 8U });
    ihdr.push_back(std::byte { 2U });
    ihdr.push_back(std::byte { 0U });
    ihdr.push_back(std::byte { 0U });
    ihdr.push_back(std::byte { 0U });
    append_png_chunk(&png, openmeta::fourcc('I', 'H', 'D', 'R'), ihdr);
    png.insert(png.end(), metadata_chunks.begin(), metadata_chunks.end());
    append_png_chunk(&png, openmeta::fourcc('I', 'E', 'N', 'D'), {});
    return png;
}

static void
append_webp_chunk(std::vector<std::byte>* out, uint32_t type,
                  std::span<const std::byte> data)
{
    append_u32be(out, type);
    append_u32le(out, static_cast<uint32_t>(data.size()));
    out->insert(out->end(), data.begin(), data.end());
    if ((data.size() & 1U) != 0U) {
        out->push_back(std::byte { 0x00 });
    }
}

static std::vector<std::byte>
build_minimal_webp_file(std::span<const std::byte> metadata_chunks,
                        uint8_t vp8x_flags)
{
    std::vector<std::byte> webp = {
        std::byte { 'R' }, std::byte { 'I' }, std::byte { 'F' },
        std::byte { 'F' }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 'W' },
        std::byte { 'E' }, std::byte { 'B' }, std::byte { 'P' },
    };

    std::vector<std::byte> vp8x = {
        std::byte { vp8x_flags }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 },       std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 },       std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 },
    };
    append_webp_chunk(&webp, openmeta::fourcc('V', 'P', '8', 'X'), vp8x);
    webp.insert(webp.end(), metadata_chunks.begin(), metadata_chunks.end());
    const std::array<std::byte, 1> vp8_payload = { std::byte { 0x00 } };
    append_webp_chunk(&webp, openmeta::fourcc('V', 'P', '8', ' '),
                      std::span<const std::byte>(vp8_payload.data(),
                                                 vp8_payload.size()));

    const uint32_t riff_size = static_cast<uint32_t>(webp.size() - 8U);
    webp[4] = static_cast<std::byte>((riff_size >> 0U) & 0xFFU);
    webp[5] = static_cast<std::byte>((riff_size >> 8U) & 0xFFU);
    webp[6] = static_cast<std::byte>((riff_size >> 16U) & 0xFFU);
    webp[7] = static_cast<std::byte>((riff_size >> 24U) & 0xFFU);
    return webp;
}

static void
append_jp2_box(std::vector<std::byte>* out, uint32_t type,
               std::span<const std::byte> data)
{
    append_u32be(out, static_cast<uint32_t>(8U + data.size()));
    append_u32be(out, type);
    out->insert(out->end(), data.begin(), data.end());
}

static std::vector<std::byte>
build_test_jp2_colr_icc_box(std::span<const std::byte> icc_profile)
{
    std::vector<std::byte> payload;
    payload.push_back(std::byte { 0x02U });
    payload.push_back(std::byte { 0x00U });
    payload.push_back(std::byte { 0x00U });
    payload.insert(payload.end(), icc_profile.begin(), icc_profile.end());

    std::vector<std::byte> out;
    append_jp2_box(&out, openmeta::fourcc('c', 'o', 'l', 'r'),
                   std::span<const std::byte>(payload.data(), payload.size()));
    return out;
}

static std::vector<std::byte>
build_test_jp2_header_box(std::span<const std::byte> icc_profile)
{
    std::vector<std::byte> payload;
    const std::array<std::byte, 14> ihdr_payload = {
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x01 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x01 }, std::byte { 0x00 },
        std::byte { 0x03 }, std::byte { 0x07 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 },
    };
    append_jp2_box(&payload, openmeta::fourcc('i', 'h', 'd', 'r'),
                   std::span<const std::byte>(ihdr_payload.data(),
                                              ihdr_payload.size()));
    const std::vector<std::byte> colr = build_test_jp2_colr_icc_box(icc_profile);
    payload.insert(payload.end(), colr.begin(), colr.end());

    std::vector<std::byte> out;
    append_jp2_box(&out, openmeta::fourcc('j', 'p', '2', 'h'),
                   std::span<const std::byte>(payload.data(), payload.size()));
    return out;
}

static std::vector<std::byte>
build_minimal_jp2_file(std::span<const std::byte> metadata_boxes)
{
    std::vector<std::byte> jp2;
    append_u32be(&jp2, 12U);
    append_u32be(&jp2, openmeta::fourcc('j', 'P', ' ', ' '));
    jp2.push_back(std::byte { 0x0D });
    jp2.push_back(std::byte { 0x0A });
    jp2.push_back(std::byte { 0x87 });
    jp2.push_back(std::byte { 0x0A });

    std::vector<std::byte> ftyp;
    append_u32be(&ftyp, openmeta::fourcc('j', 'p', '2', ' '));
    append_u32be(&ftyp, 0U);
    append_u32be(&ftyp, openmeta::fourcc('j', 'p', '2', ' '));
    append_jp2_box(&jp2, openmeta::fourcc('f', 't', 'y', 'p'), ftyp);

    jp2.insert(jp2.end(), metadata_boxes.begin(), metadata_boxes.end());
    return jp2;
}

static std::vector<std::byte>
build_png_itxt_xmp_chunk_data(const char* text)
{
    std::vector<std::byte> out;
    static constexpr char kKeyword[] = "XML:com.adobe.xmp";
    for (size_t i = 0; i + 1U < sizeof(kKeyword); ++i) {
        out.push_back(
            static_cast<std::byte>(static_cast<uint8_t>(kKeyword[i])));
    }
    out.push_back(std::byte { 0x00 });
    out.push_back(std::byte { 0x00 });
    out.push_back(std::byte { 0x00 });
    out.push_back(std::byte { 0x00 });
    out.push_back(std::byte { 0x00 });
    if (text) {
        const size_t n = std::strlen(text);
        for (size_t i = 0; i < n; ++i) {
            out.push_back(
                static_cast<std::byte>(static_cast<uint8_t>(text[i])));
        }
    }
    return out;
}

static std::vector<std::byte>
build_test_png_iccp_chunk_data(std::span<const std::byte> icc_profile)
{
    std::vector<std::byte> out;
    static constexpr char kProfileName[] = "icc";
    for (size_t i = 0; i + 1U < sizeof(kProfileName); ++i) {
        out.push_back(
            static_cast<std::byte>(static_cast<uint8_t>(kProfileName[i])));
    }
    out.push_back(std::byte { 0x00 });
    out.push_back(std::byte { 0x00 });

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
    const uLong src_size = static_cast<uLong>(icc_profile.size());
    const uLongf cap     = compressBound(src_size);
    std::vector<std::byte> compressed(static_cast<size_t>(cap));
    uLongf actual = cap;
    const int zr  = compress2(reinterpret_cast<Bytef*>(compressed.data()),
                             &actual,
                             reinterpret_cast<const Bytef*>(icc_profile.data()),
                             src_size, Z_BEST_COMPRESSION);
    if (zr != Z_OK) {
        return {};
    }
    compressed.resize(static_cast<size_t>(actual));
    out.insert(out.end(), compressed.begin(), compressed.end());
#else
    (void)icc_profile;
#endif
    return out;
}

static std::vector<std::byte>
materialize_transfer_package_batch(
    const openmeta::PreparedTransferPackageBatch& batch,
    openmeta::EmitTransferResult* out_result)
{
    std::vector<std::byte> bytes(static_cast<size_t>(batch.output_size));
    openmeta::SpanTransferByteWriter writer(
        std::span<std::byte>(bytes.data(), bytes.size()));
    const openmeta::EmitTransferResult result
        = openmeta::write_prepared_transfer_package_batch(batch, writer);
    if (out_result) {
        *out_result = result;
    }
    if (result.status != openmeta::TransferStatus::Ok) {
        return {};
    }
    if (writer.bytes_written() != bytes.size()) {
        bytes.resize(static_cast<size_t>(writer.bytes_written()));
    }
    return bytes;
}

static std::string_view
arena_text(const openmeta::MetaStore& store, const openmeta::Entry& entry) noexcept
{
    const std::span<const std::byte> raw = store.arena().span(
        entry.value.data.span);
    return std::string_view(reinterpret_cast<const char*>(raw.data()),
                            raw.size());
}

static openmeta::MetaKeyView
exif_key_view(std::string_view ifd, uint16_t tag) noexcept
{
    openmeta::MetaKeyView key;
    key.kind              = openmeta::MetaKeyKind::ExifTag;
    key.data.exif_tag.ifd = ifd;
    key.data.exif_tag.tag = tag;
    return key;
}

static openmeta::MetaKeyView
xmp_key_view(std::string_view schema_ns, std::string_view property_path) noexcept
{
    openmeta::MetaKeyView key;
    key.kind                        = openmeta::MetaKeyKind::XmpProperty;
    key.data.xmp_property.schema_ns = schema_ns;
    key.data.xmp_property.property_path = property_path;
    return key;
}

static bool
read_test_u32be(std::span<const std::byte> bytes, size_t off,
                uint32_t* out) noexcept
{
    if (!out || off + 4U > bytes.size()) {
        return false;
    }
    *out = (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 0U]))
            << 24U)
           | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 1U]))
              << 16U)
           | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 2U]))
              << 8U)
           | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 3U]))
              << 0U);
    return true;
}

static bool
read_test_u64le(std::span<const std::byte> bytes, size_t* io_off,
                uint64_t* out) noexcept
{
    if (!io_off || !out || *io_off + 8U > bytes.size()) {
        return false;
    }
    const size_t off = *io_off;
    *out = (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 0U]))
            << 0U)
           | (static_cast<uint64_t>(
                  std::to_integer<uint8_t>(bytes[off + 1U]))
              << 8U)
           | (static_cast<uint64_t>(
                  std::to_integer<uint8_t>(bytes[off + 2U]))
              << 16U)
           | (static_cast<uint64_t>(
                  std::to_integer<uint8_t>(bytes[off + 3U]))
              << 24U)
           | (static_cast<uint64_t>(
                  std::to_integer<uint8_t>(bytes[off + 4U]))
              << 32U)
           | (static_cast<uint64_t>(
                  std::to_integer<uint8_t>(bytes[off + 5U]))
              << 40U)
           | (static_cast<uint64_t>(
                  std::to_integer<uint8_t>(bytes[off + 6U]))
              << 48U)
           | (static_cast<uint64_t>(
                  std::to_integer<uint8_t>(bytes[off + 7U]))
              << 56U);
    *io_off += 8U;
    return true;
}

static void
append_test_u64le(std::vector<std::byte>* out, uint64_t value) noexcept
{
    if (!out) {
        return;
    }
    out->push_back(static_cast<std::byte>(value & 0xFFU));
    out->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
    out->push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
    out->push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
    out->push_back(static_cast<std::byte>((value >> 32U) & 0xFFU));
    out->push_back(static_cast<std::byte>((value >> 40U) & 0xFFU));
    out->push_back(static_cast<std::byte>((value >> 48U) & 0xFFU));
    out->push_back(static_cast<std::byte>((value >> 56U) & 0xFFU));
}

static std::vector<std::byte>
make_test_exr_string_attribute_payload(std::string_view name,
                                       std::string_view value)
{
    std::vector<std::byte> out;
    out.reserve(16U + name.size() + value.size());
    append_test_u64le(&out, static_cast<uint64_t>(name.size()));
    for (size_t i = 0; i < name.size(); ++i) {
        out.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(name[i])));
    }
    append_test_u64le(&out, static_cast<uint64_t>(value.size()));
    for (size_t i = 0; i < value.size(); ++i) {
        out.push_back(static_cast<std::byte>(
            static_cast<unsigned char>(value[i])));
    }
    return out;
}

static bool
parse_test_exr_string_attribute_payload(std::span<const std::byte> bytes,
                                        std::string* out_name,
                                        std::string* out_value) noexcept
{
    if (!out_name || !out_value) {
        return false;
    }
    size_t off        = 0U;
    uint64_t name_len = 0U;
    if (!read_test_u64le(bytes, &off, &name_len)
        || name_len > bytes.size() - off) {
        return false;
    }
    out_name->assign(reinterpret_cast<const char*>(bytes.data() + off),
                     static_cast<size_t>(name_len));
    off += static_cast<size_t>(name_len);

    uint64_t value_len = 0U;
    if (!read_test_u64le(bytes, &off, &value_len)
        || value_len > bytes.size() - off) {
        return false;
    }
    out_value->assign(reinterpret_cast<const char*>(bytes.data() + off),
                      static_cast<size_t>(value_len));
    off += static_cast<size_t>(value_len);
    return off == bytes.size();
}

static bool
contains_byte_pair(std::span<const std::byte> bytes, uint8_t a,
                   uint8_t b) noexcept
{
    if (bytes.size() < 2U) {
        return false;
    }
    for (size_t i = 0; i + 1U < bytes.size(); ++i) {
        if (std::to_integer<uint8_t>(bytes[i + 0U]) == a
            && std::to_integer<uint8_t>(bytes[i + 1U]) == b) {
            return true;
        }
    }
    return false;
}

static void
append_fourcc(std::vector<std::byte>* out, uint32_t v)
{
    append_u32be(out, v);
}

static void
append_bytes(std::vector<std::byte>* out, std::string_view s)
{
    for (char c : s) {
        out->push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    }
}

static void
append_bmff_box(std::vector<std::byte>* out, uint32_t type,
                std::span<const std::byte> payload)
{
    append_u32be(out, static_cast<uint32_t>(8U + payload.size()));
    append_fourcc(out, type);
    out->insert(out->end(), payload.begin(), payload.end());
}

static std::vector<std::byte>
make_minimal_bmff_file(uint32_t major_brand,
                       std::span<const uint32_t> compatible_brands)
{
    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, major_brand);
    append_u32be(&ftyp_payload, 0U);
    for (size_t i = 0; i < compatible_brands.size(); ++i) {
        append_fourcc(&ftyp_payload, compatible_brands[i]);
    }

    static const std::array<std::byte, 4> kMediaData = {
        std::byte { 0x11 },
        std::byte { 0x22 },
        std::byte { 0x33 },
        std::byte { 0x44 },
    };

    std::vector<std::byte> out;
    append_bmff_box(&out, openmeta::fourcc('f', 't', 'y', 'p'),
                    std::span<const std::byte>(ftyp_payload.data(),
                                               ftyp_payload.size()));
    append_bmff_box(&out, openmeta::fourcc('m', 'd', 'a', 't'),
                    std::span<const std::byte>(kMediaData.data(),
                                               kMediaData.size()));
    return out;
}

static std::vector<std::byte>
make_minimal_heif_file()
{
    const std::array<uint32_t, 2> compat = {
        openmeta::fourcc('m', 'i', 'f', '1'),
        openmeta::fourcc('h', 'e', 'i', 'c'),
    };
    return make_minimal_bmff_file(openmeta::fourcc('h', 'e', 'i', 'c'),
                                  std::span<const uint32_t>(compat.data(),
                                                            compat.size()));
}

static std::vector<std::byte>
make_minimal_bmff_file()
{
    return make_minimal_heif_file();
}

static std::vector<std::byte>
make_minimal_avif_file()
{
    const std::array<uint32_t, 2> compat = {
        openmeta::fourcc('m', 'i', 'f', '1'),
        openmeta::fourcc('a', 'v', 'i', 'f'),
    };
    return make_minimal_bmff_file(openmeta::fourcc('a', 'v', 'i', 'f'),
                                  std::span<const uint32_t>(compat.data(),
                                                            compat.size()));
}

static std::vector<std::byte>
make_minimal_cr3_file()
{
    const std::array<uint32_t, 2> compat = {
        openmeta::fourcc('c', 'r', 'x', ' '),
        openmeta::fourcc('C', 'R', '3', ' '),
    };
    return make_minimal_bmff_file(openmeta::fourcc('c', 'r', 'x', ' '),
                                  std::span<const uint32_t>(compat.data(),
                                                            compat.size()));
}

static std::vector<std::byte>
make_minimal_jxl_file()
{
    static const std::array<std::byte, 4> kCodestream = {
        std::byte { 0x11 },
        std::byte { 0x22 },
        std::byte { 0x33 },
        std::byte { 0x44 },
    };

    std::vector<std::byte> out;
    append_u32be(&out, 12U);
    append_fourcc(&out, openmeta::fourcc('J', 'X', 'L', ' '));
    append_u32be(&out, 0x0D0A870AU);
    append_bmff_box(&out, openmeta::fourcc('j', 'x', 'l', 'c'),
                    std::span<const std::byte>(kCodestream.data(),
                                               kCodestream.size()));
    return out;
}

#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI \
    && defined(OPENMETA_HAS_BROTLI_ENCODER) && OPENMETA_HAS_BROTLI_ENCODER
static std::vector<std::byte>
brotli_compress(std::span<const std::byte> input)
{
    const size_t max_out = BrotliEncoderMaxCompressedSize(input.size());
    std::vector<uint8_t> out(max_out);
    size_t out_size = out.size();

    const int quality            = 4;
    const int lgwin              = 22;
    const BrotliEncoderMode mode = BROTLI_MODE_GENERIC;

    const uint8_t* in_u8 = reinterpret_cast<const uint8_t*>(input.data());
    const bool ok = BrotliEncoderCompress(quality, lgwin, mode, input.size(),
                                          in_u8, &out_size, out.data());
    if (!ok) {
        return {};
    }

    std::vector<std::byte> bytes(out_size);
    for (size_t i = 0; i < out_size; ++i) {
        bytes[i] = std::byte { out[i] };
    }
    return bytes;
}

static void
append_jxl_brob_box(std::vector<std::byte>* out, uint32_t real_type,
                    std::span<const std::byte> logical_payload)
{
    ASSERT_TRUE(out != nullptr);
    ASSERT_GE(logical_payload.size(), 8U);
    const std::span<const std::byte> payload(logical_payload.data() + 8U,
                                             logical_payload.size() - 8U);
    const std::vector<std::byte> compressed = brotli_compress(payload);
    ASSERT_FALSE(compressed.empty());

    std::vector<std::byte> brob_payload;
    append_fourcc(&brob_payload, real_type);
    brob_payload.insert(brob_payload.end(), compressed.begin(),
                        compressed.end());
    append_bmff_box(out, openmeta::fourcc('b', 'r', 'o', 'b'),
                    std::span<const std::byte>(brob_payload.data(),
                                               brob_payload.size()));
}
#endif

static std::vector<std::byte>
make_app1_exif_payload();

static std::vector<std::byte>
make_test_jxl_exif_box_payload()
{
    std::vector<std::byte> out;
    const std::vector<std::byte> app1 = make_app1_exif_payload();
    append_u32be(&out, 6U);
    out.insert(out.end(), app1.begin(), app1.end());
    return out;
}

static std::vector<std::byte>
make_test_bmff_exif_item_payload()
{
    static const std::array<uint8_t, 24> kRaw = {
        0x00, 0x00, 0x00, 0x06, 0x45, 0x78, 0x69, 0x66, 0x00, 0x00, 0x4D, 0x4D,
        0x00, 0x2A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };

    std::vector<std::byte> out;
    out.reserve(kRaw.size());
    for (size_t i = 0; i < kRaw.size(); ++i) {
        out.push_back(static_cast<std::byte>(kRaw[i]));
    }
    return out;
}

static std::vector<std::byte>
make_semantic_c2pa_logical_payload(uint64_t manifest_count,
                                   bool include_claim_generator,
                                   bool include_signature_algorithm,
                                   bool include_content_binding_assertion);

static std::vector<std::byte>
make_test_bmff_with_c2pa(bool include_exif)
{
    const std::vector<std::byte> input = make_minimal_heif_file();
    const std::vector<std::byte> logical
        = make_semantic_c2pa_logical_payload(1U, true, true, true);

    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Heif;

    if (include_exif) {
        openmeta::PreparedTransferBlock exif;
        exif.kind    = openmeta::TransferBlockKind::Exif;
        exif.order   = 100U;
        exif.route   = "bmff:item-exif";
        exif.payload = make_test_bmff_exif_item_payload();
        bundle.blocks.push_back(std::move(exif));
    }

    openmeta::PreparedTransferBlock c2pa;
    c2pa.kind  = openmeta::TransferBlockKind::C2pa;
    c2pa.order = 150U;
    c2pa.route = "bmff:item-c2pa";
    c2pa.payload.assign(logical.begin(), logical.end());
    bundle.blocks.push_back(std::move(c2pa));

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);
    if (result.edit_apply.status != openmeta::TransferStatus::Ok) {
        return {};
    }
    return result.edited_output;
}

static void
append_cbor_major_u64(std::vector<std::byte>* out, uint8_t major,
                      uint64_t value)
{
    ASSERT_NE(out, nullptr);
    const uint8_t prefix = static_cast<uint8_t>(major << 5U);
    if (value < 24U) {
        out->push_back(static_cast<std::byte>(prefix | value));
        return;
    }
    if (value <= 0xFFU) {
        out->push_back(static_cast<std::byte>(prefix | 24U));
        out->push_back(static_cast<std::byte>(value & 0xFFU));
        return;
    }
    if (value <= 0xFFFFU) {
        out->push_back(static_cast<std::byte>(prefix | 25U));
        out->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
        out->push_back(static_cast<std::byte>((value >> 0U) & 0xFFU));
        return;
    }
    append_u32be(out, static_cast<uint32_t>(value));
    out->insert(out->end() - 4U, static_cast<std::byte>(prefix | 26U));
}

static void
append_cbor_text(std::vector<std::byte>* out, std::string_view text)
{
    append_cbor_major_u64(out, 3U, static_cast<uint64_t>(text.size()));
    append_bytes(out, text);
}

static void
append_cbor_bytes(std::vector<std::byte>* out, std::span<const std::byte> bytes)
{
    append_cbor_major_u64(out, 2U, static_cast<uint64_t>(bytes.size()));
    out->insert(out->end(), bytes.begin(), bytes.end());
}

static std::vector<std::byte>
make_jpeg_with_segment(uint8_t marker, std::span<const std::byte> payload)
{
    std::vector<std::byte> out;
    out.push_back(std::byte { 0xFF });
    out.push_back(std::byte { 0xD8 });
    out.push_back(std::byte { 0xFF });
    out.push_back(static_cast<std::byte>(marker));
    append_u16be(&out, static_cast<uint16_t>(payload.size() + 2U));
    out.insert(out.end(), payload.begin(), payload.end());
    out.push_back(std::byte { 0xFF });
    out.push_back(std::byte { 0xD9 });
    return out;
}

struct TestJpegSegment final {
    uint8_t marker = 0U;
    std::span<const std::byte> payload;
};

static void
append_test_jpeg_segment(std::vector<std::byte>* out, uint8_t marker,
                         std::span<const std::byte> payload)
{
    ASSERT_NE(out, nullptr);
    out->push_back(std::byte { 0xFF });
    out->push_back(static_cast<std::byte>(marker));
    append_u16be(out, static_cast<uint16_t>(payload.size() + 2U));
    out->insert(out->end(), payload.begin(), payload.end());
}

static std::vector<std::byte>
make_jpeg_with_segments(std::span<const TestJpegSegment> segments)
{
    std::vector<std::byte> out;
    out.push_back(std::byte { 0xFF });
    out.push_back(std::byte { 0xD8 });
    for (size_t i = 0; i < segments.size(); ++i) {
        append_test_jpeg_segment(&out, segments[i].marker, segments[i].payload);
    }
    out.push_back(std::byte { 0xFF });
    out.push_back(std::byte { 0xD9 });
    return out;
}

static std::vector<std::byte>
make_app11_jumbf_payload_with_cbor(std::string_view label,
                                   std::span<const std::byte> cbor_payload)
{
    std::vector<std::byte> jumd_payload;
    append_bytes(&jumd_payload, label);
    jumd_payload.push_back(std::byte { 0x00 });

    std::vector<std::byte> jumd_box;
    append_bmff_box(&jumd_box, openmeta::fourcc('j', 'u', 'm', 'd'),
                    std::span<const std::byte>(jumd_payload.data(),
                                               jumd_payload.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, openmeta::fourcc('c', 'b', 'o', 'r'),
                    cbor_payload);

    std::vector<std::byte> jumb_payload;
    jumb_payload.insert(jumb_payload.end(), jumd_box.begin(), jumd_box.end());
    jumb_payload.insert(jumb_payload.end(), cbor_box.begin(), cbor_box.end());

    std::vector<std::byte> jumb_box;
    append_bmff_box(&jumb_box, openmeta::fourcc('j', 'u', 'm', 'b'),
                    std::span<const std::byte>(jumb_payload.data(),
                                               jumb_payload.size()));

    std::vector<std::byte> seg;
    append_bytes(&seg, "JP");
    seg.push_back(std::byte { 0x00 });
    seg.push_back(std::byte { 0x00 });
    append_u32be(&seg, 1U);
    seg.insert(seg.end(), jumb_box.begin(), jumb_box.end());

    return seg;
}

static std::vector<std::byte>
make_app11_jumbf_payload(std::string_view label)
{
    const std::vector<std::byte> cbor_payload = {
        std::byte { 0xA1 },
        std::byte { 0x61 },
        std::byte { 0x61 },
        std::byte { 0x01 },
    };
    return make_app11_jumbf_payload_with_cbor(
        label,
        std::span<const std::byte>(cbor_payload.data(), cbor_payload.size()));
}

static std::vector<std::byte>
make_jpeg_with_app11_jumbf(std::string_view label)
{
    const std::vector<std::byte> seg = make_app11_jumbf_payload(label);
    return make_jpeg_with_segment(0xEBU,
                                  std::span<const std::byte>(seg.data(),
                                                             seg.size()));
}

static std::vector<std::byte>
make_logical_jumbf_payload_with_cbor(std::string_view label,
                                     std::span<const std::byte> cbor_payload)
{
    const std::vector<std::byte> seg
        = make_app11_jumbf_payload_with_cbor(label, cbor_payload);
    EXPECT_GE(seg.size(), 8U);
    return std::vector<std::byte>(seg.begin() + 8, seg.end());
}

static std::vector<std::byte>
make_logical_jumbf_payload(std::string_view label)
{
    const std::vector<std::byte> seg = make_app11_jumbf_payload(label);
    EXPECT_GE(seg.size(), 8U);
    return std::vector<std::byte>(seg.begin() + 8, seg.end());
}

static std::vector<std::byte>
make_semantic_c2pa_cbor_payload(uint64_t manifest_count          = 1U,
                                bool include_claim_generator     = true,
                                bool include_signature_algorithm = true,
                                bool include_assertions          = true)
{
    std::vector<std::byte> out;
    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "manifest");
    append_cbor_major_u64(&out, 4U, manifest_count);
    static const std::array<std::byte, 4> kSignature = {
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
        std::byte { 0x04 },
    };
    for (uint64_t i = 0U; i < manifest_count; ++i) {
        append_cbor_major_u64(&out, 5U, include_claim_generator ? 2U : 1U);
        if (include_claim_generator) {
            append_cbor_text(&out, "claim_generator");
            append_cbor_text(&out, "test");
        }
        append_cbor_text(&out, "claims");
        append_cbor_major_u64(&out, 4U, 1U);
        append_cbor_major_u64(&out, 5U, include_assertions ? 2U : 1U);
        if (include_assertions) {
            append_cbor_text(&out, "assertions");
            append_cbor_major_u64(&out, 4U, 1U);
            append_cbor_major_u64(&out, 5U, 1U);
            append_cbor_text(&out, "label");
            append_cbor_text(&out, "c2pa.hash.data");
        }
        append_cbor_text(&out, "signatures");
        append_cbor_major_u64(&out, 4U, 1U);
        append_cbor_major_u64(&out, 5U, include_signature_algorithm ? 2U : 1U);
        if (include_signature_algorithm) {
            append_cbor_text(&out, "alg");
            append_cbor_text(&out, "ES256");
        }
        append_cbor_text(&out, "signature");
        append_cbor_bytes(&out, std::span<const std::byte>(kSignature.data(),
                                                           kSignature.size()));
    }
    return out;
}

static std::vector<std::byte>
make_semantic_c2pa_logical_payload(uint64_t manifest_count          = 1U,
                                   bool include_claim_generator     = true,
                                   bool include_signature_algorithm = true,
                                   bool include_assertions          = true)
{
    const std::vector<std::byte> cbor_payload = make_semantic_c2pa_cbor_payload(
        manifest_count, include_claim_generator, include_signature_algorithm,
        include_assertions);
    return make_logical_jumbf_payload_with_cbor(
        "c2pa",
        std::span<const std::byte>(cbor_payload.data(), cbor_payload.size()));
}

static std::vector<std::byte>
make_test_jxl_with_c2pa(bool include_exif)
{
    std::vector<std::byte> out = make_minimal_jxl_file();
    if (include_exif) {
        const std::vector<std::byte> exif_payload
            = make_test_jxl_exif_box_payload();
        append_bmff_box(&out, openmeta::fourcc('E', 'x', 'i', 'f'),
                        std::span<const std::byte>(exif_payload.data(),
                                                   exif_payload.size()));
    }

    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    EXPECT_GE(logical.size(), 8U);
    append_bmff_box(&out, openmeta::fourcc('j', 'u', 'm', 'b'),
                    std::span<const std::byte>(logical.data() + 8U,
                                               logical.size() - 8U));
    return out;
}

static std::vector<std::byte>
make_semantic_c2pa_logical_payload_primary_signature_claim_ref(
    uint64_t claim_ref_index)
{
    std::vector<std::byte> out;
    static const std::array<std::byte, 3> kClaim0Payload = {
        std::byte { 0x10 },
        std::byte { 0x11 },
        std::byte { 0x12 },
    };
    static const std::array<std::byte, 3> kClaim1Payload = {
        std::byte { 0x20 },
        std::byte { 0x21 },
        std::byte { 0x22 },
    };
    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "manifest");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "claim_generator");
    append_cbor_text(&out, "test");
    append_cbor_text(&out, "claims");
    append_cbor_major_u64(&out, 4U, 2U);

    append_cbor_major_u64(&out, 5U, 3U);
    append_cbor_text(&out, "assertions");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "label");
    append_cbor_text(&out, "c2pa.hash.data");
    append_cbor_text(&out, "claim_payload");
    append_cbor_bytes(&out, std::span<const std::byte>(kClaim0Payload.data(),
                                                       kClaim0Payload.size()));
    append_cbor_text(&out, "signatures");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 3U);
    append_cbor_text(&out, "alg");
    append_cbor_text(&out, "ES256");
    append_cbor_text(&out, "signature");
    static const std::array<std::byte, 4> kSignature = {
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
        std::byte { 0x04 },
    };
    append_cbor_bytes(&out, std::span<const std::byte>(kSignature.data(),
                                                       kSignature.size()));
    append_cbor_text(&out, "claim_ref");
    append_cbor_major_u64(&out, 0U, claim_ref_index);

    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "assertions");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "label");
    append_cbor_text(&out, "c2pa.hash.data");
    append_cbor_text(&out, "claim_payload");
    append_cbor_bytes(&out, std::span<const std::byte>(kClaim1Payload.data(),
                                                       kClaim1Payload.size()));

    return make_logical_jumbf_payload_with_cbor(
        "c2pa", std::span<const std::byte>(out.data(), out.size()));
}

static std::vector<std::byte>
make_semantic_c2pa_logical_payload_primary_signature_refs_second_claim()
{
    return make_semantic_c2pa_logical_payload_primary_signature_claim_ref(1U);
}

static std::vector<std::byte>
make_semantic_c2pa_logical_payload_unresolved_primary_signature_reference()
{
    return make_semantic_c2pa_logical_payload_primary_signature_claim_ref(7U);
}

static std::vector<std::byte>
make_semantic_c2pa_logical_payload_ambiguous_primary_signature_reference()
{
    std::vector<std::byte> out;
    static const std::array<std::byte, 3> kSharedPayload = {
        std::byte { 0x30 },
        std::byte { 0x31 },
        std::byte { 0x32 },
    };
    static const std::array<std::byte, 4> kSignature = {
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
        std::byte { 0x04 },
    };
    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "manifest");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "claim_generator");
    append_cbor_text(&out, "test");
    append_cbor_text(&out, "claims");
    append_cbor_major_u64(&out, 4U, 2U);

    append_cbor_major_u64(&out, 5U, 3U);
    append_cbor_text(&out, "assertions");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "label");
    append_cbor_text(&out, "c2pa.hash.data");
    append_cbor_text(&out, "claim_payload");
    append_cbor_bytes(&out, std::span<const std::byte>(kSharedPayload.data(),
                                                       kSharedPayload.size()));
    append_cbor_text(&out, "signatures");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 3U);
    append_cbor_text(&out, "alg");
    append_cbor_text(&out, "ES256");
    append_cbor_text(&out, "signature");
    append_cbor_bytes(&out, std::span<const std::byte>(kSignature.data(),
                                                       kSignature.size()));
    append_cbor_text(&out, "claim_ref");
    append_cbor_major_u64(&out, 0U, 0U);

    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "assertions");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "label");
    append_cbor_text(&out, "c2pa.hash.data");
    append_cbor_text(&out, "claim_payload");
    append_cbor_bytes(&out, std::span<const std::byte>(kSharedPayload.data(),
                                                       kSharedPayload.size()));

    return make_logical_jumbf_payload_with_cbor(
        "c2pa", std::span<const std::byte>(out.data(), out.size()));
}

static std::vector<std::byte>
make_semantic_c2pa_logical_payload_primary_claim_two_signatures()
{
    std::vector<std::byte> out;
    static const std::array<std::byte, 4> kSignature0 = {
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
        std::byte { 0x04 },
    };
    static const std::array<std::byte, 4> kSignature1 = {
        std::byte { 0x11 },
        std::byte { 0x12 },
        std::byte { 0x13 },
        std::byte { 0x14 },
    };

    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "manifest");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "claim_generator");
    append_cbor_text(&out, "test");
    append_cbor_text(&out, "claims");
    append_cbor_major_u64(&out, 4U, 1U);

    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "assertions");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "label");
    append_cbor_text(&out, "c2pa.hash.data");
    append_cbor_text(&out, "signatures");
    append_cbor_major_u64(&out, 4U, 2U);

    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "alg");
    append_cbor_text(&out, "ES256");
    append_cbor_text(&out, "signature");
    append_cbor_bytes(&out, std::span<const std::byte>(kSignature0.data(),
                                                       kSignature0.size()));

    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "alg");
    append_cbor_text(&out, "ES256");
    append_cbor_text(&out, "signature");
    append_cbor_bytes(&out, std::span<const std::byte>(kSignature1.data(),
                                                       kSignature1.size()));

    return make_logical_jumbf_payload_with_cbor(
        "c2pa", std::span<const std::byte>(out.data(), out.size()));
}

static std::vector<std::byte>
make_semantic_c2pa_logical_payload_two_claims_two_signatures()
{
    std::vector<std::byte> out;
    static const std::array<std::byte, 4> kSignature0 = {
        std::byte { 0x01 },
        std::byte { 0x02 },
        std::byte { 0x03 },
        std::byte { 0x04 },
    };
    static const std::array<std::byte, 4> kSignature1 = {
        std::byte { 0x11 },
        std::byte { 0x12 },
        std::byte { 0x13 },
        std::byte { 0x14 },
    };

    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "manifest");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "claim_generator");
    append_cbor_text(&out, "test");
    append_cbor_text(&out, "claims");
    append_cbor_major_u64(&out, 4U, 2U);

    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "assertions");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "label");
    append_cbor_text(&out, "c2pa.hash.data");
    append_cbor_text(&out, "signatures");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "alg");
    append_cbor_text(&out, "ES256");
    append_cbor_text(&out, "signature");
    append_cbor_bytes(&out, std::span<const std::byte>(kSignature0.data(),
                                                       kSignature0.size()));

    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "assertions");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 1U);
    append_cbor_text(&out, "label");
    append_cbor_text(&out, "c2pa.hash.data");
    append_cbor_text(&out, "signatures");
    append_cbor_major_u64(&out, 4U, 1U);
    append_cbor_major_u64(&out, 5U, 2U);
    append_cbor_text(&out, "alg");
    append_cbor_text(&out, "ES256");
    append_cbor_text(&out, "signature");
    append_cbor_bytes(&out, std::span<const std::byte>(kSignature1.data(),
                                                       kSignature1.size()));

    return make_logical_jumbf_payload_with_cbor(
        "c2pa", std::span<const std::byte>(out.data(), out.size()));
}

static std::vector<std::byte>
make_jpeg_with_draft_c2pa_invalidation()
{
    std::vector<std::byte> cbor_payload;
    cbor_payload.push_back(std::byte { 0xA3 });
    cbor_payload.push_back(std::byte { 0x78 });
    cbor_payload.push_back(std::byte { 0x1A });
    append_bytes(&cbor_payload, "openmeta:c2pa_invalidation");
    cbor_payload.push_back(std::byte { 0xF5 });
    cbor_payload.push_back(std::byte { 0x6F });
    append_bytes(&cbor_payload, "openmeta:reason");
    cbor_payload.push_back(std::byte { 0x6F });
    append_bytes(&cbor_payload, "content_changed");
    cbor_payload.push_back(std::byte { 0x6D });
    append_bytes(&cbor_payload, "openmeta:mode");
    cbor_payload.push_back(std::byte { 0x6E });
    append_bytes(&cbor_payload, "unsigned_draft");

    std::vector<std::byte> jumd_payload;
    append_bytes(&jumd_payload, "c2pa");
    jumd_payload.push_back(std::byte { 0x00 });

    std::vector<std::byte> jumd_box;
    append_bmff_box(&jumd_box, openmeta::fourcc('j', 'u', 'm', 'd'),
                    std::span<const std::byte>(jumd_payload.data(),
                                               jumd_payload.size()));

    std::vector<std::byte> cbor_box;
    append_bmff_box(&cbor_box, openmeta::fourcc('c', 'b', 'o', 'r'),
                    std::span<const std::byte>(cbor_payload.data(),
                                               cbor_payload.size()));

    std::vector<std::byte> jumb_payload;
    jumb_payload.insert(jumb_payload.end(), jumd_box.begin(), jumd_box.end());
    jumb_payload.insert(jumb_payload.end(), cbor_box.begin(), cbor_box.end());

    std::vector<std::byte> jumb_box;
    append_bmff_box(&jumb_box, openmeta::fourcc('j', 'u', 'm', 'b'),
                    std::span<const std::byte>(jumb_payload.data(),
                                               jumb_payload.size()));

    std::vector<std::byte> seg;
    append_bytes(&seg, "JP");
    seg.push_back(std::byte { 0x00 });
    seg.push_back(std::byte { 0x00 });
    append_u32be(&seg, 1U);
    seg.insert(seg.end(), jumb_box.begin(), jumb_box.end());

    return make_jpeg_with_segment(0xEBU,
                                  std::span<const std::byte>(seg.data(),
                                                             seg.size()));
}

static std::vector<std::byte>
make_app11_draft_c2pa_invalidation_payload()
{
    const std::vector<std::byte> jpeg = make_jpeg_with_draft_c2pa_invalidation();
    EXPECT_GE(jpeg.size(), 10U);
    return std::vector<std::byte>(jpeg.begin() + 6, jpeg.end() - 2);
}

static std::vector<std::byte>
make_app1_exif_payload()
{
    std::vector<std::byte> t;
    append_bytes(&t, "Exif");
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x00 });
    append_bytes(&t, "II");
    t.push_back(std::byte { 0x2A });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x08 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x01 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x32 });
    t.push_back(std::byte { 0x01 });
    t.push_back(std::byte { 0x02 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x14 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x1A });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x00 });
    t.push_back(std::byte { 0x00 });
    append_bytes(&t, "2000:01:02 03:04:05");
    t.push_back(std::byte { 0x00 });
    return t;
}

static openmeta::PreparedTransferC2paSignerInput
make_test_c2pa_signer_input(std::span<const std::byte> logical_payload)
{
    openmeta::PreparedTransferC2paSignerInput input;
    input.signing_time            = "2026-03-09T00:00:00Z";
    input.certificate_chain_bytes = {
        std::byte { 0x30 },
        std::byte { 0x82 },
        std::byte { 0x01 },
        std::byte { 0x00 },
    };
    input.private_key_reference   = "test-key-ref";
    input.manifest_builder_output = {
        std::byte { 0xA1 }, std::byte { 0x64 }, std::byte { 't' },
        std::byte { 'e' },  std::byte { 's' },  std::byte { 't' },
        std::byte { 0x01 },
    };
    if (logical_payload.size() >= 8U) {
        uint32_t outer_size = 0U;
        uint32_t outer_type = 0U;
        if (read_test_u32be(logical_payload, 0U, &outer_size)
            && read_test_u32be(logical_payload, 4U, &outer_type)
            && outer_size >= 8U && outer_size <= logical_payload.size()
            && (outer_type == 0x6A756D62U || outer_type == 0x63327061U)) {
            size_t off = 8U;
            while (off + 8U <= logical_payload.size()) {
                uint32_t child_size = 0U;
                uint32_t child_type = 0U;
                if (!read_test_u32be(logical_payload, off + 0U, &child_size)
                    || !read_test_u32be(logical_payload, off + 4U, &child_type)
                    || child_size < 8U
                    || off + static_cast<size_t>(child_size)
                           > logical_payload.size()) {
                    break;
                }
                if (child_type == 0x63626F72U) {
                    input.manifest_builder_output.assign(
                        logical_payload.begin()
                            + static_cast<std::ptrdiff_t>(off + 8U),
                        logical_payload.begin()
                            + static_cast<std::ptrdiff_t>(off + child_size));
                    break;
                }
                off += static_cast<size_t>(child_size);
            }
        }
    }
    input.signed_c2pa_logical_payload.assign(logical_payload.begin(),
                                             logical_payload.end());
    return input;
}

static std::string
unique_temp_path(const char* suffix);
static bool
write_bytes_file(const std::string& path, std::span<const std::byte> bytes);

static bool
build_staged_signed_c2pa_bundle(
    std::span<const std::byte> logical_payload,
    openmeta::PreparedTransferBundle* out_bundle) noexcept
{
    if (!out_bundle) {
        return false;
    }

    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    if (!write_bytes_file(path, std::span<const std::byte>(jpeg.data(),
                                                           jpeg.size()))) {
        return false;
    }

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());
    if (result.prepare.status != openmeta::TransferStatus::Unsupported) {
        return false;
    }

    openmeta::PreparedTransferC2paSignRequest sign_request;
    if (openmeta::build_prepared_c2pa_sign_request(result.bundle, &sign_request)
        != openmeta::TransferStatus::Ok) {
        return false;
    }

    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(logical_payload);
    if (openmeta::apply_prepared_c2pa_sign_result(&result.bundle, sign_request,
                                                  input)
            .status
        != openmeta::TransferStatus::Ok) {
        return false;
    }

    *out_bundle = std::move(result.bundle);
    return true;
}

static uint32_t
count_blocks_with_route(const openmeta::PreparedTransferBundle& bundle,
                        std::string_view route) noexcept
{
    uint32_t count = 0U;
    for (size_t i = 0; i < bundle.blocks.size(); ++i) {
        if (bundle.blocks[i].route == route) {
            count += 1U;
        }
    }
    return count;
}

static openmeta::PreparedTransferBundle
make_bundle_with_draft_c2pa_signed_rewrite_contract()
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format      = openmeta::TransferTargetFormat::Jpeg;
    bundle.c2pa_rewrite.state = openmeta::TransferC2paRewriteState::Ready;
    bundle.c2pa_rewrite.target_format = openmeta::TransferTargetFormat::Jpeg;
    bundle.c2pa_rewrite.source_kind
        = openmeta::TransferC2paSourceKind::ContentBound;

    openmeta::PreparedTransferPolicyDecision decision;
    decision.subject   = openmeta::TransferPolicySubject::C2pa;
    decision.requested = openmeta::TransferPolicyAction::Rewrite;
    decision.effective = openmeta::TransferPolicyAction::Rewrite;
    decision.reason    = openmeta::TransferPolicyReason::ExternalSignedPayload;
    decision.c2pa_mode = openmeta::TransferC2paMode::SignedRewrite;
    decision.c2pa_source_kind = openmeta::TransferC2paSourceKind::ContentBound;
    decision.c2pa_prepared_output
        = openmeta::TransferC2paPreparedOutput::SignedRewrite;
    decision.matched_entries = 1U;
    decision.message         = "test signed rewrite contract";
    bundle.policy_decisions.push_back(std::move(decision));

    openmeta::PreparedTransferBlock block;
    block.kind    = openmeta::TransferBlockKind::C2pa;
    block.route   = "jpeg:app11-c2pa";
    block.payload = make_app11_draft_c2pa_invalidation_payload();
    bundle.blocks.push_back(std::move(block));
    return bundle;
}

static bool
payload_contains_ascii(std::span<const std::byte> bytes,
                       std::string_view text) noexcept
{
    if (text.empty()) {
        return true;
    }
    if (bytes.size() < text.size()) {
        return false;
    }
    for (size_t off = 0; off + text.size() <= bytes.size(); ++off) {
        bool match = true;
        for (size_t i = 0; i < text.size(); ++i) {
            if (bytes[off + i]
                != static_cast<std::byte>(static_cast<unsigned char>(text[i]))) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

static bool
payload_contains_bytes(std::span<const std::byte> bytes,
                       std::span<const std::byte> needle) noexcept
{
    if (needle.empty()) {
        return true;
    }
    if (bytes.size() < needle.size()) {
        return false;
    }
    for (size_t off = 0; off + needle.size() <= bytes.size(); ++off) {
        bool match = true;
        for (size_t i = 0; i < needle.size(); ++i) {
            if (bytes[off + i] != needle[i]) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

static std::string
temp_root()
{
    const char* env = std::getenv("TMPDIR");
    if (env && *env) {
        return std::string(env);
    }
    return "/tmp";
}

static std::string
unique_temp_path(const char* suffix)
{
    static uint64_t seq = 0U;
    seq += 1U;
    const uint64_t now = static_cast<uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count());

    std::string path = temp_root();
    if (!path.empty() && path.back() != '/') {
        path.push_back('/');
    }

    char name[160];
    std::snprintf(name, sizeof(name), "openmeta_transfer_%llu_%llu%s",
                  static_cast<unsigned long long>(now),
                  static_cast<unsigned long long>(seq), suffix ? suffix : "");
    path.append(name);
    return path;
}

static bool
write_bytes_file(const std::string& path, std::span<const std::byte> bytes)
{
    std::FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    if (!bytes.empty()) {
        const size_t n = std::fwrite(bytes.data(), 1, bytes.size(), f);
        if (n != bytes.size()) {
            std::fclose(f);
            return false;
        }
    }
    return std::fclose(f) == 0;
}

static bool
read_bytes_file(const std::string& path, std::vector<std::byte>* out)
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
    if (!out->empty()) {
        const size_t n = std::fread(out->data(), 1, out->size(), f);
        if (n != out->size()) {
            std::fclose(f);
            out->clear();
            return false;
        }
    }
    return std::fclose(f) == 0;
}

static bool
build_test_transfer_source_jpeg_bytes(std::vector<std::byte>* out)
{
    if (!out) {
        return false;
    }

    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    if (block == openmeta::kInvalidBlockId) {
        return false;
    }

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "exififd", 0x9003U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0U;
    if (store.add_entry(exif) == openmeta::kInvalidEntryId) {
        return false;
    }

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(store.arena(), "OpenMeta Transfer Source",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    if (store.add_entry(xmp) == openmeta::kInvalidEntryId) {
        return false;
    }

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Jpeg;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    if (prepared.status != openmeta::TransferStatus::Ok) {
        return false;
    }

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compiled
        = openmeta::compile_prepared_transfer_execution(
            bundle, openmeta::EmitTransferOptions {}, &plan);
    if (compiled.status != openmeta::TransferStatus::Ok
        || plan.target_format != openmeta::TransferTargetFormat::Jpeg) {
        return false;
    }

    std::vector<TestJpegSegment> segments;
    segments.reserve(plan.jpeg_emit.ops.size());
    for (size_t i = 0; i < plan.jpeg_emit.ops.size(); ++i) {
        const openmeta::PreparedJpegEmitOp& op = plan.jpeg_emit.ops[i];
        if (op.block_index >= bundle.blocks.size()) {
            return false;
        }
        TestJpegSegment seg;
        seg.marker = op.marker_code;
        seg.payload = std::span<const std::byte>(
            bundle.blocks[op.block_index].payload.data(),
            bundle.blocks[op.block_index].payload.size());
        segments.push_back(seg);
    }

    *out = make_jpeg_with_segments(
        std::span<const TestJpegSegment>(segments.data(), segments.size()));
    return true;
}

static bool
build_test_creator_tool_xmp_sidecar(std::string_view creator_tool,
                                    std::vector<std::byte>* out)
{
    if (!out) {
        return false;
    }

    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    if (block == openmeta::kInvalidBlockId) {
        return false;
    }

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(store.arena(), creator_tool,
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 0U;
    if (store.add_entry(xmp) == openmeta::kInvalidEntryId) {
        return false;
    }

    store.finalize();

    openmeta::XmpSidecarRequest request;
    request.format               = openmeta::XmpSidecarFormat::Portable;
    request.include_exif         = false;
    request.include_iptc         = false;
    request.include_existing_xmp = true;

    const openmeta::XmpDumpResult dumped
        = openmeta::dump_xmp_sidecar(store, out, request);
    return dumped.status == openmeta::XmpDumpStatus::Ok && !out->empty();
}

static bool
build_test_creator_tool_jpeg_xmp_app1_payload(std::string_view creator_tool,
                                              std::vector<std::byte>* out)
{
    if (!out) {
        return false;
    }
    out->clear();

    std::vector<std::byte> packet;
    if (!build_test_creator_tool_xmp_sidecar(creator_tool, &packet)) {
        return false;
    }

    static constexpr char kJpegXmpHeader[]
        = "http://ns.adobe.com/xap/1.0/";
    append_bytes(out, kJpegXmpHeader);
    out->push_back(std::byte { 0x00 });
    out->insert(out->end(), packet.begin(), packet.end());
    return true;
}

static bool
build_test_creator_tool_png_xmp_itxt_payload(std::string_view creator_tool,
                                             std::vector<std::byte>* out)
{
    if (!out) {
        return false;
    }
    out->clear();

    std::vector<std::byte> packet;
    if (!build_test_creator_tool_xmp_sidecar(creator_tool, &packet)) {
        return false;
    }

    static constexpr char kKeyword[] = "XML:com.adobe.xmp";
    append_bytes(out, std::string_view(kKeyword, sizeof(kKeyword) - 1U));
    out->push_back(std::byte { 0x00 });
    out->push_back(std::byte { 0x00 });
    out->push_back(std::byte { 0x00 });
    out->push_back(std::byte { 0x00 });
    out->push_back(std::byte { 0x00 });
    out->insert(out->end(), packet.begin(), packet.end());
    return true;
}

static std::vector<std::byte>
make_minimal_tiff_little_endian();

static std::vector<std::byte>
make_minimal_bigtiff_little_endian();

static bool
build_test_tiff_like_with_creator_tool_xmp(
    std::string_view creator_tool, std::span<const std::byte> input_tiff,
    std::vector<std::byte>* out)
{
    if (!out) {
        return false;
    }
    out->clear();

    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    if (block == openmeta::kInvalidBlockId) {
        return false;
    }

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(store.arena(), creator_tool,
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 0U;
    if (store.add_entry(xmp) == openmeta::kInvalidEntryId) {
        return false;
    }
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.xmp_portable       = true;
    request.include_exif_app1  = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    if (openmeta::prepare_metadata_for_target(store, request, &bundle).status
        != openmeta::TransferStatus::Ok) {
        return false;
    }

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, input_tiff, options);
    if (result.edit_apply.status != openmeta::TransferStatus::Ok
        || result.edited_output.empty()) {
        return false;
    }

    *out = result.edited_output;
    return true;
}

static bool
build_test_tiff_with_creator_tool_xmp(std::string_view creator_tool,
                                      std::vector<std::byte>* out)
{
    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    return build_test_tiff_like_with_creator_tool_xmp(
        creator_tool,
        std::span<const std::byte>(input.data(), input.size()), out);
}

static bool
build_test_bigtiff_with_creator_tool_xmp(std::string_view creator_tool,
                                         std::vector<std::byte>* out)
{
    const std::vector<std::byte> input = make_minimal_bigtiff_little_endian();
    return build_test_tiff_like_with_creator_tool_xmp(
        creator_tool,
        std::span<const std::byte>(input.data(), input.size()), out);
}

static bool
build_test_target_with_existing_creator_tool_xmp(
    openmeta::TransferTargetFormat format, std::string_view creator_tool,
    std::vector<std::byte>* out)
{
    if (!out) {
        return false;
    }
    out->clear();

    if (format == openmeta::TransferTargetFormat::Jpeg) {
        std::vector<std::byte> payload;
        if (!build_test_creator_tool_jpeg_xmp_app1_payload(creator_tool,
                                                           &payload)) {
            return false;
        }
        const TestJpegSegment segments[] = {
            { 0xE1U,
              std::span<const std::byte>(payload.data(), payload.size()) },
        };
        *out = make_jpeg_with_segments(
            std::span<const TestJpegSegment>(segments, 1U));
        return true;
    }

    if (format == openmeta::TransferTargetFormat::Tiff
        || format == openmeta::TransferTargetFormat::Dng) {
        return build_test_tiff_with_creator_tool_xmp(creator_tool, out);
    }

    std::vector<std::byte> packet;
    if (!build_test_creator_tool_xmp_sidecar(creator_tool, &packet)) {
        return false;
    }

    if (format == openmeta::TransferTargetFormat::Png) {
        std::vector<std::byte> metadata;
        std::vector<std::byte> payload;
        if (!build_test_creator_tool_png_xmp_itxt_payload(creator_tool,
                                                          &payload)) {
            return false;
        }
        append_png_chunk(&metadata, openmeta::fourcc('i', 'T', 'X', 't'),
                         std::span<const std::byte>(payload.data(),
                                                    payload.size()));
        *out = build_minimal_png_file(
            std::span<const std::byte>(metadata.data(), metadata.size()));
        return true;
    }

    if (format == openmeta::TransferTargetFormat::Webp) {
        std::vector<std::byte> metadata;
        append_webp_chunk(&metadata, openmeta::fourcc('X', 'M', 'P', ' '),
                          std::span<const std::byte>(packet.data(),
                                                     packet.size()));
        *out = build_minimal_webp_file(
            std::span<const std::byte>(metadata.data(), metadata.size()),
            0x04U);
        return true;
    }

    if (format == openmeta::TransferTargetFormat::Jp2) {
        std::vector<std::byte> metadata;
        append_jp2_box(&metadata, openmeta::fourcc('x', 'm', 'l', ' '),
                       std::span<const std::byte>(packet.data(),
                                                  packet.size()));
        *out = build_minimal_jp2_file(
            std::span<const std::byte>(metadata.data(), metadata.size()));
        return true;
    }

    if (format == openmeta::TransferTargetFormat::Jxl) {
        *out = make_minimal_jxl_file();
        append_bmff_box(out, openmeta::fourcc('x', 'm', 'l', ' '),
                        std::span<const std::byte>(packet.data(),
                                                   packet.size()));
        return true;
    }

    return false;
}

static bool
store_has_text_entry(const openmeta::MetaStore& store,
                     const openmeta::MetaKeyView& key,
                     std::string_view expected) noexcept
{
    const std::span<const openmeta::EntryId> ids = store.find_all(key);
    if (ids.size() != 1U) {
        return false;
    }
    return arena_text(store, store.entry(ids[0])) == expected;
}

static bool
store_has_u32_scalar_entry(const openmeta::MetaStore& store,
                           const openmeta::MetaKeyView& key,
                           uint64_t expected) noexcept
{
    const std::span<const openmeta::EntryId> ids = store.find_all(key);
    if (ids.size() != 1U) {
        return false;
    }
    const openmeta::Entry& entry = store.entry(ids[0]);
    return entry.value.kind == openmeta::MetaValueKind::Scalar
           && entry.value.elem_type == openmeta::MetaElementType::U32
           && entry.value.data.u64 == expected;
}

static bool
store_has_u8_array_entry(const openmeta::MetaStore& store,
                         const openmeta::MetaKeyView& key,
                         std::span<const uint8_t> expected) noexcept
{
    const std::span<const openmeta::EntryId> ids = store.find_all(key);
    if (ids.size() != 1U) {
        return false;
    }
    const openmeta::Entry& entry = store.entry(ids[0]);
    if (entry.value.kind != openmeta::MetaValueKind::Array
        || entry.value.elem_type != openmeta::MetaElementType::U8
        || entry.value.count != expected.size()) {
        return false;
    }
    const std::span<const std::byte> raw = store.arena().span(
        entry.value.data.span);
    if (raw.size() != expected.size()) {
        return false;
    }
    for (size_t i = 0; i < expected.size(); ++i) {
        if (std::to_integer<uint8_t>(raw[i]) != expected[i]) {
            return false;
        }
    }
    return true;
}

static bool
decode_transfer_roundtrip_store(std::span<const std::byte> bytes,
                                openmeta::MetaStore* out) noexcept
{
    if (!out) {
        return false;
    }
    *out = openmeta::MetaStore {};
    std::array<openmeta::ContainerBlockRef, 32> blocks {};
    std::array<openmeta::ExifIfdRef, 32> ifds {};
    std::array<std::byte, 8192> payload {};
    std::array<uint32_t, 128> payload_parts {};
    openmeta::SimpleMetaDecodeOptions decode_options;

    const openmeta::SimpleMetaResult read = openmeta::simple_meta_read(
        bytes, *out, blocks, ifds, payload, payload_parts, decode_options);
    if (read.scan.status != openmeta::ScanStatus::Ok
        && read.xmp.status != openmeta::XmpDecodeStatus::Ok) {
        return false;
    }

    out->finalize();
    return true;
}

static bool
decoded_transfer_roundtrip_has_expected_fields(
    std::span<const std::byte> bytes) noexcept
{
    openmeta::MetaStore decoded;
    if (!decode_transfer_roundtrip_store(bytes, &decoded)) {
        return false;
    }

    return store_has_text_entry(decoded, exif_key_view("exififd", 0x9003U),
                                "2024:01:02 03:04:05")
           && store_has_text_entry(
               decoded,
               xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
               "OpenMeta Transfer Source");
}

static bool
store_lacks_text_entry(const openmeta::MetaStore& store,
                       const openmeta::MetaKeyView& key) noexcept
{
    return store.find_all(key).empty();
}

static std::vector<std::byte>
make_minimal_tiff_little_endian()
{
    return {
        std::byte { 'I' },  std::byte { 'I' },  std::byte { 0x2A },
        std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 },
    };
}

static std::vector<std::byte>
make_minimal_bigtiff_little_endian()
{
    return {
        std::byte { 'I' },  std::byte { 'I' },  std::byte { 0x2B },
        std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x10 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 },
    };
}

static std::vector<std::byte>
make_minimal_multipage_tiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42U);
    append_u32le(&tiff, 8U);

    append_u16le(&tiff, 0U);
    append_u32le(&tiff, 14U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 111U);
    append_u32le(&tiff, 32U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 222U);
    append_u32le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_threepage_tiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42U);
    append_u32le(&tiff, 8U);

    append_u16le(&tiff, 0U);
    append_u32le(&tiff, 14U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 111U);
    append_u32le(&tiff, 32U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 222U);
    append_u32le(&tiff, 50U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 333U);
    append_u32le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_ifd1_nested_subifd_tiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42U);
    append_u32le(&tiff, 8U);

    append_u16le(&tiff, 0U);
    append_u32le(&tiff, 14U);

    append_u16le(&tiff, 2U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 111U);
    append_u16le(&tiff, 0x014AU);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 44U);
    append_u32le(&tiff, 0U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 444U);
    append_u32le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_exififd_nested_interop_tiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42U);
    append_u32le(&tiff, 8U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x8769U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 26U);
    append_u32le(&tiff, 0U);

    append_u16le(&tiff, 2U);
    append_u16le(&tiff, 0x9209U);
    append_u16le(&tiff, 3U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 7U);
    append_u16le(&tiff, 0xA005U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 56U);
    append_u32le(&tiff, 0U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0001U);
    append_u16le(&tiff, 2U);
    append_u32le(&tiff, 4U);
    append_bytes(&tiff, "R98");
    tiff.push_back(std::byte { 0x00 });
    append_u32le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_multipage_bigtiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 43U);
    append_u16le(&tiff, 8U);
    append_u16le(&tiff, 0U);
    append_u64le(&tiff, 16U);

    append_u64le(&tiff, 0U);
    append_u64le(&tiff, 32U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 111U);
    append_u64le(&tiff, 68U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 222U);
    append_u64le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_subifd0_nested_subifd_bigtiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 43U);
    append_u16le(&tiff, 8U);
    append_u16le(&tiff, 0U);
    append_u64le(&tiff, 16U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x014AU);
    append_u16le(&tiff, 18U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 52U);
    append_u64le(&tiff, 0U);

    append_u64le(&tiff, 2U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 222U);
    append_u16le(&tiff, 0x014AU);
    append_u16le(&tiff, 18U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 108U);
    append_u64le(&tiff, 0U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 444U);
    append_u64le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_exififd_nested_interop_bigtiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 43U);
    append_u16le(&tiff, 8U);
    append_u16le(&tiff, 0U);
    append_u64le(&tiff, 16U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x8769U);
    append_u16le(&tiff, 18U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 52U);
    append_u64le(&tiff, 0U);

    append_u64le(&tiff, 2U);
    append_u16le(&tiff, 0x9209U);
    append_u16le(&tiff, 3U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 7U);
    append_u16le(&tiff, 0xA005U);
    append_u16le(&tiff, 18U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 108U);
    append_u64le(&tiff, 0U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0001U);
    append_u16le(&tiff, 2U);
    append_u64le(&tiff, 4U);
    append_bytes(&tiff, "R98");
    tiff.push_back(std::byte { 0x00 });
    tiff.resize(tiff.size() + 4U, std::byte { 0x00 });
    append_u64le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_threepage_bigtiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 43U);
    append_u16le(&tiff, 8U);
    append_u16le(&tiff, 0U);
    append_u64le(&tiff, 16U);

    append_u64le(&tiff, 0U);
    append_u64le(&tiff, 32U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 111U);
    append_u64le(&tiff, 68U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 222U);
    append_u64le(&tiff, 104U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 333U);
    append_u64le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_subifd_chain_tiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42U);
    append_u32le(&tiff, 8U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x014AU);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 26U);
    append_u32le(&tiff, 0U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 111U);
    append_u32le(&tiff, 44U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 222U);
    append_u32le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_subifd_chain_bigtiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 43U);
    append_u16le(&tiff, 8U);
    append_u16le(&tiff, 0U);
    append_u64le(&tiff, 16U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x014AU);
    append_u16le(&tiff, 18U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 52U);
    append_u64le(&tiff, 0U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 111U);
    append_u64le(&tiff, 88U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 222U);
    append_u64le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_subifd_pair_tiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42U);
    append_u32le(&tiff, 8U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x014AU);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 2U);
    append_u32le(&tiff, 26U);
    append_u32le(&tiff, 0U);

    append_u32le(&tiff, 34U);
    append_u32le(&tiff, 52U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 111U);
    append_u32le(&tiff, 0U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 777U);
    append_u32le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_subifd_pair_bigtiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 43U);
    append_u16le(&tiff, 8U);
    append_u16le(&tiff, 0U);
    append_u64le(&tiff, 16U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x014AU);
    append_u16le(&tiff, 18U);
    append_u64le(&tiff, 2U);
    append_u64le(&tiff, 52U);
    append_u64le(&tiff, 0U);

    append_u64le(&tiff, 68U);
    append_u64le(&tiff, 104U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 111U);
    append_u64le(&tiff, 0U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 777U);
    append_u64le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_dng_merge_target_tiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42U);
    append_u32le(&tiff, 8U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x014AU);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 2U);
    append_u32le(&tiff, 26U);
    append_u32le(&tiff, 34U);

    append_u32le(&tiff, 70U);
    append_u32le(&tiff, 88U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 111U);
    append_u32le(&tiff, 52U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 222U);
    append_u32le(&tiff, 0U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 111U);
    append_u32le(&tiff, 0U);

    append_u16le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 777U);
    append_u32le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_dng_merge_target_bigtiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 43U);
    append_u16le(&tiff, 8U);
    append_u16le(&tiff, 0U);
    append_u64le(&tiff, 16U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x014AU);
    append_u16le(&tiff, 18U);
    append_u64le(&tiff, 2U);
    append_u64le(&tiff, 52U);
    append_u64le(&tiff, 68U);

    append_u64le(&tiff, 140U);
    append_u64le(&tiff, 176U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 111U);
    append_u64le(&tiff, 104U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 222U);
    append_u64le(&tiff, 0U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 111U);
    append_u64le(&tiff, 0U);

    append_u64le(&tiff, 1U);
    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u64le(&tiff, 1U);
    append_u64le(&tiff, 777U);
    append_u64le(&tiff, 0U);

    return tiff;
}

static std::vector<std::byte>
make_minimal_dng_like_tiff_little_endian()
{
    std::vector<std::byte> tiff;
    append_bytes(&tiff, "II");
    append_u16le(&tiff, 42U);
    append_u32le(&tiff, 8U);

    append_u16le(&tiff, 3U);

    append_u16le(&tiff, 0x010FU);
    append_u16le(&tiff, 2U);
    append_u32le(&tiff, 8U);
    append_u32le(&tiff, 50U);

    append_u16le(&tiff, 0x014AU);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 58U);

    append_u16le(&tiff, 0xC612U);
    append_u16le(&tiff, 1U);
    append_u32le(&tiff, 4U);
    append_u32le(&tiff, 0x00000601U);

    append_u32le(&tiff, 88U);

    EXPECT_EQ(tiff.size(), 50U);
    append_bytes(&tiff, "DNGTEST");
    tiff.push_back(std::byte { 0x00 });

    EXPECT_EQ(tiff.size(), 58U);
    append_u16le(&tiff, 2U);

    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 4000U);

    append_u16le(&tiff, 0x0101U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 3000U);

    append_u32le(&tiff, 0U);

    EXPECT_EQ(tiff.size(), 88U);
    append_u16le(&tiff, 2U);

    append_u16le(&tiff, 0x0100U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 320U);

    append_u16le(&tiff, 0x0101U);
    append_u16le(&tiff, 4U);
    append_u32le(&tiff, 1U);
    append_u32le(&tiff, 240U);

    append_u32le(&tiff, 0U);
    return tiff;
}

TEST(MetadataTransferApi, ContractDefaultsAreStable)
{
    openmeta::PreparedTransferBundle bundle;
    EXPECT_EQ(bundle.contract_version,
              openmeta::kMetadataTransferContractVersion);
    EXPECT_EQ(bundle.target_format, openmeta::TransferTargetFormat::Jpeg);
    EXPECT_EQ(bundle.profile.makernote, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(bundle.profile.jumbf, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(bundle.profile.c2pa, openmeta::TransferPolicyAction::Keep);
    EXPECT_TRUE(bundle.profile.allow_time_patch);
    EXPECT_TRUE(bundle.policy_decisions.empty());
    EXPECT_TRUE(bundle.blocks.empty());
    EXPECT_TRUE(bundle.time_patch_map.empty());
    EXPECT_TRUE(bundle.generated_xmp_sidecar.empty());
}

TEST(MetadataTransferApi, PreparedBlockCarriesRouteAndPayload)
{
    openmeta::PreparedTransferBlock b;
    b.kind     = openmeta::TransferBlockKind::Exif;
    b.order    = 7U;
    b.route    = "jpeg:app1-exif";
    b.box_type = { 'E', 'x', 'i', 'f' };
    b.payload  = { std::byte { 0x45 }, std::byte { 0x78 }, std::byte { 0x69 },
                   std::byte { 0x66 } };

    EXPECT_EQ(b.kind, openmeta::TransferBlockKind::Exif);
    EXPECT_EQ(b.order, 7U);
    EXPECT_EQ(b.route, "jpeg:app1-exif");
    ASSERT_EQ(b.payload.size(), 4U);
    EXPECT_EQ(b.payload[0], std::byte { 0x45 });
    EXPECT_EQ(b.box_type[0], 'E');
}

TEST(MetadataTransferApi, PrepareFileRejectsEmptyPath)
{
    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file("");
    EXPECT_EQ(result.file_status,
              openmeta::TransferFileStatus::InvalidArgument);
    EXPECT_EQ(result.code, openmeta::PrepareTransferFileCode::EmptyPath);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.prepare.code, openmeta::PrepareTransferCode::None);
    EXPECT_EQ(result.prepare.errors, 1U);
}

TEST(MetadataTransferApi, PrepareFilePreservesJpegApp11JumbfForJpegTarget)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("acme");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.bundle.blocks.size(), 1U);
    EXPECT_EQ(result.bundle.blocks[0].kind, openmeta::TransferBlockKind::Jumbf);
    EXPECT_EQ(result.bundle.blocks[0].route, "jpeg:app11-jumbf");

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::Jumbf);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason, openmeta::TransferPolicyReason::Default);
}

TEST(MetadataTransferApi, PrepareFilePreservesJpegApp11JumbfForJxlTarget)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("acme");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format      = openmeta::TransferTargetFormat::Jxl;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.bundle.blocks.size(), 1U);
    EXPECT_EQ(result.bundle.blocks[0].kind, openmeta::TransferBlockKind::Jumbf);
    EXPECT_EQ(result.bundle.blocks[0].route, "jxl:box-jumb");
    EXPECT_EQ(result.bundle.blocks[0].box_type,
              (std::array<char, 4> { 'j', 'u', 'm', 'b' }));
    ASSERT_GE(result.bundle.blocks[0].payload.size(), 8U);
    EXPECT_EQ(result.bundle.blocks[0].payload[4], std::byte { 'j' });
    EXPECT_EQ(result.bundle.blocks[0].payload[5], std::byte { 'u' });
    EXPECT_EQ(result.bundle.blocks[0].payload[6], std::byte { 'm' });
    EXPECT_EQ(result.bundle.blocks[0].payload[7], std::byte { 'd' });

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::Jumbf);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason, openmeta::TransferPolicyReason::Default);
}

TEST(MetadataTransferApi, PrepareFileDropsC2paForJpegTarget)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(result.bundle.blocks.empty());

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::ContentBoundTransferUnavailable);
    EXPECT_EQ(decision->c2pa_mode, openmeta::TransferC2paMode::Drop);
    EXPECT_EQ(decision->c2pa_source_kind,
              openmeta::TransferC2paSourceKind::ContentBound);
    EXPECT_EQ(decision->c2pa_prepared_output,
              openmeta::TransferC2paPreparedOutput::Dropped);
}

TEST(MetadataTransferApi, PrepareFileBuildsDraftC2paInvalidationForJpegTarget)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Invalidate;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.bundle.blocks.size(), 1U);
    EXPECT_EQ(result.bundle.blocks[0].kind, openmeta::TransferBlockKind::C2pa);
    EXPECT_EQ(result.bundle.blocks[0].route, "jpeg:app11-c2pa");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(result.bundle.blocks[0].payload.data(),
                                   result.bundle.blocks[0].payload.size()),
        "openmeta:c2pa_invalidation"));

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Invalidate);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::DraftInvalidationPayload);
    EXPECT_EQ(decision->c2pa_mode,
              openmeta::TransferC2paMode::DraftUnsignedInvalidation);
    EXPECT_EQ(decision->c2pa_source_kind,
              openmeta::TransferC2paSourceKind::ContentBound);
    EXPECT_EQ(
        decision->c2pa_prepared_output,
        openmeta::TransferC2paPreparedOutput::GeneratedDraftUnsignedInvalidation);
    EXPECT_EQ(result.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::NotRequested);
    EXPECT_EQ(result.bundle.c2pa_rewrite.target_format,
              openmeta::TransferTargetFormat::Jpeg);
    EXPECT_EQ(result.bundle.c2pa_rewrite.source_kind,
              openmeta::TransferC2paSourceKind::ContentBound);
    EXPECT_GT(result.bundle.c2pa_rewrite.matched_entries, 0U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.existing_carrier_segments, 1U);
    EXPECT_FALSE(result.bundle.c2pa_rewrite.requires_manifest_builder);
    EXPECT_FALSE(result.bundle.c2pa_rewrite.requires_private_key);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    EXPECT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(result.bundle.blocks[0].payload.data(),
                                   result.bundle.blocks[0].payload.size()),
        "openmeta:c2pa_contract"));
}

TEST(MetadataTransferApi, PrepareFilePreservesDraftC2paForJpegTarget)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_draft_c2pa_invalidation();
    const std::string path = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa       = openmeta::TransferPolicyAction::Keep;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(count_blocks_with_route(result.bundle, "jpeg:app11-c2pa"), 1U);

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason, openmeta::TransferPolicyReason::Default);
    EXPECT_EQ(decision->c2pa_mode, openmeta::TransferC2paMode::PreserveRaw);
    EXPECT_EQ(decision->c2pa_source_kind,
              openmeta::TransferC2paSourceKind::DraftUnsignedInvalidation);
    EXPECT_EQ(decision->c2pa_prepared_output,
              openmeta::TransferC2paPreparedOutput::PreservedRaw);
    EXPECT_EQ(result.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::NotRequested);
    EXPECT_EQ(result.bundle.c2pa_rewrite.source_kind,
              openmeta::TransferC2paSourceKind::DraftUnsignedInvalidation);
    EXPECT_GT(result.bundle.c2pa_rewrite.matched_entries, 0U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.existing_carrier_segments, 1U);
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(result.bundle.blocks[0].payload.data(),
                                   result.bundle.blocks[0].payload.size()),
        "openmeta:c2pa_invalidation"));
}

TEST(MetadataTransferApi, PrepareFilePreservesDraftC2paForJxlTarget)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_draft_c2pa_invalidation();
    const std::string path = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format      = openmeta::TransferTargetFormat::Jxl;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa       = openmeta::TransferPolicyAction::Keep;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.bundle.blocks.size(), 1U);
    EXPECT_EQ(result.bundle.blocks[0].kind, openmeta::TransferBlockKind::C2pa);
    EXPECT_EQ(result.bundle.blocks[0].route, "jxl:box-jumb");
    EXPECT_EQ(result.bundle.blocks[0].box_type,
              (std::array<char, 4> { 'j', 'u', 'm', 'b' }));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(result.bundle.blocks[0].payload.data(),
                                   result.bundle.blocks[0].payload.size()),
        "openmeta:c2pa_invalidation"));

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason, openmeta::TransferPolicyReason::Default);
    EXPECT_EQ(decision->c2pa_mode, openmeta::TransferC2paMode::PreserveRaw);
    EXPECT_EQ(decision->c2pa_source_kind,
              openmeta::TransferC2paSourceKind::DraftUnsignedInvalidation);
    EXPECT_EQ(decision->c2pa_prepared_output,
              openmeta::TransferC2paPreparedOutput::PreservedRaw);
}

TEST(MetadataTransferApi, PrepareFileBuildsDraftC2paInvalidationForJxlTarget)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format      = openmeta::TransferTargetFormat::Jxl;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Invalidate;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.bundle.blocks.size(), 1U);
    EXPECT_EQ(result.bundle.blocks[0].kind, openmeta::TransferBlockKind::C2pa);
    EXPECT_EQ(result.bundle.blocks[0].route, "jxl:box-jumb");
    EXPECT_EQ(result.bundle.blocks[0].box_type,
              (std::array<char, 4> { 'j', 'u', 'm', 'b' }));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(result.bundle.blocks[0].payload.data(),
                                   result.bundle.blocks[0].payload.size()),
        "openmeta:c2pa_invalidation"));

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Invalidate);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::DraftInvalidationPayload);
    EXPECT_EQ(decision->c2pa_mode,
              openmeta::TransferC2paMode::DraftUnsignedInvalidation);
    EXPECT_EQ(decision->c2pa_source_kind,
              openmeta::TransferC2paSourceKind::ContentBound);
    EXPECT_EQ(
        decision->c2pa_prepared_output,
        openmeta::TransferC2paPreparedOutput::GeneratedDraftUnsignedInvalidation);
    EXPECT_EQ(result.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::NotRequested);
    EXPECT_EQ(result.bundle.c2pa_rewrite.target_format,
              openmeta::TransferTargetFormat::Jxl);
    EXPECT_EQ(result.bundle.c2pa_rewrite.source_kind,
              openmeta::TransferC2paSourceKind::ContentBound);
    EXPECT_GT(result.bundle.c2pa_rewrite.matched_entries, 0U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.existing_carrier_segments, 1U);
    EXPECT_FALSE(result.bundle.c2pa_rewrite.requires_manifest_builder);
    EXPECT_FALSE(result.bundle.c2pa_rewrite.requires_private_key);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    EXPECT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(result.bundle.blocks[0].payload.data(),
                                   result.bundle.blocks[0].payload.size()),
        "openmeta:c2pa_contract"));
}

TEST(MetadataTransferApi, PrepareFileRejectsSignedRewriteForJxlTarget)
{
    const std::vector<std::byte> jxl = make_test_jxl_with_c2pa(false);
    const std::string path           = unique_temp_path(".jxl");
    ASSERT_TRUE(write_bytes_file(path, std::span<const std::byte>(jxl.data(),
                                                                  jxl.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format      = openmeta::TransferTargetFormat::Jxl;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::SignedRewriteUnavailable);
    EXPECT_EQ(decision->c2pa_mode, openmeta::TransferC2paMode::Drop);
    EXPECT_EQ(decision->c2pa_source_kind,
              openmeta::TransferC2paSourceKind::ContentBound);
    EXPECT_EQ(decision->c2pa_prepared_output,
              openmeta::TransferC2paPreparedOutput::Dropped);
    EXPECT_EQ(result.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::SigningMaterialRequired);
    EXPECT_EQ(result.bundle.c2pa_rewrite.target_format,
              openmeta::TransferTargetFormat::Jxl);
    EXPECT_EQ(result.bundle.c2pa_rewrite.source_kind,
              openmeta::TransferC2paSourceKind::ContentBound);
    EXPECT_GT(result.bundle.c2pa_rewrite.matched_entries, 0U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.existing_carrier_segments, 1U);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.target_carrier_available);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.content_change_invalidates_existing);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.requires_manifest_builder);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.requires_content_binding);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.requires_certificate_chain);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.requires_private_key);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.requires_signing_time);
    EXPECT_EQ(result.bundle.c2pa_rewrite.content_binding_bytes,
              static_cast<uint64_t>(make_minimal_jxl_file().size()));
    ASSERT_EQ(result.bundle.c2pa_rewrite.content_binding_chunks.size(), 1U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.content_binding_chunks[0].kind,
              openmeta::TransferC2paRewriteChunkKind::SourceRange);
    EXPECT_EQ(result.bundle.c2pa_rewrite.content_binding_chunks[0].source_offset,
              0U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.content_binding_chunks[0].size,
              static_cast<uint64_t>(make_minimal_jxl_file().size()));

    openmeta::PreparedTransferC2paSignRequest sign_request;
    EXPECT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(sign_request.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(sign_request.carrier_route, "jxl:box-jumb");
    EXPECT_EQ(sign_request.manifest_label, "c2pa");
    EXPECT_EQ(sign_request.source_range_chunks, 1U);
    EXPECT_EQ(sign_request.prepared_segment_chunks, 0U);
    EXPECT_EQ(sign_request.content_binding_bytes,
              static_cast<uint64_t>(make_minimal_jxl_file().size()));
    EXPECT_TRUE(result.bundle.c2pa_rewrite.message.find("signed c2pa rewrite")
                != std::string::npos);
}

TEST(MetadataTransferApi, PrepareFileRejectsSignedRewriteForJpegTarget)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::SignedRewriteUnavailable);
    EXPECT_EQ(decision->c2pa_mode, openmeta::TransferC2paMode::Drop);
    EXPECT_EQ(decision->c2pa_source_kind,
              openmeta::TransferC2paSourceKind::ContentBound);
    EXPECT_EQ(decision->c2pa_prepared_output,
              openmeta::TransferC2paPreparedOutput::Dropped);
    EXPECT_EQ(result.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::SigningMaterialRequired);
    EXPECT_EQ(result.bundle.c2pa_rewrite.target_format,
              openmeta::TransferTargetFormat::Jpeg);
    EXPECT_EQ(result.bundle.c2pa_rewrite.source_kind,
              openmeta::TransferC2paSourceKind::ContentBound);
    EXPECT_GT(result.bundle.c2pa_rewrite.matched_entries, 0U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.existing_carrier_segments, 1U);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.target_carrier_available);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.content_change_invalidates_existing);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.requires_manifest_builder);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.requires_content_binding);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.requires_certificate_chain);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.requires_private_key);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.requires_signing_time);
    EXPECT_EQ(result.bundle.c2pa_rewrite.content_binding_bytes, 4U);
    ASSERT_EQ(result.bundle.c2pa_rewrite.content_binding_chunks.size(), 2U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.content_binding_chunks[0].kind,
              openmeta::TransferC2paRewriteChunkKind::SourceRange);
    EXPECT_EQ(result.bundle.c2pa_rewrite.content_binding_chunks[0].source_offset,
              0U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.content_binding_chunks[0].size, 2U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.content_binding_chunks[1].kind,
              openmeta::TransferC2paRewriteChunkKind::SourceRange);
    EXPECT_EQ(result.bundle.c2pa_rewrite.content_binding_chunks[1].size, 2U);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    EXPECT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(sign_request.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(sign_request.carrier_route, "jpeg:app11-c2pa");
    EXPECT_EQ(sign_request.manifest_label, "c2pa");
    EXPECT_EQ(sign_request.source_range_chunks, 2U);
    EXPECT_EQ(sign_request.prepared_segment_chunks, 0U);
    EXPECT_EQ(sign_request.content_binding_bytes, 4U);
    EXPECT_TRUE(result.bundle.c2pa_rewrite.message.find("signed c2pa rewrite")
                != std::string::npos);
}

TEST(MetadataTransferApi, PrepareFileMixedRawC2paPrefersContentBoundDrop)
{
    const std::vector<std::byte> draft_jpeg
        = make_jpeg_with_draft_c2pa_invalidation();
    const std::vector<std::byte> content_bound_seg = make_app11_jumbf_payload(
        "c2pa");
    ASSERT_GE(draft_jpeg.size(), 4U);
    std::vector<std::byte> jpeg;
    jpeg.insert(jpeg.end(), draft_jpeg.begin(), draft_jpeg.end() - 2);
    append_test_jpeg_segment(
        &jpeg, 0xEBU,
        std::span<const std::byte>(content_bound_seg.data(),
                                   content_bound_seg.size()));
    jpeg.push_back(std::byte { 0xFF });
    jpeg.push_back(std::byte { 0xD9 });

    const std::string path = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa       = openmeta::TransferPolicyAction::Keep;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::ContentBoundTransferUnavailable);
    EXPECT_EQ(decision->c2pa_mode, openmeta::TransferC2paMode::Drop);
    EXPECT_EQ(decision->c2pa_source_kind,
              openmeta::TransferC2paSourceKind::ContentBound);
    EXPECT_EQ(decision->c2pa_prepared_output,
              openmeta::TransferC2paPreparedOutput::Dropped);
    EXPECT_EQ(result.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::NotRequested);
    EXPECT_EQ(result.bundle.c2pa_rewrite.source_kind,
              openmeta::TransferC2paSourceKind::ContentBound);
    EXPECT_GT(result.bundle.c2pa_rewrite.matched_entries, 0U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.existing_carrier_segments, 2U);
}

TEST(MetadataTransferApi, PrepareFileRewriteBuildsBindingChunkForPreparedExif)
{
    const std::vector<std::byte> exif = make_app1_exif_payload();
    const std::vector<std::byte> c2pa = make_app11_jumbf_payload("c2pa");
    const TestJpegSegment segments[]  = {
        { 0xE1U, std::span<const std::byte>(exif.data(), exif.size()) },
        { 0xEBU, std::span<const std::byte>(c2pa.data(), c2pa.size()) },
    };
    const std::vector<std::byte> jpeg = make_jpeg_with_segments(segments);
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_GT(result.prepare.warnings, 0U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::SigningMaterialRequired);
    EXPECT_GT(result.bundle.c2pa_rewrite.content_binding_bytes, 4U);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    EXPECT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    EXPECT_GT(sign_request.prepared_segment_chunks, 0U);
    EXPECT_GT(sign_request.content_binding_bytes, 4U);

    bool saw_prepared_exif = false;
    for (size_t i = 0;
         i < result.bundle.c2pa_rewrite.content_binding_chunks.size(); ++i) {
        const openmeta::PreparedTransferC2paRewriteChunk& chunk
            = result.bundle.c2pa_rewrite.content_binding_chunks[i];
        if (chunk.kind
            != openmeta::TransferC2paRewriteChunkKind::PreparedJpegSegment) {
            continue;
        }
        ASSERT_LT(chunk.block_index, result.bundle.blocks.size());
        EXPECT_EQ(chunk.jpeg_marker_code, 0xE1U);
        if (result.bundle.blocks[chunk.block_index].route == "jpeg:app1-exif") {
            saw_prepared_exif = true;
        }
    }
    EXPECT_TRUE(saw_prepared_exif);
}

TEST(MetadataTransferApi,
     BuildPreparedC2paSignRequestBindingMaterializesSourceOnlyBytes)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);

    std::vector<std::byte> binding;
    const openmeta::BuildPreparedC2paBindingResult build
        = openmeta::build_prepared_c2pa_sign_request_binding(
            result.bundle, std::span<const std::byte>(jpeg.data(), jpeg.size()),
            sign_request, &binding);

    EXPECT_EQ(build.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(build.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(build.errors, 0U);
    EXPECT_EQ(build.written, 4U);
    ASSERT_EQ(binding.size(), 4U);
    EXPECT_EQ(binding[0], std::byte { 0xFF });
    EXPECT_EQ(binding[1], std::byte { 0xD8 });
    EXPECT_EQ(binding[2], std::byte { 0xFF });
    EXPECT_EQ(binding[3], std::byte { 0xD9 });
}

TEST(MetadataTransferApi,
     BuildPreparedC2paSignRequestBindingMaterializesPreparedExifSegment)
{
    const std::vector<std::byte> exif = make_app1_exif_payload();
    const std::vector<std::byte> c2pa = make_app11_jumbf_payload("c2pa");
    const TestJpegSegment segments[]  = {
        { 0xE1U, std::span<const std::byte>(exif.data(), exif.size()) },
        { 0xEBU, std::span<const std::byte>(c2pa.data(), c2pa.size()) },
    };
    const std::vector<std::byte> jpeg = make_jpeg_with_segments(segments);
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_NE(result.prepare.status, openmeta::TransferStatus::Malformed);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);

    std::vector<std::byte> binding;
    const openmeta::BuildPreparedC2paBindingResult build
        = openmeta::build_prepared_c2pa_sign_request_binding(
            result.bundle, std::span<const std::byte>(jpeg.data(), jpeg.size()),
            sign_request, &binding);

    EXPECT_EQ(build.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(build.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(build.errors, 0U);
    EXPECT_EQ(build.written, sign_request.content_binding_bytes);
    EXPECT_GT(binding.size(), 4U);
    ASSERT_GE(binding.size(), 6U);
    EXPECT_EQ(binding[0], std::byte { 0xFF });
    EXPECT_EQ(binding[1], std::byte { 0xD8 });
    EXPECT_TRUE(contains_byte_pair(std::span<const std::byte>(binding.data(),
                                                              binding.size()),
                                   0xFFU, 0xE1U));
    EXPECT_EQ(binding[binding.size() - 2U], std::byte { 0xFF });
    EXPECT_EQ(binding[binding.size() - 1U], std::byte { 0xD9 });
}

TEST(MetadataTransferApi,
     BuildPreparedC2paSignRequestBindingRejectsPlanMismatch)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);

    result.bundle.c2pa_rewrite.content_binding_bytes += 1U;
    std::vector<std::byte> binding(1U, std::byte { 0xAA });
    const openmeta::BuildPreparedC2paBindingResult build
        = openmeta::build_prepared_c2pa_sign_request_binding(
            result.bundle, std::span<const std::byte>(jpeg.data(), jpeg.size()),
            sign_request, &binding);

    EXPECT_EQ(build.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(build.code, openmeta::EmitTransferCode::PlanMismatch);
    EXPECT_EQ(build.errors, 1U);
    EXPECT_TRUE(binding.empty());
}

TEST(MetadataTransferApi,
     BuildPreparedC2paSignRequestBindingMaterializesJxlSourceOnlyBytes)
{
    const std::vector<std::byte> jxl = make_test_jxl_with_c2pa(false);
    const std::string path           = unique_temp_path(".jxl");
    ASSERT_TRUE(write_bytes_file(path, std::span<const std::byte>(jxl.data(),
                                                                  jxl.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format      = openmeta::TransferTargetFormat::Jxl;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);

    std::vector<std::byte> binding;
    const openmeta::BuildPreparedC2paBindingResult build
        = openmeta::build_prepared_c2pa_sign_request_binding(
            result.bundle, std::span<const std::byte>(jxl.data(), jxl.size()),
            sign_request, &binding);

    const std::vector<std::byte> expected = make_minimal_jxl_file();
    EXPECT_EQ(build.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(build.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(build.errors, 0U);
    EXPECT_EQ(build.written, expected.size());
    EXPECT_EQ(binding, expected);
}

TEST(MetadataTransferApi,
     BuildPreparedC2paSignRequestBindingMaterializesPreparedJxlExifBox)
{
    const std::vector<std::byte> jxl = make_test_jxl_with_c2pa(true);
    const std::string path           = unique_temp_path(".jxl");
    ASSERT_TRUE(write_bytes_file(path, std::span<const std::byte>(jxl.data(),
                                                                  jxl.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format      = openmeta::TransferTargetFormat::Jxl;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_GT(result.prepare.warnings, 0U);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    EXPECT_GT(sign_request.prepared_segment_chunks, 0U);

    std::vector<std::byte> binding;
    const openmeta::BuildPreparedC2paBindingResult build
        = openmeta::build_prepared_c2pa_sign_request_binding(
            result.bundle, std::span<const std::byte>(jxl.data(), jxl.size()),
            sign_request, &binding);

    EXPECT_EQ(build.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(build.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(build.errors, 0U);
    EXPECT_EQ(build.written, sign_request.content_binding_bytes);

    std::vector<std::byte> expected = make_minimal_jxl_file();
    const std::vector<std::byte> exif_payload = make_test_jxl_exif_box_payload();
    append_bmff_box(&expected, openmeta::fourcc('E', 'x', 'i', 'f'),
                    std::span<const std::byte>(exif_payload.data(),
                                               exif_payload.size()));
    EXPECT_EQ(binding, expected);

    bool saw_prepared_exif = false;
    for (size_t i = 0;
         i < result.bundle.c2pa_rewrite.content_binding_chunks.size(); ++i) {
        const openmeta::PreparedTransferC2paRewriteChunk& chunk
            = result.bundle.c2pa_rewrite.content_binding_chunks[i];
        if (chunk.kind
            != openmeta::TransferC2paRewriteChunkKind::PreparedJxlBox) {
            continue;
        }
        ASSERT_LT(chunk.block_index, result.bundle.blocks.size());
        if (result.bundle.blocks[chunk.block_index].route == "jxl:box-exif") {
            saw_prepared_exif = true;
        }
    }
    EXPECT_TRUE(saw_prepared_exif);
}

TEST(MetadataTransferApi,
     BuildPreparedC2paSignRequestBindingMaterializesBmffSourceOnlyBytes)
{
    const std::vector<std::byte> bmff = make_test_bmff_with_c2pa(false);
    ASSERT_FALSE(bmff.empty());
    const std::string path = unique_temp_path(".heic");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(bmff.data(), bmff.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format      = openmeta::TransferTargetFormat::Heif;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(sign_request.target_format, openmeta::TransferTargetFormat::Heif);
    EXPECT_EQ(sign_request.carrier_route, "bmff:item-c2pa");

    std::vector<std::byte> binding;
    const openmeta::BuildPreparedC2paBindingResult build
        = openmeta::build_prepared_c2pa_sign_request_binding(
            result.bundle, std::span<const std::byte>(bmff.data(), bmff.size()),
            sign_request, &binding);

    const std::vector<std::byte> expected = make_minimal_bmff_file();
    EXPECT_EQ(build.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(build.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(build.errors, 0U);
    EXPECT_EQ(build.written, expected.size());
    EXPECT_EQ(binding, expected);
}

TEST(MetadataTransferApi,
     BuildPreparedC2paSignRequestBindingMaterializesBmffMetaBox)
{
    const std::vector<std::byte> input = make_minimal_bmff_file();

    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Heif;

    openmeta::PreparedTransferBlock exif;
    exif.kind    = openmeta::TransferBlockKind::Exif;
    exif.order   = 100U;
    exif.route   = "bmff:item-exif";
    exif.payload = make_test_bmff_exif_item_payload();
    bundle.blocks.push_back(std::move(exif));

    openmeta::ExecutePreparedTransferOptions execute_options;
    execute_options.edit_requested = true;
    execute_options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult expected
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            execute_options);
    ASSERT_EQ(expected.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_GT(expected.edited_output.size(), input.size());

    bundle.c2pa_rewrite.state
        = openmeta::TransferC2paRewriteState::SigningMaterialRequired;
    bundle.c2pa_rewrite.target_format = openmeta::TransferTargetFormat::Heif;
    bundle.c2pa_rewrite.source_kind
        = openmeta::TransferC2paSourceKind::ContentBound;
    bundle.c2pa_rewrite.target_carrier_available   = true;
    bundle.c2pa_rewrite.requires_manifest_builder  = true;
    bundle.c2pa_rewrite.requires_content_binding   = true;
    bundle.c2pa_rewrite.requires_certificate_chain = true;
    bundle.c2pa_rewrite.requires_private_key       = true;
    bundle.c2pa_rewrite.requires_signing_time      = true;

    openmeta::PreparedTransferC2paRewriteChunk source_chunk;
    source_chunk.kind = openmeta::TransferC2paRewriteChunkKind::SourceRange;
    source_chunk.source_offset = 0U;
    source_chunk.size          = static_cast<uint64_t>(input.size());

    openmeta::PreparedTransferC2paRewriteChunk meta_chunk;
    meta_chunk.kind
        = openmeta::TransferC2paRewriteChunkKind::PreparedBmffMetaBox;
    meta_chunk.size = static_cast<uint64_t>(expected.edited_output.size()
                                            - input.size());

    bundle.c2pa_rewrite.content_binding_chunks.push_back(source_chunk);
    bundle.c2pa_rewrite.content_binding_chunks.push_back(meta_chunk);
    bundle.c2pa_rewrite.content_binding_bytes = static_cast<uint64_t>(
        expected.edited_output.size());

    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(bundle, &sign_request),
              openmeta::TransferStatus::Ok);
    ASSERT_EQ(sign_request.content_binding_chunks.size(), 2U);
    EXPECT_EQ(sign_request.content_binding_chunks[1].kind,
              openmeta::TransferC2paRewriteChunkKind::PreparedBmffMetaBox);

    std::vector<std::byte> binding;
    const openmeta::BuildPreparedC2paBindingResult build
        = openmeta::build_prepared_c2pa_sign_request_binding(
            bundle, std::span<const std::byte>(input.data(), input.size()),
            sign_request, &binding);

    EXPECT_EQ(build.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(build.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(build.errors, 0U);
    EXPECT_EQ(build.written, sign_request.content_binding_bytes);
    EXPECT_EQ(binding, expected.edited_output);
}

TEST(MetadataTransferApi, BuildPreparedC2paHandoffPackageMaterializesBinding)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paHandoffPackage handoff;
    ASSERT_EQ(openmeta::build_prepared_c2pa_handoff_package(
                  result.bundle,
                  std::span<const std::byte>(jpeg.data(), jpeg.size()),
                  &handoff),
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(handoff.request.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(handoff.binding.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(handoff.binding.written, 4U);
    ASSERT_EQ(handoff.binding_bytes.size(), 4U);
    EXPECT_EQ(handoff.binding_bytes[0], std::byte { 0xFF });
    EXPECT_EQ(handoff.binding_bytes[1], std::byte { 0xD8 });
    EXPECT_EQ(handoff.binding_bytes[2], std::byte { 0xFF });
    EXPECT_EQ(handoff.binding_bytes[3], std::byte { 0xD9 });
}

TEST(MetadataTransferApi, SerializePreparedC2paHandoffPackageRoundTrip)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paHandoffPackage handoff;
    ASSERT_EQ(openmeta::build_prepared_c2pa_handoff_package(
                  result.bundle,
                  std::span<const std::byte>(jpeg.data(), jpeg.size()),
                  &handoff),
              openmeta::TransferStatus::Ok);

    std::vector<std::byte> encoded;
    const openmeta::PreparedTransferC2paPackageIoResult encode
        = openmeta::serialize_prepared_c2pa_handoff_package(handoff, &encoded);
    ASSERT_EQ(encode.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(encoded.empty());

    openmeta::PreparedTransferC2paHandoffPackage decoded;
    const openmeta::PreparedTransferC2paPackageIoResult decode
        = openmeta::deserialize_prepared_c2pa_handoff_package(
            std::span<const std::byte>(encoded.data(), encoded.size()),
            &decoded);
    ASSERT_EQ(decode.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(decoded.request.status, handoff.request.status);
    EXPECT_EQ(decoded.request.rewrite_state, handoff.request.rewrite_state);
    EXPECT_EQ(decoded.request.carrier_route, handoff.request.carrier_route);
    EXPECT_EQ(decoded.request.manifest_label, handoff.request.manifest_label);
    EXPECT_EQ(decoded.request.content_binding_bytes,
              handoff.request.content_binding_bytes);
    EXPECT_EQ(decoded.binding.status, handoff.binding.status);
    EXPECT_EQ(decoded.binding.code, handoff.binding.code);
    EXPECT_EQ(decoded.binding.written, handoff.binding.written);
    EXPECT_EQ(decoded.binding_bytes, handoff.binding_bytes);
}

TEST(MetadataTransferApi, SerializePreparedC2paSignedPackageRoundTrip)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    const std::vector<std::byte> logical = make_logical_jumbf_payload("c2pa");
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));
    openmeta::PreparedTransferC2paSignedPackage package;
    ASSERT_EQ(openmeta::build_prepared_c2pa_signed_package(result.bundle, input,
                                                           &package),
              openmeta::TransferStatus::Ok);

    std::vector<std::byte> encoded;
    const openmeta::PreparedTransferC2paPackageIoResult encode
        = openmeta::serialize_prepared_c2pa_signed_package(package, &encoded);
    ASSERT_EQ(encode.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(encoded.empty());

    openmeta::PreparedTransferC2paSignedPackage decoded;
    const openmeta::PreparedTransferC2paPackageIoResult decode
        = openmeta::deserialize_prepared_c2pa_signed_package(
            std::span<const std::byte>(encoded.data(), encoded.size()),
            &decoded);
    ASSERT_EQ(decode.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(decoded.request.status, package.request.status);
    EXPECT_EQ(decoded.request.rewrite_state, package.request.rewrite_state);
    EXPECT_EQ(decoded.request.carrier_route, package.request.carrier_route);
    EXPECT_EQ(decoded.request.content_binding_chunks.size(),
              package.request.content_binding_chunks.size());
    EXPECT_EQ(decoded.signer_input.signing_time, input.signing_time);
    EXPECT_EQ(decoded.signer_input.private_key_reference,
              input.private_key_reference);
    EXPECT_EQ(decoded.signer_input.certificate_chain_bytes,
              input.certificate_chain_bytes);
    EXPECT_EQ(decoded.signer_input.manifest_builder_output,
              input.manifest_builder_output);
    EXPECT_EQ(decoded.signer_input.signed_c2pa_logical_payload,
              input.signed_c2pa_logical_payload);
}

TEST(MetadataTransferApi, ValidatePreparedC2paSignResultAcceptsContentBound)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Ok);
    EXPECT_EQ(validate.semantic_reason, "ok");
    EXPECT_EQ(validate.semantic_manifest_present, 1U);
    EXPECT_EQ(validate.semantic_manifest_count, 1U);
    EXPECT_EQ(validate.semantic_claim_generator_present, 1U);
    EXPECT_EQ(validate.semantic_assertion_count, 1U);
    EXPECT_EQ(validate.semantic_primary_claim_assertion_count, 1U);
    EXPECT_EQ(validate.semantic_primary_claim_referenced_by_signature_count,
              1U);
    EXPECT_EQ(validate.semantic_primary_signature_linked_claim_count, 1U);
    EXPECT_EQ(validate.semantic_primary_signature_reference_key_hits, 0U);
    EXPECT_EQ(validate.semantic_primary_signature_explicit_reference_present,
              0U);
    EXPECT_EQ(
        validate
            .semantic_primary_signature_explicit_reference_resolved_claim_count,
        0U);
    EXPECT_EQ(validate.semantic_claim_count, 1U);
    EXPECT_EQ(validate.semantic_signature_count, 1U);
    EXPECT_EQ(validate.semantic_signature_linked, 1U);
    EXPECT_EQ(validate.semantic_signature_orphan, 0U);
    EXPECT_EQ(validate.logical_payload_bytes, logical.size());
    EXPECT_GT(validate.staged_payload_bytes, 0U);
    EXPECT_EQ(validate.staged_segments, 1U);
    EXPECT_EQ(validate.errors, 0U);
}

TEST(MetadataTransferApi, ValidatePreparedC2paSignResultAcceptsContentBoundBmff)
{
    const std::vector<std::byte> bmff = make_test_bmff_with_c2pa(false);
    ASSERT_FALSE(bmff.empty());
    const std::string path = unique_temp_path(".heic");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(bmff.data(), bmff.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format      = openmeta::TransferTargetFormat::Heif;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Ok);
    EXPECT_EQ(validate.semantic_reason, "ok");
    EXPECT_EQ(validate.logical_payload_bytes, logical.size());
    EXPECT_EQ(validate.staged_payload_bytes, logical.size());
    EXPECT_EQ(validate.staged_segments, 1U);
    EXPECT_EQ(validate.errors, 0U);
}

TEST(MetadataTransferApi, ValidatePreparedC2paSignResultAcceptsContentBoundJxl)
{
    const std::vector<std::byte> jxl = make_test_jxl_with_c2pa(false);
    const std::string path           = unique_temp_path(".jxl");
    ASSERT_TRUE(write_bytes_file(path, std::span<const std::byte>(jxl.data(),
                                                                  jxl.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format      = openmeta::TransferTargetFormat::Jxl;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Ok);
    EXPECT_EQ(validate.semantic_reason, "ok");
    EXPECT_EQ(validate.logical_payload_bytes, logical.size());
    EXPECT_EQ(validate.staged_payload_bytes, logical.size());
    EXPECT_EQ(validate.staged_segments, 1U);
    EXPECT_EQ(validate.errors, 0U);
}

TEST(MetadataTransferApi, ValidatePreparedC2paSignResultRejectsSemanticGap)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical = make_logical_jumbf_payload("c2pa");
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Invalid);
    EXPECT_EQ(validate.semantic_reason, "manifest_missing");
    EXPECT_EQ(validate.semantic_manifest_present, 0U);
    EXPECT_EQ(validate.semantic_manifest_count, 0U);
    EXPECT_EQ(validate.semantic_claim_generator_present, 0U);
    EXPECT_EQ(validate.semantic_claim_count, 0U);
    EXPECT_EQ(validate.semantic_signature_count, 0U);
    EXPECT_EQ(validate.staged_segments, 0U);
    EXPECT_EQ(validate.errors, 1U);
}

TEST(MetadataTransferApi,
     ValidatePreparedC2paSignResultRejectsManifestCountDrift)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical
        = make_semantic_c2pa_logical_payload(2U, true);
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Invalid);
    EXPECT_EQ(validate.semantic_reason, "manifest_count_invalid");
    EXPECT_EQ(validate.semantic_manifest_present, 1U);
    EXPECT_EQ(validate.semantic_manifest_count, 2U);
    EXPECT_EQ(validate.semantic_claim_generator_present, 1U);
    EXPECT_EQ(validate.staged_segments, 0U);
    EXPECT_EQ(validate.errors, 1U);
}

TEST(MetadataTransferApi,
     ValidatePreparedC2paSignResultRejectsMissingClaimGenerator)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical
        = make_semantic_c2pa_logical_payload(1U, false);
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Invalid);
    EXPECT_EQ(validate.semantic_reason, "claim_generator_missing");
    EXPECT_EQ(validate.semantic_manifest_present, 1U);
    EXPECT_EQ(validate.semantic_manifest_count, 1U);
    EXPECT_EQ(validate.semantic_claim_generator_present, 0U);
    EXPECT_EQ(validate.semantic_assertion_count, 1U);
    EXPECT_EQ(validate.staged_segments, 0U);
    EXPECT_EQ(validate.errors, 1U);
}

TEST(MetadataTransferApi,
     ValidatePreparedC2paSignResultRejectsMissingContentBindingAssertions)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical
        = make_semantic_c2pa_logical_payload(1U, true, true, false);
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Invalid);
    EXPECT_EQ(validate.semantic_reason, "content_binding_assertions_missing");
    EXPECT_EQ(validate.semantic_manifest_present, 1U);
    EXPECT_EQ(validate.semantic_manifest_count, 1U);
    EXPECT_EQ(validate.semantic_claim_generator_present, 1U);
    EXPECT_EQ(validate.semantic_assertion_count, 0U);
    EXPECT_EQ(validate.semantic_primary_claim_assertion_count, 0U);
    EXPECT_EQ(validate.staged_segments, 0U);
    EXPECT_EQ(validate.errors, 1U);
}

TEST(MetadataTransferApi,
     ValidatePreparedC2paSignResultRejectsMissingSignatureAlgorithm)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical
        = make_semantic_c2pa_logical_payload(1U, true, false);
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Invalid);
    EXPECT_EQ(validate.semantic_reason, "signature_algorithm_missing");
    EXPECT_EQ(validate.semantic_manifest_present, 1U);
    EXPECT_EQ(validate.semantic_manifest_count, 1U);
    EXPECT_EQ(validate.semantic_claim_generator_present, 1U);
    EXPECT_EQ(validate.semantic_assertion_count, 1U);
    EXPECT_EQ(validate.staged_segments, 0U);
    EXPECT_EQ(validate.errors, 1U);
}

TEST(MetadataTransferApi,
     ValidatePreparedC2paSignResultRejectsPrimarySignatureClaimDrift)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical
        = make_semantic_c2pa_logical_payload_primary_signature_refs_second_claim();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Invalid);
    EXPECT_EQ(validate.semantic_reason, "primary_signature_claim_drift");
    EXPECT_EQ(validate.semantic_manifest_present, 1U);
    EXPECT_EQ(validate.semantic_manifest_count, 1U);
    EXPECT_EQ(validate.semantic_claim_generator_present, 1U);
    EXPECT_EQ(validate.semantic_assertion_count, 2U);
    EXPECT_EQ(validate.semantic_primary_claim_assertion_count, 1U);
    EXPECT_EQ(validate.semantic_primary_claim_referenced_by_signature_count,
              0U);
    EXPECT_EQ(validate.semantic_primary_signature_linked_claim_count, 1U);
    EXPECT_EQ(validate.semantic_primary_signature_reference_key_hits, 1U);
    EXPECT_EQ(validate.semantic_primary_signature_explicit_reference_present,
              1U);
    EXPECT_EQ(
        validate
            .semantic_primary_signature_explicit_reference_resolved_claim_count,
        1U);
    EXPECT_EQ(validate.semantic_claim_count, 2U);
    EXPECT_EQ(validate.semantic_signature_count, 1U);
    EXPECT_EQ(validate.semantic_signature_linked, 1U);
    EXPECT_EQ(validate.semantic_signature_orphan, 0U);
    EXPECT_EQ(validate.staged_segments, 0U);
    EXPECT_EQ(validate.errors, 1U);
}

TEST(MetadataTransferApi,
     ValidatePreparedC2paSignResultRejectsPrimarySignatureReferenceUnresolved)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical
        = make_semantic_c2pa_logical_payload_unresolved_primary_signature_reference();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Invalid);
    EXPECT_EQ(validate.semantic_reason,
              "primary_signature_reference_unresolved");
    EXPECT_EQ(validate.semantic_manifest_present, 1U);
    EXPECT_EQ(validate.semantic_manifest_count, 1U);
    EXPECT_EQ(validate.semantic_claim_generator_present, 1U);
    EXPECT_EQ(validate.semantic_assertion_count, 2U);
    EXPECT_EQ(validate.semantic_primary_claim_assertion_count, 1U);
    EXPECT_EQ(validate.semantic_primary_claim_referenced_by_signature_count,
              0U);
    EXPECT_EQ(validate.semantic_primary_signature_linked_claim_count, 0U);
    EXPECT_EQ(validate.semantic_primary_signature_reference_key_hits, 1U);
    EXPECT_EQ(validate.semantic_primary_signature_explicit_reference_present,
              1U);
    EXPECT_EQ(
        validate
            .semantic_primary_signature_explicit_reference_resolved_claim_count,
        0U);
    EXPECT_EQ(validate.semantic_claim_count, 2U);
    EXPECT_EQ(validate.semantic_signature_count, 1U);
    EXPECT_EQ(validate.semantic_signature_linked, 0U);
    EXPECT_EQ(validate.semantic_signature_orphan, 1U);
    EXPECT_EQ(validate.staged_segments, 0U);
    EXPECT_EQ(validate.errors, 1U);
}

TEST(MetadataTransferApi,
     ValidatePreparedC2paSignResultRejectsPrimarySignatureReferenceAmbiguous)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical
        = make_semantic_c2pa_logical_payload_ambiguous_primary_signature_reference();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Invalid);
    EXPECT_EQ(validate.semantic_reason,
              "primary_signature_reference_ambiguous");
    EXPECT_EQ(validate.semantic_manifest_present, 1U);
    EXPECT_EQ(validate.semantic_manifest_count, 1U);
    EXPECT_EQ(validate.semantic_claim_generator_present, 1U);
    EXPECT_EQ(validate.semantic_assertion_count, 2U);
    EXPECT_EQ(validate.semantic_primary_claim_assertion_count, 1U);
    EXPECT_EQ(validate.semantic_primary_claim_referenced_by_signature_count,
              1U);
    EXPECT_EQ(validate.semantic_primary_signature_linked_claim_count, 2U);
    EXPECT_EQ(validate.semantic_primary_signature_reference_key_hits, 1U);
    EXPECT_EQ(validate.semantic_primary_signature_explicit_reference_present,
              1U);
    EXPECT_EQ(
        validate
            .semantic_primary_signature_explicit_reference_resolved_claim_count,
        2U);
    EXPECT_EQ(validate.semantic_claim_count, 2U);
    EXPECT_EQ(validate.semantic_signature_count, 1U);
    EXPECT_EQ(validate.semantic_signature_linked, 1U);
    EXPECT_EQ(validate.semantic_signature_orphan, 0U);
    EXPECT_EQ(validate.staged_segments, 0U);
    EXPECT_EQ(validate.errors, 1U);
}

TEST(MetadataTransferApi,
     ValidatePreparedC2paSignResultRejectsPrimaryClaimMultiSignature)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical
        = make_semantic_c2pa_logical_payload_primary_claim_two_signatures();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Invalid);
    EXPECT_EQ(validate.semantic_reason, "primary_claim_signature_ambiguous");
    EXPECT_EQ(validate.semantic_manifest_present, 1U);
    EXPECT_EQ(validate.semantic_manifest_count, 1U);
    EXPECT_EQ(validate.semantic_claim_generator_present, 1U);
    EXPECT_EQ(validate.semantic_assertion_count, 1U);
    EXPECT_EQ(validate.semantic_primary_claim_assertion_count, 1U);
    EXPECT_EQ(validate.semantic_primary_claim_referenced_by_signature_count,
              2U);
    EXPECT_EQ(validate.semantic_primary_signature_linked_claim_count, 1U);
    EXPECT_EQ(validate.semantic_primary_signature_reference_key_hits, 0U);
    EXPECT_EQ(validate.semantic_primary_signature_explicit_reference_present,
              0U);
    EXPECT_EQ(
        validate
            .semantic_primary_signature_explicit_reference_resolved_claim_count,
        0U);
    EXPECT_EQ(validate.semantic_claim_count, 1U);
    EXPECT_EQ(validate.semantic_signature_count, 2U);
    EXPECT_EQ(validate.semantic_signature_linked, 2U);
    EXPECT_EQ(validate.semantic_signature_orphan, 0U);
    EXPECT_EQ(validate.staged_segments, 0U);
    EXPECT_EQ(validate.errors, 1U);
}

TEST(MetadataTransferApi,
     ValidatePreparedC2paSignResultRejectsExtraLinkedSignatureDrift)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical
        = make_semantic_c2pa_logical_payload_two_claims_two_signatures();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Invalid);
    EXPECT_EQ(validate.semantic_reason, "linked_signature_count_drift");
    EXPECT_EQ(validate.semantic_manifest_present, 1U);
    EXPECT_EQ(validate.semantic_manifest_count, 1U);
    EXPECT_EQ(validate.semantic_claim_generator_present, 1U);
    EXPECT_EQ(validate.semantic_assertion_count, 2U);
    EXPECT_EQ(validate.semantic_primary_claim_assertion_count, 1U);
    EXPECT_EQ(validate.semantic_primary_claim_referenced_by_signature_count,
              1U);
    EXPECT_EQ(validate.semantic_primary_signature_linked_claim_count, 1U);
    EXPECT_EQ(validate.semantic_primary_signature_reference_key_hits, 0U);
    EXPECT_EQ(validate.semantic_primary_signature_explicit_reference_present,
              0U);
    EXPECT_EQ(
        validate
            .semantic_primary_signature_explicit_reference_resolved_claim_count,
        0U);
    EXPECT_EQ(validate.semantic_claim_count, 2U);
    EXPECT_EQ(validate.semantic_signature_count, 2U);
    EXPECT_EQ(validate.semantic_signature_linked, 2U);
    EXPECT_EQ(validate.semantic_signature_orphan, 0U);
    EXPECT_EQ(validate.staged_segments, 0U);
    EXPECT_EQ(validate.errors, 1U);
}

TEST(MetadataTransferApi,
     ValidatePreparedC2paSignResultRejectsManifestBuilderOutputMismatch)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));
    ASSERT_FALSE(input.manifest_builder_output.empty());
    input.manifest_builder_output[0] ^= std::byte { 0x01 };

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(validate.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(validate.semantic_status,
              openmeta::TransferC2paSemanticStatus::Invalid);
    EXPECT_EQ(validate.semantic_reason, "manifest_builder_output_mismatch");
    EXPECT_EQ(validate.semantic_manifest_present, 1U);
    EXPECT_EQ(validate.semantic_manifest_count, 1U);
    EXPECT_EQ(validate.semantic_claim_generator_present, 1U);
    EXPECT_EQ(validate.semantic_assertion_count, 1U);
    EXPECT_EQ(validate.staged_segments, 0U);
    EXPECT_EQ(validate.errors, 1U);
}

TEST(MetadataTransferApi, ValidatePreparedC2paSignResultRejectsGenericJumbf)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);
    const std::vector<std::byte> logical = make_logical_jumbf_payload("acme");
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ValidatePreparedC2paSignResult validate
        = openmeta::validate_prepared_c2pa_sign_result(result.bundle,
                                                       sign_request, input);
    EXPECT_EQ(validate.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(validate.code,
              openmeta::EmitTransferCode::ContentBoundPayloadUnsupported);
    EXPECT_EQ(validate.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::GenericJumbf);
    EXPECT_EQ(validate.staged_segments, 0U);
    EXPECT_EQ(validate.errors, 1U);
}

TEST(MetadataTransferApi, ApplyPreparedC2paSignResultStagesSignedPayload)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));
    const openmeta::EmitTransferResult apply
        = openmeta::apply_prepared_c2pa_sign_result(&result.bundle,
                                                    sign_request, input);

    EXPECT_EQ(apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(apply.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(apply.errors, 0U);
    EXPECT_EQ(count_blocks_with_route(result.bundle, "jpeg:app11-c2pa"),
              apply.emitted);
    EXPECT_EQ(result.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::Ready);

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Rewrite);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::ExternalSignedPayload);
    EXPECT_EQ(decision->c2pa_mode, openmeta::TransferC2paMode::SignedRewrite);
    EXPECT_EQ(decision->c2pa_prepared_output,
              openmeta::TransferC2paPreparedOutput::SignedRewrite);

    openmeta::PreparedTransferC2paSignRequest ready_request;
    EXPECT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &ready_request),
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(ready_request.rewrite_state,
              openmeta::TransferC2paRewriteState::Ready);
}

TEST(MetadataTransferApi, ApplyPreparedC2paSignResultStagesSignedPayloadBmff)
{
    const std::vector<std::byte> bmff = make_test_bmff_with_c2pa(false);
    ASSERT_FALSE(bmff.empty());
    const std::string path = unique_temp_path(".heic");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(bmff.data(), bmff.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format      = openmeta::TransferTargetFormat::Heif;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));
    const openmeta::EmitTransferResult apply
        = openmeta::apply_prepared_c2pa_sign_result(&result.bundle,
                                                    sign_request, input);

    EXPECT_EQ(apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(apply.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(apply.errors, 0U);
    EXPECT_EQ(count_blocks_with_route(result.bundle, "bmff:item-c2pa"),
              apply.emitted);
    EXPECT_EQ(result.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::Ready);

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Rewrite);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::ExternalSignedPayload);
    EXPECT_EQ(decision->c2pa_mode, openmeta::TransferC2paMode::SignedRewrite);
    EXPECT_EQ(decision->c2pa_prepared_output,
              openmeta::TransferC2paPreparedOutput::SignedRewrite);
}

TEST(MetadataTransferApi, ApplyPreparedC2paSignResultStagesSignedPayloadJxl)
{
    const std::vector<std::byte> jxl = make_test_jxl_with_c2pa(false);
    const std::string path           = unique_temp_path(".jxl");
    ASSERT_TRUE(write_bytes_file(path, std::span<const std::byte>(jxl.data(),
                                                                  jxl.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format      = openmeta::TransferTargetFormat::Jxl;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));
    const openmeta::EmitTransferResult apply
        = openmeta::apply_prepared_c2pa_sign_result(&result.bundle,
                                                    sign_request, input);

    EXPECT_EQ(apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(apply.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(apply.errors, 0U);
    EXPECT_EQ(count_blocks_with_route(result.bundle, "jxl:box-jumb"),
              apply.emitted);
    EXPECT_EQ(count_blocks_with_route(result.bundle, "jxl:box-c2pa"), 0U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::Ready);

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.bundle,
                               openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Rewrite);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::ExternalSignedPayload);
    EXPECT_EQ(decision->c2pa_mode, openmeta::TransferC2paMode::SignedRewrite);
    EXPECT_EQ(decision->c2pa_prepared_output,
              openmeta::TransferC2paPreparedOutput::SignedRewrite);
}

TEST(MetadataTransferApi, ApplyPreparedC2paSignResultRejectsMissingMaterial)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));
    input.manifest_builder_output.clear();

    const openmeta::EmitTransferResult apply
        = openmeta::apply_prepared_c2pa_sign_result(&result.bundle,
                                                    sign_request, input);

    EXPECT_EQ(apply.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(apply.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(count_blocks_with_route(result.bundle, "jpeg:app11-c2pa"), 0U);
    EXPECT_EQ(result.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::SigningMaterialRequired);
}

TEST(MetadataTransferApi, ApplyPreparedC2paSignResultRejectsStaleRequest)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);

    result.bundle.c2pa_rewrite.content_binding_bytes += 1U;

    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));
    const openmeta::EmitTransferResult apply
        = openmeta::apply_prepared_c2pa_sign_result(&result.bundle,
                                                    sign_request, input);

    EXPECT_EQ(apply.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(apply.code, openmeta::EmitTransferCode::PlanMismatch);
    EXPECT_EQ(count_blocks_with_route(result.bundle, "jpeg:app11-c2pa"), 0U);
}

TEST(MetadataTransferApi, ApplyPreparedC2paSignResultRejectsGenericJumbf)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Rewrite;

    openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Unsupported);
    openmeta::PreparedTransferC2paSignRequest sign_request;
    ASSERT_EQ(openmeta::build_prepared_c2pa_sign_request(result.bundle,
                                                         &sign_request),
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> logical = make_logical_jumbf_payload("acme");
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));
    const openmeta::EmitTransferResult apply
        = openmeta::apply_prepared_c2pa_sign_result(&result.bundle,
                                                    sign_request, input);

    EXPECT_EQ(apply.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(apply.code,
              openmeta::EmitTransferCode::ContentBoundPayloadUnsupported);
    EXPECT_EQ(count_blocks_with_route(result.bundle, "jpeg:app11-c2pa"), 0U);
}

TEST(MetadataTransferApi, AppendPreparedBundleJpegJumbfAddsPreparedBlocks)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;
    bundle.profile.jumbf = openmeta::TransferPolicyAction::Drop;

    const std::vector<std::byte> logical = make_logical_jumbf_payload("acme");
    const openmeta::EmitTransferResult result
        = openmeta::append_prepared_bundle_jpeg_jumbf(
            &bundle,
            std::span<const std::byte>(logical.data(), logical.size()));

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(result.emitted, 1U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::Jumbf);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app11-jumbf");
    EXPECT_EQ(bundle.profile.jumbf, openmeta::TransferPolicyAction::Keep);

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(bundle, openmeta::TransferPolicySubject::Jumbf);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason, openmeta::TransferPolicyReason::Default);
    EXPECT_EQ(decision->matched_entries, 1U);
}

TEST(MetadataTransferApi, AppendPreparedBundleJpegJumbfCanReplaceExisting)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock stale;
    stale.kind    = openmeta::TransferBlockKind::Jumbf;
    stale.order   = 140U;
    stale.route   = "jpeg:app11-jumbf";
    stale.payload = make_app11_jumbf_payload("old");
    bundle.blocks.push_back(std::move(stale));

    const std::vector<std::byte> logical = make_logical_jumbf_payload("new");
    openmeta::AppendPreparedJpegJumbfOptions options;
    options.replace_existing = true;
    const openmeta::EmitTransferResult result
        = openmeta::append_prepared_bundle_jpeg_jumbf(
            &bundle, std::span<const std::byte>(logical.data(), logical.size()),
            options);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(result.skipped, 1U);
    EXPECT_EQ(count_blocks_with_route(bundle, "jpeg:app11-jumbf"), 1U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].payload, make_app11_jumbf_payload("new"));
}

TEST(MetadataTransferApi, AppendPreparedBundleJpegJumbfRejectsC2paPayload)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    const openmeta::EmitTransferResult result
        = openmeta::append_prepared_bundle_jpeg_jumbf(
            &bundle,
            std::span<const std::byte>(logical.data(), logical.size()));

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code,
              openmeta::EmitTransferCode::ContentBoundPayloadUnsupported);
    EXPECT_TRUE(bundle.blocks.empty());
}

TEST(MetadataTransferApi, PrepareBuildsJpegXmpApp1Block)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key                   = openmeta::make_xmp_property_key(store.arena(),
                                                              "http://purl.org/dc/elements/1.1/",
                                                              "title");
    e.value                 = openmeta::make_text(store.arena(), "OpenMeta",
                                                  openmeta::TextEncoding::Utf8);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(result.warnings, 0U);
    ASSERT_EQ(bundle.target_format, openmeta::TransferTargetFormat::Jpeg);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    ASSERT_GE(bundle.blocks[0].payload.size(), 30U);
    EXPECT_EQ(bundle.blocks[0].payload[0], std::byte { 'h' });
}

TEST(MetadataTransferApi, PrepareCanDisableExifProjectionIntoGeneratedXmp)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x010FU);
    exif.value = openmeta::make_text(store.arena(), "Canon",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(store.arena(), "OpenMeta Transfer Source",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;
    request.xmp_project_exif     = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "xmp:CreatorTool"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "tiff:Make"));

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(
            bundle, openmeta::TransferPolicySubject::XmpExifProjection);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(decision->reason, openmeta::TransferPolicyReason::ExplicitDrop);
    EXPECT_EQ(decision->matched_entries, 1U);
}

TEST(MetadataTransferApi, PreparePortableXmpCanPreferExistingOverExifProjection)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x010FU);
    exif.value = openmeta::make_text(store.arena(), "Canon",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_make;
    xmp_make.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/tiff/1.0/", "Make");
    xmp_make.value = openmeta::make_text(store.arena(), "Nikon",
                                         openmeta::TextEncoding::Utf8);
    xmp_make.origin.block          = block;
    xmp_make.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(xmp_make), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;
    request.xmp_conflict_policy  = openmeta::XmpConflictPolicy::ExistingWins;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<tiff:Make>Nikon</tiff:Make>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<tiff:Make>Canon</tiff:Make>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizesManagedNamespacesOnlyWhenReplacementExists)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif_make;
    exif_make.key = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x010FU);
    exif_make.value = openmeta::make_text(store.arena(), "Canon",
                                          openmeta::TextEncoding::Ascii);
    exif_make.origin.block          = block;
    exif_make.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(exif_make), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_make;
    xmp_make.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/tiff/1.0/", "Make");
    xmp_make.value = openmeta::make_text(store.arena(), "Nikon",
                                         openmeta::TextEncoding::Utf8);
    xmp_make.origin.block          = block;
    xmp_make.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(xmp_make), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_model;
    xmp_model.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/tiff/1.0/", "Model");
    xmp_model.value = openmeta::make_text(store.arena(), "EOS R6",
                                          openmeta::TextEncoding::Utf8);
    xmp_model.origin.block          = block;
    xmp_model.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(xmp_model), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_description;
    xmp_description.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "description");
    xmp_description.value = openmeta::make_text(
        store.arena(), "From XMP", openmeta::TextEncoding::Utf8);
    xmp_description.origin.block          = block;
    xmp_description.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(xmp_description), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_subject;
    xmp_subject.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "subject[1]");
    xmp_subject.value = openmeta::make_text(store.arena(), "xmp-keyword",
                                            openmeta::TextEncoding::Utf8);
    xmp_subject.origin.block          = block;
    xmp_subject.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(xmp_subject), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_creator_tool;
    xmp_creator_tool.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp_creator_tool.value = openmeta::make_text(
        store.arena(), "Tool", openmeta::TextEncoding::Utf8);
    xmp_creator_tool.origin.block          = block;
    xmp_creator_tool.origin.order_in_block = 5U;
    ASSERT_NE(store.add_entry(xmp_creator_tool), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;
    request.xmp_conflict_policy  = openmeta::XmpConflictPolicy::ExistingWins;
    request.xmp_existing_standard_namespace_policy
        = openmeta::XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<tiff:Make>Canon</tiff:Make>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<tiff:Make>Nikon</tiff:Make>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<tiff:Model>EOS R6</tiff:Model>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<dc:description>From XMP</dc:description>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>xmp-keyword</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmp:CreatorTool>Tool</xmp:CreatorTool>"));
}

TEST(MetadataTransferApi, PreparePortableXmpGeneratesXmpDateAliasesFromExif)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry dt;
    dt.key = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    dt.value = openmeta::make_text(store.arena(), "2010:11:14 16:25:16",
                                   openmeta::TextEncoding::Ascii);
    dt.origin.block          = block;
    dt.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(dt), openmeta::kInvalidEntryId);

    openmeta::Entry dtd;
    dtd.key = openmeta::make_exif_tag_key(store.arena(), "exififd", 0x9004U);
    dtd.value = openmeta::make_text(store.arena(), "2010:11:14 16:25:16",
                                    openmeta::TextEncoding::Ascii);
    dtd.origin.block          = block;
    dtd.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(dtd), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<tiff:DateTime>2010-11-14T16:25:16</tiff:DateTime>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmp:ModifyDate>2010-11-14T16:25:16</xmp:ModifyDate>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<exif:DateTimeDigitized>2010-11-14T16:25:16</exif:DateTimeDigitized>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmp:CreateDate>2010-11-14T16:25:16</xmp:CreateDate>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizesManagedXmpDateAliasesOnlyWhenAvailable)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry dt;
    dt.key = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    dt.value = openmeta::make_text(store.arena(), "2010:11:14 16:25:16",
                                   openmeta::TextEncoding::Ascii);
    dt.origin.block          = block;
    dt.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(dt), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_modify_date;
    xmp_modify_date.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "ModifyDate");
    xmp_modify_date.value = openmeta::make_text(
        store.arena(), "1999-01-02T03:04:05", openmeta::TextEncoding::Utf8);
    xmp_modify_date.origin.block          = block;
    xmp_modify_date.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(xmp_modify_date), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_create_date;
    xmp_create_date.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreateDate");
    xmp_create_date.value = openmeta::make_text(
        store.arena(), "1980-01-02T03:04:05", openmeta::TextEncoding::Utf8);
    xmp_create_date.origin.block          = block;
    xmp_create_date.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(xmp_create_date), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;
    request.xmp_conflict_policy  = openmeta::XmpConflictPolicy::ExistingWins;
    request.xmp_existing_standard_namespace_policy
        = openmeta::XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmp:ModifyDate>2010-11-14T16:25:16</xmp:ModifyDate>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmp:ModifyDate>1999-01-02T03:04:05</xmp:ModifyDate>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmp:CreateDate>1980-01-02T03:04:05</xmp:CreateDate>"));
}

TEST(MetadataTransferApi, PreparePortableXmpCanPreferGeneratedOverExistingIptc)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    const std::string caption = "From IPTC";
    openmeta::Entry iptc_caption;
    iptc_caption.key = openmeta::make_iptc_dataset_key(2U, 120U);
    iptc_caption.value
        = openmeta::make_bytes(store.arena(),
                               std::span<const std::byte>(
                                   reinterpret_cast<const std::byte*>(
                                       caption.data()),
                                   caption.size()));
    iptc_caption.origin.block          = block;
    iptc_caption.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(iptc_caption), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_description;
    xmp_description.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "description");
    xmp_description.value = openmeta::make_text(store.arena(), "From XMP",
                                                openmeta::TextEncoding::Utf8);
    xmp_description.origin.block          = block;
    xmp_description.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(xmp_description), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;
    request.xmp_conflict_policy  = openmeta::XmpConflictPolicy::GeneratedWins;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<dc:description>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">From IPTC</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<dc:description>From XMP</dc:description>"));
}

TEST(MetadataTransferApi, PreparePortableXmpCanPreserveCustomExistingNamespaces)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry xmp_flag;
    xmp_flag.key = openmeta::make_xmp_property_key(
        store.arena(), "urn:vendor:test:1.0/", "Flag");
    xmp_flag.value = openmeta::make_text(store.arena(), "Alpha",
                                         openmeta::TextEncoding::Utf8);
    xmp_flag.origin.block          = block;
    xmp_flag.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(xmp_flag), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1              = false;
    request.include_icc_app2               = false;
    request.include_iptc_app13             = false;
    request.xmp_portable                   = true;
    request.xmp_include_existing           = true;
    request.xmp_existing_namespace_policy
        = openmeta::XmpExistingNamespacePolicy::PreserveCustom;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "xmlns:omns1=\"urn:vendor:test:1.0/\""));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<omns1:Flag>Alpha</omns1:Flag>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPreservesXmpRightsStandardNamespace)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    const std::string rights = "Generated copyright";
    openmeta::Entry iptc_rights;
    iptc_rights.key = openmeta::make_iptc_dataset_key(2U, 116U);
    iptc_rights.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(rights.data()), rights.size()));
    iptc_rights.origin.block          = block;
    iptc_rights.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(iptc_rights), openmeta::kInvalidEntryId);

    openmeta::Entry usage_terms;
    usage_terms.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/",
        "UsageTerms[@xml:lang=x-default]");
    usage_terms.value = openmeta::make_text(
        store.arena(), "Licensed use only", openmeta::TextEncoding::Utf8);
    usage_terms.origin.block          = block;
    usage_terms.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(usage_terms), openmeta::kInvalidEntryId);

    openmeta::Entry web_statement;
    web_statement.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/",
        "WebStatement");
    web_statement.value = openmeta::make_text(
        store.arena(), "https://example.test/license",
        openmeta::TextEncoding::Utf8);
    web_statement.origin.block          = block;
    web_statement.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(web_statement), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;
    request.xmp_conflict_policy  = openmeta::XmpConflictPolicy::GeneratedWins;
    request.xmp_existing_standard_namespace_policy
        = openmeta::XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "xmlns:xmpRights=\"http://ns.adobe.com/xap/1.0/rights/\""));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Generated copyright</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Licensed use only</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmpRights:WebStatement>https://example.test/license</xmpRights:WebStatement>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesXmpMmStandardNamespace)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry document_id;
    document_id.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/", "DocumentID");
    document_id.value = openmeta::make_text(store.arena(), "xmp.did:1234",
                                            openmeta::TextEncoding::Utf8);
    document_id.origin.block          = block;
    document_id.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(document_id), openmeta::kInvalidEntryId);

    openmeta::Entry instance_id;
    instance_id.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/mm/", "InstanceID");
    instance_id.value = openmeta::make_text(store.arena(), "xmp.iid:5678",
                                            openmeta::TextEncoding::Utf8);
    instance_id.origin.block          = block;
    instance_id.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(instance_id), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "xmlns:xmpMM=\"http://ns.adobe.com/xap/1.0/mm/\""));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmpMM:DocumentID>xmp.did:1234</xmpMM:DocumentID>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmpMM:InstanceID>xmp.iid:5678</xmpMM:InstanceID>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesAuxStandardNamespace)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry lens;
    lens.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/exif/1.0/aux/", "Lens");
    lens.value = openmeta::make_text(store.arena(),
                                     "RF24-70mm F2.8 L IS USM",
                                     openmeta::TextEncoding::Utf8);
    lens.origin.block          = block;
    lens.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(lens), openmeta::kInvalidEntryId);

    openmeta::Entry serial_number;
    serial_number.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/exif/1.0/aux/", "SerialNumber");
    serial_number.value = openmeta::make_text(store.arena(), "1234567890",
                                              openmeta::TextEncoding::Utf8);
    serial_number.origin.block          = block;
    serial_number.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(serial_number), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "xmlns:aux=\"http://ns.adobe.com/exif/1.0/aux/\""));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<aux:Lens>RF24-70mm F2.8 L IS USM</aux:Lens>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<aux:SerialNumber>1234567890</aux:SerialNumber>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesCrsNamespace)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry process_version;
    process_version.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/camera-raw-settings/1.0/",
        "ProcessVersion");
    process_version.value = openmeta::make_text(store.arena(), "16.0",
                                                openmeta::TextEncoding::Utf8);
    process_version.origin.block          = block;
    process_version.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(process_version), openmeta::kInvalidEntryId);

    openmeta::Entry exposure;
    exposure.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/camera-raw-settings/1.0/",
        "Exposure2012");
    exposure.value = openmeta::make_text(store.arena(), "+0.35",
                                         openmeta::TextEncoding::Utf8);
    exposure.origin.block          = block;
    exposure.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(exposure), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "xmlns:crs=\"http://ns.adobe.com/camera-raw-settings/1.0/\""));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<crs:ProcessVersion>16.0</crs:ProcessVersion>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<crs:Exposure2012>+0.35</crs:Exposure2012>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesLrNamespace)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry private_rtk_info;
    private_rtk_info.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/lightroom/1.0/",
        "PrivateRTKInfo");
    private_rtk_info.value = openmeta::make_text(
        store.arena(), "face-region-cache", openmeta::TextEncoding::Utf8);
    private_rtk_info.origin.block          = block;
    private_rtk_info.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(private_rtk_info), openmeta::kInvalidEntryId);

    openmeta::Entry private_rtk_flag;
    private_rtk_flag.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/lightroom/1.0/",
        "PrivateRTKFlag");
    private_rtk_flag.value = openmeta::make_text(store.arena(), "true",
                                                 openmeta::TextEncoding::Utf8);
    private_rtk_flag.origin.block          = block;
    private_rtk_flag.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(private_rtk_flag), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "xmlns:lr=\"http://ns.adobe.com/lightroom/1.0/\""));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<lr:PrivateRTKInfo>face-region-cache</lr:PrivateRTKInfo>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<lr:PrivateRTKFlag>true</lr:PrivateRTKFlag>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesLrHierarchicalSubjectAsBag)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry item1;
    item1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/lightroom/1.0/",
        "hierarchicalSubject[1]");
    item1.value = openmeta::make_text(store.arena(), "Places|Japan|Tokyo",
                                      openmeta::TextEncoding::Utf8);
    item1.origin.block          = block;
    item1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(item1), openmeta::kInvalidEntryId);

    openmeta::Entry item2;
    item2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/lightroom/1.0/",
        "hierarchicalSubject[2]");
    item2.value = openmeta::make_text(store.arena(), "Travel|Spring",
                                      openmeta::TextEncoding::Utf8);
    item2.origin.block          = block;
    item2.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(item2), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<lr:hierarchicalSubject>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Bag>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Places|Japan|Tokyo</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Travel|Spring</rdf:li>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesPdfNamespace)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry keywords;
    keywords.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/pdf/1.3/", "Keywords");
    keywords.value = openmeta::make_text(store.arena(), "tokyo,night,street",
                                         openmeta::TextEncoding::Utf8);
    keywords.origin.block          = block;
    keywords.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(keywords), openmeta::kInvalidEntryId);

    openmeta::Entry producer;
    producer.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/pdf/1.3/", "Producer");
    producer.value = openmeta::make_text(store.arena(), "OpenMetaTest",
                                         openmeta::TextEncoding::Utf8);
    producer.origin.block          = block;
    producer.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(producer), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "xmlns:pdf=\"http://ns.adobe.com/pdf/1.3/\""));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<pdf:Keywords>tokyo,night,street</pdf:Keywords>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<pdf:Producer>OpenMetaTest</pdf:Producer>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesXmpIdentifierAsBag)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry item1;
    item1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "Identifier[1]");
    item1.value = openmeta::make_text(store.arena(), "urn:om:test:1",
                                      openmeta::TextEncoding::Utf8);
    item1.origin.block          = block;
    item1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(item1), openmeta::kInvalidEntryId);

    openmeta::Entry item2;
    item2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "Identifier[2]");
    item2.value = openmeta::make_text(store.arena(), "urn:om:test:2",
                                      openmeta::TextEncoding::Utf8);
    item2.origin.block          = block;
    item2.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(item2), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmp:Identifier>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Bag>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>urn:om:test:1</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>urn:om:test:2</rdf:li>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesDcLanguageAsBag)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry item1;
    item1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "language[1]");
    item1.value = openmeta::make_text(store.arena(), "en",
                                      openmeta::TextEncoding::Utf8);
    item1.origin.block          = block;
    item1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(item1), openmeta::kInvalidEntryId);

    openmeta::Entry item2;
    item2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "language[2]");
    item2.value = openmeta::make_text(store.arena(), "ja",
                                      openmeta::TextEncoding::Utf8);
    item2.origin.block          = block;
    item2.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(item2), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<dc:language>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Bag>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>en</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>ja</rdf:li>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesDcDateAsSeq)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry item1;
    item1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "date[1]");
    item1.value = openmeta::make_text(store.arena(), "2026-04-01T10:00:00",
                                      openmeta::TextEncoding::Utf8);
    item1.origin.block          = block;
    item1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(item1), openmeta::kInvalidEntryId);

    openmeta::Entry item2;
    item2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "date[2]");
    item2.value = openmeta::make_text(store.arena(), "2026-04-02T11:30:00",
                                      openmeta::TextEncoding::Utf8);
    item2.origin.block          = block;
    item2.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(item2), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<dc:date>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Seq>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>2026-04-01T10:00:00</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>2026-04-02T11:30:00</rdf:li>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesXmpAdvisoryAsBag)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry item1;
    item1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "Advisory[1]");
    item1.value = openmeta::make_text(store.arena(), "xmp:MetadataDate",
                                      openmeta::TextEncoding::Utf8);
    item1.origin.block          = block;
    item1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(item1), openmeta::kInvalidEntryId);

    openmeta::Entry item2;
    item2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "Advisory[2]");
    item2.value = openmeta::make_text(store.arena(), "photoshop:City",
                                      openmeta::TextEncoding::Utf8);
    item2.origin.block          = block;
    item2.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(item2), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmp:Advisory>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Bag>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>xmp:MetadataDate</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>photoshop:City</rdf:li>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesXmpRightsOwnerAsBag)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry item1;
    item1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/", "Owner[1]");
    item1.value = openmeta::make_text(store.arena(), "OpenMeta Labs",
                                      openmeta::TextEncoding::Utf8);
    item1.origin.block          = block;
    item1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(item1), openmeta::kInvalidEntryId);

    openmeta::Entry item2;
    item2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/rights/", "Owner[2]");
    item2.value = openmeta::make_text(store.arena(), "Example Archive",
                                      openmeta::TextEncoding::Utf8);
    item2.origin.block          = block;
    item2.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(item2), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmpRights:Owner>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Bag>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>OpenMeta Labs</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Example Archive</rdf:li>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesStructuredIptc4xmpCore)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry email;
    email.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiEmailWork");
    email.value = openmeta::make_text(store.arena(), "editor@example.test",
                                      openmeta::TextEncoding::Utf8);
    email.origin.block          = block;
    email.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(email), openmeta::kInvalidEntryId);

    openmeta::Entry url;
    url.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiUrlWork");
    url.value = openmeta::make_text(store.arena(),
                                    "https://example.test/contact",
                                    openmeta::TextEncoding::Utf8);
    url.origin.block          = block;
    url.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(url), openmeta::kInvalidEntryId);

    openmeta::Entry city;
    city.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "LocationCreated/City");
    city.value = openmeta::make_text(store.arena(), "Paris",
                                     openmeta::TextEncoding::Utf8);
    city.origin.block          = block;
    city.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(city), openmeta::kInvalidEntryId);

    openmeta::Entry country;
    country.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "LocationCreated/CountryName");
    country.value = openmeta::make_text(store.arena(), "France",
                                        openmeta::TextEncoding::Utf8);
    country.origin.block          = block;
    country.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(country), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CiUrlWork>https://example.test/contact</Iptc4xmpCore:CiUrlWork>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:LocationCreated rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:City>Paris</Iptc4xmpCore:City>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CountryName>France</Iptc4xmpCore:CountryName>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPreservesPlusNamespaceAndIndexedStructuredResources)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry name1;
    name1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "Licensee[1]/LicenseeName");
    name1.value = openmeta::make_text(store.arena(), "Example Archive",
                                      openmeta::TextEncoding::Utf8);
    name1.origin.block          = block;
    name1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(name1), openmeta::kInvalidEntryId);

    openmeta::Entry url1;
    url1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "Licensee[1]/LicenseeURL");
    url1.value = openmeta::make_text(store.arena(),
                                     "https://example.test/archive",
                                     openmeta::TextEncoding::Utf8);
    url1.origin.block          = block;
    url1.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(url1), openmeta::kInvalidEntryId);

    openmeta::Entry name2;
    name2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "Licensee[2]/LicenseeName");
    name2.value = openmeta::make_text(store.arena(), "Editorial Partner",
                                      openmeta::TextEncoding::Utf8);
    name2.origin.block          = block;
    name2.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(name2), openmeta::kInvalidEntryId);

    openmeta::Entry id2;
    id2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "Licensee[2]/LicenseeID");
    id2.value = openmeta::make_text(store.arena(), "lic-002",
                                    openmeta::TextEncoding::Utf8);
    id2.origin.block          = block;
    id2.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(id2), openmeta::kInvalidEntryId);

    openmeta::Entry constraints1;
    constraints1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "ImageAlterationConstraints[1]");
    constraints1.value = openmeta::make_text(store.arena(), "No compositing",
                                             openmeta::TextEncoding::Utf8);
    constraints1.origin.block          = block;
    constraints1.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(constraints1), openmeta::kInvalidEntryId);

    openmeta::Entry constraints2;
    constraints2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "ImageAlterationConstraints[2]");
    constraints2.value = openmeta::make_text(store.arena(), "No AI upscaling",
                                             openmeta::TextEncoding::Utf8);
    constraints2.origin.block          = block;
    constraints2.origin.order_in_block = 5U;
    ASSERT_NE(store.add_entry(constraints2), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "xmlns:plus=\"http://ns.useplus.org/ldf/xmp/1.0/\""));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<plus:Licensee>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<plus:LicenseeName>Example Archive</plus:LicenseeName>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<plus:LicenseeURL>https://example.test/archive</plus:LicenseeURL>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<plus:LicenseeName>Editorial Partner</plus:LicenseeName>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<plus:LicenseeID>lic-002</plus:LicenseeID>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<plus:ImageAlterationConstraints>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Bag>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>No compositing</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>No AI upscaling</rdf:li>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPreservesIptc4xmpExtIndexedStructuredResources)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry city1;
    city1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/City");
    city1.value = openmeta::make_text(store.arena(), "Paris",
                                      openmeta::TextEncoding::Utf8);
    city1.origin.block          = block;
    city1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(city1), openmeta::kInvalidEntryId);

    openmeta::Entry country1;
    country1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/CountryName");
    country1.value = openmeta::make_text(store.arena(), "France",
                                         openmeta::TextEncoding::Utf8);
    country1.origin.block          = block;
    country1.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(country1), openmeta::kInvalidEntryId);

    openmeta::Entry city2;
    city2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[2]/City");
    city2.value = openmeta::make_text(store.arena(), "Kyoto",
                                      openmeta::TextEncoding::Utf8);
    city2.origin.block          = block;
    city2.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(city2), openmeta::kInvalidEntryId);

    openmeta::Entry country2;
    country2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[2]/CountryName");
    country2.value = openmeta::make_text(store.arena(), "Japan",
                                         openmeta::TextEncoding::Utf8);
    country2.origin.block          = block;
    country2.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(country2), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "xmlns:Iptc4xmpExt=\"http://iptc.org/std/Iptc4xmpExt/2008-02-29/\""));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationShown>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Seq>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:City>Paris</Iptc4xmpExt:City>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:CountryName>France</Iptc4xmpExt:CountryName>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"));
}

TEST(MetadataTransferApi, PreparePortableXmpCanonicalizesKnownStructuredBaseShapes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry bad_contact_scalar;
    bad_contact_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo");
    bad_contact_scalar.value = openmeta::make_text(
        store.arena(), "legacy-flat-contact", openmeta::TextEncoding::Utf8);
    bad_contact_scalar.origin.block          = block;
    bad_contact_scalar.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(bad_contact_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_contact_email;
    good_contact_email.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiEmailWork");
    good_contact_email.value = openmeta::make_text(
        store.arena(), "editor@example.test", openmeta::TextEncoding::Utf8);
    good_contact_email.origin.block          = block;
    good_contact_email.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(good_contact_email), openmeta::kInvalidEntryId);

    openmeta::Entry bad_location_structured;
    bad_location_structured.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown/City");
    bad_location_structured.value = openmeta::make_text(
        store.arena(), "LegacyParis", openmeta::TextEncoding::Utf8);
    bad_location_structured.origin.block          = block;
    bad_location_structured.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(bad_location_structured),
              openmeta::kInvalidEntryId);

    openmeta::Entry good_location_city;
    good_location_city.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/City");
    good_location_city.value = openmeta::make_text(
        store.arena(), "Paris", openmeta::TextEncoding::Utf8);
    good_location_city.origin.block          = block;
    good_location_city.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(good_location_city), openmeta::kInvalidEntryId);

    openmeta::Entry bad_licensee_scalar;
    bad_licensee_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "Licensee");
    bad_licensee_scalar.value = openmeta::make_text(
        store.arena(), "legacy-licensee", openmeta::TextEncoding::Utf8);
    bad_licensee_scalar.origin.block          = block;
    bad_licensee_scalar.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(bad_licensee_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_licensee_name;
    good_licensee_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.useplus.org/ldf/xmp/1.0/",
        "Licensee[1]/LicenseeName");
    good_licensee_name.value = openmeta::make_text(
        store.arena(), "Example Archive", openmeta::TextEncoding::Utf8);
    good_licensee_name.origin.block          = block;
    good_licensee_name.origin.order_in_block = 5U;
    ASSERT_NE(store.add_entry(good_licensee_name), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationShown>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:City>Paris</Iptc4xmpExt:City>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<plus:Licensee>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<plus:LicenseeName>Example Archive</plus:LicenseeName>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-flat-contact"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "LegacyParis"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-licensee"));
}

TEST(MetadataTransferApi, PreparePortableXmpCanonicalizesKnownStructuredChildShapes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry bad_region_scalar;
    bad_region_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion");
    bad_region_scalar.value = openmeta::make_text(
        store.arena(), "legacy-region", openmeta::TextEncoding::Utf8);
    bad_region_scalar.origin.block          = block;
    bad_region_scalar.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(bad_region_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_region_nested;
    good_region_nested.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceName");
    good_region_nested.value = openmeta::make_text(
        store.arena(), "Tokyo", openmeta::TextEncoding::Utf8);
    good_region_nested.origin.block          = block;
    good_region_nested.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(good_region_nested), openmeta::kInvalidEntryId);

    openmeta::Entry bad_address_scalar;
    bad_address_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address");
    bad_address_scalar.value = openmeta::make_text(
        store.arena(), "legacy-address", openmeta::TextEncoding::Utf8);
    bad_address_scalar.origin.block          = block;
    bad_address_scalar.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(bad_address_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_address_nested;
    good_address_nested.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/City");
    good_address_nested.value = openmeta::make_text(
        store.arena(), "Kyoto", openmeta::TextEncoding::Utf8);
    good_address_nested.origin.block          = block;
    good_address_nested.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(good_address_nested), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CiAdrRegion rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Tokyo</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:Address rdf:parseType=\"Resource\">"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-region"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-address"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizesKnownIndexedStructuredChildShapes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry bad_extadr_scalar;
    bad_extadr_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrExtadr");
    bad_extadr_scalar.value = openmeta::make_text(
        store.arena(), "legacy-line", openmeta::TextEncoding::Utf8);
    bad_extadr_scalar.origin.block          = block;
    bad_extadr_scalar.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(bad_extadr_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_extadr1;
    good_extadr1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrExtadr[1]");
    good_extadr1.value = openmeta::make_text(store.arena(), "Line 1",
                                             openmeta::TextEncoding::Utf8);
    good_extadr1.origin.block          = block;
    good_extadr1.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(good_extadr1), openmeta::kInvalidEntryId);

    openmeta::Entry good_extadr2;
    good_extadr2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrExtadr[2]");
    good_extadr2.value = openmeta::make_text(store.arena(), "Line 2",
                                             openmeta::TextEncoding::Utf8);
    good_extadr2.origin.block          = block;
    good_extadr2.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(good_extadr2), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CiAdrExtadr>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Line 1</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Line 2</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-line"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesStructuredChildLangAlt)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry email;
    email.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiEmailWork");
    email.value = openmeta::make_text(store.arena(), "editor@example.test",
                                      openmeta::TextEncoding::Utf8);
    email.origin.block          = block;
    email.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(email), openmeta::kInvalidEntryId);

    openmeta::Entry city_default;
    city_default.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrCity[@xml:lang=x-default]");
    city_default.value = openmeta::make_text(store.arena(), "Tokyo",
                                             openmeta::TextEncoding::Utf8);
    city_default.origin.block          = block;
    city_default.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(city_default), openmeta::kInvalidEntryId);

    openmeta::Entry city_ja;
    city_ja.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrCity[@xml:lang=ja-JP]");
    city_ja.value = openmeta::make_text(store.arena(), "東京",
                                        openmeta::TextEncoding::Utf8);
    city_ja.origin.block          = block;
    city_ja.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(city_ja), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CiEmailWork>editor@example.test</Iptc4xmpCore:CiEmailWork>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Tokyo</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"ja-JP\">東京</rdf:li>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPreservesIndexedStructuredChildLangAlt)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry country;
    country.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/CountryName");
    country.value = openmeta::make_text(store.arena(), "Japan",
                                        openmeta::TextEncoding::Utf8);
    country.origin.block          = block;
    country.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(country), openmeta::kInvalidEntryId);

    openmeta::Entry sublocation_default;
    sublocation_default.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Sublocation[@xml:lang=x-default]");
    sublocation_default.value = openmeta::make_text(
        store.arena(), "Gion", openmeta::TextEncoding::Utf8);
    sublocation_default.origin.block          = block;
    sublocation_default.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(sublocation_default),
              openmeta::kInvalidEntryId);

    openmeta::Entry sublocation_ja;
    sublocation_ja.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Sublocation[@xml:lang=ja-JP]");
    sublocation_ja.value = openmeta::make_text(store.arena(), "祇園",
                                               openmeta::TextEncoding::Utf8);
    sublocation_ja.origin.block          = block;
    sublocation_ja.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(sublocation_ja), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationShown>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Gion</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"ja-JP\">祇園</rdf:li>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesStructuredChildIndexed)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry line1;
    line1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrExtadr[1]");
    line1.value = openmeta::make_text(store.arena(), "Line 1",
                                      openmeta::TextEncoding::Utf8);
    line1.origin.block          = block;
    line1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(line1), openmeta::kInvalidEntryId);

    openmeta::Entry line2;
    line2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrExtadr[2]");
    line2.value = openmeta::make_text(store.arena(), "Line 2",
                                      openmeta::TextEncoding::Utf8);
    line2.origin.block          = block;
    line2.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(line2), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CiAdrExtadr>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Line 1</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Line 2</rdf:li>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPreservesIndexedStructuredChildIndexed)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry line1;
    line1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Sublocation[1]");
    line1.value = openmeta::make_text(store.arena(), "Gion",
                                      openmeta::TextEncoding::Utf8);
    line1.origin.block          = block;
    line1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(line1), openmeta::kInvalidEntryId);

    openmeta::Entry line2;
    line2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Sublocation[2]");
    line2.value = openmeta::make_text(store.arena(), "Hanamikoji",
                                      openmeta::TextEncoding::Utf8);
    line2.origin.block          = block;
    line2.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(line2), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationShown>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:Sublocation>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Gion</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Hanamikoji</rdf:li>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesNestedStructured)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry province_name;
    province_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceName");
    province_name.value = openmeta::make_text(store.arena(), "Tokyo",
                                              openmeta::TextEncoding::Utf8);
    province_name.origin.block          = block;
    province_name.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(province_name), openmeta::kInvalidEntryId);

    openmeta::Entry province_code;
    province_code.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceCode");
    province_code.value = openmeta::make_text(store.arena(), "13",
                                              openmeta::TextEncoding::Utf8);
    province_code.origin.block          = block;
    province_code.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(province_code), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CreatorContactInfo rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CiAdrRegion rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Tokyo</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>13</rdf:li>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizesIndexedNestedStructuredAddressAliases)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry city;
    city.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/City");
    city.value = openmeta::make_text(store.arena(), "Kyoto",
                                     openmeta::TextEncoding::Utf8);
    city.origin.block          = block;
    city.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(city), openmeta::kInvalidEntryId);

    openmeta::Entry country;
    country.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/CountryName");
    country.value = openmeta::make_text(store.arena(), "Japan",
                                        openmeta::TextEncoding::Utf8);
    country.origin.block          = block;
    country.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(country), openmeta::kInvalidEntryId);

    openmeta::Entry country_code;
    country_code.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/CountryCode");
    country_code.value = openmeta::make_text(store.arena(), "JP",
                                             openmeta::TextEncoding::Utf8);
    country_code.origin.block          = block;
    country_code.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(country_code), openmeta::kInvalidEntryId);

    openmeta::Entry province;
    province.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/ProvinceState");
    province.value = openmeta::make_text(store.arena(), "Kyoto",
                                         openmeta::TextEncoding::Utf8);
    province.origin.block          = block;
    province.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(province), openmeta::kInvalidEntryId);

    openmeta::Entry world_region;
    world_region.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/WorldRegion");
    world_region.value = openmeta::make_text(store.arena(), "APAC",
                                             openmeta::TextEncoding::Utf8);
    world_region.origin.block          = block;
    world_region.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(world_region), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationShown>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:City>Kyoto</Iptc4xmpExt:City>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:CountryName>Japan</Iptc4xmpExt:CountryName>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:CountryCode>JP</Iptc4xmpExt:CountryCode>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:ProvinceState>Kyoto</Iptc4xmpExt:ProvinceState>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:WorldRegion>APAC</Iptc4xmpExt:WorldRegion>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:Address rdf:parseType=\"Resource\">"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizesStructuredLocationCreatedAddressAliases)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry city;
    city.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/City");
    city.value = openmeta::make_text(store.arena(), "Paris",
                                     openmeta::TextEncoding::Utf8);
    city.origin.block          = block;
    city.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(city), openmeta::kInvalidEntryId);

    openmeta::Entry country_name;
    country_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/CountryName");
    country_name.value = openmeta::make_text(store.arena(), "France",
                                             openmeta::TextEncoding::Utf8);
    country_name.origin.block          = block;
    country_name.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(country_name), openmeta::kInvalidEntryId);

    openmeta::Entry country_code;
    country_code.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/CountryCode");
    country_code.value = openmeta::make_text(store.arena(), "FR",
                                             openmeta::TextEncoding::Utf8);
    country_code.origin.block          = block;
    country_code.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(country_code), openmeta::kInvalidEntryId);

    openmeta::Entry province;
    province.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/ProvinceState");
    province.value = openmeta::make_text(store.arena(), "Ile-de-France",
                                         openmeta::TextEncoding::Utf8);
    province.origin.block          = block;
    province.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(province), openmeta::kInvalidEntryId);

    openmeta::Entry world_region;
    world_region.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/Address/WorldRegion");
    world_region.value = openmeta::make_text(store.arena(), "EMEA",
                                             openmeta::TextEncoding::Utf8);
    world_region.origin.block          = block;
    world_region.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(world_region), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationCreated rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:City>Paris</Iptc4xmpExt:City>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:CountryName>France</Iptc4xmpExt:CountryName>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:CountryCode>FR</Iptc4xmpExt:CountryCode>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:ProvinceState>Ile-de-France</Iptc4xmpExt:ProvinceState>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:WorldRegion>EMEA</Iptc4xmpExt:WorldRegion>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:Address rdf:parseType=\"Resource\">"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizesIndexedLocationDetailsChildShapes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry bad_name_scalar;
    bad_name_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationName");
    bad_name_scalar.value = openmeta::make_text(store.arena(), "legacy-name",
                                                openmeta::TextEncoding::Utf8);
    bad_name_scalar.origin.block          = block;
    bad_name_scalar.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(bad_name_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_name_default;
    good_name_default.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationName[@xml:lang=x-default]");
    good_name_default.value = openmeta::make_text(
        store.arena(), "Kyoto", openmeta::TextEncoding::Utf8);
    good_name_default.origin.block          = block;
    good_name_default.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(good_name_default), openmeta::kInvalidEntryId);

    openmeta::Entry good_name_fr;
    good_name_fr.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationName[@xml:lang=fr-FR]");
    good_name_fr.value = openmeta::make_text(store.arena(), "Kyoto FR",
                                             openmeta::TextEncoding::Utf8);
    good_name_fr.origin.block          = block;
    good_name_fr.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(good_name_fr), openmeta::kInvalidEntryId);

    openmeta::Entry bad_id_scalar;
    bad_id_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationId");
    bad_id_scalar.value = openmeta::make_text(store.arena(), "legacy-id",
                                              openmeta::TextEncoding::Utf8);
    bad_id_scalar.origin.block          = block;
    bad_id_scalar.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(bad_id_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_id1;
    good_id1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationId[1]");
    good_id1.value = openmeta::make_text(store.arena(), "loc-001",
                                         openmeta::TextEncoding::Utf8);
    good_id1.origin.block          = block;
    good_id1.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(good_id1), openmeta::kInvalidEntryId);

    openmeta::Entry good_id2;
    good_id2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationId[2]");
    good_id2.value = openmeta::make_text(store.arena(), "loc-002",
                                         openmeta::TextEncoding::Utf8);
    good_id2.origin.block          = block;
    good_id2.origin.order_in_block = 5U;
    ASSERT_NE(store.add_entry(good_id2), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationShown>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationName>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Kyoto</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"fr-FR\">Kyoto FR</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationId>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Bag>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>loc-001</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>loc-002</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-name"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-id"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizesStructuredLocationCreatedChildShapes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry bad_name_scalar;
    bad_name_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationName");
    bad_name_scalar.value = openmeta::make_text(store.arena(), "legacy-name",
                                                openmeta::TextEncoding::Utf8);
    bad_name_scalar.origin.block          = block;
    bad_name_scalar.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(bad_name_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_name_default;
    good_name_default.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationName[@xml:lang=x-default]");
    good_name_default.value = openmeta::make_text(
        store.arena(), "Paris", openmeta::TextEncoding::Utf8);
    good_name_default.origin.block          = block;
    good_name_default.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(good_name_default), openmeta::kInvalidEntryId);

    openmeta::Entry good_name_fr;
    good_name_fr.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationName[@xml:lang=fr-FR]");
    good_name_fr.value = openmeta::make_text(store.arena(), "Paris FR",
                                             openmeta::TextEncoding::Utf8);
    good_name_fr.origin.block          = block;
    good_name_fr.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(good_name_fr), openmeta::kInvalidEntryId);

    openmeta::Entry bad_id_scalar;
    bad_id_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationId");
    bad_id_scalar.value = openmeta::make_text(store.arena(), "legacy-id",
                                              openmeta::TextEncoding::Utf8);
    bad_id_scalar.origin.block          = block;
    bad_id_scalar.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(bad_id_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_id1;
    good_id1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationId[1]");
    good_id1.value = openmeta::make_text(store.arena(), "paris-001",
                                         openmeta::TextEncoding::Utf8);
    good_id1.origin.block          = block;
    good_id1.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(good_id1), openmeta::kInvalidEntryId);

    openmeta::Entry good_id2;
    good_id2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationCreated/LocationId[2]");
    good_id2.value = openmeta::make_text(store.arena(), "paris-002",
                                         openmeta::TextEncoding::Utf8);
    good_id2.origin.block          = block;
    good_id2.origin.order_in_block = 5U;
    ASSERT_NE(store.add_entry(good_id2), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationCreated rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationName>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Paris</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"fr-FR\">Paris FR</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationId>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Bag>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>paris-001</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>paris-002</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-name"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-id"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPreservesMixedNamespaceStructuredLocationDetailsChildren)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry id1;
    id1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/xmp:Identifier[1]");
    id1.value = openmeta::make_text(store.arena(), "loc-001",
                                    openmeta::TextEncoding::Utf8);
    id1.origin.block          = block;
    id1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(id1), openmeta::kInvalidEntryId);

    openmeta::Entry id2;
    id2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/xmp:Identifier[2]");
    id2.value = openmeta::make_text(store.arena(), "loc-002",
                                    openmeta::TextEncoding::Utf8);
    id2.origin.block          = block;
    id2.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(id2), openmeta::kInvalidEntryId);

    openmeta::Entry gps_lat;
    gps_lat.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/exif:GPSLatitude");
    gps_lat.value = openmeta::make_text(store.arena(), "41,24.5N",
                                        openmeta::TextEncoding::Utf8);
    gps_lat.origin.block          = block;
    gps_lat.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(gps_lat), openmeta::kInvalidEntryId);

    openmeta::Entry gps_lon;
    gps_lon.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/exif:GPSLongitude");
    gps_lon.value = openmeta::make_text(store.arena(), "2,9E",
                                        openmeta::TextEncoding::Utf8);
    gps_lon.origin.block          = block;
    gps_lon.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(gps_lon), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:LocationShown>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<xmp:Identifier>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Bag>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>loc-001</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>loc-002</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<exif:GPSLatitude>41,24.5N</exif:GPSLatitude>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<exif:GPSLongitude>2,9E</exif:GPSLongitude>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizesIptcExtEntityWithRoleShapes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry bad_creator_scalar;
    bad_creator_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator");
    bad_creator_scalar.value = openmeta::make_text(
        store.arena(), "legacy-creator", openmeta::TextEncoding::Utf8);
    bad_creator_scalar.origin.block          = block;
    bad_creator_scalar.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(bad_creator_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry bad_name_scalar;
    bad_name_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Name");
    bad_name_scalar.value = openmeta::make_text(
        store.arena(), "legacy-name", openmeta::TextEncoding::Utf8);
    bad_name_scalar.origin.block          = block;
    bad_name_scalar.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(bad_name_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_name_default;
    good_name_default.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Name[@xml:lang=x-default]");
    good_name_default.value = openmeta::make_text(
        store.arena(), "Alice Example", openmeta::TextEncoding::Utf8);
    good_name_default.origin.block          = block;
    good_name_default.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(good_name_default), openmeta::kInvalidEntryId);

    openmeta::Entry bad_role_scalar;
    bad_role_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Role");
    bad_role_scalar.value = openmeta::make_text(
        store.arena(), "legacy-role", openmeta::TextEncoding::Utf8);
    bad_role_scalar.origin.block          = block;
    bad_role_scalar.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(bad_role_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_role1;
    good_role1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Role[1]");
    good_role1.value = openmeta::make_text(
        store.arena(), "photographer", openmeta::TextEncoding::Utf8);
    good_role1.origin.block          = block;
    good_role1.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(good_role1), openmeta::kInvalidEntryId);

    openmeta::Entry good_role2;
    good_role2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Role[2]");
    good_role2.value = openmeta::make_text(
        store.arena(), "editor", openmeta::TextEncoding::Utf8);
    good_role2.origin.block          = block;
    good_role2.origin.order_in_block = 5U;
    ASSERT_NE(store.add_entry(good_role2), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:Creator>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:Name>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Alice Example</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:Role>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Bag>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>photographer</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>editor</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-creator"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-name"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-role"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizesArtworkOrObjectShapes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry bad_artwork_scalar;
    bad_artwork_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject");
    bad_artwork_scalar.value = openmeta::make_text(
        store.arena(), "legacy-artwork", openmeta::TextEncoding::Utf8);
    bad_artwork_scalar.origin.block          = block;
    bad_artwork_scalar.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(bad_artwork_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry bad_title_scalar;
    bad_title_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOTitle");
    bad_title_scalar.value = openmeta::make_text(
        store.arena(), "legacy-title", openmeta::TextEncoding::Utf8);
    bad_title_scalar.origin.block          = block;
    bad_title_scalar.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(bad_title_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_title_default;
    good_title_default.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOTitle[@xml:lang=x-default]");
    good_title_default.value = openmeta::make_text(
        store.arena(), "Sunset Study", openmeta::TextEncoding::Utf8);
    good_title_default.origin.block          = block;
    good_title_default.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(good_title_default), openmeta::kInvalidEntryId);

    openmeta::Entry bad_creator_scalar;
    bad_creator_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOCreator");
    bad_creator_scalar.value = openmeta::make_text(
        store.arena(), "legacy-creator", openmeta::TextEncoding::Utf8);
    bad_creator_scalar.origin.block          = block;
    bad_creator_scalar.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(bad_creator_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_creator1;
    good_creator1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOCreator[1]");
    good_creator1.value = openmeta::make_text(
        store.arena(), "Alice Example", openmeta::TextEncoding::Utf8);
    good_creator1.origin.block          = block;
    good_creator1.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(good_creator1), openmeta::kInvalidEntryId);

    openmeta::Entry good_creator2;
    good_creator2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOCreator[2]");
    good_creator2.value = openmeta::make_text(
        store.arena(), "Bob Example", openmeta::TextEncoding::Utf8);
    good_creator2.origin.block          = block;
    good_creator2.origin.order_in_block = 5U;
    ASSERT_NE(store.add_entry(good_creator2), openmeta::kInvalidEntryId);

    openmeta::Entry bad_style_scalar;
    bad_style_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOStylePeriod");
    bad_style_scalar.value = openmeta::make_text(
        store.arena(), "legacy-style", openmeta::TextEncoding::Utf8);
    bad_style_scalar.origin.block          = block;
    bad_style_scalar.origin.order_in_block = 6U;
    ASSERT_NE(store.add_entry(bad_style_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry good_style1;
    good_style1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOStylePeriod[1]");
    good_style1.value = openmeta::make_text(
        store.arena(), "Impressionism", openmeta::TextEncoding::Utf8);
    good_style1.origin.block          = block;
    good_style1.origin.order_in_block = 7U;
    ASSERT_NE(store.add_entry(good_style1), openmeta::kInvalidEntryId);

    openmeta::Entry good_style2;
    good_style2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOStylePeriod[2]");
    good_style2.value = openmeta::make_text(
        store.arena(), "Modernism", openmeta::TextEncoding::Utf8);
    good_style2.origin.block          = block;
    good_style2.origin.order_in_block = 8U;
    ASSERT_NE(store.add_entry(good_style2), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:ArtworkOrObject>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:AOTitle>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Sunset Study</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:AOCreator>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Seq>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Alice Example</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Bob Example</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:AOStylePeriod>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Impressionism</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>Modernism</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-artwork"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-title"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-creator"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-style"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizesPersonAndProductDetailShapes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry bad_person_scalar;
    bad_person_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails");
    bad_person_scalar.value = openmeta::make_text(
        store.arena(), "legacy-person", openmeta::TextEncoding::Utf8);
    bad_person_scalar.origin.block          = block;
    bad_person_scalar.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(bad_person_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry bad_person_name;
    bad_person_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails[1]/PersonName");
    bad_person_name.value = openmeta::make_text(
        store.arena(), "legacy-person-name", openmeta::TextEncoding::Utf8);
    bad_person_name.origin.block          = block;
    bad_person_name.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(bad_person_name), openmeta::kInvalidEntryId);

    openmeta::Entry good_person_name_default;
    good_person_name_default.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails[1]/PersonName[@xml:lang=x-default]");
    good_person_name_default.value = openmeta::make_text(
        store.arena(), "Jane Doe", openmeta::TextEncoding::Utf8);
    good_person_name_default.origin.block          = block;
    good_person_name_default.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(good_person_name_default),
              openmeta::kInvalidEntryId);

    openmeta::Entry bad_person_id;
    bad_person_id.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails[1]/PersonId");
    bad_person_id.value = openmeta::make_text(
        store.arena(), "legacy-person-id", openmeta::TextEncoding::Utf8);
    bad_person_id.origin.block          = block;
    bad_person_id.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(bad_person_id), openmeta::kInvalidEntryId);

    openmeta::Entry good_person_id1;
    good_person_id1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails[1]/PersonId[1]");
    good_person_id1.value = openmeta::make_text(
        store.arena(), "person-001", openmeta::TextEncoding::Utf8);
    good_person_id1.origin.block          = block;
    good_person_id1.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(good_person_id1), openmeta::kInvalidEntryId);

    openmeta::Entry good_person_id2;
    good_person_id2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails[1]/PersonId[2]");
    good_person_id2.value = openmeta::make_text(
        store.arena(), "person-002", openmeta::TextEncoding::Utf8);
    good_person_id2.origin.block          = block;
    good_person_id2.origin.order_in_block = 5U;
    ASSERT_NE(store.add_entry(good_person_id2), openmeta::kInvalidEntryId);

    openmeta::Entry bad_product_scalar;
    bad_product_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ProductInImage");
    bad_product_scalar.value = openmeta::make_text(
        store.arena(), "legacy-product", openmeta::TextEncoding::Utf8);
    bad_product_scalar.origin.block          = block;
    bad_product_scalar.origin.order_in_block = 6U;
    ASSERT_NE(store.add_entry(bad_product_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry bad_product_name;
    bad_product_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ProductInImage[1]/ProductName");
    bad_product_name.value = openmeta::make_text(
        store.arena(), "legacy-product-name", openmeta::TextEncoding::Utf8);
    bad_product_name.origin.block          = block;
    bad_product_name.origin.order_in_block = 7U;
    ASSERT_NE(store.add_entry(bad_product_name), openmeta::kInvalidEntryId);

    openmeta::Entry good_product_name_default;
    good_product_name_default.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ProductInImage[1]/ProductName[@xml:lang=x-default]");
    good_product_name_default.value = openmeta::make_text(
        store.arena(), "Camera Body", openmeta::TextEncoding::Utf8);
    good_product_name_default.origin.block          = block;
    good_product_name_default.origin.order_in_block = 8U;
    ASSERT_NE(store.add_entry(good_product_name_default),
              openmeta::kInvalidEntryId);

    openmeta::Entry bad_product_desc;
    bad_product_desc.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ProductInImage[1]/ProductDescription");
    bad_product_desc.value = openmeta::make_text(
        store.arena(), "legacy-product-desc", openmeta::TextEncoding::Utf8);
    bad_product_desc.origin.block          = block;
    bad_product_desc.origin.order_in_block = 9U;
    ASSERT_NE(store.add_entry(bad_product_desc), openmeta::kInvalidEntryId);

    openmeta::Entry good_product_desc_default;
    good_product_desc_default.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ProductInImage[1]/ProductDescription[@xml:lang=x-default]");
    good_product_desc_default.value = openmeta::make_text(
        store.arena(), "Mirrorless", openmeta::TextEncoding::Utf8);
    good_product_desc_default.origin.block          = block;
    good_product_desc_default.origin.order_in_block = 10U;
    ASSERT_NE(store.add_entry(good_product_desc_default),
              openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:PersonInImageWDetails>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:PersonName>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Jane Doe</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:PersonId>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>person-001</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>person-002</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:ProductInImage>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:ProductName>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Camera Body</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Mirrorless</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-person"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-person-name"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-person-id"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-product"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-product-name"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "legacy-product-desc"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPromotesFlatStructuredChildScalarsToCanonicalShapes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry creator_name;
    creator_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Name");
    creator_name.value = openmeta::make_text(
        store.arena(), "Alice Flat", openmeta::TextEncoding::Utf8);
    creator_name.origin.block          = block;
    creator_name.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(creator_name), openmeta::kInvalidEntryId);

    openmeta::Entry creator_role;
    creator_role.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Creator[1]/Role");
    creator_role.value = openmeta::make_text(
        store.arena(), "photographer", openmeta::TextEncoding::Utf8);
    creator_role.origin.block          = block;
    creator_role.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(creator_role), openmeta::kInvalidEntryId);

    openmeta::Entry artwork_creator;
    artwork_creator.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ArtworkOrObject[1]/AOCreator");
    artwork_creator.value = openmeta::make_text(
        store.arena(), "Alice Example", openmeta::TextEncoding::Utf8);
    artwork_creator.origin.block          = block;
    artwork_creator.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(artwork_creator), openmeta::kInvalidEntryId);

    openmeta::Entry location_name;
    location_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationName");
    location_name.value = openmeta::make_text(
        store.arena(), "City Hall", openmeta::TextEncoding::Utf8);
    location_name.origin.block          = block;
    location_name.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(location_name), openmeta::kInvalidEntryId);

    openmeta::Entry location_id;
    location_id.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/LocationId");
    location_id.value = openmeta::make_text(
        store.arena(), "loc-001", openmeta::TextEncoding::Utf8);
    location_id.origin.block          = block;
    location_id.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(location_id), openmeta::kInvalidEntryId);

    openmeta::Entry person_id;
    person_id.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonInImageWDetails[1]/PersonId");
    person_id.value = openmeta::make_text(
        store.arena(), "person-001", openmeta::TextEncoding::Utf8);
    person_id.origin.block          = block;
    person_id.origin.order_in_block = 5U;
    ASSERT_NE(store.add_entry(person_id), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    const std::span<const std::byte> payload(bundle.blocks[0].payload.data(),
                                             bundle.blocks[0].payload.size());
    EXPECT_TRUE(payload_contains_ascii(
        payload, "<rdf:li xml:lang=\"x-default\">Alice Flat</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<Iptc4xmpExt:Role>"));
    EXPECT_TRUE(payload_contains_ascii(payload,
                                       "<rdf:li>photographer</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<Iptc4xmpExt:AOCreator>"));
    EXPECT_TRUE(payload_contains_ascii(payload,
                                       "<rdf:li>Alice Example</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        payload, "<rdf:li xml:lang=\"x-default\">City Hall</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<Iptc4xmpExt:LocationId>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<rdf:li>loc-001</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<Iptc4xmpExt:PersonId>"));
    EXPECT_TRUE(payload_contains_ascii(payload,
                                       "<rdf:li>person-001</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload,
        "<Iptc4xmpExt:LocationName>City Hall</Iptc4xmpExt:LocationName>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload, "<Iptc4xmpExt:Role>photographer</Iptc4xmpExt:Role>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPromotesAdditionalStandardStructuredChildScalars)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry city;
    city.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrCity");
    city.value = openmeta::make_text(
        store.arena(), "Paris", openmeta::TextEncoding::Utf8);
    city.origin.block          = block;
    city.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(city), openmeta::kInvalidEntryId);

    openmeta::Entry cv_term_name;
    cv_term_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "AboutCvTerm[1]/CvTermName");
    cv_term_name.value = openmeta::make_text(
        store.arena(), "Culture", openmeta::TextEncoding::Utf8);
    cv_term_name.origin.block          = block;
    cv_term_name.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(cv_term_name), openmeta::kInvalidEntryId);

    openmeta::Entry person_heard_name;
    person_heard_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PersonHeard[1]/Name");
    person_heard_name.value = openmeta::make_text(
        store.arena(), "Witness", openmeta::TextEncoding::Utf8);
    person_heard_name.origin.block          = block;
    person_heard_name.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(person_heard_name), openmeta::kInvalidEntryId);

    openmeta::Entry link_qualifier;
    link_qualifier.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "DopesheetLink[1]/LinkQualifier");
    link_qualifier.value = openmeta::make_text(
        store.arena(), "keyframe", openmeta::TextEncoding::Utf8);
    link_qualifier.origin.block          = block;
    link_qualifier.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(link_qualifier), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    const std::span<const std::byte> payload(bundle.blocks[0].payload.data(),
                                             bundle.blocks[0].payload.size());
    EXPECT_TRUE(payload_contains_ascii(payload, "<Iptc4xmpCore:CiAdrCity>"));
    EXPECT_TRUE(payload_contains_ascii(
        payload, "<rdf:li xml:lang=\"x-default\">Paris</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<Iptc4xmpExt:CvTermName>"));
    EXPECT_TRUE(payload_contains_ascii(
        payload, "<rdf:li xml:lang=\"x-default\">Culture</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<Iptc4xmpExt:Name>"));
    EXPECT_TRUE(payload_contains_ascii(
        payload, "<rdf:li xml:lang=\"x-default\">Witness</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload,
                                       "<Iptc4xmpExt:LinkQualifier>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<rdf:li>keyframe</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload, "<Iptc4xmpCore:CiAdrCity>Paris</Iptc4xmpCore:CiAdrCity>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload, "<Iptc4xmpExt:CvTermName>Culture</Iptc4xmpExt:CvTermName>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload, "<Iptc4xmpExt:Name>Witness</Iptc4xmpExt:Name>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload,
        "<Iptc4xmpExt:LinkQualifier>keyframe</Iptc4xmpExt:LinkQualifier>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPromotesRemainingIptcExtStructuredChildScalars)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry contributor_name;
    contributor_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Contributor[1]/Name");
    contributor_name.value = openmeta::make_text(
        store.arena(), "Desk Editor", openmeta::TextEncoding::Utf8);
    contributor_name.origin.block          = block;
    contributor_name.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(contributor_name), openmeta::kInvalidEntryId);

    openmeta::Entry contributor_role;
    contributor_role.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Contributor[1]/Role");
    contributor_role.value = openmeta::make_text(
        store.arena(), "editor", openmeta::TextEncoding::Utf8);
    contributor_role.origin.block          = block;
    contributor_role.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(contributor_role), openmeta::kInvalidEntryId);

    openmeta::Entry planning_name;
    planning_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PlanningRef[1]/Name");
    planning_name.value = openmeta::make_text(
        store.arena(), "Editorial Plan", openmeta::TextEncoding::Utf8);
    planning_name.origin.block          = block;
    planning_name.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(planning_name), openmeta::kInvalidEntryId);

    openmeta::Entry planning_role;
    planning_role.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "PlanningRef[1]/Role");
    planning_role.value = openmeta::make_text(
        store.arena(), "assignment", openmeta::TextEncoding::Utf8);
    planning_role.origin.block          = block;
    planning_role.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(planning_role), openmeta::kInvalidEntryId);

    openmeta::Entry shown_event_name;
    shown_event_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "ShownEvent[1]/Name");
    shown_event_name.value = openmeta::make_text(
        store.arena(), "Press Conference", openmeta::TextEncoding::Utf8);
    shown_event_name.origin.block          = block;
    shown_event_name.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(shown_event_name), openmeta::kInvalidEntryId);

    openmeta::Entry supply_chain_source_name;
    supply_chain_source_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "SupplyChainSource[1]/Name");
    supply_chain_source_name.value = openmeta::make_text(
        store.arena(), "Agency Feed", openmeta::TextEncoding::Utf8);
    supply_chain_source_name.origin.block          = block;
    supply_chain_source_name.origin.order_in_block = 5U;
    ASSERT_NE(store.add_entry(supply_chain_source_name),
              openmeta::kInvalidEntryId);

    openmeta::Entry video_shot_type_name;
    video_shot_type_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "VideoShotType[1]/Name");
    video_shot_type_name.value = openmeta::make_text(
        store.arena(), "Interview", openmeta::TextEncoding::Utf8);
    video_shot_type_name.origin.block          = block;
    video_shot_type_name.origin.order_in_block = 6U;
    ASSERT_NE(store.add_entry(video_shot_type_name),
              openmeta::kInvalidEntryId);

    openmeta::Entry snapshot_qualifier;
    snapshot_qualifier.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "Snapshot[1]/LinkQualifier");
    snapshot_qualifier.value = openmeta::make_text(
        store.arena(), "frame-001", openmeta::TextEncoding::Utf8);
    snapshot_qualifier.origin.block          = block;
    snapshot_qualifier.origin.order_in_block = 7U;
    ASSERT_NE(store.add_entry(snapshot_qualifier), openmeta::kInvalidEntryId);

    openmeta::Entry transcript_qualifier;
    transcript_qualifier.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "TranscriptLink[1]/LinkQualifier");
    transcript_qualifier.value = openmeta::make_text(
        store.arena(), "quote", openmeta::TextEncoding::Utf8);
    transcript_qualifier.origin.block          = block;
    transcript_qualifier.origin.order_in_block = 8U;
    ASSERT_NE(store.add_entry(transcript_qualifier),
              openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    const std::span<const std::byte> payload(bundle.blocks[0].payload.data(),
                                             bundle.blocks[0].payload.size());
    EXPECT_TRUE(payload_contains_ascii(payload, "<Iptc4xmpExt:Contributor>"));
    EXPECT_TRUE(payload_contains_ascii(
        payload, "<rdf:li xml:lang=\"x-default\">Desk Editor</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<rdf:li>editor</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<Iptc4xmpExt:PlanningRef>"));
    EXPECT_TRUE(payload_contains_ascii(
        payload, "<rdf:li xml:lang=\"x-default\">Editorial Plan</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload,
                                       "<rdf:li>assignment</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        payload, "<rdf:li xml:lang=\"x-default\">Press Conference</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        payload, "<rdf:li xml:lang=\"x-default\">Agency Feed</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        payload, "<rdf:li xml:lang=\"x-default\">Interview</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<rdf:li>frame-001</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<rdf:li>quote</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload, "<Iptc4xmpExt:Name>Desk Editor</Iptc4xmpExt:Name>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload, "<Iptc4xmpExt:Role>editor</Iptc4xmpExt:Role>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload, "<Iptc4xmpExt:Name>Editorial Plan</Iptc4xmpExt:Name>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload, "<Iptc4xmpExt:Role>assignment</Iptc4xmpExt:Role>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload, "<Iptc4xmpExt:Name>Press Conference</Iptc4xmpExt:Name>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload, "<Iptc4xmpExt:Name>Agency Feed</Iptc4xmpExt:Name>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload, "<Iptc4xmpExt:Name>Interview</Iptc4xmpExt:Name>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload,
        "<Iptc4xmpExt:LinkQualifier>frame-001</Iptc4xmpExt:LinkQualifier>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload,
        "<Iptc4xmpExt:LinkQualifier>quote</Iptc4xmpExt:LinkQualifier>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesNestedStructuredChildLangAlt)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry city_default;
    city_default.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=x-default]");
    city_default.value = openmeta::make_text(store.arena(), "Tokyo",
                                             openmeta::TextEncoding::Utf8);
    city_default.origin.block          = block;
    city_default.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(city_default), openmeta::kInvalidEntryId);

    openmeta::Entry city_ja;
    city_ja.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceName[@xml:lang=ja-JP]");
    city_ja.value = openmeta::make_text(store.arena(), "東京",
                                        openmeta::TextEncoding::Utf8);
    city_ja.origin.block          = block;
    city_ja.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(city_ja), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CiAdrRegion rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:ProvinceName>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Tokyo</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"ja-JP\">東京</rdf:li>"));
}

TEST(MetadataTransferApi, PreparePortableXmpPreservesNestedStructuredChildIndexed)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry code1;
    code1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceCode[1]");
    code1.value = openmeta::make_text(store.arena(), "13",
                                      openmeta::TextEncoding::Utf8);
    code1.origin.block          = block;
    code1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(code1), openmeta::kInvalidEntryId);

    openmeta::Entry code2;
    code2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceCode[2]");
    code2.value = openmeta::make_text(store.arena(), "JP-13",
                                      openmeta::TextEncoding::Utf8);
    code2.origin.block          = block;
    code2.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(code2), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:CiAdrRegion rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:ProvinceCode>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>13</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>JP-13</rdf:li>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPreservesIndexedNestedStructuredChildLangAlt)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry city_default;
    city_default.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/City[@xml:lang=x-default]");
    city_default.value = openmeta::make_text(store.arena(), "Kyoto",
                                             openmeta::TextEncoding::Utf8);
    city_default.origin.block          = block;
    city_default.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(city_default), openmeta::kInvalidEntryId);

    openmeta::Entry city_ja;
    city_ja.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/City[@xml:lang=ja-JP]");
    city_ja.value = openmeta::make_text(store.arena(), "京都",
                                        openmeta::TextEncoding::Utf8);
    city_ja.origin.block          = block;
    city_ja.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(city_ja), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:Address rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:City>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Kyoto</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"ja-JP\">京都</rdf:li>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPreservesIndexedNestedStructuredChildIndexed)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry code1;
    code1.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/CountryCode[1]");
    code1.value = openmeta::make_text(store.arena(), "JP",
                                      openmeta::TextEncoding::Utf8);
    code1.origin.block          = block;
    code1.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(code1), openmeta::kInvalidEntryId);

    openmeta::Entry code2;
    code2.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpExt/2008-02-29/",
        "LocationShown[1]/Address/CountryCode[2]");
    code2.value = openmeta::make_text(store.arena(), "JP-26",
                                      openmeta::TextEncoding::Utf8);
    code2.origin.block          = block;
    code2.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(code2), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:Address rdf:parseType=\"Resource\">"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpExt:CountryCode>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>JP</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>JP-26</rdf:li>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPromotesFlatNestedStructuredChildScalarsToCanonicalShapes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry province_name;
    province_name.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceName");
    province_name.value = openmeta::make_text(
        store.arena(), "Tokyo", openmeta::TextEncoding::Utf8);
    province_name.origin.block          = block;
    province_name.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(province_name), openmeta::kInvalidEntryId);

    openmeta::Entry province_code;
    province_code.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "CreatorContactInfo/CiAdrRegion/ProvinceCode");
    province_code.value = openmeta::make_text(
        store.arena(), "JP-13", openmeta::TextEncoding::Utf8);
    province_code.origin.block          = block;
    province_code.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(province_code), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    const std::span<const std::byte> payload(bundle.blocks[0].payload.data(),
                                             bundle.blocks[0].payload.size());
    EXPECT_TRUE(payload_contains_ascii(payload, "<Iptc4xmpCore:ProvinceName>"));
    EXPECT_TRUE(payload_contains_ascii(
        payload, "<rdf:li xml:lang=\"x-default\">Tokyo</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<Iptc4xmpCore:ProvinceCode>"));
    EXPECT_TRUE(payload_contains_ascii(payload, "<rdf:li>JP-13</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        payload,
        "<Iptc4xmpCore:ProvinceName>Tokyo</Iptc4xmpCore:ProvinceName>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpPrefersFirstSeenIndexedOverScalarExistingShape)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry xmp_subject_indexed;
    xmp_subject_indexed.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "subject[1]");
    xmp_subject_indexed.value = openmeta::make_text(
        store.arena(), "indexed-keyword", openmeta::TextEncoding::Utf8);
    xmp_subject_indexed.origin.block          = block;
    xmp_subject_indexed.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(xmp_subject_indexed), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_subject_scalar;
    xmp_subject_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "subject");
    xmp_subject_scalar.value = openmeta::make_text(
        store.arena(), "scalar-keyword", openmeta::TextEncoding::Utf8);
    xmp_subject_scalar.origin.block          = block;
    xmp_subject_scalar.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(xmp_subject_scalar), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<dc:subject>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Bag>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>indexed-keyword</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<dc:subject>scalar-keyword</dc:subject>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizeManagedReplacesXDefaultAltTextAndPreservesOtherLocales)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    const std::string title = "Generated Title";
    openmeta::Entry iptc_title;
    iptc_title.key = openmeta::make_iptc_dataset_key(2U, 5U);
    iptc_title.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(title.data()), title.size()));
    iptc_title.origin.block          = block;
    iptc_title.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(iptc_title), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_title_default;
    xmp_title_default.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/",
        "title[@xml:lang=x-default]");
    xmp_title_default.value = openmeta::make_text(
        store.arena(), "Default title", openmeta::TextEncoding::Utf8);
    xmp_title_default.origin.block          = block;
    xmp_title_default.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(xmp_title_default), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_title_fr;
    xmp_title_fr.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/",
        "title[@xml:lang=fr-FR]");
    xmp_title_fr.value = openmeta::make_text(
        store.arena(), "Titre localise", openmeta::TextEncoding::Utf8);
    xmp_title_fr.origin.block          = block;
    xmp_title_fr.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(xmp_title_fr), openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;
    request.xmp_conflict_policy  = openmeta::XmpConflictPolicy::ExistingWins;
    request.xmp_existing_standard_namespace_policy
        = openmeta::XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:Alt>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Generated Title</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"fr-FR\">Titre localise</rdf:li>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Default title</rdf:li>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizeManagedDropsStructuredDescendantsForGeneratedManagedBases)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    const std::string title = "Generated Title";
    openmeta::Entry iptc_title;
    iptc_title.key = openmeta::make_iptc_dataset_key(2U, 5U);
    iptc_title.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(title.data()), title.size()));
    iptc_title.origin.block          = block;
    iptc_title.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(iptc_title), openmeta::kInvalidEntryId);

    const std::string keyword = "museum";
    openmeta::Entry iptc_keyword;
    iptc_keyword.key = openmeta::make_iptc_dataset_key(2U, 25U);
    iptc_keyword.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       keyword.data()),
                                   keyword.size()));
    iptc_keyword.origin.block          = block;
    iptc_keyword.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(iptc_keyword), openmeta::kInvalidEntryId);

    const std::string location = "Louvre";
    openmeta::Entry iptc_location;
    iptc_location.key = openmeta::make_iptc_dataset_key(2U, 92U);
    iptc_location.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       location.data()),
                                   location.size()));
    iptc_location.origin.block          = block;
    iptc_location.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(iptc_location), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_title_structured;
    xmp_title_structured.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/",
        "title/LegacyStructured");
    xmp_title_structured.value = openmeta::make_text(
        store.arena(), "Legacy title", openmeta::TextEncoding::Utf8);
    xmp_title_structured.origin.block          = block;
    xmp_title_structured.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(xmp_title_structured), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_subject_structured;
    xmp_subject_structured.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/",
        "subject/LegacyStructured");
    xmp_subject_structured.value = openmeta::make_text(
        store.arena(), "Legacy subject", openmeta::TextEncoding::Utf8);
    xmp_subject_structured.origin.block          = block;
    xmp_subject_structured.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(xmp_subject_structured),
              openmeta::kInvalidEntryId);

    openmeta::Entry xmp_location_structured;
    xmp_location_structured.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "Location/LegacyStructured");
    xmp_location_structured.value = openmeta::make_text(
        store.arena(), "Legacy location", openmeta::TextEncoding::Utf8);
    xmp_location_structured.origin.block          = block;
    xmp_location_structured.origin.order_in_block = 5U;
    ASSERT_NE(store.add_entry(xmp_location_structured),
              openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;
    request.xmp_conflict_policy  = openmeta::XmpConflictPolicy::ExistingWins;
    request.xmp_existing_standard_namespace_policy
        = openmeta::XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li xml:lang=\"x-default\">Generated Title</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>museum</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:Location>Louvre</Iptc4xmpCore:Location>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<dc:LegacyStructured>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:LegacyStructured>"));
}

TEST(MetadataTransferApi,
     PreparePortableXmpCanonicalizeManagedDropsFlatCrossShapeExistingManagedProperties)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    const std::string keyword = "iptc-keyword";
    openmeta::Entry iptc_keyword;
    iptc_keyword.key = openmeta::make_iptc_dataset_key(2U, 25U);
    iptc_keyword.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       keyword.data()),
                                   keyword.size()));
    iptc_keyword.origin.block          = block;
    iptc_keyword.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(iptc_keyword), openmeta::kInvalidEntryId);

    const std::string location = "Louvre";
    openmeta::Entry iptc_location;
    iptc_location.key = openmeta::make_iptc_dataset_key(2U, 92U);
    iptc_location.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>(
                                       location.data()),
                                   location.size()));
    iptc_location.origin.block          = block;
    iptc_location.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(iptc_location), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_subject_scalar;
    xmp_subject_scalar.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "subject");
    xmp_subject_scalar.value = openmeta::make_text(
        store.arena(), "Legacy subject", openmeta::TextEncoding::Utf8);
    xmp_subject_scalar.origin.block          = block;
    xmp_subject_scalar.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(xmp_subject_scalar), openmeta::kInvalidEntryId);

    openmeta::Entry xmp_location_indexed;
    xmp_location_indexed.key = openmeta::make_xmp_property_key(
        store.arena(), "http://iptc.org/std/Iptc4xmpCore/1.0/xmlns/",
        "Location[1]");
    xmp_location_indexed.value = openmeta::make_text(
        store.arena(), "Legacy indexed location",
        openmeta::TextEncoding::Utf8);
    xmp_location_indexed.origin.block          = block;
    xmp_location_indexed.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(xmp_location_indexed),
              openmeta::kInvalidEntryId);

    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1    = false;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.xmp_portable         = true;
    request.xmp_include_existing = true;
    request.xmp_conflict_policy  = openmeta::XmpConflictPolicy::ExistingWins;
    request.xmp_existing_standard_namespace_policy
        = openmeta::XmpExistingStandardNamespacePolicy::CanonicalizeManaged;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<rdf:li>iptc-keyword</rdf:li>"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<Iptc4xmpCore:Location>Louvre</Iptc4xmpCore:Location>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "<dc:subject>Legacy subject</dc:subject>"));
    EXPECT_FALSE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "Legacy indexed location"));
}

TEST(MetadataTransferApi, PrepareBuildsJpegExifApp1Block)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x010FU);
    e.value = openmeta::make_text(store.arena(), "Vendor",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app1-exif");

    const std::span<const std::byte> p(bundle.blocks[0].payload.data(),
                                       bundle.blocks[0].payload.size());
    ASSERT_GE(p.size(), 20U);
    EXPECT_EQ(p[0], std::byte { 'E' });
    EXPECT_EQ(p[1], std::byte { 'x' });
    EXPECT_EQ(p[2], std::byte { 'i' });
    EXPECT_EQ(p[3], std::byte { 'f' });
    EXPECT_EQ(p[6], std::byte { 'I' });
    EXPECT_EQ(p[7], std::byte { 'I' });
    EXPECT_EQ(read_u16le(p, 8U), 42U);

    const uint32_t ifd0_off = read_u32le(p, 10U);
    const size_t ifd0_pos   = 6U + static_cast<size_t>(ifd0_off);
    ASSERT_LT(ifd0_pos + 2U, p.size());
    EXPECT_EQ(read_u16le(p, ifd0_pos), 1U);
    ASSERT_LT(ifd0_pos + 14U, p.size());
    EXPECT_EQ(read_u16le(p, ifd0_pos + 2U), 0x010FU);
    EXPECT_EQ(read_u16le(p, ifd0_pos + 4U), 2U);
}

TEST(MetadataTransferApi, PrepareBuildsJxlExifAndXmpBoxes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "exififd", 0x9003U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "title");
    xmp.value                 = openmeta::make_text(store.arena(), "OpenMeta",
                                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest jpeg_request;
    jpeg_request.target_format      = openmeta::TransferTargetFormat::Jpeg;
    jpeg_request.include_icc_app2   = false;
    jpeg_request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle jpeg_bundle;
    const openmeta::PrepareTransferResult jpeg_result
        = openmeta::prepare_metadata_for_target(store, jpeg_request,
                                                &jpeg_bundle);
    ASSERT_EQ(jpeg_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(jpeg_bundle.blocks.size(), 2U);
    ASSERT_EQ(jpeg_bundle.time_patch_map.size(), 1U);

    openmeta::PrepareTransferRequest jxl_request = jpeg_request;
    jxl_request.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBundle jxl_bundle;
    const openmeta::PrepareTransferResult jxl_result
        = openmeta::prepare_metadata_for_target(store, jxl_request,
                                                &jxl_bundle);

    EXPECT_EQ(jxl_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(jxl_result.errors, 0U);
    ASSERT_EQ(jxl_bundle.blocks.size(), 2U);
    ASSERT_EQ(jxl_bundle.time_patch_map.size(),
              jpeg_bundle.time_patch_map.size());

    EXPECT_EQ(jxl_bundle.blocks[0].route, "jxl:box-exif");
    EXPECT_EQ(jxl_bundle.blocks[0].box_type,
              (std::array<char, 4> { 'E', 'x', 'i', 'f' }));
    ASSERT_GE(jxl_bundle.blocks[0].payload.size(), 10U);
    uint32_t tiff_offset = 0U;
    ASSERT_TRUE(read_test_u32be(
        std::span<const std::byte>(jxl_bundle.blocks[0].payload.data(),
                                   jxl_bundle.blocks[0].payload.size()),
        0U, &tiff_offset));
    EXPECT_EQ(tiff_offset, 6U);
    EXPECT_EQ(jxl_bundle.blocks[0].payload[4], std::byte { 'E' });
    EXPECT_EQ(jxl_bundle.blocks[0].payload[5], std::byte { 'x' });
    EXPECT_EQ(jxl_bundle.blocks[0].payload[6], std::byte { 'i' });
    EXPECT_EQ(jxl_bundle.blocks[0].payload[7], std::byte { 'f' });

    EXPECT_EQ(jxl_bundle.blocks[1].route, "jxl:box-xml");
    EXPECT_EQ(jxl_bundle.blocks[1].box_type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
    ASSERT_FALSE(jxl_bundle.blocks[1].payload.empty());
    EXPECT_EQ(jxl_bundle.blocks[1].payload[0], std::byte { '<' });

    EXPECT_EQ(jxl_bundle.time_patch_map[0].block_index, 0U);
    EXPECT_EQ(jxl_bundle.time_patch_map[0].byte_offset,
              static_cast<uint32_t>(jpeg_bundle.time_patch_map[0].byte_offset
                                    + 4U));
    EXPECT_EQ(jxl_bundle.time_patch_map[0].width,
              jpeg_bundle.time_patch_map[0].width);
}

TEST(MetadataTransferApi, PrepareBuildsWebpExifAndXmpChunks)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "exififd", 0x9003U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "title");
    xmp.value                 = openmeta::make_text(store.arena(), "OpenMeta",
                                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Webp;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 2U);
    ASSERT_EQ(bundle.time_patch_map.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "webp:chunk-exif");
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::Exif);
    EXPECT_EQ(bundle.blocks[1].route, "webp:chunk-xmp");
    EXPECT_EQ(bundle.blocks[1].kind, openmeta::TransferBlockKind::Xmp);
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[1].payload.data(),
                                   bundle.blocks[1].payload.size()),
        "OpenMeta"));
}

TEST(MetadataTransferApi, PrepareBuildsPngExifAndXmpChunks)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "exififd", 0x9003U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(store.arena(), "OpenMeta PNG",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Png;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 2U);
    ASSERT_EQ(bundle.time_patch_map.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "png:chunk-exif");
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::Exif);
    ASSERT_GE(bundle.blocks[0].payload.size(), 14U);
    EXPECT_NE(bundle.blocks[0].payload[0], std::byte { 'E' });
    EXPECT_NE(bundle.blocks[0].payload[1], std::byte { 'x' });
    EXPECT_TRUE(contains_byte_pair(std::span<const std::byte>(
                                       bundle.blocks[0].payload.data(),
                                       bundle.blocks[0].payload.size()),
                                   static_cast<uint8_t>('I'),
                                   static_cast<uint8_t>('I'))
                || contains_byte_pair(std::span<const std::byte>(
                                          bundle.blocks[0].payload.data(),
                                          bundle.blocks[0].payload.size()),
                                      static_cast<uint8_t>('M'),
                                      static_cast<uint8_t>('M')));
    EXPECT_EQ(bundle.blocks[1].route, "png:chunk-xmp");
    EXPECT_EQ(bundle.blocks[1].kind, openmeta::TransferBlockKind::Xmp);
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[1].payload.data(),
                                   bundle.blocks[1].payload.size()),
        "XML:com.adobe.xmp"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[1].payload.data(),
                                   bundle.blocks[1].payload.size()),
        "OpenMeta PNG"));
}

TEST(MetadataTransferApi, PrepareDropsMakerNoteWhenProfileRequestsDrop)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry ifd0;
    ifd0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x010FU);
    ifd0.value = openmeta::make_text(store.arena(), "Vendor",
                                     openmeta::TextEncoding::Ascii);
    ifd0.origin.block          = block;
    ifd0.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(ifd0), openmeta::kInvalidEntryId);

    openmeta::Entry maker;
    maker.key = openmeta::make_exif_tag_key(store.arena(), "exififd", 0x927CU);
    const std::array<std::byte, 6> maker_bytes = {
        std::byte { 'A' }, std::byte { 'B' }, std::byte { 'C' },
        std::byte { 'D' }, std::byte { 'E' }, std::byte { 'F' },
    };
    maker.value
        = openmeta::make_bytes(store.arena(),
                               std::span<const std::byte>(maker_bytes.data(),
                                                          maker_bytes.size()));
    maker.origin.block          = block;
    maker.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(maker), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;
    request.profile.makernote  = openmeta::TransferPolicyAction::Drop;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_FALSE(exif_app1_contains_exififd_tag(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        0x927CU));

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(bundle,
                               openmeta::TransferPolicySubject::MakerNote);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(decision->reason, openmeta::TransferPolicyReason::ExplicitDrop);
    EXPECT_EQ(decision->matched_entries, 1U);
}

TEST(MetadataTransferApi, PrepareRecordsUnserializedJumbfAndC2paPolicies)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry jumbf;
    jumbf.key = openmeta::make_jumbf_field_key(store.arena(), "manifest.label");
    jumbf.value                 = openmeta::make_text(store.arena(), "main",
                                                      openmeta::TextEncoding::Utf8);
    jumbf.origin.block          = block;
    jumbf.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(jumbf), openmeta::kInvalidEntryId);

    openmeta::Entry c2pa;
    c2pa.key   = openmeta::make_jumbf_field_key(store.arena(), "c2pa.detected");
    c2pa.value = openmeta::make_u32(1U);
    c2pa.origin.block          = block;
    c2pa.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(c2pa), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code,
              openmeta::PrepareTransferCode::RequestedMetadataNotSerializable);
    EXPECT_GE(result.warnings, 2U);
    EXPECT_TRUE(bundle.blocks.empty());
    ASSERT_EQ(bundle.policy_decisions.size(), 3U);

    const openmeta::PreparedTransferPolicyDecision* jumbf_decision
        = find_policy_decision(bundle, openmeta::TransferPolicySubject::Jumbf);
    ASSERT_NE(jumbf_decision, nullptr);
    EXPECT_EQ(jumbf_decision->requested, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(jumbf_decision->effective, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(jumbf_decision->reason,
              openmeta::TransferPolicyReason::TargetSerializationUnavailable);
    EXPECT_EQ(jumbf_decision->matched_entries, 1U);

    const openmeta::PreparedTransferPolicyDecision* c2pa_decision
        = find_policy_decision(bundle, openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(c2pa_decision, nullptr);
    EXPECT_EQ(c2pa_decision->requested, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(c2pa_decision->effective, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(c2pa_decision->reason,
              openmeta::TransferPolicyReason::TargetSerializationUnavailable);
    EXPECT_EQ(c2pa_decision->matched_entries, 1U);
}

TEST(MetadataTransferApi, PrepareProjectsDecodedJumbfCborKeysForJpegTarget)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry title;
    title.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                "box.0.1.cbor.manifest.title");
    title.value                 = openmeta::make_text(store.arena(), "OpenMeta",
                                                      openmeta::TextEncoding::Utf8);
    title.origin.block          = block;
    title.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(title), openmeta::kInvalidEntryId);

    openmeta::Entry count;
    count.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                "box.0.1.cbor.manifest.count");
    count.value                 = openmeta::make_u32(2U);
    count.origin.block          = block;
    count.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(count), openmeta::kInvalidEntryId);

    openmeta::Entry item0;
    item0.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                "box.0.1.cbor.items[0]");
    item0.value                 = openmeta::make_u32(7U);
    item0.origin.block          = block;
    item0.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(item0), openmeta::kInvalidEntryId);

    openmeta::Entry item1;
    item1.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                "box.0.1.cbor.items[1]");
    item1.value                 = openmeta::make_text(store.arena(), "ok",
                                                      openmeta::TextEncoding::Ascii);
    item1.origin.block          = block;
    item1.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(item1), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::Jumbf);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app11-jumbf");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "openmeta.projected.box.0.1.cbor"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "OpenMeta"));

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(bundle, openmeta::TransferPolicySubject::Jumbf);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::ProjectedPayload);
    EXPECT_EQ(decision->matched_entries, 4U);
}

TEST(MetadataTransferApi, PrepareProjectsDecodedJumbfCborKeysForJxlTarget)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry title;
    title.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                "box.0.1.cbor.manifest.title");
    title.value                 = openmeta::make_text(store.arena(), "OpenMeta",
                                                      openmeta::TextEncoding::Utf8);
    title.origin.block          = block;
    title.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(title), openmeta::kInvalidEntryId);

    openmeta::Entry count;
    count.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                "box.0.1.cbor.manifest.count");
    count.value                 = openmeta::make_u32(2U);
    count.origin.block          = block;
    count.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(count), openmeta::kInvalidEntryId);

    openmeta::Entry item0;
    item0.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                "box.0.1.cbor.items[0]");
    item0.value                 = openmeta::make_u32(7U);
    item0.origin.block          = block;
    item0.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(item0), openmeta::kInvalidEntryId);

    openmeta::Entry item1;
    item1.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                "box.0.1.cbor.items[1]");
    item1.value                 = openmeta::make_text(store.arena(), "ok",
                                                      openmeta::TextEncoding::Ascii);
    item1.origin.block          = block;
    item1.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(item1), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Jxl;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::Jumbf);
    EXPECT_EQ(bundle.blocks[0].route, "jxl:box-jumb");
    EXPECT_EQ(bundle.blocks[0].box_type,
              (std::array<char, 4> { 'j', 'u', 'm', 'b' }));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "openmeta.projected.box.0.1.cbor"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "OpenMeta"));

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(bundle, openmeta::TransferPolicySubject::Jumbf);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::ProjectedPayload);
    EXPECT_EQ(decision->matched_entries, 4U);
}

TEST(MetadataTransferApi, PrepareProjectsMultipleDecodedJumbfRootsForJpegTarget)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry first;
    first.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                "box.0.1.cbor.title");
    first.value                 = openmeta::make_text(store.arena(), "one",
                                                      openmeta::TextEncoding::Ascii);
    first.origin.block          = block;
    first.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(first), openmeta::kInvalidEntryId);

    openmeta::Entry second;
    second.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                 "box.1.1.cbor.title");
    second.value                 = openmeta::make_text(store.arena(), "two",
                                                       openmeta::TextEncoding::Ascii);
    second.origin.block          = block;
    second.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(second), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 2U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app11-jumbf");
    EXPECT_EQ(bundle.blocks[1].route, "jpeg:app11-jumbf");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "openmeta.projected.box.0.1.cbor"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[1].payload.data(),
                                   bundle.blocks[1].payload.size()),
        "openmeta.projected.box.1.1.cbor"));

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(bundle, openmeta::TransferPolicySubject::Jumbf);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::ProjectedPayload);
    EXPECT_EQ(decision->matched_entries, 2U);
}

TEST(MetadataTransferApi, PrepareProjectsTaggedDecodedJumbfValueForJpegTarget)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry tagged_value;
    tagged_value.key          = openmeta::make_jumbf_cbor_key(store.arena(),
                                                              "box.0.1.cbor.when");
    tagged_value.value        = openmeta::make_text(store.arena(), "tagged",
                                                    openmeta::TextEncoding::Ascii);
    tagged_value.origin.block = block;
    tagged_value.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(tagged_value), openmeta::kInvalidEntryId);

    openmeta::Entry tag;
    tag.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                              "box.0.1.cbor.when.@tag");
    tag.value                 = openmeta::make_u64(1U);
    tag.origin.block          = block;
    tag.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(tag), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    const std::array<std::byte, 8U> tagged_pattern = {
        std::byte { 0xC1 }, std::byte { 0x66 }, std::byte { 't' },
        std::byte { 'a' },  std::byte { 'g' },  std::byte { 'g' },
        std::byte { 'e' },  std::byte { 'd' },
    };
    EXPECT_TRUE(payload_contains_bytes(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        std::span<const std::byte>(tagged_pattern.data(),
                                   tagged_pattern.size())));
}

TEST(MetadataTransferApi, PrepareRejectsAmbiguousProjectedJumbfMapKeys)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry value;
    value.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                "box.0.1.cbor.map.1");
    value.value                 = openmeta::make_u32(1U);
    value.origin.block          = block;
    value.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(value), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code,
              openmeta::PrepareTransferCode::RequestedMetadataNotSerializable);
    EXPECT_GE(result.warnings, 1U);
    EXPECT_TRUE(bundle.blocks.empty());

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(bundle, openmeta::TransferPolicySubject::Jumbf);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::TargetSerializationUnavailable);
    EXPECT_EQ(decision->message,
              "projected jumbf cbor numeric map keys are ambiguous");
}

TEST(MetadataTransferApi, PrepareRejectsAmbiguousProjectedJumbfU8Scalar)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry value;
    value.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                "box.0.1.cbor.flag");
    value.value                 = openmeta::make_u8(1U);
    value.origin.block          = block;
    value.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(value), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(bundle.blocks.empty());

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(bundle, openmeta::TransferPolicySubject::Jumbf);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::TargetSerializationUnavailable);
    EXPECT_EQ(
        decision->message,
        "projected jumbf cbor U8 scalars are ambiguous (decoded bool/simple vs integer)");
}

TEST(MetadataTransferApi, PrepareRejectsAmbiguousProjectedJumbfNullText)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry value;
    value.key                   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                                "box.0.1.cbor.value");
    value.value                 = openmeta::make_text(store.arena(), "null",
                                                      openmeta::TextEncoding::Ascii);
    value.origin.block          = block;
    value.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(value), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(bundle.blocks.empty());

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(bundle, openmeta::TransferPolicySubject::Jumbf);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(
        decision->message,
        "projected jumbf cbor sentinel text is ambiguous (decoded null/undefined vs string)");
}

TEST(MetadataTransferApi, PrepareRejectsAmbiguousProjectedJumbfSimpleText)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry value;
    value.key          = openmeta::make_jumbf_cbor_key(store.arena(),
                                                       "box.0.1.cbor.value");
    value.value        = openmeta::make_text(store.arena(), "simple(16)",
                                             openmeta::TextEncoding::Ascii);
    value.origin.block = block;
    value.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(value), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(bundle.blocks.empty());

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(bundle, openmeta::TransferPolicySubject::Jumbf);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->message,
              "projected jumbf cbor simple-value text is ambiguous");
}

TEST(MetadataTransferApi,
     PrepareRejectsAmbiguousProjectedJumbfLargeNegativeText)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry value;
    value.key   = openmeta::make_jumbf_cbor_key(store.arena(),
                                                "box.0.1.cbor.value");
    value.value = openmeta::make_text(store.arena(), "-(1+9223372036854775807)",
                                      openmeta::TextEncoding::Ascii);
    value.origin.block          = block;
    value.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(value), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(bundle.blocks.empty());

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(bundle, openmeta::TransferPolicySubject::Jumbf);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->message,
              "projected jumbf cbor large-negative fallback text is ambiguous");
}

TEST(MetadataTransferApi, PrepareBuildsTiffExifTransferBlockAndTimePatchSlots)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e0;
    e0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e0.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                   openmeta::TextEncoding::Ascii);
    e0.origin.block          = block;
    e0.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e0), openmeta::kInvalidEntryId);

    openmeta::Entry e1;
    e1.key   = openmeta::make_exif_tag_key(store.arena(), "exififd", 0x9003U);
    e1.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                   openmeta::TextEncoding::Ascii);
    e1.origin.block          = block;
    e1.origin.order_in_block = 1;
    ASSERT_NE(store.add_entry(e1), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.target_format, openmeta::TransferTargetFormat::Tiff);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "tiff:ifd-exif-app1");
    EXPECT_FALSE(bundle.time_patch_map.empty());
    for (size_t i = 0; i < bundle.time_patch_map.size(); ++i) {
        EXPECT_EQ(bundle.time_patch_map[i].block_index, 0U);
    }
}

TEST(MetadataTransferApi,
     PrepareBuildsDngExifTransferBlockWithSyntheticDngVersion)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(store.arena(),
                                    "OpenMeta Transfer Source",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Dng;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.target_format, openmeta::TransferTargetFormat::Dng);
    ASSERT_EQ(bundle.dng_target_mode,
              openmeta::DngTargetMode::MinimalFreshScaffold);
    ASSERT_GE(bundle.blocks.size(), 2U);

    bool saw_dng_exif = false;
    for (size_t i = 0; i < bundle.blocks.size(); ++i) {
        if (bundle.blocks[i].route != "tiff:ifd-exif-app1") {
            continue;
        }
        const std::array<std::byte, 12> needle = {
            std::byte { 0x12 }, std::byte { 0xC6 }, std::byte { 0x01 },
            std::byte { 0x00 }, std::byte { 0x04 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x01 },
            std::byte { 0x06 }, std::byte { 0x00 }, std::byte { 0x00 },
        };
        const auto it = std::search(bundle.blocks[i].payload.begin(),
                                    bundle.blocks[i].payload.end(),
                                    needle.begin(), needle.end());
        if (it != bundle.blocks[i].payload.end()) {
            saw_dng_exif = true;
            break;
        }
    }
    EXPECT_TRUE(saw_dng_exif);
}

TEST(MetadataTransferApi, PrepareBuildsDngBundleWithTemplateTargetMode)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(store.arena(),
                                    "OpenMeta DNG Template",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Dng;
    request.dng_target_mode    = openmeta::DngTargetMode::TemplateTarget;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.target_format, openmeta::TransferTargetFormat::Dng);
    EXPECT_EQ(bundle.dng_target_mode,
              openmeta::DngTargetMode::TemplateTarget);
}

TEST(MetadataTransferApi, PrepareBuildsTiffExifTransferBlockWithSubIfds)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry ifd0;
    ifd0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    ifd0.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    ifd0.origin.block          = block;
    ifd0.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(ifd0), openmeta::kInvalidEntryId);

    openmeta::Entry sub0;
    sub0.key = openmeta::make_exif_tag_key(store.arena(), "subifd0", 0x0100U);
    sub0.value = openmeta::make_u32(4000U);
    sub0.origin.block          = block;
    sub0.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(sub0), openmeta::kInvalidEntryId);

    openmeta::Entry sub1;
    sub1.key = openmeta::make_exif_tag_key(store.arena(), "subifd1", 0x0101U);
    sub1.value = openmeta::make_u32(3000U);
    sub1.origin.block          = block;
    sub1.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(sub1), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "tiff:ifd-exif-app1");

    uint32_t subifd_count = 0U;
    uint32_t subifd_value_or_offset = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        6U + read_u32le(std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                                   bundle.blocks[0].payload.size()),
                        10U),
        0x014AU, nullptr, &subifd_count, &subifd_value_or_offset));
    EXPECT_EQ(subifd_count, 2U);
    EXPECT_NE(subifd_value_or_offset, 0U);
}

TEST(MetadataTransferApi, PrepareBuildsTiffExifTransferBlockWithIfd1Chain)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry ifd0;
    ifd0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    ifd0.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    ifd0.origin.block          = block;
    ifd0.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(ifd0), openmeta::kInvalidEntryId);

    openmeta::Entry ifd1_width;
    ifd1_width.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd1", 0x0100U);
    ifd1_width.value             = openmeta::make_u32(320U);
    ifd1_width.origin.block      = block;
    ifd1_width.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(ifd1_width), openmeta::kInvalidEntryId);

    openmeta::Entry ifd1_height;
    ifd1_height.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd1", 0x0101U);
    ifd1_height.value             = openmeta::make_u32(240U);
    ifd1_height.origin.block      = block;
    ifd1_height.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(ifd1_height), openmeta::kInvalidEntryId);

    openmeta::Entry ifd2_width;
    ifd2_width.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd2", 0x0100U);
    ifd2_width.value             = openmeta::make_u32(160U);
    ifd2_width.origin.block      = block;
    ifd2_width.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(ifd2_width), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    ASSERT_EQ(bundle.blocks[0].route, "tiff:ifd-exif-app1");

    const std::span<const std::byte> payload(bundle.blocks[0].payload.data(),
                                             bundle.blocks[0].payload.size());
    const uint32_t ifd0_off = 6U + read_u32le(payload, 10U);
    const uint16_t ifd0_count = read_u16le(payload, static_cast<size_t>(ifd0_off));
    const uint32_t ifd1_off = read_u32le(payload,
                                         static_cast<size_t>(ifd0_off)
                                             + 2U
                                             + static_cast<size_t>(ifd0_count)
                                                   * 12U);
    EXPECT_NE(ifd1_off, 0U);

    uint32_t ifd1_width_value = 0U;
    uint32_t ifd1_height_value = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(payload, 6U + ifd1_off, 0x0100U,
                                       nullptr, nullptr, &ifd1_width_value));
    ASSERT_TRUE(find_tiff_tag_entry_le(payload, 6U + ifd1_off, 0x0101U,
                                       nullptr, nullptr, &ifd1_height_value));
    EXPECT_EQ(ifd1_width_value, 320U);
    EXPECT_EQ(ifd1_height_value, 240U);

    const uint16_t ifd1_count
        = read_u16le(payload, static_cast<size_t>(6U + ifd1_off));
    const uint32_t ifd2_off = read_u32le(
        payload, static_cast<size_t>(6U + ifd1_off) + 2U
                     + static_cast<size_t>(ifd1_count) * 12U);
    EXPECT_NE(ifd2_off, 0U);

    uint32_t ifd2_width_value = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(payload, 6U + ifd2_off, 0x0100U,
                                       nullptr, nullptr, &ifd2_width_value));
    EXPECT_EQ(ifd2_width_value, 160U);
}

TEST(MetadataTransferApi, PrepareBuildsTimePatchSlotsForExifDateFields)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e0;
    e0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e0.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                   openmeta::TextEncoding::Ascii);
    e0.origin.block          = block;
    e0.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e0), openmeta::kInvalidEntryId);

    openmeta::Entry e1;
    e1.key   = openmeta::make_exif_tag_key(store.arena(), "exififd", 0x9003U);
    e1.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                   openmeta::TextEncoding::Ascii);
    e1.origin.block          = block;
    e1.origin.order_in_block = 1;
    ASSERT_NE(store.add_entry(e1), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    bool found_ifd0_dt   = false;
    bool found_exififd_o = false;
    for (const openmeta::TimePatchSlot& slot : bundle.time_patch_map) {
        if (slot.field == openmeta::TimePatchField::DateTime) {
            found_ifd0_dt = true;
            EXPECT_EQ(slot.block_index, 0U);
            EXPECT_EQ(slot.width, 20U);
        }
        if (slot.field == openmeta::TimePatchField::DateTimeOriginal) {
            found_exififd_o = true;
            EXPECT_EQ(slot.block_index, 0U);
            EXPECT_EQ(slot.width, 20U);
        }
    }
    EXPECT_TRUE(found_ifd0_dt);
    EXPECT_TRUE(found_exififd_o);
}

TEST(MetadataTransferApi, ApplyTimePatchesUpdatesPreparedPayload)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e0;
    e0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e0.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                   openmeta::TextEncoding::Ascii);
    e0.origin.block          = block;
    e0.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e0), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    openmeta::TimePatchSlot dt_slot;
    bool have_slot = false;
    for (const openmeta::TimePatchSlot& slot : bundle.time_patch_map) {
        if (slot.field == openmeta::TimePatchField::DateTime) {
            dt_slot   = slot;
            have_slot = true;
            break;
        }
    }
    ASSERT_TRUE(have_slot);
    ASSERT_EQ(dt_slot.width, 20U);

    const std::vector<std::byte> v = ascii_z("2030:12:31 23:59:59");
    ASSERT_EQ(v.size(), 20U);
    openmeta::TimePatchUpdate u;
    u.field = openmeta::TimePatchField::DateTime;
    u.value = v;
    const std::array<openmeta::TimePatchUpdate, 1> updates = { u };

    const openmeta::ApplyTimePatchResult patch_result
        = openmeta::apply_time_patches(&bundle, updates);
    EXPECT_EQ(patch_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(patch_result.errors, 0U);
    EXPECT_GE(patch_result.patched_slots, 1U);

    const std::vector<std::byte>& payload
        = bundle.blocks[dt_slot.block_index].payload;
    ASSERT_LE(static_cast<size_t>(dt_slot.byte_offset + dt_slot.width),
              payload.size());
    EXPECT_EQ(std::memcmp(payload.data() + dt_slot.byte_offset, v.data(),
                          v.size()),
              0);
}

TEST(MetadataTransferApi, ApplyTimePatchesViewUpdatesPreparedPayload)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = ascii_z("2024:01:02 03:04:05");
    bundle.blocks.push_back(exif);

    openmeta::TimePatchSlot slot;
    slot.field       = openmeta::TimePatchField::DateTime;
    slot.block_index = 0U;
    slot.byte_offset = 0U;
    slot.width       = 20U;
    bundle.time_patch_map.push_back(slot);

    const std::vector<std::byte> patched = ascii_z("2030:12:31 23:59:59");
    openmeta::TimePatchView view;
    view.field = openmeta::TimePatchField::DateTime;
    view.value = std::span<const std::byte>(patched.data(), patched.size());

    const std::array<openmeta::TimePatchView, 1> updates = { view };
    const openmeta::ApplyTimePatchResult result
        = openmeta::apply_time_patches_view(&bundle, updates);
    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(result.patched_slots, 1U);
    EXPECT_EQ(bundle.blocks[0].payload, patched);
}

TEST(MetadataTransferApi, ApplyTimePatchesStrictWidthMismatchFails)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e0;
    e0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e0.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                   openmeta::TextEncoding::Ascii);
    e0.origin.block          = block;
    e0.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e0), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(result.status, openmeta::TransferStatus::Ok);

    openmeta::TimePatchUpdate u;
    u.field = openmeta::TimePatchField::DateTime;
    u.value = ascii_z("2030:12:31 23:59");  // too short
    const std::array<openmeta::TimePatchUpdate, 1> updates = { u };

    openmeta::ApplyTimePatchOptions patch_opts;
    patch_opts.strict_width = true;
    const openmeta::ApplyTimePatchResult patch_result
        = openmeta::apply_time_patches(&bundle, updates, patch_opts);
    EXPECT_EQ(patch_result.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_GT(patch_result.errors, 0U);
    EXPECT_EQ(patch_result.patched_slots, 0U);
}

TEST(MetadataTransferApi, PlanJpegEditAutoFallsBackToMetadataRewrite)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    const std::vector<std::byte> input = {
        std::byte { 0xFF },
        std::byte { 0xD8 },
        std::byte { 0xFF },
        std::byte { 0xD9 },
    };
    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    EXPECT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(plan.selected_mode, openmeta::JpegEditMode::MetadataRewrite);
    EXPECT_FALSE(plan.in_place_possible);
    EXPECT_EQ(plan.removed_existing_segments, 0U);
    EXPECT_EQ(plan.removed_existing_jumbf_segments, 0U);
    EXPECT_EQ(plan.removed_existing_c2pa_segments, 0U);
    EXPECT_EQ(plan.emitted_segments, 1U);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::Ok);
    ASSERT_GT(out.size(), input.size());
    EXPECT_EQ(out[0], std::byte { 0xFF });
    EXPECT_EQ(out[1], std::byte { 0xD8 });
    EXPECT_EQ(out[2], std::byte { 0xFF });
    EXPECT_EQ(out[3], std::byte { 0xE1 });
}

TEST(MetadataTransferApi, PlanJpegEditAutoSelectsInPlaceWhenPossible)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    ASSERT_EQ(bundle.blocks[0].route, "jpeg:app1-exif");

    const std::vector<std::byte> input = make_jpeg_with_segment(
        0xE1U, std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                          bundle.blocks[0].payload.size()));

    openmeta::TimePatchUpdate u;
    u.field = openmeta::TimePatchField::DateTime;
    u.value = ascii_z("2033:03:04 05:06:07");
    const std::array<openmeta::TimePatchUpdate, 1> updates = { u };
    const openmeta::ApplyTimePatchResult patched
        = openmeta::apply_time_patches(&bundle, updates);
    ASSERT_EQ(patched.status, openmeta::TransferStatus::Ok);

    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    EXPECT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(plan.selected_mode, openmeta::JpegEditMode::InPlace);
    EXPECT_TRUE(plan.in_place_possible);
    EXPECT_EQ(plan.output_size, input.size());

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(out.size(), input.size());
    EXPECT_NE(out, input);

    const std::vector<std::byte> needle = ascii_z("2033:03:04 05:06:07");
    const auto it = std::search(out.begin(), out.end(), needle.begin(),
                                needle.end());
    EXPECT_NE(it, out.end());
}

TEST(MetadataTransferApi, JpegEditDropsExistingC2paOnContentChange)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    ASSERT_EQ(bundle.blocks[0].route, "jpeg:app1-exif");

    openmeta::TimePatchUpdate update;
    update.field = openmeta::TimePatchField::DateTime;
    update.value = ascii_z("2033:03:04 05:06:07");
    const std::array<openmeta::TimePatchUpdate, 1> updates = { update };
    ASSERT_EQ(openmeta::apply_time_patches(&bundle, updates).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> c2pa = make_app11_jumbf_payload("c2pa");
    const std::array<TestJpegSegment, 2> segments = {
        TestJpegSegment {
            0xE1U,
            std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                       bundle.blocks[0].payload.size()),
        },
        TestJpegSegment {
            0xEBU,
            std::span<const std::byte>(c2pa.data(), c2pa.size()),
        },
    };
    const std::vector<std::byte> input = make_jpeg_with_segments(segments);

    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    EXPECT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(plan.selected_mode, openmeta::JpegEditMode::MetadataRewrite);
    EXPECT_FALSE(plan.in_place_possible);
    EXPECT_EQ(plan.removed_existing_segments, 2U);
    EXPECT_EQ(plan.removed_existing_jumbf_segments, 0U);
    EXPECT_EQ(plan.removed_existing_c2pa_segments, 1U);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::array<std::byte, 4> jp_magic = {
        std::byte { 'J' },
        std::byte { 'P' },
        std::byte { 0x00 },
        std::byte { 0x00 },
    };
    const auto it = std::search(out.begin(), out.end(), jp_magic.begin(),
                                jp_magic.end());
    EXPECT_EQ(it, out.end());
}

TEST(MetadataTransferApi,
     JpegEditReplacesExistingC2paWithDraftInvalidationPayload)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.include_exif_app1  = false;
    options.prepare.include_xmp_app1   = false;
    options.prepare.include_icc_app2   = false;
    options.prepare.include_iptc_app13 = false;
    options.prepare.profile.c2pa = openmeta::TransferPolicyAction::Invalidate;

    openmeta::PrepareTransferFileResult prepared
        = openmeta::prepare_metadata_for_target_file(path.c_str(), options);
    std::remove(path.c_str());

    ASSERT_EQ(prepared.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(prepared.prepare.status, openmeta::TransferStatus::Ok);

    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(
        std::span<const std::byte>(jpeg.data(), jpeg.size()), prepared.bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(plan.removed_existing_c2pa_segments, 1U);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(jpeg.data(), jpeg.size()),
            prepared.bundle, plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::Ok);
    EXPECT_TRUE(payload_contains_ascii(std::span<const std::byte>(out.data(),
                                                                  out.size()),
                                       "openmeta:c2pa_invalidation"));
}

TEST(MetadataTransferApi, JpegEditDropsExistingJumbfWhenPolicyDropsIt)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.kind    = openmeta::TransferBlockKind::Exif;
    exif.order   = 10U;
    exif.route   = "jpeg:app1-exif";
    exif.payload = ascii_z("ExifTransferPayload");
    bundle.blocks.push_back(std::move(exif));

    openmeta::PreparedTransferPolicyDecision decision;
    decision.subject         = openmeta::TransferPolicySubject::Jumbf;
    decision.requested       = openmeta::TransferPolicyAction::Drop;
    decision.effective       = openmeta::TransferPolicyAction::Drop;
    decision.reason          = openmeta::TransferPolicyReason::ExplicitDrop;
    decision.matched_entries = 1U;
    decision.message         = "jumbf transfer disabled by profile";
    bundle.policy_decisions.push_back(std::move(decision));

    const std::vector<std::byte> jumbf = make_app11_jumbf_payload("acme");
    const std::vector<std::byte> input = make_jpeg_with_segment(
        0xEBU, std::span<const std::byte>(jumbf.data(), jumbf.size()));

    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    EXPECT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(plan.selected_mode, openmeta::JpegEditMode::MetadataRewrite);
    EXPECT_FALSE(plan.in_place_possible);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::array<std::byte, 4> jp_magic = {
        std::byte { 'J' },
        std::byte { 'P' },
        std::byte { 0x00 },
        std::byte { 0x00 },
    };
    const auto it = std::search(out.begin(), out.end(), jp_magic.begin(),
                                jp_magic.end());
    EXPECT_EQ(it, out.end());
}

TEST(MetadataTransferApi, JpegEditCanReplaceExistingJumbfInPlace)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    std::vector<std::byte> new_payload = make_app11_jumbf_payload("beta");
    openmeta::PreparedTransferBlock jumbf;
    jumbf.kind    = openmeta::TransferBlockKind::Jumbf;
    jumbf.order   = 10U;
    jumbf.route   = "jpeg:app11-jumbf";
    jumbf.payload = std::move(new_payload);
    bundle.blocks.push_back(std::move(jumbf));

    const std::vector<std::byte> input = make_jpeg_with_app11_jumbf("acme");

    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    EXPECT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(plan.selected_mode, openmeta::JpegEditMode::InPlace);
    EXPECT_TRUE(plan.in_place_possible);
    EXPECT_EQ(plan.removed_existing_segments, 0U);
    EXPECT_EQ(plan.removed_existing_jumbf_segments, 0U);
    EXPECT_EQ(plan.removed_existing_c2pa_segments, 0U);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(out.size(), input.size());
    EXPECT_NE(out, input);

    const std::vector<std::byte> new_label = ascii_z("beta");
    const std::vector<std::byte> old_label = ascii_z("acme");
    EXPECT_NE(std::search(out.begin(), out.end(), new_label.begin(),
                          new_label.end()),
              out.end());
    EXPECT_EQ(std::search(out.begin(), out.end(), old_label.begin(),
                          old_label.end()),
              out.end());
}

TEST(MetadataTransferApi, PlanAndApplyTiffEditMetadataRewrite)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "title");
    xmp.value                 = openmeta::make_text(store.arena(), "OpenMeta",
                                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1;
    ASSERT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format        = openmeta::TransferTargetFormat::Tiff;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.include_xmp_app1     = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    EXPECT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_GE(plan.tag_updates, 1U);
    EXPECT_GT(plan.output_size, plan.input_size);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::Ok);
    ASSERT_GT(out.size(), input.size());
    EXPECT_EQ(out[0], std::byte { 'I' });
    EXPECT_EQ(out[1], std::byte { 'I' });
    EXPECT_EQ(read_u16le(std::span<const std::byte>(out.data(), out.size()), 2U),
              42U);
    EXPECT_GT(read_u32le(std::span<const std::byte>(out.data(), out.size()), 4U),
              8U);
}

TEST(MetadataTransferApi, ExecutePreparedTransferTiffEditRoundTripsSubIfds)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry ifd0;
    ifd0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    ifd0.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    ifd0.origin.block          = block;
    ifd0.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(ifd0), openmeta::kInvalidEntryId);

    openmeta::Entry sub0;
    sub0.key = openmeta::make_exif_tag_key(store.arena(), "subifd0", 0x0100U);
    sub0.value = openmeta::make_u32(4000U);
    sub0.origin.block          = block;
    sub0.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(sub0), openmeta::kInvalidEntryId);

    openmeta::Entry sub1;
    sub1.key = openmeta::make_exif_tag_key(store.arena(), "subifd1", 0x0101U);
    sub1.value = openmeta::make_u32(3000U);
    sub1.origin.block          = block;
    sub1.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(sub1), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(out.data(), out.size()), &decoded));
    EXPECT_TRUE(store_has_text_entry(decoded, exif_key_view("ifd0", 0x0132U),
                                     "2024:01:02 03:04:05"));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0100U),
                                           4000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd1", 0x0101U),
                                           3000U));
}

TEST(MetadataTransferApi, ExecutePreparedTransferTiffEditRoundTripsIfdChain)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry ifd0;
    ifd0.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    ifd0.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    ifd0.origin.block          = block;
    ifd0.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(ifd0), openmeta::kInvalidEntryId);

    openmeta::Entry ifd1_width;
    ifd1_width.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd1", 0x0100U);
    ifd1_width.value             = openmeta::make_u32(320U);
    ifd1_width.origin.block      = block;
    ifd1_width.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(ifd1_width), openmeta::kInvalidEntryId);

    openmeta::Entry ifd1_height;
    ifd1_height.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd1", 0x0101U);
    ifd1_height.value             = openmeta::make_u32(240U);
    ifd1_height.origin.block      = block;
    ifd1_height.origin.order_in_block = 2U;
    ASSERT_NE(store.add_entry(ifd1_height), openmeta::kInvalidEntryId);

    openmeta::Entry ifd2_width;
    ifd2_width.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd2", 0x0100U);
    ifd2_width.value             = openmeta::make_u32(160U);
    ifd2_width.origin.block      = block;
    ifd2_width.origin.order_in_block = 3U;
    ASSERT_NE(store.add_entry(ifd2_width), openmeta::kInvalidEntryId);

    openmeta::Entry ifd2_height;
    ifd2_height.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd2", 0x0101U);
    ifd2_height.value             = openmeta::make_u32(120U);
    ifd2_height.origin.block      = block;
    ifd2_height.origin.order_in_block = 4U;
    ASSERT_NE(store.add_entry(ifd2_height), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(out.data(), out.size()), &decoded));
    EXPECT_TRUE(store_has_text_entry(decoded, exif_key_view("ifd0", 0x0132U),
                                     "2024:01:02 03:04:05"));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0100U),
                                           320U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0101U),
                                           240U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd2", 0x0100U),
                                           160U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd2", 0x0101U),
                                           120U));
}

TEST(MetadataTransferApi, ExecutePreparedTransferTiffEditPreservesIfd1TailChain)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry ifd1_width;
    ifd1_width.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd1", 0x0100U);
    ifd1_width.value             = openmeta::make_u32(320U);
    ifd1_width.origin.block      = block;
    ifd1_width.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(ifd1_width), openmeta::kInvalidEntryId);

    openmeta::Entry ifd1_height;
    ifd1_height.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd1", 0x0101U);
    ifd1_height.value             = openmeta::make_u32(240U);
    ifd1_height.origin.block      = block;
    ifd1_height.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(ifd1_height), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_multipage_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> bytes(out.data(), out.size());
    const uint32_t ifd0_off = read_u32le(bytes, 4U);
    const uint16_t ifd0_count = read_u16le(bytes, static_cast<size_t>(ifd0_off));
    const uint32_t ifd1_off = read_u32le(
        bytes, static_cast<size_t>(ifd0_off) + 2U + static_cast<size_t>(ifd0_count) * 12U);
    ASSERT_NE(ifd1_off, 0U);
    EXPECT_GE(ifd1_off, static_cast<uint32_t>(input.size()));

    uint32_t width_value = 0U;
    uint32_t height_value = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd1_off, 0x0100U, nullptr,
                                       nullptr, &width_value));
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd1_off, 0x0101U, nullptr,
                                       nullptr, &height_value));
    EXPECT_EQ(width_value, 320U);
    EXPECT_EQ(height_value, 240U);

    const uint16_t ifd1_count = read_u16le(bytes, static_cast<size_t>(ifd1_off));
    const uint32_t ifd1_next_off = read_u32le(
        bytes, static_cast<size_t>(ifd1_off) + 2U + static_cast<size_t>(ifd1_count) * 12U);
    EXPECT_GE(ifd1_next_off, static_cast<uint32_t>(input.size()));

    uint32_t tail_value = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd1_next_off, 0x0100U, nullptr, nullptr,
                                       &tail_value));
    EXPECT_EQ(tail_value, 222U);
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferTiffEditPreservesIfdChainTail)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry ifd1_width;
    ifd1_width.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd1", 0x0100U);
    ifd1_width.value             = openmeta::make_u32(320U);
    ifd1_width.origin.block      = block;
    ifd1_width.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(ifd1_width), openmeta::kInvalidEntryId);

    openmeta::Entry ifd2_width;
    ifd2_width.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd2", 0x0100U);
    ifd2_width.value             = openmeta::make_u32(160U);
    ifd2_width.origin.block      = block;
    ifd2_width.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(ifd2_width), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_threepage_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> bytes(out.data(), out.size());
    const uint32_t ifd0_off = read_u32le(bytes, 4U);
    const uint16_t ifd0_count = read_u16le(bytes, static_cast<size_t>(ifd0_off));
    const uint32_t ifd1_off = read_u32le(
        bytes, static_cast<size_t>(ifd0_off) + 2U
                   + static_cast<size_t>(ifd0_count) * 12U);
    ASSERT_NE(ifd1_off, 0U);
    EXPECT_GE(ifd1_off, static_cast<uint32_t>(input.size()));

    const uint16_t ifd1_count = read_u16le(bytes, static_cast<size_t>(ifd1_off));
    const uint32_t ifd2_off = read_u32le(
        bytes, static_cast<size_t>(ifd1_off) + 2U
                   + static_cast<size_t>(ifd1_count) * 12U);
    ASSERT_NE(ifd2_off, 0U);
    EXPECT_GE(ifd2_off, static_cast<uint32_t>(input.size()));

    uint32_t ifd1_width_value = 0U;
    uint32_t ifd2_width_value = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd1_off, 0x0100U, nullptr,
                                       nullptr, &ifd1_width_value));
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd2_off, 0x0100U, nullptr,
                                       nullptr, &ifd2_width_value));
    EXPECT_EQ(ifd1_width_value, 320U);
    EXPECT_EQ(ifd2_width_value, 160U);

    const uint16_t ifd2_count = read_u16le(bytes, static_cast<size_t>(ifd2_off));
    const uint32_t ifd2_next_off = read_u32le(
        bytes, static_cast<size_t>(ifd2_off) + 2U
                   + static_cast<size_t>(ifd2_count) * 12U);
    EXPECT_GE(ifd2_next_off, static_cast<uint32_t>(input.size()));

    uint32_t tail_value = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd2_next_off, 0x0100U, nullptr,
                                       nullptr,
                                       &tail_value));
    EXPECT_EQ(tail_value, 333U);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(bytes, &decoded));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0100U),
                                           320U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd2", 0x0100U),
                                           160U));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferTiffEditPreservesNestedPointerEntriesOnReplacedIfd1)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry ifd1_width;
    ifd1_width.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd1", 0x0100U);
    ifd1_width.value             = openmeta::make_u32(320U);
    ifd1_width.origin.block      = block;
    ifd1_width.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(ifd1_width), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input
        = make_minimal_ifd1_nested_subifd_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> bytes(out.data(), out.size());
    const uint32_t ifd0_off = read_u32le(bytes, 4U);
    const uint16_t ifd0_count = read_u16le(bytes, static_cast<size_t>(ifd0_off));
    const uint32_t ifd1_off = read_u32le(
        bytes, static_cast<size_t>(ifd0_off) + 2U
                   + static_cast<size_t>(ifd0_count) * 12U);
    ASSERT_NE(ifd1_off, 0U);

    uint32_t replaced_width = 0U;
    uint32_t nested_count = 0U;
    uint32_t nested_subifd_off = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd1_off, 0x0100U, nullptr,
                                       nullptr, &replaced_width));
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd1_off, 0x014AU, nullptr,
                                       &nested_count, &nested_subifd_off));
    EXPECT_EQ(replaced_width, 320U);
    EXPECT_EQ(nested_count, 1U);

    uint32_t nested_width = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, nested_subifd_off, 0x0100U,
                                       nullptr, nullptr, &nested_width));
    EXPECT_EQ(nested_width, 444U);
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferTiffEditPreservesExistingInteropOnReplacedExifIfd)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif_dt;
    exif_dt.key
        = openmeta::make_exif_tag_key(store.arena(), "exififd", 0x9003U);
    exif_dt.value = openmeta::make_text(store.arena(),
                                        "2024:01:02 03:04:05",
                                        openmeta::TextEncoding::Ascii);
    exif_dt.origin.block          = block;
    exif_dt.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(exif_dt), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input
        = make_minimal_exififd_nested_interop_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> bytes(out.data(), out.size());
    const uint32_t ifd0_off = read_u32le(bytes, 4U);
    uint32_t exif_ifd_off = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd0_off, 0x8769U, nullptr,
                                       nullptr, &exif_ifd_off));
    EXPECT_GE(exif_ifd_off, static_cast<uint32_t>(input.size()));

    uint32_t interop_ifd_off = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, exif_ifd_off, 0xA005U, nullptr,
                                       nullptr, &interop_ifd_off));
    EXPECT_EQ(interop_ifd_off, 56U);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(bytes, &decoded));
    EXPECT_TRUE(store_has_text_entry(decoded,
                                     exif_key_view("exififd", 0x9003U),
                                     "2024:01:02 03:04:05"));
    EXPECT_TRUE(store_has_text_entry(decoded,
                                     exif_key_view("interopifd", 0x0001U),
                                     "R98"));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferTiffEditPreservesSubifdTailChain)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry subifd0_width;
    subifd0_width.key
        = openmeta::make_exif_tag_key(store.arena(), "subifd0", 0x0100U);
    subifd0_width.value             = openmeta::make_u32(320U);
    subifd0_width.origin.block      = block;
    subifd0_width.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(subifd0_width), openmeta::kInvalidEntryId);

    openmeta::Entry subifd0_height;
    subifd0_height.key
        = openmeta::make_exif_tag_key(store.arena(), "subifd0", 0x0101U);
    subifd0_height.value             = openmeta::make_u32(240U);
    subifd0_height.origin.block      = block;
    subifd0_height.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(subifd0_height), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_subifd_chain_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> bytes(out.data(), out.size());
    const uint32_t ifd0_off = read_u32le(bytes, 4U);
    uint32_t subifd_count = 0U;
    uint32_t subifd0_off = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd0_off, 0x014AU, nullptr,
                                       &subifd_count, &subifd0_off));
    EXPECT_EQ(subifd_count, 1U);
    EXPECT_GE(subifd0_off, static_cast<uint32_t>(input.size()));

    uint32_t width_value = 0U;
    uint32_t height_value = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, subifd0_off, 0x0100U, nullptr,
                                       nullptr, &width_value));
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, subifd0_off, 0x0101U, nullptr,
                                       nullptr, &height_value));
    EXPECT_EQ(width_value, 320U);
    EXPECT_EQ(height_value, 240U);

    const uint16_t subifd0_count = read_u16le(bytes, static_cast<size_t>(subifd0_off));
    const uint32_t subifd0_next_off = read_u32le(
        bytes, static_cast<size_t>(subifd0_off) + 2U
                   + static_cast<size_t>(subifd0_count) * 12U);
    EXPECT_EQ(subifd0_next_off, 44U);

    uint32_t tail_value = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, 44U, 0x0100U, nullptr, nullptr,
                                       &tail_value));
    EXPECT_EQ(tail_value, 222U);
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferTiffEditPreservesExtraExistingSubifdChildren)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry subifd0_width;
    subifd0_width.key
        = openmeta::make_exif_tag_key(store.arena(), "subifd0", 0x0100U);
    subifd0_width.value             = openmeta::make_u32(320U);
    subifd0_width.origin.block      = block;
    subifd0_width.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(subifd0_width), openmeta::kInvalidEntryId);

    openmeta::Entry subifd0_height;
    subifd0_height.key
        = openmeta::make_exif_tag_key(store.arena(), "subifd0", 0x0101U);
    subifd0_height.value             = openmeta::make_u32(240U);
    subifd0_height.origin.block      = block;
    subifd0_height.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(subifd0_height), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_subifd_pair_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> bytes(out.data(), out.size());
    const uint32_t ifd0_off = read_u32le(bytes, 4U);
    uint32_t subifd_count = 0U;
    uint32_t subifd_array_off = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd0_off, 0x014AU, nullptr,
                                       &subifd_count, &subifd_array_off));
    EXPECT_EQ(subifd_count, 2U);
    EXPECT_GE(subifd_array_off, static_cast<uint32_t>(input.size()));

    const uint32_t subifd0_off = read_u32le(bytes, subifd_array_off + 0U);
    const uint32_t subifd1_off = read_u32le(bytes, subifd_array_off + 4U);
    EXPECT_GE(subifd0_off, static_cast<uint32_t>(input.size()));
    EXPECT_GE(subifd1_off, static_cast<uint32_t>(input.size()));

    uint32_t subifd1_width = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, subifd1_off, 0x0100U, nullptr,
                                       nullptr, &subifd1_width));
    EXPECT_EQ(subifd1_width, 777U);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(bytes, &decoded));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0100U),
                                           320U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0101U),
                                           240U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd1", 0x0100U),
                                           777U));
}

TEST(MetadataTransferApi, PlanAndApplyBigTiffEditMetadataRewrite)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "title");
    xmp.value                 = openmeta::make_text(store.arena(), "OpenMeta",
                                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1;
    ASSERT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format        = openmeta::TransferTargetFormat::Tiff;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.include_xmp_app1     = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_bigtiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    EXPECT_EQ(plan.status, openmeta::TransferStatus::Ok);
    EXPECT_GE(plan.tag_updates, 1U);
    EXPECT_GT(plan.output_size, plan.input_size);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::Ok);
    ASSERT_GT(out.size(), input.size());
    EXPECT_EQ(out[0], std::byte { 'I' });
    EXPECT_EQ(out[1], std::byte { 'I' });
    EXPECT_EQ(read_u16le(std::span<const std::byte>(out.data(), out.size()), 2U),
              43U);
    EXPECT_EQ(read_u16le(std::span<const std::byte>(out.data(), out.size()), 4U),
              8U);
    EXPECT_EQ(read_u16le(std::span<const std::byte>(out.data(), out.size()), 6U),
              0U);
    EXPECT_GT(read_u64le(std::span<const std::byte>(out.data(), out.size()), 8U),
              16U);
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferBigTiffEditPreservesIfd1TailChain)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry ifd1_width;
    ifd1_width.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd1", 0x0100U);
    ifd1_width.value             = openmeta::make_u32(320U);
    ifd1_width.origin.block      = block;
    ifd1_width.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(ifd1_width), openmeta::kInvalidEntryId);

    openmeta::Entry ifd1_height;
    ifd1_height.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd1", 0x0101U);
    ifd1_height.value             = openmeta::make_u32(240U);
    ifd1_height.origin.block      = block;
    ifd1_height.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(ifd1_height), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input
        = make_minimal_multipage_bigtiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> bytes(out.data(), out.size());
    const uint64_t ifd0_off = read_u64le(bytes, 8U);
    const uint64_t ifd0_count = read_u64le(bytes, static_cast<size_t>(ifd0_off));
    const uint64_t ifd1_off = read_u64le(
        bytes, static_cast<size_t>(ifd0_off) + 8U + static_cast<size_t>(ifd0_count) * 20U);
    ASSERT_NE(ifd1_off, 0U);
    EXPECT_GE(ifd1_off, static_cast<uint64_t>(input.size()));

    uint64_t width_value = 0U;
    uint64_t height_value = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd1_off, 0x0100U, nullptr,
                                          nullptr, &width_value));
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd1_off, 0x0101U, nullptr,
                                          nullptr, &height_value));
    EXPECT_EQ(width_value, 320U);
    EXPECT_EQ(height_value, 240U);

    const uint64_t ifd1_count = read_u64le(bytes, static_cast<size_t>(ifd1_off));
    const uint64_t ifd1_next_off = read_u64le(
        bytes, static_cast<size_t>(ifd1_off) + 8U + static_cast<size_t>(ifd1_count) * 20U);
    EXPECT_GE(ifd1_next_off, static_cast<uint64_t>(input.size()));

    uint64_t tail_value = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd1_next_off, 0x0100U, nullptr,
                                          nullptr, &tail_value));
    EXPECT_EQ(tail_value, 222U);
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferBigTiffEditPreservesIfdChainTail)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry ifd1_width;
    ifd1_width.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd1", 0x0100U);
    ifd1_width.value             = openmeta::make_u32(320U);
    ifd1_width.origin.block      = block;
    ifd1_width.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(ifd1_width), openmeta::kInvalidEntryId);

    openmeta::Entry ifd2_width;
    ifd2_width.key
        = openmeta::make_exif_tag_key(store.arena(), "ifd2", 0x0100U);
    ifd2_width.value             = openmeta::make_u32(160U);
    ifd2_width.origin.block      = block;
    ifd2_width.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(ifd2_width), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input
        = make_minimal_threepage_bigtiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> bytes(out.data(), out.size());
    const uint64_t ifd0_off = read_u64le(bytes, 8U);
    const uint64_t ifd0_count = read_u64le(bytes, static_cast<size_t>(ifd0_off));
    const uint64_t ifd1_off = read_u64le(
        bytes, static_cast<size_t>(ifd0_off) + 8U
                   + static_cast<size_t>(ifd0_count) * 20U);
    ASSERT_NE(ifd1_off, 0U);
    EXPECT_GE(ifd1_off, static_cast<uint64_t>(input.size()));

    const uint64_t ifd1_count = read_u64le(bytes, static_cast<size_t>(ifd1_off));
    const uint64_t ifd2_off = read_u64le(
        bytes, static_cast<size_t>(ifd1_off) + 8U
                   + static_cast<size_t>(ifd1_count) * 20U);
    ASSERT_NE(ifd2_off, 0U);
    EXPECT_GE(ifd2_off, static_cast<uint64_t>(input.size()));

    uint64_t ifd1_width_value = 0U;
    uint64_t ifd2_width_value = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd1_off, 0x0100U, nullptr,
                                          nullptr, &ifd1_width_value));
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd2_off, 0x0100U, nullptr,
                                          nullptr, &ifd2_width_value));
    EXPECT_EQ(ifd1_width_value, 320U);
    EXPECT_EQ(ifd2_width_value, 160U);

    const uint64_t ifd2_count = read_u64le(bytes, static_cast<size_t>(ifd2_off));
    const uint64_t ifd2_next_off = read_u64le(
        bytes, static_cast<size_t>(ifd2_off) + 8U
                   + static_cast<size_t>(ifd2_count) * 20U);
    EXPECT_GE(ifd2_next_off, static_cast<uint64_t>(input.size()));

    uint64_t tail_value = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd2_next_off, 0x0100U, nullptr,
                                          nullptr, &tail_value));
    EXPECT_EQ(tail_value, 333U);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(bytes, &decoded));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0100U),
                                           320U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd2", 0x0100U),
                                           160U));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferBigTiffEditPreservesSubifdTailChain)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry subifd0_width;
    subifd0_width.key
        = openmeta::make_exif_tag_key(store.arena(), "subifd0", 0x0100U);
    subifd0_width.value             = openmeta::make_u32(320U);
    subifd0_width.origin.block      = block;
    subifd0_width.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(subifd0_width), openmeta::kInvalidEntryId);

    openmeta::Entry subifd0_height;
    subifd0_height.key
        = openmeta::make_exif_tag_key(store.arena(), "subifd0", 0x0101U);
    subifd0_height.value             = openmeta::make_u32(240U);
    subifd0_height.origin.block      = block;
    subifd0_height.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(subifd0_height), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input
        = make_minimal_subifd_chain_bigtiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> bytes(out.data(), out.size());
    const uint64_t ifd0_off = read_u64le(bytes, 8U);
    uint64_t subifd_count = 0U;
    uint64_t subifd0_off = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd0_off, 0x014AU, nullptr,
                                          &subifd_count, &subifd0_off));
    EXPECT_EQ(subifd_count, 1U);
    EXPECT_GE(subifd0_off, static_cast<uint64_t>(input.size()));

    uint64_t width_value = 0U;
    uint64_t height_value = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, subifd0_off, 0x0100U, nullptr,
                                          nullptr, &width_value));
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, subifd0_off, 0x0101U, nullptr,
                                          nullptr, &height_value));
    EXPECT_EQ(width_value, 320U);
    EXPECT_EQ(height_value, 240U);

    const uint64_t subifd0_count = read_u64le(bytes, static_cast<size_t>(subifd0_off));
    const uint64_t subifd0_next_off = read_u64le(
        bytes, static_cast<size_t>(subifd0_off) + 8U
                   + static_cast<size_t>(subifd0_count) * 20U);
    EXPECT_EQ(subifd0_next_off, 88U);

    uint64_t tail_value = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, 88U, 0x0100U, nullptr,
                                          nullptr, &tail_value));
    EXPECT_EQ(tail_value, 222U);
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferBigTiffEditPreservesExtraExistingSubifdChildren)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry subifd0_width;
    subifd0_width.key
        = openmeta::make_exif_tag_key(store.arena(), "subifd0", 0x0100U);
    subifd0_width.value             = openmeta::make_u32(320U);
    subifd0_width.origin.block      = block;
    subifd0_width.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(subifd0_width), openmeta::kInvalidEntryId);

    openmeta::Entry subifd0_height;
    subifd0_height.key
        = openmeta::make_exif_tag_key(store.arena(), "subifd0", 0x0101U);
    subifd0_height.value             = openmeta::make_u32(240U);
    subifd0_height.origin.block      = block;
    subifd0_height.origin.order_in_block = 1U;
    ASSERT_NE(store.add_entry(subifd0_height), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input
        = make_minimal_subifd_pair_bigtiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> bytes(out.data(), out.size());
    const uint64_t ifd0_off = read_u64le(bytes, 8U);
    uint64_t subifd_count = 0U;
    uint64_t subifd_array_off = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd0_off, 0x014AU, nullptr,
                                          &subifd_count, &subifd_array_off));
    EXPECT_EQ(subifd_count, 2U);
    EXPECT_GE(subifd_array_off, static_cast<uint64_t>(input.size()));

    const uint64_t subifd0_off = read_u64le(bytes, static_cast<size_t>(subifd_array_off) + 0U);
    const uint64_t subifd1_off = read_u64le(bytes, static_cast<size_t>(subifd_array_off) + 8U);
    EXPECT_GE(subifd0_off, static_cast<uint64_t>(input.size()));
    EXPECT_GE(subifd1_off, static_cast<uint64_t>(input.size()));

    uint64_t subifd1_width = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, subifd1_off, 0x0100U,
                                          nullptr, nullptr, &subifd1_width));
    EXPECT_EQ(subifd1_width, 777U);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(bytes, &decoded));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0100U),
                                           320U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0101U),
                                           240U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd1", 0x0100U),
                                           777U));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferBigTiffEditPreservesNestedPointerEntriesOnReplacedSubifd0)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry subifd0_width;
    subifd0_width.key
        = openmeta::make_exif_tag_key(store.arena(), "subifd0", 0x0100U);
    subifd0_width.value             = openmeta::make_u32(320U);
    subifd0_width.origin.block      = block;
    subifd0_width.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(subifd0_width), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input
        = make_minimal_subifd0_nested_subifd_bigtiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> bytes(out.data(), out.size());
    const uint64_t ifd0_off = read_u64le(bytes, 8U);
    uint64_t subifd_count = 0U;
    uint64_t subifd0_off = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd0_off, 0x014AU, nullptr,
                                          &subifd_count, &subifd0_off));
    EXPECT_EQ(subifd_count, 1U);

    uint64_t replaced_width = 0U;
    uint64_t nested_count = 0U;
    uint64_t nested_subifd_off = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, subifd0_off, 0x0100U, nullptr,
                                          nullptr, &replaced_width));
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, subifd0_off, 0x014AU, nullptr,
                                          &nested_count, &nested_subifd_off));
    EXPECT_EQ(replaced_width, 320U);
    EXPECT_EQ(nested_count, 1U);

    uint64_t nested_width = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, nested_subifd_off, 0x0100U,
                                          nullptr, nullptr, &nested_width));
    EXPECT_EQ(nested_width, 444U);
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferBigTiffEditPreservesExistingInteropOnReplacedExifIfd)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif_dt;
    exif_dt.key
        = openmeta::make_exif_tag_key(store.arena(), "exififd", 0x9003U);
    exif_dt.value = openmeta::make_text(store.arena(),
                                        "2024:01:02 03:04:05",
                                        openmeta::TextEncoding::Ascii);
    exif_dt.origin.block          = block;
    exif_dt.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(exif_dt), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input
        = make_minimal_exififd_nested_interop_bigtiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &out);
    ASSERT_EQ(applied.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> bytes(out.data(), out.size());
    const uint64_t ifd0_off = read_u64le(bytes, 8U);
    uint64_t exif_ifd_off = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd0_off, 0x8769U, nullptr,
                                          nullptr, &exif_ifd_off));
    EXPECT_GE(exif_ifd_off, static_cast<uint64_t>(input.size()));

    uint64_t interop_ifd_off = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, exif_ifd_off, 0xA005U,
                                          nullptr, nullptr,
                                          &interop_ifd_off));
    EXPECT_EQ(interop_ifd_off, 108U);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(bytes, &decoded));
    EXPECT_TRUE(store_has_text_entry(decoded,
                                     exif_key_view("exififd", 0x9003U),
                                     "2024:01:02 03:04:05"));
    EXPECT_TRUE(store_has_text_entry(decoded,
                                     exif_key_view("interopifd", 0x0001U),
                                     "R98"));
}

TEST(MetadataTransferApi, ExecutePreparedTransferJpegEditAndEmit)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    const std::vector<std::byte> input = make_jpeg_with_segment(
        0xE1U, std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                          bundle.blocks[0].payload.size()));

    openmeta::TransferTimePatchInput patch;
    patch.field      = openmeta::TimePatchField::DateTime;
    patch.value      = ascii_z("2033:03:04 05:06:07");
    patch.text_value = true;

    openmeta::ExecutePreparedTransferOptions options;
    options.time_patches.push_back(patch);
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.time_patch.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.compiled_ops, 1U);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.marker_summary.size(), 1U);
    EXPECT_EQ(result.marker_summary[0].marker, 0xE1U);
    EXPECT_EQ(result.marker_summary[0].count, 1U);

    EXPECT_TRUE(result.edit_requested);
    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.jpeg_edit_plan.selected_mode,
              openmeta::JpegEditMode::InPlace);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_output_size, input.size());
    ASSERT_EQ(result.edited_output.size(), input.size());

    const std::vector<std::byte> needle = ascii_z("2033:03:04 05:06:07");
    const auto it = std::search(result.edited_output.begin(),
                                result.edited_output.end(), needle.begin(),
                                needle.end());
    EXPECT_NE(it, result.edited_output.end());
}

TEST(MetadataTransferApi, WritePreparedBundleJpegEditMatchesApply)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    const std::vector<std::byte> input = make_jpeg_with_segment(
        0xE1U, std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                          bundle.blocks[0].payload.size()));
    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> expected;
    const openmeta::EmitTransferResult buffered
        = openmeta::apply_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &expected);
    ASSERT_EQ(buffered.status, openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, writer);
    EXPECT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(streamed.emitted, buffered.emitted);
    EXPECT_EQ(writer.out, expected);
}

TEST(MetadataTransferApi, BuildPreparedBundleJpegPackageMatchesRewriteOutput)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    const std::array<TestJpegSegment, 0> no_segments {};
    const std::vector<std::byte> input = make_jpeg_with_segments(no_segments);
    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.selected_mode, openmeta::JpegEditMode::MetadataRewrite);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_bundle_jpeg_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &package);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(package.target_format, openmeta::TransferTargetFormat::Jpeg);
    ASSERT_EQ(package.output_size, plan.output_size);
    ASSERT_EQ(package.chunks.size(), 3U);
    EXPECT_EQ(package.chunks[0].kind,
              openmeta::TransferPackageChunkKind::SourceRange);
    EXPECT_EQ(package.chunks[1].kind,
              openmeta::TransferPackageChunkKind::PreparedJpegSegment);
    EXPECT_EQ(package.chunks[2].kind,
              openmeta::TransferPackageChunkKind::SourceRange);

    std::vector<std::byte> expected;
    const openmeta::EmitTransferResult buffered
        = openmeta::apply_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &expected);
    ASSERT_EQ(buffered.status, openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_transfer_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            package, writer);
    EXPECT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(writer.out, expected);
}

TEST(MetadataTransferApi, BuildPreparedBundleJpegPackageMatchesInPlaceOutput)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    const std::vector<std::byte> input = make_jpeg_with_segment(
        0xE1U, std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                          bundle.blocks[0].payload.size()));
    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.selected_mode, openmeta::JpegEditMode::InPlace);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_bundle_jpeg_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &package);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(package.output_size, plan.output_size);
    ASSERT_EQ(package.chunks.size(), 3U);
    EXPECT_EQ(package.chunks[1].kind,
              openmeta::TransferPackageChunkKind::InlineBytes);

    std::vector<std::byte> expected;
    const openmeta::EmitTransferResult buffered
        = openmeta::apply_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &expected);
    ASSERT_EQ(buffered.status, openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_transfer_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            package, writer);
    EXPECT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(writer.out, expected);
}

TEST(MetadataTransferApi, BuildPreparedTransferEmitPackageJpegMatchesWriterEmit)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock com;
    com.route   = "jpeg:com";
    com.payload = {};
    bundle.blocks.push_back(com);

    openmeta::EmitTransferOptions emit_options;
    emit_options.skip_empty_payloads = false;

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_transfer_emit_package(bundle, &package,
                                                         emit_options);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(package.target_format, openmeta::TransferTargetFormat::Jpeg);
    ASSERT_EQ(package.input_size, 0U);
    ASSERT_EQ(package.output_size, 9U);
    ASSERT_EQ(package.chunks.size(), 2U);
    EXPECT_EQ(package.chunks[0].kind,
              openmeta::TransferPackageChunkKind::PreparedTransferBlock);
    EXPECT_EQ(package.chunks[1].kind,
              openmeta::TransferPackageChunkKind::PreparedTransferBlock);

    BufferByteWriter writer;
    const std::vector<std::byte> empty_input;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_transfer_package(
            std::span<const std::byte>(empty_input.data(), empty_input.size()),
            bundle, package, writer);
    ASSERT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(writer.out.size(), 9U);
    EXPECT_EQ(writer.out[0], std::byte { 0xFF });
    EXPECT_EQ(writer.out[1], std::byte { 0xE1 });
    EXPECT_EQ(writer.out[2], std::byte { 0x00 });
    EXPECT_EQ(writer.out[3], std::byte { 0x03 });
    EXPECT_EQ(writer.out[4], std::byte { 0x01 });
    EXPECT_EQ(writer.out[5], std::byte { 0xFF });
    EXPECT_EQ(writer.out[6], std::byte { 0xFE });
    EXPECT_EQ(writer.out[7], std::byte { 0x00 });
    EXPECT_EQ(writer.out[8], std::byte { 0x02 });
}

TEST(MetadataTransferApi, BuildPreparedTransferPackageBatchJpegOwnsRewriteBytes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::array<TestJpegSegment, 0> no_segments {};
    const std::vector<std::byte> input = make_jpeg_with_segments(no_segments);
    const openmeta::JpegEditPlan plan = openmeta::plan_prepared_bundle_jpeg_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_bundle_jpeg_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &package);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult built
        = openmeta::build_prepared_transfer_package_batch(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            package, &batch);
    ASSERT_EQ(built.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> expected;
    const openmeta::EmitTransferResult buffered
        = openmeta::apply_prepared_bundle_jpeg_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &expected);
    ASSERT_EQ(buffered.status, openmeta::TransferStatus::Ok);

    bundle.blocks[0].payload[0] = std::byte { 0x7F };

    BufferByteWriter writer;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_transfer_package_batch(batch, writer);
    EXPECT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(writer.out, expected);
}

TEST(MetadataTransferApi, ExecutePreparedTransferJpegEditToWriter)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);

    const std::vector<std::byte> input = make_jpeg_with_segment(
        0xE1U, std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                          bundle.blocks[0].payload.size()));

    openmeta::TransferTimePatchInput patch;
    patch.field      = openmeta::TimePatchField::DateTime;
    patch.value      = ascii_z("2033:03:04 05:06:07");
    patch.text_value = true;

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.time_patches.push_back(patch);
    options.edit_requested     = true;
    options.edit_apply         = true;
    options.edit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_output_size, input.size());
    EXPECT_TRUE(result.edited_output.empty());
    EXPECT_EQ(writer.out.size(), input.size());
}

TEST(MetadataTransferApi,
     BuildExecutedTransferPackageBatchJpegEditMatchesBufferedApply)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    e.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(
        openmeta::prepare_metadata_for_target(store, request, &bundle).status,
        openmeta::TransferStatus::Ok);

    const std::array<TestJpegSegment, 0> no_segments {};
    const std::vector<std::byte> input = make_jpeg_with_segments(no_segments);

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    const openmeta::ExecutePreparedTransferResult exec
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);
    ASSERT_EQ(exec.edit_plan_status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackageBatch batch;
    ASSERT_EQ(openmeta::build_executed_transfer_package_batch(
                  std::span<const std::byte>(input.data(), input.size()),
                  bundle, exec, &batch)
                  .status,
              openmeta::TransferStatus::Ok);

    std::vector<std::byte> expected;
    ASSERT_EQ(openmeta::apply_prepared_bundle_jpeg_edit(
                  std::span<const std::byte>(input.data(), input.size()),
                  bundle, exec.jpeg_edit_plan, &expected)
                  .status,
              openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    ASSERT_EQ(
        openmeta::write_prepared_transfer_package_batch(batch, writer).status,
        openmeta::TransferStatus::Ok);
    EXPECT_EQ(writer.out, expected);
}

TEST(MetadataTransferApi, ExecutePreparedTransferTiffEditAndEmit)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        store.arena(), "http://purl.org/dc/elements/1.1/", "title");
    xmp.value                 = openmeta::make_text(store.arena(), "OpenMeta",
                                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1;
    ASSERT_NE(store.add_entry(xmp), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format        = openmeta::TransferTargetFormat::Tiff;
    request.include_icc_app2     = false;
    request.include_iptc_app13   = false;
    request.include_xmp_app1     = true;
    request.xmp_include_existing = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();

    openmeta::TransferTimePatchInput patch;
    patch.field      = openmeta::TimePatchField::DateTime;
    patch.value      = ascii_z("2030:12:31 23:59:59");
    patch.text_value = true;

    openmeta::ExecutePreparedTransferOptions options;
    options.time_patches.push_back(patch);
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.time_patch.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_GE(result.compiled_ops, 1U);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_TRUE(result.tiff_commit);
    ASSERT_FALSE(result.tiff_tag_summary.empty());

    bool found_xmp  = false;
    bool found_exif = false;
    for (size_t i = 0; i < result.tiff_tag_summary.size(); ++i) {
        if (result.tiff_tag_summary[i].tag == 700U) {
            found_xmp = true;
        }
        if (result.tiff_tag_summary[i].tag == 34665U) {
            found_exif = true;
        }
    }
    EXPECT_TRUE(found_xmp);
    EXPECT_TRUE(found_exif);

    EXPECT_TRUE(result.edit_requested);
    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_GE(result.tiff_edit_plan.tag_updates, 1U);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_GT(result.edit_output_size, input.size());
    ASSERT_GT(result.edited_output.size(), input.size());
}

TEST(MetadataTransferApi, WritePreparedBundleTiffEditMatchesApply)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;
    request.include_xmp_app1   = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> expected;
    const openmeta::EmitTransferResult buffered
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &expected);
    ASSERT_EQ(buffered.status, openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, writer);
    EXPECT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(streamed.emitted, buffered.emitted);
    EXPECT_EQ(writer.out, expected);
}

TEST(MetadataTransferApi, BuildPreparedBundleTiffPackageMatchesRewriteOutput)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;
    request.include_xmp_app1   = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_bundle_tiff_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &package);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(package.target_format, openmeta::TransferTargetFormat::Tiff);
    ASSERT_EQ(package.output_size, plan.output_size);
    ASSERT_EQ(package.chunks.size(), 4U);
    EXPECT_EQ(package.chunks[0].kind,
              openmeta::TransferPackageChunkKind::SourceRange);
    EXPECT_EQ(package.chunks[1].kind,
              openmeta::TransferPackageChunkKind::InlineBytes);
    EXPECT_EQ(package.chunks[2].kind,
              openmeta::TransferPackageChunkKind::SourceRange);
    EXPECT_EQ(package.chunks[3].kind,
              openmeta::TransferPackageChunkKind::InlineBytes);

    std::vector<std::byte> expected;
    const openmeta::EmitTransferResult buffered
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &expected);
    ASSERT_EQ(buffered.status, openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_transfer_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            package, writer);
    EXPECT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(writer.out, expected);
}

TEST(MetadataTransferApi, BuildPreparedBundleDngPackageMatchesRewriteOutput)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Dng;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;
    request.include_xmp_app1   = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_bundle_tiff_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &package);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(package.target_format, openmeta::TransferTargetFormat::Dng);
    ASSERT_EQ(package.output_size, plan.output_size);
    ASSERT_EQ(package.chunks.size(), 4U);

    std::vector<std::byte> expected;
    const openmeta::EmitTransferResult buffered
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &expected);
    ASSERT_EQ(buffered.status, openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_transfer_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            package, writer);
    EXPECT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(writer.out, expected);
}

TEST(MetadataTransferApi, BuildPreparedBundleBigTiffPackageMatchesRewriteOutput)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;
    request.include_xmp_app1   = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(store, request, &bundle).status,
              openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_bigtiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackagePlan package;
    ASSERT_EQ(openmeta::build_prepared_bundle_tiff_package(
                  std::span<const std::byte>(input.data(), input.size()), bundle,
                  plan, &package)
                  .status,
              openmeta::TransferStatus::Ok);

    std::vector<std::byte> expected;
    ASSERT_EQ(openmeta::apply_prepared_bundle_tiff_edit(
                  std::span<const std::byte>(input.data(), input.size()), bundle,
                  plan, &expected)
                  .status,
              openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    ASSERT_EQ(openmeta::write_prepared_transfer_package(
                  std::span<const std::byte>(input.data(), input.size()), bundle,
                  package, writer)
                  .status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(writer.out, expected);
}

TEST(MetadataTransferApi,
     BuildPreparedBundleTiffPackageMatchesRewriteOutputWhenStrippingExistingXmp)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;
    request.include_xmp_app1   = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> input;
    ASSERT_TRUE(build_test_tiff_with_creator_tool_xmp("Target Embedded Existing",
                                                      &input));

    openmeta::PlanTiffEditOptions options;
    options.strip_existing_xmp = true;
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle,
        options);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> expected;
    const openmeta::EmitTransferResult buffered
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &expected);
    ASSERT_EQ(buffered.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_bundle_tiff_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &package);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_transfer_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            package, writer);
    EXPECT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(writer.out, expected);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(expected.data(), expected.size()), &decoded));
    EXPECT_FALSE(store_has_text_entry(
        decoded,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "Target Embedded Existing"));
}

TEST(MetadataTransferApi, BuildPreparedTransferPackageBatchTiffOwnsRewriteBytes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;
    request.include_xmp_app1   = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_bundle_tiff_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &package);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult built
        = openmeta::build_prepared_transfer_package_batch(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            package, &batch);
    ASSERT_EQ(built.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> expected;
    const openmeta::EmitTransferResult buffered
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &expected);
    ASSERT_EQ(buffered.status, openmeta::TransferStatus::Ok);

    bundle.blocks[0].payload[0] = std::byte { 0x7F };

    BufferByteWriter writer;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_transfer_package_batch(batch, writer);
    EXPECT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(writer.out, expected);
}

TEST(MetadataTransferApi, BuildPreparedTransferPackageBatchDngOwnsRewriteBytes)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Dng;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;
    request.include_xmp_app1   = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_bundle_tiff_package(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &package);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(package.target_format, openmeta::TransferTargetFormat::Dng);

    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult built
        = openmeta::build_prepared_transfer_package_batch(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            package, &batch);
    ASSERT_EQ(built.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> expected;
    const openmeta::EmitTransferResult buffered
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan, &expected);
    ASSERT_EQ(buffered.status, openmeta::TransferStatus::Ok);

    bundle.blocks[0].payload[0] = std::byte { 0x7F };

    BufferByteWriter writer;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_transfer_package_batch(batch, writer);
    EXPECT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(writer.out, expected);
}

TEST(MetadataTransferApi, ExecutePreparedTransferTiffEditToWriter)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Tiff;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;
    request.include_xmp_app1   = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested     = true;
    options.edit_apply         = true;
    options.edit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_output_size, result.tiff_edit_plan.output_size);
    EXPECT_TRUE(result.edited_output.empty());
    EXPECT_EQ(writer.out.size(), static_cast<size_t>(result.edit_output_size));
}

TEST(MetadataTransferApi, ExecutePreparedTransferDngEditToWriter)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Dng;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;
    request.include_xmp_app1   = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested     = true;
    options.edit_apply         = true;
    options.edit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_output_size, result.tiff_edit_plan.output_size);
    EXPECT_TRUE(result.edited_output.empty());
    EXPECT_EQ(writer.out.size(), static_cast<size_t>(result.edit_output_size));
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileRejectsEmptyPath)
{
    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file("");

    EXPECT_EQ(result.prepared.file_status,
              openmeta::TransferFileStatus::InvalidArgument);
    EXPECT_EQ(result.prepared.code,
              openmeta::PrepareTransferFileCode::EmptyPath);
    EXPECT_EQ(result.execute.compile.status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.execute.emit.status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(result.execute.compile.message.find("read/prepare failure")
                != std::string::npos);
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileStagesSignedC2pa)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.include_exif_app1  = false;
    options.prepare.prepare.include_xmp_app1   = false;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.prepare.prepare.profile.c2pa
        = openmeta::TransferPolicyAction::Rewrite;
    options.c2pa_stage_requested = true;

    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    options.c2pa_signer_input            = make_test_c2pa_signer_input(
        std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(result.execute.c2pa_stage_requested);
    EXPECT_EQ(result.execute.c2pa_stage_validation.status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.c2pa_stage_validation.code,
              openmeta::EmitTransferCode::None);
    EXPECT_EQ(result.execute.c2pa_stage_validation.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(result.execute.c2pa_stage_validation.staged_segments, 1U);
    EXPECT_EQ(result.execute.c2pa_stage.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(count_blocks_with_route(result.prepared.bundle, "jpeg:app11-c2pa"),
              result.execute.c2pa_stage.emitted);
    EXPECT_EQ(result.prepared.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::Ready);

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(result.prepared.bundle,
                               openmeta::TransferPolicySubject::C2pa);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::ExternalSignedPayload);
    EXPECT_EQ(decision->c2pa_mode, openmeta::TransferC2paMode::SignedRewrite);
    EXPECT_EQ(decision->c2pa_prepared_output,
              openmeta::TransferC2paPreparedOutput::SignedRewrite);
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileStagesSignedC2paBmff)
{
    const std::vector<std::byte> bmff = make_test_bmff_with_c2pa(false);
    ASSERT_FALSE(bmff.empty());
    const std::string path = unique_temp_path(".heic");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(bmff.data(), bmff.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Heif;
    options.prepare.prepare.include_exif_app1  = false;
    options.prepare.prepare.include_xmp_app1   = false;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.prepare.prepare.profile.c2pa
        = openmeta::TransferPolicyAction::Rewrite;
    options.c2pa_stage_requested   = true;
    options.edit_target_path       = path;
    options.execute.edit_requested = true;
    options.execute.edit_apply     = true;

    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    options.c2pa_signer_input            = make_test_c2pa_signer_input(
        std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(result.execute.c2pa_stage_requested);
    EXPECT_EQ(result.execute.c2pa_stage_validation.status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.c2pa_stage_validation.code,
              openmeta::EmitTransferCode::None);
    EXPECT_EQ(result.execute.c2pa_stage_validation.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(result.execute.c2pa_stage_validation.staged_segments, 1U);
    EXPECT_EQ(result.execute.c2pa_stage.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(count_blocks_with_route(result.prepared.bundle, "bmff:item-c2pa"),
              result.execute.c2pa_stage.emitted);
    EXPECT_EQ(result.prepared.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::Ready);

    std::array<openmeta::ContainerBlockRef, 16> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_bmff(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size()),
        std::span<openmeta::ContainerBlockRef>(blocks.data(), blocks.size()));
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    EXPECT_GE(scan.written, 1U);
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileStagesSignedC2paJxl)
{
    const std::vector<std::byte> jxl = make_test_jxl_with_c2pa(false);
    const std::string path           = unique_temp_path(".jxl");
    ASSERT_TRUE(write_bytes_file(path, std::span<const std::byte>(jxl.data(),
                                                                  jxl.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jxl;
    options.prepare.prepare.include_exif_app1  = false;
    options.prepare.prepare.include_xmp_app1   = false;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.prepare.prepare.profile.c2pa
        = openmeta::TransferPolicyAction::Rewrite;
    options.c2pa_stage_requested   = true;
    options.edit_target_path       = path;
    options.execute.edit_requested = true;
    options.execute.edit_apply     = true;

    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    options.c2pa_signer_input            = make_test_c2pa_signer_input(
        std::span<const std::byte>(logical.data(), logical.size()));

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(result.execute.c2pa_stage_requested);
    EXPECT_EQ(result.execute.c2pa_stage_validation.status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.c2pa_stage_validation.code,
              openmeta::EmitTransferCode::None);
    EXPECT_EQ(result.execute.c2pa_stage_validation.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(result.execute.c2pa_stage_validation.staged_segments, 1U);
    EXPECT_EQ(result.execute.c2pa_stage.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(count_blocks_with_route(result.prepared.bundle, "jxl:box-jumb"),
              result.execute.c2pa_stage.emitted);
    EXPECT_EQ(result.prepared.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::Ready);
    ASSERT_FALSE(result.execute.edited_output.empty());

    openmeta::PreparedTransferBundle bundle = result.prepared.bundle;
    openmeta::ExecutePreparedTransferOptions execute_options;
    execute_options.edit_requested = true;
    execute_options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult direct
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(jxl.data(), jxl.size()),
            execute_options);
    ASSERT_EQ(direct.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edited_output, direct.edited_output);
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileJpegRoundTripsSourceMetadata)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_jpeg = make_jpeg_with_segments({});
    const std::string target_path            = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_jpeg.data(), target_jpeg.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    EXPECT_TRUE(decoded_transfer_roundtrip_has_expected_fields(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size())));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileJpegSidecarOnlyStripsEmbeddedXmp)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_jpeg = make_jpeg_with_segments({});
    const std::string target_path            = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_jpeg.data(), target_jpeg.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;
    options.xmp_writeback_mode
        = openmeta::XmpWritebackMode::SidecarOnly;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_TRUE(result.xmp_sidecar_requested);
    EXPECT_EQ(result.xmp_sidecar_status, openmeta::TransferStatus::Ok);
    EXPECT_FALSE(result.xmp_sidecar_output.empty());
    EXPECT_EQ(count_blocks_with_route(result.prepared.bundle, "jpeg:app1-xmp"),
              0U);

    const size_t dot = target_path.find_last_of('.');
    ASSERT_NE(dot, std::string::npos);
    EXPECT_EQ(result.xmp_sidecar_path, target_path.substr(0, dot) + ".xmp");

    openmeta::MetaStore edited_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size()),
        &edited_store));
    EXPECT_TRUE(store_has_text_entry(edited_store,
                                     exif_key_view("exififd", 0x9003U),
                                     "2024:01:02 03:04:05"));
    EXPECT_TRUE(store_lacks_text_entry(
        edited_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool")));

    openmeta::MetaStore sidecar_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.xmp_sidecar_output.data(),
                                   result.xmp_sidecar_output.size()),
        &sidecar_store));
    EXPECT_TRUE(store_has_text_entry(
        sidecar_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileJpegSidecarOnlyPreservesDestinationEmbeddedXmpByDefault)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    std::vector<std::byte> target_xmp_payload;
    ASSERT_TRUE(build_test_creator_tool_jpeg_xmp_app1_payload(
        "Target Embedded Existing", &target_xmp_payload));
    const TestJpegSegment segments[] = {
        { 0xE1U,
          std::span<const std::byte>(target_xmp_payload.data(),
                                     target_xmp_payload.size()) },
    };
    const std::vector<std::byte> target_jpeg = make_jpeg_with_segments(
        std::span<const TestJpegSegment>(segments, 1U));
    const std::string target_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_jpeg.data(), target_jpeg.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;
    options.xmp_writeback_mode
        = openmeta::XmpWritebackMode::SidecarOnly;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);

    openmeta::MetaStore edited_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size()),
        &edited_store));
    EXPECT_TRUE(store_has_text_entry(
        edited_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "Target Embedded Existing"));
    EXPECT_FALSE(store_has_text_entry(
        edited_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));

    openmeta::MetaStore sidecar_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.xmp_sidecar_output.data(),
                                   result.xmp_sidecar_output.size()),
        &sidecar_store));
    EXPECT_TRUE(store_has_text_entry(
        sidecar_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileJpegSidecarOnlyCanStripExistingDestinationEmbeddedXmp)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    std::vector<std::byte> target_xmp_payload;
    ASSERT_TRUE(build_test_creator_tool_jpeg_xmp_app1_payload(
        "Target Embedded Existing", &target_xmp_payload));
    const TestJpegSegment segments[] = {
        { 0xE1U,
          std::span<const std::byte>(target_xmp_payload.data(),
                                     target_xmp_payload.size()) },
    };
    const std::vector<std::byte> target_jpeg = make_jpeg_with_segments(
        std::span<const TestJpegSegment>(segments, 1U));
    const std::string target_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_jpeg.data(), target_jpeg.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;
    options.xmp_writeback_mode
        = openmeta::XmpWritebackMode::SidecarOnly;
    options.xmp_destination_embedded_mode
        = openmeta::XmpDestinationEmbeddedMode::StripExisting;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);

    openmeta::MetaStore edited_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size()),
        &edited_store));
    EXPECT_FALSE(store_has_text_entry(
        edited_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "Target Embedded Existing"));
    EXPECT_FALSE(store_has_text_entry(
        edited_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));

    openmeta::MetaStore sidecar_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.xmp_sidecar_output.data(),
                                   result.xmp_sidecar_output.size()),
        &sidecar_store));
    EXPECT_TRUE(store_has_text_entry(
        sidecar_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileJpegEmbeddedAndSidecarKeepsEmbeddedXmp)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_jpeg = make_jpeg_with_segments({});
    const std::string target_path            = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_jpeg.data(), target_jpeg.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;
    options.xmp_writeback_mode
        = openmeta::XmpWritebackMode::EmbeddedAndSidecar;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_TRUE(result.xmp_sidecar_requested);
    EXPECT_EQ(result.xmp_sidecar_status, openmeta::TransferStatus::Ok);
    EXPECT_FALSE(result.xmp_sidecar_output.empty());
    EXPECT_EQ(count_blocks_with_route(result.prepared.bundle, "jpeg:app1-xmp"),
              1U);

    openmeta::MetaStore edited_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size()),
        &edited_store));
    EXPECT_TRUE(store_has_text_entry(edited_store,
                                     exif_key_view("exififd", 0x9003U),
                                     "2024:01:02 03:04:05"));
    EXPECT_TRUE(store_has_text_entry(
        edited_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));

    openmeta::MetaStore sidecar_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.xmp_sidecar_output.data(),
                                   result.xmp_sidecar_output.size()),
        &sidecar_store));
    EXPECT_TRUE(store_has_text_entry(
        sidecar_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileSidecarOnlyRequiresEditTargetPath)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.xmp_writeback_mode
        = openmeta::XmpWritebackMode::SidecarOnly;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());

    ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_TRUE(result.xmp_sidecar_requested);
    EXPECT_EQ(result.xmp_sidecar_status, openmeta::TransferStatus::Unsupported);
    EXPECT_NE(result.xmp_sidecar_message.find("edit_target_path"),
              std::string::npos);
    EXPECT_EQ(count_blocks_with_route(result.prepared.bundle, "jpeg:app1-xmp"),
              1U);
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileCanMergeExistingOutputSidecarXmp)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_jpeg = make_jpeg_with_segments({});
    const std::string target_path            = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_jpeg.data(), target_jpeg.size())));

    const std::string output_path = unique_temp_path(".jpg");
    const size_t output_dot       = output_path.find_last_of('.');
    ASSERT_NE(output_dot, std::string::npos);
    const std::string output_sidecar_path
        = output_path.substr(0, output_dot) + ".xmp";

    std::vector<std::byte> existing_sidecar;
    ASSERT_TRUE(build_test_creator_tool_xmp_sidecar("Target Sidecar Existing",
                                                    &existing_sidecar));
    ASSERT_TRUE(write_bytes_file(
        output_sidecar_path,
        std::span<const std::byte>(existing_sidecar.data(),
                                   existing_sidecar.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.prepare.prepare.xmp_include_existing = true;
    options.prepare.prepare.xmp_conflict_policy
        = openmeta::XmpConflictPolicy::ExistingWins;
    options.prepare.xmp_existing_sidecar_mode
        = openmeta::XmpExistingSidecarMode::MergeIfPresent;
    options.prepare.xmp_existing_sidecar_base_path = output_path;
    options.edit_target_path                       = target_path;
    options.xmp_sidecar_base_path                  = output_path;
    options.execute.edit_apply                     = true;
    options.xmp_writeback_mode
        = openmeta::XmpWritebackMode::SidecarOnly;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);

    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
    std::remove(output_sidecar_path.c_str());

    ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_TRUE(result.prepared.xmp_existing_sidecar_loaded);
    EXPECT_EQ(result.prepared.xmp_existing_sidecar_status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.prepared.xmp_existing_sidecar_path, output_sidecar_path);
    EXPECT_EQ(result.xmp_sidecar_path, output_sidecar_path);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);

    openmeta::MetaStore sidecar_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.xmp_sidecar_output.data(),
                                   result.xmp_sidecar_output.size()),
        &sidecar_store));
    EXPECT_TRUE(store_has_text_entry(
        sidecar_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "Target Sidecar Existing"));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileCanPreferSourceEmbeddedOverExistingOutputSidecarXmp)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_jpeg = make_jpeg_with_segments({});
    const std::string target_path            = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_jpeg.data(), target_jpeg.size())));

    const std::string output_path = unique_temp_path(".jpg");
    const size_t output_dot       = output_path.find_last_of('.');
    ASSERT_NE(output_dot, std::string::npos);
    const std::string output_sidecar_path
        = output_path.substr(0, output_dot) + ".xmp";

    std::vector<std::byte> existing_sidecar;
    ASSERT_TRUE(build_test_creator_tool_xmp_sidecar("Target Sidecar Existing",
                                                    &existing_sidecar));
    ASSERT_TRUE(write_bytes_file(
        output_sidecar_path,
        std::span<const std::byte>(existing_sidecar.data(),
                                   existing_sidecar.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.prepare.prepare.xmp_include_existing = true;
    options.prepare.prepare.xmp_conflict_policy
        = openmeta::XmpConflictPolicy::ExistingWins;
    options.prepare.xmp_existing_sidecar_mode
        = openmeta::XmpExistingSidecarMode::MergeIfPresent;
    options.prepare.xmp_existing_sidecar_precedence
        = openmeta::XmpExistingSidecarPrecedence::SourceWins;
    options.prepare.xmp_existing_sidecar_base_path = output_path;
    options.edit_target_path                       = target_path;
    options.xmp_sidecar_base_path                  = output_path;
    options.execute.edit_apply                     = true;
    options.xmp_writeback_mode
        = openmeta::XmpWritebackMode::SidecarOnly;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);

    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
    std::remove(output_sidecar_path.c_str());

    ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_TRUE(result.prepared.xmp_existing_sidecar_loaded);
    EXPECT_EQ(result.prepared.xmp_existing_sidecar_status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.prepared.xmp_existing_sidecar_path, output_sidecar_path);
    EXPECT_EQ(result.xmp_sidecar_path, output_sidecar_path);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);

    openmeta::MetaStore sidecar_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.xmp_sidecar_output.data(),
                                   result.xmp_sidecar_output.size()),
        &sidecar_store));
    EXPECT_TRUE(store_has_text_entry(
        sidecar_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));
    EXPECT_FALSE(store_has_text_entry(
        sidecar_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "Target Sidecar Existing"));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileEmbeddedOnlyCanMergeExistingDestinationEmbeddedXmpAcrossTargets)
{
    struct Case final {
        openmeta::TransferTargetFormat format;
        const char* extension;
    };
    static const Case kCases[] = {
        { openmeta::TransferTargetFormat::Jpeg, ".jpg" },
        { openmeta::TransferTargetFormat::Tiff, ".tif" },
        { openmeta::TransferTargetFormat::Dng, ".dng" },
        { openmeta::TransferTargetFormat::Png, ".png" },
        { openmeta::TransferTargetFormat::Webp, ".webp" },
        { openmeta::TransferTargetFormat::Jp2, ".jp2" },
        { openmeta::TransferTargetFormat::Jxl, ".jxl" },
    };

    for (size_t i = 0; i < std::size(kCases); ++i) {
        SCOPED_TRACE(static_cast<int>(kCases[i].format));

        std::vector<std::byte> source_jpeg;
        ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
        const std::string source_path = unique_temp_path(".jpg");
        ASSERT_TRUE(write_bytes_file(
            source_path, std::span<const std::byte>(source_jpeg.data(),
                                                    source_jpeg.size())));

        std::vector<std::byte> target_bytes;
        ASSERT_TRUE(build_test_target_with_existing_creator_tool_xmp(
            kCases[i].format, "Target Embedded Existing", &target_bytes));
        const std::string target_path = unique_temp_path(kCases[i].extension);
        ASSERT_TRUE(write_bytes_file(
            target_path, std::span<const std::byte>(target_bytes.data(),
                                                    target_bytes.size())));

        openmeta::ExecutePreparedTransferFileOptions options;
        options.prepare.prepare.target_format = kCases[i].format;
        options.prepare.prepare.include_icc_app2   = false;
        options.prepare.prepare.include_iptc_app13 = false;
        options.prepare.prepare.xmp_include_existing = true;
        options.prepare.prepare.xmp_conflict_policy
            = openmeta::XmpConflictPolicy::ExistingWins;
        options.edit_target_path = target_path;
        options.execute.edit_apply = true;
        options.xmp_existing_destination_embedded_mode
            = openmeta::XmpExistingDestinationEmbeddedMode::MergeIfPresent;
        options.xmp_writeback_mode
            = openmeta::XmpWritebackMode::EmbeddedOnly;

        const openmeta::ExecutePreparedTransferFileResult result
            = openmeta::execute_prepared_transfer_file(source_path.c_str(),
                                                       options);
        std::remove(source_path.c_str());
        std::remove(target_path.c_str());

        ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
        ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
        ASSERT_TRUE(result.xmp_existing_destination_embedded_loaded);
        EXPECT_EQ(result.xmp_existing_destination_embedded_status,
                  openmeta::TransferStatus::Ok);
        EXPECT_EQ(result.xmp_existing_destination_embedded_path, target_path);
        ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);

        openmeta::MetaStore edited_store;
        ASSERT_TRUE(decode_transfer_roundtrip_store(
            std::span<const std::byte>(result.execute.edited_output.data(),
                                       result.execute.edited_output.size()),
            &edited_store));
        EXPECT_TRUE(store_has_text_entry(
            edited_store,
            xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
            "Target Embedded Existing"));
        EXPECT_FALSE(store_has_text_entry(
            edited_store,
            xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
            "OpenMeta Transfer Source"));
    }
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileEmbeddedOnlyCanPreferSourceOverExistingDestinationEmbeddedXmpAcrossTargets)
{
    struct Case final {
        openmeta::TransferTargetFormat format;
        const char* extension;
    };
    static const Case kCases[] = {
        { openmeta::TransferTargetFormat::Jpeg, ".jpg" },
        { openmeta::TransferTargetFormat::Tiff, ".tif" },
        { openmeta::TransferTargetFormat::Dng, ".dng" },
        { openmeta::TransferTargetFormat::Png, ".png" },
        { openmeta::TransferTargetFormat::Webp, ".webp" },
        { openmeta::TransferTargetFormat::Jp2, ".jp2" },
        { openmeta::TransferTargetFormat::Jxl, ".jxl" },
    };

    for (size_t i = 0; i < std::size(kCases); ++i) {
        SCOPED_TRACE(static_cast<int>(kCases[i].format));

        std::vector<std::byte> source_jpeg;
        ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
        const std::string source_path = unique_temp_path(".jpg");
        ASSERT_TRUE(write_bytes_file(
            source_path, std::span<const std::byte>(source_jpeg.data(),
                                                    source_jpeg.size())));

        std::vector<std::byte> target_bytes;
        ASSERT_TRUE(build_test_target_with_existing_creator_tool_xmp(
            kCases[i].format, "Target Embedded Existing", &target_bytes));
        const std::string target_path = unique_temp_path(kCases[i].extension);
        ASSERT_TRUE(write_bytes_file(
            target_path, std::span<const std::byte>(target_bytes.data(),
                                                    target_bytes.size())));

        openmeta::ExecutePreparedTransferFileOptions options;
        options.prepare.prepare.target_format = kCases[i].format;
        options.prepare.prepare.include_icc_app2   = false;
        options.prepare.prepare.include_iptc_app13 = false;
        options.prepare.prepare.xmp_include_existing = true;
        options.prepare.prepare.xmp_conflict_policy
            = openmeta::XmpConflictPolicy::ExistingWins;
        options.edit_target_path = target_path;
        options.execute.edit_apply = true;
        options.xmp_existing_destination_embedded_mode
            = openmeta::XmpExistingDestinationEmbeddedMode::MergeIfPresent;
        options.xmp_existing_destination_embedded_precedence
            = openmeta::XmpExistingDestinationEmbeddedPrecedence::SourceWins;
        options.xmp_writeback_mode
            = openmeta::XmpWritebackMode::EmbeddedOnly;

        const openmeta::ExecutePreparedTransferFileResult result
            = openmeta::execute_prepared_transfer_file(source_path.c_str(),
                                                       options);
        std::remove(source_path.c_str());
        std::remove(target_path.c_str());

        ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
        ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
        ASSERT_TRUE(result.xmp_existing_destination_embedded_loaded);
        EXPECT_EQ(result.xmp_existing_destination_embedded_status,
                  openmeta::TransferStatus::Ok);
        EXPECT_EQ(result.xmp_existing_destination_embedded_path, target_path);
        ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);

        openmeta::MetaStore edited_store;
        ASSERT_TRUE(decode_transfer_roundtrip_store(
            std::span<const std::byte>(result.execute.edited_output.data(),
                                       result.execute.edited_output.size()),
            &edited_store));
        EXPECT_TRUE(store_has_text_entry(
            edited_store,
            xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
            "OpenMeta Transfer Source"));
        EXPECT_FALSE(store_has_text_entry(
            edited_store,
            xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
            "Target Embedded Existing"));
    }
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileSidecarOnlyPreservesDestinationEmbeddedXmpAcrossTargets)
{
    struct Case final {
        openmeta::TransferTargetFormat format;
        const char* extension;
    };
    static const Case kCases[] = {
        { openmeta::TransferTargetFormat::Jpeg, ".jpg" },
        { openmeta::TransferTargetFormat::Tiff, ".tif" },
        { openmeta::TransferTargetFormat::Dng, ".dng" },
        { openmeta::TransferTargetFormat::Png, ".png" },
        { openmeta::TransferTargetFormat::Webp, ".webp" },
        { openmeta::TransferTargetFormat::Jp2, ".jp2" },
        { openmeta::TransferTargetFormat::Jxl, ".jxl" },
    };

    for (size_t i = 0; i < std::size(kCases); ++i) {
        SCOPED_TRACE(static_cast<int>(kCases[i].format));

        std::vector<std::byte> source_jpeg;
        ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
        const std::string source_path = unique_temp_path(".jpg");
        ASSERT_TRUE(write_bytes_file(
            source_path, std::span<const std::byte>(source_jpeg.data(),
                                                    source_jpeg.size())));

        std::vector<std::byte> target_bytes;
        ASSERT_TRUE(build_test_target_with_existing_creator_tool_xmp(
            kCases[i].format, "Target Embedded Existing", &target_bytes));
        const std::string target_path = unique_temp_path(kCases[i].extension);
        ASSERT_TRUE(write_bytes_file(
            target_path, std::span<const std::byte>(target_bytes.data(),
                                                    target_bytes.size())));

        openmeta::ExecutePreparedTransferFileOptions options;
        options.prepare.prepare.target_format = kCases[i].format;
        options.prepare.prepare.include_icc_app2   = false;
        options.prepare.prepare.include_iptc_app13 = false;
        options.edit_target_path                   = target_path;
        options.execute.edit_apply                 = true;
        options.xmp_writeback_mode
            = openmeta::XmpWritebackMode::SidecarOnly;

        const openmeta::ExecutePreparedTransferFileResult result
            = openmeta::execute_prepared_transfer_file(source_path.c_str(),
                                                       options);
        std::remove(source_path.c_str());
        std::remove(target_path.c_str());

        ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
        ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
        ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);

        openmeta::MetaStore edited_store;
        ASSERT_TRUE(decode_transfer_roundtrip_store(
            std::span<const std::byte>(result.execute.edited_output.data(),
                                       result.execute.edited_output.size()),
            &edited_store));
        EXPECT_TRUE(store_has_text_entry(
            edited_store,
            xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
            "Target Embedded Existing"));
        EXPECT_FALSE(store_has_text_entry(
            edited_store,
            xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
            "OpenMeta Transfer Source"));

        openmeta::MetaStore sidecar_store;
        ASSERT_TRUE(decode_transfer_roundtrip_store(
            std::span<const std::byte>(result.xmp_sidecar_output.data(),
                                       result.xmp_sidecar_output.size()),
            &sidecar_store));
        EXPECT_TRUE(store_has_text_entry(
            sidecar_store,
            xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
            "OpenMeta Transfer Source"));
    }
}

TEST(MetadataTransferApi,
     PrepareTransferFileCanMergeExistingDestinationEmbeddedXmp)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    std::vector<std::byte> target_bytes;
    ASSERT_TRUE(build_test_target_with_existing_creator_tool_xmp(
        openmeta::TransferTargetFormat::Jpeg, "Target Embedded Existing",
        &target_bytes));
    const std::string target_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_bytes.data(), target_bytes.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format        = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.include_icc_app2     = false;
    options.prepare.include_iptc_app13   = false;
    options.prepare.xmp_include_existing = true;
    options.prepare.xmp_conflict_policy
        = openmeta::XmpConflictPolicy::ExistingWins;
    options.xmp_existing_destination_embedded_mode
        = openmeta::XmpExistingDestinationEmbeddedMode::MergeIfPresent;
    options.xmp_existing_destination_embedded_path = target_path;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(source_path.c_str(),
                                                     options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    ASSERT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_TRUE(result.xmp_existing_destination_embedded_loaded);
    EXPECT_EQ(result.xmp_existing_destination_embedded_status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.xmp_existing_destination_embedded_path, target_path);
    EXPECT_EQ(count_blocks_with_route(result.bundle, "jpeg:app1-xmp"), 1U);

    bool found_target_existing = false;
    bool found_source_existing = false;
    for (size_t i = 0; i < result.bundle.blocks.size(); ++i) {
        if (result.bundle.blocks[i].route != "jpeg:app1-xmp") {
            continue;
        }
        const std::span<const std::byte> payload(
            result.bundle.blocks[i].payload.data(),
            result.bundle.blocks[i].payload.size());
        found_target_existing
            = payload_contains_ascii(payload, "Target Embedded Existing");
        found_source_existing
            = payload_contains_ascii(payload, "OpenMeta Transfer Source");
    }
    EXPECT_TRUE(found_target_existing);
    EXPECT_FALSE(found_source_existing);
}

TEST(MetadataTransferApi,
     PrepareTransferFileCanPreferSourceOverExistingDestinationEmbeddedXmp)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    std::vector<std::byte> target_bytes;
    ASSERT_TRUE(build_test_target_with_existing_creator_tool_xmp(
        openmeta::TransferTargetFormat::Jpeg, "Target Embedded Existing",
        &target_bytes));
    const std::string target_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_bytes.data(), target_bytes.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format        = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.include_icc_app2     = false;
    options.prepare.include_iptc_app13   = false;
    options.prepare.xmp_include_existing = true;
    options.prepare.xmp_conflict_policy
        = openmeta::XmpConflictPolicy::ExistingWins;
    options.xmp_existing_destination_embedded_mode
        = openmeta::XmpExistingDestinationEmbeddedMode::MergeIfPresent;
    options.xmp_existing_destination_embedded_precedence
        = openmeta::XmpExistingDestinationEmbeddedPrecedence::SourceWins;
    options.xmp_existing_destination_embedded_path = target_path;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(source_path.c_str(),
                                                     options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    ASSERT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_TRUE(result.xmp_existing_destination_embedded_loaded);
    EXPECT_EQ(result.xmp_existing_destination_embedded_status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.xmp_existing_destination_embedded_path, target_path);
    EXPECT_EQ(count_blocks_with_route(result.bundle, "jpeg:app1-xmp"), 1U);

    bool found_target_existing = false;
    bool found_source_existing = false;
    for (size_t i = 0; i < result.bundle.blocks.size(); ++i) {
        if (result.bundle.blocks[i].route != "jpeg:app1-xmp") {
            continue;
        }
        const std::span<const std::byte> payload(
            result.bundle.blocks[i].payload.data(),
            result.bundle.blocks[i].payload.size());
        found_target_existing
            = payload_contains_ascii(payload, "Target Embedded Existing");
        found_source_existing
            = payload_contains_ascii(payload, "OpenMeta Transfer Source");
    }
    EXPECT_FALSE(found_target_existing);
    EXPECT_TRUE(found_source_existing);
}

TEST(MetadataTransferApi,
     PrepareTransferFileDefaultDestinationCarrierPrecedencePrefersExistingSidecar)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    std::vector<std::byte> target_bytes;
    ASSERT_TRUE(build_test_target_with_existing_creator_tool_xmp(
        openmeta::TransferTargetFormat::Jpeg, "Target Embedded Existing",
        &target_bytes));
    const std::string target_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_bytes.data(), target_bytes.size())));

    const std::string output_path = unique_temp_path(".jpg");
    const size_t output_dot       = output_path.find_last_of('.');
    ASSERT_NE(output_dot, std::string::npos);
    const std::string output_sidecar_path
        = output_path.substr(0, output_dot) + ".xmp";

    std::vector<std::byte> existing_sidecar;
    ASSERT_TRUE(build_test_creator_tool_xmp_sidecar("Target Sidecar Existing",
                                                    &existing_sidecar));
    ASSERT_TRUE(write_bytes_file(
        output_sidecar_path,
        std::span<const std::byte>(existing_sidecar.data(),
                                   existing_sidecar.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format        = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.include_icc_app2     = false;
    options.prepare.include_iptc_app13   = false;
    options.prepare.xmp_include_existing = true;
    options.prepare.xmp_conflict_policy
        = openmeta::XmpConflictPolicy::ExistingWins;
    options.xmp_existing_sidecar_mode
        = openmeta::XmpExistingSidecarMode::MergeIfPresent;
    options.xmp_existing_sidecar_base_path = output_path;
    options.xmp_existing_destination_embedded_mode
        = openmeta::XmpExistingDestinationEmbeddedMode::MergeIfPresent;
    options.xmp_existing_destination_embedded_path = target_path;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(source_path.c_str(),
                                                     options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
    std::remove(output_sidecar_path.c_str());

    ASSERT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_TRUE(result.xmp_existing_sidecar_loaded);
    EXPECT_EQ(result.xmp_existing_sidecar_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.xmp_existing_sidecar_path, output_sidecar_path);
    ASSERT_TRUE(result.xmp_existing_destination_embedded_loaded);
    EXPECT_EQ(result.xmp_existing_destination_embedded_status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.xmp_existing_destination_embedded_path, target_path);
    EXPECT_EQ(count_blocks_with_route(result.bundle, "jpeg:app1-xmp"), 1U);

    bool found_sidecar_existing = false;
    bool found_target_existing  = false;
    bool found_source_existing  = false;
    for (size_t i = 0; i < result.bundle.blocks.size(); ++i) {
        if (result.bundle.blocks[i].route != "jpeg:app1-xmp") {
            continue;
        }
        const std::span<const std::byte> payload(
            result.bundle.blocks[i].payload.data(),
            result.bundle.blocks[i].payload.size());
        found_sidecar_existing
            = payload_contains_ascii(payload, "Target Sidecar Existing");
        found_target_existing
            = payload_contains_ascii(payload, "Target Embedded Existing");
        found_source_existing
            = payload_contains_ascii(payload, "OpenMeta Transfer Source");
    }
    EXPECT_TRUE(found_sidecar_existing);
    EXPECT_FALSE(found_target_existing);
    EXPECT_FALSE(found_source_existing);
}

TEST(MetadataTransferApi,
     PrepareTransferFileCanPreferExistingDestinationEmbeddedOverExistingSidecar)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    std::vector<std::byte> target_bytes;
    ASSERT_TRUE(build_test_target_with_existing_creator_tool_xmp(
        openmeta::TransferTargetFormat::Jpeg, "Target Embedded Existing",
        &target_bytes));
    const std::string target_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_bytes.data(), target_bytes.size())));

    const std::string output_path = unique_temp_path(".jpg");
    const size_t output_dot       = output_path.find_last_of('.');
    ASSERT_NE(output_dot, std::string::npos);
    const std::string output_sidecar_path
        = output_path.substr(0, output_dot) + ".xmp";

    std::vector<std::byte> existing_sidecar;
    ASSERT_TRUE(build_test_creator_tool_xmp_sidecar("Target Sidecar Existing",
                                                    &existing_sidecar));
    ASSERT_TRUE(write_bytes_file(
        output_sidecar_path,
        std::span<const std::byte>(existing_sidecar.data(),
                                   existing_sidecar.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format        = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.include_icc_app2     = false;
    options.prepare.include_iptc_app13   = false;
    options.prepare.xmp_include_existing = true;
    options.prepare.xmp_conflict_policy
        = openmeta::XmpConflictPolicy::ExistingWins;
    options.xmp_existing_sidecar_mode
        = openmeta::XmpExistingSidecarMode::MergeIfPresent;
    options.xmp_existing_sidecar_base_path = output_path;
    options.xmp_existing_destination_embedded_mode
        = openmeta::XmpExistingDestinationEmbeddedMode::MergeIfPresent;
    options.xmp_existing_destination_embedded_path = target_path;
    options.xmp_existing_destination_carrier_precedence
        = openmeta::XmpExistingDestinationCarrierPrecedence::EmbeddedWins;

    const openmeta::PrepareTransferFileResult result
        = openmeta::prepare_metadata_for_target_file(source_path.c_str(),
                                                     options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
    std::remove(output_sidecar_path.c_str());

    ASSERT_EQ(result.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_TRUE(result.xmp_existing_sidecar_loaded);
    EXPECT_EQ(result.xmp_existing_sidecar_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.xmp_existing_sidecar_path, output_sidecar_path);
    ASSERT_TRUE(result.xmp_existing_destination_embedded_loaded);
    EXPECT_EQ(result.xmp_existing_destination_embedded_status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.xmp_existing_destination_embedded_path, target_path);
    EXPECT_EQ(count_blocks_with_route(result.bundle, "jpeg:app1-xmp"), 1U);

    bool found_sidecar_existing = false;
    bool found_target_existing  = false;
    bool found_source_existing  = false;
    for (size_t i = 0; i < result.bundle.blocks.size(); ++i) {
        if (result.bundle.blocks[i].route != "jpeg:app1-xmp") {
            continue;
        }
        const std::span<const std::byte> payload(
            result.bundle.blocks[i].payload.data(),
            result.bundle.blocks[i].payload.size());
        found_sidecar_existing
            = payload_contains_ascii(payload, "Target Sidecar Existing");
        found_target_existing
            = payload_contains_ascii(payload, "Target Embedded Existing");
        found_source_existing
            = payload_contains_ascii(payload, "OpenMeta Transfer Source");
    }
    EXPECT_FALSE(found_sidecar_existing);
    EXPECT_TRUE(found_target_existing);
    EXPECT_FALSE(found_source_existing);
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileSidecarOnlyCanStripDestinationEmbeddedXmpAcrossTargets)
{
    struct Case final {
        openmeta::TransferTargetFormat format;
        const char* extension;
    };
    static const Case kCases[] = {
        { openmeta::TransferTargetFormat::Jpeg, ".jpg" },
        { openmeta::TransferTargetFormat::Tiff, ".tif" },
        { openmeta::TransferTargetFormat::Dng, ".dng" },
        { openmeta::TransferTargetFormat::Png, ".png" },
        { openmeta::TransferTargetFormat::Webp, ".webp" },
        { openmeta::TransferTargetFormat::Jp2, ".jp2" },
        { openmeta::TransferTargetFormat::Jxl, ".jxl" },
    };

    for (size_t i = 0; i < std::size(kCases); ++i) {
        SCOPED_TRACE(static_cast<int>(kCases[i].format));

        std::vector<std::byte> source_jpeg;
        ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
        const std::string source_path = unique_temp_path(".jpg");
        ASSERT_TRUE(write_bytes_file(
            source_path, std::span<const std::byte>(source_jpeg.data(),
                                                    source_jpeg.size())));

        std::vector<std::byte> target_bytes;
        ASSERT_TRUE(build_test_target_with_existing_creator_tool_xmp(
            kCases[i].format, "Target Embedded Existing", &target_bytes));
        const std::string target_path = unique_temp_path(kCases[i].extension);
        ASSERT_TRUE(write_bytes_file(
            target_path, std::span<const std::byte>(target_bytes.data(),
                                                    target_bytes.size())));

        openmeta::ExecutePreparedTransferFileOptions options;
        options.prepare.prepare.target_format = kCases[i].format;
        options.prepare.prepare.include_icc_app2   = false;
        options.prepare.prepare.include_iptc_app13 = false;
        options.edit_target_path                   = target_path;
        options.execute.edit_apply                 = true;
        options.xmp_writeback_mode
            = openmeta::XmpWritebackMode::SidecarOnly;
        options.xmp_destination_embedded_mode
            = openmeta::XmpDestinationEmbeddedMode::StripExisting;

        const openmeta::ExecutePreparedTransferFileResult result
            = openmeta::execute_prepared_transfer_file(source_path.c_str(),
                                                       options);
        std::remove(source_path.c_str());
        std::remove(target_path.c_str());

        ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
        ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
        ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);

        openmeta::MetaStore edited_store;
        ASSERT_TRUE(decode_transfer_roundtrip_store(
            std::span<const std::byte>(result.execute.edited_output.data(),
                                       result.execute.edited_output.size()),
            &edited_store));
        EXPECT_FALSE(store_has_text_entry(
            edited_store,
            xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
            "Target Embedded Existing"));
        EXPECT_FALSE(store_has_text_entry(
            edited_store,
            xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
            "OpenMeta Transfer Source"));

        openmeta::MetaStore sidecar_store;
        ASSERT_TRUE(decode_transfer_roundtrip_store(
            std::span<const std::byte>(result.xmp_sidecar_output.data(),
                                       result.xmp_sidecar_output.size()),
            &sidecar_store));
        EXPECT_TRUE(store_has_text_entry(
            sidecar_store,
            xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
            "OpenMeta Transfer Source"));
    }
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileEmbeddedOnlyCanStripDestinationSidecarAcrossTargets)
{
    struct Case final {
        openmeta::TransferTargetFormat format;
        const char* extension;
    };
    static const Case kCases[] = {
        { openmeta::TransferTargetFormat::Jpeg, ".jpg" },
        { openmeta::TransferTargetFormat::Tiff, ".tif" },
        { openmeta::TransferTargetFormat::Dng, ".dng" },
        { openmeta::TransferTargetFormat::Png, ".png" },
        { openmeta::TransferTargetFormat::Webp, ".webp" },
        { openmeta::TransferTargetFormat::Jp2, ".jp2" },
        { openmeta::TransferTargetFormat::Jxl, ".jxl" },
    };

    for (size_t i = 0; i < std::size(kCases); ++i) {
        SCOPED_TRACE(static_cast<int>(kCases[i].format));

        std::vector<std::byte> source_jpeg;
        ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
        const std::string source_path = unique_temp_path(".jpg");
        ASSERT_TRUE(write_bytes_file(
            source_path, std::span<const std::byte>(source_jpeg.data(),
                                                    source_jpeg.size())));

        std::vector<std::byte> target_bytes;
        ASSERT_TRUE(build_test_target_with_existing_creator_tool_xmp(
            kCases[i].format, "Target Embedded Existing", &target_bytes));
        const std::string target_path = unique_temp_path(kCases[i].extension);
        ASSERT_TRUE(write_bytes_file(
            target_path, std::span<const std::byte>(target_bytes.data(),
                                                    target_bytes.size())));

        const std::string output_path = unique_temp_path(kCases[i].extension);
        const size_t output_dot       = output_path.find_last_of('.');
        ASSERT_NE(output_dot, std::string::npos);
        const std::string output_sidecar_path
            = output_path.substr(0, output_dot) + ".xmp";

        std::vector<std::byte> existing_sidecar;
        ASSERT_TRUE(build_test_creator_tool_xmp_sidecar(
            "Target Sidecar Existing", &existing_sidecar));
        ASSERT_TRUE(write_bytes_file(
            output_sidecar_path,
            std::span<const std::byte>(existing_sidecar.data(),
                                       existing_sidecar.size())));

        openmeta::ExecutePreparedTransferFileOptions options;
        options.prepare.prepare.target_format = kCases[i].format;
        options.prepare.prepare.include_icc_app2   = false;
        options.prepare.prepare.include_iptc_app13 = false;
        options.edit_target_path                   = target_path;
        options.xmp_sidecar_base_path              = output_path;
        options.execute.edit_apply                 = true;
        options.xmp_writeback_mode
            = openmeta::XmpWritebackMode::EmbeddedOnly;
        options.xmp_destination_sidecar_mode
            = openmeta::XmpDestinationSidecarMode::StripExisting;

        const openmeta::ExecutePreparedTransferFileResult result
            = openmeta::execute_prepared_transfer_file(source_path.c_str(),
                                                       options);
        std::remove(source_path.c_str());
        std::remove(target_path.c_str());
        std::remove(output_sidecar_path.c_str());

        ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
        ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
        ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
        EXPECT_FALSE(result.xmp_sidecar_requested);
        EXPECT_TRUE(result.xmp_sidecar_output.empty());
        EXPECT_TRUE(result.xmp_sidecar_cleanup_requested);
        EXPECT_EQ(result.xmp_sidecar_cleanup_status,
                  openmeta::TransferStatus::Ok);
        EXPECT_EQ(result.xmp_sidecar_cleanup_path, output_sidecar_path);

        openmeta::MetaStore edited_store;
        ASSERT_TRUE(decode_transfer_roundtrip_store(
            std::span<const std::byte>(result.execute.edited_output.data(),
                                       result.execute.edited_output.size()),
            &edited_store));
        EXPECT_TRUE(store_has_text_entry(
            edited_store,
            xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
            "OpenMeta Transfer Source"));
        EXPECT_FALSE(store_has_text_entry(
            edited_store,
            xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
            "Target Sidecar Existing"));
    }
}

TEST(MetadataTransferApi,
     PersistPreparedTransferFileResultWritesOutputAndSidecar)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_jpeg = make_jpeg_with_segments({});
    const std::string target_path            = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_jpeg.data(), target_jpeg.size())));

    const std::string output_path = unique_temp_path(".jpg");
    const size_t output_dot       = output_path.find_last_of('.');
    ASSERT_NE(output_dot, std::string::npos);
    const std::string output_sidecar_path
        = output_path.substr(0, output_dot) + ".xmp";

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.xmp_sidecar_base_path              = output_path;
    options.execute.edit_apply                 = true;
    options.xmp_writeback_mode
        = openmeta::XmpWritebackMode::EmbeddedAndSidecar;

    const openmeta::ExecutePreparedTransferFileResult prepared
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);

    openmeta::PersistPreparedTransferFileOptions persist_options;
    persist_options.output_path = output_path;
    const openmeta::PersistPreparedTransferFileResult persisted
        = openmeta::persist_prepared_transfer_file_result(prepared,
                                                          persist_options);

    std::vector<std::byte> persisted_output;
    std::vector<std::byte> persisted_sidecar;
    ASSERT_TRUE(read_bytes_file(output_path, &persisted_output));
    ASSERT_TRUE(read_bytes_file(output_sidecar_path, &persisted_sidecar));

    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
    std::remove(output_path.c_str());
    std::remove(output_sidecar_path.c_str());

    ASSERT_EQ(persisted.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(persisted.output_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(persisted.output_path, output_path);
    EXPECT_EQ(persisted.xmp_sidecar_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(persisted.xmp_sidecar_path, output_sidecar_path);

    openmeta::MetaStore output_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(persisted_output.data(),
                                   persisted_output.size()),
        &output_store));
    EXPECT_TRUE(store_has_text_entry(
        output_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));

    openmeta::MetaStore sidecar_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(persisted_sidecar.data(),
                                   persisted_sidecar.size()),
        &sidecar_store));
    EXPECT_TRUE(store_has_text_entry(
        sidecar_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));
}

TEST(MetadataTransferApi,
     PersistPreparedTransferFileResultRemovesDestinationSidecar)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_jpeg = make_jpeg_with_segments({});
    const std::string target_path            = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_jpeg.data(), target_jpeg.size())));

    const std::string output_path = unique_temp_path(".jpg");
    const size_t output_dot       = output_path.find_last_of('.');
    ASSERT_NE(output_dot, std::string::npos);
    const std::string output_sidecar_path
        = output_path.substr(0, output_dot) + ".xmp";

    std::vector<std::byte> existing_sidecar;
    ASSERT_TRUE(build_test_creator_tool_xmp_sidecar("Target Sidecar Existing",
                                                    &existing_sidecar));
    ASSERT_TRUE(write_bytes_file(
        output_sidecar_path,
        std::span<const std::byte>(existing_sidecar.data(),
                                   existing_sidecar.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.xmp_sidecar_base_path              = output_path;
    options.execute.edit_apply                 = true;
    options.xmp_writeback_mode
        = openmeta::XmpWritebackMode::EmbeddedOnly;
    options.xmp_destination_sidecar_mode
        = openmeta::XmpDestinationSidecarMode::StripExisting;

    const openmeta::ExecutePreparedTransferFileResult prepared
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);

    openmeta::PersistPreparedTransferFileOptions persist_options;
    persist_options.output_path = output_path;
    const openmeta::PersistPreparedTransferFileResult persisted
        = openmeta::persist_prepared_transfer_file_result(prepared,
                                                          persist_options);

    std::vector<std::byte> persisted_output;
    ASSERT_TRUE(read_bytes_file(output_path, &persisted_output));

    std::FILE* sidecar_file = std::fopen(output_sidecar_path.c_str(), "rb");
    const bool sidecar_still_exists = sidecar_file != nullptr;
    if (sidecar_file) {
        std::fclose(sidecar_file);
    }

    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
    std::remove(output_path.c_str());
    std::remove(output_sidecar_path.c_str());

    ASSERT_EQ(persisted.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(persisted.output_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(persisted.xmp_sidecar_cleanup_status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(persisted.xmp_sidecar_cleanup_path, output_sidecar_path);
    EXPECT_TRUE(persisted.xmp_sidecar_cleanup_removed);
    EXPECT_FALSE(sidecar_still_exists);

    openmeta::MetaStore output_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(persisted_output.data(),
                                   persisted_output.size()),
        &output_store));
    EXPECT_TRUE(store_has_text_entry(
        output_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));
}

TEST(MetadataTransferApi,
     PersistPreparedTransferFileResultCanPersistAuxiliaryOutputsAfterCallerWrite)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_jpeg = make_jpeg_with_segments({});
    const std::string target_path            = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_jpeg.data(), target_jpeg.size())));

    const std::string output_path = unique_temp_path(".jpg");
    const size_t output_dot       = output_path.find_last_of('.');
    ASSERT_NE(output_dot, std::string::npos);
    const std::string output_sidecar_path
        = output_path.substr(0, output_dot) + ".xmp";

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.xmp_sidecar_base_path              = output_path;
    options.execute.edit_apply                 = true;
    options.xmp_writeback_mode
        = openmeta::XmpWritebackMode::EmbeddedAndSidecar;

    const openmeta::ExecutePreparedTransferFileResult prepared
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    ASSERT_TRUE(write_bytes_file(
        output_path,
        std::span<const std::byte>(prepared.execute.edited_output.data(),
                                   prepared.execute.edited_output.size())));

    openmeta::PersistPreparedTransferFileOptions persist_options;
    persist_options.output_path            = output_path;
    persist_options.write_output           = false;
    persist_options.prewritten_output_bytes = static_cast<uint64_t>(
        prepared.execute.edited_output.size());
    const openmeta::PersistPreparedTransferFileResult persisted
        = openmeta::persist_prepared_transfer_file_result(prepared,
                                                          persist_options);

    std::vector<std::byte> persisted_sidecar;
    ASSERT_TRUE(read_bytes_file(output_sidecar_path, &persisted_sidecar));

    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
    std::remove(output_path.c_str());
    std::remove(output_sidecar_path.c_str());

    ASSERT_EQ(persisted.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(persisted.output_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(persisted.output_bytes,
              static_cast<uint64_t>(prepared.execute.edited_output.size()));
    EXPECT_EQ(persisted.xmp_sidecar_status, openmeta::TransferStatus::Ok);
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileTiffRoundTripsSourceMetadata)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_tiff = make_minimal_tiff_little_endian();
    const std::string target_path            = unique_temp_path(".tif");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_tiff.data(), target_tiff.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Tiff;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    EXPECT_TRUE(decoded_transfer_roundtrip_has_expected_fields(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size())));
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileDngRoundTripsSourceMetadata)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_dng = make_minimal_tiff_little_endian();
    const std::string target_path           = unique_temp_path(".dng");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_dng.data(), target_dng.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format      = openmeta::TransferTargetFormat::Dng;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.prepared.bundle.target_format,
              openmeta::TransferTargetFormat::Dng);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    EXPECT_TRUE(decoded_transfer_roundtrip_has_expected_fields(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size())));
    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size()),
        &decoded));
    const std::array<uint8_t, 4> dng_version = { 1U, 6U, 0U, 0U };
    EXPECT_TRUE(store_has_u8_array_entry(
        decoded, exif_key_view("ifd0", 0xC612U),
        std::span<const uint8_t>(dng_version.data(), dng_version.size())));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileDngExistingTargetModeRequiresEditTargetPath)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format      = openmeta::TransferTargetFormat::Dng;
    options.prepare.prepare.dng_target_mode
        = openmeta::DngTargetMode::ExistingTarget;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.prepared.bundle.dng_target_mode,
              openmeta::DngTargetMode::ExistingTarget);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.execute.edit_plan_status,
              openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.execute.edit_apply.status,
              openmeta::TransferStatus::InvalidArgument);
    EXPECT_NE(result.execute.edit_plan_message.find("edit_target_path"),
              std::string::npos);
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileDngTemplateTargetModeRequiresEditTargetPath)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format      = openmeta::TransferTargetFormat::Dng;
    options.prepare.prepare.dng_target_mode
        = openmeta::DngTargetMode::TemplateTarget;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.prepared.bundle.dng_target_mode,
              openmeta::DngTargetMode::TemplateTarget);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.execute.edit_plan_status,
              openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.execute.edit_apply.status,
              openmeta::TransferStatus::InvalidArgument);
    EXPECT_NE(result.execute.edit_plan_message.find("edit_target_path"),
              std::string::npos);
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileDngMinimalFreshScaffoldCompilesWithoutEditTarget)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format      = openmeta::TransferTargetFormat::Dng;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.prepared.bundle.dng_target_mode,
              openmeta::DngTargetMode::MinimalFreshScaffold);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_FALSE(result.execute.tiff_tag_summary.empty());
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileDngPreservesExistingTargetCoreTags)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_dng
        = make_minimal_dng_like_tiff_little_endian();
    const std::string target_path = unique_temp_path(".dng");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_dng.data(), target_dng.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format      = openmeta::TransferTargetFormat::Dng;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.prepared.bundle.target_format,
              openmeta::TransferTargetFormat::Dng);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size()),
        &decoded));
    const std::array<uint8_t, 4> dng_version = { 1U, 6U, 0U, 0U };
    EXPECT_TRUE(store_has_u8_array_entry(
        decoded, exif_key_view("ifd0", 0xC612U),
        std::span<const uint8_t>(dng_version.data(), dng_version.size())));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0100U),
                                           4000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0101U),
                                           3000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0100U),
                                           320U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0101U),
                                           240U));
    EXPECT_TRUE(decoded_transfer_roundtrip_has_expected_fields(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size())));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileTiffRoundTripsDngSourceMetadata)
{
    const std::vector<std::byte> source_dng
        = make_minimal_dng_like_tiff_little_endian();
    const std::string source_path = unique_temp_path(".dng");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_dng.data(), source_dng.size())));

    const std::vector<std::byte> target_tiff = make_minimal_tiff_little_endian();
    const std::string target_path            = unique_temp_path(".tif");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_tiff.data(), target_tiff.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format      = openmeta::TransferTargetFormat::Tiff;
    options.prepare.prepare.include_xmp_app1   = false;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size()),
        &decoded));
    const std::array<uint8_t, 4> dng_version = { 1U, 6U, 0U, 0U };
    EXPECT_TRUE(store_has_u8_array_entry(
        decoded, exif_key_view("ifd0", 0xC612U),
        std::span<const uint8_t>(dng_version.data(), dng_version.size())));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0100U),
                                           4000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0101U),
                                           3000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0100U),
                                           320U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0101U),
                                           240U));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileDngRoundTripsDngSourceMetadata)
{
    const std::vector<std::byte> source_dng
        = make_minimal_dng_like_tiff_little_endian();
    const std::string source_path = unique_temp_path(".dng");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_dng.data(), source_dng.size())));

    const std::vector<std::byte> target_dng = make_minimal_tiff_little_endian();
    const std::string target_path           = unique_temp_path(".dng");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_dng.data(), target_dng.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format      = openmeta::TransferTargetFormat::Dng;
    options.prepare.prepare.include_xmp_app1   = false;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.prepared.bundle.target_format,
              openmeta::TransferTargetFormat::Dng);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size()),
        &decoded));
    const std::array<uint8_t, 4> dng_version = { 1U, 6U, 0U, 0U };
    EXPECT_TRUE(store_has_u8_array_entry(
        decoded, exif_key_view("ifd0", 0xC612U),
        std::span<const uint8_t>(dng_version.data(), dng_version.size())));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0100U),
                                           4000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0101U),
                                           3000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0100U),
                                           320U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0101U),
                                           240U));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileTiffMergesDngSourceIntoExistingPreviewAuxTarget)
{
    const std::vector<std::byte> source_dng
        = make_minimal_dng_like_tiff_little_endian();
    const std::string source_path = unique_temp_path(".dng");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_dng.data(), source_dng.size())));

    const std::vector<std::byte> target_tiff
        = make_minimal_dng_merge_target_tiff_little_endian();
    const std::string target_path = unique_temp_path(".tif");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_tiff.data(), target_tiff.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format      = openmeta::TransferTargetFormat::Tiff;
    options.prepare.prepare.include_xmp_app1   = false;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());

    const std::span<const std::byte> bytes(result.execute.edited_output.data(),
                                           result.execute.edited_output.size());
    const uint32_t ifd0_off = read_u32le(bytes, 4U);
    const uint16_t ifd0_count = read_u16le(bytes, static_cast<size_t>(ifd0_off));
    const uint32_t ifd1_off = read_u32le(
        bytes, static_cast<size_t>(ifd0_off) + 2U
                   + static_cast<size_t>(ifd0_count) * 12U);
    ASSERT_NE(ifd1_off, 0U);

    uint32_t ifd1_width = 0U;
    uint32_t ifd1_height = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd1_off, 0x0100U, nullptr,
                                       nullptr, &ifd1_width));
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd1_off, 0x0101U, nullptr,
                                       nullptr, &ifd1_height));
    EXPECT_EQ(ifd1_width, 320U);
    EXPECT_EQ(ifd1_height, 240U);

    const uint16_t ifd1_count = read_u16le(bytes, static_cast<size_t>(ifd1_off));
    const uint32_t ifd1_next_off = read_u32le(
        bytes, static_cast<size_t>(ifd1_off) + 2U
                   + static_cast<size_t>(ifd1_count) * 12U);
    EXPECT_GE(ifd1_next_off, static_cast<uint32_t>(target_tiff.size()));

    uint32_t tail_ifd_width = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd1_next_off, 0x0100U, nullptr,
                                       nullptr,
                                       &tail_ifd_width));
    EXPECT_EQ(tail_ifd_width, 222U);

    uint32_t subifd_count = 0U;
    uint32_t subifd_array_off = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd0_off, 0x014AU, nullptr,
                                       &subifd_count, &subifd_array_off));
    EXPECT_EQ(subifd_count, 2U);

    const uint32_t subifd0_off = read_u32le(bytes, subifd_array_off + 0U);
    const uint32_t subifd1_off = read_u32le(bytes, subifd_array_off + 4U);
    uint32_t subifd0_width = 0U;
    uint32_t subifd0_height = 0U;
    uint32_t subifd1_width = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, subifd0_off, 0x0100U, nullptr,
                                       nullptr, &subifd0_width));
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, subifd0_off, 0x0101U, nullptr,
                                       nullptr, &subifd0_height));
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, subifd1_off, 0x0100U, nullptr,
                                       nullptr, &subifd1_width));
    EXPECT_EQ(subifd0_width, 4000U);
    EXPECT_EQ(subifd0_height, 3000U);
    EXPECT_EQ(subifd1_width, 777U);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(bytes, &decoded));
    const std::array<uint8_t, 4> dng_version = { 1U, 6U, 0U, 0U };
    EXPECT_TRUE(store_has_u8_array_entry(
        decoded, exif_key_view("ifd0", 0xC612U),
        std::span<const uint8_t>(dng_version.data(), dng_version.size())));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0100U),
                                           4000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0101U),
                                           3000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd1", 0x0100U),
                                           777U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0100U),
                                           320U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0101U),
                                           240U));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileDngMergesDngSourceIntoExistingPreviewAuxTarget)
{
    const std::vector<std::byte> source_dng
        = make_minimal_dng_like_tiff_little_endian();
    const std::string source_path = unique_temp_path(".dng");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_dng.data(), source_dng.size())));

    const std::vector<std::byte> target_dng
        = make_minimal_dng_merge_target_tiff_little_endian();
    const std::string target_path = unique_temp_path(".dng");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_dng.data(), target_dng.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format      = openmeta::TransferTargetFormat::Dng;
    options.prepare.prepare.include_xmp_app1   = false;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.prepared.bundle.target_format,
              openmeta::TransferTargetFormat::Dng);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());

    const std::span<const std::byte> bytes(result.execute.edited_output.data(),
                                           result.execute.edited_output.size());
    const uint32_t ifd0_off = read_u32le(bytes, 4U);
    const uint16_t ifd0_count = read_u16le(bytes, static_cast<size_t>(ifd0_off));
    const uint32_t ifd1_off = read_u32le(
        bytes, static_cast<size_t>(ifd0_off) + 2U
                   + static_cast<size_t>(ifd0_count) * 12U);
    ASSERT_NE(ifd1_off, 0U);

    uint32_t ifd1_width = 0U;
    uint32_t ifd1_height = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd1_off, 0x0100U, nullptr,
                                       nullptr, &ifd1_width));
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd1_off, 0x0101U, nullptr,
                                       nullptr, &ifd1_height));
    EXPECT_EQ(ifd1_width, 320U);
    EXPECT_EQ(ifd1_height, 240U);

    const uint16_t ifd1_count = read_u16le(bytes, static_cast<size_t>(ifd1_off));
    const uint32_t ifd1_next_off = read_u32le(
        bytes, static_cast<size_t>(ifd1_off) + 2U
                   + static_cast<size_t>(ifd1_count) * 12U);
    EXPECT_GE(ifd1_next_off, static_cast<uint32_t>(target_dng.size()));

    uint32_t tail_ifd_width = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd1_next_off, 0x0100U, nullptr,
                                       nullptr, &tail_ifd_width));
    EXPECT_EQ(tail_ifd_width, 222U);

    uint32_t subifd_count = 0U;
    uint32_t subifd_array_off = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, ifd0_off, 0x014AU, nullptr,
                                       &subifd_count, &subifd_array_off));
    EXPECT_EQ(subifd_count, 2U);

    const uint32_t subifd0_off = read_u32le(bytes, subifd_array_off + 0U);
    const uint32_t subifd1_off = read_u32le(bytes, subifd_array_off + 4U);
    uint32_t subifd0_width = 0U;
    uint32_t subifd0_height = 0U;
    uint32_t subifd1_width = 0U;
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, subifd0_off, 0x0100U, nullptr,
                                       nullptr, &subifd0_width));
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, subifd0_off, 0x0101U, nullptr,
                                       nullptr, &subifd0_height));
    ASSERT_TRUE(find_tiff_tag_entry_le(bytes, subifd1_off, 0x0100U, nullptr,
                                       nullptr, &subifd1_width));
    EXPECT_EQ(subifd0_width, 4000U);
    EXPECT_EQ(subifd0_height, 3000U);
    EXPECT_EQ(subifd1_width, 777U);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(bytes, &decoded));
    const std::array<uint8_t, 4> dng_version = { 1U, 6U, 0U, 0U };
    EXPECT_TRUE(store_has_u8_array_entry(
        decoded, exif_key_view("ifd0", 0xC612U),
        std::span<const uint8_t>(dng_version.data(), dng_version.size())));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0100U),
                                           4000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0101U),
                                           3000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd1", 0x0100U),
                                           777U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0100U),
                                           320U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0101U),
                                           240U));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileBigTiffRoundTripsSourceMetadata)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_tiff = make_minimal_bigtiff_little_endian();
    const std::string target_path            = unique_temp_path(".tif");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_tiff.data(), target_tiff.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Tiff;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    EXPECT_EQ(read_u16le(std::span<const std::byte>(result.execute.edited_output.data(),
                                                    result.execute.edited_output.size()),
                         2U),
              43U);
    EXPECT_TRUE(decoded_transfer_roundtrip_has_expected_fields(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size())));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileBigTiffSidecarOnlyCanStripExistingDestinationEmbeddedXmp)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    std::vector<std::byte> target_tiff;
    ASSERT_TRUE(build_test_bigtiff_with_creator_tool_xmp(
        "Target Embedded Existing", &target_tiff));
    const std::string target_path = unique_temp_path(".tif");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_tiff.data(), target_tiff.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Tiff;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;
    options.xmp_writeback_mode
        = openmeta::XmpWritebackMode::SidecarOnly;
    options.xmp_destination_embedded_mode
        = openmeta::XmpDestinationEmbeddedMode::StripExisting;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    ASSERT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    ASSERT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    ASSERT_FALSE(result.xmp_sidecar_output.empty());

    const std::span<const std::byte> edited_bytes(
        result.execute.edited_output.data(), result.execute.edited_output.size());
    ASSERT_GE(edited_bytes.size(), 16U);
    EXPECT_EQ(read_u16le(edited_bytes, 2U), 43U);

    const uint64_t ifd0_off = read_u64le(edited_bytes, 8U);
    ASSERT_NE(ifd0_off, 0U);
    uint64_t ignored_value = 0U;
    EXPECT_FALSE(find_bigtiff_tag_entry_le(edited_bytes, ifd0_off, 700U,
                                           nullptr, nullptr,
                                           &ignored_value));

    openmeta::MetaStore edited_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(edited_bytes, &edited_store));
    EXPECT_FALSE(store_has_text_entry(
        edited_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "Target Embedded Existing"));
    EXPECT_FALSE(store_has_text_entry(
        edited_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));

    openmeta::MetaStore sidecar_store;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.xmp_sidecar_output.data(),
                                   result.xmp_sidecar_output.size()),
        &sidecar_store));
    EXPECT_TRUE(store_has_text_entry(
        sidecar_store,
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"),
        "OpenMeta Transfer Source"));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileBigTiffRoundTripsDngSourceMetadata)
{
    const std::vector<std::byte> source_dng
        = make_minimal_dng_like_tiff_little_endian();
    const std::string source_path = unique_temp_path(".dng");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_dng.data(), source_dng.size())));

    const std::vector<std::byte> target_tiff
        = make_minimal_bigtiff_little_endian();
    const std::string target_path = unique_temp_path(".tif");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_tiff.data(), target_tiff.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format      = openmeta::TransferTargetFormat::Tiff;
    options.prepare.prepare.include_xmp_app1   = false;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    EXPECT_EQ(read_u16le(std::span<const std::byte>(result.execute.edited_output.data(),
                                                    result.execute.edited_output.size()),
                         2U),
              43U);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size()),
        &decoded));
    const std::array<uint8_t, 4> dng_version = { 1U, 6U, 0U, 0U };
    EXPECT_TRUE(store_has_u8_array_entry(
        decoded, exif_key_view("ifd0", 0xC612U),
        std::span<const uint8_t>(dng_version.data(), dng_version.size())));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0100U),
                                           4000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0101U),
                                           3000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0100U),
                                           320U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0101U),
                                           240U));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileBigTiffMergesDngSourceIntoExistingPreviewAuxTarget)
{
    const std::vector<std::byte> source_dng
        = make_minimal_dng_like_tiff_little_endian();
    const std::string source_path = unique_temp_path(".dng");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_dng.data(), source_dng.size())));

    const std::vector<std::byte> target_tiff
        = make_minimal_dng_merge_target_bigtiff_little_endian();
    const std::string target_path = unique_temp_path(".tif");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_tiff.data(), target_tiff.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format      = openmeta::TransferTargetFormat::Tiff;
    options.prepare.prepare.include_xmp_app1   = false;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());

    const std::span<const std::byte> bytes(result.execute.edited_output.data(),
                                           result.execute.edited_output.size());
    const uint64_t ifd0_off = read_u64le(bytes, 8U);
    const uint64_t ifd0_count = read_u64le(bytes, static_cast<size_t>(ifd0_off));
    const uint64_t ifd1_off = read_u64le(
        bytes, static_cast<size_t>(ifd0_off) + 8U
                   + static_cast<size_t>(ifd0_count) * 20U);
    ASSERT_NE(ifd1_off, 0U);

    uint64_t ifd1_width = 0U;
    uint64_t ifd1_height = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd1_off, 0x0100U, nullptr,
                                          nullptr, &ifd1_width));
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd1_off, 0x0101U, nullptr,
                                          nullptr, &ifd1_height));
    EXPECT_EQ(ifd1_width, 320U);
    EXPECT_EQ(ifd1_height, 240U);

    const uint64_t ifd1_count = read_u64le(bytes, static_cast<size_t>(ifd1_off));
    const uint64_t ifd1_next_off = read_u64le(
        bytes, static_cast<size_t>(ifd1_off) + 8U
                   + static_cast<size_t>(ifd1_count) * 20U);
    EXPECT_GE(ifd1_next_off, static_cast<uint64_t>(target_tiff.size()));

    uint64_t tail_ifd_width = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd1_next_off, 0x0100U, nullptr,
                                          nullptr, &tail_ifd_width));
    EXPECT_EQ(tail_ifd_width, 222U);

    uint64_t subifd_count = 0U;
    uint64_t subifd_array_off = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, ifd0_off, 0x014AU, nullptr,
                                          &subifd_count, &subifd_array_off));
    EXPECT_EQ(subifd_count, 2U);

    const uint64_t subifd0_off = read_u64le(
        bytes, static_cast<size_t>(subifd_array_off) + 0U);
    const uint64_t subifd1_off = read_u64le(
        bytes, static_cast<size_t>(subifd_array_off) + 8U);
    uint64_t subifd0_width = 0U;
    uint64_t subifd0_height = 0U;
    uint64_t subifd1_width = 0U;
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, subifd0_off, 0x0100U,
                                          nullptr, nullptr, &subifd0_width));
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, subifd0_off, 0x0101U,
                                          nullptr, nullptr, &subifd0_height));
    ASSERT_TRUE(find_bigtiff_tag_entry_le(bytes, subifd1_off, 0x0100U,
                                          nullptr, nullptr, &subifd1_width));
    EXPECT_EQ(subifd0_width, 4000U);
    EXPECT_EQ(subifd0_height, 3000U);
    EXPECT_EQ(subifd1_width, 777U);

    openmeta::MetaStore decoded;
    ASSERT_TRUE(decode_transfer_roundtrip_store(bytes, &decoded));
    const std::array<uint8_t, 4> dng_version = { 1U, 6U, 0U, 0U };
    EXPECT_TRUE(store_has_u8_array_entry(
        decoded, exif_key_view("ifd0", 0xC612U),
        std::span<const uint8_t>(dng_version.data(), dng_version.size())));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0100U),
                                           4000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd0", 0x0101U),
                                           3000U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("subifd1", 0x0100U),
                                           777U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0100U),
                                           320U));
    EXPECT_TRUE(store_has_u32_scalar_entry(decoded,
                                           exif_key_view("ifd1", 0x0101U),
                                           240U));
}

TEST(MetadataTransferApi, ExecutePreparedTransferFilePngRoundTripsSourceMetadata)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_png = build_minimal_png_file({});
    const std::string target_path           = unique_temp_path(".png");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_png.data(), target_png.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Png;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    EXPECT_TRUE(decoded_transfer_roundtrip_has_expected_fields(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size())));
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileWebpRoundTripsSourceMetadata)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_webp = build_minimal_webp_file({}, 0U);
    const std::string target_path            = unique_temp_path(".webp");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_webp.data(), target_webp.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Webp;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    EXPECT_TRUE(decoded_transfer_roundtrip_has_expected_fields(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size())));
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileJp2RoundTripsSourceMetadata)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_jp2 = build_minimal_jp2_file({});
    const std::string target_path           = unique_temp_path(".jp2");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_jp2.data(), target_jp2.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jp2;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    EXPECT_TRUE(decoded_transfer_roundtrip_has_expected_fields(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size())));
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileJxlRoundTripsSourceMetadata)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_jxl = make_minimal_jxl_file();
    const std::string target_path           = unique_temp_path(".jxl");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_jxl.data(), target_jxl.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jxl;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    EXPECT_TRUE(decoded_transfer_roundtrip_has_expected_fields(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size())));
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileHeifRoundTripsSourceMetadata)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_bmff = make_minimal_heif_file();
    const std::string target_path            = unique_temp_path(".heic");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_bmff.data(), target_bmff.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Heif;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    EXPECT_TRUE(decoded_transfer_roundtrip_has_expected_fields(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size())));
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileAvifRoundTripsSourceMetadata)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_bmff = make_minimal_avif_file();
    const std::string target_path            = unique_temp_path(".avif");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_bmff.data(), target_bmff.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Avif;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    EXPECT_TRUE(decoded_transfer_roundtrip_has_expected_fields(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size())));
}

TEST(MetadataTransferApi, ExecutePreparedTransferFileCr3RoundTripsSourceMetadata)
{
    std::vector<std::byte> source_jpeg;
    ASSERT_TRUE(build_test_transfer_source_jpeg_bytes(&source_jpeg));
    const std::string source_path = unique_temp_path(".jpg");
    ASSERT_TRUE(write_bytes_file(
        source_path,
        std::span<const std::byte>(source_jpeg.data(), source_jpeg.size())));

    const std::vector<std::byte> target_bmff = make_minimal_cr3_file();
    const std::string target_path            = unique_temp_path(".cr3");
    ASSERT_TRUE(write_bytes_file(
        target_path,
        std::span<const std::byte>(target_bmff.data(), target_bmff.size())));

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Cr3;
    options.prepare.prepare.include_icc_app2   = false;
    options.prepare.prepare.include_iptc_app13 = false;
    options.edit_target_path                   = target_path;
    options.execute.edit_apply                 = true;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(source_path.c_str(), options);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.execute.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.execute.edited_output.empty());
    EXPECT_TRUE(decoded_transfer_roundtrip_has_expected_fields(
        std::span<const std::byte>(result.execute.edited_output.data(),
                                   result.execute.edited_output.size())));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferFileStagesSignedC2paFromPackage)
{
    const std::vector<std::byte> jpeg = make_jpeg_with_app11_jumbf("c2pa");
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions prepare_options;
    prepare_options.prepare.include_exif_app1  = false;
    prepare_options.prepare.include_xmp_app1   = false;
    prepare_options.prepare.include_icc_app2   = false;
    prepare_options.prepare.include_iptc_app13 = false;
    prepare_options.prepare.profile.c2pa
        = openmeta::TransferPolicyAction::Rewrite;

    const openmeta::PrepareTransferFileResult prepared
        = openmeta::prepare_metadata_for_target_file(path.c_str(),
                                                     prepare_options);
    ASSERT_EQ(prepared.prepare.status, openmeta::TransferStatus::Unsupported);

    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    const openmeta::PreparedTransferC2paSignerInput input
        = make_test_c2pa_signer_input(
            std::span<const std::byte>(logical.data(), logical.size()));
    openmeta::PreparedTransferC2paSignedPackage package;
    ASSERT_EQ(openmeta::build_prepared_c2pa_signed_package(prepared.bundle,
                                                           input, &package),
              openmeta::TransferStatus::Ok);

    openmeta::ExecutePreparedTransferFileOptions options;
    options.prepare                      = prepare_options;
    options.c2pa_stage_requested         = true;
    options.c2pa_signed_package_provided = true;
    options.c2pa_signed_package          = package;

    const openmeta::ExecutePreparedTransferFileResult result
        = openmeta::execute_prepared_transfer_file(path.c_str(), options);
    std::remove(path.c_str());

    EXPECT_EQ(result.prepared.file_status, openmeta::TransferFileStatus::Ok);
    EXPECT_EQ(result.prepared.prepare.status,
              openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(result.execute.c2pa_stage_requested);
    EXPECT_EQ(result.execute.c2pa_stage_validation.status,
              openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.execute.c2pa_stage_validation.payload_kind,
              openmeta::TransferC2paSignedPayloadKind::ContentBound);
    EXPECT_EQ(result.execute.c2pa_stage.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.prepared.bundle.c2pa_rewrite.state,
              openmeta::TransferC2paRewriteState::Ready);
}

TEST(MetadataTransferApi, ApplyTiffEditRejectsPlanMismatch)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;
    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 'x' } };
    bundle.blocks.push_back(xmp);

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    ASSERT_EQ(plan.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferBundle changed = bundle;
    changed.blocks.clear();

    std::vector<std::byte> out;
    const openmeta::EmitTransferResult applied
        = openmeta::apply_prepared_bundle_tiff_edit(
            std::span<const std::byte>(input.data(), input.size()), changed,
            plan, &out);
    EXPECT_EQ(applied.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(applied.code, openmeta::EmitTransferCode::PlanMismatch);
    EXPECT_EQ(applied.errors, 1U);
}

TEST(MetadataTransferApi, PlanTiffEditRejectsNoUpdates)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    const std::vector<std::byte> input = make_minimal_tiff_little_endian();
    const openmeta::TiffEditPlan plan = openmeta::plan_prepared_bundle_tiff_edit(
        std::span<const std::byte>(input.data(), input.size()), bundle);
    EXPECT_EQ(plan.status, openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(plan.message.find("no tiff updates") != std::string::npos);
}

TEST(MetadataTransferApi, PrepareUnsupportedWhenExifOnlyUnsupportedIfd)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry e;
    e.key   = openmeta::make_exif_tag_key(store.arena(), "mk_test", 0x0001U);
    e.value = openmeta::make_text(store.arena(), "v",
                                  openmeta::TextEncoding::Ascii);
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_xmp_app1 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::PrepareTransferCode::ExifPackFailed);
    EXPECT_EQ(result.warnings, 1U);
    EXPECT_TRUE(result.message.find("exif app1 packer could not serialize")
                != std::string::npos);
    EXPECT_TRUE(bundle.blocks.empty());
}

TEST(MetadataTransferApi, PrepareBuildsJpegIccApp2Block)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    std::vector<std::byte> icc_tag_bytes(24U, std::byte { 0x11 });
    openmeta::Entry e;
    e.key   = openmeta::make_icc_tag_key(0x64657363U);  // 'desc'
    e.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(icc_tag_bytes.data(), icc_tag_bytes.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_iptc_app13 = false;
    request.include_icc_app2   = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app2-icc");
    ASSERT_GE(bundle.blocks[0].payload.size(), 20U);
    EXPECT_EQ(bundle.blocks[0].payload[0], std::byte { 'I' });
    EXPECT_EQ(bundle.blocks[0].payload[1], std::byte { 'C' });
    EXPECT_EQ(bundle.blocks[0].payload[2], std::byte { 'C' });
    EXPECT_EQ(bundle.blocks[0].payload[11], std::byte { 0x00 });
    EXPECT_EQ(bundle.blocks[0].payload[12], std::byte { 0x01 });
    EXPECT_EQ(bundle.blocks[0].payload[13], std::byte { 0x01 });
}

TEST(MetadataTransferApi, PrepareBuildsMultiChunkIccApp2Blocks)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    std::vector<std::byte> icc_tag_bytes(90000U, std::byte { 0x22 });
    openmeta::Entry e;
    e.key   = openmeta::make_icc_tag_key(0x64657363U);  // 'desc'
    e.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(icc_tag_bytes.data(), icc_tag_bytes.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_iptc_app13 = false;
    request.include_icc_app2   = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_GE(bundle.blocks.size(), 2U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app2-icc");
    EXPECT_EQ(bundle.blocks[1].route, "jpeg:app2-icc");
    EXPECT_EQ(bundle.blocks[0].payload[12], std::byte { 0x01 });
    EXPECT_EQ(bundle.blocks[1].payload[12], std::byte { 0x02 });
    EXPECT_EQ(bundle.blocks[0].payload[13], bundle.blocks[1].payload[13]);
}

TEST(MetadataTransferApi, PrepareBuildsJxlIccProfile)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    std::vector<std::byte> icc_tag_bytes(24U, std::byte { 0x33 });
    openmeta::Entry e;
    e.key   = openmeta::make_icc_tag_key(0x64657363U);  // 'desc'
    e.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(icc_tag_bytes.data(), icc_tag_bytes.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Jxl;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = true;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::Icc);
    EXPECT_EQ(bundle.blocks[0].route, "jxl:icc-profile");
    EXPECT_EQ(bundle.blocks[0].box_type, (std::array<char, 4> {}));
    EXPECT_GE(bundle.blocks[0].payload.size(), 24U);
    EXPECT_EQ(bundle.blocks[0].payload[0], std::byte { 0x00 });
}

TEST(MetadataTransferApi, BuildPreparedJxlEncoderHandoffViewWithIcc)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jxl:icc-profile";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 }, std::byte { 0x43 },
                    std::byte { 0x50 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jxl:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                     std::byte { 0x06 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jxl:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedJxlEncoderHandoffView view;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_jxl_encoder_handoff_view(bundle, &view);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(result.emitted, 3U);
    EXPECT_EQ(view.contract_version, bundle.contract_version);
    EXPECT_TRUE(view.has_icc_profile);
    EXPECT_EQ(view.icc_block_index, 0U);
    EXPECT_EQ(view.icc_profile_bytes, 4U);
    ASSERT_EQ(view.icc_profile.size(), 4U);
    EXPECT_EQ(view.icc_profile[0], std::byte { 0x49 });
    EXPECT_EQ(view.box_count, 2U);
    EXPECT_EQ(view.box_payload_bytes, 7U);
}

TEST(MetadataTransferApi, BuildPreparedJxlEncoderHandoffViewRejectsMultipleIcc)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock icc_a;
    icc_a.route   = "jxl:icc-profile";
    icc_a.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(icc_a);

    openmeta::PreparedTransferBlock icc_b;
    icc_b.route   = "jxl:icc-profile";
    icc_b.payload = { std::byte { 0x02 } };
    bundle.blocks.push_back(icc_b);

    openmeta::PreparedJxlEncoderHandoffView view;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_jxl_encoder_handoff_view(bundle, &view);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(result.failed_block_index, 1U);
    EXPECT_TRUE(result.message.find("multiple jxl icc profiles")
                != std::string::npos);
    EXPECT_TRUE(view.has_icc_profile);
    EXPECT_EQ(view.icc_block_index, 0U);
}

TEST(MetadataTransferApi, BuildPreparedJxlEncoderHandoffViewRejectsNonJxlBundle)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedJxlEncoderHandoffView view;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_jxl_encoder_handoff_view(bundle, &view);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(result.errors, 1U);
}

TEST(MetadataTransferApi, BuildPreparedJxlEncoderHandoffOwnsIcc)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jxl:icc-profile";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 }, std::byte { 0x43 },
                    std::byte { 0x50 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jxl:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload  = { std::byte { 0x00 }, std::byte { 0x00 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedJxlEncoderHandoff handoff;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_jxl_encoder_handoff(bundle, &handoff);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_TRUE(handoff.has_icc_profile);
    EXPECT_EQ(handoff.icc_block_index, 0U);
    EXPECT_EQ(handoff.box_count, 1U);
    ASSERT_EQ(handoff.icc_profile.size(), 4U);

    bundle.blocks[0].payload[0] = std::byte { 0x00 };
    EXPECT_EQ(handoff.icc_profile[0], std::byte { 0x49 });
}

TEST(MetadataTransferApi, SerializePreparedJxlEncoderHandoffRoundTrip)
{
    openmeta::PreparedJxlEncoderHandoff handoff;
    handoff.has_icc_profile   = true;
    handoff.icc_block_index   = 3U;
    handoff.box_count         = 2U;
    handoff.box_payload_bytes = 7U;
    handoff.icc_profile       = { std::byte { 0x49 }, std::byte { 0x43 },
                                  std::byte { 0x43 }, std::byte { 0x50 } };

    std::vector<std::byte> bytes;
    const openmeta::PreparedJxlEncoderHandoffIoResult write
        = openmeta::serialize_prepared_jxl_encoder_handoff(handoff, &bytes);
    ASSERT_EQ(write.status, openmeta::TransferStatus::Ok);
    ASSERT_GE(bytes.size(), 8U);

    openmeta::PreparedJxlEncoderHandoff parsed;
    const openmeta::PreparedJxlEncoderHandoffIoResult read
        = openmeta::deserialize_prepared_jxl_encoder_handoff(
            std::span<const std::byte>(bytes.data(), bytes.size()), &parsed);
    EXPECT_EQ(read.status, openmeta::TransferStatus::Ok);
    EXPECT_TRUE(parsed.has_icc_profile);
    EXPECT_EQ(parsed.icc_block_index, 3U);
    EXPECT_EQ(parsed.box_count, 2U);
    EXPECT_EQ(parsed.box_payload_bytes, 7U);
    EXPECT_EQ(parsed.icc_profile, handoff.icc_profile);
}

TEST(MetadataTransferApi, DeserializePreparedJxlEncoderHandoffRejectsBadMagic)
{
    const std::array<std::byte, 12> bytes
        = { std::byte { 'B' },  std::byte { 'A' },  std::byte { 'D' },
            std::byte { 'M' },  std::byte { 'A' },  std::byte { 'G' },
            std::byte { 'I' },  std::byte { 'C' },  std::byte { 0x01 },
            std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 } };
    openmeta::PreparedJxlEncoderHandoff handoff;
    const openmeta::PreparedJxlEncoderHandoffIoResult result
        = openmeta::deserialize_prepared_jxl_encoder_handoff(
            std::span<const std::byte>(bytes.data(), bytes.size()), &handoff);
    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidPayload);
}

TEST(MetadataTransferApi, InspectPreparedTransferArtifactRecognizesJxlHandoff)
{
    openmeta::PreparedJxlEncoderHandoff handoff;
    handoff.has_icc_profile   = true;
    handoff.icc_block_index   = 1U;
    handoff.box_count         = 2U;
    handoff.box_payload_bytes = 11U;
    handoff.icc_profile       = { std::byte { 0x49 }, std::byte { 0x43 },
                                  std::byte { 0x43 } };

    std::vector<std::byte> bytes;
    const openmeta::PreparedJxlEncoderHandoffIoResult write
        = openmeta::serialize_prepared_jxl_encoder_handoff(handoff, &bytes);
    ASSERT_EQ(write.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferArtifactInfo info;
    const openmeta::PreparedTransferArtifactIoResult inspect
        = openmeta::inspect_prepared_transfer_artifact(
            std::span<const std::byte>(bytes.data(), bytes.size()), &info);
    EXPECT_EQ(inspect.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(info.kind,
              openmeta::PreparedTransferArtifactKind::JxlEncoderHandoff);
    EXPECT_TRUE(info.has_target_format);
    EXPECT_EQ(info.target_format, openmeta::TransferTargetFormat::Jxl);
    EXPECT_TRUE(info.has_icc_profile);
    EXPECT_EQ(info.icc_profile_bytes, 3U);
    EXPECT_EQ(info.box_payload_bytes, 11U);
}

TEST(MetadataTransferApi,
     InspectPreparedTransferArtifactRecognizesC2paSignedPackage)
{
    openmeta::PreparedTransferC2paSignedPackage package;
    package.request.target_format  = openmeta::TransferTargetFormat::Jpeg;
    package.request.carrier_route  = "jpeg:app11-c2pa";
    package.request.manifest_label = "c2pa";
    package.request.content_binding_chunks.resize(2U);
    package.signer_input.signed_c2pa_logical_payload
        = { std::byte { 0x01 }, std::byte { 0x02 }, std::byte { 0x03 } };

    std::vector<std::byte> bytes;
    const openmeta::PreparedTransferC2paPackageIoResult write
        = openmeta::serialize_prepared_c2pa_signed_package(package, &bytes);
    ASSERT_EQ(write.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferArtifactInfo info;
    const openmeta::PreparedTransferArtifactIoResult inspect
        = openmeta::inspect_prepared_transfer_artifact(
            std::span<const std::byte>(bytes.data(), bytes.size()), &info);
    EXPECT_EQ(inspect.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(info.kind,
              openmeta::PreparedTransferArtifactKind::C2paSignedPackage);
    EXPECT_TRUE(info.has_target_format);
    EXPECT_EQ(info.target_format, openmeta::TransferTargetFormat::Jpeg);
    EXPECT_EQ(info.entry_count, 2U);
    EXPECT_EQ(info.signed_payload_bytes, 3U);
    EXPECT_EQ(info.carrier_route, "jpeg:app11-c2pa");
    EXPECT_EQ(info.manifest_label, "c2pa");
}

TEST(MetadataTransferApi, PrepareBuildsWebpIccChunk)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    std::vector<std::byte> icc_tag_bytes(24U, std::byte { 0x44 });
    openmeta::Entry e;
    e.key   = openmeta::make_icc_tag_key(0x64657363U);
    e.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(icc_tag_bytes.data(), icc_tag_bytes.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Webp;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = true;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::Icc);
    EXPECT_EQ(bundle.blocks[0].route, "webp:chunk-iccp");
    EXPECT_GE(bundle.blocks[0].payload.size(), 24U);
    EXPECT_EQ(bundle.blocks[0].payload[0], std::byte { 0x00 });
}

TEST(MetadataTransferApi, PrepareBuildsPngIccChunk)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    std::vector<std::byte> icc_tag_bytes(24U, std::byte { 0x44 });
    openmeta::Entry e;
    e.key   = openmeta::make_icc_tag_key(0x64657363U);
    e.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(icc_tag_bytes.data(), icc_tag_bytes.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Png;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = true;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::Icc);
    EXPECT_EQ(bundle.blocks[0].route, "png:chunk-iccp");
    EXPECT_GE(bundle.blocks[0].payload.size(), 6U);
    EXPECT_EQ(bundle.blocks[0].payload[0], std::byte { 'i' });
    EXPECT_EQ(bundle.blocks[0].payload[1], std::byte { 'c' });
    EXPECT_EQ(bundle.blocks[0].payload[2], std::byte { 'c' });
#else
    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::PrepareTransferCode::IccPackFailed);
    EXPECT_TRUE(result.message.find("png iccp requires zlib")
                != std::string::npos);
    EXPECT_TRUE(bundle.blocks.empty());
#endif
}

TEST(MetadataTransferApi, PrepareBuildsBmffIccProperty)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    std::vector<std::byte> icc_tag_bytes(24U, std::byte { 0x55 });
    openmeta::Entry e;
    e.key   = openmeta::make_icc_tag_key(0x64657363U);
    e.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(icc_tag_bytes.data(), icc_tag_bytes.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Heif;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = true;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::Icc);
    EXPECT_EQ(bundle.blocks[0].route, "bmff:property-colr-icc");
    ASSERT_GE(bundle.blocks[0].payload.size(), 28U);
    EXPECT_EQ(bundle.blocks[0].payload[0], std::byte { 'p' });
    EXPECT_EQ(bundle.blocks[0].payload[1], std::byte { 'r' });
    EXPECT_EQ(bundle.blocks[0].payload[2], std::byte { 'o' });
    EXPECT_EQ(bundle.blocks[0].payload[3], std::byte { 'f' });
}

TEST(MetadataTransferApi, PrepareBuildsJpegIptcApp13FromDatasets)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    const std::array<std::byte, 5> iptc_value = {
        std::byte { 'H' }, std::byte { 'e' }, std::byte { 'l' },
        std::byte { 'l' }, std::byte { 'o' },
    };

    openmeta::Entry e;
    e.key = openmeta::make_iptc_dataset_key(2U, 5U);
    e.value
        = openmeta::make_bytes(store.arena(),
                               std::span<const std::byte>(iptc_value.data(),
                                                          iptc_value.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "jpeg:app13-iptc");
    const std::span<const std::byte> p(bundle.blocks[0].payload.data(),
                                       bundle.blocks[0].payload.size());
    ASSERT_GE(p.size(), 32U);
    EXPECT_EQ(p[0], std::byte { 'P' });
    EXPECT_EQ(p[12], std::byte { '0' });
    EXPECT_EQ(p[13], std::byte { 0x00 });
    EXPECT_EQ(p[14], std::byte { '8' });
    EXPECT_EQ(p[15], std::byte { 'B' });
    EXPECT_EQ(p[16], std::byte { 'I' });
    EXPECT_EQ(p[17], std::byte { 'M' });
    EXPECT_EQ(p[18], std::byte { 0x04 });
    EXPECT_EQ(p[19], std::byte { 0x04 });
}

TEST(MetadataTransferApi, PrepareBuildsJpegIptcApp13FromPhotoshopIrb)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    const std::array<std::byte, 8> iptc_iim = {
        std::byte { 0x1C }, std::byte { 0x02 }, std::byte { 0x05 },
        std::byte { 0x00 }, std::byte { 0x03 }, std::byte { 'A' },
        std::byte { 'B' },  std::byte { 'C' },
    };

    openmeta::Entry e;
    e.key                   = openmeta::make_photoshop_irb_key(0x0404U);
    e.value                 = openmeta::make_bytes(store.arena(),
                                                   std::span<const std::byte>(iptc_iim.data(),
                                                                              iptc_iim.size()));
    e.origin.block          = block;
    e.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(e), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    const std::span<const std::byte> p(bundle.blocks[0].payload.data(),
                                       bundle.blocks[0].payload.size());
    ASSERT_GE(p.size(), 8U + iptc_iim.size());
    bool found = false;
    for (size_t i = 0; i + iptc_iim.size() <= p.size(); ++i) {
        if (std::memcmp(p.data() + i, iptc_iim.data(), iptc_iim.size()) == 0) {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
}

TEST(MetadataTransferApi, PrepareBuildsJxlXmlFromIptcWhenXmpDisabled)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry category;
    category.key   = openmeta::make_iptc_dataset_key(2U, 15U);  // Category
    category.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>("ART"),
                                   3U));
    category.origin.block          = block;
    category.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(category), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Jxl;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(result.warnings, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::Xmp);
    EXPECT_EQ(bundle.blocks[0].route, "jxl:box-xml");
    EXPECT_EQ(bundle.blocks[0].box_type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "photoshop:Category"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "ART"));

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(
            bundle, openmeta::TransferPolicySubject::XmpIptcProjection);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Keep);
    EXPECT_EQ(decision->reason,
              openmeta::TransferPolicyReason::ProjectedPayload);
    EXPECT_EQ(decision->matched_entries, 1U);
}

TEST(MetadataTransferApi, PrepareBuildsWebpXmpFromIptcWhenXmpDisabled)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry category;
    category.key   = openmeta::make_iptc_dataset_key(2U, 15U);
    category.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>("ART"),
                                   3U));
    category.origin.block          = block;
    category.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(category), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Webp;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(result.warnings, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::Xmp);
    EXPECT_EQ(bundle.blocks[0].route, "webp:chunk-xmp");
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "photoshop:Category"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "ART"));
}

TEST(MetadataTransferApi, PrepareCanDisableIptcProjectionFallbackXmp)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry category;
    category.key   = openmeta::make_iptc_dataset_key(2U, 15U);
    category.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>("ART"),
                                   3U));
    category.origin.block          = block;
    category.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(category), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Jxl;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = true;
    request.xmp_project_iptc   = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(result.warnings, 0U);
    EXPECT_TRUE(bundle.blocks.empty());

    const openmeta::PreparedTransferPolicyDecision* decision
        = find_policy_decision(
            bundle, openmeta::TransferPolicySubject::XmpIptcProjection);
    ASSERT_NE(decision, nullptr);
    EXPECT_EQ(decision->requested, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(decision->effective, openmeta::TransferPolicyAction::Drop);
    EXPECT_EQ(decision->reason, openmeta::TransferPolicyReason::ExplicitDrop);
    EXPECT_EQ(decision->matched_entries, 1U);
}

TEST(MetadataTransferApi, PrepareJxlWithXmpAndIptcAvoidsDuplicateXml)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry category;
    category.key   = openmeta::make_iptc_dataset_key(2U, 15U);  // Category
    category.value = openmeta::make_bytes(
        store.arena(),
        std::span<const std::byte>(reinterpret_cast<const std::byte*>("ART"),
                                   3U));
    category.origin.block          = block;
    category.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(category), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Jxl;
    request.include_exif_app1  = false;
    request.include_xmp_app1   = true;
    request.xmp_portable       = true;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = true;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(result.warnings, 0U);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::Xmp);
    EXPECT_EQ(bundle.blocks[0].route, "jxl:box-xml");
    EXPECT_EQ(bundle.blocks[0].box_type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "photoshop:Category"));
    EXPECT_TRUE(payload_contains_ascii(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        "ART"));
}

TEST(MetadataTransferApi, EmitJpegKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jpeg:app2-icc";
    icc.payload = { std::byte { 0x02 }, std::byte { 0x03 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferBlock iptc;
    iptc.route   = "jpeg:app13-iptc";
    iptc.payload = { std::byte { 0x04 } };
    bundle.blocks.push_back(iptc);

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 3U);
    EXPECT_EQ(result.errors, 0U);
    ASSERT_EQ(emitter.calls.size(), 3U);
    EXPECT_EQ(emitter.calls[0].first, 0xE1U);
    EXPECT_EQ(emitter.calls[1].first, 0xE2U);
    EXPECT_EQ(emitter.calls[2].first, 0xEDU);
}

TEST(MetadataTransferApi, CompileJpegPlanKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock com;
    com.route   = "jpeg:com";
    com.payload = { std::byte { 0x02 } };
    bundle.blocks.push_back(com);

    openmeta::PreparedJpegEmitPlan plan;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_jpeg(bundle, &plan);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    ASSERT_EQ(plan.ops.size(), 2U);
    EXPECT_EQ(plan.ops[0].block_index, 0U);
    EXPECT_EQ(plan.ops[0].marker_code, 0xE1U);
    EXPECT_EQ(plan.ops[1].block_index, 1U);
    EXPECT_EQ(plan.ops[1].marker_code, 0xFEU);
}

TEST(MetadataTransferApi, EmitJpegCompiledPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jpeg:app2-icc";
    icc.payload = { std::byte { 0x02 }, std::byte { 0x03 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedJpegEmitPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_bundle_jpeg(bundle, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.ops.size(), 2U);

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult emit_result
        = openmeta::emit_prepared_bundle_jpeg_compiled(bundle, plan, emitter);
    EXPECT_EQ(emit_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(emit_result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(emit_result.emitted, 2U);
    ASSERT_EQ(emitter.calls.size(), 2U);
    EXPECT_EQ(emitter.calls[0].first, 0xE1U);
    EXPECT_EQ(emitter.calls[1].first, 0xE2U);
}

TEST(MetadataTransferApi, WriteJpegCompiledPlanToWriter)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jpeg:app2-icc";
    icc.payload = { std::byte { 0x02 }, std::byte { 0x03 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedJpegEmitPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_bundle_jpeg(bundle, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    const openmeta::EmitTransferResult emit_result
        = openmeta::write_prepared_bundle_jpeg_compiled(bundle, plan, writer);
    EXPECT_EQ(emit_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(emit_result.emitted, 2U);

    std::vector<std::byte> expected;
    expected.push_back(std::byte { 0xFF });
    expected.push_back(std::byte { 0xE1 });
    append_u16be(&expected, 3U);
    expected.push_back(std::byte { 0x01 });
    expected.push_back(std::byte { 0xFF });
    expected.push_back(std::byte { 0xE2 });
    append_u16be(&expected, 4U);
    expected.push_back(std::byte { 0x02 });
    expected.push_back(std::byte { 0x03 });
    EXPECT_EQ(writer.out, expected);
}

TEST(MetadataTransferApi, CompilePreparedTransferExecutionJpeg)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock com;
    com.route   = "jpeg:com";
    com.payload = {};
    bundle.blocks.push_back(com);

    openmeta::EmitTransferOptions emit_options;
    emit_options.skip_empty_payloads = false;
    emit_options.stop_on_error       = true;

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_transfer_execution(bundle, emit_options,
                                                        &plan);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(plan.contract_version, bundle.contract_version);
    EXPECT_EQ(plan.target_format, openmeta::TransferTargetFormat::Jpeg);
    EXPECT_EQ(plan.emit.skip_empty_payloads, false);
    EXPECT_EQ(plan.emit.stop_on_error, true);
    ASSERT_EQ(plan.jpeg_emit.ops.size(), 2U);
    EXPECT_TRUE(plan.tiff_emit.ops.empty());
}

TEST(MetadataTransferApi, BuildPreparedTransferAdapterViewJpeg)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock com;
    com.route   = "jpeg:com";
    com.payload = {};
    bundle.blocks.push_back(com);

    openmeta::EmitTransferOptions emit_options;
    emit_options.skip_empty_payloads = false;

    openmeta::PreparedTransferAdapterView view;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_transfer_adapter_view(bundle, &view,
                                                         emit_options);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 2U);
    EXPECT_EQ(view.contract_version, bundle.contract_version);
    EXPECT_EQ(view.target_format, openmeta::TransferTargetFormat::Jpeg);
    EXPECT_EQ(view.emit.skip_empty_payloads, false);
    ASSERT_EQ(view.ops.size(), 2U);
    EXPECT_EQ(view.ops[0].kind, openmeta::TransferAdapterOpKind::JpegMarker);
    EXPECT_EQ(view.ops[0].block_index, 0U);
    EXPECT_EQ(view.ops[0].payload_size, 1U);
    EXPECT_EQ(view.ops[0].serialized_size, 5U);
    EXPECT_EQ(view.ops[0].jpeg_marker_code, 0xE1U);
    EXPECT_EQ(view.ops[1].kind, openmeta::TransferAdapterOpKind::JpegMarker);
    EXPECT_EQ(view.ops[1].block_index, 1U);
    EXPECT_EQ(view.ops[1].payload_size, 0U);
    EXPECT_EQ(view.ops[1].serialized_size, 4U);
    EXPECT_EQ(view.ops[1].jpeg_marker_code, 0xFEU);
}

TEST(MetadataTransferApi, BuildPreparedTransferAdapterViewTiff)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "tiff:ifd-exif-app1";
    exif.payload = { std::byte { 'E' },  std::byte { 'x' },  std::byte { 'i' },
                     std::byte { 'f' },  std::byte { 0x00 }, std::byte { 0x00 },
                     std::byte { 'I' },  std::byte { 'I' },  std::byte { 42 },
                     std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
                     std::byte { 0x00 }, std::byte { 0x00 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferAdapterView view;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_transfer_adapter_view(bundle, &view);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 2U);
    EXPECT_EQ(view.target_format, openmeta::TransferTargetFormat::Tiff);
    ASSERT_EQ(view.ops.size(), 2U);
    EXPECT_EQ(view.ops[0].kind, openmeta::TransferAdapterOpKind::TiffTagBytes);
    EXPECT_EQ(view.ops[0].block_index, 0U);
    EXPECT_EQ(view.ops[0].payload_size, exif.payload.size());
    EXPECT_EQ(view.ops[0].serialized_size, exif.payload.size());
    EXPECT_EQ(view.ops[0].tiff_tag, 34665U);
    EXPECT_EQ(view.ops[1].kind, openmeta::TransferAdapterOpKind::TiffTagBytes);
    EXPECT_EQ(view.ops[1].block_index, 1U);
    EXPECT_EQ(view.ops[1].payload_size, xmp.payload.size());
    EXPECT_EQ(view.ops[1].serialized_size, xmp.payload.size());
    EXPECT_EQ(view.ops[1].tiff_tag, 700U);
}

TEST(MetadataTransferApi, BuildPreparedTransferAdapterViewDng)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Dng;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "tiff:ifd-exif-app1";
    exif.payload = { std::byte { 'E' },  std::byte { 'x' },  std::byte { 'i' },
                     std::byte { 'f' },  std::byte { 0x00 }, std::byte { 0x00 },
                     std::byte { 'I' },  std::byte { 'I' },  std::byte { 42 },
                     std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
                     std::byte { 0x00 }, std::byte { 0x00 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferAdapterView view;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_transfer_adapter_view(bundle, &view);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 2U);
    EXPECT_EQ(view.target_format, openmeta::TransferTargetFormat::Dng);
    ASSERT_EQ(view.ops.size(), 2U);
    EXPECT_EQ(view.ops[0].kind, openmeta::TransferAdapterOpKind::TiffTagBytes);
    EXPECT_EQ(view.ops[0].block_index, 0U);
    EXPECT_EQ(view.ops[0].payload_size, exif.payload.size());
    EXPECT_EQ(view.ops[0].serialized_size, exif.payload.size());
    EXPECT_EQ(view.ops[0].tiff_tag, 34665U);
    EXPECT_EQ(view.ops[1].kind, openmeta::TransferAdapterOpKind::TiffTagBytes);
    EXPECT_EQ(view.ops[1].block_index, 1U);
    EXPECT_EQ(view.ops[1].payload_size, xmp.payload.size());
    EXPECT_EQ(view.ops[1].serialized_size, xmp.payload.size());
    EXPECT_EQ(view.ops[1].tiff_tag, 700U);
}

TEST(MetadataTransferApi, BuildPreparedTransferAdapterViewJxl)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jxl:icc-profile";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 },
                    std::byte { 0x43 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jxl:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                     std::byte { 0x06 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 },
                       std::byte { 'j' },  std::byte { 'u' },
                       std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    openmeta::PreparedTransferAdapterView view;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_transfer_adapter_view(bundle, &view);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 3U);
    EXPECT_EQ(view.target_format, openmeta::TransferTargetFormat::Jxl);
    ASSERT_EQ(view.ops.size(), 3U);
    EXPECT_EQ(view.ops[0].kind, openmeta::TransferAdapterOpKind::JxlIccProfile);
    EXPECT_EQ(view.ops[0].block_index, 0U);
    EXPECT_EQ(view.ops[0].payload_size, icc.payload.size());
    EXPECT_EQ(view.ops[0].serialized_size, icc.payload.size());
    EXPECT_EQ(view.ops[0].box_type, (std::array<char, 4> {}));
    EXPECT_FALSE(view.ops[0].compress);
    EXPECT_EQ(view.ops[1].kind, openmeta::TransferAdapterOpKind::JxlBox);
    EXPECT_EQ(view.ops[1].block_index, 1U);
    EXPECT_EQ(view.ops[1].payload_size, exif.payload.size());
    EXPECT_EQ(view.ops[1].serialized_size, 12U);
    EXPECT_EQ(view.ops[1].box_type,
              (std::array<char, 4> { 'E', 'x', 'i', 'f' }));
    EXPECT_FALSE(view.ops[1].compress);
    EXPECT_EQ(view.ops[2].kind, openmeta::TransferAdapterOpKind::JxlBox);
    EXPECT_EQ(view.ops[2].block_index, 2U);
    EXPECT_EQ(view.ops[2].payload_size, jumbf.payload.size());
    EXPECT_EQ(view.ops[2].serialized_size, 16U);
    EXPECT_EQ(view.ops[2].box_type,
              (std::array<char, 4> { 'j', 'u', 'm', 'b' }));
    EXPECT_FALSE(view.ops[2].compress);
}

TEST(MetadataTransferApi, BuildPreparedTransferAdapterViewWebp)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Webp;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "webp:chunk-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "webp:chunk-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock c2pa;
    c2pa.route   = "webp:chunk-c2pa";
    c2pa.payload = { std::byte { 0x00 }, std::byte { 0x01 } };
    bundle.blocks.push_back(c2pa);

    openmeta::PreparedTransferAdapterView view;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_transfer_adapter_view(bundle, &view);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 3U);
    EXPECT_EQ(view.target_format, openmeta::TransferTargetFormat::Webp);
    ASSERT_EQ(view.ops.size(), 3U);
    EXPECT_EQ(view.ops[0].kind, openmeta::TransferAdapterOpKind::WebpChunk);
    EXPECT_EQ(view.ops[0].block_index, 0U);
    EXPECT_EQ(view.ops[0].payload_size, exif.payload.size());
    EXPECT_EQ(view.ops[0].serialized_size, 10U);
    EXPECT_EQ(view.ops[0].chunk_type,
              (std::array<char, 4> { 'E', 'X', 'I', 'F' }));
    EXPECT_EQ(view.ops[1].kind, openmeta::TransferAdapterOpKind::WebpChunk);
    EXPECT_EQ(view.ops[1].block_index, 1U);
    EXPECT_EQ(view.ops[1].payload_size, xmp.payload.size());
    EXPECT_EQ(view.ops[1].serialized_size, 12U);
    EXPECT_EQ(view.ops[1].chunk_type,
              (std::array<char, 4> { 'X', 'M', 'P', ' ' }));
    EXPECT_EQ(view.ops[2].kind, openmeta::TransferAdapterOpKind::WebpChunk);
    EXPECT_EQ(view.ops[2].block_index, 2U);
    EXPECT_EQ(view.ops[2].payload_size, c2pa.payload.size());
    EXPECT_EQ(view.ops[2].serialized_size, 10U);
    EXPECT_EQ(view.ops[2].chunk_type,
              (std::array<char, 4> { 'C', '2', 'P', 'A' }));
}

TEST(MetadataTransferApi, BuildPreparedTransferAdapterViewPng)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Png;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "png:chunk-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "png:chunk-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "png:chunk-iccp";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferAdapterView view;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_transfer_adapter_view(bundle, &view);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 3U);
    EXPECT_EQ(view.target_format, openmeta::TransferTargetFormat::Png);
    ASSERT_EQ(view.ops.size(), 3U);
    EXPECT_EQ(view.ops[0].kind, openmeta::TransferAdapterOpKind::PngChunk);
    EXPECT_EQ(view.ops[0].block_index, 0U);
    EXPECT_EQ(view.ops[0].payload_size, exif.payload.size());
    EXPECT_EQ(view.ops[0].serialized_size, 14U);
    EXPECT_EQ(view.ops[0].chunk_type,
              (std::array<char, 4> { 'e', 'X', 'I', 'f' }));
    EXPECT_EQ(view.ops[1].kind, openmeta::TransferAdapterOpKind::PngChunk);
    EXPECT_EQ(view.ops[1].block_index, 1U);
    EXPECT_EQ(view.ops[1].payload_size, xmp.payload.size());
    EXPECT_EQ(view.ops[1].serialized_size, 15U);
    EXPECT_EQ(view.ops[1].chunk_type,
              (std::array<char, 4> { 'i', 'T', 'X', 't' }));
    EXPECT_EQ(view.ops[2].kind, openmeta::TransferAdapterOpKind::PngChunk);
    EXPECT_EQ(view.ops[2].block_index, 2U);
    EXPECT_EQ(view.ops[2].payload_size, icc.payload.size());
    EXPECT_EQ(view.ops[2].serialized_size, 14U);
    EXPECT_EQ(view.ops[2].chunk_type,
              (std::array<char, 4> { 'i', 'C', 'C', 'P' }));
}

TEST(MetadataTransferApi, BuildPreparedTransferAdapterViewJp2)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jp2;

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jp2:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload  = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jp2:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route    = "jp2:box-jp2h-colr";
    icc.box_type = { 'j', 'p', '2', 'h' };
    icc.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                     std::byte { 0x00 }, std::byte { 0x0B },
                     std::byte { 'c' },  std::byte { 'o' },
                     std::byte { 'l' },  std::byte { 'r' },
                     std::byte { 0x02 }, std::byte { 0x00 },
                     std::byte { 0x00 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferAdapterView view;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_transfer_adapter_view(bundle, &view);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 3U);
    EXPECT_EQ(view.target_format, openmeta::TransferTargetFormat::Jp2);
    ASSERT_EQ(view.ops.size(), 3U);
    EXPECT_EQ(view.ops[0].kind, openmeta::TransferAdapterOpKind::Jp2Box);
    EXPECT_EQ(view.ops[0].block_index, 0U);
    EXPECT_EQ(view.ops[0].payload_size, exif.payload.size());
    EXPECT_EQ(view.ops[0].serialized_size, 10U);
    EXPECT_EQ(view.ops[0].box_type,
              (std::array<char, 4> { 'E', 'x', 'i', 'f' }));
    EXPECT_EQ(view.ops[1].kind, openmeta::TransferAdapterOpKind::Jp2Box);
    EXPECT_EQ(view.ops[1].block_index, 1U);
    EXPECT_EQ(view.ops[1].payload_size, xmp.payload.size());
    EXPECT_EQ(view.ops[1].serialized_size, 11U);
    EXPECT_EQ(view.ops[1].box_type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
    EXPECT_EQ(view.ops[2].kind, openmeta::TransferAdapterOpKind::Jp2Box);
    EXPECT_EQ(view.ops[2].block_index, 2U);
    EXPECT_EQ(view.ops[2].payload_size, icc.payload.size());
    EXPECT_EQ(view.ops[2].serialized_size, 19U);
    EXPECT_EQ(view.ops[2].box_type,
              (std::array<char, 4> { 'j', 'p', '2', 'h' }));
}

TEST(MetadataTransferApi, PrepareBuildsExrStringAttributeBlocks)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry make;
    make.key                   = openmeta::make_exif_tag_key(store.arena(),
                                                             "ifd0", 0x010FU);
    make.value                 = openmeta::make_text(store.arena(), "Vendor",
                                                     openmeta::TextEncoding::Ascii);
    make.origin.block          = block;
    make.origin.order_in_block = 0U;
    ASSERT_NE(store.add_entry(make), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Exr;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult result
        = openmeta::prepare_metadata_for_target(store, request, &bundle);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(bundle.target_format, openmeta::TransferTargetFormat::Exr);
    EXPECT_TRUE(bundle.time_patch_map.empty());
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].kind, openmeta::TransferBlockKind::ExrAttribute);
    EXPECT_EQ(bundle.blocks[0].route, "exr:attribute-string");

    std::string name;
    std::string value;
    ASSERT_TRUE(parse_test_exr_string_attribute_payload(
        std::span<const std::byte>(bundle.blocks[0].payload.data(),
                                   bundle.blocks[0].payload.size()),
        &name, &value));
    EXPECT_EQ(name, "Make");
    EXPECT_EQ(value, "Vendor");
}

TEST(MetadataTransferApi, BuildPreparedTransferAdapterViewExr)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Exr;

    openmeta::PreparedTransferBlock make;
    make.kind    = openmeta::TransferBlockKind::ExrAttribute;
    make.route   = "exr:attribute-string";
    make.payload = make_test_exr_string_attribute_payload("Make", "Vendor");
    bundle.blocks.push_back(make);

    openmeta::PreparedTransferBlock model;
    model.kind    = openmeta::TransferBlockKind::ExrAttribute;
    model.route   = "exr:attribute-string";
    model.payload = make_test_exr_string_attribute_payload("Model", "Camera");
    bundle.blocks.push_back(model);

    openmeta::PreparedTransferAdapterView view;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_transfer_adapter_view(bundle, &view);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 2U);
    EXPECT_EQ(view.target_format, openmeta::TransferTargetFormat::Exr);
    ASSERT_EQ(view.ops.size(), 2U);
    EXPECT_EQ(view.ops[0].kind,
              openmeta::TransferAdapterOpKind::ExrAttribute);
    EXPECT_EQ(view.ops[0].block_index, 0U);
    EXPECT_EQ(view.ops[0].payload_size, make.payload.size());
    EXPECT_EQ(view.ops[0].serialized_size, make.payload.size());
    EXPECT_EQ(view.ops[1].kind,
              openmeta::TransferAdapterOpKind::ExrAttribute);
    EXPECT_EQ(view.ops[1].block_index, 1U);
    EXPECT_EQ(view.ops[1].payload_size, model.payload.size());
    EXPECT_EQ(view.ops[1].serialized_size, model.payload.size());
}

TEST(MetadataTransferApi, EmitPreparedTransferAdapterViewJpeg)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock com;
    com.route   = "jpeg:com";
    com.payload = {};
    bundle.blocks.push_back(com);

    openmeta::EmitTransferOptions emit_options;
    emit_options.skip_empty_payloads = false;

    openmeta::PreparedTransferAdapterView view;
    ASSERT_EQ(openmeta::build_prepared_transfer_adapter_view(bundle, &view,
                                                             emit_options)
                  .status,
              openmeta::TransferStatus::Ok);

    FakeTransferAdapterSink sink;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_transfer_adapter_view(bundle, view, sink);
    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 2U);
    ASSERT_EQ(sink.calls.size(), 2U);
    EXPECT_EQ(sink.calls[0].kind, openmeta::TransferAdapterOpKind::JpegMarker);
    EXPECT_EQ(sink.calls[0].jpeg_marker_code, 0xE1U);
    EXPECT_EQ(sink.calls[0].payload_size, 1U);
    EXPECT_EQ(sink.calls[1].kind, openmeta::TransferAdapterOpKind::JpegMarker);
    EXPECT_EQ(sink.calls[1].jpeg_marker_code, 0xFEU);
    EXPECT_EQ(sink.calls[1].payload_size, 0U);
}

TEST(MetadataTransferApi, EmitPreparedTransferAdapterViewTiff)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferAdapterView view;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_adapter_view(bundle, &view).status,
        openmeta::TransferStatus::Ok);

    FakeTransferAdapterSink sink;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_transfer_adapter_view(bundle, view, sink);
    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 1U);
    ASSERT_EQ(sink.calls.size(), 1U);
    EXPECT_EQ(sink.calls[0].kind,
              openmeta::TransferAdapterOpKind::TiffTagBytes);
    EXPECT_EQ(sink.calls[0].tiff_tag, 700U);
    EXPECT_EQ(sink.calls[0].payload_size, 2U);
}

TEST(MetadataTransferApi, EmitPreparedTransferAdapterViewDng)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Dng;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferAdapterView view;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_adapter_view(bundle, &view).status,
        openmeta::TransferStatus::Ok);

    FakeTransferAdapterSink sink;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_transfer_adapter_view(bundle, view, sink);
    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 1U);
    ASSERT_EQ(sink.calls.size(), 1U);
    EXPECT_EQ(sink.calls[0].kind,
              openmeta::TransferAdapterOpKind::TiffTagBytes);
    EXPECT_EQ(sink.calls[0].tiff_tag, 700U);
    EXPECT_EQ(sink.calls[0].payload_size, 2U);
}

TEST(MetadataTransferApi, EmitPreparedTransferAdapterViewRejectsPlanMismatch)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jxl:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                     std::byte { 0x06 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferAdapterView view;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_adapter_view(bundle, &view).status,
        openmeta::TransferStatus::Ok);
    view.contract_version += 1U;

    FakeTransferAdapterSink sink;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_transfer_adapter_view(bundle, view, sink);
    EXPECT_EQ(result.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::PlanMismatch);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_TRUE(sink.calls.empty());
}

TEST(MetadataTransferApi, ExecutePreparedTransferCompiledJpegEmitToWriter)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock com;
    com.route   = "jpeg:com";
    com.payload = {};
    bundle.blocks.push_back(com);

    openmeta::EmitTransferOptions compile_options;
    compile_options.skip_empty_payloads = false;

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, compile_options,
                                                        &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer       = &writer;
    options.emit.skip_empty_payloads = true;
    options.emit.stop_on_error       = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer_compiled(&bundle, plan, {},
                                                       options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.compile.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(result.compiled_ops, 2U);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.emitted, 2U);
    EXPECT_EQ(result.emit_output_size, writer.out.size());
    EXPECT_EQ(writer.out.size(), 9U);
    ASSERT_EQ(result.marker_summary.size(), 2U);
    EXPECT_EQ(result.marker_summary[0].marker, 0xE1U);
    EXPECT_EQ(result.marker_summary[0].count, 1U);
    EXPECT_EQ(result.marker_summary[0].bytes, 1U);
    EXPECT_EQ(result.marker_summary[1].marker, 0xFEU);
    EXPECT_EQ(result.marker_summary[1].count, 1U);
    EXPECT_EQ(result.marker_summary[1].bytes, 0U);
}

TEST(MetadataTransferApi, WritePreparedTransferCompiledAppliesViewAndEmits)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = ascii_z("2024:01:02 03:04:05");
    bundle.blocks.push_back(exif);

    openmeta::TimePatchSlot slot;
    slot.field       = openmeta::TimePatchField::DateTime;
    slot.block_index = 0U;
    slot.byte_offset = 0U;
    slot.width       = 20U;
    bundle.time_patch_map.push_back(slot);

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, {}, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> patched = ascii_z("2030:12:31 23:59:59");
    openmeta::TimePatchView view;
    view.field = openmeta::TimePatchField::DateTime;
    view.value = std::span<const std::byte>(patched.data(), patched.size());
    const std::array<openmeta::TimePatchView, 1> updates = { view };

    BufferByteWriter writer;
    const openmeta::ExecutePreparedTransferResult result
        = openmeta::write_prepared_transfer_compiled(&bundle, plan, writer,
                                                     updates);
    EXPECT_EQ(result.time_patch.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.time_patch.patched_slots, 1U);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.emitted, 1U);
    EXPECT_EQ(result.emit_output_size, writer.out.size());
    ASSERT_EQ(writer.out.size(), 24U);
    EXPECT_EQ(std::memcmp(writer.out.data() + 4, patched.data(), patched.size()),
              0);
}

TEST(MetadataTransferApi, SpanTransferByteWriterWorksWithCompiledTransfer)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = ascii_z("2024:01:02 03:04:05");
    bundle.blocks.push_back(exif);

    openmeta::TimePatchSlot slot;
    slot.field       = openmeta::TimePatchField::DateTime;
    slot.block_index = 0U;
    slot.byte_offset = 0U;
    slot.width       = 20U;
    bundle.time_patch_map.push_back(slot);

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, {}, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> patched = ascii_z("2030:12:31 23:59:59");
    openmeta::TimePatchView view;
    view.field = openmeta::TimePatchField::DateTime;
    view.value = std::span<const std::byte>(patched.data(), patched.size());
    const std::array<openmeta::TimePatchView, 1> updates = { view };

    std::array<std::byte, 64> buffer {};
    openmeta::SpanTransferByteWriter writer(
        std::span<std::byte>(buffer.data(), buffer.size()));

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::write_prepared_transfer_compiled(&bundle, plan, writer,
                                                     updates);
    EXPECT_EQ(result.time_patch.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit_output_size, 24U);
    EXPECT_EQ(writer.status(), openmeta::TransferStatus::Ok);
    EXPECT_EQ(writer.bytes_written(), 24U);
    ASSERT_EQ(writer.written_bytes().size(), 24U);
    EXPECT_EQ(std::memcmp(writer.written_bytes().data() + 4, patched.data(),
                          patched.size()),
              0);
}

TEST(MetadataTransferApi, SpanTransferByteWriterRejectsOverflow)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = ascii_z("2024:01:02 03:04:05");
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, {}, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);

    std::array<std::byte, 8> buffer {};
    openmeta::SpanTransferByteWriter writer(
        std::span<std::byte>(buffer.data(), buffer.size()));

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::write_prepared_transfer_compiled(&bundle, plan, writer);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::LimitExceeded);
    EXPECT_EQ(result.emit.code, openmeta::EmitTransferCode::BackendWriteFailed);
    EXPECT_EQ(result.emit.errors, 1U);
    EXPECT_TRUE(result.emit.message.find("capacity exceeded")
                != std::string::npos);
    EXPECT_EQ(writer.status(), openmeta::TransferStatus::Ok);
    EXPECT_EQ(writer.bytes_written(), 0U);
}

TEST(MetadataTransferApi, EmitPreparedTransferCompiledJpegEmitterUsesBackend)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock com;
    com.route   = "jpeg:com";
    com.payload = {};
    bundle.blocks.push_back(com);

    openmeta::EmitTransferOptions compile_options;
    compile_options.skip_empty_payloads = false;

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, compile_options,
                                                        &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);

    FakeJpegEmitter emitter;
    const openmeta::ExecutePreparedTransferResult result
        = openmeta::emit_prepared_transfer_compiled(&bundle, plan, emitter);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.emitted, 2U);
    ASSERT_EQ(emitter.calls.size(), 2U);
    ASSERT_EQ(result.marker_summary.size(), 2U);
    EXPECT_EQ(result.marker_summary[0].marker, 0xE1U);
    EXPECT_EQ(result.marker_summary[1].marker, 0xFEU);
}

TEST(MetadataTransferApi, EmitPreparedTransferCompiledTiffEmitterUsesBackend)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "tiff:tag-34675-icc";
    icc.payload = { std::byte { 0x03 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, {}, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);

    FakeTiffEmitter emitter;
    const openmeta::ExecutePreparedTransferResult result
        = openmeta::emit_prepared_transfer_compiled(&bundle, plan, emitter);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.emitted, 2U);
    EXPECT_EQ(result.tiff_commit, true);
    EXPECT_EQ(emitter.commit_calls, 1U);
    ASSERT_EQ(emitter.bytes_calls.size(), 2U);
    ASSERT_EQ(result.tiff_tag_summary.size(), 2U);
    EXPECT_EQ(result.tiff_tag_summary[0].tag, 700U);
    EXPECT_EQ(result.tiff_tag_summary[1].tag, 34675U);
}

TEST(MetadataTransferApi, EmitPreparedTransferCompiledDngEmitterUsesBackend)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Dng;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "tiff:tag-34675-icc";
    icc.payload = { std::byte { 0x03 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, {}, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);

    FakeTiffEmitter emitter;
    const openmeta::ExecutePreparedTransferResult result
        = openmeta::emit_prepared_transfer_compiled(&bundle, plan, emitter);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.emitted, 2U);
    EXPECT_EQ(result.tiff_commit, true);
    EXPECT_EQ(emitter.commit_calls, 1U);
    ASSERT_EQ(emitter.bytes_calls.size(), 2U);
    ASSERT_EQ(result.tiff_tag_summary.size(), 2U);
    EXPECT_EQ(result.tiff_tag_summary[0].tag, 700U);
    EXPECT_EQ(result.tiff_tag_summary[1].tag, 34675U);
}

TEST(MetadataTransferApi, ExecutePreparedTransferCompiledRejectsPlanMismatch)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, {}, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);

    plan.contract_version += 1U;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer_compiled(&bundle, plan);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.compile.code, openmeta::EmitTransferCode::PlanMismatch);
    EXPECT_EQ(result.compile.errors, 1U);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.emit.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(result.emit.errors, 1U);
    EXPECT_TRUE(result.emit.message.find("compile failure")
                != std::string::npos);
}

TEST(MetadataTransferApi, ExecutePreparedTransferJpegEmitToWriter)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "jpeg:app1-xmp";
    xmp.payload = { std::byte { 0x02 }, std::byte { 0x03 } };
    bundle.blocks.push_back(xmp);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_repeat        = 2U;
    options.emit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_requested, false);
    EXPECT_EQ(result.emit_output_size, writer.out.size());
    ASSERT_EQ(result.marker_summary.size(), 1U);
    EXPECT_EQ(result.marker_summary[0].marker, 0xE1U);
    EXPECT_EQ(result.marker_summary[0].count, 4U);
    EXPECT_EQ(result.marker_summary[0].bytes, 6U);
}

TEST(MetadataTransferApi, EmitJpegRejectsMalformedPreparedC2paCarrier)
{
    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    openmeta::PreparedTransferBundle bundle;
    ASSERT_TRUE(build_staged_signed_c2pa_bundle(
        std::span<const std::byte>(logical.data(), logical.size()), &bundle));
    ASSERT_EQ(count_blocks_with_route(bundle, "jpeg:app11-c2pa"), 1U);
    ASSERT_FALSE(bundle.blocks.empty());
    ASSERT_GE(bundle.blocks[0].payload.size(), 8U);
    bundle.blocks[0].payload[7] = std::byte { 0x02 };

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);
    EXPECT_EQ(result.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(result.failed_block_index, 0U);
    EXPECT_TRUE(result.message.find("carrier sequence is invalid")
                != std::string::npos);
    EXPECT_TRUE(emitter.calls.empty());
}

TEST(MetadataTransferApi, EmitJpegRejectsPreparedC2paCarrierWithInvalidSize)
{
    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    openmeta::PreparedTransferBundle bundle;
    ASSERT_TRUE(build_staged_signed_c2pa_bundle(
        std::span<const std::byte>(logical.data(), logical.size()), &bundle));
    ASSERT_EQ(count_blocks_with_route(bundle, "jpeg:app11-c2pa"), 1U);
    ASSERT_FALSE(bundle.blocks.empty());
    ASSERT_GE(bundle.blocks[0].payload.size(), 16U);
    bundle.blocks[0].payload[11] = std::byte { 0x40 };

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);
    EXPECT_EQ(result.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(result.failed_block_index, 0U);
    EXPECT_TRUE(result.message.find("logical payload size is inconsistent")
                != std::string::npos);
    EXPECT_TRUE(emitter.calls.empty());
}

TEST(MetadataTransferApi,
     EmitJpegRejectsDraftC2paCarrierForSignedRewriteContract)
{
    openmeta::PreparedTransferBundle bundle
        = make_bundle_with_draft_c2pa_signed_rewrite_contract();

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);
    EXPECT_EQ(result.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_TRUE(result.message.find("signed rewrite contract")
                != std::string::npos);
    EXPECT_TRUE(emitter.calls.empty());
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferRejectsMalformedPreparedC2paCarrier)
{
    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    openmeta::PreparedTransferBundle bundle;
    ASSERT_TRUE(build_staged_signed_c2pa_bundle(
        std::span<const std::byte>(logical.data(), logical.size()), &bundle));
    ASSERT_EQ(count_blocks_with_route(bundle, "jpeg:app11-c2pa"), 1U);
    ASSERT_FALSE(bundle.blocks.empty());
    ASSERT_GE(bundle.blocks[0].payload.size(), 8U);
    bundle.blocks[0].payload[7] = std::byte { 0x02 };

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(result.compile.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(result.compile.errors, 1U);
    EXPECT_TRUE(result.compile.message.find("carrier sequence is invalid")
                != std::string::npos);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.emit.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(result.emit.errors, 1U);
    EXPECT_TRUE(result.emit.message.find("compile failure")
                != std::string::npos);
    EXPECT_TRUE(writer.out.empty());
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferRejectsDraftC2paCarrierForSignedRewriteContract)
{
    openmeta::PreparedTransferBundle bundle
        = make_bundle_with_draft_c2pa_signed_rewrite_contract();

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(result.compile.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(result.compile.errors, 1U);
    EXPECT_TRUE(result.compile.message.find("signed rewrite contract")
                != std::string::npos);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.emit.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(result.emit.errors, 1U);
    EXPECT_TRUE(result.emit.message.find("compile failure")
                != std::string::npos);
    EXPECT_TRUE(writer.out.empty());
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferRejectsPreparedC2paCarrierWithInvalidSize)
{
    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    openmeta::PreparedTransferBundle bundle;
    ASSERT_TRUE(build_staged_signed_c2pa_bundle(
        std::span<const std::byte>(logical.data(), logical.size()), &bundle));
    ASSERT_EQ(count_blocks_with_route(bundle, "jpeg:app11-c2pa"), 1U);
    ASSERT_FALSE(bundle.blocks.empty());
    ASSERT_GE(bundle.blocks[0].payload.size(), 16U);
    bundle.blocks[0].payload[11] = std::byte { 0x40 };

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(result.compile.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(result.compile.errors, 1U);
    EXPECT_TRUE(
        result.compile.message.find("logical payload size is inconsistent")
        != std::string::npos);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.emit.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(result.emit.errors, 1U);
    EXPECT_TRUE(result.emit.message.find("compile failure")
                != std::string::npos);
    EXPECT_TRUE(writer.out.empty());
}

TEST(MetadataTransferApi, ExecutePreparedTransferTiffEmitToWriterUnsupported)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(xmp);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.emit.errors, 1U);
    EXPECT_TRUE(result.emit.message.find("not supported for tiff")
                != std::string::npos);
    EXPECT_TRUE(writer.out.empty());
}

TEST(MetadataTransferApi, ExecutePreparedTransferDngEmitToWriterUnsupported)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Dng;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(xmp);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.emit.errors, 1U);
    EXPECT_TRUE(result.emit.message.find("not supported for tiff/dng")
                != std::string::npos);
    EXPECT_TRUE(writer.out.empty());
}

TEST(MetadataTransferApi, ExecutePreparedTransferJxlEmitToWriter)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jxl:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' }, std::byte { 'x' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 },
                       std::byte { 'j' },  std::byte { 'u' },
                       std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit_output_size, 26U);
    ASSERT_EQ(writer.out.size(), 26U);
    uint32_t first_size = 0U;
    ASSERT_TRUE(read_test_u32be(std::span<const std::byte>(writer.out.data(),
                                                           writer.out.size()),
                                0U, &first_size));
    EXPECT_EQ(first_size, 10U);
    EXPECT_EQ(writer.out[4], std::byte { 'x' });
    EXPECT_EQ(writer.out[5], std::byte { 'm' });
    EXPECT_EQ(writer.out[6], std::byte { 'l' });
    EXPECT_EQ(writer.out[7], std::byte { ' ' });
    uint32_t second_size = 0U;
    ASSERT_TRUE(read_test_u32be(std::span<const std::byte>(writer.out.data(),
                                                           writer.out.size()),
                                10U, &second_size));
    EXPECT_EQ(second_size, 16U);
    EXPECT_EQ(writer.out[14], std::byte { 'j' });
    EXPECT_EQ(writer.out[15], std::byte { 'u' });
    EXPECT_EQ(writer.out[16], std::byte { 'm' });
    EXPECT_EQ(writer.out[17], std::byte { 'b' });
    ASSERT_EQ(result.jxl_box_summary.size(), 2U);
    EXPECT_EQ(result.jxl_box_summary[0].type,
              (std::array<char, 4> { 'j', 'u', 'm', 'b' }));
    EXPECT_EQ(result.jxl_box_summary[1].type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
}

TEST(MetadataTransferApi, ExecutePreparedTransferJxlEmitToWriterRejectsIcc)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jxl:icc-profile";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 },
                    std::byte { 0x43 } };
    bundle.blocks.push_back(icc);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.emit.code, openmeta::EmitTransferCode::UnsupportedRoute);
    EXPECT_EQ(result.emit.failed_block_index, 0U);
    EXPECT_TRUE(
        result.emit.message.find("unsupported jxl route: jxl:icc-profile")
        != std::string::npos);
    EXPECT_TRUE(writer.out.empty());
}

TEST(MetadataTransferApi, ExecutePreparedTransferJxlEditAppendsBoxes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jxl:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload  = make_test_jxl_exif_box_payload();
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jxl:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' }, std::byte { 'x' }, std::byte { '/' },
                     std::byte { '>' } };
    bundle.blocks.push_back(xmp);

    const std::vector<std::byte> input = make_minimal_jxl_file();

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_GT(result.edit_output_size, static_cast<uint64_t>(input.size()));
    ASSERT_FALSE(result.edited_output.empty());

    std::array<openmeta::ContainerBlockRef, 8> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_jxl(
        std::span<const std::byte>(result.edited_output.data(),
                                   result.edited_output.size()),
        std::span<openmeta::ContainerBlockRef>(blocks.data(), blocks.size()));
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    ASSERT_EQ(scan.written, 2U);
    uint32_t exif_count = 0U;
    uint32_t xmp_count  = 0U;
    for (uint32_t i = 0U; i < scan.written; ++i) {
        if (blocks[i].kind == openmeta::ContainerBlockKind::Exif) {
            exif_count += 1U;
        } else if (blocks[i].kind == openmeta::ContainerBlockKind::Xmp) {
            xmp_count += 1U;
        }
    }
    EXPECT_EQ(exif_count, 1U);
    EXPECT_EQ(xmp_count, 1U);
}

TEST(MetadataTransferApi, ExecutePreparedTransferJxlEditPreservesUnrelatedBoxes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 },
                       std::byte { 'j' },  std::byte { 'u' },
                       std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    std::vector<std::byte> input = make_minimal_jxl_file();
    const std::vector<std::byte> exif_payload = make_test_jxl_exif_box_payload();
    append_bmff_box(&input, openmeta::fourcc('E', 'x', 'i', 'f'),
                    std::span<const std::byte>(exif_payload.data(),
                                               exif_payload.size()));
    static const std::array<std::byte, 4> kXml
        = { std::byte { '<' }, std::byte { 'x' }, std::byte { '/' },
            std::byte { '>' } };
    append_bmff_box(&input, openmeta::fourcc('x', 'm', 'l', ' '),
                    std::span<const std::byte>(kXml.data(), kXml.size()));

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.edited_output.empty());

    std::array<openmeta::ContainerBlockRef, 8> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_jxl(
        std::span<const std::byte>(result.edited_output.data(),
                                   result.edited_output.size()),
        std::span<openmeta::ContainerBlockRef>(blocks.data(), blocks.size()));
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    ASSERT_EQ(scan.written, 3U);
    EXPECT_EQ(blocks[0].kind, openmeta::ContainerBlockKind::Exif);
    EXPECT_EQ(blocks[1].kind, openmeta::ContainerBlockKind::Xmp);
    EXPECT_EQ(blocks[2].kind, openmeta::ContainerBlockKind::Jumbf);
    EXPECT_EQ(blocks[2].id, openmeta::fourcc('j', 'u', 'm', 'b'));
}

TEST(MetadataTransferApi,
     ExecutePreparedTransferJxlEditPreservesSourceC2paWhenReplacingJumbf)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 },
                       std::byte { 'j' },  std::byte { 'u' },
                       std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    const std::vector<std::byte> input = make_test_jxl_with_c2pa(false);

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.edited_output.empty());

    std::array<openmeta::ContainerBlockRef, 8> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_jxl(
        std::span<const std::byte>(result.edited_output.data(),
                                   result.edited_output.size()),
        std::span<openmeta::ContainerBlockRef>(blocks.data(), blocks.size()));
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    ASSERT_EQ(scan.written, 2U);
    EXPECT_EQ(blocks[0].kind, openmeta::ContainerBlockKind::Jumbf);
    EXPECT_EQ(blocks[1].kind, openmeta::ContainerBlockKind::Jumbf);
}

#if defined(OPENMETA_HAS_BROTLI) && OPENMETA_HAS_BROTLI \
    && defined(OPENMETA_HAS_BROTLI_ENCODER) && OPENMETA_HAS_BROTLI_ENCODER
TEST(MetadataTransferApi, ExecutePreparedTransferJxlEditReplacesCompressedJumbf)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 },
                       std::byte { 'j' },  std::byte { 'u' },
                       std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    std::vector<std::byte> input         = make_minimal_jxl_file();
    const std::vector<std::byte> logical = make_logical_jumbf_payload("acme");
    append_jxl_brob_box(&input, openmeta::fourcc('j', 'u', 'm', 'b'),
                        std::span<const std::byte>(logical.data(),
                                                   logical.size()));

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.edited_output.empty());

    std::array<openmeta::ContainerBlockRef, 8> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_jxl(
        std::span<const std::byte>(result.edited_output.data(),
                                   result.edited_output.size()),
        std::span<openmeta::ContainerBlockRef>(blocks.data(), blocks.size()));
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    ASSERT_EQ(scan.written, 1U);
    EXPECT_EQ(blocks[0].kind, openmeta::ContainerBlockKind::Jumbf);
}

TEST(MetadataTransferApi, ExecutePreparedTransferJxlEditReplacesCompressedC2pa)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    const std::vector<std::byte> logical = make_semantic_c2pa_logical_payload();
    ASSERT_GE(logical.size(), 8U);

    openmeta::PreparedTransferBlock c2pa;
    c2pa.kind     = openmeta::TransferBlockKind::C2pa;
    c2pa.route    = "jxl:box-jumb";
    c2pa.box_type = { 'j', 'u', 'm', 'b' };
    c2pa.payload.assign(logical.begin() + 8U, logical.end());
    bundle.blocks.push_back(c2pa);

    std::vector<std::byte> input = make_minimal_jxl_file();
    append_jxl_brob_box(&input, openmeta::fourcc('j', 'u', 'm', 'b'),
                        std::span<const std::byte>(logical.data(),
                                                   logical.size()));

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.edited_output.empty());

    std::array<openmeta::ContainerBlockRef, 8> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_jxl(
        std::span<const std::byte>(result.edited_output.data(),
                                   result.edited_output.size()),
        std::span<openmeta::ContainerBlockRef>(blocks.data(), blocks.size()));
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    ASSERT_EQ(scan.written, 1U);
    EXPECT_EQ(blocks[0].kind, openmeta::ContainerBlockKind::Jumbf);
}
#endif

TEST(MetadataTransferApi, ExecutePreparedTransferJxlEditReplacesMatchingBoxes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jxl:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload  = make_test_jxl_exif_box_payload();
    bundle.blocks.push_back(exif);

    std::vector<std::byte> input          = make_minimal_jxl_file();
    const std::vector<std::byte> old_exif = make_test_jxl_exif_box_payload();
    append_bmff_box(&input, openmeta::fourcc('E', 'x', 'i', 'f'),
                    std::span<const std::byte>(old_exif.data(),
                                               old_exif.size()));
    static const std::array<std::byte, 4> kXml
        = { std::byte { '<' }, std::byte { 'x' }, std::byte { '/' },
            std::byte { '>' } };
    append_bmff_box(&input, openmeta::fourcc('x', 'm', 'l', ' '),
                    std::span<const std::byte>(kXml.data(), kXml.size()));

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.edited_output.empty());

    std::array<openmeta::ContainerBlockRef, 8> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_jxl(
        std::span<const std::byte>(result.edited_output.data(),
                                   result.edited_output.size()),
        std::span<openmeta::ContainerBlockRef>(blocks.data(), blocks.size()));
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    ASSERT_EQ(scan.written, 2U);
    uint32_t exif_count = 0U;
    uint32_t xmp_count  = 0U;
    for (uint32_t i = 0U; i < scan.written; ++i) {
        if (blocks[i].kind == openmeta::ContainerBlockKind::Exif) {
            exif_count += 1U;
        } else if (blocks[i].kind == openmeta::ContainerBlockKind::Xmp) {
            xmp_count += 1U;
        }
    }
    EXPECT_EQ(exif_count, 1U);
    EXPECT_EQ(xmp_count, 1U);
}

TEST(MetadataTransferApi, ExecutePreparedTransferJxlEditRejectsIccProfile)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jxl:icc-profile";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 },
                    std::byte { 0x43 } };
    bundle.blocks.push_back(icc);

    const std::vector<std::byte> input = make_minimal_jxl_file();

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);
    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(result.edit_plan_message.find("encoder ICC profile route")
                != std::string::npos);
    EXPECT_NE(result.edit_apply.status, openmeta::TransferStatus::Ok);
}

TEST(MetadataTransferApi, BuildExecutedTransferPackageBatchJxlMatchesEdit)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jxl:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload  = make_test_jxl_exif_box_payload();
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jxl:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    const std::vector<std::byte> input = make_minimal_jxl_file();

    openmeta::ExecutePreparedTransferOptions plan_only_options;
    plan_only_options.edit_requested = true;
    const openmeta::ExecutePreparedTransferResult plan_only
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            plan_only_options);
    ASSERT_EQ(plan_only.edit_plan_status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult batch_result
        = openmeta::build_executed_transfer_package_batch(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan_only, &batch);
    ASSERT_EQ(batch_result.status, openmeta::TransferStatus::Ok);

    openmeta::ExecutePreparedTransferOptions apply_options;
    apply_options.edit_requested = true;
    apply_options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult applied
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            apply_options);
    ASSERT_EQ(applied.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch.output_size,
              static_cast<uint64_t>(applied.edited_output.size()));
    ASSERT_LE(batch.output_size,
              static_cast<uint64_t>(std::numeric_limits<size_t>::max()));

    std::vector<std::byte> bytes(static_cast<size_t>(batch.output_size));
    openmeta::SpanTransferByteWriter writer(
        std::span<std::byte>(bytes.data(), bytes.size()));
    const openmeta::EmitTransferResult write_result
        = openmeta::write_prepared_transfer_package_batch(batch, writer);
    ASSERT_EQ(write_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(writer.bytes_written(), applied.edited_output.size());
    EXPECT_EQ(std::memcmp(bytes.data(), applied.edited_output.data(),
                          applied.edited_output.size()),
              0);
}

TEST(MetadataTransferApi, ExecutePreparedTransferWebpEmitToWriter)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Webp;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "webp:chunk-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "webp:chunk-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit_output_size, 22U);
    ASSERT_EQ(writer.out.size(), 22U);
    EXPECT_EQ(writer.out[0], std::byte { 'E' });
    EXPECT_EQ(writer.out[1], std::byte { 'X' });
    EXPECT_EQ(writer.out[2], std::byte { 'I' });
    EXPECT_EQ(writer.out[3], std::byte { 'F' });
    EXPECT_EQ(read_u32le(std::span<const std::byte>(writer.out.data(),
                                                    writer.out.size()),
                         4U),
              2U);
    EXPECT_EQ(writer.out[10], std::byte { 'X' });
    EXPECT_EQ(writer.out[11], std::byte { 'M' });
    EXPECT_EQ(writer.out[12], std::byte { 'P' });
    EXPECT_EQ(writer.out[13], std::byte { ' ' });
    EXPECT_EQ(read_u32le(std::span<const std::byte>(writer.out.data(),
                                                    writer.out.size()),
                         14U),
              3U);
    ASSERT_EQ(result.webp_chunk_summary.size(), 2U);
    EXPECT_EQ(result.webp_chunk_summary[0].type,
              (std::array<char, 4> { 'E', 'X', 'I', 'F' }));
    EXPECT_EQ(result.webp_chunk_summary[0].count, 1U);
    EXPECT_EQ(result.webp_chunk_summary[0].bytes, 2U);
    EXPECT_EQ(result.webp_chunk_summary[1].type,
              (std::array<char, 4> { 'X', 'M', 'P', ' ' }));
    EXPECT_EQ(result.webp_chunk_summary[1].count, 1U);
    EXPECT_EQ(result.webp_chunk_summary[1].bytes, 3U);
}

TEST(MetadataTransferApi, ExecutePreparedTransferWebpRoundTripsSimpleMetaRead)
{
    openmeta::MetaStore source;
    const openmeta::BlockId block = source.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key
        = openmeta::make_exif_tag_key(source.arena(), "exififd", 0x9003U);
    exif.value = openmeta::make_text(source.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0U;
    ASSERT_NE(source.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        source.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(source.arena(), "OpenMeta WebP",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    ASSERT_NE(source.add_entry(xmp), openmeta::kInvalidEntryId);
    source.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Webp;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(source, request, &bundle)
                  .status,
              openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;
    const openmeta::ExecutePreparedTransferResult emitted
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    ASSERT_EQ(emitted.emit.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(writer.out.empty());

    const std::vector<std::byte> webp = build_minimal_webp_file(
        std::span<const std::byte>(writer.out.data(), writer.out.size()),
        static_cast<uint8_t>(0x08U | 0x04U));

    std::array<openmeta::ContainerBlockRef, 16> scan_blocks {};
    const openmeta::ScanResult scan = openmeta::scan_webp(
        std::span<const std::byte>(webp.data(), webp.size()), scan_blocks);
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    ASSERT_EQ(scan.written, 2U);

    openmeta::MetaStore decoded;
    std::array<openmeta::ContainerBlockRef, 16> blocks {};
    std::array<openmeta::ExifIfdRef, 16> ifds {};
    std::array<std::byte, 8192> payload {};
    std::array<uint32_t, 128> payload_parts {};
    openmeta::SimpleMetaDecodeOptions decode_options;

    const openmeta::SimpleMetaResult read = openmeta::simple_meta_read(
        std::span<const std::byte>(webp.data(), webp.size()), decoded, blocks,
        ifds, payload, payload_parts, decode_options);
    EXPECT_EQ(read.scan.status, openmeta::ScanStatus::Ok);

    decoded.finalize();

    const std::span<const openmeta::EntryId> exif_ids
        = decoded.find_all(exif_key_view("exififd", 0x9003U));
    ASSERT_EQ(exif_ids.size(), 1U);
    EXPECT_EQ(arena_text(decoded, decoded.entry(exif_ids[0])),
              "2024:01:02 03:04:05");

    const std::span<const openmeta::EntryId> creator_ids = decoded.find_all(
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"));
    ASSERT_EQ(creator_ids.size(), 1U);
    EXPECT_EQ(arena_text(decoded, decoded.entry(creator_ids[0])),
              "OpenMeta WebP");
}

TEST(MetadataTransferApi, ExecutePreparedTransferWebpEditRewritesMetadataChunks)
{
    openmeta::MetaStore source;
    const openmeta::BlockId block = source.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key
        = openmeta::make_exif_tag_key(source.arena(), "exififd", 0x9003U);
    exif.value = openmeta::make_text(source.arena(), "2025:06:07 08:09:10",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0U;
    ASSERT_NE(source.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        source.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(source.arena(), "OpenMeta WebP Edit",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    ASSERT_NE(source.add_entry(xmp), openmeta::kInvalidEntryId);
    source.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Webp;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(source, request, &bundle)
                  .status,
              openmeta::TransferStatus::Ok);

    std::vector<std::byte> webp = {
        std::byte { 'R' }, std::byte { 'I' }, std::byte { 'F' },
        std::byte { 'F' }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 'W' },
        std::byte { 'E' }, std::byte { 'B' }, std::byte { 'P' },
    };
    const std::array<std::byte, 10> vp8x_payload = {
        std::byte { 0x0CU }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 },  std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 },  std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 },
    };
    append_webp_chunk(&webp, openmeta::fourcc('V', 'P', '8', 'X'),
                      std::span<const std::byte>(vp8x_payload.data(),
                                                 vp8x_payload.size()));
    const std::array<std::byte, 4> old_exif = {
        std::byte { 0x01 }, std::byte { 0x02 }, std::byte { 0x03 },
        std::byte { 0x04 },
    };
    append_webp_chunk(&webp, openmeta::fourcc('E', 'X', 'I', 'F'),
                      std::span<const std::byte>(old_exif.data(),
                                                 old_exif.size()));
    const std::vector<std::byte> old_xmp = ascii_z("<old-xmp/>");
    append_webp_chunk(&webp, openmeta::fourcc('X', 'M', 'P', ' '),
                      std::span<const std::byte>(old_xmp.data(),
                                                 old_xmp.size()));
    const std::array<std::byte, 1> vp8_payload = { std::byte { 0x00 } };
    append_webp_chunk(&webp, openmeta::fourcc('V', 'P', '8', ' '),
                      std::span<const std::byte>(vp8_payload.data(),
                                                 vp8_payload.size()));
    const uint32_t riff_size = static_cast<uint32_t>(webp.size() - 8U);
    webp[4] = static_cast<std::byte>((riff_size >> 0U) & 0xFFU);
    webp[5] = static_cast<std::byte>((riff_size >> 8U) & 0xFFU);
    webp[6] = static_cast<std::byte>((riff_size >> 16U) & 0xFFU);
    webp[7] = static_cast<std::byte>((riff_size >> 24U) & 0xFFU);

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult applied
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(webp.data(), webp.size()),
            options);
    ASSERT_EQ(applied.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(applied.edit_apply.status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult batch_result
        = openmeta::build_executed_transfer_package_batch(
            std::span<const std::byte>(webp.data(), webp.size()), bundle,
            applied, &batch);
    ASSERT_EQ(batch_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch.output_size,
              static_cast<uint64_t>(applied.edited_output.size()));
    openmeta::EmitTransferResult write_result;
    const std::vector<std::byte> batch_bytes
        = materialize_transfer_package_batch(batch, &write_result);
    ASSERT_EQ(write_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch_bytes.size(), applied.edited_output.size());
    EXPECT_EQ(std::memcmp(batch_bytes.data(), applied.edited_output.data(),
                          applied.edited_output.size()),
              0);

    const std::span<const std::byte> edited(applied.edited_output.data(),
                                            applied.edited_output.size());
    ASSERT_TRUE(payload_contains_ascii(edited, "RIFF"));
    ASSERT_TRUE(payload_contains_ascii(edited.subspan(8U), "WEBP"));
    ASSERT_TRUE(payload_contains_ascii(edited.subspan(12U), "VP8X"));
    EXPECT_EQ(std::to_integer<uint8_t>(edited[20U]) & 0x0CU, 0x0CU);

    std::array<openmeta::ContainerBlockRef, 16> scan_blocks {};
    const openmeta::ScanResult scan
        = openmeta::scan_webp(edited, scan_blocks);
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    uint32_t exif_blocks = 0U;
    uint32_t xmp_blocks  = 0U;
    for (uint32_t i = 0U; i < scan.written; ++i) {
        if (scan_blocks[i].kind == openmeta::ContainerBlockKind::Exif) {
            exif_blocks += 1U;
        } else if (scan_blocks[i].kind == openmeta::ContainerBlockKind::Xmp) {
            xmp_blocks += 1U;
        }
    }
    EXPECT_EQ(exif_blocks, 1U);
    EXPECT_EQ(xmp_blocks, 1U);

    openmeta::MetaStore decoded;
    std::array<openmeta::ContainerBlockRef, 16> blocks {};
    std::array<openmeta::ExifIfdRef, 16> ifds {};
    std::array<std::byte, 8192> payload {};
    std::array<uint32_t, 128> payload_parts {};
    openmeta::SimpleMetaDecodeOptions decode_options;
    const openmeta::SimpleMetaResult read = openmeta::simple_meta_read(
        edited, decoded, blocks, ifds, payload, payload_parts, decode_options);
    EXPECT_EQ(read.scan.status, openmeta::ScanStatus::Ok);
    decoded.finalize();

    const std::span<const openmeta::EntryId> exif_ids
        = decoded.find_all(exif_key_view("exififd", 0x9003U));
    ASSERT_EQ(exif_ids.size(), 1U);
    EXPECT_EQ(arena_text(decoded, decoded.entry(exif_ids[0])),
              "2025:06:07 08:09:10");

    const std::span<const openmeta::EntryId> creator_ids = decoded.find_all(
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"));
    ASSERT_EQ(creator_ids.size(), 1U);
    EXPECT_EQ(arena_text(decoded, decoded.entry(creator_ids[0])),
              "OpenMeta WebP Edit");
}

TEST(MetadataTransferApi, ExecutePreparedTransferWebpEditRewritesIccChunk)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Webp;

    static const std::array<std::byte, 6> kNewIcc = {
        std::byte { 'N' }, std::byte { 'E' }, std::byte { 'W' },
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    openmeta::PreparedTransferBlock icc;
    icc.route   = "webp:chunk-iccp";
    icc.payload = std::vector<std::byte>(kNewIcc.begin(), kNewIcc.end());
    bundle.blocks.push_back(std::move(icc));

    std::vector<std::byte> webp = {
        std::byte { 'R' }, std::byte { 'I' }, std::byte { 'F' },
        std::byte { 'F' }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 'W' },
        std::byte { 'E' }, std::byte { 'B' }, std::byte { 'P' },
    };
    const std::array<std::byte, 10> vp8x_payload = {
        std::byte { 0x20U }, std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 },  std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 },  std::byte { 0x00 }, std::byte { 0x00 },
        std::byte { 0x00 },
    };
    append_webp_chunk(&webp, openmeta::fourcc('V', 'P', '8', 'X'),
                      std::span<const std::byte>(vp8x_payload.data(),
                                                 vp8x_payload.size()));
    static const std::array<std::byte, 6> kOldIcc = {
        std::byte { 'O' }, std::byte { 'L' }, std::byte { 'D' },
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    append_webp_chunk(&webp, openmeta::fourcc('I', 'C', 'C', 'P'),
                      std::span<const std::byte>(kOldIcc.data(),
                                                 kOldIcc.size()));
    const std::array<std::byte, 1> vp8_payload = { std::byte { 0x00 } };
    append_webp_chunk(&webp, openmeta::fourcc('V', 'P', '8', ' '),
                      std::span<const std::byte>(vp8_payload.data(),
                                                 vp8_payload.size()));
    const uint32_t riff_size = static_cast<uint32_t>(webp.size() - 8U);
    webp[4] = static_cast<std::byte>((riff_size >> 0U) & 0xFFU);
    webp[5] = static_cast<std::byte>((riff_size >> 8U) & 0xFFU);
    webp[6] = static_cast<std::byte>((riff_size >> 16U) & 0xFFU);
    webp[7] = static_cast<std::byte>((riff_size >> 24U) & 0xFFU);

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult applied
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(webp.data(), webp.size()),
            options);
    ASSERT_EQ(applied.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(applied.edit_apply.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> edited(applied.edited_output.data(),
                                            applied.edited_output.size());
    std::array<openmeta::ContainerBlockRef, 16> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_webp(edited, blocks);
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);

    uint32_t icc_blocks = 0U;
    for (uint32_t i = 0U; i < scan.written; ++i) {
        if (blocks[i].kind != openmeta::ContainerBlockKind::Icc) {
            continue;
        }
        icc_blocks += 1U;
        const std::span<const std::byte> icc_bytes(
            edited.data() + static_cast<std::ptrdiff_t>(blocks[i].data_offset),
            static_cast<size_t>(blocks[i].data_size));
        ASSERT_EQ(icc_bytes.size(), kNewIcc.size());
        EXPECT_EQ(std::memcmp(icc_bytes.data(), kNewIcc.data(), kNewIcc.size()),
                  0);
    }
    EXPECT_EQ(icc_blocks, 1U);
}

TEST(MetadataTransferApi, BuildExecutedTransferPackageBatchWebpIccMatchesEdit)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Webp;

    static const std::array<std::byte, 6> kNewIcc = {
        std::byte { 'N' }, std::byte { 'E' }, std::byte { 'W' },
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    openmeta::PreparedTransferBlock icc;
    icc.route   = "webp:chunk-iccp";
    icc.payload = std::vector<std::byte>(kNewIcc.begin(), kNewIcc.end());
    bundle.blocks.push_back(std::move(icc));

    std::vector<std::byte> metadata;
    static const std::array<std::byte, 6> kOldIcc = {
        std::byte { 'O' }, std::byte { 'L' }, std::byte { 'D' },
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    append_webp_chunk(&metadata, openmeta::fourcc('I', 'C', 'C', 'P'),
                      std::span<const std::byte>(kOldIcc.data(),
                                                 kOldIcc.size()));
    const std::vector<std::byte> input = build_minimal_webp_file(
        std::span<const std::byte>(metadata.data(), metadata.size()),
        0x20U);

    openmeta::ExecutePreparedTransferOptions plan_only_options;
    plan_only_options.edit_requested = true;
    const openmeta::ExecutePreparedTransferResult plan_only
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            plan_only_options);
    ASSERT_EQ(plan_only.edit_plan_status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult batch_result
        = openmeta::build_executed_transfer_package_batch(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan_only, &batch);
    ASSERT_EQ(batch_result.status, openmeta::TransferStatus::Ok);

    openmeta::ExecutePreparedTransferOptions apply_options;
    apply_options.edit_requested = true;
    apply_options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult applied
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            apply_options);
    ASSERT_EQ(applied.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch.output_size,
              static_cast<uint64_t>(applied.edited_output.size()));

    openmeta::EmitTransferResult write_result;
    const std::vector<std::byte> batch_bytes
        = materialize_transfer_package_batch(batch, &write_result);
    ASSERT_EQ(write_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch_bytes.size(), applied.edited_output.size());
    EXPECT_EQ(std::memcmp(batch_bytes.data(), applied.edited_output.data(),
                          applied.edited_output.size()),
              0);
}

TEST(MetadataTransferApi, ExecutePreparedTransferPngEmitToWriter)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Png;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "png:chunk-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "png:chunk-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit_output_size, 29U);
    ASSERT_EQ(writer.out.size(), 29U);

    const std::span<const std::byte> bytes(writer.out.data(), writer.out.size());
    uint32_t first_size = 0U;
    ASSERT_TRUE(read_test_u32be(bytes, 0U, &first_size));
    EXPECT_EQ(first_size, 2U);
    EXPECT_EQ(writer.out[4], std::byte { 'e' });
    EXPECT_EQ(writer.out[5], std::byte { 'X' });
    EXPECT_EQ(writer.out[6], std::byte { 'I' });
    EXPECT_EQ(writer.out[7], std::byte { 'f' });

    const size_t second_off = 12U + static_cast<size_t>(first_size);
    uint32_t second_size    = 0U;
    ASSERT_TRUE(read_test_u32be(bytes, second_off, &second_size));
    EXPECT_EQ(second_size, 3U);
    EXPECT_EQ(writer.out[second_off + 4U], std::byte { 'i' });
    EXPECT_EQ(writer.out[second_off + 5U], std::byte { 'T' });
    EXPECT_EQ(writer.out[second_off + 6U], std::byte { 'X' });
    EXPECT_EQ(writer.out[second_off + 7U], std::byte { 't' });

    ASSERT_EQ(result.png_chunk_summary.size(), 2U);
    EXPECT_EQ(result.png_chunk_summary[0].type,
              (std::array<char, 4> { 'e', 'X', 'I', 'f' }));
    EXPECT_EQ(result.png_chunk_summary[0].count, 1U);
    EXPECT_EQ(result.png_chunk_summary[0].bytes, 2U);
    EXPECT_EQ(result.png_chunk_summary[1].type,
              (std::array<char, 4> { 'i', 'T', 'X', 't' }));
    EXPECT_EQ(result.png_chunk_summary[1].count, 1U);
    EXPECT_EQ(result.png_chunk_summary[1].bytes, 3U);
}

TEST(MetadataTransferApi, ExecutePreparedTransferPngRoundTripsSimpleMetaRead)
{
    openmeta::MetaStore source;
    const openmeta::BlockId block = source.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key
        = openmeta::make_exif_tag_key(source.arena(), "exififd", 0x9003U);
    exif.value = openmeta::make_text(source.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0U;
    ASSERT_NE(source.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        source.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(source.arena(), "OpenMeta PNG",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    ASSERT_NE(source.add_entry(xmp), openmeta::kInvalidEntryId);
    source.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Png;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(source, request, &bundle)
                  .status,
              openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;
    const openmeta::ExecutePreparedTransferResult emitted
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    ASSERT_EQ(emitted.emit.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(writer.out.empty());

    const std::vector<std::byte> png = build_minimal_png_file(
        std::span<const std::byte>(writer.out.data(), writer.out.size()));

    openmeta::MetaStore decoded;
    std::array<openmeta::ContainerBlockRef, 16> blocks {};
    std::array<openmeta::ExifIfdRef, 16> ifds {};
    std::array<std::byte, 8192> payload {};
    std::array<uint32_t, 128> payload_parts {};
    openmeta::SimpleMetaDecodeOptions decode_options;

    const openmeta::SimpleMetaResult read = openmeta::simple_meta_read(
        std::span<const std::byte>(png.data(), png.size()), decoded, blocks,
        ifds, payload, payload_parts, decode_options);
    EXPECT_EQ(read.scan.status, openmeta::ScanStatus::Ok);

    decoded.finalize();

    const std::span<const openmeta::EntryId> exif_ids
        = decoded.find_all(exif_key_view("exififd", 0x9003U));
    ASSERT_EQ(exif_ids.size(), 1U);
    EXPECT_EQ(arena_text(decoded, decoded.entry(exif_ids[0])),
              "2024:01:02 03:04:05");

    const std::span<const openmeta::EntryId> creator_ids = decoded.find_all(
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"));
    ASSERT_EQ(creator_ids.size(), 1U);
    EXPECT_EQ(arena_text(decoded, decoded.entry(creator_ids[0])),
              "OpenMeta PNG");
}

TEST(MetadataTransferApi, ExecutePreparedTransferJp2EmitToWriter)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jp2;

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jp2:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload  = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jp2:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit_output_size, 21U);
    ASSERT_EQ(writer.out.size(), 21U);

    const std::span<const std::byte> bytes(writer.out.data(), writer.out.size());
    uint32_t first_size = 0U;
    ASSERT_TRUE(read_test_u32be(bytes, 0U, &first_size));
    EXPECT_EQ(first_size, 10U);
    EXPECT_EQ(writer.out[4], std::byte { 'E' });
    EXPECT_EQ(writer.out[5], std::byte { 'x' });
    EXPECT_EQ(writer.out[6], std::byte { 'i' });
    EXPECT_EQ(writer.out[7], std::byte { 'f' });

    const size_t second_off = static_cast<size_t>(first_size);
    uint32_t second_size    = 0U;
    ASSERT_TRUE(read_test_u32be(bytes, second_off, &second_size));
    EXPECT_EQ(second_size, 11U);
    EXPECT_EQ(writer.out[second_off + 4U], std::byte { 'x' });
    EXPECT_EQ(writer.out[second_off + 5U], std::byte { 'm' });
    EXPECT_EQ(writer.out[second_off + 6U], std::byte { 'l' });
    EXPECT_EQ(writer.out[second_off + 7U], std::byte { ' ' });

    ASSERT_EQ(result.jp2_box_summary.size(), 2U);
    EXPECT_EQ(result.jp2_box_summary[0].type,
              (std::array<char, 4> { 'E', 'x', 'i', 'f' }));
    EXPECT_EQ(result.jp2_box_summary[0].count, 1U);
    EXPECT_EQ(result.jp2_box_summary[0].bytes, 2U);
    EXPECT_EQ(result.jp2_box_summary[1].type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
    EXPECT_EQ(result.jp2_box_summary[1].count, 1U);
    EXPECT_EQ(result.jp2_box_summary[1].bytes, 3U);
}

TEST(MetadataTransferApi, ExecutePreparedTransferExrEmitToWriterUnsupported)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Exr;

    openmeta::PreparedTransferBlock make;
    make.kind    = openmeta::TransferBlockKind::ExrAttribute;
    make.route   = "exr:attribute-string";
    make.payload = make_test_exr_string_attribute_payload("Make", "Vendor");
    bundle.blocks.push_back(make);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.emit.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(result.emit.errors, 1U);
    EXPECT_TRUE(result.emit.message.find("not supported for exr")
                != std::string::npos);
    EXPECT_TRUE(writer.out.empty());
}

TEST(MetadataTransferApi, ExecutePreparedTransferJp2RoundTripsSimpleMetaRead)
{
    openmeta::MetaStore source;
    const openmeta::BlockId block = source.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key
        = openmeta::make_exif_tag_key(source.arena(), "exififd", 0x9003U);
    exif.value = openmeta::make_text(source.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0U;
    ASSERT_NE(source.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        source.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(source.arena(), "OpenMeta JP2",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    ASSERT_NE(source.add_entry(xmp), openmeta::kInvalidEntryId);
    source.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Jp2;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(source, request, &bundle)
                  .status,
              openmeta::TransferStatus::Ok);

    BufferByteWriter writer;
    openmeta::ExecutePreparedTransferOptions options;
    options.emit_output_writer = &writer;
    const openmeta::ExecutePreparedTransferResult emitted
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    ASSERT_EQ(emitted.emit.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(writer.out.empty());

    const std::vector<std::byte> jp2 = build_minimal_jp2_file(
        std::span<const std::byte>(writer.out.data(), writer.out.size()));

    openmeta::MetaStore decoded;
    std::array<openmeta::ContainerBlockRef, 16> blocks {};
    std::array<openmeta::ExifIfdRef, 16> ifds {};
    std::array<std::byte, 8192> payload {};
    std::array<uint32_t, 128> payload_parts {};
    openmeta::SimpleMetaDecodeOptions decode_options;

    const openmeta::SimpleMetaResult read = openmeta::simple_meta_read(
        std::span<const std::byte>(jp2.data(), jp2.size()), decoded, blocks,
        ifds, payload, payload_parts, decode_options);
    EXPECT_EQ(read.scan.status, openmeta::ScanStatus::Ok);

    decoded.finalize();

    const std::span<const openmeta::EntryId> exif_ids
        = decoded.find_all(exif_key_view("exififd", 0x9003U));
    ASSERT_EQ(exif_ids.size(), 1U);
    EXPECT_EQ(arena_text(decoded, decoded.entry(exif_ids[0])),
              "2024:01:02 03:04:05");

    const std::span<const openmeta::EntryId> creator_ids = decoded.find_all(
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"));
    ASSERT_EQ(creator_ids.size(), 1U);
    EXPECT_EQ(arena_text(decoded, decoded.entry(creator_ids[0])),
              "OpenMeta JP2");
}

TEST(MetadataTransferApi, ExecutePreparedTransferJp2EditRewritesMetadataBoxes)
{
    openmeta::MetaStore source;
    const openmeta::BlockId block = source.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key
        = openmeta::make_exif_tag_key(source.arena(), "exififd", 0x9003U);
    exif.value = openmeta::make_text(source.arena(), "2025:06:07 08:09:10",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0U;
    ASSERT_NE(source.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        source.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(source.arena(), "OpenMeta JP2 Edit",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    ASSERT_NE(source.add_entry(xmp), openmeta::kInvalidEntryId);
    source.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Jp2;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(source, request, &bundle)
                  .status,
              openmeta::TransferStatus::Ok);

    std::vector<std::byte> old_metadata;
    const std::vector<std::byte> old_exif = make_test_jxl_exif_box_payload();
    append_jp2_box(&old_metadata, openmeta::fourcc('E', 'x', 'i', 'f'),
                   std::span<const std::byte>(old_exif.data(),
                                              old_exif.size()));
    const std::vector<std::byte> old_xmp = ascii_z("<old-xmp/>");
    append_jp2_box(&old_metadata, openmeta::fourcc('x', 'm', 'l', ' '),
                   std::span<const std::byte>(old_xmp.data(), old_xmp.size()));
    const std::array<std::byte, 3> free_payload = {
        std::byte { 0x11 }, std::byte { 0x22 }, std::byte { 0x33 },
    };
    append_jp2_box(&old_metadata, openmeta::fourcc('f', 'r', 'e', 'e'),
                   std::span<const std::byte>(free_payload.data(),
                                              free_payload.size()));
    const std::vector<std::byte> jp2 = build_minimal_jp2_file(
        std::span<const std::byte>(old_metadata.data(), old_metadata.size()));

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult applied
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(jp2.data(), jp2.size()),
            options);

    ASSERT_EQ(applied.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(applied.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(applied.edited_output.empty());

    const std::span<const std::byte> edited(applied.edited_output.data(),
                                            applied.edited_output.size());
    ASSERT_TRUE(payload_contains_ascii(edited, "jP  "));
    ASSERT_TRUE(payload_contains_ascii(edited, "ftyp"));
    ASSERT_TRUE(payload_contains_ascii(edited, "free"));

    std::array<openmeta::ContainerBlockRef, 16> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_jp2(
        edited, std::span<openmeta::ContainerBlockRef>(blocks.data(),
                                                       blocks.size()));
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    uint32_t exif_blocks = 0U;
    uint32_t xmp_blocks  = 0U;
    for (uint32_t i = 0U; i < scan.written; ++i) {
        if (blocks[i].kind == openmeta::ContainerBlockKind::Exif) {
            exif_blocks += 1U;
        } else if (blocks[i].kind == openmeta::ContainerBlockKind::Xmp) {
            xmp_blocks += 1U;
        }
    }
    EXPECT_EQ(exif_blocks, 1U);
    EXPECT_EQ(xmp_blocks, 1U);

    openmeta::MetaStore decoded;
    std::array<openmeta::ExifIfdRef, 16> ifds {};
    std::array<std::byte, 8192> payload {};
    std::array<uint32_t, 128> payload_parts {};
    openmeta::SimpleMetaDecodeOptions decode_options;
    const openmeta::SimpleMetaResult read = openmeta::simple_meta_read(
        edited, decoded, blocks, ifds, payload, payload_parts, decode_options);
    ASSERT_EQ(read.scan.status, openmeta::ScanStatus::Ok);
    decoded.finalize();

    const std::span<const openmeta::EntryId> exif_ids
        = decoded.find_all(exif_key_view("exififd", 0x9003U));
    ASSERT_EQ(exif_ids.size(), 1U);
    EXPECT_EQ(arena_text(decoded, decoded.entry(exif_ids[0])),
              "2025:06:07 08:09:10");

    const std::span<const openmeta::EntryId> creator_ids = decoded.find_all(
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"));
    ASSERT_EQ(creator_ids.size(), 1U);
    EXPECT_EQ(arena_text(decoded, decoded.entry(creator_ids[0])),
              "OpenMeta JP2 Edit");
}

TEST(MetadataTransferApi, ExecutePreparedTransferJp2EditRewritesHeaderColr)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jp2;

    static const std::array<std::byte, 6> kNewIcc = {
        std::byte { 'N' }, std::byte { 'E' }, std::byte { 'W' },
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    openmeta::PreparedTransferBlock icc;
    icc.route    = "jp2:box-jp2h-colr";
    icc.box_type = { 'j', 'p', '2', 'h' };
    icc.payload  = build_test_jp2_colr_icc_box(
        std::span<const std::byte>(kNewIcc.data(), kNewIcc.size()));
    bundle.blocks.push_back(icc);

    static const std::array<std::byte, 6> kOldIcc = {
        std::byte { 'O' }, std::byte { 'L' }, std::byte { 'D' },
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    std::vector<std::byte> metadata;
    const std::vector<std::byte> jp2h = build_test_jp2_header_box(
        std::span<const std::byte>(kOldIcc.data(), kOldIcc.size()));
    metadata.insert(metadata.end(), jp2h.begin(), jp2h.end());
    const std::array<std::byte, 3> free_payload = {
        std::byte { 0x55 }, std::byte { 0x66 }, std::byte { 0x77 },
    };
    append_jp2_box(&metadata, openmeta::fourcc('f', 'r', 'e', 'e'),
                   std::span<const std::byte>(free_payload.data(),
                                              free_payload.size()));
    const std::vector<std::byte> input = build_minimal_jp2_file(
        std::span<const std::byte>(metadata.data(), metadata.size()));

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);
    ASSERT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(result.edited_output.empty());

    const std::span<const std::byte> edited(result.edited_output.data(),
                                            result.edited_output.size());
    ASSERT_TRUE(payload_contains_ascii(edited, "free"));

    std::array<openmeta::ContainerBlockRef, 16> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_jp2(
        edited, std::span<openmeta::ContainerBlockRef>(blocks.data(),
                                                       blocks.size()));
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);

    uint32_t icc_blocks = 0U;
    for (uint32_t i = 0U; i < scan.written; ++i) {
        if (blocks[i].kind != openmeta::ContainerBlockKind::Icc) {
            continue;
        }
        icc_blocks += 1U;
        const std::span<const std::byte> icc_bytes(
            edited.data() + static_cast<std::ptrdiff_t>(blocks[i].data_offset),
            static_cast<size_t>(blocks[i].data_size));
        ASSERT_EQ(icc_bytes.size(), kNewIcc.size());
        EXPECT_EQ(std::memcmp(icc_bytes.data(), kNewIcc.data(), kNewIcc.size()),
                  0);
    }
    EXPECT_EQ(icc_blocks, 1U);
}

TEST(MetadataTransferApi, ExecutePreparedTransferJp2EditRejectsIccRouteWithoutHeader)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jp2;

    static const std::array<std::byte, 3> kIcc = {
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    openmeta::PreparedTransferBlock icc;
    icc.route    = "jp2:box-jp2h-colr";
    icc.box_type = { 'j', 'p', '2', 'h' };
    icc.payload  = build_test_jp2_colr_icc_box(
        std::span<const std::byte>(kIcc.data(), kIcc.size()));
    bundle.blocks.push_back(icc);

    const std::span<const std::byte> empty_metadata;
    const std::vector<std::byte> input = build_minimal_jp2_file(empty_metadata);

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);
    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Unsupported);
    EXPECT_TRUE(result.edit_plan_message.find("existing jp2h box")
                != std::string::npos);
    EXPECT_NE(result.edit_apply.status, openmeta::TransferStatus::Ok);
}

TEST(MetadataTransferApi, BuildExecutedTransferPackageBatchJp2MatchesEdit)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jp2;

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jp2:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload  = make_test_jxl_exif_box_payload();
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jp2:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    const std::span<const std::byte> empty_metadata;
    const std::vector<std::byte> input = build_minimal_jp2_file(empty_metadata);

    openmeta::ExecutePreparedTransferOptions plan_only_options;
    plan_only_options.edit_requested = true;
    const openmeta::ExecutePreparedTransferResult plan_only
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            plan_only_options);
    ASSERT_EQ(plan_only.edit_plan_status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult batch_result
        = openmeta::build_executed_transfer_package_batch(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan_only, &batch);
    ASSERT_EQ(batch_result.status, openmeta::TransferStatus::Ok);

    openmeta::ExecutePreparedTransferOptions apply_options;
    apply_options.edit_requested = true;
    apply_options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult applied
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            apply_options);
    ASSERT_EQ(applied.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch.output_size,
              static_cast<uint64_t>(applied.edited_output.size()));
    ASSERT_LE(batch.output_size,
              static_cast<uint64_t>(std::numeric_limits<size_t>::max()));

    std::vector<std::byte> bytes(static_cast<size_t>(batch.output_size));
    openmeta::SpanTransferByteWriter writer(
        std::span<std::byte>(bytes.data(), bytes.size()));
    const openmeta::EmitTransferResult write_result
        = openmeta::write_prepared_transfer_package_batch(batch, writer);
    ASSERT_EQ(write_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(writer.bytes_written(), applied.edited_output.size());
    EXPECT_EQ(std::memcmp(bytes.data(), applied.edited_output.data(),
                          applied.edited_output.size()),
              0);
}

TEST(MetadataTransferApi, BuildExecutedTransferPackageBatchJp2IccMatchesEdit)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jp2;

    static const std::array<std::byte, 6> kNewIcc = {
        std::byte { 'N' }, std::byte { 'E' }, std::byte { 'W' },
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    openmeta::PreparedTransferBlock icc;
    icc.route    = "jp2:box-jp2h-colr";
    icc.box_type = { 'j', 'p', '2', 'h' };
    icc.payload  = build_test_jp2_colr_icc_box(
        std::span<const std::byte>(kNewIcc.data(), kNewIcc.size()));
    bundle.blocks.push_back(icc);

    static const std::array<std::byte, 6> kOldIcc = {
        std::byte { 'O' }, std::byte { 'L' }, std::byte { 'D' },
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    std::vector<std::byte> metadata;
    const std::vector<std::byte> jp2h = build_test_jp2_header_box(
        std::span<const std::byte>(kOldIcc.data(), kOldIcc.size()));
    metadata.insert(metadata.end(), jp2h.begin(), jp2h.end());
    const std::vector<std::byte> input = build_minimal_jp2_file(
        std::span<const std::byte>(metadata.data(), metadata.size()));

    openmeta::ExecutePreparedTransferOptions plan_only_options;
    plan_only_options.edit_requested = true;
    const openmeta::ExecutePreparedTransferResult plan_only
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            plan_only_options);
    ASSERT_EQ(plan_only.edit_plan_status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult batch_result
        = openmeta::build_executed_transfer_package_batch(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan_only, &batch);
    ASSERT_EQ(batch_result.status, openmeta::TransferStatus::Ok);

    openmeta::ExecutePreparedTransferOptions apply_options;
    apply_options.edit_requested = true;
    apply_options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult applied
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            apply_options);
    ASSERT_EQ(applied.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch.output_size,
              static_cast<uint64_t>(applied.edited_output.size()));

    std::vector<std::byte> bytes(static_cast<size_t>(batch.output_size));
    openmeta::SpanTransferByteWriter writer(
        std::span<std::byte>(bytes.data(), bytes.size()));
    const openmeta::EmitTransferResult write_result
        = openmeta::write_prepared_transfer_package_batch(batch, writer);
    ASSERT_EQ(write_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(writer.bytes_written(), applied.edited_output.size());
    EXPECT_EQ(std::memcmp(bytes.data(), applied.edited_output.data(),
                          applied.edited_output.size()),
              0);
}

TEST(MetadataTransferApi, ExecutePreparedTransferPngEditRewritesMetadataChunks)
{
    openmeta::MetaStore source;
    const openmeta::BlockId block = source.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key
        = openmeta::make_exif_tag_key(source.arena(), "exififd", 0x9003U);
    exif.value = openmeta::make_text(source.arena(), "2025:06:07 08:09:10",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0U;
    ASSERT_NE(source.add_entry(exif), openmeta::kInvalidEntryId);

    openmeta::Entry xmp;
    xmp.key = openmeta::make_xmp_property_key(
        source.arena(), "http://ns.adobe.com/xap/1.0/", "CreatorTool");
    xmp.value = openmeta::make_text(source.arena(), "OpenMeta PNG Edit",
                                    openmeta::TextEncoding::Utf8);
    xmp.origin.block          = block;
    xmp.origin.order_in_block = 1U;
    ASSERT_NE(source.add_entry(xmp), openmeta::kInvalidEntryId);
    source.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Png;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    ASSERT_EQ(openmeta::prepare_metadata_for_target(source, request, &bundle)
                  .status,
              openmeta::TransferStatus::Ok);

    std::vector<std::byte> png = {
        std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
        std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
        std::byte { 0x1A }, std::byte { 0x0A },
    };
    std::vector<std::byte> ihdr;
    append_u32be(&ihdr, 1U);
    append_u32be(&ihdr, 1U);
    ihdr.push_back(std::byte { 8U });
    ihdr.push_back(std::byte { 2U });
    ihdr.push_back(std::byte { 0U });
    ihdr.push_back(std::byte { 0U });
    ihdr.push_back(std::byte { 0U });
    append_png_chunk(&png, openmeta::fourcc('I', 'H', 'D', 'R'), ihdr);

    const std::vector<std::byte> old_xmp
        = build_png_itxt_xmp_chunk_data("<old-xmp/>");
    append_png_chunk(&png, openmeta::fourcc('i', 'T', 'X', 't'), old_xmp);
    const std::array<std::byte, 4> old_exif = {
        std::byte { 0x01 }, std::byte { 0x02 }, std::byte { 0x03 },
        std::byte { 0x04 },
    };
    append_png_chunk(&png, openmeta::fourcc('e', 'X', 'I', 'f'),
                     std::span<const std::byte>(old_exif.data(),
                                                old_exif.size()));
    const std::vector<std::byte> text = ascii_z("Comment");
    append_png_chunk(&png, openmeta::fourcc('t', 'E', 'X', 't'),
                     std::span<const std::byte>(text.data(), text.size()));
    append_png_chunk(&png, openmeta::fourcc('I', 'E', 'N', 'D'), {});

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult applied
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(png.data(), png.size()),
            options);

    ASSERT_EQ(applied.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(applied.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(applied.edited_output.empty());
    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult batch_result
        = openmeta::build_executed_transfer_package_batch(
            std::span<const std::byte>(png.data(), png.size()), bundle,
            applied, &batch);
    ASSERT_EQ(batch_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch.output_size,
              static_cast<uint64_t>(applied.edited_output.size()));
    openmeta::EmitTransferResult write_result;
    const std::vector<std::byte> batch_bytes
        = materialize_transfer_package_batch(batch, &write_result);
    ASSERT_EQ(write_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch_bytes.size(), applied.edited_output.size());
    EXPECT_EQ(std::memcmp(batch_bytes.data(), applied.edited_output.data(),
                          applied.edited_output.size()),
              0);

    const std::span<const std::byte> edited(applied.edited_output.data(),
                                            applied.edited_output.size());
    EXPECT_EQ(edited[0], std::byte { 0x89 });
    uint32_t chunk_size = 0U;
    ASSERT_TRUE(read_test_u32be(edited, 8U, &chunk_size));
    EXPECT_EQ(chunk_size, 13U);

    std::array<openmeta::ContainerBlockRef, 16> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_png(edited, blocks);
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    uint32_t exif_blocks = 0U;
    uint32_t xmp_blocks  = 0U;
    uint32_t text_blocks = 0U;
    for (uint32_t i = 0U; i < scan.written; ++i) {
        if (blocks[i].kind == openmeta::ContainerBlockKind::Exif) {
            exif_blocks += 1U;
        } else if (blocks[i].kind == openmeta::ContainerBlockKind::Xmp) {
            xmp_blocks += 1U;
        } else if (blocks[i].kind == openmeta::ContainerBlockKind::Text) {
            text_blocks += 1U;
        }
    }
    EXPECT_EQ(exif_blocks, 1U);
    EXPECT_EQ(xmp_blocks, 1U);
    EXPECT_EQ(text_blocks, 1U);

    openmeta::MetaStore decoded;
    std::array<openmeta::ExifIfdRef, 16> ifds {};
    std::array<std::byte, 8192> payload {};
    std::array<uint32_t, 128> payload_parts {};
    openmeta::SimpleMetaDecodeOptions decode_options;
    const openmeta::SimpleMetaResult read = openmeta::simple_meta_read(
        edited, decoded, blocks, ifds, payload, payload_parts, decode_options);
    ASSERT_EQ(read.scan.status, openmeta::ScanStatus::Ok);
    decoded.finalize();

    const std::span<const openmeta::EntryId> exif_ids
        = decoded.find_all(exif_key_view("exififd", 0x9003U));
    ASSERT_EQ(exif_ids.size(), 1U);
    EXPECT_EQ(arena_text(decoded, decoded.entry(exif_ids[0])),
              "2025:06:07 08:09:10");

    const std::span<const openmeta::EntryId> creator_ids = decoded.find_all(
        xmp_key_view("http://ns.adobe.com/xap/1.0/", "CreatorTool"));
    ASSERT_EQ(creator_ids.size(), 1U);
    EXPECT_EQ(arena_text(decoded, decoded.entry(creator_ids[0])),
              "OpenMeta PNG Edit");
}

#if defined(OPENMETA_HAS_ZLIB) && OPENMETA_HAS_ZLIB
TEST(MetadataTransferApi, ExecutePreparedTransferPngEditRewritesIccChunk)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Png;

    static const std::array<std::byte, 6> kNewIcc = {
        std::byte { 'N' }, std::byte { 'E' }, std::byte { 'W' },
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    openmeta::PreparedTransferBlock icc;
    icc.route   = "png:chunk-iccp";
    icc.payload = build_test_png_iccp_chunk_data(
        std::span<const std::byte>(kNewIcc.data(), kNewIcc.size()));
    ASSERT_FALSE(icc.payload.empty());
    bundle.blocks.push_back(std::move(icc));

    std::vector<std::byte> png = {
        std::byte { 0x89 }, std::byte { 0x50 }, std::byte { 0x4E },
        std::byte { 0x47 }, std::byte { 0x0D }, std::byte { 0x0A },
        std::byte { 0x1A }, std::byte { 0x0A },
    };
    std::vector<std::byte> ihdr;
    append_u32be(&ihdr, 1U);
    append_u32be(&ihdr, 1U);
    ihdr.push_back(std::byte { 8U });
    ihdr.push_back(std::byte { 2U });
    ihdr.push_back(std::byte { 0U });
    ihdr.push_back(std::byte { 0U });
    ihdr.push_back(std::byte { 0U });
    append_png_chunk(&png, openmeta::fourcc('I', 'H', 'D', 'R'), ihdr);

    static const std::array<std::byte, 6> kOldIcc = {
        std::byte { 'O' }, std::byte { 'L' }, std::byte { 'D' },
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    const std::vector<std::byte> old_iccp = build_test_png_iccp_chunk_data(
        std::span<const std::byte>(kOldIcc.data(), kOldIcc.size()));
    ASSERT_FALSE(old_iccp.empty());
    append_png_chunk(&png, openmeta::fourcc('i', 'C', 'C', 'P'),
                     std::span<const std::byte>(old_iccp.data(),
                                                old_iccp.size()));
    const std::vector<std::byte> text = ascii_z("Comment");
    append_png_chunk(&png, openmeta::fourcc('t', 'E', 'X', 't'),
                     std::span<const std::byte>(text.data(), text.size()));
    append_png_chunk(&png, openmeta::fourcc('I', 'E', 'N', 'D'), {});

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult applied
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(png.data(), png.size()),
            options);
    ASSERT_EQ(applied.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(applied.edit_apply.status, openmeta::TransferStatus::Ok);

    const std::span<const std::byte> edited(applied.edited_output.data(),
                                            applied.edited_output.size());
    std::array<openmeta::ContainerBlockRef, 16> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_png(edited, blocks);
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);

    uint32_t icc_blocks  = 0U;
    uint32_t text_blocks = 0U;
    for (uint32_t i = 0U; i < scan.written; ++i) {
        if (blocks[i].kind == openmeta::ContainerBlockKind::Icc) {
            icc_blocks += 1U;
        } else if (blocks[i].kind == openmeta::ContainerBlockKind::Text) {
            text_blocks += 1U;
        }
    }
    EXPECT_EQ(icc_blocks, 1U);
    EXPECT_EQ(text_blocks, 1U);
}

TEST(MetadataTransferApi, BuildExecutedTransferPackageBatchPngIccMatchesEdit)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Png;

    static const std::array<std::byte, 6> kNewIcc = {
        std::byte { 'N' }, std::byte { 'E' }, std::byte { 'W' },
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    openmeta::PreparedTransferBlock icc;
    icc.route   = "png:chunk-iccp";
    icc.payload = build_test_png_iccp_chunk_data(
        std::span<const std::byte>(kNewIcc.data(), kNewIcc.size()));
    ASSERT_FALSE(icc.payload.empty());
    bundle.blocks.push_back(std::move(icc));

    std::vector<std::byte> metadata;
    static const std::array<std::byte, 6> kOldIcc = {
        std::byte { 'O' }, std::byte { 'L' }, std::byte { 'D' },
        std::byte { 'I' }, std::byte { 'C' }, std::byte { 'C' },
    };
    const std::vector<std::byte> old_iccp = build_test_png_iccp_chunk_data(
        std::span<const std::byte>(kOldIcc.data(), kOldIcc.size()));
    ASSERT_FALSE(old_iccp.empty());
    append_png_chunk(&metadata, openmeta::fourcc('i', 'C', 'C', 'P'),
                     std::span<const std::byte>(old_iccp.data(),
                                                old_iccp.size()));
    const std::vector<std::byte> input = build_minimal_png_file(
        std::span<const std::byte>(metadata.data(), metadata.size()));

    openmeta::ExecutePreparedTransferOptions plan_only_options;
    plan_only_options.edit_requested = true;
    const openmeta::ExecutePreparedTransferResult plan_only
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            plan_only_options);
    ASSERT_EQ(plan_only.edit_plan_status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult batch_result
        = openmeta::build_executed_transfer_package_batch(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan_only, &batch);
    ASSERT_EQ(batch_result.status, openmeta::TransferStatus::Ok);

    openmeta::ExecutePreparedTransferOptions apply_options;
    apply_options.edit_requested = true;
    apply_options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult applied
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            apply_options);
    ASSERT_EQ(applied.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch.output_size,
              static_cast<uint64_t>(applied.edited_output.size()));

    openmeta::EmitTransferResult write_result;
    const std::vector<std::byte> batch_bytes
        = materialize_transfer_package_batch(batch, &write_result);
    ASSERT_EQ(write_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch_bytes.size(), applied.edited_output.size());
    EXPECT_EQ(std::memcmp(batch_bytes.data(), applied.edited_output.data(),
                          applied.edited_output.size()),
              0);
}
#endif

TEST(MetadataTransferApi, CompileRejectsNullPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_jpeg(bundle, nullptr);
    EXPECT_EQ(result.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(result.errors, 1U);
}

TEST(MetadataTransferApi, EmitJpegGenericAppRoutesAndCom)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock app0;
    app0.route   = "jpeg:app0-jfif";
    app0.payload = { std::byte { 0xAA } };
    bundle.blocks.push_back(app0);

    openmeta::PreparedTransferBlock app15;
    app15.route   = "jpeg:app15-vendor";
    app15.payload = { std::byte { 0xBB } };
    bundle.blocks.push_back(app15);

    openmeta::PreparedTransferBlock com;
    com.route   = "jpeg:com";
    com.payload = { std::byte { 0xCC } };
    bundle.blocks.push_back(com);

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(emitter.calls.size(), 3U);
    EXPECT_EQ(emitter.calls[0].first, 0xE0U);
    EXPECT_EQ(emitter.calls[1].first, 0xEFU);
    EXPECT_EQ(emitter.calls[2].first, 0xFEU);
}

TEST(MetadataTransferApi, EmitJpegSkipsEmptyPayloadByDefault)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock empty;
    empty.route = "jpeg:app1-xmp";
    bundle.blocks.push_back(empty);

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 0U);
    EXPECT_EQ(result.skipped, 1U);
    EXPECT_TRUE(emitter.calls.empty());
}

TEST(MetadataTransferApi, EmitJpegRejectsUnknownRoute)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock bad;
    bad.route   = "jpeg:app16-oob";
    bad.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(bad);

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::UnsupportedRoute);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(result.failed_block_index, 0U);
    EXPECT_TRUE(emitter.calls.empty());
}

TEST(MetadataTransferApi, EmitJpegPropagatesBackendError)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    FakeJpegEmitter emitter;
    emitter.fail_after_calls = 1U;
    emitter.fail_status      = openmeta::TransferStatus::Malformed;

    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::BackendWriteFailed);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(result.emitted, 0U);
    EXPECT_EQ(result.failed_block_index, 0U);
}

TEST(MetadataTransferApi, EmitJpegRejectsNonJpegBundle)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::BundleTargetNotJpeg);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_TRUE(emitter.calls.empty());
}

TEST(MetadataTransferApi, EmitTiffEmitsKnownRoutesAndCommits)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock exif;
    exif.route = "tiff:ifd-exif-app1";
    exif.payload
        = { std::byte { 'E' },  std::byte { 'x' },  std::byte { 'i' },
            std::byte { 'f' },  std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 'I' },  std::byte { 'I' },  std::byte { 42 },
            std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "tiff:tag-34675-icc";
    icc.payload = { std::byte { 0x03 } };
    bundle.blocks.push_back(icc);

    FakeTiffEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_tiff(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(result.emitted, 3U);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(emitter.commit_calls, 1U);
    ASSERT_EQ(emitter.bytes_calls.size(), 3U);
    EXPECT_EQ(emitter.bytes_calls[0].first, 34665U);
    EXPECT_EQ(emitter.bytes_calls[1].first, 700U);
    EXPECT_EQ(emitter.bytes_calls[2].first, 34675U);
}

TEST(MetadataTransferApi, CompileTiffPlanKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock exif;
    exif.route = "tiff:ifd-exif-app1";
    exif.payload
        = { std::byte { 'E' },  std::byte { 'x' },  std::byte { 'i' },
            std::byte { 'f' },  std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 'I' },  std::byte { 'I' },  std::byte { 42 },
            std::byte { 0x00 }, std::byte { 0x08 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
            std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTiffEmitPlan plan;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_tiff(bundle, &plan);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    ASSERT_EQ(plan.ops.size(), 2U);
    EXPECT_EQ(plan.ops[0].block_index, 0U);
    EXPECT_EQ(plan.ops[0].tiff_tag, 34665U);
    EXPECT_EQ(plan.ops[1].block_index, 1U);
    EXPECT_EQ(plan.ops[1].tiff_tag, 700U);
}

TEST(MetadataTransferApi, EmitTiffCompiledPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock iptc;
    iptc.route   = "tiff:tag-33723-iptc";
    iptc.payload = { std::byte { 0x03 }, std::byte { 0x04 } };
    bundle.blocks.push_back(iptc);

    openmeta::PreparedTiffEmitPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_bundle_tiff(bundle, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.ops.size(), 2U);

    FakeTiffEmitter emitter;
    const openmeta::EmitTransferResult emit_result
        = openmeta::emit_prepared_bundle_tiff_compiled(bundle, plan, emitter);
    EXPECT_EQ(emit_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(emit_result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(emit_result.emitted, 2U);
    EXPECT_EQ(emit_result.errors, 0U);
    EXPECT_EQ(emitter.commit_calls, 1U);
    ASSERT_EQ(emitter.bytes_calls.size(), 2U);
    EXPECT_EQ(emitter.bytes_calls[0].first, 700U);
    EXPECT_EQ(emitter.bytes_calls[1].first, 33723U);
}

TEST(MetadataTransferApi, CompileTiffRejectsNullPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_tiff(bundle, nullptr);
    EXPECT_EQ(result.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(result.errors, 1U);
}

TEST(MetadataTransferApi, EmitTiffRejectsUnknownRoute)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock bad;
    bad.route   = "tiff:tag-1234-unknown";
    bad.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(bad);

    FakeTiffEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_tiff(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::UnsupportedRoute);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(result.failed_block_index, 0U);
    EXPECT_EQ(emitter.commit_calls, 0U);
}

TEST(MetadataTransferApi, EmitTiffCompiledRejectsPlanMismatch)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;
    bundle.contract_version += 1U;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTiffEmitPlan plan;
    plan.contract_version = openmeta::kMetadataTransferContractVersion;
    plan.ops.push_back(openmeta::PreparedTiffEmitOp { 0U, 700U });

    FakeTiffEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_tiff_compiled(bundle, plan, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::PlanMismatch);
    EXPECT_EQ(result.errors, 1U);
}

TEST(MetadataTransferApi, EmitTiffRejectsNonTiffBundle)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(xmp);

    FakeTiffEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_tiff(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidArgument);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(emitter.commit_calls, 0U);
}

TEST(MetadataTransferApi, EmitTiffPropagatesBackendWriteError)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Tiff;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "tiff:tag-700-xmp";
    xmp.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(xmp);

    FakeTiffEmitter emitter;
    emitter.fail_set_after_calls = 1U;
    emitter.fail_status          = openmeta::TransferStatus::Malformed;

    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_tiff(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::BackendWriteFailed);
    EXPECT_EQ(result.errors, 1U);
    EXPECT_EQ(result.failed_block_index, 0U);
    EXPECT_EQ(emitter.commit_calls, 0U);
}

TEST(MetadataTransferApi, EmitJxlKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jxl:icc-profile";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 }, std::byte { 0x43 },
                    std::byte { 0x50 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jxl:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                     std::byte { 0x06 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jxl:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' }, std::byte { 'x' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 },
                       std::byte { 'j' },  std::byte { 'u' },
                       std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    openmeta::PreparedTransferBlock c2pa;
    c2pa.route    = "jxl:box-c2pa";
    c2pa.box_type = { 'c', '2', 'p', 'a' };
    c2pa.payload = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                     std::byte { 0x08 }, std::byte { 'j' },  std::byte { 'u' },
                     std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(c2pa);

    FakeJxlEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jxl(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(result.emitted, 5U);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(emitter.close_calls, 1U);
    EXPECT_EQ(emitter.icc_calls, 1U);
    EXPECT_EQ(emitter.icc_bytes, 4U);
    ASSERT_EQ(emitter.calls.size(), 4U);
    EXPECT_EQ(emitter.calls[0].type,
              (std::array<char, 4> { 'E', 'x', 'i', 'f' }));
    EXPECT_EQ(emitter.calls[0].bytes, 4U);
    EXPECT_EQ(emitter.calls[0].compress, false);
    EXPECT_EQ(emitter.calls[1].type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
    EXPECT_EQ(emitter.calls[1].bytes, 2U);
    EXPECT_EQ(emitter.calls[1].compress, false);
    EXPECT_EQ(emitter.calls[2].type,
              (std::array<char, 4> { 'j', 'u', 'm', 'b' }));
    EXPECT_EQ(emitter.calls[2].bytes, 8U);
    EXPECT_EQ(emitter.calls[2].compress, false);
    EXPECT_EQ(emitter.calls[3].type,
              (std::array<char, 4> { 'c', '2', 'p', 'a' }));
    EXPECT_EQ(emitter.calls[3].bytes, 8U);
    EXPECT_EQ(emitter.calls[3].compress, false);
}

TEST(MetadataTransferApi, BuildPreparedTransferEmitPackageJxlWritesBoxes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 },
                       std::byte { 'j' },  std::byte { 'u' },
                       std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    openmeta::PreparedTransferBlock c2pa;
    c2pa.route    = "jxl:box-c2pa";
    c2pa.box_type = { 'c', '2', 'p', 'a' };
    c2pa.payload = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                     std::byte { 0x08 }, std::byte { 'j' },  std::byte { 'u' },
                     std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(c2pa);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_transfer_emit_package(bundle, &package);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(package.target_format, openmeta::TransferTargetFormat::Jxl);
    ASSERT_EQ(package.input_size, 0U);
    ASSERT_EQ(package.output_size, 32U);
    ASSERT_EQ(package.chunks.size(), 2U);
    EXPECT_EQ(package.chunks[0].kind,
              openmeta::TransferPackageChunkKind::PreparedTransferBlock);
    EXPECT_EQ(package.chunks[1].kind,
              openmeta::TransferPackageChunkKind::PreparedTransferBlock);

    BufferByteWriter writer;
    const std::vector<std::byte> empty_input;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_transfer_package(
            std::span<const std::byte>(empty_input.data(), empty_input.size()),
            bundle, package, writer);
    ASSERT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(writer.out.size(), 32U);
    uint32_t first_size = 0U;
    ASSERT_TRUE(read_test_u32be(std::span<const std::byte>(writer.out.data(),
                                                           writer.out.size()),
                                0U, &first_size));
    EXPECT_EQ(first_size, 16U);
    EXPECT_EQ(writer.out[4], std::byte { 'j' });
    EXPECT_EQ(writer.out[5], std::byte { 'u' });
    EXPECT_EQ(writer.out[6], std::byte { 'm' });
    EXPECT_EQ(writer.out[7], std::byte { 'b' });
    uint32_t second_size = 0U;
    ASSERT_TRUE(read_test_u32be(std::span<const std::byte>(writer.out.data(),
                                                           writer.out.size()),
                                16U, &second_size));
    EXPECT_EQ(second_size, 16U);
    EXPECT_EQ(writer.out[20], std::byte { 'c' });
    EXPECT_EQ(writer.out[21], std::byte { '2' });
    EXPECT_EQ(writer.out[22], std::byte { 'p' });
    EXPECT_EQ(writer.out[23], std::byte { 'a' });
}

TEST(MetadataTransferApi, BuildPreparedTransferPackageBatchJxlOwnsEmitBytes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 },
                       std::byte { 'j' },  std::byte { 'u' },
                       std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_transfer_emit_package(bundle, &package);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> empty_input;
    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult built
        = openmeta::build_prepared_transfer_package_batch(
            std::span<const std::byte>(empty_input.data(), empty_input.size()),
            bundle, package, &batch);
    ASSERT_EQ(built.status, openmeta::TransferStatus::Ok);

    bundle.blocks[0].payload[4] = std::byte { 0x99 };

    BufferByteWriter writer;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_transfer_package_batch(batch, writer);
    ASSERT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(writer.out.size(), 16U);
    EXPECT_EQ(writer.out[4], std::byte { 'j' });
    EXPECT_EQ(writer.out[5], std::byte { 'u' });
    EXPECT_EQ(writer.out[6], std::byte { 'm' });
    EXPECT_EQ(writer.out[7], std::byte { 'b' });
    EXPECT_EQ(writer.out[12], std::byte { 'j' });
}

TEST(MetadataTransferApi, BuildExecutedTransferPackageBatchJxlOwnsEmitBytes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 },
                       std::byte { 'j' },  std::byte { 'u' },
                       std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    openmeta::ExecutePreparedTransferOptions options;
    const openmeta::ExecutePreparedTransferResult exec
        = openmeta::execute_prepared_transfer(&bundle, {}, options);
    ASSERT_EQ(exec.compile.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> empty_input;
    openmeta::PreparedTransferPackageBatch batch;
    ASSERT_EQ(openmeta::build_executed_transfer_package_batch(
                  std::span<const std::byte>(empty_input.data(),
                                             empty_input.size()),
                  bundle, exec, &batch)
                  .status,
              openmeta::TransferStatus::Ok);

    bundle.blocks[0].payload[4] = std::byte { 0x99 };

    BufferByteWriter writer;
    ASSERT_EQ(
        openmeta::write_prepared_transfer_package_batch(batch, writer).status,
        openmeta::TransferStatus::Ok);
    ASSERT_EQ(writer.out.size(), 16U);
    EXPECT_EQ(writer.out[4], std::byte { 'j' });
    EXPECT_EQ(writer.out[5], std::byte { 'u' });
    EXPECT_EQ(writer.out[6], std::byte { 'm' });
    EXPECT_EQ(writer.out[7], std::byte { 'b' });
    EXPECT_EQ(writer.out[12], std::byte { 'j' });
}

TEST(MetadataTransferApi, SerializePreparedTransferPackageBatchRoundTrip)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult packaged
        = openmeta::build_prepared_transfer_emit_package(bundle, &package);
    ASSERT_EQ(packaged.status, openmeta::TransferStatus::Ok);

    const std::vector<std::byte> empty_input;
    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult built
        = openmeta::build_prepared_transfer_package_batch(
            std::span<const std::byte>(empty_input.data(), empty_input.size()),
            bundle, package, &batch);
    ASSERT_EQ(built.status, openmeta::TransferStatus::Ok);

    std::vector<std::byte> encoded;
    const openmeta::PreparedTransferPackageIoResult serialized
        = openmeta::serialize_prepared_transfer_package_batch(batch, &encoded);
    ASSERT_EQ(serialized.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(encoded.empty());

    openmeta::PreparedTransferPackageBatch decoded;
    const openmeta::PreparedTransferPackageIoResult parsed
        = openmeta::deserialize_prepared_transfer_package_batch(
            std::span<const std::byte>(encoded.data(), encoded.size()),
            &decoded);
    ASSERT_EQ(parsed.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(decoded.contract_version, batch.contract_version);
    ASSERT_EQ(decoded.target_format, batch.target_format);
    ASSERT_EQ(decoded.output_size, batch.output_size);
    ASSERT_EQ(decoded.chunks.size(), batch.chunks.size());
    ASSERT_EQ(decoded.chunks[0].bytes, batch.chunks[0].bytes);

    BufferByteWriter writer;
    const openmeta::EmitTransferResult streamed
        = openmeta::write_prepared_transfer_package_batch(decoded, writer);
    ASSERT_EQ(streamed.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(writer.out.size(), 6U);
    EXPECT_EQ(writer.out[0], std::byte { 0xFF });
    EXPECT_EQ(writer.out[1], std::byte { 0xE1 });
    EXPECT_EQ(writer.out[2], std::byte { 0x00 });
    EXPECT_EQ(writer.out[3], std::byte { 0x04 });
    EXPECT_EQ(writer.out[4], std::byte { 0x01 });
    EXPECT_EQ(writer.out[5], std::byte { 0x02 });
}

TEST(MetadataTransferApi, CollectPreparedTransferPackageViewsForJpeg)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "jpeg:app1-xmp";
    xmp.payload = { std::byte { 'x' }, std::byte { 'm' }, std::byte { 'p' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferPackagePlan package;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_emit_package(bundle, &package).status,
        openmeta::TransferStatus::Ok);

    const std::vector<std::byte> empty_input;
    openmeta::PreparedTransferPackageBatch batch;
    ASSERT_EQ(openmeta::build_prepared_transfer_package_batch(
                  std::span<const std::byte>(empty_input.data(),
                                             empty_input.size()),
                  bundle, package, &batch)
                  .status,
              openmeta::TransferStatus::Ok);

    std::vector<openmeta::PreparedTransferPackageView> views;
    const openmeta::EmitTransferResult collected
        = openmeta::collect_prepared_transfer_package_views(batch, &views);

    ASSERT_EQ(collected.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(views.size(), 2U);
    EXPECT_EQ(views[0].semantic_kind, openmeta::TransferSemanticKind::Exif);
    EXPECT_EQ(views[0].route, "jpeg:app1-exif");
    EXPECT_EQ(views[0].package_kind,
              openmeta::TransferPackageChunkKind::PreparedTransferBlock);
    ASSERT_EQ(views[0].bytes.size(), 6U);
    EXPECT_EQ(views[0].bytes[0], std::byte { 0xFF });
    EXPECT_EQ(views[0].bytes[1], std::byte { 0xE1 });

    EXPECT_EQ(views[1].semantic_kind, openmeta::TransferSemanticKind::Xmp);
    EXPECT_EQ(views[1].route, "jpeg:app1-xmp");
    EXPECT_EQ(views[1].package_kind,
              openmeta::TransferPackageChunkKind::PreparedTransferBlock);
    ASSERT_EQ(views[1].bytes.size(), 7U);
    EXPECT_EQ(views[1].bytes[0], std::byte { 0xFF });
    EXPECT_EQ(views[1].bytes[1], std::byte { 0xE1 });
}

TEST(MetadataTransferApi, CollectPreparedTransferPayloadViewsForBmff)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Heif;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "bmff:item-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "bmff:property-colr-icc";
    icc.payload = { std::byte { 'p' }, std::byte { 'r' }, std::byte { 'o' },
                    std::byte { 'f' }, std::byte { 0x20 } };
    bundle.blocks.push_back(icc);

    std::vector<openmeta::PreparedTransferPayloadView> views;
    const openmeta::EmitTransferResult collected
        = openmeta::collect_prepared_transfer_payload_views(bundle, &views);

    ASSERT_EQ(collected.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(views.size(), 2U);

    EXPECT_EQ(views[0].semantic_kind, openmeta::TransferSemanticKind::Xmp);
    EXPECT_EQ(views[0].semantic_name, "XMP");
    EXPECT_EQ(views[0].route, "bmff:item-xmp");
    EXPECT_EQ(views[0].op.kind, openmeta::TransferAdapterOpKind::BmffItem);
    EXPECT_EQ(views[0].op.bmff_item_type, openmeta::fourcc('m', 'i', 'm', 'e'));
    EXPECT_TRUE(views[0].op.bmff_mime_xmp);
    EXPECT_EQ(views[0].payload.data(), bundle.blocks[0].payload.data());

    EXPECT_EQ(views[1].semantic_kind, openmeta::TransferSemanticKind::Icc);
    EXPECT_EQ(views[1].semantic_name, "ICC");
    EXPECT_EQ(views[1].route, "bmff:property-colr-icc");
    EXPECT_EQ(views[1].op.kind, openmeta::TransferAdapterOpKind::BmffProperty);
    EXPECT_EQ(views[1].op.bmff_property_type,
              openmeta::fourcc('c', 'o', 'l', 'r'));
    EXPECT_EQ(views[1].op.bmff_property_subtype,
              openmeta::fourcc('p', 'r', 'o', 'f'));
    EXPECT_EQ(views[1].payload.data(), bundle.blocks[1].payload.data());
}

TEST(MetadataTransferApi, BuildPreparedTransferPayloadBatchOwnsBytes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x10 }, std::byte { 0x11 } };
    bundle.blocks.push_back(jumbf);

    openmeta::PreparedTransferPayloadBatch batch;
    const openmeta::EmitTransferResult built
        = openmeta::build_prepared_transfer_payload_batch(bundle, &batch);

    ASSERT_EQ(built.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch.payloads.size(), 1U);
    EXPECT_EQ(batch.contract_version, bundle.contract_version);
    EXPECT_EQ(batch.target_format, bundle.target_format);
    EXPECT_EQ(batch.payloads[0].semantic_kind,
              openmeta::TransferSemanticKind::Jumbf);
    EXPECT_EQ(batch.payloads[0].semantic_name, "JUMBF");
    EXPECT_EQ(batch.payloads[0].route, "jxl:box-jumb");
    EXPECT_EQ(batch.payloads[0].payload, bundle.blocks[0].payload);
    EXPECT_NE(batch.payloads[0].payload.data(),
              bundle.blocks[0].payload.data());

    bundle.blocks[0].payload[0] = std::byte { 0x7F };
    EXPECT_EQ(batch.payloads[0].payload[0], std::byte { 0x10 });
}

TEST(MetadataTransferApi, SerializePreparedTransferPayloadBatchRoundTrip)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Heif;

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "bmff:item-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "bmff:property-colr-icc";
    icc.payload = { std::byte { 'p' }, std::byte { 'r' }, std::byte { 'o' },
                    std::byte { 'f' }, std::byte { 0x20 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferPayloadBatch batch;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_payload_batch(bundle, &batch).status,
        openmeta::TransferStatus::Ok);

    std::vector<std::byte> encoded;
    const openmeta::PreparedTransferPayloadIoResult serialized
        = openmeta::serialize_prepared_transfer_payload_batch(batch, &encoded);
    ASSERT_EQ(serialized.status, openmeta::TransferStatus::Ok);
    ASSERT_FALSE(encoded.empty());

    openmeta::PreparedTransferPayloadBatch decoded;
    const openmeta::PreparedTransferPayloadIoResult parsed
        = openmeta::deserialize_prepared_transfer_payload_batch(
            std::span<const std::byte>(encoded.data(), encoded.size()),
            &decoded);
    ASSERT_EQ(parsed.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(decoded.contract_version, batch.contract_version);
    ASSERT_EQ(decoded.target_format, batch.target_format);
    ASSERT_EQ(decoded.emit.skip_empty_payloads, batch.emit.skip_empty_payloads);
    ASSERT_EQ(decoded.emit.stop_on_error, batch.emit.stop_on_error);
    ASSERT_EQ(decoded.payloads.size(), batch.payloads.size());
    EXPECT_EQ(decoded.payloads[0].route, batch.payloads[0].route);
    EXPECT_EQ(decoded.payloads[0].semantic_kind,
              batch.payloads[0].semantic_kind);
    EXPECT_EQ(decoded.payloads[0].semantic_name,
              batch.payloads[0].semantic_name);
    EXPECT_EQ(decoded.payloads[0].op.kind, batch.payloads[0].op.kind);
    EXPECT_EQ(decoded.payloads[0].payload, batch.payloads[0].payload);
    EXPECT_EQ(decoded.payloads[1].route, batch.payloads[1].route);
    EXPECT_EQ(decoded.payloads[1].semantic_kind,
              batch.payloads[1].semantic_kind);
    EXPECT_EQ(decoded.payloads[1].semantic_name,
              batch.payloads[1].semantic_name);
    EXPECT_EQ(decoded.payloads[1].op.kind, batch.payloads[1].op.kind);
    EXPECT_EQ(decoded.payloads[1].payload, batch.payloads[1].payload);
}

namespace {

    struct PayloadReplayState final {
        std::vector<std::string> events;
        uint32_t fail_on_payload = 0xFFFFFFFFU;
        uint32_t emitted         = 0U;
    };

    static openmeta::TransferStatus
    replay_begin_payload_batch(void* user,
                               openmeta::TransferTargetFormat target,
                               uint32_t payload_count) noexcept
    {
        if (!user) {
            return openmeta::TransferStatus::InvalidArgument;
        }
        PayloadReplayState* state = static_cast<PayloadReplayState*>(user);
        state->events.push_back("begin:"
                                + std::to_string(static_cast<uint32_t>(target))
                                + ":" + std::to_string(payload_count));
        return openmeta::TransferStatus::Ok;
    }

    static openmeta::TransferStatus replay_emit_payload(
        void* user, const openmeta::PreparedTransferPayloadView* view) noexcept
    {
        if (!user || !view) {
            return openmeta::TransferStatus::InvalidArgument;
        }
        PayloadReplayState* state = static_cast<PayloadReplayState*>(user);
        if (state->emitted == state->fail_on_payload) {
            return openmeta::TransferStatus::Unsupported;
        }
        state->events.push_back(std::string("payload:")
                                + std::string(view->route) + ":"
                                + std::string(view->semantic_name) + ":"
                                + std::to_string(view->payload.size()));
        state->emitted += 1U;
        return openmeta::TransferStatus::Ok;
    }

    static openmeta::TransferStatus
    replay_end_payload_batch(void* user,
                             openmeta::TransferTargetFormat target) noexcept
    {
        if (!user) {
            return openmeta::TransferStatus::InvalidArgument;
        }
        PayloadReplayState* state = static_cast<PayloadReplayState*>(user);
        state->events.push_back(
            "end:" + std::to_string(static_cast<uint32_t>(target)));
        return openmeta::TransferStatus::Ok;
    }

}  // namespace

TEST(MetadataTransferApi,
     DeserializePreparedTransferPayloadBatchRejectsBadMagic)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferPayloadBatch batch;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_payload_batch(bundle, &batch).status,
        openmeta::TransferStatus::Ok);

    std::vector<std::byte> encoded;
    ASSERT_EQ(openmeta::serialize_prepared_transfer_payload_batch(batch,
                                                                  &encoded)
                  .status,
              openmeta::TransferStatus::Ok);
    ASSERT_GE(encoded.size(), 8U);
    encoded[0] = std::byte { 0x00 };

    openmeta::PreparedTransferPayloadBatch decoded;
    const openmeta::PreparedTransferPayloadIoResult parsed
        = openmeta::deserialize_prepared_transfer_payload_batch(
            std::span<const std::byte>(encoded.data(), encoded.size()),
            &decoded);
    ASSERT_EQ(parsed.status, openmeta::TransferStatus::Malformed);
    ASSERT_EQ(parsed.code, openmeta::EmitTransferCode::InvalidPayload);
}

TEST(MetadataTransferApi, CollectPreparedTransferPayloadViewsFromBatch)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Webp;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "webp:chunk-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "webp:chunk-xmp";
    xmp.payload = { std::byte { 'x' }, std::byte { 'm' }, std::byte { 'p' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferPayloadBatch batch;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_payload_batch(bundle, &batch).status,
        openmeta::TransferStatus::Ok);

    std::vector<openmeta::PreparedTransferPayloadView> views;
    const openmeta::EmitTransferResult collected
        = openmeta::collect_prepared_transfer_payload_views(batch, &views);

    ASSERT_EQ(collected.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(views.size(), 2U);
    EXPECT_EQ(views[0].semantic_kind, openmeta::TransferSemanticKind::Exif);
    EXPECT_EQ(views[0].semantic_name, "Exif");
    EXPECT_EQ(views[0].route, "webp:chunk-exif");
    EXPECT_EQ(views[0].op.kind, openmeta::TransferAdapterOpKind::WebpChunk);
    EXPECT_EQ(views[0].payload.data(), batch.payloads[0].payload.data());
    EXPECT_EQ(views[1].semantic_kind, openmeta::TransferSemanticKind::Xmp);
    EXPECT_EQ(views[1].semantic_name, "XMP");
    EXPECT_EQ(views[1].route, "webp:chunk-xmp");
    EXPECT_EQ(views[1].op.kind, openmeta::TransferAdapterOpKind::WebpChunk);
    EXPECT_EQ(views[1].payload.data(), batch.payloads[1].payload.data());
}

TEST(MetadataTransferApi, ReplayPreparedTransferPayloadBatchInOrder)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "jpeg:app1-xmp";
    xmp.payload = { std::byte { 'x' }, std::byte { 'm' }, std::byte { 'p' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferPayloadBatch batch;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_payload_batch(bundle, &batch).status,
        openmeta::TransferStatus::Ok);

    PayloadReplayState state;
    openmeta::PreparedTransferPayloadReplayCallbacks callbacks;
    callbacks.begin_batch  = replay_begin_payload_batch;
    callbacks.emit_payload = replay_emit_payload;
    callbacks.end_batch    = replay_end_payload_batch;
    callbacks.user         = &state;

    const openmeta::PreparedTransferPayloadReplayResult replay
        = openmeta::replay_prepared_transfer_payload_batch(batch, callbacks);

    ASSERT_EQ(replay.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(replay.replayed, 2U);
    ASSERT_EQ(state.events.size(), 4U);
    EXPECT_EQ(state.events[0], "begin:0:2");
    EXPECT_EQ(state.events[1], "payload:jpeg:app1-exif:Exif:2");
    EXPECT_EQ(state.events[2], "payload:jpeg:app1-xmp:XMP:3");
    EXPECT_EQ(state.events[3], "end:0");
}

TEST(MetadataTransferApi, ReplayPreparedTransferPayloadBatchReportsFailure)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "jpeg:app1-xmp";
    xmp.payload = { std::byte { 'x' }, std::byte { 'm' }, std::byte { 'p' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferPayloadBatch batch;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_payload_batch(bundle, &batch).status,
        openmeta::TransferStatus::Ok);

    PayloadReplayState state;
    state.fail_on_payload = 1U;

    openmeta::PreparedTransferPayloadReplayCallbacks callbacks;
    callbacks.begin_batch  = replay_begin_payload_batch;
    callbacks.emit_payload = replay_emit_payload;
    callbacks.end_batch    = replay_end_payload_batch;
    callbacks.user         = &state;

    const openmeta::PreparedTransferPayloadReplayResult replay
        = openmeta::replay_prepared_transfer_payload_batch(batch, callbacks);

    ASSERT_EQ(replay.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(replay.code, openmeta::EmitTransferCode::BackendWriteFailed);
    EXPECT_EQ(replay.replayed, 1U);
    EXPECT_EQ(replay.failed_payload_index, 1U);
    ASSERT_EQ(state.events.size(), 2U);
    EXPECT_EQ(state.events[0], "begin:0:2");
    EXPECT_EQ(state.events[1], "payload:jpeg:app1-exif:Exif:2");
}

TEST(MetadataTransferApi, CollectPreparedTransferPackageViewsKeepsUnknownChunks)
{
    openmeta::PreparedTransferPackageBatch batch;
    batch.target_format = openmeta::TransferTargetFormat::Tiff;
    batch.output_size   = 3U;

    openmeta::PreparedTransferPackageBlob chunk;
    chunk.kind          = openmeta::TransferPackageChunkKind::SourceRange;
    chunk.source_offset = 17U;
    chunk.bytes         = { std::byte { 0x10 }, std::byte { 0x11 },
                            std::byte { 0x12 } };
    batch.chunks.push_back(std::move(chunk));

    std::vector<openmeta::PreparedTransferPackageView> views;
    const openmeta::EmitTransferResult collected
        = openmeta::collect_prepared_transfer_package_views(batch, &views);

    ASSERT_EQ(collected.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(views.size(), 1U);
    EXPECT_EQ(views[0].semantic_kind, openmeta::TransferSemanticKind::Unknown);
    EXPECT_TRUE(views[0].route.empty());
    EXPECT_EQ(views[0].package_kind,
              openmeta::TransferPackageChunkKind::SourceRange);
    EXPECT_EQ(views[0].output_offset, 0U);
    ASSERT_EQ(views[0].bytes.size(), 3U);
    EXPECT_EQ(views[0].bytes[0], std::byte { 0x10 });
    EXPECT_EQ(views[0].bytes[1], std::byte { 0x11 });
    EXPECT_EQ(views[0].bytes[2], std::byte { 0x12 });
}

TEST(MetadataTransferApi, ReplayPreparedTransferPackageBatchInOutputOrder)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "jpeg:app1-xmp";
    xmp.payload = { std::byte { 'x' }, std::byte { 'm' }, std::byte { 'p' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferPackagePlan package;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_emit_package(bundle, &package).status,
        openmeta::TransferStatus::Ok);

    const std::vector<std::byte> empty_input;
    openmeta::PreparedTransferPackageBatch batch;
    ASSERT_EQ(openmeta::build_prepared_transfer_package_batch(
                  std::span<const std::byte>(empty_input.data(),
                                             empty_input.size()),
                  bundle, package, &batch)
                  .status,
              openmeta::TransferStatus::Ok);

    PackageReplayState state;
    openmeta::PreparedTransferPackageReplayCallbacks callbacks;
    callbacks.begin_batch = replay_begin_package;
    callbacks.emit_chunk  = replay_emit_package_chunk;
    callbacks.end_batch   = replay_end_package;
    callbacks.user        = &state;

    const openmeta::PreparedTransferPackageReplayResult replay
        = openmeta::replay_prepared_transfer_package_batch(batch, callbacks);

    ASSERT_EQ(replay.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(replay.replayed, 2U);
    ASSERT_EQ(state.events.size(), 4U);
    EXPECT_EQ(state.events[0], "begin:0:2");
    EXPECT_EQ(state.events[1], "chunk:jpeg:app1-exif:0:6");
    EXPECT_EQ(state.events[2], "chunk:jpeg:app1-xmp:6:7");
    EXPECT_EQ(state.events[3], "end:0");
}

TEST(MetadataTransferApi, ReplayPreparedTransferPackageBatchReportsFailure)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "jpeg:app1-xmp";
    xmp.payload = { std::byte { 'x' }, std::byte { 'm' }, std::byte { 'p' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferPackagePlan package;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_emit_package(bundle, &package).status,
        openmeta::TransferStatus::Ok);

    const std::vector<std::byte> empty_input;
    openmeta::PreparedTransferPackageBatch batch;
    ASSERT_EQ(openmeta::build_prepared_transfer_package_batch(
                  std::span<const std::byte>(empty_input.data(),
                                             empty_input.size()),
                  bundle, package, &batch)
                  .status,
              openmeta::TransferStatus::Ok);

    PackageReplayState state;
    state.fail_on_chunk = 1U;

    openmeta::PreparedTransferPackageReplayCallbacks callbacks;
    callbacks.begin_batch = replay_begin_package;
    callbacks.emit_chunk  = replay_emit_package_chunk;
    callbacks.end_batch   = replay_end_package;
    callbacks.user        = &state;

    const openmeta::PreparedTransferPackageReplayResult replay
        = openmeta::replay_prepared_transfer_package_batch(batch, callbacks);

    ASSERT_EQ(replay.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(replay.code, openmeta::EmitTransferCode::BackendWriteFailed);
    EXPECT_EQ(replay.replayed, 1U);
    EXPECT_EQ(replay.failed_chunk_index, 1U);
    ASSERT_EQ(state.events.size(), 2U);
    EXPECT_EQ(state.events[0], "begin:0:2");
    EXPECT_EQ(state.events[1], "chunk:jpeg:app1-exif:0:6");
}

TEST(MetadataTransferApi,
     DeserializePreparedTransferPackageBatchRejectsBadMagic)
{
    openmeta::PreparedTransferPackageBatch batch;
    batch.target_format = openmeta::TransferTargetFormat::Jpeg;
    batch.output_size   = 1U;

    openmeta::PreparedTransferPackageBlob chunk;
    chunk.kind = openmeta::TransferPackageChunkKind::InlineBytes;
    chunk.bytes.push_back(std::byte { 0xAA });
    batch.chunks.push_back(std::move(chunk));

    std::vector<std::byte> encoded;
    const openmeta::PreparedTransferPackageIoResult serialized
        = openmeta::serialize_prepared_transfer_package_batch(batch, &encoded);
    ASSERT_EQ(serialized.status, openmeta::TransferStatus::Ok);
    ASSERT_GE(encoded.size(), 8U);
    encoded[0] = std::byte { 0x00 };

    openmeta::PreparedTransferPackageBatch decoded;
    const openmeta::PreparedTransferPackageIoResult parsed
        = openmeta::deserialize_prepared_transfer_package_batch(
            std::span<const std::byte>(encoded.data(), encoded.size()),
            &decoded);
    EXPECT_EQ(parsed.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(parsed.code, openmeta::EmitTransferCode::InvalidPayload);
}

TEST(MetadataTransferApi, BuildPreparedTransferEmitPackageRejectsJxlIccProfile)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jxl:icc-profile";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 },
                    std::byte { 0x43 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferPackagePlan package;
    const openmeta::EmitTransferResult result
        = openmeta::build_prepared_transfer_emit_package(bundle, &package);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Unsupported);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::UnsupportedRoute);
    EXPECT_EQ(result.failed_block_index, 0U);
    EXPECT_TRUE(result.message.find("unsupported jxl route: jxl:icc-profile")
                != std::string::npos);
}

TEST(MetadataTransferApi, CompileJxlPlanKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jxl:icc-profile";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 },
                    std::byte { 0x43 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jxl:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload  = { std::byte { 0x00 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jxl:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x01 } };
    bundle.blocks.push_back(jumbf);

    openmeta::PreparedTransferBlock c2pa;
    c2pa.route    = "jxl:box-c2pa";
    c2pa.box_type = { 'c', '2', 'p', 'a' };
    c2pa.payload  = { std::byte { 0x02 } };
    bundle.blocks.push_back(c2pa);

    openmeta::PreparedJxlEmitPlan plan;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_jxl(bundle, &plan);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    ASSERT_EQ(plan.ops.size(), 5U);
    EXPECT_EQ(plan.ops[0].kind, openmeta::PreparedJxlEmitKind::IccProfile);
    EXPECT_EQ(plan.ops[0].block_index, 0U);
    EXPECT_EQ(plan.ops[0].box_type, (std::array<char, 4> {}));
    EXPECT_FALSE(plan.ops[0].compress);
    EXPECT_EQ(plan.ops[1].kind, openmeta::PreparedJxlEmitKind::Box);
    EXPECT_EQ(plan.ops[1].block_index, 1U);
    EXPECT_EQ(plan.ops[1].box_type,
              (std::array<char, 4> { 'E', 'x', 'i', 'f' }));
    EXPECT_EQ(plan.ops[1].compress, false);
    EXPECT_EQ(plan.ops[2].kind, openmeta::PreparedJxlEmitKind::Box);
    EXPECT_EQ(plan.ops[2].block_index, 2U);
    EXPECT_EQ(plan.ops[2].box_type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
    EXPECT_EQ(plan.ops[2].compress, false);
    EXPECT_EQ(plan.ops[3].kind, openmeta::PreparedJxlEmitKind::Box);
    EXPECT_EQ(plan.ops[3].block_index, 3U);
    EXPECT_EQ(plan.ops[3].box_type,
              (std::array<char, 4> { 'j', 'u', 'm', 'b' }));
    EXPECT_EQ(plan.ops[3].compress, false);
    EXPECT_EQ(plan.ops[4].kind, openmeta::PreparedJxlEmitKind::Box);
    EXPECT_EQ(plan.ops[4].block_index, 4U);
    EXPECT_EQ(plan.ops[4].box_type,
              (std::array<char, 4> { 'c', '2', 'p', 'a' }));
    EXPECT_EQ(plan.ops[4].compress, false);
}

TEST(MetadataTransferApi, CompileJxlPlanRejectsMultipleIccProfiles)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock icc_a;
    icc_a.route   = "jxl:icc-profile";
    icc_a.payload = { std::byte { 0x49 } };
    bundle.blocks.push_back(icc_a);

    openmeta::PreparedTransferBlock icc_b;
    icc_b.route   = "jxl:icc-profile";
    icc_b.payload = { std::byte { 0x43 } };
    bundle.blocks.push_back(icc_b);

    openmeta::PreparedJxlEmitPlan plan;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_jxl(bundle, &plan);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Malformed);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::InvalidPayload);
    EXPECT_EQ(result.failed_block_index, 1U);
    EXPECT_TRUE(result.message.find("multiple jxl icc profiles")
                != std::string::npos);
}

TEST(MetadataTransferApi, EmitJxlCompiledPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jxl:icc-profile";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 }, std::byte { 0x43 },
                    std::byte { 0x50 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jxl:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload  = { std::byte { 0x00 }, std::byte { 0x00 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jxl:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' }, std::byte { 'x' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock jumbf;
    jumbf.route    = "jxl:box-jumb";
    jumbf.box_type = { 'j', 'u', 'm', 'b' };
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x00 }, std::byte { 0x08 },
                       std::byte { 'j' },  std::byte { 'u' },
                       std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    openmeta::PreparedTransferBlock c2pa;
    c2pa.route    = "jxl:box-c2pa";
    c2pa.box_type = { 'c', '2', 'p', 'a' };
    c2pa.payload = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                     std::byte { 0x08 }, std::byte { 'j' },  std::byte { 'u' },
                     std::byte { 'm' },  std::byte { 'd' } };
    bundle.blocks.push_back(c2pa);

    openmeta::PreparedJxlEmitPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_bundle_jxl(bundle, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.ops.size(), 5U);

    FakeJxlEmitter emitter;
    const openmeta::EmitTransferResult emit_result
        = openmeta::emit_prepared_bundle_jxl_compiled(bundle, plan, emitter);
    EXPECT_EQ(emit_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(emit_result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(emit_result.emitted, 5U);
    EXPECT_EQ(emitter.close_calls, 1U);
    EXPECT_EQ(emitter.icc_calls, 1U);
    EXPECT_EQ(emitter.icc_bytes, 4U);
    ASSERT_EQ(emitter.calls.size(), 4U);
    EXPECT_EQ(emitter.calls[0].type,
              (std::array<char, 4> { 'E', 'x', 'i', 'f' }));
    EXPECT_EQ(emitter.calls[1].type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
    EXPECT_EQ(emitter.calls[2].type,
              (std::array<char, 4> { 'j', 'u', 'm', 'b' }));
    EXPECT_EQ(emitter.calls[3].type,
              (std::array<char, 4> { 'c', '2', 'p', 'a' }));
}

TEST(MetadataTransferApi, EmitPreparedTransferCompiledJxlEmitterUsesBackend)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

    openmeta::PreparedTransferBlock icc;
    icc.route   = "jxl:icc-profile";
    icc.payload = { std::byte { 0x49 }, std::byte { 0x43 }, std::byte { 0x43 },
                    std::byte { 0x50 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jxl:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                     std::byte { 0x06 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jxl:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, {}, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.target_format, openmeta::TransferTargetFormat::Jxl);
    ASSERT_EQ(plan.jxl_emit.ops.size(), 3U);
    EXPECT_TRUE(plan.jpeg_emit.ops.empty());
    EXPECT_TRUE(plan.tiff_emit.ops.empty());

    FakeJxlEmitter emitter;
    const openmeta::ExecutePreparedTransferResult result
        = openmeta::emit_prepared_transfer_compiled(&bundle, plan, emitter);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.emitted, 3U);
    EXPECT_EQ(result.compiled_ops, 3U);
    EXPECT_EQ(emitter.close_calls, 1U);
    EXPECT_EQ(emitter.icc_calls, 1U);
    EXPECT_EQ(emitter.icc_bytes, 4U);
    ASSERT_EQ(emitter.calls.size(), 2U);
    ASSERT_EQ(result.jxl_box_summary.size(), 2U);
    EXPECT_EQ(result.jxl_box_summary[0].type,
              (std::array<char, 4> { 'E', 'x', 'i', 'f' }));
    EXPECT_EQ(result.jxl_box_summary[0].count, 1U);
    EXPECT_EQ(result.jxl_box_summary[0].bytes, 4U);
    EXPECT_EQ(result.jxl_box_summary[1].type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
    EXPECT_EQ(result.jxl_box_summary[1].count, 1U);
    EXPECT_EQ(result.jxl_box_summary[1].bytes, 3U);
}

TEST(MetadataTransferApi, CompileWebpPlanKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Webp;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "webp:chunk-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "webp:chunk-xmp";
    xmp.payload = { std::byte { '<' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "webp:chunk-iccp";
    icc.payload = { std::byte { 0x49 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferBlock c2pa;
    c2pa.route   = "webp:chunk-c2pa";
    c2pa.payload = { std::byte { 0x02 } };
    bundle.blocks.push_back(c2pa);

    openmeta::PreparedWebpEmitPlan plan;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_webp(bundle, &plan);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    ASSERT_EQ(plan.ops.size(), 4U);
    EXPECT_EQ(plan.ops[0].block_index, 0U);
    EXPECT_EQ(plan.ops[0].chunk_type,
              (std::array<char, 4> { 'E', 'X', 'I', 'F' }));
    EXPECT_EQ(plan.ops[1].block_index, 1U);
    EXPECT_EQ(plan.ops[1].chunk_type,
              (std::array<char, 4> { 'X', 'M', 'P', ' ' }));
    EXPECT_EQ(plan.ops[2].block_index, 2U);
    EXPECT_EQ(plan.ops[2].chunk_type,
              (std::array<char, 4> { 'I', 'C', 'C', 'P' }));
    EXPECT_EQ(plan.ops[3].block_index, 3U);
    EXPECT_EQ(plan.ops[3].chunk_type,
              (std::array<char, 4> { 'C', '2', 'P', 'A' }));
}

TEST(MetadataTransferApi, CompilePngPlanKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Png;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "png:chunk-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "png:chunk-xmp";
    xmp.payload = { std::byte { '<' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "png:chunk-iccp";
    icc.payload = { std::byte { 0x49 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedPngEmitPlan plan;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_png(bundle, &plan);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    ASSERT_EQ(plan.ops.size(), 3U);
    EXPECT_EQ(plan.ops[0].block_index, 0U);
    EXPECT_EQ(plan.ops[0].chunk_type,
              (std::array<char, 4> { 'e', 'X', 'I', 'f' }));
    EXPECT_EQ(plan.ops[1].block_index, 1U);
    EXPECT_EQ(plan.ops[1].chunk_type,
              (std::array<char, 4> { 'i', 'T', 'X', 't' }));
    EXPECT_EQ(plan.ops[2].block_index, 2U);
    EXPECT_EQ(plan.ops[2].chunk_type,
              (std::array<char, 4> { 'i', 'C', 'C', 'P' }));
}

TEST(MetadataTransferApi, CompileJp2PlanKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jp2;

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jp2:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload  = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jp2:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route    = "jp2:box-jp2h-colr";
    icc.box_type = { 'j', 'p', '2', 'h' };
    icc.payload  = { std::byte { 0x49 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedJp2EmitPlan plan;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_jp2(bundle, &plan);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    ASSERT_EQ(plan.ops.size(), 3U);
    EXPECT_EQ(plan.ops[0].block_index, 0U);
    EXPECT_EQ(plan.ops[0].box_type,
              (std::array<char, 4> { 'E', 'x', 'i', 'f' }));
    EXPECT_EQ(plan.ops[1].block_index, 1U);
    EXPECT_EQ(plan.ops[1].box_type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
    EXPECT_EQ(plan.ops[2].block_index, 2U);
    EXPECT_EQ(plan.ops[2].box_type,
              (std::array<char, 4> { 'j', 'p', '2', 'h' }));
}

TEST(MetadataTransferApi, EmitWebpCompiledPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Webp;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "webp:chunk-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "webp:chunk-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock c2pa;
    c2pa.route   = "webp:chunk-c2pa";
    c2pa.payload = { std::byte { 0x00 }, std::byte { 0x01 } };
    bundle.blocks.push_back(c2pa);

    openmeta::PreparedWebpEmitPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_bundle_webp(bundle, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.ops.size(), 3U);

    FakeWebpEmitter emitter;
    const openmeta::EmitTransferResult emit_result
        = openmeta::emit_prepared_bundle_webp_compiled(bundle, plan, emitter);
    EXPECT_EQ(emit_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(emit_result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(emit_result.emitted, 3U);
    EXPECT_EQ(emitter.close_calls, 1U);
    ASSERT_EQ(emitter.calls.size(), 3U);
    EXPECT_EQ(emitter.calls[0].type,
              (std::array<char, 4> { 'E', 'X', 'I', 'F' }));
    EXPECT_EQ(emitter.calls[1].type,
              (std::array<char, 4> { 'X', 'M', 'P', ' ' }));
    EXPECT_EQ(emitter.calls[2].type,
              (std::array<char, 4> { 'C', '2', 'P', 'A' }));
}

TEST(MetadataTransferApi, EmitPngCompiledPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Png;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "png:chunk-exif";
    exif.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "png:chunk-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedPngEmitPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_bundle_png(bundle, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.ops.size(), 2U);

    FakePngEmitter emitter;
    const openmeta::EmitTransferResult emit_result
        = openmeta::emit_prepared_bundle_png_compiled(bundle, plan, emitter);
    EXPECT_EQ(emit_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(emit_result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(emit_result.emitted, 2U);
    EXPECT_EQ(emitter.close_calls, 1U);
    ASSERT_EQ(emitter.calls.size(), 2U);
    EXPECT_EQ(emitter.calls[0].type,
              (std::array<char, 4> { 'e', 'X', 'I', 'f' }));
    EXPECT_EQ(emitter.calls[1].type,
              (std::array<char, 4> { 'i', 'T', 'X', 't' }));
}

TEST(MetadataTransferApi, EmitJp2CompiledPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jp2;

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jp2:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload  = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jp2:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedJp2EmitPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_bundle_jp2(bundle, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.ops.size(), 2U);

    FakeJp2Emitter emitter;
    const openmeta::EmitTransferResult emit_result
        = openmeta::emit_prepared_bundle_jp2_compiled(bundle, plan, emitter);
    EXPECT_EQ(emit_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(emit_result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(emit_result.emitted, 2U);
    EXPECT_EQ(emitter.close_calls, 1U);
    ASSERT_EQ(emitter.calls.size(), 2U);
    EXPECT_EQ(emitter.calls[0].type,
              (std::array<char, 4> { 'E', 'x', 'i', 'f' }));
    EXPECT_EQ(emitter.calls[1].type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
}

TEST(MetadataTransferApi, EmitPreparedTransferCompiledWebpEmitterUsesBackend)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Webp;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "webp:chunk-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "webp:chunk-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, {}, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.target_format, openmeta::TransferTargetFormat::Webp);
    ASSERT_EQ(plan.webp_emit.ops.size(), 2U);
    EXPECT_TRUE(plan.jpeg_emit.ops.empty());
    EXPECT_TRUE(plan.tiff_emit.ops.empty());
    EXPECT_TRUE(plan.jxl_emit.ops.empty());

    FakeWebpEmitter emitter;
    const openmeta::ExecutePreparedTransferResult result
        = openmeta::emit_prepared_transfer_compiled(&bundle, plan, emitter);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.emitted, 2U);
    EXPECT_EQ(result.compiled_ops, 2U);
    EXPECT_EQ(emitter.close_calls, 1U);
    ASSERT_EQ(emitter.calls.size(), 2U);
    ASSERT_EQ(result.webp_chunk_summary.size(), 2U);
    EXPECT_EQ(result.webp_chunk_summary[0].type,
              (std::array<char, 4> { 'E', 'X', 'I', 'F' }));
    EXPECT_EQ(result.webp_chunk_summary[0].count, 1U);
    EXPECT_EQ(result.webp_chunk_summary[0].bytes, 1U);
    EXPECT_EQ(result.webp_chunk_summary[1].type,
              (std::array<char, 4> { 'X', 'M', 'P', ' ' }));
    EXPECT_EQ(result.webp_chunk_summary[1].count, 1U);
    EXPECT_EQ(result.webp_chunk_summary[1].bytes, 3U);
}

TEST(MetadataTransferApi, EmitPreparedTransferCompiledPngEmitterUsesBackend)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Png;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "png:chunk-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "png:chunk-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, {}, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.target_format, openmeta::TransferTargetFormat::Png);
    ASSERT_EQ(plan.png_emit.ops.size(), 2U);
    EXPECT_TRUE(plan.jpeg_emit.ops.empty());
    EXPECT_TRUE(plan.tiff_emit.ops.empty());
    EXPECT_TRUE(plan.jxl_emit.ops.empty());
    EXPECT_TRUE(plan.webp_emit.ops.empty());

    FakePngEmitter emitter;
    const openmeta::ExecutePreparedTransferResult result
        = openmeta::emit_prepared_transfer_compiled(&bundle, plan, emitter);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.emitted, 2U);
    EXPECT_EQ(result.compiled_ops, 2U);
    EXPECT_EQ(emitter.close_calls, 1U);
    ASSERT_EQ(emitter.calls.size(), 2U);
    ASSERT_EQ(result.png_chunk_summary.size(), 2U);
    EXPECT_EQ(result.png_chunk_summary[0].type,
              (std::array<char, 4> { 'e', 'X', 'I', 'f' }));
    EXPECT_EQ(result.png_chunk_summary[0].count, 1U);
    EXPECT_EQ(result.png_chunk_summary[0].bytes, 1U);
    EXPECT_EQ(result.png_chunk_summary[1].type,
              (std::array<char, 4> { 'i', 'T', 'X', 't' }));
    EXPECT_EQ(result.png_chunk_summary[1].count, 1U);
    EXPECT_EQ(result.png_chunk_summary[1].bytes, 3U);
}

TEST(MetadataTransferApi, EmitPreparedTransferCompiledJp2EmitterUsesBackend)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jp2;

    openmeta::PreparedTransferBlock exif;
    exif.route    = "jp2:box-exif";
    exif.box_type = { 'E', 'x', 'i', 'f' };
    exif.payload  = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route    = "jp2:box-xml";
    xmp.box_type = { 'x', 'm', 'l', ' ' };
    xmp.payload  = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, {}, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.target_format, openmeta::TransferTargetFormat::Jp2);
    ASSERT_EQ(plan.jp2_emit.ops.size(), 2U);
    EXPECT_TRUE(plan.jpeg_emit.ops.empty());
    EXPECT_TRUE(plan.tiff_emit.ops.empty());
    EXPECT_TRUE(plan.jxl_emit.ops.empty());
    EXPECT_TRUE(plan.webp_emit.ops.empty());
    EXPECT_TRUE(plan.png_emit.ops.empty());

    FakeJp2Emitter emitter;
    const openmeta::ExecutePreparedTransferResult result
        = openmeta::emit_prepared_transfer_compiled(&bundle, plan, emitter);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.emitted, 2U);
    EXPECT_EQ(result.compiled_ops, 2U);
    EXPECT_EQ(emitter.close_calls, 1U);
    ASSERT_EQ(emitter.calls.size(), 2U);
    ASSERT_EQ(result.jp2_box_summary.size(), 2U);
    EXPECT_EQ(result.jp2_box_summary[0].type,
              (std::array<char, 4> { 'E', 'x', 'i', 'f' }));
    EXPECT_EQ(result.jp2_box_summary[0].count, 1U);
    EXPECT_EQ(result.jp2_box_summary[0].bytes, 1U);
    EXPECT_EQ(result.jp2_box_summary[1].type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
    EXPECT_EQ(result.jp2_box_summary[1].count, 1U);
    EXPECT_EQ(result.jp2_box_summary[1].bytes, 3U);
}

TEST(MetadataTransferApi, EmitPreparedTransferCompiledExrEmitterUsesBackend)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Exr;

    openmeta::PreparedTransferBlock make;
    make.kind    = openmeta::TransferBlockKind::ExrAttribute;
    make.route   = "exr:attribute-string";
    make.payload = make_test_exr_string_attribute_payload("Make", "Vendor");
    bundle.blocks.push_back(make);

    openmeta::PreparedTransferBlock model;
    model.kind    = openmeta::TransferBlockKind::ExrAttribute;
    model.route   = "exr:attribute-string";
    model.payload = make_test_exr_string_attribute_payload("Model", "Camera");
    bundle.blocks.push_back(model);

    openmeta::PreparedTransferExecutionPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_transfer_execution(bundle, {}, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.target_format, openmeta::TransferTargetFormat::Exr);
    ASSERT_EQ(plan.exr_emit.ops.size(), 2U);
    EXPECT_TRUE(plan.jpeg_emit.ops.empty());
    EXPECT_TRUE(plan.tiff_emit.ops.empty());
    EXPECT_TRUE(plan.jxl_emit.ops.empty());
    EXPECT_TRUE(plan.webp_emit.ops.empty());
    EXPECT_TRUE(plan.png_emit.ops.empty());
    EXPECT_TRUE(plan.jp2_emit.ops.empty());

    FakeExrEmitter emitter;
    const openmeta::ExecutePreparedTransferResult result
        = openmeta::emit_prepared_transfer_compiled(&bundle, plan, emitter);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.emitted, 2U);
    EXPECT_EQ(result.compiled_ops, 2U);
    ASSERT_EQ(emitter.calls.size(), 2U);
    EXPECT_EQ(emitter.calls[0].name, "Make");
    EXPECT_EQ(emitter.calls[0].type_name, "string");
    EXPECT_EQ(emitter.calls[0].bytes, 6U);
    EXPECT_EQ(emitter.calls[1].name, "Model");
    EXPECT_EQ(emitter.calls[1].type_name, "string");
    EXPECT_EQ(emitter.calls[1].bytes, 6U);

    ASSERT_EQ(result.exr_attribute_summary.size(), 2U);
    EXPECT_EQ(result.exr_attribute_summary[0].name, "Make");
    EXPECT_EQ(result.exr_attribute_summary[0].type_name, "string");
    EXPECT_EQ(result.exr_attribute_summary[0].count, 1U);
    EXPECT_EQ(result.exr_attribute_summary[0].bytes, 6U);
    EXPECT_EQ(result.exr_attribute_summary[1].name, "Model");
    EXPECT_EQ(result.exr_attribute_summary[1].type_name, "string");
    EXPECT_EQ(result.exr_attribute_summary[1].count, 1U);
    EXPECT_EQ(result.exr_attribute_summary[1].bytes, 6U);
}

TEST(MetadataTransferApi, PrepareBuildsBmffExifItem)
{
    openmeta::MetaStore store;
    const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});
    ASSERT_NE(block, openmeta::kInvalidBlockId);

    openmeta::Entry exif;
    exif.key   = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x0132U);
    exif.value = openmeta::make_text(store.arena(), "2024:01:02 03:04:05",
                                     openmeta::TextEncoding::Ascii);
    exif.origin.block          = block;
    exif.origin.order_in_block = 0;
    ASSERT_NE(store.add_entry(exif), openmeta::kInvalidEntryId);
    store.finalize();

    openmeta::PrepareTransferRequest request;
    request.target_format      = openmeta::TransferTargetFormat::Heif;
    request.include_xmp_app1   = false;
    request.include_icc_app2   = false;
    request.include_iptc_app13 = false;

    openmeta::PreparedTransferBundle bundle;
    const openmeta::PrepareTransferResult prepared
        = openmeta::prepare_metadata_for_target(store, request, &bundle);
    ASSERT_EQ(prepared.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(bundle.target_format, openmeta::TransferTargetFormat::Heif);
    ASSERT_EQ(bundle.blocks.size(), 1U);
    EXPECT_EQ(bundle.blocks[0].route, "bmff:item-exif");
    ASSERT_GE(bundle.blocks[0].payload.size(), 10U);
    EXPECT_EQ(bundle.blocks[0].payload[0], std::byte { 0x00 });
    EXPECT_EQ(bundle.blocks[0].payload[1], std::byte { 0x00 });
    EXPECT_EQ(bundle.blocks[0].payload[2], std::byte { 0x00 });
    EXPECT_EQ(bundle.blocks[0].payload[3], std::byte { 0x06 });
    EXPECT_EQ(std::memcmp(bundle.blocks[0].payload.data() + 4, "Exif\0\0", 6U),
              0);
}

TEST(MetadataTransferApi, CompileBmffPlanKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Heif;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "bmff:item-exif";
    exif.payload = { std::byte { 0x00 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "bmff:item-xmp";
    xmp.payload = { std::byte { '<' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock jumb;
    jumb.route   = "bmff:item-jumb";
    jumb.payload = { std::byte { 0x01 }, std::byte { 0x02 } };
    bundle.blocks.push_back(jumb);

    openmeta::PreparedTransferBlock c2pa;
    c2pa.route   = "bmff:item-c2pa";
    c2pa.payload = { std::byte { 0x03 } };
    bundle.blocks.push_back(c2pa);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "bmff:property-colr-icc";
    icc.payload = { std::byte { 'p' }, std::byte { 'r' }, std::byte { 'o' },
                    std::byte { 'f' }, std::byte { 0x01 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedBmffEmitPlan plan;
    const openmeta::EmitTransferResult result
        = openmeta::compile_prepared_bundle_bmff(bundle, &plan);

    ASSERT_EQ(result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.ops.size(), 5U);
    EXPECT_EQ(plan.ops[0].kind, openmeta::PreparedBmffEmitKind::Item);
    EXPECT_EQ(plan.ops[0].item_type, openmeta::fourcc('E', 'x', 'i', 'f'));
    EXPECT_EQ(plan.ops[1].kind, openmeta::PreparedBmffEmitKind::MimeXmp);
    EXPECT_EQ(plan.ops[1].item_type, openmeta::fourcc('m', 'i', 'm', 'e'));
    EXPECT_EQ(plan.ops[2].kind, openmeta::PreparedBmffEmitKind::Item);
    EXPECT_EQ(plan.ops[2].item_type, openmeta::fourcc('j', 'u', 'm', 'b'));
    EXPECT_EQ(plan.ops[3].kind, openmeta::PreparedBmffEmitKind::Item);
    EXPECT_EQ(plan.ops[3].item_type, openmeta::fourcc('c', '2', 'p', 'a'));
    EXPECT_EQ(plan.ops[4].kind, openmeta::PreparedBmffEmitKind::Property);
    EXPECT_EQ(plan.ops[4].property_type, openmeta::fourcc('c', 'o', 'l', 'r'));
    EXPECT_EQ(plan.ops[4].property_subtype,
              openmeta::fourcc('p', 'r', 'o', 'f'));
}

TEST(MetadataTransferApi, EmitBmffCompiledPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Heif;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "bmff:item-exif";
    exif.payload = { std::byte { 0x00 }, std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "bmff:item-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "bmff:property-colr-icc";
    icc.payload = { std::byte { 'p' }, std::byte { 'r' }, std::byte { 'o' },
                    std::byte { 'f' }, std::byte { 0x40 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedBmffEmitPlan plan;
    ASSERT_EQ(openmeta::compile_prepared_bundle_bmff(bundle, &plan).status,
              openmeta::TransferStatus::Ok);

    FakeBmffEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_bmff_compiled(bundle, plan, emitter);
    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 3U);
    EXPECT_EQ(emitter.close_calls, 1U);
    ASSERT_EQ(emitter.calls.size(), 3U);
    EXPECT_EQ(emitter.calls[0].item_type, openmeta::fourcc('E', 'x', 'i', 'f'));
    EXPECT_FALSE(emitter.calls[0].mime_xmp);
    EXPECT_EQ(emitter.calls[1].item_type, openmeta::fourcc('m', 'i', 'm', 'e'));
    EXPECT_TRUE(emitter.calls[1].mime_xmp);
    EXPECT_TRUE(emitter.calls[2].property);
    EXPECT_EQ(emitter.calls[2].property_type,
              openmeta::fourcc('c', 'o', 'l', 'r'));
    EXPECT_EQ(emitter.calls[2].property_subtype,
              openmeta::fourcc('p', 'r', 'o', 'f'));
}

TEST(MetadataTransferApi, BuildPreparedTransferAdapterViewBmff)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Cr3;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "bmff:item-exif";
    exif.payload = { std::byte { 0x00 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "bmff:item-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "bmff:property-colr-icc";
    icc.payload = { std::byte { 'p' }, std::byte { 'r' }, std::byte { 'o' },
                    std::byte { 'f' }, std::byte { 0x55 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferAdapterView view;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_adapter_view(bundle, &view).status,
        openmeta::TransferStatus::Ok);
    ASSERT_EQ(view.ops.size(), 3U);
    EXPECT_EQ(view.ops[0].kind, openmeta::TransferAdapterOpKind::BmffItem);
    EXPECT_EQ(view.ops[0].bmff_item_type, openmeta::fourcc('E', 'x', 'i', 'f'));
    EXPECT_FALSE(view.ops[0].bmff_mime_xmp);
    EXPECT_EQ(view.ops[1].kind, openmeta::TransferAdapterOpKind::BmffItem);
    EXPECT_EQ(view.ops[1].bmff_item_type, openmeta::fourcc('m', 'i', 'm', 'e'));
    EXPECT_TRUE(view.ops[1].bmff_mime_xmp);
    EXPECT_EQ(view.ops[2].kind, openmeta::TransferAdapterOpKind::BmffProperty);
    EXPECT_EQ(view.ops[2].bmff_property_type,
              openmeta::fourcc('c', 'o', 'l', 'r'));
    EXPECT_EQ(view.ops[2].bmff_property_subtype,
              openmeta::fourcc('p', 'r', 'o', 'f'));
}

TEST(MetadataTransferApi, EmitPreparedTransferAdapterViewBmff)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Heif;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "bmff:item-exif";
    exif.payload = { std::byte { 0x00 }, std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "bmff:item-xmp";
    xmp.payload = { std::byte { '<' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "bmff:property-colr-icc";
    icc.payload = { std::byte { 'p' }, std::byte { 'r' }, std::byte { 'o' },
                    std::byte { 'f' }, std::byte { 0x21 } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferAdapterView view;
    ASSERT_EQ(
        openmeta::build_prepared_transfer_adapter_view(bundle, &view).status,
        openmeta::TransferStatus::Ok);

    FakeTransferAdapterSink sink;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_transfer_adapter_view(bundle, view, sink);
    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emitted, 3U);
    ASSERT_EQ(sink.calls.size(), 3U);
    EXPECT_EQ(sink.calls[0].kind, openmeta::TransferAdapterOpKind::BmffItem);
    EXPECT_EQ(sink.calls[0].bmff_item_type,
              openmeta::fourcc('E', 'x', 'i', 'f'));
    EXPECT_FALSE(sink.calls[0].bmff_mime_xmp);
    EXPECT_EQ(sink.calls[1].bmff_item_type,
              openmeta::fourcc('m', 'i', 'm', 'e'));
    EXPECT_TRUE(sink.calls[1].bmff_mime_xmp);
    EXPECT_EQ(sink.calls[2].kind,
              openmeta::TransferAdapterOpKind::BmffProperty);
    EXPECT_EQ(sink.calls[2].bmff_property_type,
              openmeta::fourcc('c', 'o', 'l', 'r'));
    EXPECT_EQ(sink.calls[2].bmff_property_subtype,
              openmeta::fourcc('p', 'r', 'o', 'f'));
}

TEST(MetadataTransferApi, EmitPreparedTransferCompiledBmffEmitterUsesBackend)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Avif;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "bmff:item-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "bmff:item-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "bmff:property-colr-icc";
    icc.payload = { std::byte { 'p' }, std::byte { 'r' }, std::byte { 'o' },
                    std::byte { 'f' }, std::byte { 0x7F } };
    bundle.blocks.push_back(icc);

    openmeta::PreparedTransferExecutionPlan plan;
    ASSERT_EQ(
        openmeta::compile_prepared_transfer_execution(bundle, {}, &plan).status,
        openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.target_format, openmeta::TransferTargetFormat::Avif);
    ASSERT_EQ(plan.bmff_emit.ops.size(), 3U);

    FakeBmffEmitter emitter;
    const openmeta::ExecutePreparedTransferResult result
        = openmeta::emit_prepared_transfer_compiled(&bundle, plan, emitter);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.emitted, 3U);
    EXPECT_EQ(result.compiled_ops, 3U);
    EXPECT_EQ(emitter.close_calls, 1U);
    ASSERT_EQ(result.bmff_item_summary.size(), 2U);
    EXPECT_EQ(result.bmff_item_summary[0].item_type,
              openmeta::fourcc('E', 'x', 'i', 'f'));
    EXPECT_FALSE(result.bmff_item_summary[0].mime_xmp);
    EXPECT_EQ(result.bmff_item_summary[1].item_type,
              openmeta::fourcc('m', 'i', 'm', 'e'));
    EXPECT_TRUE(result.bmff_item_summary[1].mime_xmp);
    ASSERT_EQ(result.bmff_property_summary.size(), 1U);
    EXPECT_EQ(result.bmff_property_summary[0].property_type,
              openmeta::fourcc('c', 'o', 'l', 'r'));
    EXPECT_EQ(result.bmff_property_summary[0].property_subtype,
              openmeta::fourcc('p', 'r', 'o', 'f'));
}

TEST(MetadataTransferApi, ExecutePreparedTransferBmffEditAppendsMetaBox)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Heif;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "bmff:item-exif";
    exif.payload = make_test_bmff_exif_item_payload();
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "bmff:item-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' }, std::byte { '/' },
                    std::byte { '>' } };
    bundle.blocks.push_back(xmp);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "bmff:property-colr-icc";
    icc.payload = { std::byte { 'p' },  std::byte { 'r' },  std::byte { 'o' },
                    std::byte { 'f' },  std::byte { 0x01 }, std::byte { 0x02 },
                    std::byte { 0x03 }, std::byte { 0x04 } };
    bundle.blocks.push_back(icc);

    const std::vector<std::byte> input = make_minimal_bmff_file();

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult result
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);

    EXPECT_EQ(result.edit_plan_status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_GT(result.edit_output_size, static_cast<uint64_t>(input.size()));
    ASSERT_FALSE(result.edited_output.empty());

    std::array<openmeta::ContainerBlockRef, 16> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_bmff(
        std::span<const std::byte>(result.edited_output.data(),
                                   result.edited_output.size()),
        std::span<openmeta::ContainerBlockRef>(blocks.data(), blocks.size()));
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    ASSERT_EQ(scan.written, 3U);
    EXPECT_EQ(blocks[0].kind, openmeta::ContainerBlockKind::Exif);
    EXPECT_EQ(blocks[1].kind, openmeta::ContainerBlockKind::Xmp);
    EXPECT_EQ(blocks[2].kind, openmeta::ContainerBlockKind::Icc);
}

TEST(MetadataTransferApi, ExecutePreparedTransferBmffEditReplacesPriorMetaBox)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Avif;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "bmff:item-exif";
    exif.payload = make_test_bmff_exif_item_payload();
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock xmp;
    xmp.route   = "bmff:item-xmp";
    xmp.payload = { std::byte { '<' }, std::byte { 'x' }, std::byte { 'm' },
                    std::byte { 'p' } };
    bundle.blocks.push_back(xmp);

    const std::vector<std::byte> input = make_minimal_bmff_file();

    openmeta::ExecutePreparedTransferOptions options;
    options.edit_requested = true;
    options.edit_apply     = true;

    const openmeta::ExecutePreparedTransferResult first
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            options);
    ASSERT_EQ(first.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(first.edit_apply.status, openmeta::TransferStatus::Ok);

    const openmeta::ExecutePreparedTransferResult second
        = openmeta::execute_prepared_transfer(
            &bundle,
            std::span<const std::byte>(first.edited_output.data(),
                                       first.edited_output.size()),
            options);
    ASSERT_EQ(second.edit_plan_status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(second.edit_apply.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(second.edit_output_size, first.edit_output_size);

    std::array<openmeta::ContainerBlockRef, 16> blocks {};
    const openmeta::ScanResult scan = openmeta::scan_bmff(
        std::span<const std::byte>(second.edited_output.data(),
                                   second.edited_output.size()),
        std::span<openmeta::ContainerBlockRef>(blocks.data(), blocks.size()));
    ASSERT_EQ(scan.status, openmeta::ScanStatus::Ok);
    ASSERT_EQ(scan.written, 2U);
    EXPECT_EQ(blocks[0].kind, openmeta::ContainerBlockKind::Exif);
    EXPECT_EQ(blocks[1].kind, openmeta::ContainerBlockKind::Xmp);
}

TEST(MetadataTransferApi, BuildExecutedTransferPackageBatchBmffMatchesEdit)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Cr3;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "bmff:item-exif";
    exif.payload = make_test_bmff_exif_item_payload();
    bundle.blocks.push_back(exif);

    openmeta::PreparedTransferBlock icc;
    icc.route   = "bmff:property-colr-icc";
    icc.payload = { std::byte { 'p' },  std::byte { 'r' },  std::byte { 'o' },
                    std::byte { 'f' },  std::byte { 0x10 }, std::byte { 0x20 },
                    std::byte { 0x30 }, std::byte { 0x40 } };
    bundle.blocks.push_back(icc);

    const std::vector<std::byte> input = make_minimal_bmff_file();

    openmeta::ExecutePreparedTransferOptions plan_only_options;
    plan_only_options.edit_requested = true;
    const openmeta::ExecutePreparedTransferResult plan_only
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            plan_only_options);
    ASSERT_EQ(plan_only.edit_plan_status, openmeta::TransferStatus::Ok);

    openmeta::PreparedTransferPackageBatch batch;
    const openmeta::EmitTransferResult batch_result
        = openmeta::build_executed_transfer_package_batch(
            std::span<const std::byte>(input.data(), input.size()), bundle,
            plan_only, &batch);
    ASSERT_EQ(batch_result.status, openmeta::TransferStatus::Ok);

    openmeta::ExecutePreparedTransferOptions apply_options;
    apply_options.edit_requested = true;
    apply_options.edit_apply     = true;
    const openmeta::ExecutePreparedTransferResult applied
        = openmeta::execute_prepared_transfer(
            &bundle, std::span<const std::byte>(input.data(), input.size()),
            apply_options);
    ASSERT_EQ(applied.edit_apply.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(batch.output_size,
              static_cast<uint64_t>(applied.edited_output.size()));
    ASSERT_LE(batch.output_size,
              static_cast<uint64_t>(std::numeric_limits<size_t>::max()));

    std::vector<std::byte> bytes(static_cast<size_t>(batch.output_size));
    openmeta::SpanTransferByteWriter writer(
        std::span<std::byte>(bytes.data(), bytes.size()));
    const openmeta::EmitTransferResult write_result
        = openmeta::write_prepared_transfer_package_batch(batch, writer);
    ASSERT_EQ(write_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(writer.bytes_written(), applied.edited_output.size());
    EXPECT_EQ(std::memcmp(bytes.data(), applied.edited_output.data(),
                          applied.edited_output.size()),
              0);
}

TEST(MetadataTransferApi, EmitCompiledRejectsPlanMismatch)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jpeg;
    bundle.contract_version += 1U;

    openmeta::PreparedTransferBlock exif;
    exif.route   = "jpeg:app1-exif";
    exif.payload = { std::byte { 0x01 } };
    bundle.blocks.push_back(exif);

    openmeta::PreparedJpegEmitPlan plan;
    plan.contract_version = openmeta::kMetadataTransferContractVersion;
    plan.ops.push_back(openmeta::PreparedJpegEmitOp { 0U, 0xE1U });

    FakeJpegEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jpeg_compiled(bundle, plan, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::InvalidArgument);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::PlanMismatch);
    EXPECT_EQ(result.errors, 1U);
}

}  // namespace
