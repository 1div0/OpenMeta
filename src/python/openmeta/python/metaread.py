#!/usr/bin/env python3

from __future__ import annotations

import argparse
import math
import re
import sys
from collections import defaultdict
from typing import Iterable, Tuple

try:
    import openmeta
except ModuleNotFoundError:
    sys.stderr.write("error: Python module 'openmeta' was not found.\n")
    sys.stderr.write("Run this script with the same Python environment where the OpenMeta wheel is installed.\n")
    sys.stderr.write("\n")
    sys.stderr.write("Examples:\n")
    sys.stderr.write("  uv run python -m openmeta.python.metaread <file>\n")
    sys.stderr.write("  /path/to/venv/bin/python -m openmeta.python.metaread <file>\n")
    raise SystemExit(2)


def _snake(name: str) -> str:
    return re.sub(r"(?<!^)([A-Z])", r"_\1", name).lower()


def _tiff_type_name(code: int) -> str:
    return {
        1: "BYTE",
        2: "ASCII",
        3: "SHORT",
        4: "LONG",
        5: "RATIONAL",
        6: "SBYTE",
        7: "UNDEFINED",
        8: "SSHORT",
        9: "SLONG",
        10: "SRATIONAL",
        11: "FLOAT",
        12: "DOUBLE",
        13: "IFD",
        16: "LONG8",
        17: "SLONG8",
        18: "IFD8",
        129: "UTF8",
    }.get(code, "UNKNOWN")


def _fmt_float(x: float) -> str:
    if math.isnan(x):
        return "nan"
    if math.isinf(x):
        return "inf" if x > 0 else "-inf"
    return f"{x:.6f}".rstrip("0").rstrip(".")


def _rational_to_decimal(n: int, d: int) -> str:
    if d == 0:
        return "inf"
    try:
        return _fmt_float(float(n) / float(d))
    except Exception:
        return "nan"


def _truncate_cell(s: str, max_chars: int) -> str:
    if max_chars != 0 and len(s) > max_chars:
        return s[: max(0, max_chars - 3)] + "..."
    return s


def _val_type(e: openmeta.Entry) -> str:
    k = e.value_kind
    if k == openmeta.MetaValueKind.Empty:
        return "-"
    if k == openmeta.MetaValueKind.Scalar:
        t = e.elem_type
        if t in (openmeta.MetaElementType.URational, openmeta.MetaElementType.SRational):
            return t.name.lower()
        if t in (openmeta.MetaElementType.F32, openmeta.MetaElementType.F64):
            return "f"
        if t.name.startswith("U"):
            return "u"
        if t.name.startswith("I"):
            return "i"
        return t.name.lower()
    if k == openmeta.MetaValueKind.Array:
        return f"array[{e.count}]"
    if k == openmeta.MetaValueKind.Text:
        return f"text[{e.count}]"
    if k == openmeta.MetaValueKind.Bytes:
        return f"bytes[{e.count}]"
    return k.name.lower()


def _format_value(e: openmeta.Entry, *, max_elements: int, max_bytes: int) -> Tuple[str, str]:
    v = e.value(max_elements=max_elements, max_bytes=max_bytes)

    if v is None:
        return "-", "-"

    if isinstance(v, bytes):
        if e.value_kind == openmeta.MetaValueKind.Text:
            raw, dangerous = openmeta.console_text(v, max_bytes=int(max_bytes))
            val = raw if not dangerous else "(DANGEROUS) " + raw
            return raw, val

        raw = openmeta.hex_bytes(v, max_bytes=int(max_bytes))
        return raw, raw

    if isinstance(v, tuple) and len(v) == 2 and all(isinstance(x, int) for x in v):
        raw = f"{v[0]}/{v[1]}"
        val = _rational_to_decimal(v[0], v[1])
        return raw, val

    if isinstance(v, list):
        if v and isinstance(v[0], tuple) and len(v[0]) == 2:
            raw_parts = []
            val_parts = []
            for item in v:
                if not (isinstance(item, tuple) and len(item) == 2):
                    break
                n, d = item
                raw_parts.append(f"{n}/{d}")
                val_parts.append(_rational_to_decimal(int(n), int(d)))
            return ", ".join(raw_parts), ", ".join(val_parts)
        return ", ".join(str(x) for x in v), ", ".join(str(x) for x in v)

    return str(v), str(v)


def _iter_exif_entries(doc: openmeta.Document) -> Iterable[openmeta.Entry]:
    for i in range(int(doc.entry_count)):
        e = doc[i]
        if e.key_kind == openmeta.MetaKeyKind.ExifTag:
            yield e


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="metaread.py")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--no-build-info", action="store_true", help="hide OpenMeta build info header")
    ap.add_argument("--no-blocks", action="store_true", help="hide container block summary")
    ap.add_argument("--no-pointer-tags", action="store_true", help="do not store pointer tags")
    ap.add_argument("--no-decompress", action="store_true", help="do not decompress payloads")
    ap.add_argument("--max-elements", type=int, default=16, help="max array elements to print")
    ap.add_argument("--max-bytes", type=int, default=256, help="max bytes to print for text/bytes")
    ap.add_argument("--max-cell-chars", type=int, default=32, help="max chars per table cell")
    ap.add_argument(
        "--max-file-bytes",
        type=int,
        default=512 * 1024 * 1024,
        help="refuse to read files larger than N bytes (0=unlimited)",
    )
    args = ap.parse_args(argv)

    if not args.no_build_info:
        l1, l2 = openmeta.info_lines()
        print(l1)
        print(l2)
        print(openmeta.python_info_line())

    for path in args.files:
        doc = openmeta.read(
            path,
            include_pointer_tags=not args.no_pointer_tags,
            decompress=not args.no_decompress,
            max_file_bytes=int(args.max_file_bytes),
        )

        print(f"== {path}")
        print(f"size={doc.file_size}")
        print(f"scan={_snake(doc.scan_status.name)} written={doc.scan_written} needed={doc.scan_needed}")

        if not args.no_blocks:
            for i, b in enumerate(doc.blocks):
                print(
                    "block[{i}] format={fmt} kind={kind} comp={comp} chunking={chunk} "
                    "id=0x{id:08X} outer=({oo},{os}) data=({do},{ds})".format(
                        i=i,
                        fmt=_snake(b.format.name),
                        kind=_snake(b.kind.name),
                        comp=_snake(b.compression.name),
                        chunk=_snake(b.chunking.name),
                        id=int(b.id),
                        oo=int(b.outer_offset),
                        os=int(b.outer_size),
                        do=int(b.data_offset),
                        ds=int(b.data_size),
                    )
                )

        print(f"exif={_snake(doc.exif_status.name)} ifds_decoded={doc.block_count} entries={doc.entry_count}")

        if doc.exif_status != openmeta.ExifDecodeStatus.Ok:
            continue

        by_ifd_block: dict[Tuple[int, str], list[openmeta.Entry]] = defaultdict(list)
        for e in _iter_exif_entries(doc):
            ifd = e.ifd or ""
            by_ifd_block[(int(e.origin_block), str(ifd))].append(e)

        for (block_id, ifd), entries in sorted(by_ifd_block.items()):
            entries.sort(key=lambda x: int(x.origin_order))

            width = 119
            print("=" * width)
            print(f" ifd={ifd} block={block_id} entries={len(entries)}")
            print("=" * width)
            print(
                " idx | ifd    | name               | tag    | tag type     | count | type       | raw val                        | val"
            )
            print("-" * width)

            for idx, e in enumerate(entries):
                tag = e.tag if e.tag is not None else 0
                name = e.name if e.name is not None else "-"
                ifd_short = (e.ifd or "-")[:6]

                type_code = int(e.wire_type_code)
                type_name = _tiff_type_name(type_code)
                tag_type = f"{type_code}({type_name})"

                raw, val = _format_value(e, max_elements=args.max_elements, max_bytes=args.max_bytes)
                raw = _truncate_cell(raw, args.max_cell_chars)
                val = _truncate_cell(val, args.max_cell_chars)

                print(
                    f"{idx:4d} | {ifd_short:<6} | {_truncate_cell(str(name), 18):<18} | "
                    f"0x{int(tag):04X} | {_truncate_cell(tag_type, 12):<12} | "
                    f"{int(e.wire_count):5d} | {_truncate_cell(_val_type(e), 10):<10} | "
                    f"{raw:<30} | {val}"
                )
            print("=" * width)

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
