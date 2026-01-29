# EXIF Registry

This folder contains **EXIF/TIFF tag registries** used by OpenMeta tools.

- `makernotes/*.jsonl`: MakerNote tag namespaces keyed by `ifd` + `tag` with a
  human-readable `name`. Files are split by maker namespace for easier review
  (for example: `makernotes/canon.jsonl`), but the record format is identical
  across files.
  The registry only includes **16-bit IFD tag ids** (TIFF/EXIF tag space).

The compiled C++ lookup used by `metaread` lives in
`src/openmeta/exif_makernote_tag_names_generated.inc` and can be regenerated
from this registry directory:

```bash
python3 OpenMeta/registry/tools/generate_makernote_tag_names_inc.py
```

See `registry/SCHEMA.md` for the JSONL schema.
