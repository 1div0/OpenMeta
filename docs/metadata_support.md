# Metadata Support Matrix (Draft)

This document summarizes the current read-path coverage in OpenMeta.

It is meant to answer four basic questions:
- which containers are scanned
- which metadata families are decoded into `MetaStore`
- where display-name mapping exists
- what can be dumped or exported today

## Status Labels

- `Yes`: supported in current code
- `Partial`: supported, but still bounded or best-effort
- `No`: not supported yet

Host integrations can query the same kind of runtime support information with
`openmeta/metadata_capabilities.h`. That API reports read, structured decode,
transfer preparation, target edit, and raw-preservation support by target
format and metadata family.

For the public camera RAW read-depth plan against ExifTool-style coverage, see
[raw_read_parity_plan.md](raw_read_parity_plan.md).

## Coverage Snapshot

Current tracked-gate status:

- EXIF tag-id compare gates are passing on tracked `HEIC/HEIF`, `CR3`, and
  mixed RAW corpora.
- Standalone EXIF/TIFF payload recovery is covered for files with a short
  non-TIFF prefix or a malformed JPEG prefix before the `Exif` preamble.
- EXR header metadata compare is passing for the documented
  name/type/value-class contract.
- Sidecar export paths (`lossless` and `portable`) are covered by baseline and
  smoke tests.
- MakerNote coverage is tracked by baseline gates with broad vendor support;
  unknown vendor tags are preserved as raw metadata for lossless workflows.
  Current public naming includes Fujifilm flash white-balance and native RAF
  firmware fields, plus selected model-specific Sony correction-offset routes.
- Decoded vendor MakerNote sub-IFDs are interpreted/query metadata. Writers do
  not reconstruct MakerNote blobs from those decoded fields; they preserve the
  original raw MakerNote payload when it is present.
- Metadata-family presence gates for XMP, ICC, IPTC-IIM, Photoshop IRB, and
  JUMBF/C2PA are clean on the tracked still-image corpus. Current read coverage
  includes EXIF/TIFF-carried ICC/IPTC payloads, bare JPEG APP1 XMP packets, and
  XMP packets using alternate `xmpmeta` namespace prefixes.
- BMFF edge-path tests include `iloc` construction-method-2 relation variants
  and safe-skip handling for invalid references.

## Container Coverage

| Container / input type | Block discovery | Structured decode in `simple_meta_read(...)` | Notes |
| --- | --- | --- | --- |
| JPEG | Yes | Yes | EXIF, standard and bare APP1 XMP, extended XMP, ICC, MPF, Photoshop IRB, comments, vendor APP blocks, and bounded JUMBF/C2PA |
| PNG | Yes | Yes | EXIF, XMP, ICC, structured PNG text, and bounded JUMBF/C2PA |
| WebP | Yes | Yes | EXIF, XMP, ICC, and bounded JUMBF/C2PA |
| GIF | Yes | Partial | XMP, ICC, and structured comments |
| TIFF / DNG / TIFF-based RAW | Yes | Yes | EXIF, MakerNote, XMP, IPTC, Photoshop IRB, ICC, GeoTIFF, and bounded JUMBF/C2PA |
| CRW / CIFF | Yes | Partial | Recursive CIFF directories, stable scalar/subtable decode, derived EXIF bridge, and bounded native Canon CIFF naming/projection |
| RAF / X3F | Partial | Partial | RAF includes header-declared preview-JPEG EXIF/XMP discovery, FujiIFD/TIFF follow path, native RAF header/directory geometry tags, RAFData geometry projection, and standalone XMP fallback; X3F includes header fields, known PROP properties, section-directory JPEG metadata follow path, and legacy embedded-EXIF fallback |
| JP2 | Yes | Yes | EXIF, XMP, IPTC, ICC, and GeoTIFF |
| JXL | Yes | Yes | EXIF, XMP, and bounded JUMBF/C2PA; supported `brob` wrapped metadata is decoded |
| HEIF / AVIF / CR3 | Yes | Partial | EXIF, XMP, ICC, CR3 maker blocks, BMFF derived fields including `avif`/`avis`/`avio` AVIF brands, and bounded JUMBF/C2PA |
| EXR | n/a via `scan_auto(...)` | Yes | Header attributes only; no pixel decode |

## Metadata Family Coverage

| Metadata family | Decode | Name mapping | Dump / export | Notes |
| --- | --- | --- | --- | --- |
| EXIF (`MetaKeyKind::ExifTag`) | Yes | Yes | Yes | Standard EXIF plus pointer IFDs, including current EXIF 3.1 learning/development/correction/noise tag names and bounded correction/noise value labels |
| MakerNote | Partial / Yes | Partial / Yes | Lossless yes; portable limited | Broad vendor coverage; unknown tags may remain raw; selected version/firmware payloads, live-vendor scalar fields, Fujifilm flash white-balance and native RAF firmware names, selected Sony correction-offset routes, Reconyx scalar/string-coded fields, Microsoft stitch fields, Nintendo category fields, Sanyo public-context scalar fields, current Canon RF/Nikon Z lens labels, and an ambiguous Pentax Sigma/Samsung/Tokina lens-family label have bounded display helpers |
| XMP (`MetaKeyKind::XmpProperty`) | Yes | Native schema/path | Yes | Requires Expat at build time |
| ICC (`IccHeaderField`, `IccTag`) | Yes | Yes | Yes | Header fields plus tag table; raw tag payload preserved |
| IPTC-IIM (`IptcDataset`) | Yes | Yes | Yes | Raw dataset bytes preserved |
| Photoshop IRB (`PhotoshopIrb`) | Yes | Partial / Yes | Yes | Raw resources preserved, bounded interpreted subset, fixed-layout, Lightroom workflow, IPTC-NAA byte count, descriptor-header, simple descriptor-item, enum, and nested list/object summaries, embedded IPTC/XMP/ICC decode |
| MPF | Yes | Yes | Yes | Basic TIFF-IFD decode |
| GeoTIFF (`GeotiffKey`) | Yes | Yes | Yes | GeoKeyDirectoryTag decode |
| BMFF derived fields (`BmffField`) | Yes | Yes | Yes | `ftyp` brand names including `avif`/`avis`/`avio` AVIF-compatible brands, item-info, `ipco`, `ipma`, `iref`, `grpl`, `iloc`/`idat`, graph summaries, aux semantics, primary item properties, primary metadata-carrier flags, primary sidecar summaries, primary-scene summaries, and bounded primary-linked image roles |
| JUMBF / C2PA (`JumbfField`, `JumbfCborKey`) | Partial | Yes | Yes | Draft structural and semantic layer with box labels; not full conformance |
| EXR attributes (`ExrAttribute`) | Yes | Native names | Yes | Header attributes only |

## Important Bounded Areas

### CRW / CIFF

OpenMeta now does more than a pure derived-EXIF bridge:
- common native CIFF tags are named
- a bounded set of native subtables is projected
- stable scalar native CIFF fields are decoded where the layout is clear

It is still a partial lane compared to the deepest legacy Canon tooling.

### RAF

OpenMeta now follows both RAF metadata carriers that matter for common camera
files:
- the header-declared preview JPEG is scanned for standard EXIF/XMP-style
  metadata
- the header-declared FujiIFD/TIFF area is scanned for native RAF/raw fields
- native RAF header, directory, and RAFData-derived fields are classified as
  source-specific metadata for rendered-transfer safety

Deeper model-specific RAF sections remain a partial lane.

### X3F

OpenMeta now has a bounded native Sigma X3F lane:
- common X3F header fields are decoded as `x3f_header`
- stable header-extension adjustment fields are decoded as `x3f_header_ext`
- known `PROP` properties are decoded as `x3f_prop`
- section-directory JPEG metadata is followed for embedded EXIF/XMP-style
  metadata blocks, with the older embedded-EXIF scan kept as fallback

Deeper image-processing/compression sections remain partial and should only be
promoted when fields can be typed, named, and safety-classified.

### Photoshop IRB

OpenMeta preserves raw IRB resources and also decodes a bounded interpreted
subset. That subset includes common fixed-layout resources and descriptor-header
summaries such as:
- `Photoshop2Info`
- `Photoshop2ColorTable`
- `ResolutionInfo`
- `AlphaChannelsNames`
- `DisplayInfo`
- `PStringCaption`
- `BorderInformation`
- `BackgroundColor`
- `VersionInfo`
- `PrintFlags`
- print-flag byte fields
- `EffectiveBW`
- `QuickMaskInfo`
- `RawImageMode`
- `JPEG_Quality`
- `GridGuidesInfo`
- legacy halftone, transfer-function, duotone-image, and EPS byte summaries
- `PhotoshopBGRThumbnail` / `PhotoshopThumbnail` headers
- `SpotHalftone`
- `UnicodeAlphaNames`
- `AlphaIdentifiers`
- `JumpToXPEP`
- `PrintScaleInfo`
- `PixelInfo`
- `AutoSaveFilePath`
- `AutoSaveFormat`
- `XMLData`
- `ImageReadyVariables`
- `ImageReadyDataSets`
- `LightroomWorkflow`
- `IPTCDataBytes`
- `ColorSamplersResource` / `ColorSamplersResource2` headers and records
- `WorkingPath` and numbered path resources as record counts/selectors
- descriptor-header summaries, bounded simple descriptor item bodies, enum
  value summaries, raw-data descriptor byte counts, and bounded nested
  list/object traversal with item paths, depths, list indices, and
  parsed-value counts for resources such as
  `LayerComps`,
  `MeasurementScale`, `HDRToningInfo`, `PrintInfo`, `TimelineInfo`,
  `SheetDisclosure`, `OnionSkins`, `CountInfo`, `PrintInfo2`, `PrintStyle`,
  `PathSelectionState`, and `OriginPathInfo`
- `ChannelOptions`
- `PrintFlagsInfo`
- `ClippingPathName`
- embedded IPTC-NAA, ICC, EXIF, EXIF2, and XMP resource byte counts

When enabled, embedded IPTC-IIM, XMP, and ICC payloads in IRB resources are
also decoded into their regular OpenMeta entry families.

Current Photoshop IRB interpretation status:

| Resource area | Status | Notes |
| --- | --- | --- |
| Raw IRB resources | Preserved | Every accepted resource keeps a lossless `PhotoshopIrb` raw entry. |
| Embedded IPTC/XMP/ICC/EXIF carriers | Interpreted where enabled | Payload bytes remain preserved; enabled decoders also emit regular family entries. |
| Fixed-layout scalar/string resources | Interpreted | Resolution, alpha/caption strings, version, print flags, quick mask, JPEG quality, URL/list, pixel info, autosave strings, `XMLData`, ImageReady text, Lightroom workflow text, and similar bounded fields. |
| Geometry/display/color-sampler/path headers | Interpreted / record-summary | Display/grid/thumbnail/color-sampler headers are decoded; path resources expose record counts and selectors without interpreting Bezier payloads. |
| Descriptor-backed Photoshop resources | Descriptor header plus bounded item summaries | Descriptor version, remaining byte count, class ID/name, item count, complete simple item bodies for `bool`, `long`, `doub`, `UntF`, `TEXT`, and `enum`, raw-data descriptor byte counts, nested object/list paths, depths, list indices, list/object counts, and parsed-value count fields are emitted. Raw descriptor-data payload contents and full Photoshop action semantics remain out of scope for this subset. |
| Legacy halftone/transfer/duotone/EPS resources | Header / byte-count only | OpenMeta emits byte counts and first header words where safe, without claiming full Photoshop semantics. |
| Proprietary, obsolete, or OS-specific resources | Raw-only | Kept losslessly until a bounded public layout is implemented. |

This is useful, but it is still not full Photoshop-resource parity.

### BMFF (`HEIF` / `AVIF` / `CR3`)

OpenMeta now has a bounded semantic model on top of raw item discovery:
- `ftyp.*`, including brand names and compatible-brand counts
- primary item properties, primary metadata-carrier flags, primary sidecar
  counts/flags for linked metadata and image sidecars, content-bound
  C2PA/JUMBF sidecar policy hints, and compact primary-scene node/edge
  summaries
- `ipco` property-container summaries with total, known, unknown, and
  per-known-type property counts
- `ipma` item-property association rows with item ids, property indices,
  essential flags, and known property type names
- per-known-property `ipma.<type>` association, primary-association, and
  essential-count rollups for common primary-item properties
- `iinf/infe` item-info rows
- item type-name and semantic labels for EXIF, XMP, JUMBF, C2PA, ICC profile,
  image, URI, auxiliary, thumbnail, derived-image, and content-description items
- aggregate item semantic counters for known, metadata, image, EXIF, XMP,
  JUMBF, C2PA, ICC profile, auxiliary, derived, thumbnail,
  content-description, URI, and JSON roles
- typed `iref.<type>.*` rows
- bounded `grpl` item-group rows and per-group-type summaries
- bounded `iloc`/`idat` item-data layout summaries, including construction
  methods, extent counts, total extent byte counts, idat byte counts, and
  primary-item location aliases
- graph summaries
- `auxC`-typed auxiliary semantics
- bounded primary-linked image-role fields, compact primary sidecar aggregate
  fields, and compact primary-scene aggregate fields
- primary `colr` summaries for `nclx`/`nclc` color fields and ICC profile-size
  carriers
- primary `pasp`, `pixi`, and `clap` item-property summaries for pixel aspect
  ratio, pixel component bit depth, and clean aperture

This is intentionally smaller than a full QuickTime/BMFF semantic model.

### JXL

OpenMeta decodes:
- direct `Exif`
- direct `xml `
- direct `jumb`
- direct `c2pa`
- wrapped `brob` forms for those same realtypes

Other `brob` realtypes are still out of scope.

### JUMBF / C2PA

Current support is intentionally draft:
- structural BMFF box decode
- JUMBF box labels from parsed `jumd` boxes
- bounded CBOR traversal
- draft `c2pa.semantic.*` projection
- draft verify scaffolding with opt-in trusted certificate-chain enforcement

What this means in practice:
- OpenMeta can expose useful manifest / claim / signature / ingredient shape
  information
- OpenMeta can report signature verification and certificate-chain trust as
  separate signals, or fail verification when
  `verify_require_trusted_chain` / `--c2pa-verify-require-trusted-chain` is set
- OpenMeta does not yet claim full C2PA manifest semantics or full policy
  validation

## Tool-Level Behavior

| Tool | Purpose | Current state |
| --- | --- | --- |
| `metaread` | Human-readable metadata listing | Shows decoded entries with mapped names where available |
| `metavalidate` | Metadata validation | Reports decode-status and validation issues with machine-readable issue codes |
| `metadump` | Sidecar and preview dump tool | Supports `lossless` and `portable` sidecar output plus preview extraction |
| `thumdump` | Preview extractor | Extracts embedded preview candidates |
| `metatransfer` | Transfer/edit smoke tool | Exercises the transfer core for supported target families |

## Main Current Gaps

- `HEIF/AVIF` scene semantics beyond the current bounded primary-linked role,
  primary sidecar, and compact primary-scene surfaces
- additional `JXL brob` realtypes beyond `Exif`, `xml `, `jumb`, and `c2pa`
- full `JUMBF/C2PA` semantics and policy validation
- deeper RAF model-specific native tables and X3F image-processing sections
  beyond the current bounded carrier/header/property lanes
- full Photoshop action semantics and broader IRB interpretation beyond the
  current bounded subset

## Related Docs

- [metadata_transfer_plan.md](metadata_transfer_plan.md)
- [development.md](development.md)
