#include "openmeta/metadata_transfer.h"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

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
    size_t close_calls      = 0U;
    size_t fail_after_calls = 0U;
    bool fail_close         = false;
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
    options.prepare.target_format       = openmeta::TransferTargetFormat::Jxl;
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
    const std::string path            = unique_temp_path(".jpg");
    ASSERT_TRUE(
        write_bytes_file(path,
                         std::span<const std::byte>(jpeg.data(), jpeg.size())));

    openmeta::PrepareTransferFileOptions options;
    options.prepare.target_format       = openmeta::TransferTargetFormat::Jxl;
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

    ASSERT_EQ(result.prepare.status, openmeta::TransferStatus::Ok);
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
    EXPECT_TRUE(result.emit.message.find("only supported for jpeg")
                != std::string::npos);
    EXPECT_TRUE(writer.out.empty());
}

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
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x08 }, std::byte { 'j' }, std::byte { 'u' },
                       std::byte { 'm' }, std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    openmeta::PreparedTransferBlock c2pa;
    c2pa.route    = "jxl:box-c2pa";
    c2pa.box_type = { 'c', '2', 'p', 'a' };
    c2pa.payload  = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                      std::byte { 0x08 }, std::byte { 'j' }, std::byte { 'u' },
                      std::byte { 'm' }, std::byte { 'd' } };
    bundle.blocks.push_back(c2pa);

    FakeJxlEmitter emitter;
    const openmeta::EmitTransferResult result
        = openmeta::emit_prepared_bundle_jxl(bundle, emitter);

    EXPECT_EQ(result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(result.emitted, 4U);
    EXPECT_EQ(result.errors, 0U);
    EXPECT_EQ(emitter.close_calls, 1U);
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
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x08 }, std::byte { 'j' }, std::byte { 'u' },
                       std::byte { 'm' }, std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    openmeta::PreparedTransferBlock c2pa;
    c2pa.route    = "jxl:box-c2pa";
    c2pa.box_type = { 'c', '2', 'p', 'a' };
    c2pa.payload  = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                      std::byte { 0x08 }, std::byte { 'j' }, std::byte { 'u' },
                      std::byte { 'm' }, std::byte { 'd' } };
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
    ASSERT_TRUE(read_test_u32be(
        std::span<const std::byte>(writer.out.data(), writer.out.size()), 0U,
        &first_size));
    EXPECT_EQ(first_size, 16U);
    EXPECT_EQ(writer.out[4], std::byte { 'j' });
    EXPECT_EQ(writer.out[5], std::byte { 'u' });
    EXPECT_EQ(writer.out[6], std::byte { 'm' });
    EXPECT_EQ(writer.out[7], std::byte { 'b' });
    uint32_t second_size = 0U;
    ASSERT_TRUE(read_test_u32be(
        std::span<const std::byte>(writer.out.data(), writer.out.size()), 16U,
        &second_size));
    EXPECT_EQ(second_size, 16U);
    EXPECT_EQ(writer.out[20], std::byte { 'c' });
    EXPECT_EQ(writer.out[21], std::byte { '2' });
    EXPECT_EQ(writer.out[22], std::byte { 'p' });
    EXPECT_EQ(writer.out[23], std::byte { 'a' });
}

TEST(MetadataTransferApi, CompileJxlPlanKnownRoutes)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

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
    ASSERT_EQ(plan.ops.size(), 4U);
    EXPECT_EQ(plan.ops[0].block_index, 0U);
    EXPECT_EQ(plan.ops[0].box_type,
              (std::array<char, 4> { 'E', 'x', 'i', 'f' }));
    EXPECT_EQ(plan.ops[0].compress, false);
    EXPECT_EQ(plan.ops[1].block_index, 1U);
    EXPECT_EQ(plan.ops[1].box_type,
              (std::array<char, 4> { 'x', 'm', 'l', ' ' }));
    EXPECT_EQ(plan.ops[1].compress, false);
    EXPECT_EQ(plan.ops[2].block_index, 2U);
    EXPECT_EQ(plan.ops[2].box_type,
              (std::array<char, 4> { 'j', 'u', 'm', 'b' }));
    EXPECT_EQ(plan.ops[2].compress, false);
    EXPECT_EQ(plan.ops[3].block_index, 3U);
    EXPECT_EQ(plan.ops[3].box_type,
              (std::array<char, 4> { 'c', '2', 'p', 'a' }));
    EXPECT_EQ(plan.ops[3].compress, false);
}

TEST(MetadataTransferApi, EmitJxlCompiledPlan)
{
    openmeta::PreparedTransferBundle bundle;
    bundle.target_format = openmeta::TransferTargetFormat::Jxl;

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
    jumbf.payload  = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                       std::byte { 0x08 }, std::byte { 'j' }, std::byte { 'u' },
                       std::byte { 'm' }, std::byte { 'd' } };
    bundle.blocks.push_back(jumbf);

    openmeta::PreparedTransferBlock c2pa;
    c2pa.route    = "jxl:box-c2pa";
    c2pa.box_type = { 'c', '2', 'p', 'a' };
    c2pa.payload  = { std::byte { 0x00 }, std::byte { 0x00 }, std::byte { 0x00 },
                      std::byte { 0x08 }, std::byte { 'j' }, std::byte { 'u' },
                      std::byte { 'm' }, std::byte { 'd' } };
    bundle.blocks.push_back(c2pa);

    openmeta::PreparedJxlEmitPlan plan;
    const openmeta::EmitTransferResult compile_result
        = openmeta::compile_prepared_bundle_jxl(bundle, &plan);
    ASSERT_EQ(compile_result.status, openmeta::TransferStatus::Ok);
    ASSERT_EQ(plan.ops.size(), 4U);

    FakeJxlEmitter emitter;
    const openmeta::EmitTransferResult emit_result
        = openmeta::emit_prepared_bundle_jxl_compiled(bundle, plan, emitter);
    EXPECT_EQ(emit_result.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(emit_result.code, openmeta::EmitTransferCode::None);
    EXPECT_EQ(emit_result.emitted, 4U);
    EXPECT_EQ(emitter.close_calls, 1U);
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
    ASSERT_EQ(plan.jxl_emit.ops.size(), 2U);
    EXPECT_TRUE(plan.jpeg_emit.ops.empty());
    EXPECT_TRUE(plan.tiff_emit.ops.empty());

    FakeJxlEmitter emitter;
    const openmeta::ExecutePreparedTransferResult result
        = openmeta::emit_prepared_transfer_compiled(&bundle, plan, emitter);
    EXPECT_EQ(result.compile.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.status, openmeta::TransferStatus::Ok);
    EXPECT_EQ(result.emit.emitted, 2U);
    EXPECT_EQ(result.compiled_ops, 2U);
    EXPECT_EQ(emitter.close_calls, 1U);
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
