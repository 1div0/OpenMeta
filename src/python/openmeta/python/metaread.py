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


def _fourcc_str(v: int) -> str:
    b = bytes([(v >> 24) & 0xFF, (v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF])
    if all(0x20 <= c <= 0x7E for c in b):
        return b.decode("ascii", errors="replace")
    return f"0x{v:08X}"


def _icc_header_field_name(offset: int) -> str:
    return {
        0: "profile_size",
        4: "cmm_type",
        8: "version",
        12: "class",
        16: "data_space",
        20: "pcs",
        24: "date_time",
        36: "signature",
        40: "platform",
        44: "flags",
        48: "manufacturer",
        52: "model",
        56: "attributes",
        64: "rendering_intent",
        68: "pcs_illuminant",
        80: "creator",
        84: "profile_id",
    }.get(offset, "-")


def _photoshop_resource_name(rid: int) -> str:
    return {
        0x0404: "IPTC_NAA",
        0x0422: "EXIF_DATA_1",
        0x0423: "EXIF_DATA_3",
    }.get(rid, "-")


def _looks_ascii(data: bytes) -> bool:
    if not data:
        return False
    if b"\x00" in data:
        return False
    for c in data:
        if c < 0x09 or (0x0D < c < 0x20) or c > 0x7E:
            return False
    return True


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
        if e.key_kind == openmeta.MetaKeyKind.IptcDataset and _looks_ascii(v):
            raw_hex = openmeta.hex_bytes(v, max_bytes=int(max_bytes))
            text, dangerous = openmeta.console_text(v, max_bytes=int(max_bytes))
            val = text if not dangerous else "(DANGEROUS) " + text
            return raw_hex, val

        if e.key_kind == openmeta.MetaKeyKind.IccHeaderField and len(v) == 4 and _looks_ascii(v):
            raw_hex = openmeta.hex_bytes(v, max_bytes=int(max_bytes))
            return raw_hex, v.decode("ascii", errors="replace")

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
    ap.add_argument("--makernotes", action="store_true", help="attempt MakerNote decode (best-effort)")
    ap.add_argument("--no-decompress", action="store_true", help="do not decompress payloads")
    ap.add_argument("--xmp-sidecar", action="store_true", help="also read sidecar XMP (<file>.xmp, <basename>.xmp)")
    ap.add_argument("--max-elements", type=int, default=16, help="max array elements to print")
    ap.add_argument("--max-bytes", type=int, default=256, help="max bytes to print for text/bytes")
    ap.add_argument("--max-cell-chars", type=int, default=32, help="max chars per table cell")
    ap.add_argument(
        "--max-file-bytes",
        type=int,
        default=0,
        help="optional file mapping cap in bytes (0=unlimited)",
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
            decode_makernote=bool(args.makernotes),
            decompress=not args.no_decompress,
            include_xmp_sidecar=bool(args.xmp_sidecar),
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

        print(
            f"exif={_snake(doc.exif_status.name)} ifds_decoded={doc.exif_ifds_decoded} "
            f"exr={_snake(doc.exr_status.name)} exr_parts={doc.exr_parts_decoded} exr_entries={doc.exr_entries_decoded} "
            f"xmp={_snake(doc.xmp_status.name)} xmp_entries={doc.xmp_entries_decoded} "
            f"entries={doc.entry_count} blocks={doc.block_count}"
        )
        if (
            doc.exif_status == openmeta.ExifDecodeStatus.LimitExceeded
            and doc.exif_limit_reason != openmeta.ExifLimitReason.None_
        ):
            print(
                "exif_limit "
                f"reason={_snake(doc.exif_limit_reason.name)} "
                f"ifd_off={int(doc.exif_limit_ifd_offset)} "
                f"tag=0x{int(doc.exif_limit_tag):04X}"
            )

        by_block: dict[int, list[openmeta.Entry]] = defaultdict(list)
        for i in range(int(doc.entry_count)):
            e = doc[i]
            by_block[int(e.origin_block)].append(e)

        for block_id in sorted(by_block.keys()):
            entries = by_block[block_id]
            entries.sort(key=lambda x: int(x.origin_order))

            if entries and entries[0].key_kind == openmeta.MetaKeyKind.ExifTag:
                # Group by IFD token inside the EXIF block.
                by_ifd: dict[str, list[openmeta.Entry]] = defaultdict(list)
                for e in entries:
                    by_ifd[str(e.ifd or "")].append(e)

                for ifd, ifd_entries in sorted(by_ifd.items()):
                    width = 119
                    print("=" * width)
                    print(f" ifd={ifd} block={block_id} entries={len(ifd_entries)}")
                    print("=" * width)
                    print(
                        " idx | ifd    | name               | tag    | tag type     | count | type       | raw val                        | val"
                    )
                    print("-" * width)

                    for idx, e in enumerate(ifd_entries):
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
                continue

            if entries and entries[0].key_kind == openmeta.MetaKeyKind.XmpProperty:
                width = 120
                print("=" * width)
                print(f" xmp block={block_id} entries={len(entries)}")
                print("=" * width)
                print(" idx | schema                 | path                   | type       | raw val                        | val")
                print("-" * width)

                for idx, e in enumerate(entries):
                    schema = str(e.xmp_schema_ns or "-")
                    path_s = str(e.xmp_path or "-")

                    raw, val = _format_value(e, max_elements=args.max_elements, max_bytes=args.max_bytes)
                    raw = _truncate_cell(raw, args.max_cell_chars)
                    val = _truncate_cell(val, args.max_cell_chars)

                    print(
                        f"{idx:4d} | {_truncate_cell(schema, 22):<22} | {_truncate_cell(path_s, 22):<22} | "
                        f"{_truncate_cell(_val_type(e), 10):<10} | {raw:<30} | {val}"
                    )
                print("=" * width)
                continue

            # Generic non-EXIF block table.
            width = 100
            print("=" * width)
            if entries:
                print(f" kind={_snake(entries[0].key_kind.name)} block={block_id} entries={len(entries)}")
            else:
                print(f" block={block_id} entries=0")
            print("=" * width)
            print(" idx | key            | name            | type       | raw val                        | val")
            print("-" * width)

            for idx, e in enumerate(entries):
                key = "-"
                name = "-"
                if e.key_kind == openmeta.MetaKeyKind.IptcDataset:
                    key = f"{int(e.iptc_record)}:{int(e.iptc_dataset)}"
                elif e.key_kind == openmeta.MetaKeyKind.PhotoshopIrb:
                    key = f"0x{int(e.photoshop_resource_id):04X}"
                    name = _photoshop_resource_name(int(e.photoshop_resource_id))
                elif e.key_kind == openmeta.MetaKeyKind.IccHeaderField:
                    key = f"0x{int(e.icc_header_offset):X}"
                    name = _icc_header_field_name(int(e.icc_header_offset))
                elif e.key_kind == openmeta.MetaKeyKind.IccTag:
                    key = _fourcc_str(int(e.icc_tag_signature))
                elif e.key_kind == openmeta.MetaKeyKind.ExrAttribute:
                    key = f"part:{int(e.exr_part)}"
                    name = str(e.exr_name or "-")

                raw, val = _format_value(e, max_elements=args.max_elements, max_bytes=args.max_bytes)
                raw = _truncate_cell(raw, args.max_cell_chars)
                val = _truncate_cell(val, args.max_cell_chars)

                print(
                    f"{idx:4d} | {_truncate_cell(key, 12):<12} | {_truncate_cell(name, 14):<14} | "
                    f"{_truncate_cell(_val_type(e), 10):<10} | {raw:<30} | {val}"
                )
            print("=" * width)

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
