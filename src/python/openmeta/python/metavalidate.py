#!/usr/bin/env python3

from __future__ import annotations

import argparse
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


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="metavalidate.py")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--no-build-info", action="store_true", help="hide OpenMeta build info header")
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

    if not args.no_build_info:
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

    for path in args.files:
        total_files += 1
        print(f"== {path}")

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
            print(f"result=fail errors={err_count} warnings={warn_count}")
            total_failed += 1
        elif warn_count != 0:
            print(f"result=warn errors={err_count} warnings={warn_count}")
        else:
            print("result=ok errors=0 warnings=0")

        total_errors += err_count
        total_warnings += warn_count

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
