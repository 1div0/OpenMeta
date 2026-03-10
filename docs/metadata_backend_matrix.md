# Metadata Backend Matrix (Draft)

Date: March 5, 2026

## Goal

Define one OpenMeta write/transfer contract that can target multiple container
backends without per-backend metadata logic duplication.

## Capability Matrix

| Backend | Native metadata write primitives | Best use in OpenMeta |
| --- | --- | --- |
| libjpeg-turbo | `jpeg_write_marker`, `jpeg_write_m_header`, `jpeg_write_m_byte`, `jpeg_write_icc_profile` | Primary JPEG metadata emitter (APP1/APP2/APP13 direct control) |
| libtiff | `TIFFSetField`, `TIFFCreateEXIFDirectory`, `TIFFCreateGPSDirectory`, `TIFFWriteCustomDirectory`, `TIFFMergeFieldInfo` | Primary TIFF metadata emitter (native tag and IFD path) |
| libjxl | `JxlEncoderUseBoxes`, `JxlEncoderAddBox`, `JxlEncoderCloseBoxes` | Primary JXL metadata emitter (`Exif`, `xml `, `jumb`, optional `brob`) |
| OpenImageIO | Plugin-level writes use backend APIs (JPEG/TIFF/JXL/OpenEXR) | Optional adapter layer when host app already writes through OIIO |
| OpenEXR | `Header::insert`, typed attributes, `OpaqueAttribute` for unknown attr types | EXR header attribute path (not EXIF block packaging) |

## Container-Level Notes

### JPEG (libjpeg-turbo)

- Write EXIF as APP1 (`Exif\\0\\0` + TIFF payload).
- Write XMP as APP1 (`http://ns.adobe.com/xap/1.0/\\0` + packet).
- Write IPTC/IRB as APP13 (`Photoshop 3.0` resource block layout).
- Write ICC as APP2 chunk chain (`ICC_PROFILE` header + sequence index/count).
- OpenMeta should own marker ordering policy and payload splitting limits.

### TIFF (libtiff)

- EXIF/GPS should be written as directories and linked from root IFD pointers.
- XMP uses `TIFFTAG_XMLPACKET` (700).
- IPTC uses `TIFFTAG_RICHTIFFIPTC` / `TIFFTAG_EP_IPTC_NAA` (33723 alias).
- Photoshop IRB uses `TIFFTAG_PHOTOSHOP` (34377).
- ICC uses `TIFFTAG_ICCPROFILE` (34675).
- OpenMeta should own EXIF serializer policy before `TIFFSetField`.
- Public OpenMeta fast path is backend-emitter or rewrite/edit based:
  `emit_prepared_transfer_compiled(..., TiffTransferEmitter&)` or
  `write_prepared_bundle_tiff_edit(...)`.
- TIFF intentionally does not expose a metadata-only byte-writer emit API.

### JXL (libjxl)

- Metadata requires container boxes enabled.
- Add `Exif`, `xml `, `jumb` boxes through `JxlEncoderAddBox`.
- `Exif` box content must include the 4-byte TIFF header offset prefix.
- Optional Brotli-compressed box storage (`brob`) is backend-controlled.
- OpenMeta should prepare box payloads once and reuse in emit path.

### EXR (OpenEXR)

- EXR metadata is typed header attributes, not EXIF/TIFF IFD blocks.
- Use typed attributes for semantic fields.
- Unknown attribute types can be preserved as opaque attributes.
- OpenMeta EXR path should remain an attribute adapter, not block repackaging.

## Unified Workaround (Single Contract)

Use one two-phase pipeline for all backends:

1. `prepare_metadata_for_target(source_store, target_format, profile)`
   - Build target-ready block payloads once.
   - Build deterministic write order.
   - Build optional fixed-size patch map for per-frame time fields.
2. `emit_prepared_bundle(bundle, writer_target, frame_patch)`
   - Emit prebuilt payloads through backend-specific write calls.
   - Apply optional fixed-width time patch before final write.

This keeps heavy work out of hot per-frame loops.

## Backend Interface Shape (Draft)

Define narrow backend interfaces:

- `JpegWriterBackend::write_app_block(app_marker, bytes)`
- `TiffWriterBackend::set_tag(tag_id, bytes_or_scalar)` plus
  `commit_exif_directory(offset_ref)`
- `JxlWriterBackend::add_box(type4cc, bytes, compress)`
- `ExrWriterBackend::set_attribute(name, typed_or_opaque_value)`

OpenMeta core emits backend-neutral prepared payloads. Backends only perform
container call mapping.

## Policy Defaults (Draft)

- No-edits transfer mode: preserve payloads when legal for target container.
- EXIF pointer tags are regenerated for target container layout.
- MakerNote and C2PA/JUMBF transfer policy is explicit and deterministic.
- Current JPEG/TIFF prepare behavior:
  - MakerNote: `Keep` default, `Drop` supported, `Invalidate` currently
    resolves to `Drop`, `Rewrite` currently resolves to raw-preserve.
  - JUMBF: file-based JPEG prepare can preserve source payloads by repacking
    them into APP11 segments; store-only JPEG prepare can project decoded
    non-C2PA `JumbfCborKey` roots into generic APP11 JUMBF payloads; explicit
    raw JUMBF -> JPEG APP11 append is available through
    `append_prepared_bundle_jpeg_jumbf(...)`; and JPEG rewrite/edit removes
    existing APP11 JUMBF when the resolved policy is `Drop`. Ambiguous numeric
    map keys and decoded-CBOR bool/simple/sentinel/large-negative fallback
    forms in projected JUMBF are rejected; tagged CBOR values are preserved.
  - C2PA: `Invalidate` on JPEG now emits a draft unsigned APP11 C2PA
    invalidation payload.
    - `PreparedTransferPolicyDecision` exposes `mode`, `source_kind`, and
      `prepared_output` so adapters can branch without parsing free-form
      messages.
    - `PreparedTransferBundle::c2pa_rewrite` exposes the separate
      rewrite-prerequisites contract for future signing flows:
      current rewrite state, source kind, existing carrier segment count,
      and whether manifest builder, content binding, certificate chain,
      private key, and signing time are still required.
    - For JPEG it also exposes `content_binding_chunks`, the exact
      rewrite-without-C2PA chunk sequence a future signer would hash or
      reconstruct.
    - `build_prepared_c2pa_sign_request(...)` derives the external signer view
      from that same data without changing the bundle contract.
    - `build_prepared_c2pa_sign_request_binding(...)` materializes the exact
      content-binding bytes from that request and the target JPEG input.
    - `build_prepared_c2pa_handoff_package(...)` combines the signer request
      and exact binding bytes into one public handoff object.
    - `serialize_prepared_c2pa_handoff_package(...)` and
      `deserialize_prepared_c2pa_handoff_package(...)` persist that object.
    - `build_prepared_c2pa_signed_package(...)` combines the sign request and
      signer-returned material into one persisted signed package.
    - `serialize_prepared_c2pa_signed_package(...)` and
      `deserialize_prepared_c2pa_signed_package(...)` persist that package.
    - Thin wrappers expose that as CLI dump output and Python unsafe raw
      bytes without duplicating the reconstruction logic.
    - `validate_prepared_c2pa_sign_result(...)` validates a returned signed
      logical C2PA payload before bundle mutation and reports staged carrier
      size and segment count.
    - Current JPEG validation also checks semantic manifest/claim/signature
      consistency, resolved explicit references, request-aware manifest count
      / `claim_generator` requirements, decoded-assertion presence when
      content binding is required, the primary signature linking back to the
      prepared primary claim under that same content-binding contract,
      manifest/claim/signature projection shape under the prepared manifest
      contract, exact primary manifest-CBOR equality against
      `manifest_builder_output`, staged APP11 sequence order, and exact
      logical-payload reconstruction.
    - `apply_prepared_c2pa_sign_result(...)` accepts the external signer
      output and stages a content-bound logical C2PA payload back into
      prepared JPEG APP11 blocks after strict request validation.
    - The file-level execution helper and thin CLI/Python wrappers can now
      validate that stage step, apply it, and continue into normal JPEG
      emit/edit flow.
    - File-based JPEG prepare can preserve an existing OpenMeta draft
      invalidation payload as raw APP11 C2PA (`TransferC2paMode::PreserveRaw`).
    - Content-bound `Keep` still resolves to `Drop`.
    - `Rewrite` resolves to `Drop` with explicit
      `SignedRewriteUnavailable` until re-sign support exists, but externally
      signed payload staging is now available once a signer has consumed the
      request.
    - JPEG content-changing rewrite/edit removes existing APP11 C2PA from the
      target before inserting the new prepared payload.
- Safety limits are enforced before backend calls (size, truncation, malformed).

## Recommended Integration Order

1. JPEG and TIFF direct backends (highest transfer value).
2. JXL box emitter parity for EXIF/XMP/JUMBF.
3. OIIO bridge as optional host integration layer.
4. EXR attribute adapter for EXR-native metadata workflows.

This order aligns with fast transfer requirements and minimal overhead in
high-FPS pipelines.
