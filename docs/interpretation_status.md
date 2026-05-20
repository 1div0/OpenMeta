# Interpretation Status

This page tracks how far OpenMeta has moved beyond raw metadata decoding into
meaningful interpretation. Interpretation means that decoded entries have
stable names, typed values, semantic groups, query shapes, and transfer-safety
classification that host applications can use directly.

Current overall status: **medium-high, about 86%** for the public target scope.
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
| Interpreted | Known enum-like values, orientation states, geometry, exposure/gain, color, white-balance, lens-correction, RAW-processing, and source-private meanings are projected into public helpers or query candidates. |
| Classified | Source-bound data is classified as portable, target-owned, source RAW-specific, vendor-private, computational, thermal, preview/face/stitch metadata, or opaque/lossless. |
| Queryable | Host/UI workflows can find the interpreted meaning through focused query helpers with source entries, confidence, value shape, and normalized fields where available. |
| Structured | Host code can consume query-backed interpretation records without reassembling raw query candidates manually. |
| Conflict-aware | Duplicated cross-family concepts either have a documented precedence rule or surface enough source information for host conflict handling. |

## Coverage Matrix

| Area | Current coverage | Readiness | Main remaining gap |
| --- | --- | --- | --- |
| Standard EXIF/TIFF/DNG tag names and typed values | Standard tag names, common scalar/vector values, DNG crop/color/exposure/RAW-processing fields, GeoTIFF key names, and common EXIF/TIFF/DNG numeric value-name helpers are available. Exposure time, aperture, ISO sensitivity, exposure bias, exposure program, gain, and raw exposure-adjustment records now flow into concept candidates. | High, about 91-95%. | More enum-style human-readable values and richer conflict handling between duplicated families. |
| ICC profiles | ICC header/tag table decode plus interpreted `desc`, text, signatures, XYZ, curves, named-color, measurement, viewing-condition, MFT/MAB/MBA, numeric array, and malformed/limit handling. | High, about 90-95%. | Full color-management policy remains host-owned; OpenMeta interprets profile metadata, not rendered color transforms. |
| IPTC-IIM and portable XMP | IPTC datasets and XMP properties decode into typed entries, and bounded EXIF/IPTC-to-XMP projection exists for transfer/writeback. | Medium-high, about 75-85%. | Full MWG-style reconciliation of duplicated EXIF/XMP/IPTC concepts is still bounded. |
| Orientation | EXIF/TIFF orientation query, LibRaw flip mapping, generic orientation helpers for index, rotation degrees, mirrored state, dimension swap, rotation-only fallback, human-readable labels, and EXIF-vs-XMP conflict reporting in the LibRaw bridge. | High, about 90-95%. | Higher-level policy for resolving container and host pixel-orientation state remains host-specific. |
| Geometry, crop, active area, and borders | DNG crop/active-area/masked-area tags, Phase One/Leaf geometry, Fujifilm RAF raw crop/zoom rectangles, Canon aspect/crop metadata, Nikon Capture crop bounds, Sony panorama crop margins, canonical border margins, vendor RAW-processing geometry buckets, and fuzzy crop/border-style paths are queryable. | High, about 88-92%. | More vendor-specific normalized rectangles and stronger output contracts for ambiguous multi-tag geometry. |
| Exposure and gain | Standard EXIF exposure time, f-number, exposure program, photographic sensitivity, exposure bias, exposure index, gain control, selected DNG baseline/raw-preview gain fields, and matching XMP paths are queryable and promoted into cross-family exposure roles. Capture exposure facts are marked safe, while raw/DNG exposure adjustments are marked unsafe for rendered-image transfer. | Medium-high, about 84-90%. | More vendor MakerNote exposure print conversions and richer human-readable exposure-program/gain labels. |
| Color, white balance, and matrices | DNG color/calibration/reduction/forward matrix groups, white-balance vector groups, ICC metadata, RAW color/source-processing safety buckets, transfer hints, per-family grouped vendor color/WB candidates, long-tail camera-to-XYZ/RGB, style/color, and white-balance gain aliases, and cross-family concept candidates with full grouped value vectors are identified. Matrix/vector groups require numeric payloads with conservative minimum shapes before promotion. | Medium-high, about 82-90%. | Deeper camera/vendor color science interpretation is intentionally conservative, especially for rendered-image transfer. |
| Lens correction and RAW processing | Lens-correction groups, black/white levels, linearization, CFA/sensor layout, raw-storage identifiers, vendor RAW/source-processing buckets, creative/picture style, film simulation, dynamic-range, optical correction, and raw-development aliases, per-family vendor raw-storage/sensor/source-processing table candidates, transfer hints, transfer diagnostics, and concept candidates with grouped table/vector values are classified for query and transfer safety. Lens-correction grouped tables require numeric payloads before promotion. | Medium-high, about 82-89%. | Long-tail per-model correction tables and richer numeric normalization. |
| Vendor MakerNotes | Broad MakerNote naming and source-processing classification exists for common vendors and several live computational/thermal vendors. Unknown entries remain lossless and source-private subgroups distinguish preview, face geometry, computational, thermal, stitch/panorama, pixel-shift, multi-shot, composite, auto-lighting, RAW crop/active-area, source color-transform, source style/rendering aliases, lens-correction, raw-level processing data, and Phase One/Leaf RAW-processing fields handled by direct classification plus dedicated normalized helpers. Classified multi-field vendor groups now surface as grouped query/interpretation candidates where safe to expose structurally. | Medium-high, about 83-90%. | ExifTool-style long-tail print conversions, encrypted/custom settings, and per-model private tables. |
| BMFF item graph, HEIF/AVIF/CR3, JUMBF, and C2PA | BMFF derived fields, item-info rows, bounded relations, primary-linked roles, aux semantics, and draft C2PA/JUMBF structural fields are exposed. | Medium, about 60-70%. | Full BMFF scene modeling and full C2PA manifest/policy semantics. |
| Photoshop IRB | Raw resources are preserved and a bounded interpreted subset is decoded for fixed-layout resources. | Medium, about 60-70%. | Broader resource-specific interpretation. |
| Semantic query/search and records | Query helpers expose raw matches, confidence, provenance, value shapes, normalized candidates, canonical crop/active-area rectangles, Fujifilm RAF raw crop/zoom rectangles, Canon/Nikon/Sony crop and border patterns, border margins, exposure/gain roles, per-family grouped vendor records, expanded source color/style/lens/source-processing aliases, source-processing buckets, optional RapidFuzz near-miss matching, structured interpretation records, and bounded cross-family concept resolution for orientation, date/time, exposure/gain, color/profile, GPS, geometry, lens-correction, and RAW-processing with parsed date/time fields, timezone/precision classification, combined GPS timestamps, GPS altitude-reference state and display token, canonical geometry origin/size/rect/margins, normalized exposure values, shape-checked grouped value vectors, transfer hints, rendered/compatible safety booleans, and tolerance-aware GPS/exposure/color/geometry conflicts. | Medium-high, about 77-83%. | More long-tail per-model concept aliases and richer localized policy wording. |
| Transfer-safety classification | Compatible-file versus rendered-image safety policies classify source-specific image geometry, color/profile, RAW-processing, MakerNote, JUMBF/C2PA, and vendor-private data, with concept-level diagnostics that report keep/drop/requires-target-image-spec actions, severity, and role-specific default message text before prepare. | High, about 89-93%. | More per-family policy tests and optional host localization hooks. |

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

1. Deepen transfer diagnostics with optional host localization/formatting hooks
   for GUI workflows.
2. Expand GPS policy beyond current coordinate tolerance, altitude-reference
   display, and EXIF timestamp assembly into broader cross-family
   reconciliation.
3. Expand MakerNote meaning depth in vendor order: Nikon, Canon, Sony,
   Fujifilm, Phase One/Leaf, then remaining active vendors.
4. Deepen BMFF/CR3/HEIF/AVIF item graph semantics only where hosts can use the
   result safely.
5. Keep transfer-safety classification conservative when interpretation is
   incomplete.
