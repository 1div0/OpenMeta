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
| EXR | Bridge-only today | EXR-native attribute export and replay for host integrations | No first-class transfer target path yet |

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
- transfer-policy decisions for MakerNote, JUMBF, and C2PA

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

Implemented today as an integration bridge, not a first-class transfer target:
- `build_exr_attribute_batch(...)`
- `build_exr_attribute_part_spans(...)`
- `build_exr_attribute_part_views(...)`
- `replay_exr_attribute_batch(...)`

This is useful for OpenEXR / OIIO-style hosts, but it is not yet a true
`prepare -> compile -> emit/edit` path in the same sense as JPEG or TIFF.

## Transfer Policies

The public transfer contract already models three policy subjects:
- MakerNote
- JUMBF
- C2PA

Each uses explicit `TransferPolicyAction` values:
- `Keep`
- `Drop`
- `Invalidate`
- `Rewrite`

Prepared bundles also record the resolved policy decisions and reasons so
callers do not have to infer behavior from warning text alone.

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

### 2. EXIF / IPTC / XMP sync policy

This remains one of the biggest product gaps for writer adoption.

Missing pieces include:
- conflict resolution rules
- sidecar vs embedded policy
- canonical writeback policy
- broader namespace reconciliation behavior

### 3. MakerNote-safe rewrite expectations

Read parity is strong, but broad rewrite guarantees for vendor metadata are not
yet at the level of mature editing tools.

### 4. EXR direction

The architectural question is now explicit:
- keep EXR as an attribute bridge only, or
- promote it to a first-class transfer target

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
3. Decide the EXR direction explicitly.
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
