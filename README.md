# OpenMeta

OpenMeta is a metadata processing library.

Current focus: **safe, format-agnostic reads** - locate metadata blocks in
common containers and decode common metadata payloads into a normalized
in-memory model.

## Status

Read-path support is broad and actively regression-gated. API surface for
write/edit workflows is still draft in some areas, so expect targeted breaking
changes as those paths stabilize.

### Read Coverage Snapshot

Current baseline-gated status on tracked corpora:
- HEIC/HEIF, CR3, and mixed RAW EXIF tag-id compare gates are passing.
- EXR header metadata compare gate is passing (name/type/value-class contract).
- Portable and lossless sidecar export paths are covered by baseline and smoke
  gates.
- MakerNote decode is baseline-gated with broad vendor support (unknown tags are
  preserved losslessly when no structured mapping exists).

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
    properties (`pitm`, `iprp/ipco ispe/irot/imir`, `ipma`), typed `iref.<type>.*`
    rows (`auxl`/`dimg`/`thmb`/`cdsc`), graph-summary counters, and `auxC`-typed
    auxiliary semantics.
- CLI tools:
  - `metaread`: human-readable dump; output is sanitized.
  - `metavalidate`: metadata validation tool (decode-status health + DNG/CCM checks),
    including machine-readable issue codes (for example
    `xmp/output_truncated`, `xmp/invalid_or_malformed_xml_text`).
  - `metadump`: sidecar/preview dump tool (`--format lossless|portable`,
    `--extract-preview`, optional draft C2PA verify scaffold controls
    (`--c2pa-verify`, `--c2pa-verify-backend`), supports both positional
    `<source> <destination>` and explicit `-i/--input` + `-o/--out`; when
    multiple previews are found, `--out name.jpg` writes `name_1.jpg`,
    `name_2.jpg`, ...).
  - `metatransfer`: transfer/edit smoke tool (`read -> prepare -> emit`,
    source/target split inject, edit plan/apply) for JPEG and TIFF; thin
    wrapper over the core transfer APIs, with optional prepared-payload dumps.
    The shared transfer core now supports sink-based output paths; JPEG can
    stream metadata emit bytes and edited output directly, TIFF edit output
    streams the original file plus a planned metadata tail, and the public API
    now supports both a reusable `prepare -> compile -> patch/emit` execution
    plan and a narrow compiled writer helper with non-owning time patches plus
    a fixed-buffer `SpanTransferByteWriter` for high-throughput pipelines.
    TIFF hot-path integration uses backend emitters or rewrite/edit, not a
    metadata-only byte-writer emit path.
    The prepare path also records explicit per-family transfer policy
    decisions for MakerNote, JUMBF, and C2PA in the prepared bundle. File-based
    JPEG prepare can now preserve source JUMBF payloads as APP11 transfer
    blocks. Store-only JPEG prepare can also project decoded non-C2PA
    `JumbfCborKey` roots into generic APP11 JUMBF payloads when no raw source
    payload is available; ambiguous numeric map keys and decoded-CBOR
    bool/simple/sentinel/large-negative fallback forms still fail closed. The
    public core API also exposes
    `append_prepared_bundle_jpeg_jumbf(...)` for explicit logical raw JUMBF
    append into prepared JPEG bundles. `metatransfer --jpeg-jumbf file.jumbf`
    is the thin CLI path on top of that helper. JPEG content-changing
    rewrite/edit also drops stale APP11 C2PA and removes APP11 JUMBF when the
    resolved transfer policy is `Drop`. `c2pa=invalidate` now emits a draft
    unsigned APP11 C2PA invalidation payload for JPEG outputs instead of
    drop-only behavior. File-based JPEG prepare can also preserve an existing
    OpenMeta draft invalidation payload as raw APP11 C2PA. Re-sign is still
    unavailable. `PreparedTransferPolicyDecision` now carries an explicit
    C2PA contract surface:
    - `TransferC2paMode`
    - `TransferC2paSourceKind`
    - `TransferC2paPreparedOutput`
    so callers can distinguish `drop`, generated draft invalidation, raw
    draft preserve, and future signed rewrite without parsing messages.
    `PreparedTransferBundle::c2pa_rewrite` now exposes rewrite prerequisites
    separately from the resolved transfer policy: current state, detected
    source kind, existing carrier segment count, and whether manifest builder,
    content binding, certificate chain, private key, and signing time are
    still required before signed rewrite can exist. For JPEG rewrite prep it
    also exposes a deterministic `content_binding_chunks` sequence describing
    the rewrite-without-C2PA byte stream as preserved source ranges plus
    prepared JPEG segments. `build_prepared_c2pa_sign_request(...)` derives an
    explicit external signer request from that state,
    `build_prepared_c2pa_sign_request_binding(...)` materializes the exact
    content-binding bytes for an external signer,
    `build_prepared_c2pa_handoff_package(...)` bundles both into one public
    handoff object, `validate_prepared_c2pa_sign_result(...)` validates a
    returned signed logical C2PA payload before bundle mutation, including
    semantic manifest/claim/signature consistency plus request-aware manifest
    count / `claim_generator` checks plus manifest/claim/signature projection
    shape validation under the prepared manifest contract. When content
    binding is required, the returned payload must also carry at least one
    decoded assertion and the primary signature must link back to the
    prepared primary claim. Primary-signature explicit references that resolve
    to multiple claims under the prepared sign request are now rejected, and
    the primary claim may not be referenced by multiple signatures under that
    same request. Extra linked signatures beyond the prepared sign request are
    also rejected. When manifest builder output is required, the returned
    payload must also carry the same primary CBOR manifest payload bytes, and
    `apply_prepared_c2pa_sign_result(...)` stages externally signed logical
    C2PA payloads back into prepared JPEG APP11 blocks.
    OpenMeta can also serialize that handoff object and one persisted signed
    package for external signer round-trips.
    `PreparedTransferPackagePlan`,
    `PreparedTransferPackageBatch`,
    `build_prepared_transfer_emit_package(...)`,
    `build_prepared_bundle_jpeg_package(...)`,
    `build_prepared_bundle_tiff_package(...)`, and
    `write_prepared_transfer_package(...)` now expose deterministic
    final-output chunk plans for current JPEG/TIFF rewrite paths plus direct
    prepared-block emit packaging for JPEG/JXL/WebP. The new owned batch path
    (`build_prepared_transfer_package_batch(...)` /
    `write_prepared_transfer_package_batch(...)`) lets callers cache or hand
    off those final bytes without retaining the original input stream or
    prepared bundle storage, and
    `serialize_prepared_transfer_package_batch(...)` /
    `deserialize_prepared_transfer_package_batch(...)` persist that owned
    batch for cross-process or cross-layer handoff.
    `collect_prepared_transfer_payload_views(...)` and
    `build_prepared_transfer_payload_batch(...)` now provide the matching
    target-neutral semantic surface directly over prepared bundles, before the
    final package layer is materialized, and
    `serialize_prepared_transfer_payload_batch(...)` /
    `deserialize_prepared_transfer_payload_batch(...)` persist that earlier
    semantic payload batch when callers want cross-process or cross-layer
    handoff before final package materialization. The thin `metatransfer`
    wrappers can now dump that persisted semantic payload batch directly
    through `--dump-transfer-payload-batch`.
    `collect_prepared_transfer_package_views(...)` now exposes the first
    target-neutral semantic view over that persisted package, and
    `replay_prepared_transfer_package_batch(...)` is the matching target-neutral
    callback replay path. The OIIO bridge can consume the same batch directly through
    `collect_oiio_transfer_package_views(...)` or replay it through
    `replay_oiio_transfer_package_batch(...)`, without reopening the source
    file or keeping the original prepared bundle alive. OIIO payload
    views/batches now sit on top of the core semantic payload layer instead
    of rebuilding classification and copies independently.
    `build_prepared_transfer_adapter_view(...)` now exposes the same prepared
    bundle as one target-neutral operation list for JPEG/TIFF/JXL/WebP host
    integrations that do not want to parse route strings, and
    `emit_prepared_transfer_adapter_view(...)` streams that compiled view into
    one generic host sink. `collect_oiio_transfer_payload_views(...)` is the
    first thin bridge on top of that surface: it exposes one zero-copy
    OIIO-facing payload list with explicit semantic kinds (`ExifBlob`,
    `XMPPacket`, `ICCProfile`, `IPTCBlock`, `JUMBF`, `C2PA`) plus the compiled
    per-target operation metadata. `build_oiio_transfer_payload_batch(...)`
    is the owned form of that bridge for host layers that want to cache or
    move transfer payloads without keeping the prepared bundle alive.
    `serialize_prepared_transfer_payload_batch(...)` /
    `deserialize_prepared_transfer_payload_batch(...)` persist that semantic
    payload layer directly, and
    `replay_prepared_transfer_payload_batch(...)` plus
    `replay_oiio_transfer_payload_batch(...)` expose the matching
    target-neutral and OIIO-facing callback replay paths over the persisted
    payload batch.
    OpenMeta also now exposes an EXR-native attribute bridge outside the block
    transfer core: `build_exr_attribute_batch(...)` exports per-part EXR header
    attributes as owned `(part_index, name, type_name, value_bytes)` records,
    `build_exr_attribute_part_spans(...)` groups them into contiguous per-part
    spans, `build_exr_attribute_part_views(...)` exposes zero-copy grouped
    part views for host code, and `replay_exr_attribute_batch(...)` replays
    the grouped batch through explicit host callbacks. Known scalar/vector EXR
    types are re-encoded deterministically, while unknown/custom attributes
    can be preserved as opaque raw bytes when their original type name is
    available.
    JPEG XL is now a first transfer target in the same core API:
    `prepare_metadata_for_target(..., TransferTargetFormat::Jxl, ...)`
    can build `Exif` and `xml ` box payloads plus an encoder ICC profile from
    `MetaStore`. `compile_prepared_bundle_jxl(...)` precomputes route dispatch
    once, and `emit_prepared_bundle_jxl(...)` /
    `emit_prepared_bundle_jxl_compiled(...)` emit those payloads through a
    `JxlTransferEmitter`: `Exif`/`xml ``/`jumb`/`c2pa` stay on the box path,
    while `jxl:icc-profile` uses the encoder ICC-profile path and is not
    serialized as a box. JXL transfer also supports bounded JUMBF/C2PA
    packaging on the same contract: file-based prepare can preserve source
    generic JUMBF payloads and raw OpenMeta draft C2PA invalidation payloads
    as JXL boxes, generate a draft unsigned invalidation payload for
    content-bound source C2PA, and store-only prepare can project decoded
    non-C2PA `JumbfCborKey` roots into generic JXL `jumb` boxes when no raw
    source payload is available. JXL IPTC uses the same bounded model: when
    raw IPTC is requested for JXL, OpenMeta projects it into the `xml ` XMP
    box rather than inventing a raw IIM carrier.
    `build_prepared_transfer_emit_package(...)` plus
    `write_prepared_transfer_package(...)` can serialize direct JXL box output
    bytes from prepared bundles, and `execute_prepared_transfer(...)` can use
    that same box-only path through `emit_output_writer`. `jxl:icc-profile`
    still stays on the encoder ICC path and is not serialized through the
    byte-writer path. Signed C2PA rewrite/re-sign and edit/rewrite are still
    out of scope for the JXL path.
    WebP is now the next bounded transfer target on the same contract:
    `prepare_metadata_for_target(..., TransferTargetFormat::Webp, ...)`
    can build `EXIF`, `XMP `, and `ICCP` RIFF metadata chunks from
    `MetaStore`. When IPTC is requested for WebP without an explicit XMP
    carrier, OpenMeta projects it into the `XMP ` chunk rather than inventing
    a raw IIM chunk. Bounded C2PA support follows the same draft model as the
    JXL path: raw OpenMeta draft invalidation payloads and generated draft
    invalidation output are carried as `C2PA` chunks. The core API now
    exposes `compile_prepared_bundle_webp(...)`,
    `emit_prepared_bundle_webp(...)`,
    `emit_prepared_bundle_webp_compiled(...)`, and the generic
    `emit_prepared_transfer_compiled(..., WebpTransferEmitter&)` path, while
    `build_prepared_transfer_emit_package(...)` /
    `write_prepared_transfer_package(...)` can serialize direct WebP chunk
    output bytes. Full WebP file rewrite/edit and signed C2PA rewrite are
    still follow-up work.
    ISO-BMFF metadata-item transfer is now the next bounded target family on
    the same core API:
    `prepare_metadata_for_target(..., TransferTargetFormat::{Heif,Avif,Cr3}, ...)`
    can build `bmff:item-exif`, `bmff:item-xmp`, bounded `bmff:item-jumb`,
    bounded `bmff:item-c2pa`, and `bmff:property-colr-icc` payloads. EXIF
    uses the BMFF item payload shape with the 4-byte big-endian TIFF-offset
    prefix plus full `Exif\0\0` data; IPTC is projected into
    `bmff:item-xmp` rather than inventing a raw IPTC-IIM BMFF carrier. ICC
    uses the bounded property path: `bmff:property-colr-icc` carries a `colr`
    property payload whose bytes are `u32be('prof') + <icc-profile>`, not a
    metadata item. File-based prepare can preserve source generic JUMBF
    payloads and raw OpenMeta draft C2PA invalidation payloads as BMFF
    metadata items, and store-only prepare can project decoded non-C2PA
    `JumbfCborKey` roots into `bmff:item-jumb` when no raw source payload is
    available. The bounded BMFF surface is summary/emitter/package-batch
    oriented: `compile_prepared_bundle_bmff(...)`,
    `emit_prepared_bundle_bmff(...)`,
    `emit_prepared_bundle_bmff_compiled(...)`, and
    `emit_prepared_transfer_compiled(..., BmffTransferEmitter&)` expose the
    reusable item/property-emitter path, while the shared package-batch
    persistence and replay layers can own and hand off stable BMFF item and
    property payload bytes. `metatransfer` and `openmeta.transfer_probe(...)`
    expose summary-only BMFF output for this bounded path, including
    `bmff_item ...` and `bmff_property colr/prof ...` lines. Full BMFF file
    rewrite/edit and signed C2PA rewrite/re-sign remain follow-up work.
    `metatransfer` and `openmeta.transfer_probe(...)` now expose both the
    resolved transfer-policy decisions and JPEG edit-plan removal counts for
    existing APP11 JUMBF/C2PA segments, plus the derived `c2pa_sign_request`
    view for external signer integration, exact binding-byte materialization,
    a `c2pa_stage_validate` result for signed-payload validation, a
    `c2pa_stage` result when wrappers stage external signed payloads before
    emit/edit, and persisted handoff/signed-package dump-load flows in both
    the C++ and Python `metatransfer` wrappers. Final JPEG emit/write now
    also preflights prepared APP11 C2PA carriers for sequence continuity,
    consistent headers, valid JUMBF/C2PA root type, BMFF declared-size
    consistency, and bundle-contract consistency before bytes are written.
    Missing required carriers, draft-invalidated carriers under a signed
    rewrite contract, signed-rewrite carriers under a draft contract, and
    `Ready` rewrite state without signed-rewrite prepared output are all
    rejected before backend bytes are emitted.
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
- `src/tools/`: CLI tools (`metaread`, `metavalidate`, `metadump`,
  `metatransfer`, `thumdump`)
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
- Metadata support matrix (draft): `docs/metadata_support.md`
- API reference (Doxygen): `docs/doxygen.md`
