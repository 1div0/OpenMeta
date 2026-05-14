# Interpretation Status

This page tracks how far OpenMeta has moved beyond raw metadata decoding into
meaningful interpretation. Interpretation means that decoded entries have
stable names, typed values, semantic groups, query shapes, and transfer-safety
classification that host applications can use directly.

Current overall status: **medium-high, about 80%** for the public target scope.
This is intentionally lower than decode coverage. Decode parity only proves
that metadata carriers and entries are visible; interpretation also requires
human-readable meaning and safe cross-format behavior.

## 100% Acceptance Gates

For the declared target scope, an entry counts as covered when it has one
explicit outcome:

| Gate | Requirement |
| --- | --- |
| Decoded | The carrier and entry are visible in `MetaStore`, or the decoder reports an explicit unsupported/limit/malformed reason. |
| Named | The entry has a stable public name, or it is deliberately exposed as an unknown numeric/private field. |
| Typed | The raw value shape is preserved as scalar, vector, matrix, table, bytes, text, or opaque blob. |
| Interpreted | Known enum-like values, orientation states, geometry, color, white-balance, lens-correction, RAW-processing, and source-private meanings are projected into public helpers or query candidates. |
| Classified | Source-bound data is classified as portable, target-owned, source RAW-specific, vendor-private, computational, thermal, preview/face/stitch metadata, or opaque/lossless. |
| Queryable | Host/UI workflows can find the interpreted meaning through focused query helpers with source entries, confidence, value shape, and normalized fields where available. |
| Structured | Host code can consume query-backed interpretation records without reassembling raw query candidates manually. |
| Conflict-aware | Duplicated cross-family concepts either have a documented precedence rule or surface enough source information for host conflict handling. |

## Coverage Matrix

| Area | Current coverage | Readiness | Main remaining gap |
| --- | --- | --- | --- |
| Standard EXIF/TIFF/DNG tag names and typed values | Standard tag names, common scalar/vector values, DNG crop/color/RAW-processing fields, GeoTIFF key names, and common EXIF/TIFF/DNG numeric value-name helpers are available. | High, about 90-95%. | More enum-style human-readable values and richer conflict handling between duplicated families. |
| ICC profiles | ICC header/tag table decode plus interpreted `desc`, text, signatures, XYZ, curves, named-color, measurement, viewing-condition, MFT/MAB/MBA, numeric array, and malformed/limit handling. | High, about 90-95%. | Full color-management policy remains host-owned; OpenMeta interprets profile metadata, not rendered color transforms. |
| IPTC-IIM and portable XMP | IPTC datasets and XMP properties decode into typed entries, and bounded EXIF/IPTC-to-XMP projection exists for transfer/writeback. | Medium-high, about 75-85%. | Full MWG-style reconciliation of duplicated EXIF/XMP/IPTC concepts is still bounded. |
| Orientation | EXIF/TIFF orientation query, LibRaw flip mapping, generic orientation helpers for index, rotation degrees, mirrored state, dimension swap, rotation-only fallback, human-readable labels, and EXIF-vs-XMP conflict reporting in the LibRaw bridge. | High, about 90-95%. | Higher-level policy for resolving container and host pixel-orientation state remains host-specific. |
| Geometry, crop, active area, and borders | DNG crop/active-area/masked-area tags, Phase One/Leaf geometry, canonical border margins, vendor RAW-processing geometry buckets, and fuzzy crop/border-style paths are queryable. | Medium-high, about 85-90%. | More vendor-specific normalized rectangles and stronger output contracts for ambiguous multi-tag geometry. |
| Color, white balance, and matrices | DNG color/calibration/reduction/forward matrix groups, white-balance vector groups, ICC metadata, and RAW color/source-processing safety buckets are identified. | Medium-high, about 75-85%. | Deeper camera/vendor color science interpretation is intentionally conservative, especially for rendered-image transfer. |
| Lens correction and RAW processing | Lens-correction groups, black/white levels, linearization, CFA/sensor layout, raw-storage identifiers, and vendor RAW/source-processing buckets are classified for query and transfer safety. | Medium, about 70-80%. | Long-tail per-model correction tables and richer numeric normalization. |
| Vendor MakerNotes | Broad MakerNote naming and source-processing classification exists for common vendors and several live computational/thermal vendors. Unknown entries remain lossless and source-private subgroups distinguish preview, face geometry, computational, thermal, and stitch/panorama data. | Medium-high, about 75-85%. | ExifTool-style long-tail print conversions, encrypted/custom settings, and per-model private tables. |
| BMFF item graph, HEIF/AVIF/CR3, JUMBF, and C2PA | BMFF derived fields, item-info rows, bounded relations, primary-linked roles, aux semantics, and draft C2PA/JUMBF structural fields are exposed. | Medium, about 60-70%. | Full BMFF scene modeling and full C2PA manifest/policy semantics. |
| Photoshop IRB | Raw resources are preserved and a bounded interpreted subset is decoded for fixed-layout resources. | Medium, about 60-70%. | Broader resource-specific interpretation. |
| Semantic query/search and records | Query helpers expose raw matches, confidence, provenance, value shapes, normalized candidates, canonical crop/active-area rectangles, border margins, source-processing buckets, optional RapidFuzz near-miss matching, structured interpretation records, and first bounded cross-family concept resolution for orientation, date/time, color/profile, and GPS with parsed date/time fields and combined GPS timestamps. | Medium, about 52-60%. | More canonical shapes, deeper GPS/date normalization, and richer per-record host policy hints. |
| Transfer-safety classification | Compatible-file versus rendered-image safety policies classify source-specific image geometry, color/profile, RAW-processing, MakerNote, JUMBF/C2PA, and vendor-private data. | High, about 85-90%. | More user-facing diagnostics and additional per-family policy tests. |

## Competitor Position

ExifTool remains the practical reference for long-tail tag names, MakerNote
tables, and human-readable print conversions. OpenMeta is now close on decode
visibility for the current target scope, but interpretation still trails
ExifTool in per-model private meanings.

Exiv2 is strong for common EXIF/IPTC/XMP workflows. OpenMeta's differentiator
is the explicit safe-transfer and host-query model: it classifies whether data
is portable, target-owned, source RAW-specific, or unsafe to move into rendered
outputs.

## Next Interpretation Priorities

1. Expand cross-family concept resolution beyond the current orientation,
   date/time, color/profile, and GPS pass, especially richer timezone, GPS
   altitude/reference, and host policy hints.
2. Add more canonical record shapes for vendor geometry, color, lens
   correction, and RAW-processing groups.
3. Expand MakerNote meaning depth in vendor order: Nikon, Canon, Sony,
   Fujifilm, Phase One/Leaf, then remaining active vendors.
4. Deepen BMFF/CR3/HEIF/AVIF item graph semantics only where hosts can use the
   result safely.
5. Keep transfer-safety classification conservative when interpretation is
   incomplete.
