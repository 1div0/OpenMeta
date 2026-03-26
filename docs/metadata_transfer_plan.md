# Metadata Transfer Plan (Draft)

Date: March 5, 2026

Related:
- [metadata_backend_matrix.md](metadata_backend_matrix.md)

## Goal

The transfer core is for metadata-only workflows:
- source: camera RAW and other supported inputs
- target: export-oriented container metadata
- scope: prepare once, then emit or edit many times

Pixel transcoding is out of scope.

## Design Rule

Transfer should not bottleneck high-throughput pipelines.

The core rule is:
- do the expensive work once during `prepare`
- reuse prebuilt metadata payloads during `compile` and `emit`
- keep per-frame work limited to optional time patching plus final container
  write calls

## Current Status

Source-side readiness is already strong:
- tracked EXIF read gates are green on `HEIC/HEIF`, `CR3`, and mixed RAW corpora
- tracked MakerNote gates are green
- portable XMP gates are green
- EXR header interop gates are green

The main remaining work is now on the target side.

The first public write-side sync controls are also in place:
- generated XMP can explicitly suppress EXIF-derived projection
- generated XMP can explicitly suppress IPTC-derived projection
- prepared bundles record those resolved projection decisions alongside the
  existing preservation policies

## Target Status Matrix

| Target | Status | Current shape | Main limits |
| --- | --- | --- | --- |
| JPEG | First-class | Prepared bundle, compiled emit, byte-writer emit, edit planning/apply, file helper, bounded JUMBF/C2PA staging | Not a full arbitrary metadata editor yet |
| TIFF | First-class | Prepared bundle, compiled emit, edit planning/apply, file helper, streaming edit path | Broader TIFF/DNG rewrite coverage is still narrower than JPEG |
| PNG | Bounded but real | Prepared bundle, compiled emit, bounded chunk rewrite/edit, file-helper roundtrip | Not a general PNG chunk editor |
| WebP | Bounded but real | Prepared bundle, compiled emit, bounded chunk rewrite/edit, file-helper roundtrip | Not a general WebP chunk editor |
| JP2 | Bounded but real | Prepared bundle, compiled emit, bounded box rewrite/edit, file-helper roundtrip | `jp2h` synthesis is still out of scope |
| JXL | Bounded but real | Prepared bundle, compiled emit, bounded box rewrite/edit, file-helper roundtrip | Still narrower than JPEG/TIFF |
| HEIF / AVIF / CR3 | Bounded but real | Prepared bundle, compiled emit, bounded BMFF item/property edit, file-helper roundtrip | Not broad BMFF writer parity |
| EXR | Bounded but real | Prepared bundle, compiled emit, direct backend attribute emit, CLI/Python transfer surface | No file rewrite/edit path yet; current transfer payload is safe string attributes only |

## What Is Already Implemented

### Core API

The shared transfer core already provides:
- `prepare_metadata_for_target(...)`
- `prepare_metadata_for_target_file(...)`
- `compile_prepared_transfer_execution(...)`
- `execute_prepared_transfer(...)`
- `execute_prepared_transfer_compiled(...)`
- `execute_prepared_transfer_file(...)`

This supports both:
- `prepare -> compile -> emit`
- `prepare -> compile -> patch -> emit/edit`

### Core Utility Layers

These support the public transfer flow:
- `TransferByteWriter`
- `SpanTransferByteWriter`
- prepared payload and package batch persistence
- adapter views for host integrations
- explicit time-patch support for fixed-width EXIF date/time fields
- transfer-policy decisions for MakerNote, JUMBF, C2PA, EXIF-to-XMP
  projection, and IPTC-to-XMP projection

### Current File-Helper Regression Coverage

OpenMeta now has explicit end-to-end read-backed transfer tests for:
- source JPEG -> JPEG edit/apply -> read-back
- source JPEG -> TIFF edit/apply -> read-back
- source JPEG -> PNG edit/apply -> read-back
- source JPEG -> WebP edit/apply -> read-back
- source JPEG -> JP2 edit/apply -> read-back
- source JPEG -> JXL edit/apply -> read-back
- source JPEG -> HEIF edit/apply -> read-back
- source JPEG -> AVIF edit/apply -> read-back
- source JPEG -> CR3 edit/apply -> read-back

That does not make all targets equally mature, but it does mean the transfer
core has real roundtrip regression gates across the primary supported export
families.

## Per-Target Notes

### JPEG

Strongest current target.

Implemented:
- EXIF as APP1
- XMP as APP1
- ICC as APP2
- IPTC as APP13
- edit planning and apply
- byte-writer emit
- bounded JUMBF/C2PA staging

### TIFF

Also a first-class target.

Implemented:
- EXIF, XMP, ICC, and IPTC transfer
- edit planning and apply
- file-based helper flow
- streaming edit output

### PNG

Implemented as a bounded chunk target:
- `eXIf`
- XMP `iTXt`
- `iCCP`
- bounded rewrite/edit for managed metadata chunks

### WebP

Implemented as a bounded RIFF metadata target:
- `EXIF`
- `XMP `
- `ICCP`
- bounded `C2PA`
- bounded rewrite/edit for managed metadata chunks

### JP2

Implemented as a bounded box target:
- `Exif`
- `xml `
- bounded `jp2h` / `colr`
- bounded rewrite/edit for top-level managed metadata
- replacement of managed `colr` in an existing `jp2h`

### JXL

Implemented as a bounded box target:
- `Exif`
- `xml `
- bounded `jumb`
- bounded `c2pa`
- encoder ICC handoff
- bounded box-based edit path

### HEIF / AVIF / CR3

Implemented as a bounded BMFF target family:
- `bmff:item-exif`
- `bmff:item-xmp`
- bounded `bmff:item-jumb`
- bounded `bmff:item-c2pa`
- bounded `bmff:property-colr-icc`
- bounded metadata-only `meta` rewrite path

### EXR

Implemented today as a bounded first-class target:
- `prepare_metadata_for_target(...)`
- `prepare_metadata_for_target_file(...)`
- `compile_prepared_transfer_execution(...)`
- `emit_prepared_bundle_exr(...)`
- `emit_prepared_transfer_compiled(...)`
- public CLI/Python `--target-exr` transfer surface

It still keeps the older integration bridge:
- `build_exr_attribute_batch(...)`
- `build_exr_attribute_part_spans(...)`
- `build_exr_attribute_part_views(...)`
- `replay_exr_attribute_batch(...)`

Current EXR transfer scope is intentionally conservative:
- safe flattened `string` header attributes
- backend emission through `ExrTransferEmitter`
- no general file-based EXR metadata rewrite/edit path yet
- no typed EXR attribute synthesis beyond the current safe string projection

## Transfer Policies

The public transfer contract now models five policy subjects:
- MakerNote
- JUMBF
- C2PA
- XMP EXIF projection
- XMP IPTC projection

Each uses explicit `TransferPolicyAction` values:
- `Keep`
- `Drop`
- `Invalidate`
- `Rewrite`

Prepared bundles also record the resolved policy decisions and reasons so
callers do not have to infer behavior from warning text alone.

For the XMP projection subjects, the current public knobs are intentionally
simple:
- EXIF-derived properties can be mirrored into generated XMP or suppressed
- IPTC-derived properties can be mirrored into generated XMP or suppressed

This gives callers stable write-side control over the most important projection
behavior without forcing them to reverse-engineer the transfer output.

## Write-Side Sync Controls

OpenMeta now has a bounded public sync-policy layer for generated XMP.

Current controls:
- `xmp_project_exif`
- `xmp_project_iptc`
- CLI:
  - `--xmp-no-exif-projection`
  - `--xmp-no-iptc-projection`

Current behavior:
- existing XMP can still be included independently
- EXIF payload emission stays independent from EXIF-to-XMP projection
- IPTC native carrier emission stays independent from IPTC-to-XMP projection
- some targets without a native IPTC carrier can still use XMP as the bounded
  fallback carrier when IPTC projection is enabled

This is deliberately narrower than a full sync engine. It does not yet define:
- full EXIF vs XMP precedence rules
- MWG-style reconciliation
- canonical sidecar vs embedded writeback policy
- namespace-wide deduplication and normalization rules beyond the current
  generated-XMP path

## Time Patch Plan

Time patching is intentionally narrow and fixed-width.

Current model:
- build EXIF payloads once
- record patch slots in the bundle
- patch only the affected bytes during execution

Primary fields:
- `DateTime`
- `DateTimeOriginal`
- `DateTimeDigitized`
- `SubSecTime*`
- `OffsetTime*`

This is meant for fast repeated transfer, not general metadata editing.

## Main Blockers

### 1. General edit UX

OpenMeta still does not present one fully mature, general-purpose metadata
editor across all formats. The current transfer core is real, but still more
bounded than ExifTool or Exiv2.

### 2. Broader EXIF / IPTC / XMP sync policy

This remains one of the biggest product gaps for writer adoption, even though
the first public projection controls now exist.

Missing pieces include:
- conflict resolution rules
- sidecar vs embedded policy
- canonical writeback policy
- broader namespace reconciliation behavior

### 3. MakerNote-safe rewrite expectations

Read parity is strong, but broad rewrite guarantees for vendor metadata are not
yet at the level of mature editing tools.

### 4. EXR depth

The architectural question is now how far to deepen the current bounded EXR
target:
- keep EXR as a backend-emitter target plus bridge helpers, or
- add a broader EXR file rewrite/edit path

## Recommended Next Priorities

1. Keep transfer ahead of further read-breadth work.
2. Stabilize the current target family:
   - JPEG
   - TIFF
   - PNG
   - WebP
   - JP2
   - JXL
   - bounded BMFF
3. Decide how deep EXR should go beyond the current bounded target.
4. Add more transfer-focused roundtrip and compare gates where they improve
   confidence for adopters.
5. Add an explicit EXIF / IPTC / XMP sync policy.

## Postponed Work

Still out of scope for the current milestone:
- full arbitrary metadata editing parity
- full C2PA signed rewrite / trust-policy parity
- full EXIF / IPTC / XMP sync engine
- broad TIFF/DNG and BMFF rewrite parity beyond the bounded current targets

## Practical Summary

OpenMeta is no longer blocked by read-path quality for adoption-oriented
transfer work.

The main opportunity now is to make the current bounded transfer core easier
to use and easier to trust across the primary export targets, instead of
continuing to expand read-only surface area first.
