// SPDX-License-Identifier: Apache-2.0

#include "openmeta/build_info.h"
#include "openmeta/ccm_query.h"
#include "openmeta/console_format.h"
#include "openmeta/container_payload.h"
#include "openmeta/exr_adapter.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/geotiff_key_names.h"
#include "openmeta/icc_interpret.h"
#include "openmeta/interop_export.h"
#include "openmeta/mapped_file.h"
#include "openmeta/metadata_transfer.h"
#include "openmeta/ocio_adapter.h"
#include "openmeta/oiio_adapter.h"
#include "openmeta/resource_policy.h"
#include "openmeta/simple_meta.h"
#include "openmeta/validate.h"
#include "openmeta/xmp_dump.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace openmeta {
namespace {

    static nb::str sv_to_py(std::string_view s)
    {
        return nb::str(s.data(), s.size());
    }


    static std::pair<std::string, std::string> info_lines()
    {
        std::string line1;
        std::string line2;
        format_build_info_lines(&line1, &line2);
        return { std::move(line1), std::move(line2) };
    }


    static std::pair<nb::bytes, XmpDumpResult>
    dump_xmp_sidecar_to_python(const MetaStore& store,
                               const XmpSidecarRequest& request)
    {
        std::vector<std::byte> out;
        XmpDumpResult res;
        {
            nb::gil_scoped_release gil_release;
            res = dump_xmp_sidecar(store, &out, request);
        }

        if (res.status != XmpDumpStatus::Ok) {
            throw std::runtime_error("XMP dump failed");
        }

        const size_t n = out.size();
        nb::bytes b(reinterpret_cast<const char*>(out.data()), n);
        return std::make_pair(b, res);
    }


    static XmpSidecarRequest make_xmp_sidecar_request(
        XmpSidecarFormat format, uint64_t max_output_bytes,
        uint32_t max_entries, bool include_exif, bool include_iptc,
        bool include_existing_xmp, bool portable_exiftool_gpsdatetime_alias,
        XmpConflictPolicy portable_conflict_policy, bool include_origin,
        bool include_wire, bool include_flags, bool include_names)
    {
        XmpSidecarRequest request;
        request.format                  = format;
        request.limits.max_output_bytes = max_output_bytes;
        request.limits.max_entries      = max_entries;
        request.include_exif            = include_exif;
        request.include_iptc            = include_iptc;
        request.include_existing_xmp    = include_existing_xmp;
        request.portable_conflict_policy = portable_conflict_policy;
        request.portable_exiftool_gpsdatetime_alias
            = portable_exiftool_gpsdatetime_alias;
        request.include_origin = include_origin;
        request.include_wire   = include_wire;
        request.include_flags  = include_flags;
        request.include_names  = include_names;
        return request;
    }


    static std::string python_info_line()
    {
        const char* ver = Py_GetVersion();
        size_t n        = 0;
        while (ver && ver[n] && ver[n] != ' ') {
            n += 1;
        }

        std::string out;
        out.reserve(96);
        out.append("Python ");
        if (ver && n != 0U) {
            out.append(ver, n);
        } else {
            out.append("unknown");
        }
        out.append(" nanobind ");

        char buf[64];
#if defined(NB_VERSION_DEV) && (NB_VERSION_DEV > 0)
        std::snprintf(buf, sizeof(buf), "%d.%d.%d-dev%d", NB_VERSION_MAJOR,
                      NB_VERSION_MINOR, NB_VERSION_PATCH, NB_VERSION_DEV);
#else
        std::snprintf(buf, sizeof(buf), "%d.%d.%d", NB_VERSION_MAJOR,
                      NB_VERSION_MINOR, NB_VERSION_PATCH);
#endif
        out.append(buf);
        return out;
    }


    static std::string arena_string(const ByteArena& arena,
                                    ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string(reinterpret_cast<const char*>(bytes.data()),
                           bytes.size());
    }


    static const char* exr_adapter_status_name(ExrAdapterStatus status) noexcept
    {
        switch (status) {
            case ExrAdapterStatus::Ok: return "ok";
            case ExrAdapterStatus::InvalidArgument:
                return "invalid_argument";
            case ExrAdapterStatus::Unsupported: return "unsupported";
        }
        return "unknown";
    }


    static std::vector<std::byte> bytes_object_to_vector(nb::object obj)
    {
        std::vector<std::byte> out;
        if (obj.is_none()) {
            return out;
        }
        const nb::bytes value = nb::cast<nb::bytes>(obj);
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(value.data()), value.size());
        out.assign(bytes.begin(), bytes.end());
        return out;
    }


    static std::pair<std::string, bool> console_text(nb::bytes data,
                                                     uint32_t max_bytes)
    {
        const std::string_view s(reinterpret_cast<const char*>(data.data()),
                                 data.size());
        std::string out;
        const bool dangerous = append_console_escaped_ascii(s, max_bytes, &out);
        return { std::move(out), dangerous };
    }


    static std::string hex_bytes(nb::bytes data, uint32_t max_bytes)
    {
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(data.data()), data.size());
        std::string out;
        out.append("0x");
        append_hex_bytes(bytes, max_bytes, &out);
        return out;
    }


    static nb::str unsafe_text(nb::bytes data, uint32_t max_bytes)
    {
        size_t n = data.size();
        if (max_bytes != 0U && n > static_cast<size_t>(max_bytes)) {
            n = static_cast<size_t>(max_bytes);
        }
        PyObject* s
            = PyUnicode_DecodeLatin1(reinterpret_cast<const char*>(data.data()),
                                     static_cast<Py_ssize_t>(n), nullptr);
        if (!s) {
            nb::raise_python_error();
        }
        return nb::steal<nb::str>(nb::handle(s));
    }


    static std::vector<std::byte> read_file_bytes(const char* path,
                                                  uint64_t max_file_bytes)
    {
        if (!path || !*path) {
            throw std::runtime_error("empty path");
        }
        std::FILE* f = std::fopen(path, "rb");
        if (!f) {
            throw std::runtime_error("failed to open file");
        }

        std::vector<std::byte> bytes;
        if (std::fseek(f, 0, SEEK_END) != 0) {
            std::fclose(f);
            throw std::runtime_error("failed to seek file");
        }
        const long size_long = std::ftell(f);
        if (size_long < 0) {
            std::fclose(f);
            throw std::runtime_error("failed to stat file");
        }
        const uint64_t size = static_cast<uint64_t>(size_long);
        if (max_file_bytes != 0U && size > max_file_bytes) {
            std::fclose(f);
            throw std::runtime_error("file too large");
        }
        if (std::fseek(f, 0, SEEK_SET) != 0) {
            std::fclose(f);
            throw std::runtime_error("failed to rewind file");
        }

        bytes.resize(static_cast<size_t>(size));
        const size_t n = std::fread(bytes.data(), 1, bytes.size(), f);
        std::fclose(f);

        if (n != bytes.size()) {
            throw std::runtime_error("failed to read file");
        }
        return bytes;
    }


    static nb::object scalar_to_python(const MetaValue& v)
    {
        switch (v.elem_type) {
        case MetaElementType::U8:
        case MetaElementType::U16:
        case MetaElementType::U32:
        case MetaElementType::U64: return nb::int_(v.data.u64);
        case MetaElementType::I8:
        case MetaElementType::I16:
        case MetaElementType::I32:
        case MetaElementType::I64: return nb::int_(v.data.i64);
        case MetaElementType::F32: {
            const float f = std::bit_cast<float>(v.data.f32_bits);
            return nb::float_(static_cast<double>(f));
        }
        case MetaElementType::F64: {
            const double f = std::bit_cast<double>(v.data.f64_bits);
            return nb::float_(f);
        }
        case MetaElementType::URational:
            return nb::make_tuple(nb::int_(v.data.ur.numer),
                                  nb::int_(v.data.ur.denom));
        case MetaElementType::SRational:
            return nb::make_tuple(nb::int_(v.data.sr.numer),
                                  nb::int_(v.data.sr.denom));
        }
        return nb::none();
    }


    template<typename T>
    static std::span<const T> array_span(const ByteArena& arena,
                                         const MetaValue& v)
    {
        const std::span<const std::byte> bytes = arena.span(v.data.span);
        if (bytes.size() != static_cast<size_t>(v.count) * sizeof(T)) {
            return {};
        }
        return std::span<const T>(reinterpret_cast<const T*>(bytes.data()),
                                  static_cast<size_t>(v.count));
    }


    static nb::object value_to_python(const ByteArena& arena,
                                      const MetaValue& v, uint32_t max_elements,
                                      uint32_t max_bytes)
    {
        switch (v.kind) {
        case MetaValueKind::Empty: return nb::none();
        case MetaValueKind::Scalar: return scalar_to_python(v);
        case MetaValueKind::Text:
        case MetaValueKind::Bytes: {
            const std::span<const std::byte> bytes = arena.span(v.data.span);
            const size_t n = (max_bytes != 0U && bytes.size() > max_bytes)
                                 ? static_cast<size_t>(max_bytes)
                                 : bytes.size();
            return nb::bytes(reinterpret_cast<const char*>(bytes.data()), n);
        }
        case MetaValueKind::Array: break;
        }

        const uint32_t n = (max_elements != 0U && v.count > max_elements)
                               ? max_elements
                               : v.count;
        nb::list out;

        switch (v.elem_type) {
        case MetaElementType::U8: {
            const std::span<const uint8_t> s = array_span<uint8_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::I8: {
            const std::span<const int8_t> s = array_span<int8_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::U16: {
            const std::span<const uint16_t> s = array_span<uint16_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::I16: {
            const std::span<const int16_t> s = array_span<int16_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::U32: {
            const std::span<const uint32_t> s = array_span<uint32_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::I32: {
            const std::span<const int32_t> s = array_span<int32_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::U64: {
            const std::span<const uint64_t> s = array_span<uint64_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::I64: {
            const std::span<const int64_t> s = array_span<int64_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::int_(s[i]));
            }
            break;
        }
        case MetaElementType::F32: {
            const std::span<const uint32_t> s = array_span<uint32_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::float_(
                    static_cast<double>(std::bit_cast<float>(s[i]))));
            }
            break;
        }
        case MetaElementType::F64: {
            const std::span<const uint64_t> s = array_span<uint64_t>(arena, v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(nb::float_(std::bit_cast<double>(s[i])));
            }
            break;
        }
        case MetaElementType::URational: {
            const std::span<const URational> s = array_span<URational>(arena,
                                                                       v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(
                    nb::make_tuple(nb::int_(s[i].numer), nb::int_(s[i].denom)));
            }
            break;
        }
        case MetaElementType::SRational: {
            const std::span<const SRational> s = array_span<SRational>(arena,
                                                                       v);
            for (uint32_t i = 0; i < n && i < s.size(); ++i) {
                out.append(
                    nb::make_tuple(nb::int_(s[i].numer), nb::int_(s[i].denom)));
            }
            break;
        }
        }

        return out;
    }


    class NameCollectSink final : public MetadataSink {
    public:
        explicit NameCollectSink(std::vector<std::string>* out) noexcept
            : out_(out)
        {
        }

        void on_item(const ExportItem& item) noexcept override
        {
            if (!out_) {
                return;
            }
            out_->emplace_back(item.name.data(), item.name.size());
        }

    private:
        std::vector<std::string>* out_ = nullptr;
    };


    static std::vector<std::string> export_names(const MetaStore& store,
                                                 const ExportOptions& options)
    {
        std::vector<std::string> out;
        NameCollectSink sink(&out);
        visit_metadata(store, options, sink);
        return out;
    }

    static nb::dict ccm_field_to_python(const CcmField& field)
    {
        nb::dict d;
        d["name"] = nb::str(field.name.c_str(), field.name.size());
        d["ifd"]  = nb::str(field.ifd.c_str(), field.ifd.size());
        d["tag"]  = nb::int_(field.tag);
        d["rows"] = nb::int_(field.rows);
        d["cols"] = nb::int_(field.cols);
        nb::list values;
        for (size_t i = 0; i < field.values.size(); ++i) {
            values.append(nb::float_(field.values[i]));
        }
        d["values"] = std::move(values);
        return d;
    }

    static nb::dict ccm_issue_to_python(const CcmIssue& issue)
    {
        nb::dict d;
        d["severity"] = issue.severity;
        d["code"]     = issue.code;
        d["ifd"]      = nb::str(issue.ifd.c_str(), issue.ifd.size());
        d["name"]     = nb::str(issue.name.c_str(), issue.name.size());
        d["tag"]      = nb::int_(issue.tag);
        d["message"]  = nb::str(issue.message.c_str(), issue.message.size());
        return d;
    }

    static nb::dict collect_dng_ccm_to_python(const MetaStore& store,
                                              bool require_dng_context,
                                              bool include_reduction_matrices,
                                              uint32_t max_fields,
                                              uint32_t max_values_per_field,
                                              CcmValidationMode validation_mode)
    {
        CcmQueryOptions options;
        options.require_dng_context         = require_dng_context;
        options.include_reduction_matrices  = include_reduction_matrices;
        options.validation_mode             = validation_mode;
        options.limits.max_fields           = max_fields;
        options.limits.max_values_per_field = max_values_per_field;

        std::vector<CcmField> fields;
        std::vector<CcmIssue> issues;
        const CcmQueryResult result = collect_dng_ccm_fields(store, &fields,
                                                             options, &issues);

        nb::list out_fields;
        for (size_t i = 0; i < fields.size(); ++i) {
            out_fields.append(ccm_field_to_python(fields[i]));
        }
        nb::list out_issues;
        for (size_t i = 0; i < issues.size(); ++i) {
            out_issues.append(ccm_issue_to_python(issues[i]));
        }

        nb::dict out;
        out["status"]          = result.status;
        out["fields_found"]    = nb::int_(result.fields_found);
        out["fields_dropped"]  = nb::int_(result.fields_dropped);
        out["issues_reported"] = nb::int_(result.issues_reported);
        out["fields"]          = std::move(out_fields);
        out["issues"]          = std::move(out_issues);
        return out;
    }

    static nb::dict validate_issue_to_python(const ValidateIssue& issue)
    {
        nb::dict d;
        d["severity"] = issue.severity;
        d["category"] = nb::str(issue.category.c_str(), issue.category.size());
        d["code"]     = nb::str(issue.code.c_str(), issue.code.size());
        d["ifd"]      = nb::str(issue.ifd.c_str(), issue.ifd.size());
        d["name"]     = nb::str(issue.name.c_str(), issue.name.size());
        d["tag"]      = nb::int_(issue.tag);
        d["message"]  = nb::str(issue.message.c_str(), issue.message.size());
        return d;
    }

    static nb::dict validate_file_to_python(
        const std::string& path, bool include_pointer_tags,
        bool decode_makernote, bool decode_printim, bool decompress,
        bool include_xmp_sidecar, bool verify_c2pa,
        C2paVerifyBackend verify_backend,
        bool verify_require_resolved_references, bool warnings_as_errors,
        bool ccm_require_dng_context, bool ccm_include_reduction_matrices,
        uint32_t ccm_max_fields, uint32_t ccm_max_values_per_field,
        CcmValidationMode ccm_validation_mode, uint64_t max_file_bytes,
        nb::object policy_obj)
    {
        ValidateOptions options;
        options.include_pointer_tags = include_pointer_tags;
        options.decode_makernote     = decode_makernote;
        options.decode_printim       = decode_printim;
        options.decompress           = decompress;
        options.include_xmp_sidecar  = include_xmp_sidecar;
        options.verify_c2pa          = verify_c2pa;
        options.verify_backend       = verify_backend;
        options.verify_require_resolved_references
            = verify_require_resolved_references;
        options.warnings_as_errors             = warnings_as_errors;
        options.ccm.require_dng_context        = ccm_require_dng_context;
        options.ccm.include_reduction_matrices = ccm_include_reduction_matrices;
        options.ccm.limits.max_fields          = ccm_max_fields;
        options.ccm.limits.max_values_per_field = ccm_max_values_per_field;
        options.ccm.validation_mode             = ccm_validation_mode;
        options.policy.max_file_bytes           = max_file_bytes;
        if (!policy_obj.is_none()) {
            options.policy = nb::cast<OpenMetaResourcePolicy>(policy_obj);
            if (max_file_bytes != 0U) {
                options.policy.max_file_bytes = max_file_bytes;
            }
        }

        ValidateResult result;
        {
            nb::gil_scoped_release gil_release;
            result = validate_file(path.c_str(), options);
        }

        nb::list issues;
        for (size_t i = 0; i < result.issues.size(); ++i) {
            issues.append(validate_issue_to_python(result.issues[i]));
        }

        nb::dict out;
        out["status"]               = result.status;
        out["file_size"]            = nb::int_(result.file_size);
        out["scan_status"]          = result.read.scan.status;
        out["payload_status"]       = result.read.payload.status;
        out["exif_status"]          = result.read.exif.status;
        out["xmp_status"]           = result.read.xmp.status;
        out["exr_status"]           = result.read.exr.status;
        out["jumbf_status"]         = result.read.jumbf.status;
        out["jumbf_verify_status"]  = result.read.jumbf.verify_status;
        out["jumbf_verify_backend"] = result.read.jumbf.verify_backend_selected;
        out["jumbf_verify_require_resolved_references"] = nb::bool_(
            verify_require_resolved_references);
        out["entries"]             = nb::int_(result.entries);
        out["ccm_status"]          = result.ccm.status;
        out["ccm_fields"]          = nb::int_(result.ccm_fields);
        out["ccm_fields_found"]    = nb::int_(result.ccm.fields_found);
        out["ccm_fields_dropped"]  = nb::int_(result.ccm.fields_dropped);
        out["ccm_issues_reported"] = nb::int_(result.ccm.issues_reported);
        out["warning_count"]       = nb::int_(result.warning_count);
        out["error_count"]         = nb::int_(result.error_count);
        out["failed"]              = nb::bool_(result.failed);
        out["issues"]              = std::move(issues);
        return out;
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

    static const char*
    transfer_policy_subject_name(TransferPolicySubject subject) noexcept
    {
        switch (subject) {
        case TransferPolicySubject::MakerNote: return "makernote";
        case TransferPolicySubject::Jumbf: return "jumbf";
        case TransferPolicySubject::C2pa: return "c2pa";
        case TransferPolicySubject::XmpExifProjection:
            return "xmp_exif_projection";
        case TransferPolicySubject::XmpIptcProjection:
            return "xmp_iptc_projection";
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
        case TransferC2paRewriteChunkKind::PreparedJxlBox:
            return "prepared_jxl_box";
        case TransferC2paRewriteChunkKind::PreparedBmffMetaBox:
            return "prepared_bmff_meta_box";
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
        case TransferTargetFormat::Dng: return "dng";
        case TransferTargetFormat::Jxl: return "jxl";
        case TransferTargetFormat::Webp: return "webp";
        case TransferTargetFormat::Heif: return "heif";
        case TransferTargetFormat::Avif: return "avif";
        case TransferTargetFormat::Cr3: return "cr3";
        case TransferTargetFormat::Exr: return "exr";
        case TransferTargetFormat::Png: return "png";
        case TransferTargetFormat::Jp2: return "jp2";
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
        case TransferAdapterOpKind::PngChunk: return "png_chunk";
        case TransferAdapterOpKind::Jp2Box: return "jp2_box";
        case TransferAdapterOpKind::ExrAttribute: return "exr_attribute";
        case TransferAdapterOpKind::BmffItem: return "bmff_item";
        case TransferAdapterOpKind::BmffProperty: return "bmff_property";
        }
        return "unknown";
    }

    static TransferStatus
    transfer_status_from_file_status(TransferFileStatus status) noexcept
    {
        switch (status) {
        case TransferFileStatus::Ok: return TransferStatus::Ok;
        case TransferFileStatus::InvalidArgument:
            return TransferStatus::InvalidArgument;
        case TransferFileStatus::TooLarge: return TransferStatus::LimitExceeded;
        case TransferFileStatus::OpenFailed:
        case TransferFileStatus::StatFailed:
        case TransferFileStatus::MapFailed:
        case TransferFileStatus::ReadFailed: return TransferStatus::Unsupported;
        }
        return TransferStatus::InternalError;
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

    static std::vector<TransferTimePatchInput>
    parse_time_patches_object(nb::object time_patches_obj)
    {
        std::vector<TransferTimePatchInput> out;
        if (time_patches_obj.is_none()) {
            return out;
        }

        nb::dict patch_dict = nb::cast<nb::dict>(time_patches_obj);
        PyObject* dict_obj  = patch_dict.ptr();
        if (!dict_obj || !PyDict_Check(dict_obj)) {
            throw std::runtime_error(
                "time_patches must be a dict[str, str|bytes]");
        }

        PyObject* py_key   = nullptr;
        PyObject* py_value = nullptr;
        Py_ssize_t pos     = 0;
        while (PyDict_Next(dict_obj, &pos, &py_key, &py_value) != 0) {
            if (!PyUnicode_Check(py_key)) {
                throw std::runtime_error(
                    "time_patches key must be str field name");
            }
            Py_ssize_t key_n  = 0;
            const char* key_s = PyUnicode_AsUTF8AndSize(py_key, &key_n);
            if (!key_s || key_n <= 0) {
                throw std::runtime_error("invalid time patch field name");
            }

            TransferTimePatchInput one;
            if (!parse_time_patch_field(
                    std::string_view(key_s, static_cast<size_t>(key_n)),
                    &one.field)) {
                throw std::runtime_error("unknown time patch field");
            }

            if (PyUnicode_Check(py_value)) {
                Py_ssize_t n  = 0;
                const char* s = PyUnicode_AsUTF8AndSize(py_value, &n);
                if (!s || n < 0) {
                    throw std::runtime_error(
                        "failed to encode time patch text value as UTF-8");
                }
                one.text_value = true;
                one.value.resize(static_cast<size_t>(n));
                for (size_t i = 0; i < one.value.size(); ++i) {
                    one.value[i] = static_cast<std::byte>(
                        static_cast<unsigned char>(s[i]));
                }
            } else if (PyBytes_Check(py_value)) {
                char* s      = nullptr;
                Py_ssize_t n = 0;
                if (PyBytes_AsStringAndSize(py_value, &s, &n) != 0) {
                    throw std::runtime_error(
                        "failed to read bytes time patch value");
                }
                if (!s || n < 0) {
                    throw std::runtime_error("invalid bytes time patch value");
                }
                one.text_value = false;
                one.value.resize(static_cast<size_t>(n));
                for (size_t i = 0; i < one.value.size(); ++i) {
                    one.value[i] = static_cast<std::byte>(
                        static_cast<unsigned char>(s[i]));
                }
            } else {
                throw std::runtime_error(
                    "time_patches value must be str or bytes");
            }

            out.push_back(std::move(one));
        }
        return out;
    }

    static nb::dict
    inspect_transfer_payload_batch_to_python(nb::object payload_batch_obj,
                                             bool include_payloads,
                                             bool unsafe_payload_access)
    {
        if (payload_batch_obj.is_none()) {
            throw std::runtime_error(
                "transfer_payload_batch must be bytes-like");
        }

        const std::vector<std::byte> payload_batch_bytes
            = bytes_object_to_vector(payload_batch_obj);
        PreparedTransferPayloadBatch batch;
        const PreparedTransferPayloadIoResult batch_io
            = deserialize_prepared_transfer_payload_batch(
                std::span<const std::byte>(payload_batch_bytes.data(),
                                           payload_batch_bytes.size()),
                &batch);

        std::vector<PreparedTransferPayloadView> payload_views;
        EmitTransferResult inspect_result;
        if (batch_io.status == TransferStatus::Ok) {
            inspect_result
                = collect_prepared_transfer_payload_views(batch,
                                                          &payload_views);
        } else {
            inspect_result.status  = batch_io.status;
            inspect_result.code    = batch_io.code;
            inspect_result.errors  = batch_io.errors;
            inspect_result.message = batch_io.message;
        }

        nb::dict out;
        out["requested_payload_bytes"] = nb::bool_(include_payloads);
        out["status"]                  = batch_io.status;
        out["status_name"]   = nb::str(transfer_status_name(batch_io.status));
        out["code"]          = batch_io.code;
        out["code_name"]     = nb::str(emit_transfer_code_name(batch_io.code));
        out["bytes_read"]    = nb::int_(batch_io.bytes);
        out["errors"]        = nb::int_(batch_io.errors);
        out["message"]       = nb::str(batch_io.message.c_str(),
                                       batch_io.message.size());
        out["target_format"] = batch.target_format;
        out["target_format_name"] = nb::str(
            transfer_target_format_name(batch.target_format));
        out["payload_count"] = nb::int_(payload_views.size());

        const bool allow_payload_bytes = include_payloads
                                         && unsafe_payload_access;
        nb::list payloads;
        for (size_t i = 0; i < payload_views.size(); ++i) {
            const PreparedTransferPayloadView& view = payload_views[i];
            nb::dict one;
            one["index"]         = nb::int_(static_cast<uint32_t>(i));
            one["semantic_kind"] = nb::int_(
                static_cast<uint32_t>(view.semantic_kind));
            one["semantic_name"] = nb::str(view.semantic_name.data(),
                                           view.semantic_name.size());
            one["route"] = nb::str(view.route.data(), view.route.size());
            one["size"]  = nb::int_(static_cast<uint64_t>(view.payload.size()));
            one["op_kind"]      = nb::int_(static_cast<uint32_t>(view.op.kind));
            one["op_kind_name"] = nb::str(
                transfer_adapter_op_kind_name(view.op.kind));
            one["jpeg_marker_code"]      = nb::int_(view.op.jpeg_marker_code);
            one["tiff_tag"]              = nb::int_(view.op.tiff_tag);
            one["box_type"]              = nb::str(view.op.box_type.data(),
                                                   view.op.box_type.size());
            one["chunk_type"]            = nb::str(view.op.chunk_type.data(),
                                                   view.op.chunk_type.size());
            one["bmff_item_type"]        = nb::int_(view.op.bmff_item_type);
            one["bmff_property_type"]    = nb::int_(view.op.bmff_property_type);
            one["bmff_property_subtype"] = nb::int_(
                view.op.bmff_property_subtype);
            one["bmff_mime_xmp"] = nb::bool_(view.op.bmff_mime_xmp);
            one["compress"]      = nb::bool_(view.op.compress);
            if (allow_payload_bytes) {
                one["payload"] = nb::bytes(reinterpret_cast<const char*>(
                                               view.payload.data()),
                                           view.payload.size());
            } else {
                one["payload"] = nb::none();
            }
            payloads.append(std::move(one));
        }
        out["payloads"] = std::move(payloads);

        TransferStatus overall_status = TransferStatus::Ok;
        std::string error_stage       = "none";
        std::string error_code        = "none";
        std::string error_message;
        if (include_payloads && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_payloads_forbidden";
            error_message
                = "safe inspect_transfer_payload_batch forbids payload bytes; "
                  "use unsafe_inspect_transfer_payload_batch("
                  "include_payloads=True)";
        } else if (batch_io.status != TransferStatus::Ok) {
            overall_status = batch_io.status;
            error_stage    = "payload_batch";
            error_code     = emit_transfer_code_name(batch_io.code);
            error_message  = batch_io.message;
        } else if (inspect_result.status != TransferStatus::Ok) {
            overall_status = inspect_result.status;
            error_stage    = "inspect";
            error_code     = emit_transfer_code_name(inspect_result.code);
            error_message  = inspect_result.message;
        }
        out["overall_status"]      = overall_status;
        out["overall_status_name"] = nb::str(
            transfer_status_name(overall_status));
        out["error_stage"]   = nb::str(error_stage.c_str(), error_stage.size());
        out["error_code"]    = nb::str(error_code.c_str(), error_code.size());
        out["error_message"] = nb::str(error_message.c_str(),
                                       error_message.size());
        return out;
    }

    static const char*
    transfer_package_chunk_kind_name(TransferPackageChunkKind kind) noexcept
    {
        switch (kind) {
        case TransferPackageChunkKind::SourceRange: return "source_range";
        case TransferPackageChunkKind::InlineBytes: return "inline_bytes";
        case TransferPackageChunkKind::PreparedTransferBlock:
            return "prepared_transfer_block";
        case TransferPackageChunkKind::PreparedJpegSegment:
            return "prepared_jpeg_segment";
        }
        return "unknown";
    }

    static nb::dict
    inspect_transfer_package_batch_to_python(nb::object package_batch_obj,
                                             bool include_chunk_bytes,
                                             bool unsafe_payload_access)
    {
        if (package_batch_obj.is_none()) {
            throw std::runtime_error(
                "transfer_package_batch must be bytes-like");
        }

        const std::vector<std::byte> package_batch_bytes
            = bytes_object_to_vector(package_batch_obj);
        PreparedTransferPackageBatch batch;
        const PreparedTransferPackageIoResult batch_io
            = deserialize_prepared_transfer_package_batch(
                std::span<const std::byte>(package_batch_bytes.data(),
                                           package_batch_bytes.size()),
                &batch);

        std::vector<PreparedTransferPackageView> chunk_views;
        EmitTransferResult inspect_result;
        if (batch_io.status == TransferStatus::Ok) {
            inspect_result
                = collect_prepared_transfer_package_views(batch, &chunk_views);
        } else {
            inspect_result.status  = batch_io.status;
            inspect_result.code    = batch_io.code;
            inspect_result.errors  = batch_io.errors;
            inspect_result.message = batch_io.message;
        }

        nb::dict out;
        out["requested_chunk_bytes"] = nb::bool_(include_chunk_bytes);
        out["status"]                = batch_io.status;
        out["status_name"]   = nb::str(transfer_status_name(batch_io.status));
        out["code"]          = batch_io.code;
        out["code_name"]     = nb::str(emit_transfer_code_name(batch_io.code));
        out["bytes_read"]    = nb::int_(batch_io.bytes);
        out["errors"]        = nb::int_(batch_io.errors);
        out["message"]       = nb::str(batch_io.message.c_str(),
                                       batch_io.message.size());
        out["target_format"] = batch.target_format;
        out["target_format_name"] = nb::str(
            transfer_target_format_name(batch.target_format));
        out["chunk_count"] = nb::int_(chunk_views.size());

        const bool allow_chunk_bytes = include_chunk_bytes
                                       && unsafe_payload_access;
        nb::list chunks;
        for (size_t i = 0; i < chunk_views.size(); ++i) {
            const PreparedTransferPackageView& view = chunk_views[i];
            nb::dict one;
            one["index"]         = nb::int_(static_cast<uint32_t>(i));
            one["semantic_kind"] = nb::int_(
                static_cast<uint32_t>(view.semantic_kind));
            const std::string_view semantic_name = transfer_semantic_name(
                view.semantic_kind);
            one["semantic_name"] = nb::str(semantic_name.data(),
                                           semantic_name.size());
            one["route"]        = nb::str(view.route.data(), view.route.size());
            one["package_kind"] = nb::int_(
                static_cast<uint32_t>(view.package_kind));
            one["package_kind_name"] = nb::str(
                transfer_package_chunk_kind_name(view.package_kind));
            one["output_offset"]    = nb::int_(view.output_offset);
            one["jpeg_marker_code"] = nb::int_(view.jpeg_marker_code);
            one["size"] = nb::int_(static_cast<uint64_t>(view.bytes.size()));
            if (allow_chunk_bytes) {
                one["bytes"] = nb::bytes(reinterpret_cast<const char*>(
                                             view.bytes.data()),
                                         view.bytes.size());
            } else {
                one["bytes"] = nb::none();
            }
            chunks.append(std::move(one));
        }
        out["chunks"] = std::move(chunks);

        TransferStatus overall_status = TransferStatus::Ok;
        std::string error_stage       = "none";
        std::string error_code        = "none";
        std::string error_message;
        if (include_chunk_bytes && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_chunk_bytes_forbidden";
            error_message
                = "safe inspect_transfer_package_batch forbids chunk bytes; "
                  "use unsafe_inspect_transfer_package_batch("
                  "include_chunk_bytes=True)";
        } else if (batch_io.status != TransferStatus::Ok) {
            overall_status = batch_io.status;
            error_stage    = "package_batch";
            error_code     = emit_transfer_code_name(batch_io.code);
            error_message  = batch_io.message;
        } else if (inspect_result.status != TransferStatus::Ok) {
            overall_status = inspect_result.status;
            error_stage    = "inspect";
            error_code     = emit_transfer_code_name(inspect_result.code);
            error_message  = inspect_result.message;
        }
        out["overall_status"]      = overall_status;
        out["overall_status_name"] = nb::str(
            transfer_status_name(overall_status));
        out["error_stage"]   = nb::str(error_stage.c_str(), error_stage.size());
        out["error_code"]    = nb::str(error_code.c_str(), error_code.size());
        out["error_message"] = nb::str(error_message.c_str(),
                                       error_message.size());
        return out;
    }

    static nb::dict
    inspect_jxl_encoder_handoff_to_python(nb::object handoff_obj,
                                          bool include_icc_profile,
                                          bool unsafe_payload_access)
    {
        if (handoff_obj.is_none()) {
            throw std::runtime_error("jxl_encoder_handoff must be bytes-like");
        }

        const std::vector<std::byte> handoff_bytes = bytes_object_to_vector(
            handoff_obj);
        PreparedJxlEncoderHandoff handoff;
        const PreparedJxlEncoderHandoffIoResult handoff_io
            = deserialize_prepared_jxl_encoder_handoff(
                std::span<const std::byte>(handoff_bytes.data(),
                                           handoff_bytes.size()),
                &handoff);

        nb::dict out;
        out["requested_icc_profile_bytes"] = nb::bool_(include_icc_profile);
        out["status"]                      = handoff_io.status;
        out["status_name"] = nb::str(transfer_status_name(handoff_io.status));
        out["code"]        = handoff_io.code;
        out["code_name"]   = nb::str(emit_transfer_code_name(handoff_io.code));
        out["bytes_read"]  = nb::int_(handoff_io.bytes);
        out["errors"]      = nb::int_(handoff_io.errors);
        out["message"]     = nb::str(handoff_io.message.c_str(),
                                     handoff_io.message.size());
        out["has_icc_profile"]   = nb::bool_(handoff.has_icc_profile);
        out["icc_block_index"]   = nb::int_(handoff.icc_block_index);
        out["icc_profile_bytes"] = nb::int_(
            static_cast<uint64_t>(handoff.icc_profile.size()));
        out["box_count"]         = nb::int_(handoff.box_count);
        out["box_payload_bytes"] = nb::int_(handoff.box_payload_bytes);
        if (include_icc_profile && unsafe_payload_access
            && handoff_io.status == TransferStatus::Ok) {
            out["icc_profile"] = nb::bytes(reinterpret_cast<const char*>(
                                               handoff.icc_profile.data()),
                                           handoff.icc_profile.size());
        } else {
            out["icc_profile"] = nb::none();
        }

        TransferStatus overall_status = TransferStatus::Ok;
        std::string error_stage       = "none";
        std::string error_code        = "none";
        std::string error_message;
        if (include_icc_profile && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_icc_profile_forbidden";
            error_message
                = "safe inspect_jxl_encoder_handoff forbids icc profile "
                  "bytes; use unsafe_inspect_jxl_encoder_handoff("
                  "include_icc_profile=True)";
        } else if (handoff_io.status != TransferStatus::Ok) {
            overall_status = handoff_io.status;
            error_stage    = "jxl_encoder_handoff";
            error_code     = emit_transfer_code_name(handoff_io.code);
            error_message  = handoff_io.message;
        }
        out["overall_status"]      = overall_status;
        out["overall_status_name"] = nb::str(
            transfer_status_name(overall_status));
        out["error_stage"]   = nb::str(error_stage.c_str(), error_stage.size());
        out["error_code"]    = nb::str(error_code.c_str(), error_code.size());
        out["error_message"] = nb::str(error_message.c_str(),
                                       error_message.size());
        return out;
    }

    static nb::dict inspect_transfer_artifact_to_python(nb::object artifact_obj)
    {
        if (artifact_obj.is_none()) {
            throw std::runtime_error("transfer_artifact must be bytes-like");
        }

        const std::vector<std::byte> artifact_bytes = bytes_object_to_vector(
            artifact_obj);
        PreparedTransferArtifactInfo info;
        const PreparedTransferArtifactIoResult artifact_io
            = inspect_prepared_transfer_artifact(
                std::span<const std::byte>(artifact_bytes.data(),
                                           artifact_bytes.size()),
                &info);

        nb::dict out;
        out["status"]      = artifact_io.status;
        out["status_name"] = nb::str(transfer_status_name(artifact_io.status));
        out["code"]        = artifact_io.code;
        out["code_name"]   = nb::str(emit_transfer_code_name(artifact_io.code));
        out["bytes_read"]  = nb::int_(artifact_io.bytes);
        out["errors"]      = nb::int_(artifact_io.errors);
        out["message"]     = nb::str(artifact_io.message.c_str(),
                                     artifact_io.message.size());
        out["kind"]        = nb::int_(static_cast<uint32_t>(info.kind));
        {
            const std::string_view kind_name
                = prepared_transfer_artifact_kind_name(info.kind);
            out["kind_name"] = nb::str(kind_name.data(), kind_name.size());
        }
        out["has_contract_version"] = nb::bool_(info.has_contract_version);
        out["contract_version"]     = nb::int_(info.contract_version);
        out["has_target_format"]    = nb::bool_(info.has_target_format);
        if (info.has_target_format) {
            out["target_format"]      = info.target_format;
            out["target_format_name"] = nb::str(
                transfer_target_format_name(info.target_format));
        } else {
            out["target_format"]      = nb::none();
            out["target_format_name"] = nb::none();
        }
        out["entry_count"]          = nb::int_(info.entry_count);
        out["payload_bytes"]        = nb::int_(info.payload_bytes);
        out["binding_bytes"]        = nb::int_(info.binding_bytes);
        out["signed_payload_bytes"] = nb::int_(info.signed_payload_bytes);
        out["has_icc_profile"]      = nb::bool_(info.has_icc_profile);
        out["icc_block_index"]      = nb::int_(info.icc_block_index);
        out["icc_profile_bytes"]    = nb::int_(info.icc_profile_bytes);
        out["box_payload_bytes"]    = nb::int_(info.box_payload_bytes);
        out["carrier_route"]        = nb::str(info.carrier_route.c_str(),
                                              info.carrier_route.size());
        out["manifest_label"]       = nb::str(info.manifest_label.c_str(),
                                              info.manifest_label.size());
        out["overall_status"]       = artifact_io.status;
        out["overall_status_name"]  = nb::str(
            transfer_status_name(artifact_io.status));
        out["error_stage"] = nb::str("artifact");
        out["error_code"]  = nb::str(emit_transfer_code_name(artifact_io.code));
        out["error_message"] = nb::str(artifact_io.message.c_str(),
                                       artifact_io.message.size());
        return out;
    }

    static void append_persist_result_to_python(
        const PersistPreparedTransferFileResult& persisted,
        bool persist_requested, nb::dict* out)
    {
        if (!out) {
            return;
        }
        (*out)["persist_requested"] = nb::bool_(persist_requested);
        (*out)["persist_status"]    = persisted.status;
        (*out)["persist_status_name"]
            = nb::str(transfer_status_name(persisted.status));
        (*out)["persist_message"] = nb::str(persisted.message.c_str(),
                                            persisted.message.size());

        (*out)["persist_output_status"] = persisted.output_status;
        (*out)["persist_output_status_name"]
            = nb::str(transfer_status_name(persisted.output_status));
        (*out)["persist_output_message"] = nb::str(
            persisted.output_message.c_str(), persisted.output_message.size());
        (*out)["persist_output_path"]
            = nb::str(persisted.output_path.c_str(),
                      persisted.output_path.size());
        (*out)["persist_output_bytes"] = nb::int_(persisted.output_bytes);

        (*out)["persist_xmp_sidecar_status"] = persisted.xmp_sidecar_status;
        (*out)["persist_xmp_sidecar_status_name"] = nb::str(
            transfer_status_name(persisted.xmp_sidecar_status));
        (*out)["persist_xmp_sidecar_message"] = nb::str(
            persisted.xmp_sidecar_message.c_str(),
            persisted.xmp_sidecar_message.size());
        (*out)["persist_xmp_sidecar_path"]
            = nb::str(persisted.xmp_sidecar_path.c_str(),
                      persisted.xmp_sidecar_path.size());
        (*out)["persist_xmp_sidecar_bytes"]
            = nb::int_(persisted.xmp_sidecar_bytes);

        (*out)["persist_xmp_sidecar_cleanup_status"]
            = persisted.xmp_sidecar_cleanup_status;
        (*out)["persist_xmp_sidecar_cleanup_status_name"] = nb::str(
            transfer_status_name(persisted.xmp_sidecar_cleanup_status));
        (*out)["persist_xmp_sidecar_cleanup_message"] = nb::str(
            persisted.xmp_sidecar_cleanup_message.c_str(),
            persisted.xmp_sidecar_cleanup_message.size());
        (*out)["persist_xmp_sidecar_cleanup_path"]
            = nb::str(persisted.xmp_sidecar_cleanup_path.c_str(),
                      persisted.xmp_sidecar_cleanup_path.size());
        (*out)["persist_xmp_sidecar_cleanup_removed"]
            = nb::bool_(persisted.xmp_sidecar_cleanup_removed);
    }

    static nb::dict build_exr_attribute_batch_from_file_to_python(
        const std::string& path, XmpSidecarFormat format,
        bool include_pointer_tags, bool decode_makernote,
        bool decode_embedded_containers, bool decompress,
        bool include_exif_app1, bool include_xmp_app1, bool include_icc_app2,
        bool include_iptc_app13, bool xmp_include_existing,
        bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
        bool xmp_project_iptc, TransferPolicyAction makernote_policy,
        TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
        uint64_t max_file_bytes, nb::object policy_obj,
        bool include_values)
    {
        BuildExrAttributeBatchFileOptions options;
        options.prepare.include_pointer_tags       = include_pointer_tags;
        options.prepare.decode_makernote           = decode_makernote;
        options.prepare.decode_embedded_containers = decode_embedded_containers;
        options.prepare.decompress                 = decompress;
        options.prepare.prepare.xmp_portable
            = (format == XmpSidecarFormat::Portable);
        options.prepare.prepare.include_exif_app1 = include_exif_app1;
        options.prepare.prepare.include_xmp_app1  = include_xmp_app1;
        options.prepare.prepare.include_icc_app2  = include_icc_app2;
        options.prepare.prepare.include_iptc_app13 = include_iptc_app13;
        options.prepare.prepare.xmp_include_existing = xmp_include_existing;
        options.prepare.prepare.xmp_exiftool_gpsdatetime_alias
            = xmp_exiftool_gpsdatetime_alias;
        options.prepare.prepare.xmp_project_exif = xmp_project_exif;
        options.prepare.prepare.xmp_project_iptc = xmp_project_iptc;
        options.prepare.prepare.profile.makernote = makernote_policy;
        options.prepare.prepare.profile.jumbf     = jumbf_policy;
        options.prepare.prepare.profile.c2pa      = c2pa_policy;
        options.prepare.policy.max_file_bytes     = max_file_bytes;
        if (!policy_obj.is_none()) {
            options.prepare.policy
                = nb::cast<OpenMetaResourcePolicy>(policy_obj);
            if (max_file_bytes != 0U) {
                options.prepare.policy.max_file_bytes = max_file_bytes;
            }
        }

        ExrAdapterBatch batch;
        BuildExrAttributeBatchFileResult result;
        {
            nb::gil_scoped_release gil_release;
            result = build_exr_attribute_batch_from_file(path.c_str(), &batch,
                                                         options);
        }

        nb::dict out;
        out["path"] = nb::str(path.c_str(), path.size());
        out["file_status"] = result.prepared.file_status;
        out["file_status_name"]
            = nb::str(transfer_file_status_name(result.prepared.file_status));
        out["file_code"] = result.prepared.code;
        out["file_code_name"]
            = nb::str(prepare_transfer_file_code_name(result.prepared.code));
        out["file_size"]   = nb::int_(result.prepared.file_size);
        out["entry_count"] = nb::int_(result.prepared.entry_count);
        out["prepare_status"] = result.prepared.prepare.status;
        out["prepare_status_name"]
            = nb::str(transfer_status_name(result.prepared.prepare.status));
        out["prepare_code"] = result.prepared.prepare.code;
        out["prepare_code_name"]
            = nb::str(prepare_transfer_code_name(result.prepared.prepare.code));
        out["prepare_warnings"] = nb::int_(result.prepared.prepare.warnings);
        out["prepare_errors"]   = nb::int_(result.prepared.prepare.errors);
        out["prepare_message"]  = nb::str(
            result.prepared.prepare.message.c_str(),
            result.prepared.prepare.message.size());
        out["exr_attribute_batch_status"] = result.adapter.status;
        out["exr_attribute_batch_status_name"]
            = nb::str(exr_adapter_status_name(result.adapter.status));
        out["exr_attribute_batch_exported"] = nb::int_(result.adapter.exported);
        out["exr_attribute_batch_skipped"]  = nb::int_(result.adapter.skipped);
        out["exr_attribute_batch_errors"]   = nb::int_(result.adapter.errors);
        out["exr_attribute_batch_message"]  = nb::str(
            result.adapter.message.c_str(), result.adapter.message.size());
        out["exr_attribute_values_requested"] = nb::bool_(include_values);

        TransferStatus values_status = TransferStatus::Ok;
        std::string values_message;
        if (include_values && result.adapter.status == ExrAdapterStatus::Ok) {
            values_status = TransferStatus::Ok;
        } else if (result.adapter.status == ExrAdapterStatus::InvalidArgument) {
            values_status = TransferStatus::InvalidArgument;
            values_message = result.adapter.message;
        } else if (result.adapter.status == ExrAdapterStatus::Unsupported) {
            values_status = TransferStatus::Unsupported;
            values_message = result.adapter.message;
        }
        out["exr_attribute_values_status"] = values_status;
        out["exr_attribute_values_status_name"]
            = nb::str(transfer_status_name(values_status));
        out["exr_attribute_values_message"]
            = nb::str(values_message.c_str(), values_message.size());

        nb::list exr_attribute_batch;
        if (result.adapter.status == ExrAdapterStatus::Ok) {
            for (size_t i = 0; i < batch.attributes.size(); ++i) {
                const ExrAdapterAttribute& attr = batch.attributes[i];
                nb::dict one;
                one["part_index"] = nb::int_(attr.part_index);
                one["name"] = nb::str(attr.name.c_str(), attr.name.size());
                one["type_name"]
                    = nb::str(attr.type_name.c_str(), attr.type_name.size());
                one["is_opaque"] = nb::bool_(attr.is_opaque);
                one["bytes"]
                    = nb::int_(static_cast<uint64_t>(attr.value.size()));
                if (include_values) {
                    one["value"] = nb::bytes(
                        reinterpret_cast<const char*>(attr.value.data()),
                        attr.value.size());
                } else {
                    one["value"] = nb::none();
                }
                exr_attribute_batch.append(std::move(one));
            }
            out["exr_attribute_batch"] = std::move(exr_attribute_batch);
        } else {
            out["exr_attribute_batch"] = nb::none();
        }

        TransferStatus overall_status = TransferStatus::Ok;
        std::string error_stage       = "none";
        std::string error_code        = "none";
        std::string error_message;
        if (result.prepared.file_status != TransferFileStatus::Ok) {
            overall_status = transfer_status_from_file_status(
                result.prepared.file_status);
            error_stage   = "file";
            error_code    = prepare_transfer_file_code_name(
                result.prepared.code);
            error_message = result.prepared.prepare.message;
        } else if (result.prepared.prepare.status != TransferStatus::Ok) {
            overall_status = result.prepared.prepare.status;
            error_stage    = "prepare";
            error_code     = prepare_transfer_code_name(
                result.prepared.prepare.code);
            error_message = result.prepared.prepare.message;
        } else if (result.adapter.status != ExrAdapterStatus::Ok) {
            if (result.adapter.status == ExrAdapterStatus::InvalidArgument) {
                overall_status = TransferStatus::InvalidArgument;
            } else {
                overall_status = TransferStatus::Unsupported;
            }
            error_stage   = "exr_attribute_batch";
            error_code    = exr_adapter_status_name(result.adapter.status);
            error_message = result.adapter.message;
        }
        out["overall_status"] = overall_status;
        out["overall_status_name"]
            = nb::str(transfer_status_name(overall_status));
        out["error_stage"] = nb::str(error_stage.c_str(), error_stage.size());
        out["error_code"]  = nb::str(error_code.c_str(), error_code.size());
        out["error_message"]
            = nb::str(error_message.c_str(), error_message.size());
        return out;
    }

    static nb::dict transfer_probe_to_python(
        const std::string& path, TransferTargetFormat target_format,
        XmpSidecarFormat format, bool include_pointer_tags,
        bool decode_makernote, bool decode_embedded_containers, bool decompress,
        bool include_exif_app1, bool include_xmp_app1, bool include_icc_app2,
        bool include_iptc_app13, bool xmp_include_existing,
        bool xmp_exiftool_gpsdatetime_alias, bool xmp_project_exif,
        bool xmp_project_iptc,
        TransferPolicyAction makernote_policy,
        TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
        uint64_t max_file_bytes, nb::object policy_obj,
        nb::object c2pa_signed_package_obj,
        nb::object c2pa_signed_logical_payload_obj,
        nb::object c2pa_certificate_chain_obj,
        nb::object c2pa_private_key_reference_obj,
        nb::object c2pa_signing_time_obj,
        nb::object c2pa_manifest_builder_output_obj, bool include_payloads,
        bool unsafe_payload_access, nb::object time_patches_obj,
        bool time_patch_strict_width, bool time_patch_require_slot,
        bool time_patch_auto_nul, nb::object edit_target_path_obj,
        nb::object xmp_sidecar_base_path_obj,
        XmpExistingSidecarMode xmp_existing_sidecar_mode,
        XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
        XmpExistingDestinationEmbeddedMode
            xmp_existing_destination_embedded_mode,
        XmpExistingDestinationEmbeddedPrecedence
            xmp_existing_destination_embedded_precedence,
        bool edit_do_apply,
        bool include_edited_bytes,
        bool unsafe_edited_bytes_access, bool include_c2pa_binding_bytes,
        bool unsafe_c2pa_binding_access, bool include_c2pa_handoff_bytes,
        bool include_c2pa_signed_package_bytes,
        bool include_jxl_encoder_handoff_bytes,
        bool include_exr_attribute_values,
        bool include_transfer_payload_batch_bytes,
        bool include_transfer_package_batch_bytes,
        bool unsafe_c2pa_package_access,
        XmpConflictPolicy xmp_conflict_policy,
        XmpWritebackMode xmp_writeback_mode,
        XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
        XmpDestinationSidecarMode xmp_destination_sidecar_mode,
        nb::object persist_output_path_obj, bool persist_overwrite_output,
        bool persist_overwrite_xmp_sidecar,
        bool persist_remove_destination_xmp_sidecar)
    {
        PrepareTransferFileOptions prepare_options;
        prepare_options.include_pointer_tags       = include_pointer_tags;
        prepare_options.decode_makernote           = decode_makernote;
        prepare_options.decode_embedded_containers = decode_embedded_containers;
        prepare_options.decompress                 = decompress;
        prepare_options.prepare.target_format      = target_format;
        prepare_options.prepare.xmp_portable       = (format
                                                == XmpSidecarFormat::Portable);
        prepare_options.prepare.include_exif_app1  = include_exif_app1;
        prepare_options.prepare.include_xmp_app1   = include_xmp_app1;
        prepare_options.prepare.include_icc_app2   = include_icc_app2;
        prepare_options.prepare.include_iptc_app13 = include_iptc_app13;
        prepare_options.prepare.xmp_project_exif   = xmp_project_exif;
        prepare_options.prepare.xmp_project_iptc   = xmp_project_iptc;
        prepare_options.prepare.xmp_include_existing = xmp_include_existing;
        prepare_options.prepare.xmp_conflict_policy = xmp_conflict_policy;
        prepare_options.prepare.xmp_exiftool_gpsdatetime_alias
            = xmp_exiftool_gpsdatetime_alias;
        prepare_options.prepare.profile.makernote = makernote_policy;
        prepare_options.prepare.profile.jumbf     = jumbf_policy;
        prepare_options.prepare.profile.c2pa      = c2pa_policy;
        prepare_options.policy.max_file_bytes     = max_file_bytes;
        prepare_options.xmp_existing_sidecar_mode
            = xmp_existing_sidecar_mode;
        prepare_options.xmp_existing_sidecar_precedence
            = xmp_existing_sidecar_precedence;

        if (!policy_obj.is_none()) {
            prepare_options.policy = nb::cast<OpenMetaResourcePolicy>(
                policy_obj);
            if (max_file_bytes != 0U) {
                prepare_options.policy.max_file_bytes = max_file_bytes;
            }
        }

        std::vector<TransferTimePatchInput> parsed_updates
            = parse_time_patches_object(time_patches_obj);

        ExecutePreparedTransferFileOptions file_options;
        file_options.prepare              = prepare_options;
        file_options.execute.time_patches = std::move(parsed_updates);
        file_options.execute.time_patch.strict_width = time_patch_strict_width;
        file_options.execute.time_patch.require_slot = time_patch_require_slot;
        file_options.execute.time_patch_auto_nul     = time_patch_auto_nul;
        file_options.execute.edit_apply              = edit_do_apply;
        file_options.execute.edit_requested          = false;
        file_options.xmp_existing_destination_embedded_mode
            = xmp_existing_destination_embedded_mode;
        file_options.xmp_existing_destination_embedded_precedence
            = xmp_existing_destination_embedded_precedence;
        file_options.xmp_writeback_mode              = xmp_writeback_mode;
        file_options.xmp_destination_embedded_mode
            = xmp_destination_embedded_mode;
        file_options.xmp_destination_sidecar_mode
            = xmp_destination_sidecar_mode;

        if (!edit_target_path_obj.is_none()) {
            file_options.edit_target_path = nb::cast<std::string>(
                edit_target_path_obj);
            if (!file_options.edit_target_path.empty()) {
                file_options.execute.edit_requested = true;
            }
        }
        if (!xmp_sidecar_base_path_obj.is_none()) {
            const std::string sidecar_base_path = nb::cast<std::string>(
                xmp_sidecar_base_path_obj);
            prepare_options.xmp_existing_sidecar_base_path = sidecar_base_path;
            file_options.xmp_sidecar_base_path             = sidecar_base_path;
        }
        file_options.prepare = prepare_options;

        const bool have_c2pa_signed_package = !c2pa_signed_package_obj.is_none();
        const bool have_individual_c2pa_inputs
            = !c2pa_signed_logical_payload_obj.is_none()
              || !c2pa_certificate_chain_obj.is_none()
              || !c2pa_private_key_reference_obj.is_none()
              || !c2pa_signing_time_obj.is_none()
              || !c2pa_manifest_builder_output_obj.is_none();
        if (have_c2pa_signed_package && have_individual_c2pa_inputs) {
            throw std::runtime_error(
                "c2pa_signed_package is mutually exclusive with individual "
                "signed c2pa inputs");
        }

        if (have_c2pa_signed_package) {
            const std::vector<std::byte> package_bytes = bytes_object_to_vector(
                c2pa_signed_package_obj);
            PreparedTransferC2paSignedPackage package;
            const PreparedTransferC2paPackageIoResult package_io
                = deserialize_prepared_c2pa_signed_package(
                    std::span<const std::byte>(package_bytes.data(),
                                               package_bytes.size()),
                    &package);
            if (package_io.status != TransferStatus::Ok) {
                throw std::runtime_error(
                    package_io.message.empty()
                        ? "failed to deserialize c2pa signed package"
                        : package_io.message);
            }
            file_options.c2pa_signed_package_provided = true;
            file_options.c2pa_signed_package          = std::move(package);
            file_options.prepare.prepare.profile.c2pa
                = TransferPolicyAction::Rewrite;
        } else if (have_individual_c2pa_inputs) {
            file_options.c2pa_stage_requested = true;
            file_options.prepare.prepare.profile.c2pa
                = TransferPolicyAction::Rewrite;
            file_options.c2pa_signer_input.signed_c2pa_logical_payload
                = bytes_object_to_vector(c2pa_signed_logical_payload_obj);
            file_options.c2pa_signer_input.certificate_chain_bytes
                = bytes_object_to_vector(c2pa_certificate_chain_obj);
            if (!c2pa_private_key_reference_obj.is_none()) {
                file_options.c2pa_signer_input.private_key_reference
                    = nb::cast<std::string>(c2pa_private_key_reference_obj);
            }
            if (!c2pa_signing_time_obj.is_none()) {
                file_options.c2pa_signer_input.signing_time
                    = nb::cast<std::string>(c2pa_signing_time_obj);
            }
            file_options.c2pa_signer_input.manifest_builder_output
                = bytes_object_to_vector(c2pa_manifest_builder_output_obj);
        }

        const bool persist_requested = !persist_output_path_obj.is_none();
        std::string persist_output_path;
        if (persist_requested) {
            persist_output_path = nb::cast<std::string>(persist_output_path_obj);
        }

        ExecutePreparedTransferFileResult executed;
        PersistPreparedTransferFileResult persisted;
        {
            nb::gil_scoped_release gil_release;
            executed = execute_prepared_transfer_file(path.c_str(),
                                                      file_options);
            if (persist_requested) {
                PersistPreparedTransferFileOptions persist_options;
                persist_options.output_path = persist_output_path;
                persist_options.overwrite_output = persist_overwrite_output;
                persist_options.overwrite_xmp_sidecar
                    = persist_overwrite_xmp_sidecar;
                persist_options.remove_destination_xmp_sidecar
                    = persist_remove_destination_xmp_sidecar;
                persisted = persist_prepared_transfer_file_result(
                    executed, persist_options);
            }
        }
        const PrepareTransferFileResult& prepared = executed.prepared;
        const ExecutePreparedTransferResult& exec = executed.execute;

        nb::dict out;
        out["path"]             = nb::str(path.c_str(), path.size());
        out["file_status"]      = prepared.file_status;
        out["file_status_name"] = nb::str(
            transfer_file_status_name(prepared.file_status));
        out["file_code"]      = prepared.code;
        out["file_code_name"] = nb::str(
            prepare_transfer_file_code_name(prepared.code));
        out["file_size"]           = nb::int_(prepared.file_size);
        out["entry_count"]         = nb::int_(prepared.entry_count);
        out["scan_status"]         = prepared.read.scan.status;
        out["payload_status"]      = prepared.read.payload.status;
        out["exif_status"]         = prepared.read.exif.status;
        out["xmp_status"]          = prepared.read.xmp.status;
        out["exr_status"]          = prepared.read.exr.status;
        out["jumbf_status"]        = prepared.read.jumbf.status;
        out["prepare_status"]      = prepared.prepare.status;
        out["prepare_status_name"] = nb::str(
            transfer_status_name(prepared.prepare.status));
        out["prepare_code"]      = prepared.prepare.code;
        out["prepare_code_name"] = nb::str(
            prepare_transfer_code_name(prepared.prepare.code));
        out["prepare_warnings"] = nb::int_(prepared.prepare.warnings);
        out["prepare_errors"]   = nb::int_(prepared.prepare.errors);
        out["prepare_message"]  = nb::str(prepared.prepare.message.c_str(),
                                          prepared.prepare.message.size());
        out["xmp_existing_sidecar_loaded"]
            = nb::bool_(prepared.xmp_existing_sidecar_loaded);
        out["xmp_existing_sidecar_status"]
            = prepared.xmp_existing_sidecar_status;
        out["xmp_existing_sidecar_status_name"] = nb::str(
            transfer_status_name(prepared.xmp_existing_sidecar_status));
        out["xmp_existing_sidecar_message"] = nb::str(
            prepared.xmp_existing_sidecar_message.c_str(),
            prepared.xmp_existing_sidecar_message.size());
        out["xmp_existing_sidecar_path"] = nb::str(
            prepared.xmp_existing_sidecar_path.c_str(),
            prepared.xmp_existing_sidecar_path.size());
        out["xmp_existing_destination_embedded_loaded"]
            = nb::bool_(executed.xmp_existing_destination_embedded_loaded);
        out["xmp_existing_destination_embedded_status"]
            = executed.xmp_existing_destination_embedded_status;
        out["xmp_existing_destination_embedded_status_name"] = nb::str(
            transfer_status_name(
                executed.xmp_existing_destination_embedded_status));
        out["xmp_existing_destination_embedded_message"] = nb::str(
            executed.xmp_existing_destination_embedded_message.c_str(),
            executed.xmp_existing_destination_embedded_message.size());
        out["xmp_existing_destination_embedded_path"] = nb::str(
            executed.xmp_existing_destination_embedded_path.c_str(),
            executed.xmp_existing_destination_embedded_path.size());
        out["xmp_sidecar_requested"] = nb::bool_(executed.xmp_sidecar_requested);
        out["xmp_sidecar_status"]    = executed.xmp_sidecar_status;
        out["xmp_sidecar_status_name"]
            = nb::str(transfer_status_name(executed.xmp_sidecar_status));
        out["xmp_sidecar_message"] = nb::str(
            executed.xmp_sidecar_message.c_str(),
            executed.xmp_sidecar_message.size());
        out["xmp_sidecar_path"] = nb::str(executed.xmp_sidecar_path.c_str(),
                                          executed.xmp_sidecar_path.size());
        out["xmp_sidecar_bytes"] = nb::int_(
            static_cast<uint64_t>(executed.xmp_sidecar_output.size()));
        out["xmp_sidecar_cleanup_requested"]
            = nb::bool_(executed.xmp_sidecar_cleanup_requested);
        out["xmp_sidecar_cleanup_status"]
            = executed.xmp_sidecar_cleanup_status;
        out["xmp_sidecar_cleanup_status_name"] = nb::str(
            transfer_status_name(executed.xmp_sidecar_cleanup_status));
        out["xmp_sidecar_cleanup_message"] = nb::str(
            executed.xmp_sidecar_cleanup_message.c_str(),
            executed.xmp_sidecar_cleanup_message.size());
        out["xmp_sidecar_cleanup_path"] = nb::str(
            executed.xmp_sidecar_cleanup_path.c_str(),
            executed.xmp_sidecar_cleanup_path.size());
        if (!executed.xmp_sidecar_output.empty()) {
            out["xmp_sidecar_output"] = nb::bytes(
                reinterpret_cast<const char*>(
                    executed.xmp_sidecar_output.data()),
                executed.xmp_sidecar_output.size());
        } else {
            out["xmp_sidecar_output"] = nb::none();
        }
        append_persist_result_to_python(persisted, persist_requested, &out);

        out["time_patch_status"]      = exec.time_patch.status;
        out["time_patch_status_name"] = nb::str(
            transfer_status_name(exec.time_patch.status));
        out["time_patch_patched_slots"] = nb::int_(
            exec.time_patch.patched_slots);
        out["time_patch_skipped_slots"] = nb::int_(
            exec.time_patch.skipped_slots);
        out["time_patch_errors"]      = nb::int_(exec.time_patch.errors);
        out["time_patch_message"]     = nb::str(exec.time_patch.message.c_str(),
                                                exec.time_patch.message.size());
        out["c2pa_stage_requested"]   = nb::bool_(exec.c2pa_stage_requested);
        out["c2pa_stage_status"]      = exec.c2pa_stage.status;
        out["c2pa_stage_status_name"] = nb::str(
            transfer_status_name(exec.c2pa_stage.status));
        out["c2pa_stage_code"]      = exec.c2pa_stage.code;
        out["c2pa_stage_code_name"] = nb::str(
            emit_transfer_code_name(exec.c2pa_stage.code));
        out["c2pa_stage_emitted"] = nb::int_(exec.c2pa_stage.emitted);
        out["c2pa_stage_skipped"] = nb::int_(exec.c2pa_stage.skipped);
        out["c2pa_stage_errors"]  = nb::int_(exec.c2pa_stage.errors);
        out["c2pa_stage_message"] = nb::str(exec.c2pa_stage.message.c_str(),
                                            exec.c2pa_stage.message.size());
        out["c2pa_stage_validation_status"] = exec.c2pa_stage_validation.status;
        out["c2pa_stage_validation_status_name"] = nb::str(
            transfer_status_name(exec.c2pa_stage_validation.status));
        out["c2pa_stage_validation_code"] = exec.c2pa_stage_validation.code;
        out["c2pa_stage_validation_code_name"] = nb::str(
            emit_transfer_code_name(exec.c2pa_stage_validation.code));
        out["c2pa_stage_validation_payload_kind"]
            = exec.c2pa_stage_validation.payload_kind;
        out["c2pa_stage_validation_payload_kind_name"] = nb::str(
            transfer_c2pa_signed_payload_kind_name(
                exec.c2pa_stage_validation.payload_kind));
        out["c2pa_stage_validation_semantic_status"]
            = exec.c2pa_stage_validation.semantic_status;
        out["c2pa_stage_validation_semantic_status_name"] = nb::str(
            transfer_c2pa_semantic_status_name(
                exec.c2pa_stage_validation.semantic_status));
        out["c2pa_stage_validation_semantic_reason"]
            = nb::str(exec.c2pa_stage_validation.semantic_reason.c_str(),
                      exec.c2pa_stage_validation.semantic_reason.size());
        out["c2pa_stage_validation_logical_payload_bytes"] = nb::int_(
            exec.c2pa_stage_validation.logical_payload_bytes);
        out["c2pa_stage_validation_staged_payload_bytes"] = nb::int_(
            exec.c2pa_stage_validation.staged_payload_bytes);
        out["c2pa_stage_validation_semantic_manifest_present"] = nb::int_(
            exec.c2pa_stage_validation.semantic_manifest_present);
        out["c2pa_stage_validation_semantic_manifest_count"] = nb::int_(
            exec.c2pa_stage_validation.semantic_manifest_count);
        out["c2pa_stage_validation_semantic_claim_generator_present"] = nb::int_(
            exec.c2pa_stage_validation.semantic_claim_generator_present);
        out["c2pa_stage_validation_semantic_assertion_count"] = nb::int_(
            exec.c2pa_stage_validation.semantic_assertion_count);
        out["c2pa_stage_validation_semantic_primary_claim_assertion_count"]
            = nb::int_(exec.c2pa_stage_validation
                           .semantic_primary_claim_assertion_count);
        out["c2pa_stage_validation_semantic_primary_claim_referenced_by_signature_count"]
            = nb::int_(
                exec.c2pa_stage_validation
                    .semantic_primary_claim_referenced_by_signature_count);
        out["c2pa_stage_validation_semantic_primary_signature_linked_claim_count"]
            = nb::int_(exec.c2pa_stage_validation
                           .semantic_primary_signature_linked_claim_count);
        out["c2pa_stage_validation_semantic_primary_signature_reference_key_hits"]
            = nb::int_(exec.c2pa_stage_validation
                           .semantic_primary_signature_reference_key_hits);
        out["c2pa_stage_validation_semantic_primary_signature_explicit_reference_present"]
            = nb::int_(
                exec.c2pa_stage_validation
                    .semantic_primary_signature_explicit_reference_present);
        out["c2pa_stage_validation_semantic_primary_signature_explicit_reference_resolved_claim_count"]
            = nb::int_(
                exec.c2pa_stage_validation
                    .semantic_primary_signature_explicit_reference_resolved_claim_count);
        out["c2pa_stage_validation_semantic_claim_count"] = nb::int_(
            exec.c2pa_stage_validation.semantic_claim_count);
        out["c2pa_stage_validation_semantic_signature_count"] = nb::int_(
            exec.c2pa_stage_validation.semantic_signature_count);
        out["c2pa_stage_validation_semantic_signature_linked"] = nb::int_(
            exec.c2pa_stage_validation.semantic_signature_linked);
        out["c2pa_stage_validation_semantic_signature_orphan"] = nb::int_(
            exec.c2pa_stage_validation.semantic_signature_orphan);
        out["c2pa_stage_validation_semantic_explicit_reference_signature_count"]
            = nb::int_(exec.c2pa_stage_validation
                           .semantic_explicit_reference_signature_count);
        out["c2pa_stage_validation_semantic_explicit_reference_unresolved_signature_count"]
            = nb::int_(
                exec.c2pa_stage_validation
                    .semantic_explicit_reference_unresolved_signature_count);
        out["c2pa_stage_validation_semantic_explicit_reference_ambiguous_signature_count"]
            = nb::int_(
                exec.c2pa_stage_validation
                    .semantic_explicit_reference_ambiguous_signature_count);
        out["c2pa_stage_validation_staged_segments"] = nb::int_(
            exec.c2pa_stage_validation.staged_segments);
        out["c2pa_stage_validation_errors"] = nb::int_(
            exec.c2pa_stage_validation.errors);
        out["c2pa_stage_validation_message"]
            = nb::str(exec.c2pa_stage_validation.message.c_str(),
                      exec.c2pa_stage_validation.message.size());

        const bool allow_payload_bytes = include_payloads
                                         && unsafe_payload_access;

        nb::list blocks;
        for (size_t i = 0; i < prepared.bundle.blocks.size(); ++i) {
            const PreparedTransferBlock& b = prepared.bundle.blocks[i];
            nb::dict one;
            one["index"] = nb::int_(static_cast<uint32_t>(i));
            one["kind"]  = b.kind;
            one["order"] = nb::int_(b.order);
            one["route"] = nb::str(b.route.c_str(), b.route.size());
            one["size"]  = nb::int_(static_cast<uint64_t>(b.payload.size()));
            if (allow_payload_bytes) {
                one["payload"]
                    = nb::bytes(reinterpret_cast<const char*>(b.payload.data()),
                                b.payload.size());
            } else {
                one["payload"] = nb::none();
            }
            blocks.append(std::move(one));
        }
        out["blocks"] = std::move(blocks);

        nb::list policy_decisions;
        for (size_t i = 0; i < prepared.bundle.policy_decisions.size(); ++i) {
            const PreparedTransferPolicyDecision& d
                = prepared.bundle.policy_decisions[i];
            nb::dict one;
            one["subject"]      = d.subject;
            one["subject_name"] = nb::str(
                transfer_policy_subject_name(d.subject));
            one["requested"]      = d.requested;
            one["requested_name"] = nb::str(
                transfer_policy_action_name(d.requested));
            one["effective"]      = d.effective;
            one["effective_name"] = nb::str(
                transfer_policy_action_name(d.effective));
            one["reason"]      = d.reason;
            one["reason_name"] = nb::str(transfer_policy_reason_name(d.reason));
            one["c2pa_mode"]   = d.c2pa_mode;
            one["c2pa_mode_name"] = nb::str(
                transfer_c2pa_mode_name(d.c2pa_mode));
            one["c2pa_source_kind"]      = d.c2pa_source_kind;
            one["c2pa_source_kind_name"] = nb::str(
                transfer_c2pa_source_kind_name(d.c2pa_source_kind));
            one["c2pa_prepared_output"]      = d.c2pa_prepared_output;
            one["c2pa_prepared_output_name"] = nb::str(
                transfer_c2pa_prepared_output_name(d.c2pa_prepared_output));
            one["matched_entries"] = nb::int_(d.matched_entries);
            one["message"] = nb::str(d.message.c_str(), d.message.size());
            policy_decisions.append(std::move(one));
        }
        out["policy_decisions"] = std::move(policy_decisions);

        const PreparedTransferC2paRewriteRequirements& rewrite
            = prepared.bundle.c2pa_rewrite;
        nb::dict rewrite_dict;
        rewrite_dict["state"]      = rewrite.state;
        rewrite_dict["state_name"] = nb::str(
            transfer_c2pa_rewrite_state_name(rewrite.state));
        rewrite_dict["target_format"]      = rewrite.target_format;
        rewrite_dict["target_format_name"] = nb::str(
            transfer_target_format_name(rewrite.target_format));
        rewrite_dict["source_kind"]      = rewrite.source_kind;
        rewrite_dict["source_kind_name"] = nb::str(
            transfer_c2pa_source_kind_name(rewrite.source_kind));
        rewrite_dict["matched_entries"] = nb::int_(rewrite.matched_entries);
        rewrite_dict["existing_carrier_segments"] = nb::int_(
            rewrite.existing_carrier_segments);
        rewrite_dict["target_carrier_available"] = nb::bool_(
            rewrite.target_carrier_available);
        rewrite_dict["content_change_invalidates_existing"] = nb::bool_(
            rewrite.content_change_invalidates_existing);
        rewrite_dict["requires_manifest_builder"] = nb::bool_(
            rewrite.requires_manifest_builder);
        rewrite_dict["requires_content_binding"] = nb::bool_(
            rewrite.requires_content_binding);
        rewrite_dict["requires_certificate_chain"] = nb::bool_(
            rewrite.requires_certificate_chain);
        rewrite_dict["requires_private_key"] = nb::bool_(
            rewrite.requires_private_key);
        rewrite_dict["requires_signing_time"] = nb::bool_(
            rewrite.requires_signing_time);
        rewrite_dict["content_binding_bytes"] = nb::int_(
            rewrite.content_binding_bytes);
        nb::list binding_chunks;
        for (size_t i = 0; i < rewrite.content_binding_chunks.size(); ++i) {
            const PreparedTransferC2paRewriteChunk& chunk
                = rewrite.content_binding_chunks[i];
            nb::dict one;
            one["index"]     = nb::int_(static_cast<uint32_t>(i));
            one["kind"]      = chunk.kind;
            one["kind_name"] = nb::str(
                transfer_c2pa_rewrite_chunk_kind_name(chunk.kind));
            one["source_offset"]    = nb::int_(chunk.source_offset);
            one["size"]             = nb::int_(chunk.size);
            one["block_index"]      = nb::int_(chunk.block_index);
            one["jpeg_marker_code"] = nb::int_(chunk.jpeg_marker_code);
            if (chunk.block_index < prepared.bundle.blocks.size()) {
                one["route"] = nb::str(
                    prepared.bundle.blocks[chunk.block_index].route.c_str(),
                    prepared.bundle.blocks[chunk.block_index].route.size());
            } else {
                one["route"] = nb::none();
            }
            binding_chunks.append(std::move(one));
        }
        rewrite_dict["content_binding_chunks"] = std::move(binding_chunks);
        rewrite_dict["message"] = nb::str(rewrite.message.c_str(),
                                          rewrite.message.size());
        out["c2pa_rewrite"]     = std::move(rewrite_dict);

        PreparedTransferC2paSignRequest sign_request;
        const TransferStatus sign_request_status
            = build_prepared_c2pa_sign_request(prepared.bundle, &sign_request);
        nb::dict sign_request_dict;
        sign_request_dict["status"]      = sign_request_status;
        sign_request_dict["status_name"] = nb::str(
            transfer_status_name(sign_request_status));
        sign_request_dict["rewrite_state"]      = sign_request.rewrite_state;
        sign_request_dict["rewrite_state_name"] = nb::str(
            transfer_c2pa_rewrite_state_name(sign_request.rewrite_state));
        sign_request_dict["target_format"]      = sign_request.target_format;
        sign_request_dict["target_format_name"] = nb::str(
            transfer_target_format_name(sign_request.target_format));
        sign_request_dict["source_kind"]      = sign_request.source_kind;
        sign_request_dict["source_kind_name"] = nb::str(
            transfer_c2pa_source_kind_name(sign_request.source_kind));
        sign_request_dict["carrier_route"]
            = nb::str(sign_request.carrier_route.c_str(),
                      sign_request.carrier_route.size());
        sign_request_dict["manifest_label"]
            = nb::str(sign_request.manifest_label.c_str(),
                      sign_request.manifest_label.size());
        sign_request_dict["existing_carrier_segments"] = nb::int_(
            sign_request.existing_carrier_segments);
        sign_request_dict["source_range_chunks"] = nb::int_(
            sign_request.source_range_chunks);
        sign_request_dict["prepared_segment_chunks"] = nb::int_(
            sign_request.prepared_segment_chunks);
        sign_request_dict["content_binding_bytes"] = nb::int_(
            sign_request.content_binding_bytes);
        sign_request_dict["requires_manifest_builder"] = nb::bool_(
            sign_request.requires_manifest_builder);
        sign_request_dict["requires_content_binding"] = nb::bool_(
            sign_request.requires_content_binding);
        sign_request_dict["requires_certificate_chain"] = nb::bool_(
            sign_request.requires_certificate_chain);
        sign_request_dict["requires_private_key"] = nb::bool_(
            sign_request.requires_private_key);
        sign_request_dict["requires_signing_time"] = nb::bool_(
            sign_request.requires_signing_time);
        nb::list sign_request_chunks;
        for (size_t i = 0; i < sign_request.content_binding_chunks.size();
             ++i) {
            const PreparedTransferC2paRewriteChunk& chunk
                = sign_request.content_binding_chunks[i];
            nb::dict one;
            one["index"]     = nb::int_(static_cast<uint32_t>(i));
            one["kind"]      = chunk.kind;
            one["kind_name"] = nb::str(
                transfer_c2pa_rewrite_chunk_kind_name(chunk.kind));
            one["source_offset"]    = nb::int_(chunk.source_offset);
            one["size"]             = nb::int_(chunk.size);
            one["block_index"]      = nb::int_(chunk.block_index);
            one["jpeg_marker_code"] = nb::int_(chunk.jpeg_marker_code);
            if (chunk.block_index < prepared.bundle.blocks.size()) {
                one["route"] = nb::str(
                    prepared.bundle.blocks[chunk.block_index].route.c_str(),
                    prepared.bundle.blocks[chunk.block_index].route.size());
            } else {
                one["route"] = nb::none();
            }
            sign_request_chunks.append(std::move(one));
        }
        sign_request_dict["content_binding_chunks"] = std::move(
            sign_request_chunks);
        sign_request_dict["message"] = nb::str(sign_request.message.c_str(),
                                               sign_request.message.size());
        out["c2pa_sign_request"]     = std::move(sign_request_dict);

        PreparedTransferC2paHandoffPackage c2pa_handoff;
        PreparedTransferC2paSignedPackage c2pa_signed_package;
        PreparedTransferC2paPackageIoResult c2pa_handoff_io;
        PreparedTransferC2paPackageIoResult c2pa_signed_package_io;
        std::vector<std::byte> c2pa_handoff_bytes;
        std::vector<std::byte> c2pa_signed_package_bytes;
        out["c2pa_binding_requested"] = nb::bool_(include_c2pa_binding_bytes);
        if (include_c2pa_binding_bytes && !unsafe_c2pa_binding_access) {
            c2pa_handoff.binding.status = TransferStatus::UnsafeData;
            c2pa_handoff.binding.code   = EmitTransferCode::InvalidArgument;
            c2pa_handoff.binding.errors = 1U;
            c2pa_handoff.binding.message
                = "safe transfer_probe forbids c2pa binding bytes; use "
                  "unsafe_transfer_probe(include_c2pa_binding_bytes=True)";
        } else if (include_c2pa_binding_bytes) {
            const std::string binding_path
                = !file_options.edit_target_path.empty()
                      ? file_options.edit_target_path
                      : path;
            try {
                std::vector<std::byte> binding_input;
                {
                    nb::gil_scoped_release gil_release;
                    binding_input = read_file_bytes(
                        binding_path.c_str(),
                        file_options.prepare.policy.max_file_bytes);
                }
                build_prepared_c2pa_handoff_package(
                    prepared.bundle,
                    std::span<const std::byte>(binding_input.data(),
                                               binding_input.size()),
                    &c2pa_handoff);
            } catch (const std::exception& ex) {
                c2pa_handoff.binding.status = TransferStatus::InvalidArgument;
                c2pa_handoff.binding.code   = EmitTransferCode::InvalidArgument;
                c2pa_handoff.binding.errors = 1U;
                c2pa_handoff.binding.message = ex.what();
            }
        }
        out["c2pa_binding_status"]      = c2pa_handoff.binding.status;
        out["c2pa_binding_status_name"] = nb::str(
            transfer_status_name(c2pa_handoff.binding.status));
        out["c2pa_binding_code"]      = c2pa_handoff.binding.code;
        out["c2pa_binding_code_name"] = nb::str(
            emit_transfer_code_name(c2pa_handoff.binding.code));
        out["c2pa_binding_bytes_written"] = nb::int_(
            c2pa_handoff.binding.written);
        out["c2pa_binding_errors"] = nb::int_(c2pa_handoff.binding.errors);
        out["c2pa_binding_message"]
            = nb::str(c2pa_handoff.binding.message.c_str(),
                      c2pa_handoff.binding.message.size());
        if (include_c2pa_binding_bytes && unsafe_c2pa_binding_access
            && c2pa_handoff.binding.status == TransferStatus::Ok) {
            out["c2pa_binding_bytes"]
                = nb::bytes(reinterpret_cast<const char*>(
                                c2pa_handoff.binding_bytes.data()),
                            c2pa_handoff.binding_bytes.size());
        } else {
            out["c2pa_binding_bytes"] = nb::none();
        }

        out["c2pa_handoff_requested"] = nb::bool_(include_c2pa_handoff_bytes);
        if (include_c2pa_handoff_bytes && !unsafe_c2pa_package_access) {
            c2pa_handoff_io.status = TransferStatus::UnsafeData;
            c2pa_handoff_io.code   = EmitTransferCode::InvalidArgument;
            c2pa_handoff_io.errors = 1U;
            c2pa_handoff_io.message
                = "safe transfer_probe forbids c2pa handoff bytes; use "
                  "unsafe_transfer_probe(include_c2pa_handoff_bytes=True)";
        } else if (include_c2pa_handoff_bytes) {
            const std::string binding_path
                = !file_options.edit_target_path.empty()
                      ? file_options.edit_target_path
                      : path;
            try {
                std::vector<std::byte> binding_input;
                {
                    nb::gil_scoped_release gil_release;
                    binding_input = read_file_bytes(
                        binding_path.c_str(),
                        file_options.prepare.policy.max_file_bytes);
                }
                build_prepared_c2pa_handoff_package(
                    prepared.bundle,
                    std::span<const std::byte>(binding_input.data(),
                                               binding_input.size()),
                    &c2pa_handoff);
                if (c2pa_handoff.binding.status == TransferStatus::Ok) {
                    c2pa_handoff_io = serialize_prepared_c2pa_handoff_package(
                        c2pa_handoff, &c2pa_handoff_bytes);
                } else {
                    c2pa_handoff_io.status  = c2pa_handoff.binding.status;
                    c2pa_handoff_io.code    = c2pa_handoff.binding.code;
                    c2pa_handoff_io.bytes   = 0U;
                    c2pa_handoff_io.errors  = c2pa_handoff.binding.errors;
                    c2pa_handoff_io.message = c2pa_handoff.binding.message;
                }
            } catch (const std::exception& ex) {
                c2pa_handoff_io.status  = TransferStatus::InvalidArgument;
                c2pa_handoff_io.code    = EmitTransferCode::InvalidArgument;
                c2pa_handoff_io.errors  = 1U;
                c2pa_handoff_io.message = ex.what();
            }
        }
        out["c2pa_handoff_status"]      = c2pa_handoff_io.status;
        out["c2pa_handoff_status_name"] = nb::str(
            transfer_status_name(c2pa_handoff_io.status));
        out["c2pa_handoff_code"]      = c2pa_handoff_io.code;
        out["c2pa_handoff_code_name"] = nb::str(
            emit_transfer_code_name(c2pa_handoff_io.code));
        out["c2pa_handoff_bytes_written"] = nb::int_(c2pa_handoff_io.bytes);
        out["c2pa_handoff_errors"]        = nb::int_(c2pa_handoff_io.errors);
        out["c2pa_handoff_message"] = nb::str(c2pa_handoff_io.message.c_str(),
                                              c2pa_handoff_io.message.size());
        if (include_c2pa_handoff_bytes && unsafe_c2pa_package_access
            && c2pa_handoff_io.status == TransferStatus::Ok) {
            out["c2pa_handoff_bytes"] = nb::bytes(
                reinterpret_cast<const char*>(c2pa_handoff_bytes.data()),
                c2pa_handoff_bytes.size());
        } else {
            out["c2pa_handoff_bytes"] = nb::none();
        }

        out["c2pa_signed_package_requested"] = nb::bool_(
            include_c2pa_signed_package_bytes);
        if (include_c2pa_signed_package_bytes && !unsafe_c2pa_package_access) {
            c2pa_signed_package_io.status = TransferStatus::UnsafeData;
            c2pa_signed_package_io.code   = EmitTransferCode::InvalidArgument;
            c2pa_signed_package_io.errors = 1U;
            c2pa_signed_package_io.message
                = "safe transfer_probe forbids c2pa signed package bytes; "
                  "use unsafe_transfer_probe("
                  "include_c2pa_signed_package_bytes=True)";
        } else if (include_c2pa_signed_package_bytes) {
            if (have_c2pa_signed_package) {
                c2pa_signed_package    = file_options.c2pa_signed_package;
                c2pa_signed_package_io = serialize_prepared_c2pa_signed_package(
                    c2pa_signed_package, &c2pa_signed_package_bytes);
            } else {
                const TransferStatus signed_package_status
                    = build_prepared_c2pa_signed_package(
                        prepared.bundle, file_options.c2pa_signer_input,
                        &c2pa_signed_package);
                if (signed_package_status != TransferStatus::Ok) {
                    c2pa_signed_package_io.status = signed_package_status;
                    c2pa_signed_package_io.code
                        = EmitTransferCode::InvalidArgument;
                    c2pa_signed_package_io.errors = 1U;
                    c2pa_signed_package_io.message
                        = c2pa_signed_package.request.message.empty()
                              ? "failed to build c2pa signed package"
                              : c2pa_signed_package.request.message;
                } else {
                    c2pa_signed_package_io
                        = serialize_prepared_c2pa_signed_package(
                            c2pa_signed_package, &c2pa_signed_package_bytes);
                }
            }
        }
        out["c2pa_signed_package_status"]      = c2pa_signed_package_io.status;
        out["c2pa_signed_package_status_name"] = nb::str(
            transfer_status_name(c2pa_signed_package_io.status));
        out["c2pa_signed_package_code"]      = c2pa_signed_package_io.code;
        out["c2pa_signed_package_code_name"] = nb::str(
            emit_transfer_code_name(c2pa_signed_package_io.code));
        out["c2pa_signed_package_bytes_written"] = nb::int_(
            c2pa_signed_package_io.bytes);
        out["c2pa_signed_package_errors"] = nb::int_(
            c2pa_signed_package_io.errors);
        out["c2pa_signed_package_message"]
            = nb::str(c2pa_signed_package_io.message.c_str(),
                      c2pa_signed_package_io.message.size());
        if (include_c2pa_signed_package_bytes && unsafe_c2pa_package_access
            && c2pa_signed_package_io.status == TransferStatus::Ok) {
            out["c2pa_signed_package_bytes"] = nb::bytes(
                reinterpret_cast<const char*>(c2pa_signed_package_bytes.data()),
                c2pa_signed_package_bytes.size());
        } else {
            out["c2pa_signed_package_bytes"] = nb::none();
        }

        PreparedTransferPayloadBatch transfer_payload_batch;
        PreparedTransferPayloadIoResult transfer_payload_batch_io;
        std::vector<std::byte> transfer_payload_batch_bytes;
        out["transfer_payload_batch_requested"] = nb::bool_(
            include_transfer_payload_batch_bytes);
        if (include_transfer_payload_batch_bytes && !unsafe_payload_access) {
            transfer_payload_batch_io.status = TransferStatus::UnsafeData;
            transfer_payload_batch_io.code = EmitTransferCode::InvalidArgument;
            transfer_payload_batch_io.errors = 1U;
            transfer_payload_batch_io.message
                = "safe transfer_probe forbids transfer payload batch bytes; "
                  "use unsafe_transfer_probe("
                  "include_transfer_payload_batch_bytes=True)";
        } else if (include_transfer_payload_batch_bytes) {
            const EmitTransferResult payload_batch_status
                = build_prepared_transfer_payload_batch(prepared.bundle,
                                                        &transfer_payload_batch);
            if (payload_batch_status.status != TransferStatus::Ok) {
                transfer_payload_batch_io.status = payload_batch_status.status;
                transfer_payload_batch_io.code   = payload_batch_status.code;
                transfer_payload_batch_io.errors = payload_batch_status.errors;
                transfer_payload_batch_io.message = payload_batch_status.message;
            } else {
                transfer_payload_batch_io
                    = serialize_prepared_transfer_payload_batch(
                        transfer_payload_batch, &transfer_payload_batch_bytes);
            }
        }
        out["transfer_payload_batch_status"] = transfer_payload_batch_io.status;
        out["transfer_payload_batch_status_name"] = nb::str(
            transfer_status_name(transfer_payload_batch_io.status));
        out["transfer_payload_batch_code"] = transfer_payload_batch_io.code;
        out["transfer_payload_batch_code_name"] = nb::str(
            emit_transfer_code_name(transfer_payload_batch_io.code));
        out["transfer_payload_batch_bytes_written"] = nb::int_(
            transfer_payload_batch_io.bytes);
        out["transfer_payload_batch_errors"] = nb::int_(
            transfer_payload_batch_io.errors);
        out["transfer_payload_batch_message"]
            = nb::str(transfer_payload_batch_io.message.c_str(),
                      transfer_payload_batch_io.message.size());
        if (include_transfer_payload_batch_bytes && unsafe_payload_access
            && transfer_payload_batch_io.status == TransferStatus::Ok) {
            out["transfer_payload_batch_bytes"]
                = nb::bytes(reinterpret_cast<const char*>(
                                transfer_payload_batch_bytes.data()),
                            transfer_payload_batch_bytes.size());
        } else {
            out["transfer_payload_batch_bytes"] = nb::none();
        }

        PreparedTransferPackageBatch transfer_package_batch;
        PreparedTransferPackageIoResult transfer_package_batch_io;
        std::vector<std::byte> transfer_package_batch_bytes;
        out["transfer_package_batch_requested"] = nb::bool_(
            include_transfer_package_batch_bytes);
        if (include_transfer_package_batch_bytes && !unsafe_payload_access) {
            transfer_package_batch_io.status = TransferStatus::UnsafeData;
            transfer_package_batch_io.code = EmitTransferCode::InvalidArgument;
            transfer_package_batch_io.errors = 1U;
            transfer_package_batch_io.message
                = "safe transfer_probe forbids transfer package batch bytes; "
                  "use unsafe_transfer_probe("
                  "include_transfer_package_batch_bytes=True)";
        } else if (include_transfer_package_batch_bytes) {
            try {
                if (prepared.file_status != TransferFileStatus::Ok
                    || (prepared.prepare.status != TransferStatus::Ok
                        && (!exec.c2pa_stage_requested
                            || exec.c2pa_stage.status != TransferStatus::Ok))) {
                    transfer_package_batch_io.status
                        = TransferStatus::InvalidArgument;
                    transfer_package_batch_io.code
                        = EmitTransferCode::InvalidArgument;
                    transfer_package_batch_io.errors = 1U;
                    transfer_package_batch_io.message
                        = "transfer execution is not available for package batch";
                } else {
                    std::vector<std::byte> package_input;
                    if (exec.edit_requested) {
                        const std::string package_path
                            = !file_options.edit_target_path.empty()
                                  ? file_options.edit_target_path
                                  : path;
                        {
                            nb::gil_scoped_release gil_release;
                            package_input = read_file_bytes(
                                package_path.c_str(),
                                file_options.prepare.policy.max_file_bytes);
                        }
                    }
                    const EmitTransferResult package_batch_status
                        = build_executed_transfer_package_batch(
                            std::span<const std::byte>(package_input.data(),
                                                       package_input.size()),
                            prepared.bundle, exec, &transfer_package_batch);
                    if (package_batch_status.status != TransferStatus::Ok) {
                        transfer_package_batch_io.status
                            = package_batch_status.status;
                        transfer_package_batch_io.code
                            = package_batch_status.code;
                        transfer_package_batch_io.errors
                            = package_batch_status.errors;
                        transfer_package_batch_io.message
                            = package_batch_status.message;
                    } else {
                        transfer_package_batch_io
                            = serialize_prepared_transfer_package_batch(
                                transfer_package_batch,
                                &transfer_package_batch_bytes);
                    }
                }
            } catch (const std::exception& ex) {
                transfer_package_batch_io.status
                    = TransferStatus::InvalidArgument;
                transfer_package_batch_io.code
                    = EmitTransferCode::InvalidArgument;
                transfer_package_batch_io.errors  = 1U;
                transfer_package_batch_io.message = ex.what();
            }
        }
        out["transfer_package_batch_status"] = transfer_package_batch_io.status;
        out["transfer_package_batch_status_name"] = nb::str(
            transfer_status_name(transfer_package_batch_io.status));
        out["transfer_package_batch_code"] = transfer_package_batch_io.code;
        out["transfer_package_batch_code_name"] = nb::str(
            emit_transfer_code_name(transfer_package_batch_io.code));
        out["transfer_package_batch_bytes_written"] = nb::int_(
            transfer_package_batch_io.bytes);
        out["transfer_package_batch_errors"] = nb::int_(
            transfer_package_batch_io.errors);
        out["transfer_package_batch_message"]
            = nb::str(transfer_package_batch_io.message.c_str(),
                      transfer_package_batch_io.message.size());
        if (include_transfer_package_batch_bytes && unsafe_payload_access
            && transfer_package_batch_io.status == TransferStatus::Ok) {
            out["transfer_package_batch_bytes"]
                = nb::bytes(reinterpret_cast<const char*>(
                                transfer_package_batch_bytes.data()),
                            transfer_package_batch_bytes.size());
        } else {
            out["transfer_package_batch_bytes"] = nb::none();
        }

        out["compile_status"]      = exec.compile.status;
        out["compile_status_name"] = nb::str(
            transfer_status_name(exec.compile.status));
        out["compile_code"]      = exec.compile.code;
        out["compile_code_name"] = nb::str(
            emit_transfer_code_name(exec.compile.code));
        out["compile_ops"]     = nb::int_(exec.compiled_ops);
        out["compile_message"] = nb::str(exec.compile.message.c_str(),
                                         exec.compile.message.size());

        out["emit_status"]      = exec.emit.status;
        out["emit_status_name"] = nb::str(
            transfer_status_name(exec.emit.status));
        out["emit_code"]      = exec.emit.code;
        out["emit_code_name"] = nb::str(
            emit_transfer_code_name(exec.emit.code));
        out["emit_emitted"]            = nb::int_(exec.emit.emitted);
        out["emit_skipped"]            = nb::int_(exec.emit.skipped);
        out["emit_errors"]             = nb::int_(exec.emit.errors);
        out["emit_failed_block_index"] = nb::int_(exec.emit.failed_block_index);
        out["emit_message"]            = nb::str(exec.emit.message.c_str(),
                                                 exec.emit.message.size());

        nb::list marker_summary;
        for (size_t i = 0; i < exec.marker_summary.size(); ++i) {
            nb::dict one;
            one["marker"] = nb::int_(exec.marker_summary[i].marker);
            one["count"]  = nb::int_(exec.marker_summary[i].count);
            one["bytes"]  = nb::int_(exec.marker_summary[i].bytes);
            marker_summary.append(std::move(one));
        }
        out["marker_summary"] = std::move(marker_summary);

        nb::list tiff_tag_summary;
        for (size_t i = 0; i < exec.tiff_tag_summary.size(); ++i) {
            nb::dict one;
            one["tag"]   = nb::int_(exec.tiff_tag_summary[i].tag);
            one["count"] = nb::int_(exec.tiff_tag_summary[i].count);
            one["bytes"] = nb::int_(exec.tiff_tag_summary[i].bytes);
            tiff_tag_summary.append(std::move(one));
        }
        out["tiff_tag_summary"] = std::move(tiff_tag_summary);
        out["tiff_commit"]      = nb::bool_(exec.tiff_commit);

        nb::list jxl_box_summary;
        for (size_t i = 0; i < exec.jxl_box_summary.size(); ++i) {
            nb::dict one;
            one["type"]  = nb::str(exec.jxl_box_summary[i].type.data(),
                                   exec.jxl_box_summary[i].type.size());
            one["count"] = nb::int_(exec.jxl_box_summary[i].count);
            one["bytes"] = nb::int_(exec.jxl_box_summary[i].bytes);
            jxl_box_summary.append(std::move(one));
        }
        out["jxl_box_summary"] = std::move(jxl_box_summary);
        if (prepared.bundle.target_format == TransferTargetFormat::Jxl) {
            PreparedJxlEncoderHandoffView jxl_handoff;
            const EmitTransferResult jxl_handoff_result
                = build_prepared_jxl_encoder_handoff_view(prepared.bundle,
                                                          &jxl_handoff);
            nb::dict one;
            one["status"]      = jxl_handoff_result.status;
            one["status_name"] = nb::str(
                transfer_status_name(jxl_handoff_result.status));
            one["code"]      = jxl_handoff_result.code;
            one["code_name"] = nb::str(
                emit_transfer_code_name(jxl_handoff_result.code));
            one["errors"]          = nb::int_(jxl_handoff_result.errors);
            one["message"]         = nb::str(jxl_handoff_result.message.c_str(),
                                             jxl_handoff_result.message.size());
            one["has_icc_profile"] = nb::bool_(jxl_handoff.has_icc_profile);
            one["icc_block_index"] = nb::int_(jxl_handoff.icc_block_index);
            one["icc_profile_bytes"] = nb::int_(jxl_handoff.icc_profile_bytes);
            one["box_count"]         = nb::int_(jxl_handoff.box_count);
            one["box_payload_bytes"] = nb::int_(jxl_handoff.box_payload_bytes);
            out["jxl_encoder_handoff"] = std::move(one);
        } else {
            out["jxl_encoder_handoff"] = nb::none();
        }
        PreparedJxlEncoderHandoff jxl_encoder_handoff_owned;
        PreparedJxlEncoderHandoffIoResult jxl_encoder_handoff_io;
        std::vector<std::byte> jxl_encoder_handoff_bytes;
        out["jxl_encoder_handoff_requested"] = nb::bool_(
            include_jxl_encoder_handoff_bytes);
        if (include_jxl_encoder_handoff_bytes && !unsafe_payload_access) {
            jxl_encoder_handoff_io.status = TransferStatus::UnsafeData;
            jxl_encoder_handoff_io.code   = EmitTransferCode::InvalidArgument;
            jxl_encoder_handoff_io.errors = 1U;
            jxl_encoder_handoff_io.message
                = "safe transfer_probe forbids jxl encoder handoff bytes; "
                  "use unsafe_transfer_probe("
                  "include_jxl_encoder_handoff_bytes=True)";
        } else if (include_jxl_encoder_handoff_bytes) {
            if (prepared.bundle.target_format != TransferTargetFormat::Jxl) {
                jxl_encoder_handoff_io.status = TransferStatus::InvalidArgument;
                jxl_encoder_handoff_io.code = EmitTransferCode::InvalidArgument;
                jxl_encoder_handoff_io.errors = 1U;
                jxl_encoder_handoff_io.message
                    = "jxl encoder handoff bytes require target_format=Jxl";
            } else {
                const EmitTransferResult handoff_status
                    = build_prepared_jxl_encoder_handoff(
                        prepared.bundle, &jxl_encoder_handoff_owned);
                if (handoff_status.status != TransferStatus::Ok) {
                    jxl_encoder_handoff_io.status  = handoff_status.status;
                    jxl_encoder_handoff_io.code    = handoff_status.code;
                    jxl_encoder_handoff_io.errors  = handoff_status.errors;
                    jxl_encoder_handoff_io.message = handoff_status.message;
                } else {
                    jxl_encoder_handoff_io
                        = serialize_prepared_jxl_encoder_handoff(
                            jxl_encoder_handoff_owned,
                            &jxl_encoder_handoff_bytes);
                }
            }
        }
        out["jxl_encoder_handoff_status"]      = jxl_encoder_handoff_io.status;
        out["jxl_encoder_handoff_status_name"] = nb::str(
            transfer_status_name(jxl_encoder_handoff_io.status));
        out["jxl_encoder_handoff_code"]      = jxl_encoder_handoff_io.code;
        out["jxl_encoder_handoff_code_name"] = nb::str(
            emit_transfer_code_name(jxl_encoder_handoff_io.code));
        out["jxl_encoder_handoff_bytes_written"] = nb::int_(
            jxl_encoder_handoff_io.bytes);
        out["jxl_encoder_handoff_errors"] = nb::int_(
            jxl_encoder_handoff_io.errors);
        out["jxl_encoder_handoff_message"]
            = nb::str(jxl_encoder_handoff_io.message.c_str(),
                      jxl_encoder_handoff_io.message.size());
        if (include_jxl_encoder_handoff_bytes && unsafe_payload_access
            && jxl_encoder_handoff_io.status == TransferStatus::Ok) {
            out["jxl_encoder_handoff_bytes"] = nb::bytes(
                reinterpret_cast<const char*>(jxl_encoder_handoff_bytes.data()),
                jxl_encoder_handoff_bytes.size());
        } else {
            out["jxl_encoder_handoff_bytes"] = nb::none();
        }

        nb::list webp_chunk_summary;
        for (size_t i = 0; i < exec.webp_chunk_summary.size(); ++i) {
            nb::dict one;
            one["type"]  = nb::str(exec.webp_chunk_summary[i].type.data(),
                                   exec.webp_chunk_summary[i].type.size());
            one["count"] = nb::int_(exec.webp_chunk_summary[i].count);
            one["bytes"] = nb::int_(exec.webp_chunk_summary[i].bytes);
            webp_chunk_summary.append(std::move(one));
        }
        out["webp_chunk_summary"] = std::move(webp_chunk_summary);

        nb::list png_chunk_summary;
        for (size_t i = 0; i < exec.png_chunk_summary.size(); ++i) {
            nb::dict one;
            one["type"]  = nb::str(exec.png_chunk_summary[i].type.data(),
                                   exec.png_chunk_summary[i].type.size());
            one["count"] = nb::int_(exec.png_chunk_summary[i].count);
            one["bytes"] = nb::int_(exec.png_chunk_summary[i].bytes);
            png_chunk_summary.append(std::move(one));
        }
        out["png_chunk_summary"] = std::move(png_chunk_summary);

        nb::list jp2_box_summary;
        for (size_t i = 0; i < exec.jp2_box_summary.size(); ++i) {
            nb::dict one;
            one["type"]  = nb::str(exec.jp2_box_summary[i].type.data(),
                                   exec.jp2_box_summary[i].type.size());
            one["count"] = nb::int_(exec.jp2_box_summary[i].count);
            one["bytes"] = nb::int_(exec.jp2_box_summary[i].bytes);
            jp2_box_summary.append(std::move(one));
        }
        out["jp2_box_summary"] = std::move(jp2_box_summary);

        nb::list exr_attribute_summary;
        for (size_t i = 0; i < exec.exr_attribute_summary.size(); ++i) {
            nb::dict one;
            one["name"] = nb::str(exec.exr_attribute_summary[i].name.c_str(),
                                  exec.exr_attribute_summary[i].name.size());
            one["type_name"] = nb::str(
                exec.exr_attribute_summary[i].type_name.c_str(),
                exec.exr_attribute_summary[i].type_name.size());
            one["count"] = nb::int_(exec.exr_attribute_summary[i].count);
            one["bytes"] = nb::int_(exec.exr_attribute_summary[i].bytes);
            exr_attribute_summary.append(std::move(one));
        }
        out["exr_attribute_summary"] = std::move(exr_attribute_summary);

        ExrAdapterBatch exr_attribute_batch_owned;
        ExrAdapterResult exr_attribute_batch_result;
        const bool exr_target = (prepared.bundle.target_format
                                 == TransferTargetFormat::Exr);
        if (exr_target) {
            exr_attribute_batch_result = build_prepared_exr_attribute_batch(
                prepared.bundle, &exr_attribute_batch_owned);
        } else if (include_exr_attribute_values) {
            exr_attribute_batch_result.status  = ExrAdapterStatus::InvalidArgument;
            exr_attribute_batch_result.errors  = 1U;
            exr_attribute_batch_result.message
                = "exr attribute values require target_format=Exr";
        }

        TransferStatus exr_attribute_values_status = TransferStatus::Ok;
        std::string exr_attribute_values_message;
        if (include_exr_attribute_values && !unsafe_payload_access) {
            exr_attribute_values_status = TransferStatus::UnsafeData;
            exr_attribute_values_message
                = "safe transfer_probe forbids exr attribute values; use "
                  "unsafe_transfer_probe(include_exr_attribute_values=True)";
        } else if (include_exr_attribute_values
                   && exr_attribute_batch_result.status
                          == ExrAdapterStatus::InvalidArgument) {
            exr_attribute_values_status = TransferStatus::InvalidArgument;
            exr_attribute_values_message
                = exr_attribute_batch_result.message;
        } else if (include_exr_attribute_values
                   && exr_attribute_batch_result.status
                          == ExrAdapterStatus::Unsupported) {
            exr_attribute_values_status = TransferStatus::Unsupported;
            exr_attribute_values_message
                = exr_attribute_batch_result.message;
        }

        nb::list exr_attribute_batch;
        if (exr_target && exr_attribute_batch_result.status
                              == ExrAdapterStatus::Ok) {
            const bool include_values = include_exr_attribute_values
                                        && unsafe_payload_access;
            for (size_t i = 0; i < exr_attribute_batch_owned.attributes.size();
                 ++i) {
                const ExrAdapterAttribute& attr
                    = exr_attribute_batch_owned.attributes[i];
                nb::dict one;
                one["part_index"] = nb::int_(attr.part_index);
                one["name"] = nb::str(attr.name.c_str(), attr.name.size());
                one["type_name"]
                    = nb::str(attr.type_name.c_str(), attr.type_name.size());
                one["is_opaque"] = nb::bool_(attr.is_opaque);
                one["bytes"] = nb::int_(static_cast<uint64_t>(attr.value.size()));
                if (include_values) {
                    one["value"] = nb::bytes(
                        reinterpret_cast<const char*>(attr.value.data()),
                        attr.value.size());
                } else {
                    one["value"] = nb::none();
                }
                exr_attribute_batch.append(std::move(one));
            }
            out["exr_attribute_batch"] = std::move(exr_attribute_batch);
        } else {
            out["exr_attribute_batch"] = nb::none();
        }
        if (exr_target || include_exr_attribute_values) {
            out["exr_attribute_batch_status"] = exr_attribute_batch_result.status;
            out["exr_attribute_batch_status_name"] = nb::str(
                exr_adapter_status_name(exr_attribute_batch_result.status));
        } else {
            out["exr_attribute_batch_status"] = nb::none();
            out["exr_attribute_batch_status_name"] = nb::none();
        }
        out["exr_attribute_batch_exported"]
            = nb::int_(exr_attribute_batch_result.exported);
        out["exr_attribute_batch_skipped"]
            = nb::int_(exr_attribute_batch_result.skipped);
        out["exr_attribute_batch_errors"]
            = nb::int_(exr_attribute_batch_result.errors);
        out["exr_attribute_batch_message"] = nb::str(
            exr_attribute_batch_result.message.c_str(),
            exr_attribute_batch_result.message.size());
        out["exr_attribute_values_requested"]
            = nb::bool_(include_exr_attribute_values);
        out["exr_attribute_values_status"] = exr_attribute_values_status;
        out["exr_attribute_values_status_name"] = nb::str(
            transfer_status_name(exr_attribute_values_status));
        out["exr_attribute_values_message"] = nb::str(
            exr_attribute_values_message.c_str(),
            exr_attribute_values_message.size());

        nb::list bmff_item_summary;
        for (size_t i = 0; i < exec.bmff_item_summary.size(); ++i) {
            nb::dict one;
            one["item_type"] = nb::int_(exec.bmff_item_summary[i].item_type);
            one["count"]     = nb::int_(exec.bmff_item_summary[i].count);
            one["bytes"]     = nb::int_(exec.bmff_item_summary[i].bytes);
            one["mime_xmp"]  = nb::bool_(exec.bmff_item_summary[i].mime_xmp);
            bmff_item_summary.append(std::move(one));
        }
        out["bmff_item_summary"] = std::move(bmff_item_summary);

        nb::list bmff_property_summary;
        for (size_t i = 0; i < exec.bmff_property_summary.size(); ++i) {
            nb::dict one;
            one["property_type"] = nb::int_(
                exec.bmff_property_summary[i].property_type);
            one["property_subtype"] = nb::int_(
                exec.bmff_property_summary[i].property_subtype);
            one["count"] = nb::int_(exec.bmff_property_summary[i].count);
            one["bytes"] = nb::int_(exec.bmff_property_summary[i].bytes);
            bmff_property_summary.append(std::move(one));
        }
        out["bmff_property_summary"] = std::move(bmff_property_summary);

        out["edit_requested"]        = nb::bool_(exec.edit_requested);
        out["edit_plan_status"]      = exec.edit_plan_status;
        out["edit_plan_status_name"] = nb::str(
            transfer_status_name(exec.edit_plan_status));
        out["edit_plan_message"]      = nb::str(exec.edit_plan_message.c_str(),
                                                exec.edit_plan_message.size());
        out["edit_apply_status"]      = exec.edit_apply.status;
        out["edit_apply_status_name"] = nb::str(
            transfer_status_name(exec.edit_apply.status));
        out["edit_apply_code"]      = exec.edit_apply.code;
        out["edit_apply_code_name"] = nb::str(
            emit_transfer_code_name(exec.edit_apply.code));
        out["edit_apply_emitted"] = nb::int_(exec.edit_apply.emitted);
        out["edit_apply_skipped"] = nb::int_(exec.edit_apply.skipped);
        out["edit_apply_errors"]  = nb::int_(exec.edit_apply.errors);
        out["edit_apply_message"] = nb::str(exec.edit_apply.message.c_str(),
                                            exec.edit_apply.message.size());
        out["edit_input_size"]    = nb::int_(exec.edit_input_size);
        out["edit_output_size"]   = nb::int_(exec.edit_output_size);
        out["edit_removed_existing_segments"] = nb::int_(
            exec.jpeg_edit_plan.removed_existing_segments);
        out["edit_removed_existing_jumbf_segments"] = nb::int_(
            exec.jpeg_edit_plan.removed_existing_jumbf_segments);
        out["edit_removed_existing_c2pa_segments"] = nb::int_(
            exec.jpeg_edit_plan.removed_existing_c2pa_segments);

        const bool allow_edited_bytes = include_edited_bytes
                                        && unsafe_edited_bytes_access;
        if (allow_edited_bytes
            && exec.edit_apply.status == TransferStatus::Ok) {
            out["edited_bytes"] = nb::bytes(reinterpret_cast<const char*>(
                                                exec.edited_output.data()),
                                            exec.edited_output.size());
        } else {
            out["edited_bytes"] = nb::none();
        }

        TransferStatus overall_status = TransferStatus::Ok;
        std::string error_stage       = "none";
        std::string error_code        = "none";
        std::string error_message;

        if (include_payloads && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_payloads_forbidden";
            error_message  = "safe transfer_probe forbids payload bytes; use "
                             "unsafe_transfer_probe(include_payloads=True)";
        } else if (include_transfer_payload_batch_bytes
                   && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_transfer_payload_batch_forbidden";
            error_message  = "safe transfer_probe forbids transfer payload "
                             "batch bytes; use unsafe_transfer_probe("
                             "include_transfer_payload_batch_bytes=True)";
        } else if (include_transfer_package_batch_bytes
                   && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_transfer_package_batch_forbidden";
            error_message  = "safe transfer_probe forbids transfer package "
                             "batch bytes; use unsafe_transfer_probe("
                             "include_transfer_package_batch_bytes=True)";
        } else if (include_exr_attribute_values && !unsafe_payload_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_exr_attribute_values_forbidden";
            error_message  = "safe transfer_probe forbids exr attribute "
                             "values; use unsafe_transfer_probe("
                             "include_exr_attribute_values=True)";
        } else if (include_edited_bytes && !unsafe_edited_bytes_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_edited_bytes_forbidden";
            error_message  = "safe transfer_probe forbids edited bytes; use "
                             "unsafe_transfer_probe(include_edited_bytes=True)";
        } else if (include_c2pa_binding_bytes && !unsafe_c2pa_binding_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_c2pa_binding_bytes_forbidden";
            error_message  = "safe transfer_probe forbids c2pa binding bytes; "
                             "use unsafe_transfer_probe("
                             "include_c2pa_binding_bytes=True)";
        } else if ((include_c2pa_handoff_bytes
                    || include_c2pa_signed_package_bytes)
                   && !unsafe_c2pa_package_access) {
            overall_status = TransferStatus::UnsafeData;
            error_stage    = "api";
            error_code     = "unsafe_c2pa_package_bytes_forbidden";
            error_message  = "safe transfer_probe forbids c2pa package bytes; "
                             "use unsafe_transfer_probe("
                             "include_c2pa_handoff_bytes=True or "
                             "include_c2pa_signed_package_bytes=True)";
        } else if (exec.c2pa_stage_requested
                   && exec.c2pa_stage.status != TransferStatus::Ok) {
            overall_status = exec.c2pa_stage.status;
            error_stage    = "c2pa_stage";
            error_code     = emit_transfer_code_name(exec.c2pa_stage.code);
            error_message  = exec.c2pa_stage.message;
        } else if (include_c2pa_signed_package_bytes
                   && c2pa_signed_package_io.status != TransferStatus::Ok) {
            overall_status = c2pa_signed_package_io.status;
            error_stage    = "c2pa_signed_package";
            error_code = emit_transfer_code_name(c2pa_signed_package_io.code);
            error_message = c2pa_signed_package_io.message;
        } else if (include_c2pa_handoff_bytes
                   && c2pa_handoff_io.status != TransferStatus::Ok) {
            overall_status = c2pa_handoff_io.status;
            error_stage    = "c2pa_handoff";
            error_code     = emit_transfer_code_name(c2pa_handoff_io.code);
            error_message  = c2pa_handoff_io.message;
        } else if (include_c2pa_binding_bytes
                   && c2pa_handoff.binding.status != TransferStatus::Ok) {
            overall_status = c2pa_handoff.binding.status;
            error_stage    = "c2pa_binding";
            error_code     = emit_transfer_code_name(c2pa_handoff.binding.code);
            error_message  = c2pa_handoff.binding.message;
        } else if (include_transfer_payload_batch_bytes
                   && transfer_payload_batch_io.status != TransferStatus::Ok) {
            overall_status = transfer_payload_batch_io.status;
            error_stage    = "transfer_payload_batch";
            error_code     = emit_transfer_code_name(
                transfer_payload_batch_io.code);
            error_message = transfer_payload_batch_io.message;
        } else if (include_transfer_package_batch_bytes
                   && transfer_package_batch_io.status != TransferStatus::Ok) {
            overall_status = transfer_package_batch_io.status;
            error_stage    = "transfer_package_batch";
            error_code     = emit_transfer_code_name(
                transfer_package_batch_io.code);
            error_message = transfer_package_batch_io.message;
        } else if (exr_target
                   && exr_attribute_batch_result.status
                          != ExrAdapterStatus::Ok) {
            if (exr_attribute_batch_result.status
                == ExrAdapterStatus::InvalidArgument) {
                overall_status = TransferStatus::InvalidArgument;
            } else if (exr_attribute_batch_result.status
                       == ExrAdapterStatus::Unsupported) {
                overall_status = TransferStatus::Unsupported;
            } else {
                overall_status = TransferStatus::InternalError;
            }
            error_stage   = "exr_attribute_batch";
            error_code    = exr_adapter_status_name(
                exr_attribute_batch_result.status);
            error_message = exr_attribute_batch_result.message;
        } else if (include_exr_attribute_values
                   && exr_attribute_values_status
                          != TransferStatus::Ok) {
            overall_status = exr_attribute_values_status;
            error_stage    = "exr_attribute_values";
            error_code     = transfer_status_name(exr_attribute_values_status);
            error_message  = exr_attribute_values_message;
        } else if (exec.time_patch.status != TransferStatus::Ok) {
            overall_status = exec.time_patch.status;
            error_stage    = "time_patch";
            error_code     = "apply_time_patches_failed";
            error_message  = exec.time_patch.message;
        } else if (prepared.file_status != TransferFileStatus::Ok) {
            overall_status = transfer_status_from_file_status(
                prepared.file_status);
            error_stage   = "file";
            error_code    = prepare_transfer_file_code_name(prepared.code);
            error_message = prepared.prepare.message;
        } else if (prepared.prepare.status != TransferStatus::Ok
                   && (!exec.c2pa_stage_requested
                       || exec.c2pa_stage.status != TransferStatus::Ok)) {
            overall_status = prepared.prepare.status;
            error_stage    = "prepare";
            error_code     = prepare_transfer_code_name(prepared.prepare.code);
            error_message  = prepared.prepare.message;
        } else if (exec.compile.status != TransferStatus::Ok) {
            overall_status = exec.compile.status;
            error_stage    = "compile";
            error_code     = emit_transfer_code_name(exec.compile.code);
            error_message  = exec.compile.message;
        } else if (exec.edit_requested
                   && exec.edit_plan_status != TransferStatus::Ok) {
            overall_status = exec.edit_plan_status;
            error_stage    = "edit_plan";
            error_code     = "edit_plan_failed";
            error_message  = exec.edit_plan_message;
        } else if (exec.edit_requested && edit_do_apply
                   && exec.edit_apply.status != TransferStatus::Ok) {
            overall_status = exec.edit_apply.status;
            error_stage    = "edit_apply";
            error_code     = emit_transfer_code_name(exec.edit_apply.code);
            error_message  = exec.edit_apply.message;
        } else if (exec.emit.status != TransferStatus::Ok) {
            overall_status = exec.emit.status;
            error_stage    = "emit";
            error_code     = emit_transfer_code_name(exec.emit.code);
            error_message  = exec.emit.message;
        } else if (persist_requested && persisted.output_status
                                        != TransferStatus::Ok) {
            overall_status = persisted.output_status;
            error_stage    = "persist_output";
            error_code     = "persist_output_failed";
            error_message  = persisted.output_message;
        } else if (persist_requested && executed.xmp_sidecar_requested
                   && persisted.xmp_sidecar_status != TransferStatus::Ok) {
            overall_status = persisted.xmp_sidecar_status;
            error_stage    = "persist_xmp_sidecar";
            error_code     = "persist_xmp_sidecar_failed";
            error_message  = persisted.xmp_sidecar_message;
        } else if (persist_requested
                   && executed.xmp_sidecar_cleanup_requested
                   && persisted.xmp_sidecar_cleanup_status
                          != TransferStatus::Ok) {
            overall_status = persisted.xmp_sidecar_cleanup_status;
            error_stage    = "persist_xmp_sidecar_cleanup";
            error_code     = "persist_xmp_sidecar_cleanup_failed";
            error_message  = persisted.xmp_sidecar_cleanup_message;
        }

        out["overall_status"]      = overall_status;
        out["overall_status_name"] = nb::str(
            transfer_status_name(overall_status));
        out["error_stage"]   = nb::str(error_stage.c_str(), error_stage.size());
        out["error_code"]    = nb::str(error_code.c_str(), error_code.size());
        out["error_message"] = nb::str(error_message.c_str(),
                                       error_message.size());
        return out;
    }

    static nb::list unsafe_oiio_attributes_to_python(
        const MetaStore& store, uint32_t max_value_bytes,
        ExportNamePolicy name_policy, bool include_makernotes,
        bool include_empty)
    {
        OiioAdapterRequest request;
        request.max_value_bytes    = max_value_bytes;
        request.include_empty      = include_empty;
        request.name_policy        = name_policy;
        request.include_makernotes = include_makernotes;

        std::vector<OiioAttribute> attrs;
        collect_oiio_attributes(store, &attrs, request);

        nb::list out;
        for (size_t i = 0; i < attrs.size(); ++i) {
            out.append(nb::make_tuple(
                nb::str(attrs[i].name.c_str(), attrs[i].name.size()),
                nb::str(attrs[i].value.c_str(), attrs[i].value.size())));
        }
        return out;
    }

    static std::string
    format_safety_error_message(const InteropSafetyError& error)
    {
        std::string msg = error.message.empty() ? "unsafe metadata value"
                                                : error.message;
        if (!error.field_name.empty()) {
            msg.append(" [field=");
            msg.append(error.field_name);
            msg.push_back(']');
        }
        if (!error.key_path.empty()) {
            msg.append(" [key=");
            msg.append(error.key_path);
            msg.push_back(']');
        }
        return msg;
    }

    static void throw_safety_error(const InteropSafetyError& error)
    {
        throw std::runtime_error(format_safety_error_message(error));
    }

    static nb::str decode_text_safe_for_python(std::span<const std::byte> bytes,
                                               TextEncoding encoding)
    {
        PyObject* decoded = nullptr;
        switch (encoding) {
        case TextEncoding::Ascii:
            decoded = PyUnicode_DecodeASCII(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<Py_ssize_t>(bytes.size()), "strict");
            break;
        case TextEncoding::Utf8:
        case TextEncoding::Unknown:
            decoded = PyUnicode_DecodeUTF8(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<Py_ssize_t>(bytes.size()), "strict");
            break;
        case TextEncoding::Utf16LE: {
            int byteorder = -1;
            decoded       = PyUnicode_DecodeUTF16(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<Py_ssize_t>(bytes.size()), "strict", &byteorder);
            break;
        }
        case TextEncoding::Utf16BE: {
            int byteorder = 1;
            decoded       = PyUnicode_DecodeUTF16(
                reinterpret_cast<const char*>(bytes.data()),
                static_cast<Py_ssize_t>(bytes.size()), "strict", &byteorder);
            break;
        }
        }

        if (!decoded) {
            PyErr_Clear();
            throw std::runtime_error(
                "unsafe text value: invalid or unsupported encoding");
        }
        return nb::steal<nb::str>(nb::handle(decoded));
    }

    static nb::dict icc_interpret_to_python(uint32_t signature,
                                            nb::bytes tag_bytes,
                                            uint32_t max_values,
                                            uint32_t max_text_bytes)
    {
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(tag_bytes.data()),
            tag_bytes.size());
        IccTagInterpretOptions options;
        options.limits.max_values     = max_values;
        options.limits.max_text_bytes = max_text_bytes;

        IccTagInterpretation interpretation;
        const IccTagInterpretStatus status
            = interpret_icc_tag(signature, bytes, &interpretation, options);

        nb::dict out;
        out["status"]    = status;
        out["signature"] = nb::int_(interpretation.signature);
        if (!interpretation.name.empty()) {
            out["name"] = nb::str(interpretation.name.data(),
                                  interpretation.name.size());
        } else {
            out["name"] = nb::none();
        }
        out["type"] = nb::str(interpretation.type.c_str(),
                              interpretation.type.size());

        if (!interpretation.text.empty()) {
            const std::span<const std::byte> text_bytes(
                reinterpret_cast<const std::byte*>(interpretation.text.data()),
                interpretation.text.size());
            out["text"] = decode_text_safe_for_python(text_bytes,
                                                      TextEncoding::Ascii);
        } else {
            out["text"] = nb::none();
        }

        nb::list values;
        for (size_t i = 0; i < interpretation.values.size(); ++i) {
            values.append(nb::float_(interpretation.values[i]));
        }
        out["values"] = std::move(values);
        out["rows"]   = nb::int_(interpretation.rows);
        out["cols"]   = nb::int_(interpretation.cols);
        return out;
    }

    static nb::object icc_render_value_to_python(uint32_t signature,
                                                 nb::bytes tag_bytes,
                                                 uint32_t max_values,
                                                 uint32_t max_text_bytes)
    {
        const std::span<const std::byte> bytes(
            reinterpret_cast<const std::byte*>(tag_bytes.data()),
            tag_bytes.size());
        std::string rendered;
        if (!format_icc_tag_display_value(signature, bytes, max_values,
                                          max_text_bytes, &rendered)) {
            return nb::none();
        }
        const std::span<const std::byte> text_bytes(
            reinterpret_cast<const std::byte*>(rendered.data()),
            rendered.size());
        return decode_text_safe_for_python(text_bytes, TextEncoding::Utf8);
    }

    static nb::list oiio_attributes_to_python(const MetaStore& store,
                                              uint32_t max_value_bytes,
                                              ExportNamePolicy name_policy,
                                              bool include_makernotes,
                                              bool include_empty)
    {
        OiioAdapterRequest request;
        request.max_value_bytes    = max_value_bytes;
        request.include_empty      = include_empty;
        request.name_policy        = name_policy;
        request.include_makernotes = include_makernotes;

        InteropSafetyError error;
        std::vector<OiioAttribute> attrs;
        const InteropSafetyStatus status
            = collect_oiio_attributes_safe(store, &attrs, request, &error);
        if (status != InteropSafetyStatus::Ok) {
            throw_safety_error(error);
        }

        nb::list out;
        for (size_t i = 0; i < attrs.size(); ++i) {
            out.append(nb::make_tuple(
                nb::str(attrs[i].name.c_str(), attrs[i].name.size()),
                nb::str(attrs[i].value.c_str(), attrs[i].value.size())));
        }
        return out;
    }

    static nb::object oiio_typed_value_to_python(const OiioTypedValue& typed,
                                                 bool unsafe_text)
    {
        if (typed.kind == MetaValueKind::Text) {
            const std::span<const std::byte> raw(typed.storage.data(),
                                                 typed.storage.size());
            if (unsafe_text) {
                return nb::bytes(reinterpret_cast<const char*>(raw.data()),
                                 raw.size());
            }
            return decode_text_safe_for_python(raw, typed.text_encoding);
        }
        if (typed.kind == MetaValueKind::Bytes) {
            if (unsafe_text) {
                return nb::bytes(reinterpret_cast<const char*>(
                                     typed.storage.data()),
                                 typed.storage.size());
            }
            throw std::runtime_error(
                "unsafe bytes value in typed export; use unsafe_oiio_attributes_typed()");
        }

        MetaValue value;
        value.kind          = typed.kind;
        value.elem_type     = typed.elem_type;
        value.text_encoding = typed.text_encoding;
        value.count         = typed.count;
        value.data          = typed.data;

        ByteArena arena;
        if (typed.kind == MetaValueKind::Array
            || typed.kind == MetaValueKind::Bytes
            || typed.kind == MetaValueKind::Text) {
            if (!typed.storage.empty()) {
                value.data.span = arena.append(
                    std::span<const std::byte>(typed.storage.data(),
                                               typed.storage.size()));
            } else {
                value.data.span = ByteSpan {};
            }
        }
        return value_to_python(arena, value, 0U, 0U);
    }


    static nb::list oiio_typed_attributes_to_python(
        const MetaStore& store, uint32_t max_value_bytes,
        ExportNamePolicy name_policy, bool include_makernotes,
        bool include_empty, bool unsafe_text)
    {
        OiioAdapterRequest request;
        request.max_value_bytes    = max_value_bytes;
        request.include_empty      = include_empty;
        request.name_policy        = name_policy;
        request.include_makernotes = include_makernotes;

        std::vector<OiioTypedAttribute> attrs;
        if (unsafe_text) {
            collect_oiio_attributes_typed(store, &attrs, request);
        } else {
            InteropSafetyError error;
            const InteropSafetyStatus status
                = collect_oiio_attributes_typed_safe(store, &attrs, request,
                                                     &error);
            if (status != InteropSafetyStatus::Ok) {
                throw_safety_error(error);
            }
        }

        nb::list out;
        for (size_t i = 0; i < attrs.size(); ++i) {
            out.append(nb::make_tuple(
                nb::str(attrs[i].name.c_str(), attrs[i].name.size()),
                oiio_typed_value_to_python(attrs[i].value, unsafe_text)));
        }
        return out;
    }


    static nb::dict ocio_node_to_python(const OcioMetadataNode& node)
    {
        nb::dict out;
        out["name"]  = nb::str(node.name.c_str(), node.name.size());
        out["value"] = nb::str(node.value.c_str(), node.value.size());
        nb::list children;
        for (size_t i = 0; i < node.children.size(); ++i) {
            children.append(ocio_node_to_python(node.children[i]));
        }
        out["children"] = std::move(children);
        return out;
    }

    static nb::dict unsafe_ocio_metadata_tree_to_python(
        const MetaStore& store, ExportNameStyle style,
        ExportNamePolicy name_policy, uint32_t max_value_bytes,
        bool include_makernotes, bool include_empty)
    {
        OcioAdapterRequest request;
        request.style              = style;
        request.name_policy        = name_policy;
        request.max_value_bytes    = max_value_bytes;
        request.include_makernotes = include_makernotes;
        request.include_empty      = include_empty;

        OcioMetadataNode root;
        build_ocio_metadata_tree(store, &root, request);
        return ocio_node_to_python(root);
    }

    static nb::dict
    ocio_tree_to_python(const MetaStore& store, ExportNameStyle style,
                        ExportNamePolicy name_policy, uint32_t max_value_bytes,
                        bool include_makernotes, bool include_empty)
    {
        OcioAdapterRequest request;
        request.style              = style;
        request.name_policy        = name_policy;
        request.max_value_bytes    = max_value_bytes;
        request.include_makernotes = include_makernotes;
        request.include_empty      = include_empty;

        OcioMetadataNode root;
        InteropSafetyError error;
        const InteropSafetyStatus status
            = build_ocio_metadata_tree_safe(store, &root, request, &error);
        if (status != InteropSafetyStatus::Ok) {
            throw_safety_error(error);
        }

        return ocio_node_to_python(root);
    }

}  // namespace

struct PyDocument final {
    std::string path;
    MappedFile file;
    std::span<const std::byte> file_bytes;
    std::vector<ContainerBlockRef> blocks;
    std::vector<ExifIfdRef> ifds;
    std::vector<std::byte> payload;
    std::vector<uint32_t> payload_parts;
    MetaStore store;
    SimpleMetaResult result;
};

struct PyEntry final {
    std::shared_ptr<PyDocument> doc;
    EntryId id = kInvalidEntryId;
};

static std::shared_ptr<PyDocument>
read_document(const std::string& path, bool include_pointer_tags,
              bool decode_makernote, bool decompress, bool include_xmp_sidecar,
              bool verify_c2pa, C2paVerifyBackend verify_backend,
              bool verify_require_resolved_references, uint64_t max_file_bytes,
              const OpenMetaResourcePolicy* policy_ptr)
{
    auto doc  = std::make_shared<PyDocument>();
    doc->path = path;

    OpenMetaResourcePolicy policy;
    policy.max_file_bytes = max_file_bytes;
    if (policy_ptr) {
        policy = *policy_ptr;
        if (max_file_bytes != 0U) {
            policy.max_file_bytes = max_file_bytes;
        }
    }

    SimpleMetaDecodeOptions decode_options;
    apply_resource_policy(policy, &decode_options.exif,
                          &decode_options.payload);
    apply_resource_policy(policy, &decode_options.xmp, &decode_options.exr,
                          &decode_options.jumbf, &decode_options.icc,
                          &decode_options.iptc, &decode_options.photoshop_irb);
    decode_options.xmp.malformed_mode = XmpDecodeMalformedMode::OutputTruncated;
    decode_options.exif.include_pointer_tags       = include_pointer_tags;
    decode_options.exif.decode_makernote           = decode_makernote;
    decode_options.exif.decode_embedded_containers = true;
    decode_options.payload.decompress              = decompress;
    decode_options.jumbf.verify_c2pa               = verify_c2pa;
    decode_options.jumbf.verify_backend            = verify_backend;
    decode_options.jumbf.verify_require_resolved_references
        = verify_require_resolved_references;

    // Release the GIL while performing file I/O and metadata decoding so callers
    // (and internal comparison tools) can read in parallel from multiple Python
    // threads. All work below this point is pure C/C++ and does not touch the
    // Python C API.
    nb::gil_scoped_release gil_release;

    const MappedFileStatus st = doc->file.open(path.c_str(),
                                               policy.max_file_bytes);
    if (st != MappedFileStatus::Ok) {
        if (st == MappedFileStatus::TooLarge) {
            throw std::runtime_error("file too large");
        }
        if (st == MappedFileStatus::OpenFailed) {
            throw std::runtime_error("failed to open file");
        }
        if (st == MappedFileStatus::StatFailed) {
            throw std::runtime_error("failed to stat file");
        }
        throw std::runtime_error("failed to map file");
    }
    doc->file_bytes = doc->file.bytes();

    doc->blocks.resize(128);
    doc->ifds.resize(256);
    doc->payload.resize(1024 * 1024);
    doc->payload_parts.resize(16384);

    auto merge_xmp_status = [](XmpDecodeStatus* out,
                               XmpDecodeStatus in) noexcept {
        if (!out) {
            return;
        }
        if (*out == XmpDecodeStatus::LimitExceeded) {
            return;
        }
        if (in == XmpDecodeStatus::LimitExceeded) {
            *out = in;
            return;
        }
        if (*out == XmpDecodeStatus::Malformed) {
            return;
        }
        if (in == XmpDecodeStatus::Malformed) {
            *out = in;
            return;
        }
        if (*out == XmpDecodeStatus::OutputTruncated) {
            return;
        }
        if (in == XmpDecodeStatus::OutputTruncated) {
            *out = in;
            return;
        }
        if (*out == XmpDecodeStatus::Ok) {
            return;
        }
        if (in == XmpDecodeStatus::Ok) {
            *out = in;
            return;
        }
    };

    for (;;) {
        doc->store  = MetaStore();
        doc->result = simple_meta_read(
            doc->file_bytes, doc->store,
            std::span<ContainerBlockRef>(doc->blocks.data(), doc->blocks.size()),
            std::span<ExifIfdRef>(doc->ifds.data(), doc->ifds.size()),
            std::span<std::byte>(doc->payload.data(), doc->payload.size()),
            std::span<uint32_t>(doc->payload_parts.data(),
                                doc->payload_parts.size()),
            decode_options);

        if (doc->result.scan.status == ScanStatus::OutputTruncated
            && doc->result.scan.needed > doc->blocks.size()) {
            doc->blocks.resize(doc->result.scan.needed);
            continue;
        }
        if (doc->result.payload.status == PayloadStatus::OutputTruncated
            && doc->result.payload.needed > doc->payload.size()) {
            doc->payload.resize(
                static_cast<size_t>(doc->result.payload.needed));
            continue;
        }
        break;
    }

    if (include_xmp_sidecar) {
        std::string sidecar_a;
        std::string sidecar_b;
        {
            const std::string s(path);
            sidecar_b = s + ".xmp";

            const size_t sep = s.find_last_of("/\\");
            const size_t dot = s.find_last_of('.');
            if (dot != std::string::npos
                && (sep == std::string::npos || dot > sep)) {
                sidecar_a = s.substr(0, dot) + ".xmp";
            } else {
                sidecar_a = sidecar_b;
            }
            if (sidecar_a == sidecar_b) {
                sidecar_b.clear();
            }
        }

        auto try_read = [&](const std::string& p) -> bool {
            if (p.empty()) {
                return false;
            }
            std::FILE* f = std::fopen(p.c_str(), "rb");
            if (!f) {
                return false;
            }
            std::fclose(f);
            return true;
        };

        const std::string* candidates[2] = { &sidecar_a, &sidecar_b };
        for (int i = 0; i < 2; ++i) {
            const std::string& sp = *candidates[i];
            if (sp.empty() || !try_read(sp)) {
                continue;
            }
            const std::vector<std::byte> xmp_bytes
                = read_file_bytes(sp.c_str(), policy.max_file_bytes);
            const XmpDecodeResult one = decode_xmp_packet(xmp_bytes, doc->store,
                                                          EntryFlags::None,
                                                          decode_options.xmp);
            merge_xmp_status(&doc->result.xmp.status, one.status);
            doc->result.xmp.entries_decoded += one.entries_decoded;
        }
    }

    doc->blocks.resize(doc->result.scan.written);
    const uint32_t ifds_written = doc->result.exif.ifds_written;
    if (ifds_written < doc->ifds.size()) {
        doc->ifds.resize(ifds_written);
    }

    doc->store.finalize();
    return doc;
}

}  // namespace openmeta

NB_MODULE(_openmeta, m)
{
    using namespace openmeta;

    m.doc()               = "OpenMeta metadata reading bindings (nanobind).";
    m.attr("__version__") = OPENMETA_VERSION_STRING;

    nb::enum_<ScanStatus>(m, "ScanStatus")
        .value("Ok", ScanStatus::Ok)
        .value("OutputTruncated", ScanStatus::OutputTruncated)
        .value("Unsupported", ScanStatus::Unsupported)
        .value("Malformed", ScanStatus::Malformed);

    nb::enum_<PayloadStatus>(m, "PayloadStatus")
        .value("Ok", PayloadStatus::Ok)
        .value("OutputTruncated", PayloadStatus::OutputTruncated)
        .value("Unsupported", PayloadStatus::Unsupported)
        .value("Malformed", PayloadStatus::Malformed)
        .value("LimitExceeded", PayloadStatus::LimitExceeded);

    nb::enum_<ExifDecodeStatus>(m, "ExifDecodeStatus")
        .value("Ok", ExifDecodeStatus::Ok)
        .value("OutputTruncated", ExifDecodeStatus::OutputTruncated)
        .value("Unsupported", ExifDecodeStatus::Unsupported)
        .value("Malformed", ExifDecodeStatus::Malformed)
        .value("LimitExceeded", ExifDecodeStatus::LimitExceeded);

    nb::enum_<CcmQueryStatus>(m, "CcmQueryStatus")
        .value("Ok", CcmQueryStatus::Ok)
        .value("LimitExceeded", CcmQueryStatus::LimitExceeded);

    nb::enum_<CcmValidationMode>(m, "CcmValidationMode")
        .value("None", CcmValidationMode::None)
        .value("DngSpecWarnings", CcmValidationMode::DngSpecWarnings);

    nb::enum_<CcmIssueSeverity>(m, "CcmIssueSeverity")
        .value("Warning", CcmIssueSeverity::Warning)
        .value("Error", CcmIssueSeverity::Error);

    nb::enum_<CcmIssueCode>(m, "CcmIssueCode")
        .value("DecodeFailed", CcmIssueCode::DecodeFailed)
        .value("NonFiniteValue", CcmIssueCode::NonFiniteValue)
        .value("UnexpectedCount", CcmIssueCode::UnexpectedCount)
        .value("MatrixCountNotDivisibleBy3",
               CcmIssueCode::MatrixCountNotDivisibleBy3)
        .value("NonPositiveValue", CcmIssueCode::NonPositiveValue)
        .value("AsShotConflict", CcmIssueCode::AsShotConflict)
        .value("MissingCompanionTag", CcmIssueCode::MissingCompanionTag)
        .value("TripleIlluminantRule", CcmIssueCode::TripleIlluminantRule)
        .value("CalibrationSignatureMismatch",
               CcmIssueCode::CalibrationSignatureMismatch)
        .value("MissingIlluminantData", CcmIssueCode::MissingIlluminantData)
        .value("InvalidIlluminantCode", CcmIssueCode::InvalidIlluminantCode)
        .value("WhiteXYOutOfRange", CcmIssueCode::WhiteXYOutOfRange);

    nb::enum_<ValidateStatus>(m, "ValidateStatus")
        .value("Ok", ValidateStatus::Ok)
        .value("OpenFailed", ValidateStatus::OpenFailed)
        .value("TooLarge", ValidateStatus::TooLarge)
        .value("ReadFailed", ValidateStatus::ReadFailed);

    nb::enum_<ValidateIssueSeverity>(m, "ValidateIssueSeverity")
        .value("Warning", ValidateIssueSeverity::Warning)
        .value("Error", ValidateIssueSeverity::Error);

    nb::enum_<IccTagInterpretStatus>(m, "IccTagInterpretStatus")
        .value("Ok", IccTagInterpretStatus::Ok)
        .value("Unsupported", IccTagInterpretStatus::Unsupported)
        .value("Malformed", IccTagInterpretStatus::Malformed)
        .value("LimitExceeded", IccTagInterpretStatus::LimitExceeded);

    nb::enum_<ExifLimitReason>(m, "ExifLimitReason")
        .value("None_", ExifLimitReason::None)
        .value("MaxIfds", ExifLimitReason::MaxIfds)
        .value("MaxEntriesPerIfd", ExifLimitReason::MaxEntriesPerIfd)
        .value("MaxTotalEntries", ExifLimitReason::MaxTotalEntries)
        .value("ValueCountTooLarge", ExifLimitReason::ValueCountTooLarge);

    nb::enum_<ExrDecodeStatus>(m, "ExrDecodeStatus")
        .value("Ok", ExrDecodeStatus::Ok)
        .value("Unsupported", ExrDecodeStatus::Unsupported)
        .value("Malformed", ExrDecodeStatus::Malformed)
        .value("LimitExceeded", ExrDecodeStatus::LimitExceeded);

    nb::enum_<ExrAdapterStatus>(m, "ExrAdapterStatus")
        .value("Ok", ExrAdapterStatus::Ok)
        .value("InvalidArgument", ExrAdapterStatus::InvalidArgument)
        .value("Unsupported", ExrAdapterStatus::Unsupported);

    nb::enum_<XmpDecodeStatus>(m, "XmpDecodeStatus")
        .value("Ok", XmpDecodeStatus::Ok)
        .value("OutputTruncated", XmpDecodeStatus::OutputTruncated)
        .value("Unsupported", XmpDecodeStatus::Unsupported)
        .value("Malformed", XmpDecodeStatus::Malformed)
        .value("LimitExceeded", XmpDecodeStatus::LimitExceeded);

    nb::enum_<ContainerFormat>(m, "ContainerFormat")
        .value("Unknown", ContainerFormat::Unknown)
        .value("Jpeg", ContainerFormat::Jpeg)
        .value("Png", ContainerFormat::Png)
        .value("Webp", ContainerFormat::Webp)
        .value("Gif", ContainerFormat::Gif)
        .value("Tiff", ContainerFormat::Tiff)
        .value("Crw", ContainerFormat::Crw)
        .value("Raf", ContainerFormat::Raf)
        .value("X3f", ContainerFormat::X3f)
        .value("Jp2", ContainerFormat::Jp2)
        .value("Jxl", ContainerFormat::Jxl)
        .value("Heif", ContainerFormat::Heif)
        .value("Avif", ContainerFormat::Avif)
        .value("Cr3", ContainerFormat::Cr3);

    nb::enum_<ContainerBlockKind>(m, "ContainerBlockKind")
        .value("Unknown", ContainerBlockKind::Unknown)
        .value("Exif", ContainerBlockKind::Exif)
        .value("Ciff", ContainerBlockKind::Ciff)
        .value("MakerNote", ContainerBlockKind::MakerNote)
        .value("Xmp", ContainerBlockKind::Xmp)
        .value("XmpExtended", ContainerBlockKind::XmpExtended)
        .value("Jumbf", ContainerBlockKind::Jumbf)
        .value("Icc", ContainerBlockKind::Icc)
        .value("IptcIim", ContainerBlockKind::IptcIim)
        .value("PhotoshopIrB", ContainerBlockKind::PhotoshopIrB)
        .value("Mpf", ContainerBlockKind::Mpf)
        .value("Comment", ContainerBlockKind::Comment)
        .value("Text", ContainerBlockKind::Text)
        .value("CompressedMetadata", ContainerBlockKind::CompressedMetadata);

    nb::enum_<BlockCompression>(m, "BlockCompression")
        .value("None", BlockCompression::None)
        .value("Deflate", BlockCompression::Deflate)
        .value("Brotli", BlockCompression::Brotli);

    nb::enum_<BlockChunking>(m, "BlockChunking")
        .value("None", BlockChunking::None)
        .value("JpegApp2SeqTotal", BlockChunking::JpegApp2SeqTotal)
        .value("JpegXmpExtendedGuidOffset",
               BlockChunking::JpegXmpExtendedGuidOffset)
        .value("GifSubBlocks", BlockChunking::GifSubBlocks)
        .value("BmffExifTiffOffsetU32Be",
               BlockChunking::BmffExifTiffOffsetU32Be)
        .value("BrobU32BeRealTypePrefix",
               BlockChunking::BrobU32BeRealTypePrefix)
        .value("Jp2UuidPayload", BlockChunking::Jp2UuidPayload)
        .value("PsIrB8Bim", BlockChunking::PsIrB8Bim);

    nb::enum_<MetaKeyKind>(m, "MetaKeyKind")
        .value("ExifTag", MetaKeyKind::ExifTag)
        .value("Comment", MetaKeyKind::Comment)
        .value("ExrAttribute", MetaKeyKind::ExrAttribute)
        .value("IptcDataset", MetaKeyKind::IptcDataset)
        .value("XmpProperty", MetaKeyKind::XmpProperty)
        .value("IccHeaderField", MetaKeyKind::IccHeaderField)
        .value("IccTag", MetaKeyKind::IccTag)
        .value("PhotoshopIrb", MetaKeyKind::PhotoshopIrb)
        .value("PhotoshopIrbField", MetaKeyKind::PhotoshopIrbField)
        .value("GeotiffKey", MetaKeyKind::GeotiffKey)
        .value("PrintImField", MetaKeyKind::PrintImField)
        .value("BmffField", MetaKeyKind::BmffField)
        .value("JumbfField", MetaKeyKind::JumbfField)
        .value("JumbfCborKey", MetaKeyKind::JumbfCborKey)
        .value("PngText", MetaKeyKind::PngText);

    nb::enum_<WireFamily>(m, "WireFamily")
        .value("None", WireFamily::None)
        .value("Tiff", WireFamily::Tiff)
        .value("Other", WireFamily::Other);

    nb::enum_<MetaValueKind>(m, "MetaValueKind")
        .value("Empty", MetaValueKind::Empty)
        .value("Scalar", MetaValueKind::Scalar)
        .value("Array", MetaValueKind::Array)
        .value("Bytes", MetaValueKind::Bytes)
        .value("Text", MetaValueKind::Text);

    nb::enum_<MetaElementType>(m, "MetaElementType")
        .value("U8", MetaElementType::U8)
        .value("I8", MetaElementType::I8)
        .value("U16", MetaElementType::U16)
        .value("I16", MetaElementType::I16)
        .value("U32", MetaElementType::U32)
        .value("I32", MetaElementType::I32)
        .value("U64", MetaElementType::U64)
        .value("I64", MetaElementType::I64)
        .value("F32", MetaElementType::F32)
        .value("F64", MetaElementType::F64)
        .value("URational", MetaElementType::URational)
        .value("SRational", MetaElementType::SRational);

    nb::enum_<TextEncoding>(m, "TextEncoding")
        .value("Unknown", TextEncoding::Unknown)
        .value("Ascii", TextEncoding::Ascii)
        .value("Utf8", TextEncoding::Utf8)
        .value("Utf16LE", TextEncoding::Utf16LE)
        .value("Utf16BE", TextEncoding::Utf16BE);

    nb::enum_<ExportNameStyle>(m, "ExportNameStyle")
        .value("Canonical", ExportNameStyle::Canonical)
        .value("XmpPortable", ExportNameStyle::XmpPortable)
        .value("Oiio", ExportNameStyle::Oiio);
    nb::enum_<ExportNamePolicy>(m, "ExportNamePolicy")
        .value("Spec", ExportNamePolicy::Spec)
        .value("ExifToolAlias", ExportNamePolicy::ExifToolAlias);

    nb::enum_<XmpDumpStatus>(m, "XmpDumpStatus")
        .value("Ok", XmpDumpStatus::Ok)
        .value("OutputTruncated", XmpDumpStatus::OutputTruncated)
        .value("LimitExceeded", XmpDumpStatus::LimitExceeded);

    nb::enum_<JumbfDecodeStatus>(m, "JumbfDecodeStatus")
        .value("Ok", JumbfDecodeStatus::Ok)
        .value("Unsupported", JumbfDecodeStatus::Unsupported)
        .value("Malformed", JumbfDecodeStatus::Malformed)
        .value("LimitExceeded", JumbfDecodeStatus::LimitExceeded);

    nb::enum_<C2paVerifyStatus>(m, "C2paVerifyStatus")
        .value("NotRequested", C2paVerifyStatus::NotRequested)
        .value("DisabledByBuild", C2paVerifyStatus::DisabledByBuild)
        .value("BackendUnavailable", C2paVerifyStatus::BackendUnavailable)
        .value("NoSignatures", C2paVerifyStatus::NoSignatures)
        .value("InvalidSignature", C2paVerifyStatus::InvalidSignature)
        .value("VerificationFailed", C2paVerifyStatus::VerificationFailed)
        .value("Verified", C2paVerifyStatus::Verified)
        .value("NotImplemented", C2paVerifyStatus::NotImplemented);

    nb::enum_<C2paVerifyBackend>(m, "C2paVerifyBackend")
        .value("None", C2paVerifyBackend::None)
        .value("Auto", C2paVerifyBackend::Auto)
        .value("Native", C2paVerifyBackend::Native)
        .value("OpenSsl", C2paVerifyBackend::OpenSsl);

    nb::enum_<XmpSidecarFormat>(m, "XmpSidecarFormat")
        .value("Lossless", XmpSidecarFormat::Lossless)
        .value("Portable", XmpSidecarFormat::Portable);

    nb::enum_<XmpConflictPolicy>(m, "XmpConflictPolicy")
        .value("CurrentBehavior", XmpConflictPolicy::CurrentBehavior)
        .value("ExistingWins", XmpConflictPolicy::ExistingWins)
        .value("GeneratedWins", XmpConflictPolicy::GeneratedWins);

    nb::enum_<XmpWritebackMode>(m, "XmpWritebackMode")
        .value("EmbeddedOnly", XmpWritebackMode::EmbeddedOnly)
        .value("SidecarOnly", XmpWritebackMode::SidecarOnly)
        .value("EmbeddedAndSidecar",
               XmpWritebackMode::EmbeddedAndSidecar);

    nb::enum_<XmpDestinationEmbeddedMode>(m,
                                          "XmpDestinationEmbeddedMode")
        .value("PreserveExisting",
               XmpDestinationEmbeddedMode::PreserveExisting)
        .value("StripExisting", XmpDestinationEmbeddedMode::StripExisting);

    nb::enum_<XmpDestinationSidecarMode>(m,
                                         "XmpDestinationSidecarMode")
        .value("PreserveExisting",
               XmpDestinationSidecarMode::PreserveExisting)
        .value("StripExisting", XmpDestinationSidecarMode::StripExisting);

    nb::enum_<XmpExistingSidecarMode>(m, "XmpExistingSidecarMode")
        .value("Ignore", XmpExistingSidecarMode::Ignore)
        .value("MergeIfPresent", XmpExistingSidecarMode::MergeIfPresent);

    nb::enum_<XmpExistingSidecarPrecedence>(m,
                                            "XmpExistingSidecarPrecedence")
        .value("SidecarWins", XmpExistingSidecarPrecedence::SidecarWins)
        .value("SourceWins", XmpExistingSidecarPrecedence::SourceWins);

    nb::enum_<XmpExistingDestinationEmbeddedMode>(
        m, "XmpExistingDestinationEmbeddedMode")
        .value("Ignore", XmpExistingDestinationEmbeddedMode::Ignore)
        .value("MergeIfPresent",
               XmpExistingDestinationEmbeddedMode::MergeIfPresent);

    nb::enum_<XmpExistingDestinationEmbeddedPrecedence>(
        m, "XmpExistingDestinationEmbeddedPrecedence")
        .value("DestinationWins",
               XmpExistingDestinationEmbeddedPrecedence::DestinationWins)
        .value("SourceWins",
               XmpExistingDestinationEmbeddedPrecedence::SourceWins);

    nb::enum_<TransferTargetFormat>(m, "TransferTargetFormat")
        .value("Jpeg", TransferTargetFormat::Jpeg)
        .value("Tiff", TransferTargetFormat::Tiff)
        .value("Dng", TransferTargetFormat::Dng)
        .value("Jxl", TransferTargetFormat::Jxl)
        .value("Webp", TransferTargetFormat::Webp)
        .value("Heif", TransferTargetFormat::Heif)
        .value("Avif", TransferTargetFormat::Avif)
        .value("Cr3", TransferTargetFormat::Cr3)
        .value("Exr", TransferTargetFormat::Exr)
        .value("Png", TransferTargetFormat::Png)
        .value("Jp2", TransferTargetFormat::Jp2);

    nb::enum_<TransferPolicySubject>(m, "TransferPolicySubject")
        .value("MakerNote", TransferPolicySubject::MakerNote)
        .value("Jumbf", TransferPolicySubject::Jumbf)
        .value("C2pa", TransferPolicySubject::C2pa)
        .value("XmpExifProjection",
               TransferPolicySubject::XmpExifProjection)
        .value("XmpIptcProjection",
               TransferPolicySubject::XmpIptcProjection);

    nb::enum_<TransferPolicyAction>(m, "TransferPolicyAction")
        .value("Keep", TransferPolicyAction::Keep)
        .value("Drop", TransferPolicyAction::Drop)
        .value("Invalidate", TransferPolicyAction::Invalidate)
        .value("Rewrite", TransferPolicyAction::Rewrite);

    nb::enum_<TransferPolicyReason>(m, "TransferPolicyReason")
        .value("Default", TransferPolicyReason::Default)
        .value("NotPresent", TransferPolicyReason::NotPresent)
        .value("ExplicitDrop", TransferPolicyReason::ExplicitDrop)
        .value("CarrierDisabled", TransferPolicyReason::CarrierDisabled)
        .value("ProjectedPayload", TransferPolicyReason::ProjectedPayload)
        .value("DraftInvalidationPayload",
               TransferPolicyReason::DraftInvalidationPayload)
        .value("ExternalSignedPayload",
               TransferPolicyReason::ExternalSignedPayload)
        .value("ContentBoundTransferUnavailable",
               TransferPolicyReason::ContentBoundTransferUnavailable)
        .value("SignedRewriteUnavailable",
               TransferPolicyReason::SignedRewriteUnavailable)
        .value("PortableInvalidationUnavailable",
               TransferPolicyReason::PortableInvalidationUnavailable)
        .value("RewriteUnavailablePreservedRaw",
               TransferPolicyReason::RewriteUnavailablePreservedRaw)
        .value("TargetSerializationUnavailable",
               TransferPolicyReason::TargetSerializationUnavailable);

    nb::enum_<TransferC2paMode>(m, "TransferC2paMode")
        .value("NotApplicable", TransferC2paMode::NotApplicable)
        .value("NotPresent", TransferC2paMode::NotPresent)
        .value("Drop", TransferC2paMode::Drop)
        .value("DraftUnsignedInvalidation",
               TransferC2paMode::DraftUnsignedInvalidation)
        .value("PreserveRaw", TransferC2paMode::PreserveRaw)
        .value("SignedRewrite", TransferC2paMode::SignedRewrite);

    nb::enum_<TransferC2paSourceKind>(m, "TransferC2paSourceKind")
        .value("NotApplicable", TransferC2paSourceKind::NotApplicable)
        .value("NotPresent", TransferC2paSourceKind::NotPresent)
        .value("DecodedOnly", TransferC2paSourceKind::DecodedOnly)
        .value("ContentBound", TransferC2paSourceKind::ContentBound)
        .value("DraftUnsignedInvalidation",
               TransferC2paSourceKind::DraftUnsignedInvalidation);

    nb::enum_<TransferC2paPreparedOutput>(m, "TransferC2paPreparedOutput")
        .value("NotApplicable", TransferC2paPreparedOutput::NotApplicable)
        .value("NotPresent", TransferC2paPreparedOutput::NotPresent)
        .value("Dropped", TransferC2paPreparedOutput::Dropped)
        .value("PreservedRaw", TransferC2paPreparedOutput::PreservedRaw)
        .value("GeneratedDraftUnsignedInvalidation",
               TransferC2paPreparedOutput::GeneratedDraftUnsignedInvalidation)
        .value("SignedRewrite", TransferC2paPreparedOutput::SignedRewrite);

    nb::enum_<TransferC2paRewriteState>(m, "TransferC2paRewriteState")
        .value("NotApplicable", TransferC2paRewriteState::NotApplicable)
        .value("NotRequested", TransferC2paRewriteState::NotRequested)
        .value("SigningMaterialRequired",
               TransferC2paRewriteState::SigningMaterialRequired)
        .value("Ready", TransferC2paRewriteState::Ready);

    nb::enum_<TransferC2paRewriteChunkKind>(m, "TransferC2paRewriteChunkKind")
        .value("SourceRange", TransferC2paRewriteChunkKind::SourceRange)
        .value("PreparedJpegSegment",
               TransferC2paRewriteChunkKind::PreparedJpegSegment)
        .value("PreparedJxlBox", TransferC2paRewriteChunkKind::PreparedJxlBox)
        .value("PreparedBmffMetaBox",
               TransferC2paRewriteChunkKind::PreparedBmffMetaBox);

    nb::enum_<TransferC2paSignedPayloadKind>(m, "TransferC2paSignedPayloadKind")
        .value("NotApplicable", TransferC2paSignedPayloadKind::NotApplicable)
        .value("GenericJumbf", TransferC2paSignedPayloadKind::GenericJumbf)
        .value("DraftUnsignedInvalidation",
               TransferC2paSignedPayloadKind::DraftUnsignedInvalidation)
        .value("ContentBound", TransferC2paSignedPayloadKind::ContentBound);

    nb::enum_<TransferC2paSemanticStatus>(m, "TransferC2paSemanticStatus")
        .value("NotChecked", TransferC2paSemanticStatus::NotChecked)
        .value("Ok", TransferC2paSemanticStatus::Ok)
        .value("Invalid", TransferC2paSemanticStatus::Invalid);

    nb::enum_<TransferStatus>(m, "TransferStatus")
        .value("Ok", TransferStatus::Ok)
        .value("InvalidArgument", TransferStatus::InvalidArgument)
        .value("Unsupported", TransferStatus::Unsupported)
        .value("LimitExceeded", TransferStatus::LimitExceeded)
        .value("Malformed", TransferStatus::Malformed)
        .value("UnsafeData", TransferStatus::UnsafeData)
        .value("InternalError", TransferStatus::InternalError);

    nb::enum_<PrepareTransferCode>(m, "PrepareTransferCode")
        .value("None", PrepareTransferCode::None)
        .value("NullOutBundle", PrepareTransferCode::NullOutBundle)
        .value("UnsupportedTargetFormat",
               PrepareTransferCode::UnsupportedTargetFormat)
        .value("ExifPackFailed", PrepareTransferCode::ExifPackFailed)
        .value("XmpPackFailed", PrepareTransferCode::XmpPackFailed)
        .value("IccPackFailed", PrepareTransferCode::IccPackFailed)
        .value("IptcPackFailed", PrepareTransferCode::IptcPackFailed)
        .value("RequestedMetadataNotSerializable",
               PrepareTransferCode::RequestedMetadataNotSerializable);

    nb::enum_<EmitTransferCode>(m, "EmitTransferCode")
        .value("None", EmitTransferCode::None)
        .value("InvalidArgument", EmitTransferCode::InvalidArgument)
        .value("BundleTargetNotJpeg", EmitTransferCode::BundleTargetNotJpeg)
        .value("UnsupportedRoute", EmitTransferCode::UnsupportedRoute)
        .value("InvalidPayload", EmitTransferCode::InvalidPayload)
        .value("ContentBoundPayloadUnsupported",
               EmitTransferCode::ContentBoundPayloadUnsupported)
        .value("BackendWriteFailed", EmitTransferCode::BackendWriteFailed)
        .value("PlanMismatch", EmitTransferCode::PlanMismatch);

    nb::enum_<PrepareTransferFileCode>(m, "PrepareTransferFileCode")
        .value("None", PrepareTransferFileCode::None)
        .value("EmptyPath", PrepareTransferFileCode::EmptyPath)
        .value("MapFailed", PrepareTransferFileCode::MapFailed)
        .value("PayloadBufferPlatformLimit",
               PrepareTransferFileCode::PayloadBufferPlatformLimit)
        .value("DecodeFailed", PrepareTransferFileCode::DecodeFailed);

    nb::enum_<TransferFileStatus>(m, "TransferFileStatus")
        .value("Ok", TransferFileStatus::Ok)
        .value("InvalidArgument", TransferFileStatus::InvalidArgument)
        .value("OpenFailed", TransferFileStatus::OpenFailed)
        .value("StatFailed", TransferFileStatus::StatFailed)
        .value("TooLarge", TransferFileStatus::TooLarge)
        .value("MapFailed", TransferFileStatus::MapFailed)
        .value("ReadFailed", TransferFileStatus::ReadFailed);

    nb::enum_<TransferBlockKind>(m, "TransferBlockKind")
        .value("Exif", TransferBlockKind::Exif)
        .value("Xmp", TransferBlockKind::Xmp)
        .value("IptcIim", TransferBlockKind::IptcIim)
        .value("PhotoshopIrb", TransferBlockKind::PhotoshopIrb)
        .value("Icc", TransferBlockKind::Icc)
        .value("Jumbf", TransferBlockKind::Jumbf)
        .value("C2pa", TransferBlockKind::C2pa)
        .value("ExrAttribute", TransferBlockKind::ExrAttribute)
        .value("Other", TransferBlockKind::Other);

    nb::class_<PayloadLimits>(m, "PayloadLimits")
        .def(nb::init<>())
        .def_rw("max_parts", &PayloadLimits::max_parts)
        .def_rw("max_output_bytes", &PayloadLimits::max_output_bytes);

    nb::class_<ExifDecodeLimits>(m, "ExifDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_ifds", &ExifDecodeLimits::max_ifds)
        .def_rw("max_entries_per_ifd", &ExifDecodeLimits::max_entries_per_ifd)
        .def_rw("max_total_entries", &ExifDecodeLimits::max_total_entries)
        .def_rw("max_value_bytes", &ExifDecodeLimits::max_value_bytes);

    nb::class_<XmpDecodeLimits>(m, "XmpDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_depth", &XmpDecodeLimits::max_depth)
        .def_rw("max_properties", &XmpDecodeLimits::max_properties)
        .def_rw("max_input_bytes", &XmpDecodeLimits::max_input_bytes)
        .def_rw("max_path_bytes", &XmpDecodeLimits::max_path_bytes)
        .def_rw("max_value_bytes", &XmpDecodeLimits::max_value_bytes)
        .def_rw("max_total_value_bytes",
                &XmpDecodeLimits::max_total_value_bytes);

    nb::class_<ExrDecodeLimits>(m, "ExrDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_parts", &ExrDecodeLimits::max_parts)
        .def_rw("max_attributes_per_part",
                &ExrDecodeLimits::max_attributes_per_part)
        .def_rw("max_attributes", &ExrDecodeLimits::max_attributes)
        .def_rw("max_name_bytes", &ExrDecodeLimits::max_name_bytes)
        .def_rw("max_type_name_bytes", &ExrDecodeLimits::max_type_name_bytes)
        .def_rw("max_attribute_bytes", &ExrDecodeLimits::max_attribute_bytes)
        .def_rw("max_total_attribute_bytes",
                &ExrDecodeLimits::max_total_attribute_bytes);

    nb::class_<JumbfDecodeLimits>(m, "JumbfDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_input_bytes", &JumbfDecodeLimits::max_input_bytes)
        .def_rw("max_box_depth", &JumbfDecodeLimits::max_box_depth)
        .def_rw("max_boxes", &JumbfDecodeLimits::max_boxes)
        .def_rw("max_entries", &JumbfDecodeLimits::max_entries)
        .def_rw("max_cbor_depth", &JumbfDecodeLimits::max_cbor_depth)
        .def_rw("max_cbor_items", &JumbfDecodeLimits::max_cbor_items)
        .def_rw("max_cbor_key_bytes", &JumbfDecodeLimits::max_cbor_key_bytes)
        .def_rw("max_cbor_text_bytes", &JumbfDecodeLimits::max_cbor_text_bytes)
        .def_rw("max_cbor_bytes_bytes",
                &JumbfDecodeLimits::max_cbor_bytes_bytes);

    nb::class_<IccDecodeLimits>(m, "IccDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_tags", &IccDecodeLimits::max_tags)
        .def_rw("max_tag_bytes", &IccDecodeLimits::max_tag_bytes)
        .def_rw("max_total_tag_bytes", &IccDecodeLimits::max_total_tag_bytes);

    nb::class_<IptcIimDecodeLimits>(m, "IptcIimDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_datasets", &IptcIimDecodeLimits::max_datasets)
        .def_rw("max_dataset_bytes", &IptcIimDecodeLimits::max_dataset_bytes)
        .def_rw("max_total_bytes", &IptcIimDecodeLimits::max_total_bytes);

    nb::class_<PhotoshopIrbDecodeLimits>(m, "PhotoshopIrbDecodeLimits")
        .def(nb::init<>())
        .def_rw("max_resources", &PhotoshopIrbDecodeLimits::max_resources)
        .def_rw("max_total_bytes", &PhotoshopIrbDecodeLimits::max_total_bytes)
        .def_rw("max_resource_len",
                &PhotoshopIrbDecodeLimits::max_resource_len);

    nb::class_<PreviewScanLimits>(m, "PreviewScanLimits")
        .def(nb::init<>())
        .def_rw("max_ifds", &PreviewScanLimits::max_ifds)
        .def_rw("max_total_entries", &PreviewScanLimits::max_total_entries)
        .def_rw("max_preview_bytes", &PreviewScanLimits::max_preview_bytes);

    nb::class_<XmpDumpLimits>(m, "XmpDumpLimits")
        .def(nb::init<>())
        .def_rw("max_output_bytes", &XmpDumpLimits::max_output_bytes)
        .def_rw("max_entries", &XmpDumpLimits::max_entries);

    nb::class_<OpenMetaResourcePolicy>(m, "ResourcePolicy")
        .def(nb::init<>())
        .def_rw("max_file_bytes", &OpenMetaResourcePolicy::max_file_bytes)
        .def_rw("payload_limits", &OpenMetaResourcePolicy::payload_limits)
        .def_rw("exif_limits", &OpenMetaResourcePolicy::exif_limits)
        .def_rw("xmp_limits", &OpenMetaResourcePolicy::xmp_limits)
        .def_rw("exr_limits", &OpenMetaResourcePolicy::exr_limits)
        .def_rw("jumbf_limits", &OpenMetaResourcePolicy::jumbf_limits)
        .def_rw("icc_limits", &OpenMetaResourcePolicy::icc_limits)
        .def_rw("iptc_limits", &OpenMetaResourcePolicy::iptc_limits)
        .def_rw("photoshop_irb_limits",
                &OpenMetaResourcePolicy::photoshop_irb_limits)
        .def_rw("preview_scan_limits",
                &OpenMetaResourcePolicy::preview_scan_limits)
        .def_rw("max_preview_output_bytes",
                &OpenMetaResourcePolicy::max_preview_output_bytes)
        .def_rw("xmp_dump_limits", &OpenMetaResourcePolicy::xmp_dump_limits)
        .def_rw("max_decode_millis", &OpenMetaResourcePolicy::max_decode_millis)
        .def_rw("max_decompression_ratio",
                &OpenMetaResourcePolicy::max_decompression_ratio)
        .def_rw("max_total_decode_work_bytes",
                &OpenMetaResourcePolicy::max_total_decode_work_bytes);

    nb::class_<XmpDumpResult>(m, "XmpDumpResult")
        .def_ro("status", &XmpDumpResult::status)
        .def_ro("written", &XmpDumpResult::written)
        .def_ro("needed", &XmpDumpResult::needed)
        .def_ro("entries", &XmpDumpResult::entries);

    nb::class_<ContainerBlockRef>(m, "BlockRef")
        .def(nb::init<>())
        .def_ro("format", &ContainerBlockRef::format)
        .def_ro("kind", &ContainerBlockRef::kind)
        .def_ro("compression", &ContainerBlockRef::compression)
        .def_ro("chunking", &ContainerBlockRef::chunking)
        .def_ro("outer_offset", &ContainerBlockRef::outer_offset)
        .def_ro("outer_size", &ContainerBlockRef::outer_size)
        .def_ro("data_offset", &ContainerBlockRef::data_offset)
        .def_ro("data_size", &ContainerBlockRef::data_size)
        .def_ro("id", &ContainerBlockRef::id)
        .def_ro("part_index", &ContainerBlockRef::part_index)
        .def_ro("part_count", &ContainerBlockRef::part_count)
        .def_ro("logical_offset", &ContainerBlockRef::logical_offset)
        .def_ro("logical_size", &ContainerBlockRef::logical_size)
        .def_ro("group", &ContainerBlockRef::group)
        .def_ro("aux_u32", &ContainerBlockRef::aux_u32);

    nb::class_<PyDocument>(m, "Document")
        .def_prop_ro("path", [](const PyDocument& d) { return d.path; })
        .def_prop_ro("file_size",
                     [](const PyDocument& d) {
                         return static_cast<uint64_t>(d.file_bytes.size());
                     })
        .def_prop_ro("scan_status",
                     [](const PyDocument& d) { return d.result.scan.status; })
        .def_prop_ro("scan_written",
                     [](const PyDocument& d) { return d.result.scan.written; })
        .def_prop_ro("scan_needed",
                     [](const PyDocument& d) { return d.result.scan.needed; })
        .def_prop_ro("payload_status",
                     [](const PyDocument& d) { return d.result.payload.status; })
        .def_prop_ro("payload_written",
                     [](const PyDocument& d) {
                         return static_cast<uint64_t>(d.result.payload.written);
                     })
        .def_prop_ro("payload_needed",
                     [](const PyDocument& d) {
                         return static_cast<uint64_t>(d.result.payload.needed);
                     })
        .def_prop_ro("xmp_status",
                     [](const PyDocument& d) { return d.result.xmp.status; })
        .def_prop_ro("xmp_entries_decoded",
                     [](const PyDocument& d) {
                         return d.result.xmp.entries_decoded;
                     })
        .def_prop_ro("jumbf_status",
                     [](const PyDocument& d) { return d.result.jumbf.status; })
        .def_prop_ro("jumbf_boxes_decoded",
                     [](const PyDocument& d) {
                         return d.result.jumbf.boxes_decoded;
                     })
        .def_prop_ro("jumbf_cbor_items",
                     [](const PyDocument& d) {
                         return d.result.jumbf.cbor_items;
                     })
        .def_prop_ro("jumbf_entries_decoded",
                     [](const PyDocument& d) {
                         return d.result.jumbf.entries_decoded;
                     })
        .def_prop_ro("jumbf_verify_status",
                     [](const PyDocument& d) {
                         return d.result.jumbf.verify_status;
                     })
        .def_prop_ro("jumbf_verify_backend",
                     [](const PyDocument& d) {
                         return d.result.jumbf.verify_backend_selected;
                     })
        .def_prop_ro("exif_status",
                     [](const PyDocument& d) { return d.result.exif.status; })
        .def_prop_ro("exif_ifds_decoded",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(
                             d.result.exif.ifds_written);
                     })
        .def_prop_ro("exif_ifds_needed",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(
                             d.result.exif.ifds_needed);
                     })
        .def_prop_ro("exif_entries_decoded",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(
                             d.result.exif.entries_decoded);
                     })
        .def_prop_ro("exif_limit_reason",
                     [](const PyDocument& d) {
                         return d.result.exif.limit_reason;
                     })
        .def_prop_ro("exif_limit_ifd_offset",
                     [](const PyDocument& d) {
                         return static_cast<uint64_t>(
                             d.result.exif.limit_ifd_offset);
                     })
        .def_prop_ro("exif_limit_tag",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(d.result.exif.limit_tag);
                     })
        .def_prop_ro("exr_status",
                     [](const PyDocument& d) { return d.result.exr.status; })
        .def_prop_ro("exr_parts_decoded",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(
                             d.result.exr.parts_decoded);
                     })
        .def_prop_ro("exr_entries_decoded",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(
                             d.result.exr.entries_decoded);
                     })
        .def_prop_ro("entry_count",
                     [](const PyDocument& d) {
                         return static_cast<uint64_t>(d.store.entries().size());
                     })
        .def_prop_ro("block_count",
                     [](const PyDocument& d) {
                         return static_cast<uint32_t>(d.store.block_count());
                     })
        .def_prop_ro("blocks", [](const PyDocument& d) { return d.blocks; })
        .def(
            "export_names",
            [](std::shared_ptr<PyDocument> d, ExportNameStyle style,
               ExportNamePolicy name_policy, bool include_makernotes) {
                ExportOptions options;
                options.style              = style;
                options.name_policy        = name_policy;
                options.include_makernotes = include_makernotes;
                return export_names(d->store, options);
            },
            "style"_a              = ExportNameStyle::Canonical,
            "name_policy"_a        = ExportNamePolicy::ExifToolAlias,
            "include_makernotes"_a = true)
        .def(
            "dng_ccm_fields",
            [](std::shared_ptr<PyDocument> d, bool require_dng_context,
               bool include_reduction_matrices, uint32_t max_fields,
               uint32_t max_values_per_field,
               CcmValidationMode validation_mode) {
                return collect_dng_ccm_to_python(d->store, require_dng_context,
                                                 include_reduction_matrices,
                                                 max_fields,
                                                 max_values_per_field,
                                                 validation_mode);
            },
            "require_dng_context"_a        = true,
            "include_reduction_matrices"_a = true, "max_fields"_a = 128U,
            "max_values_per_field"_a = 256U,
            "validation_mode"_a      = CcmValidationMode::DngSpecWarnings)
        .def(
            "oiio_attributes",
            [](std::shared_ptr<PyDocument> d, uint32_t max_value_bytes,
               ExportNamePolicy name_policy, bool include_makernotes,
               bool include_empty) {
                return oiio_attributes_to_python(d->store, max_value_bytes,
                                                 name_policy,
                                                 include_makernotes,
                                                 include_empty);
            },
            "max_value_bytes"_a    = 1024U,
            "name_policy"_a        = ExportNamePolicy::ExifToolAlias,
            "include_makernotes"_a = true, "include_empty"_a = false)
        .def(
            "unsafe_oiio_attributes",
            [](std::shared_ptr<PyDocument> d, uint32_t max_value_bytes,
               ExportNamePolicy name_policy, bool include_makernotes,
               bool include_empty) {
                return unsafe_oiio_attributes_to_python(d->store,
                                                        max_value_bytes,
                                                        name_policy,
                                                        include_makernotes,
                                                        include_empty);
            },
            "max_value_bytes"_a    = 1024U,
            "name_policy"_a        = ExportNamePolicy::ExifToolAlias,
            "include_makernotes"_a = true, "include_empty"_a = false)
        .def(
            "oiio_attributes_typed",
            [](std::shared_ptr<PyDocument> d, uint32_t max_value_bytes,
               ExportNamePolicy name_policy, bool include_makernotes,
               bool include_empty) {
                return oiio_typed_attributes_to_python(d->store,
                                                       max_value_bytes,
                                                       name_policy,
                                                       include_makernotes,
                                                       include_empty, false);
            },
            "max_value_bytes"_a    = 1024U,
            "name_policy"_a        = ExportNamePolicy::ExifToolAlias,
            "include_makernotes"_a = true, "include_empty"_a = false)
        .def(
            "unsafe_oiio_attributes_typed",
            [](std::shared_ptr<PyDocument> d, uint32_t max_value_bytes,
               ExportNamePolicy name_policy, bool include_makernotes,
               bool include_empty) {
                return oiio_typed_attributes_to_python(d->store,
                                                       max_value_bytes,
                                                       name_policy,
                                                       include_makernotes,
                                                       include_empty, true);
            },
            "max_value_bytes"_a    = 1024U,
            "name_policy"_a        = ExportNamePolicy::ExifToolAlias,
            "include_makernotes"_a = true, "include_empty"_a = false)
        .def(
            "ocio_metadata_tree",
            [](std::shared_ptr<PyDocument> d, ExportNameStyle style,
               ExportNamePolicy name_policy, uint32_t max_value_bytes,
               bool include_makernotes, bool include_empty) {
                return ocio_tree_to_python(d->store, style, name_policy,
                                           max_value_bytes, include_makernotes,
                                           include_empty);
            },
            "style"_a           = ExportNameStyle::XmpPortable,
            "name_policy"_a     = ExportNamePolicy::ExifToolAlias,
            "max_value_bytes"_a = 1024U, "include_makernotes"_a = false,
            "include_empty"_a = false)
        .def(
            "unsafe_ocio_metadata_tree",
            [](std::shared_ptr<PyDocument> d, ExportNameStyle style,
               ExportNamePolicy name_policy, uint32_t max_value_bytes,
               bool include_makernotes, bool include_empty) {
                return unsafe_ocio_metadata_tree_to_python(d->store, style,
                                                           name_policy,
                                                           max_value_bytes,
                                                           include_makernotes,
                                                           include_empty);
            },
            "style"_a           = ExportNameStyle::XmpPortable,
            "name_policy"_a     = ExportNamePolicy::ExifToolAlias,
            "max_value_bytes"_a = 1024U, "include_makernotes"_a = false,
            "include_empty"_a = false)
        .def(
            "dump_xmp_lossless",
            [](std::shared_ptr<PyDocument> d, uint64_t max_output_bytes,
               uint32_t max_entries, bool include_origin, bool include_wire,
               bool include_flags, bool include_names) {
                const XmpSidecarRequest request = make_xmp_sidecar_request(
                    XmpSidecarFormat::Lossless, max_output_bytes, max_entries,
                    true, true, false, false,
                    XmpConflictPolicy::CurrentBehavior, include_origin,
                    include_wire, include_flags, include_names);
                return dump_xmp_sidecar_to_python(d->store, request);
            },
            "max_output_bytes"_a = 0ULL, "max_entries"_a = 0U,
            "include_origin"_a = true, "include_wire"_a = true,
            "include_flags"_a = true, "include_names"_a = true)
        .def(
            "dump_xmp_portable",
            [](std::shared_ptr<PyDocument> d, uint64_t max_output_bytes,
               uint32_t max_entries, bool include_exif,
               bool include_existing_xmp, bool exiftool_gpsdatetime_alias,
               bool include_iptc, XmpConflictPolicy conflict_policy) {
                const XmpSidecarRequest request = make_xmp_sidecar_request(
                    XmpSidecarFormat::Portable, max_output_bytes, max_entries,
                    include_exif, include_iptc, include_existing_xmp,
                    exiftool_gpsdatetime_alias, conflict_policy, true, true,
                    true, true);
                return dump_xmp_sidecar_to_python(d->store, request);
            },
            "max_output_bytes"_a = 0ULL, "max_entries"_a = 0U,
            "include_exif"_a = true, "include_existing_xmp"_a = false,
            "exiftool_gpsdatetime_alias"_a = false, "include_iptc"_a = true,
            "conflict_policy"_a = XmpConflictPolicy::CurrentBehavior)
        .def(
            "dump_xmp_sidecar",
            [](std::shared_ptr<PyDocument> d, XmpSidecarFormat format,
               uint64_t max_output_bytes, uint32_t max_entries,
               bool include_exif, bool include_existing_xmp,
               bool portable_exiftool_gpsdatetime_alias, bool include_origin,
               bool include_wire, bool include_flags, bool include_names,
               bool include_iptc, XmpConflictPolicy conflict_policy) {
                const XmpSidecarRequest request = make_xmp_sidecar_request(
                    format, max_output_bytes, max_entries, include_exif,
                    include_iptc, include_existing_xmp,
                    portable_exiftool_gpsdatetime_alias, conflict_policy,
                    include_origin, include_wire, include_flags, include_names);
                return dump_xmp_sidecar_to_python(d->store, request);
            },
            "format"_a           = XmpSidecarFormat::Lossless,
            "max_output_bytes"_a = 0ULL, "max_entries"_a = 0U,
            "include_exif"_a = true, "include_existing_xmp"_a = false,
            "portable_exiftool_gpsdatetime_alias"_a = false,
            "include_origin"_a = true, "include_wire"_a = true,
            "include_flags"_a = true, "include_names"_a = true,
            "include_iptc"_a = true,
            "conflict_policy"_a = XmpConflictPolicy::CurrentBehavior)
        .def(
            "extract_payload",
            [](PyDocument& d, uint32_t block_index, bool decompress,
               uint64_t max_output_bytes) {
                if (block_index >= d.blocks.size()) {
                    throw std::runtime_error("block_index out of range");
                }
                PayloadOptions options;
                options.decompress              = decompress;
                options.limits.max_output_bytes = max_output_bytes;
                options.limits.max_parts        = 1U << 14;

                std::vector<uint32_t> indices(options.limits.max_parts);

                if (d.payload.empty()) {
                    d.payload.resize(1024 * 1024);
                }
                for (;;) {
                    const PayloadResult r = extract_payload(
                        d.file_bytes, d.blocks, block_index,
                        std::span<std::byte>(d.payload.data(), d.payload.size()),
                        std::span<uint32_t>(indices.data(), indices.size()),
                        options);
                    if (r.status == PayloadStatus::OutputTruncated
                        && r.needed > d.payload.size()) {
                        d.payload.resize(static_cast<size_t>(r.needed));
                        continue;
                    }
                    if (r.status != PayloadStatus::Ok) {
                        throw std::runtime_error("payload extraction failed");
                    }
                    return nb::bytes(reinterpret_cast<const char*>(
                                         d.payload.data()),
                                     static_cast<size_t>(r.written));
                }
            },
            "block_index"_a, "decompress"_a = true,
            "max_output_bytes"_a = 64ULL * 1024ULL * 1024ULL)
        .def("__len__",
             [](const PyDocument& d) {
                 return static_cast<uint64_t>(d.store.entries().size());
             })
        .def(
            "find_exif",
            [](std::shared_ptr<PyDocument> d, const std::string& ifd,
               uint16_t tag) {
                MetaKeyView key;
                key.kind              = MetaKeyKind::ExifTag;
                key.data.exif_tag.ifd = ifd;
                key.data.exif_tag.tag = tag;

                const std::span<const EntryId> ids = d->store.find_all(key);
                std::vector<PyEntry> out;
                out.reserve(ids.size());
                for (const EntryId id : ids) {
                    PyEntry e;
                    e.doc = d;
                    e.id  = id;
                    out.push_back(std::move(e));
                }
                return out;
            },
            "ifd"_a, "tag"_a)
        .def(
            "find_exr",
            [](std::shared_ptr<PyDocument> d, uint32_t part_index,
               const std::string& name) {
                MetaKeyView key;
                key.kind                          = MetaKeyKind::ExrAttribute;
                key.data.exr_attribute.part_index = part_index;
                key.data.exr_attribute.name       = name;

                const std::span<const EntryId> ids = d->store.find_all(key);
                std::vector<PyEntry> out;
                out.reserve(ids.size());
                for (const EntryId id : ids) {
                    PyEntry e;
                    e.doc = d;
                    e.id  = id;
                    out.push_back(std::move(e));
                }
                return out;
            },
            "part_index"_a, "name"_a)
        .def("__getitem__", [](std::shared_ptr<PyDocument> d, int64_t index) {
            const size_t n = d->store.entries().size();
            int64_t i      = index;
            if (i < 0) {
                i += static_cast<int64_t>(n);
            }
            if (i < 0 || static_cast<size_t>(i) >= n) {
                throw std::out_of_range("entry index out of range");
            }
            PyEntry e;
            e.doc = std::move(d);
            e.id  = static_cast<EntryId>(i);
            return e;
        });

    nb::class_<PyEntry>(m, "Entry")
        .def_prop_ro("key_kind",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).key.kind;
                     })
        .def_prop_ro("ifd",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::ExifTag) {
                             return nb::none();
                         }
                         const std::string ifd
                             = arena_string(e.doc->store.arena(),
                                            en.key.data.exif_tag.ifd);
                         return nb::str(ifd.c_str(), ifd.size());
                     })
        .def_prop_ro("tag",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::ExifTag) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.exif_tag.tag);
                     })
        .def_prop_ro("exr_part",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::ExrAttribute) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.exr_attribute.part_index);
                     })
        .def_prop_ro("exr_name",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::ExrAttribute) {
                             return nb::none();
                         }
                         const std::string s
                             = arena_string(e.doc->store.arena(),
                                            en.key.data.exr_attribute.name);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro("geotiff_key_id",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::GeotiffKey) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.geotiff_key.key_id);
                     })
        .def_prop_ro("iptc_record",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::IptcDataset) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.iptc_dataset.record);
                     })
        .def_prop_ro("iptc_dataset",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::IptcDataset) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.iptc_dataset.dataset);
                     })
        .def_prop_ro("photoshop_resource_id",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind == MetaKeyKind::PhotoshopIrb) {
                             return nb::int_(
                                 en.key.data.photoshop_irb.resource_id);
                         }
                         if (en.key.kind == MetaKeyKind::PhotoshopIrbField) {
                             return nb::int_(
                                 en.key.data.photoshop_irb_field.resource_id);
                         }
                         return nb::none();
                     })
        .def_prop_ro("photoshop_field",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::PhotoshopIrbField) {
                             return nb::none();
                         }
                         const std::string s = arena_string(
                             e.doc->store.arena(),
                             en.key.data.photoshop_irb_field.field);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro("icc_header_offset",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::IccHeaderField) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.icc_header_field.offset);
                     })
        .def_prop_ro("icc_tag_signature",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::IccTag) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.icc_tag.signature);
                     })
        .def_prop_ro("xmp_schema_ns",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::XmpProperty) {
                             return nb::none();
                         }
                         const std::string s
                             = arena_string(e.doc->store.arena(),
                                            en.key.data.xmp_property.schema_ns);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro("xmp_path",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::XmpProperty) {
                             return nb::none();
                         }
                         const std::string s = arena_string(
                             e.doc->store.arena(),
                             en.key.data.xmp_property.property_path);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro("png_text_keyword",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::PngText) {
                             return nb::none();
                         }
                         const std::string s
                             = arena_string(e.doc->store.arena(),
                                            en.key.data.png_text.keyword);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro("png_text_field",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind != MetaKeyKind::PngText) {
                             return nb::none();
                         }
                         const std::string s
                             = arena_string(e.doc->store.arena(),
                                            en.key.data.png_text.field);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro(
            "name",
            [](const PyEntry& e) -> nb::object {
                const Entry& en = e.doc->store.entry(e.id);
                if (en.key.kind == MetaKeyKind::ExifTag) {
                    const std::string_view n
                        = exif_entry_name(e.doc->store, en,
                                          ExifTagNamePolicy::ExifToolCompat);
                    if (n.empty()) {
                        return nb::none();
                    }
                    return nb::str(n.data(), n.size());
                }
                if (en.key.kind == MetaKeyKind::Comment) {
                    return nb::str("comment");
                }
                if (en.key.kind == MetaKeyKind::GeotiffKey) {
                    const std::string_view n = geotiff_key_name(
                        en.key.data.geotiff_key.key_id);
                    if (n.empty()) {
                        return nb::none();
                    }
                    return nb::str(n.data(), n.size());
                }
                if (en.key.kind == MetaKeyKind::IccTag) {
                    const std::string_view n = icc_tag_name(
                        en.key.data.icc_tag.signature);
                    if (n.empty()) {
                        return nb::none();
                    }
                    return nb::str(n.data(), n.size());
                }
                if (en.key.kind == MetaKeyKind::ExrAttribute) {
                    const std::string s
                        = arena_string(e.doc->store.arena(),
                                       en.key.data.exr_attribute.name);
                    return nb::str(s.c_str(), s.size());
                }
                if (en.key.kind == MetaKeyKind::PhotoshopIrbField) {
                    const std::string s
                        = arena_string(e.doc->store.arena(),
                                       en.key.data.photoshop_irb_field.field);
                    return nb::str(s.c_str(), s.size());
                }
                if (en.key.kind == MetaKeyKind::BmffField) {
                    const std::string s
                        = arena_string(e.doc->store.arena(),
                                       en.key.data.bmff_field.field);
                    return nb::str(s.c_str(), s.size());
                }
                if (en.key.kind == MetaKeyKind::JumbfField) {
                    const std::string s
                        = arena_string(e.doc->store.arena(),
                                       en.key.data.jumbf_field.field);
                    return nb::str(s.c_str(), s.size());
                }
                if (en.key.kind == MetaKeyKind::JumbfCborKey) {
                    const std::string s
                        = arena_string(e.doc->store.arena(),
                                       en.key.data.jumbf_cbor_key.key);
                    return nb::str(s.c_str(), s.size());
                }
                if (en.key.kind == MetaKeyKind::PngText) {
                    const std::string s
                        = arena_string(e.doc->store.arena(),
                                       en.key.data.png_text.keyword);
                    return nb::str(s.c_str(), s.size());
                }
                return nb::none();
            })
        .def_prop_ro("value_kind",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).value.kind;
                     })
        .def_prop_ro("elem_type",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).value.elem_type;
                     })
        .def_prop_ro("count",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).value.count;
                     })
        .def_prop_ro("text_encoding",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).value.text_encoding;
                     })
        .def_prop_ro("origin_block",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).origin.block;
                     })
        .def_prop_ro("origin_order",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).origin.order_in_block;
                     })
        .def_prop_ro("wire_family",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).origin.wire_type.family;
                     })
        .def_prop_ro("wire_type_code",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).origin.wire_type.code;
                     })
        .def_prop_ro("wire_type_name",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.origin.wire_type_name.size == 0U) {
                             return nb::none();
                         }
                         const std::string s
                             = arena_string(e.doc->store.arena(),
                                            en.origin.wire_type_name);
                         return nb::str(s.c_str(), s.size());
                     })
        .def_prop_ro("wire_count",
                     [](const PyEntry& e) {
                         return e.doc->store.entry(e.id).origin.wire_count;
                     })
        .def(
            "value",
            [](const PyEntry& e, uint32_t max_elements, uint32_t max_bytes) {
                const Entry& en = e.doc->store.entry(e.id);
                return value_to_python(e.doc->store.arena(), en.value,
                                       max_elements, max_bytes);
            },
            "max_elements"_a = 256, "max_bytes"_a = 4096)
        .def("__repr__", [](const PyEntry& e) {
            const Entry& en = e.doc->store.entry(e.id);
            std::string s;
            s.reserve(128);
            s.append("Entry(");
            if (en.key.kind == MetaKeyKind::ExifTag) {
                const std::string ifd = arena_string(e.doc->store.arena(),
                                                     en.key.data.exif_tag.ifd);
                s.append("ifd=\"");
                append_console_escaped_ascii(ifd, 64, &s);
                s.append("\", tag=0x");
                char tag_buf[8];
                std::snprintf(tag_buf, sizeof(tag_buf), "%04X",
                              static_cast<unsigned>(en.key.data.exif_tag.tag));
                s.append(tag_buf);
            } else if (en.key.kind == MetaKeyKind::ExrAttribute) {
                s.append("part=");
                s.append(std::to_string(static_cast<unsigned>(
                    en.key.data.exr_attribute.part_index)));
                s.append(", name=\"");
                const std::string name
                    = arena_string(e.doc->store.arena(),
                                   en.key.data.exr_attribute.name);
                append_console_escaped_ascii(name, 64, &s);
                s.append("\"");
            } else if (en.key.kind == MetaKeyKind::PhotoshopIrbField) {
                s.append("psirb=0x");
                char tag_buf[8];
                std::snprintf(tag_buf, sizeof(tag_buf), "%04X",
                              static_cast<unsigned>(
                                  en.key.data.photoshop_irb_field.resource_id));
                s.append(tag_buf);
                s.append(", field=\"");
                const std::string field
                    = arena_string(e.doc->store.arena(),
                                   en.key.data.photoshop_irb_field.field);
                append_console_escaped_ascii(field, 64, &s);
                s.append("\"");
            } else if (en.key.kind == MetaKeyKind::JumbfField) {
                s.append("jumbf=\"");
                const std::string field
                    = arena_string(e.doc->store.arena(),
                                   en.key.data.jumbf_field.field);
                append_console_escaped_ascii(field, 64, &s);
                s.append("\"");
            } else if (en.key.kind == MetaKeyKind::JumbfCborKey) {
                s.append("jumbf_cbor=\"");
                const std::string key
                    = arena_string(e.doc->store.arena(),
                                   en.key.data.jumbf_cbor_key.key);
                append_console_escaped_ascii(key, 64, &s);
                s.append("\"");
            } else if (en.key.kind == MetaKeyKind::PngText) {
                s.append("png_text=\"");
                const std::string keyword
                    = arena_string(e.doc->store.arena(),
                                   en.key.data.png_text.keyword);
                const std::string field
                    = arena_string(e.doc->store.arena(),
                                   en.key.data.png_text.field);
                append_console_escaped_ascii(keyword, 64, &s);
                s.append(".");
                append_console_escaped_ascii(field, 32, &s);
                s.append("\"");
            } else if (en.key.kind == MetaKeyKind::Comment) {
                s.append("comment");
            } else {
                s.append("kind=");
                s.append(std::to_string(static_cast<unsigned>(en.key.kind)));
            }
            s.append(", kind=");
            s.append(std::to_string(static_cast<unsigned>(en.value.kind)));
            s.append(", count=");
            s.append(std::to_string(static_cast<unsigned>(en.value.count)));
            s.append(")");
            return s;
        });

    m.def(
        "read",
        [](const std::string& path, bool include_pointer_tags,
           bool decode_makernote, bool decompress, bool include_xmp_sidecar,
           bool verify_c2pa, C2paVerifyBackend verify_backend,
           bool verify_require_resolved_references, uint64_t max_file_bytes,
           nb::object policy_obj) {
            OpenMetaResourcePolicy policy;
            const OpenMetaResourcePolicy* policy_ptr = nullptr;
            if (!policy_obj.is_none()) {
                policy     = nb::cast<OpenMetaResourcePolicy>(policy_obj);
                policy_ptr = &policy;
            }
            return read_document(path, include_pointer_tags, decode_makernote,
                                 decompress, include_xmp_sidecar, verify_c2pa,
                                 verify_backend,
                                 verify_require_resolved_references,
                                 max_file_bytes, policy_ptr);
        },
        "path"_a, "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decompress"_a = true, "include_xmp_sidecar"_a = false,
        "verify_c2pa"_a = false, "verify_backend"_a = C2paVerifyBackend::Auto,
        "verify_require_resolved_references"_a = false,
        "max_file_bytes"_a = 0ULL, "policy"_a = nb::none());

    m.def(
        "validate",
        [](const std::string& path, bool include_pointer_tags,
           bool decode_makernote, bool decode_printim, bool decompress,
           bool include_xmp_sidecar, bool verify_c2pa,
           C2paVerifyBackend verify_backend,
           bool verify_require_resolved_references, bool warnings_as_errors,
           bool ccm_require_dng_context, bool ccm_include_reduction_matrices,
           uint32_t ccm_max_fields, uint32_t ccm_max_values_per_field,
           CcmValidationMode ccm_validation_mode, uint64_t max_file_bytes,
           nb::object policy_obj) {
            return validate_file_to_python(
                path, include_pointer_tags, decode_makernote, decode_printim,
                decompress, include_xmp_sidecar, verify_c2pa, verify_backend,
                verify_require_resolved_references, warnings_as_errors,
                ccm_require_dng_context, ccm_include_reduction_matrices,
                ccm_max_fields, ccm_max_values_per_field, ccm_validation_mode,
                max_file_bytes, policy_obj);
        },
        "path"_a, "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_printim"_a = true, "decompress"_a = true,
        "include_xmp_sidecar"_a = false, "verify_c2pa"_a = false,
        "verify_backend"_a                     = C2paVerifyBackend::Auto,
        "verify_require_resolved_references"_a = false,
        "warnings_as_errors"_a = false, "ccm_require_dng_context"_a = true,
        "ccm_include_reduction_matrices"_a = true, "ccm_max_fields"_a = 128U,
        "ccm_max_values_per_field"_a = 256U,
        "ccm_validation_mode"_a      = CcmValidationMode::DngSpecWarnings,
        "max_file_bytes"_a = 0ULL, "policy"_a = nb::none());

    m.def(
        "transfer_probe",
        [](const std::string& path, TransferTargetFormat target_format,
           XmpSidecarFormat format, bool include_pointer_tags,
           bool decode_makernote, bool decode_embedded_containers,
           bool decompress, bool include_exif_app1, bool include_xmp_app1,
           bool include_icc_app2, bool include_iptc_app13,
           bool xmp_include_existing, bool xmp_exiftool_gpsdatetime_alias,
           bool xmp_project_exif, bool xmp_project_iptc,
           TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           nb::object c2pa_signed_package,
           nb::object c2pa_signed_logical_payload,
           nb::object c2pa_certificate_chain,
           nb::object c2pa_private_key_reference, nb::object c2pa_signing_time,
           nb::object c2pa_manifest_builder_output, bool include_payloads,
           nb::object time_patches, bool time_patch_strict_width,
           bool time_patch_require_slot, bool time_patch_auto_nul,
           nb::object edit_target_path, nb::object xmp_sidecar_base_path,
           XmpExistingSidecarMode xmp_existing_sidecar_mode,
           XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
           XmpExistingDestinationEmbeddedMode
               xmp_existing_destination_embedded_mode,
           XmpExistingDestinationEmbeddedPrecedence
               xmp_existing_destination_embedded_precedence,
           bool edit_apply,
           bool include_edited_bytes, bool include_c2pa_binding_bytes,
           bool include_c2pa_handoff_bytes,
           bool include_c2pa_signed_package_bytes,
           bool include_jxl_encoder_handoff_bytes,
           bool include_exr_attribute_values,
           bool include_transfer_payload_batch_bytes,
           bool include_transfer_package_batch_bytes,
           XmpConflictPolicy xmp_conflict_policy,
           XmpWritebackMode xmp_writeback_mode,
           XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
           XmpDestinationSidecarMode xmp_destination_sidecar_mode) {
            return transfer_probe_to_python(
                path, target_format, format, include_pointer_tags,
                decode_makernote, decode_embedded_containers, decompress,
                include_exif_app1, include_xmp_app1, include_icc_app2,
                include_iptc_app13, xmp_include_existing,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy,
                c2pa_policy, max_file_bytes, policy_obj, c2pa_signed_package,
                c2pa_signed_logical_payload, c2pa_certificate_chain,
                c2pa_private_key_reference, c2pa_signing_time,
                c2pa_manifest_builder_output, include_payloads, false,
                time_patches, time_patch_strict_width, time_patch_require_slot,
                time_patch_auto_nul, edit_target_path, xmp_sidecar_base_path,
                xmp_existing_sidecar_mode, xmp_existing_sidecar_precedence,
                xmp_existing_destination_embedded_mode,
                xmp_existing_destination_embedded_precedence,
                edit_apply,
                include_edited_bytes, false, include_c2pa_binding_bytes, false,
                include_c2pa_handoff_bytes, include_c2pa_signed_package_bytes,
                include_jxl_encoder_handoff_bytes,
                include_exr_attribute_values,
                include_transfer_payload_batch_bytes,
                include_transfer_package_batch_bytes, false,
                xmp_conflict_policy, xmp_writeback_mode,
                xmp_destination_embedded_mode,
                xmp_destination_sidecar_mode, nb::none(), false, false,
                true);
        },
        "path"_a, "target_format"_a = TransferTargetFormat::Jpeg,
        "format"_a               = XmpSidecarFormat::Portable,
        "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_embedded_containers"_a = true, "decompress"_a = true,
        "include_exif_app1"_a = true, "include_xmp_app1"_a = true,
        "include_icc_app2"_a = true, "include_iptc_app13"_a = true,
        "xmp_include_existing"_a           = false,
        "xmp_exiftool_gpsdatetime_alias"_a = false,
        "xmp_project_exif"_a             = true,
        "xmp_project_iptc"_a             = true,
        "makernote_policy"_a               = TransferPolicyAction::Keep,
        "jumbf_policy"_a                   = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "c2pa_signed_package"_a = nb::none(),
        "c2pa_signed_logical_payload"_a  = nb::none(),
        "c2pa_certificate_chain"_a       = nb::none(),
        "c2pa_private_key_reference"_a   = nb::none(),
        "c2pa_signing_time"_a            = nb::none(),
        "c2pa_manifest_builder_output"_a = nb::none(),
        "include_payloads"_a = false, "time_patches"_a = nb::none(),
        "time_patch_strict_width"_a = true, "time_patch_require_slot"_a = false,
        "time_patch_auto_nul"_a = true, "edit_target_path"_a = nb::none(),
        "xmp_sidecar_base_path"_a = nb::none(),
        "xmp_existing_sidecar_mode"_a = XmpExistingSidecarMode::Ignore,
        "xmp_existing_sidecar_precedence"_a
        = XmpExistingSidecarPrecedence::SidecarWins,
        "xmp_existing_destination_embedded_mode"_a
        = XmpExistingDestinationEmbeddedMode::Ignore,
        "xmp_existing_destination_embedded_precedence"_a
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
        "edit_apply"_a = true, "include_edited_bytes"_a = false,
        "include_c2pa_binding_bytes"_a           = false,
        "include_c2pa_handoff_bytes"_a           = false,
        "include_c2pa_signed_package_bytes"_a    = false,
        "include_jxl_encoder_handoff_bytes"_a    = false,
        "include_exr_attribute_values"_a         = false,
        "include_transfer_payload_batch_bytes"_a = false,
        "include_transfer_package_batch_bytes"_a = false,
        "xmp_conflict_policy"_a = XmpConflictPolicy::CurrentBehavior,
        "xmp_writeback_mode"_a = XmpWritebackMode::EmbeddedOnly,
        "xmp_destination_embedded_mode"_a
        = XmpDestinationEmbeddedMode::PreserveExisting,
        "xmp_destination_sidecar_mode"_a
        = XmpDestinationSidecarMode::PreserveExisting);

    m.def(
        "unsafe_transfer_probe",
        [](const std::string& path, TransferTargetFormat target_format,
           XmpSidecarFormat format, bool include_pointer_tags,
           bool decode_makernote, bool decode_embedded_containers,
           bool decompress, bool include_exif_app1, bool include_xmp_app1,
           bool include_icc_app2, bool include_iptc_app13,
           bool xmp_include_existing, bool xmp_exiftool_gpsdatetime_alias,
           bool xmp_project_exif, bool xmp_project_iptc,
           TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           nb::object c2pa_signed_package,
           nb::object c2pa_signed_logical_payload,
           nb::object c2pa_certificate_chain,
           nb::object c2pa_private_key_reference, nb::object c2pa_signing_time,
           nb::object c2pa_manifest_builder_output, bool include_payloads,
           nb::object time_patches, bool time_patch_strict_width,
           bool time_patch_require_slot, bool time_patch_auto_nul,
           nb::object edit_target_path, nb::object xmp_sidecar_base_path,
           XmpExistingSidecarMode xmp_existing_sidecar_mode,
           XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
           XmpExistingDestinationEmbeddedMode
               xmp_existing_destination_embedded_mode,
           XmpExistingDestinationEmbeddedPrecedence
               xmp_existing_destination_embedded_precedence,
           bool edit_apply,
           bool include_edited_bytes, bool include_c2pa_binding_bytes,
           bool include_c2pa_handoff_bytes,
           bool include_c2pa_signed_package_bytes,
           bool include_jxl_encoder_handoff_bytes,
           bool include_exr_attribute_values,
           bool include_transfer_payload_batch_bytes,
           bool include_transfer_package_batch_bytes,
           XmpConflictPolicy xmp_conflict_policy,
           XmpWritebackMode xmp_writeback_mode,
           XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
           XmpDestinationSidecarMode xmp_destination_sidecar_mode) {
            return transfer_probe_to_python(
                path, target_format, format, include_pointer_tags,
                decode_makernote, decode_embedded_containers, decompress,
                include_exif_app1, include_xmp_app1, include_icc_app2,
                include_iptc_app13, xmp_include_existing,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy,
                c2pa_policy, max_file_bytes, policy_obj, c2pa_signed_package,
                c2pa_signed_logical_payload, c2pa_certificate_chain,
                c2pa_private_key_reference, c2pa_signing_time,
                c2pa_manifest_builder_output, include_payloads, true,
                time_patches, time_patch_strict_width, time_patch_require_slot,
                time_patch_auto_nul, edit_target_path, xmp_sidecar_base_path,
                xmp_existing_sidecar_mode, xmp_existing_sidecar_precedence,
                xmp_existing_destination_embedded_mode,
                xmp_existing_destination_embedded_precedence,
                edit_apply,
                include_edited_bytes, true, include_c2pa_binding_bytes, true,
                include_c2pa_handoff_bytes, include_c2pa_signed_package_bytes,
                include_jxl_encoder_handoff_bytes,
                include_exr_attribute_values,
                include_transfer_payload_batch_bytes,
                include_transfer_package_batch_bytes, true,
                xmp_conflict_policy, xmp_writeback_mode,
                xmp_destination_embedded_mode,
                xmp_destination_sidecar_mode, nb::none(), false, false,
                true);
        },
        "path"_a, "target_format"_a = TransferTargetFormat::Jpeg,
        "format"_a               = XmpSidecarFormat::Portable,
        "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_embedded_containers"_a = true, "decompress"_a = true,
        "include_exif_app1"_a = true, "include_xmp_app1"_a = true,
        "include_icc_app2"_a = true, "include_iptc_app13"_a = true,
        "xmp_include_existing"_a           = false,
        "xmp_exiftool_gpsdatetime_alias"_a = false,
        "xmp_project_exif"_a             = true,
        "xmp_project_iptc"_a             = true,
        "makernote_policy"_a               = TransferPolicyAction::Keep,
        "jumbf_policy"_a                   = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "c2pa_signed_package"_a = nb::none(),
        "c2pa_signed_logical_payload"_a  = nb::none(),
        "c2pa_certificate_chain"_a       = nb::none(),
        "c2pa_private_key_reference"_a   = nb::none(),
        "c2pa_signing_time"_a            = nb::none(),
        "c2pa_manifest_builder_output"_a = nb::none(),
        "include_payloads"_a = false, "time_patches"_a = nb::none(),
        "time_patch_strict_width"_a = true, "time_patch_require_slot"_a = false,
        "time_patch_auto_nul"_a = true, "edit_target_path"_a = nb::none(),
        "xmp_sidecar_base_path"_a = nb::none(),
        "xmp_existing_sidecar_mode"_a = XmpExistingSidecarMode::Ignore,
        "xmp_existing_sidecar_precedence"_a
        = XmpExistingSidecarPrecedence::SidecarWins,
        "xmp_existing_destination_embedded_mode"_a
        = XmpExistingDestinationEmbeddedMode::Ignore,
        "xmp_existing_destination_embedded_precedence"_a
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
        "edit_apply"_a = true, "include_edited_bytes"_a = false,
        "include_c2pa_binding_bytes"_a           = false,
        "include_c2pa_handoff_bytes"_a           = false,
        "include_c2pa_signed_package_bytes"_a    = false,
        "include_jxl_encoder_handoff_bytes"_a    = false,
        "include_exr_attribute_values"_a         = false,
        "include_transfer_payload_batch_bytes"_a = false,
        "include_transfer_package_batch_bytes"_a = false,
        "xmp_conflict_policy"_a = XmpConflictPolicy::CurrentBehavior,
        "xmp_writeback_mode"_a = XmpWritebackMode::EmbeddedOnly,
        "xmp_destination_embedded_mode"_a
        = XmpDestinationEmbeddedMode::PreserveExisting,
        "xmp_destination_sidecar_mode"_a
        = XmpDestinationSidecarMode::PreserveExisting);

    m.def(
        "transfer_file",
        [](const std::string& path, TransferTargetFormat target_format,
           XmpSidecarFormat format, bool include_pointer_tags,
           bool decode_makernote, bool decode_embedded_containers,
           bool decompress, bool include_exif_app1, bool include_xmp_app1,
           bool include_icc_app2, bool include_iptc_app13,
           bool xmp_include_existing, bool xmp_exiftool_gpsdatetime_alias,
           bool xmp_project_exif, bool xmp_project_iptc,
           TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           nb::object c2pa_signed_package,
           nb::object c2pa_signed_logical_payload,
           nb::object c2pa_certificate_chain,
           nb::object c2pa_private_key_reference, nb::object c2pa_signing_time,
           nb::object c2pa_manifest_builder_output, bool include_payloads,
           nb::object time_patches, bool time_patch_strict_width,
           bool time_patch_require_slot, bool time_patch_auto_nul,
           nb::object edit_target_path, nb::object xmp_sidecar_base_path,
           XmpExistingSidecarMode xmp_existing_sidecar_mode,
           XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
           XmpExistingDestinationEmbeddedMode
               xmp_existing_destination_embedded_mode,
           XmpExistingDestinationEmbeddedPrecedence
               xmp_existing_destination_embedded_precedence,
           bool edit_apply,
           bool include_edited_bytes, bool include_c2pa_binding_bytes,
           bool include_c2pa_handoff_bytes,
           bool include_c2pa_signed_package_bytes,
           bool include_jxl_encoder_handoff_bytes,
           bool include_exr_attribute_values,
           bool include_transfer_payload_batch_bytes,
           bool include_transfer_package_batch_bytes,
           XmpConflictPolicy xmp_conflict_policy,
           XmpWritebackMode xmp_writeback_mode,
           XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
           XmpDestinationSidecarMode xmp_destination_sidecar_mode,
           const std::string& output_path, bool overwrite_output,
           bool overwrite_xmp_sidecar,
           bool remove_destination_xmp_sidecar) {
            return transfer_probe_to_python(
                path, target_format, format, include_pointer_tags,
                decode_makernote, decode_embedded_containers, decompress,
                include_exif_app1, include_xmp_app1, include_icc_app2,
                include_iptc_app13, xmp_include_existing,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy,
                c2pa_policy, max_file_bytes, policy_obj, c2pa_signed_package,
                c2pa_signed_logical_payload, c2pa_certificate_chain,
                c2pa_private_key_reference, c2pa_signing_time,
                c2pa_manifest_builder_output, include_payloads, false,
                time_patches, time_patch_strict_width, time_patch_require_slot,
                time_patch_auto_nul, edit_target_path, xmp_sidecar_base_path,
                xmp_existing_sidecar_mode, xmp_existing_sidecar_precedence,
                xmp_existing_destination_embedded_mode,
                xmp_existing_destination_embedded_precedence,
                edit_apply,
                include_edited_bytes, false, include_c2pa_binding_bytes, false,
                include_c2pa_handoff_bytes, include_c2pa_signed_package_bytes,
                include_jxl_encoder_handoff_bytes,
                include_exr_attribute_values,
                include_transfer_payload_batch_bytes,
                include_transfer_package_batch_bytes, false,
                xmp_conflict_policy, xmp_writeback_mode,
                xmp_destination_embedded_mode,
                xmp_destination_sidecar_mode,
                nb::str(output_path.c_str(), output_path.size()),
                overwrite_output, overwrite_xmp_sidecar,
                remove_destination_xmp_sidecar);
        },
        "path"_a, "target_format"_a = TransferTargetFormat::Jpeg,
        "format"_a               = XmpSidecarFormat::Portable,
        "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_embedded_containers"_a = true, "decompress"_a = true,
        "include_exif_app1"_a = true, "include_xmp_app1"_a = true,
        "include_icc_app2"_a = true, "include_iptc_app13"_a = true,
        "xmp_include_existing"_a           = false,
        "xmp_exiftool_gpsdatetime_alias"_a = false,
        "xmp_project_exif"_a             = true,
        "xmp_project_iptc"_a             = true,
        "makernote_policy"_a               = TransferPolicyAction::Keep,
        "jumbf_policy"_a                   = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "c2pa_signed_package"_a = nb::none(),
        "c2pa_signed_logical_payload"_a  = nb::none(),
        "c2pa_certificate_chain"_a       = nb::none(),
        "c2pa_private_key_reference"_a   = nb::none(),
        "c2pa_signing_time"_a            = nb::none(),
        "c2pa_manifest_builder_output"_a = nb::none(),
        "include_payloads"_a = false, "time_patches"_a = nb::none(),
        "time_patch_strict_width"_a = true, "time_patch_require_slot"_a = false,
        "time_patch_auto_nul"_a = true, "edit_target_path"_a = nb::none(),
        "xmp_sidecar_base_path"_a = nb::none(),
        "xmp_existing_sidecar_mode"_a = XmpExistingSidecarMode::Ignore,
        "xmp_existing_sidecar_precedence"_a
        = XmpExistingSidecarPrecedence::SidecarWins,
        "xmp_existing_destination_embedded_mode"_a
        = XmpExistingDestinationEmbeddedMode::Ignore,
        "xmp_existing_destination_embedded_precedence"_a
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
        "edit_apply"_a = true, "include_edited_bytes"_a = false,
        "include_c2pa_binding_bytes"_a           = false,
        "include_c2pa_handoff_bytes"_a           = false,
        "include_c2pa_signed_package_bytes"_a    = false,
        "include_jxl_encoder_handoff_bytes"_a    = false,
        "include_exr_attribute_values"_a         = false,
        "include_transfer_payload_batch_bytes"_a = false,
        "include_transfer_package_batch_bytes"_a = false,
        "xmp_conflict_policy"_a = XmpConflictPolicy::CurrentBehavior,
        "xmp_writeback_mode"_a = XmpWritebackMode::EmbeddedOnly,
        "xmp_destination_embedded_mode"_a
        = XmpDestinationEmbeddedMode::PreserveExisting,
        "xmp_destination_sidecar_mode"_a
        = XmpDestinationSidecarMode::PreserveExisting,
        "output_path"_a, "overwrite_output"_a = false,
        "overwrite_xmp_sidecar"_a = false,
        "remove_destination_xmp_sidecar"_a = true);

    m.def(
        "unsafe_transfer_file",
        [](const std::string& path, TransferTargetFormat target_format,
           XmpSidecarFormat format, bool include_pointer_tags,
           bool decode_makernote, bool decode_embedded_containers,
           bool decompress, bool include_exif_app1, bool include_xmp_app1,
           bool include_icc_app2, bool include_iptc_app13,
           bool xmp_include_existing, bool xmp_exiftool_gpsdatetime_alias,
           bool xmp_project_exif, bool xmp_project_iptc,
           TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy, TransferPolicyAction c2pa_policy,
           uint64_t max_file_bytes, nb::object policy_obj,
           nb::object c2pa_signed_package,
           nb::object c2pa_signed_logical_payload,
           nb::object c2pa_certificate_chain,
           nb::object c2pa_private_key_reference, nb::object c2pa_signing_time,
           nb::object c2pa_manifest_builder_output, bool include_payloads,
           nb::object time_patches, bool time_patch_strict_width,
           bool time_patch_require_slot, bool time_patch_auto_nul,
           nb::object edit_target_path, nb::object xmp_sidecar_base_path,
           XmpExistingSidecarMode xmp_existing_sidecar_mode,
           XmpExistingSidecarPrecedence xmp_existing_sidecar_precedence,
           XmpExistingDestinationEmbeddedMode
               xmp_existing_destination_embedded_mode,
           XmpExistingDestinationEmbeddedPrecedence
               xmp_existing_destination_embedded_precedence,
           bool edit_apply,
           bool include_edited_bytes, bool include_c2pa_binding_bytes,
           bool include_c2pa_handoff_bytes,
           bool include_c2pa_signed_package_bytes,
           bool include_jxl_encoder_handoff_bytes,
           bool include_exr_attribute_values,
           bool include_transfer_payload_batch_bytes,
           bool include_transfer_package_batch_bytes,
           XmpConflictPolicy xmp_conflict_policy,
           XmpWritebackMode xmp_writeback_mode,
           XmpDestinationEmbeddedMode xmp_destination_embedded_mode,
           XmpDestinationSidecarMode xmp_destination_sidecar_mode,
           const std::string& output_path, bool overwrite_output,
           bool overwrite_xmp_sidecar,
           bool remove_destination_xmp_sidecar) {
            return transfer_probe_to_python(
                path, target_format, format, include_pointer_tags,
                decode_makernote, decode_embedded_containers, decompress,
                include_exif_app1, include_xmp_app1, include_icc_app2,
                include_iptc_app13, xmp_include_existing,
                xmp_exiftool_gpsdatetime_alias, xmp_project_exif,
                xmp_project_iptc, makernote_policy, jumbf_policy,
                c2pa_policy, max_file_bytes, policy_obj, c2pa_signed_package,
                c2pa_signed_logical_payload, c2pa_certificate_chain,
                c2pa_private_key_reference, c2pa_signing_time,
                c2pa_manifest_builder_output, include_payloads, true,
                time_patches, time_patch_strict_width, time_patch_require_slot,
                time_patch_auto_nul, edit_target_path, xmp_sidecar_base_path,
                xmp_existing_sidecar_mode, xmp_existing_sidecar_precedence,
                xmp_existing_destination_embedded_mode,
                xmp_existing_destination_embedded_precedence,
                edit_apply,
                include_edited_bytes, true, include_c2pa_binding_bytes, true,
                include_c2pa_handoff_bytes, include_c2pa_signed_package_bytes,
                include_jxl_encoder_handoff_bytes,
                include_exr_attribute_values,
                include_transfer_payload_batch_bytes,
                include_transfer_package_batch_bytes, true,
                xmp_conflict_policy, xmp_writeback_mode,
                xmp_destination_embedded_mode,
                xmp_destination_sidecar_mode,
                nb::str(output_path.c_str(), output_path.size()),
                overwrite_output, overwrite_xmp_sidecar,
                remove_destination_xmp_sidecar);
        },
        "path"_a, "target_format"_a = TransferTargetFormat::Jpeg,
        "format"_a               = XmpSidecarFormat::Portable,
        "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_embedded_containers"_a = true, "decompress"_a = true,
        "include_exif_app1"_a = true, "include_xmp_app1"_a = true,
        "include_icc_app2"_a = true, "include_iptc_app13"_a = true,
        "xmp_include_existing"_a           = false,
        "xmp_exiftool_gpsdatetime_alias"_a = false,
        "xmp_project_exif"_a             = true,
        "xmp_project_iptc"_a             = true,
        "makernote_policy"_a               = TransferPolicyAction::Keep,
        "jumbf_policy"_a                   = TransferPolicyAction::Keep,
        "c2pa_policy"_a = TransferPolicyAction::Keep, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none(), "c2pa_signed_package"_a = nb::none(),
        "c2pa_signed_logical_payload"_a  = nb::none(),
        "c2pa_certificate_chain"_a       = nb::none(),
        "c2pa_private_key_reference"_a   = nb::none(),
        "c2pa_signing_time"_a            = nb::none(),
        "c2pa_manifest_builder_output"_a = nb::none(),
        "include_payloads"_a = false, "time_patches"_a = nb::none(),
        "time_patch_strict_width"_a = true, "time_patch_require_slot"_a = false,
        "time_patch_auto_nul"_a = true, "edit_target_path"_a = nb::none(),
        "xmp_sidecar_base_path"_a = nb::none(),
        "xmp_existing_sidecar_mode"_a = XmpExistingSidecarMode::Ignore,
        "xmp_existing_sidecar_precedence"_a
        = XmpExistingSidecarPrecedence::SidecarWins,
        "xmp_existing_destination_embedded_mode"_a
        = XmpExistingDestinationEmbeddedMode::Ignore,
        "xmp_existing_destination_embedded_precedence"_a
        = XmpExistingDestinationEmbeddedPrecedence::DestinationWins,
        "edit_apply"_a = true, "include_edited_bytes"_a = false,
        "include_c2pa_binding_bytes"_a           = false,
        "include_c2pa_handoff_bytes"_a           = false,
        "include_c2pa_signed_package_bytes"_a    = false,
        "include_jxl_encoder_handoff_bytes"_a    = false,
        "include_exr_attribute_values"_a         = false,
        "include_transfer_payload_batch_bytes"_a = false,
        "include_transfer_package_batch_bytes"_a = false,
        "xmp_conflict_policy"_a = XmpConflictPolicy::CurrentBehavior,
        "xmp_writeback_mode"_a = XmpWritebackMode::EmbeddedOnly,
        "xmp_destination_embedded_mode"_a
        = XmpDestinationEmbeddedMode::PreserveExisting,
        "xmp_destination_sidecar_mode"_a
        = XmpDestinationSidecarMode::PreserveExisting,
        "output_path"_a, "overwrite_output"_a = false,
        "overwrite_xmp_sidecar"_a = false,
        "remove_destination_xmp_sidecar"_a = true);

    m.def(
        "build_exr_attribute_batch_from_file",
        [](const std::string& path, XmpSidecarFormat format,
           bool include_pointer_tags, bool decode_makernote,
           bool decode_embedded_containers, bool decompress,
           bool include_exif_app1, bool include_xmp_app1,
           bool include_icc_app2, bool include_iptc_app13,
           bool xmp_include_existing, bool xmp_exiftool_gpsdatetime_alias,
           bool xmp_project_exif, bool xmp_project_iptc,
           TransferPolicyAction makernote_policy,
           TransferPolicyAction jumbf_policy,
           TransferPolicyAction c2pa_policy, uint64_t max_file_bytes,
           nb::object policy_obj, bool include_values) {
            return build_exr_attribute_batch_from_file_to_python(
                path, format, include_pointer_tags, decode_makernote,
                decode_embedded_containers, decompress, include_exif_app1,
                include_xmp_app1, include_icc_app2, include_iptc_app13,
                xmp_include_existing, xmp_exiftool_gpsdatetime_alias,
                xmp_project_exif, xmp_project_iptc, makernote_policy,
                jumbf_policy, c2pa_policy, max_file_bytes, policy_obj,
                include_values);
        },
        "path"_a, "format"_a = XmpSidecarFormat::Portable,
        "include_pointer_tags"_a = true, "decode_makernote"_a = false,
        "decode_embedded_containers"_a = true, "decompress"_a = true,
        "include_exif_app1"_a = true, "include_xmp_app1"_a = true,
        "include_icc_app2"_a = true, "include_iptc_app13"_a = true,
        "xmp_include_existing"_a           = false,
        "xmp_exiftool_gpsdatetime_alias"_a = false,
        "xmp_project_exif"_a               = true,
        "xmp_project_iptc"_a               = true,
        "makernote_policy"_a = TransferPolicyAction::Keep,
        "jumbf_policy"_a     = TransferPolicyAction::Keep,
        "c2pa_policy"_a      = TransferPolicyAction::Keep,
        "max_file_bytes"_a   = 0ULL,
        "policy"_a           = nb::none(),
        "include_values"_a   = false);

    m.def(
        "inspect_transfer_payload_batch",
        [](nb::object payload_batch, bool include_payloads) {
            return inspect_transfer_payload_batch_to_python(payload_batch,
                                                            include_payloads,
                                                            false);
        },
        "payload_batch"_a, "include_payloads"_a = false);

    m.def(
        "unsafe_inspect_transfer_payload_batch",
        [](nb::object payload_batch, bool include_payloads) {
            return inspect_transfer_payload_batch_to_python(payload_batch,
                                                            include_payloads,
                                                            true);
        },
        "payload_batch"_a, "include_payloads"_a = false);

    m.def(
        "inspect_transfer_package_batch",
        [](nb::object package_batch, bool include_chunk_bytes) {
            return inspect_transfer_package_batch_to_python(package_batch,
                                                            include_chunk_bytes,
                                                            false);
        },
        "package_batch"_a, "include_chunk_bytes"_a = false);

    m.def(
        "unsafe_inspect_transfer_package_batch",
        [](nb::object package_batch, bool include_chunk_bytes) {
            return inspect_transfer_package_batch_to_python(package_batch,
                                                            include_chunk_bytes,
                                                            true);
        },
        "package_batch"_a, "include_chunk_bytes"_a = false);

    m.def(
        "inspect_jxl_encoder_handoff",
        [](nb::object handoff, bool include_icc_profile) {
            return inspect_jxl_encoder_handoff_to_python(handoff,
                                                         include_icc_profile,
                                                         false);
        },
        "handoff"_a, "include_icc_profile"_a = false);

    m.def(
        "unsafe_inspect_jxl_encoder_handoff",
        [](nb::object handoff, bool include_icc_profile) {
            return inspect_jxl_encoder_handoff_to_python(handoff,
                                                         include_icc_profile,
                                                         true);
        },
        "handoff"_a, "include_icc_profile"_a = false);

    m.def(
        "inspect_transfer_artifact",
        [](nb::object artifact) {
            return inspect_transfer_artifact_to_python(artifact);
        },
        "artifact"_a);

    m.def("console_text", &console_text, "data"_a, "max_bytes"_a = 4096U);
    m.def("hex_bytes", &hex_bytes, "data"_a, "max_bytes"_a = 4096U);
    m.def("unsafe_text", &unsafe_text, "data"_a, "max_bytes"_a = 4096U);
    m.def("unsafe_test", &unsafe_text, "data"_a, "max_bytes"_a = 4096U);

    m.def("build_info", []() {
        const BuildInfo& bi = build_info();
        nb::dict d;
        d["version"]              = sv_to_py(bi.version);
        d["build_timestamp_utc"]  = sv_to_py(bi.build_timestamp_utc);
        d["build_type"]           = sv_to_py(bi.build_type);
        d["cmake_generator"]      = sv_to_py(bi.cmake_generator);
        d["system_name"]          = sv_to_py(bi.system_name);
        d["system_processor"]     = sv_to_py(bi.system_processor);
        d["cxx_compiler_id"]      = sv_to_py(bi.cxx_compiler_id);
        d["cxx_compiler_version"] = sv_to_py(bi.cxx_compiler_version);
        d["cxx_compiler"]         = sv_to_py(bi.cxx_compiler);
        d["linkage_static"]       = nb::bool_(bi.linkage_static);
        d["linkage_shared"]       = nb::bool_(bi.linkage_shared);
        d["option_with_zlib"]     = nb::bool_(bi.option_with_zlib);
        d["option_with_brotli"]   = nb::bool_(bi.option_with_brotli);
        d["option_with_expat"]    = nb::bool_(bi.option_with_expat);
        d["has_zlib"]             = nb::bool_(bi.has_zlib);
        d["has_brotli"]           = nb::bool_(bi.has_brotli);
        d["has_expat"]            = nb::bool_(bi.has_expat);
        return d;
    });

    m.def("info_lines", &info_lines);
    m.def("python_info_line", &python_info_line);

    m.def(
        "icc_tag_name",
        [](uint32_t signature) -> nb::object {
            const std::string_view n = icc_tag_name(signature);
            if (n.empty()) {
                return nb::none();
            }
            return nb::str(n.data(), n.size());
        },
        "signature"_a);

    m.def(
        "icc_interpret",
        [](uint32_t signature, nb::bytes tag_bytes, uint32_t max_values,
           uint32_t max_text_bytes) {
            return icc_interpret_to_python(signature, tag_bytes, max_values,
                                           max_text_bytes);
        },
        "signature"_a, "tag_bytes"_a, "max_values"_a = 512U,
        "max_text_bytes"_a = 4096U);

    m.def(
        "icc_render_value",
        [](uint32_t signature, nb::bytes tag_bytes, uint32_t max_values,
           uint32_t max_text_bytes) {
            return icc_render_value_to_python(signature, tag_bytes, max_values,
                                              max_text_bytes);
        },
        "signature"_a, "tag_bytes"_a, "max_values"_a = 512U,
        "max_text_bytes"_a = 4096U);

    m.def(
        "exif_tag_name",
        [](const std::string& ifd, uint16_t tag) -> nb::object {
            const std::string_view n = exif_tag_name(ifd, tag);
            if (n.empty()) {
                return nb::none();
            }
            return nb::str(n.data(), n.size());
        },
        "ifd"_a, "tag"_a);
}
