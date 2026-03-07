#include "openmeta/build_info.h"
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
            "  --no-decompress        Disable payload decompression in read phase\n"
            "  --unsafe-write-payloads\n"
            "                         Write prepared raw block payload bytes to files\n"
            "  --write-payloads       Deprecated alias for --unsafe-write-payloads\n"
            "  --out-dir <dir>        Output directory for --write-payloads\n"
            "  --source-meta <path>   Metadata source for prepare phase\n"
            "  --target-jpeg <path>   Target JPEG stream for edit/apply phase\n"
            "  --target-tiff <path>   Target TIFF stream for edit/apply phase\n"
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
        if (!s || !*s || !out) {
            return false;
        }
        char* end            = nullptr;
        unsigned long long v = std::strtoull(s, &end, 10);
        if (!end || *end != '\0') {
            return false;
        }
        *out = static_cast<uint64_t>(v);
        return true;
    }


    static bool parse_u32_arg(const char* s, uint32_t* out)
    {
        if (!s || !*s || !out) {
            return false;
        }
        char* end       = nullptr;
        unsigned long v = std::strtoul(s, &end, 10);
        if (!end || *end != '\0') {
            return false;
        }
        *out = static_cast<uint32_t>(v);
        return true;
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

}  // namespace
}  // namespace openmeta


int
main(int argc, char** argv)
{
    using namespace openmeta;

    bool show_build_info     = true;
    bool force               = false;
    bool write_payloads      = false;
    bool dry_run             = false;
    uint32_t emit_repeat     = 1U;
    bool time_patch_auto_nul = true;
    std::string out_dir;
    std::string source_meta_path;
    std::string target_jpeg_path;
    std::string target_tiff_path;
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
    if (input_paths.empty()) {
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
    if (!target_jpeg_path.empty() && !target_tiff_path.empty()) {
        std::fprintf(stderr,
                     "--target-jpeg and --target-tiff are mutually exclusive\n");
        return 2;
    }
    if (!target_tiff_path.empty()) {
        options.prepare.target_format = TransferTargetFormat::Tiff;
    } else {
        options.prepare.target_format = TransferTargetFormat::Jpeg;
    }
    if (options.prepare.target_format == TransferTargetFormat::Tiff
        && (edit_plan_opts.mode != JpegEditMode::Auto
            || edit_plan_opts.require_in_place)) {
        std::fprintf(
            stderr,
            "--mode/--require-in-place are only valid for JPEG targets\n");
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

        ExecutePreparedTransferFileOptions execute_options;
        execute_options.prepare              = options;
        execute_options.execute.time_patches = build_transfer_time_patch_inputs(
            pending_time_patches);
        execute_options.execute.time_patch          = time_patch_opts;
        execute_options.execute.time_patch_auto_nul = time_patch_auto_nul;
        execute_options.execute.emit_repeat         = emit_repeat;
        execute_options.execute.jpeg_edit           = edit_plan_opts;
        execute_options.execute.edit_requested      = need_jpeg_edit
                                                 || need_tiff_edit;
        execute_options.execute.edit_apply = !dry_run && !output_path.empty();
        if (execute_options.execute.edit_requested) {
            execute_options.edit_target_path = target_path;
        }

        const ExecutePreparedTransferFileResult executed
            = execute_prepared_transfer_file(source_path.c_str(),
                                             execute_options);
        const PrepareTransferFileResult& prepared = executed.prepared;
        const ExecutePreparedTransferResult& exec = executed.execute;

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

        if (prepared.file_status != TransferFileStatus::Ok
            || prepared.prepare.status != TransferStatus::Ok) {
            any_failed = true;
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
                        "input=%llu output=%llu scan_end=%llu\n",
                        transfer_status_name(exec.edit_plan_status),
                        jpeg_edit_mode_name(exec.jpeg_edit_plan.requested_mode),
                        jpeg_edit_mode_name(exec.jpeg_edit_plan.selected_mode),
                        exec.jpeg_edit_plan.in_place_possible ? "true"
                                                              : "false",
                        exec.jpeg_edit_plan.emitted_segments,
                        exec.jpeg_edit_plan.replaced_segments,
                        exec.jpeg_edit_plan.appended_segments,
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
                if (!force && file_exists(output_path)) {
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
                    if (!write_file_bytes(output_path,
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
                                    exec.edited_output.size()));
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
                if (!force && file_exists(output_path)) {
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
                    if (!write_file_bytes(output_path,
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
                                    exec.edited_output.size()));
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

        std::fprintf(stderr, "  emit: unsupported bundle target format\n");
        any_failed = true;
    }

    return any_failed ? 1 : 0;
}
