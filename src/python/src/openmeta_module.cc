#include "openmeta/build_info.h"
#include "openmeta/console_format.h"
#include "openmeta/container_payload.h"
#include "openmeta/exif_tag_names.h"
#include "openmeta/geotiff_key_names.h"
#include "openmeta/interop_export.h"
#include "openmeta/mapped_file.h"
#include "openmeta/ocio_adapter.h"
#include "openmeta/oiio_adapter.h"
#include "openmeta/resource_policy.h"
#include "openmeta/simple_meta.h"
#include "openmeta/xmp_dump.h"

#include <nanobind/nanobind.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/shared_ptr.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <algorithm>
#include <bit>
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


    static XmpSidecarRequest
    make_xmp_sidecar_request(XmpSidecarFormat format, uint64_t max_output_bytes,
                             uint32_t max_entries, bool include_exif,
                             bool include_existing_xmp, bool include_origin,
                             bool include_wire, bool include_flags,
                             bool include_names)
    {
        XmpSidecarRequest request;
        request.format                  = format;
        request.limits.max_output_bytes = max_output_bytes;
        request.limits.max_entries      = max_entries;
        request.include_exif            = include_exif;
        request.include_existing_xmp    = include_existing_xmp;
        request.include_origin          = include_origin;
        request.include_wire            = include_wire;
        request.include_flags           = include_flags;
        request.include_names           = include_names;
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

        void on_item(const ExportItem& item) override
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
              uint64_t max_file_bytes,
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
    apply_resource_policy(policy, &decode_options.exif, &decode_options.payload);
    apply_resource_policy(policy, &decode_options.xmp, &decode_options.exr,
                          &decode_options.icc, &decode_options.iptc,
                          &decode_options.photoshop_irb);
    decode_options.exif.include_pointer_tags       = include_pointer_tags;
    decode_options.exif.decode_makernote           = decode_makernote;
    decode_options.exif.decode_embedded_containers = true;
    decode_options.payload.decompress              = decompress;

    // Release the GIL while performing file I/O and metadata decoding so callers
    // (and internal comparison tools) can read in parallel from multiple Python
    // threads. All work below this point is pure C/C++ and does not touch the
    // Python C API.
    nb::gil_scoped_release gil_release;

    const MappedFileStatus st
        = doc->file.open(path.c_str(), policy.max_file_bytes);
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
            const XmpDecodeResult one = decode_xmp_packet(
                xmp_bytes, doc->store, EntryFlags::None, decode_options.xmp);
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
        .value("ExrAttribute", MetaKeyKind::ExrAttribute)
        .value("IptcDataset", MetaKeyKind::IptcDataset)
        .value("XmpProperty", MetaKeyKind::XmpProperty)
        .value("IccHeaderField", MetaKeyKind::IccHeaderField)
        .value("IccTag", MetaKeyKind::IccTag)
        .value("PhotoshopIrb", MetaKeyKind::PhotoshopIrb)
        .value("GeotiffKey", MetaKeyKind::GeotiffKey)
        .value("PrintImField", MetaKeyKind::PrintImField)
        .value("BmffField", MetaKeyKind::BmffField)
        .value("JumbfField", MetaKeyKind::JumbfField)
        .value("JumbfCborKey", MetaKeyKind::JumbfCborKey);

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

    nb::enum_<XmpSidecarFormat>(m, "XmpSidecarFormat")
        .value("Lossless", XmpSidecarFormat::Lossless)
        .value("Portable", XmpSidecarFormat::Portable);

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
        .def_rw("icc_limits", &OpenMetaResourcePolicy::icc_limits)
        .def_rw("iptc_limits", &OpenMetaResourcePolicy::iptc_limits)
        .def_rw("photoshop_irb_limits",
                &OpenMetaResourcePolicy::photoshop_irb_limits)
        .def_rw("preview_scan_limits",
                &OpenMetaResourcePolicy::preview_scan_limits)
        .def_rw("max_preview_output_bytes",
                &OpenMetaResourcePolicy::max_preview_output_bytes)
        .def_rw("xmp_dump_limits", &OpenMetaResourcePolicy::xmp_dump_limits)
        .def_rw("max_decode_millis",
                &OpenMetaResourcePolicy::max_decode_millis)
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
                    true, false, include_origin, include_wire, include_flags,
                    include_names);
                return dump_xmp_sidecar_to_python(d->store, request);
            },
            "max_output_bytes"_a = 0ULL, "max_entries"_a = 0U,
            "include_origin"_a = true, "include_wire"_a = true,
            "include_flags"_a = true, "include_names"_a = true)
        .def(
            "dump_xmp_portable",
            [](std::shared_ptr<PyDocument> d, uint64_t max_output_bytes,
               uint32_t max_entries, bool include_exif,
               bool include_existing_xmp) {
                const XmpSidecarRequest request = make_xmp_sidecar_request(
                    XmpSidecarFormat::Portable, max_output_bytes, max_entries,
                    include_exif, include_existing_xmp, true, true, true, true);
                return dump_xmp_sidecar_to_python(d->store, request);
            },
            "max_output_bytes"_a = 0ULL, "max_entries"_a = 0U,
            "include_exif"_a = true, "include_existing_xmp"_a = false)
        .def(
            "dump_xmp_sidecar",
            [](std::shared_ptr<PyDocument> d, XmpSidecarFormat format,
               uint64_t max_output_bytes, uint32_t max_entries,
               bool include_exif, bool include_existing_xmp,
               bool include_origin, bool include_wire, bool include_flags,
               bool include_names) {
                const XmpSidecarRequest request = make_xmp_sidecar_request(
                    format, max_output_bytes, max_entries, include_exif,
                    include_existing_xmp, include_origin, include_wire,
                    include_flags, include_names);
                return dump_xmp_sidecar_to_python(d->store, request);
            },
            "format"_a           = XmpSidecarFormat::Lossless,
            "max_output_bytes"_a = 0ULL, "max_entries"_a = 0U,
            "include_exif"_a = true, "include_existing_xmp"_a = false,
            "include_origin"_a = true, "include_wire"_a = true,
            "include_flags"_a = true, "include_names"_a = true)
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
                         if (en.key.kind != MetaKeyKind::PhotoshopIrb) {
                             return nb::none();
                         }
                         return nb::int_(en.key.data.photoshop_irb.resource_id);
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
        .def_prop_ro("name",
                     [](const PyEntry& e) -> nb::object {
                         const Entry& en = e.doc->store.entry(e.id);
                         if (en.key.kind == MetaKeyKind::ExifTag) {
                             const std::string ifd
                                 = arena_string(e.doc->store.arena(),
                                                en.key.data.exif_tag.ifd);
                             const std::string_view n
                                 = exif_tag_name(ifd, en.key.data.exif_tag.tag);
                             if (n.empty()) {
                                 return nb::none();
                             }
                             return nb::str(n.data(), n.size());
                         }
                         if (en.key.kind == MetaKeyKind::GeotiffKey) {
                             const std::string_view n = geotiff_key_name(
                                 en.key.data.geotiff_key.key_id);
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
                         if (en.key.kind == MetaKeyKind::BmffField) {
                             const std::string s
                                 = arena_string(e.doc->store.arena(),
                                                en.key.data.bmff_field.field);
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
           uint64_t max_file_bytes, nb::object policy_obj) {
            OpenMetaResourcePolicy policy;
            const OpenMetaResourcePolicy* policy_ptr = nullptr;
            if (!policy_obj.is_none()) {
                policy     = nb::cast<OpenMetaResourcePolicy>(policy_obj);
                policy_ptr = &policy;
            }
            return read_document(path, include_pointer_tags, decode_makernote,
                                 decompress, include_xmp_sidecar,
                                 max_file_bytes, policy_ptr);
        },
        "path"_a, "include_pointer_tags"_a = true,
        "decode_makernote"_a = false, "decompress"_a = true,
        "include_xmp_sidecar"_a = false, "max_file_bytes"_a = 0ULL,
        "policy"_a = nb::none());

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
