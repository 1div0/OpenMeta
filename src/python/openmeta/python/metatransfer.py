#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import sys

try:
    import openmeta
except ModuleNotFoundError:
    sys.stderr.write("error: Python module 'openmeta' was not found.\n")
    sys.stderr.write(
        "Run this script with the same Python environment where the OpenMeta wheel is installed.\n"
    )
    sys.stderr.write("\n")
    sys.stderr.write("Examples:\n")
    sys.stderr.write("  uv run python -m openmeta.python.metatransfer <file>\n")
    sys.stderr.write(
        "  /path/to/venv/bin/python -m openmeta.python.metatransfer <file>\n"
    )
    raise SystemExit(2)


def _parse_status_maps() -> tuple[
    dict[object, str], dict[object, str], dict[object, str], dict[object, str], dict[object, str]
]:
    scan = {
        openmeta.ScanStatus.Ok: "ok",
        openmeta.ScanStatus.OutputTruncated: "output_truncated",
        openmeta.ScanStatus.Unsupported: "unsupported",
        openmeta.ScanStatus.Malformed: "malformed",
    }
    payload = {
        openmeta.PayloadStatus.Ok: "ok",
        openmeta.PayloadStatus.OutputTruncated: "output_truncated",
        openmeta.PayloadStatus.Unsupported: "unsupported",
        openmeta.PayloadStatus.Malformed: "malformed",
        openmeta.PayloadStatus.LimitExceeded: "limit_exceeded",
    }
    exif = {
        openmeta.ExifDecodeStatus.Ok: "ok",
        openmeta.ExifDecodeStatus.OutputTruncated: "output_truncated",
        openmeta.ExifDecodeStatus.Unsupported: "unsupported",
        openmeta.ExifDecodeStatus.Malformed: "malformed",
        openmeta.ExifDecodeStatus.LimitExceeded: "limit_exceeded",
    }
    xmp = {
        openmeta.XmpDecodeStatus.Ok: "ok",
        openmeta.XmpDecodeStatus.OutputTruncated: "output_truncated",
        openmeta.XmpDecodeStatus.Unsupported: "unsupported",
        openmeta.XmpDecodeStatus.Malformed: "malformed",
        openmeta.XmpDecodeStatus.LimitExceeded: "limit_exceeded",
    }
    jumbf = {
        openmeta.JumbfDecodeStatus.Ok: "ok",
        openmeta.JumbfDecodeStatus.Unsupported: "unsupported",
        openmeta.JumbfDecodeStatus.Malformed: "malformed",
        openmeta.JumbfDecodeStatus.LimitExceeded: "limit_exceeded",
    }
    return scan, payload, exif, xmp, jumbf


def _transfer_kind_name(kind: object) -> str:
    mapping = {
        openmeta.TransferBlockKind.Exif: "exif",
        openmeta.TransferBlockKind.Xmp: "xmp",
        openmeta.TransferBlockKind.IptcIim: "iptc_iim",
        openmeta.TransferBlockKind.PhotoshopIrb: "photoshop_irb",
        openmeta.TransferBlockKind.Icc: "icc",
        openmeta.TransferBlockKind.Jumbf: "jumbf",
        openmeta.TransferBlockKind.C2pa: "c2pa",
        openmeta.TransferBlockKind.ExrAttribute: "exr_attribute",
        openmeta.TransferBlockKind.Other: "other",
    }
    return mapping.get(kind, "unknown")


def _marker_name(marker: int) -> str:
    if 0xE0 <= marker <= 0xEF:
        return f"APP{marker - 0xE0}(0x{marker:02X})"
    if marker == 0xFE:
        return "COM(0xFE)"
    return f"0x{marker:02X}"


def _sanitize_filename(s: str) -> str:
    out = []
    for ch in s:
        ok = ch.isalnum() or ch in "._-"
        out.append(ch if ok else "_")
    return "".join(out) or "file"


def _payload_dump_path(src_path: str, out_dir: str, route: str, index: int) -> str:
    base = _sanitize_filename(os.path.basename(src_path))
    rr = _sanitize_filename(route)
    name = f"{base}.meta.{index:03d}.{rr}.bin"
    if out_dir:
        return os.path.join(out_dir, name)
    return name


def _parse_time_patch_specs(specs: list[str]) -> dict[str, str]:
    out: dict[str, str] = {}
    for spec in specs:
        if "=" not in spec:
            raise ValueError(f"invalid --time-patch '{spec}' (expected Field=Value)")
        field, value = spec.split("=", 1)
        field = field.strip()
        if not field:
            raise ValueError(f"invalid --time-patch '{spec}' (empty field)")
        out[field] = value
    return out


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="metatransfer.py")
    ap.add_argument("files", nargs="*")
    ap.add_argument("-i", "--input", action="append", default=[], help="input file (repeatable)")
    ap.add_argument("--no-build-info", action="store_true", help="hide OpenMeta build info header")
    ap.add_argument("--format", choices=["portable", "lossless"], default="portable", help="XMP mode for transfer-prepared APP1 XMP")
    ap.add_argument("--portable", action="store_true", help="alias for --format portable")
    ap.add_argument("--lossless", action="store_true", help="alias for --format lossless")
    ap.add_argument("--xmp-include-existing", action="store_true", help="include existing decoded XMP in generated transfer XMP")
    ap.add_argument("--xmp-exiftool-gpsdatetime-alias", action="store_true", help="emit exif:GPSDateTime alias for GPS time in portable mode")
    ap.add_argument("--no-exif", action="store_true", help="skip EXIF APP1 preparation")
    ap.add_argument("--no-xmp", action="store_true", help="skip XMP APP1 preparation")
    ap.add_argument("--no-icc", action="store_true", help="skip ICC APP2 preparation")
    ap.add_argument("--no-iptc", action="store_true", help="skip IPTC APP13 preparation")
    ap.add_argument("--makernotes", action="store_true", help="enable best-effort MakerNote decode in read phase")
    ap.add_argument("--no-decompress", action="store_true", help="disable payload decompression in read phase")
    ap.add_argument(
        "--unsafe-write-payloads",
        action="store_true",
        help="write prepared raw block payload bytes to files",
    )
    ap.add_argument(
        "--write-payloads",
        action="store_true",
        help="deprecated alias for --unsafe-write-payloads",
    )
    ap.add_argument(
        "--out-dir",
        type=str,
        default="",
        help="output directory for --unsafe-write-payloads",
    )
    ap.add_argument("--target-jpeg", type=str, default="", help="target JPEG stream for edit/apply")
    ap.add_argument("--target-tiff", type=str, default="", help="target TIFF stream for edit/apply")
    ap.add_argument("-o", "--output", type=str, default="", help="write edited output file")
    ap.add_argument("--dry-run", action="store_true", help="plan edit only; do not write output")
    ap.add_argument("--force", action="store_true", help="overwrite existing payload files")
    ap.add_argument(
        "--time-patch",
        action="append",
        default=[],
        help="time patch update Field=Value (repeatable)",
    )
    ap.add_argument(
        "--time-patch-lax-width",
        action="store_true",
        help="disable strict patch width checks",
    )
    ap.add_argument(
        "--time-patch-require-slot",
        action="store_true",
        help="fail if requested patch field is not present",
    )
    ap.add_argument(
        "--time-patch-no-auto-nul",
        action="store_true",
        help="disable auto NUL append for text patch values",
    )
    ap.add_argument("--max-file-bytes", type=int, default=0, help="optional mapped file cap in bytes (0=unlimited)")
    ap.add_argument("--max-payload-bytes", type=int, default=0, help="max reassembled/decompressed payload bytes")
    ap.add_argument("--max-payload-parts", type=int, default=0, help="max payload part count")
    ap.add_argument("--max-exif-ifds", type=int, default=0, help="max EXIF/TIFF IFD count")
    ap.add_argument("--max-exif-entries", type=int, default=0, help="max EXIF/TIFF entries per IFD")
    ap.add_argument("--max-exif-total", type=int, default=0, help="max EXIF/TIFF total entries")
    args = ap.parse_args(argv)

    input_paths = list(args.input) + list(args.files)
    if not input_paths:
        ap.print_help(sys.stderr)
        return 2
    if args.target_jpeg and args.target_tiff:
        ap.error("--target-jpeg and --target-tiff are mutually exclusive")
    if args.output and not (args.target_jpeg or args.target_tiff):
        ap.error("--output requires --target-jpeg or --target-tiff")
    if (args.target_jpeg or args.target_tiff) and len(input_paths) != 1:
        ap.error("edit mode supports exactly one source input")

    if args.portable:
        args.format = "portable"
    if args.lossless:
        args.format = "lossless"
    write_payloads = bool(args.unsafe_write_payloads or args.write_payloads)
    try:
        time_patches = _parse_time_patch_specs(list(args.time_patch))
    except ValueError as ex:
        ap.error(str(ex))

    if not args.no_build_info:
        l1, l2 = openmeta.info_lines()
        print(l1)
        print(l2)
        print(openmeta.python_info_line())

    scan_map, payload_map, exif_map, xmp_map, jumbf_map = _parse_status_maps()

    policy = openmeta.ResourcePolicy()
    policy.max_file_bytes = int(args.max_file_bytes)
    if args.max_payload_bytes > 0:
        policy.payload_limits.max_output_bytes = int(args.max_payload_bytes)
    if args.max_payload_parts > 0:
        policy.payload_limits.max_parts = int(args.max_payload_parts)
    if args.max_exif_ifds > 0:
        policy.exif_limits.max_ifds = int(args.max_exif_ifds)
    if args.max_exif_entries > 0:
        policy.exif_limits.max_entries_per_ifd = int(args.max_exif_entries)
    if args.max_exif_total > 0:
        policy.exif_limits.max_total_entries = int(args.max_exif_total)

    sidecar_format = (
        openmeta.XmpSidecarFormat.Portable
        if args.format == "portable"
        else openmeta.XmpSidecarFormat.Lossless
    )

    rc = 0
    target_format = (
        openmeta.TransferTargetFormat.Tiff
        if args.target_tiff
        else openmeta.TransferTargetFormat.Jpeg
    )
    target_path = args.target_tiff if args.target_tiff else args.target_jpeg
    edit_requested = bool(target_path)
    edit_apply = bool(edit_requested and not args.dry_run)
    need_edited_bytes = bool(edit_apply and args.output)

    for path in input_paths:
        probe_fn = (
            openmeta.unsafe_transfer_probe
            if (write_payloads or need_edited_bytes)
            else openmeta.transfer_probe
        )
        probe = probe_fn(
            path,
            target_format=target_format,
            format=sidecar_format,
            include_pointer_tags=True,
            decode_makernote=bool(args.makernotes),
            decode_embedded_containers=True,
            decompress=not args.no_decompress,
            include_exif_app1=not args.no_exif,
            include_xmp_app1=not args.no_xmp,
            include_icc_app2=not args.no_icc,
            include_iptc_app13=not args.no_iptc,
            xmp_include_existing=bool(args.xmp_include_existing),
            xmp_exiftool_gpsdatetime_alias=bool(args.xmp_exiftool_gpsdatetime_alias),
            max_file_bytes=int(args.max_file_bytes),
            policy=policy,
            include_payloads=write_payloads,
            time_patches=time_patches if time_patches else None,
            time_patch_strict_width=not bool(args.time_patch_lax_width),
            time_patch_require_slot=bool(args.time_patch_require_slot),
            time_patch_auto_nul=not bool(args.time_patch_no_auto_nul),
            edit_target_path=(target_path if edit_requested else None),
            edit_apply=edit_apply,
            include_edited_bytes=need_edited_bytes,
        )

        print(f"== {path}")
        print(
            f"  file_status={probe['file_status_name']} size={probe['file_size']} entries={probe['entry_count']}"
        )
        print(
            "  read: "
            f"scan={scan_map.get(probe['scan_status'], 'unknown')} "
            f"payload={payload_map.get(probe['payload_status'], 'unknown')} "
            f"exif={exif_map.get(probe['exif_status'], 'unknown')} "
            f"xmp={xmp_map.get(probe['xmp_status'], 'unknown')} "
            f"jumbf={jumbf_map.get(probe['jumbf_status'], 'unknown')}"
        )
        print(
            f"  prepare: status={probe['prepare_status_name']} blocks={len(probe['blocks'])} "
            f"warnings={probe['prepare_warnings']} errors={probe['prepare_errors']}"
        )
        print(
            f"  time_patch: status={probe['time_patch_status_name']} patched={probe['time_patch_patched_slots']} "
            f"skipped={probe['time_patch_skipped_slots']} errors={probe['time_patch_errors']}"
        )
        print(
            f"  overall: status={probe['overall_status_name']} stage={probe['error_stage']} code={probe['error_code']}"
        )
        if probe["prepare_message"]:
            print(f"  prepare_message={probe['prepare_message']}")
        if probe["time_patch_message"]:
            print(f"  time_patch_message={probe['time_patch_message']}")
        if probe["error_message"]:
            print(f"  error_message={probe['error_message']}")

        if probe["file_status"] != openmeta.TransferFileStatus.Ok:
            rc = 1
            continue
        if probe["overall_status"] != openmeta.TransferStatus.Ok:
            rc = 1
            continue

        for block in probe["blocks"]:
            idx = int(block["index"])
            route = str(block["route"])
            size = int(block["size"])
            print(
                f"  [{idx}] route={route} kind={_transfer_kind_name(block['kind'])} size={size}"
            )
            if not write_payloads:
                continue
            out_path = _payload_dump_path(path, args.out_dir, route, idx)
            if os.path.exists(out_path) and not args.force:
                sys.stderr.write(f"  [{idx}] exists: {out_path} (use --force)\n")
                rc = 1
                continue
            payload = block["payload"]
            if payload is None:
                sys.stderr.write(
                    f"  [{idx}] payload_missing (internal probe payload disabled)\n"
                )
                rc = 1
                continue
            os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
            with open(out_path, "wb") as f:
                f.write(payload)
            print(f"  [{idx}] wrote={out_path}")

        print(
            f"  compile: status={probe['compile_status_name']} code={probe['compile_code_name']} "
            f"ops={int(probe['compile_ops'])}"
        )
        if probe["compile_message"]:
            print(f"  compile_message={probe['compile_message']}")

        print(
            f"  emit: status={probe['emit_status_name']} emitted={probe['emit_emitted']} "
            f"skipped={probe['emit_skipped']} errors={probe['emit_errors']}"
        )
        if probe["emit_message"]:
            print(f"  emit_message={probe['emit_message']}")

        if edit_requested:
            print(
                f"  edit_plan: status={probe['edit_plan_status_name']} input={int(probe['edit_input_size'])} "
                f"output={int(probe['edit_output_size'])}"
            )
            if probe["edit_plan_message"]:
                print(f"  edit_plan_message={probe['edit_plan_message']}")
            print(
                f"  edit_apply: status={probe['edit_apply_status_name']} code={probe['edit_apply_code_name']} "
                f"emitted={int(probe['edit_apply_emitted'])} skipped={int(probe['edit_apply_skipped'])} "
                f"errors={int(probe['edit_apply_errors'])}"
            )
            if probe["edit_apply_message"]:
                print(f"  edit_apply_message={probe['edit_apply_message']}")

            if need_edited_bytes:
                if os.path.exists(args.output) and not args.force:
                    sys.stderr.write(f"  output exists: {args.output} (use --force)\n")
                    rc = 1
                    continue
                edited = probe["edited_bytes"]
                if edited is None:
                    sys.stderr.write("  edited_bytes missing from probe result\n")
                    rc = 1
                    continue
                os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
                with open(args.output, "wb") as f:
                    f.write(edited)
                print(f"  output={args.output} bytes={len(edited)}")

        if probe["overall_status"] != openmeta.TransferStatus.Ok:
            rc = 1
            continue
        for m in probe["marker_summary"]:
            marker = int(m["marker"])
            print(
                f"  marker {_marker_name(marker)} count={int(m['count'])} bytes={int(m['bytes'])}"
            )
        for t in probe["tiff_tag_summary"]:
            print(
                f"  tiff_tag {int(t['tag'])} count={int(t['count'])} bytes={int(t['bytes'])}"
            )
        if probe["tiff_commit"]:
            print("  tiff_commit=true")

    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
