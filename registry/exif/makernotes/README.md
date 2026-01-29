# MakerNotes

MakerNote tags are **vendor-defined** EXIF/TIFF IFD namespaces.

This directory contains JSONL files split by maker namespace to keep reviews
and diffs manageable (for example: `canon.jsonl`, `nikon.jsonl`, `sony.jsonl`).
Each line follows the `kind: "exif.tag"` schema in `registry/SCHEMA.md`.

Notes:
- Records use a normalized `ifd` key like `makernote:canon:main`.
- Only **16-bit** IFD tag ids are included.

