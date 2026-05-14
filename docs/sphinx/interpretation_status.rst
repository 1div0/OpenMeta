Interpretation Status
=====================

This page tracks how far OpenMeta has moved beyond raw metadata decoding into
meaningful interpretation. Interpretation means that decoded entries have
stable names, typed values, semantic groups, query shapes, and transfer-safety
classification that host applications can use directly.

Current overall status: **medium-high, about 80%** for the public target scope.
This is intentionally lower than decode coverage. Decode parity only proves
that metadata carriers and entries are visible; interpretation also requires
human-readable meaning and safe cross-format behavior.

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
       RAW-processing fields, and GeoTIFF key names are available through
       decoded entries and query helpers.
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
       dimension swap, rotation-only fallback, and human-readable labels.
     - High, about 90-95%.
     - Higher-level policy for resolving conflicting EXIF, XMP, container, and
       host pixel-orientation state remains host-specific.
   * - Geometry, crop, active area, and borders
     - DNG crop/active-area tags, Phase One/Leaf geometry, vendor RAW-processing
       geometry buckets, and fuzzy crop/border-style paths are queryable.
     - Medium-high, about 80-85%.
     - More vendor-specific normalized rectangles and stronger output contracts
       for ambiguous multi-tag geometry.
   * - Color, white balance, and matrices
     - DNG color/calibration/reduction/forward matrix groups, white-balance
       vector groups, ICC metadata, and RAW color/source-processing safety
       buckets are identified.
     - Medium-high, about 75-85%.
     - Deeper camera/vendor color science interpretation is intentionally
       conservative, especially for rendered-image transfer.
   * - Lens correction and RAW processing
     - Lens-correction groups, black/white levels, linearization, CFA/sensor
       layout, raw-storage identifiers, and vendor RAW/source-processing
       buckets are classified for query and transfer safety.
     - Medium, about 70-80%.
     - Long-tail per-model correction tables and richer numeric normalization.
   * - Vendor MakerNotes
     - Broad MakerNote naming and source-processing classification exists for
       common vendors and several live computational/thermal vendors. Unknown
       entries remain lossless.
     - Medium-high, about 75-85%.
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
   * - Semantic query/search
     - Query helpers expose raw matches, confidence, provenance, value shapes,
       normalized candidates, and optional RapidFuzz near-miss matching.
     - Low-medium, about 30-40%.
     - Stronger result contracts, more canonical shapes, and broader
       cross-family concept resolution.
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

1. Harden semantic query output contracts for orientation, geometry, color,
   lens correction, and RAW-processing groups.
2. Add corpus-backed interpretation checks that compare public-safe names and
   human-readable values against established tool behavior.
3. Expand MakerNote meaning depth in vendor order: Nikon, Canon, Sony,
   Fujifilm, Phase One/Leaf, then remaining active vendors.
4. Deepen BMFF/CR3/HEIF/AVIF item graph semantics only where hosts can use the
   result safely.
5. Keep transfer-safety classification conservative when interpretation is
   incomplete.
