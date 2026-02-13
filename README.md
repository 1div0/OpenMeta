# OpenMeta

OpenMeta is a metadata processing library.

Current focus: **safe, format-agnostic reads** - locate metadata blocks in
common containers and decode common metadata payloads into a normalized
in-memory model.

## Status

This is early-stage. Expect breaking API changes.

## Features

- Container scanning: locate metadata blocks in `jpeg`, `png`, `webp`, `gif`,
  `tiff/dng`, `jp2`, `jxl`, `heif/avif/cr3` (ISO-BMFF).
- Payload extraction: reassemble chunked streams and optionally decompress
  (zlib/deflate, brotli) with strict limits.
- Structured decode into `MetaStore`:
  - EXIF: TIFF-IFD tags (including pointer IFDs).
  - CRW/CIFF bridge: derives common EXIF fields (`Make`, `Model`,
    `DateTimeOriginal`, `SubjectDistance`, `PixelXDimension`,
    `PixelYDimension`, `Orientation`) from legacy Canon CRW directory tags.
  - XMP: RDF/XML packets into properties (schema namespace URI + property path).
  - ICC: profile header + tag table (raw tag bytes preserved).
  - Photoshop IRB: 8BIM resources (raw payload preserved; IPTC from 0x0404 is
    decoded as derived datasets when present).
  - IPTC-IIM: dataset streams (raw dataset bytes preserved).
  - ISO-BMFF derived fields (`MetaKeyKind::BmffField`): `ftyp.*` and primary item
    properties (`pitm`, `iprp/ipco ispe/irot/imir`, `ipma`).
- CLI tools:
  - `metaread`: human-readable dump; output is sanitized.
  - `metadump`: sidecar/preview dump tool (`--format lossless|portable`,
    `--extract-preview`, supports both positional
    `<source> <destination>` and explicit `-i/--input` + `-o/--out`; when
    multiple previews are found, `--out name.jpg` writes `name_1.jpg`,
    `name_2.jpg`, ...).
  - `thumdump`: preview-only extractor, also supports positional
    `<source> <destination>` and explicit `-i/--input` + `-o/--out`; when
    multiple previews are found, `--out name.jpg` writes `name_1.jpg`,
    `name_2.jpg`, ...).
- Security-first: explicit decode limits + fuzz targets; see `SECURITY.md`.
- Draft resource policy surface in public headers:
  `src/include/openmeta/resource_policy.h`.

## Layout

- `src/include/openmeta/`: public headers
- `src/openmeta/`: library implementation
- `src/tools/`: CLI tools (`metaread`, `metadump`)
- `src/python/`: Python bindings (nanobind) + helper scripts
- `tests/`: unit tests + fuzz targets
- `docs/`: developer docs (build, tests, fuzzing)

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Useful options:
- `-DOPENMETA_BUILD_TOOLS=ON|OFF`
- `-DOPENMETA_BUILD_TESTS=ON` (requires GoogleTest)
- `-DOPENMETA_BUILD_FUZZERS=ON` (requires Clang + libFuzzer)
- `-DOPENMETA_USE_LIBCXX=ON` (use libc++; helpful when linking against deps built with libc++)
- `-DOPENMETA_BUILD_DOCS=ON` (requires Doxygen; installs HTML docs)
- `-DOPENMETA_BUILD_SPHINX_DOCS=ON` (requires Python + Sphinx+Breathe; installs HTML docs via Sphinx)

Developer notes: `docs/development.md`

## Quick Usage (read)

`simple_meta_read(...)` does `scan_auto(...)` + payload extraction + decode:
- Input: whole file bytes
- Output: `MetaStore` (decoded entries) + `ContainerBlockRef[]` (all discovered blocks)
- Scratch: caller-provided block list, IFD list, payload buffer, and part-index buffer

## Documentation

- Security: `SECURITY.md`
- Notices (trademarks, third-party deps): `NOTICE.md`
- API reference (Doxygen): `docs/doxygen.md`
