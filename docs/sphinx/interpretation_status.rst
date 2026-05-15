Interpretation Status
=====================

This page tracks how far OpenMeta has moved beyond raw metadata decoding into
meaningful interpretation. Interpretation means that decoded entries have
stable names, typed values, semantic groups, query shapes, and transfer-safety
classification that host applications can use directly.

Current overall status: **medium-high, about 82%** for the public target scope.
This is intentionally lower than decode coverage. Decode parity only proves
that metadata carriers and entries are visible; interpretation also requires
human-readable meaning and safe cross-format behavior.

100% acceptance gates
---------------------

For the declared target scope, an entry counts as covered when it has one
explicit outcome:

.. list-table::
   :header-rows: 1
   :widths: 22 78

   * - Gate
     - Requirement
   * - Decoded
     - The carrier and entry are visible in ``MetaStore``, or the decoder
       reports an explicit unsupported/limit/malformed reason.
   * - Named
     - The entry has a stable public name, or it is deliberately exposed as an
       unknown numeric/private field.
   * - Typed
     - The raw value shape is preserved as scalar, vector, matrix, table, bytes,
       text, or opaque blob.
   * - Interpreted
     - Known enum-like values, orientation states, geometry, color,
       white-balance, lens-correction, RAW-processing, and source-private
       meanings are projected into public helpers or query candidates.
   * - Classified
     - Source-bound data is classified as portable, target-owned, source
       RAW-specific, vendor-private, computational, thermal, preview/face/
       stitch metadata, or opaque/lossless.
   * - Queryable
     - Host/UI workflows can find the interpreted meaning through focused query
       helpers with source entries, confidence, value shape, and normalized
       fields where available.
   * - Structured
     - Host code can consume query-backed interpretation records without
       reassembling raw query candidates manually.
   * - Conflict-aware
     - Duplicated cross-family concepts either have a documented precedence rule
       or surface enough source information for host conflict handling.

Coverage matrix
---------------

.. list-table::
   :header-rows: 1
   :widths: 22 43 15 20

   * - Area
     - Current coverage
     - Readiness
     - Main remaining gap
   * - Standard EXIF/TIFF/DNG tag names and typed values
     - Standard tag names, common scalar/vector values, DNG crop/color/
       RAW-processing fields, GeoTIFF key names, and common EXIF/TIFF/DNG
       numeric value-name helpers are available.
     - High, about 90-95%.
     - More enum-style human-readable values and richer conflict handling
       between duplicated families.
   * - ICC profiles
     - ICC header/tag table decode plus interpreted ``desc``, text,
       signatures, XYZ, curves, named-color, measurement, viewing-condition,
       MFT/MAB/MBA, numeric array, and malformed/limit handling.
     - High, about 90-95%.
     - Full color-management policy remains host-owned; OpenMeta interprets
       profile metadata, not rendered color transforms.
   * - IPTC-IIM and portable XMP
     - IPTC datasets and XMP properties decode into typed entries, and bounded
       EXIF/IPTC-to-XMP projection exists for transfer/writeback.
     - Medium-high, about 75-85%.
     - Full MWG-style reconciliation of duplicated EXIF/XMP/IPTC concepts is
       still bounded.
   * - Orientation
     - EXIF/TIFF orientation query, LibRaw flip mapping, and generic
       orientation helpers for index, rotation degrees, mirrored state,
       dimension swap, rotation-only fallback, human-readable labels, and
       EXIF-vs-XMP conflict reporting in the LibRaw bridge.
     - High, about 90-95%.
     - Higher-level policy for resolving container and host pixel-orientation
       state remains host-specific.
   * - Geometry, crop, active area, and borders
     - DNG crop/active-area/masked-area tags, Phase One/Leaf geometry,
       canonical border margins, vendor RAW-processing geometry buckets, and
       fuzzy crop/border-style paths are queryable.
     - Medium-high, about 85-90%.
     - More vendor-specific normalized rectangles and stronger output contracts
       for ambiguous multi-tag geometry.
   * - Color, white balance, and matrices
     - DNG color/calibration/reduction/forward matrix groups, white-balance
       vector groups, ICC metadata, RAW color/source-processing safety
       buckets, and cross-family concept candidates with full grouped value
       vectors are identified.
     - Medium-high, about 78-86%.
     - Deeper camera/vendor color science interpretation is intentionally
       conservative, especially for rendered-image transfer.
   * - Lens correction and RAW processing
     - Lens-correction groups, black/white levels, linearization, CFA/sensor
       layout, raw-storage identifiers, and vendor RAW/source-processing
       buckets, and concept candidates with grouped table/vector values are
       classified for query and transfer safety.
     - Medium-high, about 74-82%.
     - Long-tail per-model correction tables and richer numeric normalization.
   * - Vendor MakerNotes
     - Broad MakerNote naming and source-processing classification exists for
       common vendors and several live computational/thermal vendors. Unknown
       entries remain lossless and source-private subgroups distinguish
       preview, face geometry, computational, thermal, stitch/panorama,
       pixel-shift, multi-shot, composite, and auto-lighting processing data.
     - Medium-high, about 77-86%.
     - ExifTool-style long-tail print conversions, encrypted/custom settings,
       and per-model private tables.
   * - BMFF item graph, HEIF/AVIF/CR3, JUMBF, and C2PA
     - BMFF derived fields, item-info rows, bounded relations, primary-linked
       roles, aux semantics, and draft C2PA/JUMBF structural fields are exposed.
     - Medium, about 60-70%.
     - Full BMFF scene modeling and full C2PA manifest/policy semantics.
   * - Photoshop IRB
     - Raw resources are preserved and a bounded interpreted subset is decoded
       for fixed-layout resources.
     - Medium, about 60-70%.
     - Broader resource-specific interpretation.
   * - Semantic query/search and records
     - Query helpers expose raw matches, confidence, provenance, value shapes,
       normalized candidates, canonical crop/active-area rectangles, border
       margins, source-processing buckets, optional RapidFuzz near-miss
       matching, structured interpretation records, and bounded cross-family
       concept resolution for orientation, date/time, color/profile, GPS,
       geometry, lens-correction, and RAW-processing with parsed date/time
       fields, timezone/precision classification, combined GPS timestamps, GPS
       altitude-reference state, canonical geometry origin/size/rect/margins,
       full grouped value vectors, and tolerance-aware GPS conflicts.
     - Medium, about 63-70%.
     - Richer host policy hints and more long-tail per-model concept aliases.
   * - Transfer-safety classification
     - Compatible-file versus rendered-image safety policies classify
       source-specific image geometry, color/profile, RAW-processing, MakerNote,
       JUMBF/C2PA, and vendor-private data.
     - High, about 85-90%.
     - More user-facing diagnostics and additional per-family policy tests.

Competitor position
-------------------

ExifTool remains the practical reference for long-tail tag names, MakerNote
tables, and human-readable print conversions. OpenMeta is now close on decode
visibility for the current target scope, but interpretation still trails
ExifTool in per-model private meanings.

Exiv2 is strong for common EXIF/IPTC/XMP workflows. OpenMeta's differentiator
is the explicit safe-transfer and host-query model: it classifies whether data
is portable, target-owned, source RAW-specific, or unsafe to move into rendered
outputs.

Next interpretation priorities
------------------------------

1. Add richer host policy hints to concept candidates so inspection UIs can
   distinguish portable facts, source-bound facts, and target-owned facts
   without reimplementing transfer-safety logic.
2. Expand GPS policy beyond current coordinate tolerance and altitude-reference
   state, including unit/reference presentation and cross-family timestamp
   reconciliation.
3. Expand MakerNote meaning depth in vendor order: Nikon, Canon, Sony,
   Fujifilm, Phase One/Leaf, then remaining active vendors.
4. Deepen BMFF/CR3/HEIF/AVIF item graph semantics only where hosts can use the
   result safely.
5. Keep transfer-safety classification conservative when interpretation is
   incomplete.
