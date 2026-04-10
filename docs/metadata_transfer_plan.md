# Metadata Transfer Plan (Draft)

Date: March 5, 2026

Related:
- [quick_start.md](quick_start.md)
- [host_integration.md](host_integration.md)
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

Current planning estimate for this lane: about `80-85%`.

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
| TIFF | First-class | Prepared bundle, compiled emit, classic-TIFF and BigTIFF edit planning/apply, bounded preview-page chain rewrite (`ifd1`, `ifd2`, and preserved downstream tails), bounded SubIFD rewrite with preserved downstream auxiliary tails and preserved trailing existing children when only the front subset is replaced, bounded `ExifIFD -> InteropIFD` preservation when a replaced ExifIFD omits its own interop child, file helper, streaming edit path | Broader TIFF rewrite coverage is still narrower than JPEG |
| DNG | First-class | Dedicated public target layered on the TIFF backend; prepared bundle, compiled emit, minimal `DNGVersion` synthesis when missing, bounded DNG preview/aux merge policy, read-backed file-helper roundtrip, and the same bounded `SubIFD`/preview-tail/interop preservation contract as TIFF | Not a full DNG-specific rewrite engine or broad DNG policy surface |
| PNG | Bounded but real | Prepared bundle, compiled emit, bounded chunk rewrite/edit, file-helper roundtrip | Not a general PNG chunk editor |
| WebP | Bounded but real | Prepared bundle, compiled emit, bounded chunk rewrite/edit, file-helper roundtrip | Not a general WebP chunk editor |
| JP2 | Bounded but real | Prepared bundle, compiled emit, bounded box rewrite/edit, file-helper roundtrip | `jp2h` synthesis is still out of scope |
| JXL | Bounded but real | Prepared bundle, compiled emit, bounded box rewrite/edit, file-helper roundtrip | Still narrower than JPEG/TIFF |
| HEIF / AVIF / CR3 | Bounded but real | Prepared bundle, compiled emit, bounded BMFF item/property edit, file-helper roundtrip | Not broad BMFF writer parity |
| EXR | Bounded but real | Prepared bundle, compiled emit, direct backend attribute emit, prepared-bundle to `ExrAdapterBatch` bridge, CLI/Python transfer surface | No file rewrite/edit path yet; current transfer payload is safe string attributes only |

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
- source JPEG -> DNG edit/apply -> read-back
- source JPEG -> TIFF edit/apply with bounded preview-page chain ->
  read-back
- source TIFF/BigTIFF with existing multi-page preview chain ->
  replace the front preview pages and preserve downstream tails
- source DNG-like TIFF with `subifd0` + `ifd1` -> TIFF edit/apply -> read-back
- source DNG-like TIFF with `subifd0` + `ifd1` -> BigTIFF edit/apply -> read-back
- source DNG-like TIFF with `subifd0` + `ifd1` -> DNG edit/apply -> read-back
- source TIFF/BigTIFF with existing `subifd0 -> next` auxiliary chain ->
  replace `subifd0` -> preserve downstream auxiliary tail
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

There is now also a named in-tree transfer release gate:
- `openmeta_gate_transfer_release`
- `openmeta_transfer_release_gate`

In a non-Python test tree it runs:
- `MetadataTransferApi.*`
- `XmpDump.*`
- `ExrAdapter.*`
- `DngSdkAdapter.*`
- `openmeta_cli_metatransfer_smoke`

In a Python-enabled test tree it also runs:
- `openmeta_python_transfer_probe_smoke`
- `openmeta_python_metatransfer_edit_smoke`

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
- classic-TIFF and BigTIFF rewrite support
- bounded `ifd1` chain rewrite support, including preserving an existing
  downstream page tail when `ifd1` is replaced
- bounded TIFF/DNG-style SubIFD rewrite support, including preserving an
  existing downstream auxiliary tail when `subifdN` is replaced
- bounded front-subset `SubIFD` replacement that preserves trailing existing
  children from the target file
- bounded `ExifIFD` replacement that preserves an existing target
  `InteropIFD` when the source replacement omits its own interop child
- bounded DNG-style merge policy in the file-helper path:
  source-supplied preview/aux front structures replace the target front
  structures, while existing target page tails and trailing auxiliary
  children are preserved
- file-based helper flow
- streaming edit output

### DNG

Implemented as a dedicated public target on top of the TIFF backend.

Implemented:
- EXIF, XMP, ICC, and IPTC transfer through the TIFF-family backend
- read-backed file-helper roundtrip for JPEG-source and DNG-like-source input
- minimal `DNGVersion` synthesis when the source metadata lacks it
- explicit public target modes:
  - `ExistingTarget`
  - `TemplateTarget`
  - `MinimalFreshScaffold`
- bounded preview-page chain rewrite/merge
- bounded raw-image `SubIFD` rewrite/merge
- preservation of existing target DNG core tags when a non-DNG source is
  merged into an existing DNG target
- preserved downstream page tails and downstream auxiliary tails
- preserved trailing existing auxiliary children when only the front subset
  is replaced
- bounded `ExifIFD` replacement that preserves an existing target
  `InteropIFD` when the source replacement omits its own interop child

Current limits:
- still a bounded DNG policy layer, not a full DNG-specific rewrite engine
- broader arbitrary nested-IFD graph rewrite is still out of scope
- in the file-helper path, `ExistingTarget` and `TemplateTarget` now require
  an explicit target path; only `MinimalFreshScaffold` keeps the metadata-only
  prepare/emit path available without a backing DNG container

Optional host bridge:
- When OpenMeta is built with `OPENMETA_WITH_DNG_SDK_ADAPTER=ON` and a
  `dng_sdk` package is discoverable, `openmeta/dng_sdk_adapter.h` exposes a
  bounded Adobe DNG SDK bridge for:
  - applying prepared OpenMeta DNG bundles onto `dng_negative`
  - updating an existing `dng_stream` via the SDK's metadata-update path
  - a direct file-helper for `source file -> existing DNG file` in-place
    update
  - a matching direct Python binding over that file-helper
  - a thin CLI wrapper via `metatransfer --update-dng-sdk-file`
- This adapter is optional. Core `Dng` transfer support remains available
  without the Adobe SDK.
- The OpenMeta build must use a C++ runtime/standard library compatible with
  the discovered `dng_sdk` package.
- Public automated CI intentionally excludes this SDK-backed lane because the
  Adobe DNG SDK is not part of the public dependency/distribution story.
  Treat it as a maintainer or release-validation path rather than a default
  GitHub Actions requirement.

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

The transfer lane now also exposes:
- `build_prepared_exr_attribute_batch(...)`
- `build_exr_attribute_batch_from_file(...)`
- Python `build_exr_attribute_batch_from_file(...)` for direct file-to-batch
  host-side inspection without going through the generic transfer probe
- Python helper wrappers:
  `openmeta.python.probe_exr_attribute_batch(...)` and
  `openmeta.python.get_exr_attribute_batch(...)`

That keeps EXR host integrations on the transfer path: callers can prepare one
`TransferTargetFormat::Exr` bundle, then materialize a native
`ExrAdapterBatch` without re-projecting from the source `MetaStore`.

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
- generated portable XMP can choose how existing decoded XMP conflicts with
  generated EXIF/IPTC mappings

This gives callers stable write-side control over the most important projection
behavior without forcing them to reverse-engineer the transfer output.

## Write-Side Sync Controls

OpenMeta now has a bounded public sync-policy layer for generated XMP.

Current controls:
- `xmp_project_exif`
- `xmp_project_iptc`
- `xmp_existing_namespace_policy`
  - `KnownPortableOnly`
  - `PreserveCustom`
- `xmp_conflict_policy`
- `xmp_existing_sidecar_mode` on the file-read/prepare path:
  - `Ignore`
  - `MergeIfPresent`
- `xmp_existing_sidecar_precedence` on the file-read/prepare path:
  - `SidecarWins`
  - `SourceWins`
- `xmp_existing_destination_embedded_mode` on the file-read/prepare and
  file-helper execution paths:
  - `Ignore`
  - `MergeIfPresent`
- `xmp_existing_destination_embedded_precedence` on the file-read/prepare and
  file-helper execution paths:
  - `DestinationWins`
  - `SourceWins`
- `xmp_writeback_mode` on the file-helper execution path:
  - `EmbeddedOnly`
  - `SidecarOnly`
  - `EmbeddedAndSidecar`
- `xmp_destination_embedded_mode` on the file-helper execution path:
  - `PreserveExisting`
  - `StripExisting`
- CLI:
  - `--xmp-include-existing-sidecar`
  - `--xmp-existing-sidecar-precedence <sidecar_wins|source_wins>`
  - `--xmp-include-existing-destination-embedded`
  - `--xmp-existing-destination-embedded-precedence <destination_wins|source_wins>`
  - `--xmp-no-exif-projection`
  - `--xmp-no-iptc-projection`
  - `--xmp-conflict-policy <current|existing_wins|generated_wins>`
  - `--xmp-writeback <embedded|sidecar|embedded_and_sidecar>`
  - `--xmp-destination-embedded <preserve_existing|strip_existing>`
  - `--xmp-destination-sidecar <preserve_existing|strip_existing>`

Current behavior:
- existing XMP can still be included independently
- EXIF payload emission stays independent from EXIF-to-XMP projection
- IPTC native carrier emission stays independent from IPTC-to-XMP projection
- portable generated XMP can keep the historical mixed order, prefer existing
  decoded XMP, or prefer generated EXIF/IPTC mappings when the same portable
  property would collide
- existing sibling `.xmp` sidecars from the destination path can be merged
  into generated portable XMP before transfer packaging when explicitly
  requested
- that sidecar merge path now has explicit precedence against source-embedded
  existing XMP instead of relying on implicit decode order
- existing embedded XMP from the destination file can also be merged into
  generated portable XMP on the file-read/prepare path and on the file-helper
  path when explicitly requested
- that destination-embedded merge path has its own explicit precedence
  against source-embedded existing XMP instead of relying on implicit decode
  order
- some targets without a native IPTC carrier can still use XMP as the bounded
  fallback carrier when IPTC projection is enabled
- file-helper export can now strip prepared embedded XMP blocks and return
  canonical sidecar output guidance instead
- file-helper export can also keep generated embedded XMP while emitting the
  same generated packet as a sibling `.xmp` sidecar
- the public `metatransfer` CLI and Python transfer wrapper can now persist
  that generated XMP as a sibling `.xmp` sidecar when sidecar or dual-write
  XMP writeback is selected
- the public `metatransfer` CLI and Python transfer wrapper now also expose
  the bounded destination-embedded merge and precedence controls directly
- sidecar-only writeback now has an explicit destination embedded-XMP policy:
  - preserve existing embedded XMP by default
  - strip existing embedded XMP for `jpeg`, `tiff`, `png`, `webp`, `jp2`,
    and `jxl`
- embedded-only writeback now has an explicit destination sidecar policy:
  - preserve an existing sibling `.xmp` by default
  - strip an existing sibling `.xmp` when explicitly requested
- the C++ API now also has a bounded persistence helper for
  `execute_prepared_transfer_file(...)` results, so applications can write the
  edited file, write the generated `.xmp` sidecar, and remove a stale sibling
  `.xmp` without reimplementing wrapper-side file logic
- the Python binding now exposes the same persistence path through
  `transfer_file(...)` and `unsafe_transfer_file(...)`, and the public Python
  wrapper uses that core helper instead of maintaining its own sidecar write
  and cleanup implementation

This is deliberately narrower than a full sync engine. It does not yet define:
- full EXIF vs XMP precedence rules
- MWG-style reconciliation
- full destination embedded-vs-sidecar reconciliation policy beyond the
  current bounded merge, precedence, carrier-mode, and strip rules
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
- broader sidecar vs embedded policy beyond the current bounded writeback mode
- canonical writeback policy
- broader namespace reconciliation behavior beyond the current bounded
  custom-namespace preservation control

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
