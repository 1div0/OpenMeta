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

## Coverage Snapshot

Current tracked-gate status:

- EXIF tag-id compare gates are passing on tracked `HEIC/HEIF`, `CR3`, and
  mixed RAW corpora.
- EXR header metadata compare is passing for the documented
  name/type/value-class contract.
- Sidecar export paths (`lossless` and `portable`) are covered by baseline and
  smoke tests.
- MakerNote coverage is tracked by baseline gates with broad vendor support;
  unknown vendor tags are preserved as raw metadata for lossless workflows.
- BMFF edge-path tests include `iloc` construction-method-2 relation variants
  and safe-skip handling for invalid references.

## Container Coverage

| Container / input type | Block discovery | Structured decode in `simple_meta_read(...)` | Notes |
| --- | --- | --- | --- |
| JPEG | Yes | Yes | EXIF, XMP, extended XMP, ICC, MPF, Photoshop IRB, comments, vendor APP blocks, and bounded JUMBF/C2PA |
| PNG | Yes | Yes | EXIF, XMP, ICC, structured PNG text, and bounded JUMBF/C2PA |
| WebP | Yes | Yes | EXIF, XMP, ICC, and bounded JUMBF/C2PA |
| GIF | Yes | Partial | XMP, ICC, and structured comments |
| TIFF / DNG / TIFF-based RAW | Yes | Yes | EXIF, MakerNote, XMP, IPTC, Photoshop IRB, ICC, GeoTIFF, and bounded JUMBF/C2PA |
| CRW / CIFF | Yes | Partial | Derived EXIF bridge plus bounded native Canon CIFF naming and projection |
| RAF / X3F | Partial | Partial | Mainly embedded-TIFF follow path with best-effort extras |
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

### Photoshop IRB

OpenMeta preserves raw IRB resources and also decodes a bounded interpreted
subset. That subset includes common fixed-layout resources such as:
- `ResolutionInfo`
- `VersionInfo`
- `PrintFlags`
- `JPEG_Quality`
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
- typed `iref.<type>.*` rows
- graph summaries
- `auxC`-typed auxiliary semantics
- bounded primary-linked image-role fields

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
- draft verify scaffolding

What this means in practice:
- OpenMeta can expose useful manifest / claim / signature / ingredient shape
  information
- OpenMeta does not yet claim full C2PA manifest semantics, trust semantics,
  or full policy validation

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
- deeper `RAF / X3F` native semantics beyond embedded-TIFF follow paths
- broader Photoshop IRB interpretation beyond the current bounded subset

## Related Docs

- [metadata_transfer_plan.md](metadata_transfer_plan.md)
- [development.md](development.md)
