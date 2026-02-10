# Security Policy

OpenMeta treats **all file metadata as untrusted input**. Image/container files
and their metadata blocks are a common attack surface (memory corruption,
resource exhaustion, and output/log injection).

## Threat Model

Assume inputs may be maliciously crafted to:
- trigger out-of-bounds reads/writes via bad offsets/sizes/counts,
- cause integer overflows (e.g., `count * element_size`),
- create cycles/recursion in pointer structures (IFD loops),
- force unbounded allocations or decompression bombs ("zip bombs"),
- inject control sequences or confusing Unicode into console/logs/exports.

## Parsing Safety Requirements

All parsers/decoders must:
- Validate **every** offset/length against the available buffer before reading.
- Guard multiplications/additions against overflow and validate computed ranges.
- Enforce explicit resource limits (entries/blocks/value bytes) and fail "soft"
  (skip entry / mark status) rather than crashing.
- Avoid recursion; prefer iterative decoding with visited-set loop detection.
- Preserve unknown/invalid payloads losslessly as `bytes` (do not assume C-strings).

EXIF/TIFF decoding uses `ExifDecodeLimits` to cap IFD count, entry count, and
total value bytes.

## Output & Export Safety Requirements

Tools and exporters must never emit raw metadata bytes without sanitization.

Console (`metaread`) rules:
- Print **ASCII-only** output. Non-ASCII and control bytes must be escaped
  (e.g. `\\x1B`, `\\xE2\\x80\\xAE`) to prevent terminal injection and display spoofing.
- Apply strict size limits for printing (`--max-bytes`, `--max-elements`,
  `--max-cell-chars`) and for reading whole files (`--max-file-bytes`).
- If sanitization or truncation is triggered, annotate the value (e.g.
  `(DANGEROUS)`) so users don't mistake it for authoritative text.

Structured exports (JSON/XML/XMP/etc.) rules:
- Escape per-format (JSON string escaping; XML entity escaping) and never embed
  untrusted text into markup/attributes without escaping.
- Prefer explicit encodings for binary fields (hex/base64) instead of "best effort" text.
- Preserve provenance: mark values that were lossy-sanitized or truncated.

## Testing & Fuzzing (Required)

Before merging changes that touch parsing, decoding, or exports:
- Build and run unit tests (`OPENMETA_BUILD_TESTS=ON`) with sanitizers when possible.
- Run libFuzzer targets (`OPENMETA_BUILD_FUZZERS=ON`) under ASan/UBSan for a
  reasonable time budget and keep corpus/regressions.
- Run FuzzTest targets (`OPENMETA_BUILD_FUZZTEST=ON`) when enabled in your environment.

Unit tests require GoogleTest to be discoverable via `find_package(GTest CONFIG)`
or `CMAKE_PREFIX_PATH`.

Example workflow (Linux/Clang):
```bash
cmake -S . -B build-tests -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOPENMETA_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure

cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug -DOPENMETA_BUILD_FUZZERS=ON
cmake --build build-fuzz
./build-fuzz/openmeta_fuzz_exif_tiff_decode -max_total_time=60
```

## Reporting

If you find a security issue, please provide a minimal reproducer file (or
hex snippet), build flags, and stack trace. Avoid publishing exploit details
until a fix is available.
