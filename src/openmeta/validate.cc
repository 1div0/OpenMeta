#include "openmeta/validate.h"

#include "openmeta/mapped_file.h"
#include "openmeta/xmp_decode.h"

#include <cstdio>
#include <cstring>
#include <string_view>

namespace openmeta {
namespace {

    enum class ReadFileStatus : uint8_t {
        Ok,
        OpenFailed,
        TooLarge,
        ReadFailed,
    };


    static const char* ccm_issue_code_name(CcmIssueCode code) noexcept
    {
        switch (code) {
        case CcmIssueCode::DecodeFailed: return "decode_failed";
        case CcmIssueCode::NonFiniteValue: return "non_finite_value";
        case CcmIssueCode::UnexpectedCount: return "unexpected_count";
        case CcmIssueCode::MatrixCountNotDivisibleBy3:
            return "matrix_count_not_divisible_by_3";
        case CcmIssueCode::NonPositiveValue: return "non_positive_value";
        case CcmIssueCode::AsShotConflict: return "as_shot_conflict";
        case CcmIssueCode::MissingCompanionTag: return "missing_companion_tag";
        case CcmIssueCode::TripleIlluminantRule:
            return "triple_illuminant_rule";
        case CcmIssueCode::CalibrationSignatureMismatch:
            return "calibration_signature_mismatch";
        case CcmIssueCode::MissingIlluminantData:
            return "missing_illuminant_data";
        case CcmIssueCode::InvalidIlluminantCode:
            return "invalid_illuminant_code";
        case CcmIssueCode::WhiteXYOutOfRange: return "white_xy_out_of_range";
        }
        return "unknown";
    }


    static void add_issue(ValidateResult* out, ValidateIssueSeverity severity,
                          std::string_view category, std::string_view code,
                          std::string_view message, std::string_view ifd = {},
                          std::string_view name = {}, uint16_t tag = 0)
    {
        if (!out) {
            return;
        }
        ValidateIssue issue;
        issue.severity = severity;
        issue.category.assign(category.data(), category.size());
        issue.code.assign(code.data(), code.size());
        issue.ifd.assign(ifd.data(), ifd.size());
        issue.name.assign(name.data(), name.size());
        issue.tag = tag;
        issue.message.assign(message.data(), message.size());
        out->issues.push_back(std::move(issue));
    }


    static void tally_issues(ValidateResult* out) noexcept
    {
        if (!out) {
            return;
        }
        out->warning_count = 0;
        out->error_count   = 0;
        for (size_t i = 0; i < out->issues.size(); ++i) {
            if (out->issues[i].severity == ValidateIssueSeverity::Error) {
                out->error_count += 1U;
            } else {
                out->warning_count += 1U;
            }
        }
    }


    static bool file_size_u64(const char* path, uint64_t* out_size)
    {
        if (!path || !out_size) {
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
        std::fclose(f);
        if (end < 0) {
            return false;
        }
        *out_size = static_cast<uint64_t>(end);
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
        *out_b = s;
        out_b->append(".xmp");

        const size_t sep = s.find_last_of("/\\");
        const size_t dot = s.find_last_of('.');
        if (dot != std::string::npos
            && (sep == std::string::npos || dot > sep)) {
            *out_a = s.substr(0, dot);
            out_a->append(".xmp");
        } else {
            *out_a = *out_b;
        }

        if (*out_a == *out_b) {
            out_b->clear();
        }
    }


    static ReadFileStatus read_file_bytes(const char* path,
                                          std::vector<std::byte>* out,
                                          uint64_t max_bytes,
                                          uint64_t* file_size)
    {
        if (!out) {
            return ReadFileStatus::ReadFailed;
        }
        out->clear();
        if (file_size) {
            *file_size = 0;
        }
        if (!path || !*path) {
            return ReadFileStatus::OpenFailed;
        }

        std::FILE* f = std::fopen(path, "rb");
        if (!f) {
            return ReadFileStatus::OpenFailed;
        }
        if (std::fseek(f, 0, SEEK_END) != 0) {
            std::fclose(f);
            return ReadFileStatus::ReadFailed;
        }
        const long end = std::ftell(f);
        if (end < 0) {
            std::fclose(f);
            return ReadFileStatus::ReadFailed;
        }
        const uint64_t sz = static_cast<uint64_t>(end);
        if (file_size) {
            *file_size = sz;
        }
        if (max_bytes != 0U && sz > max_bytes) {
            std::fclose(f);
            return ReadFileStatus::TooLarge;
        }
        if (std::fseek(f, 0, SEEK_SET) != 0) {
            std::fclose(f);
            return ReadFileStatus::ReadFailed;
        }
        out->resize(static_cast<size_t>(end));
        if (end > 0) {
            const size_t n = std::fread(out->data(), 1, out->size(), f);
            if (n != out->size()) {
                std::fclose(f);
                out->clear();
                return ReadFileStatus::ReadFailed;
            }
        }
        std::fclose(f);
        return ReadFileStatus::Ok;
    }


    static void merge_xmp_status(XmpDecodeStatus* out,
                                 XmpDecodeStatus in) noexcept
    {
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
        }
    }


    static void add_decode_status_issues(ValidateResult* out,
                                         const ValidateOptions& options)
    {
        if (!out) {
            return;
        }

        if (out->read.scan.status == ScanStatus::OutputTruncated) {
            add_issue(out, ValidateIssueSeverity::Warning, "scan",
                      "output_truncated", "output_truncated");
        } else if (out->read.scan.status == ScanStatus::Malformed) {
            add_issue(out, ValidateIssueSeverity::Error, "scan", "malformed",
                      "malformed");
        }

        if (out->read.payload.status == PayloadStatus::OutputTruncated) {
            add_issue(out, ValidateIssueSeverity::Warning, "payload",
                      "output_truncated", "output_truncated");
        } else if (out->read.payload.status == PayloadStatus::Malformed) {
            add_issue(out, ValidateIssueSeverity::Error, "payload", "malformed",
                      "malformed");
        } else if (out->read.payload.status == PayloadStatus::LimitExceeded) {
            add_issue(out, ValidateIssueSeverity::Error, "payload",
                      "limit_exceeded", "limit_exceeded");
        }

        if (out->read.exif.status == ExifDecodeStatus::OutputTruncated) {
            add_issue(out, ValidateIssueSeverity::Warning, "exif",
                      "output_truncated", "output_truncated");
        } else if (out->read.exif.status == ExifDecodeStatus::Malformed) {
            add_issue(out, ValidateIssueSeverity::Error, "exif", "malformed",
                      "malformed");
        } else if (out->read.exif.status == ExifDecodeStatus::LimitExceeded) {
            add_issue(out, ValidateIssueSeverity::Error, "exif",
                      "limit_exceeded", "limit_exceeded");
        }

        if (out->read.xmp.status == XmpDecodeStatus::OutputTruncated) {
            add_issue(out, ValidateIssueSeverity::Warning, "xmp",
                      "output_truncated", "output_truncated");
        } else if (out->read.xmp.status == XmpDecodeStatus::Malformed) {
            add_issue(out, ValidateIssueSeverity::Error, "xmp", "malformed",
                      "malformed");
        } else if (out->read.xmp.status == XmpDecodeStatus::LimitExceeded) {
            add_issue(out, ValidateIssueSeverity::Error, "xmp",
                      "limit_exceeded", "limit_exceeded");
        }

        if (out->read.exr.status == ExrDecodeStatus::Malformed) {
            add_issue(out, ValidateIssueSeverity::Error, "exr", "malformed",
                      "malformed");
        } else if (out->read.exr.status == ExrDecodeStatus::LimitExceeded) {
            add_issue(out, ValidateIssueSeverity::Error, "exr",
                      "limit_exceeded", "limit_exceeded");
        }

        if (out->read.jumbf.status == JumbfDecodeStatus::Malformed) {
            add_issue(out, ValidateIssueSeverity::Error, "jumbf", "malformed",
                      "malformed");
        } else if (out->read.jumbf.status == JumbfDecodeStatus::LimitExceeded) {
            add_issue(out, ValidateIssueSeverity::Error, "jumbf",
                      "limit_exceeded", "limit_exceeded");
        }

        if (options.verify_c2pa) {
            switch (out->read.jumbf.verify_status) {
            case C2paVerifyStatus::InvalidSignature:
                add_issue(out, ValidateIssueSeverity::Error, "c2pa",
                          "invalid_signature", "invalid_signature");
                break;
            case C2paVerifyStatus::VerificationFailed:
                add_issue(out, ValidateIssueSeverity::Error, "c2pa",
                          "verification_failed", "verification_failed");
                break;
            case C2paVerifyStatus::BackendUnavailable:
                add_issue(out, ValidateIssueSeverity::Warning, "c2pa",
                          "backend_unavailable", "backend_unavailable");
                break;
            case C2paVerifyStatus::DisabledByBuild:
                add_issue(out, ValidateIssueSeverity::Warning, "c2pa",
                          "disabled_by_build", "disabled_by_build");
                break;
            case C2paVerifyStatus::NoSignatures:
                add_issue(out, ValidateIssueSeverity::Warning, "c2pa",
                          "no_signatures", "no_signatures");
                break;
            case C2paVerifyStatus::NotImplemented:
                add_issue(out, ValidateIssueSeverity::Warning, "c2pa",
                          "not_implemented", "not_implemented");
                break;
            case C2paVerifyStatus::NotRequested:
            case C2paVerifyStatus::Verified: break;
            }
        }
    }

}  // namespace


ValidateResult
validate_file(const char* path, const ValidateOptions& options) noexcept
{
    ValidateResult out;

    if (!path || !*path) {
        out.status = ValidateStatus::OpenFailed;
        add_issue(&out, ValidateIssueSeverity::Error, "file", "open_failed",
                  "open_failed");
        tally_issues(&out);
        out.failed = true;
        return out;
    }

    MappedFile file;
    const MappedFileStatus map_status
        = file.open(path, options.policy.max_file_bytes);
    if (map_status != MappedFileStatus::Ok) {
        if (map_status == MappedFileStatus::TooLarge) {
            out.status = ValidateStatus::TooLarge;
            (void)file_size_u64(path, &out.file_size);
            add_issue(&out, ValidateIssueSeverity::Error, "file", "too_large",
                      "too_large");
        } else if (map_status == MappedFileStatus::OpenFailed) {
            out.status = ValidateStatus::OpenFailed;
            add_issue(&out, ValidateIssueSeverity::Error, "file", "open_failed",
                      "open_failed");
        } else {
            out.status = ValidateStatus::ReadFailed;
            add_issue(&out, ValidateIssueSeverity::Error, "file", "read_failed",
                      "read_failed");
        }
        tally_issues(&out);
        out.failed = true;
        return out;
    }

    out.status    = ValidateStatus::Ok;
    out.file_size = file.size();

    SimpleMetaDecodeOptions decode_options;
    apply_resource_policy(options.policy, &decode_options.exif,
                          &decode_options.payload);
    apply_resource_policy(options.policy, &decode_options.xmp,
                          &decode_options.exr, &decode_options.jumbf,
                          &decode_options.icc, &decode_options.iptc,
                          &decode_options.photoshop_irb);
    decode_options.xmp.malformed_mode = XmpDecodeMalformedMode::OutputTruncated;
    decode_options.exif.include_pointer_tags = options.include_pointer_tags;
    decode_options.exif.decode_makernote     = options.decode_makernote;
    decode_options.exif.decode_printim       = options.decode_printim;
    decode_options.exif.decode_embedded_containers = true;
    decode_options.payload.decompress              = options.decompress;
    decode_options.jumbf.verify_c2pa               = options.verify_c2pa;
    decode_options.jumbf.verify_backend            = options.verify_backend;

    std::vector<ContainerBlockRef> blocks(128);
    std::vector<ExifIfdRef> ifd_refs(256);
    std::vector<std::byte> payload(1024 * 1024);
    std::vector<uint32_t> payload_parts(16384);

    MetaStore store;
    for (;;) {
        store    = MetaStore();
        out.read = simple_meta_read(
            file.bytes(), store,
            std::span<ContainerBlockRef>(blocks.data(), blocks.size()),
            std::span<ExifIfdRef>(ifd_refs.data(), ifd_refs.size()),
            std::span<std::byte>(payload.data(), payload.size()),
            std::span<uint32_t>(payload_parts.data(), payload_parts.size()),
            decode_options);

        if (out.read.scan.status == ScanStatus::OutputTruncated
            && out.read.scan.needed > blocks.size()) {
            blocks.resize(out.read.scan.needed);
            continue;
        }
        if (out.read.payload.status == PayloadStatus::OutputTruncated
            && out.read.payload.needed > payload.size()) {
            payload.resize(static_cast<size_t>(out.read.payload.needed));
            continue;
        }
        break;
    }

    if (options.include_xmp_sidecar) {
        std::string sidecar_a;
        std::string sidecar_b;
        xmp_sidecar_candidates(path, &sidecar_a, &sidecar_b);

        const char* candidates[2] = { sidecar_a.c_str(),
                                      sidecar_b.empty() ? nullptr
                                                        : sidecar_b.c_str() };
        for (size_t i = 0; i < 2; ++i) {
            const char* sp = candidates[i];
            if (!sp || !*sp) {
                continue;
            }
            std::vector<std::byte> xmp_bytes;
            uint64_t sidecar_size = 0;
            const ReadFileStatus st
                = read_file_bytes(sp, &xmp_bytes, options.policy.max_file_bytes,
                                  &sidecar_size);
            if (st == ReadFileStatus::OpenFailed) {
                continue;
            }
            if (st == ReadFileStatus::TooLarge) {
                add_issue(&out, ValidateIssueSeverity::Error, "xmp_sidecar",
                          "too_large", "too_large");
                continue;
            }
            if (st != ReadFileStatus::Ok) {
                add_issue(&out, ValidateIssueSeverity::Error, "xmp_sidecar",
                          "read_failed", "read_failed");
                continue;
            }

            const XmpDecodeResult one = decode_xmp_packet(xmp_bytes, store,
                                                          EntryFlags::None,
                                                          decode_options.xmp);
            merge_xmp_status(&out.read.xmp.status, one.status);
            out.read.xmp.entries_decoded += one.entries_decoded;
        }
    }

    store.finalize();
    out.entries = static_cast<uint32_t>(store.entries().size());

    add_decode_status_issues(&out, options);

    std::vector<CcmField> ccm_fields;
    std::vector<CcmIssue> ccm_issues;
    out.ccm        = collect_dng_ccm_fields(store, &ccm_fields, options.ccm,
                                            &ccm_issues);
    out.ccm_fields = static_cast<uint32_t>(ccm_fields.size());

    if (out.ccm.status == CcmQueryStatus::LimitExceeded) {
        add_issue(&out, ValidateIssueSeverity::Warning, "ccm", "limit_exceeded",
                  "limit_exceeded");
    }

    for (size_t i = 0; i < ccm_issues.size(); ++i) {
        const CcmIssue& issue = ccm_issues[i];
        add_issue(&out,
                  issue.severity == CcmIssueSeverity::Error
                      ? ValidateIssueSeverity::Error
                      : ValidateIssueSeverity::Warning,
                  "ccm", ccm_issue_code_name(issue.code), issue.message,
                  issue.ifd, issue.name, issue.tag);
    }

    tally_issues(&out);
    out.failed = (out.error_count != 0U)
                 || (options.warnings_as_errors && out.warning_count != 0U);
    return out;
}

}  // namespace openmeta
