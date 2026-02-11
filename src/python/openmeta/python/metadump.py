#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import sys

try:
    import openmeta
except ModuleNotFoundError:
    sys.stderr.write("error: Python module 'openmeta' was not found.\n")
    sys.stderr.write("Run this script with the same Python environment where the OpenMeta wheel is installed.\n")
    sys.stderr.write("\n")
    sys.stderr.write("Examples:\n")
    sys.stderr.write("  uv run python -m openmeta.python.metadump <file>\n")
    sys.stderr.write("  /path/to/venv/bin/python -m openmeta.python.metadump <file>\n")
    raise SystemExit(2)


def _default_out_path(path: str, out_dir: str) -> str:
    if out_dir:
        base = os.path.basename(path)
        return os.path.join(out_dir, base + ".xmp")
    return path + ".xmp"


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="metadump.py")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--no-build-info", action="store_true", help="hide OpenMeta build info header")
    ap.add_argument("--format", choices=["lossless", "portable"], default="lossless", help="XMP output format")
    ap.add_argument("--portable", action="store_true", help="alias for --format portable")
    ap.add_argument("--portable-no-exif", action="store_true", help="portable mode: skip EXIF/TIFF/GPS mapped fields")
    ap.add_argument(
        "--portable-include-existing-xmp",
        action="store_true",
        help="portable mode: include decoded standard XMP properties",
    )
    ap.add_argument("--out", type=str, default="", help="output path (single input only)")
    ap.add_argument("--out-dir", type=str, default="", help="output directory (multiple inputs)")
    ap.add_argument("--force", action="store_true", help="overwrite existing output files")
    ap.add_argument("--xmp-sidecar", action="store_true", help="also read sidecar XMP (<file>.xmp, <basename>.xmp)")
    ap.add_argument("--no-pointer-tags", action="store_true", help="do not store pointer tags")
    ap.add_argument("--makernotes", action="store_true", help="attempt MakerNote decode (best-effort)")
    ap.add_argument("--no-decompress", action="store_true", help="do not decompress payloads")
    ap.add_argument("--max-file-bytes", type=int, default=512 * 1024 * 1024, help="refuse to read files larger than N bytes (0=unlimited)")
    ap.add_argument("--max-output-bytes", type=int, default=0, help="refuse to generate dumps larger than N bytes (0=unlimited)")
    ap.add_argument("--max-entries", type=int, default=0, help="refuse to emit more than N entries (0=unlimited)")
    args = ap.parse_args(argv)

    if args.portable:
        args.format = "portable"

    if args.out and len(args.files) != 1:
        sys.stderr.write("error: --out requires exactly one input file\n")
        return 2

    if not args.no_build_info:
        l1, l2 = openmeta.info_lines()
        print(l1)
        print(l2)
        print(openmeta.python_info_line())

    rc = 0
    for path in args.files:
        out_path = args.out if args.out else _default_out_path(path, args.out_dir)

        if os.path.exists(out_path) and not args.force:
            sys.stderr.write(f"metadump: refusing to overwrite '{out_path}' (use --force)\n")
            rc = 1
            continue

        doc = openmeta.read(
            path,
            include_pointer_tags=not args.no_pointer_tags,
            decode_makernote=bool(args.makernotes),
            decompress=not args.no_decompress,
            include_xmp_sidecar=bool(args.xmp_sidecar),
            max_file_bytes=int(args.max_file_bytes),
        )

        sidecar_format = (
            openmeta.XmpSidecarFormat.Portable
            if args.format == "portable"
            else openmeta.XmpSidecarFormat.Lossless
        )
        data, res = doc.dump_xmp_sidecar(
            format=sidecar_format,
            max_output_bytes=int(args.max_output_bytes),
            max_entries=int(args.max_entries),
            include_exif=not args.portable_no_exif,
            include_existing_xmp=bool(args.portable_include_existing_xmp),
        )

        try:
            os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
            with open(out_path, "wb") as f:
                f.write(data)
        except OSError as e:
            sys.stderr.write(f"metadump: failed to write '{out_path}': {e}\n")
            rc = 1
            continue

        print(f"wrote={out_path} format={args.format} bytes={len(data)} entries={res.entries}")

    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
