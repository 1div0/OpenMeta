#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import sys

try:
    import openmeta
except ModuleNotFoundError:
    sys.stderr.write("error: Python module 'openmeta' was not found.\n")
    sys.stderr.write("Run this script with the same Python environment where the OpenMeta wheel is installed.\n")
    sys.stderr.write("\n")
    sys.stderr.write("Examples:\n")
    sys.stderr.write("  uv run python -m openmeta.python.metavalidate <file>\n")
    sys.stderr.write("  /path/to/venv/bin/python -m openmeta.python.metavalidate <file>\n")
    raise SystemExit(2)


def _snake(name: str) -> str:
    out = []
    for i, ch in enumerate(name):
        if ch.isupper() and i > 0:
            out.append("_")
        out.append(ch.lower())
    return "".join(out)


def _status_name(value: object) -> str:
    return _snake(getattr(value, "name", "unknown"))


def _result_name(result: dict[str, object]) -> str:
    if bool(result["failed"]):
        return "fail"
    if int(result["warning_count"]) != 0:
        return "warn"
    return "ok"


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="metavalidate.py")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--no-build-info", action="store_true", help="hide OpenMeta build info header")
    ap.add_argument("--json", action="store_true", help="print machine-readable JSON output")
    ap.add_argument("--xmp-sidecar", action="store_true", help="also read sidecar XMP (<file>.xmp, <basename>.xmp)")
    ap.add_argument("--no-pointer-tags", action="store_true", help="do not store pointer tags")
    ap.add_argument("--makernotes", action="store_true", help="attempt MakerNote decode (best-effort)")
    ap.add_argument("--no-decompress", action="store_true", help="do not decompress payloads")
    ap.add_argument("--warnings-as-errors", action="store_true", help="fail files that have warnings")
    ap.add_argument("--strict", action="store_true", help="alias for --warnings-as-errors")
    ap.add_argument("--c2pa-verify", action="store_true", help="request draft C2PA verify scaffold evaluation")
    ap.add_argument(
        "--c2pa-verify-backend",
        choices=["none", "auto", "native", "openssl"],
        default="auto",
        help="verification backend preference",
    )
    ap.add_argument(
        "--ccm-validation",
        choices=["none", "dng-warnings"],
        default="dng-warnings",
        help="CCM validation mode",
    )
    ap.add_argument(
        "--ccm-no-require-dng-context",
        action="store_true",
        help="allow CCM query outside explicit DNG context",
    )
    ap.add_argument("--ccm-no-reduction", action="store_true", help="skip ReductionMatrix* fields")
    ap.add_argument("--max-ccm-fields", type=int, default=128, help="max CCM fields to collect")
    ap.add_argument("--max-ccm-values", type=int, default=256, help="max values per CCM field")
    ap.add_argument(
        "--max-file-bytes",
        type=int,
        default=0,
        help="optional file mapping cap in bytes (0=unlimited)",
    )
    args = ap.parse_args(argv)

    warnings_as_errors = bool(args.warnings_as_errors or args.strict)

    if not args.no_build_info and not args.json:
        l1, l2 = openmeta.info_lines()
        print(l1)
        print(l2)
        print(openmeta.python_info_line())

    backend_none = getattr(openmeta.C2paVerifyBackend, "None")
    ccm_mode_none = getattr(openmeta.CcmValidationMode, "None")
    backend_map = {
        "none": backend_none,
        "auto": openmeta.C2paVerifyBackend.Auto,
        "native": openmeta.C2paVerifyBackend.Native,
        "openssl": openmeta.C2paVerifyBackend.OpenSsl,
    }
    ccm_mode_map = {
        "none": ccm_mode_none,
        "dng-warnings": openmeta.CcmValidationMode.DngSpecWarnings,
    }

    total_files = 0
    total_failed = 0
    total_errors = 0
    total_warnings = 0
    json_rows: list[dict[str, object]] = []

    for path in args.files:
        total_files += 1

        result = openmeta.validate(
            path,
            include_pointer_tags=not args.no_pointer_tags,
            decode_makernote=bool(args.makernotes),
            decode_printim=True,
            decompress=not args.no_decompress,
            include_xmp_sidecar=bool(args.xmp_sidecar),
            verify_c2pa=bool(args.c2pa_verify),
            verify_backend=backend_map[args.c2pa_verify_backend],
            warnings_as_errors=bool(warnings_as_errors),
            ccm_require_dng_context=not args.ccm_no_require_dng_context,
            ccm_include_reduction_matrices=not args.ccm_no_reduction,
            ccm_max_fields=int(args.max_ccm_fields),
            ccm_max_values_per_field=int(args.max_ccm_values),
            ccm_validation_mode=ccm_mode_map[args.ccm_validation],
            max_file_bytes=int(args.max_file_bytes),
        )

        if args.json:
            row: dict[str, object] = {
                "path": path,
                "file_size": int(result["file_size"]),
                "status": _status_name(result["status"]),
                "result": _result_name(result),
                "failed": bool(result["failed"]),
                "error_count": int(result["error_count"]),
                "warning_count": int(result["warning_count"]),
                "entries": int(result["entries"]),
                "issues": [],
            }
            if _status_name(result["status"]) == "ok":
                row.update(
                    {
                        "scan_status": _status_name(result["scan_status"]),
                        "payload_status": _status_name(result["payload_status"]),
                        "exif_status": _status_name(result["exif_status"]),
                        "xmp_status": _status_name(result["xmp_status"]),
                        "exr_status": _status_name(result["exr_status"]),
                        "jumbf_status": _status_name(result["jumbf_status"]),
                        "c2pa_verify_status": _status_name(result["jumbf_verify_status"]),
                        "c2pa_verify_backend": _status_name(result["jumbf_verify_backend"]),
                        "ccm_status": _status_name(result["ccm_status"]),
                        "ccm_mode": args.ccm_validation.replace("-", "_"),
                        "ccm_require_dng": not args.ccm_no_require_dng_context,
                        "ccm_include_reduction": not args.ccm_no_reduction,
                        "ccm_fields_found": int(result["ccm_fields_found"]),
                        "ccm_fields_dropped": int(result["ccm_fields_dropped"]),
                        "ccm_issues_reported": int(result["ccm_issues_reported"]),
                    }
                )
            issues: list[dict[str, object]] = []
            for issue in result["issues"]:
                issues.append(
                    {
                        "severity": _status_name(issue["severity"]),
                        "category": str(issue["category"]),
                        "code": str(issue["code"]),
                        "ifd": str(issue["ifd"]),
                        "name": str(issue["name"]),
                        "tag": int(issue["tag"]),
                        "message": str(issue["message"]),
                    }
                )
            row["issues"] = issues
            json_rows.append(row)
        else:
            print(f"== {path}")
            print(f"size={int(result['file_size'])}")
            print(
                "scan={scan} payload={payload} exif={exif} xmp={xmp} exr={exr} jumbf={jumbf} "
                "c2pa_verify={verify} c2pa_backend={backend} entries={entries}".format(
                    scan=_status_name(result["scan_status"]),
                    payload=_status_name(result["payload_status"]),
                    exif=_status_name(result["exif_status"]),
                    xmp=_status_name(result["xmp_status"]),
                    exr=_status_name(result["exr_status"]),
                    jumbf=_status_name(result["jumbf_status"]),
                    verify=_status_name(result["jumbf_verify_status"]),
                    backend=_status_name(result["jumbf_verify_backend"]),
                    entries=int(result["entries"]),
                )
            )
            print(
                "ccm=status={status} mode={mode} require_dng={require_dng} include_reduction={include_red} "
                "fields={fields} dropped={dropped} issues={issues}".format(
                    status=_status_name(result["ccm_status"]),
                    mode=args.ccm_validation.replace("-", "_"),
                    require_dng="off" if args.ccm_no_require_dng_context else "on",
                    include_red="off" if args.ccm_no_reduction else "on",
                    fields=int(result["ccm_fields_found"]),
                    dropped=int(result["ccm_fields_dropped"]),
                    issues=int(result["ccm_issues_reported"]),
                )
            )
            for issue in result["issues"]:
                severity = _status_name(issue["severity"])
                category = str(issue["category"])
                code = str(issue["code"])
                if category == "ccm":
                    print(
                        "  {sev}[ccm] code={code} ifd={ifd} tag=0x{tag:04X} name={name} msg={msg}".format(
                            sev=severity,
                            code=code,
                            ifd=str(issue["ifd"]),
                            tag=int(issue["tag"]),
                            name=str(issue["name"]),
                            msg=str(issue["message"]),
                        )
                    )
                else:
                    print(f"  {severity}[{category}] {code}")

        err_count = int(result["error_count"])
        warn_count = int(result["warning_count"])
        fail = bool(result["failed"])
        if fail:
            total_failed += 1
            if not args.json:
                print(f"result=fail errors={err_count} warnings={warn_count}")
        elif warn_count != 0:
            if not args.json:
                print(f"result=warn errors={err_count} warnings={warn_count}")
        elif not args.json:
            print("result=ok errors=0 warnings=0")

        total_errors += err_count
        total_warnings += warn_count

    if args.json:
        payload: dict[str, object] = {
            "strict": bool(warnings_as_errors),
            "files": json_rows,
            "summary": {
                "files": total_files,
                "failed": total_failed,
                "errors": total_errors,
                "warnings": total_warnings,
                "strict": bool(warnings_as_errors),
            },
        }
        if not args.no_build_info:
            line1, line2 = openmeta.info_lines()
            payload["build_info"] = {
                "line1": line1,
                "line2": line2,
                "python": openmeta.python_info_line(),
            }
        print(json.dumps(payload, ensure_ascii=True))
    else:
        print(
            "summary files={files} failed={failed} errors={errors} warnings={warnings} strict={strict}".format(
                files=total_files,
                failed=total_failed,
                errors=total_errors,
                warnings=total_warnings,
                strict="on" if warnings_as_errors else "off",
            )
        )
    return 1 if total_failed != 0 else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
