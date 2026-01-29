#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Dict, List, Optional, Tuple


def _cpp_ident(name: str) -> str:
    out: List[str] = []
    cap_next = True
    for ch in name:
        if ch.isalnum():
            out.append(ch.upper() if cap_next else ch)
            cap_next = False
        else:
            cap_next = True
    return "".join(out) if out else "Unknown"


def _emit(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    with tmp.open("w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def _escape_c_string(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def main(argv: Optional[List[str]] = None) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--in-path",
        type=Path,
        default=Path("OpenMeta/registry/exif/makernotes"),
        help="Input registry path (a .jsonl file or a directory of .jsonl files).",
    )
    ap.add_argument(
        "--out-inc",
        type=Path,
        default=Path("OpenMeta/src/openmeta/exif_makernote_tag_names_generated.inc"),
        help="Output C++ include with static tag-name tables.",
    )
    args = ap.parse_args(argv)

    tables: Dict[str, Dict[int, str]] = {}

    paths: List[Path] = []
    if args.in_path.is_dir():
        paths = sorted(p for p in args.in_path.glob("*.jsonl") if p.is_file())
    else:
        paths = [args.in_path]

    for path in paths:
        with path.open("r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                rec = json.loads(line)
                if rec.get("kind") != "exif.tag":
                    continue
                ifd = rec.get("ifd", "")
                if not ifd.endswith(":main"):
                    continue

                tag_s = rec.get("tag", "")
                if not tag_s:
                    continue
                tag = int(tag_s, 16)
                if tag < 0 or tag > 0xFFFF:
                    continue

                name = rec.get("name", "")
                if not name:
                    continue

                d = tables.setdefault(ifd, {})
                prev = d.get(tag)
                if prev is None or name < prev:
                    d[tag] = name

    out: List[str] = []
    out.append("// Generated file. Do not edit by hand.\n")
    out.append("// Generated from: OpenMeta/registry/exif/makernotes/*.jsonl\n\n")

    for ifd in sorted(tables.keys()):
        items: List[Tuple[int, str]] = sorted(tables[ifd].items(), key=lambda kv: kv[0])
        ident = _cpp_ident(ifd)
        out.append(f"static constexpr MakerNoteTagNameEntry k{ident}[] = {{\n")
        for tag, name in items:
            out.append(f"    {{ 0x{tag:04X}u, \"{_escape_c_string(name)}\" }},\n")
        out.append("};\n\n")

    out.append("static constexpr MakerNoteTableMap kMakerNoteMainTables[] = {\n")
    for ifd in sorted(tables.keys()):
        ident = _cpp_ident(ifd)
        count = len(tables[ifd])
        out.append(f"    {{ \"{ifd}\", k{ident}, {count}u }},\n")
    out.append("};\n")

    _emit(args.out_inc, "".join(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
