# Development

## Build Prerequisites

- CMake `>= 3.20`
- A C++20 compiler (Clang is recommended; fuzzing requires Clang)
- Optional: Ninja (`-G Ninja`)

OpenMeta discovers optional dependencies via `find_package(...)`. If you install
deps into a custom prefix, pass it via `CMAKE_PREFIX_PATH` (example:
`-DCMAKE_PREFIX_PATH=/mnt/f/UBSd`).

## Optional Dependencies (Why They Exist)

OpenMeta's core scanning and EXIF/TIFF decoding do not require third-party
libraries. Some metadata payloads are compressed or structured; these optional
dependencies let OpenMeta decode more content:

- **Expat** (`OPENMETA_WITH_EXPAT`): parses XMP RDF/XML packets (embedded blocks
  and `.xmp` sidecars). Expat is used as a streaming parser so OpenMeta can
  enforce strict limits and avoid building a full XML DOM from untrusted input.
- **zlib** (`OPENMETA_WITH_ZLIB`): inflates Deflate-compressed payloads,
  including PNG `iCCP` (ICC profiles) and compressed text/XMP chunks (`iTXt`,
  `zTXt`).
- **Brotli** (`OPENMETA_WITH_BROTLI`): decompresses JPEG XL `brob` "compressed
  metadata" boxes so wrapped metadata payloads can be decoded.

If you link against dependencies that were built with `libc++` (common when
using Clang), configure OpenMeta with:

```bash
-DOPENMETA_USE_LIBCXX=ON
```

## Versioning

`VERSION` is the single source of truth for the project version:
- CMake reads `VERSION` and sets `PROJECT_VERSION`.
- The Python wheel version is derived from `VERSION` (via scikit-build-core metadata).

## Code Organization (EXIF + MakerNotes)

- Core EXIF/TIFF decoding: `src/openmeta/exif_tiff_decode.cc`
- ISO-BMFF (HEIF/AVIF/CR3) container-derived fields: `src/openmeta/bmff_fields_decode.cc`
  - Emitted during `simple_meta_read(...)` as `MetaKeyKind::BmffField` entries.
  - Current fields: `ftyp.*` and primary item properties (`pitm`, `iprp/ipco ispe/irot/imir`, `ipma`).
  - Parsing is intentionally bounded (depth/box count caps) and ignores unknown properties.
- GeoTIFF GeoKey decoding (derived keys): `src/openmeta/geotiff_decode.cc`
- Vendor MakerNote decoders: `src/openmeta/exif_makernote_*.cc`
  (Canon, Nikon, Sony, Olympus, Pentax, Casio, Panasonic, Kodak, Ricoh, Samsung, FLIR, etc.)
- Shared internal-only helpers: `src/openmeta/exif_tiff_decode_internal.h`
  (not installed; used to keep vendor logic out of the public API)
- Tests: `tests/makernote_decode_test.cc`

When adding or changing MakerNote code, prefer extending the vendor files and
keeping the EXIF/TIFF core container-agnostic. Add/adjust a unit test for any
new subtable or decode path.

Internal helper conventions (used by vendor decoders):
- `read_classic_ifd_entry(...)` + `ClassicIfdEntry`: parse a single 12-byte classic TIFF IFD entry.
- `resolve_classic_ifd_value_ref(...)` + `ClassicIfdValueRef`: compute the value location/size for a classic IFD entry (inline vs out-of-line), using `MakerNoteLayout` + `OffsetPolicy`.
- `MakerNoteLayout` + `OffsetPolicy`: makes "value offsets are relative to X" explicit for vendor formats. `OffsetPolicy` supports both the common unsigned base (default) and a signed base for vendors that require it (eg Canon).
- `ExifContext`: a small, decode-time cache for frequently accessed EXIF values (avoids repeated linear scans of `store.entries()`).
- MakerNote tag-name tables are generated from `registry/exif/makernotes/*.jsonl` and looked up via binary search (`exif_makernote_tag_names.cc`).
- GeoTIFF key-name table is generated from `registry/geotiff/keys.jsonl` and looked up via binary search (`geotiff_key_names.cc`).

## Tests (GoogleTest)

Requirements:
- A GoogleTest package that provides `GTest::gtest_main` (or `GTest::Main`).

Note: if your GoogleTest was built against `libc++` (common with Clang),
build OpenMeta against the same C++ standard library. Otherwise you may see
link errors involving `std::__1` vs `std::__cxx11`.

Build + run:
```bash
cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/mnt/f/UBSd \
  -DOPENMETA_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

## libFuzzer Targets

Requirements:
- Clang with libFuzzer support.

Notes:
- On Linux, Clang's bundled libFuzzer runtime is typically built against
  `libstdc++`. When `OPENMETA_USE_LIBCXX=ON`, OpenMeta keeps tests/tools on
  `libc++` but builds fuzz targets against `libstdc++` to match libFuzzer.
- libFuzzer treats metadata as untrusted input; always run under sanitizers
  and with explicit size limits.

Build + run (example 5s smoke run):
```bash
cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DOPENMETA_BUILD_FUZZERS=ON
cmake --build build-fuzz
ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_exif_tiff_decode -max_total_time=5
```

Corpus runs (seed corpora)
--------------------------

If you pass corpus directories to libFuzzer, it treats the **first** directory
as the main corpus and may add/reduce files there. To avoid modifying your seed
corpus directories, use an empty output directory first:

```bash
mkdir -p build-fuzz/_corpus_out
ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_container_scan \
  build-fuzz/_corpus_out \
  /path/to/seed-corpus-a /path/to/seed-corpus-b \
  -runs=1000
```

## FuzzTest

Requirements:
- A FuzzTest package that provides `fuzztest::fuzztest` and `fuzztest::fuzztest_gtest_main`.

Build + run:
```bash
cmake -S . -B build-fuzztest -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/mnt/f/UBSd \
  -DOPENMETA_BUILD_FUZZTEST=ON -DOPENMETA_FUZZTEST_FUZZING_MODE=ON
cmake --build build-fuzztest
ASAN_OPTIONS=detect_leaks=0 ./build-fuzztest/openmeta_fuzztest_metastore --list_fuzz_tests
ASAN_OPTIONS=detect_leaks=0 ./build-fuzztest/openmeta_fuzztest_metastore --fuzz=MetaStoreFuzz.meta_store_op_stream --fuzz_for=5s
```

## Python (nanobind)

Requirements:
- Python `>= 3.9` + development headers/libraries
- `nanobind` installed as a CMake package (findable via `CMAKE_PREFIX_PATH`)

Build:
```bash
cmake -S . -B build-py -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH=/mnt/f/UBS \
  -DOPENMETA_BUILD_PYTHON=ON -DOPENMETA_BUILD_TOOLS=OFF
cmake --build build-py
PYTHONPATH=build-py/python python3 -c "import openmeta; print(openmeta.read('file.jpg').entry_count)"
```

Notes:
- `openmeta.read(...)` releases the Python GIL while doing file I/O and decode,
  so it can be called from multiple Python threads in parallel (useful for corpus
  comparisons).

Example scripts (repo tree):
```bash
PYTHONPATH=build-py/python python3 -m openmeta.python.openmeta_stats file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metaread file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump --format portable file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump --format portable --portable-include-existing-xmp --xmp-sidecar file.jpg
```

## Python Wheel

Requirements:
- `scikit-build-core` installed in your Python environment.
- A wheel builder: `pip` (recommended) or `uv` (works even if your venv has no `pip`).

Build:
```bash
python3 -m pip wheel . -w dist --no-deps
```
Or using `uv`:
```bash
uv --no-cache build --wheel --no-build-isolation -o dist -p "$(command -v python3)" .
```
After installing the wheel, example modules are available as:
```bash
python3 -m openmeta.python.openmeta_stats file.jpg
python3 -m openmeta.python.metaread file.jpg
```
Or via CMake:
```bash
cmake -S . -B build-wheel -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DOPENMETA_BUILD_WHEEL=ON \
  -DOPENMETA_PYTHON_EXECUTABLE=/path/to/venv/bin/python3
cmake --build build-wheel --target openmeta_wheel
```

When `OPENMETA_BUILD_WHEEL=ON`, `cmake --install` also builds a wheel and copies
it into `${CMAKE_INSTALL_PREFIX}/share/openmeta/wheels` (and also copies the
Python helper scripts `metaread.py`, `metadump.py`, and `openmeta_stats.py`
into the same directory):
```bash
cmake --install build-wheel --prefix /tmp/openmeta-install
ls /tmp/openmeta-install/share/openmeta/wheels
```

If you are building offline (or want strict control of the build environment),
install `scikit-build-core` into your Python environment and enable:
`-DOPENMETA_WHEEL_NO_BUILD_ISOLATION=ON`.

## Interop Adapters

Interop adapter APIs for ASF integration targets:

- `openmeta/interop_export.h`: shared traversal and naming styles
  (`Canonical`, `XmpPortable`, `Oiio`).
- `openmeta/oiio_adapter.h`: flat OIIO-style name/value export.
- `openmeta/ocio_adapter.h`: deterministic OCIO-style metadata tree export.

Current Python binding entry points:

- `Document.export_names(style=..., include_makernotes=...)`
- `Document.oiio_attributes(...)`
- `Document.ocio_metadata_tree(...)`

Cached corpus parity gate (internal workflow):

```bash
OpenMeta-internal/internals/run_premerge_gates.sh --jobs 4
```

Include OpenEXR ground-truth checks:

```bash
OpenMeta-internal/internals/run_interop_gates.sh --tier exr --jobs 4
OpenMeta-internal/internals/run_interop_gates.sh --tier all --jobs 4
OpenMeta-internal/internals/run_interop_gates.sh --tier heic --jobs 4 --require-exr 0
```

Notes:
- `run_premerge_gates.sh` is the standard pre-merge gate entrypoint.
- `run_interop_gates.sh` now defaults to `--tier all`.
- EXR gate is required by default (`--require-exr 1`) and includes safe value parity checks (`--exr-check-values 1`).

Cached adapter-name parity (internal workflow):

```bash
python3 OpenMeta-internal/internals/compare_openmeta_interop_names_cached.py \
  --tool both --jobs 4 --quiet
```

## Doxygen (Optional)

Requirements:
- `doxygen` (optional: `graphviz`)

Generate API docs:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENMETA_BUILD_DOCS=ON
cmake --build build --target openmeta_docs
```

## Sphinx Docs (Optional)

Requirements:
- `doxygen`
- Python packages listed in `docs/requirements.txt` (Sphinx + Breathe; `furo` is optional)

Install the Python deps into your active environment (example with `uv`):
```bash
uv pip install -r docs/requirements.txt
```

Build:
```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENMETA_BUILD_SPHINX_DOCS=ON
cmake --build build --target openmeta_docs_sphinx
```

Install:
```bash
cmake --install build --prefix /tmp/openmeta-install
ls /tmp/openmeta-install/share/doc/OpenMeta/html/index.html
```

When both `OPENMETA_BUILD_SPHINX_DOCS=ON` and `OPENMETA_BUILD_DOCS=ON`, the
Doxygen HTML output is installed under `share/doc/OpenMeta/doxygen/html`.
