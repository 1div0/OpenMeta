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

Current bounded writeback contract:

| `xmp_writeback_mode` | Edited file | Sibling `.xmp` | Default cleanup behavior |
| --- | --- | --- | --- |
| `EmbeddedOnly` | Keep generated XMP in the managed embedded carrier | No generated sidecar output | Preserve any existing sibling `.xmp` unless `xmp_destination_sidecar_mode=StripExisting` |
| `SidecarOnly` | Suppress generated embedded XMP carriers | Return generated sidecar output | Preserve existing embedded XMP unless `xmp_destination_embedded_mode=StripExisting` |
| `EmbeddedAndSidecar` | Keep generated XMP in the managed embedded carrier | Return the same generated XMP as sibling `.xmp` output | No sidecar cleanup path is requested |

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
- the Python binding now also exposes the reusable decoded-source path through
  `read_transfer_source_snapshot_file(...)`,
  `read_transfer_source_snapshot_bytes(...)`,
  `Document.build_transfer_source_snapshot()`, and
  `transfer_snapshot_probe(...)` / `transfer_snapshot_file(...)`
- public API regression coverage now asserts dual-write roundtrip and
  persistence behavior across `jpeg`, `tiff`, `dng`, `png`, `webp`, `jp2`,
  `jxl`, `heif`, `avif`, and `cr3`

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

## Three-Phase Writer Confidence Roadmap

This is the public roadmap for closing the main read/transfer/write gaps
against mature metadata tools without expanding the project scope into full
pixel editing or unbounded arbitrary metadata editing.

### Working Rule

For the current milestone:
- do not add broad new read families before the current writer target family is
  stable
- every new writer behavior should ship with explicit roundtrip and compare
  gates
- prefer one documented bounded contract per target over format-specific hidden
  behavior

### Phase 1: Writer Confidence Baseline

Goal:
- restore a fully green public tree
- make current bounded writer behavior explicit and regression-gated
- remove adoption risk caused by ambiguous sync and preservation behavior

Cross-cutting deliverables:
- fix all public unit and regression failures before adding further writer
  breadth
- publish one explicit EXIF / IPTC / XMP writeback policy for:
  - source-embedded vs destination-embedded precedence
  - source-embedded vs destination-sidecar precedence
  - canonical generated-XMP writeback rules
  - namespace preservation and canonicalization rules
- add compare-backed release validation for the current primary target family
- document unmanaged-metadata preservation rules for each writer target

Exit criteria:
- public test tree is green
- public release gates cover read-back plus compare-style validation for the
  primary target family
- writer-side XMP behavior is explicit instead of implementation-defined

### Phase 2: Stable First-Class Writer Set

Goal:
- make the current target family feel consistent across C++, CLI, and Python
- raise bounded edit paths into stable first-class writer contracts
- improve rewrite safety for vendor metadata without claiming full arbitrary
  edit parity

Cross-cutting deliverables:
- one stable user-facing edit/transfer surface across C++, CLI, and Python
- one policy surface for MakerNote, JUMBF, C2PA, EXIF projection, IPTC
  projection, and XMP carrier behavior
- stronger rewrite guarantees for preserve-vs-replace behavior on existing
  target metadata
- compare and roundtrip gates promoted from smoke coverage into release-facing
  validation for the first-class target family

Exit criteria:
- the first-class target family has documented write guarantees and matching
  regression gates
- major host surfaces expose the same bounded policy controls
- target maturity differences are reduced to known documented limits instead of
  accidental behavior

### Phase 3: Deeper Parity and Read-Depth Follow-Through

Goal:
- close the most visible remaining competitor gaps after the primary writer
  contract is stable
- deepen read semantics only where they materially improve writer confidence or
  interop parity

Cross-cutting deliverables:
- deeper modern-container semantics where current bounded projections are too
  small for parity workflows
- long-tail read-depth work for formats that still rely mainly on embedded-TIFF
  follow paths
- broader compare gates for newer target families and long-tail metadata
  structures

Exit criteria:
- remaining gaps are mostly strategic out-of-scope items rather than missing
  baseline format behavior
- OpenMeta can defend a clear public answer for which targets are first-class,
  bounded, or read-only

### Per-Target Deliverables

| Target family | Phase 1 deliverable | Phase 2 deliverable | Phase 3 follow-up |
| --- | --- | --- | --- |
| JPEG | Lock explicit sync/writeback defaults for APP1/APP2/APP13, add compare-backed read-back gates for edit/apply, and document unmanaged-marker preservation | Promote JPEG from strongest target to reference writer contract for other backends, including clearer preserve/replace guarantees for existing metadata carriers | Expand parity coverage for more mixed metadata bundles and signed/unsigned JUMBF handoff workflows where still bounded |
| TIFF | Lock rewrite guarantees for root IFD, `ExifIFD`, preview-page chains, and bounded `SubIFD` replacement; add compare-backed roundtrip gates for classic TIFF and BigTIFF | Raise current bounded rewrite behavior into a stable first-class contract, including clearer preserve/replace rules for existing auxiliary chains and downstream tails | Broaden nested-IFD graph handling only where it improves practical export parity without claiming arbitrary graph rewrite |
| DNG | Lock explicit behavior for `ExistingTarget`, `TemplateTarget`, and `MinimalFreshScaffold`; add compare-backed roundtrip gates for `DNGVersion`, preview chains, and raw `SubIFD` merge behavior | Stabilize the DNG policy layer so hosts can rely on predictable preserve/merge behavior without the Adobe SDK path | Decide whether any additional DNG-specific rewrite depth is worth public contract expansion beyond the TIFF-derived bounded model |
| PNG | Lock chunk replacement rules for `eXIf`, XMP `iTXt`, and `iCCP`; add compare-backed roundtrip gates for embedded-only, sidecar-only, and dual-carrier XMP flows where applicable | Promote the current bounded chunk path into a stable managed-metadata editor with explicit preservation rules for unrelated chunks | Add deeper parity only if compare workflows show recurring gaps against major tools |
| WebP | Lock replacement rules for `EXIF`, `XMP `, `ICCP`, and bounded `C2PA`; add compare-backed roundtrip gates | Promote the current bounded RIFF metadata path into a stable managed-metadata editor with explicit preservation guarantees for unrelated chunks | Extend only where public parity data shows material gaps for common WebP metadata workflows |
| JP2 | Lock top-level managed-box rewrite rules and `colr` replacement behavior; add compare-backed roundtrip gates | Promote bounded box rewrite into a stable JP2 metadata contract with explicit preserve/replace guarantees for unmanaged boxes | Revisit `jp2h` synthesis only if it becomes necessary for common export parity |
| JXL | Lock current box rewrite rules for `Exif`, `xml `, `jumb`, bounded `c2pa`, and encoder-side ICC handoff; add compare-backed roundtrip gates for edit/apply paths | Promote JXL from bounded but real to a stable first-class metadata target with explicit unmanaged-box preservation rules and clearer encoder/file-edit split | Add more `brob` realtype coverage only after the current direct and bounded compressed routes are stable |
| HEIF / AVIF / CR3 | Lock the bounded metadata-only `meta` edit contract, item/property preservation rules, and compare-backed roundtrip gates | Stabilize the bounded BMFF writer contract for metadata items, ICC property handling, and existing OpenMeta-authored `meta` replacement | Deepen BMFF scene semantics and relation modeling where current bounded fields are too small for parity workflows |
| EXR | Decide whether the public target remains an attribute-emitter contract or grows into a file rewrite/edit path; add compare-backed gates for the chosen contract | If EXR stays bounded, make that contract final and explicit across C++, CLI, and Python; if it grows, define one narrow first-class rewrite scope and gate it | Add depth only after the architectural choice is settled; avoid half-bounded expansion |
| RAF / X3F | Keep current follow-path reads stable and add focused compare coverage for the existing best-effort semantics | Only add read depth that directly improves downstream transfer/export confidence | Deepen native semantics beyond embedded-TIFF follow paths if parity evidence shows real user-facing gaps |
| CRW / CIFF | Keep the current bounded native CIFF projection stable and well-gated | Improve only the parts that materially affect interop or transfer workflows | Revisit deeper native legacy coverage if it becomes a recurring parity blocker |
| Photoshop IRB | Keep raw preservation stable and add compare coverage for the current interpreted subset | Expand interpreted subset only where it improves practical writer parity | Revisit broader Photoshop-resource parity after the first-class writer set is stable |
| JUMBF / C2PA | Keep bounded preserve/invalidate behavior deterministic and regression-gated | Improve public signer-handoff and bounded rewrite workflows without claiming full trust-policy parity | Revisit deeper semantics and signed rewrite coverage after the main writer contract is stable |

### Phase Ordering Summary

1. Fix public regressions and lock the sync/writeback contract.
2. Turn the current target family into a stable first-class writer set.
3. Spend follow-up effort on deeper parity lanes only after the writer
   baseline is trustworthy.

## Competitor Parity Checklist

This section is a practical tracking view for the remaining gap against
general-purpose metadata competitors.

Estimated remaining work packages:
- to reach normal still-image workflow parity close to `Exiv2`: about `8`
  major work packages
- to reach broader overall parity closer to `ExifTool`: about `12-15`
  major work packages

These are not release-percentage numbers. They are rough planning counts for
distinct parity workstreams that still matter after the current public writer
contract work.

### Package Status

Status legend:
- `Done`: public contract and regression coverage are good enough that this is
  no longer a primary parity blocker
- `Partial`: real support exists, but competitor-visible limits still remain
- `Missing`: still a clear parity gap

| Work package | Why it matters for parity | Status | Remaining package count | Main target families |
| --- | --- | --- | --- | --- |
| Public writer contract for primary targets | Competitors feel predictable on preserve/replace behavior; OpenMeta still needs that same trust level across all first-class targets | `Partial` | `1` | `TIFF`, `DNG`, `PNG`, `WebP`, `JP2`, `JXL`, `HEIF/AVIF/CR3` |
| General EXIF / IPTC / XMP sync engine | One of the biggest remaining gaps for general editing adoption | `Partial` | `1-2` | Cross-cutting |
| Compare-backed release validation | Needed to defend parity claims with repeatable read-back and compare gates | `Partial` | `1` | Cross-cutting |
| MakerNote rewrite trust | Read parity is strong, but rewrite guarantees still trail mature tools | `Partial` | `1` | `JPEG`, `TIFF`, `DNG`, RAW-derived lanes |
| TIFF / DNG deeper rewrite guarantees | Important for serious export/edit trust on camera-originated files | `Partial` | `1` | `TIFF`, `DNG` |
| BMFF writer depth beyond current bounded contract | Needed for stronger `HEIF/AVIF/CR3` parity beyond the current metadata-only `meta` model | `Partial` | `1` | `HEIF`, `AVIF`, `CR3` |
| Modern container read-depth follow-through | Remaining visible read gaps are mostly here | `Partial` | `1` | `HEIF/AVIF`, `JXL` |
| Long-tail native format semantics | Matters more against `ExifTool` than against `Exiv2` | `Partial` | `2-3` | `RAF`, `X3F`, `CRW/CIFF`, `Photoshop IRB` |
| EXR target decision | Current EXR target is real but still architecturally bounded | `Partial` | `1` | `EXR` |
| JUMBF / C2PA deeper semantics | Current support is bounded and useful, but not full trust-policy parity | `Partial` | `1-2` | `JPEG`, `PNG`, `WebP`, `JXL`, `BMFF` |
| Full arbitrary metadata editing parity | Mature competitors expose a broader open-ended editor surface | `Missing` | Strategic / out of scope | Cross-cutting |

### Format-Family Gap Map

This map is intentionally coarse. It answers where the main remaining work
still sits after the current public regression and writer-contract work.

| Format family | Read parity | Transfer/write parity | Main remaining competitor gap |
| --- | --- | --- | --- |
| `JPEG` | `Strong` | `Strong` | needs continued compare-backed hardening and deeper mixed-bundle parity, not a new baseline writer |
| `TIFF` | `Strong` | `Partial` | deeper rewrite guarantees for more existing-graph and tail-preservation cases |
| `DNG` | `Strong` | `Partial` | more predictable preserve/merge behavior across target modes and raw `SubIFD` chains |
| `PNG` | `Strong` | `Partial` | stable unmanaged-chunk preservation contract and broader compare-backed validation |
| `WebP` | `Strong` | `Partial` | stable unrelated-chunk preservation contract and broader compare-backed validation |
| `JP2` | `Strong` | `Partial` | stronger managed-box preservation guarantees and more roundtrip validation |
| `JXL` | `Strong` on current lanes | `Partial` | more explicit box-preservation guarantees and deeper `brob` realtype follow-through |
| `HEIF / AVIF / CR3` | `Strong` on tracked lanes | `Partial` | BMFF writer depth and deeper scene/relation semantics beyond the bounded current model |
| `EXR` | `Bounded but real` | `Bounded but real` | still needs an explicit long-term decision between stable bounded target vs rewrite/edit path |
| `RAF / X3F` | `Partial` | not a main writer lane | deeper native semantics beyond embedded-TIFF follow paths |
| `CRW / CIFF` | `Partial` | bounded | legacy native depth still trails mature tools |
| `Photoshop IRB` | `Partial` | bounded preservation | interpreted subset still smaller than mature tools |
| `JUMBF / C2PA` | `Partial` | bounded | deeper semantics, trust-policy behavior, and signed rewrite parity remain out of scope |

### Practical Readout

If OpenMeta stops after the current writer-contract work, it can already argue
that it is close to competitor parity on the main tracked still-image targets.

To get materially closer to `Exiv2`, the remaining work is mostly:
- finish stable writer guarantees for the first-class target family
- finish the broader sync policy
- harden compare-backed release validation
- improve TIFF/DNG/BMFF rewrite trust

To get materially closer to `ExifTool`, OpenMeta also needs:
- more long-tail native format depth
- broader general editing behavior
- deeper `JUMBF/C2PA` semantics
- a clearer answer for `EXR`

### Execution Order

Use this as the practical delivery order for the remaining parity work.

Priority legend:
- `Now`: should be in the next active delivery slice
- `Next`: should start after the `Now` slice is stable
- `Later`: important for broader parity, but not the next blocker

| Work package | Priority | Why this order |
| --- | --- | --- |
| Public writer contract for primary targets | `Now` | This is the core trust gap that still keeps OpenMeta below mature writer parity |
| General EXIF / IPTC / XMP sync engine | `Now` | This is still one of the biggest adoption blockers for general edit workflows |
| Compare-backed release validation | `Now` | Parity claims remain weaker until compare-backed gates are release-facing instead of mostly API-facing |
| TIFF / DNG deeper rewrite guarantees | `Now` | This is the highest-risk writer lane for serious still-image export confidence |
| BMFF writer depth beyond current bounded contract | `Next` | `HEIF/AVIF/CR3` are already real targets, but the bounded writer model still needs more depth for stronger parity |
| MakerNote rewrite trust | `Next` | Important for trust, but it benefits from the writer-contract and validation work landing first |
| Modern container read-depth follow-through | `Next` | Visible gap, but less urgent than finishing the current writer baseline |
| EXR target decision | `Next` | Needs an explicit product choice, but should follow the main writer-contract stabilization work |
| Long-tail native format semantics | `Later` | Matters more for broad `ExifTool` parity than for the first still-image writer milestone |
| JUMBF / C2PA deeper semantics | `Later` | Current bounded behavior is already useful; deeper trust semantics should wait until the core writer contract is stable |
| Full arbitrary metadata editing parity | `Later` | Strategic follow-up, not part of the next parity-closing milestone |

Suggested delivery sequence:
1. Finish the stable writer contract for the first-class target family.
2. Finish the broader sync-policy layer and compare-backed release validation.
3. Harden the two highest-risk writer lanes: `TIFF/DNG` and bounded `BMFF`.
4. Revisit MakerNote rewrite trust after the first-class writer contract is stable.
5. Spend follow-up time on modern-container depth, `EXR`, and long-tail native semantics only after the main writer baseline is defendable.

### Now Slice Implementation Board

This board turns the current `Now` slice into concrete delivery checklists.

The sync item here means the bounded next-slice policy completion needed for
practical writer parity. It does not mean full arbitrary EXIF/IPTC/XMP sync
parity across every workflow.

#### 1. Public Writer Contract For Primary Targets

- [ ] document final preserve-vs-replace behavior for existing embedded XMP on `TIFF`, `DNG`, `PNG`, `WebP`, `JP2`, `JXL`, and bounded `BMFF`
- [ ] document final preserve-vs-replace behavior for destination sidecars across embedded-only, sidecar-only, and dual-write flows
- [ ] lock explicit unmanaged-metadata preservation rules for unrelated chunks, boxes, items, and tails per target family
- [ ] add compare-backed read-back gates for each first-class target instead of relying mainly on API-shape regression coverage
- [ ] make CLI and Python surfaces describe the same writeback behavior and path-derivation rules as the C++ helper
- [ ] reduce remaining target differences to documented limits instead of accidental implementation details

#### 2. Bounded EXIF / IPTC / XMP Sync Layer

- [ ] publish one final precedence table for source embedded XMP, destination embedded XMP, and destination sidecar XMP
- [ ] lock conflict behavior for generated EXIF-to-XMP and IPTC-to-XMP projections when existing XMP is also present
- [ ] lock canonical generated-XMP writeback behavior for embedded-only, sidecar-only, and dual-write flows
- [ ] lock namespace preservation and canonicalization rules for managed vs unmanaged XMP content
- [ ] add regression cases for mixed embedded-plus-sidecar destination states across the primary target family
- [ ] document the explicit non-goals of this bounded sync layer so it is not confused with full arbitrary sync parity

#### 3. Compare-Backed Release Validation

- [ ] promote the current primary-target roundtrip checks into explicit release-facing compare gates
- [ ] add compare-backed validation for `TIFF`, `DNG`, `PNG`, `WebP`, `JP2`, `JXL`, and bounded `BMFF` target outputs
- [ ] cover embedded-only, sidecar-only, and dual-write XMP flows in release-facing compare validation
- [ ] add compare-backed validation for explicit sidecar-base overrides and destination-sidecar cleanup behavior
- [ ] gate the primary writer family on deterministic read-back of managed metadata after edit/apply
- [ ] keep public parity claims tied to compare-backed evidence instead of only unit or smoke coverage

#### 4. TIFF / DNG Deeper Rewrite Guarantees

- [ ] lock rewrite guarantees for classic TIFF and BigTIFF root IFD, `ExifIFD`, preview chains, and bounded `SubIFD` replacement
- [ ] lock explicit DNG behavior for `ExistingTarget`, `TemplateTarget`, and `MinimalFreshScaffold`
- [ ] add compare-backed roundtrip gates for preview chains, raw `SubIFD` merge behavior, and `DNGVersion` persistence
- [ ] document preserve-vs-replace guarantees for existing auxiliary IFD chains and downstream tails
- [ ] harden read-back and rewrite tests around mixed existing metadata carriers on camera-originated TIFF/DNG files
- [ ] define the bounded edge of TIFF/DNG rewrite depth clearly enough that hosts know what is guaranteed and what is not

#### Done-When Readout

- [ ] the first-class target family has one explicit public writer contract
- [ ] the bounded sync-policy layer is documented and regression-gated
- [ ] release-facing compare validation covers the main still-image writer set
- [ ] `TIFF/DNG` rewrite guarantees are strong enough to stop being a primary parity blocker
- [ ] the next work slice can move to bounded `BMFF` depth instead of still backfilling the writer baseline

### Host Integration And Adoption Backlog

This backlog captures adoption-oriented work that supports host projects with
flat metadata models and deferred output writes. It should not replace the
writer-confidence slice above; it should be sequenced around it.

#### Near-Term Host Contract Work

- [x] add a small runtime capability query API for read, structured decode,
  transfer preparation, and target edit support by format and metadata family
- [ ] mark public host-facing APIs with stability levels such as stable,
  experimental, or internal; start with `visit_metadata(...)`, snapshot
  read/build, fileless execution, and bundle execution
- [ ] publish the generic `FlatHost` mapping contract: name style, duplicate
  handling, type projection, deterministic ordering, and namespace behavior
- [ ] add a deterministic compatibility dump for names, values, scalar types,
  origins, and transfer/writeback decisions so downstream tests can avoid
  binary-packet baselines
- [ ] document final conflict and precedence decisions for generated EXIF/XMP,
  IPTC/XMP, source embedded XMP, destination embedded XMP, and destination
  sidecar XMP

#### Medium-Term Fidelity Work

- [ ] design an opt-in raw-preserving `TransferSourceSnapshot` mode that keeps
  original carrier bytes alongside decoded `MetaStore` state
- [ ] preserve bounded per-carrier provenance in raw snapshots: container type,
  block kind, byte range, original order, route identity, and decoded entries
  derived from the block
- [ ] define policy choices for raw passthrough vs decoded re-emission when the
  destination container can safely accept either form
- [ ] provide versioned snapshot serialization only after the raw/provenance
  model is settled
- [ ] provide full prepared-bundle serialization if hosts need to prepare once
  and apply later to encoded target bytes

#### Supporting Work

- [ ] improve structured diagnostics with severity, stable code, carrier/family,
  offset or byte range where available, and short host-facing messages
- [ ] extend resource accounting beyond current hard limits with preflight
  estimates for prepared transfers, sidecar output, and serialized snapshots
- [ ] add clean-room public micro fixtures for host integration tests; do not use
  private corpus files, vendor drops, or scraped/spec text
- [ ] add examples for `read bytes -> snapshot -> target bytes -> edited bytes`
  and `visit_metadata(...) -> flat host attribute list`
- [ ] consider a bounded random-access IO callback only after bytes/file APIs
  and raw snapshot serialization are stable
- [ ] defer any C ABI or opaque-handle facade until the host-facing C++ API is
  stable enough to freeze a narrow bridge

## Postponed Work

Still out of scope for the current milestone:
- full arbitrary metadata editing parity
- full C2PA signed rewrite / trust-policy parity
- full EXIF / IPTC / XMP sync engine
- broad TIFF/DNG and BMFF rewrite parity beyond the bounded current targets
- mandatory raw-passthrough snapshots or snapshot serialization in the default
  read path
- C ABI / opaque-handle stability commitments

## Practical Summary

OpenMeta is no longer blocked by read-path quality for adoption-oriented
transfer work.

The main opportunity now is to make the current bounded transfer core easier
to use and easier to trust across the primary export targets, instead of
continuing to expand read-only surface area first.
