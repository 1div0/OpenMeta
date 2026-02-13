#include "openmeta/build_info.h"
#include "openmeta/mapped_file.h"
#include "openmeta/preview_extract.h"
#include "openmeta/resource_policy.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static void usage(const char* argv0)
    {
        std::printf(
            "Usage: %s [options] <file> [file...]\n"
            "       %s [options] <source> <destination>\n"
            "\n"
            "Extracts embedded thumbnail/preview images discovered in metadata blocks.\n"
            "\n"
            "Options:\n"
            "  --help                 Show this help\n"
            "  --version              Print OpenMeta build info\n"
            "  --no-build-info        Hide build info header\n"
            "  -i, --input <path>     Input file (repeatable)\n"
            "  -o, --out <path>       Output file path (single input only;\n"
            "                         auto-suffixed as _N for multiple previews)\n"
            "  --out-dir <dir>        Output directory (default: alongside input)\n"
            "  --force                Overwrite existing files\n"
            "  --first-only           Export only the first candidate per file\n"
            "  --require-jpeg-soi     Keep only candidates starting with JPEG SOI (FFD8)\n"
            "  --max-file-bytes N     Optional file mapping cap in bytes (default: 0=unlimited)\n"
            "  --max-preview-ifds N   Max preview scan IFD count\n"
            "  --max-preview-total N  Max preview scan total entries\n"
            "  --max-preview-bytes N  Refuse preview candidates larger than N bytes\n"
            "                         (default: 134217728)\n"
            "  --max-candidates N     Max candidates written per file (default: 32)\n",
            argv0 ? argv0 : "thumdump", argv0 ? argv0 : "thumdump");
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


    static void print_build_info_header()
    {
        std::string line1;
        std::string line2;
        format_build_info_lines(&line1, &line2);
        std::printf("%s\n%s\n", line1.c_str(), line2.c_str());
    }


    static const char* mapped_file_status_name(MappedFileStatus status) noexcept
    {
        switch (status) {
        case MappedFileStatus::Ok: return "ok";
        case MappedFileStatus::OpenFailed: return "open_failed";
        case MappedFileStatus::StatFailed: return "stat_failed";
        case MappedFileStatus::TooLarge: return "too_large";
        case MappedFileStatus::MapFailed: return "map_failed";
        }
        return "unknown";
    }


    static const char* preview_kind_name(PreviewKind kind) noexcept
    {
        switch (kind) {
        case PreviewKind::ExifJpegInterchange: return "exif_jpeg_interchange";
        case PreviewKind::ExifJpgFromRaw: return "exif_jpg_from_raw";
        case PreviewKind::ExifJpgFromRaw2: return "exif_jpg_from_raw2";
        }
        return "unknown";
    }


    static const char*
    preview_scan_status_name(PreviewScanStatus status) noexcept
    {
        switch (status) {
        case PreviewScanStatus::Ok: return "ok";
        case PreviewScanStatus::OutputTruncated: return "output_truncated";
        case PreviewScanStatus::Unsupported: return "unsupported";
        case PreviewScanStatus::Malformed: return "malformed";
        case PreviewScanStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
    }


    static const char*
    preview_extract_status_name(PreviewExtractStatus status) noexcept
    {
        switch (status) {
        case PreviewExtractStatus::Ok: return "ok";
        case PreviewExtractStatus::OutputTruncated: return "output_truncated";
        case PreviewExtractStatus::Malformed: return "malformed";
        case PreviewExtractStatus::LimitExceeded: return "limit_exceeded";
        }
        return "unknown";
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


    static std::string basename_only(const std::string& path)
    {
        const size_t sep = path.find_last_of("/\\");
        if (sep == std::string::npos) {
            return path;
        }
        return path.substr(sep + 1);
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


    static std::string build_output_path(const char* input_path,
                                         const std::string& out_dir,
                                         uint32_t idx, bool is_jpeg)
    {
        const std::string ext = is_jpeg ? ".jpg" : ".bin";
        const std::string num = format_index(idx);
        if (out_dir.empty()) {
            std::string out = input_path ? std::string(input_path) : "file";
            out.append(".thumb.");
            out.append(num);
            out.append(ext);
            return out;
        }

        std::string base = input_path ? basename_only(input_path)
                                      : std::string("file");
        base             = sanitize_filename(base);
        std::string out  = base;
        out.append(".thumb.");
        out.append(num);
        out.append(ext);
        return join_path(out_dir, out);
    }

    static std::string with_index_suffix(const std::string& path,
                                         uint32_t one_based_index)
    {
        const size_t sep   = path.find_last_of("/\\");
        const size_t dot   = path.find_last_of('.');
        const bool has_ext = (dot != std::string::npos)
                             && (sep == std::string::npos || dot > sep);

        const std::string suffix = "_" + std::to_string(one_based_index);
        if (!has_ext) {
            return path + suffix;
        }

        std::string out;
        out.reserve(path.size() + suffix.size());
        out.append(path.data(), dot);
        out.append(suffix);
        out.append(path.data() + dot, path.size() - dot);
        return out;
    }

    static bool looks_like_output_path(std::string_view path) noexcept
    {
        if (path.find('/') != std::string_view::npos
            || path.find('\\') != std::string_view::npos) {
            return true;
        }
        if (path.size() >= 4) {
            const auto tail4 = path.substr(path.size() - 4);
            if (tail4 == ".jpg" || tail4 == ".JPG" || tail4 == ".bin"
                || tail4 == ".BIN") {
                return true;
            }
        }
        if (path.size() >= 5) {
            const auto tail5 = path.substr(path.size() - 5);
            if (tail5 == ".jpeg" || tail5 == ".JPEG") {
                return true;
            }
        }
        return false;
    }

}  // namespace
}  // namespace openmeta


int
main(int argc, char** argv)
{
    using namespace openmeta;

    bool show_build_info  = true;
    bool force            = false;
    bool first_only       = false;
    bool require_jpeg_soi = false;
    std::string out_path;
    std::string out_dir;
    std::vector<std::string> explicit_inputs;
    OpenMetaResourcePolicy policy;
    policy.max_file_bytes      = 0;
    uint64_t max_file_bytes    = policy.max_file_bytes;
    uint64_t max_preview_bytes = policy.preview_scan_limits.max_preview_bytes;
    uint32_t max_preview_ifds  = policy.preview_scan_limits.max_ifds;
    uint32_t max_preview_total = policy.preview_scan_limits.max_total_entries;
    uint32_t max_candidates    = 32U;

    int first_path = 1;
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
            first_path += 1;
            continue;
        }
        if ((std::strcmp(arg, "-i") == 0 || std::strcmp(arg, "--input") == 0)
            && i + 1 < argc) {
            explicit_inputs.emplace_back(argv[i + 1]);
            i += 1;
            first_path += 2;
            continue;
        }
        if ((std::strcmp(arg, "-o") == 0 || std::strcmp(arg, "--out") == 0)
            && i + 1 < argc) {
            out_path = argv[i + 1];
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--out-dir") == 0 && i + 1 < argc) {
            out_dir = argv[i + 1];
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--force") == 0) {
            force = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--first-only") == 0) {
            first_only = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--require-jpeg-soi") == 0) {
            require_jpeg_soi = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-file-bytes") == 0 && i + 1 < argc) {
            if (!parse_u64_arg(argv[i + 1], &max_file_bytes)) {
                std::fprintf(stderr, "invalid --max-file-bytes value\n");
                return 2;
            }
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-preview-ifds") == 0 && i + 1 < argc) {
            if (!parse_u32_arg(argv[i + 1], &max_preview_ifds)
                || max_preview_ifds == 0U) {
                std::fprintf(stderr, "invalid --max-preview-ifds value\n");
                return 2;
            }
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-preview-total") == 0 && i + 1 < argc) {
            if (!parse_u32_arg(argv[i + 1], &max_preview_total)
                || max_preview_total == 0U) {
                std::fprintf(stderr, "invalid --max-preview-total value\n");
                return 2;
            }
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-preview-bytes") == 0 && i + 1 < argc) {
            if (!parse_u64_arg(argv[i + 1], &max_preview_bytes)
                || max_preview_bytes == 0U) {
                std::fprintf(stderr, "invalid --max-preview-bytes value\n");
                return 2;
            }
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-candidates") == 0 && i + 1 < argc) {
            if (!parse_u32_arg(argv[i + 1], &max_candidates)
                || max_candidates == 0U) {
                std::fprintf(stderr, "invalid --max-candidates value\n");
                return 2;
            }
            i += 1;
            first_path += 2;
            continue;
        }
        break;
    }

    std::vector<std::string> input_paths = explicit_inputs;
    for (int i = first_path; i < argc; ++i) {
        if (argv[i] && argv[i][0] != '\0') {
            input_paths.emplace_back(argv[i]);
        }
    }

    if (input_paths.empty()) {
        usage(argv[0]);
        return 2;
    }

    if (input_paths.size() == 2U && out_path.empty() && out_dir.empty()
        && explicit_inputs.empty() && looks_like_output_path(input_paths[1])) {
        out_path = input_paths[1];
        input_paths.resize(1U);
    }

    if (!out_path.empty() && input_paths.size() != 1U) {
        std::fprintf(stderr,
                     "thumdump: --out requires exactly one input file\n");
        return 2;
    }

    if (show_build_info) {
        print_build_info_header();
    }

    bool any_failed = false;
    for (size_t i = 0; i < input_paths.size(); ++i) {
        const char* path = input_paths[i].c_str();
        if (!path || !*path) {
            continue;
        }

        MappedFile mapped;
        const MappedFileStatus st = mapped.open(path, max_file_bytes);
        if (st != MappedFileStatus::Ok) {
            std::fprintf(stderr, "thumdump: %s: %s\n", path,
                         mapped_file_status_name(st));
            any_failed = true;
            continue;
        }

        std::vector<ContainerBlockRef> blocks(4096U);
        std::vector<PreviewCandidate> previews(max_candidates);
        PreviewScanOptions scan_options;
        scan_options.require_jpeg_soi         = require_jpeg_soi;
        scan_options.limits.max_ifds          = max_preview_ifds;
        scan_options.limits.max_total_entries = max_preview_total;
        scan_options.limits.max_preview_bytes = max_preview_bytes;
        const PreviewScanResult scan          = scan_preview_candidates(
            mapped.bytes(),
            std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
            std::span<PreviewCandidate>(previews.data(), previews.size()),
            scan_options);

        if (scan.status == PreviewScanStatus::Unsupported) {
            std::printf("== %s\n  previews=none (unsupported)\n", path);
            continue;
        }
        if (scan.status == PreviewScanStatus::Malformed
            || scan.status == PreviewScanStatus::LimitExceeded) {
            std::fprintf(stderr, "thumdump: %s: preview_scan=%s needed=%u\n",
                         path, preview_scan_status_name(scan.status),
                         scan.needed);
            any_failed = true;
            continue;
        }

        const uint32_t avail = (scan.written < previews.size())
                                   ? scan.written
                                   : static_cast<uint32_t>(previews.size());
        std::printf("== %s\n", path);
        std::printf("  preview_scan=%s written=%u needed=%u\n",
                    preview_scan_status_name(scan.status), scan.written,
                    scan.needed);
        if (avail == 0U) {
            std::printf("  exported=0\n");
            continue;
        }

        uint32_t exported = 0U;
        for (uint32_t pi = 0; pi < avail; ++pi) {
            const PreviewCandidate& candidate = previews[pi];
            if (candidate.size
                > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
                std::fprintf(stderr, "  [%u] skip: candidate too large\n", pi);
                any_failed = true;
                continue;
            }

            std::vector<std::byte> out(static_cast<size_t>(candidate.size));
            PreviewExtractOptions extract_options;
            extract_options.max_output_bytes     = max_preview_bytes;
            extract_options.require_jpeg_soi     = require_jpeg_soi;
            const PreviewExtractResult extracted = extract_preview_candidate(
                mapped.bytes(), candidate,
                std::span<std::byte>(out.data(), out.size()), extract_options);
            if (extracted.status != PreviewExtractStatus::Ok) {
                std::fprintf(stderr, "  [%u] kind=%s extract=%s needed=%llu\n",
                             pi, preview_kind_name(candidate.kind),
                             preview_extract_status_name(extracted.status),
                             static_cast<unsigned long long>(extracted.needed));
                any_failed = true;
                continue;
            }

            const std::string out_file
                = out_path.empty()
                      ? build_output_path(path, out_dir, pi,
                                          candidate.has_jpeg_soi_signature)
                      : ((avail > 1U && !first_only)
                             ? with_index_suffix(out_path, pi + 1U)
                             : out_path);
            if (!force && file_exists(out_file)) {
                std::fprintf(stderr, "  [%u] exists: %s (use --force)\n", pi,
                             out_file.c_str());
                any_failed = true;
                continue;
            }
            if (!write_file_bytes(out_file,
                                  std::span<const std::byte>(
                                      out.data(), static_cast<size_t>(
                                                      extracted.written)))) {
                std::fprintf(stderr, "  [%u] write failed: %s\n", pi,
                             out_file.c_str());
                any_failed = true;
                continue;
            }

            std::printf("  [%u] kind=%s block=%u size=%llu soi=%u -> %s\n", pi,
                        preview_kind_name(candidate.kind),
                        candidate.block_index,
                        static_cast<unsigned long long>(candidate.size),
                        candidate.has_jpeg_soi_signature ? 1U : 0U,
                        out_file.c_str());
            exported += 1U;
            if (first_only) {
                break;
            }
        }
        std::printf("  exported=%u\n", exported);
    }

    return any_failed ? 1 : 0;
}
