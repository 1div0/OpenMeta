#include "openmeta/metadata_transfer.h"

#include "openmeta/jumbf_decode.h"
#include "openmeta/mapped_file.h"
#include "openmeta/xmp_dump.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    struct SerializedIfdEntry final {
        uint16_t tag          = 0;
        uint16_t type         = 0;
        uint32_t count        = 0;
        uint32_t source_order = 0;
        std::vector<std::byte> value;
        bool inline_value = false;
        std::array<std::byte, 4> inline_bytes {};
        uint32_t value_offset = 0;
    };

    struct SerializedIfd final {
        bool present        = false;
        uint32_t dir_offset = 0;
        std::vector<SerializedIfdEntry> entries;
    };

    enum class ExifIfdSlot : uint8_t {
        Ifd0,
        ExifIfd,
        GpsIfd,
        InteropIfd,
        Unsupported,
    };

    struct ExifPackBuild final {
        bool produced          = false;
        uint32_t skipped_count = 0;
        std::vector<std::byte> app1_payload;
        std::vector<TimePatchSlot> time_patch_map;
    };

    struct IccPackBuild final {
        bool produced          = false;
        uint32_t skipped_count = 0;
        std::vector<PreparedTransferBlock> blocks;
    };

    struct IptcPackBuild final {
        bool produced          = false;
        uint32_t skipped_count = 0;
        std::vector<std::byte> app13_payload;
    };

    enum class C2paPayloadClass : uint8_t {
        NotC2pa,
        DraftUnsignedInvalidation,
        ContentBound,
    };

    struct TransferC2paSemanticSummary final {
        bool available                                         = false;
        uint64_t manifest_present                              = 0;
        uint64_t manifest_count                                = 0;
        uint64_t claim_generator_present                       = 0;
        uint64_t assertion_count                               = 0;
        uint64_t claim_present                                 = 0;
        uint64_t signature_present                             = 0;
        uint64_t claim_count                                   = 0;
        uint64_t signature_count                               = 0;
        uint64_t signature_linked                              = 0;
        uint64_t signature_orphan                              = 0;
        uint64_t explicit_reference_signature_count            = 0;
        uint64_t explicit_reference_unresolved_signature_count = 0;
        uint64_t explicit_reference_ambiguous_signature_count  = 0;
    };

    struct TransferPrepareCapabilities final {
        bool jpeg_jumbf_passthrough                = false;
        bool jxl_jumbf_passthrough                 = false;
        bool bmff_jumbf_passthrough                = false;
        C2paPayloadClass source_c2pa_payload_class = C2paPayloadClass::NotC2pa;
    };

    struct SourceJumbfAppendResult final {
        uint32_t emitted_blocks = 0;
        uint32_t emitted_jumbf  = 0;
        uint32_t emitted_c2pa   = 0;
        uint32_t errors         = 0;
        std::string message;
    };

    enum class ProjectedCborNodeKind : uint8_t {
        Unknown,
        Leaf,
        Map,
        Array,
    };

    struct ProjectedCborChild final {
        uint32_t node_index  = 0U;
        uint32_t array_index = 0U;
        bool array_child     = false;
        std::string map_key;
    };

    struct ProjectedCborNode final {
        ProjectedCborNodeKind kind = ProjectedCborNodeKind::Unknown;
        const Entry* leaf          = nullptr;
        bool has_tag               = false;
        uint64_t tag               = 0U;
        std::vector<ProjectedCborChild> children;
    };

    struct ProjectedCborTree final {
        std::string root_prefix;
        std::vector<ProjectedCborNode> nodes;
    };

    struct ProjectedJumbfPayload final {
        std::string root_prefix;
        std::vector<std::byte> logical_payload;
    };

    struct IfdEntryLess final {
        bool operator()(const SerializedIfdEntry& a,
                        const SerializedIfdEntry& b) const noexcept
        {
            if (a.tag != b.tag) {
                return a.tag < b.tag;
            }
            return a.source_order < b.source_order;
        }
    };

    static bool starts_with(std::string_view s,
                            std::string_view prefix) noexcept
    {
        return s.size() >= prefix.size()
               && s.substr(0, prefix.size()) == prefix;
    }

    static std::string_view arena_string(const ByteArena& arena,
                                         ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool meta_scalar_to_u64(const MetaValue& value,
                                   uint64_t* out) noexcept;

    static bool path_separator(char c) noexcept
    {
        return c == '.' || c == '[' || c == ']' || c == '@';
    }

    static bool contains_path_segment(std::string_view text,
                                      std::string_view segment) noexcept
    {
        if (text.empty() || segment.empty()) {
            return false;
        }
        size_t pos = 0U;
        while (true) {
            pos = text.find(segment, pos);
            if (pos == std::string_view::npos) {
                return false;
            }
            const size_t end    = pos + segment.size();
            const bool left_ok  = (pos == 0U) || path_separator(text[pos - 1U]);
            const bool right_ok = (end >= text.size())
                                  || path_separator(text[end]);
            if (left_ok && right_ok) {
                return true;
            }
            pos += 1U;
        }
    }

    static bool parse_u32_decimal(std::string_view s, uint32_t* out) noexcept
    {
        if (!out || s.empty()) {
            return false;
        }
        uint32_t v = 0;
        for (char c : s) {
            if (c < '0' || c > '9') {
                return false;
            }
            const uint32_t d = static_cast<uint32_t>(c - '0');
            if (v > (UINT32_MAX - d) / 10U) {
                return false;
            }
            v = v * 10U + d;
        }
        *out = v;
        return true;
    }

    static bool decimal_text(std::string_view s) noexcept
    {
        if (s.empty()) {
            return false;
        }
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] < '0' || s[i] > '9') {
                return false;
            }
        }
        return true;
    }

    static bool
    find_jumbf_cbor_root_prefix(std::string_view key,
                                std::string_view* out_root,
                                std::string_view* out_suffix) noexcept
    {
        if (!out_root || !out_suffix) {
            return false;
        }
        size_t pos = 0U;
        while (true) {
            pos = key.find(".cbor", pos);
            if (pos == std::string_view::npos) {
                return false;
            }
            const size_t end = pos + 5U;
            if (end == key.size() || key[end] == '.' || key[end] == '['
                || key[end] == '@') {
                *out_root   = key.substr(0U, end);
                *out_suffix = key.substr(end);
                return true;
            }
            pos = end;
        }
    }

    static bool append_unique_string(std::vector<std::string>* out,
                                     std::string_view value) noexcept
    {
        if (!out || value.empty()) {
            return false;
        }
        for (size_t i = 0; i < out->size(); ++i) {
            if ((*out)[i] == value) {
                return false;
            }
        }
        out->emplace_back(value);
        return true;
    }

    static const char* time_patch_field_name(TimePatchField f) noexcept
    {
        switch (f) {
        case TimePatchField::DateTime: return "DateTime";
        case TimePatchField::DateTimeOriginal: return "DateTimeOriginal";
        case TimePatchField::DateTimeDigitized: return "DateTimeDigitized";
        case TimePatchField::SubSecTime: return "SubSecTime";
        case TimePatchField::SubSecTimeOriginal: return "SubSecTimeOriginal";
        case TimePatchField::SubSecTimeDigitized: return "SubSecTimeDigitized";
        case TimePatchField::OffsetTime: return "OffsetTime";
        case TimePatchField::OffsetTimeOriginal: return "OffsetTimeOriginal";
        case TimePatchField::OffsetTimeDigitized: return "OffsetTimeDigitized";
        case TimePatchField::GpsDateStamp: return "GpsDateStamp";
        case TimePatchField::GpsTimeStamp: return "GpsTimeStamp";
        }
        return "Unknown";
    }

    static bool read_u32be(std::span<const std::byte> bytes, size_t off,
                           uint32_t* out) noexcept
    {
        if (!out || off > bytes.size() || bytes.size() - off < 4U) {
            return false;
        }
        *out
            = (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 0U]))
               << 24U)
              | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 1U]))
                 << 16U)
              | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 2U]))
                 << 8U)
              | (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[off + 3U]))
                 << 0U);
        return true;
    }

    static bool read_u64be(std::span<const std::byte> bytes, size_t off,
                           uint64_t* out) noexcept
    {
        if (!out || off > bytes.size() || bytes.size() - off < 8U) {
            return false;
        }
        *out
            = (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 0U]))
               << 56U)
              | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 1U]))
                 << 48U)
              | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 2U]))
                 << 40U)
              | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 3U]))
                 << 32U)
              | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 4U]))
                 << 24U)
              | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 5U]))
                 << 16U)
              | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 6U]))
                 << 8U)
              | (static_cast<uint64_t>(std::to_integer<uint8_t>(bytes[off + 7U]))
                 << 0U);
        return true;
    }

    static bool is_openmeta_draft_c2pa_invalidation_payload(
        std::span<const std::byte> bytes) noexcept;

    static bool
    is_c2pa_jumbf_payload(std::span<const std::byte> logical_payload) noexcept;

    static bool marker_from_jpeg_route(std::string_view route,
                                       uint8_t* out_marker) noexcept
    {
        if (!out_marker) {
            return false;
        }

        if (route == "jpeg:com") {
            *out_marker = 0xFEU;
            return true;
        }
        if (route == "jpeg:app1-exif" || route == "jpeg:app1-xmp") {
            *out_marker = 0xE1U;
            return true;
        }
        if (route == "jpeg:app2-icc") {
            *out_marker = 0xE2U;
            return true;
        }
        if (route == "jpeg:app13-iptc") {
            *out_marker = 0xEDU;
            return true;
        }
        if (!starts_with(route, "jpeg:app")) {
            return false;
        }

        std::string_view rest = route.substr(8);
        if (rest.empty()) {
            return false;
        }

        uint32_t split = 0;
        while (split < rest.size()) {
            const char c = rest[split];
            if (c < '0' || c > '9') {
                break;
            }
            split += 1U;
        }
        if (split == 0U) {
            return false;
        }
        if (split < rest.size() && rest[split] != '-') {
            return false;
        }

        uint32_t app_n = 0;
        if (!parse_u32_decimal(rest.substr(0, split), &app_n) || app_n > 15U) {
            return false;
        }
        *out_marker = static_cast<uint8_t>(0xE0U + app_n);
        return true;
    }

    static bool tiff_tag_from_route(std::string_view route,
                                    uint16_t* out_tag) noexcept
    {
        if (!out_tag) {
            return false;
        }
        if (route == "tiff:tag-700-xmp") {
            *out_tag = 700U;
            return true;
        }
        if (route == "tiff:ifd-exif-app1") {
            *out_tag = 34665U;  // backend interprets payload and sets ExifIFD
            return true;
        }
        if (route == "tiff:tag-34675-icc") {
            *out_tag = 34675U;
            return true;
        }
        if (route == "tiff:tag-33723-iptc") {
            *out_tag = 33723U;
            return true;
        }
        return false;
    }

    static bool jxl_box_from_route(std::string_view route,
                                   std::array<char, 4>* out_type,
                                   bool* out_compress) noexcept
    {
        if (!out_type || !out_compress) {
            return false;
        }
        *out_compress = false;
        if (route == "jxl:box-exif") {
            *out_type = { 'E', 'x', 'i', 'f' };
            return true;
        }
        if (route == "jxl:box-xml") {
            *out_type = { 'x', 'm', 'l', ' ' };
            return true;
        }
        if (route == "jxl:box-jumb") {
            *out_type = { 'j', 'u', 'm', 'b' };
            return true;
        }
        if (route == "jxl:box-c2pa") {
            *out_type = { 'c', '2', 'p', 'a' };
            return true;
        }
        return false;
    }

    static bool jxl_route_is_icc_profile(std::string_view route) noexcept
    {
        return route == "jxl:icc-profile";
    }

    static bool webp_chunk_from_route(std::string_view route,
                                      std::array<char, 4>* out_type) noexcept
    {
        if (!out_type) {
            return false;
        }
        if (route == "webp:chunk-exif") {
            *out_type = { 'E', 'X', 'I', 'F' };
            return true;
        }
        if (route == "webp:chunk-xmp") {
            *out_type = { 'X', 'M', 'P', ' ' };
            return true;
        }
        if (route == "webp:chunk-iccp") {
            *out_type = { 'I', 'C', 'C', 'P' };
            return true;
        }
        if (route == "webp:chunk-c2pa") {
            *out_type = { 'C', '2', 'P', 'A' };
            return true;
        }
        return false;
    }

    static bool transfer_target_is_bmff(TransferTargetFormat target) noexcept
    {
        return target == TransferTargetFormat::Heif
               || target == TransferTargetFormat::Avif
               || target == TransferTargetFormat::Cr3;
    }

    static bool bmff_item_from_route(std::string_view route,
                                     uint32_t* out_item_type,
                                     bool* out_mime_xmp) noexcept
    {
        if (!out_item_type || !out_mime_xmp) {
            return false;
        }
        *out_mime_xmp = false;
        if (route == "bmff:item-exif") {
            *out_item_type = fourcc('E', 'x', 'i', 'f');
            return true;
        }
        if (route == "bmff:item-xmp") {
            *out_item_type = fourcc('m', 'i', 'm', 'e');
            *out_mime_xmp  = true;
            return true;
        }
        if (route == "bmff:item-jumb") {
            *out_item_type = fourcc('j', 'u', 'm', 'b');
            return true;
        }
        if (route == "bmff:item-c2pa") {
            *out_item_type = fourcc('c', '2', 'p', 'a');
            return true;
        }
        return false;
    }

    struct PlannedJpegSegment final {
        uint8_t marker = 0;
        std::string route;
        std::span<const std::byte> payload;
    };

    struct ExistingJpegSegment final {
        uint8_t marker     = 0;
        size_t marker_off  = 0;
        size_t payload_off = 0;
        size_t payload_len = 0;
        std::string route;
        bool route_known = false;
    };

    struct JpegScanResult final {
        TransferStatus status = TransferStatus::Ok;
        size_t scan_end       = 0;
        std::vector<ExistingJpegSegment> leading_segments;
        std::string message;
    };

    static uint16_t read_u16be(std::span<const std::byte> data,
                               size_t off) noexcept
    {
        if (off + 2U > data.size()) {
            return 0U;
        }
        const uint16_t hi = static_cast<uint16_t>(
            std::to_integer<uint8_t>(data[off + 0U]));
        const uint16_t lo = static_cast<uint16_t>(
            std::to_integer<uint8_t>(data[off + 1U]));
        return static_cast<uint16_t>((hi << 8U) | lo);
    }

    static bool has_prefix(std::span<const std::byte> data, const char* prefix,
                           size_t prefix_len) noexcept
    {
        if (!prefix) {
            return false;
        }
        if (data.size() < prefix_len) {
            return false;
        }
        for (size_t i = 0; i < prefix_len; ++i) {
            if (data[i]
                != static_cast<std::byte>(static_cast<uint8_t>(prefix[i]))) {
                return false;
            }
        }
        return true;
    }

    static std::string
    detect_existing_jpeg_route(uint8_t marker,
                               std::span<const std::byte> payload,
                               bool* out_known) noexcept
    {
        bool known = false;
        std::string route;
        if (marker == 0xE1U && has_prefix(payload, "Exif\0\0", 6U)) {
            known = true;
            route = "jpeg:app1-exif";
        } else if (marker == 0xE1U
                   && has_prefix(payload, "http://ns.adobe.com/xap/1.0/\0",
                                 29U)) {
            known = true;
            route = "jpeg:app1-xmp";
        } else if (marker == 0xEBU && has_prefix(payload, "JP\0\0", 4U)
                   && payload.size() >= 8U) {
            known = true;
            route = is_c2pa_jumbf_payload(payload.subspan(8U))
                        ? "jpeg:app11-c2pa"
                        : "jpeg:app11-jumbf";
        } else if (marker == 0xE2U
                   && has_prefix(payload, "ICC_PROFILE\0", 12U)) {
            known = true;
            route = "jpeg:app2-icc";
        } else if (marker == 0xEDU
                   && has_prefix(payload, "Photoshop 3.0\0", 14U)) {
            known = true;
            route = "jpeg:app13-iptc";
        } else if (marker >= 0xE0U && marker <= 0xEFU) {
            known = true;
            route = "jpeg:app"
                    + std::to_string(static_cast<unsigned>(marker - 0xE0U));
        } else if (marker == 0xFEU) {
            known = true;
            route = "jpeg:com";
        }
        if (out_known) {
            *out_known = known;
        }
        return route;
    }

    static JpegScanResult
    scan_leading_jpeg_segments(std::span<const std::byte> input) noexcept
    {
        JpegScanResult out;
        if (input.size() < 2U) {
            out.status  = TransferStatus::Malformed;
            out.message = "jpeg input is too small";
            return out;
        }
        if (input[0] != std::byte { 0xFF } || input[1] != std::byte { 0xD8 }) {
            out.status  = TransferStatus::Malformed;
            out.message = "jpeg input does not start with SOI";
            return out;
        }

        size_t pos = 2U;
        while (pos + 1U < input.size()) {
            if (input[pos] != std::byte { 0xFF }) {
                break;
            }
            const uint8_t marker = std::to_integer<uint8_t>(input[pos + 1U]);
            if (marker == 0xDAU || marker == 0xD9U) {
                break;
            }
            const bool is_app = (marker >= 0xE0U && marker <= 0xEFU);
            const bool is_com = (marker == 0xFEU);
            if (!is_app && !is_com) {
                break;
            }

            if (pos + 4U > input.size()) {
                out.status  = TransferStatus::Malformed;
                out.message = "jpeg marker header truncated";
                return out;
            }
            const uint16_t seg_len = read_u16be(input, pos + 2U);
            if (seg_len < 2U) {
                out.status  = TransferStatus::Malformed;
                out.message = "jpeg marker length is invalid";
                return out;
            }
            const size_t seg_total = 2U + static_cast<size_t>(seg_len);
            if (pos + seg_total > input.size()) {
                out.status  = TransferStatus::Malformed;
                out.message = "jpeg marker payload truncated";
                return out;
            }

            ExistingJpegSegment seg;
            seg.marker      = marker;
            seg.marker_off  = pos;
            seg.payload_off = pos + 4U;
            seg.payload_len = static_cast<size_t>(seg_len) - 2U;

            const std::span<const std::byte> payload
                = input.subspan(seg.payload_off, seg.payload_len);
            seg.route = detect_existing_jpeg_route(marker, payload,
                                                   &seg.route_known);
            out.leading_segments.push_back(std::move(seg));

            pos += seg_total;
        }

        out.scan_end = pos;
        out.status   = TransferStatus::Ok;
        return out;
    }

    static EmitTransferResult
    collect_planned_jpeg_segments(const PreparedTransferBundle& bundle,
                                  bool skip_empty_payloads,
                                  std::vector<PlannedJpegSegment>* out)
    {
        EmitTransferResult status;
        if (!out) {
            status.status  = TransferStatus::InvalidArgument;
            status.code    = EmitTransferCode::InvalidArgument;
            status.errors  = 1U;
            status.message = "planned segment output is null";
            return status;
        }
        out->clear();

        EmitTransferOptions opts;
        opts.skip_empty_payloads = skip_empty_payloads;
        opts.stop_on_error       = true;
        PreparedJpegEmitPlan compiled;
        status = compile_prepared_bundle_jpeg(bundle, &compiled, opts);
        if (status.status != TransferStatus::Ok) {
            return status;
        }

        out->reserve(compiled.ops.size());
        for (size_t i = 0; i < compiled.ops.size(); ++i) {
            const PreparedJpegEmitOp& op = compiled.ops[i];
            if (op.block_index >= bundle.blocks.size()) {
                status.status  = TransferStatus::InternalError;
                status.code    = EmitTransferCode::PlanMismatch;
                status.errors  = 1U;
                status.message = "compiled op block index out of range";
                return status;
            }
            const PreparedTransferBlock& block = bundle.blocks[op.block_index];
            PlannedJpegSegment seg;
            seg.marker  = op.marker_code;
            seg.route   = block.route;
            seg.payload = std::span<const std::byte>(block.payload.data(),
                                                     block.payload.size());
            out->push_back(std::move(seg));
        }
        return status;
    }

    static size_t
    route_occurrence_before(const std::vector<PlannedJpegSegment>& desired,
                            size_t idx, std::string_view route) noexcept
    {
        size_t occ = 0;
        for (size_t i = 0; i < idx; ++i) {
            if (desired[i].route == route) {
                occ += 1U;
            }
        }
        return occ;
    }

    static bool find_existing_by_route_occurrence(
        const std::vector<ExistingJpegSegment>& existing,
        std::string_view route, size_t occurrence, size_t* out_index) noexcept
    {
        if (!out_index) {
            return false;
        }
        size_t seen = 0;
        for (size_t i = 0; i < existing.size(); ++i) {
            if (existing[i].route != route) {
                continue;
            }
            if (seen == occurrence) {
                *out_index = i;
                return true;
            }
            seen += 1U;
        }
        return false;
    }

    static bool
    route_in_desired(std::string_view route,
                     const std::vector<PlannedJpegSegment>& desired) noexcept
    {
        for (size_t i = 0; i < desired.size(); ++i) {
            if (desired[i].route == route) {
                return true;
            }
        }
        return false;
    }

    static const PreparedTransferPolicyDecision*
    find_policy_decision(const PreparedTransferBundle& bundle,
                         TransferPolicySubject subject) noexcept
    {
        for (size_t i = 0; i < bundle.policy_decisions.size(); ++i) {
            if (bundle.policy_decisions[i].subject == subject) {
                return &bundle.policy_decisions[i];
            }
        }
        return nullptr;
    }

    static bool should_strip_existing_jpeg_segment(
        const PreparedTransferBundle& bundle,
        const ExistingJpegSegment& existing,
        const std::vector<PlannedJpegSegment>& desired) noexcept
    {
        if (!existing.route_known) {
            return false;
        }
        if (route_in_desired(existing.route, desired)) {
            return true;
        }
        if (existing.route == "jpeg:app11-jumbf") {
            const PreparedTransferPolicyDecision* decision
                = find_policy_decision(bundle, TransferPolicySubject::Jumbf);
            return decision && decision->effective == TransferPolicyAction::Drop
                   && decision->matched_entries > 0U;
        }
        if (existing.route == "jpeg:app11-c2pa") {
            if (!desired.empty()) {
                return true;
            }
            const PreparedTransferPolicyDecision* decision
                = find_policy_decision(bundle, TransferPolicySubject::C2pa);
            return decision && decision->effective == TransferPolicyAction::Drop
                   && decision->matched_entries > 0U;
        }
        return false;
    }

    static void append_c2pa_rewrite_source_chunk(
        PreparedTransferC2paRewriteRequirements* out, uint64_t offset,
        uint64_t size) noexcept
    {
        if (!out || size == 0U) {
            return;
        }
        if (!out->content_binding_chunks.empty()) {
            PreparedTransferC2paRewriteChunk& last
                = out->content_binding_chunks.back();
            if (last.kind == TransferC2paRewriteChunkKind::SourceRange
                && last.source_offset + last.size == offset) {
                last.size += size;
                out->content_binding_bytes += size;
                return;
            }
        }
        PreparedTransferC2paRewriteChunk chunk;
        chunk.kind          = TransferC2paRewriteChunkKind::SourceRange;
        chunk.source_offset = offset;
        chunk.size          = size;
        out->content_binding_chunks.push_back(std::move(chunk));
        out->content_binding_bytes += size;
    }

    static void append_c2pa_rewrite_prepared_chunk(
        PreparedTransferC2paRewriteRequirements* out, uint32_t block_index,
        uint8_t marker_code, uint64_t size) noexcept
    {
        if (!out || size == 0U) {
            return;
        }
        PreparedTransferC2paRewriteChunk chunk;
        chunk.kind        = TransferC2paRewriteChunkKind::PreparedJpegSegment;
        chunk.block_index = block_index;
        chunk.jpeg_marker_code = marker_code;
        chunk.size             = size;
        out->content_binding_chunks.push_back(std::move(chunk));
        out->content_binding_bytes += size;
    }

    static bool build_jpeg_c2pa_rewrite_chunks(
        std::span<const std::byte> input_jpeg,
        const PreparedTransferBundle& bundle,
        PreparedTransferC2paRewriteRequirements* rewrite,
        std::string* err) noexcept
    {
        if (!rewrite) {
            if (err) {
                *err = "c2pa rewrite output is null";
            }
            return false;
        }
        rewrite->content_binding_bytes = 0;
        rewrite->content_binding_chunks.clear();

        if (rewrite->state
            != TransferC2paRewriteState::SigningMaterialRequired) {
            return true;
        }
        if (bundle.target_format != TransferTargetFormat::Jpeg) {
            if (err) {
                *err = "c2pa rewrite binding chunks currently require jpeg";
            }
            return false;
        }
        if (input_jpeg.size() < 2U) {
            if (err) {
                *err = "jpeg input is too small";
            }
            return false;
        }

        EmitTransferOptions opts;
        opts.skip_empty_payloads = true;
        opts.stop_on_error       = true;
        PreparedJpegEmitPlan compiled;
        const EmitTransferResult compiled_status
            = compile_prepared_bundle_jpeg(bundle, &compiled, opts);
        if (compiled_status.status != TransferStatus::Ok) {
            if (err) {
                *err = compiled_status.message.empty()
                           ? "failed to compile jpeg rewrite binding chunks"
                           : compiled_status.message;
            }
            return false;
        }

        std::vector<PlannedJpegSegment> desired;
        desired.reserve(compiled.ops.size());
        std::vector<PreparedJpegEmitOp> desired_ops;
        desired_ops.reserve(compiled.ops.size());
        for (size_t i = 0; i < compiled.ops.size(); ++i) {
            const PreparedJpegEmitOp& op = compiled.ops[i];
            if (op.block_index >= bundle.blocks.size()) {
                if (err) {
                    *err = "compiled op block index out of range";
                }
                return false;
            }
            const PreparedTransferBlock& block = bundle.blocks[op.block_index];
            if (block.route == "jpeg:app11-c2pa") {
                continue;
            }
            PlannedJpegSegment seg;
            seg.marker  = op.marker_code;
            seg.route   = block.route;
            seg.payload = std::span<const std::byte>(block.payload.data(),
                                                     block.payload.size());
            desired.push_back(std::move(seg));
            desired_ops.push_back(op);
        }

        const JpegScanResult scan = scan_leading_jpeg_segments(input_jpeg);
        if (scan.status != TransferStatus::Ok) {
            if (err) {
                *err = scan.message.empty() ? "jpeg scan failed" : scan.message;
            }
            return false;
        }

        append_c2pa_rewrite_source_chunk(rewrite, 0U, 2U);

        bool inserted = false;
        for (size_t i = 0; i < scan.leading_segments.size(); ++i) {
            const ExistingJpegSegment& e = scan.leading_segments[i];
            const bool replaced = should_strip_existing_jpeg_segment(bundle, e,
                                                                     desired);
            if (replaced) {
                if (!inserted) {
                    for (size_t oi = 0; oi < desired_ops.size(); ++oi) {
                        const PreparedJpegEmitOp& op = desired_ops[oi];
                        const PreparedTransferBlock& block
                            = bundle.blocks[op.block_index];
                        append_c2pa_rewrite_prepared_chunk(
                            rewrite, op.block_index, op.marker_code,
                            static_cast<uint64_t>(4U + block.payload.size()));
                    }
                    inserted = true;
                }
                continue;
            }

            const size_t seg_end = e.payload_off + e.payload_len;
            if (seg_end > input_jpeg.size() || e.marker_off > seg_end) {
                if (err) {
                    *err = "existing jpeg segment range is invalid";
                }
                return false;
            }
            append_c2pa_rewrite_source_chunk(
                rewrite, static_cast<uint64_t>(e.marker_off),
                static_cast<uint64_t>(seg_end - e.marker_off));
        }

        if (!inserted) {
            for (size_t oi = 0; oi < desired_ops.size(); ++oi) {
                const PreparedJpegEmitOp& op = desired_ops[oi];
                const PreparedTransferBlock& block
                    = bundle.blocks[op.block_index];
                append_c2pa_rewrite_prepared_chunk(
                    rewrite, op.block_index, op.marker_code,
                    static_cast<uint64_t>(4U + block.payload.size()));
            }
        }

        if (scan.scan_end > input_jpeg.size()) {
            if (err) {
                *err = "jpeg rewrite scan end is out of range";
            }
            return false;
        }
        append_c2pa_rewrite_source_chunk(
            rewrite, static_cast<uint64_t>(scan.scan_end),
            static_cast<uint64_t>(input_jpeg.size() - scan.scan_end));
        return true;
    }

    static void append_jpeg_segment(std::vector<std::byte>* out, uint8_t marker,
                                    std::span<const std::byte> payload) noexcept
    {
        if (!out) {
            return;
        }
        const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
        out->push_back(std::byte { 0xFF });
        out->push_back(static_cast<std::byte>(marker));
        out->push_back(static_cast<std::byte>((seg_len >> 8U) & 0xFFU));
        out->push_back(static_cast<std::byte>((seg_len >> 0U) & 0xFFU));
        if (!payload.empty()) {
            out->insert(out->end(), payload.begin(), payload.end());
        }
    }

    struct PlannedJpegReplacement final {
        size_t payload_off = 0;
        size_t payload_len = 0;
        std::span<const std::byte> payload;
        std::string route;
    };

    struct PlannedJpegReplacementLess final {
        bool operator()(const PlannedJpegReplacement& a,
                        const PlannedJpegReplacement& b) const noexcept
        {
            return a.payload_off < b.payload_off;
        }
    };

    static void append_package_source_chunk(PreparedTransferPackagePlan* plan,
                                            uint64_t source_offset,
                                            uint64_t size) noexcept;

    static uint64_t package_plan_next_output_offset(
        const PreparedTransferPackagePlan& plan) noexcept;

    static void
    append_package_inline_chunk(PreparedTransferPackagePlan* plan,
                                std::span<const std::byte> bytes) noexcept;

    static void
    append_package_prepared_block_chunk(PreparedTransferPackagePlan* plan,
                                        uint32_t block_index,
                                        uint64_t size) noexcept;

    static void
    append_package_jpeg_segment_chunk(PreparedTransferPackagePlan* plan,
                                      uint32_t block_index, uint8_t marker_code,
                                      size_t payload_size) noexcept;

    static EmitTransferResult validate_prepared_transfer_package_plan(
        std::span<const std::byte> input, const PreparedTransferBundle& bundle,
        const PreparedTransferPackagePlan& plan) noexcept;

    static EmitTransferResult validate_prepared_transfer_package_batch(
        const PreparedTransferPackageBatch& batch) noexcept;

    static bool serialize_jpeg_marker_segment(
        uint8_t marker, std::span<const std::byte> payload,
        std::vector<std::byte>* out_bytes, EmitTransferResult* out) noexcept;

    static bool serialize_jxl_box(std::array<char, 4> box_type,
                                  std::span<const std::byte> payload,
                                  std::vector<std::byte>* out_bytes,
                                  EmitTransferResult* out) noexcept;

    static bool write_webp_chunk(TransferByteWriter& writer,
                                 std::array<char, 4> chunk_type,
                                 std::span<const std::byte> payload,
                                 EmitTransferResult* out) noexcept;

    static bool serialize_webp_chunk(std::array<char, 4> chunk_type,
                                     std::span<const std::byte> payload,
                                     std::vector<std::byte>* out_bytes,
                                     EmitTransferResult* out) noexcept;

    static bool materialize_prepared_transfer_package_chunk(
        std::span<const std::byte> input, const PreparedTransferBundle& bundle,
        const PreparedTransferPackageChunk& chunk,
        std::vector<std::byte>* out_bytes, EmitTransferResult* out) noexcept;

    static bool write_transfer_bytes(TransferByteWriter& writer,
                                     std::span<const std::byte> bytes,
                                     EmitTransferResult* out,
                                     const char* message) noexcept
    {
        if (!out) {
            return false;
        }
        if (bytes.empty()) {
            return true;
        }
        const TransferStatus st = writer.write(bytes);
        if (st == TransferStatus::Ok) {
            return true;
        }
        out->status = st;
        out->code   = EmitTransferCode::BackendWriteFailed;
        out->errors += 1U;
        out->message = message ? message : "transfer byte writer failed";
        return false;
    }

    static bool write_jpeg_marker_segment(TransferByteWriter& writer,
                                          uint8_t marker,
                                          std::span<const std::byte> payload,
                                          EmitTransferResult* out) noexcept
    {
        std::array<std::byte, 4> header = {
            std::byte { 0xFF },
            static_cast<std::byte>(marker),
            std::byte { 0x00 },
            std::byte { 0x00 },
        };
        const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
        header[2] = static_cast<std::byte>((seg_len >> 8U) & 0xFFU);
        header[3] = static_cast<std::byte>((seg_len >> 0U) & 0xFFU);
        if (!write_transfer_bytes(writer,
                                  std::span<const std::byte>(header.data(),
                                                             header.size()),
                                  out, "jpeg edit header write failed")) {
            return false;
        }
        return write_transfer_bytes(writer, payload, out,
                                    "jpeg edit payload write failed");
    }

    static bool serialize_jpeg_marker_segment(
        uint8_t marker, std::span<const std::byte> payload,
        std::vector<std::byte>* out_bytes, EmitTransferResult* out) noexcept
    {
        if (!out_bytes) {
            if (out) {
                out->status = TransferStatus::InvalidArgument;
                out->code   = EmitTransferCode::InvalidArgument;
                out->errors += 1U;
                out->message = "jpeg segment output buffer is null";
            }
            return false;
        }
        out_bytes->clear();
        out_bytes->reserve(4U + payload.size());
        const uint16_t seg_len = static_cast<uint16_t>(payload.size() + 2U);
        out_bytes->push_back(std::byte { 0xFF });
        out_bytes->push_back(static_cast<std::byte>(marker));
        out_bytes->push_back(static_cast<std::byte>((seg_len >> 8U) & 0xFFU));
        out_bytes->push_back(static_cast<std::byte>((seg_len >> 0U) & 0xFFU));
        out_bytes->insert(out_bytes->end(), payload.begin(), payload.end());
        return true;
    }

    static bool write_jxl_box(TransferByteWriter& writer,
                              std::array<char, 4> box_type,
                              std::span<const std::byte> payload,
                              EmitTransferResult* out) noexcept
    {
        if (payload.size() > static_cast<size_t>(0xFFFFFFFFU - 8U)) {
            if (out) {
                out->status = TransferStatus::LimitExceeded;
                out->code   = EmitTransferCode::InvalidPayload;
                out->errors += 1U;
                out->message = "jxl box payload exceeds 32-bit box size";
            }
            return false;
        }
        std::array<std::byte, 8> header = {
            std::byte { 0x00 },
            std::byte { 0x00 },
            std::byte { 0x00 },
            std::byte { 0x00 },
            std::byte { static_cast<uint8_t>(box_type[0]) },
            std::byte { static_cast<uint8_t>(box_type[1]) },
            std::byte { static_cast<uint8_t>(box_type[2]) },
            std::byte { static_cast<uint8_t>(box_type[3]) },
        };
        const uint32_t box_size = static_cast<uint32_t>(payload.size() + 8U);
        header[0] = static_cast<std::byte>((box_size >> 24U) & 0xFFU);
        header[1] = static_cast<std::byte>((box_size >> 16U) & 0xFFU);
        header[2] = static_cast<std::byte>((box_size >> 8U) & 0xFFU);
        header[3] = static_cast<std::byte>((box_size >> 0U) & 0xFFU);
        if (!write_transfer_bytes(writer,
                                  std::span<const std::byte>(header.data(),
                                                             header.size()),
                                  out, "jxl package header write failed")) {
            return false;
        }
        return write_transfer_bytes(writer, payload, out,
                                    "jxl package payload write failed");
    }

    static bool serialize_jxl_box(std::array<char, 4> box_type,
                                  std::span<const std::byte> payload,
                                  std::vector<std::byte>* out_bytes,
                                  EmitTransferResult* out) noexcept
    {
        if (!out_bytes) {
            if (out) {
                out->status = TransferStatus::InvalidArgument;
                out->code   = EmitTransferCode::InvalidArgument;
                out->errors += 1U;
                out->message = "jxl box output buffer is null";
            }
            return false;
        }
        if (payload.size() > static_cast<size_t>(0xFFFFFFFFU - 8U)) {
            if (out) {
                out->status = TransferStatus::LimitExceeded;
                out->code   = EmitTransferCode::InvalidPayload;
                out->errors += 1U;
                out->message = "jxl box payload exceeds 32-bit box size";
            }
            return false;
        }
        out_bytes->clear();
        out_bytes->reserve(8U + payload.size());
        const uint32_t box_size = static_cast<uint32_t>(payload.size() + 8U);
        out_bytes->push_back(static_cast<std::byte>((box_size >> 24U) & 0xFFU));
        out_bytes->push_back(static_cast<std::byte>((box_size >> 16U) & 0xFFU));
        out_bytes->push_back(static_cast<std::byte>((box_size >> 8U) & 0xFFU));
        out_bytes->push_back(static_cast<std::byte>((box_size >> 0U) & 0xFFU));
        out_bytes->push_back(static_cast<std::byte>(box_type[0]));
        out_bytes->push_back(static_cast<std::byte>(box_type[1]));
        out_bytes->push_back(static_cast<std::byte>(box_type[2]));
        out_bytes->push_back(static_cast<std::byte>(box_type[3]));
        out_bytes->insert(out_bytes->end(), payload.begin(), payload.end());
        return true;
    }

    static bool write_webp_chunk(TransferByteWriter& writer,
                                 std::array<char, 4> chunk_type,
                                 std::span<const std::byte> payload,
                                 EmitTransferResult* out) noexcept
    {
        if (payload.size() > static_cast<size_t>(0xFFFFFFFFU)) {
            if (out) {
                out->status = TransferStatus::LimitExceeded;
                out->code   = EmitTransferCode::InvalidPayload;
                out->errors += 1U;
                out->message = "webp chunk payload exceeds 32-bit chunk size";
            }
            return false;
        }

        std::array<std::byte, 8> header = {
            static_cast<std::byte>(chunk_type[0]),
            static_cast<std::byte>(chunk_type[1]),
            static_cast<std::byte>(chunk_type[2]),
            static_cast<std::byte>(chunk_type[3]),
            std::byte { 0x00 },
            std::byte { 0x00 },
            std::byte { 0x00 },
            std::byte { 0x00 },
        };
        const uint32_t chunk_size = static_cast<uint32_t>(payload.size());
        header[4] = static_cast<std::byte>((chunk_size >> 0U) & 0xFFU);
        header[5] = static_cast<std::byte>((chunk_size >> 8U) & 0xFFU);
        header[6] = static_cast<std::byte>((chunk_size >> 16U) & 0xFFU);
        header[7] = static_cast<std::byte>((chunk_size >> 24U) & 0xFFU);
        if (!write_transfer_bytes(writer,
                                  std::span<const std::byte>(header.data(),
                                                             header.size()),
                                  out, "webp package header write failed")) {
            return false;
        }
        if (!write_transfer_bytes(writer, payload, out,
                                  "webp package payload write failed")) {
            return false;
        }
        if ((chunk_size & 1U) != 0U) {
            const std::byte pad = std::byte { 0x00 };
            return write_transfer_bytes(writer,
                                        std::span<const std::byte>(&pad, 1U),
                                        out, "webp package pad write failed");
        }
        return true;
    }

    static bool serialize_webp_chunk(std::array<char, 4> chunk_type,
                                     std::span<const std::byte> payload,
                                     std::vector<std::byte>* out_bytes,
                                     EmitTransferResult* out) noexcept
    {
        if (!out_bytes) {
            if (out) {
                out->status = TransferStatus::InvalidArgument;
                out->code   = EmitTransferCode::InvalidArgument;
                out->errors += 1U;
                out->message = "webp chunk output buffer is null";
            }
            return false;
        }
        if (payload.size() > static_cast<size_t>(0xFFFFFFFFU)) {
            if (out) {
                out->status = TransferStatus::LimitExceeded;
                out->code   = EmitTransferCode::InvalidPayload;
                out->errors += 1U;
                out->message = "webp chunk payload exceeds 32-bit chunk size";
            }
            return false;
        }
        const uint32_t chunk_size      = static_cast<uint32_t>(payload.size());
        const uint64_t serialized_size = 8U + static_cast<uint64_t>(chunk_size)
                                         + static_cast<uint64_t>(
                                             (chunk_size & 1U) != 0U);
        out_bytes->clear();
        out_bytes->reserve(static_cast<size_t>(serialized_size));
        out_bytes->push_back(static_cast<std::byte>(chunk_type[0]));
        out_bytes->push_back(static_cast<std::byte>(chunk_type[1]));
        out_bytes->push_back(static_cast<std::byte>(chunk_type[2]));
        out_bytes->push_back(static_cast<std::byte>(chunk_type[3]));
        out_bytes->push_back(
            static_cast<std::byte>((chunk_size >> 0U) & 0xFFU));
        out_bytes->push_back(
            static_cast<std::byte>((chunk_size >> 8U) & 0xFFU));
        out_bytes->push_back(
            static_cast<std::byte>((chunk_size >> 16U) & 0xFFU));
        out_bytes->push_back(
            static_cast<std::byte>((chunk_size >> 24U) & 0xFFU));
        out_bytes->insert(out_bytes->end(), payload.begin(), payload.end());
        if ((chunk_size & 1U) != 0U) {
            out_bytes->push_back(std::byte { 0x00 });
        }
        return true;
    }

    static void append_message(std::string* dst, std::string_view msg) noexcept
    {
        if (!dst || msg.empty()) {
            return;
        }
        if (!dst->empty()) {
            dst->append("; ");
        }
        dst->append(msg.data(), msg.size());
    }

    static uint32_t
    next_prepared_block_order(const PreparedTransferBundle& bundle,
                              uint32_t base) noexcept
    {
        uint32_t order = base;
        for (size_t i = 0; i < bundle.blocks.size(); ++i) {
            if (bundle.blocks[i].order >= order) {
                order = bundle.blocks[i].order + 1U;
            }
        }
        return order;
    }

    static uint32_t
    remove_prepared_blocks_by_route(PreparedTransferBundle* bundle,
                                    std::string_view route) noexcept
    {
        if (!bundle || bundle->blocks.empty()) {
            return 0U;
        }

        size_t write     = 0U;
        uint32_t removed = 0U;
        for (size_t read = 0U; read < bundle->blocks.size(); ++read) {
            if (bundle->blocks[read].route == route) {
                removed += 1U;
                continue;
            }
            if (write != read) {
                bundle->blocks[write] = std::move(bundle->blocks[read]);
            }
            write += 1U;
        }
        bundle->blocks.resize(write);
        return removed;
    }

    static bool has_kind(const MetaStore& store, MetaKeyKind kind) noexcept
    {
        for (const Entry& e : store.entries()) {
            if (any(e.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (e.key.kind == kind) {
                return true;
            }
        }
        return false;
    }

    static uint32_t count_makernote_entries(const MetaStore& store) noexcept
    {
        uint32_t count = 0U;
        for (const Entry& e : store.entries()) {
            if (any(e.flags, EntryFlags::Deleted)
                || e.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            if (e.key.data.exif_tag.tag == 0x927CU) {
                count += 1U;
            }
        }
        return count;
    }

    static uint32_t count_jumbf_entries(const MetaStore& store) noexcept
    {
        uint32_t count = 0U;
        for (const Entry& e : store.entries()) {
            if (any(e.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (e.key.kind == MetaKeyKind::JumbfField) {
                if (!contains_path_segment(
                        arena_string(store.arena(),
                                     e.key.data.jumbf_field.field),
                        "c2pa")) {
                    count += 1U;
                }
                continue;
            }
            if (e.key.kind == MetaKeyKind::JumbfCborKey) {
                if (!contains_path_segment(
                        arena_string(store.arena(),
                                     e.key.data.jumbf_cbor_key.key),
                        "c2pa")) {
                    count += 1U;
                }
            }
        }
        return count;
    }

    static uint32_t
    count_non_c2pa_jumbf_cbor_entries(const MetaStore& store) noexcept
    {
        uint32_t count = 0U;
        for (const Entry& e : store.entries()) {
            if (any(e.flags, EntryFlags::Deleted)
                || e.key.kind != MetaKeyKind::JumbfCborKey) {
                continue;
            }
            if (!contains_path_segment(
                    arena_string(store.arena(), e.key.data.jumbf_cbor_key.key),
                    "c2pa")) {
                count += 1U;
            }
        }
        return count;
    }

    static bool is_c2pa_entry(const MetaStore& store, const Entry& e) noexcept
    {
        if (any(e.flags, EntryFlags::Deleted)) {
            return false;
        }
        if (e.key.kind == MetaKeyKind::JumbfField) {
            return contains_path_segment(
                arena_string(store.arena(), e.key.data.jumbf_field.field),
                "c2pa");
        }
        if (e.key.kind == MetaKeyKind::JumbfCborKey) {
            return contains_path_segment(
                arena_string(store.arena(), e.key.data.jumbf_cbor_key.key),
                "c2pa");
        }
        if (e.key.kind == MetaKeyKind::BmffField) {
            return contains_path_segment(
                arena_string(store.arena(), e.key.data.bmff_field.field),
                "c2pa");
        }
        return false;
    }

    static uint32_t count_c2pa_entries(const MetaStore& store) noexcept
    {
        uint32_t count = 0U;
        for (const Entry& e : store.entries()) {
            if (is_c2pa_entry(store, e)) {
                count += 1U;
            }
        }
        return count;
    }

    static bool read_transfer_jumbf_field_u64(const MetaStore& store,
                                              std::string_view field_name,
                                              uint64_t* out) noexcept
    {
        if (!out) {
            return false;
        }
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        if (ids.size() != 1U) {
            return false;
        }
        return meta_scalar_to_u64(store.entry(ids[0]).value, out);
    }

    static bool read_transfer_jumbf_field_text(const MetaStore& store,
                                               std::string_view field_name,
                                               std::string_view* out) noexcept
    {
        if (!out) {
            return false;
        }
        MetaKeyView key;
        key.kind                           = MetaKeyKind::JumbfField;
        key.data.jumbf_field.field         = field_name;
        const std::span<const EntryId> ids = store.find_all(key);
        if (ids.size() != 1U) {
            return false;
        }
        const MetaValue& value = store.entry(ids[0]).value;
        if (value.kind != MetaValueKind::Text) {
            return false;
        }
        *out = arena_string(store.arena(), value.data.span);
        return true;
    }

    static bool collect_transfer_c2pa_semantic_summary(
        const MetaStore& store, TransferC2paSemanticSummary* out) noexcept
    {
        if (!out) {
            return false;
        }
        *out = TransferC2paSemanticSummary {};

        uint64_t value = 0U;
        bool available = false;
        std::string_view text;

        if (read_transfer_jumbf_field_u64(store, "c2pa.semantic.claim_count",
                                          &value)) {
            out->claim_count = value;
            available        = true;
        }
        if (read_transfer_jumbf_field_u64(store,
                                          "c2pa.semantic.signature_count",
                                          &value)) {
            out->signature_count = value;
            available            = true;
        }
        if (read_transfer_jumbf_field_u64(
                store, "c2pa.semantic.signature_linked_count", &value)) {
            out->signature_linked = value;
            available             = true;
        }
        if (read_transfer_jumbf_field_u64(
                store, "c2pa.semantic.signature_orphan_count", &value)) {
            out->signature_orphan = value;
            available             = true;
        }
        if (read_transfer_jumbf_field_u64(
                store, "c2pa.semantic.explicit_reference_signature_count",
                &value)) {
            out->explicit_reference_signature_count = value;
            available                               = true;
        }
        if (read_transfer_jumbf_field_u64(
                store,
                "c2pa.semantic.explicit_reference_unresolved_signature_count",
                &value)) {
            out->explicit_reference_unresolved_signature_count = value;
            available                                          = true;
        }
        if (read_transfer_jumbf_field_u64(
                store,
                "c2pa.semantic.explicit_reference_ambiguous_signature_count",
                &value)) {
            out->explicit_reference_ambiguous_signature_count = value;
            available                                         = true;
        }
        if (read_transfer_jumbf_field_u64(store,
                                          "c2pa.semantic.manifest_present",
                                          &value)) {
            out->manifest_present = value;
            available             = true;
        }
        if (read_transfer_jumbf_field_u64(store, "c2pa.semantic.manifest_count",
                                          &value)) {
            out->manifest_count = value;
            available           = true;
        }
        if (read_transfer_jumbf_field_u64(store, "c2pa.semantic.claim_present",
                                          &value)) {
            out->claim_present = value;
            available          = true;
        }
        if (read_transfer_jumbf_field_u64(store,
                                          "c2pa.semantic.signature_present",
                                          &value)) {
            out->signature_present = value;
            available              = true;
        }
        if (read_transfer_jumbf_field_u64(store,
                                          "c2pa.semantic.assertion_count",
                                          &value)) {
            out->assertion_count = value;
            available            = true;
        }
        if (read_transfer_jumbf_field_text(store,
                                           "c2pa.semantic.claim_generator",
                                           &text)) {
            out->claim_generator_present = text.empty() ? 0U : 1U;
            available                    = true;
        }

        out->available = available;
        return true;
    }

    static void copy_transfer_c2pa_semantic_summary(
        const TransferC2paSemanticSummary& summary,
        ValidatePreparedC2paSignResult* out) noexcept
    {
        if (!out) {
            return;
        }
        out->semantic_manifest_present        = summary.manifest_present;
        out->semantic_manifest_count          = summary.manifest_count;
        out->semantic_claim_generator_present = summary.claim_generator_present;
        out->semantic_assertion_count         = summary.assertion_count;
        out->semantic_claim_count             = summary.claim_count;
        out->semantic_signature_count         = summary.signature_count;
        out->semantic_signature_linked        = summary.signature_linked;
        out->semantic_signature_orphan        = summary.signature_orphan;
        out->semantic_explicit_reference_signature_count
            = summary.explicit_reference_signature_count;
        out->semantic_explicit_reference_unresolved_signature_count
            = summary.explicit_reference_unresolved_signature_count;
        out->semantic_explicit_reference_ambiguous_signature_count
            = summary.explicit_reference_ambiguous_signature_count;
    }

    static bool fail_transfer_c2pa_semantic_validation(
        ValidatePreparedC2paSignResult* out, std::string_view reason,
        std::string_view message, TransferStatus status) noexcept
    {
        if (!out) {
            return false;
        }
        out->semantic_status = TransferC2paSemanticStatus::Invalid;
        out->semantic_reason.assign(reason.data(), reason.size());
        out->status = status;
        out->code   = EmitTransferCode::InvalidPayload;
        out->errors = 1U;
        out->message.assign(message.data(), message.size());
        return false;
    }

    static bool validate_transfer_c2pa_projection_prefix(
        std::string_view child_prefix, std::string_view parent_prefix) noexcept
    {
        return !child_prefix.empty() && !parent_prefix.empty()
               && starts_with(child_prefix, parent_prefix)
               && child_prefix.size() > parent_prefix.size()
               && child_prefix[parent_prefix.size()] == '.';
    }

    static bool validate_transfer_c2pa_semantics(
        std::span<const std::byte> logical_payload,
        const PreparedTransferC2paSignRequest& request,
        ValidatePreparedC2paSignResult* out) noexcept
    {
        if (!out) {
            return false;
        }

        MetaStore store;
        const JumbfDecodeResult decode = decode_jumbf_payload(logical_payload,
                                                              store);
        if (decode.status != JumbfDecodeStatus::Ok) {
            if (decode.status == JumbfDecodeStatus::LimitExceeded) {
                return fail_transfer_c2pa_semantic_validation(
                    out, "semantic_decode_limit_exceeded",
                    "signed c2pa payload semantic decode exceeded limits",
                    TransferStatus::LimitExceeded);
            }
            return fail_transfer_c2pa_semantic_validation(
                out,
                decode.status == JumbfDecodeStatus::Malformed
                    ? "semantic_decode_malformed"
                    : "semantic_decode_unsupported",
                "signed c2pa payload semantic decode failed",
                TransferStatus::Malformed);
        }

        store.finalize();

        TransferC2paSemanticSummary summary;
        if (!collect_transfer_c2pa_semantic_summary(store, &summary)
            || !summary.available) {
            return fail_transfer_c2pa_semantic_validation(
                out, "semantic_fields_missing",
                "signed c2pa payload is missing semantic summary fields",
                TransferStatus::Malformed);
        }

        copy_transfer_c2pa_semantic_summary(summary, out);

        std::string_view manifest_prefix;
        std::string_view claim_prefix;
        std::string_view signature_prefix;
        std::string_view signature_algorithm;
        std::string_view primary_signature_linked_claim_prefix;
        uint64_t primary_claim_assertion_count                             = 0U;
        uint64_t primary_claim_referenced_by_signature_count               = 0U;
        uint64_t primary_signature_linked_claim_count                      = 0U;
        uint64_t primary_signature_reference_key_hits                      = 0U;
        uint64_t primary_signature_explicit_reference_present              = 0U;
        uint64_t primary_signature_explicit_reference_resolved_claim_count = 0U;
        const bool have_manifest_prefix = read_transfer_jumbf_field_text(
            store, "c2pa.semantic.manifest.0.prefix", &manifest_prefix);
        const bool have_claim_prefix = read_transfer_jumbf_field_text(
            store, "c2pa.semantic.claim.0.prefix", &claim_prefix);
        const bool have_signature_prefix = read_transfer_jumbf_field_text(
            store, "c2pa.semantic.claim.0.signature.0.prefix",
            &signature_prefix);
        const bool have_signature_algorithm = read_transfer_jumbf_field_text(
            store, "c2pa.semantic.claim.0.signature.0.algorithm",
            &signature_algorithm);
        const bool have_primary_signature_linked_claim_prefix
            = read_transfer_jumbf_field_text(
                store, "c2pa.semantic.signature.0.linked_claim.0.prefix",
                &primary_signature_linked_claim_prefix);
        (void)read_transfer_jumbf_field_u64(
            store, "c2pa.semantic.claim.0.assertion_count",
            &primary_claim_assertion_count);
        (void)read_transfer_jumbf_field_u64(
            store, "c2pa.semantic.claim.0.referenced_by_signature_count",
            &primary_claim_referenced_by_signature_count);
        (void)read_transfer_jumbf_field_u64(
            store, "c2pa.semantic.signature.0.linked_claim_count",
            &primary_signature_linked_claim_count);
        (void)read_transfer_jumbf_field_u64(
            store, "c2pa.semantic.signature.0.reference_key_hits",
            &primary_signature_reference_key_hits);
        (void)read_transfer_jumbf_field_u64(
            store, "c2pa.semantic.signature.0.explicit_reference_present",
            &primary_signature_explicit_reference_present);
        (void)read_transfer_jumbf_field_u64(
            store,
            "c2pa.semantic.signature.0.explicit_reference_resolved_claim_count",
            &primary_signature_explicit_reference_resolved_claim_count);
        out->semantic_primary_claim_assertion_count
            = primary_claim_assertion_count;
        out->semantic_primary_claim_referenced_by_signature_count
            = primary_claim_referenced_by_signature_count;
        out->semantic_primary_signature_linked_claim_count
            = primary_signature_linked_claim_count;
        out->semantic_primary_signature_reference_key_hits
            = primary_signature_reference_key_hits;
        out->semantic_primary_signature_explicit_reference_present
            = primary_signature_explicit_reference_present;
        out->semantic_primary_signature_explicit_reference_resolved_claim_count
            = primary_signature_explicit_reference_resolved_claim_count;

        if (summary.manifest_present == 0U) {
            return fail_transfer_c2pa_semantic_validation(
                out, "manifest_missing",
                "signed c2pa payload is missing a manifest",
                TransferStatus::Malformed);
        }
        if (!request.manifest_label.empty() && summary.manifest_count != 1U) {
            return fail_transfer_c2pa_semantic_validation(
                out, "manifest_count_invalid",
                "signed c2pa payload must contain exactly one manifest for the current sign request",
                TransferStatus::Malformed);
        }
        if (!request.manifest_label.empty()) {
            if (!have_manifest_prefix || manifest_prefix.empty()) {
                return fail_transfer_c2pa_semantic_validation(
                    out, "manifest_prefix_missing",
                    "signed c2pa payload is missing the primary manifest prefix for the current sign request",
                    TransferStatus::Malformed);
            }
            if (!have_claim_prefix
                || !validate_transfer_c2pa_projection_prefix(claim_prefix,
                                                             manifest_prefix)) {
                return fail_transfer_c2pa_semantic_validation(
                    out, "claim_prefix_mismatch",
                    "signed c2pa payload claim prefix does not match the current manifest contract",
                    TransferStatus::Malformed);
            }
            if (!have_signature_prefix
                || !validate_transfer_c2pa_projection_prefix(signature_prefix,
                                                             claim_prefix)) {
                return fail_transfer_c2pa_semantic_validation(
                    out, "signature_prefix_mismatch",
                    "signed c2pa payload signature prefix does not match the current claim contract",
                    TransferStatus::Malformed);
            }
        }
        if (summary.claim_count == 0U || summary.claim_present == 0U) {
            return fail_transfer_c2pa_semantic_validation(
                out, "claim_missing", "signed c2pa payload is missing a claim",
                TransferStatus::Malformed);
        }
        if (request.requires_manifest_builder
            && summary.claim_generator_present == 0U) {
            return fail_transfer_c2pa_semantic_validation(
                out, "claim_generator_missing",
                "signed c2pa payload is missing claim_generator for the current sign request",
                TransferStatus::Malformed);
        }
        if (request.requires_content_binding && summary.assertion_count == 0U) {
            return fail_transfer_c2pa_semantic_validation(
                out, "content_binding_assertions_missing",
                "signed c2pa payload is missing assertions for the current content-binding contract",
                TransferStatus::Malformed);
        }
        if (request.requires_content_binding
            && primary_claim_assertion_count == 0U) {
            return fail_transfer_c2pa_semantic_validation(
                out, "primary_claim_assertions_missing",
                "signed c2pa payload primary claim is missing assertions for the current content-binding contract",
                TransferStatus::Malformed);
        }
        if (summary.signature_count == 0U || summary.signature_present == 0U) {
            return fail_transfer_c2pa_semantic_validation(
                out, "signature_missing",
                "signed c2pa payload is missing a signature",
                TransferStatus::Malformed);
        }
        if (!have_signature_algorithm || signature_algorithm.empty()) {
            return fail_transfer_c2pa_semantic_validation(
                out, "signature_algorithm_missing",
                "signed c2pa payload is missing the primary signature algorithm",
                TransferStatus::Malformed);
        }
        if (primary_signature_reference_key_hits != 0U) {
            if (primary_signature_explicit_reference_present == 0U) {
                return fail_transfer_c2pa_semantic_validation(
                    out, "primary_signature_reference_state_invalid",
                    "signed c2pa payload primary signature has reference keys without explicit-reference state",
                    TransferStatus::Malformed);
            }
            if (primary_signature_explicit_reference_resolved_claim_count
                == 0U) {
                return fail_transfer_c2pa_semantic_validation(
                    out, "primary_signature_reference_unresolved",
                    "signed c2pa payload primary signature explicit references do not resolve under the current sign request",
                    TransferStatus::Malformed);
            }
            if (primary_signature_explicit_reference_resolved_claim_count
                > 1U) {
                return fail_transfer_c2pa_semantic_validation(
                    out, "primary_signature_reference_ambiguous",
                    "signed c2pa payload primary signature explicit references resolve to multiple claims under the current sign request",
                    TransferStatus::Malformed);
            }
        }
        if (summary.signature_linked > summary.signature_count) {
            return fail_transfer_c2pa_semantic_validation(
                out, "signature_linked_count_invalid",
                "signed c2pa payload has invalid linked-signature counts",
                TransferStatus::Malformed);
        }
        if (summary.signature_orphan > summary.signature_count) {
            return fail_transfer_c2pa_semantic_validation(
                out, "signature_orphan_count_invalid",
                "signed c2pa payload has invalid orphan-signature counts",
                TransferStatus::Malformed);
        }
        if ((summary.signature_linked + summary.signature_orphan)
            != summary.signature_count) {
            return fail_transfer_c2pa_semantic_validation(
                out, "signature_link_consistency_invalid",
                "signed c2pa payload has inconsistent signature-link counts",
                TransferStatus::Malformed);
        }
        if (summary.explicit_reference_signature_count
            > summary.signature_count) {
            return fail_transfer_c2pa_semantic_validation(
                out, "explicit_reference_count_invalid",
                "signed c2pa payload has invalid explicit-reference counts",
                TransferStatus::Malformed);
        }
        if (summary.explicit_reference_unresolved_signature_count
            > summary.explicit_reference_signature_count) {
            return fail_transfer_c2pa_semantic_validation(
                out, "explicit_reference_unresolved_count_invalid",
                "signed c2pa payload has invalid unresolved explicit-reference counts",
                TransferStatus::Malformed);
        }
        if (summary.explicit_reference_ambiguous_signature_count
            > summary.explicit_reference_signature_count) {
            return fail_transfer_c2pa_semantic_validation(
                out, "explicit_reference_ambiguous_count_invalid",
                "signed c2pa payload has invalid ambiguous explicit-reference counts",
                TransferStatus::Malformed);
        }
        if (summary.explicit_reference_unresolved_signature_count
            > (summary.explicit_reference_signature_count
               - summary.explicit_reference_ambiguous_signature_count)) {
            return fail_transfer_c2pa_semantic_validation(
                out, "explicit_reference_consistency_invalid",
                "signed c2pa payload has inconsistent explicit-reference counts",
                TransferStatus::Malformed);
        }
        if (summary.signature_linked == 0U) {
            return fail_transfer_c2pa_semantic_validation(
                out, "signature_unlinked",
                "signed c2pa payload does not link any signature to a claim",
                TransferStatus::Malformed);
        }
        if (!request.manifest_label.empty()
            && primary_signature_linked_claim_count != 0U) {
            if (!have_primary_signature_linked_claim_prefix
                || primary_signature_linked_claim_prefix.empty()) {
                return fail_transfer_c2pa_semantic_validation(
                    out, "primary_signature_claim_missing",
                    "signed c2pa payload is missing the primary signature linked-claim prefix under the current sign request",
                    TransferStatus::Malformed);
            }
            if (!have_claim_prefix
                || primary_signature_linked_claim_prefix != claim_prefix) {
                return fail_transfer_c2pa_semantic_validation(
                    out, "primary_signature_claim_drift",
                    "signed c2pa payload primary signature links to a different claim than the prepared sign request",
                    TransferStatus::Malformed);
            }
        }
        if (primary_claim_referenced_by_signature_count == 0U
            || primary_signature_linked_claim_count == 0U) {
            return fail_transfer_c2pa_semantic_validation(
                out, "primary_claim_signature_link_missing",
                "signed c2pa payload does not link the primary claim and signature under the current sign request",
                TransferStatus::Malformed);
        }
        if (!request.manifest_label.empty()
            && primary_claim_referenced_by_signature_count > 1U) {
            return fail_transfer_c2pa_semantic_validation(
                out, "primary_claim_signature_ambiguous",
                "signed c2pa payload primary claim is referenced by multiple signatures under the current sign request",
                TransferStatus::Malformed);
        }
        if (!request.manifest_label.empty() && summary.signature_linked > 1U) {
            return fail_transfer_c2pa_semantic_validation(
                out, "linked_signature_count_drift",
                "signed c2pa payload has extra linked signatures beyond the current sign request",
                TransferStatus::Malformed);
        }
        if (summary.explicit_reference_signature_count != 0U) {
            if (summary.explicit_reference_unresolved_signature_count != 0U) {
                return fail_transfer_c2pa_semantic_validation(
                    out, "explicit_reference_unresolved",
                    "signed c2pa payload has unresolved explicit references",
                    TransferStatus::Malformed);
            }
            if (summary.explicit_reference_ambiguous_signature_count != 0U) {
                return fail_transfer_c2pa_semantic_validation(
                    out, "explicit_reference_ambiguous",
                    "signed c2pa payload has ambiguous explicit references",
                    TransferStatus::Malformed);
            }
        }

        out->semantic_status = TransferC2paSemanticStatus::Ok;
        out->semantic_reason = "ok";
        return true;
    }

    static TransferPolicyAction
    resolve_makernote_policy(TransferPolicyAction requested,
                             TransferPolicyReason* out_reason) noexcept
    {
        if (!out_reason) {
            return TransferPolicyAction::Keep;
        }
        switch (requested) {
        case TransferPolicyAction::Keep:
            *out_reason = TransferPolicyReason::Default;
            return TransferPolicyAction::Keep;
        case TransferPolicyAction::Drop:
            *out_reason = TransferPolicyReason::ExplicitDrop;
            return TransferPolicyAction::Drop;
        case TransferPolicyAction::Invalidate:
            *out_reason = TransferPolicyReason::PortableInvalidationUnavailable;
            return TransferPolicyAction::Drop;
        case TransferPolicyAction::Rewrite:
            *out_reason = TransferPolicyReason::RewriteUnavailablePreservedRaw;
            return TransferPolicyAction::Keep;
        }
        *out_reason = TransferPolicyReason::Default;
        return TransferPolicyAction::Keep;
    }

    static TransferPolicyAction
    resolve_unserialized_policy(TransferPolicyAction requested,
                                TransferPolicyReason* out_reason) noexcept
    {
        if (!out_reason) {
            return TransferPolicyAction::Drop;
        }
        switch (requested) {
        case TransferPolicyAction::Drop:
            *out_reason = TransferPolicyReason::ExplicitDrop;
            return TransferPolicyAction::Drop;
        case TransferPolicyAction::Keep:
        case TransferPolicyAction::Invalidate:
        case TransferPolicyAction::Rewrite:
            *out_reason = TransferPolicyReason::TargetSerializationUnavailable;
            return TransferPolicyAction::Drop;
        }
        *out_reason = TransferPolicyReason::TargetSerializationUnavailable;
        return TransferPolicyAction::Drop;
    }

    static TransferPolicyAction
    resolve_raw_jumbf_policy(TransferPolicyAction requested,
                             TransferPolicyReason* out_reason) noexcept
    {
        if (!out_reason) {
            return TransferPolicyAction::Keep;
        }
        switch (requested) {
        case TransferPolicyAction::Keep:
            *out_reason = TransferPolicyReason::Default;
            return TransferPolicyAction::Keep;
        case TransferPolicyAction::Drop:
            *out_reason = TransferPolicyReason::ExplicitDrop;
            return TransferPolicyAction::Drop;
        case TransferPolicyAction::Invalidate:
            *out_reason = TransferPolicyReason::PortableInvalidationUnavailable;
            return TransferPolicyAction::Drop;
        case TransferPolicyAction::Rewrite:
            *out_reason = TransferPolicyReason::RewriteUnavailablePreservedRaw;
            return TransferPolicyAction::Keep;
        }
        *out_reason = TransferPolicyReason::Default;
        return TransferPolicyAction::Keep;
    }

    static TransferPolicyAction
    resolve_projected_jumbf_policy(TransferPolicyAction requested,
                                   TransferPolicyReason* out_reason) noexcept
    {
        if (!out_reason) {
            return TransferPolicyAction::Keep;
        }
        switch (requested) {
        case TransferPolicyAction::Keep:
        case TransferPolicyAction::Rewrite:
            *out_reason = TransferPolicyReason::ProjectedPayload;
            return TransferPolicyAction::Keep;
        case TransferPolicyAction::Drop:
            *out_reason = TransferPolicyReason::ExplicitDrop;
            return TransferPolicyAction::Drop;
        case TransferPolicyAction::Invalidate:
            *out_reason = TransferPolicyReason::PortableInvalidationUnavailable;
            return TransferPolicyAction::Drop;
        }
        *out_reason = TransferPolicyReason::ProjectedPayload;
        return TransferPolicyAction::Keep;
    }

    static TransferPolicyAction resolve_c2pa_transfer_policy(
        TransferPolicyAction requested, bool raw_passthrough_available,
        bool draft_invalidation_available, C2paPayloadClass source_class,
        TransferPolicyReason* out_reason, TransferC2paMode* out_mode) noexcept
    {
        if (!out_reason) {
            return TransferPolicyAction::Drop;
        }
        TransferC2paMode ignored_mode = TransferC2paMode::NotApplicable;
        if (!out_mode) {
            out_mode = &ignored_mode;
        }
        switch (requested) {
        case TransferPolicyAction::Drop:
            *out_reason = TransferPolicyReason::ExplicitDrop;
            *out_mode   = TransferC2paMode::Drop;
            return TransferPolicyAction::Drop;
        case TransferPolicyAction::Invalidate:
            if (draft_invalidation_available) {
                *out_reason = TransferPolicyReason::DraftInvalidationPayload;
                *out_mode   = TransferC2paMode::DraftUnsignedInvalidation;
                return TransferPolicyAction::Keep;
            }
            *out_reason = TransferPolicyReason::PortableInvalidationUnavailable;
            *out_mode   = TransferC2paMode::Drop;
            return TransferPolicyAction::Drop;
        case TransferPolicyAction::Keep:
            if (raw_passthrough_available
                && source_class
                       == C2paPayloadClass::DraftUnsignedInvalidation) {
                *out_reason = TransferPolicyReason::Default;
                *out_mode   = TransferC2paMode::PreserveRaw;
                return TransferPolicyAction::Keep;
            }
            *out_reason
                = raw_passthrough_available
                      ? TransferPolicyReason::ContentBoundTransferUnavailable
                      : TransferPolicyReason::TargetSerializationUnavailable;
            *out_mode = TransferC2paMode::Drop;
            return TransferPolicyAction::Drop;
        case TransferPolicyAction::Rewrite:
            *out_reason = TransferPolicyReason::SignedRewriteUnavailable;
            *out_mode   = TransferC2paMode::Drop;
            return TransferPolicyAction::Drop;
        }
        *out_reason = TransferPolicyReason::TargetSerializationUnavailable;
        *out_mode   = TransferC2paMode::Drop;
        return TransferPolicyAction::Drop;
    }

    static TransferC2paSourceKind
    classify_c2pa_source_kind(uint32_t c2pa_count,
                              bool raw_passthrough_available,
                              C2paPayloadClass source_class) noexcept
    {
        if (c2pa_count == 0U) {
            return TransferC2paSourceKind::NotPresent;
        }
        if (!raw_passthrough_available) {
            return TransferC2paSourceKind::DecodedOnly;
        }
        switch (source_class) {
        case C2paPayloadClass::DraftUnsignedInvalidation:
            return TransferC2paSourceKind::DraftUnsignedInvalidation;
        case C2paPayloadClass::ContentBound:
            return TransferC2paSourceKind::ContentBound;
        case C2paPayloadClass::NotC2pa:
            return TransferC2paSourceKind::DecodedOnly;
        }
        return TransferC2paSourceKind::DecodedOnly;
    }

    static TransferC2paPreparedOutput
    classify_c2pa_prepared_output(uint32_t c2pa_count,
                                  TransferPolicyAction effective,
                                  TransferC2paMode mode) noexcept
    {
        if (c2pa_count == 0U) {
            return TransferC2paPreparedOutput::NotPresent;
        }
        if (effective == TransferPolicyAction::Drop) {
            return TransferC2paPreparedOutput::Dropped;
        }
        switch (mode) {
        case TransferC2paMode::PreserveRaw:
            return TransferC2paPreparedOutput::PreservedRaw;
        case TransferC2paMode::DraftUnsignedInvalidation:
            return TransferC2paPreparedOutput::GeneratedDraftUnsignedInvalidation;
        case TransferC2paMode::SignedRewrite:
            return TransferC2paPreparedOutput::SignedRewrite;
        case TransferC2paMode::NotPresent:
            return TransferC2paPreparedOutput::NotPresent;
        case TransferC2paMode::Drop: return TransferC2paPreparedOutput::Dropped;
        case TransferC2paMode::NotApplicable:
            return TransferC2paPreparedOutput::NotApplicable;
        }
        return TransferC2paPreparedOutput::NotApplicable;
    }

    static uint32_t
    count_existing_jpeg_segments_by_route(const JpegScanResult& scan,
                                          std::string_view route) noexcept
    {
        uint32_t count = 0U;
        for (size_t i = 0; i < scan.leading_segments.size(); ++i) {
            if (scan.leading_segments[i].route == route) {
                count += 1U;
            }
        }
        return count;
    }

    static PreparedTransferC2paRewriteRequirements
    build_c2pa_rewrite_requirements(TransferTargetFormat target_format,
                                    TransferPolicyAction requested,
                                    uint32_t c2pa_count,
                                    TransferC2paSourceKind source_kind) noexcept
    {
        PreparedTransferC2paRewriteRequirements out;
        out.target_format   = target_format;
        out.source_kind     = source_kind;
        out.matched_entries = c2pa_count;
        if (c2pa_count == 0U) {
            out.state   = TransferC2paRewriteState::NotApplicable;
            out.message = "no c2pa entries in source metadata";
            return out;
        }
        if (requested != TransferPolicyAction::Rewrite) {
            out.state   = TransferC2paRewriteState::NotRequested;
            out.message = "signed c2pa rewrite was not requested";
            return out;
        }

        out.state = TransferC2paRewriteState::SigningMaterialRequired;
        out.target_carrier_available = target_format
                                       == TransferTargetFormat::Jpeg;
        out.content_change_invalidates_existing = target_format
                                                  == TransferTargetFormat::Jpeg;
        out.requires_manifest_builder  = true;
        out.requires_content_binding   = true;
        out.requires_certificate_chain = true;
        out.requires_private_key       = true;
        out.requires_signing_time      = true;
        out.message
            = out.target_carrier_available
                  ? "signed c2pa rewrite requires manifest rebuild, content "
                    "binding, certificate chain, private key, and signing "
                    "time"
                  : "signed c2pa rewrite requires signing material and a "
                    "target-specific carrier serializer";
        return out;
    }

    static void append_policy_decision(
        PreparedTransferBundle* bundle, TransferPolicySubject subject,
        TransferPolicyAction requested, TransferPolicyAction effective,
        TransferPolicyReason reason, uint32_t matched_entries,
        std::string_view message,
        TransferC2paMode c2pa_mode = TransferC2paMode::NotApplicable,
        TransferC2paSourceKind c2pa_source_kind
        = TransferC2paSourceKind::NotApplicable,
        TransferC2paPreparedOutput c2pa_prepared_output
        = TransferC2paPreparedOutput::NotApplicable) noexcept
    {
        if (!bundle) {
            return;
        }
        PreparedTransferPolicyDecision decision;
        decision.subject              = subject;
        decision.requested            = requested;
        decision.effective            = effective;
        decision.reason               = reason;
        decision.c2pa_mode            = c2pa_mode;
        decision.c2pa_source_kind     = c2pa_source_kind;
        decision.c2pa_prepared_output = c2pa_prepared_output;
        decision.matched_entries      = matched_entries;
        decision.message.assign(message.data(), message.size());
        bundle->policy_decisions.push_back(std::move(decision));
    }

    static void add_prepare_warning(PrepareTransferResult* out,
                                    PrepareTransferCode code,
                                    std::string_view message) noexcept
    {
        if (!out) {
            return;
        }
        if (out->code == PrepareTransferCode::None) {
            out->code = code;
        }
        out->warnings += 1U;
        append_message(&out->message, message);
    }

    static PreparedTransferPolicyDecision*
    find_policy_decision(PreparedTransferBundle* bundle,
                         TransferPolicySubject subject) noexcept
    {
        if (!bundle) {
            return nullptr;
        }
        for (size_t i = 0; i < bundle->policy_decisions.size(); ++i) {
            if (bundle->policy_decisions[i].subject == subject) {
                return &bundle->policy_decisions[i];
            }
        }
        return nullptr;
    }

    static TransferFileStatus map_file_status(MappedFileStatus status) noexcept
    {
        switch (status) {
        case MappedFileStatus::Ok: return TransferFileStatus::Ok;
        case MappedFileStatus::OpenFailed:
            return TransferFileStatus::OpenFailed;
        case MappedFileStatus::StatFailed:
            return TransferFileStatus::StatFailed;
        case MappedFileStatus::TooLarge: return TransferFileStatus::TooLarge;
        case MappedFileStatus::MapFailed: return TransferFileStatus::MapFailed;
        }
        return TransferFileStatus::ReadFailed;
    }

    static bool has_read_failure(const SimpleMetaResult& r) noexcept
    {
        if (r.scan.status == ScanStatus::Malformed
            || r.payload.status == PayloadStatus::Malformed
            || r.payload.status == PayloadStatus::LimitExceeded
            || r.exif.status == ExifDecodeStatus::Malformed
            || r.exif.status == ExifDecodeStatus::LimitExceeded
            || r.xmp.status == XmpDecodeStatus::Malformed
            || r.xmp.status == XmpDecodeStatus::LimitExceeded
            || r.jumbf.status == JumbfDecodeStatus::Malformed
            || r.jumbf.status == JumbfDecodeStatus::LimitExceeded) {
            return true;
        }
        return false;
    }

    static void append_ascii_bytes(std::vector<std::byte>* out,
                                   std::string_view s) noexcept
    {
        if (!out || s.empty()) {
            return;
        }
        out->reserve(out->size() + s.size());
        for (char c : s) {
            out->push_back(static_cast<std::byte>(static_cast<uint8_t>(c)));
        }
    }

    static void append_u16le(std::vector<std::byte>* out, uint16_t v) noexcept
    {
        if (!out) {
            return;
        }
        out->push_back(static_cast<std::byte>((v >> 0) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 8) & 0xFFU));
    }

    static void append_u8(std::vector<std::byte>* out, uint8_t v) noexcept
    {
        if (!out) {
            return;
        }
        out->push_back(static_cast<std::byte>(v));
    }

    static void append_u32le(std::vector<std::byte>* out, uint32_t v) noexcept
    {
        if (!out) {
            return;
        }
        out->push_back(static_cast<std::byte>((v >> 0) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 8) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 16) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 24) & 0xFFU));
    }

    static void append_u64le(std::vector<std::byte>* out, uint64_t v) noexcept
    {
        if (!out) {
            return;
        }
        out->push_back(static_cast<std::byte>((v >> 0) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 8) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 16) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 24) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 32) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 40) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 48) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 56) & 0xFFU));
    }

    static bool read_u8(std::span<const std::byte> bytes, size_t* io_off,
                        uint8_t* out) noexcept
    {
        if (!io_off || !out || *io_off >= bytes.size()) {
            return false;
        }
        *out = static_cast<uint8_t>(bytes[*io_off]);
        *io_off += 1U;
        return true;
    }

    static bool read_u32le(std::span<const std::byte> bytes, size_t* io_off,
                           uint32_t* out) noexcept
    {
        if (!io_off || !out || *io_off + 4U > bytes.size()) {
            return false;
        }
        const size_t off = *io_off;
        *out             = static_cast<uint32_t>(
            (static_cast<uint32_t>(static_cast<uint8_t>(bytes[off + 0U])) << 0U)
            | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[off + 1U]))
               << 8U)
            | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[off + 2U]))
               << 16U)
            | (static_cast<uint32_t>(static_cast<uint8_t>(bytes[off + 3U]))
               << 24U));
        *io_off += 4U;
        return true;
    }

    static bool read_u64le(std::span<const std::byte> bytes, size_t* io_off,
                           uint64_t* out) noexcept
    {
        if (!io_off || !out || *io_off + 8U > bytes.size()) {
            return false;
        }
        const size_t off = *io_off;
        *out             = static_cast<uint64_t>(
            (static_cast<uint64_t>(static_cast<uint8_t>(bytes[off + 0U])) << 0U)
            | (static_cast<uint64_t>(static_cast<uint8_t>(bytes[off + 1U]))
               << 8U)
            | (static_cast<uint64_t>(static_cast<uint8_t>(bytes[off + 2U]))
               << 16U)
            | (static_cast<uint64_t>(static_cast<uint8_t>(bytes[off + 3U]))
               << 24U)
            | (static_cast<uint64_t>(static_cast<uint8_t>(bytes[off + 4U]))
               << 32U)
            | (static_cast<uint64_t>(static_cast<uint8_t>(bytes[off + 5U]))
               << 40U)
            | (static_cast<uint64_t>(static_cast<uint8_t>(bytes[off + 6U]))
               << 48U)
            | (static_cast<uint64_t>(static_cast<uint8_t>(bytes[off + 7U]))
               << 56U));
        *io_off += 8U;
        return true;
    }

    static bool read_exact_bytes(std::span<const std::byte> bytes,
                                 size_t* io_off, uint64_t size,
                                 std::vector<std::byte>* out) noexcept
    {
        if (!io_off || !out || size > bytes.size() || *io_off > bytes.size()
            || *io_off + size > bytes.size()) {
            return false;
        }
        out->assign(bytes.begin() + static_cast<std::ptrdiff_t>(*io_off),
                    bytes.begin()
                        + static_cast<std::ptrdiff_t>(*io_off + size));
        *io_off += static_cast<size_t>(size);
        return true;
    }

    static bool read_string_le(std::span<const std::byte> bytes, size_t* io_off,
                               std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        uint64_t size = 0U;
        if (!read_u64le(bytes, io_off, &size) || size > bytes.size()) {
            return false;
        }
        std::vector<std::byte> tmp;
        if (!read_exact_bytes(bytes, io_off, size, &tmp)) {
            return false;
        }
        out->assign(reinterpret_cast<const char*>(tmp.data()), tmp.size());
        return true;
    }

    static bool read_blob_le(std::span<const std::byte> bytes, size_t* io_off,
                             std::vector<std::byte>* out) noexcept
    {
        uint64_t size = 0U;
        if (!read_u64le(bytes, io_off, &size)) {
            return false;
        }
        return read_exact_bytes(bytes, io_off, size, out);
    }

    static void append_u16be(std::vector<std::byte>* out, uint16_t v) noexcept
    {
        if (!out) {
            return;
        }
        out->push_back(static_cast<std::byte>((v >> 8) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 0) & 0xFFU));
    }

    static void append_u32be(std::vector<std::byte>* out, uint32_t v) noexcept
    {
        if (!out) {
            return;
        }
        out->push_back(static_cast<std::byte>((v >> 24) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 16) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 8) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 0) & 0xFFU));
    }

    static bool parse_bmff_box_header(std::span<const std::byte> bytes,
                                      uint32_t* out_header_len,
                                      uint32_t* out_type) noexcept
    {
        if (!out_header_len || !out_type) {
            return false;
        }
        uint32_t size32 = 0U;
        uint32_t type   = 0U;
        if (!read_u32be(bytes, 0U, &size32) || !read_u32be(bytes, 4U, &type)) {
            return false;
        }
        uint32_t header_len = 8U;
        if (size32 == 1U) {
            uint64_t size64 = 0U;
            if (!read_u64be(bytes, 8U, &size64) || size64 < 16U) {
                return false;
            }
            header_len = 16U;
        } else if (size32 != 0U && size32 < 8U) {
            return false;
        }
        if (bytes.size() < header_len) {
            return false;
        }
        *out_header_len = header_len;
        *out_type       = type;
        return true;
    }

    static bool read_bmff_box_size(std::span<const std::byte> bytes,
                                   uint64_t* out_size) noexcept
    {
        if (!out_size) {
            return false;
        }
        uint32_t size32 = 0U;
        if (!read_u32be(bytes, 0U, &size32)) {
            return false;
        }
        if (size32 == 1U) {
            uint64_t size64 = 0U;
            if (!read_u64be(bytes, 8U, &size64) || size64 < 16U) {
                return false;
            }
            *out_size = size64;
            return true;
        }
        if (size32 == 0U) {
            *out_size = static_cast<uint64_t>(bytes.size());
            return true;
        }
        if (size32 < 8U) {
            return false;
        }
        *out_size = size32;
        return true;
    }

    static bool extract_first_cbor_box_payload(
        std::span<const std::byte> logical_payload,
        std::span<const std::byte>* out_payload) noexcept
    {
        if (!out_payload) {
            return false;
        }
        *out_payload = std::span<const std::byte>();

        uint32_t header_len = 0U;
        uint32_t box_type   = 0U;
        if (!parse_bmff_box_header(logical_payload, &header_len, &box_type)) {
            return false;
        }
        if (box_type != fourcc('j', 'u', 'm', 'b')
            && box_type != fourcc('c', '2', 'p', 'a')) {
            return false;
        }

        size_t off = static_cast<size_t>(header_len);
        while (off < logical_payload.size()) {
            const std::span<const std::byte> child_bytes
                = logical_payload.subspan(off);
            uint32_t child_header_len = 0U;
            uint32_t child_type       = 0U;
            uint64_t child_size       = 0U;
            if (!parse_bmff_box_header(child_bytes, &child_header_len,
                                       &child_type)
                || !read_bmff_box_size(child_bytes, &child_size)
                || child_size < child_header_len
                || child_size > child_bytes.size()) {
                return false;
            }
            if (child_type == fourcc('c', 'b', 'o', 'r')) {
                *out_payload = child_bytes.subspan(
                    static_cast<size_t>(child_header_len),
                    static_cast<size_t>(child_size - child_header_len));
                return true;
            }
            off += static_cast<size_t>(child_size);
        }
        return false;
    }

    static bool payload_contains_ascii(std::span<const std::byte> bytes,
                                       std::string_view text) noexcept
    {
        if (text.empty()) {
            return true;
        }
        if (bytes.size() < text.size()) {
            return false;
        }
        for (size_t i = 0; i + text.size() <= bytes.size(); ++i) {
            bool match = true;
            for (size_t j = 0; j < text.size(); ++j) {
                if (std::to_integer<uint8_t>(bytes[i + j])
                    != static_cast<uint8_t>(text[j])) {
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

    static C2paPayloadClass classify_c2pa_jumbf_payload(
        std::span<const std::byte> logical_payload) noexcept
    {
        uint32_t header_len = 0U;
        uint32_t type       = 0U;
        if (!parse_bmff_box_header(logical_payload, &header_len, &type)) {
            return C2paPayloadClass::NotC2pa;
        }
        if (type == fourcc('c', '2', 'p', 'a')) {
            return is_openmeta_draft_c2pa_invalidation_payload(logical_payload)
                       ? C2paPayloadClass::DraftUnsignedInvalidation
                       : C2paPayloadClass::ContentBound;
        }
        if (type != fourcc('j', 'u', 'm', 'b')) {
            return C2paPayloadClass::NotC2pa;
        }

        size_t off = header_len;
        while (off < logical_payload.size()) {
            uint32_t child_header_len = 0U;
            uint32_t child_type       = 0U;
            if (!parse_bmff_box_header(logical_payload.subspan(off),
                                       &child_header_len, &child_type)) {
                return C2paPayloadClass::NotC2pa;
            }
            uint32_t child_size32 = 0U;
            if (!read_u32be(logical_payload, off + 0U, &child_size32)) {
                return C2paPayloadClass::NotC2pa;
            }
            uint64_t child_size = child_size32;
            if (child_size32 == 1U) {
                if (!read_u64be(logical_payload, off + 8U, &child_size)) {
                    return C2paPayloadClass::NotC2pa;
                }
            } else if (child_size32 == 0U) {
                child_size = logical_payload.size() - off;
            }
            if (child_size < child_header_len
                || child_size > logical_payload.size() - off) {
                return C2paPayloadClass::NotC2pa;
            }

            if (child_type == fourcc('j', 'u', 'm', 'd')) {
                const size_t payload_off = off + child_header_len;
                const size_t payload_len = static_cast<size_t>(
                    child_size - child_header_len);
                size_t label_len = 0U;
                while (label_len < payload_len
                       && logical_payload[payload_off + label_len]
                              != std::byte { 0x00 }) {
                    label_len += 1U;
                }
                const std::string_view label(reinterpret_cast<const char*>(
                                                 logical_payload.data()
                                                 + payload_off),
                                             label_len);
                if (label != "c2pa") {
                    return C2paPayloadClass::NotC2pa;
                }
                return is_openmeta_draft_c2pa_invalidation_payload(
                           logical_payload)
                           ? C2paPayloadClass::DraftUnsignedInvalidation
                           : C2paPayloadClass::ContentBound;
            }

            off += static_cast<size_t>(child_size);
        }
        return C2paPayloadClass::NotC2pa;
    }

    static bool
    is_c2pa_jumbf_payload(std::span<const std::byte> logical_payload) noexcept
    {
        return classify_c2pa_jumbf_payload(logical_payload)
               != C2paPayloadClass::NotC2pa;
    }

    static bool is_source_jumbf_block(const ContainerBlockRef& block) noexcept
    {
        if (block.kind == ContainerBlockKind::Jumbf) {
            return true;
        }
        return block.kind == ContainerBlockKind::CompressedMetadata
               && block.compression == BlockCompression::Brotli
               && (block.aux_u32 == fourcc('j', 'u', 'm', 'b')
                   || block.aux_u32 == fourcc('c', '2', 'p', 'a'));
    }

    static bool
    is_duplicate_jumbf_seed(std::span<const ContainerBlockRef> blocks,
                            uint32_t seed_index) noexcept
    {
        if (seed_index >= blocks.size()) {
            return true;
        }
        const ContainerBlockRef& seed = blocks[seed_index];
        if (!is_source_jumbf_block(seed) || seed.part_count <= 1U) {
            return false;
        }
        for (uint32_t i = 0U; i < seed_index; ++i) {
            const ContainerBlockRef& prev = blocks[i];
            if (!is_source_jumbf_block(prev)) {
                continue;
            }
            if (prev.format == seed.format && prev.kind == seed.kind
                && prev.id == seed.id && prev.group == seed.group
                && prev.part_count == seed.part_count) {
                return true;
            }
        }
        return false;
    }

    static void append_cbor_major_u64(std::vector<std::byte>* out,
                                      uint8_t major, uint64_t value) noexcept
    {
        if (!out) {
            return;
        }
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
        if (value <= 0xFFFFFFFFULL) {
            out->push_back(static_cast<std::byte>(prefix | 26U));
            out->push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
            out->push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
            out->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
            out->push_back(static_cast<std::byte>((value >> 0U) & 0xFFU));
            return;
        }
        out->push_back(static_cast<std::byte>(prefix | 27U));
        out->push_back(static_cast<std::byte>((value >> 56U) & 0xFFU));
        out->push_back(static_cast<std::byte>((value >> 48U) & 0xFFU));
        out->push_back(static_cast<std::byte>((value >> 40U) & 0xFFU));
        out->push_back(static_cast<std::byte>((value >> 32U) & 0xFFU));
        out->push_back(static_cast<std::byte>((value >> 24U) & 0xFFU));
        out->push_back(static_cast<std::byte>((value >> 16U) & 0xFFU));
        out->push_back(static_cast<std::byte>((value >> 8U) & 0xFFU));
        out->push_back(static_cast<std::byte>((value >> 0U) & 0xFFU));
    }

    static void append_cbor_text(std::vector<std::byte>* out,
                                 std::string_view text) noexcept
    {
        if (!out) {
            return;
        }
        append_cbor_major_u64(out, 3U, static_cast<uint64_t>(text.size()));
        for (size_t i = 0; i < text.size(); ++i) {
            out->push_back(
                static_cast<std::byte>(static_cast<uint8_t>(text[i])));
        }
    }

    static void
    append_bmff_box_bytes(std::vector<std::byte>* out, uint32_t type,
                          std::span<const std::byte> payload) noexcept
    {
        if (!out) {
            return;
        }
        append_u32be(out, static_cast<uint32_t>(8U + payload.size()));
        append_u32be(out, type);
        out->insert(out->end(), payload.begin(), payload.end());
    }

    static bool is_openmeta_draft_c2pa_invalidation_payload(
        std::span<const std::byte> bytes) noexcept
    {
        return payload_contains_ascii(bytes, "openmeta:c2pa_invalidation")
               || payload_contains_ascii(bytes, "openmeta:c2pa_contract");
    }

    static std::vector<std::byte> build_draft_c2pa_invalidation_payload()
    {
        std::vector<std::byte> cbor;
        cbor.reserve(160U);
        append_cbor_major_u64(&cbor, 5U, 5U);
        append_cbor_text(&cbor, "openmeta:c2pa_contract");
        append_cbor_text(&cbor, "draft_invalidation");
        append_cbor_text(&cbor, "openmeta:contract_version");
        append_cbor_major_u64(&cbor, 0U, 1U);
        append_cbor_text(&cbor, "openmeta:c2pa_invalidation");
        cbor.push_back(std::byte { 0xF5 });
        append_cbor_text(&cbor, "openmeta:reason");
        append_cbor_text(&cbor, "content_changed");
        append_cbor_text(&cbor, "openmeta:mode");
        append_cbor_text(&cbor, "unsigned_draft");

        std::vector<std::byte> jumd_payload;
        append_ascii_bytes(&jumd_payload, "c2pa");
        jumd_payload.push_back(std::byte { 0x00 });

        std::vector<std::byte> jumd_box;
        append_bmff_box_bytes(&jumd_box, fourcc('j', 'u', 'm', 'd'),
                              std::span<const std::byte>(jumd_payload.data(),
                                                         jumd_payload.size()));

        std::vector<std::byte> cbor_box;
        append_bmff_box_bytes(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                              std::span<const std::byte>(cbor.data(),
                                                         cbor.size()));

        std::vector<std::byte> jumb_payload;
        jumb_payload.reserve(jumd_box.size() + cbor_box.size());
        jumb_payload.insert(jumb_payload.end(), jumd_box.begin(),
                            jumd_box.end());
        jumb_payload.insert(jumb_payload.end(), cbor_box.begin(),
                            cbor_box.end());

        std::vector<std::byte> jumb_box;
        append_bmff_box_bytes(&jumb_box, fourcc('j', 'u', 'm', 'b'),
                              std::span<const std::byte>(jumb_payload.data(),
                                                         jumb_payload.size()));
        return jumb_box;
    }

    static void append_cbor_bytes(std::vector<std::byte>* out,
                                  std::span<const std::byte> bytes) noexcept
    {
        if (!out) {
            return;
        }
        append_cbor_major_u64(out, 2U, static_cast<uint64_t>(bytes.size()));
        out->insert(out->end(), bytes.begin(), bytes.end());
    }

    static void append_cbor_f32_bits(std::vector<std::byte>* out,
                                     uint32_t bits) noexcept
    {
        if (!out) {
            return;
        }
        out->push_back(std::byte { 0xFA });
        out->push_back(static_cast<std::byte>((bits >> 24U) & 0xFFU));
        out->push_back(static_cast<std::byte>((bits >> 16U) & 0xFFU));
        out->push_back(static_cast<std::byte>((bits >> 8U) & 0xFFU));
        out->push_back(static_cast<std::byte>((bits >> 0U) & 0xFFU));
    }

    static void append_cbor_f64_bits(std::vector<std::byte>* out,
                                     uint64_t bits) noexcept
    {
        if (!out) {
            return;
        }
        out->push_back(std::byte { 0xFB });
        out->push_back(static_cast<std::byte>((bits >> 56U) & 0xFFU));
        out->push_back(static_cast<std::byte>((bits >> 48U) & 0xFFU));
        out->push_back(static_cast<std::byte>((bits >> 40U) & 0xFFU));
        out->push_back(static_cast<std::byte>((bits >> 32U) & 0xFFU));
        out->push_back(static_cast<std::byte>((bits >> 24U) & 0xFFU));
        out->push_back(static_cast<std::byte>((bits >> 16U) & 0xFFU));
        out->push_back(static_cast<std::byte>((bits >> 8U) & 0xFFU));
        out->push_back(static_cast<std::byte>((bits >> 0U) & 0xFFU));
    }

    static bool meta_scalar_to_u64(const MetaValue& value,
                                   uint64_t* out) noexcept
    {
        if (!out || value.kind != MetaValueKind::Scalar) {
            return false;
        }
        switch (value.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64: *out = value.data.u64; return true;
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64:
            if (value.data.i64 < 0) {
                return false;
            }
            *out = static_cast<uint64_t>(value.data.i64);
            return true;
        default: return false;
        }
    }

    static bool supported_projected_text_encoding(const MetaValue& value,
                                                  std::string_view text) noexcept
    {
        if (value.kind != MetaValueKind::Text) {
            return false;
        }
        switch (value.text_encoding) {
        case TextEncoding::Ascii: return true;
        case TextEncoding::Utf8: return true;
        case TextEncoding::Unknown:
            for (size_t i = 0; i < text.size(); ++i) {
                if (static_cast<unsigned char>(text[i]) > 0x7FU) {
                    return false;
                }
            }
            return true;
        case TextEncoding::Utf16LE:
        case TextEncoding::Utf16BE: return false;
        }
        return false;
    }

    static bool looks_like_projected_simple_text(std::string_view text) noexcept
    {
        if (!starts_with(text, "simple(") || text.size() < 9U
            || text.back() != ')') {
            return false;
        }
        return decimal_text(text.substr(7U, text.size() - 8U));
    }

    static bool
    looks_like_projected_large_negative_text(std::string_view text) noexcept
    {
        if (!starts_with(text, "-(1+") || text.size() < 7U
            || text.back() != ')') {
            return false;
        }
        return decimal_text(text.substr(4U, text.size() - 5U));
    }

    static uint32_t append_projected_cbor_node(ProjectedCborTree* tree) noexcept
    {
        if (!tree) {
            return 0U;
        }
        tree->nodes.push_back(ProjectedCborNode());
        return static_cast<uint32_t>(tree->nodes.size() - 1U);
    }

    static bool assign_projected_cbor_tag(ProjectedCborTree* tree,
                                          uint32_t node_index,
                                          const Entry& entry,
                                          std::string* out_error) noexcept
    {
        if (!tree || node_index >= tree->nodes.size()) {
            if (out_error) {
                *out_error = "projected jumbf cbor tag target is invalid";
            }
            return false;
        }
        uint64_t tag = 0U;
        if (!meta_scalar_to_u64(entry.value, &tag)) {
            if (out_error) {
                *out_error
                    = "projected jumbf cbor tag is not a scalar unsigned integer";
            }
            return false;
        }
        ProjectedCborNode& node = tree->nodes[node_index];
        if (node.has_tag && node.tag != tag) {
            if (out_error) {
                *out_error = "projected jumbf cbor tag is duplicated";
            }
            return false;
        }
        node.has_tag = true;
        node.tag     = tag;
        return true;
    }

    static bool assign_projected_cbor_leaf(ProjectedCborTree* tree,
                                           uint32_t node_index,
                                           const Entry& entry,
                                           std::string* out_error) noexcept
    {
        if (!tree || node_index >= tree->nodes.size()) {
            if (out_error) {
                *out_error = "projected jumbf cbor leaf target is invalid";
            }
            return false;
        }
        ProjectedCborNode& node = tree->nodes[node_index];
        if (!node.children.empty()) {
            if (out_error) {
                *out_error
                    = "projected jumbf cbor node cannot be both container and scalar";
            }
            return false;
        }
        if (node.leaf && node.leaf != &entry) {
            if (out_error) {
                *out_error = "projected jumbf cbor leaf is duplicated";
            }
            return false;
        }
        node.kind = ProjectedCborNodeKind::Leaf;
        node.leaf = &entry;
        return true;
    }

    static bool find_or_add_projected_map_child(ProjectedCborTree* tree,
                                                uint32_t parent_index,
                                                std::string_view map_key,
                                                uint32_t* out_child_index,
                                                std::string* out_error) noexcept
    {
        if (!tree || !out_child_index || parent_index >= tree->nodes.size()) {
            if (out_error) {
                *out_error = "projected jumbf cbor map parent is invalid";
            }
            return false;
        }
        if (map_key.empty() || map_key == "@tag") {
            if (out_error) {
                *out_error = "projected jumbf cbor map key is empty";
            }
            return false;
        }
        if (decimal_text(map_key)) {
            if (out_error) {
                *out_error
                    = "projected jumbf cbor numeric map keys are ambiguous";
            }
            return false;
        }

        ProjectedCborNode& parent = tree->nodes[parent_index];
        if (parent.kind == ProjectedCborNodeKind::Leaf || parent.leaf) {
            if (out_error) {
                *out_error
                    = "projected jumbf cbor scalar node cannot gain children";
            }
            return false;
        }
        if (parent.kind == ProjectedCborNodeKind::Unknown) {
            parent.kind = ProjectedCborNodeKind::Map;
        } else if (parent.kind != ProjectedCborNodeKind::Map) {
            if (out_error) {
                *out_error = "projected jumbf cbor container kind mismatch";
            }
            return false;
        }

        for (size_t i = 0; i < parent.children.size(); ++i) {
            if (!parent.children[i].array_child
                && parent.children[i].map_key == map_key) {
                *out_child_index = parent.children[i].node_index;
                return true;
            }
        }

        const uint32_t child_index = append_projected_cbor_node(tree);
        ProjectedCborChild child;
        child.node_index  = child_index;
        child.array_child = false;
        child.map_key.assign(map_key.data(), map_key.size());
        tree->nodes[parent_index].children.push_back(std::move(child));
        *out_child_index = child_index;
        return true;
    }

    static bool find_or_add_projected_array_child(
        ProjectedCborTree* tree, uint32_t parent_index, uint32_t array_index,
        uint32_t* out_child_index, std::string* out_error) noexcept
    {
        if (!tree || !out_child_index || parent_index >= tree->nodes.size()) {
            if (out_error) {
                *out_error = "projected jumbf cbor array parent is invalid";
            }
            return false;
        }

        ProjectedCborNode& parent = tree->nodes[parent_index];
        if (parent.kind == ProjectedCborNodeKind::Leaf || parent.leaf) {
            if (out_error) {
                *out_error
                    = "projected jumbf cbor scalar node cannot gain children";
            }
            return false;
        }
        if (parent.kind == ProjectedCborNodeKind::Unknown) {
            parent.kind = ProjectedCborNodeKind::Array;
        } else if (parent.kind != ProjectedCborNodeKind::Array) {
            if (out_error) {
                *out_error = "projected jumbf cbor container kind mismatch";
            }
            return false;
        }

        size_t insert_at = parent.children.size();
        for (size_t i = 0; i < parent.children.size(); ++i) {
            if (!parent.children[i].array_child) {
                continue;
            }
            if (parent.children[i].array_index == array_index) {
                *out_child_index = parent.children[i].node_index;
                return true;
            }
            if (parent.children[i].array_index > array_index) {
                insert_at = i;
                break;
            }
        }

        const uint32_t child_index = append_projected_cbor_node(tree);
        ProjectedCborChild child;
        child.node_index  = child_index;
        child.array_child = true;
        child.array_index = array_index;
        tree->nodes[parent_index].children.insert(
            tree->nodes[parent_index].children.begin()
                + static_cast<std::ptrdiff_t>(insert_at),
            std::move(child));
        *out_child_index = child_index;
        return true;
    }

    static bool build_projected_cbor_tree(const MetaStore& store,
                                          std::string_view root_prefix,
                                          ProjectedCborTree* out_tree,
                                          std::string* out_error) noexcept
    {
        if (!out_tree || root_prefix.empty()) {
            if (out_error) {
                *out_error = "projected jumbf cbor root is invalid";
            }
            return false;
        }

        out_tree->root_prefix.assign(root_prefix.data(), root_prefix.size());
        out_tree->nodes.clear();
        out_tree->nodes.push_back(ProjectedCborNode());

        static constexpr uint32_t kMaxProjectedDepth = 64U;
        bool matched_any                             = false;

        for (const Entry& e : store.entries()) {
            if (any(e.flags, EntryFlags::Deleted)
                || e.key.kind != MetaKeyKind::JumbfCborKey) {
                continue;
            }
            const std::string_view full_key
                = arena_string(store.arena(), e.key.data.jumbf_cbor_key.key);
            if (contains_path_segment(full_key, "c2pa")) {
                continue;
            }
            if (!starts_with(full_key, root_prefix)) {
                continue;
            }

            const std::string_view relative = full_key.substr(
                root_prefix.size());
            matched_any           = true;
            uint32_t current_node = 0U;
            uint32_t depth        = 0U;
            bool tag_only         = false;
            size_t pos            = 0U;

            while (pos < relative.size()) {
                depth += 1U;
                if (depth > kMaxProjectedDepth) {
                    if (out_error) {
                        *out_error
                            = "projected jumbf cbor nesting exceeds transfer limits";
                    }
                    return false;
                }
                if (relative[pos] == '.') {
                    pos += 1U;
                    if (pos >= relative.size()) {
                        if (out_error) {
                            *out_error
                                = "projected jumbf cbor path has a trailing separator";
                        }
                        return false;
                    }
                    if (relative.substr(pos) == "@tag") {
                        tag_only = true;
                        pos      = relative.size();
                        break;
                    }
                    size_t end = pos;
                    while (end < relative.size() && relative[end] != '.'
                           && relative[end] != '[') {
                        end += 1U;
                    }
                    const std::string_view map_key = relative.substr(pos,
                                                                     end - pos);
                    if (!find_or_add_projected_map_child(out_tree, current_node,
                                                         map_key, &current_node,
                                                         out_error)) {
                        return false;
                    }
                    pos = end;
                    continue;
                }
                if (relative[pos] == '[') {
                    pos += 1U;
                    const size_t index_begin = pos;
                    while (pos < relative.size() && relative[pos] != ']') {
                        pos += 1U;
                    }
                    if (pos >= relative.size() || pos == index_begin) {
                        if (out_error) {
                            *out_error
                                = "projected jumbf cbor array index is malformed";
                        }
                        return false;
                    }
                    uint32_t array_index = 0U;
                    if (!parse_u32_decimal(relative.substr(index_begin,
                                                           pos - index_begin),
                                           &array_index)) {
                        if (out_error) {
                            *out_error
                                = "projected jumbf cbor array index is invalid";
                        }
                        return false;
                    }
                    if (!find_or_add_projected_array_child(
                            out_tree, current_node, array_index, &current_node,
                            out_error)) {
                        return false;
                    }
                    pos += 1U;
                    continue;
                }
                if (out_error) {
                    *out_error
                        = "projected jumbf cbor path has unsupported syntax";
                }
                return false;
            }

            if (tag_only) {
                if (!assign_projected_cbor_tag(out_tree, current_node, e,
                                               out_error)) {
                    return false;
                }
                continue;
            }

            if (!assign_projected_cbor_leaf(out_tree, current_node, e,
                                            out_error)) {
                return false;
            }
        }

        if (!matched_any) {
            if (out_error) {
                *out_error = "projected jumbf cbor root has no entries";
            }
            return false;
        }
        return true;
    }

    static bool emit_projected_cbor_leaf(const MetaStore& store,
                                         const Entry& entry,
                                         std::vector<std::byte>* out,
                                         std::string* out_error) noexcept
    {
        if (!out) {
            if (out_error) {
                *out_error = "projected jumbf cbor output is null";
            }
            return false;
        }

        const MetaValue& value = entry.value;
        if (value.kind == MetaValueKind::Scalar) {
            switch (value.elem_type) {
            case MetaElementType::U8:
                if (out_error) {
                    *out_error
                        = "projected jumbf cbor U8 scalars are ambiguous "
                          "(decoded bool/simple vs integer)";
                }
                return false;
            case MetaElementType::U16:
            case MetaElementType::U32:
            case MetaElementType::U64:
                append_cbor_major_u64(out, 0U, value.data.u64);
                return true;
            case MetaElementType::I8:
            case MetaElementType::I16:
            case MetaElementType::I32:
            case MetaElementType::I64:
                if (value.data.i64 >= 0) {
                    append_cbor_major_u64(out, 0U,
                                          static_cast<uint64_t>(value.data.i64));
                } else {
                    const uint64_t cbor_negative = static_cast<uint64_t>(
                        -(value.data.i64 + 1));
                    append_cbor_major_u64(out, 1U, cbor_negative);
                }
                return true;
            case MetaElementType::F32:
                append_cbor_f32_bits(out, value.data.f32_bits);
                return true;
            case MetaElementType::F64:
                append_cbor_f64_bits(out, value.data.f64_bits);
                return true;
            case MetaElementType::URational:
            case MetaElementType::SRational: break;
            }
        } else if (value.kind == MetaValueKind::Bytes) {
            const std::span<const std::byte> bytes = store.arena().span(
                value.data.span);
            append_cbor_bytes(out, bytes);
            return true;
        } else if (value.kind == MetaValueKind::Text) {
            const std::span<const std::byte> bytes = store.arena().span(
                value.data.span);
            const std::string_view text(reinterpret_cast<const char*>(
                                            bytes.data()),
                                        bytes.size());
            if (!supported_projected_text_encoding(value, text)) {
                if (out_error) {
                    *out_error = "projected jumbf cbor text is not ASCII/UTF-8";
                }
                return false;
            }
            if (text == "null" || text == "undefined") {
                if (out_error) {
                    *out_error
                        = "projected jumbf cbor sentinel text is ambiguous "
                          "(decoded null/undefined vs string)";
                }
                return false;
            }
            if (looks_like_projected_simple_text(text)) {
                if (out_error) {
                    *out_error
                        = "projected jumbf cbor simple-value text is ambiguous";
                }
                return false;
            }
            if (looks_like_projected_large_negative_text(text)) {
                if (out_error) {
                    *out_error
                        = "projected jumbf cbor large-negative fallback text "
                          "is ambiguous";
                }
                return false;
            }
            append_cbor_text(out, text);
            return true;
        }

        if (out_error) {
            *out_error = "projected jumbf cbor value kind is not serializable";
        }
        return false;
    }

    static bool emit_projected_cbor_node(const MetaStore& store,
                                         const ProjectedCborTree& tree,
                                         uint32_t node_index, uint32_t depth,
                                         std::vector<std::byte>* out,
                                         std::string* out_error) noexcept
    {
        if (!out || node_index >= tree.nodes.size()) {
            if (out_error) {
                *out_error = "projected jumbf cbor node is invalid";
            }
            return false;
        }
        if (depth > 64U) {
            if (out_error) {
                *out_error
                    = "projected jumbf cbor nesting exceeds transfer limits";
            }
            return false;
        }

        const ProjectedCborNode& node = tree.nodes[node_index];
        if (node.has_tag) {
            append_cbor_major_u64(out, 6U, node.tag);
        }

        switch (node.kind) {
        case ProjectedCborNodeKind::Leaf:
            if (!node.leaf) {
                if (out_error) {
                    *out_error = "projected jumbf cbor leaf is missing";
                }
                return false;
            }
            return emit_projected_cbor_leaf(store, *node.leaf, out, out_error);
        case ProjectedCborNodeKind::Map:
            append_cbor_major_u64(out, 5U,
                                  static_cast<uint64_t>(node.children.size()));
            for (size_t i = 0; i < node.children.size(); ++i) {
                if (node.children[i].array_child) {
                    if (out_error) {
                        *out_error
                            = "projected jumbf cbor map child kind mismatch";
                    }
                    return false;
                }
                append_cbor_text(out, node.children[i].map_key);
                if (!emit_projected_cbor_node(store, tree,
                                              node.children[i].node_index,
                                              depth + 1U, out, out_error)) {
                    return false;
                }
            }
            return true;
        case ProjectedCborNodeKind::Array:
            append_cbor_major_u64(out, 4U,
                                  static_cast<uint64_t>(node.children.size()));
            for (size_t i = 0; i < node.children.size(); ++i) {
                if (!node.children[i].array_child
                    || node.children[i].array_index != i) {
                    if (out_error) {
                        *out_error
                            = "projected jumbf cbor array indices are sparse";
                    }
                    return false;
                }
                if (!emit_projected_cbor_node(store, tree,
                                              node.children[i].node_index,
                                              depth + 1U, out, out_error)) {
                    return false;
                }
            }
            return true;
        case ProjectedCborNodeKind::Unknown:
            if (out_error) {
                *out_error = "projected jumbf cbor root is empty";
            }
            return false;
        }
        if (out_error) {
            *out_error = "projected jumbf cbor node kind is unsupported";
        }
        return false;
    }

    static std::string build_projected_jumbf_label(std::string_view root_prefix)
    {
        std::string label("openmeta.projected.");
        label.append(root_prefix.data(), root_prefix.size());
        return label;
    }

    static bool build_projected_jumbf_logical_payload_for_root(
        const MetaStore& store, std::string_view root_prefix,
        std::vector<std::byte>* out_payload, std::string* out_error) noexcept
    {
        if (!out_payload || root_prefix.empty()) {
            if (out_error) {
                *out_error = "projected jumbf logical payload target is invalid";
            }
            return false;
        }

        ProjectedCborTree tree;
        if (!build_projected_cbor_tree(store, root_prefix, &tree, out_error)) {
            return false;
        }

        std::vector<std::byte> cbor_payload;
        if (!emit_projected_cbor_node(store, tree, 0U, 0U, &cbor_payload,
                                      out_error)) {
            return false;
        }

        const std::string label = build_projected_jumbf_label(root_prefix);
        std::vector<std::byte> jumd_payload;
        append_ascii_bytes(&jumd_payload, label);
        jumd_payload.push_back(std::byte { 0x00 });

        std::vector<std::byte> jumd_box;
        append_bmff_box_bytes(&jumd_box, fourcc('j', 'u', 'm', 'd'),
                              std::span<const std::byte>(jumd_payload.data(),
                                                         jumd_payload.size()));

        std::vector<std::byte> cbor_box;
        append_bmff_box_bytes(&cbor_box, fourcc('c', 'b', 'o', 'r'),
                              std::span<const std::byte>(cbor_payload.data(),
                                                         cbor_payload.size()));

        std::vector<std::byte> jumb_payload;
        jumb_payload.reserve(jumd_box.size() + cbor_box.size());
        jumb_payload.insert(jumb_payload.end(), jumd_box.begin(),
                            jumd_box.end());
        jumb_payload.insert(jumb_payload.end(), cbor_box.begin(),
                            cbor_box.end());

        out_payload->clear();
        append_bmff_box_bytes(out_payload, fourcc('j', 'u', 'm', 'b'),
                              std::span<const std::byte>(jumb_payload.data(),
                                                         jumb_payload.size()));
        return true;
    }

    static bool build_projected_jumbf_logical_payloads(
        const MetaStore& store,
        std::vector<ProjectedJumbfPayload>* out_payloads,
        std::string* out_error) noexcept
    {
        if (!out_payloads) {
            if (out_error) {
                *out_error = "projected jumbf logical payload output is null";
            }
            return false;
        }

        std::vector<std::string> roots;
        for (const Entry& e : store.entries()) {
            if (any(e.flags, EntryFlags::Deleted)
                || e.key.kind != MetaKeyKind::JumbfCborKey) {
                continue;
            }
            const std::string_view full_key
                = arena_string(store.arena(), e.key.data.jumbf_cbor_key.key);
            if (contains_path_segment(full_key, "c2pa")) {
                continue;
            }
            std::string_view root;
            std::string_view suffix;
            if (!find_jumbf_cbor_root_prefix(full_key, &root, &suffix)) {
                if (out_error) {
                    *out_error
                        = "projected jumbf cbor key does not contain a .cbor root";
                }
                return false;
            }
            append_unique_string(&roots, root);
        }

        if (roots.empty()) {
            if (out_error) {
                *out_error = "no non-c2pa jumbf cbor keys are available";
            }
            return false;
        }

        out_payloads->clear();
        out_payloads->reserve(roots.size());
        for (size_t i = 0; i < roots.size(); ++i) {
            ProjectedJumbfPayload one;
            one.root_prefix = roots[i];
            if (!build_projected_jumbf_logical_payload_for_root(
                    store, roots[i], &one.logical_payload, out_error)) {
                return false;
            }
            out_payloads->push_back(std::move(one));
        }
        return true;
    }

    static bool append_jpeg_app11_jumbf_segments(
        std::span<const std::byte> logical_payload, TransferBlockKind kind,
        uint32_t* io_order, std::vector<PreparedTransferBlock>* out_blocks,
        std::string* out_error) noexcept
    {
        if (!io_order || !out_blocks) {
            if (out_error) {
                *out_error = "jpeg app11 jumbf output is null";
            }
            return false;
        }

        uint32_t header_len = 0U;
        uint32_t box_type   = 0U;
        if (!parse_bmff_box_header(logical_payload, &header_len, &box_type)) {
            if (out_error) {
                *out_error
                    = "jumbf payload does not start with a valid bmff box";
            }
            return false;
        }

        static constexpr uint32_t kMaxJpegSegmentPayload = 65533U;
        const uint32_t fixed_overhead                    = 8U + header_len;
        if (fixed_overhead > kMaxJpegSegmentPayload) {
            if (out_error) {
                *out_error = "jumbf app11 overhead exceeds jpeg segment limits";
            }
            return false;
        }
        const uint32_t max_chunk = kMaxJpegSegmentPayload - fixed_overhead;
        const std::span<const std::byte> header
            = logical_payload.subspan(0U, header_len);
        const std::span<const std::byte> body = logical_payload.subspan(
            header_len);

        uint64_t body_off = 0U;
        uint32_t seq      = 1U;
        do {
            const uint32_t chunk = static_cast<uint32_t>(
                std::min<uint64_t>(max_chunk, body.size() - body_off));

            PreparedTransferBlock block;
            block.kind  = kind;
            block.order = *io_order;
            *io_order += 1U;
            block.route    = (kind == TransferBlockKind::C2pa)
                                 ? "jpeg:app11-c2pa"
                                 : "jpeg:app11-jumbf";
            block.box_type = {
                static_cast<char>((box_type >> 24U) & 0xFFU),
                static_cast<char>((box_type >> 16U) & 0xFFU),
                static_cast<char>((box_type >> 8U) & 0xFFU),
                static_cast<char>((box_type >> 0U) & 0xFFU),
            };
            append_ascii_bytes(&block.payload, "JP");
            block.payload.push_back(std::byte { 0x00 });
            block.payload.push_back(std::byte { 0x00 });
            append_u32be(&block.payload, seq);
            block.payload.insert(block.payload.end(), header.begin(),
                                 header.end());
            block.payload.insert(
                block.payload.end(),
                body.begin() + static_cast<std::ptrdiff_t>(body_off),
                body.begin() + static_cast<std::ptrdiff_t>(body_off + chunk));
            out_blocks->push_back(std::move(block));
            body_off += chunk;
            seq += 1U;
        } while (body_off < body.size() || (body.empty() && seq == 2U));

        return true;
    }

    static bool
    append_jxl_jumbf_box(std::span<const std::byte> logical_payload,
                         TransferBlockKind kind, uint32_t* io_order,
                         std::vector<PreparedTransferBlock>* out_blocks,
                         std::string* out_error) noexcept
    {
        if (!io_order || !out_blocks) {
            if (out_error) {
                *out_error = "jxl jumbf output is null";
            }
            return false;
        }

        uint32_t header_len = 0U;
        uint32_t box_type   = 0U;
        if (!parse_bmff_box_header(logical_payload, &header_len, &box_type)) {
            if (out_error) {
                *out_error
                    = "jumbf payload does not start with a valid bmff box";
            }
            return false;
        }
        if (box_type != fourcc('j', 'u', 'm', 'b')
            && box_type != fourcc('c', '2', 'p', 'a')) {
            if (out_error) {
                *out_error = "jxl transfer only supports jumb/c2pa root boxes";
            }
            return false;
        }

        PreparedTransferBlock block;
        block.kind  = kind;
        block.order = *io_order;
        *io_order += 1U;
        block.box_type = {
            static_cast<char>((box_type >> 24U) & 0xFFU),
            static_cast<char>((box_type >> 16U) & 0xFFU),
            static_cast<char>((box_type >> 8U) & 0xFFU),
            static_cast<char>((box_type >> 0U) & 0xFFU),
        };
        block.route = box_type == fourcc('c', '2', 'p', 'a') ? "jxl:box-c2pa"
                                                             : "jxl:box-jumb";
        block.payload.assign(logical_payload.begin()
                                 + static_cast<std::ptrdiff_t>(header_len),
                             logical_payload.end());
        out_blocks->push_back(std::move(block));
        return true;
    }

    static bool
    append_webp_c2pa_chunk(std::span<const std::byte> logical_payload,
                           uint32_t* io_order,
                           std::vector<PreparedTransferBlock>* out_blocks,
                           std::string* out_error) noexcept
    {
        if (!io_order || !out_blocks) {
            if (out_error) {
                *out_error = "webp c2pa output is null";
            }
            return false;
        }
        if (logical_payload.empty()) {
            if (out_error) {
                *out_error = "webp c2pa payload is empty";
            }
            return false;
        }
        PreparedTransferBlock block;
        block.kind  = TransferBlockKind::C2pa;
        block.order = *io_order;
        *io_order += 1U;
        block.route = "webp:chunk-c2pa";
        block.payload.assign(logical_payload.begin(), logical_payload.end());
        out_blocks->push_back(std::move(block));
        return true;
    }

    static bool
    append_bmff_metadata_item(std::span<const std::byte> logical_payload,
                              std::string_view route, TransferBlockKind kind,
                              uint32_t* io_order,
                              std::vector<PreparedTransferBlock>* out_blocks,
                              std::string* out_error) noexcept
    {
        if (!io_order || !out_blocks) {
            if (out_error) {
                *out_error = "bmff metadata output is null";
            }
            return false;
        }
        if (logical_payload.empty()) {
            if (out_error) {
                *out_error = "bmff metadata payload is empty";
            }
            return false;
        }

        uint32_t header_len = 0U;
        uint32_t box_type   = 0U;
        if (!parse_bmff_box_header(logical_payload, &header_len, &box_type)) {
            if (out_error) {
                *out_error
                    = "bmff metadata payload does not start with a valid bmff box";
            }
            return false;
        }

        if (route == "bmff:item-jumb") {
            if (box_type != fourcc('j', 'u', 'm', 'b')) {
                if (out_error) {
                    *out_error
                        = "bmff jumb transfer only supports jumb root boxes";
                }
                return false;
            }
        } else if (route == "bmff:item-c2pa") {
            if (box_type != fourcc('j', 'u', 'm', 'b')
                && box_type != fourcc('c', '2', 'p', 'a')) {
                if (out_error) {
                    *out_error
                        = "bmff c2pa transfer only supports jumb/c2pa root boxes";
                }
                return false;
            }
        }

        PreparedTransferBlock block;
        block.kind  = kind;
        block.order = *io_order;
        *io_order += 1U;
        block.route = std::string(route);
        block.payload.assign(logical_payload.begin(), logical_payload.end());
        out_blocks->push_back(std::move(block));
        return true;
    }

    static C2paPayloadClass classify_source_c2pa_payloads_for_jpeg(
        std::span<const std::byte> file_bytes,
        std::span<const ContainerBlockRef> blocks,
        const PayloadOptions& options) noexcept
    {
        std::vector<std::byte> payload(1024U * 1024U);
        std::vector<uint32_t> scratch(16384U);
        bool saw_draft = false;

        for (uint32_t i = 0U; i < blocks.size(); ++i) {
            const ContainerBlockRef& block = blocks[i];
            if (!is_source_jumbf_block(block)
                || is_duplicate_jumbf_seed(blocks, i)) {
                continue;
            }

            PayloadResult extracted;
            for (;;) {
                extracted = extract_payload(
                    file_bytes, blocks, i,
                    std::span<std::byte>(payload.data(), payload.size()),
                    std::span<uint32_t>(scratch.data(), scratch.size()),
                    options);
                if (extracted.status == PayloadStatus::OutputTruncated
                    && extracted.needed > payload.size()
                    && extracted.needed <= static_cast<uint64_t>(SIZE_MAX)) {
                    payload.resize(static_cast<size_t>(extracted.needed));
                    continue;
                }
                break;
            }

            if (extracted.status != PayloadStatus::Ok
                || extracted.written == 0U) {
                continue;
            }

            const C2paPayloadClass payload_class = classify_c2pa_jumbf_payload(
                std::span<const std::byte>(
                    payload.data(), static_cast<size_t>(extracted.written)));
            if (payload_class == C2paPayloadClass::ContentBound) {
                return payload_class;
            }
            if (payload_class == C2paPayloadClass::DraftUnsignedInvalidation) {
                saw_draft = true;
            }
        }

        return saw_draft ? C2paPayloadClass::DraftUnsignedInvalidation
                         : C2paPayloadClass::NotC2pa;
    }

    static SourceJumbfAppendResult append_source_jumbf_blocks_for_jpeg(
        std::span<const std::byte> file_bytes,
        std::span<const ContainerBlockRef> blocks,
        const PayloadOptions& options, PreparedTransferBundle* bundle) noexcept
    {
        SourceJumbfAppendResult out;
        if (!bundle) {
            out.errors  = 1U;
            out.message = "bundle is null";
            return out;
        }

        PreparedTransferPolicyDecision* jumbf_policy
            = find_policy_decision(bundle, TransferPolicySubject::Jumbf);
        PreparedTransferPolicyDecision* c2pa_policy
            = find_policy_decision(bundle, TransferPolicySubject::C2pa);
        const bool keep_jumbf = jumbf_policy
                                && jumbf_policy->effective
                                       == TransferPolicyAction::Keep;
        const bool keep_c2pa
            = c2pa_policy
              && c2pa_policy->effective == TransferPolicyAction::Keep
              && c2pa_policy->c2pa_mode == TransferC2paMode::PreserveRaw;
        if (!keep_jumbf && !keep_c2pa) {
            return out;
        }

        uint32_t order = 140U;
        for (size_t i = 0; i < bundle->blocks.size(); ++i) {
            if (bundle->blocks[i].order >= order) {
                order = bundle->blocks[i].order + 1U;
            }
        }

        std::vector<std::byte> payload(1024U * 1024U);
        std::vector<uint32_t> scratch(16384U);

        for (uint32_t i = 0U; i < blocks.size(); ++i) {
            const ContainerBlockRef& block = blocks[i];
            if (!is_source_jumbf_block(block)
                || is_duplicate_jumbf_seed(blocks, i)) {
                continue;
            }

            PayloadResult extracted;
            std::span<const std::byte> logical;
            for (;;) {
                extracted = extract_payload(
                    file_bytes, blocks, i,
                    std::span<std::byte>(payload.data(), payload.size()),
                    std::span<uint32_t>(scratch.data(), scratch.size()),
                    options);
                if (extracted.status == PayloadStatus::OutputTruncated
                    && extracted.needed > payload.size()
                    && extracted.needed <= static_cast<uint64_t>(SIZE_MAX)) {
                    payload.resize(static_cast<size_t>(extracted.needed));
                    continue;
                }
                break;
            }

            if (extracted.status != PayloadStatus::Ok) {
                out.errors += 1U;
                append_message(&out.message,
                               "failed to extract source jumbf payload");
                continue;
            }

            logical = std::span<const std::byte>(payload.data(),
                                                 static_cast<size_t>(
                                                     extracted.written));
            if (logical.empty()) {
                continue;
            }

            const C2paPayloadClass payload_class = classify_c2pa_jumbf_payload(
                logical);
            if (payload_class == C2paPayloadClass::DraftUnsignedInvalidation) {
                if (!keep_c2pa) {
                    continue;
                }
                std::string error;
                if (!append_jpeg_app11_jumbf_segments(logical,
                                                      TransferBlockKind::C2pa,
                                                      &order, &bundle->blocks,
                                                      &error)) {
                    out.errors += 1U;
                    append_message(&out.message, error);
                    continue;
                }
                out.emitted_blocks += 1U;
                out.emitted_c2pa += 1U;
                continue;
            }
            if (payload_class == C2paPayloadClass::ContentBound
                || !keep_jumbf) {
                continue;
            }

            std::string error;
            if (!append_jpeg_app11_jumbf_segments(logical,
                                                  TransferBlockKind::Jumbf,
                                                  &order, &bundle->blocks,
                                                  &error)) {
                out.errors += 1U;
                append_message(&out.message, error);
                continue;
            }
            out.emitted_blocks += 1U;
            out.emitted_jumbf += 1U;
        }
        return out;
    }

    static SourceJumbfAppendResult append_source_jumbf_blocks_for_jxl(
        std::span<const std::byte> file_bytes,
        std::span<const ContainerBlockRef> blocks,
        const PayloadOptions& options, PreparedTransferBundle* bundle) noexcept
    {
        SourceJumbfAppendResult out;
        if (!bundle) {
            out.errors  = 1U;
            out.message = "bundle is null";
            return out;
        }

        PreparedTransferPolicyDecision* jumbf_policy
            = find_policy_decision(bundle, TransferPolicySubject::Jumbf);
        PreparedTransferPolicyDecision* c2pa_policy
            = find_policy_decision(bundle, TransferPolicySubject::C2pa);
        const bool keep_jumbf = jumbf_policy
                                && jumbf_policy->effective
                                       == TransferPolicyAction::Keep;
        const bool keep_c2pa
            = c2pa_policy
              && c2pa_policy->effective == TransferPolicyAction::Keep
              && c2pa_policy->c2pa_mode == TransferC2paMode::PreserveRaw;
        if (!keep_jumbf && !keep_c2pa) {
            return out;
        }

        uint32_t order = next_prepared_block_order(*bundle, 140U);
        std::vector<std::byte> payload(1024U * 1024U);
        std::vector<uint32_t> scratch(16384U);

        for (uint32_t i = 0U; i < blocks.size(); ++i) {
            const ContainerBlockRef& block = blocks[i];
            if (!is_source_jumbf_block(block)
                || is_duplicate_jumbf_seed(blocks, i)) {
                continue;
            }

            PayloadResult extracted;
            std::span<const std::byte> logical;
            for (;;) {
                extracted = extract_payload(
                    file_bytes, blocks, i,
                    std::span<std::byte>(payload.data(), payload.size()),
                    std::span<uint32_t>(scratch.data(), scratch.size()),
                    options);
                if (extracted.status == PayloadStatus::OutputTruncated
                    && extracted.needed > payload.size()
                    && extracted.needed <= static_cast<uint64_t>(SIZE_MAX)) {
                    payload.resize(static_cast<size_t>(extracted.needed));
                    continue;
                }
                break;
            }

            if (extracted.status != PayloadStatus::Ok) {
                out.errors += 1U;
                append_message(&out.message,
                               "failed to extract source jumbf payload");
                continue;
            }

            logical = std::span<const std::byte>(payload.data(),
                                                 static_cast<size_t>(
                                                     extracted.written));
            if (logical.empty()) {
                continue;
            }

            const C2paPayloadClass payload_class = classify_c2pa_jumbf_payload(
                logical);
            if (payload_class == C2paPayloadClass::DraftUnsignedInvalidation) {
                if (!keep_c2pa) {
                    continue;
                }
                std::string error;
                if (!append_jxl_jumbf_box(logical, TransferBlockKind::C2pa,
                                          &order, &bundle->blocks, &error)) {
                    out.errors += 1U;
                    append_message(&out.message, error);
                    continue;
                }
                out.emitted_blocks += 1U;
                out.emitted_c2pa += 1U;
                continue;
            }
            if (payload_class == C2paPayloadClass::ContentBound
                || !keep_jumbf) {
                continue;
            }

            std::string error;
            if (!append_jxl_jumbf_box(logical, TransferBlockKind::Jumbf, &order,
                                      &bundle->blocks, &error)) {
                out.errors += 1U;
                append_message(&out.message, error);
                continue;
            }
            out.emitted_blocks += 1U;
            out.emitted_jumbf += 1U;
        }
        return out;
    }

    static SourceJumbfAppendResult append_source_jumbf_blocks_for_bmff(
        std::span<const std::byte> file_bytes,
        std::span<const ContainerBlockRef> blocks,
        const PayloadOptions& options, PreparedTransferBundle* bundle) noexcept
    {
        SourceJumbfAppendResult out;
        if (!bundle) {
            out.errors  = 1U;
            out.message = "bundle is null";
            return out;
        }

        PreparedTransferPolicyDecision* jumbf_policy
            = find_policy_decision(bundle, TransferPolicySubject::Jumbf);
        PreparedTransferPolicyDecision* c2pa_policy
            = find_policy_decision(bundle, TransferPolicySubject::C2pa);
        const bool keep_jumbf = jumbf_policy
                                && jumbf_policy->effective
                                       == TransferPolicyAction::Keep;
        const bool keep_c2pa
            = c2pa_policy
              && c2pa_policy->effective == TransferPolicyAction::Keep
              && c2pa_policy->c2pa_mode == TransferC2paMode::PreserveRaw;
        if (!keep_jumbf && !keep_c2pa) {
            return out;
        }

        uint32_t order = next_prepared_block_order(*bundle, 140U);
        std::vector<std::byte> payload(1024U * 1024U);
        std::vector<uint32_t> scratch(16384U);

        for (uint32_t i = 0U; i < blocks.size(); ++i) {
            const ContainerBlockRef& block = blocks[i];
            if (!is_source_jumbf_block(block)
                || is_duplicate_jumbf_seed(blocks, i)) {
                continue;
            }

            PayloadResult extracted;
            std::span<const std::byte> logical;
            for (;;) {
                extracted = extract_payload(
                    file_bytes, blocks, i,
                    std::span<std::byte>(payload.data(), payload.size()),
                    std::span<uint32_t>(scratch.data(), scratch.size()),
                    options);
                if (extracted.status == PayloadStatus::OutputTruncated
                    && extracted.needed > payload.size()
                    && extracted.needed <= static_cast<uint64_t>(SIZE_MAX)) {
                    payload.resize(static_cast<size_t>(extracted.needed));
                    continue;
                }
                break;
            }

            if (extracted.status != PayloadStatus::Ok) {
                out.errors += 1U;
                append_message(&out.message,
                               "failed to extract source jumbf payload");
                continue;
            }

            logical = std::span<const std::byte>(payload.data(),
                                                 static_cast<size_t>(
                                                     extracted.written));
            if (logical.empty()) {
                continue;
            }

            const C2paPayloadClass payload_class = classify_c2pa_jumbf_payload(
                logical);
            if (payload_class == C2paPayloadClass::DraftUnsignedInvalidation) {
                if (!keep_c2pa) {
                    continue;
                }
                std::string error;
                if (!append_bmff_metadata_item(logical, "bmff:item-c2pa",
                                               TransferBlockKind::C2pa, &order,
                                               &bundle->blocks, &error)) {
                    out.errors += 1U;
                    append_message(&out.message, error);
                    continue;
                }
                out.emitted_blocks += 1U;
                out.emitted_c2pa += 1U;
                continue;
            }
            if (payload_class == C2paPayloadClass::ContentBound
                || !keep_jumbf) {
                continue;
            }

            std::string error;
            if (!append_bmff_metadata_item(logical, "bmff:item-jumb",
                                           TransferBlockKind::Jumbf, &order,
                                           &bundle->blocks, &error)) {
                out.errors += 1U;
                append_message(&out.message, error);
                continue;
            }
            out.emitted_blocks += 1U;
            out.emitted_jumbf += 1U;
        }
        return out;
    }

    static void append_u64be(std::vector<std::byte>* out, uint64_t v) noexcept
    {
        if (!out) {
            return;
        }
        out->push_back(static_cast<std::byte>((v >> 56) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 48) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 40) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 32) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 24) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 16) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 8) & 0xFFU));
        out->push_back(static_cast<std::byte>((v >> 0) & 0xFFU));
    }

    static void put_u16le(std::vector<std::byte>* out, uint32_t off,
                          uint16_t v) noexcept
    {
        if (!out || off + 2U > out->size()) {
            return;
        }
        (*out)[off + 0U] = static_cast<std::byte>((v >> 0) & 0xFFU);
        (*out)[off + 1U] = static_cast<std::byte>((v >> 8) & 0xFFU);
    }

    static void put_u32le(std::vector<std::byte>* out, uint32_t off,
                          uint32_t v) noexcept
    {
        if (!out || off + 4U > out->size()) {
            return;
        }
        (*out)[off + 0U] = static_cast<std::byte>((v >> 0) & 0xFFU);
        (*out)[off + 1U] = static_cast<std::byte>((v >> 8) & 0xFFU);
        (*out)[off + 2U] = static_cast<std::byte>((v >> 16) & 0xFFU);
        (*out)[off + 3U] = static_cast<std::byte>((v >> 24) & 0xFFU);
    }

    static void put_u32be(std::vector<std::byte>* out, uint32_t off,
                          uint32_t v) noexcept
    {
        if (!out || off + 4U > out->size()) {
            return;
        }
        (*out)[off + 0U] = static_cast<std::byte>((v >> 24) & 0xFFU);
        (*out)[off + 1U] = static_cast<std::byte>((v >> 16) & 0xFFU);
        (*out)[off + 2U] = static_cast<std::byte>((v >> 8) & 0xFFU);
        (*out)[off + 3U] = static_cast<std::byte>((v >> 0) & 0xFFU);
    }

    static uint16_t tiff_type_from_elem(MetaElementType elem) noexcept
    {
        switch (elem) {
        case MetaElementType::U8: return 1U;
        case MetaElementType::I8: return 6U;
        case MetaElementType::U16: return 3U;
        case MetaElementType::I16: return 8U;
        case MetaElementType::U32: return 4U;
        case MetaElementType::I32: return 9U;
        case MetaElementType::F32: return 11U;
        case MetaElementType::F64: return 12U;
        case MetaElementType::URational: return 5U;
        case MetaElementType::SRational: return 10U;
        default: return 0U;
        }
    }

    static uint32_t tiff_type_size(uint16_t type) noexcept
    {
        switch (type) {
        case 1U:  // BYTE
        case 2U:  // ASCII
        case 6U:  // SBYTE
        case 7U:  // UNDEFINED
            return 1U;
        case 3U:  // SHORT
        case 8U:  // SSHORT
            return 2U;
        case 4U:   // LONG
        case 9U:   // SLONG
        case 11U:  // FLOAT
            return 4U;
        case 5U:   // RATIONAL
        case 10U:  // SRATIONAL
        case 12U:  // DOUBLE
            return 8U;
        default: return 0U;
        }
    }

    static ExifIfdSlot classify_exif_ifd_name(std::string_view ifd) noexcept
    {
        if (ifd == "ifd0") {
            return ExifIfdSlot::Ifd0;
        }
        if (ifd == "exififd") {
            return ExifIfdSlot::ExifIfd;
        }
        if (ifd == "gpsifd") {
            return ExifIfdSlot::GpsIfd;
        }
        if (ifd == "interopifd") {
            return ExifIfdSlot::InteropIfd;
        }
        return ExifIfdSlot::Unsupported;
    }

    static SerializedIfd* select_ifd(SerializedIfd* ifd0,
                                     SerializedIfd* exififd,
                                     SerializedIfd* gpsifd,
                                     SerializedIfd* interopifd,
                                     ExifIfdSlot slot) noexcept
    {
        switch (slot) {
        case ExifIfdSlot::Ifd0: return ifd0;
        case ExifIfdSlot::ExifIfd: return exififd;
        case ExifIfdSlot::GpsIfd: return gpsifd;
        case ExifIfdSlot::InteropIfd: return interopifd;
        case ExifIfdSlot::Unsupported: return nullptr;
        }
        return nullptr;
    }

    static bool is_regenerated_pointer_tag(ExifIfdSlot slot,
                                           uint16_t tag) noexcept
    {
        if (slot == ExifIfdSlot::Ifd0) {
            return tag == 0x8769U || tag == 0x8825U;
        }
        if (slot == ExifIfdSlot::ExifIfd) {
            return tag == 0xA005U;
        }
        return false;
    }

    static bool time_patch_field_for_exif_tag(ExifIfdSlot slot, uint16_t tag,
                                              TimePatchField* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (slot == ExifIfdSlot::Ifd0 && tag == 0x0132U) {
            *out = TimePatchField::DateTime;
            return true;
        }
        if (slot == ExifIfdSlot::ExifIfd) {
            switch (tag) {
            case 0x9003U: *out = TimePatchField::DateTimeOriginal; return true;
            case 0x9004U: *out = TimePatchField::DateTimeDigitized; return true;
            case 0x9010U: *out = TimePatchField::OffsetTime; return true;
            case 0x9011U:
                *out = TimePatchField::OffsetTimeOriginal;
                return true;
            case 0x9012U:
                *out = TimePatchField::OffsetTimeDigitized;
                return true;
            case 0x9290U: *out = TimePatchField::SubSecTime; return true;
            case 0x9291U:
                *out = TimePatchField::SubSecTimeOriginal;
                return true;
            case 0x9292U:
                *out = TimePatchField::SubSecTimeDigitized;
                return true;
            default: break;
            }
        }
        if (slot == ExifIfdSlot::GpsIfd) {
            if (tag == 0x001DU) {
                *out = TimePatchField::GpsDateStamp;
                return true;
            }
            if (tag == 0x0007U) {
                *out = TimePatchField::GpsTimeStamp;
                return true;
            }
        }
        return false;
    }

    static bool ifd_name_for_entry(const MetaStore& store, const Entry& e,
                                   std::string_view* out_ifd) noexcept
    {
        if (!out_ifd || e.key.kind != MetaKeyKind::ExifTag) {
            return false;
        }
        const std::span<const std::byte> ifd_bytes = store.arena().span(
            e.key.data.exif_tag.ifd);
        if (ifd_bytes.empty()) {
            return false;
        }
        *out_ifd
            = std::string_view(reinterpret_cast<const char*>(ifd_bytes.data()),
                               ifd_bytes.size());
        return true;
    }

    static bool encode_tiff_value(const MetaStore& store, const Entry& e,
                                  SerializedIfdEntry* out) noexcept
    {
        if (!out) {
            return false;
        }

        const MetaValue& v = e.value;
        out->value.clear();
        out->type  = 0U;
        out->count = 0U;

        if (v.kind == MetaValueKind::Text) {
            out->type                                   = 2U;  // ASCII
            const std::span<const std::byte> text_bytes = store.arena().span(
                v.data.span);
            out->value.assign(text_bytes.begin(), text_bytes.end());
            out->value.push_back(std::byte { 0x00 });
            out->count = static_cast<uint32_t>(out->value.size());
            return true;
        }
        if (v.kind == MetaValueKind::Bytes) {
            out->type                              = 7U;  // UNDEFINED
            const std::span<const std::byte> bytes = store.arena().span(
                v.data.span);
            out->value.assign(bytes.begin(), bytes.end());
            out->count = static_cast<uint32_t>(out->value.size());
            return true;
        }
        if (v.kind == MetaValueKind::Array) {
            out->type = tiff_type_from_elem(v.elem_type);
            if (out->type == 0U) {
                return false;
            }
            const std::span<const std::byte> bytes = store.arena().span(
                v.data.span);
            out->value.assign(bytes.begin(), bytes.end());
            out->count = v.count;
            if (out->count == 0U) {
                const uint32_t elem_size = tiff_type_size(out->type);
                if (elem_size == 0U || out->value.size() % elem_size != 0U) {
                    return false;
                }
                out->count = static_cast<uint32_t>(out->value.size()
                                                   / elem_size);
            }
            return true;
        }
        if (v.kind != MetaValueKind::Scalar) {
            return false;
        }

        switch (v.elem_type) {
        case MetaElementType::U8:
            out->type  = 1U;
            out->count = 1U;
            out->value.push_back(static_cast<std::byte>(v.data.u64 & 0xFFU));
            return true;
        case MetaElementType::I8:
            out->type  = 6U;
            out->count = 1U;
            out->value.push_back(static_cast<std::byte>(
                static_cast<uint8_t>(static_cast<int8_t>(v.data.i64))));
            return true;
        case MetaElementType::U16:
            out->type  = 3U;
            out->count = 1U;
            append_u16le(&out->value, static_cast<uint16_t>(v.data.u64));
            return true;
        case MetaElementType::I16:
            out->type  = 8U;
            out->count = 1U;
            append_u16le(&out->value, static_cast<uint16_t>(
                                          static_cast<int16_t>(v.data.i64)));
            return true;
        case MetaElementType::U32:
            out->type  = 4U;
            out->count = 1U;
            append_u32le(&out->value, static_cast<uint32_t>(v.data.u64));
            return true;
        case MetaElementType::I32:
            out->type  = 9U;
            out->count = 1U;
            append_u32le(&out->value, static_cast<uint32_t>(
                                          static_cast<int32_t>(v.data.i64)));
            return true;
        case MetaElementType::URational:
            out->type  = 5U;
            out->count = 1U;
            append_u32le(&out->value, v.data.ur.numer);
            append_u32le(&out->value, v.data.ur.denom);
            return true;
        case MetaElementType::SRational:
            out->type  = 10U;
            out->count = 1U;
            append_u32le(&out->value,
                         static_cast<uint32_t>(
                             static_cast<int32_t>(v.data.sr.numer)));
            append_u32le(&out->value,
                         static_cast<uint32_t>(
                             static_cast<int32_t>(v.data.sr.denom)));
            return true;
        case MetaElementType::F32:
            out->type  = 11U;
            out->count = 1U;
            append_u32le(&out->value, v.data.f32_bits);
            return true;
        case MetaElementType::F64:
            out->type  = 12U;
            out->count = 1U;
            append_u32le(&out->value,
                         static_cast<uint32_t>(v.data.f64_bits & 0xFFFFFFFFU));
            append_u32le(&out->value,
                         static_cast<uint32_t>(v.data.f64_bits >> 32));
            return true;
        default: return false;
        }
    }

    static void sort_ifd_entries(SerializedIfd* ifd) noexcept
    {
        if (!ifd) {
            return;
        }
        std::stable_sort(ifd->entries.begin(), ifd->entries.end(),
                         IfdEntryLess {});
    }

    static void add_u32_pointer_entry(SerializedIfd* ifd, uint16_t tag) noexcept
    {
        if (!ifd) {
            return;
        }
        SerializedIfdEntry e;
        e.tag          = tag;
        e.type         = 4U;
        e.count        = 1U;
        e.source_order = UINT32_MAX;
        e.value        = {
            std::byte { 0x00 },
            std::byte { 0x00 },
            std::byte { 0x00 },
            std::byte { 0x00 },
        };
        ifd->entries.push_back(std::move(e));
    }

    static bool set_u32_pointer_value(SerializedIfd* ifd, uint16_t tag,
                                      uint32_t value) noexcept
    {
        if (!ifd) {
            return false;
        }
        for (SerializedIfdEntry& e : ifd->entries) {
            if (e.tag != tag) {
                continue;
            }
            e.value.clear();
            append_u32le(&e.value, value);
            e.type  = 4U;
            e.count = 1U;
            return true;
        }
        return false;
    }

    static uint32_t ifd_directory_size_bytes(const SerializedIfd& ifd) noexcept
    {
        return 2U + static_cast<uint32_t>(ifd.entries.size()) * 12U + 4U;
    }

    static uint32_t align_to_2(uint32_t v) noexcept { return (v + 1U) & ~1U; }

    static ExifPackBuild
    build_jpeg_exif_app1_payload(const MetaStore& store,
                                 TransferPolicyAction makernote_policy) noexcept
    {
        ExifPackBuild out;

        SerializedIfd ifd0 {};
        SerializedIfd exififd {};
        SerializedIfd gpsifd {};
        SerializedIfd interopifd {};

        for (const Entry& e : store.entries()) {
            if (any(e.flags, EntryFlags::Deleted)
                || e.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }

            std::string_view ifd_name;
            if (!ifd_name_for_entry(store, e, &ifd_name)) {
                out.skipped_count += 1U;
                continue;
            }

            const ExifIfdSlot slot = classify_exif_ifd_name(ifd_name);
            if (slot == ExifIfdSlot::Unsupported) {
                out.skipped_count += 1U;
                continue;
            }

            const uint16_t tag = e.key.data.exif_tag.tag;
            if (is_regenerated_pointer_tag(slot, tag)) {
                continue;
            }
            if (tag == 0x927CU
                && makernote_policy == TransferPolicyAction::Drop) {
                continue;
            }

            SerializedIfdEntry encoded;
            encoded.tag          = tag;
            encoded.source_order = e.origin.order_in_block;
            if (!encode_tiff_value(store, e, &encoded)) {
                out.skipped_count += 1U;
                continue;
            }

            SerializedIfd* dst = select_ifd(&ifd0, &exififd, &gpsifd,
                                            &interopifd, slot);
            if (!dst) {
                out.skipped_count += 1U;
                continue;
            }
            dst->present = true;
            dst->entries.push_back(std::move(encoded));
        }

        if (interopifd.present) {
            exififd.present = true;
        }
        if (exififd.present || gpsifd.present) {
            ifd0.present = true;
        }

        if (!ifd0.present) {
            return out;
        }

        if (exififd.present) {
            add_u32_pointer_entry(&ifd0, 0x8769U);
        }
        if (gpsifd.present) {
            add_u32_pointer_entry(&ifd0, 0x8825U);
        }
        if (interopifd.present) {
            add_u32_pointer_entry(&exififd, 0xA005U);
        }

        sort_ifd_entries(&ifd0);
        sort_ifd_entries(&exififd);
        sort_ifd_entries(&gpsifd);
        sort_ifd_entries(&interopifd);

        std::array<SerializedIfd*, 4> order = {
            &ifd0,
            &exififd,
            &gpsifd,
            &interopifd,
        };

        uint32_t cursor = 8U;  // TIFF header
        for (SerializedIfd* ifd : order) {
            if (!ifd->present) {
                continue;
            }
            ifd->dir_offset = cursor;
            cursor += ifd_directory_size_bytes(*ifd);
        }

        if (exififd.present) {
            (void)set_u32_pointer_value(&ifd0, 0x8769U, exififd.dir_offset);
        }
        if (gpsifd.present) {
            (void)set_u32_pointer_value(&ifd0, 0x8825U, gpsifd.dir_offset);
        }
        if (interopifd.present) {
            (void)set_u32_pointer_value(&exififd, 0xA005U,
                                        interopifd.dir_offset);
        }

        uint32_t data_cursor = cursor;
        for (SerializedIfd* ifd : order) {
            if (!ifd->present) {
                continue;
            }
            for (SerializedIfdEntry& e : ifd->entries) {
                e.inline_value = false;
                e.inline_bytes = { std::byte { 0x00 }, std::byte { 0x00 },
                                   std::byte { 0x00 }, std::byte { 0x00 } };
                if (e.value.size() <= 4U) {
                    e.inline_value = true;
                    for (uint32_t i = 0; i < e.value.size(); ++i) {
                        e.inline_bytes[i] = e.value[i];
                    }
                    continue;
                }
                data_cursor    = align_to_2(data_cursor);
                e.value_offset = data_cursor;
                data_cursor += static_cast<uint32_t>(e.value.size());
            }
        }

        std::vector<std::byte> tiff_bytes(data_cursor, std::byte { 0x00 });
        tiff_bytes[0] = std::byte { 'I' };
        tiff_bytes[1] = std::byte { 'I' };
        put_u16le(&tiff_bytes, 2U, 42U);
        put_u32le(&tiff_bytes, 4U, ifd0.dir_offset);

        for (SerializedIfd* ifd : order) {
            if (!ifd->present) {
                continue;
            }

            ExifIfdSlot slot = ExifIfdSlot::Unsupported;
            if (ifd == &ifd0) {
                slot = ExifIfdSlot::Ifd0;
            } else if (ifd == &exififd) {
                slot = ExifIfdSlot::ExifIfd;
            } else if (ifd == &gpsifd) {
                slot = ExifIfdSlot::GpsIfd;
            } else if (ifd == &interopifd) {
                slot = ExifIfdSlot::InteropIfd;
            }

            uint32_t p = ifd->dir_offset;
            put_u16le(&tiff_bytes, p,
                      static_cast<uint16_t>(ifd->entries.size()));
            p += 2U;
            for (const SerializedIfdEntry& e : ifd->entries) {
                put_u16le(&tiff_bytes, p + 0U, e.tag);
                put_u16le(&tiff_bytes, p + 2U, e.type);
                put_u32le(&tiff_bytes, p + 4U, e.count);
                if (e.inline_value) {
                    for (uint32_t i = 0; i < 4U; ++i) {
                        tiff_bytes[p + 8U + i] = e.inline_bytes[i];
                    }
                } else {
                    put_u32le(&tiff_bytes, p + 8U, e.value_offset);
                }

                TimePatchField patch_field = TimePatchField::DateTime;
                if (time_patch_field_for_exif_tag(slot, e.tag, &patch_field)) {
                    uint32_t patch_offset = 0U;
                    if (e.inline_value) {
                        patch_offset = p + 8U;
                    } else {
                        patch_offset = e.value_offset;
                    }
                    uint32_t patch_width = static_cast<uint32_t>(
                        e.value.size());
                    if (patch_width > 0U
                        && patch_offset + patch_width <= tiff_bytes.size()
                        && patch_width <= static_cast<uint32_t>(UINT16_MAX)) {
                        TimePatchSlot slot_desc;
                        slot_desc.field       = patch_field;
                        slot_desc.block_index = 0U;
                        slot_desc.byte_offset = 6U + patch_offset;
                        slot_desc.width = static_cast<uint16_t>(patch_width);
                        out.time_patch_map.push_back(slot_desc);
                    } else {
                        out.skipped_count += 1U;
                    }
                }
                p += 12U;
            }
            put_u32le(&tiff_bytes, p, 0U);  // next IFD offset
        }

        for (SerializedIfd* ifd : order) {
            if (!ifd->present) {
                continue;
            }
            for (const SerializedIfdEntry& e : ifd->entries) {
                if (e.inline_value || e.value.empty()) {
                    continue;
                }
                if (e.value_offset + e.value.size() > tiff_bytes.size()) {
                    out.skipped_count += 1U;
                    continue;
                }
                std::memcpy(tiff_bytes.data() + e.value_offset, e.value.data(),
                            e.value.size());
            }
        }

        append_ascii_bytes(&out.app1_payload, "Exif");
        out.app1_payload.push_back(std::byte { 0x00 });
        out.app1_payload.push_back(std::byte { 0x00 });
        out.app1_payload.insert(out.app1_payload.end(), tiff_bytes.begin(),
                                tiff_bytes.end());
        out.produced = true;
        return out;
    }

    struct IccHeaderFieldItem final {
        uint32_t offset = 0;
        std::vector<std::byte> bytes;
    };

    struct IccTagItem final {
        uint32_t signature = 0;
        std::vector<std::byte> bytes;
    };

    struct IccTagItemLess final {
        bool operator()(const IccTagItem& a, const IccTagItem& b) const noexcept
        {
            return a.signature < b.signature;
        }
    };

    static bool build_icc_profile_bytes_from_items(
        const std::vector<IccHeaderFieldItem>& headers,
        const std::vector<IccTagItem>& tags,
        std::vector<std::byte>* out_profile) noexcept
    {
        if (!out_profile) {
            return false;
        }
        out_profile->clear();
        if (tags.empty()) {
            return false;
        }

        const auto align4 = [](uint32_t v) noexcept -> uint32_t {
            return (v + 3U) & ~3U;
        };

        const uint32_t count = static_cast<uint32_t>(tags.size());
        uint32_t table_off   = 128U;
        uint32_t data_off    = table_off + 4U + count * 12U;
        data_off             = align4(data_off);

        struct TagPos final {
            uint32_t signature                  = 0;
            uint32_t offset                     = 0;
            uint32_t size                       = 0;
            const std::vector<std::byte>* bytes = nullptr;
        };

        std::vector<TagPos> positions;
        positions.reserve(tags.size());
        uint32_t cursor = data_off;
        for (const IccTagItem& t : tags) {
            cursor = align4(cursor);
            TagPos p;
            p.signature = t.signature;
            p.offset    = cursor;
            p.size      = static_cast<uint32_t>(t.bytes.size());
            p.bytes     = &t.bytes;
            positions.push_back(p);
            cursor += p.size;
        }

        out_profile->assign(cursor, std::byte { 0x00 });
        for (const IccHeaderFieldItem& h : headers) {
            if (h.offset >= 128U || h.bytes.empty()) {
                continue;
            }
            const size_t avail = static_cast<size_t>(128U - h.offset);
            const size_t n     = std::min(avail, h.bytes.size());
            std::memcpy(out_profile->data() + h.offset, h.bytes.data(), n);
        }

        (*out_profile)[36] = std::byte { 'a' };
        (*out_profile)[37] = std::byte { 'c' };
        (*out_profile)[38] = std::byte { 's' };
        (*out_profile)[39] = std::byte { 'p' };

        put_u32be(out_profile, 128U, count);
        uint32_t table_pos = 132U;
        for (const TagPos& p : positions) {
            put_u32be(out_profile, table_pos + 0U, p.signature);
            put_u32be(out_profile, table_pos + 4U, p.offset);
            put_u32be(out_profile, table_pos + 8U, p.size);
            table_pos += 12U;
        }
        for (const TagPos& p : positions) {
            if (!p.bytes || p.bytes->empty()) {
                continue;
            }
            std::memcpy(out_profile->data() + p.offset, p.bytes->data(),
                        p.bytes->size());
        }
        put_u32be(out_profile, 0U, static_cast<uint32_t>(out_profile->size()));
        return true;
    }

    static bool value_to_icc_bytes(const MetaStore& store, const MetaValue& v,
                                   std::vector<std::byte>* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();

        if (v.kind == MetaValueKind::Bytes || v.kind == MetaValueKind::Text) {
            const std::span<const std::byte> bytes = store.arena().span(
                v.data.span);
            out->assign(bytes.begin(), bytes.end());
            return true;
        }
        if (v.kind == MetaValueKind::Array) {
            const std::span<const std::byte> bytes = store.arena().span(
                v.data.span);
            if (bytes.empty()) {
                return true;
            }
            const uint16_t t = tiff_type_from_elem(v.elem_type);
            if (t == 0U) {
                return false;
            }
            if (t == 1U || t == 6U) {
                out->assign(bytes.begin(), bytes.end());
                return true;
            }
            const uint32_t n = v.count;
            uint32_t elem_sz = tiff_type_size(t);
            if (elem_sz == 0U) {
                return false;
            }
            uint32_t count = n;
            if (count == 0U) {
                if (bytes.size() % elem_sz != 0U) {
                    return false;
                }
                count = static_cast<uint32_t>(bytes.size() / elem_sz);
            }

            out->reserve(count * elem_sz);
            if (t == 3U || t == 8U) {
                for (uint32_t i = 0; i < count; ++i) {
                    uint16_t x = 0;
                    std::memcpy(&x, bytes.data() + i * 2U, sizeof(uint16_t));
                    append_u16be(out, x);
                }
                return true;
            }
            if (t == 4U || t == 9U || t == 11U) {
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t x = 0;
                    std::memcpy(&x, bytes.data() + i * 4U, sizeof(uint32_t));
                    append_u32be(out, x);
                }
                return true;
            }
            if (t == 5U || t == 10U) {
                for (uint32_t i = 0; i < count; ++i) {
                    uint32_t a = 0;
                    uint32_t b = 0;
                    std::memcpy(&a, bytes.data() + i * 8U + 0U,
                                sizeof(uint32_t));
                    std::memcpy(&b, bytes.data() + i * 8U + 4U,
                                sizeof(uint32_t));
                    append_u32be(out, a);
                    append_u32be(out, b);
                }
                return true;
            }
            if (t == 12U) {
                for (uint32_t i = 0; i < count; ++i) {
                    uint64_t x = 0;
                    std::memcpy(&x, bytes.data() + i * 8U, sizeof(uint64_t));
                    append_u64be(out, x);
                }
                return true;
            }
            return false;
        }
        if (v.kind != MetaValueKind::Scalar) {
            return false;
        }

        switch (v.elem_type) {
        case MetaElementType::U8:
            out->push_back(static_cast<std::byte>(v.data.u64 & 0xFFU));
            return true;
        case MetaElementType::I8:
            out->push_back(static_cast<std::byte>(
                static_cast<uint8_t>(static_cast<int8_t>(v.data.i64))));
            return true;
        case MetaElementType::U16:
            append_u16be(out, static_cast<uint16_t>(v.data.u64));
            return true;
        case MetaElementType::I16:
            append_u16be(out, static_cast<uint16_t>(
                                  static_cast<int16_t>(v.data.i64)));
            return true;
        case MetaElementType::U32:
            append_u32be(out, static_cast<uint32_t>(v.data.u64));
            return true;
        case MetaElementType::I32:
            append_u32be(out, static_cast<uint32_t>(
                                  static_cast<int32_t>(v.data.i64)));
            return true;
        case MetaElementType::U64: append_u64be(out, v.data.u64); return true;
        case MetaElementType::I64:
            append_u64be(out, static_cast<uint64_t>(v.data.i64));
            return true;
        case MetaElementType::URational:
            append_u32be(out, v.data.ur.numer);
            append_u32be(out, v.data.ur.denom);
            return true;
        case MetaElementType::SRational:
            append_u32be(out, static_cast<uint32_t>(v.data.sr.numer));
            append_u32be(out, static_cast<uint32_t>(v.data.sr.denom));
            return true;
        case MetaElementType::F32:
            append_u32be(out, v.data.f32_bits);
            return true;
        case MetaElementType::F64:
            append_u64be(out, v.data.f64_bits);
            return true;
        default: return false;
        }
    }

    static IccPackBuild
    build_jpeg_icc_app2_blocks(const MetaStore& store) noexcept
    {
        IccPackBuild out;

        std::vector<IccHeaderFieldItem> headers;
        std::vector<IccTagItem> tags;

        for (const Entry& e : store.entries()) {
            if (any(e.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (e.key.kind == MetaKeyKind::IccHeaderField) {
                std::vector<std::byte> bytes;
                if (!value_to_icc_bytes(store, e.value, &bytes)) {
                    out.skipped_count += 1U;
                    continue;
                }
                bool exists = false;
                for (const IccHeaderFieldItem& h : headers) {
                    if (h.offset == e.key.data.icc_header_field.offset) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    IccHeaderFieldItem h;
                    h.offset = e.key.data.icc_header_field.offset;
                    h.bytes  = std::move(bytes);
                    headers.push_back(std::move(h));
                }
                continue;
            }

            if (e.key.kind == MetaKeyKind::IccTag) {
                std::vector<std::byte> bytes;
                if (!value_to_icc_bytes(store, e.value, &bytes)) {
                    out.skipped_count += 1U;
                    continue;
                }
                if (bytes.empty()) {
                    out.skipped_count += 1U;
                    continue;
                }
                bool exists = false;
                for (const IccTagItem& t : tags) {
                    if (t.signature == e.key.data.icc_tag.signature) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    IccTagItem t;
                    t.signature = e.key.data.icc_tag.signature;
                    t.bytes     = std::move(bytes);
                    tags.push_back(std::move(t));
                }
                continue;
            }
        }

        if (tags.empty()) {
            return out;
        }

        std::stable_sort(tags.begin(), tags.end(), IccTagItemLess {});

        std::vector<std::byte> profile;
        if (!build_icc_profile_bytes_from_items(headers, tags, &profile)
            || profile.empty()) {
            out.skipped_count += 1U;
            return out;
        }

        static constexpr uint32_t kIccChunkDataMax = 65519U;
        uint32_t chunk_count                       = static_cast<uint32_t>(
            (profile.size() + kIccChunkDataMax - 1U) / kIccChunkDataMax);
        if (chunk_count == 0U || chunk_count > 255U) {
            out.skipped_count += 1U;
            return out;
        }

        uint32_t consumed = 0U;
        for (uint32_t i = 0; i < chunk_count; ++i) {
            const uint32_t remaining = static_cast<uint32_t>(profile.size())
                                       - consumed;
            const uint32_t n = std::min(kIccChunkDataMax, remaining);

            PreparedTransferBlock b;
            b.kind  = TransferBlockKind::Icc;
            b.order = 120U + i;
            b.route = "jpeg:app2-icc";
            append_ascii_bytes(&b.payload, "ICC_PROFILE");
            b.payload.push_back(std::byte { 0x00 });
            b.payload.push_back(
                static_cast<std::byte>(static_cast<uint8_t>(i + 1U)));
            b.payload.push_back(
                static_cast<std::byte>(static_cast<uint8_t>(chunk_count)));
            b.payload.insert(b.payload.end(), profile.begin() + consumed,
                             profile.begin() + consumed + n);
            consumed += n;
            out.blocks.push_back(std::move(b));
        }

        out.produced = !out.blocks.empty();
        return out;
    }

    static bool build_icc_profile_bytes(const MetaStore& store,
                                        std::vector<std::byte>* out_profile,
                                        uint32_t* skipped_count) noexcept
    {
        if (!out_profile) {
            return false;
        }
        out_profile->clear();
        if (skipped_count) {
            *skipped_count = 0U;
        }

        std::vector<IccHeaderFieldItem> headers;
        std::vector<IccTagItem> tags;

        for (const Entry& e : store.entries()) {
            if (any(e.flags, EntryFlags::Deleted)) {
                continue;
            }
            if (e.key.kind == MetaKeyKind::IccHeaderField) {
                std::vector<std::byte> bytes;
                if (!value_to_icc_bytes(store, e.value, &bytes)) {
                    if (skipped_count) {
                        *skipped_count += 1U;
                    }
                    continue;
                }
                bool exists = false;
                for (const IccHeaderFieldItem& h : headers) {
                    if (h.offset == e.key.data.icc_header_field.offset) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    IccHeaderFieldItem h;
                    h.offset = e.key.data.icc_header_field.offset;
                    h.bytes  = std::move(bytes);
                    headers.push_back(std::move(h));
                }
                continue;
            }
            if (e.key.kind == MetaKeyKind::IccTag) {
                std::vector<std::byte> bytes;
                if (!value_to_icc_bytes(store, e.value, &bytes)) {
                    if (skipped_count) {
                        *skipped_count += 1U;
                    }
                    continue;
                }
                if (bytes.empty()) {
                    if (skipped_count) {
                        *skipped_count += 1U;
                    }
                    continue;
                }
                bool exists = false;
                for (const IccTagItem& t : tags) {
                    if (t.signature == e.key.data.icc_tag.signature) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    IccTagItem t;
                    t.signature = e.key.data.icc_tag.signature;
                    t.bytes     = std::move(bytes);
                    tags.push_back(std::move(t));
                }
                continue;
            }
        }

        if (tags.empty()) {
            return false;
        }

        std::stable_sort(tags.begin(), tags.end(), IccTagItemLess {});
        return build_icc_profile_bytes_from_items(headers, tags, out_profile);
    }

    struct IptcDatasetItem final {
        uint16_t record       = 0;
        uint16_t dataset      = 0;
        uint32_t source_order = 0;
        std::vector<std::byte> payload;
    };

    struct IptcDatasetItemLess final {
        bool operator()(const IptcDatasetItem& a,
                        const IptcDatasetItem& b) const noexcept
        {
            if (a.source_order != b.source_order) {
                return a.source_order < b.source_order;
            }
            if (a.record != b.record) {
                return a.record < b.record;
            }
            return a.dataset < b.dataset;
        }
    };

    static bool value_to_raw_bytes(const MetaStore& store, const MetaValue& v,
                                   std::vector<std::byte>* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (v.kind == MetaValueKind::Bytes || v.kind == MetaValueKind::Text
            || v.kind == MetaValueKind::Array) {
            const std::span<const std::byte> bytes = store.arena().span(
                v.data.span);
            out->assign(bytes.begin(), bytes.end());
            return true;
        }
        return false;
    }

    static void append_iptc_len(std::vector<std::byte>* out,
                                uint32_t n) noexcept
    {
        if (!out) {
            return;
        }
        if (n <= 0x7FFFU) {
            append_u16be(out, static_cast<uint16_t>(n));
            return;
        }
        append_u16be(out, 0x8004U);
        append_u32be(out, n);
    }

    static std::vector<std::byte>
    build_iptc_iim_stream_from_datasets(const MetaStore& store,
                                        uint32_t* skipped_count) noexcept
    {
        std::vector<IptcDatasetItem> items;
        if (skipped_count) {
            *skipped_count = 0U;
        }

        for (const Entry& e : store.entries()) {
            if (any(e.flags, EntryFlags::Deleted)
                || e.key.kind != MetaKeyKind::IptcDataset) {
                continue;
            }
            if (e.key.data.iptc_dataset.record > 255U
                || e.key.data.iptc_dataset.dataset > 255U) {
                if (skipped_count) {
                    *skipped_count += 1U;
                }
                continue;
            }
            IptcDatasetItem item;
            item.record       = e.key.data.iptc_dataset.record;
            item.dataset      = e.key.data.iptc_dataset.dataset;
            item.source_order = e.origin.order_in_block;
            if (!value_to_raw_bytes(store, e.value, &item.payload)) {
                if (skipped_count) {
                    *skipped_count += 1U;
                }
                continue;
            }
            items.push_back(std::move(item));
        }

        std::stable_sort(items.begin(), items.end(), IptcDatasetItemLess {});

        std::vector<std::byte> out;
        for (const IptcDatasetItem& item : items) {
            out.push_back(std::byte { 0x1CU });
            out.push_back(
                static_cast<std::byte>(static_cast<uint8_t>(item.record)));
            out.push_back(
                static_cast<std::byte>(static_cast<uint8_t>(item.dataset)));
            append_iptc_len(&out, static_cast<uint32_t>(item.payload.size()));
            out.insert(out.end(), item.payload.begin(), item.payload.end());
        }
        return out;
    }

    static bool
    first_photoshop_iptc_payload(const MetaStore& store,
                                 std::vector<std::byte>* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        for (const Entry& e : store.entries()) {
            if (any(e.flags, EntryFlags::Deleted)
                || e.key.kind != MetaKeyKind::PhotoshopIrb
                || e.key.data.photoshop_irb.resource_id != 0x0404U) {
                continue;
            }
            if (!value_to_raw_bytes(store, e.value, out)) {
                continue;
            }
            if (!out->empty()) {
                return true;
            }
        }
        return false;
    }

    static std::vector<std::byte>
    wrap_iptc_as_photoshop_app13(std::span<const std::byte> iptc_iim) noexcept
    {
        std::vector<std::byte> out;
        append_ascii_bytes(&out, "Photoshop 3.0");
        out.push_back(std::byte { 0x00 });  // C string terminator

        append_ascii_bytes(&out, "8BIM");
        append_u16be(&out, 0x0404U);

        // Pascal name: empty string + pad to even.
        out.push_back(std::byte { 0x00 });
        out.push_back(std::byte { 0x00 });

        append_u32be(&out, static_cast<uint32_t>(iptc_iim.size()));
        out.insert(out.end(), iptc_iim.begin(), iptc_iim.end());
        if ((iptc_iim.size() & 1U) != 0U) {
            out.push_back(std::byte { 0x00 });
        }

        return out;
    }

    static IptcPackBuild
    build_jpeg_iptc_app13_payload(const MetaStore& store) noexcept
    {
        IptcPackBuild out;

        std::vector<std::byte> iptc_iim;
        if (!first_photoshop_iptc_payload(store, &iptc_iim)) {
            iptc_iim = build_iptc_iim_stream_from_datasets(store,
                                                           &out.skipped_count);
        }

        if (iptc_iim.empty()) {
            return out;
        }

        out.app13_payload = wrap_iptc_as_photoshop_app13(
            std::span<const std::byte>(iptc_iim.data(), iptc_iim.size()));
        out.produced = !out.app13_payload.empty();
        return out;
    }

    enum class TiffEndian : uint8_t {
        Little,
        Big,
    };

    struct TiffTagUpdate final {
        uint16_t tag   = 0U;
        uint16_t type  = 7U;  // UNDEFINED
        uint32_t count = 0U;
        std::vector<std::byte> payload;
    };

    struct TiffIfdEntry final {
        uint16_t tag            = 0U;
        uint16_t type           = 0U;
        uint32_t count          = 0U;
        uint32_t value_or_off   = 0U;
        bool has_inline_payload = false;
        std::array<std::byte, 4> inline_payload {};
        bool has_rewrite_payload = false;
        std::vector<std::byte> rewrite_payload;
    };

    struct TiffIfdEntryLess final {
        bool operator()(const TiffIfdEntry& a,
                        const TiffIfdEntry& b) const noexcept
        {
            return a.tag < b.tag;
        }
    };

    struct ParsedTiffIfdEntry final {
        uint16_t tag   = 0U;
        uint16_t type  = 0U;
        uint32_t count = 0U;
        std::vector<std::byte> payload;
    };

    struct ParsedTiffIfd final {
        bool present = false;
        std::vector<ParsedTiffIfdEntry> entries;
    };

    struct ParsedTransferExif final {
        std::vector<TiffTagUpdate> ifd0_updates;
        ParsedTiffIfd exif_ifd;
        ParsedTiffIfd gps_ifd;
        ParsedTiffIfd interop_ifd;
        TiffEndian source_endian = TiffEndian::Little;
    };

    struct TiffRewriteTail final {
        TiffEndian endian     = TiffEndian::Little;
        uint32_t new_ifd0_off = 0U;
        std::vector<std::byte> tail_bytes;
    };

    static uint16_t read_u16_tiff(std::span<const std::byte> b, size_t off,
                                  TiffEndian endian) noexcept
    {
        if (off + 2U > b.size()) {
            return 0U;
        }
        const uint16_t a = static_cast<uint16_t>(
            std::to_integer<uint8_t>(b[off + 0U]));
        const uint16_t c = static_cast<uint16_t>(
            std::to_integer<uint8_t>(b[off + 1U]));
        if (endian == TiffEndian::Little) {
            return static_cast<uint16_t>(a | (c << 8U));
        }
        return static_cast<uint16_t>((a << 8U) | c);
    }

    static uint32_t read_u32_tiff(std::span<const std::byte> b, size_t off,
                                  TiffEndian endian) noexcept
    {
        if (off + 4U > b.size()) {
            return 0U;
        }
        const uint32_t b0 = static_cast<uint32_t>(
            std::to_integer<uint8_t>(b[off + 0U]));
        const uint32_t b1 = static_cast<uint32_t>(
            std::to_integer<uint8_t>(b[off + 1U]));
        const uint32_t b2 = static_cast<uint32_t>(
            std::to_integer<uint8_t>(b[off + 2U]));
        const uint32_t b3 = static_cast<uint32_t>(
            std::to_integer<uint8_t>(b[off + 3U]));
        if (endian == TiffEndian::Little) {
            return b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
        }
        return (b0 << 24U) | (b1 << 16U) | (b2 << 8U) | b3;
    }

    static void write_u16_tiff(std::vector<std::byte>* out, size_t off,
                               uint16_t v, TiffEndian endian) noexcept
    {
        if (!out || off + 2U > out->size()) {
            return;
        }
        if (endian == TiffEndian::Little) {
            (*out)[off + 0U] = static_cast<std::byte>((v >> 0U) & 0xFFU);
            (*out)[off + 1U] = static_cast<std::byte>((v >> 8U) & 0xFFU);
        } else {
            (*out)[off + 0U] = static_cast<std::byte>((v >> 8U) & 0xFFU);
            (*out)[off + 1U] = static_cast<std::byte>((v >> 0U) & 0xFFU);
        }
    }

    static void write_u32_tiff(std::vector<std::byte>* out, size_t off,
                               uint32_t v, TiffEndian endian) noexcept
    {
        if (!out || off + 4U > out->size()) {
            return;
        }
        if (endian == TiffEndian::Little) {
            (*out)[off + 0U] = static_cast<std::byte>((v >> 0U) & 0xFFU);
            (*out)[off + 1U] = static_cast<std::byte>((v >> 8U) & 0xFFU);
            (*out)[off + 2U] = static_cast<std::byte>((v >> 16U) & 0xFFU);
            (*out)[off + 3U] = static_cast<std::byte>((v >> 24U) & 0xFFU);
        } else {
            (*out)[off + 0U] = static_cast<std::byte>((v >> 24U) & 0xFFU);
            (*out)[off + 1U] = static_cast<std::byte>((v >> 16U) & 0xFFU);
            (*out)[off + 2U] = static_cast<std::byte>((v >> 8U) & 0xFFU);
            (*out)[off + 3U] = static_cast<std::byte>((v >> 0U) & 0xFFU);
        }
    }

    static void write_u32_tiff_bytes(std::array<std::byte, 4>* out, uint32_t v,
                                     TiffEndian endian) noexcept
    {
        if (!out) {
            return;
        }
        if (endian == TiffEndian::Little) {
            (*out)[0] = static_cast<std::byte>((v >> 0U) & 0xFFU);
            (*out)[1] = static_cast<std::byte>((v >> 8U) & 0xFFU);
            (*out)[2] = static_cast<std::byte>((v >> 16U) & 0xFFU);
            (*out)[3] = static_cast<std::byte>((v >> 24U) & 0xFFU);
        } else {
            (*out)[0] = static_cast<std::byte>((v >> 24U) & 0xFFU);
            (*out)[1] = static_cast<std::byte>((v >> 16U) & 0xFFU);
            (*out)[2] = static_cast<std::byte>((v >> 8U) & 0xFFU);
            (*out)[3] = static_cast<std::byte>((v >> 0U) & 0xFFU);
        }
    }

    static void append_u32_tiff(std::vector<std::byte>* out, uint32_t v,
                                TiffEndian endian) noexcept
    {
        if (!out) {
            return;
        }
        const size_t off = out->size();
        out->resize(off + 4U, std::byte { 0x00 });
        write_u32_tiff(out, off, v, endian);
    }

    static bool tiff_type_byte_len(uint16_t type, uint32_t count,
                                   size_t* out_len) noexcept
    {
        if (!out_len) {
            return false;
        }
        *out_len           = 0U;
        const uint32_t sz  = tiff_type_size(type);
        const uint64_t mul = static_cast<uint64_t>(sz)
                             * static_cast<uint64_t>(count);
        if (sz == 0U || mul > static_cast<uint64_t>(SIZE_MAX)) {
            return false;
        }
        *out_len = static_cast<size_t>(mul);
        return true;
    }

    static bool convert_tiff_payload_endian_inplace(
        std::vector<std::byte>* bytes, uint16_t type, uint32_t count,
        TiffEndian from_endian, TiffEndian to_endian) noexcept
    {
        if (!bytes) {
            return false;
        }
        if (from_endian == to_endian || bytes->empty()) {
            return true;
        }
        size_t expected = 0U;
        if (!tiff_type_byte_len(type, count, &expected)
            || expected != bytes->size()) {
            return false;
        }

        std::vector<std::byte>& v = *bytes;
        switch (type) {
        case 1U:  // BYTE
        case 2U:  // ASCII
        case 6U:  // SBYTE
        case 7U:  // UNDEFINED
            return true;
        case 3U:  // SHORT
        case 8U:  // SSHORT
            for (size_t i = 0; i < v.size(); i += 2U) {
                std::swap(v[i + 0U], v[i + 1U]);
            }
            return true;
        case 4U:   // LONG
        case 9U:   // SLONG
        case 11U:  // FLOAT
            for (size_t i = 0; i < v.size(); i += 4U) {
                std::swap(v[i + 0U], v[i + 3U]);
                std::swap(v[i + 1U], v[i + 2U]);
            }
            return true;
        case 5U:   // RATIONAL
        case 10U:  // SRATIONAL
            for (size_t i = 0; i < v.size(); i += 8U) {
                std::swap(v[i + 0U], v[i + 3U]);
                std::swap(v[i + 1U], v[i + 2U]);
                std::swap(v[i + 4U], v[i + 7U]);
                std::swap(v[i + 5U], v[i + 6U]);
            }
            return true;
        case 12U:  // DOUBLE
            for (size_t i = 0; i < v.size(); i += 8U) {
                std::swap(v[i + 0U], v[i + 7U]);
                std::swap(v[i + 1U], v[i + 6U]);
                std::swap(v[i + 2U], v[i + 5U]);
                std::swap(v[i + 3U], v[i + 4U]);
            }
            return true;
        default: return false;
        }
    }

    static bool convert_parsed_ifd_endian(ParsedTiffIfd* ifd,
                                          TiffEndian from_endian,
                                          TiffEndian to_endian) noexcept
    {
        if (!ifd || !ifd->present) {
            return true;
        }
        for (size_t i = 0; i < ifd->entries.size(); ++i) {
            ParsedTiffIfdEntry& e = ifd->entries[i];
            if (!convert_tiff_payload_endian_inplace(&e.payload, e.type,
                                                     e.count, from_endian,
                                                     to_endian)) {
                return false;
            }
        }
        return true;
    }

    static bool convert_transfer_exif_endian(ParsedTransferExif* exif,
                                             TiffEndian to_endian) noexcept
    {
        if (!exif) {
            return false;
        }
        const TiffEndian from_endian = exif->source_endian;
        if (from_endian == to_endian) {
            return true;
        }
        for (size_t i = 0; i < exif->ifd0_updates.size(); ++i) {
            TiffTagUpdate& u = exif->ifd0_updates[i];
            if (!convert_tiff_payload_endian_inplace(&u.payload, u.type,
                                                     u.count, from_endian,
                                                     to_endian)) {
                return false;
            }
        }
        return convert_parsed_ifd_endian(&exif->exif_ifd, from_endian,
                                         to_endian)
               && convert_parsed_ifd_endian(&exif->gps_ifd, from_endian,
                                            to_endian)
               && convert_parsed_ifd_endian(&exif->interop_ifd, from_endian,
                                            to_endian);
    }

    static bool parse_classic_ifd(std::span<const std::byte> tiff,
                                  size_t ifd_off, TiffEndian endian,
                                  ParsedTiffIfd* out, std::string* err) noexcept
    {
        if (!out) {
            return false;
        }
        out->present = false;
        out->entries.clear();

        if (ifd_off == 0U) {
            return false;
        }
        if (ifd_off + 2U > tiff.size()) {
            if (err) {
                *err = "exif ifd offset out of range";
            }
            return false;
        }
        const uint16_t count_u16 = read_u16_tiff(tiff, ifd_off, endian);
        const size_t count       = static_cast<size_t>(count_u16);
        const size_t entries_off = ifd_off + 2U;
        const size_t entries_end = entries_off + count * 12U;
        if (entries_end + 4U > tiff.size()) {
            if (err) {
                *err = "exif ifd entries truncated";
            }
            return false;
        }

        out->entries.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const size_t p            = entries_off + i * 12U;
            const uint16_t tag        = read_u16_tiff(tiff, p + 0U, endian);
            const uint16_t type       = read_u16_tiff(tiff, p + 2U, endian);
            const uint32_t elem_count = read_u32_tiff(tiff, p + 4U, endian);
            size_t payload_n          = 0U;
            if (!tiff_type_byte_len(type, elem_count, &payload_n)) {
                if (err) {
                    *err = "unsupported exif ifd entry type/size";
                }
                return false;
            }

            ParsedTiffIfdEntry e;
            e.tag   = tag;
            e.type  = type;
            e.count = elem_count;
            if (payload_n <= 4U) {
                e.payload.resize(payload_n, std::byte { 0x00 });
                for (size_t bi = 0; bi < payload_n; ++bi) {
                    e.payload[bi] = tiff[p + 8U + bi];
                }
            } else {
                const uint32_t value_off_u32 = read_u32_tiff(tiff, p + 8U,
                                                             endian);
                const size_t value_off = static_cast<size_t>(value_off_u32);
                if (value_off + payload_n > tiff.size()) {
                    if (err) {
                        *err = "exif ifd value offset out of range";
                    }
                    return false;
                }
                e.payload.resize(payload_n, std::byte { 0x00 });
                if (payload_n > 0U) {
                    std::memcpy(e.payload.data(), tiff.data() + value_off,
                                payload_n);
                }
            }
            out->entries.push_back(std::move(e));
        }
        out->present = true;
        return true;
    }

    static bool find_ifd_entry(const ParsedTiffIfd& ifd, uint16_t tag,
                               ParsedTiffIfdEntry* out) noexcept
    {
        if (!out) {
            return false;
        }
        for (size_t i = 0; i < ifd.entries.size(); ++i) {
            if (ifd.entries[i].tag == tag) {
                *out = ifd.entries[i];
                return true;
            }
        }
        return false;
    }

    static bool payload_to_u32(const ParsedTiffIfdEntry& e, TiffEndian endian,
                               uint32_t* out) noexcept
    {
        if (!out || e.type != 4U || e.count != 1U || e.payload.size() != 4U) {
            return false;
        }
        const uint32_t b0 = static_cast<uint32_t>(
            std::to_integer<uint8_t>(e.payload[0U]));
        const uint32_t b1 = static_cast<uint32_t>(
            std::to_integer<uint8_t>(e.payload[1U]));
        const uint32_t b2 = static_cast<uint32_t>(
            std::to_integer<uint8_t>(e.payload[2U]));
        const uint32_t b3 = static_cast<uint32_t>(
            std::to_integer<uint8_t>(e.payload[3U]));
        if (endian == TiffEndian::Little) {
            *out = b0 | (b1 << 8U) | (b2 << 16U) | (b3 << 24U);
        } else {
            *out = (b0 << 24U) | (b1 << 16U) | (b2 << 8U) | b3;
        }
        return true;
    }

    static bool
    parse_exif_app1_for_tiff_transfer(std::span<const std::byte> exif_app1,
                                      ParsedTransferExif* out,
                                      std::string* err) noexcept
    {
        if (!out) {
            return false;
        }
        out->ifd0_updates.clear();
        out->exif_ifd      = ParsedTiffIfd {};
        out->gps_ifd       = ParsedTiffIfd {};
        out->interop_ifd   = ParsedTiffIfd {};
        out->source_endian = TiffEndian::Little;

        if (exif_app1.size() < 14U) {
            if (err) {
                *err = "exif app1 payload too small";
            }
            return false;
        }
        const char kExifPrefix[6] = { 'E', 'x', 'i', 'f', '\0', '\0' };
        for (size_t i = 0; i < 6U; ++i) {
            if (std::to_integer<uint8_t>(exif_app1[i])
                != static_cast<uint8_t>(kExifPrefix[i])) {
                if (err) {
                    *err = "exif app1 payload missing Exif\\0\\0 prefix";
                }
                return false;
            }
        }

        const std::span<const std::byte> tiff = exif_app1.subspan(6U);
        if (tiff.size() < 8U) {
            if (err) {
                *err = "exif tiff payload too small";
            }
            return false;
        }
        TiffEndian endian = TiffEndian::Little;
        if (tiff[0] == std::byte { 'I' } && tiff[1] == std::byte { 'I' }) {
            endian = TiffEndian::Little;
        } else if (tiff[0] == std::byte { 'M' }
                   && tiff[1] == std::byte { 'M' }) {
            endian = TiffEndian::Big;
        } else {
            if (err) {
                *err = "unsupported exif tiff byte order";
            }
            return false;
        }
        if (read_u16_tiff(tiff, 2U, endian) != 42U) {
            if (err) {
                *err = "unsupported exif tiff magic";
            }
            return false;
        }
        const size_t ifd0_off = static_cast<size_t>(
            read_u32_tiff(tiff, 4U, endian));

        ParsedTiffIfd ifd0;
        if (!parse_classic_ifd(tiff, ifd0_off, endian, &ifd0, err)) {
            return false;
        }

        uint32_t exif_ifd_off = 0U;
        uint32_t gps_ifd_off  = 0U;
        for (size_t i = 0; i < ifd0.entries.size(); ++i) {
            const ParsedTiffIfdEntry& e = ifd0.entries[i];
            if (e.tag == 0x8769U && payload_to_u32(e, endian, &exif_ifd_off)) {
                continue;
            }
            if (e.tag == 0x8825U && payload_to_u32(e, endian, &gps_ifd_off)) {
                continue;
            }
            TiffTagUpdate u;
            u.tag     = e.tag;
            u.type    = e.type;
            u.count   = e.count;
            u.payload = e.payload;
            out->ifd0_updates.push_back(std::move(u));
        }

        if (exif_ifd_off != 0U) {
            if (!parse_classic_ifd(tiff, static_cast<size_t>(exif_ifd_off),
                                   endian, &out->exif_ifd, err)) {
                return false;
            }
            ParsedTiffIfdEntry interop_ptr;
            if (find_ifd_entry(out->exif_ifd, 0xA005U, &interop_ptr)) {
                uint32_t interop_ifd_off = 0U;
                if (payload_to_u32(interop_ptr, endian, &interop_ifd_off)
                    && interop_ifd_off != 0U) {
                    if (!parse_classic_ifd(tiff,
                                           static_cast<size_t>(interop_ifd_off),
                                           endian, &out->interop_ifd, err)) {
                        return false;
                    }
                }
            }
        }

        if (gps_ifd_off != 0U
            && !parse_classic_ifd(tiff, static_cast<size_t>(gps_ifd_off),
                                  endian, &out->gps_ifd, err)) {
            return false;
        }

        out->source_endian = endian;
        return true;
    }

    static std::vector<TiffTagUpdate>
    collect_tiff_tag_updates(const PreparedTransferBundle& bundle) noexcept
    {
        std::vector<TiffTagUpdate> out;
        for (size_t i = 0; i < bundle.blocks.size(); ++i) {
            const PreparedTransferBlock& b = bundle.blocks[i];
            uint16_t tag                   = 0;
            if (!tiff_tag_from_route(b.route, &tag)) {
                continue;
            }
            if (tag == 34665U) {
                continue;
            }
            bool exists = false;
            for (size_t j = 0; j < out.size(); ++j) {
                if (out[j].tag == tag) {
                    exists = true;
                    break;
                }
            }
            if (exists) {
                continue;
            }
            TiffTagUpdate u;
            u.tag   = tag;
            u.type  = 7U;
            u.count = static_cast<uint32_t>(b.payload.size());
            u.payload.assign(b.payload.begin(), b.payload.end());
            out.push_back(std::move(u));
        }
        return out;
    }

    static std::vector<std::byte>
    first_tiff_exif_app1_payload(const PreparedTransferBundle& bundle) noexcept
    {
        for (size_t i = 0; i < bundle.blocks.size(); ++i) {
            const PreparedTransferBlock& b = bundle.blocks[i];
            if (b.route == "tiff:ifd-exif-app1" && !b.payload.empty()) {
                return b.payload;
            }
        }
        return {};
    }

    static bool has_update_for_tag(const std::vector<TiffTagUpdate>& updates,
                                   uint16_t tag) noexcept
    {
        for (size_t i = 0; i < updates.size(); ++i) {
            if (updates[i].tag == tag) {
                return true;
            }
        }
        return false;
    }

    static void upsert_ifd_pointer(ParsedTiffIfd* ifd, uint16_t tag,
                                   uint32_t value, TiffEndian endian) noexcept
    {
        if (!ifd) {
            return;
        }
        std::vector<std::byte> payload;
        append_u32_tiff(&payload, value, endian);
        for (size_t i = 0; i < ifd->entries.size(); ++i) {
            ParsedTiffIfdEntry& e = ifd->entries[i];
            if (e.tag == tag) {
                e.type    = 4U;
                e.count   = 1U;
                e.payload = payload;
                return;
            }
        }
        ParsedTiffIfdEntry p;
        p.tag     = tag;
        p.type    = 4U;
        p.count   = 1U;
        p.payload = std::move(payload);
        ifd->entries.push_back(std::move(p));
    }

    static bool align_tiff_tail(std::vector<std::byte>* tail,
                                uint32_t base_offset, std::string* err) noexcept
    {
        if (!tail) {
            return false;
        }
        const uint64_t abs_size = static_cast<uint64_t>(base_offset)
                                  + static_cast<uint64_t>(tail->size());
        if (abs_size > static_cast<uint64_t>(0xFFFFFFFFU)) {
            if (err) {
                *err = "tiff output exceeds classic 32-bit offset range";
            }
            return false;
        }
        if ((abs_size & 1U) != 0U) {
            tail->push_back(std::byte { 0x00 });
        }
        return true;
    }

    static bool
    append_serialized_ifd(std::vector<std::byte>* out, uint32_t base_offset,
                          const ParsedTiffIfd& ifd, TiffEndian endian,
                          uint32_t* out_ifd_offset, std::string* err) noexcept
    {
        if (!out || !out_ifd_offset || !ifd.present) {
            return false;
        }
        if (ifd.entries.size() > static_cast<size_t>(0xFFFFU)) {
            if (err) {
                *err = "too many EXIF sub-IFD entries";
            }
            return false;
        }

        if (!align_tiff_tail(out, base_offset, err)) {
            return false;
        }
        const uint64_t dir_off_u64 = static_cast<uint64_t>(base_offset)
                                     + static_cast<uint64_t>(out->size());
        if (dir_off_u64 > static_cast<uint64_t>(0xFFFFFFFFU)) {
            if (err) {
                *err = "tiff output exceeds classic 32-bit offset range";
            }
            return false;
        }
        const uint32_t dir_off = static_cast<uint32_t>(dir_off_u64);
        *out_ifd_offset        = dir_off;

        struct Placement final {
            bool inline_value     = false;
            uint32_t value_offset = 0U;
            std::array<std::byte, 4> inline_bytes {};
        };

        std::vector<Placement> placements(ifd.entries.size());
        uint32_t cursor = dir_off + 2U
                          + static_cast<uint32_t>(ifd.entries.size()) * 12U
                          + 4U;

        for (size_t i = 0; i < ifd.entries.size(); ++i) {
            const ParsedTiffIfdEntry& e = ifd.entries[i];
            if (e.payload.size() <= 4U) {
                placements[i].inline_value = true;
                for (size_t bi = 0; bi < e.payload.size(); ++bi) {
                    placements[i].inline_bytes[bi] = e.payload[bi];
                }
                continue;
            }
            cursor                     = (cursor + 1U) & ~1U;
            placements[i].value_offset = cursor;
            cursor += static_cast<uint32_t>(e.payload.size());
        }

        if (cursor > static_cast<uint32_t>(0xFFFFFFFFU)) {
            if (err) {
                *err = "tiff output exceeds classic 32-bit offset range";
            }
            return false;
        }
        if (cursor < base_offset) {
            if (err) {
                *err = "serialized EXIF sub-IFD offset underflow";
            }
            return false;
        }
        out->resize(static_cast<size_t>(cursor - base_offset),
                    std::byte { 0x00 });

        size_t p = static_cast<size_t>(dir_off - base_offset);
        write_u16_tiff(out, p, static_cast<uint16_t>(ifd.entries.size()),
                       endian);
        p += 2U;
        for (size_t i = 0; i < ifd.entries.size(); ++i) {
            const ParsedTiffIfdEntry& e = ifd.entries[i];
            write_u16_tiff(out, p + 0U, e.tag, endian);
            write_u16_tiff(out, p + 2U, e.type, endian);
            write_u32_tiff(out, p + 4U, e.count, endian);
            if (placements[i].inline_value) {
                for (size_t bi = 0; bi < 4U; ++bi) {
                    (*out)[p + 8U + bi] = placements[i].inline_bytes[bi];
                }
            } else {
                write_u32_tiff(out, p + 8U, placements[i].value_offset, endian);
            }
            p += 12U;
        }
        write_u32_tiff(out, p, 0U, endian);

        for (size_t i = 0; i < ifd.entries.size(); ++i) {
            const ParsedTiffIfdEntry& e = ifd.entries[i];
            if (placements[i].inline_value || e.payload.empty()) {
                continue;
            }
            const size_t off = static_cast<size_t>(placements[i].value_offset
                                                   - base_offset);
            if (off + e.payload.size() > out->size()) {
                if (err) {
                    *err = "serialized EXIF sub-IFD payload out of range";
                }
                return false;
            }
            std::memcpy(out->data() + off, e.payload.data(), e.payload.size());
        }

        return true;
    }

    static bool
    build_tiff_rewrite_tail(std::span<const std::byte> input,
                            const std::vector<TiffTagUpdate>& updates,
                            std::span<const std::byte> exif_app1_payload,
                            TiffRewriteTail* out, std::string* err) noexcept
    {
        if (!out) {
            return false;
        }
        out->tail_bytes.clear();
        out->new_ifd0_off = 0U;

        if (updates.empty() && exif_app1_payload.empty()) {
            if (err) {
                *err = "no tiff updates";
            }
            return false;
        }
        if (input.size() < 8U) {
            if (err) {
                *err = "tiff input too small";
            }
            return false;
        }
        if (input.size() > static_cast<size_t>(0xFFFFFFFFU)) {
            if (err) {
                *err = "tiff output exceeds classic 32-bit offset range";
            }
            return false;
        }

        TiffEndian endian = TiffEndian::Little;
        if (input[0] == std::byte { 'I' } && input[1] == std::byte { 'I' }) {
            endian = TiffEndian::Little;
        } else if (input[0] == std::byte { 'M' }
                   && input[1] == std::byte { 'M' }) {
            endian = TiffEndian::Big;
        } else {
            if (err) {
                *err = "unsupported tiff byte order";
            }
            return false;
        }
        const uint16_t magic = read_u16_tiff(input, 2U, endian);
        if (magic != 42U) {
            if (err) {
                *err = "unsupported tiff variant (expected classic tiff)";
            }
            return false;
        }

        const uint32_t ifd0_off_u32 = read_u32_tiff(input, 4U, endian);
        const size_t ifd0_off       = static_cast<size_t>(ifd0_off_u32);
        if (ifd0_off + 2U > input.size()) {
            if (err) {
                *err = "ifd0 offset out of range";
            }
            return false;
        }
        const uint16_t count_u16 = read_u16_tiff(input, ifd0_off, endian);
        const size_t count       = static_cast<size_t>(count_u16);
        const size_t entries_off = ifd0_off + 2U;
        const size_t entries_end = entries_off + count * 12U;
        if (entries_end + 4U > input.size()) {
            if (err) {
                *err = "ifd0 entries truncated";
            }
            return false;
        }
        const uint32_t next_ifd_off = read_u32_tiff(input, entries_end, endian);

        ParsedTransferExif parsed_exif;
        if (!exif_app1_payload.empty()
            && !parse_exif_app1_for_tiff_transfer(exif_app1_payload,
                                                  &parsed_exif, err)) {
            return false;
        }
        if (parsed_exif.interop_ifd.present && !parsed_exif.exif_ifd.present) {
            if (err) {
                *err = "interop IFD present without ExifIFD";
            }
            return false;
        }
        if (!convert_transfer_exif_endian(&parsed_exif, endian)) {
            if (err) {
                *err = "failed to convert EXIF payload endian for target TIFF";
            }
            return false;
        }

        std::vector<TiffTagUpdate> merged_updates = updates;
        for (size_t i = 0; i < parsed_exif.ifd0_updates.size(); ++i) {
            const TiffTagUpdate& src = parsed_exif.ifd0_updates[i];
            bool replaced            = false;
            for (size_t j = 0; j < merged_updates.size(); ++j) {
                if (merged_updates[j].tag == src.tag) {
                    merged_updates[j] = src;
                    replaced          = true;
                    break;
                }
            }
            if (!replaced) {
                merged_updates.push_back(src);
            }
        }

        const bool need_exif_ptr = parsed_exif.exif_ifd.present;
        const bool need_gps_ptr  = parsed_exif.gps_ifd.present;

        std::vector<TiffIfdEntry> final_entries;
        final_entries.reserve(count + merged_updates.size() + 2U);

        for (size_t i = 0; i < count; ++i) {
            const size_t p     = entries_off + i * 12U;
            const uint16_t tag = read_u16_tiff(input, p + 0U, endian);
            if (has_update_for_tag(merged_updates, tag)
                || (need_exif_ptr && tag == 0x8769U)
                || (need_gps_ptr && tag == 0x8825U)) {
                continue;
            }
            TiffIfdEntry e;
            e.tag          = tag;
            e.type         = read_u16_tiff(input, p + 2U, endian);
            e.count        = read_u32_tiff(input, p + 4U, endian);
            e.value_or_off = read_u32_tiff(input, p + 8U, endian);
            final_entries.push_back(e);
        }

        for (size_t u = 0; u < merged_updates.size(); ++u) {
            const TiffTagUpdate& src = merged_updates[u];
            TiffIfdEntry e;
            e.tag                 = src.tag;
            e.type                = src.type;
            e.count               = src.count;
            e.has_rewrite_payload = true;
            e.rewrite_payload     = src.payload;
            if (e.count == 0U) {
                const uint32_t sz = tiff_type_size(e.type);
                if (sz == 0U) {
                    if (err) {
                        *err = "unsupported update tag type";
                    }
                    return false;
                }
                if (e.rewrite_payload.size() % sz != 0U) {
                    if (err) {
                        *err = "update payload size/type mismatch";
                    }
                    return false;
                }
                e.count = static_cast<uint32_t>(e.rewrite_payload.size() / sz);
            }
            final_entries.push_back(std::move(e));
        }

        if (need_exif_ptr) {
            TiffIfdEntry e;
            e.tag                 = 0x8769U;
            e.type                = 4U;
            e.count               = 1U;
            e.has_rewrite_payload = true;
            e.rewrite_payload.resize(4U, std::byte { 0x00 });
            final_entries.push_back(std::move(e));
        }
        if (need_gps_ptr) {
            TiffIfdEntry e;
            e.tag                 = 0x8825U;
            e.type                = 4U;
            e.count               = 1U;
            e.has_rewrite_payload = true;
            e.rewrite_payload.resize(4U, std::byte { 0x00 });
            final_entries.push_back(std::move(e));
        }

        std::sort(final_entries.begin(), final_entries.end(),
                  TiffIfdEntryLess {});

        const uint32_t base_offset   = static_cast<uint32_t>(input.size());
        std::vector<std::byte>& tail = out->tail_bytes;
        if (!align_tiff_tail(&tail, base_offset, err)) {
            return false;
        }

        for (size_t i = 0; i < final_entries.size(); ++i) {
            TiffIfdEntry& e = final_entries[i];
            if (!e.has_rewrite_payload) {
                continue;
            }
            if (e.rewrite_payload.size() <= 4U) {
                e.has_inline_payload = true;
                e.inline_payload     = { std::byte { 0x00 }, std::byte { 0x00 },
                                         std::byte { 0x00 }, std::byte { 0x00 } };
                for (size_t bi = 0; bi < e.rewrite_payload.size(); ++bi) {
                    e.inline_payload[bi] = e.rewrite_payload[bi];
                }
                continue;
            }
            if (!align_tiff_tail(&tail, base_offset, err)) {
                return false;
            }
            const uint64_t value_off_u64 = static_cast<uint64_t>(base_offset)
                                           + static_cast<uint64_t>(tail.size());
            if (value_off_u64 > static_cast<uint64_t>(0xFFFFFFFFU)) {
                if (err) {
                    *err = "tiff output exceeds classic 32-bit offset range";
                }
                return false;
            }
            e.value_or_off = static_cast<uint32_t>(value_off_u64);
            tail.insert(tail.end(), e.rewrite_payload.begin(),
                        e.rewrite_payload.end());
        }

        uint32_t interop_ifd_off = 0U;
        if (parsed_exif.interop_ifd.present
            && !append_serialized_ifd(&tail, base_offset,
                                      parsed_exif.interop_ifd, endian,
                                      &interop_ifd_off, err)) {
            return false;
        }

        uint32_t exif_ifd_off = 0U;
        if (parsed_exif.exif_ifd.present) {
            ParsedTiffIfd exif_ifd = parsed_exif.exif_ifd;
            if (parsed_exif.interop_ifd.present) {
                upsert_ifd_pointer(&exif_ifd, 0xA005U, interop_ifd_off, endian);
            }
            if (!append_serialized_ifd(&tail, base_offset, exif_ifd, endian,
                                       &exif_ifd_off, err)) {
                return false;
            }
        }

        uint32_t gps_ifd_off = 0U;
        if (parsed_exif.gps_ifd.present
            && !append_serialized_ifd(&tail, base_offset, parsed_exif.gps_ifd,
                                      endian, &gps_ifd_off, err)) {
            return false;
        }

        if (exif_ifd_off != 0U || gps_ifd_off != 0U) {
            for (size_t i = 0; i < final_entries.size(); ++i) {
                TiffIfdEntry& e = final_entries[i];
                if (e.tag == 0x8769U && exif_ifd_off != 0U) {
                    e.type               = 4U;
                    e.count              = 1U;
                    e.has_inline_payload = true;
                    e.inline_payload = { std::byte { 0x00 }, std::byte { 0x00 },
                                         std::byte { 0x00 },
                                         std::byte { 0x00 } };
                    std::vector<std::byte> tmp;
                    append_u32_tiff(&tmp, exif_ifd_off, endian);
                    for (size_t bi = 0; bi < 4U; ++bi) {
                        e.inline_payload[bi] = tmp[bi];
                    }
                    continue;
                }
                if (e.tag == 0x8825U && gps_ifd_off != 0U) {
                    e.type               = 4U;
                    e.count              = 1U;
                    e.has_inline_payload = true;
                    e.inline_payload = { std::byte { 0x00 }, std::byte { 0x00 },
                                         std::byte { 0x00 },
                                         std::byte { 0x00 } };
                    std::vector<std::byte> tmp;
                    append_u32_tiff(&tmp, gps_ifd_off, endian);
                    for (size_t bi = 0; bi < 4U; ++bi) {
                        e.inline_payload[bi] = tmp[bi];
                    }
                }
            }
        }

        if (!align_tiff_tail(&tail, base_offset, err)) {
            return false;
        }
        const uint64_t new_ifd0_off_u64 = static_cast<uint64_t>(base_offset)
                                          + static_cast<uint64_t>(tail.size());
        if (new_ifd0_off_u64 > static_cast<uint64_t>(0xFFFFFFFFU)) {
            if (err) {
                *err = "tiff output exceeds classic 32-bit offset range";
            }
            return false;
        }
        out->new_ifd0_off = static_cast<uint32_t>(new_ifd0_off_u64);

        if (final_entries.size() > static_cast<size_t>(0xFFFFU)) {
            if (err) {
                *err = "too many tiff ifd0 entries";
            }
            return false;
        }
        const size_t ifd_bytes     = 2U + final_entries.size() * 12U + 4U;
        const size_t local_ifd_off = tail.size();
        tail.resize(tail.size() + ifd_bytes, std::byte { 0x00 });
        size_t w = local_ifd_off;
        write_u16_tiff(&tail, w, static_cast<uint16_t>(final_entries.size()),
                       endian);
        w += 2U;
        for (size_t i = 0; i < final_entries.size(); ++i) {
            const TiffIfdEntry& e = final_entries[i];
            write_u16_tiff(&tail, w + 0U, e.tag, endian);
            write_u16_tiff(&tail, w + 2U, e.type, endian);
            write_u32_tiff(&tail, w + 4U, e.count, endian);
            if (e.has_inline_payload) {
                for (size_t k = 0; k < 4U; ++k) {
                    tail[w + 8U + k] = e.inline_payload[k];
                }
            } else {
                write_u32_tiff(&tail, w + 8U, e.value_or_off, endian);
            }
            w += 12U;
        }
        write_u32_tiff(&tail, w, next_ifd_off, endian);
        out->endian = endian;
        return true;
    }

    static bool
    rewrite_tiff_ifd0_tags(std::span<const std::byte> input,
                           const std::vector<TiffTagUpdate>& updates,
                           std::span<const std::byte> exif_app1_payload,
                           std::vector<std::byte>* out,
                           std::string* err) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();

        TiffRewriteTail rewrite;
        if (!build_tiff_rewrite_tail(input, updates, exif_app1_payload,
                                     &rewrite, err)) {
            return false;
        }

        out->assign(input.begin(), input.end());
        write_u32_tiff(out, 4U, rewrite.new_ifd0_off, rewrite.endian);
        out->insert(out->end(), rewrite.tail_bytes.begin(),
                    rewrite.tail_bytes.end());
        return true;
    }

    static TransferStatus
    tiff_edit_status_from_error(std::string_view message) noexcept
    {
        if (message.empty()) {
            return TransferStatus::Malformed;
        }
        if (starts_with(message, "unsupported")
            || starts_with(message, "no tiff updates")) {
            return TransferStatus::Unsupported;
        }
        if (starts_with(message, "tiff output exceeds")
            || starts_with(message, "too many tiff")) {
            return TransferStatus::LimitExceeded;
        }
        return TransferStatus::Malformed;
    }

}  // namespace

static PrepareTransferResult
prepare_metadata_for_target_impl(const MetaStore& store,
                                 const PrepareTransferRequest& request,
                                 const TransferPrepareCapabilities& caps,
                                 PreparedTransferBundle* out_bundle) noexcept
{
    PrepareTransferResult r;
    if (!out_bundle) {
        r.status  = TransferStatus::InvalidArgument;
        r.code    = PrepareTransferCode::NullOutBundle;
        r.errors  = 1U;
        r.message = "out_bundle is null";
        return r;
    }

    PreparedTransferBundle bundle;
    bundle.target_format = request.target_format;
    bundle.profile       = request.profile;

    if (request.target_format != TransferTargetFormat::Jpeg
        && request.target_format != TransferTargetFormat::Tiff
        && request.target_format != TransferTargetFormat::Jxl
        && request.target_format != TransferTargetFormat::Webp
        && !transfer_target_is_bmff(request.target_format)) {
        r.status = TransferStatus::Unsupported;
        r.code   = PrepareTransferCode::UnsupportedTargetFormat;
        r.errors = 1U;
        r.message
            = "prepare currently supports jpeg, tiff, jxl, webp, and bmff targets";
        *out_bundle = std::move(bundle);
        return r;
    }

    const bool has_exif = has_kind(store, MetaKeyKind::ExifTag);
    const bool has_iptc = has_kind(store, MetaKeyKind::IptcDataset)
                          || has_kind(store, MetaKeyKind::PhotoshopIrb);
    const bool has_icc = has_kind(store, MetaKeyKind::IccHeaderField)
                         || has_kind(store, MetaKeyKind::IccTag);
    const uint32_t makernote_count = count_makernote_entries(store);
    const uint32_t jumbf_count     = count_jumbf_entries(store);
    const uint32_t c2pa_count      = count_c2pa_entries(store);

    bool requested_present_but_unpacked = false;

    TransferPolicyAction effective_makernote = request.profile.makernote;
    TransferPolicyReason makernote_reason    = TransferPolicyReason::NotPresent;
    if (makernote_count == 0U) {
        append_policy_decision(&bundle, TransferPolicySubject::MakerNote,
                               request.profile.makernote,
                               request.profile.makernote,
                               TransferPolicyReason::NotPresent, 0U,
                               "no maker note entries in source metadata");
    } else if (!request.include_exif_app1) {
        effective_makernote = TransferPolicyAction::Drop;
        makernote_reason    = TransferPolicyReason::CarrierDisabled;
        append_policy_decision(&bundle, TransferPolicySubject::MakerNote,
                               request.profile.makernote, effective_makernote,
                               makernote_reason, makernote_count,
                               "maker notes require EXIF transfer; EXIF output "
                               "is disabled");
    } else {
        effective_makernote = resolve_makernote_policy(request.profile.makernote,
                                                       &makernote_reason);
        if (makernote_reason
            == TransferPolicyReason::PortableInvalidationUnavailable) {
            add_prepare_warning(
                &r, PrepareTransferCode::RequestedMetadataNotSerializable,
                "maker note invalidation is not portable; dropping maker "
                "notes");
        } else if (makernote_reason
                   == TransferPolicyReason::RewriteUnavailablePreservedRaw) {
            add_prepare_warning(
                &r, PrepareTransferCode::RequestedMetadataNotSerializable,
                "maker note rewrite is not implemented; preserving raw maker "
                "notes");
        }
        append_policy_decision(
            &bundle, TransferPolicySubject::MakerNote,
            request.profile.makernote, effective_makernote, makernote_reason,
            makernote_count,
            makernote_reason == TransferPolicyReason::ExplicitDrop
                ? "maker notes will be dropped from prepared EXIF"
                : (makernote_reason
                           == TransferPolicyReason::PortableInvalidationUnavailable
                       ? "maker notes have no portable invalidation path; "
                         "dropping from prepared EXIF"
                       : (makernote_reason
                                  == TransferPolicyReason::
                                      RewriteUnavailablePreservedRaw
                              ? "maker note rewrite is unavailable; preserving "
                                "raw maker note payload"
                              : "maker notes will be preserved in prepared "
                                "EXIF")));
    }

    const bool can_pack_source_jumbf
        = (caps.jpeg_jumbf_passthrough
           && request.target_format == TransferTargetFormat::Jpeg)
          || (caps.jxl_jumbf_passthrough
              && request.target_format == TransferTargetFormat::Jxl)
          || (caps.bmff_jumbf_passthrough
              && transfer_target_is_bmff(request.target_format));
    const bool can_project_store_jumbf
        = !can_pack_source_jumbf
          && (request.target_format == TransferTargetFormat::Jpeg
              || request.target_format == TransferTargetFormat::Jxl
              || transfer_target_is_bmff(request.target_format))
          && count_non_c2pa_jumbf_cbor_entries(store) > 0U;

    TransferPolicyAction effective_jumbf = request.profile.jumbf;
    TransferPolicyReason jumbf_reason    = TransferPolicyReason::NotPresent;
    if (jumbf_count == 0U) {
        append_policy_decision(&bundle, TransferPolicySubject::Jumbf,
                               request.profile.jumbf, request.profile.jumbf,
                               TransferPolicyReason::NotPresent, 0U,
                               "no jumbf entries in source metadata");
    } else if (can_pack_source_jumbf) {
        effective_jumbf = resolve_raw_jumbf_policy(request.profile.jumbf,
                                                   &jumbf_reason);
        if (jumbf_reason
            == TransferPolicyReason::PortableInvalidationUnavailable) {
            add_prepare_warning(
                &r, PrepareTransferCode::RequestedMetadataNotSerializable,
                "jumbf invalidation is not available; dropping jumbf data");
        } else if (jumbf_reason
                   == TransferPolicyReason::RewriteUnavailablePreservedRaw) {
            add_prepare_warning(
                &r, PrepareTransferCode::RequestedMetadataNotSerializable,
                "jumbf rewrite is not implemented; preserving raw jumbf "
                "payloads");
        }
        append_policy_decision(
            &bundle, TransferPolicySubject::Jumbf, request.profile.jumbf,
            effective_jumbf, jumbf_reason, jumbf_count,
            jumbf_reason == TransferPolicyReason::ExplicitDrop
                ? "jumbf transfer disabled by profile"
                : (jumbf_reason
                           == TransferPolicyReason::PortableInvalidationUnavailable
                       ? "jumbf invalidation is not available; dropping "
                         "jumbf data"
                       : (jumbf_reason
                                  == TransferPolicyReason::
                                      RewriteUnavailablePreservedRaw
                              ? "jumbf rewrite is unavailable; preserving raw "
                                "jumbf payloads"
                              : (request.target_format
                                         == TransferTargetFormat::Jxl
                                     ? "source jumbf payloads will be emitted "
                                       "as jxl boxes"
                                     : (transfer_target_is_bmff(
                                            request.target_format)
                                            ? "source jumbf payloads will be "
                                              "emitted as bmff metadata items"
                                            : "source jumbf payloads will be "
                                              "repacked into jpeg app11 "
                                              "segments")))));
    } else if (can_project_store_jumbf) {
        effective_jumbf = resolve_projected_jumbf_policy(request.profile.jumbf,
                                                         &jumbf_reason);
        if (jumbf_reason
            == TransferPolicyReason::PortableInvalidationUnavailable) {
            add_prepare_warning(
                &r, PrepareTransferCode::RequestedMetadataNotSerializable,
                "jumbf invalidation is not available; dropping jumbf data");
        }
        append_policy_decision(
            &bundle, TransferPolicySubject::Jumbf, request.profile.jumbf,
            effective_jumbf, jumbf_reason, jumbf_count,
            jumbf_reason == TransferPolicyReason::ExplicitDrop
                ? "jumbf transfer disabled by profile"
                : (jumbf_reason
                           == TransferPolicyReason::PortableInvalidationUnavailable
                       ? "jumbf invalidation is not available; dropping "
                         "jumbf data"
                       : (request.target_format == TransferTargetFormat::Jxl
                              ? "decoded non-c2pa jumbf cbor keys will be "
                                "projected into generic jxl jumbf boxes"
                              : (transfer_target_is_bmff(request.target_format)
                                     ? "decoded non-c2pa jumbf cbor keys will "
                                       "be projected into bmff metadata items"
                                     : "decoded non-c2pa jumbf cbor keys will "
                                       "be projected into generic jpeg app11 "
                                       "jumbf payloads"))));
    } else {
        effective_jumbf = resolve_unserialized_policy(request.profile.jumbf,
                                                      &jumbf_reason);
        if (request.profile.jumbf != TransferPolicyAction::Drop) {
            requested_present_but_unpacked = true;
            add_prepare_warning(
                &r, PrepareTransferCode::RequestedMetadataNotSerializable,
                "jumbf transfer is not yet serialized for jpeg/tiff targets; "
                "dropping jumbf data");
        }
        append_policy_decision(
            &bundle, TransferPolicySubject::Jumbf, request.profile.jumbf,
            effective_jumbf, jumbf_reason, jumbf_count,
            request.profile.jumbf == TransferPolicyAction::Drop
                ? "jumbf transfer disabled by profile"
                : "jumbf transfer is not yet serialized for current targets");
    }

    TransferPolicyAction effective_c2pa = request.profile.c2pa;
    TransferPolicyReason c2pa_reason    = TransferPolicyReason::NotPresent;
    TransferC2paMode c2pa_mode          = TransferC2paMode::NotPresent;
    TransferC2paSourceKind c2pa_source_kind = TransferC2paSourceKind::NotPresent;
    TransferC2paPreparedOutput c2pa_prepared_output
        = TransferC2paPreparedOutput::NotPresent;
    if (c2pa_count == 0U) {
        append_policy_decision(&bundle, TransferPolicySubject::C2pa,
                               request.profile.c2pa, request.profile.c2pa,
                               TransferPolicyReason::NotPresent, 0U,
                               "no c2pa entries in source metadata",
                               TransferC2paMode::NotPresent,
                               TransferC2paSourceKind::NotPresent,
                               TransferC2paPreparedOutput::NotPresent);
    } else {
        const bool can_generate_c2pa_invalidation
            = request.target_format == TransferTargetFormat::Jpeg
              || request.target_format == TransferTargetFormat::Jxl
              || request.target_format == TransferTargetFormat::Webp
              || transfer_target_is_bmff(request.target_format);
        effective_c2pa = resolve_c2pa_transfer_policy(
            request.profile.c2pa, can_pack_source_jumbf,
            can_generate_c2pa_invalidation, caps.source_c2pa_payload_class,
            &c2pa_reason, &c2pa_mode);
        c2pa_source_kind
            = classify_c2pa_source_kind(c2pa_count, can_pack_source_jumbf,
                                        caps.source_c2pa_payload_class);
        c2pa_prepared_output = classify_c2pa_prepared_output(c2pa_count,
                                                             effective_c2pa,
                                                             c2pa_mode);
        if (request.profile.c2pa != TransferPolicyAction::Drop
            && effective_c2pa == TransferPolicyAction::Drop) {
            std::string_view warning_message
                = "c2pa transfer is not yet serialized for current targets; "
                  "dropping c2pa data";
            if (c2pa_reason == TransferPolicyReason::SignedRewriteUnavailable) {
                warning_message
                    = "c2pa signed rewrite/re-sign is not implemented; "
                      "dropping c2pa data";
            } else if (c2pa_reason
                       == TransferPolicyReason::ContentBoundTransferUnavailable) {
                warning_message
                    = "c2pa transfer is content-bound; dropping c2pa data "
                      "until invalidation/rewrite is available";
            } else if (c2pa_reason
                       == TransferPolicyReason::PortableInvalidationUnavailable) {
                warning_message
                    = "c2pa invalidation is not available for current "
                      "targets; dropping c2pa data";
            }
            requested_present_but_unpacked = true;
            add_prepare_warning(
                &r, PrepareTransferCode::RequestedMetadataNotSerializable,
                warning_message);
        }
        std::string_view policy_message = "c2pa transfer is not yet "
                                          "serialized for current targets";
        if (request.profile.c2pa == TransferPolicyAction::Drop) {
            policy_message = "c2pa transfer disabled by profile";
        } else if (c2pa_mode == TransferC2paMode::PreserveRaw) {
            if (request.target_format == TransferTargetFormat::Jxl) {
                policy_message = "existing draft unsigned c2pa invalidation "
                                 "payload will be preserved as raw jxl box "
                                 "data";
            } else if (request.target_format == TransferTargetFormat::Webp) {
                policy_message = "existing draft unsigned c2pa invalidation "
                                 "payload will be preserved as raw webp c2pa "
                                 "chunk data";
            } else if (transfer_target_is_bmff(request.target_format)) {
                policy_message = "existing draft unsigned c2pa invalidation "
                                 "payload will be preserved as raw bmff "
                                 "metadata item data";
            } else {
                policy_message = "existing draft unsigned c2pa invalidation "
                                 "payload will be preserved as raw jpeg "
                                 "app11 data";
            }
        } else if (c2pa_reason
                   == TransferPolicyReason::DraftInvalidationPayload) {
            if (request.target_format == TransferTargetFormat::Jxl) {
                policy_message = "draft unsigned c2pa invalidation payload "
                                 "will be emitted for content-changing jxl "
                                 "outputs";
            } else if (request.target_format == TransferTargetFormat::Webp) {
                policy_message = "draft unsigned c2pa invalidation payload "
                                 "will be emitted for content-changing webp "
                                 "outputs";
            } else if (transfer_target_is_bmff(request.target_format)) {
                policy_message = "draft unsigned c2pa invalidation payload "
                                 "will be emitted for content-changing bmff "
                                 "outputs";
            } else {
                policy_message = "draft unsigned c2pa invalidation payload "
                                 "will be emitted for content-changing jpeg "
                                 "outputs";
            }
        } else if (c2pa_reason
                   == TransferPolicyReason::SignedRewriteUnavailable) {
            policy_message = "c2pa signed rewrite/re-sign is not implemented "
                             "for current targets";
        } else if (can_pack_source_jumbf) {
            policy_message = "c2pa transfer is content-bound; dropping until "
                             "invalidation/rewrite is available";
        }
        append_policy_decision(&bundle, TransferPolicySubject::C2pa,
                               request.profile.c2pa, effective_c2pa,
                               c2pa_reason, c2pa_count, policy_message,
                               c2pa_mode, c2pa_source_kind,
                               c2pa_prepared_output);
    }
    bundle.c2pa_rewrite = build_c2pa_rewrite_requirements(request.target_format,
                                                          request.profile.c2pa,
                                                          c2pa_count,
                                                          c2pa_source_kind);

    if (request.include_exif_app1 && has_exif) {
        ExifPackBuild exif_build
            = build_jpeg_exif_app1_payload(store, effective_makernote);
        if (exif_build.produced && !exif_build.app1_payload.empty()) {
            const uint32_t block_index = static_cast<uint32_t>(
                bundle.blocks.size());
            PreparedTransferBlock b;
            b.kind  = TransferBlockKind::Exif;
            b.order = 100U;
            if (request.target_format == TransferTargetFormat::Jpeg) {
                b.route   = "jpeg:app1-exif";
                b.payload = std::move(exif_build.app1_payload);
            } else if (request.target_format == TransferTargetFormat::Tiff) {
                // TIFF backends can consume this as a serialized Exif APP1 blob
                // and materialize ExifIFD pointers/entries natively.
                b.route   = "tiff:ifd-exif-app1";
                b.payload = std::move(exif_build.app1_payload);
            } else if (transfer_target_is_bmff(request.target_format)) {
                b.route = "bmff:item-exif";
                append_u32be(&b.payload, 6U);
                b.payload.insert(b.payload.end(),
                                 exif_build.app1_payload.begin(),
                                 exif_build.app1_payload.end());
            } else if (request.target_format == TransferTargetFormat::Webp) {
                b.route   = "webp:chunk-exif";
                b.payload = std::move(exif_build.app1_payload);
            } else {
                b.route    = "jxl:box-exif";
                b.box_type = { 'E', 'x', 'i', 'f' };
                append_u32be(&b.payload, 6U);
                b.payload.insert(b.payload.end(),
                                 exif_build.app1_payload.begin(),
                                 exif_build.app1_payload.end());
            }
            bundle.blocks.push_back(std::move(b));
            for (size_t i = 0; i < exif_build.time_patch_map.size(); ++i) {
                TimePatchSlot slot = exif_build.time_patch_map[i];
                slot.block_index   = block_index;
                if (request.target_format == TransferTargetFormat::Jxl) {
                    slot.byte_offset = static_cast<uint32_t>(slot.byte_offset
                                                             + 4U);
                }
                bundle.time_patch_map.push_back(slot);
            }
            if (exif_build.skipped_count > 0U) {
                r.warnings += 1U;
                append_message(&r.message,
                               "exif serializer skipped "
                                   + std::to_string(exif_build.skipped_count)
                                   + " unsupported exif entries");
            }
        } else {
            requested_present_but_unpacked = true;
            if (r.code == PrepareTransferCode::None) {
                r.code = PrepareTransferCode::ExifPackFailed;
            }
            r.warnings += 1U;
            append_message(
                &r.message,
                "exif app1 packer could not serialize current exif set");
        }
    }

    if (request.include_xmp_app1) {
        XmpSidecarRequest xmp_req;
        xmp_req.format       = request.xmp_portable ? XmpSidecarFormat::Portable
                                                    : XmpSidecarFormat::Lossless;
        xmp_req.include_exif = true;
        xmp_req.include_existing_xmp = request.xmp_include_existing;
        xmp_req.portable_exiftool_gpsdatetime_alias
            = request.xmp_exiftool_gpsdatetime_alias;

        std::vector<std::byte> xmp_packet;
        const XmpDumpResult xr = dump_xmp_sidecar(store, &xmp_packet, xmp_req);
        if (xr.status == XmpDumpStatus::Ok && !xmp_packet.empty()) {
            PreparedTransferBlock b;
            b.kind  = TransferBlockKind::Xmp;
            b.order = 110U;
            if (request.target_format == TransferTargetFormat::Jpeg) {
                b.route = "jpeg:app1-xmp";
                append_ascii_bytes(&b.payload, "http://ns.adobe.com/xap/1.0/");
                b.payload.push_back(std::byte { 0x00 });
                b.payload.insert(b.payload.end(), xmp_packet.begin(),
                                 xmp_packet.end());
            } else if (request.target_format == TransferTargetFormat::Tiff) {
                b.route   = "tiff:tag-700-xmp";
                b.payload = std::move(xmp_packet);
            } else if (transfer_target_is_bmff(request.target_format)) {
                b.route   = "bmff:item-xmp";
                b.payload = std::move(xmp_packet);
            } else if (request.target_format == TransferTargetFormat::Webp) {
                b.route   = "webp:chunk-xmp";
                b.payload = std::move(xmp_packet);
            } else {
                b.route    = "jxl:box-xml";
                b.box_type = { 'x', 'm', 'l', ' ' };
                b.payload  = std::move(xmp_packet);
            }
            bundle.blocks.push_back(std::move(b));
        } else if (xr.status != XmpDumpStatus::Ok) {
            if (r.code == PrepareTransferCode::None) {
                r.code = PrepareTransferCode::XmpPackFailed;
            }
            r.warnings += 1U;
            append_message(&r.message, "xmp packet generation failed");
        }
    }

    if (request.include_icc_app2 && has_icc) {
        if (request.target_format == TransferTargetFormat::Jpeg) {
            IccPackBuild icc_build = build_jpeg_icc_app2_blocks(store);
            if (icc_build.produced && !icc_build.blocks.empty()) {
                for (PreparedTransferBlock& b : icc_build.blocks) {
                    bundle.blocks.push_back(std::move(b));
                }
                if (icc_build.skipped_count > 0U) {
                    r.warnings += 1U;
                    append_message(&r.message,
                                   "icc serializer skipped "
                                       + std::to_string(icc_build.skipped_count)
                                       + " unsupported icc entries");
                }
            } else {
                requested_present_but_unpacked = true;
                if (r.code == PrepareTransferCode::None) {
                    r.code = PrepareTransferCode::IccPackFailed;
                }
                r.warnings += 1U;
                append_message(&r.message,
                               "icc app2 packer could not serialize current "
                               "icc set");
            }
        } else if (request.target_format == TransferTargetFormat::Tiff) {
            std::vector<std::byte> icc_profile;
            uint32_t skipped_icc = 0U;
            if (build_icc_profile_bytes(store, &icc_profile, &skipped_icc)
                && !icc_profile.empty()) {
                PreparedTransferBlock b;
                b.kind    = TransferBlockKind::Icc;
                b.order   = 120U;
                b.route   = "tiff:tag-34675-icc";
                b.payload = std::move(icc_profile);
                bundle.blocks.push_back(std::move(b));
                if (skipped_icc > 0U) {
                    r.warnings += 1U;
                    append_message(&r.message,
                                   "icc serializer skipped "
                                       + std::to_string(skipped_icc)
                                       + " unsupported icc entries");
                }
            } else {
                requested_present_but_unpacked = true;
                if (r.code == PrepareTransferCode::None) {
                    r.code = PrepareTransferCode::IccPackFailed;
                }
                r.warnings += 1U;
                append_message(&r.message,
                               "tiff icc packer could not serialize current "
                               "icc set");
            }
        } else if (request.target_format == TransferTargetFormat::Webp) {
            std::vector<std::byte> icc_profile;
            uint32_t skipped_icc = 0U;
            if (build_icc_profile_bytes(store, &icc_profile, &skipped_icc)
                && !icc_profile.empty()) {
                PreparedTransferBlock b;
                b.kind    = TransferBlockKind::Icc;
                b.order   = 120U;
                b.route   = "webp:chunk-iccp";
                b.payload = std::move(icc_profile);
                bundle.blocks.push_back(std::move(b));
                if (skipped_icc > 0U) {
                    r.warnings += 1U;
                    append_message(&r.message,
                                   "icc serializer skipped "
                                       + std::to_string(skipped_icc)
                                       + " unsupported icc entries");
                }
            } else {
                requested_present_but_unpacked = true;
                if (r.code == PrepareTransferCode::None) {
                    r.code = PrepareTransferCode::IccPackFailed;
                }
                r.warnings += 1U;
                append_message(&r.message,
                               "webp icc packer could not serialize current "
                               "icc set");
            }
        } else if (transfer_target_is_bmff(request.target_format)) {
            requested_present_but_unpacked = true;
            if (r.code == PrepareTransferCode::None) {
                r.code = PrepareTransferCode::IccPackFailed;
            }
            r.warnings += 1U;
            append_message(&r.message,
                           "bmff icc packaging is not implemented yet");
        } else {
            std::vector<std::byte> icc_profile;
            uint32_t skipped_icc = 0U;
            if (build_icc_profile_bytes(store, &icc_profile, &skipped_icc)
                && !icc_profile.empty()) {
                PreparedTransferBlock b;
                b.kind    = TransferBlockKind::Icc;
                b.order   = 120U;
                b.route   = "jxl:icc-profile";
                b.payload = std::move(icc_profile);
                bundle.blocks.push_back(std::move(b));
                if (skipped_icc > 0U) {
                    r.warnings += 1U;
                    append_message(&r.message,
                                   "icc serializer skipped "
                                       + std::to_string(skipped_icc)
                                       + " unsupported icc entries");
                }
            } else {
                requested_present_but_unpacked = true;
                if (r.code == PrepareTransferCode::None) {
                    r.code = PrepareTransferCode::IccPackFailed;
                }
                r.warnings += 1U;
                append_message(&r.message,
                               "jxl icc packer could not serialize current "
                               "icc set");
            }
        }
    }
    if (request.include_iptc_app13 && has_iptc) {
        if (request.target_format == TransferTargetFormat::Jpeg) {
            IptcPackBuild iptc_build = build_jpeg_iptc_app13_payload(store);
            if (iptc_build.produced && !iptc_build.app13_payload.empty()) {
                PreparedTransferBlock b;
                b.kind    = TransferBlockKind::IptcIim;
                b.order   = 130U;
                b.route   = "jpeg:app13-iptc";
                b.payload = std::move(iptc_build.app13_payload);
                bundle.blocks.push_back(std::move(b));
                if (iptc_build.skipped_count > 0U) {
                    r.warnings += 1U;
                    append_message(&r.message,
                                   "iptc serializer skipped "
                                       + std::to_string(iptc_build.skipped_count)
                                       + " unsupported iptc entries");
                }
            } else {
                requested_present_but_unpacked = true;
                if (r.code == PrepareTransferCode::None) {
                    r.code = PrepareTransferCode::IptcPackFailed;
                }
                r.warnings += 1U;
                append_message(
                    &r.message,
                    "iptc app13 packer could not serialize current iptc set");
            }
        } else if (request.target_format == TransferTargetFormat::Tiff) {
            std::vector<std::byte> iptc_iim;
            uint32_t skipped_iptc = 0U;
            if (!first_photoshop_iptc_payload(store, &iptc_iim)) {
                iptc_iim = build_iptc_iim_stream_from_datasets(store,
                                                               &skipped_iptc);
            }
            if (!iptc_iim.empty()) {
                PreparedTransferBlock b;
                b.kind    = TransferBlockKind::IptcIim;
                b.order   = 130U;
                b.route   = "tiff:tag-33723-iptc";
                b.payload = std::move(iptc_iim);
                bundle.blocks.push_back(std::move(b));
                if (skipped_iptc > 0U) {
                    r.warnings += 1U;
                    append_message(&r.message,
                                   "iptc serializer skipped "
                                       + std::to_string(skipped_iptc)
                                       + " unsupported iptc entries");
                }
            } else {
                requested_present_but_unpacked = true;
                if (r.code == PrepareTransferCode::None) {
                    r.code = PrepareTransferCode::IptcPackFailed;
                }
                r.warnings += 1U;
                append_message(
                    &r.message,
                    "tiff iptc packer could not serialize current iptc set");
            }
        } else {
            if (!request.include_xmp_app1) {
                XmpSidecarRequest xmp_req;
                xmp_req.format               = XmpSidecarFormat::Portable;
                xmp_req.include_exif         = false;
                xmp_req.include_existing_xmp = false;

                std::vector<std::byte> xmp_packet;
                const XmpDumpResult xr = dump_xmp_sidecar(store, &xmp_packet,
                                                          xmp_req);
                if (xr.status == XmpDumpStatus::Ok && !xmp_packet.empty()) {
                    PreparedTransferBlock b;
                    b.kind  = TransferBlockKind::Xmp;
                    b.order = 130U;
                    if (request.target_format == TransferTargetFormat::Webp) {
                        b.route = "webp:chunk-xmp";
                    } else if (transfer_target_is_bmff(request.target_format)) {
                        b.route = "bmff:item-xmp";
                    } else {
                        b.route    = "jxl:box-xml";
                        b.box_type = { 'x', 'm', 'l', ' ' };
                    }
                    b.payload = std::move(xmp_packet);
                    bundle.blocks.push_back(std::move(b));
                } else {
                    requested_present_but_unpacked = true;
                    if (r.code == PrepareTransferCode::None) {
                        r.code = PrepareTransferCode::IptcPackFailed;
                    }
                    r.warnings += 1U;
                    append_message(
                        &r.message,
                        request.target_format == TransferTargetFormat::Webp
                            ? "webp iptc projection to xmp could not serialize "
                              "current iptc set"
                            : (transfer_target_is_bmff(request.target_format)
                                   ? "bmff iptc projection to xmp could not "
                                     "serialize current iptc set"
                                   : "jxl iptc projection to xml could not serialize "
                                     "current iptc set"));
                }
            }
        }
    }

    if ((request.target_format == TransferTargetFormat::Jpeg
         || request.target_format == TransferTargetFormat::Jxl
         || transfer_target_is_bmff(request.target_format))
        && !can_pack_source_jumbf
        && effective_jumbf == TransferPolicyAction::Keep
        && jumbf_reason == TransferPolicyReason::ProjectedPayload) {
        std::vector<ProjectedJumbfPayload> projected_jumbf_payloads;
        std::string error;
        if (!build_projected_jumbf_logical_payloads(store,
                                                    &projected_jumbf_payloads,
                                                    &error)) {
            requested_present_but_unpacked = true;
            if (r.code == PrepareTransferCode::None) {
                r.code = PrepareTransferCode::RequestedMetadataNotSerializable;
            }
            r.warnings += 1U;
            append_message(&r.message,
                           error.empty()
                               ? "decoded jumbf cbor keys could not be projected"
                               : error);
            PreparedTransferPolicyDecision* decision
                = find_policy_decision(&bundle, TransferPolicySubject::Jumbf);
            if (decision) {
                decision->effective = TransferPolicyAction::Drop;
                decision->reason
                    = TransferPolicyReason::TargetSerializationUnavailable;
                decision->message
                    = error.empty()
                          ? "decoded jumbf cbor keys could not be projected "
                            "into generic jpeg app11 payloads"
                          : error;
            }
        } else {
            const size_t before = bundle.blocks.size();
            uint32_t order      = next_prepared_block_order(bundle, 140U);
            bool append_failed  = false;
            for (size_t i = 0; i < projected_jumbf_payloads.size(); ++i) {
                const ProjectedJumbfPayload& payload
                    = projected_jumbf_payloads[i];
                const std::span<const std::byte> logical(
                    payload.logical_payload.data(),
                    payload.logical_payload.size());
                bool appended = false;
                if (request.target_format == TransferTargetFormat::Jxl) {
                    appended = append_jxl_jumbf_box(logical,
                                                    TransferBlockKind::Jumbf,
                                                    &order, &bundle.blocks,
                                                    &error);
                } else if (transfer_target_is_bmff(request.target_format)) {
                    appended = append_bmff_metadata_item(
                        logical, "bmff:item-jumb", TransferBlockKind::Jumbf,
                        &order, &bundle.blocks, &error);
                } else {
                    appended = append_jpeg_app11_jumbf_segments(
                        logical, TransferBlockKind::Jumbf, &order,
                        &bundle.blocks, &error);
                }
                if (!appended) {
                    append_failed = true;
                    break;
                }
            }
            if (append_failed) {
                requested_present_but_unpacked = true;
                if (r.code == PrepareTransferCode::None) {
                    r.code
                        = PrepareTransferCode::RequestedMetadataNotSerializable;
                }
                r.warnings += 1U;
                append_message(&r.message,
                               error.empty()
                                   ? "projected jumbf payload could not be "
                                     "serialized for target container"
                                   : error);
                PreparedTransferPolicyDecision* decision
                    = find_policy_decision(&bundle,
                                           TransferPolicySubject::Jumbf);
                if (decision) {
                    decision->effective = TransferPolicyAction::Drop;
                    decision->reason
                        = TransferPolicyReason::TargetSerializationUnavailable;
                    decision->message
                        = error.empty()
                              ? "projected jumbf payload could not be "
                                "serialized for target container"
                              : error;
                }
            } else if (bundle.blocks.size() == before) {
                requested_present_but_unpacked = true;
                if (r.code == PrepareTransferCode::None) {
                    r.code
                        = PrepareTransferCode::RequestedMetadataNotSerializable;
                }
                r.warnings += 1U;
                append_message(&r.message, "projected jumbf payload was empty");
                PreparedTransferPolicyDecision* decision
                    = find_policy_decision(&bundle,
                                           TransferPolicySubject::Jumbf);
                if (decision) {
                    decision->effective = TransferPolicyAction::Drop;
                    decision->reason
                        = TransferPolicyReason::TargetSerializationUnavailable;
                    decision->message = "projected jumbf payload was empty";
                }
            }
        }
    }

    if ((request.target_format == TransferTargetFormat::Jpeg
         || request.target_format == TransferTargetFormat::Jxl
         || request.target_format == TransferTargetFormat::Webp
         || transfer_target_is_bmff(request.target_format))
        && effective_c2pa == TransferPolicyAction::Keep
        && c2pa_reason == TransferPolicyReason::DraftInvalidationPayload) {
        const std::vector<std::byte> c2pa_payload
            = build_draft_c2pa_invalidation_payload();
        const size_t before = bundle.blocks.size();
        uint32_t order      = next_prepared_block_order(bundle, 150U);
        std::string error;
        const std::span<const std::byte> c2pa_bytes(c2pa_payload.data(),
                                                    c2pa_payload.size());
        bool appended = false;
        if (request.target_format == TransferTargetFormat::Jxl) {
            appended = append_jxl_jumbf_box(c2pa_bytes, TransferBlockKind::C2pa,
                                            &order, &bundle.blocks, &error);
        } else if (request.target_format == TransferTargetFormat::Webp) {
            appended = append_webp_c2pa_chunk(c2pa_bytes, &order,
                                              &bundle.blocks, &error);
        } else if (transfer_target_is_bmff(request.target_format)) {
            appended = append_bmff_metadata_item(c2pa_bytes, "bmff:item-c2pa",
                                                 TransferBlockKind::C2pa,
                                                 &order, &bundle.blocks,
                                                 &error);
        } else {
            appended = append_jpeg_app11_jumbf_segments(c2pa_bytes,
                                                        TransferBlockKind::C2pa,
                                                        &order, &bundle.blocks,
                                                        &error);
        }
        if (!appended) {
            requested_present_but_unpacked = true;
            if (r.code == PrepareTransferCode::None) {
                r.code = PrepareTransferCode::RequestedMetadataNotSerializable;
            }
            r.warnings += 1U;
            append_message(
                &r.message,
                error.empty()
                    ? "draft c2pa invalidation payload could not be serialized"
                    : error);
            PreparedTransferPolicyDecision* decision
                = find_policy_decision(&bundle, TransferPolicySubject::C2pa);
            if (decision) {
                decision->effective = TransferPolicyAction::Drop;
                decision->reason
                    = TransferPolicyReason::TargetSerializationUnavailable;
                decision->c2pa_mode = TransferC2paMode::Drop;
                decision->c2pa_prepared_output
                    = TransferC2paPreparedOutput::Dropped;
                decision->message = "draft c2pa invalidation payload could "
                                    "not be serialized";
            }
        } else if (bundle.blocks.size() == before) {
            requested_present_but_unpacked = true;
            if (r.code == PrepareTransferCode::None) {
                r.code = PrepareTransferCode::RequestedMetadataNotSerializable;
            }
            r.warnings += 1U;
            append_message(&r.message,
                           "draft c2pa invalidation payload was empty");
            PreparedTransferPolicyDecision* decision
                = find_policy_decision(&bundle, TransferPolicySubject::C2pa);
            if (decision) {
                decision->effective = TransferPolicyAction::Drop;
                decision->reason
                    = TransferPolicyReason::TargetSerializationUnavailable;
                decision->c2pa_mode = TransferC2paMode::Drop;
                decision->c2pa_prepared_output
                    = TransferC2paPreparedOutput::Dropped;
                decision->message = "draft c2pa invalidation payload was empty";
            }
        }
    }

    *out_bundle = std::move(bundle);

    if (requested_present_but_unpacked && out_bundle->blocks.empty()) {
        r.status = TransferStatus::Unsupported;
        if (r.code == PrepareTransferCode::None) {
            r.code = PrepareTransferCode::RequestedMetadataNotSerializable;
        }
        return r;
    }
    r.status = TransferStatus::Ok;
    if (r.code == PrepareTransferCode::None) {
        r.code = PrepareTransferCode::None;
    }
    return r;
}

PrepareTransferResult
prepare_metadata_for_target(const MetaStore& store,
                            const PrepareTransferRequest& request,
                            PreparedTransferBundle* out_bundle) noexcept
{
    const TransferPrepareCapabilities caps {};
    return prepare_metadata_for_target_impl(store, request, caps, out_bundle);
}

EmitTransferResult
append_prepared_bundle_jpeg_jumbf(
    PreparedTransferBundle* bundle, std::span<const std::byte> logical_payload,
    const AppendPreparedJpegJumbfOptions& options) noexcept
{
    EmitTransferResult out;
    if (!bundle) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "bundle is null";
        return out;
    }
    if (bundle->target_format != TransferTargetFormat::Jpeg) {
        out.status  = TransferStatus::Unsupported;
        out.code    = EmitTransferCode::BundleTargetNotJpeg;
        out.errors  = 1U;
        out.message = "bundle target format is not jpeg";
        return out;
    }
    if (logical_payload.empty()) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "logical jumbf payload is empty";
        return out;
    }

    uint32_t header_len = 0U;
    uint32_t box_type   = 0U;
    if (!parse_bmff_box_header(logical_payload, &header_len, &box_type)) {
        out.status  = TransferStatus::Malformed;
        out.code    = EmitTransferCode::InvalidPayload;
        out.errors  = 1U;
        out.message = "jumbf payload does not start with a valid bmff box";
        return out;
    }
    static constexpr uint32_t kMaxJpegSegmentPayload = 65533U;
    if (8U + header_len > kMaxJpegSegmentPayload) {
        out.status  = TransferStatus::LimitExceeded;
        out.code    = EmitTransferCode::InvalidPayload;
        out.errors  = 1U;
        out.message = "jumbf app11 overhead exceeds jpeg segment limits";
        return out;
    }
    if (is_c2pa_jumbf_payload(logical_payload)) {
        out.status = TransferStatus::Unsupported;
        out.code   = EmitTransferCode::ContentBoundPayloadUnsupported;
        out.errors = 1U;
        out.message
            = "content-bound c2pa payloads require a dedicated invalidation or re-sign path";
        return out;
    }

    if (options.replace_existing) {
        out.skipped = remove_prepared_blocks_by_route(bundle,
                                                      "jpeg:app11-jumbf");
    }

    uint32_t order = next_prepared_block_order(*bundle, 140U);
    std::string error;
    const size_t before = bundle->blocks.size();
    if (!append_jpeg_app11_jumbf_segments(logical_payload,
                                          TransferBlockKind::Jumbf, &order,
                                          &bundle->blocks, &error)) {
        out.status  = TransferStatus::Malformed;
        out.code    = EmitTransferCode::InvalidPayload;
        out.errors  = 1U;
        out.message = error.empty()
                          ? "failed to append jpeg app11 jumbf payload"
                          : error;
        return out;
    }

    bundle->profile.jumbf = TransferPolicyAction::Keep;
    PreparedTransferPolicyDecision* decision
        = find_policy_decision(bundle, TransferPolicySubject::Jumbf);
    if (!decision) {
        append_policy_decision(
            bundle, TransferPolicySubject::Jumbf, TransferPolicyAction::Keep,
            TransferPolicyAction::Keep, TransferPolicyReason::Default, 1U,
            "explicit raw jumbf payload will be emitted as jpeg app11 segments");
    } else {
        decision->requested = TransferPolicyAction::Keep;
        decision->effective = TransferPolicyAction::Keep;
        decision->reason    = TransferPolicyReason::Default;
        if (decision->matched_entries == 0U) {
            decision->matched_entries = 1U;
        } else {
            decision->matched_entries += 1U;
        }
        decision->message
            = "explicit raw jumbf payload will be emitted as jpeg app11 segments";
    }

    out.status  = TransferStatus::Ok;
    out.code    = EmitTransferCode::None;
    out.emitted = static_cast<uint32_t>(bundle->blocks.size() - before);
    return out;
}

namespace {

    static EmitTransferResult validate_prepared_jpeg_c2pa_contract(
        const PreparedTransferBundle& bundle, bool saw_c2pa,
        C2paPayloadClass payload_class) noexcept
    {
        EmitTransferResult out;
        const PreparedTransferPolicyDecision* decision
            = find_policy_decision(bundle, TransferPolicySubject::C2pa);
        const bool rewrite_ready = bundle.c2pa_rewrite.state
                                   == TransferC2paRewriteState::Ready;

        if (!saw_c2pa) {
            if (rewrite_ready
                || (decision
                    && (decision->c2pa_prepared_output
                            == TransferC2paPreparedOutput::PreservedRaw
                        || decision->c2pa_prepared_output
                               == TransferC2paPreparedOutput::
                                   GeneratedDraftUnsignedInvalidation
                        || decision->c2pa_prepared_output
                               == TransferC2paPreparedOutput::SignedRewrite))) {
                out.status = TransferStatus::Malformed;
                out.code   = EmitTransferCode::InvalidPayload;
                out.errors = 1U;
                out.message
                    = "prepared jpeg c2pa carrier is missing for the bundle c2pa contract";
                return out;
            }
            out.status = TransferStatus::Ok;
            out.code   = EmitTransferCode::None;
            return out;
        }

        if (!decision) {
            out.status = TransferStatus::Malformed;
            out.code   = EmitTransferCode::InvalidPayload;
            out.errors = 1U;
            out.message
                = "prepared jpeg c2pa carrier has no c2pa policy contract";
            return out;
        }
        if (payload_class == C2paPayloadClass::NotC2pa) {
            out.status = TransferStatus::Malformed;
            out.code   = EmitTransferCode::InvalidPayload;
            out.errors = 1U;
            out.message
                = "prepared jpeg c2pa carrier logical payload is not c2pa";
            return out;
        }

        switch (decision->c2pa_prepared_output) {
        case TransferC2paPreparedOutput::Dropped:
        case TransferC2paPreparedOutput::NotPresent:
        case TransferC2paPreparedOutput::NotApplicable:
            out.status = TransferStatus::Malformed;
            out.code   = EmitTransferCode::InvalidPayload;
            out.errors = 1U;
            out.message
                = "prepared jpeg c2pa carrier is present but the bundle c2pa contract does not allow output";
            return out;
        case TransferC2paPreparedOutput::GeneratedDraftUnsignedInvalidation:
            if (payload_class != C2paPayloadClass::DraftUnsignedInvalidation) {
                out.status = TransferStatus::Malformed;
                out.code   = EmitTransferCode::InvalidPayload;
                out.errors = 1U;
                out.message
                    = "prepared jpeg c2pa carrier does not match the draft invalidation contract";
                return out;
            }
            break;
        case TransferC2paPreparedOutput::SignedRewrite:
            if (payload_class != C2paPayloadClass::ContentBound) {
                out.status = TransferStatus::Malformed;
                out.code   = EmitTransferCode::InvalidPayload;
                out.errors = 1U;
                out.message
                    = "prepared jpeg c2pa carrier does not match the signed rewrite contract";
                return out;
            }
            if (!rewrite_ready) {
                out.status = TransferStatus::Malformed;
                out.code   = EmitTransferCode::InvalidPayload;
                out.errors = 1U;
                out.message
                    = "prepared jpeg c2pa signed rewrite carrier is not ready";
                return out;
            }
            break;
        case TransferC2paPreparedOutput::PreservedRaw:
            if (decision->c2pa_mode == TransferC2paMode::PreserveRaw
                && decision->c2pa_source_kind
                       == TransferC2paSourceKind::DraftUnsignedInvalidation
                && payload_class
                       != C2paPayloadClass::DraftUnsignedInvalidation) {
                out.status = TransferStatus::Malformed;
                out.code   = EmitTransferCode::InvalidPayload;
                out.errors = 1U;
                out.message
                    = "prepared jpeg c2pa carrier does not match the preserved raw source kind";
                return out;
            }
            if (decision->c2pa_mode == TransferC2paMode::PreserveRaw
                && decision->c2pa_source_kind
                       == TransferC2paSourceKind::ContentBound
                && payload_class != C2paPayloadClass::ContentBound) {
                out.status = TransferStatus::Malformed;
                out.code   = EmitTransferCode::InvalidPayload;
                out.errors = 1U;
                out.message
                    = "prepared jpeg c2pa carrier does not match the preserved raw source kind";
                return out;
            }
            break;
        }

        if (rewrite_ready
            && decision->c2pa_prepared_output
                   != TransferC2paPreparedOutput::SignedRewrite) {
            out.status = TransferStatus::Malformed;
            out.code   = EmitTransferCode::InvalidPayload;
            out.errors = 1U;
            out.message
                = "bundle c2pa rewrite state is ready but the prepared output contract is not signed rewrite";
            return out;
        }

        out.status = TransferStatus::Ok;
        out.code   = EmitTransferCode::None;
        return out;
    }

    static EmitTransferResult validate_prepared_jpeg_c2pa_blocks_for_emit(
        const PreparedTransferBundle& bundle,
        const EmitTransferOptions& options) noexcept
    {
        EmitTransferResult out;
        bool saw_c2pa             = false;
        uint32_t first_c2pa_index = 0U;
        uint32_t expected_seq     = 1U;
        uint32_t first_header_len = 0U;
        uint32_t first_box_type   = 0U;
        uint64_t declared_size    = 0U;
        uint64_t logical_size     = 0U;
        std::span<const std::byte> first_header;
        std::vector<std::byte> reconstructed;

        for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
            const PreparedTransferBlock& block = bundle.blocks[i];
            if (block.route != "jpeg:app11-c2pa") {
                continue;
            }
            if (options.skip_empty_payloads && block.payload.empty()) {
                out.status             = TransferStatus::Malformed;
                out.code               = EmitTransferCode::InvalidPayload;
                out.errors             = 1U;
                out.failed_block_index = i;
                out.message
                    = "prepared jpeg c2pa carrier block payload is empty";
                return out;
            }
            if (block.kind != TransferBlockKind::C2pa) {
                out.status             = TransferStatus::Malformed;
                out.code               = EmitTransferCode::InvalidPayload;
                out.errors             = 1U;
                out.failed_block_index = i;
                out.message = "prepared jpeg c2pa carrier block kind is invalid";
                return out;
            }
            if (block.payload.size() < 16U
                || block.payload[0] != std::byte { 'J' }
                || block.payload[1] != std::byte { 'P' }
                || block.payload[2] != std::byte { 0x00 }
                || block.payload[3] != std::byte { 0x00 }) {
                out.status             = TransferStatus::Malformed;
                out.code               = EmitTransferCode::InvalidPayload;
                out.errors             = 1U;
                out.failed_block_index = i;
                out.message
                    = "prepared jpeg c2pa carrier block header is invalid";
                return out;
            }

            uint32_t seq = 0U;
            if (!read_u32be(block.payload, 4U, &seq) || seq != expected_seq) {
                out.status             = TransferStatus::Malformed;
                out.code               = EmitTransferCode::InvalidPayload;
                out.errors             = 1U;
                out.failed_block_index = i;
                out.message = "prepared jpeg c2pa carrier sequence is invalid";
                return out;
            }

            const std::span<const std::byte> payload_span(block.payload.data(),
                                                          block.payload.size());
            const std::span<const std::byte> logical = payload_span.subspan(8U);
            uint32_t header_len                      = 0U;
            uint32_t box_type                        = 0U;
            if (!parse_bmff_box_header(logical, &header_len, &box_type)
                || !read_bmff_box_size(logical, &declared_size)
                || payload_span.size() < static_cast<size_t>(8U + header_len)) {
                out.status             = TransferStatus::Malformed;
                out.code               = EmitTransferCode::InvalidPayload;
                out.errors             = 1U;
                out.failed_block_index = i;
                out.message
                    = "prepared jpeg c2pa carrier bmff header is invalid";
                return out;
            }

            const std::span<const std::byte> header
                = payload_span.subspan(8U, header_len);
            if (!saw_c2pa) {
                saw_c2pa         = true;
                first_c2pa_index = i;
                first_header_len = header_len;
                first_box_type   = box_type;
                first_header     = header;
                if (first_box_type != fourcc('j', 'u', 'm', 'b')
                    && first_box_type != fourcc('c', '2', 'p', 'a')) {
                    out.status             = TransferStatus::Malformed;
                    out.code               = EmitTransferCode::InvalidPayload;
                    out.errors             = 1U;
                    out.failed_block_index = i;
                    out.message
                        = "prepared jpeg c2pa logical root type is invalid";
                    return out;
                }
                reconstructed.insert(reconstructed.end(), header.begin(),
                                     header.end());
            } else if (header_len != first_header_len
                       || box_type != first_box_type
                       || header.size() != first_header.size()
                       || !std::equal(header.begin(), header.end(),
                                      first_header.begin(),
                                      first_header.end())) {
                out.status             = TransferStatus::Malformed;
                out.code               = EmitTransferCode::InvalidPayload;
                out.errors             = 1U;
                out.failed_block_index = i;
                out.message
                    = "prepared jpeg c2pa carrier header is inconsistent";
                return out;
            }

            logical_size += static_cast<uint64_t>(payload_span.size())
                            - static_cast<uint64_t>(8U + header_len);
            if (i == first_c2pa_index) {
                logical_size += static_cast<uint64_t>(header_len);
            }
            const std::span<const std::byte> body = payload_span.subspan(
                8U + header_len);
            reconstructed.insert(reconstructed.end(), body.begin(), body.end());
            expected_seq += 1U;
        }

        if (saw_c2pa && logical_size != declared_size) {
            out.status             = TransferStatus::Malformed;
            out.code               = EmitTransferCode::InvalidPayload;
            out.errors             = 1U;
            out.failed_block_index = first_c2pa_index;
            out.message
                = "prepared jpeg c2pa logical payload size is inconsistent";
            return out;
        }

        const C2paPayloadClass payload_class
            = saw_c2pa ? classify_c2pa_jumbf_payload(
                             std::span<const std::byte>(reconstructed.data(),
                                                        reconstructed.size()))
                       : C2paPayloadClass::NotC2pa;
        const EmitTransferResult contract
            = validate_prepared_jpeg_c2pa_contract(bundle, saw_c2pa,
                                                   payload_class);
        if (contract.status != TransferStatus::Ok) {
            return contract;
        }

        out.status = TransferStatus::Ok;
        out.code   = EmitTransferCode::None;
        return out;
    }

}  // namespace

EmitTransferResult
emit_prepared_bundle_jpeg(const PreparedTransferBundle& bundle,
                          JpegTransferEmitter& emitter,
                          const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (bundle.target_format != TransferTargetFormat::Jpeg) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::BundleTargetNotJpeg;
        r.errors  = 1U;
        r.message = "bundle target format is not jpeg";
        return r;
    }

    const EmitTransferResult c2pa_preflight
        = validate_prepared_jpeg_c2pa_blocks_for_emit(bundle, options);
    if (c2pa_preflight.status != TransferStatus::Ok) {
        return c2pa_preflight;
    }

    for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
        const PreparedTransferBlock& block = bundle.blocks[i];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        uint8_t marker = 0;
        if (!marker_from_jpeg_route(block.route, &marker)) {
            r.status = TransferStatus::Unsupported;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::UnsupportedRoute;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "unsupported jpeg route: " + block.route;
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        const TransferStatus st = emitter.write_app_marker(
            marker, std::span<const std::byte>(block.payload.data(),
                                               block.payload.size()));
        if (st != TransferStatus::Ok) {
            r.status = st;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::BackendWriteFailed;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "jpeg emitter write_app_marker failed";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        r.emitted += 1U;
    }

    if (r.errors == 0U) {
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
emit_prepared_bundle_tiff(const PreparedTransferBundle& bundle,
                          TiffTransferEmitter& emitter,
                          const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (bundle.target_format != TransferTargetFormat::Tiff) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "bundle target format is not tiff";
        return r;
    }

    for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
        const PreparedTransferBlock& block = bundle.blocks[i];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        uint16_t tag = 0;
        if (!tiff_tag_from_route(block.route, &tag)) {
            r.status = TransferStatus::Unsupported;
            r.code   = EmitTransferCode::UnsupportedRoute;
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "unsupported tiff route: " + block.route;
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        const TransferStatus st = emitter.set_tag_bytes(
            tag, std::span<const std::byte>(block.payload.data(),
                                            block.payload.size()));
        if (st != TransferStatus::Ok) {
            r.status = st;
            r.code   = EmitTransferCode::BackendWriteFailed;
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "tiff emitter set_tag_bytes failed";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        r.emitted += 1U;
    }

    if (r.errors == 0U) {
        uint64_t exif_ifd_off          = 0U;
        const TransferStatus commit_st = emitter.commit_exif_directory(
            &exif_ifd_off);
        if (commit_st != TransferStatus::Ok) {
            r.status  = commit_st;
            r.code    = EmitTransferCode::BackendWriteFailed;
            r.errors  = 1U;
            r.message = "tiff emitter commit_exif_directory failed";
            return r;
        }
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
compile_prepared_bundle_tiff(const PreparedTransferBundle& bundle,
                             PreparedTiffEmitPlan* out_plan,
                             const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (!out_plan) {
        r.status  = TransferStatus::InvalidArgument;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "out_plan is null";
        return r;
    }
    out_plan->contract_version = bundle.contract_version;
    out_plan->ops.clear();

    if (bundle.target_format != TransferTargetFormat::Tiff) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "bundle target format is not tiff";
        return r;
    }

    for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
        const PreparedTransferBlock& block = bundle.blocks[i];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        uint16_t tag = 0;
        if (!tiff_tag_from_route(block.route, &tag)) {
            r.status = TransferStatus::Unsupported;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::UnsupportedRoute;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "unsupported tiff route: " + block.route;
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        PreparedTiffEmitOp op;
        op.block_index = i;
        op.tiff_tag    = tag;
        out_plan->ops.push_back(op);
    }

    if (r.errors == 0U) {
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
emit_prepared_bundle_tiff_compiled(const PreparedTransferBundle& bundle,
                                   const PreparedTiffEmitPlan& plan,
                                   TiffTransferEmitter& emitter,
                                   const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (bundle.target_format != TransferTargetFormat::Tiff) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "bundle target format is not tiff";
        return r;
    }
    if (plan.contract_version != bundle.contract_version) {
        r.status  = TransferStatus::InvalidArgument;
        r.code    = EmitTransferCode::PlanMismatch;
        r.errors  = 1U;
        r.message = "compiled plan contract_version mismatch";
        return r;
    }

    for (uint32_t i = 0; i < plan.ops.size(); ++i) {
        const PreparedTiffEmitOp& op = plan.ops[i];
        if (op.block_index >= bundle.blocks.size()) {
            r.status = TransferStatus::InvalidArgument;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::PlanMismatch;
            }
            r.errors += 1U;
            r.failed_block_index = op.block_index;
            r.message            = "compiled plan block_index out of range";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        const PreparedTransferBlock& block = bundle.blocks[op.block_index];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        const TransferStatus st = emitter.set_tag_bytes(
            op.tiff_tag, std::span<const std::byte>(block.payload.data(),
                                                    block.payload.size()));
        if (st != TransferStatus::Ok) {
            r.status = st;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::BackendWriteFailed;
            }
            r.errors += 1U;
            r.failed_block_index = op.block_index;
            r.message            = "tiff emitter set_tag_bytes failed";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        r.emitted += 1U;
    }

    if (r.errors == 0U) {
        uint64_t exif_ifd_off          = 0U;
        const TransferStatus commit_st = emitter.commit_exif_directory(
            &exif_ifd_off);
        if (commit_st != TransferStatus::Ok) {
            r.status  = commit_st;
            r.code    = EmitTransferCode::BackendWriteFailed;
            r.errors  = 1U;
            r.message = "tiff emitter commit_exif_directory failed";
            return r;
        }
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
emit_prepared_bundle_jxl(const PreparedTransferBundle& bundle,
                         JxlTransferEmitter& emitter,
                         const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (bundle.target_format != TransferTargetFormat::Jxl) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "bundle target format is not jxl";
        return r;
    }

    for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
        const PreparedTransferBlock& block = bundle.blocks[i];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        if (jxl_route_is_icc_profile(block.route)) {
            const TransferStatus st = emitter.set_icc_profile(
                std::span<const std::byte>(block.payload.data(),
                                           block.payload.size()));
            if (st != TransferStatus::Ok) {
                r.status = st;
                if (r.code == EmitTransferCode::None) {
                    r.code = EmitTransferCode::BackendWriteFailed;
                }
                r.errors += 1U;
                r.failed_block_index = i;
                r.message            = "jxl emitter set_icc_profile failed";
                if (options.stop_on_error) {
                    return r;
                }
                continue;
            }
            r.emitted += 1U;
            continue;
        }

        std::array<char, 4> box_type = { '\0', '\0', '\0', '\0' };
        bool compress                = false;
        if (!jxl_box_from_route(block.route, &box_type, &compress)) {
            r.status = TransferStatus::Unsupported;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::UnsupportedRoute;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "unsupported jxl route: " + block.route;
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        if (block.box_type != std::array<char, 4> { '\0', '\0', '\0', '\0' }
            && block.box_type != box_type) {
            r.status = TransferStatus::Malformed;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::InvalidPayload;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "prepared jxl box_type does not match route";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        const TransferStatus st
            = emitter.add_box(box_type,
                              std::span<const std::byte>(block.payload.data(),
                                                         block.payload.size()),
                              compress);
        if (st != TransferStatus::Ok) {
            r.status = st;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::BackendWriteFailed;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "jxl emitter add_box failed";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        r.emitted += 1U;
    }

    if (r.errors == 0U) {
        const TransferStatus close_st = emitter.close_boxes();
        if (close_st != TransferStatus::Ok) {
            r.status  = close_st;
            r.code    = EmitTransferCode::BackendWriteFailed;
            r.errors  = 1U;
            r.message = "jxl emitter close_boxes failed";
            return r;
        }
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
compile_prepared_bundle_jxl(const PreparedTransferBundle& bundle,
                            PreparedJxlEmitPlan* out_plan,
                            const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (!out_plan) {
        r.status  = TransferStatus::InvalidArgument;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "out_plan is null";
        return r;
    }
    out_plan->contract_version = bundle.contract_version;
    out_plan->ops.clear();

    if (bundle.target_format != TransferTargetFormat::Jxl) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "bundle target format is not jxl";
        return r;
    }

    for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
        const PreparedTransferBlock& block = bundle.blocks[i];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        PreparedJxlEmitOp op;
        if (jxl_route_is_icc_profile(block.route)) {
            op.kind = PreparedJxlEmitKind::IccProfile;
        } else if (!jxl_box_from_route(block.route, &op.box_type,
                                       &op.compress)) {
            r.status = TransferStatus::Unsupported;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::UnsupportedRoute;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "unsupported jxl route: " + block.route;
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        if (op.kind == PreparedJxlEmitKind::Box
            && block.box_type != std::array<char, 4> { '\0', '\0', '\0', '\0' }
            && block.box_type != op.box_type) {
            r.status = TransferStatus::Malformed;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::InvalidPayload;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "prepared jxl box_type does not match route";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        op.block_index = i;
        out_plan->ops.push_back(op);
    }

    if (r.errors == 0U) {
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
emit_prepared_bundle_jxl_compiled(const PreparedTransferBundle& bundle,
                                  const PreparedJxlEmitPlan& plan,
                                  JxlTransferEmitter& emitter,
                                  const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (bundle.target_format != TransferTargetFormat::Jxl) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "bundle target format is not jxl";
        return r;
    }
    if (plan.contract_version != bundle.contract_version) {
        r.status  = TransferStatus::InvalidArgument;
        r.code    = EmitTransferCode::PlanMismatch;
        r.errors  = 1U;
        r.message = "compiled plan contract_version mismatch";
        return r;
    }

    for (uint32_t i = 0; i < plan.ops.size(); ++i) {
        const PreparedJxlEmitOp& op = plan.ops[i];
        if (op.block_index >= bundle.blocks.size()) {
            r.status = TransferStatus::InvalidArgument;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::PlanMismatch;
            }
            r.errors += 1U;
            r.failed_block_index = op.block_index;
            r.message            = "compiled plan block_index out of range";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        const PreparedTransferBlock& block = bundle.blocks[op.block_index];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        TransferStatus st = TransferStatus::Ok;
        if (op.kind == PreparedJxlEmitKind::IccProfile) {
            st = emitter.set_icc_profile(
                std::span<const std::byte>(block.payload.data(),
                                           block.payload.size()));
        } else {
            st = emitter.add_box(
                op.box_type,
                std::span<const std::byte>(block.payload.data(),
                                           block.payload.size()),
                op.compress);
        }
        if (st != TransferStatus::Ok) {
            r.status = st;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::BackendWriteFailed;
            }
            r.errors += 1U;
            r.failed_block_index = op.block_index;
            r.message            = op.kind == PreparedJxlEmitKind::IccProfile
                                       ? "jxl emitter set_icc_profile failed"
                                       : "jxl emitter add_box failed";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        r.emitted += 1U;
    }

    if (r.errors == 0U) {
        const TransferStatus close_st = emitter.close_boxes();
        if (close_st != TransferStatus::Ok) {
            r.status  = close_st;
            r.code    = EmitTransferCode::BackendWriteFailed;
            r.errors  = 1U;
            r.message = "jxl emitter close_boxes failed";
            return r;
        }
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
emit_prepared_bundle_webp(const PreparedTransferBundle& bundle,
                          WebpTransferEmitter& emitter,
                          const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (bundle.target_format != TransferTargetFormat::Webp) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "bundle target format is not webp";
        return r;
    }

    for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
        const PreparedTransferBlock& block = bundle.blocks[i];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        std::array<char, 4> chunk_type = { '\0', '\0', '\0', '\0' };
        if (!webp_chunk_from_route(block.route, &chunk_type)) {
            r.status = TransferStatus::Unsupported;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::UnsupportedRoute;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "unsupported webp route: " + block.route;
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        const TransferStatus st = emitter.add_chunk(
            chunk_type, std::span<const std::byte>(block.payload.data(),
                                                   block.payload.size()));
        if (st != TransferStatus::Ok) {
            r.status = st;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::BackendWriteFailed;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "webp emitter add_chunk failed";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        r.emitted += 1U;
    }

    if (r.errors == 0U) {
        const TransferStatus close_st = emitter.close_chunks();
        if (close_st != TransferStatus::Ok) {
            r.status  = close_st;
            r.code    = EmitTransferCode::BackendWriteFailed;
            r.errors  = 1U;
            r.message = "webp emitter close_chunks failed";
            return r;
        }
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
compile_prepared_bundle_webp(const PreparedTransferBundle& bundle,
                             PreparedWebpEmitPlan* out_plan,
                             const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (!out_plan) {
        r.status  = TransferStatus::InvalidArgument;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "out_plan is null";
        return r;
    }
    out_plan->contract_version = bundle.contract_version;
    out_plan->ops.clear();

    if (bundle.target_format != TransferTargetFormat::Webp) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "bundle target format is not webp";
        return r;
    }

    for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
        const PreparedTransferBlock& block = bundle.blocks[i];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        PreparedWebpEmitOp op;
        if (!webp_chunk_from_route(block.route, &op.chunk_type)) {
            r.status = TransferStatus::Unsupported;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::UnsupportedRoute;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "unsupported webp route: " + block.route;
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        op.block_index = i;
        out_plan->ops.push_back(op);
    }

    if (r.errors == 0U) {
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
emit_prepared_bundle_webp_compiled(const PreparedTransferBundle& bundle,
                                   const PreparedWebpEmitPlan& plan,
                                   WebpTransferEmitter& emitter,
                                   const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (bundle.target_format != TransferTargetFormat::Webp) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "bundle target format is not webp";
        return r;
    }
    if (plan.contract_version != bundle.contract_version) {
        r.status  = TransferStatus::InvalidArgument;
        r.code    = EmitTransferCode::PlanMismatch;
        r.errors  = 1U;
        r.message = "compiled plan contract_version mismatch";
        return r;
    }

    for (uint32_t i = 0; i < plan.ops.size(); ++i) {
        const PreparedWebpEmitOp& op = plan.ops[i];
        if (op.block_index >= bundle.blocks.size()) {
            r.status = TransferStatus::InvalidArgument;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::PlanMismatch;
            }
            r.errors += 1U;
            r.failed_block_index = op.block_index;
            r.message            = "compiled plan block_index out of range";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        const PreparedTransferBlock& block = bundle.blocks[op.block_index];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        const TransferStatus st = emitter.add_chunk(
            op.chunk_type, std::span<const std::byte>(block.payload.data(),
                                                      block.payload.size()));
        if (st != TransferStatus::Ok) {
            r.status = st;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::BackendWriteFailed;
            }
            r.errors += 1U;
            r.failed_block_index = op.block_index;
            r.message            = "webp emitter add_chunk failed";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        r.emitted += 1U;
    }

    if (r.errors == 0U) {
        const TransferStatus close_st = emitter.close_chunks();
        if (close_st != TransferStatus::Ok) {
            r.status  = close_st;
            r.code    = EmitTransferCode::BackendWriteFailed;
            r.errors  = 1U;
            r.message = "webp emitter close_chunks failed";
            return r;
        }
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
emit_prepared_bundle_bmff(const PreparedTransferBundle& bundle,
                          BmffTransferEmitter& emitter,
                          const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (!transfer_target_is_bmff(bundle.target_format)) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "bundle target format is not bmff";
        return r;
    }

    for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
        const PreparedTransferBlock& block = bundle.blocks[i];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        uint32_t item_type = 0U;
        bool mime_xmp      = false;
        if (!bmff_item_from_route(block.route, &item_type, &mime_xmp)) {
            r.status = TransferStatus::Unsupported;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::UnsupportedRoute;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "unsupported bmff route: " + block.route;
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        const TransferStatus st
            = mime_xmp ? emitter.add_mime_xmp_item(
                             std::span<const std::byte>(block.payload.data(),
                                                        block.payload.size()))
                       : emitter.add_item(item_type, std::span<const std::byte>(
                                                         block.payload.data(),
                                                         block.payload.size()));
        if (st != TransferStatus::Ok) {
            r.status = st;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::BackendWriteFailed;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message = mime_xmp ? "bmff emitter add_mime_xmp_item failed"
                                 : "bmff emitter add_item failed";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        r.emitted += 1U;
    }

    if (r.errors == 0U) {
        const TransferStatus close_st = emitter.close_items();
        if (close_st != TransferStatus::Ok) {
            r.status  = close_st;
            r.code    = EmitTransferCode::BackendWriteFailed;
            r.errors  = 1U;
            r.message = "bmff emitter close_items failed";
            return r;
        }
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
compile_prepared_bundle_bmff(const PreparedTransferBundle& bundle,
                             PreparedBmffEmitPlan* out_plan,
                             const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (!out_plan) {
        r.status  = TransferStatus::InvalidArgument;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "out_plan is null";
        return r;
    }
    out_plan->contract_version = bundle.contract_version;
    out_plan->ops.clear();

    if (!transfer_target_is_bmff(bundle.target_format)) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "bundle target format is not bmff";
        return r;
    }

    for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
        const PreparedTransferBlock& block = bundle.blocks[i];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        uint32_t item_type = 0U;
        bool mime_xmp      = false;
        if (!bmff_item_from_route(block.route, &item_type, &mime_xmp)) {
            r.status = TransferStatus::Unsupported;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::UnsupportedRoute;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "unsupported bmff route: " + block.route;
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        PreparedBmffEmitOp op;
        op.kind        = mime_xmp ? PreparedBmffEmitKind::MimeXmp
                                  : PreparedBmffEmitKind::Item;
        op.block_index = i;
        op.item_type   = item_type;
        out_plan->ops.push_back(op);
    }

    if (r.errors == 0U) {
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
emit_prepared_bundle_bmff_compiled(const PreparedTransferBundle& bundle,
                                   const PreparedBmffEmitPlan& plan,
                                   BmffTransferEmitter& emitter,
                                   const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (!transfer_target_is_bmff(bundle.target_format)) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "bundle target format is not bmff";
        return r;
    }
    if (plan.contract_version != bundle.contract_version) {
        r.status  = TransferStatus::InvalidArgument;
        r.code    = EmitTransferCode::PlanMismatch;
        r.errors  = 1U;
        r.message = "compiled plan contract_version mismatch";
        return r;
    }

    for (uint32_t i = 0; i < plan.ops.size(); ++i) {
        const PreparedBmffEmitOp& op = plan.ops[i];
        if (op.block_index >= bundle.blocks.size()) {
            r.status = TransferStatus::InvalidArgument;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::PlanMismatch;
            }
            r.errors += 1U;
            r.failed_block_index = op.block_index;
            r.message            = "compiled plan block_index out of range";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        const PreparedTransferBlock& block = bundle.blocks[op.block_index];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        TransferStatus st = TransferStatus::Ok;
        if (op.kind == PreparedBmffEmitKind::MimeXmp) {
            st = emitter.add_mime_xmp_item(
                std::span<const std::byte>(block.payload.data(),
                                           block.payload.size()));
        } else {
            st = emitter.add_item(
                op.item_type, std::span<const std::byte>(block.payload.data(),
                                                         block.payload.size()));
        }
        if (st != TransferStatus::Ok) {
            r.status = st;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::BackendWriteFailed;
            }
            r.errors += 1U;
            r.failed_block_index = op.block_index;
            r.message            = op.kind == PreparedBmffEmitKind::MimeXmp
                                       ? "bmff emitter add_mime_xmp_item failed"
                                       : "bmff emitter add_item failed";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        r.emitted += 1U;
    }

    if (r.errors == 0U) {
        const TransferStatus close_st = emitter.close_items();
        if (close_st != TransferStatus::Ok) {
            r.status  = close_st;
            r.code    = EmitTransferCode::BackendWriteFailed;
            r.errors  = 1U;
            r.message = "bmff emitter close_items failed";
            return r;
        }
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
compile_prepared_bundle_jpeg(const PreparedTransferBundle& bundle,
                             PreparedJpegEmitPlan* out_plan,
                             const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (!out_plan) {
        r.status  = TransferStatus::InvalidArgument;
        r.code    = EmitTransferCode::InvalidArgument;
        r.errors  = 1U;
        r.message = "out_plan is null";
        return r;
    }
    out_plan->contract_version = bundle.contract_version;
    out_plan->ops.clear();

    if (bundle.target_format != TransferTargetFormat::Jpeg) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::BundleTargetNotJpeg;
        r.errors  = 1U;
        r.message = "bundle target format is not jpeg";
        return r;
    }

    const EmitTransferResult c2pa_preflight
        = validate_prepared_jpeg_c2pa_blocks_for_emit(bundle, options);
    if (c2pa_preflight.status != TransferStatus::Ok) {
        return c2pa_preflight;
    }

    for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
        const PreparedTransferBlock& block = bundle.blocks[i];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        uint8_t marker = 0;
        if (!marker_from_jpeg_route(block.route, &marker)) {
            r.status = TransferStatus::Unsupported;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::UnsupportedRoute;
            }
            r.errors += 1U;
            r.failed_block_index = i;
            r.message            = "unsupported jpeg route: " + block.route;
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        PreparedJpegEmitOp op;
        op.block_index = i;
        op.marker_code = marker;
        out_plan->ops.push_back(op);
    }

    if (r.errors == 0U) {
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
emit_prepared_bundle_jpeg_compiled(const PreparedTransferBundle& bundle,
                                   const PreparedJpegEmitPlan& plan,
                                   JpegTransferEmitter& emitter,
                                   const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (bundle.target_format != TransferTargetFormat::Jpeg) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::BundleTargetNotJpeg;
        r.errors  = 1U;
        r.message = "bundle target format is not jpeg";
        return r;
    }
    if (plan.contract_version != bundle.contract_version) {
        r.status  = TransferStatus::InvalidArgument;
        r.code    = EmitTransferCode::PlanMismatch;
        r.errors  = 1U;
        r.message = "compiled plan contract_version mismatch";
        return r;
    }

    const EmitTransferResult c2pa_preflight
        = validate_prepared_jpeg_c2pa_blocks_for_emit(bundle, options);
    if (c2pa_preflight.status != TransferStatus::Ok) {
        return c2pa_preflight;
    }

    for (uint32_t i = 0; i < plan.ops.size(); ++i) {
        const PreparedJpegEmitOp& op = plan.ops[i];
        if (op.block_index >= bundle.blocks.size()) {
            r.status = TransferStatus::InvalidArgument;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::PlanMismatch;
            }
            r.errors += 1U;
            r.failed_block_index = op.block_index;
            r.message            = "compiled plan block_index out of range";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        const PreparedTransferBlock& block = bundle.blocks[op.block_index];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        const TransferStatus st = emitter.write_app_marker(
            op.marker_code, std::span<const std::byte>(block.payload.data(),
                                                       block.payload.size()));
        if (st != TransferStatus::Ok) {
            r.status = st;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::BackendWriteFailed;
            }
            r.errors += 1U;
            r.failed_block_index = op.block_index;
            r.message            = "jpeg emitter write_app_marker failed";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        r.emitted += 1U;
    }

    if (r.errors == 0U) {
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

EmitTransferResult
write_prepared_bundle_jpeg(const PreparedTransferBundle& bundle,
                           TransferByteWriter& writer,
                           const EmitTransferOptions& options) noexcept
{
    PreparedJpegEmitPlan plan;
    const EmitTransferResult compiled
        = compile_prepared_bundle_jpeg(bundle, &plan, options);
    if (compiled.status != TransferStatus::Ok) {
        return compiled;
    }
    return write_prepared_bundle_jpeg_compiled(bundle, plan, writer, options);
}

EmitTransferResult
write_prepared_bundle_jpeg_compiled(const PreparedTransferBundle& bundle,
                                    const PreparedJpegEmitPlan& plan,
                                    TransferByteWriter& writer,
                                    const EmitTransferOptions& options) noexcept
{
    EmitTransferResult r;
    if (bundle.target_format != TransferTargetFormat::Jpeg) {
        r.status  = TransferStatus::Unsupported;
        r.code    = EmitTransferCode::BundleTargetNotJpeg;
        r.errors  = 1U;
        r.message = "bundle target format is not jpeg";
        return r;
    }
    if (plan.contract_version != bundle.contract_version) {
        r.status  = TransferStatus::InvalidArgument;
        r.code    = EmitTransferCode::PlanMismatch;
        r.errors  = 1U;
        r.message = "compiled plan contract_version mismatch";
        return r;
    }

    const EmitTransferResult c2pa_preflight
        = validate_prepared_jpeg_c2pa_blocks_for_emit(bundle, options);
    if (c2pa_preflight.status != TransferStatus::Ok) {
        return c2pa_preflight;
    }

    for (uint32_t i = 0; i < plan.ops.size(); ++i) {
        const PreparedJpegEmitOp& op = plan.ops[i];
        if (op.block_index >= bundle.blocks.size()) {
            r.status = TransferStatus::InvalidArgument;
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::PlanMismatch;
            }
            r.errors += 1U;
            r.failed_block_index = op.block_index;
            r.message            = "compiled plan block_index out of range";
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }

        const PreparedTransferBlock& block = bundle.blocks[op.block_index];
        if (options.skip_empty_payloads && block.payload.empty()) {
            r.skipped += 1U;
            continue;
        }

        if (!write_jpeg_marker_segment(
                writer, op.marker_code,
                std::span<const std::byte>(block.payload.data(),
                                           block.payload.size()),
                &r)) {
            if (r.code == EmitTransferCode::None) {
                r.code = EmitTransferCode::BackendWriteFailed;
            }
            r.failed_block_index = op.block_index;
            if (r.message.empty()) {
                r.message = "jpeg byte writer failed";
            }
            if (options.stop_on_error) {
                return r;
            }
            continue;
        }
        r.emitted += 1U;
    }

    if (r.errors == 0U) {
        r.status = TransferStatus::Ok;
        r.code   = EmitTransferCode::None;
    }
    return r;
}

JpegEditPlan
plan_prepared_bundle_jpeg_edit(std::span<const std::byte> input_jpeg,
                               const PreparedTransferBundle& bundle,
                               const PlanJpegEditOptions& options) noexcept
{
    JpegEditPlan plan;
    plan.requested_mode = options.mode;
    plan.input_size     = static_cast<uint64_t>(input_jpeg.size());

    if (bundle.target_format != TransferTargetFormat::Jpeg) {
        plan.status  = TransferStatus::Unsupported;
        plan.message = "bundle target format is not jpeg";
        return plan;
    }

    std::vector<PlannedJpegSegment> desired;
    const EmitTransferResult desired_status
        = collect_planned_jpeg_segments(bundle, options.skip_empty_payloads,
                                        &desired);
    if (desired_status.status != TransferStatus::Ok) {
        plan.status  = desired_status.status;
        plan.message = desired_status.message.empty()
                           ? "failed to compile prepared jpeg segments"
                           : desired_status.message;
        return plan;
    }
    plan.emitted_segments = static_cast<uint32_t>(desired.size());

    const JpegScanResult scan = scan_leading_jpeg_segments(input_jpeg);
    plan.leading_scan_end     = static_cast<uint64_t>(scan.scan_end);
    if (scan.status != TransferStatus::Ok) {
        plan.status  = scan.status;
        plan.message = scan.message;
        return plan;
    }

    bool in_place_possible  = true;
    uint32_t in_place_count = 0;
    uint64_t replaced_bytes = 0;
    for (size_t i = 0; i < desired.size(); ++i) {
        const PlannedJpegSegment& d = desired[i];
        const size_t occ    = route_occurrence_before(desired, i, d.route);
        size_t existing_idx = 0;
        if (!find_existing_by_route_occurrence(scan.leading_segments, d.route,
                                               occ, &existing_idx)) {
            in_place_possible = false;
            break;
        }
        const ExistingJpegSegment& e = scan.leading_segments[existing_idx];
        if (e.payload_len != d.payload.size()) {
            in_place_possible = false;
            break;
        }
        in_place_count += 1U;
    }
    if (in_place_possible) {
        for (size_t i = 0; i < scan.leading_segments.size(); ++i) {
            if (!should_strip_existing_jpeg_segment(bundle,
                                                    scan.leading_segments[i],
                                                    desired)) {
                continue;
            }
            if (!route_in_desired(scan.leading_segments[i].route, desired)) {
                in_place_possible = false;
                break;
            }
        }
    }
    plan.in_place_possible = in_place_possible;

    JpegEditMode selected = JpegEditMode::MetadataRewrite;
    if (options.mode == JpegEditMode::InPlace) {
        if (!in_place_possible) {
            plan.status  = TransferStatus::Unsupported;
            plan.message = "in_place edit is not possible for current jpeg";
            return plan;
        }
        selected = JpegEditMode::InPlace;
    } else if (options.mode == JpegEditMode::MetadataRewrite) {
        selected = JpegEditMode::MetadataRewrite;
    } else {
        selected = in_place_possible ? JpegEditMode::InPlace
                                     : JpegEditMode::MetadataRewrite;
    }

    if (options.require_in_place && selected != JpegEditMode::InPlace) {
        plan.status  = TransferStatus::Unsupported;
        plan.message = "in_place edit required but not possible";
        return plan;
    }

    plan.selected_mode = selected;
    if (selected == JpegEditMode::InPlace) {
        plan.replaced_segments = in_place_count;
        plan.appended_segments = 0U;
        plan.output_size       = plan.input_size;
        plan.status            = TransferStatus::Ok;
        return plan;
    }

    uint32_t replaced_segments      = 0;
    uint32_t removed_jumbf_segments = 0;
    uint32_t removed_c2pa_segments  = 0;
    for (size_t i = 0; i < scan.leading_segments.size(); ++i) {
        const ExistingJpegSegment& e = scan.leading_segments[i];
        if (!should_strip_existing_jpeg_segment(bundle, e, desired)) {
            continue;
        }
        replaced_segments += 1U;
        if (e.route == "jpeg:app11-jumbf") {
            removed_jumbf_segments += 1U;
        } else if (e.route == "jpeg:app11-c2pa") {
            removed_c2pa_segments += 1U;
        }
        replaced_bytes += static_cast<uint64_t>(4U + e.payload_len);
    }

    uint64_t added_bytes = 0;
    for (size_t i = 0; i < desired.size(); ++i) {
        added_bytes += static_cast<uint64_t>(4U + desired[i].payload.size());
    }

    plan.replaced_segments         = replaced_segments;
    plan.appended_segments         = static_cast<uint32_t>(desired.size());
    plan.removed_existing_segments = replaced_segments;
    plan.removed_existing_jumbf_segments = removed_jumbf_segments;
    plan.removed_existing_c2pa_segments  = removed_c2pa_segments;
    if (plan.input_size >= replaced_bytes) {
        plan.output_size = plan.input_size - replaced_bytes + added_bytes;
    } else {
        plan.output_size = plan.input_size + added_bytes;
    }
    plan.status = TransferStatus::Ok;
    return plan;
}

EmitTransferResult
apply_prepared_bundle_jpeg_edit(std::span<const std::byte> input_jpeg,
                                const PreparedTransferBundle& bundle,
                                const JpegEditPlan& plan,
                                std::vector<std::byte>* out_jpeg) noexcept
{
    EmitTransferResult out;
    if (!out_jpeg) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_jpeg is null";
        return out;
    }
    if (plan.status != TransferStatus::Ok) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "edit plan status is not ok";
        return out;
    }
    if (bundle.target_format != TransferTargetFormat::Jpeg) {
        out.status  = TransferStatus::Unsupported;
        out.code    = EmitTransferCode::BundleTargetNotJpeg;
        out.errors  = 1U;
        out.message = "bundle target format is not jpeg";
        return out;
    }

    std::vector<PlannedJpegSegment> desired;
    const EmitTransferResult desired_status
        = collect_planned_jpeg_segments(bundle, true, &desired);
    if (desired_status.status != TransferStatus::Ok) {
        return desired_status;
    }

    const JpegScanResult scan = scan_leading_jpeg_segments(input_jpeg);
    if (scan.status != TransferStatus::Ok) {
        out.status  = scan.status;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = scan.message;
        return out;
    }

    if (plan.selected_mode == JpegEditMode::InPlace) {
        out_jpeg->assign(input_jpeg.begin(), input_jpeg.end());
        for (size_t i = 0; i < desired.size(); ++i) {
            const PlannedJpegSegment& d = desired[i];
            const size_t occ    = route_occurrence_before(desired, i, d.route);
            size_t existing_idx = 0;
            if (!find_existing_by_route_occurrence(scan.leading_segments,
                                                   d.route, occ,
                                                   &existing_idx)) {
                out.status  = TransferStatus::Unsupported;
                out.code    = EmitTransferCode::PlanMismatch;
                out.errors  = 1U;
                out.message = "in_place match not found for route: " + d.route;
                return out;
            }
            const ExistingJpegSegment& e = scan.leading_segments[existing_idx];
            if (e.payload_len != d.payload.size()) {
                out.status  = TransferStatus::Unsupported;
                out.code    = EmitTransferCode::PlanMismatch;
                out.errors  = 1U;
                out.message = "in_place size mismatch for route: " + d.route;
                return out;
            }
            if (e.payload_off + e.payload_len > out_jpeg->size()) {
                out.status  = TransferStatus::Malformed;
                out.code    = EmitTransferCode::PlanMismatch;
                out.errors  = 1U;
                out.message = "in_place payload range out of bounds";
                return out;
            }
            if (!d.payload.empty()) {
                std::memcpy(out_jpeg->data() + e.payload_off, d.payload.data(),
                            d.payload.size());
            }
            out.emitted += 1U;
        }
        out.status = TransferStatus::Ok;
        out.code   = EmitTransferCode::None;
        return out;
    }

    if (plan.selected_mode != JpegEditMode::MetadataRewrite) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "unsupported selected edit mode";
        return out;
    }

    out_jpeg->clear();
    out_jpeg->reserve(static_cast<size_t>(plan.output_size));

    if (input_jpeg.size() < 2U) {
        out.status  = TransferStatus::Malformed;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "jpeg input is too small";
        return out;
    }
    out_jpeg->push_back(input_jpeg[0]);
    out_jpeg->push_back(input_jpeg[1]);

    bool inserted = false;
    for (size_t i = 0; i < scan.leading_segments.size(); ++i) {
        const ExistingJpegSegment& e = scan.leading_segments[i];
        const bool replaced = should_strip_existing_jpeg_segment(bundle, e,
                                                                 desired);
        if (replaced) {
            if (!inserted) {
                for (size_t si = 0; si < desired.size(); ++si) {
                    append_jpeg_segment(out_jpeg, desired[si].marker,
                                        desired[si].payload);
                }
                inserted    = true;
                out.emitted = static_cast<uint32_t>(desired.size());
            }
            out.skipped += 1U;
            continue;
        }

        const size_t seg_end = e.payload_off + e.payload_len;
        if (seg_end > input_jpeg.size() || e.marker_off > seg_end) {
            out.status  = TransferStatus::Malformed;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "existing jpeg segment range is invalid";
            return out;
        }
        const std::ptrdiff_t marker_off = static_cast<std::ptrdiff_t>(
            e.marker_off);
        const std::ptrdiff_t seg_end_off = static_cast<std::ptrdiff_t>(seg_end);
        out_jpeg->insert(out_jpeg->end(), input_jpeg.begin() + marker_off,
                         input_jpeg.begin() + seg_end_off);
    }

    if (!inserted) {
        for (size_t si = 0; si < desired.size(); ++si) {
            append_jpeg_segment(out_jpeg, desired[si].marker,
                                desired[si].payload);
        }
        out.emitted = static_cast<uint32_t>(desired.size());
    }

    if (scan.scan_end <= input_jpeg.size()) {
        const std::ptrdiff_t scan_end = static_cast<std::ptrdiff_t>(
            scan.scan_end);
        out_jpeg->insert(out_jpeg->end(), input_jpeg.begin() + scan_end,
                         input_jpeg.end());
    }

    out.status = TransferStatus::Ok;
    out.code   = EmitTransferCode::None;
    return out;
}

EmitTransferResult
write_prepared_bundle_jpeg_edit(std::span<const std::byte> input_jpeg,
                                const PreparedTransferBundle& bundle,
                                const JpegEditPlan& plan,
                                TransferByteWriter& writer) noexcept
{
    PreparedTransferPackagePlan package;
    EmitTransferResult out = build_prepared_bundle_jpeg_package(input_jpeg,
                                                                bundle, plan,
                                                                &package);
    if (out.status != TransferStatus::Ok) {
        return out;
    }
    out = write_prepared_transfer_package(input_jpeg, bundle, package, writer);
    if (out.status == TransferStatus::Ok) {
        out.emitted = plan.emitted_segments;
        out.skipped = plan.removed_existing_segments;
    }
    return out;
}

TiffEditPlan
plan_prepared_bundle_tiff_edit(std::span<const std::byte> input_tiff,
                               const PreparedTransferBundle& bundle,
                               const PlanTiffEditOptions& options) noexcept
{
    TiffEditPlan plan;
    plan.input_size = static_cast<uint64_t>(input_tiff.size());

    if (bundle.target_format != TransferTargetFormat::Tiff) {
        plan.status  = TransferStatus::Unsupported;
        plan.message = "bundle target format is not tiff";
        return plan;
    }

    const std::vector<TiffTagUpdate> updates = collect_tiff_tag_updates(bundle);
    const std::vector<std::byte> exif_app1_payload
        = first_tiff_exif_app1_payload(bundle);
    plan.tag_updates  = static_cast<uint32_t>(updates.size());
    plan.has_exif_ifd = !exif_app1_payload.empty();

    if (options.require_updates && updates.empty()
        && exif_app1_payload.empty()) {
        plan.status  = TransferStatus::Unsupported;
        plan.message = "no tiff updates";
        return plan;
    }

    TiffRewriteTail rewrite;
    std::string err;
    if (!build_tiff_rewrite_tail(
            input_tiff, updates,
            std::span<const std::byte>(exif_app1_payload.data(),
                                       exif_app1_payload.size()),
            &rewrite, &err)) {
        plan.status  = tiff_edit_status_from_error(err);
        plan.message = err;
        return plan;
    }
    plan.output_size = static_cast<uint64_t>(input_tiff.size())
                       + static_cast<uint64_t>(rewrite.tail_bytes.size());
    plan.status = TransferStatus::Ok;
    return plan;
}

EmitTransferResult
apply_prepared_bundle_tiff_edit(std::span<const std::byte> input_tiff,
                                const PreparedTransferBundle& bundle,
                                const TiffEditPlan& plan,
                                std::vector<std::byte>* out_tiff) noexcept
{
    EmitTransferResult out;
    if (!out_tiff) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_tiff is null";
        return out;
    }
    if (plan.status != TransferStatus::Ok) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "edit plan status is not ok";
        return out;
    }
    if (bundle.target_format != TransferTargetFormat::Tiff) {
        out.status  = TransferStatus::Unsupported;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "bundle target format is not tiff";
        return out;
    }

    const std::vector<TiffTagUpdate> updates = collect_tiff_tag_updates(bundle);
    const std::vector<std::byte> exif_app1_payload
        = first_tiff_exif_app1_payload(bundle);
    if (plan.tag_updates != static_cast<uint32_t>(updates.size())
        || plan.has_exif_ifd != !exif_app1_payload.empty()) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::PlanMismatch;
        out.errors  = 1U;
        out.message = "tiff edit plan no longer matches bundle";
        return out;
    }

    std::string err;
    const bool rewritten = rewrite_tiff_ifd0_tags(
        input_tiff, updates,
        std::span<const std::byte>(exif_app1_payload.data(),
                                   exif_app1_payload.size()),
        out_tiff, &err);
    if (!rewritten) {
        out.status  = tiff_edit_status_from_error(err);
        out.code    = EmitTransferCode::PlanMismatch;
        out.errors  = 1U;
        out.message = err;
        return out;
    }

    out.status  = TransferStatus::Ok;
    out.code    = EmitTransferCode::None;
    out.emitted = plan.tag_updates + (plan.has_exif_ifd ? 1U : 0U);
    return out;
}

EmitTransferResult
write_prepared_bundle_tiff_edit(std::span<const std::byte> input_tiff,
                                const PreparedTransferBundle& bundle,
                                const TiffEditPlan& plan,
                                TransferByteWriter& writer) noexcept
{
    PreparedTransferPackagePlan package;
    EmitTransferResult out = build_prepared_bundle_tiff_package(input_tiff,
                                                                bundle, plan,
                                                                &package);
    if (out.status != TransferStatus::Ok) {
        return out;
    }
    out = write_prepared_transfer_package(input_tiff, bundle, package, writer);
    if (out.status == TransferStatus::Ok) {
        out.emitted = plan.tag_updates + (plan.has_exif_ifd ? 1U : 0U);
    }
    return out;
}

EmitTransferResult
build_prepared_transfer_emit_package(const PreparedTransferBundle& bundle,
                                     PreparedTransferPackagePlan* out_plan,
                                     const EmitTransferOptions& options) noexcept
{
    EmitTransferResult out;
    if (!out_plan) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_plan is null";
        return out;
    }

    PreparedTransferPackagePlan plan;
    plan.contract_version = bundle.contract_version;
    plan.target_format    = bundle.target_format;
    plan.input_size       = 0U;

    if (bundle.target_format == TransferTargetFormat::Jpeg) {
        const EmitTransferResult c2pa_preflight
            = validate_prepared_jpeg_c2pa_blocks_for_emit(bundle, options);
        if (c2pa_preflight.status != TransferStatus::Ok) {
            return c2pa_preflight;
        }
        for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
            const PreparedTransferBlock& block = bundle.blocks[i];
            if (options.skip_empty_payloads && block.payload.empty()) {
                out.skipped += 1U;
                continue;
            }
            uint8_t marker = 0U;
            if (!marker_from_jpeg_route(block.route, &marker)) {
                out.status = TransferStatus::Unsupported;
                if (out.code == EmitTransferCode::None) {
                    out.code = EmitTransferCode::UnsupportedRoute;
                }
                out.errors += 1U;
                out.failed_block_index = i;
                out.message = "unsupported jpeg route: " + block.route;
                if (options.stop_on_error) {
                    return out;
                }
                continue;
            }
            append_package_prepared_block_chunk(
                &plan, i, 4U + static_cast<uint64_t>(block.payload.size()));
        }
    } else if (bundle.target_format == TransferTargetFormat::Jxl) {
        for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
            const PreparedTransferBlock& block = bundle.blocks[i];
            if (options.skip_empty_payloads && block.payload.empty()) {
                out.skipped += 1U;
                continue;
            }
            std::array<char, 4> box_type = { '\0', '\0', '\0', '\0' };
            bool compress                = false;
            if (!jxl_box_from_route(block.route, &box_type, &compress)) {
                out.status = TransferStatus::Unsupported;
                if (out.code == EmitTransferCode::None) {
                    out.code = EmitTransferCode::UnsupportedRoute;
                }
                out.errors += 1U;
                out.failed_block_index = i;
                out.message = "unsupported jxl route: " + block.route;
                if (options.stop_on_error) {
                    return out;
                }
                continue;
            }
            if (block.payload.size() > static_cast<size_t>(0xFFFFFFFFU - 8U)) {
                out.status = TransferStatus::LimitExceeded;
                out.code   = EmitTransferCode::InvalidPayload;
                out.errors += 1U;
                out.failed_block_index = i;
                out.message = "jxl box payload exceeds 32-bit box size";
                if (options.stop_on_error) {
                    return out;
                }
                continue;
            }
            append_package_prepared_block_chunk(
                &plan, i, 8U + static_cast<uint64_t>(block.payload.size()));
        }
    } else if (bundle.target_format == TransferTargetFormat::Webp) {
        for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
            const PreparedTransferBlock& block = bundle.blocks[i];
            if (options.skip_empty_payloads && block.payload.empty()) {
                out.skipped += 1U;
                continue;
            }
            std::array<char, 4> chunk_type = { '\0', '\0', '\0', '\0' };
            if (!webp_chunk_from_route(block.route, &chunk_type)) {
                out.status = TransferStatus::Unsupported;
                if (out.code == EmitTransferCode::None) {
                    out.code = EmitTransferCode::UnsupportedRoute;
                }
                out.errors += 1U;
                out.failed_block_index = i;
                out.message = "unsupported webp route: " + block.route;
                if (options.stop_on_error) {
                    return out;
                }
                continue;
            }
            append_package_prepared_block_chunk(
                &plan, i,
                8U + static_cast<uint64_t>(block.payload.size())
                    + static_cast<uint64_t>((block.payload.size() & 1U) != 0U));
        }
    } else if (transfer_target_is_bmff(bundle.target_format)) {
        for (uint32_t i = 0; i < bundle.blocks.size(); ++i) {
            const PreparedTransferBlock& block = bundle.blocks[i];
            if (options.skip_empty_payloads && block.payload.empty()) {
                out.skipped += 1U;
                continue;
            }
            uint32_t item_type = 0U;
            bool mime_xmp      = false;
            if (!bmff_item_from_route(block.route, &item_type, &mime_xmp)) {
                out.status = TransferStatus::Unsupported;
                if (out.code == EmitTransferCode::None) {
                    out.code = EmitTransferCode::UnsupportedRoute;
                }
                out.errors += 1U;
                out.failed_block_index = i;
                out.message = "unsupported bmff route: " + block.route;
                if (options.stop_on_error) {
                    return out;
                }
                continue;
            }
            (void)item_type;
            (void)mime_xmp;
            append_package_prepared_block_chunk(&plan, i,
                                                static_cast<uint64_t>(
                                                    block.payload.size()));
        }
    } else {
        out.status = TransferStatus::Unsupported;
        out.code   = EmitTransferCode::InvalidArgument;
        out.errors = 1U;
        out.message
            = "emit package builder currently supports jpeg, jxl, webp, and bmff targets";
        return out;
    }

    plan.output_size = package_plan_next_output_offset(plan);
    *out_plan        = std::move(plan);

    if (out.errors == 0U) {
        out.status  = TransferStatus::Ok;
        out.code    = EmitTransferCode::None;
        out.emitted = static_cast<uint32_t>(out_plan->chunks.size());
    }
    return out;
}

EmitTransferResult
build_prepared_bundle_jpeg_package(
    std::span<const std::byte> input_jpeg, const PreparedTransferBundle& bundle,
    const JpegEditPlan& plan, PreparedTransferPackagePlan* out_plan) noexcept
{
    EmitTransferResult out;
    if (!out_plan) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_plan is null";
        return out;
    }
    out_plan->contract_version = bundle.contract_version;
    out_plan->target_format    = bundle.target_format;
    out_plan->input_size       = static_cast<uint64_t>(input_jpeg.size());
    out_plan->output_size      = 0U;
    out_plan->chunks.clear();

    if (plan.status != TransferStatus::Ok) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "edit plan status is not ok";
        return out;
    }
    if (bundle.target_format != TransferTargetFormat::Jpeg) {
        out.status  = TransferStatus::Unsupported;
        out.code    = EmitTransferCode::BundleTargetNotJpeg;
        out.errors  = 1U;
        out.message = "bundle target format is not jpeg";
        return out;
    }

    EmitTransferOptions opts;
    opts.skip_empty_payloads = true;
    opts.stop_on_error       = true;
    PreparedJpegEmitPlan compiled;
    out = compile_prepared_bundle_jpeg(bundle, &compiled, opts);
    if (out.status != TransferStatus::Ok) {
        return out;
    }

    std::vector<PlannedJpegSegment> desired;
    desired.reserve(compiled.ops.size());
    std::vector<PreparedJpegEmitOp> desired_ops;
    desired_ops.reserve(compiled.ops.size());
    for (size_t i = 0; i < compiled.ops.size(); ++i) {
        const PreparedJpegEmitOp& op = compiled.ops[i];
        if (op.block_index >= bundle.blocks.size()) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "compiled jpeg op block index out of range";
            return out;
        }
        const PreparedTransferBlock& block = bundle.blocks[op.block_index];
        PlannedJpegSegment seg;
        seg.marker  = op.marker_code;
        seg.route   = block.route;
        seg.payload = std::span<const std::byte>(block.payload.data(),
                                                 block.payload.size());
        desired.push_back(std::move(seg));
        desired_ops.push_back(op);
    }

    const JpegScanResult scan = scan_leading_jpeg_segments(input_jpeg);
    if (scan.status != TransferStatus::Ok) {
        out.status  = scan.status;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = scan.message;
        return out;
    }

    if (plan.selected_mode == JpegEditMode::InPlace) {
        std::vector<PlannedJpegReplacement> replacements;
        replacements.reserve(desired.size());
        for (size_t i = 0; i < desired.size(); ++i) {
            const PlannedJpegSegment& d = desired[i];
            const size_t occ    = route_occurrence_before(desired, i, d.route);
            size_t existing_idx = 0U;
            if (!find_existing_by_route_occurrence(scan.leading_segments,
                                                   d.route, occ,
                                                   &existing_idx)) {
                out.status  = TransferStatus::Unsupported;
                out.code    = EmitTransferCode::PlanMismatch;
                out.errors  = 1U;
                out.message = "in_place match not found for route: " + d.route;
                return out;
            }
            const ExistingJpegSegment& e = scan.leading_segments[existing_idx];
            if (e.payload_len != d.payload.size()) {
                out.status  = TransferStatus::Unsupported;
                out.code    = EmitTransferCode::PlanMismatch;
                out.errors  = 1U;
                out.message = "in_place size mismatch for route: " + d.route;
                return out;
            }
            PlannedJpegReplacement repl;
            repl.payload_off = e.payload_off;
            repl.payload_len = e.payload_len;
            repl.payload     = d.payload;
            repl.route       = d.route;
            replacements.push_back(std::move(repl));
        }
        std::sort(replacements.begin(), replacements.end(),
                  PlannedJpegReplacementLess {});

        size_t cursor = 0U;
        for (size_t i = 0; i < replacements.size(); ++i) {
            const PlannedJpegReplacement& repl = replacements[i];
            if (repl.payload_off < cursor
                || repl.payload_off + repl.payload_len > input_jpeg.size()) {
                out.status  = TransferStatus::Malformed;
                out.code    = EmitTransferCode::PlanMismatch;
                out.errors  = 1U;
                out.message = "in_place payload range out of bounds";
                return out;
            }
            append_package_source_chunk(out_plan, static_cast<uint64_t>(cursor),
                                        static_cast<uint64_t>(repl.payload_off
                                                              - cursor));
            append_package_inline_chunk(
                out_plan, std::span<const std::byte>(repl.payload.data(),
                                                     repl.payload.size()));
            cursor = repl.payload_off + repl.payload_len;
        }
        if (cursor > input_jpeg.size()) {
            out.status  = TransferStatus::Malformed;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "jpeg in_place cursor is out of range";
            return out;
        }
        append_package_source_chunk(out_plan, static_cast<uint64_t>(cursor),
                                    static_cast<uint64_t>(input_jpeg.size()
                                                          - cursor));
        out_plan->output_size = plan.output_size;
        out.status            = TransferStatus::Ok;
        out.code              = EmitTransferCode::None;
        return out;
    }

    if (plan.selected_mode != JpegEditMode::MetadataRewrite) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "unsupported selected edit mode";
        return out;
    }
    if (input_jpeg.size() < 2U) {
        out.status  = TransferStatus::Malformed;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "jpeg input is too small";
        return out;
    }

    append_package_source_chunk(out_plan, 0U, 2U);

    bool inserted = false;
    for (size_t i = 0; i < scan.leading_segments.size(); ++i) {
        const ExistingJpegSegment& e = scan.leading_segments[i];
        const bool replaced = should_strip_existing_jpeg_segment(bundle, e,
                                                                 desired);
        if (replaced) {
            if (!inserted) {
                for (size_t si = 0; si < desired.size(); ++si) {
                    append_package_jpeg_segment_chunk(
                        out_plan, desired_ops[si].block_index,
                        desired_ops[si].marker_code,
                        desired[si].payload.size());
                }
                inserted = true;
            }
            continue;
        }
        const size_t seg_end = e.payload_off + e.payload_len;
        if (seg_end > input_jpeg.size() || e.marker_off > seg_end) {
            out.status  = TransferStatus::Malformed;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "existing jpeg segment range is invalid";
            return out;
        }
        append_package_source_chunk(out_plan,
                                    static_cast<uint64_t>(e.marker_off),
                                    static_cast<uint64_t>(seg_end
                                                          - e.marker_off));
    }
    if (!inserted) {
        for (size_t si = 0; si < desired.size(); ++si) {
            append_package_jpeg_segment_chunk(out_plan,
                                              desired_ops[si].block_index,
                                              desired_ops[si].marker_code,
                                              desired[si].payload.size());
        }
    }
    if (scan.scan_end > input_jpeg.size()) {
        out.status  = TransferStatus::Malformed;
        out.code    = EmitTransferCode::PlanMismatch;
        out.errors  = 1U;
        out.message = "jpeg rewrite scan end is out of range";
        return out;
    }
    append_package_source_chunk(out_plan, static_cast<uint64_t>(scan.scan_end),
                                static_cast<uint64_t>(input_jpeg.size()
                                                      - scan.scan_end));

    out_plan->output_size = plan.output_size;
    out.status            = TransferStatus::Ok;
    out.code              = EmitTransferCode::None;
    return out;
}

EmitTransferResult
build_prepared_bundle_tiff_package(
    std::span<const std::byte> input_tiff, const PreparedTransferBundle& bundle,
    const TiffEditPlan& plan, PreparedTransferPackagePlan* out_plan) noexcept
{
    EmitTransferResult out;
    if (!out_plan) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_plan is null";
        return out;
    }
    out_plan->contract_version = bundle.contract_version;
    out_plan->target_format    = bundle.target_format;
    out_plan->input_size       = static_cast<uint64_t>(input_tiff.size());
    out_plan->output_size      = 0U;
    out_plan->chunks.clear();

    if (plan.status != TransferStatus::Ok) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "edit plan status is not ok";
        return out;
    }
    if (bundle.target_format != TransferTargetFormat::Tiff) {
        out.status  = TransferStatus::Unsupported;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "bundle target format is not tiff";
        return out;
    }

    const std::vector<TiffTagUpdate> updates = collect_tiff_tag_updates(bundle);
    const std::vector<std::byte> exif_app1_payload
        = first_tiff_exif_app1_payload(bundle);
    if (plan.tag_updates != static_cast<uint32_t>(updates.size())
        || plan.has_exif_ifd != !exif_app1_payload.empty()) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::PlanMismatch;
        out.errors  = 1U;
        out.message = "tiff edit plan no longer matches bundle";
        return out;
    }

    TiffRewriteTail rewrite;
    std::string err;
    if (!build_tiff_rewrite_tail(
            input_tiff, updates,
            std::span<const std::byte>(exif_app1_payload.data(),
                                       exif_app1_payload.size()),
            &rewrite, &err)) {
        out.status  = tiff_edit_status_from_error(err);
        out.code    = EmitTransferCode::PlanMismatch;
        out.errors  = 1U;
        out.message = err;
        return out;
    }

    if (input_tiff.size() < 8U) {
        out.status  = TransferStatus::Malformed;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "tiff input is too small";
        return out;
    }

    append_package_source_chunk(out_plan, 0U, 4U);
    std::array<std::byte, 4> patched_ifd0 = {
        std::byte { 0x00 },
        std::byte { 0x00 },
        std::byte { 0x00 },
        std::byte { 0x00 },
    };
    write_u32_tiff_bytes(&patched_ifd0, rewrite.new_ifd0_off, rewrite.endian);
    append_package_inline_chunk(out_plan,
                                std::span<const std::byte>(patched_ifd0.data(),
                                                           patched_ifd0.size()));
    append_package_source_chunk(out_plan, 8U,
                                static_cast<uint64_t>(input_tiff.size() - 8U));
    append_package_inline_chunk(
        out_plan, std::span<const std::byte>(rewrite.tail_bytes.data(),
                                             rewrite.tail_bytes.size()));
    out_plan->output_size = plan.output_size;
    out.status            = TransferStatus::Ok;
    out.code              = EmitTransferCode::None;
    return out;
}

EmitTransferResult
write_prepared_transfer_package(std::span<const std::byte> input,
                                const PreparedTransferBundle& bundle,
                                const PreparedTransferPackagePlan& plan,
                                TransferByteWriter& writer) noexcept
{
    EmitTransferResult out
        = validate_prepared_transfer_package_plan(input, bundle, plan);
    if (out.status != TransferStatus::Ok) {
        return out;
    }

    for (size_t i = 0; i < plan.chunks.size(); ++i) {
        const PreparedTransferPackageChunk& chunk = plan.chunks[i];
        switch (chunk.kind) {
        case TransferPackageChunkKind::SourceRange:
            if (!write_transfer_bytes(
                    writer,
                    input.subspan(static_cast<size_t>(chunk.source_offset),
                                  static_cast<size_t>(chunk.size)),
                    &out, "prepared transfer package source write failed")) {
                return out;
            }
            break;
        case TransferPackageChunkKind::PreparedJpegSegment: {
            const PreparedTransferBlock& block
                = bundle.blocks[chunk.block_index];
            if (!write_jpeg_marker_segment(
                    writer, chunk.jpeg_marker_code,
                    std::span<const std::byte>(block.payload.data(),
                                               block.payload.size()),
                    &out)) {
                return out;
            }
            break;
        }
        case TransferPackageChunkKind::PreparedTransferBlock: {
            const PreparedTransferBlock& block
                = bundle.blocks[chunk.block_index];
            if (bundle.target_format == TransferTargetFormat::Jpeg) {
                uint8_t marker = 0U;
                if (!marker_from_jpeg_route(block.route, &marker)) {
                    out.status = TransferStatus::Unsupported;
                    out.code   = EmitTransferCode::UnsupportedRoute;
                    out.errors += 1U;
                    out.failed_block_index = chunk.block_index;
                    out.message
                        = "prepared transfer block route is not a supported jpeg marker";
                    return out;
                }
                if (!write_jpeg_marker_segment(
                        writer, marker,
                        std::span<const std::byte>(block.payload.data(),
                                                   block.payload.size()),
                        &out)) {
                    return out;
                }
            } else if (bundle.target_format == TransferTargetFormat::Jxl) {
                std::array<char, 4> box_type = { '\0', '\0', '\0', '\0' };
                bool compress                = false;
                if (!jxl_box_from_route(block.route, &box_type, &compress)) {
                    out.status = TransferStatus::Unsupported;
                    out.code   = EmitTransferCode::UnsupportedRoute;
                    out.errors += 1U;
                    out.failed_block_index = chunk.block_index;
                    out.message
                        = "prepared transfer block route is not a supported jxl box";
                    return out;
                }
                if (!write_jxl_box(
                        writer, box_type,
                        std::span<const std::byte>(block.payload.data(),
                                                   block.payload.size()),
                        &out)) {
                    return out;
                }
            } else if (bundle.target_format == TransferTargetFormat::Webp) {
                std::array<char, 4> chunk_type = { '\0', '\0', '\0', '\0' };
                if (!webp_chunk_from_route(block.route, &chunk_type)) {
                    out.status = TransferStatus::Unsupported;
                    out.code   = EmitTransferCode::UnsupportedRoute;
                    out.errors += 1U;
                    out.failed_block_index = chunk.block_index;
                    out.message
                        = "prepared transfer block route is not a supported webp chunk";
                    return out;
                }
                if (!write_webp_chunk(
                        writer, chunk_type,
                        std::span<const std::byte>(block.payload.data(),
                                                   block.payload.size()),
                        &out)) {
                    return out;
                }
            } else {
                out.status = TransferStatus::Unsupported;
                out.code   = EmitTransferCode::InvalidArgument;
                out.errors += 1U;
                out.failed_block_index = chunk.block_index;
                out.message
                    = "prepared transfer block chunks are only supported for jpeg, jxl, and webp targets";
                return out;
            }
            break;
        }
        case TransferPackageChunkKind::InlineBytes:
            if (!write_transfer_bytes(
                    writer,
                    std::span<const std::byte>(chunk.inline_bytes.data(),
                                               chunk.inline_bytes.size()),
                    &out, "prepared transfer package inline write failed")) {
                return out;
            }
            break;
        }
    }

    out.status = TransferStatus::Ok;
    out.code   = EmitTransferCode::None;
    return out;
}

EmitTransferResult
build_prepared_transfer_package_batch(
    std::span<const std::byte> input, const PreparedTransferBundle& bundle,
    const PreparedTransferPackagePlan& plan,
    PreparedTransferPackageBatch* out_batch) noexcept
{
    EmitTransferResult out
        = validate_prepared_transfer_package_plan(input, bundle, plan);
    if (out.status != TransferStatus::Ok) {
        return out;
    }
    if (!out_batch) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "prepared transfer package batch output is null";
        return out;
    }

    PreparedTransferPackageBatch batch;
    batch.contract_version = plan.contract_version;
    batch.target_format    = plan.target_format;
    batch.input_size       = plan.input_size;
    batch.output_size      = plan.output_size;
    batch.chunks.reserve(plan.chunks.size());

    for (size_t i = 0; i < plan.chunks.size(); ++i) {
        const PreparedTransferPackageChunk& chunk = plan.chunks[i];
        PreparedTransferPackageBlob one;
        one.kind             = chunk.kind;
        one.output_offset    = chunk.output_offset;
        one.source_offset    = chunk.source_offset;
        one.block_index      = chunk.block_index;
        one.jpeg_marker_code = chunk.jpeg_marker_code;
        if ((chunk.kind == TransferPackageChunkKind::PreparedTransferBlock
             || chunk.kind == TransferPackageChunkKind::PreparedJpegSegment)
            && chunk.block_index < bundle.blocks.size()) {
            one.route = bundle.blocks[chunk.block_index].route;
        }
        if (!materialize_prepared_transfer_package_chunk(input, bundle, chunk,
                                                         &one.bytes, &out)) {
            return out;
        }
        batch.chunks.push_back(std::move(one));
    }

    const EmitTransferResult validated
        = validate_prepared_transfer_package_batch(batch);
    if (validated.status != TransferStatus::Ok) {
        return validated;
    }
    *out_batch = std::move(batch);
    out.status = TransferStatus::Ok;
    out.code   = EmitTransferCode::None;
    return out;
}

EmitTransferResult
write_prepared_transfer_package_batch(const PreparedTransferPackageBatch& batch,
                                      TransferByteWriter& writer) noexcept
{
    EmitTransferResult out = validate_prepared_transfer_package_batch(batch);
    if (out.status != TransferStatus::Ok) {
        return out;
    }

    for (size_t i = 0; i < batch.chunks.size(); ++i) {
        const PreparedTransferPackageBlob& chunk = batch.chunks[i];
        if (!write_transfer_bytes(
                writer,
                std::span<const std::byte>(chunk.bytes.data(),
                                           chunk.bytes.size()),
                &out, "prepared transfer package batch write failed")) {
            return out;
        }
    }

    out.status = TransferStatus::Ok;
    out.code   = EmitTransferCode::None;
    return out;
}


TransferSemanticKind
classify_transfer_route_semantic_kind(std::string_view route) noexcept
{
    if (route == "jpeg:app1-exif" || route == "tiff:ifd-exif-app1"
        || route == "jxl:box-exif" || route == "webp:chunk-exif"
        || route == "bmff:item-exif") {
        return TransferSemanticKind::Exif;
    }
    if (route == "jpeg:app1-xmp" || route == "tiff:tag-700-xmp"
        || route == "jxl:box-xml" || route == "webp:chunk-xmp"
        || route == "bmff:item-xmp") {
        return TransferSemanticKind::Xmp;
    }
    if (route == "jpeg:app2-icc" || route == "tiff:tag-34675-icc"
        || route == "jxl:icc-profile" || route == "webp:chunk-iccp") {
        return TransferSemanticKind::Icc;
    }
    if (route == "jpeg:app13-iptc" || route == "tiff:tag-33723-iptc") {
        return TransferSemanticKind::Iptc;
    }
    if (route == "jpeg:app11-jumbf" || route == "jxl:box-jumb"
        || route == "bmff:item-jumb") {
        return TransferSemanticKind::Jumbf;
    }
    if (route == "jpeg:app11-c2pa" || route == "jxl:box-c2pa"
        || route == "webp:chunk-c2pa" || route == "bmff:item-c2pa") {
        return TransferSemanticKind::C2pa;
    }
    return TransferSemanticKind::Unknown;
}


EmitTransferResult
collect_prepared_transfer_package_views(
    const PreparedTransferPackageBatch& batch,
    std::vector<PreparedTransferPackageView>* out) noexcept
{
    EmitTransferResult result = validate_prepared_transfer_package_batch(batch);
    if (result.status != TransferStatus::Ok) {
        return result;
    }
    if (!out) {
        result.status  = TransferStatus::InvalidArgument;
        result.code    = EmitTransferCode::InvalidArgument;
        result.errors  = 1U;
        result.message = "out is null";
        return result;
    }

    out->clear();
    out->reserve(batch.chunks.size());
    for (size_t i = 0; i < batch.chunks.size(); ++i) {
        const PreparedTransferPackageBlob& chunk = batch.chunks[i];
        PreparedTransferPackageView one;
        one.semantic_kind = classify_transfer_route_semantic_kind(chunk.route);
        one.route         = chunk.route;
        one.package_kind  = chunk.kind;
        one.output_offset = chunk.output_offset;
        one.jpeg_marker_code = chunk.jpeg_marker_code;
        one.bytes            = std::span<const std::byte>(chunk.bytes.data(),
                                                          chunk.bytes.size());
        out->push_back(one);
    }

    result.status  = TransferStatus::Ok;
    result.code    = EmitTransferCode::None;
    result.emitted = static_cast<uint32_t>(out->size());
    return result;
}


PreparedTransferPackageReplayResult
replay_prepared_transfer_package_batch(
    const PreparedTransferPackageBatch& batch,
    const PreparedTransferPackageReplayCallbacks& callbacks) noexcept
{
    PreparedTransferPackageReplayResult result;
    const EmitTransferResult validated
        = validate_prepared_transfer_package_batch(batch);
    if (validated.status != TransferStatus::Ok) {
        result.status  = validated.status;
        result.code    = validated.code;
        result.message = validated.message;
        return result;
    }
    if (!callbacks.emit_chunk) {
        result.status  = TransferStatus::InvalidArgument;
        result.code    = EmitTransferCode::InvalidArgument;
        result.message = "emit_chunk callback is null";
        return result;
    }

    if (callbacks.begin_batch) {
        const TransferStatus begin_status
            = callbacks.begin_batch(callbacks.user, batch.target_format,
                                    static_cast<uint32_t>(batch.chunks.size()));
        if (begin_status != TransferStatus::Ok) {
            result.status = begin_status;
            result.code   = EmitTransferCode::BackendWriteFailed;
            result.message
                = "prepared transfer package replay begin callback failed";
            return result;
        }
    }

    for (size_t i = 0; i < batch.chunks.size(); ++i) {
        const PreparedTransferPackageBlob& chunk = batch.chunks[i];
        PreparedTransferPackageView view;
        view.semantic_kind = classify_transfer_route_semantic_kind(chunk.route);
        view.route         = chunk.route;
        view.package_kind  = chunk.kind;
        view.output_offset = chunk.output_offset;
        view.jpeg_marker_code = chunk.jpeg_marker_code;
        view.bytes            = std::span<const std::byte>(chunk.bytes.data(),
                                                           chunk.bytes.size());
        const TransferStatus emit_status = callbacks.emit_chunk(callbacks.user,
                                                                &view);
        if (emit_status != TransferStatus::Ok) {
            result.status             = emit_status;
            result.code               = EmitTransferCode::BackendWriteFailed;
            result.failed_chunk_index = static_cast<uint32_t>(i);
            result.message
                = "prepared transfer package replay emit callback failed";
            return result;
        }
        result.replayed += 1U;
    }

    if (callbacks.end_batch) {
        const TransferStatus end_status
            = callbacks.end_batch(callbacks.user, batch.target_format);
        if (end_status != TransferStatus::Ok) {
            result.status = end_status;
            result.code   = EmitTransferCode::BackendWriteFailed;
            result.message
                = "prepared transfer package replay end callback failed";
            return result;
        }
    }

    result.status = TransferStatus::Ok;
    result.code   = EmitTransferCode::None;
    return result;
}

PrepareTransferFileResult
prepare_metadata_for_target_file(
    const char* path, const PrepareTransferFileOptions& options) noexcept
{
    PrepareTransferFileResult out;

    if (!path || !*path) {
        out.file_status      = TransferFileStatus::InvalidArgument;
        out.code             = PrepareTransferFileCode::EmptyPath;
        out.prepare.status   = TransferStatus::InvalidArgument;
        out.prepare.code     = PrepareTransferCode::None;
        out.prepare.errors   = 1U;
        out.prepare.message  = "path is empty";
        out.read.scan.status = ScanStatus::Unsupported;
        return out;
    }

    OpenMetaResourcePolicy policy = options.policy;
    normalize_resource_policy(&policy);

    SimpleMetaDecodeOptions decode_options;
    apply_resource_policy(policy, &decode_options.exif,
                          &decode_options.payload);
    apply_resource_policy(policy, &decode_options.xmp, &decode_options.exr,
                          &decode_options.jumbf, &decode_options.icc,
                          &decode_options.iptc, &decode_options.photoshop_irb);

    decode_options.exif.include_pointer_tags = options.include_pointer_tags;
    decode_options.exif.decode_makernote     = options.decode_makernote;
    decode_options.exif.decode_embedded_containers
        = options.decode_embedded_containers;
    decode_options.payload.decompress = options.decompress;
    decode_options.xmp.malformed_mode = XmpDecodeMalformedMode::OutputTruncated;

    MappedFile mapped;
    const MappedFileStatus file_status = mapped.open(path,
                                                     policy.max_file_bytes);
    if (file_status != MappedFileStatus::Ok) {
        out.file_status    = map_file_status(file_status);
        out.code           = PrepareTransferFileCode::MapFailed;
        out.prepare.status = TransferStatus::Unsupported;
        out.prepare.errors = 1U;
        out.prepare.message
            = "failed to map input file for transfer preparation";
        return out;
    }

    out.file_size = mapped.size();

    std::vector<ContainerBlockRef> blocks(256U);
    std::vector<ExifIfdRef> ifds(512U);
    std::vector<std::byte> payload(1024U * 1024U);
    std::vector<uint32_t> payload_parts(16384U);
    MetaStore store;

    for (;;) {
        store    = MetaStore();
        out.read = simple_meta_read(
            mapped.bytes(), store,
            std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
            std::span<ExifIfdRef>(ifds.data(), ifds.size()),
            std::span<std::byte>(payload.data(), payload.size()),
            std::span<uint32_t>(payload_parts.data(), payload_parts.size()),
            decode_options);

        bool retried = false;
        if (out.read.scan.status == ScanStatus::OutputTruncated
            && out.read.scan.needed > blocks.size()) {
            blocks.resize(out.read.scan.needed);
            retried = true;
        }
        if (out.read.exif.status == ExifDecodeStatus::OutputTruncated
            && out.read.exif.ifds_needed > ifds.size()) {
            ifds.resize(out.read.exif.ifds_needed);
            retried = true;
        }
        if (out.read.payload.status == PayloadStatus::OutputTruncated
            && out.read.payload.needed > payload.size()) {
            if (out.read.payload.needed > static_cast<uint64_t>(SIZE_MAX)) {
                out.file_status = TransferFileStatus::ReadFailed;
                out.code = PrepareTransferFileCode::PayloadBufferPlatformLimit;
                out.prepare.status = TransferStatus::LimitExceeded;
                out.prepare.errors = 1U;
                out.prepare.message
                    = "payload buffer growth exceeds platform limits";
                return out;
            }
            payload.resize(static_cast<size_t>(out.read.payload.needed));
            retried = true;
        }
        if (!retried) {
            break;
        }
    }

    if (has_read_failure(out.read)) {
        out.file_status    = TransferFileStatus::ReadFailed;
        out.code           = PrepareTransferFileCode::DecodeFailed;
        out.prepare.status = TransferStatus::Malformed;
        out.prepare.errors = 1U;
        out.prepare.message
            = "read/decode phase failed before transfer preparation";
        return out;
    }

    store.finalize();
    out.entry_count = static_cast<uint32_t>(store.entries().size());
    out.file_status = TransferFileStatus::Ok;
    out.code        = PrepareTransferFileCode::None;
    TransferPrepareCapabilities caps;
    if (options.prepare.target_format == TransferTargetFormat::Jpeg
        || options.prepare.target_format == TransferTargetFormat::Jxl
        || transfer_target_is_bmff(options.prepare.target_format)) {
        const std::span<const ContainerBlockRef> scanned_blocks(
            blocks.data(), static_cast<size_t>(out.read.scan.written));
        for (size_t i = 0; i < scanned_blocks.size(); ++i) {
            if (is_source_jumbf_block(scanned_blocks[i])) {
                if (options.prepare.target_format
                    == TransferTargetFormat::Jpeg) {
                    caps.jpeg_jumbf_passthrough = true;
                } else if (options.prepare.target_format
                           == TransferTargetFormat::Jxl) {
                    caps.jxl_jumbf_passthrough = true;
                } else {
                    caps.bmff_jumbf_passthrough = true;
                }
                break;
            }
        }
        if (caps.jpeg_jumbf_passthrough || caps.jxl_jumbf_passthrough
            || caps.bmff_jumbf_passthrough) {
            caps.source_c2pa_payload_class
                = classify_source_c2pa_payloads_for_jpeg(
                    mapped.bytes(), scanned_blocks, decode_options.payload);
        }
    }

    out.prepare = prepare_metadata_for_target_impl(store, options.prepare, caps,
                                                   &out.bundle);

    if (options.prepare.target_format == TransferTargetFormat::Jpeg) {
        const JpegScanResult jpeg_scan = scan_leading_jpeg_segments(
            mapped.bytes());
        if (jpeg_scan.status == TransferStatus::Ok) {
            out.bundle.c2pa_rewrite.existing_carrier_segments
                = count_existing_jpeg_segments_by_route(jpeg_scan,
                                                        "jpeg:app11-c2pa");
        }
    }

    if (caps.jpeg_jumbf_passthrough
        && out.prepare.status != TransferStatus::Malformed) {
        const SourceJumbfAppendResult append_jumbf
            = append_source_jumbf_blocks_for_jpeg(
                mapped.bytes(),
                std::span<const ContainerBlockRef>(
                    blocks.data(), static_cast<size_t>(out.read.scan.written)),
                decode_options.payload, &out.bundle);
        if (append_jumbf.errors > 0U) {
            out.prepare.warnings += append_jumbf.errors;
            append_message(&out.prepare.message, append_jumbf.message);
        }

        PreparedTransferPolicyDecision* decision
            = find_policy_decision(&out.bundle, TransferPolicySubject::Jumbf);
        if (decision && decision->effective == TransferPolicyAction::Keep
            && append_jumbf.emitted_jumbf == 0U) {
            decision->effective = TransferPolicyAction::Drop;
            decision->reason
                = TransferPolicyReason::TargetSerializationUnavailable;
            decision->message
                = "source jumbf payloads could not be repacked into jpeg app11";
            if (out.bundle.blocks.empty()) {
                out.prepare.status = TransferStatus::Unsupported;
                if (out.prepare.code == PrepareTransferCode::None) {
                    out.prepare.code
                        = PrepareTransferCode::RequestedMetadataNotSerializable;
                }
            }
        }

        PreparedTransferPolicyDecision* c2pa_decision
            = find_policy_decision(&out.bundle, TransferPolicySubject::C2pa);
        if (c2pa_decision
            && c2pa_decision->c2pa_mode == TransferC2paMode::PreserveRaw
            && append_jumbf.emitted_c2pa == 0U) {
            c2pa_decision->effective = TransferPolicyAction::Drop;
            c2pa_decision->reason
                = TransferPolicyReason::TargetSerializationUnavailable;
            c2pa_decision->c2pa_mode = TransferC2paMode::Drop;
            c2pa_decision->c2pa_prepared_output
                = TransferC2paPreparedOutput::Dropped;
            c2pa_decision->message
                = "source draft c2pa invalidation payload could not be "
                  "repacked into jpeg app11";
            if (out.bundle.blocks.empty()) {
                out.prepare.status = TransferStatus::Unsupported;
                if (out.prepare.code == PrepareTransferCode::None) {
                    out.prepare.code
                        = PrepareTransferCode::RequestedMetadataNotSerializable;
                }
            }
        } else if (append_jumbf.emitted_blocks > 0U
                   && out.prepare.status == TransferStatus::Unsupported
                   && out.prepare.code
                          == PrepareTransferCode::
                              RequestedMetadataNotSerializable) {
            out.prepare.status = TransferStatus::Ok;
        }
    } else if (caps.jxl_jumbf_passthrough
               && out.prepare.status != TransferStatus::Malformed) {
        const SourceJumbfAppendResult append_jumbf
            = append_source_jumbf_blocks_for_jxl(
                mapped.bytes(),
                std::span<const ContainerBlockRef>(
                    blocks.data(), static_cast<size_t>(out.read.scan.written)),
                decode_options.payload, &out.bundle);
        if (append_jumbf.errors > 0U) {
            out.prepare.warnings += append_jumbf.errors;
            append_message(&out.prepare.message, append_jumbf.message);
        }

        PreparedTransferPolicyDecision* decision
            = find_policy_decision(&out.bundle, TransferPolicySubject::Jumbf);
        if (decision && decision->effective == TransferPolicyAction::Keep
            && append_jumbf.emitted_jumbf == 0U) {
            decision->effective = TransferPolicyAction::Drop;
            decision->reason
                = TransferPolicyReason::TargetSerializationUnavailable;
            decision->message
                = "source jumbf payloads could not be emitted as jxl boxes";
            if (out.bundle.blocks.empty()) {
                out.prepare.status = TransferStatus::Unsupported;
                if (out.prepare.code == PrepareTransferCode::None) {
                    out.prepare.code
                        = PrepareTransferCode::RequestedMetadataNotSerializable;
                }
            }
        }

        PreparedTransferPolicyDecision* c2pa_decision
            = find_policy_decision(&out.bundle, TransferPolicySubject::C2pa);
        if (c2pa_decision
            && c2pa_decision->c2pa_mode == TransferC2paMode::PreserveRaw
            && append_jumbf.emitted_c2pa == 0U) {
            c2pa_decision->effective = TransferPolicyAction::Drop;
            c2pa_decision->reason
                = TransferPolicyReason::TargetSerializationUnavailable;
            c2pa_decision->c2pa_mode = TransferC2paMode::Drop;
            c2pa_decision->c2pa_prepared_output
                = TransferC2paPreparedOutput::Dropped;
            c2pa_decision->message
                = "source draft c2pa invalidation payload could not be "
                  "emitted as a jxl box";
            if (out.bundle.blocks.empty()) {
                out.prepare.status = TransferStatus::Unsupported;
                if (out.prepare.code == PrepareTransferCode::None) {
                    out.prepare.code
                        = PrepareTransferCode::RequestedMetadataNotSerializable;
                }
            }
        } else if (append_jumbf.emitted_blocks > 0U
                   && out.prepare.status == TransferStatus::Unsupported
                   && out.prepare.code
                          == PrepareTransferCode::
                              RequestedMetadataNotSerializable) {
            out.prepare.status = TransferStatus::Ok;
        }
    } else if (caps.bmff_jumbf_passthrough
               && out.prepare.status != TransferStatus::Malformed) {
        const SourceJumbfAppendResult append_jumbf
            = append_source_jumbf_blocks_for_bmff(
                mapped.bytes(),
                std::span<const ContainerBlockRef>(
                    blocks.data(), static_cast<size_t>(out.read.scan.written)),
                decode_options.payload, &out.bundle);
        if (append_jumbf.errors > 0U) {
            out.prepare.warnings += append_jumbf.errors;
            append_message(&out.prepare.message, append_jumbf.message);
        }

        PreparedTransferPolicyDecision* decision
            = find_policy_decision(&out.bundle, TransferPolicySubject::Jumbf);
        if (decision && decision->effective == TransferPolicyAction::Keep
            && append_jumbf.emitted_jumbf == 0U) {
            decision->effective = TransferPolicyAction::Drop;
            decision->reason
                = TransferPolicyReason::TargetSerializationUnavailable;
            decision->message
                = "source jumbf payloads could not be emitted as bmff metadata items";
            if (out.bundle.blocks.empty()) {
                out.prepare.status = TransferStatus::Unsupported;
                if (out.prepare.code == PrepareTransferCode::None) {
                    out.prepare.code
                        = PrepareTransferCode::RequestedMetadataNotSerializable;
                }
            }
        }

        PreparedTransferPolicyDecision* c2pa_decision
            = find_policy_decision(&out.bundle, TransferPolicySubject::C2pa);
        if (c2pa_decision
            && c2pa_decision->c2pa_mode == TransferC2paMode::PreserveRaw
            && append_jumbf.emitted_c2pa == 0U) {
            c2pa_decision->effective = TransferPolicyAction::Drop;
            c2pa_decision->reason
                = TransferPolicyReason::TargetSerializationUnavailable;
            c2pa_decision->c2pa_mode = TransferC2paMode::Drop;
            c2pa_decision->c2pa_prepared_output
                = TransferC2paPreparedOutput::Dropped;
            c2pa_decision->message
                = "source draft c2pa invalidation payload could not be "
                  "emitted as a bmff metadata item";
            if (out.bundle.blocks.empty()) {
                out.prepare.status = TransferStatus::Unsupported;
                if (out.prepare.code == PrepareTransferCode::None) {
                    out.prepare.code
                        = PrepareTransferCode::RequestedMetadataNotSerializable;
                }
            }
        } else if (append_jumbf.emitted_blocks > 0U
                   && out.prepare.status == TransferStatus::Unsupported
                   && out.prepare.code
                          == PrepareTransferCode::
                              RequestedMetadataNotSerializable) {
            out.prepare.status = TransferStatus::Ok;
        }
    }

    if (options.prepare.target_format == TransferTargetFormat::Jpeg
        && out.bundle.c2pa_rewrite.state
               == TransferC2paRewriteState::SigningMaterialRequired) {
        std::string rewrite_error;
        if (!build_jpeg_c2pa_rewrite_chunks(mapped.bytes(), out.bundle,
                                            &out.bundle.c2pa_rewrite,
                                            &rewrite_error)) {
            if (!rewrite_error.empty()) {
                if (!out.bundle.c2pa_rewrite.message.empty()) {
                    out.bundle.c2pa_rewrite.message.append("; ");
                }
                out.bundle.c2pa_rewrite.message.append(rewrite_error);
            }
        }
    }
    return out;
}

namespace {

    static constexpr char kC2paHandoffMagic[8] = { 'O', 'M', 'C', '2',
                                                   'P', 'H', '0', '1' };
    static constexpr char kC2paSignedMagic[8]  = { 'O', 'M', 'C', '2',
                                                   'P', 'S', '0', '1' };
    static constexpr char kTransferPackageBatchMagic[8]
        = { 'O', 'M', 'T', 'P', 'K', 'G', '0', '1' };
    static constexpr uint32_t kC2paPackageVersion          = 1U;
    static constexpr uint32_t kTransferPackageBatchVersion = 2U;

    static void append_bool_u8(std::vector<std::byte>* out, bool value) noexcept
    {
        if (!out) {
            return;
        }
        out->push_back(value ? std::byte { 1U } : std::byte { 0U });
    }

    static void append_blob_le(std::vector<std::byte>* out,
                               std::span<const std::byte> bytes) noexcept
    {
        append_u64le(out, static_cast<uint64_t>(bytes.size()));
        if (!out || bytes.empty()) {
            return;
        }
        out->insert(out->end(), bytes.begin(), bytes.end());
    }

    static void append_string_le(std::vector<std::byte>* out,
                                 std::string_view value) noexcept
    {
        append_u64le(out, static_cast<uint64_t>(value.size()));
        append_ascii_bytes(out, value);
    }

    static void append_c2pa_rewrite_chunks_le(
        std::vector<std::byte>* out,
        std::span<const PreparedTransferC2paRewriteChunk> chunks) noexcept
    {
        append_u32le(out, static_cast<uint32_t>(chunks.size()));
        for (size_t i = 0; i < chunks.size(); ++i) {
            append_u8(out, static_cast<uint8_t>(chunks[i].kind));
            append_u64le(out, chunks[i].source_offset);
            append_u64le(out, chunks[i].size);
            append_u32le(out, chunks[i].block_index);
            append_u8(out, chunks[i].jpeg_marker_code);
        }
    }

    static bool parse_c2pa_rewrite_chunks_le(
        std::span<const std::byte> bytes, size_t* io_off,
        std::vector<PreparedTransferC2paRewriteChunk>* out) noexcept
    {
        if (!io_off || !out) {
            return false;
        }
        out->clear();
        uint32_t count = 0U;
        if (!read_u32le(bytes, io_off, &count)) {
            return false;
        }
        const size_t min_chunk_size = 1U + 8U + 8U + 4U + 1U;
        if (count > 0U && (bytes.size() - *io_off) / min_chunk_size < count) {
            return false;
        }
        out->reserve(count);
        for (uint32_t i = 0U; i < count; ++i) {
            uint8_t kind             = 0U;
            uint64_t source_offset   = 0U;
            uint64_t size            = 0U;
            uint32_t block_index     = 0U;
            uint8_t jpeg_marker_code = 0U;
            if (!read_u8(bytes, io_off, &kind)
                || !read_u64le(bytes, io_off, &source_offset)
                || !read_u64le(bytes, io_off, &size)
                || !read_u32le(bytes, io_off, &block_index)
                || !read_u8(bytes, io_off, &jpeg_marker_code)) {
                return false;
            }
            PreparedTransferC2paRewriteChunk chunk;
            chunk.kind = static_cast<TransferC2paRewriteChunkKind>(kind);
            chunk.source_offset    = source_offset;
            chunk.size             = size;
            chunk.block_index      = block_index;
            chunk.jpeg_marker_code = jpeg_marker_code;
            out->push_back(chunk);
        }
        return true;
    }

    static void append_c2pa_sign_request_le(
        std::vector<std::byte>* out,
        const PreparedTransferC2paSignRequest& request) noexcept
    {
        append_u8(out, static_cast<uint8_t>(request.status));
        append_u8(out, static_cast<uint8_t>(request.rewrite_state));
        append_u8(out, static_cast<uint8_t>(request.target_format));
        append_u8(out, static_cast<uint8_t>(request.source_kind));
        append_string_le(out, request.carrier_route);
        append_string_le(out, request.manifest_label);
        append_u32le(out, request.existing_carrier_segments);
        append_u32le(out, request.source_range_chunks);
        append_u32le(out, request.prepared_segment_chunks);
        append_u64le(out, request.content_binding_bytes);
        append_bool_u8(out, request.requires_manifest_builder);
        append_bool_u8(out, request.requires_content_binding);
        append_bool_u8(out, request.requires_certificate_chain);
        append_bool_u8(out, request.requires_private_key);
        append_bool_u8(out, request.requires_signing_time);
        append_string_le(out, request.message);
        append_c2pa_rewrite_chunks_le(out, request.content_binding_chunks);
    }

    static bool parse_bool_u8(std::span<const std::byte> bytes, size_t* io_off,
                              bool* out) noexcept
    {
        uint8_t value = 0U;
        if (!read_u8(bytes, io_off, &value) || value > 1U || !out) {
            return false;
        }
        *out = (value != 0U);
        return true;
    }

    static bool
    parse_c2pa_sign_request_le(std::span<const std::byte> bytes, size_t* io_off,
                               PreparedTransferC2paSignRequest* out) noexcept
    {
        if (!io_off || !out) {
            return false;
        }
        PreparedTransferC2paSignRequest request;
        uint8_t status        = 0U;
        uint8_t rewrite_state = 0U;
        uint8_t target_format = 0U;
        uint8_t source_kind   = 0U;
        if (!read_u8(bytes, io_off, &status)
            || !read_u8(bytes, io_off, &rewrite_state)
            || !read_u8(bytes, io_off, &target_format)
            || !read_u8(bytes, io_off, &source_kind)
            || !read_string_le(bytes, io_off, &request.carrier_route)
            || !read_string_le(bytes, io_off, &request.manifest_label)
            || !read_u32le(bytes, io_off, &request.existing_carrier_segments)
            || !read_u32le(bytes, io_off, &request.source_range_chunks)
            || !read_u32le(bytes, io_off, &request.prepared_segment_chunks)
            || !read_u64le(bytes, io_off, &request.content_binding_bytes)
            || !parse_bool_u8(bytes, io_off, &request.requires_manifest_builder)
            || !parse_bool_u8(bytes, io_off, &request.requires_content_binding)
            || !parse_bool_u8(bytes, io_off, &request.requires_certificate_chain)
            || !parse_bool_u8(bytes, io_off, &request.requires_private_key)
            || !parse_bool_u8(bytes, io_off, &request.requires_signing_time)
            || !read_string_le(bytes, io_off, &request.message)
            || !parse_c2pa_rewrite_chunks_le(bytes, io_off,
                                             &request.content_binding_chunks)) {
            return false;
        }
        request.status        = static_cast<TransferStatus>(status);
        request.rewrite_state = static_cast<TransferC2paRewriteState>(
            rewrite_state);
        request.target_format = static_cast<TransferTargetFormat>(
            target_format);
        request.source_kind = static_cast<TransferC2paSourceKind>(source_kind);
        *out                = std::move(request);
        return true;
    }

    static void append_c2pa_signer_input_le(
        std::vector<std::byte>* out,
        const PreparedTransferC2paSignerInput& input) noexcept
    {
        append_string_le(out, input.signing_time);
        append_blob_le(out, std::span<const std::byte>(
                                input.certificate_chain_bytes.data(),
                                input.certificate_chain_bytes.size()));
        append_string_le(out, input.private_key_reference);
        append_blob_le(out, std::span<const std::byte>(
                                input.manifest_builder_output.data(),
                                input.manifest_builder_output.size()));
        append_blob_le(out, std::span<const std::byte>(
                                input.signed_c2pa_logical_payload.data(),
                                input.signed_c2pa_logical_payload.size()));
    }

    static bool
    parse_c2pa_signer_input_le(std::span<const std::byte> bytes, size_t* io_off,
                               PreparedTransferC2paSignerInput* out) noexcept
    {
        if (!io_off || !out) {
            return false;
        }
        PreparedTransferC2paSignerInput input;
        if (!read_string_le(bytes, io_off, &input.signing_time)
            || !read_blob_le(bytes, io_off, &input.certificate_chain_bytes)
            || !read_string_le(bytes, io_off, &input.private_key_reference)
            || !read_blob_le(bytes, io_off, &input.manifest_builder_output)
            || !read_blob_le(bytes, io_off,
                             &input.signed_c2pa_logical_payload)) {
            return false;
        }
        *out = std::move(input);
        return true;
    }

    static void append_c2pa_binding_result_le(
        std::vector<std::byte>* out,
        const BuildPreparedC2paBindingResult& binding) noexcept
    {
        append_u8(out, static_cast<uint8_t>(binding.status));
        append_u8(out, static_cast<uint8_t>(binding.code));
        append_u64le(out, binding.written);
        append_u32le(out, binding.errors);
        append_string_le(out, binding.message);
    }

    static bool
    parse_c2pa_binding_result_le(std::span<const std::byte> bytes,
                                 size_t* io_off,
                                 BuildPreparedC2paBindingResult* out) noexcept
    {
        if (!io_off || !out) {
            return false;
        }
        BuildPreparedC2paBindingResult binding;
        uint8_t status = 0U;
        uint8_t code   = 0U;
        if (!read_u8(bytes, io_off, &status) || !read_u8(bytes, io_off, &code)
            || !read_u64le(bytes, io_off, &binding.written)
            || !read_u32le(bytes, io_off, &binding.errors)
            || !read_string_le(bytes, io_off, &binding.message)) {
            return false;
        }
        binding.status = static_cast<TransferStatus>(status);
        binding.code   = static_cast<EmitTransferCode>(code);
        *out           = std::move(binding);
        return true;
    }

    static PreparedTransferC2paPackageIoResult
    deserialize_c2pa_package_header(std::span<const std::byte> bytes,
                                    const char magic[8],
                                    size_t* io_off) noexcept
    {
        PreparedTransferC2paPackageIoResult out;
        if (!io_off) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::InvalidArgument;
            out.errors  = 1U;
            out.message = "package cursor is null";
            return out;
        }
        *io_off = 0U;
        if (bytes.size() < 12U) {
            out.status  = TransferStatus::Malformed;
            out.code    = EmitTransferCode::InvalidPayload;
            out.errors  = 1U;
            out.message = "package is too small";
            return out;
        }
        for (size_t i = 0; i < 8U; ++i) {
            if (bytes[i] != static_cast<std::byte>(magic[i])) {
                out.status  = TransferStatus::Malformed;
                out.code    = EmitTransferCode::InvalidPayload;
                out.errors  = 1U;
                out.message = "package magic is invalid";
                return out;
            }
        }
        *io_off          = 8U;
        uint32_t version = 0U;
        if (!read_u32le(bytes, io_off, &version)) {
            out.status  = TransferStatus::Malformed;
            out.code    = EmitTransferCode::InvalidPayload;
            out.errors  = 1U;
            out.message = "package header is truncated";
            return out;
        }
        if (version != kC2paPackageVersion) {
            out.status  = TransferStatus::Unsupported;
            out.code    = EmitTransferCode::InvalidPayload;
            out.errors  = 1U;
            out.message = "package version is unsupported";
            return out;
        }
        out.status = TransferStatus::Ok;
        out.code   = EmitTransferCode::None;
        return out;
    }

    static PreparedTransferPackageIoResult
    deserialize_transfer_package_batch_header(std::span<const std::byte> bytes,
                                              size_t* io_off) noexcept
    {
        PreparedTransferPackageIoResult out;
        if (!io_off) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::InvalidArgument;
            out.errors  = 1U;
            out.message = "package batch cursor is null";
            return out;
        }
        *io_off = 0U;
        if (bytes.size() < 12U) {
            out.status  = TransferStatus::Malformed;
            out.code    = EmitTransferCode::InvalidPayload;
            out.errors  = 1U;
            out.message = "package batch is too small";
            return out;
        }
        for (size_t i = 0; i < 8U; ++i) {
            if (bytes[i]
                != static_cast<std::byte>(kTransferPackageBatchMagic[i])) {
                out.status  = TransferStatus::Malformed;
                out.code    = EmitTransferCode::InvalidPayload;
                out.errors  = 1U;
                out.message = "package batch magic is invalid";
                return out;
            }
        }
        *io_off          = 8U;
        uint32_t version = 0U;
        if (!read_u32le(bytes, io_off, &version)) {
            out.status  = TransferStatus::Malformed;
            out.code    = EmitTransferCode::InvalidPayload;
            out.errors  = 1U;
            out.message = "package batch header is truncated";
            return out;
        }
        if (version != kTransferPackageBatchVersion) {
            out.status  = TransferStatus::Unsupported;
            out.code    = EmitTransferCode::InvalidPayload;
            out.errors  = 1U;
            out.message = "package batch version is unsupported";
            return out;
        }
        out.status = TransferStatus::Ok;
        out.code   = EmitTransferCode::None;
        return out;
    }

}  // namespace

TransferStatus
build_prepared_c2pa_sign_request(
    const PreparedTransferBundle& bundle,
    PreparedTransferC2paSignRequest* out_request) noexcept
{
    if (!out_request) {
        return TransferStatus::InvalidArgument;
    }

    PreparedTransferC2paSignRequest out;
    const PreparedTransferC2paRewriteRequirements& rewrite = bundle.c2pa_rewrite;
    out.rewrite_state              = rewrite.state;
    out.target_format              = rewrite.target_format;
    out.source_kind                = rewrite.source_kind;
    out.existing_carrier_segments  = rewrite.existing_carrier_segments;
    out.content_binding_bytes      = rewrite.content_binding_bytes;
    out.content_binding_chunks     = rewrite.content_binding_chunks;
    out.requires_manifest_builder  = rewrite.requires_manifest_builder;
    out.requires_content_binding   = rewrite.requires_content_binding;
    out.requires_certificate_chain = rewrite.requires_certificate_chain;
    out.requires_private_key       = rewrite.requires_private_key;
    out.requires_signing_time      = rewrite.requires_signing_time;
    out.carrier_route              = "jpeg:app11-c2pa";
    out.manifest_label             = "c2pa";
    out.message                    = rewrite.message;

    for (size_t i = 0; i < out.content_binding_chunks.size(); ++i) {
        const PreparedTransferC2paRewriteChunk& chunk
            = out.content_binding_chunks[i];
        if (chunk.kind == TransferC2paRewriteChunkKind::SourceRange) {
            out.source_range_chunks += 1U;
        } else if (chunk.kind
                   == TransferC2paRewriteChunkKind::PreparedJpegSegment) {
            out.prepared_segment_chunks += 1U;
        }
    }

    if (rewrite.state == TransferC2paRewriteState::NotApplicable) {
        out.status = TransferStatus::Unsupported;
        if (out.message.empty()) {
            out.message = "c2pa rewrite is not applicable for current bundle";
        }
    } else if (rewrite.state == TransferC2paRewriteState::NotRequested) {
        out.status = TransferStatus::Unsupported;
        if (out.message.empty()) {
            out.message = "c2pa signed rewrite was not requested";
        }
    } else if (bundle.target_format != TransferTargetFormat::Jpeg
               || rewrite.target_format != TransferTargetFormat::Jpeg) {
        out.status = TransferStatus::Unsupported;
        out.carrier_route.clear();
        out.manifest_label.clear();
        if (out.message.empty()) {
            out.message
                = "c2pa sign request currently supports jpeg targets only";
        }
    } else if (!rewrite.target_carrier_available) {
        out.status = TransferStatus::Unsupported;
        if (out.message.empty()) {
            out.message
                = "c2pa sign request requires an available target carrier";
        }
    } else if (rewrite.content_binding_chunks.empty()
               || rewrite.content_binding_bytes == 0U) {
        out.status = TransferStatus::Unsupported;
        if (out.message.empty()) {
            out.message = "c2pa sign request is missing content-binding chunks";
        }
    } else {
        out.status = TransferStatus::Ok;
        if (out.message.empty()) {
            out.message
                = "c2pa sign request is ready for external signer input assembly";
        }
    }

    *out_request = std::move(out);
    return out_request->status;
}

static bool
c2pa_rewrite_chunks_match(
    std::span<const PreparedTransferC2paRewriteChunk> a,
    std::span<const PreparedTransferC2paRewriteChunk> b) noexcept
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].kind != b[i].kind || a[i].source_offset != b[i].source_offset
            || a[i].size != b[i].size || a[i].block_index != b[i].block_index
            || a[i].jpeg_marker_code != b[i].jpeg_marker_code) {
            return false;
        }
    }
    return true;
}

static TransferC2paSignedPayloadKind
transfer_c2pa_signed_payload_kind(C2paPayloadClass payload_class) noexcept
{
    if (payload_class == C2paPayloadClass::ContentBound) {
        return TransferC2paSignedPayloadKind::ContentBound;
    }
    if (payload_class == C2paPayloadClass::DraftUnsignedInvalidation) {
        return TransferC2paSignedPayloadKind::DraftUnsignedInvalidation;
    }
    return TransferC2paSignedPayloadKind::GenericJumbf;
}

TransferStatus
build_prepared_c2pa_handoff_package(
    const PreparedTransferBundle& bundle,
    std::span<const std::byte> target_input,
    PreparedTransferC2paHandoffPackage* out_package) noexcept
{
    if (!out_package) {
        return TransferStatus::InvalidArgument;
    }

    PreparedTransferC2paHandoffPackage out;
    out.request.status = build_prepared_c2pa_sign_request(bundle, &out.request);
    if (out.request.status != TransferStatus::Ok) {
        out.binding.status  = out.request.status;
        out.binding.code    = EmitTransferCode::InvalidArgument;
        out.binding.errors  = 1U;
        out.binding.message = out.request.message.empty()
                                  ? "failed to build c2pa handoff request"
                                  : out.request.message;
        *out_package        = std::move(out);
        return out_package->binding.status;
    }

    out.binding = build_prepared_c2pa_sign_request_binding(bundle, target_input,
                                                           out.request,
                                                           &out.binding_bytes);
    *out_package = std::move(out);
    return out_package->binding.status;
}

TransferStatus
build_prepared_c2pa_signed_package(
    const PreparedTransferBundle& bundle,
    const PreparedTransferC2paSignerInput& input,
    PreparedTransferC2paSignedPackage* out_package) noexcept
{
    if (!out_package) {
        return TransferStatus::InvalidArgument;
    }
    PreparedTransferC2paSignedPackage out;
    const TransferStatus status
        = build_prepared_c2pa_sign_request(bundle, &out.request);
    out.signer_input = input;
    *out_package     = std::move(out);
    return status;
}

BuildPreparedC2paBindingResult
build_prepared_c2pa_sign_request_binding(
    const PreparedTransferBundle& bundle,
    std::span<const std::byte> target_input,
    const PreparedTransferC2paSignRequest& request,
    std::vector<std::byte>* out_bytes) noexcept
{
    BuildPreparedC2paBindingResult out;
    if (!out_bytes) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_bytes is null";
        return out;
    }

    out_bytes->clear();
    if (request.status != TransferStatus::Ok) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "c2pa sign request is not ready";
        return out;
    }
    if (bundle.target_format != TransferTargetFormat::Jpeg
        || request.target_format != TransferTargetFormat::Jpeg) {
        out.status = TransferStatus::Unsupported;
        out.code   = EmitTransferCode::BundleTargetNotJpeg;
        out.errors = 1U;
        out.message
            = "c2pa binding materialization currently supports jpeg only";
        return out;
    }
    if (request.carrier_route != "jpeg:app11-c2pa"
        || request.manifest_label != "c2pa") {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::PlanMismatch;
        out.errors  = 1U;
        out.message = "c2pa sign request carrier contract mismatch";
        return out;
    }

    const PreparedTransferC2paRewriteRequirements& rewrite = bundle.c2pa_rewrite;
    if (request.rewrite_state != rewrite.state
        || request.source_kind != rewrite.source_kind
        || request.content_binding_bytes != rewrite.content_binding_bytes
        || !c2pa_rewrite_chunks_match(request.content_binding_chunks,
                                      rewrite.content_binding_chunks)) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::PlanMismatch;
        out.errors  = 1U;
        out.message = "c2pa sign request no longer matches prepared bundle";
        return out;
    }

    out_bytes->reserve(static_cast<size_t>(request.content_binding_bytes));
    uint64_t written = 0U;
    for (size_t i = 0; i < request.content_binding_chunks.size(); ++i) {
        const PreparedTransferC2paRewriteChunk& chunk
            = request.content_binding_chunks[i];
        if (chunk.kind == TransferC2paRewriteChunkKind::SourceRange) {
            if (chunk.source_offset > target_input.size()
                || chunk.size > target_input.size()
                || chunk.source_offset + chunk.size > target_input.size()) {
                out.status  = TransferStatus::Malformed;
                out.code    = EmitTransferCode::InvalidPayload;
                out.errors  = 1U;
                out.message = "c2pa sign request source range is out of bounds";
                out_bytes->clear();
                return out;
            }
            const size_t begin = static_cast<size_t>(chunk.source_offset);
            const std::span<const std::byte> range
                = target_input.subspan(begin, static_cast<size_t>(chunk.size));
            out_bytes->insert(out_bytes->end(), range.begin(), range.end());
            written += chunk.size;
            continue;
        }

        if (chunk.kind != TransferC2paRewriteChunkKind::PreparedJpegSegment) {
            out.status  = TransferStatus::Malformed;
            out.code    = EmitTransferCode::InvalidPayload;
            out.errors  = 1U;
            out.message = "c2pa sign request chunk kind is invalid";
            out_bytes->clear();
            return out;
        }
        if (chunk.block_index >= bundle.blocks.size()) {
            out.status = TransferStatus::Malformed;
            out.code   = EmitTransferCode::InvalidPayload;
            out.errors = 1U;
            out.message
                = "c2pa sign request prepared segment index is out of range";
            out_bytes->clear();
            return out;
        }

        const PreparedTransferBlock& block = bundle.blocks[chunk.block_index];
        const uint64_t expected_size       = static_cast<uint64_t>(
            4U + block.payload.size());
        if (expected_size != chunk.size) {
            out.status = TransferStatus::InvalidArgument;
            out.code   = EmitTransferCode::PlanMismatch;
            out.errors = 1U;
            out.message
                = "c2pa sign request prepared segment size no longer matches";
            out_bytes->clear();
            return out;
        }

        const size_t before = out_bytes->size();
        append_jpeg_segment(out_bytes, chunk.jpeg_marker_code,
                            std::span<const std::byte>(block.payload.data(),
                                                       block.payload.size()));
        const uint64_t actual_size = static_cast<uint64_t>(out_bytes->size()
                                                           - before);
        if (actual_size != expected_size) {
            out.status = TransferStatus::Malformed;
            out.code   = EmitTransferCode::InvalidPayload;
            out.errors = 1U;
            out.message
                = "c2pa sign request prepared segment emitted invalid size";
            out_bytes->clear();
            return out;
        }
        written += actual_size;
    }

    if (written != request.content_binding_bytes) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::PlanMismatch;
        out.errors  = 1U;
        out.message = "c2pa sign request emitted size no longer matches bundle";
        out_bytes->clear();
        return out;
    }

    out.status  = TransferStatus::Ok;
    out.code    = EmitTransferCode::None;
    out.written = written;
    out.message = "c2pa binding bytes materialized from prepared rewrite chunks";
    return out;
}

PreparedTransferC2paPackageIoResult
serialize_prepared_c2pa_handoff_package(
    const PreparedTransferC2paHandoffPackage& package,
    std::vector<std::byte>* out_bytes) noexcept
{
    PreparedTransferC2paPackageIoResult out;
    if (!out_bytes) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_bytes is null";
        return out;
    }
    out_bytes->clear();
    out_bytes->reserve(64U + package.binding_bytes.size()
                       + package.request.carrier_route.size()
                       + package.request.manifest_label.size()
                       + package.request.message.size()
                       + package.binding.message.size());
    append_ascii_bytes(out_bytes, std::string_view(kC2paHandoffMagic, 8U));
    append_u32le(out_bytes, kC2paPackageVersion);
    append_c2pa_sign_request_le(out_bytes, package.request);
    append_c2pa_binding_result_le(out_bytes, package.binding);
    append_blob_le(out_bytes,
                   std::span<const std::byte>(package.binding_bytes.data(),
                                              package.binding_bytes.size()));
    out.status  = TransferStatus::Ok;
    out.code    = EmitTransferCode::None;
    out.bytes   = static_cast<uint64_t>(out_bytes->size());
    out.message = "c2pa handoff package serialized";
    return out;
}

PreparedTransferC2paPackageIoResult
deserialize_prepared_c2pa_handoff_package(
    std::span<const std::byte> bytes,
    PreparedTransferC2paHandoffPackage* out_package) noexcept
{
    PreparedTransferC2paPackageIoResult out;
    if (!out_package) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_package is null";
        return out;
    }
    size_t off = 0U;
    out = deserialize_c2pa_package_header(bytes, kC2paHandoffMagic, &off);
    if (out.status != TransferStatus::Ok) {
        return out;
    }
    PreparedTransferC2paHandoffPackage package;
    if (!parse_c2pa_sign_request_le(bytes, &off, &package.request)
        || !parse_c2pa_binding_result_le(bytes, &off, &package.binding)
        || !read_blob_le(bytes, &off, &package.binding_bytes)
        || off != bytes.size()) {
        out.status  = TransferStatus::Malformed;
        out.code    = EmitTransferCode::InvalidPayload;
        out.errors  = 1U;
        out.message = "c2pa handoff package is malformed";
        return out;
    }
    *out_package = std::move(package);
    out.status   = TransferStatus::Ok;
    out.code     = EmitTransferCode::None;
    out.bytes    = static_cast<uint64_t>(bytes.size());
    out.message  = "c2pa handoff package parsed";
    return out;
}

PreparedTransferC2paPackageIoResult
serialize_prepared_c2pa_signed_package(
    const PreparedTransferC2paSignedPackage& package,
    std::vector<std::byte>* out_bytes) noexcept
{
    PreparedTransferC2paPackageIoResult out;
    if (!out_bytes) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_bytes is null";
        return out;
    }
    out_bytes->clear();
    out_bytes->reserve(
        64U + package.request.carrier_route.size()
        + package.request.manifest_label.size() + package.request.message.size()
        + package.signer_input.signing_time.size()
        + package.signer_input.private_key_reference.size()
        + package.signer_input.certificate_chain_bytes.size()
        + package.signer_input.manifest_builder_output.size()
        + package.signer_input.signed_c2pa_logical_payload.size());
    append_ascii_bytes(out_bytes, std::string_view(kC2paSignedMagic, 8U));
    append_u32le(out_bytes, kC2paPackageVersion);
    append_c2pa_sign_request_le(out_bytes, package.request);
    append_c2pa_signer_input_le(out_bytes, package.signer_input);
    out.status  = TransferStatus::Ok;
    out.code    = EmitTransferCode::None;
    out.bytes   = static_cast<uint64_t>(out_bytes->size());
    out.message = "signed c2pa package serialized";
    return out;
}

PreparedTransferC2paPackageIoResult
deserialize_prepared_c2pa_signed_package(
    std::span<const std::byte> bytes,
    PreparedTransferC2paSignedPackage* out_package) noexcept
{
    PreparedTransferC2paPackageIoResult out;
    if (!out_package) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_package is null";
        return out;
    }
    size_t off = 0U;
    out        = deserialize_c2pa_package_header(bytes, kC2paSignedMagic, &off);
    if (out.status != TransferStatus::Ok) {
        return out;
    }
    PreparedTransferC2paSignedPackage package;
    if (!parse_c2pa_sign_request_le(bytes, &off, &package.request)
        || !parse_c2pa_signer_input_le(bytes, &off, &package.signer_input)
        || off != bytes.size()) {
        out.status  = TransferStatus::Malformed;
        out.code    = EmitTransferCode::InvalidPayload;
        out.errors  = 1U;
        out.message = "signed c2pa package is malformed";
        return out;
    }
    *out_package = std::move(package);
    out.status   = TransferStatus::Ok;
    out.code     = EmitTransferCode::None;
    out.bytes    = static_cast<uint64_t>(bytes.size());
    out.message  = "signed c2pa package parsed";
    return out;
}

PreparedTransferPackageIoResult
serialize_prepared_transfer_package_batch(
    const PreparedTransferPackageBatch& batch,
    std::vector<std::byte>* out_bytes) noexcept
{
    PreparedTransferPackageIoResult out;
    if (!out_bytes) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_bytes is null";
        return out;
    }

    const EmitTransferResult validated
        = validate_prepared_transfer_package_batch(batch);
    if (validated.status != TransferStatus::Ok) {
        out.status  = validated.status;
        out.code    = validated.code;
        out.errors  = validated.errors;
        out.message = validated.message;
        return out;
    }

    out_bytes->clear();
    out_bytes->reserve(40U + batch.chunks.size() * 30U
                       + static_cast<size_t>(batch.output_size));
    append_ascii_bytes(out_bytes,
                       std::string_view(kTransferPackageBatchMagic, 8U));
    append_u32le(out_bytes, kTransferPackageBatchVersion);
    append_u32le(out_bytes, batch.contract_version);
    append_u8(out_bytes, static_cast<uint8_t>(batch.target_format));
    append_u64le(out_bytes, batch.input_size);
    append_u64le(out_bytes, batch.output_size);
    append_u32le(out_bytes, static_cast<uint32_t>(batch.chunks.size()));
    for (size_t i = 0; i < batch.chunks.size(); ++i) {
        const PreparedTransferPackageBlob& chunk = batch.chunks[i];
        append_u8(out_bytes, static_cast<uint8_t>(chunk.kind));
        append_u64le(out_bytes, chunk.output_offset);
        append_u64le(out_bytes, chunk.source_offset);
        append_u32le(out_bytes, chunk.block_index);
        append_u8(out_bytes, chunk.jpeg_marker_code);
        append_string_le(out_bytes, chunk.route);
        append_blob_le(out_bytes,
                       std::span<const std::byte>(chunk.bytes.data(),
                                                  chunk.bytes.size()));
    }
    out.status  = TransferStatus::Ok;
    out.code    = EmitTransferCode::None;
    out.bytes   = static_cast<uint64_t>(out_bytes->size());
    out.message = "prepared transfer package batch serialized";
    return out;
}

PreparedTransferPackageIoResult
deserialize_prepared_transfer_package_batch(
    std::span<const std::byte> bytes,
    PreparedTransferPackageBatch* out_batch) noexcept
{
    PreparedTransferPackageIoResult out;
    if (!out_batch) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_batch is null";
        return out;
    }

    size_t off = 0U;
    out        = deserialize_transfer_package_batch_header(bytes, &off);
    if (out.status != TransferStatus::Ok) {
        return out;
    }

    PreparedTransferPackageBatch batch;
    uint32_t contract_version = 0U;
    uint8_t target_format     = 0U;
    uint32_t count            = 0U;
    if (!read_u32le(bytes, &off, &contract_version)
        || !read_u8(bytes, &off, &target_format)
        || !read_u64le(bytes, &off, &batch.input_size)
        || !read_u64le(bytes, &off, &batch.output_size)
        || !read_u32le(bytes, &off, &count)) {
        out.status  = TransferStatus::Malformed;
        out.code    = EmitTransferCode::InvalidPayload;
        out.errors  = 1U;
        out.message = "prepared transfer package batch header is malformed";
        return out;
    }

    batch.contract_version = contract_version;
    batch.target_format    = static_cast<TransferTargetFormat>(target_format);
    batch.chunks.reserve(count);

    const size_t min_chunk_size = 1U + 8U + 8U + 4U + 1U + 8U + 8U;
    if (count > 0U && (bytes.size() - off) / min_chunk_size < count) {
        out.status  = TransferStatus::Malformed;
        out.code    = EmitTransferCode::InvalidPayload;
        out.errors  = 1U;
        out.message = "prepared transfer package batch chunk list is truncated";
        return out;
    }

    for (uint32_t i = 0U; i < count; ++i) {
        PreparedTransferPackageBlob chunk;
        uint8_t kind = 0U;
        if (!read_u8(bytes, &off, &kind)
            || !read_u64le(bytes, &off, &chunk.output_offset)
            || !read_u64le(bytes, &off, &chunk.source_offset)
            || !read_u32le(bytes, &off, &chunk.block_index)
            || !read_u8(bytes, &off, &chunk.jpeg_marker_code)
            || !read_string_le(bytes, &off, &chunk.route)
            || !read_blob_le(bytes, &off, &chunk.bytes)) {
            out.status  = TransferStatus::Malformed;
            out.code    = EmitTransferCode::InvalidPayload;
            out.errors  = 1U;
            out.message = "prepared transfer package batch chunk is malformed";
            return out;
        }
        chunk.kind = static_cast<TransferPackageChunkKind>(kind);
        batch.chunks.push_back(std::move(chunk));
    }
    if (off != bytes.size()) {
        out.status  = TransferStatus::Malformed;
        out.code    = EmitTransferCode::InvalidPayload;
        out.errors  = 1U;
        out.message = "prepared transfer package batch has trailing bytes";
        return out;
    }

    const EmitTransferResult validated
        = validate_prepared_transfer_package_batch(batch);
    if (validated.status != TransferStatus::Ok) {
        out.status  = validated.status;
        out.code    = validated.code;
        out.errors  = validated.errors;
        out.message = validated.message;
        return out;
    }

    *out_batch  = std::move(batch);
    out.status  = TransferStatus::Ok;
    out.code    = EmitTransferCode::None;
    out.bytes   = static_cast<uint64_t>(bytes.size());
    out.message = "prepared transfer package batch parsed";
    return out;
}

namespace {

    static bool validate_staged_jpeg_c2pa_blocks(
        std::span<const PreparedTransferBlock> blocks,
        std::span<const std::byte> logical_payload, uint32_t header_len,
        ValidatePreparedC2paSignResult* out) noexcept
    {
        if (!out || blocks.empty() || header_len == 0U
            || logical_payload.size() < header_len) {
            return false;
        }

        std::vector<std::byte> reconstructed;
        reconstructed.reserve(logical_payload.size());
        uint32_t expected_seq = 1U;
        for (size_t i = 0; i < blocks.size(); ++i) {
            const PreparedTransferBlock& block = blocks[i];
            if (block.kind != TransferBlockKind::C2pa
                || block.route != "jpeg:app11-c2pa"
                || block.payload.size() < static_cast<size_t>(8U + header_len)
                || block.payload[0] != std::byte { 'J' }
                || block.payload[1] != std::byte { 'P' }
                || block.payload[2] != std::byte { 0x00 }
                || block.payload[3] != std::byte { 0x00 }) {
                out->status = TransferStatus::Malformed;
                out->code   = EmitTransferCode::InvalidPayload;
                out->errors = 1U;
                out->message
                    = "validated signed c2pa carrier block is malformed";
                return false;
            }

            uint32_t seq = 0U;
            if (!read_u32be(block.payload, 4U, &seq) || seq != expected_seq) {
                out->status = TransferStatus::Malformed;
                out->code   = EmitTransferCode::InvalidPayload;
                out->errors = 1U;
                out->message
                    = "validated signed c2pa carrier sequence is invalid";
                return false;
            }
            expected_seq += 1U;

            const std::span<const std::byte> payload_span(block.payload.data(),
                                                          block.payload.size());
            const std::span<const std::byte> header
                = payload_span.subspan(8U, header_len);
            if (!std::equal(header.begin(), header.end(),
                            logical_payload.begin(),
                            logical_payload.begin()
                                + static_cast<std::ptrdiff_t>(header_len))) {
                out->status = TransferStatus::Malformed;
                out->code   = EmitTransferCode::InvalidPayload;
                out->errors = 1U;
                out->message
                    = "validated signed c2pa carrier header does not match logical payload";
                return false;
            }
            if (i == 0U) {
                reconstructed.insert(reconstructed.end(), header.begin(),
                                     header.end());
            }
            const std::span<const std::byte> body = payload_span.subspan(
                8U + header_len);
            reconstructed.insert(reconstructed.end(), body.begin(), body.end());
            out->staged_payload_bytes += static_cast<uint64_t>(
                block.payload.size());
        }

        if (reconstructed.size() != logical_payload.size()
            || !std::equal(reconstructed.begin(), reconstructed.end(),
                           logical_payload.begin(), logical_payload.end())) {
            out->status = TransferStatus::Malformed;
            out->code   = EmitTransferCode::InvalidPayload;
            out->errors = 1U;
            out->message
                = "validated signed c2pa carrier does not reconstruct the logical payload";
            return false;
        }

        out->staged_segments = static_cast<uint32_t>(blocks.size());
        return true;
    }

    static ValidatePreparedC2paSignResult validate_prepared_c2pa_sign_stage(
        const PreparedTransferBundle& bundle,
        const PreparedTransferC2paSignRequest& request,
        const PreparedTransferC2paSignerInput& input,
        std::vector<PreparedTransferBlock>* out_staged_blocks) noexcept
    {
        ValidatePreparedC2paSignResult out;
        out.logical_payload_bytes = static_cast<uint64_t>(
            input.signed_c2pa_logical_payload.size());
        if (out_staged_blocks) {
            out_staged_blocks->clear();
        }
        if (bundle.target_format != TransferTargetFormat::Jpeg
            || request.target_format != TransferTargetFormat::Jpeg) {
            out.status = TransferStatus::Unsupported;
            out.code   = EmitTransferCode::BundleTargetNotJpeg;
            out.errors = 1U;
            out.message
                = "c2pa sign result staging currently supports jpeg only";
            return out;
        }
        if (request.status != TransferStatus::Ok) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::InvalidArgument;
            out.errors  = 1U;
            out.message = "c2pa sign request is not ready";
            return out;
        }
        if (request.carrier_route != "jpeg:app11-c2pa"
            || request.manifest_label != "c2pa") {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "c2pa sign request carrier contract mismatch";
            return out;
        }
        if (request.rewrite_state
                != TransferC2paRewriteState::SigningMaterialRequired
            && request.rewrite_state != TransferC2paRewriteState::Ready) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "c2pa sign request rewrite state is not signable";
            return out;
        }

        const PreparedTransferC2paRewriteRequirements& rewrite
            = bundle.c2pa_rewrite;
        if (rewrite.state != TransferC2paRewriteState::SigningMaterialRequired
            && rewrite.state != TransferC2paRewriteState::Ready) {
            out.status = TransferStatus::InvalidArgument;
            out.code   = EmitTransferCode::PlanMismatch;
            out.errors = 1U;
            out.message
                = "bundle c2pa rewrite state is not ready for signer payloads";
            return out;
        }
        if (rewrite.target_format != TransferTargetFormat::Jpeg
            || !rewrite.target_carrier_available) {
            out.status  = TransferStatus::Unsupported;
            out.code    = EmitTransferCode::BundleTargetNotJpeg;
            out.errors  = 1U;
            out.message = "bundle c2pa rewrite carrier is unavailable";
            return out;
        }
        if (request.source_kind != rewrite.source_kind
            || request.content_binding_bytes != rewrite.content_binding_bytes
            || !c2pa_rewrite_chunks_match(request.content_binding_chunks,
                                          rewrite.content_binding_chunks)) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "c2pa sign request no longer matches prepared bundle";
            return out;
        }
        if (request.requires_manifest_builder
            && input.manifest_builder_output.empty()) {
            out.status = TransferStatus::InvalidArgument;
            out.code   = EmitTransferCode::InvalidArgument;
            out.errors = 1U;
            out.message = "missing manifest_builder_output for c2pa sign result";
            return out;
        }
        if (request.requires_certificate_chain
            && input.certificate_chain_bytes.empty()) {
            out.status = TransferStatus::InvalidArgument;
            out.code   = EmitTransferCode::InvalidArgument;
            out.errors = 1U;
            out.message = "missing certificate_chain_bytes for c2pa sign result";
            return out;
        }
        if (request.requires_private_key
            && input.private_key_reference.empty()) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::InvalidArgument;
            out.errors  = 1U;
            out.message = "missing private_key_reference for c2pa sign result";
            return out;
        }
        if (request.requires_signing_time && input.signing_time.empty()) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::InvalidArgument;
            out.errors  = 1U;
            out.message = "missing signing_time for c2pa sign result";
            return out;
        }
        if (input.signed_c2pa_logical_payload.empty()) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::InvalidArgument;
            out.errors  = 1U;
            out.message = "signed_c2pa_logical_payload is empty";
            return out;
        }

        uint32_t header_len = 0U;
        uint32_t box_type   = 0U;
        if (!parse_bmff_box_header(input.signed_c2pa_logical_payload,
                                   &header_len, &box_type)) {
            out.status = TransferStatus::Malformed;
            out.code   = EmitTransferCode::InvalidPayload;
            out.errors = 1U;
            out.message
                = "signed c2pa logical payload does not start with a valid bmff box";
            return out;
        }
        static constexpr uint32_t kMaxJpegSegmentPayload = 65533U;
        if (8U + header_len > kMaxJpegSegmentPayload) {
            out.status = TransferStatus::LimitExceeded;
            out.code   = EmitTransferCode::InvalidPayload;
            out.errors = 1U;
            out.message
                = "signed c2pa app11 overhead exceeds jpeg segment limits";
            return out;
        }

        const C2paPayloadClass payload_class = classify_c2pa_jumbf_payload(
            input.signed_c2pa_logical_payload);
        out.payload_kind = transfer_c2pa_signed_payload_kind(payload_class);
        if (payload_class != C2paPayloadClass::ContentBound) {
            out.status = TransferStatus::Unsupported;
            out.code   = EmitTransferCode::ContentBoundPayloadUnsupported;
            out.errors = 1U;
            out.message
                = "signed rewrite requires a content-bound c2pa logical payload";
            return out;
        }

        if (!validate_transfer_c2pa_semantics(input.signed_c2pa_logical_payload,
                                              request, &out)) {
            return out;
        }
        if (request.requires_manifest_builder) {
            std::span<const std::byte> manifest_builder_payload;
            if (!extract_first_cbor_box_payload(
                    input.signed_c2pa_logical_payload,
                    &manifest_builder_payload)) {
                (void)fail_transfer_c2pa_semantic_validation(
                    &out, "manifest_builder_payload_missing",
                    "signed c2pa payload is missing the primary cbor manifest payload",
                    TransferStatus::Malformed);
                return out;
            }
            const std::span<const std::byte> builder_output(
                input.manifest_builder_output.data(),
                input.manifest_builder_output.size());
            if (builder_output.size() != manifest_builder_payload.size()
                || !std::equal(builder_output.begin(), builder_output.end(),
                               manifest_builder_payload.begin(),
                               manifest_builder_payload.end())) {
                (void)fail_transfer_c2pa_semantic_validation(
                    &out, "manifest_builder_output_mismatch",
                    "signed c2pa payload does not match manifest_builder_output",
                    TransferStatus::Malformed);
                return out;
            }
        }

        std::vector<PreparedTransferBlock> staged_blocks;
        uint32_t order = next_prepared_block_order(bundle, 150U);
        std::string error;
        if (!append_jpeg_app11_jumbf_segments(input.signed_c2pa_logical_payload,
                                              TransferBlockKind::C2pa, &order,
                                              &staged_blocks, &error)) {
            out.status = TransferStatus::Malformed;
            out.code   = EmitTransferCode::InvalidPayload;
            out.errors = 1U;
            out.message
                = error.empty()
                      ? "failed to stage signed c2pa payload for jpeg app11"
                      : error;
            return out;
        }

        if (!validate_staged_jpeg_c2pa_blocks(
                std::span<const PreparedTransferBlock>(staged_blocks.data(),
                                                       staged_blocks.size()),
                input.signed_c2pa_logical_payload, header_len, &out)) {
            return out;
        }

        out.status  = TransferStatus::Ok;
        out.code    = EmitTransferCode::None;
        out.message = "signed c2pa payload validated for jpeg app11 staging";
        if (out_staged_blocks) {
            *out_staged_blocks = std::move(staged_blocks);
        }
        return out;
    }

}  // namespace

ValidatePreparedC2paSignResult
validate_prepared_c2pa_sign_result(
    const PreparedTransferBundle& bundle,
    const PreparedTransferC2paSignRequest& request,
    const PreparedTransferC2paSignerInput& input) noexcept
{
    return validate_prepared_c2pa_sign_stage(bundle, request, input, nullptr);
}

ValidatePreparedC2paSignResult
validate_prepared_c2pa_signed_package(
    const PreparedTransferBundle& bundle,
    const PreparedTransferC2paSignedPackage& package) noexcept
{
    return validate_prepared_c2pa_sign_result(bundle, package.request,
                                              package.signer_input);
}

EmitTransferResult
apply_prepared_c2pa_sign_result(
    PreparedTransferBundle* bundle,
    const PreparedTransferC2paSignRequest& request,
    const PreparedTransferC2paSignerInput& input) noexcept
{
    EmitTransferResult out;
    if (!bundle) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "bundle is null";
        return out;
    }
    std::vector<PreparedTransferBlock> staged_blocks;
    const ValidatePreparedC2paSignResult validation
        = validate_prepared_c2pa_sign_stage(*bundle, request, input,
                                            &staged_blocks);
    if (validation.status != TransferStatus::Ok) {
        out.status  = validation.status;
        out.code    = validation.code;
        out.errors  = validation.errors;
        out.message = validation.message;
        return out;
    }
    const PreparedTransferC2paRewriteRequirements& rewrite
        = bundle->c2pa_rewrite;

    out.skipped = remove_prepared_blocks_by_route(bundle, "jpeg:app11-c2pa");
    for (size_t i = 0; i < staged_blocks.size(); ++i) {
        bundle->blocks.push_back(std::move(staged_blocks[i]));
    }

    bundle->profile.c2pa       = TransferPolicyAction::Rewrite;
    bundle->c2pa_rewrite.state = TransferC2paRewriteState::Ready;
    bundle->c2pa_rewrite.message
        = "external signed c2pa payload is staged for jpeg app11 emission";

    PreparedTransferPolicyDecision* decision
        = find_policy_decision(bundle, TransferPolicySubject::C2pa);
    if (!decision) {
        append_policy_decision(
            bundle, TransferPolicySubject::C2pa, TransferPolicyAction::Rewrite,
            TransferPolicyAction::Keep,
            TransferPolicyReason::ExternalSignedPayload,
            rewrite.matched_entries == 0U ? 1U : rewrite.matched_entries,
            "external signed c2pa payload will be emitted as jpeg app11 segments",
            TransferC2paMode::SignedRewrite, rewrite.source_kind,
            TransferC2paPreparedOutput::SignedRewrite);
    } else {
        decision->requested = TransferPolicyAction::Rewrite;
        decision->effective = TransferPolicyAction::Keep;
        decision->reason    = TransferPolicyReason::ExternalSignedPayload;
        decision->c2pa_mode = TransferC2paMode::SignedRewrite;
        decision->c2pa_source_kind = rewrite.source_kind;
        decision->c2pa_prepared_output
            = TransferC2paPreparedOutput::SignedRewrite;
        if (decision->matched_entries == 0U) {
            decision->matched_entries = rewrite.matched_entries == 0U
                                            ? 1U
                                            : rewrite.matched_entries;
        }
        decision->message
            = "external signed c2pa payload will be emitted as jpeg app11 segments";
    }

    out.status  = TransferStatus::Ok;
    out.code    = EmitTransferCode::None;
    out.emitted = static_cast<uint32_t>(staged_blocks.size());
    return out;
}

EmitTransferResult
apply_prepared_c2pa_signed_package(
    PreparedTransferBundle* bundle,
    const PreparedTransferC2paSignedPackage& package) noexcept
{
    return apply_prepared_c2pa_sign_result(bundle, package.request,
                                           package.signer_input);
}

namespace {

    static void apply_one_time_patch(PreparedTransferBundle* bundle,
                                     TimePatchField field,
                                     std::span<const std::byte> value,
                                     const ApplyTimePatchOptions& options,
                                     ApplyTimePatchResult* out) noexcept
    {
        if (!bundle || !out) {
            return;
        }

        bool matched_any = false;
        for (size_t si = 0; si < bundle->time_patch_map.size(); ++si) {
            const TimePatchSlot& slot = bundle->time_patch_map[si];
            if (slot.field != field) {
                continue;
            }
            matched_any = true;

            if (slot.block_index >= bundle->blocks.size()) {
                out->errors += 1U;
                append_message(&out->message,
                               std::string(
                                   "time patch slot block out of range: ")
                                   + time_patch_field_name(slot.field));
                continue;
            }
            PreparedTransferBlock& block = bundle->blocks[slot.block_index];
            const uint32_t width         = static_cast<uint32_t>(slot.width);
            if (width == 0U) {
                out->errors += 1U;
                append_message(&out->message,
                               std::string("time patch slot width is zero: ")
                                   + time_patch_field_name(slot.field));
                continue;
            }
            if (slot.byte_offset + width > block.payload.size()) {
                out->errors += 1U;
                append_message(&out->message,
                               std::string(
                                   "time patch slot out of payload bounds: ")
                                   + time_patch_field_name(slot.field));
                continue;
            }
            if (options.strict_width && value.size() != width) {
                out->errors += 1U;
                append_message(&out->message,
                               std::string("time patch width mismatch: ")
                                   + time_patch_field_name(slot.field));
                continue;
            }

            const uint32_t copy_n = static_cast<uint32_t>(
                std::min(value.size(), static_cast<size_t>(width)));
            if (copy_n > 0U) {
                std::memcpy(block.payload.data() + slot.byte_offset,
                            value.data(), copy_n);
            }
            if (copy_n < width) {
                std::memset(block.payload.data() + slot.byte_offset + copy_n, 0,
                            width - copy_n);
            }
            out->patched_slots += 1U;
        }

        if (!matched_any) {
            if (options.require_slot) {
                out->errors += 1U;
                append_message(&out->message,
                               std::string("time patch slot not found: ")
                                   + time_patch_field_name(field));
            } else {
                out->skipped_slots += 1U;
            }
        }
    }

    static void finalize_time_patch_result(ApplyTimePatchResult* out) noexcept
    {
        if (!out) {
            return;
        }
        out->status = out->errors == 0U ? TransferStatus::Ok
                                        : TransferStatus::InvalidArgument;
    }

}  // namespace

ApplyTimePatchResult
apply_time_patches(PreparedTransferBundle* bundle,
                   std::span<const TimePatchUpdate> updates,
                   const ApplyTimePatchOptions& options) noexcept
{
    ApplyTimePatchResult out;
    if (!bundle) {
        out.status  = TransferStatus::InvalidArgument;
        out.errors  = 1U;
        out.message = "bundle is null";
        return out;
    }
    if (updates.empty()) {
        out.status  = TransferStatus::InvalidArgument;
        out.errors  = 1U;
        out.message = "updates are empty";
        return out;
    }

    for (size_t ui = 0; ui < updates.size(); ++ui) {
        apply_one_time_patch(bundle, updates[ui].field, updates[ui].value,
                             options, &out);
    }
    finalize_time_patch_result(&out);
    return out;
}

ApplyTimePatchResult
apply_time_patches_view(PreparedTransferBundle* bundle,
                        std::span<const TimePatchView> updates,
                        const ApplyTimePatchOptions& options) noexcept
{
    ApplyTimePatchResult out;
    if (!bundle) {
        out.status  = TransferStatus::InvalidArgument;
        out.errors  = 1U;
        out.message = "bundle is null";
        return out;
    }
    if (updates.empty()) {
        out.status  = TransferStatus::InvalidArgument;
        out.errors  = 1U;
        out.message = "updates are empty";
        return out;
    }

    for (size_t ui = 0; ui < updates.size(); ++ui) {
        apply_one_time_patch(bundle, updates[ui].field, updates[ui].value,
                             options, &out);
    }
    finalize_time_patch_result(&out);
    return out;
}

namespace {

    struct EmittedTiffTagSummaryLess final {
        bool operator()(const EmittedTiffTagSummary& a,
                        const EmittedTiffTagSummary& b) const noexcept
        {
            return a.tag < b.tag;
        }
    };

    struct EmittedJxlBoxSummaryLess final {
        bool operator()(const EmittedJxlBoxSummary& a,
                        const EmittedJxlBoxSummary& b) const noexcept
        {
            if (a.type[0] != b.type[0]) {
                return a.type[0] < b.type[0];
            }
            if (a.type[1] != b.type[1]) {
                return a.type[1] < b.type[1];
            }
            if (a.type[2] != b.type[2]) {
                return a.type[2] < b.type[2];
            }
            return a.type[3] < b.type[3];
        }
    };

    struct EmittedWebpChunkSummaryLess final {
        bool operator()(const EmittedWebpChunkSummary& a,
                        const EmittedWebpChunkSummary& b) const noexcept
        {
            if (a.type[0] != b.type[0]) {
                return a.type[0] < b.type[0];
            }
            if (a.type[1] != b.type[1]) {
                return a.type[1] < b.type[1];
            }
            if (a.type[2] != b.type[2]) {
                return a.type[2] < b.type[2];
            }
            return a.type[3] < b.type[3];
        }
    };

    struct EmittedBmffItemSummaryLess final {
        bool operator()(const EmittedBmffItemSummary& a,
                        const EmittedBmffItemSummary& b) const noexcept
        {
            if (a.item_type != b.item_type) {
                return a.item_type < b.item_type;
            }
            return static_cast<uint32_t>(a.mime_xmp)
                   < static_cast<uint32_t>(b.mime_xmp);
        }
    };

    class ExecuteRecordingJpegEmitter final : public JpegTransferEmitter {
    public:
        void reset() noexcept
        {
            marker_count_.fill(0U);
            marker_bytes_.fill(0U);
        }

        TransferStatus
        write_app_marker(uint8_t marker_code,
                         std::span<const std::byte> payload) noexcept override
        {
            marker_count_[marker_code] += 1U;
            marker_bytes_[marker_code] += static_cast<uint64_t>(payload.size());
            return TransferStatus::Ok;
        }

        void
        build_summary(std::vector<EmittedJpegMarkerSummary>* out) const noexcept
        {
            if (!out) {
                return;
            }
            out->clear();
            for (uint32_t i = 0; i < 256U; ++i) {
                if (marker_count_[i] == 0U) {
                    continue;
                }
                EmittedJpegMarkerSummary one;
                one.marker = static_cast<uint8_t>(i);
                one.count  = marker_count_[i];
                one.bytes  = marker_bytes_[i];
                out->push_back(one);
            }
        }

    private:
        std::array<uint32_t, 256> marker_count_ {};
        std::array<uint64_t, 256> marker_bytes_ {};
    };

    class ExecuteRecordingTiffEmitter final : public TiffTransferEmitter {
    public:
        void reset() noexcept
        {
            committed_ = false;
            tags_.clear();
        }

        bool committed() const noexcept { return committed_; }

        TransferStatus set_tag_u32(uint16_t tag,
                                   uint32_t /*value*/) noexcept override
        {
            add_tag(tag, 4U);
            return TransferStatus::Ok;
        }

        TransferStatus
        set_tag_bytes(uint16_t tag,
                      std::span<const std::byte> payload) noexcept override
        {
            add_tag(tag, static_cast<uint64_t>(payload.size()));
            return TransferStatus::Ok;
        }

        TransferStatus
        commit_exif_directory(uint64_t* out_ifd_offset) noexcept override
        {
            committed_ = true;
            if (out_ifd_offset) {
                *out_ifd_offset = 0U;
            }
            return TransferStatus::Ok;
        }

        void
        build_summary(std::vector<EmittedTiffTagSummary>* out) const noexcept
        {
            if (!out) {
                return;
            }
            *out = tags_;
            std::sort(out->begin(), out->end(), EmittedTiffTagSummaryLess {});
        }

    private:
        void add_tag(uint16_t tag, uint64_t bytes) noexcept
        {
            for (size_t i = 0; i < tags_.size(); ++i) {
                if (tags_[i].tag != tag) {
                    continue;
                }
                tags_[i].count += 1U;
                tags_[i].bytes += bytes;
                return;
            }

            EmittedTiffTagSummary one;
            one.tag   = tag;
            one.count = 1U;
            one.bytes = bytes;
            tags_.push_back(one);
        }

        bool committed_ = false;
        std::vector<EmittedTiffTagSummary> tags_;
    };

    class ExecuteRecordingJxlEmitter final : public JxlTransferEmitter {
    public:
        void reset() noexcept { boxes_.clear(); }

        TransferStatus set_icc_profile(
            std::span<const std::byte> /*payload*/) noexcept override
        {
            return TransferStatus::Ok;
        }

        TransferStatus add_box(std::array<char, 4> type,
                               std::span<const std::byte> payload,
                               bool /*compress*/) noexcept override
        {
            add_box_bytes(type, static_cast<uint64_t>(payload.size()));
            return TransferStatus::Ok;
        }

        TransferStatus close_boxes() noexcept override
        {
            return TransferStatus::Ok;
        }

        void build_summary(std::vector<EmittedJxlBoxSummary>* out) const noexcept
        {
            if (!out) {
                return;
            }
            *out = boxes_;
            std::sort(out->begin(), out->end(), EmittedJxlBoxSummaryLess {});
        }

    private:
        void add_box_bytes(std::array<char, 4> type, uint64_t bytes) noexcept
        {
            for (size_t i = 0; i < boxes_.size(); ++i) {
                if (boxes_[i].type != type) {
                    continue;
                }
                boxes_[i].count += 1U;
                boxes_[i].bytes += bytes;
                return;
            }

            EmittedJxlBoxSummary one;
            one.type  = type;
            one.count = 1U;
            one.bytes = bytes;
            boxes_.push_back(one);
        }

        std::vector<EmittedJxlBoxSummary> boxes_;
    };

    class ExecuteRecordingWebpEmitter final : public WebpTransferEmitter {
    public:
        void reset() noexcept { chunks_.clear(); }

        TransferStatus
        add_chunk(std::array<char, 4> type,
                  std::span<const std::byte> payload) noexcept override
        {
            add_chunk_bytes(type, static_cast<uint64_t>(payload.size()));
            return TransferStatus::Ok;
        }

        TransferStatus close_chunks() noexcept override
        {
            return TransferStatus::Ok;
        }

        void
        build_summary(std::vector<EmittedWebpChunkSummary>* out) const noexcept
        {
            if (!out) {
                return;
            }
            *out = chunks_;
            std::sort(out->begin(), out->end(), EmittedWebpChunkSummaryLess {});
        }

    private:
        void add_chunk_bytes(std::array<char, 4> type, uint64_t bytes) noexcept
        {
            for (size_t i = 0; i < chunks_.size(); ++i) {
                if (chunks_[i].type != type) {
                    continue;
                }
                chunks_[i].count += 1U;
                chunks_[i].bytes += bytes;
                return;
            }
            EmittedWebpChunkSummary one;
            one.type  = type;
            one.count = 1U;
            one.bytes = bytes;
            chunks_.push_back(one);
        }

        std::vector<EmittedWebpChunkSummary> chunks_;
    };

    class ExecuteRecordingBmffEmitter final : public BmffTransferEmitter {
    public:
        void reset() noexcept { items_.clear(); }

        TransferStatus
        add_item(uint32_t item_type,
                 std::span<const std::byte> payload) noexcept override
        {
            add_item_bytes(item_type, false,
                           static_cast<uint64_t>(payload.size()));
            return TransferStatus::Ok;
        }

        TransferStatus
        add_mime_xmp_item(std::span<const std::byte> payload) noexcept override
        {
            add_item_bytes(fourcc('m', 'i', 'm', 'e'), true,
                           static_cast<uint64_t>(payload.size()));
            return TransferStatus::Ok;
        }

        TransferStatus close_items() noexcept override
        {
            return TransferStatus::Ok;
        }

        void
        build_summary(std::vector<EmittedBmffItemSummary>* out) const noexcept
        {
            if (!out) {
                return;
            }
            *out = items_;
            std::sort(out->begin(), out->end(), EmittedBmffItemSummaryLess {});
        }

    private:
        void add_item_bytes(uint32_t item_type, bool mime_xmp,
                            uint64_t bytes) noexcept
        {
            for (size_t i = 0; i < items_.size(); ++i) {
                if (items_[i].item_type != item_type
                    || items_[i].mime_xmp != mime_xmp) {
                    continue;
                }
                items_[i].count += 1U;
                items_[i].bytes += bytes;
                return;
            }
            EmittedBmffItemSummary one;
            one.item_type = item_type;
            one.count     = 1U;
            one.bytes     = bytes;
            one.mime_xmp  = mime_xmp;
            items_.push_back(one);
        }

        std::vector<EmittedBmffItemSummary> items_;
    };

    class CountingTransferByteWriter final : public TransferByteWriter {
    public:
        explicit CountingTransferByteWriter(TransferByteWriter& inner) noexcept
            : inner_(inner)
        {
        }

        TransferStatus write(std::span<const std::byte> bytes) noexcept override
        {
            const TransferStatus st = inner_.write(bytes);
            if (st == TransferStatus::Ok) {
                bytes_written_ += static_cast<uint64_t>(bytes.size());
            }
            return st;
        }

        uint64_t bytes_written() const noexcept { return bytes_written_; }

    private:
        TransferByteWriter& inner_;
        uint64_t bytes_written_ = 0;
    };

    static void build_jpeg_emit_summary_from_plan(
        const PreparedTransferBundle& bundle, const PreparedJpegEmitPlan& plan,
        const EmitTransferOptions& options, uint32_t repeat,
        std::vector<EmittedJpegMarkerSummary>* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();

        std::array<uint32_t, 256> counts {};
        std::array<uint64_t, 256> bytes {};
        for (uint32_t rep = 0; rep < repeat; ++rep) {
            for (size_t i = 0; i < plan.ops.size(); ++i) {
                const PreparedJpegEmitOp& op = plan.ops[i];
                if (op.block_index >= bundle.blocks.size()) {
                    continue;
                }
                const PreparedTransferBlock& block
                    = bundle.blocks[op.block_index];
                if (options.skip_empty_payloads && block.payload.empty()) {
                    continue;
                }
                counts[op.marker_code] += 1U;
                bytes[op.marker_code] += static_cast<uint64_t>(
                    block.payload.size());
            }
        }
        for (uint32_t i = 0; i < 256U; ++i) {
            if (counts[i] == 0U) {
                continue;
            }
            EmittedJpegMarkerSummary one;
            one.marker = static_cast<uint8_t>(i);
            one.count  = counts[i];
            one.bytes  = bytes[i];
            out->push_back(one);
        }
    }

    static bool measure_jpeg_emit_bytes_from_plan(
        const PreparedTransferBundle& bundle, const PreparedJpegEmitPlan& plan,
        const EmitTransferOptions& options, uint32_t repeat,
        uint64_t* out_bytes) noexcept
    {
        if (!out_bytes) {
            return false;
        }
        uint64_t per_emit = 0U;
        for (size_t i = 0; i < plan.ops.size(); ++i) {
            const PreparedJpegEmitOp& op = plan.ops[i];
            if (op.block_index >= bundle.blocks.size()) {
                return false;
            }
            const PreparedTransferBlock& block = bundle.blocks[op.block_index];
            if (options.skip_empty_payloads && block.payload.empty()) {
                continue;
            }
            const uint64_t part = 4U
                                  + static_cast<uint64_t>(block.payload.size());
            if (UINT64_MAX - per_emit < part) {
                return false;
            }
            per_emit += part;
        }
        if (repeat != 0U && per_emit > (UINT64_MAX / repeat)) {
            return false;
        }
        *out_bytes = per_emit * repeat;
        return true;
    }

    static void build_tiff_emit_summary_from_plan(
        const PreparedTransferBundle& bundle, const PreparedTiffEmitPlan& plan,
        const EmitTransferOptions& options, uint32_t repeat,
        std::vector<EmittedTiffTagSummary>* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        for (uint32_t rep = 0; rep < repeat; ++rep) {
            for (size_t i = 0; i < plan.ops.size(); ++i) {
                const PreparedTiffEmitOp& op = plan.ops[i];
                if (op.block_index >= bundle.blocks.size()) {
                    continue;
                }
                const PreparedTransferBlock& block
                    = bundle.blocks[op.block_index];
                if (options.skip_empty_payloads && block.payload.empty()) {
                    continue;
                }
                bool merged = false;
                for (size_t j = 0; j < out->size(); ++j) {
                    if ((*out)[j].tag != op.tiff_tag) {
                        continue;
                    }
                    (*out)[j].count += 1U;
                    (*out)[j].bytes += static_cast<uint64_t>(
                        block.payload.size());
                    merged = true;
                    break;
                }
                if (merged) {
                    continue;
                }
                EmittedTiffTagSummary one;
                one.tag   = op.tiff_tag;
                one.count = 1U;
                one.bytes = static_cast<uint64_t>(block.payload.size());
                out->push_back(one);
            }
        }
        std::sort(out->begin(), out->end(), EmittedTiffTagSummaryLess {});
    }

    static void build_jxl_emit_summary_from_plan(
        const PreparedTransferBundle& bundle, const PreparedJxlEmitPlan& plan,
        const EmitTransferOptions& options, uint32_t repeat,
        std::vector<EmittedJxlBoxSummary>* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        for (uint32_t rep = 0; rep < repeat; ++rep) {
            for (size_t i = 0; i < plan.ops.size(); ++i) {
                const PreparedJxlEmitOp& op = plan.ops[i];
                if (op.block_index >= bundle.blocks.size()) {
                    continue;
                }
                const PreparedTransferBlock& block
                    = bundle.blocks[op.block_index];
                if (options.skip_empty_payloads && block.payload.empty()) {
                    continue;
                }
                if (op.kind != PreparedJxlEmitKind::Box) {
                    continue;
                }

                bool merged = false;
                for (size_t j = 0; j < out->size(); ++j) {
                    if ((*out)[j].type != op.box_type) {
                        continue;
                    }
                    (*out)[j].count += 1U;
                    (*out)[j].bytes += static_cast<uint64_t>(
                        block.payload.size());
                    merged = true;
                    break;
                }
                if (merged) {
                    continue;
                }

                EmittedJxlBoxSummary one;
                one.type  = op.box_type;
                one.count = 1U;
                one.bytes = static_cast<uint64_t>(block.payload.size());
                out->push_back(one);
            }
        }
        std::sort(out->begin(), out->end(), EmittedJxlBoxSummaryLess {});
    }

    static void build_webp_emit_summary_from_plan(
        const PreparedTransferBundle& bundle, const PreparedWebpEmitPlan& plan,
        const EmitTransferOptions& options, uint32_t repeat,
        std::vector<EmittedWebpChunkSummary>* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        for (uint32_t rep = 0; rep < repeat; ++rep) {
            for (size_t i = 0; i < plan.ops.size(); ++i) {
                const PreparedWebpEmitOp& op = plan.ops[i];
                if (op.block_index >= bundle.blocks.size()) {
                    continue;
                }
                const PreparedTransferBlock& block
                    = bundle.blocks[op.block_index];
                if (options.skip_empty_payloads && block.payload.empty()) {
                    continue;
                }
                bool merged = false;
                for (size_t j = 0; j < out->size(); ++j) {
                    if ((*out)[j].type != op.chunk_type) {
                        continue;
                    }
                    (*out)[j].count += 1U;
                    (*out)[j].bytes += static_cast<uint64_t>(
                        block.payload.size());
                    merged = true;
                    break;
                }
                if (merged) {
                    continue;
                }
                EmittedWebpChunkSummary one;
                one.type  = op.chunk_type;
                one.count = 1U;
                one.bytes = static_cast<uint64_t>(block.payload.size());
                out->push_back(one);
            }
        }
        std::sort(out->begin(), out->end(), EmittedWebpChunkSummaryLess {});
    }

    static void build_bmff_emit_summary_from_plan(
        const PreparedTransferBundle& bundle, const PreparedBmffEmitPlan& plan,
        const EmitTransferOptions& options, uint32_t repeat,
        std::vector<EmittedBmffItemSummary>* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        for (uint32_t rep = 0; rep < repeat; ++rep) {
            for (size_t i = 0; i < plan.ops.size(); ++i) {
                const PreparedBmffEmitOp& op = plan.ops[i];
                if (op.block_index >= bundle.blocks.size()) {
                    continue;
                }
                const PreparedTransferBlock& block
                    = bundle.blocks[op.block_index];
                if (options.skip_empty_payloads && block.payload.empty()) {
                    continue;
                }
                bool merged = false;
                for (size_t j = 0; j < out->size(); ++j) {
                    if ((*out)[j].item_type != op.item_type
                        || (*out)[j].mime_xmp
                               != (op.kind == PreparedBmffEmitKind::MimeXmp)) {
                        continue;
                    }
                    (*out)[j].count += 1U;
                    (*out)[j].bytes += static_cast<uint64_t>(
                        block.payload.size());
                    merged = true;
                    break;
                }
                if (merged) {
                    continue;
                }
                EmittedBmffItemSummary one;
                one.item_type = op.item_type;
                one.count     = 1U;
                one.bytes     = static_cast<uint64_t>(block.payload.size());
                one.mime_xmp  = op.kind == PreparedBmffEmitKind::MimeXmp;
                out->push_back(one);
            }
        }
        std::sort(out->begin(), out->end(), EmittedBmffItemSummaryLess {});
    }

    static uint64_t package_plan_next_output_offset(
        const PreparedTransferPackagePlan& plan) noexcept
    {
        if (plan.chunks.empty()) {
            return 0U;
        }
        const PreparedTransferPackageChunk& back = plan.chunks.back();
        return back.output_offset + back.size;
    }

    static void append_package_source_chunk(PreparedTransferPackagePlan* plan,
                                            uint64_t source_offset,
                                            uint64_t size) noexcept
    {
        if (!plan || size == 0U) {
            return;
        }
        PreparedTransferPackageChunk one;
        one.kind          = TransferPackageChunkKind::SourceRange;
        one.output_offset = package_plan_next_output_offset(*plan);
        one.source_offset = source_offset;
        one.size          = size;
        plan->chunks.push_back(std::move(one));
    }

    static void
    append_package_inline_chunk(PreparedTransferPackagePlan* plan,
                                std::span<const std::byte> bytes) noexcept
    {
        if (!plan || bytes.empty()) {
            return;
        }
        PreparedTransferPackageChunk one;
        one.kind          = TransferPackageChunkKind::InlineBytes;
        one.output_offset = package_plan_next_output_offset(*plan);
        one.size          = static_cast<uint64_t>(bytes.size());
        one.inline_bytes.assign(bytes.begin(), bytes.end());
        plan->chunks.push_back(std::move(one));
    }

    static void
    append_package_prepared_block_chunk(PreparedTransferPackagePlan* plan,
                                        uint32_t block_index,
                                        uint64_t size) noexcept
    {
        if (!plan) {
            return;
        }
        PreparedTransferPackageChunk one;
        one.kind          = TransferPackageChunkKind::PreparedTransferBlock;
        one.output_offset = package_plan_next_output_offset(*plan);
        one.block_index   = block_index;
        one.size          = size;
        plan->chunks.push_back(std::move(one));
    }

    static void
    append_package_jpeg_segment_chunk(PreparedTransferPackagePlan* plan,
                                      uint32_t block_index, uint8_t marker_code,
                                      size_t payload_size) noexcept
    {
        if (!plan) {
            return;
        }
        PreparedTransferPackageChunk one;
        one.kind             = TransferPackageChunkKind::PreparedJpegSegment;
        one.output_offset    = package_plan_next_output_offset(*plan);
        one.block_index      = block_index;
        one.jpeg_marker_code = marker_code;
        one.size             = 4U + static_cast<uint64_t>(payload_size);
        plan->chunks.push_back(std::move(one));
    }

    static EmitTransferResult validate_prepared_transfer_package_plan(
        std::span<const std::byte> input, const PreparedTransferBundle& bundle,
        const PreparedTransferPackagePlan& plan) noexcept
    {
        EmitTransferResult out;
        if (plan.contract_version != bundle.contract_version) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "prepared transfer package contract_version mismatch";
            return out;
        }
        if (plan.target_format != bundle.target_format) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "prepared transfer package target_format mismatch";
            return out;
        }
        if (plan.input_size != static_cast<uint64_t>(input.size())) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "prepared transfer package input_size mismatch";
            return out;
        }

        uint64_t expected_output = 0U;
        for (size_t i = 0; i < plan.chunks.size(); ++i) {
            const PreparedTransferPackageChunk& chunk = plan.chunks[i];
            if (chunk.output_offset != expected_output) {
                out.status = TransferStatus::InvalidArgument;
                out.code   = EmitTransferCode::PlanMismatch;
                out.errors = 1U;
                out.message
                    = "prepared transfer package output offsets are not contiguous";
                return out;
            }
            switch (chunk.kind) {
            case TransferPackageChunkKind::SourceRange:
                if (chunk.source_offset > static_cast<uint64_t>(input.size())
                    || chunk.size > static_cast<uint64_t>(input.size())
                                        - chunk.source_offset) {
                    out.status = TransferStatus::Malformed;
                    out.code   = EmitTransferCode::PlanMismatch;
                    out.errors = 1U;
                    out.message
                        = "prepared transfer package source range is out of bounds";
                    return out;
                }
                break;
            case TransferPackageChunkKind::PreparedTransferBlock:
                if (chunk.block_index >= bundle.blocks.size()) {
                    out.status = TransferStatus::InvalidArgument;
                    out.code   = EmitTransferCode::PlanMismatch;
                    out.errors = 1U;
                    out.message
                        = "prepared transfer block chunk block_index is out of range";
                    return out;
                }
                if (bundle.target_format == TransferTargetFormat::Jpeg) {
                    const PreparedTransferBlock& block
                        = bundle.blocks[chunk.block_index];
                    uint8_t marker = 0U;
                    if (!marker_from_jpeg_route(block.route, &marker)) {
                        out.status = TransferStatus::Unsupported;
                        out.code   = EmitTransferCode::UnsupportedRoute;
                        out.errors = 1U;
                        out.message
                            = "prepared transfer block route is not a supported jpeg marker";
                        return out;
                    }
                    if (chunk.size
                        != 4U + static_cast<uint64_t>(block.payload.size())) {
                        out.status = TransferStatus::InvalidArgument;
                        out.code   = EmitTransferCode::PlanMismatch;
                        out.errors = 1U;
                        out.message
                            = "prepared transfer block jpeg size mismatch";
                        return out;
                    }
                } else if (bundle.target_format == TransferTargetFormat::Jxl) {
                    const PreparedTransferBlock& block
                        = bundle.blocks[chunk.block_index];
                    std::array<char, 4> box_type = { '\0', '\0', '\0', '\0' };
                    bool compress                = false;
                    if (!jxl_box_from_route(block.route, &box_type, &compress)) {
                        out.status = TransferStatus::Unsupported;
                        out.code   = EmitTransferCode::UnsupportedRoute;
                        out.errors = 1U;
                        out.message
                            = "prepared transfer block route is not a supported jxl box";
                        return out;
                    }
                    if (block.box_type
                            != std::array<char, 4> { '\0', '\0', '\0', '\0' }
                        && block.box_type != box_type) {
                        out.status = TransferStatus::Malformed;
                        out.code   = EmitTransferCode::InvalidPayload;
                        out.errors = 1U;
                        out.message
                            = "prepared transfer block jxl box_type does not match route";
                        return out;
                    }
                    if (chunk.size
                        != 8U + static_cast<uint64_t>(block.payload.size())) {
                        out.status = TransferStatus::InvalidArgument;
                        out.code   = EmitTransferCode::PlanMismatch;
                        out.errors = 1U;
                        out.message
                            = "prepared transfer block jxl size mismatch";
                        return out;
                    }
                } else if (bundle.target_format == TransferTargetFormat::Webp) {
                    const PreparedTransferBlock& block
                        = bundle.blocks[chunk.block_index];
                    std::array<char, 4> chunk_type = { '\0', '\0', '\0', '\0' };
                    if (!webp_chunk_from_route(block.route, &chunk_type)) {
                        out.status = TransferStatus::Unsupported;
                        out.code   = EmitTransferCode::UnsupportedRoute;
                        out.errors = 1U;
                        out.message
                            = "prepared transfer block route is not a supported webp chunk";
                        return out;
                    }
                    const uint64_t expected_size
                        = 8U + static_cast<uint64_t>(block.payload.size())
                          + static_cast<uint64_t>((block.payload.size() & 1U)
                                                  != 0U);
                    if (chunk.size != expected_size) {
                        out.status = TransferStatus::InvalidArgument;
                        out.code   = EmitTransferCode::PlanMismatch;
                        out.errors = 1U;
                        out.message
                            = "prepared transfer block webp size mismatch";
                        return out;
                    }
                } else if (transfer_target_is_bmff(bundle.target_format)) {
                    const PreparedTransferBlock& block
                        = bundle.blocks[chunk.block_index];
                    uint32_t item_type = 0U;
                    bool mime_xmp      = false;
                    if (!bmff_item_from_route(block.route, &item_type,
                                              &mime_xmp)) {
                        out.status = TransferStatus::Unsupported;
                        out.code   = EmitTransferCode::UnsupportedRoute;
                        out.errors = 1U;
                        out.message
                            = "prepared transfer block route is not a supported bmff item";
                        return out;
                    }
                    (void)item_type;
                    (void)mime_xmp;
                    if (chunk.size
                        != static_cast<uint64_t>(block.payload.size())) {
                        out.status = TransferStatus::InvalidArgument;
                        out.code   = EmitTransferCode::PlanMismatch;
                        out.errors = 1U;
                        out.message
                            = "prepared transfer block bmff size mismatch";
                        return out;
                    }
                } else {
                    out.status = TransferStatus::Unsupported;
                    out.code   = EmitTransferCode::InvalidArgument;
                    out.errors = 1U;
                    out.message
                        = "prepared transfer block chunks are only supported for jpeg, jxl, webp, and bmff targets";
                    return out;
                }
                break;
            case TransferPackageChunkKind::PreparedJpegSegment:
                if (bundle.target_format != TransferTargetFormat::Jpeg) {
                    out.status = TransferStatus::Unsupported;
                    out.code   = EmitTransferCode::InvalidArgument;
                    out.errors = 1U;
                    out.message
                        = "prepared jpeg segment chunk requires jpeg target";
                    return out;
                }
                if (chunk.block_index >= bundle.blocks.size()) {
                    out.status = TransferStatus::InvalidArgument;
                    out.code   = EmitTransferCode::PlanMismatch;
                    out.errors = 1U;
                    out.message
                        = "prepared jpeg segment chunk block_index is out of range";
                    return out;
                }
                if (chunk.size
                    != 4U
                           + static_cast<uint64_t>(
                               bundle.blocks[chunk.block_index].payload.size())) {
                    out.status  = TransferStatus::InvalidArgument;
                    out.code    = EmitTransferCode::PlanMismatch;
                    out.errors  = 1U;
                    out.message = "prepared jpeg segment chunk size mismatch";
                    return out;
                }
                break;
            case TransferPackageChunkKind::InlineBytes:
                if (chunk.size
                    != static_cast<uint64_t>(chunk.inline_bytes.size())) {
                    out.status = TransferStatus::InvalidArgument;
                    out.code   = EmitTransferCode::PlanMismatch;
                    out.errors = 1U;
                    out.message
                        = "prepared transfer package inline chunk size mismatch";
                    return out;
                }
                break;
            }
            if (UINT64_MAX - expected_output < chunk.size) {
                out.status  = TransferStatus::LimitExceeded;
                out.code    = EmitTransferCode::InvalidPayload;
                out.errors  = 1U;
                out.message = "prepared transfer package output size overflow";
                return out;
            }
            expected_output += chunk.size;
        }
        if (expected_output != plan.output_size) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "prepared transfer package output_size mismatch";
            return out;
        }
        if (bundle.target_format == TransferTargetFormat::Jpeg) {
            const EmitTransferResult c2pa_preflight
                = validate_prepared_jpeg_c2pa_blocks_for_emit(bundle, {});
            if (c2pa_preflight.status != TransferStatus::Ok) {
                return c2pa_preflight;
            }
        }
        out.status = TransferStatus::Ok;
        out.code   = EmitTransferCode::None;
        return out;
    }

    static EmitTransferResult validate_prepared_transfer_package_batch(
        const PreparedTransferPackageBatch& batch) noexcept
    {
        EmitTransferResult out;
        if (batch.contract_version != kMetadataTransferContractVersion) {
            out.status = TransferStatus::InvalidArgument;
            out.code   = EmitTransferCode::PlanMismatch;
            out.errors = 1U;
            out.message
                = "prepared transfer package batch contract_version mismatch";
            return out;
        }

        uint64_t expected_output = 0U;
        for (size_t i = 0; i < batch.chunks.size(); ++i) {
            const PreparedTransferPackageBlob& chunk = batch.chunks[i];
            if (chunk.output_offset != expected_output) {
                out.status = TransferStatus::InvalidArgument;
                out.code   = EmitTransferCode::PlanMismatch;
                out.errors = 1U;
                out.message
                    = "prepared transfer package batch output offsets are not contiguous";
                return out;
            }
            if (UINT64_MAX - expected_output
                < static_cast<uint64_t>(chunk.bytes.size())) {
                out.status = TransferStatus::LimitExceeded;
                out.code   = EmitTransferCode::InvalidPayload;
                out.errors = 1U;
                out.message
                    = "prepared transfer package batch output size overflow";
                return out;
            }
            expected_output += static_cast<uint64_t>(chunk.bytes.size());
        }
        if (expected_output != batch.output_size) {
            out.status = TransferStatus::InvalidArgument;
            out.code   = EmitTransferCode::PlanMismatch;
            out.errors = 1U;
            out.message = "prepared transfer package batch output_size mismatch";
            return out;
        }
        out.status = TransferStatus::Ok;
        out.code   = EmitTransferCode::None;
        return out;
    }

    static bool materialize_prepared_transfer_package_chunk(
        std::span<const std::byte> input, const PreparedTransferBundle& bundle,
        const PreparedTransferPackageChunk& chunk,
        std::vector<std::byte>* out_bytes, EmitTransferResult* out) noexcept
    {
        if (!out_bytes || !out) {
            return false;
        }

        switch (chunk.kind) {
        case TransferPackageChunkKind::SourceRange:
            out_bytes->assign(input.begin()
                                  + static_cast<std::ptrdiff_t>(
                                      chunk.source_offset),
                              input.begin()
                                  + static_cast<std::ptrdiff_t>(
                                      chunk.source_offset + chunk.size));
            return true;
        case TransferPackageChunkKind::PreparedJpegSegment: {
            const PreparedTransferBlock& block
                = bundle.blocks[chunk.block_index];
            return serialize_jpeg_marker_segment(
                chunk.jpeg_marker_code,
                std::span<const std::byte>(block.payload.data(),
                                           block.payload.size()),
                out_bytes, out);
        }
        case TransferPackageChunkKind::PreparedTransferBlock: {
            const PreparedTransferBlock& block
                = bundle.blocks[chunk.block_index];
            if (bundle.target_format == TransferTargetFormat::Jpeg) {
                uint8_t marker = 0U;
                if (!marker_from_jpeg_route(block.route, &marker)) {
                    out->status = TransferStatus::Unsupported;
                    out->code   = EmitTransferCode::UnsupportedRoute;
                    out->errors += 1U;
                    out->failed_block_index = chunk.block_index;
                    out->message
                        = "prepared transfer block route is not a supported jpeg marker";
                    return false;
                }
                return serialize_jpeg_marker_segment(
                    marker,
                    std::span<const std::byte>(block.payload.data(),
                                               block.payload.size()),
                    out_bytes, out);
            }
            if (bundle.target_format == TransferTargetFormat::Jxl) {
                std::array<char, 4> box_type = { '\0', '\0', '\0', '\0' };
                bool compress                = false;
                if (!jxl_box_from_route(block.route, &box_type, &compress)) {
                    out->status = TransferStatus::Unsupported;
                    out->code   = EmitTransferCode::UnsupportedRoute;
                    out->errors += 1U;
                    out->failed_block_index = chunk.block_index;
                    out->message
                        = "prepared transfer block route is not a supported jxl box";
                    return false;
                }
                return serialize_jxl_box(
                    box_type,
                    std::span<const std::byte>(block.payload.data(),
                                               block.payload.size()),
                    out_bytes, out);
            }
            if (bundle.target_format == TransferTargetFormat::Webp) {
                std::array<char, 4> chunk_type = { '\0', '\0', '\0', '\0' };
                if (!webp_chunk_from_route(block.route, &chunk_type)) {
                    out->status = TransferStatus::Unsupported;
                    out->code   = EmitTransferCode::UnsupportedRoute;
                    out->errors += 1U;
                    out->failed_block_index = chunk.block_index;
                    out->message
                        = "prepared transfer block route is not a supported webp chunk";
                    return false;
                }
                return serialize_webp_chunk(
                    chunk_type,
                    std::span<const std::byte>(block.payload.data(),
                                               block.payload.size()),
                    out_bytes, out);
            }
            if (transfer_target_is_bmff(bundle.target_format)) {
                uint32_t item_type = 0U;
                bool mime_xmp      = false;
                if (!bmff_item_from_route(block.route, &item_type, &mime_xmp)) {
                    out->status = TransferStatus::Unsupported;
                    out->code   = EmitTransferCode::UnsupportedRoute;
                    out->errors += 1U;
                    out->failed_block_index = chunk.block_index;
                    out->message
                        = "prepared transfer block route is not a supported bmff item";
                    return false;
                }
                (void)item_type;
                (void)mime_xmp;
                out_bytes->assign(block.payload.begin(), block.payload.end());
                return true;
            }
            out->status = TransferStatus::Unsupported;
            out->code   = EmitTransferCode::InvalidArgument;
            out->errors += 1U;
            out->failed_block_index = chunk.block_index;
            out->message
                = "prepared transfer block chunks are only supported for jpeg, jxl, webp, and bmff targets";
            return false;
        }
        case TransferPackageChunkKind::InlineBytes:
            *out_bytes = chunk.inline_bytes;
            return true;
        }
        out->status = TransferStatus::InvalidArgument;
        out->code   = EmitTransferCode::InvalidArgument;
        out->errors += 1U;
        out->message = "prepared transfer package chunk kind is invalid";
        return false;
    }

    static bool has_time_patch_width(const PreparedTransferBundle& bundle,
                                     TimePatchField field,
                                     size_t width) noexcept
    {
        for (size_t i = 0; i < bundle.time_patch_map.size(); ++i) {
            const TimePatchSlot& slot = bundle.time_patch_map[i];
            if (slot.field == field
                && static_cast<size_t>(slot.width) == width) {
                return true;
            }
        }
        return false;
    }

    static void maybe_append_auto_nul(const PreparedTransferBundle& bundle,
                                      const TransferTimePatchInput& input,
                                      bool auto_nul,
                                      TimePatchUpdate* out) noexcept
    {
        if (!out || !auto_nul || !input.text_value) {
            return;
        }
        const size_t n = out->value.size();
        if (has_time_patch_width(bundle, input.field, n)) {
            return;
        }
        if (has_time_patch_width(bundle, input.field, n + 1U)) {
            out->value.push_back(std::byte { 0 });
        }
    }

    static void build_execute_time_patch_updates(
        const PreparedTransferBundle& bundle,
        const ExecutePreparedTransferOptions& options,
        std::vector<TimePatchUpdate>* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        out->reserve(options.time_patches.size());
        for (size_t i = 0; i < options.time_patches.size(); ++i) {
            TimePatchUpdate one;
            one.field = options.time_patches[i].field;
            one.value = options.time_patches[i].value;
            maybe_append_auto_nul(bundle, options.time_patches[i],
                                  options.time_patch_auto_nul, &one);
            out->push_back(std::move(one));
        }
    }

    static TransferStatus
    map_mapped_file_status_to_transfer(MappedFileStatus status) noexcept
    {
        switch (status) {
        case MappedFileStatus::Ok: return TransferStatus::Ok;
        case MappedFileStatus::TooLarge: return TransferStatus::LimitExceeded;
        case MappedFileStatus::OpenFailed:
        case MappedFileStatus::StatFailed:
        case MappedFileStatus::MapFailed: return TransferStatus::Unsupported;
        }
        return TransferStatus::InternalError;
    }

    static const char* edit_target_file_error(MappedFileStatus status) noexcept
    {
        switch (status) {
        case MappedFileStatus::OpenFailed:
            return "failed to open edit target file";
        case MappedFileStatus::StatFailed:
            return "failed to stat edit target file";
        case MappedFileStatus::TooLarge:
            return "edit target file exceeds size limit";
        case MappedFileStatus::MapFailed:
            return "failed to map edit target file";
        case MappedFileStatus::Ok: break;
        }
        return "failed to read edit target file";
    }

    static EmitTransferResult skipped_emit_result(const char* message) noexcept
    {
        EmitTransferResult out;
        out.status  = TransferStatus::Unsupported;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = message ? message : "emit skipped";
        return out;
    }

    static uint32_t prepared_transfer_execution_plan_ops(
        const PreparedTransferExecutionPlan& plan) noexcept
    {
        switch (plan.target_format) {
        case TransferTargetFormat::Jpeg:
            return static_cast<uint32_t>(plan.jpeg_emit.ops.size());
        case TransferTargetFormat::Tiff:
            return static_cast<uint32_t>(plan.tiff_emit.ops.size());
        case TransferTargetFormat::Jxl:
            return static_cast<uint32_t>(plan.jxl_emit.ops.size());
        case TransferTargetFormat::Webp:
            return static_cast<uint32_t>(plan.webp_emit.ops.size());
        case TransferTargetFormat::Heif:
        case TransferTargetFormat::Avif:
        case TransferTargetFormat::Cr3:
            return static_cast<uint32_t>(plan.bmff_emit.ops.size());
        default: break;
        }
        return 0U;
    }

    static EmitTransferResult validate_prepared_transfer_execution_plan(
        const PreparedTransferBundle& bundle,
        const PreparedTransferExecutionPlan& plan) noexcept
    {
        EmitTransferResult out;
        if (plan.contract_version != bundle.contract_version) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "compiled execution plan contract_version mismatch";
            return out;
        }
        if (plan.target_format != bundle.target_format) {
            out.status  = TransferStatus::InvalidArgument;
            out.code    = EmitTransferCode::PlanMismatch;
            out.errors  = 1U;
            out.message = "compiled execution plan target_format mismatch";
            return out;
        }

        switch (bundle.target_format) {
        case TransferTargetFormat::Jpeg:
            if (plan.jpeg_emit.contract_version != bundle.contract_version) {
                out.status = TransferStatus::InvalidArgument;
                out.code   = EmitTransferCode::PlanMismatch;
                out.errors = 1U;
                out.message
                    = "compiled jpeg emit plan contract_version mismatch";
                return out;
            }
            break;
        case TransferTargetFormat::Tiff:
            if (plan.tiff_emit.contract_version != bundle.contract_version) {
                out.status = TransferStatus::InvalidArgument;
                out.code   = EmitTransferCode::PlanMismatch;
                out.errors = 1U;
                out.message
                    = "compiled tiff emit plan contract_version mismatch";
                return out;
            }
            break;
        case TransferTargetFormat::Jxl:
            if (plan.jxl_emit.contract_version != bundle.contract_version) {
                out.status = TransferStatus::InvalidArgument;
                out.code   = EmitTransferCode::PlanMismatch;
                out.errors = 1U;
                out.message = "compiled jxl emit plan contract_version mismatch";
                return out;
            }
            break;
        case TransferTargetFormat::Webp:
            if (plan.webp_emit.contract_version != bundle.contract_version) {
                out.status = TransferStatus::InvalidArgument;
                out.code   = EmitTransferCode::PlanMismatch;
                out.errors = 1U;
                out.message
                    = "compiled webp emit plan contract_version mismatch";
                return out;
            }
            break;
        case TransferTargetFormat::Heif:
        case TransferTargetFormat::Avif:
        case TransferTargetFormat::Cr3:
            if (plan.bmff_emit.contract_version != bundle.contract_version) {
                out.status = TransferStatus::InvalidArgument;
                out.code   = EmitTransferCode::PlanMismatch;
                out.errors = 1U;
                out.message
                    = "compiled bmff emit plan contract_version mismatch";
                return out;
            }
            break;
        default:
            out.status  = TransferStatus::Unsupported;
            out.code    = EmitTransferCode::InvalidArgument;
            out.errors  = 1U;
            out.message = "unsupported target format for emit";
            return out;
        }

        out.status = TransferStatus::Ok;
        out.code   = EmitTransferCode::None;
        return out;
    }

    static ExecutePreparedTransferResult execute_prepared_transfer_impl(
        PreparedTransferBundle* bundle,
        const PreparedTransferExecutionPlan* compiled_plan,
        std::span<const std::byte> edit_input,
        const ExecutePreparedTransferOptions& options) noexcept
    {
        ExecutePreparedTransferResult out;
        out.edit_requested     = options.edit_requested;
        out.edit_apply.status  = TransferStatus::Unsupported;
        out.edit_apply.code    = EmitTransferCode::InvalidArgument;
        out.edit_apply.message = options.edit_requested
                                     ? "edit apply not requested"
                                     : "edit not requested";

        if (!bundle) {
            out.time_patch.status  = TransferStatus::InvalidArgument;
            out.time_patch.errors  = 1U;
            out.time_patch.message = "bundle is null";
            out.compile.status     = TransferStatus::InvalidArgument;
            out.compile.code       = EmitTransferCode::InvalidArgument;
            out.compile.errors     = 1U;
            out.compile.message    = "bundle is null";
            out.emit               = out.compile;
            out.edit_plan_status   = TransferStatus::InvalidArgument;
            out.edit_plan_message  = "bundle is null";
            out.edit_apply.status  = TransferStatus::InvalidArgument;
            out.edit_apply.code    = EmitTransferCode::InvalidArgument;
            out.edit_apply.errors  = 1U;
            out.edit_apply.message = "bundle is null";
            return out;
        }

        if (!options.time_patches.empty()) {
            std::vector<TimePatchUpdate> updates;
            build_execute_time_patch_updates(*bundle, options, &updates);
            out.time_patch = apply_time_patches(bundle, updates,
                                                options.time_patch);
        }

        if (out.time_patch.status != TransferStatus::Ok) {
            out.compile = skipped_emit_result(
                "skipped emit due to time patch failure");
            out.emit = out.compile;
            if (options.edit_requested) {
                out.edit_plan_status = TransferStatus::Unsupported;
                out.edit_plan_message = "skipped edit due to time patch failure";
                out.edit_apply.status = TransferStatus::Unsupported;
                out.edit_apply.code   = EmitTransferCode::InvalidArgument;
                out.edit_apply.errors = 1U;
                out.edit_apply.message
                    = "skipped edit due to time patch failure";
            }
            return out;
        }

        PreparedTransferExecutionPlan local_plan;
        const PreparedTransferExecutionPlan* effective_plan = compiled_plan;
        if (effective_plan) {
            out.compile
                = validate_prepared_transfer_execution_plan(*bundle,
                                                            *effective_plan);
            if (out.compile.status == TransferStatus::Ok) {
                out.compiled_ops = prepared_transfer_execution_plan_ops(
                    *effective_plan);
            }
        } else {
            out.compile      = compile_prepared_transfer_execution(*bundle,
                                                                   options.emit,
                                                                   &local_plan);
            out.compiled_ops = prepared_transfer_execution_plan_ops(local_plan);
            effective_plan   = &local_plan;
        }

        const uint32_t emit_repeat = options.emit_repeat == 0U
                                         ? 1U
                                         : options.emit_repeat;
        if (out.compile.status == TransferStatus::Ok && effective_plan) {
            switch (bundle->target_format) {
            case TransferTargetFormat::Jpeg: {
                if (options.emit_output_writer) {
                    const uint64_t writer_hint
                        = options.emit_output_writer->remaining_capacity_hint();
                    if (writer_hint != UINT64_MAX) {
                        uint64_t needed_bytes = 0U;
                        if (!measure_jpeg_emit_bytes_from_plan(
                                *bundle, effective_plan->jpeg_emit,
                                effective_plan->emit, emit_repeat,
                                &needed_bytes)) {
                            out.emit.status = TransferStatus::InvalidArgument;
                            out.emit.code   = EmitTransferCode::PlanMismatch;
                            out.emit.errors = 1U;
                            out.emit.message
                                = "failed to measure compiled jpeg emit bytes";
                            break;
                        }
                        if (needed_bytes > writer_hint) {
                            out.emit.status = TransferStatus::LimitExceeded;
                            out.emit.code = EmitTransferCode::BackendWriteFailed;
                            out.emit.errors = 1U;
                            out.emit.message
                                = "emit_output_writer capacity exceeded";
                            break;
                        }
                    }

                    CountingTransferByteWriter writer(
                        *options.emit_output_writer);
                    for (uint32_t rep = 0; rep < emit_repeat; ++rep) {
                        out.emit = write_prepared_bundle_jpeg_compiled(
                            *bundle, effective_plan->jpeg_emit, writer,
                            effective_plan->emit);
                        if (out.emit.status != TransferStatus::Ok) {
                            break;
                        }
                    }
                    out.emit_output_size = writer.bytes_written();
                    if (out.emit.status == TransferStatus::Ok) {
                        build_jpeg_emit_summary_from_plan(
                            *bundle, effective_plan->jpeg_emit,
                            effective_plan->emit, emit_repeat,
                            &out.marker_summary);
                    }
                } else {
                    ExecuteRecordingJpegEmitter emitter;
                    emitter.reset();
                    for (uint32_t rep = 0; rep < emit_repeat; ++rep) {
                        out.emit = emit_prepared_bundle_jpeg_compiled(
                            *bundle, effective_plan->jpeg_emit, emitter,
                            effective_plan->emit);
                        if (out.emit.status != TransferStatus::Ok) {
                            break;
                        }
                    }
                    emitter.build_summary(&out.marker_summary);
                }
                break;
            }
            case TransferTargetFormat::Tiff: {
                if (options.emit_output_writer) {
                    out.emit = skipped_emit_result(
                        "emit_output_writer is not supported for tiff");
                } else {
                    ExecuteRecordingTiffEmitter emitter;
                    emitter.reset();
                    for (uint32_t rep = 0; rep < emit_repeat; ++rep) {
                        out.emit = emit_prepared_bundle_tiff_compiled(
                            *bundle, effective_plan->tiff_emit, emitter,
                            effective_plan->emit);
                        if (out.emit.status != TransferStatus::Ok) {
                            break;
                        }
                    }
                    out.tiff_commit = emitter.committed();
                    build_tiff_emit_summary_from_plan(*bundle,
                                                      effective_plan->tiff_emit,
                                                      effective_plan->emit,
                                                      emit_repeat,
                                                      &out.tiff_tag_summary);
                }
                break;
            }
            case TransferTargetFormat::Jxl: {
                if (options.emit_output_writer) {
                    PreparedTransferPackagePlan package;
                    out.emit = build_prepared_transfer_emit_package(
                        *bundle, &package, effective_plan->emit);
                    if (out.emit.status != TransferStatus::Ok) {
                        break;
                    }

                    const uint64_t writer_hint
                        = options.emit_output_writer->remaining_capacity_hint();
                    if (writer_hint != UINT64_MAX) {
                        uint64_t needed_bytes = package.output_size;
                        if (emit_repeat != 0U
                            && needed_bytes > (UINT64_MAX / emit_repeat)) {
                            out.emit.status = TransferStatus::LimitExceeded;
                            out.emit.code = EmitTransferCode::BackendWriteFailed;
                            out.emit.errors = 1U;
                            out.emit.message
                                = "emit_output_writer capacity exceeded";
                            break;
                        }
                        needed_bytes *= emit_repeat;
                        if (needed_bytes > writer_hint) {
                            out.emit.status = TransferStatus::LimitExceeded;
                            out.emit.code = EmitTransferCode::BackendWriteFailed;
                            out.emit.errors = 1U;
                            out.emit.message
                                = "emit_output_writer capacity exceeded";
                            break;
                        }
                    }

                    CountingTransferByteWriter writer(
                        *options.emit_output_writer);
                    const std::span<const std::byte> empty_input;
                    for (uint32_t rep = 0; rep < emit_repeat; ++rep) {
                        out.emit = write_prepared_transfer_package(empty_input,
                                                                   *bundle,
                                                                   package,
                                                                   writer);
                        if (out.emit.status != TransferStatus::Ok) {
                            break;
                        }
                    }
                    out.emit_output_size = writer.bytes_written();
                    if (out.emit.status == TransferStatus::Ok) {
                        build_jxl_emit_summary_from_plan(
                            *bundle, effective_plan->jxl_emit,
                            effective_plan->emit, emit_repeat,
                            &out.jxl_box_summary);
                    }
                } else {
                    ExecuteRecordingJxlEmitter emitter;
                    emitter.reset();
                    for (uint32_t rep = 0; rep < emit_repeat; ++rep) {
                        out.emit = emit_prepared_bundle_jxl_compiled(
                            *bundle, effective_plan->jxl_emit, emitter,
                            effective_plan->emit);
                        if (out.emit.status != TransferStatus::Ok) {
                            break;
                        }
                    }
                    if (out.emit.status == TransferStatus::Ok) {
                        emitter.build_summary(&out.jxl_box_summary);
                    }
                }
                break;
            }
            case TransferTargetFormat::Webp: {
                if (options.emit_output_writer) {
                    PreparedTransferPackagePlan package;
                    out.emit = build_prepared_transfer_emit_package(
                        *bundle, &package, effective_plan->emit);
                    if (out.emit.status != TransferStatus::Ok) {
                        break;
                    }
                    const uint64_t writer_hint
                        = options.emit_output_writer->remaining_capacity_hint();
                    if (writer_hint != UINT64_MAX) {
                        uint64_t needed_bytes = package.output_size;
                        if (emit_repeat != 0U
                            && needed_bytes > (UINT64_MAX / emit_repeat)) {
                            out.emit.status = TransferStatus::LimitExceeded;
                            out.emit.code = EmitTransferCode::BackendWriteFailed;
                            out.emit.errors = 1U;
                            out.emit.message
                                = "emit_output_writer capacity exceeded";
                            break;
                        }
                        needed_bytes *= emit_repeat;
                        if (needed_bytes > writer_hint) {
                            out.emit.status = TransferStatus::LimitExceeded;
                            out.emit.code = EmitTransferCode::BackendWriteFailed;
                            out.emit.errors = 1U;
                            out.emit.message
                                = "emit_output_writer capacity exceeded";
                            break;
                        }
                    }

                    CountingTransferByteWriter writer(
                        *options.emit_output_writer);
                    const std::span<const std::byte> empty_input;
                    for (uint32_t rep = 0; rep < emit_repeat; ++rep) {
                        out.emit = write_prepared_transfer_package(empty_input,
                                                                   *bundle,
                                                                   package,
                                                                   writer);
                        if (out.emit.status != TransferStatus::Ok) {
                            break;
                        }
                    }
                    out.emit_output_size = writer.bytes_written();
                    if (out.emit.status == TransferStatus::Ok) {
                        build_webp_emit_summary_from_plan(
                            *bundle, effective_plan->webp_emit,
                            effective_plan->emit, emit_repeat,
                            &out.webp_chunk_summary);
                    }
                } else {
                    ExecuteRecordingWebpEmitter emitter;
                    emitter.reset();
                    for (uint32_t rep = 0; rep < emit_repeat; ++rep) {
                        out.emit = emit_prepared_bundle_webp_compiled(
                            *bundle, effective_plan->webp_emit, emitter,
                            effective_plan->emit);
                        if (out.emit.status != TransferStatus::Ok) {
                            break;
                        }
                    }
                    if (out.emit.status == TransferStatus::Ok) {
                        emitter.build_summary(&out.webp_chunk_summary);
                    }
                }
                break;
            }
            case TransferTargetFormat::Heif:
            case TransferTargetFormat::Avif:
            case TransferTargetFormat::Cr3: {
                if (options.emit_output_writer) {
                    out.emit = skipped_emit_result(
                        "emit_output_writer is not supported for bmff targets");
                } else {
                    ExecuteRecordingBmffEmitter emitter;
                    emitter.reset();
                    for (uint32_t rep = 0; rep < emit_repeat; ++rep) {
                        out.emit = emit_prepared_bundle_bmff_compiled(
                            *bundle, effective_plan->bmff_emit, emitter,
                            effective_plan->emit);
                        if (out.emit.status != TransferStatus::Ok) {
                            break;
                        }
                    }
                    if (out.emit.status == TransferStatus::Ok) {
                        emitter.build_summary(&out.bmff_item_summary);
                    }
                }
                break;
            }
            default:
                out.compile.status  = TransferStatus::Unsupported;
                out.compile.code    = EmitTransferCode::InvalidArgument;
                out.compile.errors  = 1U;
                out.compile.message = "unsupported target format for emit";
                out.emit            = skipped_emit_result(
                    "unsupported target format for emit");
                break;
            }
        } else {
            out.emit = skipped_emit_result(
                "skipped emit due to compile failure");
        }

        if (!options.edit_requested) {
            return out;
        }

        out.edit_input_size = static_cast<uint64_t>(edit_input.size());
        if (bundle->target_format == TransferTargetFormat::Jpeg) {
            out.jpeg_edit_plan
                = plan_prepared_bundle_jpeg_edit(edit_input, *bundle,
                                                 options.jpeg_edit);
            out.edit_plan_status  = out.jpeg_edit_plan.status;
            out.edit_plan_message = out.jpeg_edit_plan.message;
            out.edit_output_size  = out.jpeg_edit_plan.output_size;
            if (out.jpeg_edit_plan.status == TransferStatus::Ok
                && options.edit_apply) {
                if (options.edit_output_writer) {
                    out.edit_apply = write_prepared_bundle_jpeg_edit(
                        edit_input, *bundle, out.jpeg_edit_plan,
                        *options.edit_output_writer);
                } else {
                    out.edit_apply
                        = apply_prepared_bundle_jpeg_edit(edit_input, *bundle,
                                                          out.jpeg_edit_plan,
                                                          &out.edited_output);
                    if (out.edit_apply.status == TransferStatus::Ok) {
                        out.edit_output_size = static_cast<uint64_t>(
                            out.edited_output.size());
                    }
                }
            }
        } else if (bundle->target_format == TransferTargetFormat::Tiff) {
            out.tiff_edit_plan
                = plan_prepared_bundle_tiff_edit(edit_input, *bundle,
                                                 options.tiff_edit);
            out.edit_plan_status  = out.tiff_edit_plan.status;
            out.edit_plan_message = out.tiff_edit_plan.message;
            out.edit_output_size  = out.tiff_edit_plan.output_size;
            if (out.tiff_edit_plan.status == TransferStatus::Ok
                && options.edit_apply) {
                if (options.edit_output_writer) {
                    out.edit_apply = write_prepared_bundle_tiff_edit(
                        edit_input, *bundle, out.tiff_edit_plan,
                        *options.edit_output_writer);
                } else {
                    out.edit_apply
                        = apply_prepared_bundle_tiff_edit(edit_input, *bundle,
                                                          out.tiff_edit_plan,
                                                          &out.edited_output);
                    if (out.edit_apply.status == TransferStatus::Ok) {
                        out.edit_output_size = static_cast<uint64_t>(
                            out.edited_output.size());
                    }
                }
            }
        } else {
            out.edit_plan_status   = TransferStatus::Unsupported;
            out.edit_plan_message  = "unsupported target format for edit";
            out.edit_apply.status  = TransferStatus::Unsupported;
            out.edit_apply.code    = EmitTransferCode::InvalidArgument;
            out.edit_apply.errors  = 1U;
            out.edit_apply.message = "unsupported target format for edit";
        }

        return out;
    }

}  // namespace

EmitTransferResult
compile_prepared_transfer_execution(
    const PreparedTransferBundle& bundle, const EmitTransferOptions& options,
    PreparedTransferExecutionPlan* out_plan) noexcept
{
    EmitTransferResult out;
    if (!out_plan) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_plan is null";
        return out;
    }

    out_plan->contract_version           = bundle.contract_version;
    out_plan->target_format              = bundle.target_format;
    out_plan->emit                       = options;
    out_plan->jpeg_emit.contract_version = bundle.contract_version;
    out_plan->jpeg_emit.ops.clear();
    out_plan->tiff_emit.contract_version = bundle.contract_version;
    out_plan->tiff_emit.ops.clear();
    out_plan->jxl_emit.contract_version = bundle.contract_version;
    out_plan->jxl_emit.ops.clear();
    out_plan->webp_emit.contract_version = bundle.contract_version;
    out_plan->webp_emit.ops.clear();
    out_plan->bmff_emit.contract_version = bundle.contract_version;
    out_plan->bmff_emit.ops.clear();

    switch (bundle.target_format) {
    case TransferTargetFormat::Jpeg:
        return compile_prepared_bundle_jpeg(bundle, &out_plan->jpeg_emit,
                                            options);
    case TransferTargetFormat::Tiff:
        return compile_prepared_bundle_tiff(bundle, &out_plan->tiff_emit,
                                            options);
    case TransferTargetFormat::Jxl:
        return compile_prepared_bundle_jxl(bundle, &out_plan->jxl_emit,
                                           options);
    case TransferTargetFormat::Webp:
        return compile_prepared_bundle_webp(bundle, &out_plan->webp_emit,
                                            options);
    case TransferTargetFormat::Heif:
    case TransferTargetFormat::Avif:
    case TransferTargetFormat::Cr3:
        return compile_prepared_bundle_bmff(bundle, &out_plan->bmff_emit,
                                            options);
    default:
        out.status  = TransferStatus::Unsupported;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "unsupported target format for emit";
        return out;
    }
}

EmitTransferResult
build_prepared_transfer_adapter_view(const PreparedTransferBundle& bundle,
                                     PreparedTransferAdapterView* out_view,
                                     const EmitTransferOptions& options) noexcept
{
    EmitTransferResult out;
    if (!out_view) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "out_view is null";
        return out;
    }

    PreparedTransferExecutionPlan plan;
    out = compile_prepared_transfer_execution(bundle, options, &plan);
    if (out.status != TransferStatus::Ok) {
        return out;
    }

    PreparedTransferAdapterView view;
    view.contract_version = bundle.contract_version;
    view.target_format    = bundle.target_format;
    view.emit             = options;

    if (bundle.target_format == TransferTargetFormat::Jpeg) {
        view.ops.reserve(plan.jpeg_emit.ops.size());
        for (size_t i = 0; i < plan.jpeg_emit.ops.size(); ++i) {
            const PreparedJpegEmitOp& src      = plan.jpeg_emit.ops[i];
            const PreparedTransferBlock& block = bundle.blocks[src.block_index];
            PreparedTransferAdapterOp op;
            op.kind             = TransferAdapterOpKind::JpegMarker;
            op.block_index      = src.block_index;
            op.payload_size     = static_cast<uint64_t>(block.payload.size());
            op.serialized_size  = 4U + op.payload_size;
            op.jpeg_marker_code = src.marker_code;
            view.ops.push_back(op);
        }
    } else if (bundle.target_format == TransferTargetFormat::Tiff) {
        view.ops.reserve(plan.tiff_emit.ops.size());
        for (size_t i = 0; i < plan.tiff_emit.ops.size(); ++i) {
            const PreparedTiffEmitOp& src      = plan.tiff_emit.ops[i];
            const PreparedTransferBlock& block = bundle.blocks[src.block_index];
            PreparedTransferAdapterOp op;
            op.kind            = TransferAdapterOpKind::TiffTagBytes;
            op.block_index     = src.block_index;
            op.payload_size    = static_cast<uint64_t>(block.payload.size());
            op.serialized_size = op.payload_size;
            op.tiff_tag        = src.tiff_tag;
            view.ops.push_back(op);
        }
    } else if (bundle.target_format == TransferTargetFormat::Jxl) {
        view.ops.reserve(plan.jxl_emit.ops.size());
        for (size_t i = 0; i < plan.jxl_emit.ops.size(); ++i) {
            const PreparedJxlEmitOp& src       = plan.jxl_emit.ops[i];
            const PreparedTransferBlock& block = bundle.blocks[src.block_index];
            PreparedTransferAdapterOp op;
            op.kind            = src.kind == PreparedJxlEmitKind::IccProfile
                                     ? TransferAdapterOpKind::JxlIccProfile
                                     : TransferAdapterOpKind::JxlBox;
            op.block_index     = src.block_index;
            op.payload_size    = static_cast<uint64_t>(block.payload.size());
            op.serialized_size = src.kind == PreparedJxlEmitKind::IccProfile
                                     ? op.payload_size
                                     : 8U + op.payload_size;
            op.box_type        = src.box_type;
            op.compress        = src.compress;
            view.ops.push_back(op);
        }
    } else if (bundle.target_format == TransferTargetFormat::Webp) {
        view.ops.reserve(plan.webp_emit.ops.size());
        for (size_t i = 0; i < plan.webp_emit.ops.size(); ++i) {
            const PreparedWebpEmitOp& src      = plan.webp_emit.ops[i];
            const PreparedTransferBlock& block = bundle.blocks[src.block_index];
            PreparedTransferAdapterOp op;
            op.kind            = TransferAdapterOpKind::WebpChunk;
            op.block_index     = src.block_index;
            op.payload_size    = static_cast<uint64_t>(block.payload.size());
            op.serialized_size = 8U + op.payload_size
                                 + static_cast<uint64_t>(
                                     (block.payload.size() & 1U) != 0U);
            op.chunk_type = src.chunk_type;
            view.ops.push_back(op);
        }
    } else if (transfer_target_is_bmff(bundle.target_format)) {
        view.ops.reserve(plan.bmff_emit.ops.size());
        for (size_t i = 0; i < plan.bmff_emit.ops.size(); ++i) {
            const PreparedBmffEmitOp& src      = plan.bmff_emit.ops[i];
            const PreparedTransferBlock& block = bundle.blocks[src.block_index];
            PreparedTransferAdapterOp op;
            op.kind            = TransferAdapterOpKind::BmffItem;
            op.block_index     = src.block_index;
            op.payload_size    = static_cast<uint64_t>(block.payload.size());
            op.serialized_size = op.payload_size;
            op.bmff_item_type  = src.item_type;
            op.bmff_mime_xmp   = src.kind == PreparedBmffEmitKind::MimeXmp;
            view.ops.push_back(op);
        }
    } else {
        out.status  = TransferStatus::Unsupported;
        out.code    = EmitTransferCode::InvalidArgument;
        out.errors  = 1U;
        out.message = "unsupported target format for adapter view";
        return out;
    }

    *out_view   = std::move(view);
    out.status  = TransferStatus::Ok;
    out.code    = EmitTransferCode::None;
    out.emitted = static_cast<uint32_t>(out_view->ops.size());
    out.skipped = 0U;
    out.errors  = 0U;
    out.message.clear();
    return out;
}

EmitTransferResult
emit_prepared_transfer_adapter_view(const PreparedTransferBundle& bundle,
                                    const PreparedTransferAdapterView& view,
                                    TransferAdapterSink& sink) noexcept
{
    EmitTransferResult out;
    if (view.contract_version != bundle.contract_version) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::PlanMismatch;
        out.errors  = 1U;
        out.message = "adapter view contract_version mismatch";
        return out;
    }
    if (view.target_format != bundle.target_format) {
        out.status  = TransferStatus::InvalidArgument;
        out.code    = EmitTransferCode::PlanMismatch;
        out.errors  = 1U;
        out.message = "adapter view target_format mismatch";
        return out;
    }

    for (size_t i = 0; i < view.ops.size(); ++i) {
        const PreparedTransferAdapterOp& op = view.ops[i];
        if (op.block_index >= bundle.blocks.size()) {
            out.status             = TransferStatus::InvalidArgument;
            out.code               = EmitTransferCode::PlanMismatch;
            out.errors             = 1U;
            out.message            = "adapter view block_index is out of range";
            out.failed_block_index = static_cast<uint32_t>(op.block_index);
            return out;
        }

        const PreparedTransferBlock& block = bundle.blocks[op.block_index];
        const uint64_t payload_size        = static_cast<uint64_t>(
            block.payload.size());
        if (op.payload_size != payload_size) {
            out.status = TransferStatus::InvalidArgument;
            out.code   = EmitTransferCode::PlanMismatch;
            out.errors += 1U;
            out.failed_block_index = op.block_index;
            out.message            = "adapter view payload_size mismatch";
            return out;
        }

        if (bundle.target_format == TransferTargetFormat::Jpeg) {
            if (op.kind != TransferAdapterOpKind::JpegMarker
                || op.serialized_size != 4U + payload_size) {
                out.status = TransferStatus::InvalidArgument;
                out.code   = EmitTransferCode::PlanMismatch;
                out.errors += 1U;
                out.failed_block_index = op.block_index;
                out.message            = "adapter view jpeg op is inconsistent";
                return out;
            }
        } else if (bundle.target_format == TransferTargetFormat::Tiff) {
            if (op.kind != TransferAdapterOpKind::TiffTagBytes
                || op.serialized_size != payload_size) {
                out.status = TransferStatus::InvalidArgument;
                out.code   = EmitTransferCode::PlanMismatch;
                out.errors += 1U;
                out.failed_block_index = op.block_index;
                out.message            = "adapter view tiff op is inconsistent";
                return out;
            }
        } else if (bundle.target_format == TransferTargetFormat::Jxl) {
            if (op.kind == TransferAdapterOpKind::JxlIccProfile) {
                if (op.serialized_size != payload_size) {
                    out.status = TransferStatus::InvalidArgument;
                    out.code   = EmitTransferCode::PlanMismatch;
                    out.errors += 1U;
                    out.failed_block_index = op.block_index;
                    out.message = "adapter view jxl icc op is inconsistent";
                    return out;
                }
            } else if (op.kind != TransferAdapterOpKind::JxlBox
                       || op.serialized_size != 8U + payload_size) {
                out.status = TransferStatus::InvalidArgument;
                out.code   = EmitTransferCode::PlanMismatch;
                out.errors += 1U;
                out.failed_block_index = op.block_index;
                out.message            = "adapter view jxl op is inconsistent";
                return out;
            }
        } else if (bundle.target_format == TransferTargetFormat::Webp) {
            const uint64_t expected_size = 8U + payload_size
                                           + static_cast<uint64_t>(
                                               (payload_size & 1U) != 0U);
            if (op.kind != TransferAdapterOpKind::WebpChunk
                || op.serialized_size != expected_size) {
                out.status = TransferStatus::InvalidArgument;
                out.code   = EmitTransferCode::PlanMismatch;
                out.errors += 1U;
                out.failed_block_index = op.block_index;
                out.message            = "adapter view webp op is inconsistent";
                return out;
            }
        } else if (transfer_target_is_bmff(bundle.target_format)) {
            uint32_t item_type = 0U;
            bool mime_xmp      = false;
            if (!bmff_item_from_route(block.route, &item_type, &mime_xmp)
                || op.kind != TransferAdapterOpKind::BmffItem
                || op.serialized_size != payload_size
                || op.bmff_item_type != item_type
                || op.bmff_mime_xmp != mime_xmp) {
                out.status = TransferStatus::InvalidArgument;
                out.code   = EmitTransferCode::PlanMismatch;
                out.errors += 1U;
                out.failed_block_index = op.block_index;
                out.message            = "adapter view bmff op is inconsistent";
                return out;
            }
        } else {
            out.status  = TransferStatus::Unsupported;
            out.code    = EmitTransferCode::InvalidArgument;
            out.errors  = 1U;
            out.message = "unsupported target format for adapter view emit";
            return out;
        }

        const TransferStatus st
            = sink.emit_op(op,
                           std::span<const std::byte>(block.payload.data(),
                                                      block.payload.size()));
        if (st != TransferStatus::Ok) {
            out.status = st;
            out.code   = EmitTransferCode::BackendWriteFailed;
            out.errors += 1U;
            out.failed_block_index = op.block_index;
            out.message            = "adapter sink emit_op failed";
            return out;
        }
        out.emitted += 1U;
    }

    out.status = TransferStatus::Ok;
    out.code   = EmitTransferCode::None;
    return out;
}

ExecutePreparedTransferResult
execute_prepared_transfer(PreparedTransferBundle* bundle,
                          std::span<const std::byte> edit_input,
                          const ExecutePreparedTransferOptions& options) noexcept
{
    return execute_prepared_transfer_impl(bundle, nullptr, edit_input, options);
}

ExecutePreparedTransferResult
execute_prepared_transfer_compiled(
    PreparedTransferBundle* bundle, const PreparedTransferExecutionPlan& plan,
    std::span<const std::byte> edit_input,
    const ExecutePreparedTransferOptions& options) noexcept
{
    return execute_prepared_transfer_impl(bundle, &plan, edit_input, options);
}

ExecutePreparedTransferResult
write_prepared_transfer_compiled(PreparedTransferBundle* bundle,
                                 const PreparedTransferExecutionPlan& plan,
                                 TransferByteWriter& writer,
                                 std::span<const TimePatchView> time_patches,
                                 const ApplyTimePatchOptions& time_patch,
                                 uint32_t emit_repeat) noexcept
{
    if (!bundle) {
        return execute_prepared_transfer_compiled(bundle, plan);
    }

    ApplyTimePatchResult patch_result;
    if (!time_patches.empty()) {
        patch_result = apply_time_patches_view(bundle, time_patches,
                                               time_patch);
        if (patch_result.status != TransferStatus::Ok) {
            ExecutePreparedTransferResult out;
            out.time_patch = patch_result;
            out.compile    = skipped_emit_result(
                "skipped emit due to time patch failure");
            out.emit = out.compile;
            return out;
        }
    }

    ExecutePreparedTransferOptions options;
    options.emit_repeat        = emit_repeat;
    options.emit_output_writer = &writer;

    ExecutePreparedTransferResult out
        = execute_prepared_transfer_compiled(bundle, plan, {}, options);
    if (!time_patches.empty()) {
        out.time_patch = patch_result;
    }
    return out;
}

ExecutePreparedTransferResult
emit_prepared_transfer_compiled(PreparedTransferBundle* bundle,
                                const PreparedTransferExecutionPlan& plan,
                                JpegTransferEmitter& emitter,
                                std::span<const TimePatchView> time_patches,
                                const ApplyTimePatchOptions& time_patch,
                                uint32_t emit_repeat) noexcept
{
    ExecutePreparedTransferResult out;
    if (!bundle) {
        return execute_prepared_transfer_compiled(bundle, plan);
    }

    if (!time_patches.empty()) {
        out.time_patch = apply_time_patches_view(bundle, time_patches,
                                                 time_patch);
        if (out.time_patch.status != TransferStatus::Ok) {
            out.compile = skipped_emit_result(
                "skipped emit due to time patch failure");
            out.emit = out.compile;
            return out;
        }
    }

    out.compile      = validate_prepared_transfer_execution_plan(*bundle, plan);
    out.compiled_ops = prepared_transfer_execution_plan_ops(plan);
    if (out.compile.status != TransferStatus::Ok) {
        out.emit = skipped_emit_result("skipped emit due to compile failure");
        return out;
    }

    const uint32_t repeat = emit_repeat == 0U ? 1U : emit_repeat;
    for (uint32_t rep = 0; rep < repeat; ++rep) {
        out.emit = emit_prepared_bundle_jpeg_compiled(*bundle, plan.jpeg_emit,
                                                      emitter, plan.emit);
        if (out.emit.status != TransferStatus::Ok) {
            break;
        }
    }
    if (out.emit.status == TransferStatus::Ok) {
        build_jpeg_emit_summary_from_plan(*bundle, plan.jpeg_emit, plan.emit,
                                          repeat, &out.marker_summary);
    }
    return out;
}

ExecutePreparedTransferResult
emit_prepared_transfer_compiled(PreparedTransferBundle* bundle,
                                const PreparedTransferExecutionPlan& plan,
                                TiffTransferEmitter& emitter,
                                std::span<const TimePatchView> time_patches,
                                const ApplyTimePatchOptions& time_patch,
                                uint32_t emit_repeat) noexcept
{
    ExecutePreparedTransferResult out;
    if (!bundle) {
        return execute_prepared_transfer_compiled(bundle, plan);
    }

    if (!time_patches.empty()) {
        out.time_patch = apply_time_patches_view(bundle, time_patches,
                                                 time_patch);
        if (out.time_patch.status != TransferStatus::Ok) {
            out.compile = skipped_emit_result(
                "skipped emit due to time patch failure");
            out.emit = out.compile;
            return out;
        }
    }

    out.compile      = validate_prepared_transfer_execution_plan(*bundle, plan);
    out.compiled_ops = prepared_transfer_execution_plan_ops(plan);
    if (out.compile.status != TransferStatus::Ok) {
        out.emit = skipped_emit_result("skipped emit due to compile failure");
        return out;
    }

    const uint32_t repeat = emit_repeat == 0U ? 1U : emit_repeat;
    for (uint32_t rep = 0; rep < repeat; ++rep) {
        out.emit = emit_prepared_bundle_tiff_compiled(*bundle, plan.tiff_emit,
                                                      emitter, plan.emit);
        if (out.emit.status != TransferStatus::Ok) {
            break;
        }
    }
    if (out.emit.status == TransferStatus::Ok) {
        out.tiff_commit = true;
        build_tiff_emit_summary_from_plan(*bundle, plan.tiff_emit, plan.emit,
                                          repeat, &out.tiff_tag_summary);
    }
    return out;
}

ExecutePreparedTransferResult
emit_prepared_transfer_compiled(PreparedTransferBundle* bundle,
                                const PreparedTransferExecutionPlan& plan,
                                JxlTransferEmitter& emitter,
                                std::span<const TimePatchView> time_patches,
                                const ApplyTimePatchOptions& time_patch,
                                uint32_t emit_repeat) noexcept
{
    ExecutePreparedTransferResult out;
    if (!bundle) {
        return execute_prepared_transfer_compiled(bundle, plan);
    }

    if (!time_patches.empty()) {
        out.time_patch = apply_time_patches_view(bundle, time_patches,
                                                 time_patch);
        if (out.time_patch.status != TransferStatus::Ok) {
            out.compile = skipped_emit_result(
                "skipped emit due to time patch failure");
            out.emit = out.compile;
            return out;
        }
    }

    out.compile      = validate_prepared_transfer_execution_plan(*bundle, plan);
    out.compiled_ops = prepared_transfer_execution_plan_ops(plan);
    if (out.compile.status != TransferStatus::Ok) {
        out.emit = skipped_emit_result("skipped emit due to compile failure");
        return out;
    }

    const uint32_t repeat = emit_repeat == 0U ? 1U : emit_repeat;
    for (uint32_t rep = 0; rep < repeat; ++rep) {
        out.emit = emit_prepared_bundle_jxl_compiled(*bundle, plan.jxl_emit,
                                                     emitter, plan.emit);
        if (out.emit.status != TransferStatus::Ok) {
            break;
        }
    }
    if (out.emit.status == TransferStatus::Ok) {
        build_jxl_emit_summary_from_plan(*bundle, plan.jxl_emit, plan.emit,
                                         repeat, &out.jxl_box_summary);
    }
    return out;
}

ExecutePreparedTransferResult
emit_prepared_transfer_compiled(PreparedTransferBundle* bundle,
                                const PreparedTransferExecutionPlan& plan,
                                WebpTransferEmitter& emitter,
                                std::span<const TimePatchView> time_patches,
                                const ApplyTimePatchOptions& time_patch,
                                uint32_t emit_repeat) noexcept
{
    ExecutePreparedTransferResult out;
    if (!bundle) {
        return execute_prepared_transfer_compiled(bundle, plan);
    }

    if (!time_patches.empty()) {
        out.time_patch = apply_time_patches_view(bundle, time_patches,
                                                 time_patch);
        if (out.time_patch.status != TransferStatus::Ok) {
            out.compile = skipped_emit_result(
                "skipped emit due to time patch failure");
            out.emit = out.compile;
            return out;
        }
    }

    out.compile      = validate_prepared_transfer_execution_plan(*bundle, plan);
    out.compiled_ops = prepared_transfer_execution_plan_ops(plan);
    if (out.compile.status != TransferStatus::Ok) {
        out.emit = skipped_emit_result("skipped emit due to compile failure");
        return out;
    }

    const uint32_t repeat = emit_repeat == 0U ? 1U : emit_repeat;
    for (uint32_t rep = 0; rep < repeat; ++rep) {
        out.emit = emit_prepared_bundle_webp_compiled(*bundle, plan.webp_emit,
                                                      emitter, plan.emit);
        if (out.emit.status != TransferStatus::Ok) {
            break;
        }
    }
    if (out.emit.status == TransferStatus::Ok) {
        build_webp_emit_summary_from_plan(*bundle, plan.webp_emit, plan.emit,
                                          repeat, &out.webp_chunk_summary);
    }
    return out;
}

ExecutePreparedTransferResult
emit_prepared_transfer_compiled(PreparedTransferBundle* bundle,
                                const PreparedTransferExecutionPlan& plan,
                                BmffTransferEmitter& emitter,
                                std::span<const TimePatchView> time_patches,
                                const ApplyTimePatchOptions& time_patch,
                                uint32_t emit_repeat) noexcept
{
    ExecutePreparedTransferResult out;
    if (!bundle) {
        return execute_prepared_transfer_compiled(bundle, plan);
    }

    if (!time_patches.empty()) {
        out.time_patch = apply_time_patches_view(bundle, time_patches,
                                                 time_patch);
        if (out.time_patch.status != TransferStatus::Ok) {
            out.compile = skipped_emit_result(
                "skipped emit due to time patch failure");
            out.emit = out.compile;
            return out;
        }
    }

    out.compile      = validate_prepared_transfer_execution_plan(*bundle, plan);
    out.compiled_ops = prepared_transfer_execution_plan_ops(plan);
    if (out.compile.status != TransferStatus::Ok) {
        out.emit = skipped_emit_result("skipped emit due to compile failure");
        return out;
    }

    const uint32_t repeat = emit_repeat == 0U ? 1U : emit_repeat;
    for (uint32_t rep = 0; rep < repeat; ++rep) {
        out.emit = emit_prepared_bundle_bmff_compiled(*bundle, plan.bmff_emit,
                                                      emitter, plan.emit);
        if (out.emit.status != TransferStatus::Ok) {
            break;
        }
    }
    if (out.emit.status == TransferStatus::Ok) {
        build_bmff_emit_summary_from_plan(*bundle, plan.bmff_emit, plan.emit,
                                          repeat, &out.bmff_item_summary);
    }
    return out;
}

ExecutePreparedTransferFileResult
execute_prepared_transfer_file(
    const char* path,
    const ExecutePreparedTransferFileOptions& options) noexcept
{
    ExecutePreparedTransferFileResult out;
    const bool c2pa_stage_requested = options.c2pa_stage_requested
                                      || options.c2pa_signed_package_provided;
    out.execute.c2pa_stage_requested = c2pa_stage_requested;
    out.prepared = prepare_metadata_for_target_file(path, options.prepare);

    if (out.prepared.file_status != TransferFileStatus::Ok
        || (out.prepared.prepare.status != TransferStatus::Ok
            && !c2pa_stage_requested)) {
        if (c2pa_stage_requested) {
            out.execute.c2pa_stage.status = TransferStatus::Unsupported;
            out.execute.c2pa_stage.code   = EmitTransferCode::InvalidArgument;
            out.execute.c2pa_stage.errors = 1U;
            out.execute.c2pa_stage.message
                = "skipped c2pa sign staging due to read/prepare failure";
        }
        out.execute.compile = skipped_emit_result(
            "skipped emit due to read/prepare failure");
        out.execute.emit = out.execute.compile;
        if (!options.edit_target_path.empty()
            || options.execute.edit_requested) {
            out.execute.edit_requested   = true;
            out.execute.edit_plan_status = TransferStatus::Unsupported;
            out.execute.edit_plan_message
                = "skipped edit due to read/prepare failure";
            out.execute.edit_apply.status = TransferStatus::Unsupported;
            out.execute.edit_apply.code   = EmitTransferCode::InvalidArgument;
            out.execute.edit_apply.errors = 1U;
            out.execute.edit_apply.message
                = "skipped edit due to read/prepare failure";
        }
        return out;
    }

    if (c2pa_stage_requested) {
        if (options.c2pa_signed_package_provided) {
            out.execute.c2pa_stage_validation
                = validate_prepared_c2pa_signed_package(
                    out.prepared.bundle, options.c2pa_signed_package);
            out.execute.c2pa_stage = apply_prepared_c2pa_signed_package(
                &out.prepared.bundle, options.c2pa_signed_package);
        } else {
            PreparedTransferC2paSignRequest request;
            const TransferStatus request_status
                = build_prepared_c2pa_sign_request(out.prepared.bundle,
                                                   &request);
            if (request_status != TransferStatus::Ok) {
                out.execute.c2pa_stage.status = request_status;
                out.execute.c2pa_stage.code = EmitTransferCode::InvalidArgument;
                out.execute.c2pa_stage.errors = 1U;
                out.execute.c2pa_stage.message
                    = request.message.empty()
                          ? "failed to build c2pa sign request"
                          : request.message;
                out.execute.compile = skipped_emit_result(
                    "skipped emit due to c2pa sign staging failure");
                out.execute.emit = out.execute.compile;
                if (!options.edit_target_path.empty()
                    || options.execute.edit_requested) {
                    out.execute.edit_requested   = true;
                    out.execute.edit_plan_status = TransferStatus::Unsupported;
                    out.execute.edit_plan_message
                        = "skipped edit due to c2pa sign staging failure";
                    out.execute.edit_apply.status = TransferStatus::Unsupported;
                    out.execute.edit_apply.code
                        = EmitTransferCode::InvalidArgument;
                    out.execute.edit_apply.errors = 1U;
                    out.execute.edit_apply.message
                        = "skipped edit due to c2pa sign staging failure";
                }
                return out;
            }

            out.execute.c2pa_stage_validation
                = validate_prepared_c2pa_sign_result(out.prepared.bundle,
                                                     request,
                                                     options.c2pa_signer_input);
            out.execute.c2pa_stage
                = apply_prepared_c2pa_sign_result(&out.prepared.bundle, request,
                                                  options.c2pa_signer_input);
        }
        if (out.execute.c2pa_stage.status != TransferStatus::Ok) {
            out.execute.compile = skipped_emit_result(
                "skipped emit due to c2pa sign staging failure");
            out.execute.emit = out.execute.compile;
            if (!options.edit_target_path.empty()
                || options.execute.edit_requested) {
                out.execute.edit_requested   = true;
                out.execute.edit_plan_status = TransferStatus::Unsupported;
                out.execute.edit_plan_message
                    = "skipped edit due to c2pa sign staging failure";
                out.execute.edit_apply.status = TransferStatus::Unsupported;
                out.execute.edit_apply.code = EmitTransferCode::InvalidArgument;
                out.execute.edit_apply.errors = 1U;
                out.execute.edit_apply.message
                    = "skipped edit due to c2pa sign staging failure";
            }
            return out;
        }
    }

    if (!options.edit_target_path.empty()) {
        MappedFile edit_file;
        const MappedFileStatus edit_status
            = edit_file.open(options.edit_target_path.c_str(),
                             options.prepare.policy.max_file_bytes);
        if (edit_status != MappedFileStatus::Ok) {
            ExecutePreparedTransferOptions execute_options = options.execute;
            execute_options.edit_requested                 = false;
            ExecutePreparedTransferResult execute
                = execute_prepared_transfer(&out.prepared.bundle, {},
                                            execute_options);
            execute.c2pa_stage_requested  = out.execute.c2pa_stage_requested;
            execute.c2pa_stage            = out.execute.c2pa_stage;
            execute.c2pa_stage_validation = out.execute.c2pa_stage_validation;
            out.execute                   = std::move(execute);
            out.execute.edit_requested    = true;
            out.execute.edit_plan_status  = map_mapped_file_status_to_transfer(
                edit_status);
            out.execute.edit_plan_message = edit_target_file_error(edit_status);
            out.execute.edit_apply.status = out.execute.edit_plan_status;
            out.execute.edit_apply.code   = EmitTransferCode::InvalidArgument;
            out.execute.edit_apply.errors = 1U;
            out.execute.edit_apply.message = out.execute.edit_plan_message;
            return out;
        }

        ExecutePreparedTransferOptions execute_options = options.execute;
        execute_options.edit_requested                 = true;
        ExecutePreparedTransferResult execute
            = execute_prepared_transfer(&out.prepared.bundle, edit_file.bytes(),
                                        execute_options);
        execute.c2pa_stage_requested  = out.execute.c2pa_stage_requested;
        execute.c2pa_stage            = out.execute.c2pa_stage;
        execute.c2pa_stage_validation = out.execute.c2pa_stage_validation;
        out.execute                   = std::move(execute);
        return out;
    }

    ExecutePreparedTransferResult execute
        = execute_prepared_transfer(&out.prepared.bundle, {}, options.execute);
    execute.c2pa_stage_requested  = out.execute.c2pa_stage_requested;
    execute.c2pa_stage            = out.execute.c2pa_stage;
    execute.c2pa_stage_validation = out.execute.c2pa_stage_validation;
    out.execute                   = std::move(execute);
    return out;
}

}  // namespace openmeta
