#Metadata Support Matrix(Draft)

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

## Coverage Snapshot (Tracked Gates)

- EXIF tag-id compare gates are currently passing on tracked HEIC/HEIF, CR3,
  and mixed RAW corpora.
- EXR header metadata compare gate is currently passing for the documented
  name/type/value-class contract.
- Sidecar export paths (`lossless` and `portable`) are covered by baseline
  and smoke tests.
- MakerNote coverage is tracked by baseline gates with broad vendor support;
  unknown/unsupported vendor tags are preserved as raw metadata for lossless
  workflows.
- BMFF HEIF/AVIF/CR3 edge-path tests include `iloc` construction-method-2
  relation variants (valid `iref` v1 mapping plus missing/out-of-range/mismatch
  safe-skip behavior).

## Container and Block Coverage

| Container / input type | Block discovery | Structured decode in `simple_meta_read` | Notes |
| --- | --- | --- | --- |
| JPEG | EXIF, XMP, extended XMP, ICC, MPF, Photoshop IRB, comment, vendor APP1/APP4 maker blocks, JUMBF/C2PA (APP11) | EXIF, MakerNote (opt-in), XMP, ICC, IPTC (from IRB), Photoshop IRB, MPF, structured comment (`MetaKeyKind::Comment`), draft JUMBF/C2PA decode, DJI/FLIR/Casio vendor paths | One of the most complete paths today. |
| PNG | EXIF, XMP/text chunks, ICC, text, JUMBF/C2PA (caBX) | EXIF, XMP, ICC, structured PNG text (`MetaKeyKind::PngText`), draft JUMBF/C2PA decode | `tEXt`, `zTXt`, and non-XMP `iTXt` now promote to `keyword + field` entries; XMP `iTXt` stays on the XMP path. | | WebP | EXIF, XMP, ICC,
      JUMBF / C2PA(C2PA chunk) | EXIF, XMP, ICC,
      draft JUMBF / C2PA decode | RIFF chunk metadata path.| | GIF
          | XMP app - extension,
      ICC app - extension, comment extension | XMP,
      ICC, structured comment (`MetaKeyKind::Comment`) | Metadata is reassembled from GIF sub - blocks; comment extensions now decode into one generic comment key.|
          | TIFF / DNG / TIFF - based RAW
          | TIFF stream + embedded XMP / IPTC / IRB / ICC / MakerNote tags;
  best - effort JUMBF / C2PA in tag blobs(BMFF `jumb`/`c2pa`)
      | EXIF(incl pointer IFDs),
      MakerNote(opt - in), XMP, IPTC, Photoshop IRB, ICC, GeoTIFF keys,
      draft JUMBF / C2PA decode | Includes RW2 / ORF TIFF - variant headers.|
          | CRW(CIFF) | CIFF root block | Partial(named CIFF fields + bounded native CIFF subtable projection + semantic native scalar decode + derived EXIF bridge fields)
          | Best - effort mapping from CIFF to common EXIF fields plus named native CIFF entries for common CanonRaw directories/tags (`MakeModel`, `CanonFileDescription`, `OwnerName`, `CanonFirmwareVersion`, `ComponentVersion`, `OriginalFileName`, `ImageFormat`, `CanonFlashInfo`, `FlashInfo`, `FocalLength`, `DecoderTable`, `RawJpgInfo`, `WhiteSample`, `CanonShotInfo`, ...) and bounded native projection of CIFF `MakeModel`, `ImageFormat`, `TimeStamp`, `ImageInfo`, `ExposureInfo`, `FlashInfo`, `FocalLength`, `DecoderTable`, `RawJpgInfo`, `WhiteSample`, and the leading raw signed `CanonShotInfo` fields, plus semantic scalar decode for stable native CIFF fields such as `ShutterReleaseMethod`, `ReleaseSetting`, `BaseISO`, `RecordID`, `SelfTimerTime`, `FileNumber`, `CanonModelID`, `SerialNumberFormat`, and `MeasuredEV`; scanner now reports `ContainerFormat::Crw`.| | RAF / X3F
          | Embedded TIFF located heuristically;
  RAF standalone XMP signature scan
      | Same as TIFF decode after embed
      detection(plus RAF XMP best - effort decode when present)
      | Reported as `ContainerFormat::Raf` / `ContainerFormat::X3f` with the existing TIFF decode path.| | JP2
      | UUID / direct metadata boxes(EXIF / XMP / IPTC / ICC / GeoTIFF) | EXIF,
      XMP, IPTC, ICC,
      GeoTIFF | GeoJP2 TIFF payload is decoded via EXIF / TIFF path.| | JXL | `Exif`, `xml `, `jumb`, `brob` compressed
                                                                                                          metadata
                                                                                                          | EXIF
      ,
      XMP, draft JUMBF / C2PA decode,
      compressed metadata decode(brob : Exif / xml / jumb / c2pa realtypes)
          | `brob` decode dispatches wrapped EXIF,
      XMP,
      and JUMBF / C2PA payloads by realtype.| | HEIF / AVIF / CR3(BMFF)
          | `meta` item graph(`iinf`/`iloc`),
      ICC from `iprp / ipco colr`, CR3 Canon UUID metadata | EXIF, XMP, ICC,
      CR3 maker blocks; BMFF derived fields (`ftyp`, `meta.primary_item_id`, `primary.width`, `primary.height`, `primary.rotation_degrees`, `primary.mirror`, `iinf/infe` item-info rows, `iref` edge fields, generic `iref.ref_type_name` rows, typed `iref.<type>.*` rows for known and safe ASCII FourCC relation types, `iref` graph summary fields, bounded primary-linked image-role rows, `auxC`-typed aux fields), draft JUMBF/C2PA block decode | Bounded `iref` relation emission is available (`iref.*`), including typed rows for `auxl`/`dimg`/`thmb`/`cdsc` and other safe ASCII FourCC relation types, per-type edge counters and per-type unique source/target counters (`iref.<type>.from_item_unique_count`, `iref.<type>.to_item_unique_count`), per-type graph-summary aliases (`iref.graph.<type>.edge_count`, `iref.graph.<type>.from_item_unique_count`, `iref.graph.<type>.to_item_unique_count`), per-type item summaries (`iref.<type>.item_count`, `iref.<type>.item_id`, `iref.<type>.item_*_edge_count`), graph-summary fields (`iref.item_count`, `iref.from_item_unique_count`, `iref.to_item_unique_count`, `iref.item_*_edge_count`), item-info fields (`item.info_count`, `item.id`, `item.type`, `item.name`, `item.content_type`, `item.content_encoding`, `item.uri_type`, plus `primary.item_type`, `primary.item_name`, `primary.content_type`, `primary.content_encoding`, and `primary.uri_type` when `pitm` is present), and bounded primary-linked image-role fields (`primary.linked_item_role_count`, row-wise `primary.linked_item_id` + `primary.linked_item_type` + `primary.linked_item_name` + `primary.linked_item_role` when linked items have `iinf/infe` data, with roles such as `depth`, `alpha`, `auxiliary`, `derived`, `thumbnail`, and `content_description`). Item-info rows are emitted from `iinf/infe` even when `meta` has no `pitm`. Aux semantics are typed via `auxC` (`aux.item_count`, `aux.item_id`, `aux.semantic`, `aux.type`, `aux.subtype_hex`, `aux.subtype_kind`, `aux.subtype_text`, `aux.subtype_uuid`, `aux.subtype_u32`, `aux.subtype_u64`, `aux.alpha_count`, `aux.depth_count`, `aux.disparity_count`, `aux.matte_count`, `primary.auxl_count`, `primary.auxl_semantic`, `primary.depth_count`, `primary.depth_item_id`, `primary.alpha_count`, `primary.alpha_item_id`, `primary.disparity_count`, `primary.disparity_item_id`, `primary.matte_count`, `primary.matte_item_id`, `primary.dimg_count`, `primary.thmb_count`, `primary.cdsc_count`, ...), with subtype kinds including `ascii_z`, `u64be`, and `uuid`. `iloc` supports construction_method 0(file), 1(`idat`), and 2(item offset via `iref/iloc` references; slices spanning multiple referenced extents are emitted as multipart blocks). `iloc` items with `data_reference_index` that resolve to self-contained `dref/url ` entries are treated as local;
  non - self - contained references are skipped safely per item,
      and out - of - range known - item extents are skipped best
          - effort.JUMBF / C2PA is draft phase
          - 3(box structure + bounded CBOR key / value decode,
              including indefinite CBOR forms,
              plus draft `c2pa.semantic.*` projections),
      not full manifest semantics yet.| | EXR
          | n / a via `scan_auto` (decoded directly by EXR header decoder)
          | EXR header attributes(typed known attrs + raw fallback)
          | Header metadata only;
  no pixel decode.|

      ##Metadata Key - Space Coverage

      | Key / block family | Decode | Name mapping | Dump / export |
      | -- -| -- -| -- -| -- -| | EXIF tag(`MetaKeyKind::ExifTag`) | Yes
      | Yes(standard EXIF + many MakerNote tables)
      | Yes(lossless sidecar, portable mapping subset, OIIO / OCIO adapters;
            DNG color tags are exported with `dng :`
            /`DNG :` adapter names when DNG context is detected)
      | | MakerNote(`ExifTag` in `mk_*` IFD namespaces)
      | Partial / Yes(vendor - dependent, opt - in)
      | Partial / Yes(generated tables; unknown tags remain unnamed)
      | Yes in lossless;
  portable mode does not preserve vendor schema as standard XMP fields |
      | XMP property(`MetaKeyKind::XmpProperty`)
      | Yes(requires Expat at build time) | Native schema URI + property path
      | Yes(lossless + portable) | | ICC(`IccHeaderField`, `IccTag`) | Yes
      | Signature
                / offset based with typed header interpretation(
                    u32 / u64 / s15Fixed16 where stable) plus best
            - effort tag payload interpretation
                helpers(`desc`, `text`, `sig `, `mluc`, `dtim`, `view`, `meas`, `chrm`, `sf32`, `uf32`, `ui08`, `ui16`, `ui32`, `mft1`, `mft2`, `mAB`, `mBA`, `XYZ `, `curv`, `para`)
      | Yes(lossless; canonical + adapter - friendly `icc : *`/`ICC : *` names)
      | | IPTC - IIM(`IptcDataset`) | Yes | Record / dataset id based
      | Yes(lossless; portable behavior depends on mapping path) |
| Photoshop IRB(`PhotoshopIrb`) | Yes(raw resources) and bounded
      interpreted `PhotoshopIrbField` entries for `ResolutionInfo`,
      `PrintFlags`, `EffectiveBW`, `TargetLayerID`, `CopyrightFlag`, `URL`,
      `GlobalAngle`, `Watermark`, `ICC_Untagged`, `EffectsVisible`,
      `IDsBaseValue`, `IndexedColorTableCount`, `TransparentIndex`,
      `GlobalAltitude`, `IPTCDigest`, `PrintScaleInfo`, `PixelInfo`, and
      `LayerGroupsEnabledID` | Resource id based plus fixed-layout
      derived fields | Yes(lossless) | | MPF | Yes | EXIF - like tag mapping | Yes |
      | GeoTIFF key(`GeotiffKey`) | Yes | Yes(generated key - name table) | Yes
      | | EXR attribute(`ExrAttribute`) | Yes(header attrs)
      | Attribute names are native in file
      | Yes(primary item / ftyp + bounded `iref` relation fields
            + typed `iref.<type>.*` rows + `iref` graph summaries
            + `auxC` typing)
      | Field names are explicit(`ftyp.*`, `primary.*`, `meta.*`,
                                 `iref.*`, `aux.*`)
      | Yes | | JUMBF / C2PA keys(`JumbfField`, `JumbfCborKey`)
      | Partial(draft phase - 3 BMFF box + CBOR decode)
      | Explicit structural
            / key names(`box.*`, `*.cbor.*`, `c2pa.detected`,
                        draft `c2pa.semantic.*`)
      | Yes(lossless dump / export of decoded entries) |

      ##Tool - Level Behavior

`metatransfer` complements `metadump` with a transfer-specific smoke path:
it runs `read -> prepare_metadata_for_target_file -> emit_prepared_bundle_jpeg`,
prints prepared block routes/sizes, and summarizes emitted JPEG marker usage
for quick packaging checks.

      | Tool | Purpose | Current state | | -- -| -- -| -- -| | `metaread`
      | Human - readable metadata listing | Shows decoded entries;
  uses tag - name mapping where available; unknown names are shown as `-`. Unsafe/corrupted text is rendered as `<CORRUPTED_TEXT:...>` placeholders in safe output paths. |
| `metavalidate` | Metadata validation | Reports decode-status warnings/errors and DNG/CCM validation issues (for example `invalid_illuminant_code`, `white_xy_out_of_range`, companion-tag and channel-count mismatches) with machine-readable issue codes (`category/code`);
  supports strict mode(`--warnings - as - errors`).| | `metadump`
      | Sidecar and preview dump tool
      | `lossless` mode preserves broad key - space data;
  `portable` mode targets interoperable XMP fields (including selected IPTC-IIM
  mappings to `dc:*`, `photoshop:*`, and `Iptc4xmpCore:*`). `GPSTimeStamp` is exported
              as date
          - time text only when `GPSDateStamp` is available
                .Optional compatibility mode emits `GPSDateTime` alias.
      | | `thumdump` | Preview extractor | Extracts embedded preview candidates; multi-preview outputs auto-suffix (`_1`, `_2`, ...). |

## Important Current Gaps

- HEIF/AVIF auxiliary semantics are now promoted into a bounded primary-linked
  image-role surface (`primary.linked_item_role*`) on top of the existing
  relation and `auxC` decode. Full multi-image scene modeling beyond the
  primary-linked role summary is still follow-up work.
- JXL compressed metadata handling supports wrapped EXIF/XMP/JUMBF/C2PA
  (`brob`), but other realtypes are not decoded yet.
- JUMBF/C2PA support is intentionally draft phase-3: structural box fields and
  bounded CBOR values are decoded (including definite and indefinite forms,
  synthesized names for composite map keys, and broader scalar coverage). A
  minimal semantic layer is emitted as draft `c2pa.semantic.*` fields
  (including active-manifest summary fields such as
  `active_manifest_present`, `active_manifest_count`,
  `active_manifest.prefix`, and `manifest.{k}.is_active`, plus per-claim,
  per-assertion, per-claim-signature, and per-signature draft projections,
  reference-link projections such as
  `reference_key_hits`, `cross_claim_link_count`,
  `signature.{k}.linked_claim.{m}.prefix`, and draft linkage counters such as
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
  references, scalar index references, indexed array-element references
  such as `claimRef[0]`, and nested URI-like fields such as
  `references[].href`/`references[].link`), then fall back to best-effort probing from claim
  bytes, single-claim `claims[*]` arrays, nearby/nested claim JUMBF boxes,
  and cross-manifest claim candidates during verification. Current draft tests
  cover conflicting mixed-reference cases and multi-claim/multi-signature
  cross-manifest precedence behavior. Explicit-reference parsing is now scope-
  strict for signature prefixes (avoids `signatures[1]` vs `signatures[10]`
  collisions), and draft verify profile checks include basic semantic counter
  consistency invariants.

This page is intentionally marked draft and should be updated as support grows.
