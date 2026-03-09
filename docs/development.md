#Development

See also: `docs/metadata_support.md` for current container/block/decode support.

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
- **Draft C2PA verify scaffold** (`OPENMETA_ENABLE_C2PA_VERIFY`,
  `OPENMETA_C2PA_VERIFY_BACKEND`): enables backend selection/reporting fields
  (`none|auto|native|openssl`) and draft verification flow. Native backend
  availability is platform-based (Windows/macOS), while OpenSSL availability
  is discovered via `find_package(OpenSSL)` when needed.

If you link against dependencies that were built with `libc++` (common when
using Clang), configure OpenMeta with:

```bash
-DOPENMETA_USE_LIBCXX=ON
```

## Versioning

`VERSION` is the single source of truth for the project version:
- CMake reads `VERSION` and sets `PROJECT_VERSION`.
- The Python wheel version is derived from `VERSION` (via scikit-build-core metadata).

## CLI Tools

`metavalidate` checks decode-status health and DNG/CCM validation:

```bash
#Basic validation
./build/metavalidate input.dng

#Strict mode : warnings fail the file
./build/metavalidate --strict input.dng

#Machine - readable JSON output
./build/metavalidate --json input.dng

#Validate with sidecar + MakerNotes + C2PA verify status
./build/metavalidate --xmp-sidecar --makernotes --c2pa-verify input.jpg
```
`metavalidate` CLI is a thin wrapper over `openmeta::validate_file(...)`.
Machine-readable JSON output includes issue codes suitable for gating, for
example `xmp/output_truncated` and `xmp/invalid_or_malformed_xml_text`.

`metadump` is the general dump/save tool:

```bash
#Lossless sidecar
./build/metadump --format lossless input.jpg output.xmp

#Portable sidecar
./build/metadump --format portable --portable-include-existing-xmp input.jpg output.xmp

#Portable sidecar with ExifTool GPS time alias compatibility
./build/metadump --format portable --portable-exiftool-gpsdatetime-alias input.jpg output.xmp

#Portable sidecar + draft C2PA verify scaffold status reporting
./build/metadump --format portable --c2pa-verify --c2pa-verify-backend auto input.jpg output.xmp

#Explicit input / output form
./build/metadump -i input.jpg -o output.xmp

#Extract first embedded preview
./build/metadump --extract-preview --first-only input.jpg preview.jpg

#If multiple previews exist, --out gets auto - suffixed:
#preview_1.jpg, preview_2.jpg, ...
./build/metadump --extract-preview input.arq preview.jpg
```

Portable sidecar note:
- `exif:GPSTimeStamp` is emitted as XMP date-time text (`YYYY-MM-DDThh:mm:ssZ`)
  only when `GPSDateStamp` is available; otherwise it is skipped.
- Compatibility mode `--portable-exiftool-gpsdatetime-alias` emits
  `exif:GPSDateTime` instead of `exif:GPSTimeStamp`.
- Portable IPTC-IIM mapping covers `dc:*` plus selected `photoshop:*` and
  `Iptc4xmpCore:*` fields (for example city/state/country/headline/credit and
  location/country-code).

`metatransfer` is a transfer smoke tool for JPEG/TIFF packaging:

```bash
#read -> prepare -> emit simulation
./build/metatransfer input.jpg

#Portable vs lossless transfer-prepared XMP block
./build/metatransfer --format portable input.jpg
./build/metatransfer --format lossless input.jpg

#Write prepared payload bytes for inspection
./build/metatransfer --unsafe-write-payloads --out-dir payloads input.jpg

#Prepare once, emit many times (same bundle)
./build/metatransfer --emit-repeat 100 input.jpg

#Patch prepared EXIF time fields before emit
./build/metatransfer --time-patch DateTimeOriginal="2026:03:06 12:34:56" input.jpg

#Plan edit strategy without writing output
./build/metatransfer --mode auto --dry-run input.jpg

#Write edited JPEG output (metadata rewrite mode)
./build/metatransfer --mode metadata_rewrite -o output.jpg input.jpg

#Use separate metadata source and JPEG target stream
./build/metatransfer \
  --source-meta source.tif \
  --target-jpeg target.jpg \
  --mode metadata_rewrite \
  -o injected.jpg

#Use separate metadata source and TIFF target stream
./build/metatransfer \
  --source-meta source.jpg \
  --target-tiff target.tif \
  -o injected.tif
```

`metatransfer` is a thin CLI wrapper over
`execute_prepared_transfer_file(...)`, which wraps
`prepare_metadata_for_target_file(...)` plus the shared
`execute_prepared_transfer(...)` path for time patching, route compile/emit,
and optional JPEG/TIFF edit plan/apply. The core also exposes
`compile_prepared_transfer_execution(...)` plus
`execute_prepared_transfer_compiled(...)` for
`prepare once -> compile once -> patch/emit many` workflows. When `-o` is
used, the CLI passes a `TransferByteWriter` sink into the shared execution
path so edited output can stream directly to disk instead of always
materializing a full output buffer.
Current v1 behavior is:

- JPEG edit output is streamed directly from the shared core path.
- JPEG metadata-only emit can also stream marker bytes directly through the
  shared core API.
- TIFF edit output uses the same sink API and only buffers the appended
  metadata tail; it no longer materializes a second full-file output buffer.
- `TransferProfile` now uses explicit `TransferPolicyAction` values for
  `makernote`, `jumbf`, and `c2pa`.
- `PreparedTransferBundle::policy_decisions` records the resolved per-family
  transfer decision during prepare.
- Current policy resolution for JPEG/TIFF prepare is:
  - MakerNote: `Keep` by default, `Drop` when requested, `Invalidate`
    resolves to `Drop`, and `Rewrite` currently resolves to raw-preserve
    (`Keep`) with a warning.
  - JUMBF/C2PA: current JPEG/TIFF prepare does not serialize them, so
    non-`Drop` requests resolve to `Drop` with explicit policy diagnostics and
    prepare warnings.

`thumdump` is preview-only and optimized for batch preview extraction:

```bash
#Positional input / output
./build/thumdump input.jpg preview.jpg

#Explicit input / output
./build/thumdump -i input.jpg -o preview.jpg

#Batch mode
./build/thumdump --out-dir previews --first-only input1.jpg input2.cr2

#If multiple previews exist, --out gets auto - suffixed:
#preview_1.jpg, preview_2.jpg, ...
./build/thumdump input.arq preview.jpg
```

### Resource Budgets (Draft)

OpenMeta tools now default to **no hard file-size cap** (`--max-file-bytes 0`).
Resource control is expected to come from parser/decode budgets:

- `metaread` / `metavalidate` / `metadump` / `metatransfer`:
  - `--max-payload-bytes`, `--max-payload-parts`
  - `--max-exif-ifds`, `--max-exif-entries`, `--max-exif-total`
  - `--max-exif-value-bytes`, `--max-xmp-input-bytes`
- `metadump` / `thumdump` preview scan:
  - `--max-preview-ifds`, `--max-preview-total`, `--max-preview-bytes`

This policy surface is intentionally marked draft and may be refined.

## Code Organization (EXIF + MakerNotes)

- Core EXIF/TIFF decoding: `src/openmeta/exif_tiff_decode.cc`
- Normalized DNG/RAW CCM query surface: `src/include/openmeta/ccm_query.h`,
  `src/openmeta/ccm_query.cc` (`collect_dng_ccm_fields(...)`)
  with DNG-oriented validation diagnostics (`CcmIssue`) in warning mode and
  non-finite numeric field rejection.
  Current warning taxonomy also includes practical checks such as
  `invalid_illuminant_code`, `white_xy_out_of_range`, and unusually large
  matrix-like field counts.
- ICC tag interpretation helpers: `src/include/openmeta/icc_interpret.h`,
  `src/openmeta/icc_interpret.cc` (`icc_tag_name(...)`,
  `interpret_icc_tag(...)` for `desc`/`text`/`sig `/`mluc`/`dtim`/`view`/`meas`/`chrm`/`sf32`/`uf32`/`ui08`/`ui16`/`ui32`/`mft1`/`mft2`/`mAB`/`mBA`/`XYZ `/`curv`/`para`,
  plus `format_icc_tag_display_value(...)` for shared CLI/Python rendering)
- ISO-BMFF (HEIF/AVIF/CR3) container-derived fields: `src/openmeta/bmff_fields_decode.cc`
  - Emitted during `simple_meta_read(...)` as `MetaKeyKind::BmffField` entries.
  - Current fields: `ftyp.*`, primary item properties (`pitm`, `iprp/ipco ispe/irot/imir`, `ipma`), draft `iref.*` relation fields (`ref_type`, `from_item_id`, `to_item_id`, `edge_count`), typed derived relation rows (`iref.auxl.*`, `iref.dimg.*`, `iref.thmb.*`, `iref.cdsc.*`), per-type relation counters (`iref.<type>.edge_count`) and per-type unique source/target counters (`iref.<type>.from_item_unique_count`, `iref.<type>.to_item_unique_count`), per-type graph-summary aliases (`iref.graph.<type>.edge_count`, `iref.graph.<type>.from_item_unique_count`, `iref.graph.<type>.to_item_unique_count`), typed relation item summaries (`iref.<type>.item_count`, `iref.<type>.item_id`, `iref.<type>.item_out_edge_count`, `iref.<type>.item_in_edge_count`), draft relation-graph summaries (`iref.item_count`, `iref.from_item_unique_count`, `iref.to_item_unique_count`, row-wise `iref.item_id` + `iref.item_out_edge_count` + `iref.item_in_edge_count`), and `auxC`-based aux semantics (`aux.item_id`, `aux.semantic`, `aux.type`, `aux.subtype_hex`, `aux.subtype_kind`, `aux.subtype_text`, `aux.subtype_uuid`, `aux.subtype_u32`, `aux.subtype_u64`, `primary.auxl_semantic`, `primary.depth_item_id`, `primary.alpha_item_id`, ...).
  - `auxC` subtype interpretation now includes `ascii_z` and `u64be` kinds in addition to earlier numeric/FourCC/UUID/ASCII forms.
  - Parsing is intentionally bounded (depth/box count caps) and ignores unknown properties.
- JUMBF/C2PA decode (draft phase-3): `src/openmeta/jumbf_decode.cc`
  - Routed from container scan blocks tagged as `ContainerBlockKind::Jumbf`
    (BMFF `jumb`/C2PA hints and JXL `jumb` boxes).
  - Emits structural fields as `MetaKeyKind::JumbfField` (`box.*`, `c2pa.*`)
    and decoded CBOR keys as `MetaKeyKind::JumbfCborKey` (`*.cbor.*`).
  - Current CBOR path supports bounded definite and indefinite forms, with
    composite-key fallback naming (`k{map_index}_{
    major}`) and broader scalar
    decode coverage (simple values + half/float/double bit-preserving paths).
  - Draft semantic projection emits stable `c2pa.semantic.*` fields
    (`manifest_present`, `claim_present`, `assertion_present`,
    `signature_present`, `assertion_key_hits`, `cbor_key_count`,
    `signature_count`,
    `claim_generator` when ASCII-safe), plus draft per-claim fields
    (`claim_count`, `assertion_count`, `claim.{i}.prefix`,
    `claim.{i}.assertion_count`, `claim.{i}.key_hits`,
    `claim.{i}.signature_count`, `claim.{i}.signature_key_hits`,
    `claim.{i}.claim_generator` when ASCII-safe) and per-assertion fields
    (`claim.{i}.assertion.{j}.prefix`, `claim.{i}.assertion.{j}.key_hits`),
    plus draft per-claim signature fields
    (`claim.{i}.signature.{k}.prefix`, `claim.{i}.signature.{k}.key_hits`,
    `claim.{i}.signature.{k}.algorithm` when available), plus draft
    per-signature fields
    (`signature_count`, `signature_key_hits`, `signature.{k}.prefix`,
    `signature.{k}.key_hits`, `signature.{k}.algorithm` when available,
    `signature.{k}.reference_key_hits`,
    `signature.{k}.linked_claim_count`,
    `signature.{k}.cross_claim_link_count`,
    `signature.{k}.explicit_reference_present`,
    `signature.{k}.explicit_reference_resolved_claim_count`,
    `signature.{k}.explicit_reference_unresolved`,
    `signature.{k}.explicit_reference_ambiguous`,
    `signature.{k}.linked_claim.{m}.prefix`),
    plus reference-link counters
    (`reference_key_hits`, `cross_claim_link_count`,
    `explicit_reference_signature_count`,
    `explicit_reference_unresolved_signature_count`,
    `explicit_reference_ambiguous_signature_count`,
    `claim.{i}.referenced_by_signature_count`),
    and linkage counters (`signature_linked_count`,
    `signature_orphan_count`).
  - Draft verify scaffold (`c2pa.verify.*`) now includes:
    - signature-shape validation (`invalid_signature`) for malformed payloads;
    - OpenSSL-backed cryptographic verification (`verified` /
      `verification_failed`) when a signature entry provides algorithm +
      signing input + public key material (`public_key_der`/`public_key_pem` or
      `certificate_der`).
    - COSE_Sign1 support (array or embedded CBOR byte-string forms): extracts
      `alg` from protected headers, reconstructs Sig_structure signing bytes
      when payload is present, extracts `x5chain` from unprotected headers, and
      accepts raw ECDSA signatures (`r||s`) by converting to DER for OpenSSL.
    - detached payload resolution (`payload=null`) using explicit
      reference-linked candidates first (for example `claims[n]` / claim-label
      references in decoded claim/signature fields, scalar index references,
      and indexed array-element reference keys such as `claimRef[0]`), then
      including plural reference-key variants (`references`, `refs`,
      `claim_references`) plus hyphenated variants (`claim-reference`,
      `claim-uri`, `claim-ref-index`), nested URI-like map fields such as
      `references[].href`/`references[].link`, query-style index tokens in URI
      text (`claim-index=...`, `claim_ref=...`), and percent-encoded URI/label
      forms where present. Candidate ordering is deterministic with sorted
      index-like references resolved before sorted label-based references, then
      best-effort fallback probing via claim bytes, single-claim `claims[*]`
      arrays, nearby/nested claim JUMBF boxes, and additional cross-manifest
      candidates. Current tests include conflicting mixed references and
      multi-claim/multi-signature cross-manifest precedence cases, nested
      `references[]` map forms, duplicate overlapping explicit references,
      unresolved explicit-reference no-fallback behavior, conflict/consistent
      `index + claim_reference + href` nested-map ambiguity/consistency cases, and
      percent-encoded query-index URI variants.
    - draft profile checks (`profile_status`/`profile_reason`) from decoded
      `c2pa.semantic.*` shape fields (manifest/claim/signature linkage);
    - draft certificate trust checks (`chain_status`/`chain_reason`) when
      `certificate_der` is present (certificate parse, time validity, and
      OpenSSL trust-store verification).
    Full C2PA/COSE manifest binding and policy validation is still pending.
- GeoTIFF GeoKey decoding (derived keys): `src/openmeta/geotiff_decode.cc`
- Vendor MakerNote decoders: `src/openmeta/exif_makernote_*.cc`
  (Canon, Nikon, Sony, Olympus, Pentax, Casio, Panasonic, Kodak, Ricoh, Samsung, FLIR, etc.)
- Shared internal-only helpers: `src/openmeta/exif_tiff_decode_internal.h`
  (not installed; used to keep vendor logic out of the public API)
- Tests: `tests/makernote_decode_test.cc`
  and `tests/jumbf_decode_test.cc`

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

Optional CLI integration test for preview index suffixing:
```bash
cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/mnt/f/UBSd \
  -DOPENMETA_BUILD_TESTS=ON \
  -DOPENMETA_MULTI_PREVIEW_SAMPLE=/path/to/file_with_multiple_previews
cmake --build build-tests
ctest --test-dir build-tests -R openmeta_cli_preview_index --output-on-failure
```

If `OPENMETA_MULTI_PREVIEW_SAMPLE` is not set (or the file is missing),
`openmeta_cli_preview_index` is skipped.

Fast public smoke gate for `metavalidate` (self-contained, no corpus needed):
```bash
cmake --build build-tests --target openmeta_gate_metavalidate_smoke
ctest --test-dir build-tests -R openmeta_cli_metavalidate_smoke --output-on-failure
```

Fast public smoke gate for `metaread` safe-text placeholder behavior:
```bash
cmake --build build-tests --target openmeta_gate_metaread_safe_text_smoke
```

Fast public smoke gate for `metatransfer` thin wrapper behavior:
```bash
cmake --build build-tests --target openmeta_gate_metatransfer_smoke
ctest --test-dir build-tests -R openmeta_cli_metatransfer_smoke --output-on-failure
```

Fast public smoke gate for Python `openmeta.transfer_probe` thin wrapper
behavior (requires `-DOPENMETA_BUILD_PYTHON=ON`):
```bash
cmake --build build-tests --target openmeta_gate_python_transfer_probe_smoke
ctest --test-dir build-tests -R openmeta_python_transfer_probe_smoke --output-on-failure
```

Fast public smoke gate for Python `openmeta.python.metatransfer` edit mode
behavior (requires `-DOPENMETA_BUILD_PYTHON=ON`):
```bash
cmake --build build-tests --target openmeta_gate_python_metatransfer_edit_smoke
ctest --test-dir build-tests -R openmeta_python_metatransfer_edit_smoke --output-on-failure
```

Coverage note:
- Public tree tests focus on deterministic unit/fuzz/smoke behavior.
- Corpus-scale compare/baseline workflows are external to the public tree and
  should be run in your CI/release validation pipeline.

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

Public seed corpus is available in-tree:

```bash
mkdir -p build-fuzz/_corpus_out
ASAN_OPTIONS=detect_leaks=0 ./build-fuzz/openmeta_fuzz_container_scan \
  build-fuzz/_corpus_out \
  tests/fuzz/corpus/container_scan \
  -runs=1000
```

The `container_scan` seed set includes BMFF `iloc` method-2 edge cases:
- valid `iref` v1 (`32-bit` item-id) resolution,
- missing `iref` mapping,
- out-of-range explicit `extent_index`,
- `idx_size=0` extent/reference mismatch fallback behavior.

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
- `openmeta.validate(...)` is the library-backed validation API used by
  `openmeta.python.metavalidate`; it returns decode/CCM issue summaries without
  Python-side validation logic.
- Python bindings are thin wrappers over C++ decode logic. Resource/safety
  limits should be configured via `openmeta.ResourcePolicy` and passed to
  `openmeta.read(...)`.

Example policy usage:
```bash
PYTHONPATH=build-py/python python3 - <<'PY'
import openmeta
policy = openmeta.ResourcePolicy()
policy.max_file_bytes = 0
policy.exif_limits.max_total_entries = 200000
doc = openmeta.read("file.jpg", policy=policy)
print(doc.entry_count)
PY
```

C++ policy setup:
```cpp
#include "openmeta/resource_policy.h"

openmeta::OpenMetaResourcePolicy policy
    = openmeta::recommended_resource_policy();
policy.jumbf_limits.max_box_depth = 24;  // optional override
```

JUMBF preflight depth estimate (before full decode):
```cpp
#include "openmeta/jumbf_decode.h"

const openmeta::JumbfStructureEstimate est
    = openmeta::measure_jumbf_structure(bytes, policy.jumbf_limits);
if (est.status == openmeta::JumbfDecodeStatus::LimitExceeded) {
    // reject or route to stricter handling
}
```

Other preflight estimate entry points follow the same limit model:
```cpp
#include "openmeta/container_scan.h"
#include "openmeta/exif_tiff_decode.h"
#include "openmeta/exr_decode.h"
#include "openmeta/icc_decode.h"
#include "openmeta/iptc_iim_decode.h"
#include "openmeta/jumbf_decode.h"
#include "openmeta/photoshop_irb_decode.h"
#include "openmeta/xmp_decode.h"

const openmeta::ScanResult scan_est
    = openmeta::measure_scan_auto(file_bytes);
const openmeta::ExifDecodeResult exif_est
    = openmeta::measure_exif_tiff(exif_bytes, exif_options);
const openmeta::XmpDecodeResult xmp_est
    = openmeta::measure_xmp_packet(xmp_bytes, xmp_options);
const openmeta::IccDecodeResult icc_est
    = openmeta::measure_icc_profile(icc_bytes, icc_options);
const openmeta::IptcIimDecodeResult iptc_est
    = openmeta::measure_iptc_iim(iptc_bytes, iptc_options);
const openmeta::PhotoshopIrbDecodeResult irb_est
    = openmeta::measure_photoshop_irb(irb_bytes, irb_options);
const openmeta::ExrDecodeResult exr_est
    = openmeta::measure_exr_header(exr_bytes, exr_options);
const openmeta::JumbfDecodeResult jumbf_est
    = openmeta::measure_jumbf_payload(jumbf_bytes, jumbf_options);
```

Example scripts (repo tree):
```bash
PYTHONPATH=build-py/python python3 -m openmeta.python.openmeta_stats file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metaread file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metavalidate file.dng
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump file.jpg output.xmp
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump --format portable file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump --format portable --portable-exiftool-gpsdatetime-alias file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump --format portable --c2pa-verify --c2pa-verify-backend auto file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metadump --format portable --portable-include-existing-xmp --xmp-sidecar file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer file.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer --target-jpeg target.jpg -o edited.jpg source.jpg
PYTHONPATH=build-py/python python3 -m openmeta.python.metatransfer --target-tiff target.tif --dry-run source.jpg
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
python3 -m openmeta.python.metatransfer file.jpg
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
Python helper scripts `metaread.py`, `metavalidate.py`, `metadump.py`, `metatransfer.py`,
and `openmeta_stats.py`
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
- `Document.unsafe_oiio_attributes(...)`
- `Document.oiio_attributes_typed(...)`
- `Document.unsafe_oiio_attributes_typed(...)`
- `Document.ocio_metadata_tree(...)`
- `Document.unsafe_ocio_metadata_tree(...)`
- `Document.dump_xmp_sidecar(format=...)`

Current C++ adapter entry points:

- `openmeta/oiio_adapter.h`:
  - safe API: `collect_oiio_attributes_safe(..., InteropSafetyError*)`
  - unsafe API: `collect_oiio_attributes(...)`
  - `collect_oiio_attributes(..., const OiioAdapterRequest&)` (stable flat request API)
  - `collect_oiio_attributes(..., const OiioAdapterOptions&)` (advanced/legacy shape)
  - safe typed API: `collect_oiio_attributes_typed_safe(..., InteropSafetyError*)`
  - unsafe typed API: `collect_oiio_attributes_typed(...)`
  - `collect_oiio_attributes_typed(..., const OiioAdapterRequest&)` (typed values)
  - `collect_oiio_attributes_typed(..., const OiioAdapterOptions&)` (typed values)
  - typed payload model: `OiioTypedValue` / `OiioTypedAttribute`

Python typed behavior:
- `Document.oiio_attributes(...)` is safe-by-default and raises on unsafe raw
  byte payloads; use `Document.unsafe_oiio_attributes(...)` for legacy/raw
  fallback output.
- `Document.oiio_attributes_typed(...)` decodes text values to Python `str` in
  safe mode and raises on unsafe/invalid text bytes.
- `Document.unsafe_oiio_attributes_typed(...)` returns raw text bytes for
  explicit unsafe workflows.
- `Document.ocio_metadata_tree(...)` is safe-by-default and raises on unsafe
  raw byte payloads; use `Document.unsafe_ocio_metadata_tree(...)` for
  legacy/raw fallback output.
- `openmeta/ocio_adapter.h`:
  - safe API: `build_ocio_metadata_tree_safe(..., InteropSafetyError*)`
  - unsafe API: `build_ocio_metadata_tree(...)`
  - `build_ocio_metadata_tree(..., const OcioAdapterRequest&)` (stable flat request API)
  - `build_ocio_metadata_tree(..., const OcioAdapterOptions&)` (advanced/legacy shape)

Current C++ sidecar entry points:

- `openmeta/xmp_dump.h`:
  - `dump_xmp_sidecar(..., const XmpSidecarRequest&)` (stable flat request API)
  - `dump_xmp_sidecar(..., const XmpSidecarOptions&)` (advanced/legacy shape)

Draft C++ transfer entry points (prepare/emit scaffold):

- `openmeta/metadata_transfer.h`:
  - `PreparedTransferBundle` (target-ready payload container)
  - backend emitter contracts:
    - `JpegTransferEmitter`
    - `TiffTransferEmitter`
    - `JxlTransferEmitter`
    - `ExrTransferEmitter`
  - `prepare_metadata_for_target(..., PreparedTransferBundle*)` currently
    prepares JPEG/TIFF transfer blocks: EXIF APP1 (JPEG), XMP (JPEG APP1 /
    TIFF tag 700), ICC (JPEG APP2 / TIFF tag 34675), IPTC (JPEG APP13 / TIFF
    tag 33723), with explicit warnings for unsupported/skipped entries.
  - `emit_prepared_bundle_jpeg(...)` is implemented for route-based JPEG marker
    emission (`jpeg:appN...`, `jpeg:com`).
  - `emit_prepared_bundle_tiff(...)` is implemented for route-based TIFF tag
    emission (`tiff:ifd-exif-app1`, `tiff:tag-700-xmp`, `tiff:tag-34675-icc`,
    `tiff:tag-33723-iptc`) and commit hook.
  - Current CLI TIFF rewrite path supports classic TIFF (little- and
    big-endian) for ExifIFD materialization (`tiff:ifd-exif-app1`).
  - `compile_prepared_bundle_jpeg(...)` + `emit_prepared_bundle_jpeg_compiled(...)`
    provide route-compile + reusable emit plan for high-throughput
    "prepare once, emit many" use.
  - `compile_prepared_bundle_tiff(...)` + `emit_prepared_bundle_tiff_compiled(...)`
    provide the same reusable route-compile emit plan for TIFF tag emission.
  - `apply_time_patches(...)` applies fixed-width in-place updates over
    `bundle.time_patch_map` (for example EXIF `DateTime*`, `SubSec*`,
    `OffsetTime*`, GPS date/time slots) without full re-prepare.
  - TIFF edit path mirrors JPEG edit path:
    - `plan_prepared_bundle_tiff_edit(...)`
    - `apply_prepared_bundle_tiff_edit(...)`
    (classic TIFF rewrite for prepared EXIF/XMP/ICC/IPTC updates).
  - Writer/sink edit path is available for both targets:
    - `TransferByteWriter`
    - `SpanTransferByteWriter`
    - `PreparedTransferExecutionPlan`
    - `TimePatchView`
    - `write_prepared_bundle_jpeg(...)`
    - `write_prepared_bundle_jpeg_compiled(...)`
    - `write_prepared_bundle_jpeg_edit(...)`
    - `write_prepared_bundle_tiff_edit(...)`
    - `apply_time_patches_view(...)`
    - `compile_prepared_transfer_execution(...)`
    - `execute_prepared_transfer_compiled(...)`
    - `write_prepared_transfer_compiled(...)`
    - `emit_prepared_transfer_compiled(..., JpegTransferEmitter&)`
    - `emit_prepared_transfer_compiled(..., TiffTransferEmitter&)`
    - `ExecutePreparedTransferOptions::emit_output_writer`
    - `ExecutePreparedTransferOptions::edit_output_writer`
    JPEG can stream either metadata-only emit bytes or edited output directly.
    TIFF edit output streams original input plus a planned metadata tail,
    avoiding a temporary full-file rewrite buffer.
  - `prepare_metadata_for_target_file(...)` provides the file-level
    `read/decode -> prepare bundle` step.
  - `execute_prepared_transfer(...)` runs the shared
    `time_patch -> compile -> emit -> optional edit` flow on an already
    prepared bundle.
  - `compile_prepared_transfer_execution(...)` compiles a reusable execution
    plan that stores target-specific route mapping plus emit policy.
  - `apply_time_patches_view(...)` accepts non-owning patch spans for
    per-frame patching without owned update buffers.
  - `execute_prepared_transfer_compiled(...)` runs the same shared
    `time_patch -> emit -> optional edit` flow using a precompiled execution
    plan.
  - `write_prepared_transfer_compiled(...)` is the narrow encoder-integration
    helper for `prepare once -> compile once -> patch -> write` workflows.
  - `SpanTransferByteWriter` is the fixed-buffer adapter for encoder paths that
    want preallocated output memory and deterministic overflow reporting before
    any JPEG marker bytes are written.
  - `emit_prepared_transfer_compiled(..., TiffTransferEmitter&)` is the
    intended TIFF hot path; TIFF does not expose a metadata-only byte-writer
    emit contract.
  - `execute_prepared_transfer_file(...)` wraps the full
    `read/decode -> prepare -> execute` flow and is now the main thin-wrapper
    entry point for CLI/Python tooling.

Python transfer entry point:

- `openmeta.transfer_probe(...)` (safe):
  - calls the same file-level transfer execution API as the CLI,
    returning read/prepare/compile/emit summaries and prepared block
    routes/sizes;
  - supports `time_patches={Field: "Value" | b"..."}`
    with shared C++ patch logic inside
    `execute_prepared_transfer(...)`;
  - exposes `time_patch_*` summary fields
    (`time_patch_status_name`, `time_patch_patched_slots`, ...);
  - if `include_payloads=True`, returns
    `overall_status=unsafe_data` with `error_code=unsafe_payloads_forbidden`.
- `openmeta.unsafe_transfer_probe(...)`:
  - same probe contract, but allows `include_payloads=True` and returns raw
    payload bytes (`bytes`) in `blocks[i].payload`.
  - intended for explicit raw/unsafe workflows only.

Transfer probe contract hardening (stable machine fields):
- `overall_status`, `overall_status_name`
- `error_stage` (`none|api|file|prepare|emit`)
- `error_code`, `error_message`
- stage-specific stable code enums/strings:
  - file: `PrepareTransferFileCode` / `file_code_name`
  - prepare: `PrepareTransferCode` / `prepare_code_name`
  - emit: `EmitTransferCode` / `emit_code_name`

Current adapter/name-policy behavior:

- `ExportNamePolicy::ExifToolAlias` applies compatibility aliases for
  interop-name parity workflows.
- `ExportNamePolicy::Spec` preserves spec/native names.
- OIIO adapter keeps numeric unknown names (for example `Exif_0x....`) even
  when value formatting is empty, and keeps `Exif:MakerNote` in spec mode.
- When DNG context is detected (`DNGVersion` present in the same IFD), DNG
  color/CCM tags are exported with dedicated adapter namespaces:
  `dng:*` (portable) and `DNG:*` (OIIO).
- ICC entries are exported with adapter-friendly names:
  `icc:*` (portable) and `ICC:*` (OIIO), alongside canonical `icc:header:*`
  / `icc:tag:*` naming.

Adapter-focused tests (public tree):

```bash
cmake --build build-tests --target openmeta_tests
./build-tests/openmeta_tests --gtest_filter='InteropExport.*:OiioAdapter.*:OcioAdapter.*'
./build-tests/openmeta_tests --gtest_filter='CrwCiffDecode.*'
```

Notes:
- `CrwCiffDecode` tests cover CRW/CIFF derived EXIF mapping for legacy Canon RAW.
- `OiioAdapter` tests cover stable handling of empty-value attributes needed by
  interop parity workflows.

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
