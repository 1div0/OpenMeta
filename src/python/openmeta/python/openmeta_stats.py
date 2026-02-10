#!/usr/bin/env python3

from __future__ import annotations

import argparse
import re
import sys
from collections import Counter

try:
    import openmeta
except ModuleNotFoundError:
    sys.stderr.write("error: Python module 'openmeta' was not found.\n")
    sys.stderr.write("Run this script with the same Python environment where the OpenMeta wheel is installed.\n")
    sys.stderr.write("\n")
    sys.stderr.write("Examples:\n")
    sys.stderr.write("  uv run python -m openmeta.python.openmeta_stats <file>\n")
    sys.stderr.write("  /path/to/venv/bin/python -m openmeta.python.openmeta_stats <file>\n")
    raise SystemExit(2)


def _snake(name: str) -> str:
    return re.sub(r"(?<!^)([A-Z])", r"_\1", name).lower()


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(prog="openmeta_stats.py")
    ap.add_argument("files", nargs="+")
    ap.add_argument("--no-build-info", action="store_true", help="hide OpenMeta build info header")
    ap.add_argument("--xmp-sidecar", action="store_true", help="also read sidecar XMP (<file>.xmp, <basename>.xmp)")
    ap.add_argument("--max-file-bytes", type=int, default=512 * 1024 * 1024)
    args = ap.parse_args(argv)

    if not args.no_build_info:
        l1, l2 = openmeta.info_lines()
        print(l1)
        print(l2)
        print(openmeta.python_info_line())

    for path in args.files:
        doc = openmeta.read(
            path,
            include_xmp_sidecar=bool(args.xmp_sidecar),
            max_file_bytes=int(args.max_file_bytes),
        )

        print(f"== {path}")
        print(f"size={doc.file_size}")
        print(f"scan={_snake(doc.scan_status.name)} blocks={len(doc.blocks)}")
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

        fmt_counts = Counter(_snake(b.format.name) for b in doc.blocks)
        kind_counts = Counter(_snake(b.kind.name) for b in doc.blocks)
        comp_counts = Counter(_snake(b.compression.name) for b in doc.blocks)

        if doc.blocks:
            print("container_formats:", ", ".join(f"{k}={v}" for k, v in sorted(fmt_counts.items())))
            print("block_kinds:", ", ".join(f"{k}={v}" for k, v in sorted(kind_counts.items())))
            print("compressions:", ", ".join(f"{k}={v}" for k, v in sorted(comp_counts.items())))

        if doc.entry_count:
            ifd_counts = Counter()
            tag_counts = Counter()
            unknown_names = 0
            value_kinds = Counter()
            elem_types = Counter()

            for i in range(int(doc.entry_count)):
                e = doc[i]
                if e.key_kind != openmeta.MetaKeyKind.ExifTag:
                    continue

                ifd = str(e.ifd or "")
                tag = int(e.tag or 0)
                ifd_counts[ifd] += 1
                tag_counts[(ifd, tag)] += 1
                if e.name is None:
                    unknown_names += 1
                value_kinds[_snake(e.value_kind.name)] += 1
                elem_types[_snake(e.elem_type.name)] += 1

            dups = [(k, v) for (k, v) in tag_counts.items() if v > 1]

            print("ifds:", ", ".join(f"{k}={v}" for k, v in sorted(ifd_counts.items())))
            print(f"unknown_tag_names={unknown_names}")
            print(f"duplicate_tags={len(dups)}")
            if dups:
                dups.sort(key=lambda x: x[1], reverse=True)
                top = dups[:10]
                print(
                    "top_duplicates:",
                    ", ".join(f"{ifd}:0x{tag:04X}={cnt}" for ((ifd, tag), cnt) in top),
                )

            print("value_kinds:", ", ".join(f"{k}={v}" for k, v in sorted(value_kinds.items())))
            print("elem_types:", ", ".join(f"{k}={v}" for k, v in sorted(elem_types.items())))

    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
