# OpenMeta Changes

## 0.4.9 - 2026-04-28

Changes compared with `0.4.8`.

### Added

- Added Phase One/Leaf RAW geometry helpers that normalize decoded
  `SensorWidth`, `SensorHeight`, `SensorLeftMargin`, `SensorTopMargin`,
  `ImageWidth`, and `ImageHeight` fields into an active raw rectangle plus
  right/bottom margins.
- Added `phaseone_raw_processing_from_store()` to expose normalized Phase
  One/Leaf RAW color matrices, white-balance levels, black level, sensor
  temperatures, raw-data/storage sizes, and sensor-calibration summaries.
- Python `Document` and `TransferSourceSnapshot` now expose
  `phaseone_raw_geometry()` and `phaseone_raw_processing()` thin wrappers over
  the same C++ helpers.
- Added `vendor_raw_processing_from_store()` and
  `classify_vendor_raw_processing_field()` for conservative Sony, Canon,
  Nikon, Fujifilm, Pentax, Panasonic, Olympus, Kodak, Minolta, Sigma, Samsung,
  Ricoh, Apple, DJI, Google, FLIR, Casio, Sanyo, KyoceraRaw, Reconyx, HP, JVC,
  GE, Motorola, Nintendo, and Microsoft RAW/source-processing field summaries,
  including a vendor-private RAW table bucket for private or unknown table
  entries.
- Python `Document` and `TransferSourceSnapshot` now expose
  `vendor_raw_processing(family)` for the same grouped
  Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/Sigma/
  Samsung/Ricoh/Apple/DJI/Google/FLIR/Casio/Sanyo/KyoceraRaw/Reconyx/HP/JVC/
  GE/Motorola/Nintendo/Microsoft summaries.
- Added `transfer_safety_audit_from_store()` plus Python
  `transfer_safety_audit()` methods so hosts can preflight rendered-image
  drops by source group, filtered count, C2PA invalidation count, and
  Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/Sigma/
  Samsung/Ricoh/Apple/DJI/Google/FLIR/Casio/Sanyo/KyoceraRaw/Reconyx/HP/JVC/
  GE/Motorola/Nintendo/Microsoft RAW/source-processing bucket.
- Added the first experimental semantic metadata query API in
  `openmeta/metadata_query.h`, with `query_crop_metadata(...)` returning both
  raw matches and normalized crop/active-area candidates for DNG crop tags,
  `ActiveArea`, Phase One/Leaf RAW geometry, and fuzzy crop/border-style XMP
  property paths.
- Extended the experimental semantic metadata query API with focused helpers
  for exposure/gain, white balance, color, lens correction, and orientation
  metadata. Non-crop queries return deterministic per-entry candidates with
  numeric value extraction when values are scalar or bounded numeric arrays.
- Added native Fujifilm RAF read coverage for header-declared FujiIFD/TIFF
  offsets, RAF header fields, RAF directory geometry tags, and RAFData geometry
  projection.
- RAF scanning now follows the header-declared preview JPEG metadata before the
  FujiIFD/raw section, so standard preview-carried EXIF tags are decoded
  together with native RAF fields.
- Rendered-image transfer safety now treats decoded native RAF header/directory
  fields as Fujifilm RAW/source-processing metadata and drops them before
  rendered-target serialization.
- BMFF direct emit package planning/writing now accepts both metadata item
  routes and bounded ICC `colr/prof` property routes, and the package replay
  path is regression-covered for foreign-`meta` graph merges.
- Added native Sigma X3F read coverage for common header fields, header
  extension adjustment fields, known `PROP` properties, and section-directory
  JPEG metadata discovery while preserving the older embedded-EXIF fallback.
- Added grouped semantic-query candidates for DNG color matrix/calibration/
  reduction/forward matrix sets, DNG white-balance vector sets, and
  lens-correction table groups. Candidate value-shape labels now include
  `vector_set`, `matrix_set`, and `table`.
- Added `query_raw_processing_metadata(...)` plus Python wrappers for
  conservative RAW-processing queries covering black/white levels,
  linearization tables, CFA/sensor layout, source geometry, and raw-storage
  identifiers.
- Python `Document` and `TransferSourceSnapshot` now expose thin wrappers for
  the experimental semantic metadata query API, including generic
  `metadata_query(kind)` and focused query helpers.
- Added opt-in raw-carrier preservation for `TransferSourceSnapshot` reads,
  including original container block ranges, route hints, bounded payload
  bytes, snapshot-local decoded entry links, C++ result counters, and thin
  Python accessors. Transfer execution still uses decoded metadata by default.
- Added `raw_carrier_passthrough_audit_from_snapshot()` plus Python
  `TransferSourceSnapshot.raw_carrier_passthrough_audit()` so hosts can
  preflight opt-in raw carriers before any host-owned passthrough decision.
  The audit reports candidate carriers and primary block reasons including
  missing payloads, target incompatibility, safety filtering, content-bound
  C2PA, profile policy, missing decoded entry links, and unsupported carrier
  kinds.
- Added `TransferRawCarrierPassthroughMode::WhenSafe` for snapshot-based
  preparation, plus the matching Python enum and snapshot transfer keyword.
  The first bounded path preserves eligible non-C2PA JUMBF and OpenMeta draft
  unsigned C2PA invalidation carriers for JPEG, JXL, and BMFF targets, plus
  draft unsigned C2PA invalidation carriers for WebP. EXIF/XMP/ICC/IPTC
  transfer remains decoded re-emission.
- Added optional `OPENMETA_ENABLE_RAPIDFUZZ` support for RapidFuzz-backed
  semantic-query XMP/property-path matching, plus
  `metadata_query_fuzzy_search_available()` so tools can detect whether the
  stronger fuzzy matcher is compiled in.
- Semantic-query matches now report `exact_match`, `fuzzy_match`, and
  `fuzzy_score` so host UI and Python tools can distinguish exact tag/name
  hits from RapidFuzz near-miss results.
- Added `openmeta/orientation.h` with stable EXIF/TIFF orientation
  interpretation helpers for user-facing labels, clockwise rotation degrees,
  mirrored-state checks, width/height-swap checks, and rotation-only fallbacks,
  plus matching thin Python wrappers.
- Added focused regression coverage for compatible-file versus rendered-image
  transfer safety: compatible mode keeps serializable source RAW/camera-specific
  metadata, while rendered mode drops source-specific metadata and uses
  host-provided target image specs.
- Configured BMFF image-usability checks now infer target-owned image specs
  before transfer, so real HEIF/AVIF/CR3 targets can exercise EXIF
  image-property, MakerNote, ICC, and XMP transfer paths without using the
  synthetic fixture geometry.
- Added a public RAW read-parity plan that tracks camera RAW family gaps
  against ExifTool-style coverage without broadening writer guarantees.
- Added a public interpretation status matrix that separates decode visibility
  from semantic names, query shapes, transfer-safety classification, and
  competitor-facing interpretation gaps.
- Added read-path coverage for EXIF/TIFF-carried ICC profiles and IPTC-IIM
  payloads, bare JPEG APP1 XMP packets, and XMP packets that use alternate
  `xmpmeta` namespace prefixes.

### Changed

- Added `OPENMETA_TEST_RUNTIME_LIBRARY_PATH` so CTest-launched external
  validation tools can run with a matching non-default C++ runtime lookup path,
  and documented the `libc++` test-prefix workflow.
- Empty `rdf:about=""` XMP description attributes are now ignored, matching
  common tool behavior, while non-empty `rdf:about` values and empty RDF
  collections such as empty `dc:subject` bags remain decoded.
- The `openmeta_wheel` CMake target now forwards the active compiler flags,
  Python selection, `OPENMETA_USE_LIBCXX`, and optional-feature defines into
  the nested scikit-build wheel configure step and shares the install-time
  wheel script, instead of relying only on environment variables.
- Phase One-family IIQ MakerNote detection now recognizes Leaf/Credo-style
  files as Phase One MakerNotes before the generic Kodak `IIII` fallback.
- Rendered-image transfer safety now treats Phase One/Leaf RAW sensor geometry,
  color matrices, white-balance coefficients, raw-data/storage fields,
  black-level fields, and sensor-calibration tables as raw-specific metadata
  and drops them for rendered outputs.
- Rendered-image transfer safety now also filters decoded Sony, Canon, Nikon,
  Fujifilm, Pentax, Panasonic, Olympus, Kodak, Minolta, Sigma, Samsung, Ricoh,
  Apple, DJI, Google, FLIR, Casio, Sanyo, KyoceraRaw, Reconyx, HP, JVC, GE,
  Motorola, Nintendo, and Microsoft MakerNote fields classified as source RAW,
  computational, thermal, color/WB, geometry/storage, raw-data, sensor,
  lens-correction, preview/face geometry, stitch/panorama geometry, or
  vendor-private table metadata.
- Live-vendor RAW/source-processing classification now covers additional Apple
  computational capture/HDR/motion fields, DJI pose and thermal fields, Google
  shot-log metering fields, and FLIR radiometric/raw-value/geometry fields.
- Portable XMP output now recognizes Adobe DNG XMP properties (`dng:*`) as a
  known namespace, so compatible-file transfer can serialize retained DNG
  profile metadata while rendered-image safety still filters it as source raw
  calibration metadata.
- Prepared EXIF transfer now reports decoded-only vendor MakerNote sub-IFDs as
  non-serializable writer inputs instead of implying that OpenMeta can
  reconstruct vendor MakerNote blobs. The original raw `ExifIFD:MakerNote`
  payload is still preserved when available.
- Rendered-image transfer safety now uses the current DNG tag numbers for
  source-bound profile/gain tables, raw digests/storage identifiers,
  forward matrices, and opcode/correction lists.
- `metaread` and `python -m openmeta.python.metaread` now print compact Phase
  One RAW geometry/processing summaries when those decoded fields are present.
- Draft C2PA verification now exposes opt-in trusted certificate-chain
  enforcement through `ValidateOptions`, `metavalidate`, `metadump`, and Python
  `read()`/`validate()` wrappers. Without this option, signature status and
  chain-trust detail remain separate signals; with it, untrusted or missing
  chains fail verification instead of reporting a loose `verified` result.
- `metaread` and `python -m openmeta.python.metaread` now print compact
  Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/Sigma/
  Samsung/Ricoh/Apple/DJI/Google/FLIR/Casio/Sanyo/KyoceraRaw/Reconyx/HP/JVC/
  GE/Motorola/Nintendo/Microsoft `vendor_raw_processing[...]` summaries when
  matching decoded fields are present.
- BMFF foreign-`meta` insertion now upgrades supported `iloc` version 0/1
  graphs to output `iloc` version 2 when inserted metadata needs 32-bit item
  IDs, removing the previous requirement that the target already used
  `iloc` version 2.
- BMFF foreign-`meta` insertion now treats retained `iloc` construction method
  1 records as supported when they point into an existing `idat` with data
  reference index 0, and rejects external data references or unsupported
  construction methods safely.
- BMFF foreign-`meta` insertion now preserves retained `iloc` construction
  method 2 item-reference extents when their `iref` `iloc` references are
  parseable and every referenced item remains retained with a supported local
  location.
- BMFF ICC property replacement now preserves the prior ICC association scope
  across retained items instead of collapsing replacement to only the primary
  item, and keeps the prior essential association bit on replacement
  associations.

### Fixed

- Fixed standalone EXIF/TIFF recovery when a file has a short non-TIFF prefix
  or malformed JPEG prefix before the `Exif` preamble.
- Fixed draft C2PA verification status handling for malformed COSE_Sign1 byte
  arrays, unresolved explicit detached-payload references, and nested numeric
  claim references with conflicting label/URI fields.
- Fixed appended metadata-only BMFF `meta` boxes to advertise inserted item
  payloads through file-offset `iloc` records, allowing CR3-style targets to
  expose appended EXIF/XMP metadata through ExifTool-compatible readers.
- Fixed sidecar-only BMFF transfer so existing OpenMeta-written XMP items are
  preserved when the prior metadata-only `meta` box uses file-offset `iloc`
  records.
- High-level C2PA validation now emits a warning when a signature verifies but
  the certificate chain is not trusted unless strict trusted-chain enforcement
  is enabled.
- Prepared-transfer payload/package artifact deserializers now reject truncated
  oversized entry counts before reserving output vectors.

### Tests And Validation

- Added public synthetic coverage for Leaf/Credo IIQ MakerNote detection and
  normalized Phase One RAW geometry and raw-processing helper behavior.
- Added public synthetic coverage for semantic crop queries, including DNG
  default crop pairs, DNG `ActiveArea`, Phase One/Leaf RAW geometry, fuzzy XMP
  path matching, deleted-entry filtering, and same-IFD crop pairing.
- Added public synthetic coverage for standard EXIF exposure/gain and
  orientation queries, XMP white-balance matching, DNG color-matrix matching,
  and vendor RAW-processing lens-correction classification reuse.
- Added public synthetic coverage for grouped semantic-query candidates:
  DNG color matrix sets, DNG white-balance vector sets, and vendor
  lens-correction table groups.
- Added public synthetic coverage for standalone EXIF/TIFF recovery after
  unknown-prefix and malformed-JPEG-prefix inputs.
- Added public regression coverage and a libFuzzer target for prepared-transfer
  payload/package artifact deserialization and replay.
- Added public synthetic coverage for
  Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/Sigma/
  Samsung/Ricoh/Apple/DJI/Google/FLIR/Casio/Sanyo/KyoceraRaw/Reconyx/HP/JVC/
  GE/Motorola/Nintendo/Microsoft RAW/source-processing
  classification and grouped summary behavior.
- Extended rendered-image safety coverage to verify Phase One/Leaf RAW
  geometry, color, raw-data/storage, black-level, and sensor-calibration fields
  are counted as raw-specific metadata and filtered from rendered transfers.
- Extended rendered-image safety coverage to verify decoded Sony, Canon, Nikon,
  Fujifilm, Pentax, Panasonic, Olympus, Kodak, Minolta, Sigma, Samsung, Ricoh,
  Apple, DJI, Google, FLIR, Casio, Sanyo, KyoceraRaw, Reconyx, HP, JVC, GE,
  Motorola, Nintendo, and Microsoft source-processing fields are counted as
  raw/source-specific metadata and filtered from rendered transfers, including
  anonymous/private RAW, computational, thermal, preview, face-geometry, and
  stitch/panorama table entries.
- Added rendered-image writer-output coverage for JPEG, TIFF/DNG, PNG, WebP,
  JP2, JXL, HEIF, AVIF, and CR3 metadata rewrites to verify serialized outputs
  omit source RAW calibration, raw digests/gain metadata, vendor private RAW
  tables, MakerNotes, camera raw settings XMP, and source JUMBF while
  preserving safe EXIF/XMP fields.
- Added BMFF transfer coverage for merging metadata into a foreign top-level
  `meta` item table that uses `iinf` version 2.
- Added BMFF transfer coverage that verifies multiple foreign top-level `meta`
  boxes and unsupported foreign `iloc`/`iref` versions fail safely instead of
  producing a partial rewrite.
- Added BMFF relation/property graph coverage that verifies stale `cdsc`
  references are removed when replacing foreign Exif/XMP items, existing `ipma`
  associations are preserved while adding/replacing ICC properties, and
  unsupported foreign `ipma` versions fail safely.
- Added BMFF foreign property-graph rejection coverage for duplicate `ipco`,
  duplicate `ipma`, and `ipma` associations that point past the available
  `ipco` property table.
- Added BMFF high-item-ID `iref` coverage that verifies inserted metadata
  references use version 1 item IDs and existing small-ID references are
  preserved when the relation table is upgraded.
- Added BMFF coverage for upgrading an `iloc` version 1 graph at item ID
  `65535` so inserted metadata uses item ID `65536` and remains readable.
- Added BMFF coverage for preserving retained `idat`-relative item extents
  while inserting metadata with absolute file-offset extents, plus fail-safe
  rejection for missing `idat`, external data references, and unsupported
  construction methods.
- Added BMFF coverage for preserving retained method-2 item-reference extents
  with explicit extent indexes and reference-order fallback, plus fail-safe
  rejection when method-2 references are missing or would point to an item
  removed by metadata replacement.
- Added BMFF coverage for multi-item ICC replacement, verifying secondary items
  that referenced the replaced ICC property are retargeted to the transferred
  ICC property.
- Added BMFF coverage that verifies essential `ipma` association flags are
  preserved when ICC properties are replaced.
- Extended the external image-usability gate so optional configured CR3 targets
  also exercise MakerNote transfer. Rendered-mode checks now compare against
  any pre-existing target MakerNote bytes instead of assuming the target had no
  MakerNotes.

## 0.4.8 - 2026-04-27

Changes compared with `0.4.7`.

### Added

- Added bounded 32-bit BMFF item-id insertion for parseable foreign item graphs
  that already use `iloc` version 2. `iloc` version 0/1 targets remain on the
  existing 16-bit insertion path.
- Added `TransferSafetyMode` on `TransferProfile`. The default
  `CompatibleFile` mode keeps current transfer behavior, while `RenderedImage`
  drops source raw color calibration/correction metadata, camera raw settings
  XMP, source ICC profiles, MakerNotes, and non-C2PA JUMBF data for rendered
  image outputs.
- Added transfer policy decisions for filtered image properties, ICC profiles,
  raw color calibration, and camera raw settings.

### Changed

- Public BMFF writer-contract docs now state the remaining item-id-width limit
  explicitly: 32-bit inserted item IDs require an existing `iloc` v2 graph, and
  unsupported or exhausted item-id spaces fail safely instead of truncating IDs.
- BMFF foreign-`meta` insertion keeps newly inserted metadata item records on
  `iloc` construction method 0 with absolute file-offset extents for broad
  reader compatibility.
- BMFF foreign-`meta` insertion now compacts zero/foldable `iloc` base offsets
  to a zero-width base-offset field when rebuilding supported item graphs,
  preserving absolute self-contained item extents for simpler readers.
- C++ and Python `metatransfer` wrappers now accept
  `--transfer-safety compatible|rendered`.
- Writer-contract docs now include a per-group transfer safety matrix for
  rendered-image exports, including opaque MakerNote handling.

### Fixed

- Fixed BMFF `iinf` version 1 scanning to read its 32-bit entry count, keeping
  writer read-back validation aligned with version 1/2 item tables.

### Tests And Validation

- Added a BMFF API roundtrip test that writes Exif and XMP into an `iloc` v2
  target with high item IDs and scans the result back as one Exif item and one
  XMP item.
- Added focused BMFF coverage that verifies inserted Exif/XMP item records use
  construction method 0 and absolute file-offset extents.
- Extended the BMFF image-usability gate with an explicit ExifTool
  reader-layout regression check for transferred Exif items in HEIF/AVIF/CR3
  targets.
- Extended rendered-image safety coverage to require policy decisions for
  image-layout fields, ICC, RAW/DNG color and correction tags, camera raw
  settings XMP, opaque MakerNotes, and non-C2PA JUMBF data.
- Extended the `metatransfer` smoke gate to verify that
  `--transfer-safety rendered` prints user-visible policy decisions for the
  same safety-filtered groups.
- Extended the Python `metatransfer` smoke gate with the same
  `--transfer-safety rendered` policy-output coverage.

## 0.4.7 - 2026-04-27

Changes compared with `0.4.6`.

### Added

- Added bounded foreign top-level BMFF `meta` merge support for parseable
  HEIF/AVIF/CR3-style item graphs. OpenMeta can now merge prepared
  Exif/XMP/JUMBF/C2PA item metadata into an existing foreign `meta` graph
  instead of appending a second competing `meta` box.
- Added BMFF XMP replacement and strip support for foreign item graphs that
  satisfy the bounded primary-item contract: a single parseable `iinf`,
  `iloc` version 0/1/2, `pitm`, and at most one `idat`.
- Added bounded BMFF ICC property merge support for foreign `meta` graphs.
  Existing ICC `colr/prof` and `colr/rICC` properties are removed from
  `iprp/ipco`, existing `ipma` associations are compacted/remapped, and the
  transferred `colr/prof` property is associated with the primary item.
- Added public CMake cache options for external BMFF usability checks:
  `OPENMETA_BMFF_HEIF_TEST_TARGET`, `OPENMETA_BMFF_AVIF_TEST_TARGET`,
  `OPENMETA_BMFF_CR3_TEST_TARGET`, and `OPENMETA_FFMPEG_EXECUTABLE`.
- Added external image-usability gate coverage for BMFF ICC and XMP transfer
  on configured HEIF/AVIF/CR3 targets when local tools can validate them.

### Changed

- BMFF edit/apply now preserves non-`meta` top-level boxes while rebuilding
  supported foreign `meta` item/property graphs in place.
- BMFF embedded-XMP strip mode no longer requires an OpenMeta-authored
  metadata `meta` box when the foreign graph is parseable and has a valid
  primary-item relationship.
- BMFF summary output and gate checks now cover both `bmff_item mime/xmp` and
  `bmff_property colr/prof` transfer results.
- Public transfer docs now describe the updated BMFF preserve/replace/strip
  contract and the configured-target validation limits.

### Fixed

- Fixed duplicate or stale BMFF ICC associations by replacing prior ICC
  properties and remapping `ipma` instead of adding competing property
  entries.
- Fixed supported foreign BMFF XMP strip/replacement paths that previously
  failed even when the target graph had enough structure for a safe bounded
  rewrite.
- Fixed Nikon MakerNote FlashInfo decoding for `0107`/`0108` and
  `0300`/`0301` layouts by emitting Flash Group A/B/C control-mode fields
  with ExifTool-compatible contextual names.

### Tests And Validation

- Added focused BMFF API tests for foreign Exif/XMP item replacement, XMP
  strip, ICC property replacement, existing `ipma` merge/remap, `iloc`
  rebasing, and fail-safe rejection of unsupported foreign graphs.
- Extended Nikon MakerNote tests for Flash Group A/B/C control-mode decoding.
- Extended the public transfer release and image-usability gates to accept
  configured BMFF target files and optional ffmpeg decode fallback.
- Verified the public release with the unit test suite, transfer release gate,
  external image-usability gate, documentation build, and whitespace checks.
