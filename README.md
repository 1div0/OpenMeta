# OpenMeta

OpenMeta is a metadata processing library.

Current focus: **safe, format-agnostic reads** — locate metadata blocks in
common containers and decode EXIF/TIFF tags into a normalized in-memory model.

## Status

This is early-stage. Expect breaking API changes.

## Features

- Container scanning: locate metadata blocks in `jpeg`, `png`, `webp`, `gif`,
  `tiff/dng`, `jp2`, `jxl`, `heif/avif/cr3` (ISO-BMFF).
- EXIF decoding: decode TIFF-IFD tags (including pointer IFDs) into `MetaStore`.
- CLI tool: `metaread` (human-readable dump; output is sanitized).
- Security-first: explicit decode limits + fuzz targets; see `SECURITY.md`.

## Layout

- `src/include/openmeta/`: public headers
- `src/openmeta/`: library implementation
- `src/tools/`: CLI tools (`metaread`)
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
- `-DOPENMETA_BUILD_DOCS=ON` (requires Doxygen; installs HTML docs)
- `-DOPENMETA_BUILD_SPHINX_DOCS=ON` (requires Python + Sphinx+Breathe; installs HTML docs via Sphinx)

Developer notes: `docs/development.md`

## Quick Usage (read)

`simple_meta_read(...)` does `scan_auto(...)` + payload extraction + EXIF decode:
- Input: whole file bytes
- Output: `MetaStore` (EXIF tags) + `ContainerBlockRef[]` (all discovered blocks)
- Scratch: caller-provided block list, IFD list, payload buffer, and part-index buffer

## Documentation

- Security: `SECURITY.md`
- API reference (Doxygen): `docs/doxygen.md`
