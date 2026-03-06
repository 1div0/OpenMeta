#include "openmeta/metadata_transfer.h"

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
    build_jpeg_exif_app1_payload(const MetaStore& store) noexcept
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

    static uint32_t align_to_4(uint32_t v) noexcept { return (v + 3U) & ~3U; }

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

        const uint32_t count = static_cast<uint32_t>(tags.size());
        uint32_t table_off   = 128U;
        uint32_t data_off    = table_off + 4U + count * 12U;
        data_off             = align_to_4(data_off);

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
            cursor = align_to_4(cursor);
            TagPos p;
            p.signature = t.signature;
            p.offset    = cursor;
            p.size      = static_cast<uint32_t>(t.bytes.size());
            p.bytes     = &t.bytes;
            positions.push_back(p);
            cursor += p.size;
        }

        std::vector<std::byte> profile(cursor, std::byte { 0x00 });

        for (const IccHeaderFieldItem& h : headers) {
            if (h.offset >= 128U || h.bytes.empty()) {
                continue;
            }
            const size_t avail = static_cast<size_t>(128U - h.offset);
            const size_t n     = std::min(avail, h.bytes.size());
            std::memcpy(profile.data() + h.offset, h.bytes.data(), n);
        }

        profile[36] = std::byte { 'a' };
        profile[37] = std::byte { 'c' };
        profile[38] = std::byte { 's' };
        profile[39] = std::byte { 'p' };

        put_u32be(&profile, 128U, count);
        uint32_t table_pos = 132U;
        for (const TagPos& p : positions) {
            put_u32be(&profile, table_pos + 0U, p.signature);
            put_u32be(&profile, table_pos + 4U, p.offset);
            put_u32be(&profile, table_pos + 8U, p.size);
            table_pos += 12U;
        }
        for (const TagPos& p : positions) {
            if (!p.bytes || p.bytes->empty()) {
                continue;
            }
            std::memcpy(profile.data() + p.offset, p.bytes->data(),
                        p.bytes->size());
        }

        put_u32be(&profile, 0U, static_cast<uint32_t>(profile.size()));

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

}  // namespace

PrepareTransferResult
prepare_metadata_for_target(const MetaStore& store,
                            const PrepareTransferRequest& request,
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

    if (request.target_format != TransferTargetFormat::Jpeg) {
        r.status    = TransferStatus::Unsupported;
        r.code      = PrepareTransferCode::UnsupportedTargetFormat;
        r.errors    = 1U;
        r.message   = "prepare currently supports jpeg target only";
        *out_bundle = std::move(bundle);
        return r;
    }

    const bool has_exif = has_kind(store, MetaKeyKind::ExifTag);
    const bool has_iptc = has_kind(store, MetaKeyKind::IptcDataset)
                          || has_kind(store, MetaKeyKind::PhotoshopIrb);
    const bool has_icc = has_kind(store, MetaKeyKind::IccHeaderField)
                         || has_kind(store, MetaKeyKind::IccTag);

    bool requested_present_but_unpacked = false;

    if (request.include_exif_app1 && has_exif) {
        ExifPackBuild exif_build = build_jpeg_exif_app1_payload(store);
        if (exif_build.produced && !exif_build.app1_payload.empty()) {
            const uint32_t block_index = static_cast<uint32_t>(
                bundle.blocks.size());
            PreparedTransferBlock b;
            b.kind    = TransferBlockKind::Exif;
            b.order   = 100U;
            b.route   = "jpeg:app1-exif";
            b.payload = std::move(exif_build.app1_payload);
            bundle.blocks.push_back(std::move(b));
            for (size_t i = 0; i < exif_build.time_patch_map.size(); ++i) {
                TimePatchSlot slot = exif_build.time_patch_map[i];
                slot.block_index   = block_index;
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
            append_message(&r.message,
                           "exif app1 packer could not serialize current exif "
                           "set");
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
            b.route = "jpeg:app1-xmp";
            append_ascii_bytes(&b.payload, "http://ns.adobe.com/xap/1.0/");
            b.payload.push_back(std::byte { 0x00 });
            b.payload.insert(b.payload.end(), xmp_packet.begin(),
                             xmp_packet.end());
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
                           "icc app2 packer could not serialize current icc "
                           "set");
        }
    }
    if (request.include_iptc_app13 && has_iptc) {
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

    uint32_t replaced_segments = 0;
    for (size_t i = 0; i < scan.leading_segments.size(); ++i) {
        const ExistingJpegSegment& e = scan.leading_segments[i];
        if (!e.route_known) {
            continue;
        }
        if (!route_in_desired(e.route, desired)) {
            continue;
        }
        replaced_segments += 1U;
        replaced_bytes += static_cast<uint64_t>(4U + e.payload_len);
    }

    uint64_t added_bytes = 0;
    for (size_t i = 0; i < desired.size(); ++i) {
        added_bytes += static_cast<uint64_t>(4U + desired[i].payload.size());
    }

    plan.replaced_segments = replaced_segments;
    plan.appended_segments = static_cast<uint32_t>(desired.size());
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
        const bool replaced          = e.route_known
                              && route_in_desired(e.route, desired);
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
    out.prepare     = prepare_metadata_for_target(store, options.prepare,
                                                  &out.bundle);
    return out;
}

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
        const TimePatchUpdate& update = updates[ui];
        bool matched_any              = false;

        for (size_t si = 0; si < bundle->time_patch_map.size(); ++si) {
            const TimePatchSlot& slot = bundle->time_patch_map[si];
            if (slot.field != update.field) {
                continue;
            }
            matched_any = true;

            if (slot.block_index >= bundle->blocks.size()) {
                out.errors += 1U;
                append_message(&out.message,
                               std::string(
                                   "time patch slot block out of range: ")
                                   + time_patch_field_name(slot.field));
                continue;
            }
            PreparedTransferBlock& block = bundle->blocks[slot.block_index];
            const uint32_t width         = static_cast<uint32_t>(slot.width);
            if (width == 0U) {
                out.errors += 1U;
                append_message(&out.message,
                               std::string("time patch slot width is zero: ")
                                   + time_patch_field_name(slot.field));
                continue;
            }
            if (slot.byte_offset + width > block.payload.size()) {
                out.errors += 1U;
                append_message(&out.message,
                               std::string(
                                   "time patch slot out of payload bounds: ")
                                   + time_patch_field_name(slot.field));
                continue;
            }
            if (options.strict_width && update.value.size() != width) {
                out.errors += 1U;
                append_message(&out.message,
                               std::string("time patch width mismatch: ")
                                   + time_patch_field_name(slot.field));
                continue;
            }

            const uint32_t copy_n = static_cast<uint32_t>(
                std::min(update.value.size(), static_cast<size_t>(width)));
            if (copy_n > 0U) {
                std::memcpy(block.payload.data() + slot.byte_offset,
                            update.value.data(), copy_n);
            }
            if (copy_n < width) {
                std::memset(block.payload.data() + slot.byte_offset + copy_n, 0,
                            width - copy_n);
            }
            out.patched_slots += 1U;
        }

        if (!matched_any) {
            if (options.require_slot) {
                out.errors += 1U;
                append_message(&out.message,
                               std::string("time patch slot not found: ")
                                   + time_patch_field_name(update.field));
            } else {
                out.skipped_slots += 1U;
            }
        }
    }

    if (out.errors == 0U) {
        out.status = TransferStatus::Ok;
    } else {
        out.status = TransferStatus::InvalidArgument;
    }
    return out;
}

}  // namespace openmeta
