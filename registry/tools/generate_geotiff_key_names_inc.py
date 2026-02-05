#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
from typing import Dict, List, Optional, Tuple


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
        default=Path("registry/geotiff/keys.jsonl"),
        help="Input registry path (a .jsonl file).",
    )
    ap.add_argument(
        "--out-inc",
        type=Path,
        default=Path("src/openmeta/geotiff_key_names_generated.inc"),
        help="Output C++ include with a static key-id -> name table.",
    )
    args = ap.parse_args(argv)

    table: Dict[int, str] = {}
    with args.in_path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rec = json.loads(line)
            if rec.get("kind") != "geotiff.key":
                continue
            key_id = rec.get("key_id")
            if not isinstance(key_id, int) or key_id < 0 or key_id > 0xFFFF:
                continue
            name = rec.get("name", "")
            if not isinstance(name, str) or not name:
                continue

            prev = table.get(key_id)
            if prev is None or name < prev:
                table[key_id] = name

    items: List[Tuple[int, str]] = sorted(table.items(), key=lambda kv: kv[0])

    out: List[str] = []
    out.append("// Generated file. Do not edit by hand.\n")
    out.append("// Generated from: registry/geotiff/keys.jsonl\n\n")
    out.append("static constexpr GeotiffKeyNameEntry kGeotiffKeys[] = {\n")
    for key_id, name in items:
        out.append(
            f"    {{ {key_id}u, \"{_escape_c_string(name)}\" }},\n"
        )
    out.append("};\n")

    _emit(args.out_inc, "".join(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

