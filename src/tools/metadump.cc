#include "openmeta/build_info.h"
#include "openmeta/mapped_file.h"
#include "openmeta/simple_meta.h"
#include "openmeta/xmp_decode.h"
#include "openmeta/xmp_dump.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    static void usage(const char* argv0)
    {
        std::printf(
            "Usage: %s [options] <file> [file...]\n"
            "\n"
            "Writes an OpenMeta-generated XMP sidecar next to the input file.\n"
            "\n"
            "Options:\n"
            "  --help                 Show this help\n"
            "  --version              Print OpenMeta build info\n"
            "  --no-build-info         Hide build info header\n"
            "  --format <lossless|portable>\n"
            "                         XMP output format (default: lossless)\n"
            "  --portable             Alias for --format portable\n"
            "  --portable-no-exif     Portable mode: skip EXIF/TIFF/GPS mapped fields\n"
            "  --portable-include-existing-xmp\n"
            "                         Portable mode: include decoded standard XMP properties\n"
            "  --out <path>            Output path (single input only)\n"
            "  --out-dir <dir>         Output directory (for multiple inputs)\n"
            "  --force                 Overwrite existing output files\n"
            "  --xmp-sidecar           Also read sidecar XMP (<file>.xmp, <basename>.xmp)\n"
            "  --no-pointer-tags       Do not store pointer tags\n"
            "  --makernotes            Attempt MakerNote decode (best-effort)\n"
            "  --no-decompress         Do not decompress payloads\n"
            "  --max-file-bytes N      Refuse to read files larger than N bytes (0=unlimited)\n"
            "  --max-output-bytes N    Refuse to generate dumps larger than N bytes (0=unlimited)\n"
            "  --max-entries N         Refuse to emit more than N entries (0=unlimited)\n",
            argv0 ? argv0 : "metadump");
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


    static bool read_file_bytes(const char* path, std::vector<std::byte>* out,
                                uint64_t max_bytes)
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (!path || !*path) {
            return false;
        }

        std::FILE* f = std::fopen(path, "rb");
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
        if (max_bytes != 0U && static_cast<uint64_t>(end) > max_bytes) {
            std::fclose(f);
            return false;
        }
        if (std::fseek(f, 0, SEEK_SET) != 0) {
            std::fclose(f);
            return false;
        }

        out->resize(static_cast<size_t>(end));
        if (end > 0) {
            const size_t n = std::fread(out->data(), 1, out->size(), f);
            if (n != out->size()) {
                std::fclose(f);
                out->clear();
                return false;
            }
        }
        std::fclose(f);
        return true;
    }


    static void xmp_sidecar_candidates(const char* path, std::string* out_a,
                                       std::string* out_b)
    {
        if (out_a) {
            out_a->clear();
        }
        if (out_b) {
            out_b->clear();
        }
        if (!path || !*path || !out_a || !out_b) {
            return;
        }

        const std::string s(path);
        *out_b = s + ".xmp";

        const size_t sep = s.find_last_of("/\\");
        const size_t dot = s.find_last_of('.');
        if (dot != std::string::npos
            && (sep == std::string::npos || dot > sep)) {
            *out_a = s.substr(0, dot) + ".xmp";
        } else {
            *out_a = *out_b;
        }
        if (*out_a == *out_b) {
            out_b->clear();
        }
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

    static std::string default_out_path_for(const char* in_path,
                                            const std::string& out_dir)
    {
        if (!in_path) {
            return {};
        }
        const std::string in(in_path);
        const std::string base     = basename_only(in);
        const std::string out_name = base + ".xmp";
        if (out_dir.empty()) {
            return in + ".xmp";
        }
        return join_path(out_dir, out_name);
    }


    static void print_build_info_header()
    {
        std::string line1;
        std::string line2;
        format_build_info_lines(&line1, &line2);
        std::printf("%s\n%s\n", line1.c_str(), line2.c_str());
    }

}  // namespace
}  // namespace openmeta


int
main(int argc, char** argv)
{
    using namespace openmeta;

    bool show_build_info               = true;
    bool xmp_sidecar                   = false;
    bool force_overwrite               = false;
    XmpSidecarFormat format            = XmpSidecarFormat::Lossless;
    bool portable_include_exif         = true;
    bool portable_include_existing_xmp = false;
    std::string out_path;
    std::string out_dir;

    ExifDecodeOptions exif_options;
    PayloadOptions payload_options;
    payload_options.decompress = true;

    uint64_t max_file_bytes   = 512ULL * 1024ULL * 1024ULL;
    uint64_t max_output_bytes = 0;
    uint32_t max_entries      = 0;

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
        if (std::strcmp(arg, "--portable") == 0) {
            format = XmpSidecarFormat::Portable;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--portable-no-exif") == 0) {
            portable_include_exif = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--portable-include-existing-xmp") == 0) {
            portable_include_existing_xmp = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--format") == 0 && i + 1 < argc) {
            const char* v = argv[i + 1];
            if (!v) {
                std::fprintf(stderr, "invalid --format value\n");
                return 2;
            }
            if (std::strcmp(v, "lossless") == 0) {
                format = XmpSidecarFormat::Lossless;
            } else if (std::strcmp(v, "portable") == 0) {
                format = XmpSidecarFormat::Portable;
            } else {
                std::fprintf(
                    stderr,
                    "invalid --format value (expected lossless|portable)\n");
                return 2;
            }
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--xmp-sidecar") == 0) {
            xmp_sidecar = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--force") == 0) {
            force_overwrite = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--out") == 0 && i + 1 < argc) {
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
        if (std::strcmp(arg, "--no-pointer-tags") == 0) {
            exif_options.include_pointer_tags = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--makernotes") == 0) {
            exif_options.decode_makernote = true;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--no-decompress") == 0) {
            payload_options.decompress = false;
            first_path += 1;
            continue;
        }
        if (std::strcmp(arg, "--max-file-bytes") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (!parse_u64_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --max-file-bytes value\n");
                return 2;
            }
            max_file_bytes = v;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-output-bytes") == 0 && i + 1 < argc) {
            uint64_t v = 0;
            if (!parse_u64_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --max-output-bytes value\n");
                return 2;
            }
            max_output_bytes = v;
            i += 1;
            first_path += 2;
            continue;
        }
        if (std::strcmp(arg, "--max-entries") == 0 && i + 1 < argc) {
            uint32_t v = 0;
            if (!parse_u32_arg(argv[i + 1], &v)) {
                std::fprintf(stderr, "invalid --max-entries value\n");
                return 2;
            }
            max_entries = v;
            i += 1;
            first_path += 2;
            continue;
        }
        break;
    }

    if (argc <= first_path) {
        usage(argv[0]);
        return 2;
    }

    const int file_count = argc - first_path;
    if (!out_path.empty() && file_count != 1) {
        std::fprintf(stderr,
                     "metadump: --out requires exactly one input file\n");
        return 2;
    }

    if (show_build_info) {
        print_build_info_header();
    }

    int exit_code = 0;
    for (int argi = first_path; argi < argc; ++argi) {
        const char* path = argv[argi];
        if (!path || !*path) {
            continue;
        }

        const std::string out = out_path.empty()
                                    ? default_out_path_for(path, out_dir)
                                    : out_path;

        if (!force_overwrite && file_exists(out)) {
            std::fprintf(stderr,
                         "metadump: refusing to overwrite `%s` (use --force)\n",
                         out.c_str());
            exit_code = 1;
            continue;
        }

        MappedFile file;
        const MappedFileStatus st = file.open(path, max_file_bytes);
        if (st != MappedFileStatus::Ok) {
            std::fprintf(stderr, "metadump: failed to read `%s`\n", path);
            exit_code = 1;
            continue;
        }

        std::vector<ContainerBlockRef> blocks(128);
        std::vector<ExifIfdRef> ifd_refs(256);
        std::vector<std::byte> payload(1024 * 1024);
        std::vector<uint32_t> payload_parts(16384);

        MetaStore store;
        SimpleMetaResult read;
        for (;;) {
            store = MetaStore();
            read  = simple_meta_read(
                file.bytes(), store,
                std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
                std::span<ExifIfdRef>(ifd_refs.data(), ifd_refs.size()),
                std::span<std::byte>(payload.data(), payload.size()),
                std::span<uint32_t>(payload_parts.data(), payload_parts.size()),
                exif_options, payload_options);

            if (read.scan.status == ScanStatus::OutputTruncated
                && read.scan.needed > blocks.size()) {
                blocks.resize(read.scan.needed);
                continue;
            }
            if (read.payload.status == PayloadStatus::OutputTruncated
                && read.payload.needed > payload.size()) {
                payload.resize(static_cast<size_t>(read.payload.needed));
                continue;
            }
            break;
        }

        if (xmp_sidecar) {
            std::string sidecar_a;
            std::string sidecar_b;
            xmp_sidecar_candidates(path, &sidecar_a, &sidecar_b);
            const char* candidates[2]
                = { sidecar_a.c_str(),
                    sidecar_b.empty() ? nullptr : sidecar_b.c_str() };
            for (size_t i = 0; i < 2; ++i) {
                const char* sp = candidates[i];
                if (!sp || !*sp) {
                    continue;
                }
                std::vector<std::byte> xmp_bytes;
                if (!read_file_bytes(sp, &xmp_bytes, max_file_bytes)) {
                    continue;
                }
                (void)decode_xmp_packet(xmp_bytes, store);
            }
        }

        store.finalize();

        XmpSidecarOptions dump_opts;
        dump_opts.format                           = format;
        dump_opts.initial_output_bytes             = 1024ULL * 1024ULL;
        dump_opts.portable.limits.max_output_bytes = max_output_bytes;
        dump_opts.portable.limits.max_entries      = max_entries;
        dump_opts.portable.include_exif            = portable_include_exif;
        dump_opts.portable.include_existing_xmp = portable_include_existing_xmp;
        dump_opts.lossless.limits.max_output_bytes = max_output_bytes;
        dump_opts.lossless.limits.max_entries      = max_entries;

        std::vector<std::byte> out_buf;
        const XmpDumpResult dump_res = dump_xmp_sidecar(store, &out_buf,
                                                        dump_opts);

        if (dump_res.status != XmpDumpStatus::Ok) {
            std::fprintf(stderr, "metadump: dump failed for `%s` (status=%s)\n",
                         path,
                         (dump_res.status == XmpDumpStatus::LimitExceeded)
                             ? "limit_exceeded"
                             : "output_truncated");
            exit_code = 1;
            continue;
        }

        if (!write_file_bytes(out, std::span<const std::byte>(out_buf.data(),
                                                              out_buf.size()))) {
            std::fprintf(stderr, "metadump: failed to write `%s`\n",
                         out.c_str());
            exit_code = 1;
            continue;
        }

        std::printf("wrote=%s format=%s bytes=%llu entries=%u\n", out.c_str(),
                    (format == XmpSidecarFormat::Portable) ? "portable"
                                                           : "lossless",
                    static_cast<unsigned long long>(dump_res.written),
                    static_cast<unsigned>(dump_res.entries));
    }

    return exit_code;
}
