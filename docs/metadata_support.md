# Metadata Support Matrix (Draft)

This document tracks current read-path support in OpenMeta and is intended as a
single reference for:
- what containers are scanned,
- which metadata blocks are decoded into `MetaStore`,
- where name mapping exists,
- what can be dumped/exported.

Status labels used below:
- `Yes`: supported in current code.
- `Partial`: supported with limits or best-effort paths.
- `No`: not supported yet.

## Container and Block Coverage

| Container / input type | Block discovery | Structured decode in `simple_meta_read` | Notes |
| --- | --- | --- | --- |
| JPEG | EXIF, XMP, extended XMP, ICC, MPF, Photoshop IRB, comment, vendor APP1/APP4 maker blocks | EXIF, MakerNote (opt-in), XMP, ICC, IPTC (from IRB), Photoshop IRB, MPF, DJI/FLIR/Casio vendor paths | One of the most complete paths today. |
| PNG | EXIF, XMP/text chunks, ICC, text | EXIF, XMP, ICC | Generic text chunks are discovered; not all become structured entries. |
| WebP | EXIF, XMP, ICC | EXIF, XMP, ICC | RIFF chunk metadata path. |
| GIF | XMP app-extension, ICC app-extension | XMP, ICC | Metadata is reassembled from GIF sub-blocks. |
| TIFF / DNG / TIFF-based RAW | TIFF stream + embedded XMP/IPTC/IRB/ICC/MakerNote tags | EXIF (incl pointer IFDs), MakerNote (opt-in), XMP, IPTC, Photoshop IRB, ICC, GeoTIFF keys | Includes RW2/ORF TIFF-variant headers. |
| CRW (CIFF) | CIFF root block | Partial (derived EXIF bridge fields) | Best-effort mapping from CIFF to common EXIF fields. |
| RAF / X3F | Embedded TIFF located heuristically | Same as TIFF decode after embed detection | Reported as `ContainerFormat::Unknown` with TIFF decode path. |
| JP2 | UUID/direct metadata boxes (EXIF/XMP/IPTC/ICC/GeoTIFF) | EXIF, XMP, IPTC, ICC, GeoTIFF | GeoJP2 TIFF payload is decoded via EXIF/TIFF path. |
| JXL | `Exif`, `xml `, `brob` compressed metadata | EXIF, XMP, partial compressed metadata decode | `brob` decode currently handles wrapped EXIF path. |
| HEIF / AVIF / CR3 (BMFF) | `meta` item graph (`iinf`/`iloc`), ICC from `iprp/ipco colr`, CR3 Canon UUID metadata | EXIF, XMP, ICC, CR3 maker blocks; BMFF derived fields (`ftyp`, primary item props, draft `iref` edge fields) | Draft `iref` relation emission is available (`iref.*`, `primary.auxl_item_id`, etc.); full auxiliary semantics are still partial. |
| EXR | n/a via `scan_auto` (decoded directly by EXR header decoder) | EXR header attributes (typed known attrs + raw fallback) | Header metadata only; no pixel decode. |

## Metadata Key-Space Coverage

| Key / block family | Decode | Name mapping | Dump / export |
| --- | --- | --- | --- |
| EXIF tag (`MetaKeyKind::ExifTag`) | Yes | Yes (standard EXIF + many MakerNote tables) | Yes (lossless sidecar, portable XMP mapping subset, OIIO/OCIO adapters) |
| MakerNote (`ExifTag` in `mk_*` IFD namespaces) | Partial/Yes (vendor-dependent, opt-in) | Partial/Yes (generated tables; unknown tags remain unnamed) | Yes in lossless; portable mode does not preserve vendor schema as standard XMP fields |
| XMP property (`MetaKeyKind::XmpProperty`) | Yes (requires Expat at build time) | Native schema URI + property path | Yes (lossless + portable) |
| ICC (`IccHeaderField`, `IccTag`) | Yes | Signature/offset based | Yes (lossless; adapter export as names/values) |
| IPTC-IIM (`IptcDataset`) | Yes | Record/dataset id based | Yes (lossless; portable behavior depends on mapping path) |
| Photoshop IRB (`PhotoshopIrb`) | Yes (raw resources) | Resource id based | Yes (lossless) |
| MPF | Yes | EXIF-like tag mapping | Yes |
| GeoTIFF key (`GeotiffKey`) | Yes | Yes (generated key-name table) | Yes |
| EXR attribute (`ExrAttribute`) | Yes (header attrs) | Attribute names are native in file | Yes (lossless + OIIO/OCIO adapters) |
| BMFF derived (`BmffField`) | Partial (primary item/ftyp + draft relation fields) | Field names are explicit (`ftyp.*`, `primary.*`, `meta.*`, draft `iref.*`) | Yes |
| JUMBF / C2PA keys (`JumbfField`, `JumbfCborKey`) | No current decode pipeline | n/a | Can be preserved/exported if entries are injected upstream |

## Tool-Level Behavior

| Tool | Purpose | Current state |
| --- | --- | --- |
| `metaread` | Human-readable metadata listing | Shows decoded entries; uses tag-name mapping where available; unknown names are shown as `-`. |
| `metadump` | Sidecar and preview dump tool | `lossless` mode preserves broad key-space data; `portable` mode targets interoperable XMP fields. |
| `thumdump` | Preview extractor | Extracts embedded preview candidates; multi-preview outputs auto-suffix (`_1`, `_2`, ...). |

## Important Current Gaps

- HEIF/AVIF auxiliary semantics are still partial: OpenMeta now emits draft
  `iref` relation fields, but richer interpretation (`auxC`, typed depth/disparity
  semantics, full relationship graph views) is not complete.
- JXL compressed metadata handling is focused on wrapped EXIF (`brob` + `Exif`).
- JUMBF/C2PA key-space exists in `MetaKeyKind` but does not yet have a full
  public decode path.

This page is intentionally marked draft and should be updated as support grows.
