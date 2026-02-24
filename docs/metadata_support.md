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
| JPEG | EXIF, XMP, extended XMP, ICC, MPF, Photoshop IRB, comment, vendor APP1/APP4 maker blocks, JUMBF/C2PA (APP11) | EXIF, MakerNote (opt-in), XMP, ICC, IPTC (from IRB), Photoshop IRB, MPF, draft JUMBF/C2PA decode, DJI/FLIR/Casio vendor paths | One of the most complete paths today. |
| PNG | EXIF, XMP/text chunks, ICC, text, JUMBF/C2PA (caBX) | EXIF, XMP, ICC, draft JUMBF/C2PA decode | Generic text chunks are discovered; not all become structured entries. |
| WebP | EXIF, XMP, ICC, JUMBF/C2PA (C2PA chunk) | EXIF, XMP, ICC, draft JUMBF/C2PA decode | RIFF chunk metadata path. |
| GIF | XMP app-extension, ICC app-extension | XMP, ICC | Metadata is reassembled from GIF sub-blocks. |
| TIFF / DNG / TIFF-based RAW | TIFF stream + embedded XMP/IPTC/IRB/ICC/MakerNote tags; best-effort JUMBF/C2PA in tag blobs (BMFF `jumb`/`c2pa`) | EXIF (incl pointer IFDs), MakerNote (opt-in), XMP, IPTC, Photoshop IRB, ICC, GeoTIFF keys, draft JUMBF/C2PA decode | Includes RW2/ORF TIFF-variant headers. |
| CRW (CIFF) | CIFF root block | Partial (derived EXIF bridge fields) | Best-effort mapping from CIFF to common EXIF fields. |
| RAF / X3F | Embedded TIFF located heuristically; RAF standalone XMP signature scan | Same as TIFF decode after embed detection (plus RAF XMP best-effort decode when present) | Reported as `ContainerFormat::Unknown` with TIFF decode path. |
| JP2 | UUID/direct metadata boxes (EXIF/XMP/IPTC/ICC/GeoTIFF) | EXIF, XMP, IPTC, ICC, GeoTIFF | GeoJP2 TIFF payload is decoded via EXIF/TIFF path. |
| JXL | `Exif`, `xml `, `jumb`, `brob` compressed metadata | EXIF, XMP, draft JUMBF/C2PA decode, compressed metadata decode (brob: Exif/xml/jumb/c2pa realtypes) | `brob` decode dispatches wrapped EXIF, XMP, and JUMBF/C2PA payloads by realtype. |
| HEIF / AVIF / CR3 (BMFF) | `meta` item graph (`iinf`/`iloc`), ICC from `iprp/ipco colr`, CR3 Canon UUID metadata | EXIF, XMP, ICC, CR3 maker blocks; BMFF derived fields (`ftyp`, primary item props, draft `iref` edge fields, typed `iref.<type>.*` rows, `iref` graph summary fields, `auxC`-typed aux fields), draft JUMBF/C2PA block decode | Draft `iref` relation emission is available (`iref.*`), including typed rows for `auxl`/`dimg`/`thmb`/`cdsc`, per-type edge counters and per-type unique source/target counters (`iref.<type>.from_item_unique_count`, `iref.<type>.to_item_unique_count`), and graph-summary fields (`iref.item_count`, `iref.from_item_unique_count`, `iref.to_item_unique_count`, `iref.item_*_edge_count`). Aux semantics are typed via `auxC` (`aux.item_id`, `aux.semantic`, `aux.type`, `aux.subtype_hex`, `aux.subtype_kind`, `aux.subtype_u32`, `primary.auxl_semantic`, `primary.depth_item_id`, ...). `iloc` supports construction_method 0(file), 1(`idat`), and 2(item offset via `iref/iloc` references; slices spanning multiple referenced extents are emitted as multipart blocks). `iloc` items with `data_reference_index` that resolve to self-contained `dref/url ` entries are treated as local; non-self-contained references are skipped safely per item, and out-of-range known-item extents are skipped best-effort. JUMBF/C2PA is draft phase-3 (box structure + bounded CBOR key/value decode, including indefinite CBOR forms, plus draft `c2pa.semantic.*` projections), not full manifest semantics yet. |
| EXR | n/a via `scan_auto` (decoded directly by EXR header decoder) | EXR header attributes (typed known attrs + raw fallback) | Header metadata only; no pixel decode. |

## Metadata Key-Space Coverage

| Key / block family | Decode | Name mapping | Dump / export |
| --- | --- | --- | --- |
| EXIF tag (`MetaKeyKind::ExifTag`) | Yes | Yes (standard EXIF + many MakerNote tables) | Yes (lossless sidecar, portable mapping subset, OIIO/OCIO adapters; DNG color tags are exported with `dng:`/`DNG:` adapter names when DNG context is detected) |
| MakerNote (`ExifTag` in `mk_*` IFD namespaces) | Partial/Yes (vendor-dependent, opt-in) | Partial/Yes (generated tables; unknown tags remain unnamed) | Yes in lossless; portable mode does not preserve vendor schema as standard XMP fields |
| XMP property (`MetaKeyKind::XmpProperty`) | Yes (requires Expat at build time) | Native schema URI + property path | Yes (lossless + portable) |
| ICC (`IccHeaderField`, `IccTag`) | Yes | Signature/offset based with typed header interpretation (u32/u64/s15Fixed16 where stable) plus best-effort tag payload interpretation helpers (`desc`, `text`, `sig `, `mluc`, `dtim`, `view`, `meas`, `sf32`, `uf32`, `mft1`, `mft2`, `mAB`, `mBA`, `XYZ `, `curv`, `para`) | Yes (lossless; canonical + adapter-friendly `icc:*`/`ICC:*` names) |
| IPTC-IIM (`IptcDataset`) | Yes | Record/dataset id based | Yes (lossless; portable behavior depends on mapping path) |
| Photoshop IRB (`PhotoshopIrb`) | Yes (raw resources) | Resource id based | Yes (lossless) |
| MPF | Yes | EXIF-like tag mapping | Yes |
| GeoTIFF key (`GeotiffKey`) | Yes | Yes (generated key-name table) | Yes |
| EXR attribute (`ExrAttribute`) | Yes (header attrs) | Attribute names are native in file | Yes (lossless + OIIO/OCIO adapters) |
| BMFF derived (`BmffField`) | Partial (primary item/ftyp + draft relation fields + typed `iref.<type>.*` rows + `iref` graph summaries + `auxC` typing) | Field names are explicit (`ftyp.*`, `primary.*`, `meta.*`, draft `iref.*`, `aux.*`) | Yes |
| JUMBF / C2PA keys (`JumbfField`, `JumbfCborKey`) | Partial (draft phase-3 BMFF box + CBOR decode) | Explicit structural/key names (`box.*`, `*.cbor.*`, `c2pa.detected`, draft `c2pa.semantic.*`) | Yes (lossless dump/export of decoded entries) |

## Tool-Level Behavior

| Tool | Purpose | Current state |
| --- | --- | --- |
| `metaread` | Human-readable metadata listing | Shows decoded entries; uses tag-name mapping where available; unknown names are shown as `-`. |
| `metavalidate` | Metadata validation | Reports decode-status warnings/errors and DNG/CCM validation issues (for example `invalid_illuminant_code`, `white_xy_out_of_range`); supports strict mode (`--warnings-as-errors`). |
| `metadump` | Sidecar and preview dump tool | `lossless` mode preserves broad key-space data; `portable` mode targets interoperable XMP fields. |
| `thumdump` | Preview extractor | Extracts embedded preview candidates; multi-preview outputs auto-suffix (`_1`, `_2`, ...). |

## Important Current Gaps

- HEIF/AVIF auxiliary semantics are still partial: OpenMeta emits draft
  `iref` relation fields and primary `auxC`-typed semantics (alpha/depth/disparity/matte),
  but full relationship graph views and broader auxiliary interpretation are not complete.
- JXL compressed metadata handling supports wrapped EXIF/XMP/JUMBF (`brob`), but other realtypes are not decoded yet.
- JUMBF/C2PA support is intentionally draft phase-3: structural box fields and
  bounded CBOR values are decoded (including definite and indefinite forms,
  synthesized names for composite map keys, and broader scalar coverage). A
  minimal semantic layer is emitted as draft `c2pa.semantic.*` fields
  (including per-claim, per-assertion, per-claim-signature, and per-signature
  draft projections, plus draft linkage counters such as
  `signature_linked_count`/`signature_orphan_count`). Draft verify scaffold
  status (`c2pa.verify.*`) includes malformed-signature detection and optional
  OpenSSL-backed cryptographic verification for signatures that expose explicit
  algorithm + signing-input + key material fields (including COSE_Sign1
  extraction in array or embedded CBOR byte-string forms, `alg`/`x5chain`
  extraction, and Sig_structure reconstruction when payload is present), plus
  draft profile and certificate trust checks (`profile_*`, `chain_*`). Full
  C2PA/COSE manifest
  binding and policy validation are still not implemented.
  Detached COSE payloads (`payload=null`) now prefer explicit
  reference-linked candidates first (for example `claims[n]`, claim-label
  references, scalar index references, and indexed array-element references
  such as `claimRef[0]`), then fall back to best-effort probing from claim
  bytes, single-claim `claims[*]` arrays, nearby/nested claim JUMBF boxes,
  and cross-manifest claim candidates during verification. Current draft tests
  cover conflicting mixed-reference cases and multi-claim/multi-signature
  cross-manifest precedence behavior.

This page is intentionally marked draft and should be updated as support grows.
