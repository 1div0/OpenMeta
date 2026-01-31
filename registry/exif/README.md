# EXIF Registry

This folder contains **EXIF/TIFF tag registries** used by OpenMeta tools.

- `standard/*.jsonl`: standard (non-MakerNote) TIFF/EXIF tag names, keyed by
  canonical tag-space + 16-bit tag id. Current tag-space ids:
  - `ifd`: TIFF-style IFD tags (applies to `ifd*` / `subifd*` decoder tokens)
  - `exififd`: EXIF IFD tags (decoder token: `exififd`, plus `*_exififd`)
  - `gpsifd`: GPS IFD tags (decoder token: `gpsifd`, plus `*_gpsifd`)
  - `interopifd`: Interop IFD tags (decoder token: `interopifd`, plus `*_interopifd`)
  - `mpf`: MPF tag space (decoder tokens: `mpf*`)
- `makernotes/*.jsonl`: MakerNote tag namespaces keyed by `ifd` + `tag` with a
  human-readable `name`. Files are split by maker namespace for easier review
  (for example: `makernotes/canon.jsonl`), but the record format is identical
  across files.
  The registry only includes **16-bit IFD tag ids** (TIFF/EXIF tag space).

The compiled C++ lookups used by `metaread` live in:
- `src/openmeta/exif_standard_tag_names_generated.inc`
- `src/openmeta/exif_makernote_tag_names_generated.inc`

They can be regenerated from this registry directory:

```bash
python3 registry/tools/generate_exif_standard_tag_names_inc.py \
  --in-path registry/exif/standard \
  --out-inc src/openmeta/exif_standard_tag_names_generated.inc

python3 registry/tools/generate_makernote_tag_names_inc.py \
  --in-path registry/exif/makernotes \
  --out-inc src/openmeta/exif_makernote_tag_names_generated.inc
```

See `registry/SCHEMA.md` for the JSONL schema.
