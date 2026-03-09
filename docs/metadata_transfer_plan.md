# Metadata Transfer Plan (Draft)

Date: March 5, 2026

Related:
- `docs/metadata_backend_matrix.md` (backend capability matrix and unified
  writer contract draft)

## Goal

Implement a first write milestone for metadata transfer:

- Source: camera RAW and other supported inputs
- Target: TIFF and JPEG
- Mode: no-edits metadata transfer (preserve semantics as-is where possible)

This plan is for metadata-only transfer. Pixel encode/decode policy is out of scope.

## Performance Requirement (Primary)

Metadata transfer must not bottleneck high-throughput pipelines (machine-vision
and GPU encode pipelines at high FPS).

Design rule:
- Do expensive work once (`prepare`), then reuse prebuilt metadata blocks for
  many frames (`emit`).
- Per-frame work should be reduced to optional timestamp patching plus final
  container write calls.

## Current Status Snapshot

Read-path readiness is high:

- EXIF read gates are green on tracked HEIC/HEIF, CR3, and RAW corpora.
- MakerNote tag-id coverage is at 100% on tracked ExifTool sample-image baselines.
- EXR header metadata interop is at 100% for name/type/value-class checks.
- Portable XMP sidecar export is stable with high fidelity and 100% parse/roundtrip rates in baseline gates.
- Draft C2PA semantic gates are stable (invariant checks passing), including new BMFF edge fixtures.
- Draft transfer API scaffold is available in `openmeta/metadata_transfer.h`
  (`PreparedTransferBundle` + backend emitter contracts).
- Route-based bundle emission is implemented for both targets:
  - JPEG: `emit_prepared_bundle_jpeg(...)`
  - TIFF: `emit_prepared_bundle_tiff(...)`
- Reusable compiled emit plans are implemented for both targets:
  - `compile_prepared_bundle_jpeg(...)` +
    `emit_prepared_bundle_jpeg_compiled(...)`
  - `compile_prepared_bundle_tiff(...)` +
    `emit_prepared_bundle_tiff_compiled(...)`
- High-level file wrapper is implemented:
  `prepare_metadata_for_target_file(...)` (`read/decode -> prepare bundle`).
- Shared execution helpers are implemented:
  - `execute_prepared_transfer(...)`
  - `compile_prepared_transfer_execution(...)`
  - `execute_prepared_transfer_compiled(...)`
  - `execute_prepared_transfer_file(...)`
  These now cover both `time_patch -> compile -> emit -> optional edit` and
  `prepare once -> compile once -> patch/emit many`.
- Streaming edit sinks are implemented in the shared core API:
  - `TransferByteWriter`
  - `SpanTransferByteWriter`
  - `PreparedTransferExecutionPlan`
  - `TimePatchView`
  - `write_prepared_bundle_jpeg(...)`
  - `write_prepared_bundle_jpeg_compiled(...)`
  - `write_prepared_bundle_jpeg_edit(...)`
  - `write_prepared_bundle_tiff_edit(...)`
  - `apply_time_patches_view(...)`
  - `write_prepared_transfer_compiled(...)`
  - `emit_prepared_transfer_compiled(..., JpegTransferEmitter&)`
  - `emit_prepared_transfer_compiled(..., TiffTransferEmitter&)`
  - `ExecutePreparedTransferOptions::emit_output_writer`
  - `ExecutePreparedTransferOptions::edit_output_writer`
  JPEG can stream both metadata-only emit bytes and edited output directly to a
  sink. TIFF edit output streams original input plus a planned metadata tail,
  avoiding a temporary full-file rewrite buffer.
- Thin-wrapper tooling is in place:
  - C++ CLI: `metatransfer`
  - Python binding helper: `openmeta.transfer_probe(...)`
  - Python script: `openmeta.python.metatransfer`
- Core edit planning/apply APIs now cover both targets:
  - JPEG: `plan_prepared_bundle_jpeg_edit(...)` /
    `apply_prepared_bundle_jpeg_edit(...)`
  - TIFF: `plan_prepared_bundle_tiff_edit(...)` /
    `apply_prepared_bundle_tiff_edit(...)`
- JPEG prepare path now emits APP1 EXIF + APP1 XMP + APP2 ICC + APP13 IPTC
  payload bundles from `MetaStore`, with explicit warnings for
  unsupported/skipped EXIF/ICC/IPTC entries.
- Transfer policy contract is now explicit in the public API:
  - `TransferProfile::{makernote,jumbf,c2pa}` use `TransferPolicyAction`
  - `PreparedTransferBundle::policy_decisions` records resolved prepare-time
    decisions and reasons
  - MakerNote `Drop` is active in the EXIF prepare path
  - JUMBF/C2PA currently resolve to explicit drop decisions for JPEG/TIFF
    prepare because those targets do not yet serialize them in the transfer
    pack path

## Main Blockers For Transfer

1. No general streaming/container-packaging API yet for zero-copy or
   chunked output assembly beyond the current edit-output sink path.
2. JUMBF/C2PA transfer policy is decision-only today; target serialization,
   rewrite, and invalidation paths are still missing.
3. No deterministic conflict/precedence contract yet for EXIF/IPTC/XMP
   remap in slow-path transfer mode.
4. No target-family write adapters yet beyond the current JPEG/TIFF draft path.

## Runtime Model

Two paths:

1. Fast path (default)
- No semantic remap.
- No full EXIF rebuild per frame.
- Reuse prebuilt target-ready blocks.

2. Slow path (explicit)
- Full normalize/remap/re-encode when requested.

## Proposed API Direction (Draft)

- `prepare_metadata_for_target(source_store, target_format, profile)`
  - Output: immutable `PreparedTransferBundle` + diagnostics.
- `compile_prepared_transfer_execution(bundle, emit_options, out_plan)`
  - Compiles target-specific route mapping once and stores emit policy in a
    reusable execution plan.
- `apply_time_patches_view(bundle, patch_views, time_patch_options)`
  - Applies non-owning fixed-width patch views without owned patch buffers.
- `execute_prepared_transfer(bundle, edit_target?, options)`
  - Applies optional frame patch data.
  - Compiles and runs the shared core execution path in one call.
  - Can stream JPEG metadata emit bytes and JPEG/TIFF edited output through a
    `TransferByteWriter` sink.
- `execute_prepared_transfer_compiled(bundle, plan, edit_target?, options)`
  - Reuses a precompiled execution plan for repeated emit/edit work.
- `write_prepared_transfer_compiled(bundle, plan, writer, patch_views, ...)`
  - Narrow hot-path helper for `prepare once -> compile once -> patch -> write`.
  - `SpanTransferByteWriter` provides the preallocated fixed-buffer adapter for
    encoder integration without a custom writer subclass, with deterministic
    overflow rejection before JPEG emit writes begin.
- `emit_prepared_transfer_compiled(bundle, plan, backend, patch_views, ...)`
  - Direct backend-emitter hot path for JPEG/TIFF integrations.
  - TIFF intentionally uses this backend path or rewrite/edit, not a
    metadata-only byte-writer emit API.
- `execute_prepared_transfer_file(source_path, options)`
  - Thin-wrapper entry point for CLI/Python.

Bundle contents (conceptual):
- Target-ready EXIF payload.
- Target-ready XMP packet(s).
- Target-ready ICC/IPTC/IRB payload(s).
- Optional JUMBF/C2PA payload(s) according to policy.
- `TimePatchMap` (offsets and expected lengths for patchable fields).

## V1 Scope (No-Edits)

For TIFF/JPEG targets:

- EXIF: reserialize for target container, rebuild offsets, preserve known-safe tags.
- XMP: preserve packet, repackage for target container limits.
- ICC: preserve payload, repackage for target container limits.
- IPTC/IRB: preserve payload, repackage where target supports it.
- MakerNote: preserve raw bytes by default; explicit drop policy is active in
  the current prepare path.
- JUMBF/C2PA: current JPEG/TIFF prepare records explicit drop decisions, but
  does not yet serialize or invalidate/rewrite these payloads.

## Time Patch Plan (V1)

Support per-frame capture time injection without full EXIF rebuild.

Approach:
- Build EXIF template once with fixed-width slots.
- Record patch offsets in `TimePatchMap`.
- Patch only specific bytes per frame.

Patchable EXIF fields:
- `DateTime` (`0x0132`) as `YYYY:MM:DD HH:MM:SS\0` (fixed 20 bytes).
- `DateTimeOriginal` (`0x9003`) fixed 20 bytes.
- `DateTimeDigitized` (`0x9004`) fixed 20 bytes.
- `SubSecTime` (`0x9290`), `SubSecTimeOriginal` (`0x9291`),
  `SubSecTimeDigitized` (`0x9292`) for milliseconds (for example `"123"`).
- `OffsetTime` (`0x9010`), `OffsetTimeOriginal` (`0x9011`),
  `OffsetTimeDigitized` (`0x9012`) as `+HH:MM`/`-HH:MM` (fixed 7 bytes including NUL).
- Optional UTC pair:
  - `GPSDateStamp` (`YYYY:MM:DD\0`)
  - `GPSTimeStamp` (RATIONAL triplet: h/m/s with fixed denominator policy).

Important:
- Milliseconds are stored in `SubSecTime*`, not in `DateTime*`.
- Patch path must validate expected slot size before write.
- If a required slot is absent, emit diagnostic and follow profile behavior
  (`warn`, `fail`, or `skip`).

## Proposed Implementation Plan

1. Contract and policy layer
- Keep `TransferProfile` action-based and extend it only when new target
  serializers can honor a policy directly.
- Define deterministic precedence and conflict behavior for overlapping
  families (`JUMBF` vs `C2PA`, carrier-disabled EXIF, content-changing edits).

2. Block preparation API
- Add target-agnostic API:
  - `prepare_metadata_for_target(source, target_format, profile)`.
- Output prepared container-ready block payloads plus diagnostics.

3. EXIF serializers (first hard dependency)
- Implement EXIF->JPEG APP1 serializer.
- Implement EXIF->TIFF serializer.
- Add strict tag filtering for target-layout-only pointers/offset tags.

4. XMP/ICC/IPTC packagers
- Add JPEG chunking/splitting rules.
- Add TIFF tag payload packagers.

5. Writer adapters
- JPEG writer adapter (inject prepared APP blocks).
- TIFF writer adapter (set prepared metadata payloads).

6. Transfer gates
- Add corpus gates: source -> transfer -> target -> compare.
- Validate block presence, critical tag value parity, and no malformed outputs.

7. Time patch implementation and gates
- Implement `TimePatchMap` generation during EXIF prepare.
- Add `apply_time_patch(bundle_mutable_view, capture_time)` API.
- Add tests for:
  - fixed-width patch safety,
  - timezone and subsec formatting,
  - GPS timestamp optional mode,
  - multi-thread parallel patch usage with per-thread buffers.

## Concurrency Model

- `PreparedTransferBundle` is immutable and shared across threads.
- Per-thread mutable emit buffer/view is used for patching and write call
  integration.
- No global locks in fast path.
- No structural reallocations in emit path.

## Success Criteria (V1)

Functional:
- Source metadata transfers to TIFF/JPEG with expected block presence.
- EXIF core time fields can be patched per frame.
- Safety checks reject malformed patch operations.

Performance:
- Prepare cost amortized over many frames.
- Per-frame metadata path is constant-time relative to metadata size class
  (bounded patch + write call overhead).
- Meets pipeline target where metadata path is a small fraction of frame budget.

## Postponed Tasks (Explicit)

Not required for V1 no-edits transfer:

- Full C2PA conformance verification.
- Advanced ICC interpretation parity.
- Advanced CCM normalization/validation parity.
- Full EXIF/IPTC/XMP sync engine and merge heuristics.
- Fuzzy metadata search/query UX.

## Discussion Items

1. When content changes, should C2PA default to explicit invalidate or always drop?
2. What target families should gain first-class JUMBF/C2PA serializers first
   (JPEG APP11, JXL boxes, BMFF)?
3. Strictness profile defaults for transfer diagnostics: warning vs hard-fail.
4. How much of transfer-policy diagnostics should be surfaced directly in
   CLI/Python thin wrappers.
