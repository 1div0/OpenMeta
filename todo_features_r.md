#TODO(Read) : Feature Roadmap

This document tracks **read-only** capabilities to close the gap vs ExifTool/Exiv2/libexif (excluding write/edit features).

## Current Draft Trackers

- RAW read-path issue list (draft): maintained in a local draft tracker.

## Next Execution Plan (Global)

1. **JUMBF/C2PA: draft -> read-complete (current focus)**
   - Added: explicit-reference nested map `link` field coverage in both
     semantic projection and verify paths (detached payload resolution).
   - Added: PNG/WebP C2PA container-variant regression coverage for
     `scan_auto` parity and odd-size WebP `C2PA` chunk padding with
     following-chunk alignment.
   - Broaden detached payload reference resolution (reference key variants and
     precedence handling).
   - Expand ambiguity/precedence tests for multi-claim/multi-signature layouts.
   - Keep cryptographic verify scaffold stable while improving deterministic
     candidate selection semantics.
2. **BMFF/CR3 semantic enrichment**
   - Added: `auxC` subtype interpretation now includes `ascii_z` and `u64be`
     (`aux.subtype_u64` / `iref.auxl.subtype_u64`).
   - Added: UUID subtype projection for `auxC` when subtype is 16 bytes
     (`aux.subtype_uuid` / `iref.auxl.subtype_uuid`).
   - Added: per-type relation item summaries for `auxl`/`dimg`/`thmb`/`cdsc`
     (`iref.<type>.item_count`, `iref.<type>.item_id`,
     `iref.<type>.item_out_edge_count`, `iref.<type>.item_in_edge_count`).
   - Added: regression coverage for `iloc` construction-method-2 resolution via
     `iref` version-0 (16-bit IDs) reference tables.
   - Next: keep BMFF/CR3 edge-case gates green as corpus grows and extend auxC
     subtype semantics beyond scalar/text forms when new samples justify it.
3. **ICC/CCM enrichment close-out**
   - Added: ICC `ui08` (uInt8 array) interpretation, alongside existing
     `ui16`/`ui32`.
   - Added: optional CCM warning for unusually large matrix-like field counts
     (`>36` values) in DNG-spec warning mode.
   - Add stricter optional shape/range checks and richer typed interpretation.
4. **Interpretation/query UX**
   - Add exact tag-name lookup API and optional fuzzy/topic search.
5. **Next milestone**
   - Start EXIF/IPTC/XMP mapping/sync engine for transfer/portable workflows.

## What's Missing Today (Highest Impact)

1. **ISO-BMFF / CR3 metadata decode paths** (read baseline closed)
   - Today:
OpenMeta scans BMFF / CR3 containers and extracts Exif
        / XMP from common
          layouts(BMFF `meta / iinf + iloc` items;
                  CR3 Canon UUID `CMT1..CMT4` TIFF blocks)
              .
    - Hardened : `iloc.data_reference_index` is resolved via `meta / dinf
          / dref` when present; self-contained `dref/url ` and `dref/urn ` entries are treated as local, and non-self-contained references are skipped safely per item.
   - Hardened: out-of-range extents for known metadata items are now skipped per-extent/item (best-effort), allowing remaining valid items to be emitted.
   - Draft now: BMFF relation fields from `iref` are emitted as derived fields (`iref.ref_type`, `iref.from_item_id`, `iref.to_item_id`, `iref.edge_count`) plus primary convenience links (for example `primary.auxl_item_id`).
   - Added: `auxC` property parsing for typed aux semantics fields (`aux.item_id`, `aux.semantic`, `aux.type`, `aux.subtype_hex`) and draft subtype interpretation fields (`aux.subtype_kind`, `aux.subtype_u32`), plus primary-focused outputs (`primary.auxl_semantic`, `primary.depth_item_id`, `primary.alpha_item_id`, `primary.disparity_item_id`, `primary.matte_item_id`) and `iref.auxl.*` derived rows.
   - Gate status (cached corpus): `HEIC/HEIF`, `CR3`, and `raw_bmff` tiers pass with no missing EXIF tags in semantic compare mode.
   - Added: typed non-primary relation rows for `dimg`/`thmb`/`cdsc` (`iref.<type>.from_item_id`, `iref.<type>.to_item_id`) and per-type edge counters (`iref.<type>.edge_count`).
   - Added: per-type unique source/target counters (`iref.<type>.from_item_unique_count`, `iref.<type>.to_item_unique_count`) for `auxl`/`dimg`/`thmb`/`cdsc`.
   - Added: per-type graph-summary aliases for relation counters (`iref.graph.<type>.edge_count`, `iref.graph.<type>.from_item_unique_count`, `iref.graph.<type>.to_item_unique_count`) for `auxl`/`dimg`/`thmb`/`cdsc`.
   - Added: draft relation-graph summary fields (`iref.item_count`, `iref.from_item_unique_count`, `iref.to_item_unique_count`, plus row-wise `iref.item_id` + `iref.item_out_edge_count` + `iref.item_in_edge_count`).
   - Next: keep this green as corpus grows (new HEIF/AVIF and CR3 edge files) and extend auxC subtype semantics as new real-world payloads appear.

2. **Additional structured decoders**
   - Today: OpenMeta decodes EXIF/TIFF-IFD, XMP properties, IPTC-IIM, ICC (header + tag table), and Photoshop IRB (8BIM).
   - Recently added:
     - MPF (JPEG APP2 `MPF\0`) basic TIFF-IFD decode (emits `mpf0` tags).
     - PrintIM (EXIF 0xC4A5) decode into `MetaKeyKind::PrintImField`.
   - Recently added:
     - GeoTIFF GeoKeyDirectoryTag (0x87AF) decode into `MetaKeyKind::GeotiffKey` (best-effort; values are preserved as raw EXIF tags too).
   - Needed next: JUMBF/C2PA, and richer MPF/PrintIM interpretation (e.g. MPEntry expansion).

3. **MakerNote parsing + registries** (read baseline near-closed)
   - Today: MakerNote is preserved as bytes (lossless read), and can be **decoded into `mk_*` IFD blocks** when enabled.
   - Coverage (corpus gate): on the ExifTool `sample_images` corpus (from `https://exiftool.org/sample_images.html`), OpenMeta matches **100% of ExifTool MakerNote tag ids for all major vendors including FLIR** in vendor-focused runs; full-corpus gate currently tracks ground-truth drift warnings separately.
   - Supported (best-effort, read-only):
     - Nikon: embedded TIFF header + common BinaryData subdirectories (VRInfo, PictureControl, WorldTime, ISOInfo, DistortInfo, FlashInfo01xx, MultiExposure, AFInfo2, FileInfo, RetouchInfo) + ShotInfo decrypt probing + LensData (0100/0204/0400-0403/0800-0802) + NikonCustom settings extraction. Verified to match ExifTool tag IDs on the ExifTool `sample_images` Nikon set.
     - Fuji: `FUJIFILM` signature + u32 IFD offset
     - Canon: classic IFD at offset 0 (auto-detects endianness + value-offset base; supports ExifTool-style "Adjusted MakerNotes base" cases), plus:
       - common BinaryData subdirectories (CameraSettings, ShotInfo, FileInfo, CropInfo, AspectInfo, Processing, TimeInfo)
       - AFInfo2 (`0x0026`) decode into `mk_canon_afinfo2_*` (arrays + signed positions)
       - PictureStyleInfo (`psinfo`) best-effort decode from CanonCameraInfo (`0x000d`) into `mk_canon_psinfo_*`
       - CustomFunctions2 (`0x0099`) decode into `mk_canoncustom_functions2_*`
       - Verified to match ExifTool tag IDs on the ExifTool `sample_images` Canon set (including older compact-camera MakerNotes).
     - Apple iOS: classic IFD scan
     - Olympus: `OLYMP\0` / `CAMER\0` header variants (classic IFD at +8, offsets relative to outer EXIF TIFF)
     - Sony: classic IFD forms (some use value offsets relative to outer EXIF/TIFF)
     - Minolta/Konica-Minolta: classic IFD + CameraSettings binary tables
     - Pentax: `AOC\0` header variants (`II`/`MM`/blank, u16 count at +6)
     - Ricoh: classic IFD variants + binary ImageInfo/RicohSubdir tables
     - Casio: `QVC\0` / `DCI\0` / `AOC\0` (Optio 330RS/430RS) Casio type2 (big-endian directory)
     - Panasonic/Leica: classic IFD + known subdirectories
     - Kodak: multiple Kodak tables (including fixed-layout `IIII...` type9)
     - Samsung: classic IFD
     - JVC: `JVC ` header (classic IFD at +4)
     - GE: `GE\0\0` / `GENIC\0` signatures (classic IFD)
     - Motorola: classic IFD
     - DJI: MakerNote tables + ThermalParams (APP4) best-effort decode
     - Reconyx: fixed-layout HyperFire/HyperFire2/UltraFire decode
     - HP: fixed-layout type4/type6 decode (`IIII\x04/05/06\0`) + classic IFD variants
     - Nintendo: classic IFD + CameraInfo binary subdirectory (tag `0x1101`), including absolute-offset variant
   - Tag names: registry-driven via `registry/exif/makernotes/*.jsonl`.
   - Next: deeper value interpretation (enums/units), name harmonization where useful, and safe write semantics (later).

4. **Interpretation / presentation layer**
   - Today: typed values + registry-driven EXIF/TIFF/MakerNote tag-name lookup + CLI formatting.
   - Needed next:
     - richer value interpretation (enums/units/composites, GPS formatting, common conversions) while keeping a strict "raw bytes always available" option;
     - registry query APIs:
       - exact lookup by name (supports collisions / duplicates -> returns 0..N matches);
     - optional fuzzy/concept search (token index built from `name` + `aliases`, plus a small curated synonym/topic list such as `iso -> gain/exposure`, `color -> matrix/wb/gamma`).

5. **Cross-namespace mapping/sync**
   - Added (portable sidecar): IPTC-IIM -> XMP mapping for common fields:
     - `dc:*`: `2:005` -> `dc:title`, `2:025` -> `dc:subject` (`rdf:Bag`),
       `2:080` -> `dc:creator` (`rdf:Seq`), `2:116` -> `dc:rights`,
       `2:120` -> `dc:description`.
     - `photoshop:*`: `2:015` -> `Category`, `2:020` ->
       `SupplementalCategories` (`rdf:Bag`), `2:040` -> `Instructions`,
       `2:085` -> `AuthorsPosition`, `2:090` -> `City`, `2:095` -> `State`,
       `2:101` -> `Country`, `2:103` -> `TransmissionReference`,
       `2:105` -> `Headline`, `2:110` -> `Credit`, `2:115` -> `Source`,
       `2:122` -> `CaptionWriter`.
     - `Iptc4xmpCore:*`: `2:092` -> `Location`, `2:100` -> `CountryCode`.
   - Added precedence rule: EXIF/existing XMP are emitted before IPTC-derived
     portable fields so existing XMP values are not overwritten.
   - Needed: EXIF/IPTC<->XMP mapping engine, MWG-style conflict rules, and explicit provenance links ("derived from ...").

6. **Previews/thumbnails** (phase-1 done, draft)
   - Added: read-only candidate discovery + safe byte extraction API (`openmeta/preview_extract.h`):
     - `scan_preview_candidates(...)` / `find_preview_candidates(...)`
     - `extract_preview_candidate(...)`
   - Current support: EXIF/TIFF `JPEGInterchangeFormat` and `JpgFromRaw`/`JpgFromRaw2` style blobs.
   - Next: expand to multi-strip/tile previews and MakerNote preview pairs with the same limit/safety model.

7. **ICC + CCM enrichment** (next priority)
   - Needed:
     - richer ICC interpretation (profile identity + selected tag-level views);
     - CCM normalization for RAW/DNG color matrices (`ColorMatrix*`, `ForwardMatrix*`, `CameraCalibration*`);
     - consistent export surface for flat host/OCIO adapters.

8. **Coverage + regression gates**
   - Needed: systematic comparison vs ExifTool/Exiv2 on real-world corpora (including RAWs), plus fuzz/regression tests that run before commits.

## Recommended Work Order (Read-Only)

### Phase 0 - Guardrails & Validation (always-on)
- Expand corpora and "OpenMeta vs ExifTool" comparison scripts (per-format; avoid relying on tag-name equality only).
- Add regression expectations for:
  - block discovery counts (per container),
  - EXIF entry counts (per IFD),
  - payload extraction behavior (extended XMP, ICC sequences, brob/brotli).

### Phase 1 - Block -> Payload Completeness
- Ensure every discovered block kind can be reassembled into a stable logical byte stream (`extract_payload`) with:
  - strict size limits,
  - consistent ordering,
  - explicit "compressed/encoded" metadata surfaced in `ContainerBlockRef`.

### Phase 2 - XMP Read (done)
- Parses XMP packets into `MetaKeyKind::XmpProperty` (schema namespace URI + property path).
- Note: currently focuses on safe, high-value RDF/XML patterns (attributes, text values, arrays, `rdf:resource`).

### Phase 3 - IPTC-IIM Read (done)
- Decodes IPTC datasets from TIFF tag `0x83BB` and (when present) from Photoshop IRB resource `0x0404` as derived IPTC datasets.

### Phase 4 - ICC Read (done)
- Decodes ICC header fields and tag table into `MetaKeyKind::IccHeaderField`/`IccTag` and preserves raw tag bytes.
- Later (optional): integrate **LCMS2** for deeper ICC validation and richer presentation while still preserving raw bytes.

### Phase 5 - Photoshop IRB Read (done)
- Parses 8BIM resource blocks from APP13 / TIFF tag `0x8649` into `MetaKeyKind::PhotoshopIrb`.

### Phase 6 - MPF + Minor Blocks
- MPF (JPEG APP2 "MPF\0") decode (done) + PrintIM decode (done) + GeoTIFF keys decode (done).

### Phase 7 - MakerNotes (vendor-by-vendor)
- Start with "read-only structured decode", preserving the original MakerNote bytes unmodified.
- Add a registry-driven approach so new vendors/tables can be added without touching core parsing.

### Phase 8 - Preview/Thumbnail Extraction (draft baseline done)
- Implemented: candidate enumeration + safe extraction (`preview_extract.h`).
- Next: expand preview kinds (strip/tile/MakerNote pointers) while keeping strict limits.

### Phase 9 - ICC + CCM Enrichment
- In progress: ICC header decode now emits typed values for stable fields
  (u32/u64 signatures/flags/attributes and s15Fixed16 illuminant triplet).
- In progress: ICC tag interpretation helper now covers `desc`, `text`,
  `sig `, `data`, `mluc`, `ncl2`, `dtim`, `view`, `meas`, `chrm`, `sf32`, `uf32`, `mft1`, `mft2`, `mAB`, `mBA`,
  `XYZ `, `curv`, and `para` (raw ICC tag bytes still preserved), with shared display formatting via
  `format_icc_tag_display_value(...)` used by CLI/Python paths.
- In progress: DNG color/CCM tags are surfaced with adapter-friendly names
  (`dng:*` in portable style and a flat host-style variant) when DNG context
  is present.
- Added adapter policy tests for normalized CCM stability:
  - require DNG context by default (no `DNGNorm`/`dngnorm` on non-DNG EXIF)
  - include/exclude behavior via `include_normalized_ccm`
  - safe typed flat-host export still includes normalized CCM when safe path is `Ok`.
- Added DNG-oriented CCM validation diagnostics (`CcmIssue`) in query/export
  paths: structural/spec coherency checks emit warnings, while non-finite
  numeric payloads are treated as hard-invalid per field (field dropped, no
  whole-document rejection).
- Added cross-field CCM checks:
  - `CalibrationIlluminant1` <-> `ColorMatrix1` companion-pair warnings.
  - `AsShotNeutral` / `AnalogBalance` channel-count checks against
    `ColorMatrix1` channel count when available.
- Added ICC tag-name coverage for additional common signatures
  (`chrm`, `dmnd`, `dmdd`, `gamt`, `ncl2`, `resp`, `targ`).
- Next: add richer ICC tag-level interpretation helpers while preserving raw bytes,
  and extend the normalized CCM query surface with optional stricter shape/range checks.

### Phase 10 - Mapping / Sync
- Add explicit transforms (EXIF->XMP, IPTC->XMP, merges) with:
  - initial portable IPTC-IIM -> XMP `dc:*` mapping done for common text/list
    fields (keywords/creator/title/description/rights),
  - deterministic ordering,
  - provenance links,
  - "merge/cleanup" tools.

### Phase 11 - JUMBF / C2PA
- Draft phase-3 implemented:
  - BMFF/JXL/JPEG/PNG/WebP block classification for JUMBF/C2PA payloads:
    - BMFF meta items (`jumb`/`c2pa` item types and JUMBF MIME items).
    - JXL `jumb`/`c2pa` boxes and `brob` realtype dispatch (Exif/xml/jumb/c2pa).
    - JPEG APP11 JUMBF/C2PA streams (`JP..` segments, reassembled).
    - PNG `caBX` chunks.
    - WebP `C2PA` chunks.
  - Minimal structural decode into `JumbfField` (`box.*`, `c2pa.*`).
  - Bounded definite and indefinite CBOR decode into `JumbfCborKey`.
  - Composite CBOR map-key fallback naming (`k{index}_{major}`) to avoid
    decode aborts on non-text keys.
  - Broader scalar decode coverage (simple values + half-float -> f32 bits).
  - Draft semantic projection fields (`c2pa.semantic.*`) for high-level
    presence/shape signals (`manifest`, `claim`, `assertion`, `signature`).
  - Draft per-claim projections with stable key naming
    (`c2pa.semantic.claim.{i}.*`) including claim prefix, key hits,
    assertion count, signature key hits, and claim generator (ASCII-safe).
  - Draft per-assertion projections with stable key naming
    (`c2pa.semantic.claim.{i}.assertion.{j}.*`) including assertion prefix and
    observed key-hit count.
  - Draft per-claim signature projections with stable key naming
    (`c2pa.semantic.claim.{i}.signature.{k}.*`) including signature prefix,
    observed key-hit count, and algorithm hint when available.
  - Draft per-signature projections with stable key naming
    (`c2pa.semantic.signature.{k}.*`) including signature prefix, observed
    key-hit count, and algorithm hint when available.
  - Draft linkage counters (`c2pa.semantic.signature_linked_count`,
    `c2pa.semantic.signature_orphan_count`) to distinguish claim-linked vs
    orphan signature groups.
  - Draft verification scaffold fields (`c2pa.verify.*`) with backend/status
    reporting and optional backend preference (`auto|native|openssl|none`).
  - Draft verification currently includes:
    - signature-shape checks for common algorithms (`invalid_signature`);
    - OpenSSL-backed cryptographic verification (`verified` /
      `verification_failed`) when signatures expose explicit algorithm +
      signing-input + key material fields.
    - COSE_Sign1 extraction (array or embedded CBOR byte-string forms): `alg`
      from protected headers, `x5chain` from unprotected headers (with
      intermediate chain hints), Sig_structure reconstruction when payload is
      present, and raw ECDSA signature support (`r||s` converted to DER for
      OpenSSL).
    - Draft profile checks (`profile_status` / `profile_reason`) based on
      semantic shape fields (manifest presence, claim presence, linked
      signatures).
    - Draft certificate trust checks (`chain_status` / `chain_reason`) when a
      certificate is present (`certificate_der` or COSE `x5chain`) (parse +
      validity window + trust-store verification via OpenSSL).
  - Added: detached payload reference-key matching accepts plural variants
    (`refs`/`references`/`claim_references`) in addition to existing singular
    forms.
  - Added: detached payload reference-key matching accepts hyphenated forms
    such as `claim-reference`, `claim-uri`, and `claim-ref-index`.
  - Added: URI-like nested reference map fields now include `.href` and
    generic `.link` suffixes (for example `references[0].href`) under
    claim/reference/jumbf/manifest-scoped keys.
  - Added: `reference.index` / `references.index` scalar keys are treated as
    explicit claim-index references (deterministic index-first resolution when
    mixed with URI/label keys in the same signature).
  - Added: `claim_id`/`claim-id` and `reference.id`/`references.id` scalar
    keys are treated as explicit claim-index references; multi-signature mixed
    index+label+URI ambiguity coverage is now in regression tests.
  - Added: `claim_ref_id` / `claim-reference-id` key variants and multi-
    signature unresolved no-fallback coverage for index-only explicit
    references.
  - Added: verify-path regressions for `claim_ref_id` + `reference.claim_id`
    candidate selection (deterministic) and unresolved explicit no-fallback
    behavior.
  - Added: multi-claim/multi-signature verify regressions for nested
    `references[]` id-variant maps (`claim_ref_id` / `claim-reference-id` +
    `reference.claim_id`), including deterministic and unresolved no-fallback
    scenarios.
  - Added: flat reference-field id/index variants
    (`reference_id`/`reference-id`/`reference_index`/`ref_index`) to detached
    explicit-reference parsing and verify-path regressions for deterministic
    precedence and unresolved no-fallback behavior in multi-signature layouts.
  - Added: detached payload URI/text reference parsing now recognizes
    query-style index tokens (`claim-index=...`, `claim_ref=...`) when
    resolving detached claim payload candidates.
  - Added: deterministic explicit-reference ordering in detached payload
    resolution (sorted index-like references are applied before sorted
    label-based references).
  - Added: draft explicit-reference semantic counters for ambiguity tracking:
    - global: `explicit_reference_signature_count`,
      `explicit_reference_unresolved_signature_count`,
      `explicit_reference_ambiguous_signature_count`.
    - per-signature: `explicit_reference_present`,
      `explicit_reference_resolved_claim_count`,
      `explicit_reference_unresolved`,
      `explicit_reference_ambiguous`.
  - Added: detached payload reference parsing percent-decodes URI/label text,
    so encoded forms such as `claims%5B40%5D` and
    `jumbf=%63%32%70%61%2E...` resolve correctly.
  - Regression coverage now includes nested `references[]` map structures,
    overlapping duplicate explicit refs, unresolved explicit-ref no-fallback
    behavior, percent-encoded query-index forms, and multi-signature nested
    reference-map determinism.
  - Added: strict signature-prefix scoping for explicit-reference parsing to
    prevent cross-index collisions (for example `signatures[1]` no longer
    matches keys under `signatures[10]`).
  - Added: profile-summary consistency invariants in verify policy
    evaluation (`linked + orphan == signature_count`, explicit-reference
    counters bounded by signature counters).
  - Regression coverage includes explicit-precedence and plural/encoded
    reference cases.
  - Added: manifest-level semantic projections with deterministic ordering:
    `c2pa.semantic.manifest_count` and per-manifest counters for claim,
    assertion, signature, linked/orphan signature, cross-claim links, and
    explicit-reference ambiguity counts.
- Next:
  - Keep expanding explicit reference-linked detached payload resolution for
    additional real-world C2PA key variants while preserving deterministic
    precedence and no-fallback-on-explicit behavior.
  - Continue extending semantic projection for deeper manifest graph details
    (for example richer per-assertion and per-reference relation summaries).
  - Add more precedence/ambiguity tests for multi-claim/multi-signature cases
    as new corpora are added.
  - Expand verification beyond draft field-based checks to full C2PA/COSE
    manifest binding and policy validation, still isolated from the decode-only
    metadata parse path.

### Phase 12 - Metadata Transfer (No-Edits V1, planned)

- Goal:
  - Transfer metadata from source files (for example RAW) to TIFF/JPEG targets
    with no semantic edits by default.
  - Support high-throughput "prepare once, reuse many frames" operation.
  - Support optional per-frame capture-time patching without full EXIF rebuild.
- Main blockers:
  - missing write-side packaging pipeline,
  - missing EXIF target-side reserialization (offset/pointer rebuild),
  - missing explicit policy defaults for MakerNote and C2PA transfer.
- Planned order:
  1. Define `TransferProfile::NoEdits` contract and per-block policy.
  2. Add `prepare_metadata_for_target(...)` API (container-agnostic output,
     immutable prepared bundle model).
  3. Implement EXIF serializers first (JPEG APP1 and TIFF forms), plus
     `TimePatchMap` slot generation for patchable EXIF time tags.
  4. Add XMP/ICC/IPTC packagers for target-specific placement/chunking.
  5. Add TIFF/JPEG writer adapters using prepared blocks (fast emit path).
  6. Add transfer gates: source -> target -> compare.
  7. Add time-patch gates for `DateTime*`, `SubSecTime*`, `OffsetTime*`,
     optional GPS date/time fields, and patch safety checks.
- Postponed from V1:
  - full C2PA conformance validation,
  - advanced ICC/CCM interpretation parity,
  - full EXIF/IPTC/XMP sync engine.
- Planning reference:
  - `docs/metadata_transfer_plan.md`
