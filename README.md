# OpenMeta

OpenMeta is a metadata processing library.

Current focus: **safe, format-agnostic reads** (locate metadata blocks inside
common containers and decode EXIF/TIFF tags into a normalized in-memory model).

## Status

Early development. APIs and data model are expected to evolve.

## Features

- Container scanning: locate metadata blocks in `jpeg`, `png`, `webp`, `gif`,
  `tiff/dng`, `jp2`, `jxl`.
- EXIF decoding: decode TIFF-IFD tags (including pointer IFDs) into `MetaStore`.
- CLI tool: `metaread` (human-readable dump; output is sanitized).
- Security-first: explicit decode limits + fuzz targets; see `SECURITY.md`.

## Layout

- `include/openmeta/`: public headers
- `src/`: library implementation
- `tools/`: CLI tools (`metaread`)
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
- `-DOPENMETA_BUILD_DOCS=ON` (requires Doxygen; installs HTML docs)

Developer notes: `docs/development.md`

## Quick Usage (read)

`simple_meta_read(...)` does `scan_auto(...)` + EXIF decode in one call:
- Input: whole file bytes
- Output: `MetaStore` (EXIF tags) + `ContainerBlockRef[]` (all discovered blocks)

## Documentation

- Security: `SECURITY.md`
- API reference (Doxygen): `docs/doxygen.md`
