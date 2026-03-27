# OpenMeta

OpenMeta is a metadata processing library for image files.

The current focus is safe, format-agnostic reads: find metadata blocks in
common containers, decode them into a normalized in-memory model, and expose
stable transfer/edit building blocks for export workflows.

## Status

Read-path coverage is broad and regression-gated. Write/edit support is real
for the main transfer targets, but parts of that API surface are still draft
and may change as the transfer contract stabilizes.

Current baseline-gated snapshot on tracked corpora:
- HEIC/HEIF, CR3, and mixed RAW EXIF tag-id compare gates are passing.
- EXR header metadata compare is passing for name/type/value-class checks.
- Portable and lossless sidecar export paths are covered by baseline and smoke
  gates.
- MakerNote decode is baseline-gated with broad vendor support; unknown tags
  are preserved losslessly when no structured mapping exists.

## What OpenMeta Does

- Scan containers to locate metadata blocks in `jpeg`, `png`, `webp`, `gif`,
  `tiff/dng`, `crw/ciff`, `raf`, `x3f`, `jp2`, `jxl`, and
  `heif/avif/cr3` (ISO-BMFF).
- Reassemble chunked payloads and optionally decompress supported carriers.
- Decode metadata into a normalized `MetaStore`.
- Export sidecars and previews.
- Prepare, compile, emit, and edit metadata transfers for bounded target
  families.

## Metadata Families

OpenMeta currently covers these major families:

- EXIF, including pointer IFDs and broad MakerNote support.
- Legacy Canon CRW/CIFF bridge with bounded native CIFF naming and projection.
- XMP as RDF/XML properties.
- ICC profile header and tag table.
- Photoshop IRB, with raw preservation plus a bounded interpreted subset.
- IPTC-IIM datasets.
- JPEG comments, GIF comments, and PNG text chunks.
- ISO-BMFF derived fields for primary-item, relation, and auxiliary semantics.
- JUMBF / C2PA draft structural and semantic projection.
- EXR header attributes.

For the detailed support matrix, see
[docs/metadata_support.md](docs/metadata_support.md).

## Naming Model

EXIF and MakerNote display names have two layers:

- Canonical names from `exif_tag_name(...)`
- ExifTool-compatible display names from
  `exif_entry_name(..., ExifTagNamePolicy::ExifToolCompat)`

That split lets OpenMeta keep stable internal naming while still matching
common external tooling where compatibility matters.

## Transfer and Edit

OpenMeta now has a real transfer core built around:

- `prepare_metadata_for_target(...)`
- `compile_prepared_transfer_execution(...)`
- `execute_prepared_transfer(...)`
- `execute_prepared_transfer_file(...)`

Current target status:

| Target | Status |
| --- | --- |
| JPEG | First-class |
| TIFF | First-class |
| PNG | Bounded but real |
| WebP | Bounded but real |
| JP2 | Bounded but real |
| JXL | Bounded but real |
| HEIF / AVIF / CR3 | Bounded but real |
| EXR | Bounded but real |

In practice:
- JPEG and TIFF are the strongest transfer targets today.
- PNG, WebP, JP2, JXL, bounded BMFF, and EXR all have real first-class
  transfer entry points.
- EXR is still narrower than the container-edit targets: it emits safe string
  header attributes through the transfer core, but it does not rewrite full
  EXR files yet.
- Writer-side sync behavior is now partially explicit instead of implicit:
  generated XMP can independently keep or suppress EXIF-derived and
  IPTC-derived projection during transfer preparation.
- Generated portable XMP also has an explicit conflict policy for existing
  decoded XMP versus generated EXIF/IPTC mappings:
  current behavior, `existing_wins`, or `generated_wins`.
- Transfer preparation can also fold an existing sibling `.xmp` sidecar from
  the destination path into generated portable XMP when that bounded mode is
  requested, with explicit `sidecar_wins` or `source_wins` precedence against
  source-embedded existing XMP.
- File-helper execution, `metatransfer`, and the Python transfer wrapper now
  share a bounded XMP carrier choice:
  embedded XMP only, sidecar-only writeback to a sibling `.xmp`, or dual
  embedded-plus-sidecar writeback when a generated XMP packet exists for the
  prepared transfer.
- Prepared bundles record resolved policy decisions for MakerNote, JUMBF,
  C2PA, EXIF-to-XMP projection, and IPTC-to-XMP projection.
- This is still not a full MWG-style sync engine. OpenMeta does not yet try to
  solve all EXIF/IPTC/XMP conflict resolution or canonical writeback policy.

For transfer details, see
[docs/metadata_transfer_plan.md](docs/metadata_transfer_plan.md).

## Tools

OpenMeta ships a small set of CLI tools:

| Tool | Purpose |
| --- | --- |
| `metaread` | Human-readable metadata dump |
| `metavalidate` | Metadata validation and issue reporting |
| `metadump` | Sidecar and preview dump tool |
| `metatransfer` | Transfer/edit smoke tool over the core transfer APIs |
| `thumdump` | Preview extractor |

The Python bindings expose the same read and transfer core through thin wrapper
helpers in `src/python/`.

## Layout

- `src/include/openmeta/`: public headers
- `src/openmeta/`: library implementation
- `src/tools/`: CLI tools
- `src/python/`: Python bindings and helper scripts
- `tests/`: unit tests and fuzz targets
- `docs/`: design notes and developer documentation

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Useful options:
- `-DOPENMETA_BUILD_TOOLS=ON|OFF`
- `-DOPENMETA_BUILD_TESTS=ON` for GoogleTest-based unit tests
- `-DOPENMETA_BUILD_FUZZERS=ON` for Clang + libFuzzer targets
- `-DOPENMETA_USE_LIBCXX=ON` when linking against dependencies built with
  `libc++`
- `-DOPENMETA_BUILD_DOCS=ON` for Doxygen HTML docs
- `-DOPENMETA_BUILD_SPHINX_DOCS=ON` for Sphinx + Breathe HTML docs

Developer notes live in [docs/development.md](docs/development.md).

## Quick Usage

`simple_meta_read(...)` performs `scan_auto(...)`, payload extraction, and
decode in one call.

- Input: whole-file bytes
- Output: `MetaStore` plus discovered `ContainerBlockRef[]`
- Scratch: caller-provided block list, IFD list, payload buffer, and
  part-index buffer

## Documentation

- [docs/metadata_support.md](docs/metadata_support.md): metadata support matrix
- [docs/metadata_transfer_plan.md](docs/metadata_transfer_plan.md): transfer
  status and roadmap
- [docs/doxygen.md](docs/doxygen.md): API reference
- [SECURITY.md](SECURITY.md): security model and reporting
- [NOTICE.md](NOTICE.md): notices and third-party dependency information
