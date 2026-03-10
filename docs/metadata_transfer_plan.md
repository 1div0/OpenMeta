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
  - `metatransfer` / `openmeta.transfer_probe(...)` can now accept explicit
    `keep|drop|invalidate|rewrite` policy selections for MakerNote/JUMBF/C2PA
    and report the resolved decisions back to the caller
  - C2PA decisions now carry an explicit structured contract:
    - `TransferC2paMode`
    - `TransferC2paSourceKind`
    - `TransferC2paPreparedOutput`
  - `PreparedTransferBundle::c2pa_rewrite` now carries the separate
    future-signing contract for `c2pa=rewrite`:
    - `TransferC2paRewriteState`
    - source kind
    - matched decoded-entry count
    - existing carrier segment count
    - signer prerequisites
    - JPEG `content_binding_chunks` for the rewrite-without-C2PA byte stream
  - MakerNote `Drop` is active in the EXIF prepare path
  - File-based JPEG prepare can now preserve source JUMBF payloads by repacking
    them into APP11 segments
  - Store-only JPEG prepare can now project decoded non-C2PA
    `JumbfCborKey` roots into generic APP11 JUMBF payloads
  - `append_prepared_bundle_jpeg_jumbf(...)` now provides the first explicit
    public raw JUMBF -> JPEG APP11 serializer path for prepared bundles
  - `c2pa=invalidate` for JPEG targets now emits a draft unsigned APP11 C2PA
    invalidation payload instead of drop-only behavior
  - The generated draft invalidation payload now carries an OpenMeta contract
    marker and contract version
  - File-based JPEG prepare can preserve an existing OpenMeta draft unsigned
    invalidation payload as raw APP11 C2PA
  - `c2pa=rewrite` now resolves to `Drop` with explicit
    `SignedRewriteUnavailable`
  - File-based JPEG prepare now records rewrite prerequisites even when
    signed rewrite is unavailable, so a future signer can consume the same
    prepared contract without another API change
  - The JPEG rewrite prep path now emits deterministic content-binding chunks
    as preserved source ranges plus prepared JPEG segments
  - `build_prepared_c2pa_sign_request(...)` now derives the explicit external
    signer request from those rewrite chunks without another bundle-level API
    addition
  - `build_prepared_c2pa_sign_request_binding(...)` now materializes the exact
    content-binding bytes for that request from preserved source ranges plus
    prepared JPEG segments
  - Thin wrappers now expose that byte stream directly:
    `metatransfer --dump-c2pa-binding` and Python
    `unsafe_transfer_probe(include_c2pa_binding_bytes=True)`
  - `build_prepared_c2pa_handoff_package(...)` now bundles the external signer
    request and exact binding bytes into one public handoff object
  - `serialize_prepared_c2pa_handoff_package(...)` and
    `deserialize_prepared_c2pa_handoff_package(...)` now persist that handoff
    object
  - `build_prepared_c2pa_signed_package(...)` now bundles the external signer
    request and returned signer material into one persisted signed package
  - `serialize_prepared_c2pa_signed_package(...)` and
    `deserialize_prepared_c2pa_signed_package(...)` now persist that signed
    package
  - Python thin wrappers now expose the same persisted package flow:
    `openmeta.unsafe_transfer_probe(include_c2pa_handoff_bytes=True)`,
    `openmeta.unsafe_transfer_probe(include_c2pa_signed_package_bytes=True)`,
    and `openmeta.python.metatransfer --dump-c2pa-handoff /
    --dump-c2pa-signed-package / --load-c2pa-signed-package`
  - Signed C2PA staging now requires both:
    - structural carrier validity
    - semantic manifest/claim/signature consistency from decoded
      `c2pa.semantic.*` fields before JPEG APP11 staging succeeds
    - request-aware manifest count / `claim_generator` checks before the
      returned payload can drift away from the prepared sign request
    - at least one decoded assertion when the prepared request requires
      content binding
    - the primary signature linking back to the prepared primary claim under
      that same content-binding contract
    - rejection of primary-signature explicit references that resolve to
      multiple claims under the prepared sign request
    - rejection of multi-signature drift where the prepared primary claim is
      referenced by more than one signature under the current sign request
    - rejection of extra linked signatures beyond the prepared sign request,
      even when the primary claim/signature pair still looks valid
    - manifest/claim/signature projection shape checks under the prepared
      manifest contract
    - exact primary manifest-CBOR equality against the external signer’s
      `manifest_builder_output`
  - `validate_prepared_c2pa_sign_result(...)` now validates a returned signed
    logical C2PA payload before bundle mutation and reports staged carrier
    bytes and segment count
  - JPEG signed-payload validation now also checks APP11 sequence order,
    repeated-header consistency, root-type validity, BMFF declared-size
    consistency, and exact logical-payload reconstruction before final
    emit/write
  - Final JPEG emit/write now also validates prepared APP11 C2PA carriers
    against the resolved bundle contract:
    - required carriers may not be missing
    - draft invalidation output may not carry a content-bound signed-rewrite
      payload
    - signed rewrite output may not carry a draft invalidation payload
    - `TransferC2paRewriteState::Ready` may not appear without
      `TransferC2paPreparedOutput::SignedRewrite`
  - `apply_prepared_c2pa_sign_result(...)` now stages externally signed
    content-bound logical C2PA payloads back into prepared JPEG APP11 blocks
    after strict request/material validation
  - `execute_prepared_transfer_file(...)` and the thin CLI/Python wrappers can
    now carry optional signer material, validate signed C2PA output, stage it,
    and continue through normal JPEG emit/edit execution in the same
    high-level flow
  - JPEG edit/rewrite now treats existing APP11 JUMBF/C2PA as managed routes:
    content-changing edits drop stale C2PA, and explicit JUMBF `Drop` removes
    existing APP11 JUMBF segments
  - JPEG edit plans now report how many existing managed APP11 segments will be
    removed, including separate counts for JUMBF and C2PA
  - OpenMeta still does not sign or re-sign internally; external signer output
    can now be staged back into the prepared bundle contract

## Main Blockers For Transfer

1. No general target-agnostic streaming/container-packaging API yet beyond the
   current JPEG/TIFF final-output package plan.
   Metadata-only TIFF byte-writer emit and broader target families are still
   missing.
2. C2PA still has no real preserve/re-sign path; JPEG now has a draft
   invalidation payload path, but full signed-manifest rewrite/re-sign is still
   missing.
3. JUMBF transfer now has three draft paths for JPEG:
   file-based APP11 preserve, explicit raw logical append, and a bounded
   `MetaStore` projection path from decoded non-C2PA CBOR roots. General
   target-agnostic JUMBF serialization is still incomplete.
4. No deterministic conflict/precedence contract yet for EXIF/IPTC/XMP
   remap in slow-path transfer mode.
5. No target-family write adapters yet beyond the current JPEG/TIFF draft path.

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
- `build_prepared_bundle_jpeg_package(input, bundle, jpeg_plan, out_plan)`
- `build_prepared_bundle_tiff_package(input, bundle, tiff_plan, out_plan)`
- `write_prepared_transfer_package(input, bundle, package_plan, writer)`
  - Shared final-output package contract for current JPEG/TIFF rewrite paths.
  - Exposes deterministic source ranges, prepared JPEG segments, and inline
    generated bytes through one public chunk plan.
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
- JUMBF: file-based JPEG transfer can preserve raw source payloads by
  repacking them into APP11 segments, and JPEG rewrite/edit removes existing
  APP11 JUMBF segments when the resolved policy is `Drop`.
- C2PA: `Invalidate` on JPEG now emits a draft unsigned APP11 C2PA
  invalidation payload, and JPEG content-changing rewrite/edit removes
  existing APP11 C2PA from the target before inserting the new prepared
  payload. Preserve/re-sign and signed rewrite remain future work.

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
