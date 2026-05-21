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
| HEIF / AVIF / CR3 | Yes | Partial | EXIF, XMP, ICC, CR3 maker blocks, BMFF derived fields, and bounded JUMBF/C2PA |
| EXR | n/a via `scan_auto(...)` | Yes | Header attributes only; no pixel decode |

## Metadata Family Coverage

| Metadata family | Decode | Name mapping | Dump / export | Notes |
| --- | --- | --- | --- | --- |
| EXIF (`MetaKeyKind::ExifTag`) | Yes | Yes | Yes | Standard EXIF plus pointer IFDs |
| MakerNote | Partial / Yes | Partial / Yes | Lossless yes; portable limited | Broad vendor coverage; unknown tags may remain raw |
| XMP (`MetaKeyKind::XmpProperty`) | Yes | Native schema/path | Yes | Requires Expat at build time |
| ICC (`IccHeaderField`, `IccTag`) | Yes | Yes | Yes | Header fields plus tag table; raw tag payload preserved |
| IPTC-IIM (`IptcDataset`) | Yes | Yes | Yes | Raw dataset bytes preserved |
| Photoshop IRB (`PhotoshopIrb`) | Yes | Partial / Yes | Yes | Raw resources preserved plus a bounded interpreted subset |
| MPF | Yes | Yes | Yes | Basic TIFF-IFD decode |
| GeoTIFF (`GeotiffKey`) | Yes | Yes | Yes | GeoKeyDirectoryTag decode |
| BMFF derived fields (`BmffField`) | Yes | Yes | Yes | `ftyp`, item-info, `iref`, graph summaries, aux semantics, and bounded primary-linked image roles |
| JUMBF / C2PA (`JumbfField`, `JumbfCborKey`) | Partial | Yes | Yes | Draft structural and semantic layer; not full conformance |
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
subset. That subset includes common fixed-layout resources such as:
- `ResolutionInfo`
- `AlphaChannelsNames`
- `PStringCaption`
- `VersionInfo`
- `PrintFlags`
- `QuickMaskInfo`
- `JPEG_Quality`
- `UnicodeAlphaNames`
- `AlphaIdentifiers`
- `PrintScaleInfo`
- `PixelInfo`
- `ChannelOptions`
- `PrintFlagsInfo`
- `ClippingPathName`

This is useful, but it is still not full Photoshop-resource parity.

### BMFF (`HEIF` / `AVIF` / `CR3`)

OpenMeta now has a bounded semantic model on top of raw item discovery:
- `ftyp.*`
- primary item properties
- `iinf/infe` item-info rows
- item type-name and semantic labels for EXIF, XMP, JUMBF, C2PA, ICC profile,
  image, URI, auxiliary, thumbnail, derived-image, and content-description items
- typed `iref.<type>.*` rows
- graph summaries
- `auxC`-typed auxiliary semantics
- bounded primary-linked image-role fields
- primary `colr` summaries for `nclx`/`nclc` color fields and ICC profile-size
  carriers

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

- `HEIF/AVIF` scene semantics beyond the current bounded primary-linked role
  surface
- additional `JXL brob` realtypes beyond `Exif`, `xml `, `jumb`, and `c2pa`
- full `JUMBF/C2PA` semantics and policy validation
- deeper RAF model-specific native tables and X3F image-processing sections
  beyond the current bounded carrier/header/property lanes
- broader Photoshop IRB interpretation beyond the current bounded subset

## Related Docs

- [metadata_transfer_plan.md](metadata_transfer_plan.md)
- [development.md](development.md)
