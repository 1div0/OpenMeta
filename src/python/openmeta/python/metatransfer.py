#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

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


def _transfer_policy_action(name: str) -> object:
    mapping = {
        "keep": openmeta.TransferPolicyAction.Keep,
        "drop": openmeta.TransferPolicyAction.Drop,
        "invalidate": openmeta.TransferPolicyAction.Invalidate,
        "rewrite": openmeta.TransferPolicyAction.Rewrite,
    }
    return mapping[name]


def _marker_name(marker: int) -> str:
    if 0xE0 <= marker <= 0xEF:
        return f"APP{marker - 0xE0}(0x{marker:02X})"
    if marker == 0xFE:
        return "COM(0xFE)"
    return f"0x{marker:02X}"


def _bmff_item_name(item_type: int, mime_xmp: bool) -> str:
    if mime_xmp:
        return "mime/xmp"
    return "".join(chr((item_type >> shift) & 0xFF) for shift in (24, 16, 8, 0))


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
    ap.add_argument("--makernote-policy", choices=["keep", "drop", "invalidate", "rewrite"], default="keep", help="transfer policy for MakerNote payloads")
    ap.add_argument("--jumbf-policy", choices=["keep", "drop", "invalidate", "rewrite"], default="keep", help="transfer policy for JUMBF payloads")
    ap.add_argument("--c2pa-policy", choices=["keep", "drop", "invalidate", "rewrite"], default="keep", help="transfer policy for C2PA payloads")
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
    ap.add_argument("--target-jxl", action="store_true", help="target JPEG XL metadata emit summary")
    ap.add_argument("--target-webp", action="store_true", help="target WebP metadata chunk emit summary")
    ap.add_argument("--target-heif", action="store_true", help="target HEIF metadata item emit summary")
    ap.add_argument("--target-avif", action="store_true", help="target AVIF metadata item emit summary")
    ap.add_argument("--target-cr3", action="store_true", help="target CR3 metadata item emit summary")
    ap.add_argument("--target-jpeg", type=str, default="", help="target JPEG stream for edit/apply")
    ap.add_argument("--target-tiff", type=str, default="", help="target TIFF stream for edit/apply")
    ap.add_argument("--jpeg-c2pa-signed", type=str, default="", help="externally signed logical C2PA payload for JPEG staging")
    ap.add_argument("--c2pa-manifest-output", type=str, default="", help="external manifest-builder output bytes for signed C2PA staging")
    ap.add_argument("--c2pa-certificate-chain", type=str, default="", help="external certificate-chain bytes for signed C2PA staging")
    ap.add_argument("--c2pa-key-ref", type=str, default="", help="private-key reference string for signed C2PA staging")
    ap.add_argument("--c2pa-signing-time", type=str, default="", help="signing time for signed C2PA staging")
    ap.add_argument("--dump-c2pa-binding", type=str, default="", help="write exact C2PA content-binding bytes for external signing")
    ap.add_argument("--dump-c2pa-handoff", type=str, default="", help="write one persisted C2PA handoff package")
    ap.add_argument("--dump-c2pa-signed-package", type=str, default="", help="write one persisted signed C2PA package")
    ap.add_argument("--load-c2pa-signed-package", type=str, default="", help="load one persisted signed C2PA package for staging")
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
    target_count = (
        int(bool(args.target_jpeg))
        + int(bool(args.target_tiff))
        + int(bool(args.target_jxl))
        + int(bool(args.target_webp))
        + int(bool(args.target_heif))
        + int(bool(args.target_avif))
        + int(bool(args.target_cr3))
    )
    if target_count > 1:
        ap.error("--target-jpeg, --target-tiff, --target-jxl, --target-webp, --target-heif, --target-avif, and --target-cr3 are mutually exclusive")
    if (
        args.jpeg_c2pa_signed
        or args.c2pa_manifest_output
        or args.c2pa_certificate_chain
        or args.c2pa_key_ref
        or args.c2pa_signing_time
    ) and (
        args.target_tiff
        or args.target_jxl
        or args.target_webp
        or args.target_heif
        or args.target_avif
        or args.target_cr3
    ):
        ap.error("signed C2PA staging is only supported for JPEG targets")
    if args.output and not (args.target_jpeg or args.target_tiff):
        if args.target_jxl:
            ap.error("--output is not supported for JXL targets yet")
        if args.target_webp:
            ap.error("--output is not supported for WebP targets yet")
        if args.target_heif or args.target_avif or args.target_cr3:
            ap.error("--output is not supported for BMFF targets yet")
        ap.error("--output requires --target-jpeg or --target-tiff")
    if args.dump_c2pa_binding and (
        args.target_tiff
        or args.target_jxl
        or args.target_webp
        or args.target_heif
        or args.target_avif
        or args.target_cr3
    ):
        ap.error("--dump-c2pa-binding is only supported for JPEG targets")
    if (
        args.dump_c2pa_handoff
        or args.dump_c2pa_signed_package
        or args.load_c2pa_signed_package
    ) and (
        args.target_tiff
        or args.target_jxl
        or args.target_webp
        or args.target_heif
        or args.target_avif
        or args.target_cr3
    ):
        ap.error("C2PA package options are only supported for JPEG targets")
    if (args.target_jpeg or args.target_tiff) and len(input_paths) != 1:
        ap.error("edit mode supports exactly one source input")
    if args.dump_c2pa_binding and len(input_paths) != 1:
        ap.error("--dump-c2pa-binding supports exactly one input path per run")
    if (
        args.dump_c2pa_handoff
        or args.dump_c2pa_signed_package
        or args.load_c2pa_signed_package
    ) and len(input_paths) != 1:
        ap.error("C2PA package options support exactly one input path per run")
    if args.load_c2pa_signed_package and (
        args.jpeg_c2pa_signed
        or args.c2pa_manifest_output
        or args.c2pa_certificate_chain
        or args.c2pa_key_ref
        or args.c2pa_signing_time
    ):
        ap.error(
            "--load-c2pa-signed-package is mutually exclusive with individual signed C2PA inputs"
        )

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
    makernote_policy = _transfer_policy_action(args.makernote_policy)
    jumbf_policy = _transfer_policy_action(args.jumbf_policy)
    c2pa_policy = _transfer_policy_action(args.c2pa_policy)

    rc = 0
    if args.target_tiff:
        target_format = openmeta.TransferTargetFormat.Tiff
    elif args.target_jxl:
        target_format = openmeta.TransferTargetFormat.Jxl
    elif args.target_webp:
        target_format = openmeta.TransferTargetFormat.Webp
    elif args.target_heif:
        target_format = openmeta.TransferTargetFormat.Heif
    elif args.target_avif:
        target_format = openmeta.TransferTargetFormat.Avif
    elif args.target_cr3:
        target_format = openmeta.TransferTargetFormat.Cr3
    else:
        target_format = openmeta.TransferTargetFormat.Jpeg
    target_path = args.target_tiff if args.target_tiff else args.target_jpeg
    edit_requested = bool(target_path)
    edit_apply = bool(edit_requested and not args.dry_run)
    need_edited_bytes = bool(edit_apply and args.output)
    need_c2pa_binding_bytes = bool(args.dump_c2pa_binding)
    need_c2pa_handoff_bytes = bool(args.dump_c2pa_handoff)
    need_c2pa_signed_package_bytes = bool(args.dump_c2pa_signed_package)
    c2pa_signed_payload = (
        Path(args.jpeg_c2pa_signed).read_bytes() if args.jpeg_c2pa_signed else None
    )
    c2pa_manifest_output = (
        Path(args.c2pa_manifest_output).read_bytes()
        if args.c2pa_manifest_output
        else None
    )
    c2pa_certificate_chain = (
        Path(args.c2pa_certificate_chain).read_bytes()
        if args.c2pa_certificate_chain
        else None
    )
    c2pa_signed_package = (
        Path(args.load_c2pa_signed_package).read_bytes()
        if args.load_c2pa_signed_package
        else None
    )

    for path in input_paths:
        probe_fn = (
            openmeta.unsafe_transfer_probe
            if (
                write_payloads
                or need_edited_bytes
                or need_c2pa_binding_bytes
                or need_c2pa_handoff_bytes
                or need_c2pa_signed_package_bytes
            )
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
            makernote_policy=makernote_policy,
            jumbf_policy=jumbf_policy,
            c2pa_policy=c2pa_policy,
            max_file_bytes=int(args.max_file_bytes),
            policy=policy,
            c2pa_signed_package=c2pa_signed_package,
            c2pa_signed_logical_payload=c2pa_signed_payload,
            c2pa_certificate_chain=c2pa_certificate_chain,
            c2pa_private_key_reference=(args.c2pa_key_ref or None),
            c2pa_signing_time=(args.c2pa_signing_time or None),
            c2pa_manifest_builder_output=c2pa_manifest_output,
            include_payloads=write_payloads,
            time_patches=time_patches if time_patches else None,
            time_patch_strict_width=not bool(args.time_patch_lax_width),
            time_patch_require_slot=bool(args.time_patch_require_slot),
            time_patch_auto_nul=not bool(args.time_patch_no_auto_nul),
            edit_target_path=(target_path if edit_requested else None),
            edit_apply=edit_apply,
            include_edited_bytes=need_edited_bytes,
            include_c2pa_binding_bytes=need_c2pa_binding_bytes,
            include_c2pa_handoff_bytes=need_c2pa_handoff_bytes,
            include_c2pa_signed_package_bytes=need_c2pa_signed_package_bytes,
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
        for decision in probe["policy_decisions"]:
            mode_text = ""
            if str(decision["subject_name"]) == "c2pa":
                mode_text = (
                    f" mode={decision['c2pa_mode_name']}"
                    f" source={decision['c2pa_source_kind_name']}"
                    f" output={decision['c2pa_prepared_output_name']}"
                )
            print(
                "  policy[{subject}]: requested={requested} effective={effective} "
                "reason={reason}{mode} matched={matched}".format(
                    subject=str(decision["subject_name"]),
                    requested=str(decision["requested_name"]),
                    effective=str(decision["effective_name"]),
                    reason=str(decision["reason_name"]),
                    mode=mode_text,
                    matched=int(decision["matched_entries"]),
                )
            )
            if decision["message"]:
                print(
                    f"  policy[{decision['subject_name']}]_message={decision['message']}"
                )
        rewrite = probe["c2pa_rewrite"]
        if (
            str(rewrite["state_name"]) != "not_applicable"
            or int(rewrite["matched_entries"]) > 0
        ):
            print(
                f"  c2pa_rewrite: state={rewrite['state_name']} "
                f"target={rewrite['target_format_name']} "
                f"source={rewrite['source_kind_name']} "
                f"matched={int(rewrite['matched_entries'])} "
                f"existing_segments={int(rewrite['existing_carrier_segments'])} "
                f"carrier_available={'yes' if rewrite['target_carrier_available'] else 'no'} "
                f"invalidates_existing={'yes' if rewrite['content_change_invalidates_existing'] else 'no'}"
            )
            if (
                rewrite["requires_manifest_builder"]
                or rewrite["requires_content_binding"]
                or rewrite["requires_certificate_chain"]
                or rewrite["requires_private_key"]
                or rewrite["requires_signing_time"]
            ):
                print(
                    "  c2pa_rewrite_requirements: "
                    f"manifest_builder={'yes' if rewrite['requires_manifest_builder'] else 'no'} "
                    f"content_binding={'yes' if rewrite['requires_content_binding'] else 'no'} "
                    f"certificate_chain={'yes' if rewrite['requires_certificate_chain'] else 'no'} "
                    f"private_key={'yes' if rewrite['requires_private_key'] else 'no'} "
                    f"signing_time={'yes' if rewrite['requires_signing_time'] else 'no'}"
                )
            if rewrite["message"]:
                print(f"  c2pa_rewrite_message={rewrite['message']}")
            if int(rewrite["content_binding_bytes"]) > 0:
                print(
                    f"  c2pa_rewrite_binding: chunks={len(rewrite['content_binding_chunks'])} "
                    f"bytes={int(rewrite['content_binding_bytes'])}"
                )
                for chunk in rewrite["content_binding_chunks"]:
                    if str(chunk["kind_name"]) == "source_range":
                        print(
                            f"  c2pa_rewrite_chunk[{int(chunk['index'])}]: "
                            f"kind={chunk['kind_name']} "
                            f"offset={int(chunk['source_offset'])} "
                            f"size={int(chunk['size'])}"
                        )
                    else:
                        print(
                            f"  c2pa_rewrite_chunk[{int(chunk['index'])}]: "
                            f"kind={chunk['kind_name']} "
                            f"block={int(chunk['block_index'])} "
                            f"marker=0x{int(chunk['jpeg_marker_code']):02X} "
                            f"size={int(chunk['size'])} "
                            f"route={chunk['route']}"
                        )
        sign_request = probe["c2pa_sign_request"]
        if (
            str(sign_request["rewrite_state_name"]) != "not_applicable"
            or int(sign_request["content_binding_bytes"]) > 0
        ):
            print(
                f"  c2pa_sign_request: status={sign_request['status_name']} "
                f"carrier={sign_request['carrier_route'] or '-'} "
                f"manifest_label={sign_request['manifest_label'] or '-'} "
                f"source_ranges={int(sign_request['source_range_chunks'])} "
                f"prepared_segments={int(sign_request['prepared_segment_chunks'])} "
                f"bytes={int(sign_request['content_binding_bytes'])}"
            )
        if sign_request["message"]:
            print(f"  c2pa_sign_request_message={sign_request['message']}")
        if probe["c2pa_binding_requested"]:
            print(
                f"  c2pa_binding: status={probe['c2pa_binding_status_name']} "
                f"code={probe['c2pa_binding_code_name']} "
                f"bytes={int(probe['c2pa_binding_bytes_written'])} "
                f"errors={int(probe['c2pa_binding_errors'])}"
            )
            if probe["c2pa_binding_message"]:
                print(f"  c2pa_binding_message={probe['c2pa_binding_message']}")
        if probe["c2pa_handoff_requested"]:
            print(
                f"  c2pa_handoff: status={probe['c2pa_handoff_status_name']} "
                f"code={probe['c2pa_handoff_code_name']} "
                f"bytes={int(probe['c2pa_handoff_bytes_written'])} "
                f"errors={int(probe['c2pa_handoff_errors'])}"
            )
            if probe["c2pa_handoff_message"]:
                print(f"  c2pa_handoff_message={probe['c2pa_handoff_message']}")
        if probe["c2pa_signed_package_requested"]:
            print(
                f"  c2pa_signed_package: status={probe['c2pa_signed_package_status_name']} "
                f"code={probe['c2pa_signed_package_code_name']} "
                f"bytes={int(probe['c2pa_signed_package_bytes_written'])} "
                f"errors={int(probe['c2pa_signed_package_errors'])}"
            )
            if probe["c2pa_signed_package_message"]:
                print(
                    f"  c2pa_signed_package_message={probe['c2pa_signed_package_message']}"
                )
        if probe["c2pa_stage_requested"]:
            print(
                f"  c2pa_stage_validate: status={probe['c2pa_stage_validation_status_name']} "
                f"code={probe['c2pa_stage_validation_code_name']} "
                f"kind={probe['c2pa_stage_validation_payload_kind_name']} "
                f"payload_bytes={int(probe['c2pa_stage_validation_logical_payload_bytes'])} "
                f"carrier_bytes={int(probe['c2pa_stage_validation_staged_payload_bytes'])} "
                f"segments={int(probe['c2pa_stage_validation_staged_segments'])} "
                f"errors={int(probe['c2pa_stage_validation_errors'])}"
            )
            print(
                f"  c2pa_stage_semantics: status={probe['c2pa_stage_validation_semantic_status_name']} "
                f"reason={probe['c2pa_stage_validation_semantic_reason']} "
                f"manifest={int(probe['c2pa_stage_validation_semantic_manifest_present'])} "
                f"manifests={int(probe['c2pa_stage_validation_semantic_manifest_count'])} "
                f"claim_generator={int(probe['c2pa_stage_validation_semantic_claim_generator_present'])} "
                f"assertions={int(probe['c2pa_stage_validation_semantic_assertion_count'])} "
                f"claims={int(probe['c2pa_stage_validation_semantic_claim_count'])} "
                f"signatures={int(probe['c2pa_stage_validation_semantic_signature_count'])} "
                f"linked={int(probe['c2pa_stage_validation_semantic_signature_linked'])} "
                f"orphan={int(probe['c2pa_stage_validation_semantic_signature_orphan'])} "
                f"explicit_refs={int(probe['c2pa_stage_validation_semantic_explicit_reference_signature_count'])} "
                f"unresolved={int(probe['c2pa_stage_validation_semantic_explicit_reference_unresolved_signature_count'])} "
                f"ambiguous={int(probe['c2pa_stage_validation_semantic_explicit_reference_ambiguous_signature_count'])}"
            )
            print(
                f"  c2pa_stage_linkage: claim0_assertions={int(probe['c2pa_stage_validation_semantic_primary_claim_assertion_count'])} "
                f"claim0_refs={int(probe['c2pa_stage_validation_semantic_primary_claim_referenced_by_signature_count'])} "
                f"sig0_links={int(probe['c2pa_stage_validation_semantic_primary_signature_linked_claim_count'])}"
            )
            print(
                f"  c2pa_stage_references: sig0_keys={int(probe['c2pa_stage_validation_semantic_primary_signature_reference_key_hits'])} "
                f"sig0_present={int(probe['c2pa_stage_validation_semantic_primary_signature_explicit_reference_present'])} "
                f"sig0_resolved={int(probe['c2pa_stage_validation_semantic_primary_signature_explicit_reference_resolved_claim_count'])}"
            )
            if probe["c2pa_stage_validation_message"]:
                print(
                    f"  c2pa_stage_validate_message={probe['c2pa_stage_validation_message']}"
                )
            print(
                f"  c2pa_stage: status={probe['c2pa_stage_status_name']} "
                f"code={probe['c2pa_stage_code_name']} "
                f"emitted={int(probe['c2pa_stage_emitted'])} "
                f"removed={int(probe['c2pa_stage_skipped'])} "
                f"errors={int(probe['c2pa_stage_errors'])}"
            )
            if probe["c2pa_stage_message"]:
                print(f"  c2pa_stage_message={probe['c2pa_stage_message']}")
        if probe["time_patch_message"]:
            print(f"  time_patch_message={probe['time_patch_message']}")
        if probe["error_message"]:
            print(f"  error_message={probe['error_message']}")

        if probe["file_status"] != openmeta.TransferFileStatus.Ok:
            rc = 1
            continue

        if args.dump_c2pa_binding:
            if os.path.exists(args.dump_c2pa_binding) and not args.force:
                sys.stderr.write(
                    f"  c2pa binding output exists: {args.dump_c2pa_binding} (use --force)\n"
                )
                rc = 1
                continue
            binding = probe["c2pa_binding_bytes"]
            if binding is None:
                sys.stderr.write("  c2pa_binding_bytes missing from probe result\n")
                rc = 1
                continue
            os.makedirs(os.path.dirname(args.dump_c2pa_binding) or ".", exist_ok=True)
            with open(args.dump_c2pa_binding, "wb") as f:
                f.write(binding)
            print(
                f"  c2pa_binding_output={args.dump_c2pa_binding} bytes={len(binding)}"
            )
        if args.dump_c2pa_handoff:
            if os.path.exists(args.dump_c2pa_handoff) and not args.force:
                sys.stderr.write(
                    f"  c2pa handoff output exists: {args.dump_c2pa_handoff} (use --force)\n"
                )
                rc = 1
                continue
            handoff = probe["c2pa_handoff_bytes"]
            if handoff is None:
                sys.stderr.write("  c2pa_handoff_bytes missing from probe result\n")
                rc = 1
                continue
            os.makedirs(os.path.dirname(args.dump_c2pa_handoff) or ".", exist_ok=True)
            with open(args.dump_c2pa_handoff, "wb") as f:
                f.write(handoff)
            print(
                f"  c2pa_handoff_output={args.dump_c2pa_handoff} bytes={len(handoff)}"
            )
        if args.dump_c2pa_signed_package:
            if os.path.exists(args.dump_c2pa_signed_package) and not args.force:
                sys.stderr.write(
                    f"  signed c2pa package output exists: {args.dump_c2pa_signed_package} (use --force)\n"
                )
                rc = 1
                continue
            signed_package = probe["c2pa_signed_package_bytes"]
            if signed_package is None:
                sys.stderr.write(
                    "  c2pa_signed_package_bytes missing from probe result\n"
                )
                rc = 1
                continue
            os.makedirs(
                os.path.dirname(args.dump_c2pa_signed_package) or ".", exist_ok=True
            )
            with open(args.dump_c2pa_signed_package, "wb") as f:
                f.write(signed_package)
            print(
                f"  c2pa_signed_package_output={args.dump_c2pa_signed_package} bytes={len(signed_package)}"
            )

        allow_output_only_success = (
            not edit_requested
            and not write_payloads
            and (
                (args.dump_c2pa_binding and probe["c2pa_binding_status"] == openmeta.TransferStatus.Ok)
                or (args.dump_c2pa_handoff and probe["c2pa_handoff_status"] == openmeta.TransferStatus.Ok)
                or (
                    args.dump_c2pa_signed_package
                    and probe["c2pa_signed_package_status"] == openmeta.TransferStatus.Ok
                )
            )
        )

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

        if (
            probe["overall_status"] != openmeta.TransferStatus.Ok
            and not allow_output_only_success
        ):
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
        for b in probe["jxl_box_summary"]:
            print(
                f"  jxl_box {str(b['type'])} count={int(b['count'])} bytes={int(b['bytes'])}"
            )
        for c in probe["webp_chunk_summary"]:
            print(
                f"  webp_chunk {str(c['type'])} count={int(c['count'])} bytes={int(c['bytes'])}"
            )
        for item in probe["bmff_item_summary"]:
            print(
                "  bmff_item "
                f"{_bmff_item_name(int(item['item_type']), bool(item['mime_xmp']))} "
                f"count={int(item['count'])} bytes={int(item['bytes'])}"
            )
        if probe["tiff_commit"]:
            print("  tiff_commit=true")

    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
