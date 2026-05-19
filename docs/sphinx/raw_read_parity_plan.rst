RAW Read Parity Plan
====================

This page tracks the public read-path work needed to move OpenMeta closer to
ExifTool-level camera RAW coverage. It is about decoding and interpretation,
not writer policy.

OpenMeta should keep a conservative rule here: preserve raw payloads whenever a
family is only partially understood, and only promote fields to structured
entries when their location, type, byte order, and meaning are stable enough to
test.

Validation Method
-----------------

RAW parity work is compared against ExifTool output with normalized values,
group names, and intentional-difference notes. Each new lane should add:

- a block-discovery test for the native container or embedded metadata carrier
- a structured decode test for stable tags
- a display-name or semantic-group test when the tag is user-visible
- a transfer-safety test when the decoded value is source-specific
- a compare note for unsupported or intentionally raw-only payloads

Family Gap Matrix
-----------------

.. list-table::
   :header-rows: 1
   :widths: 16 22 32 30

   * - Family
     - Current lane
     - Main gap versus ExifTool
     - Next work
   * - DNG and TIFF-based RAW
     - Strong baseline through TIFF/EXIF/IFD, DNG tags, XMP, ICC, and
       MakerNote payloads
     - Long-tail vendor private tables and model-specific MakerNote fields
     - Keep adding named tables only when they are stable and safety-classified
   * - Nikon NEF/NRW
     - Strong TIFF/EXIF path plus expanded Nikon MakerNote tables
     - Model-specific encrypted/custom-setting tables and less common
       correction records
     - Add focused Nikon tables with byte-order/version gates and safety
       buckets
   * - Sony ARW/SR2/SRF
     - Strong TIFF/EXIF path plus Sony RAW/source-processing classification
     - Older SRF/SR2 private structures and model-specific private tables
     - Extend native SR2/SRF table naming and keep raw payload preservation as
       the fallback
   * - Canon CR2
     - Strong TIFF/EXIF path plus Canon MakerNote and crop/aspect/color-data
       classification
     - Long-tail Canon custom functions and per-model color/correction tables
     - Continue table-by-table decode with rendered-transfer safety coverage
   * - Canon CR3
     - Bounded BMFF plus EXIF/XMP/ICC/CR3 maker metadata
     - Deeper BMFF item/property graph semantics and CR3-specific private
       records
     - Deepen BMFF metadata graph interpretation before broad CR3 private-table
       work
   * - Canon CRW/CIFF
     - Partial native lane: recursive CIFF directories, stable
       scalar/subtable decoding, common native names, and derived EXIF bridge
     - Older Canon private tables and long-tail legacy records
     - Continue table-by-table decode only where stable validation data exists
   * - Fujifilm RAF
     - Partial native lane: header-declared preview-JPEG EXIF/XMP discovery,
       FujiIFD/TIFF follow path, RAF header fields, RAF directory geometry
       tags, RAFData geometry projection, normalized raw crop/zoom rectangles,
       and standalone XMP fallback
     - Model-specific RAF tables and less common native sections outside the
       stable carrier/header/directory subset
     - Extend native RAF section inventory table-by-table, with broader
       color/correction safety buckets before transfer use
   * - Sigma X3F
     - Partial native lane: header fields, known PROP properties,
       section-directory JPEG metadata follow path, and legacy embedded-EXIF
       fallback
     - Deeper image-processing/compression sections and model-specific private
       records
     - Add X3F native sections only when they expose stable user-visible fields
       or transfer-safety inputs
   * - Panasonic, Olympus, Pentax, Kodak, Minolta, Samsung, Ricoh
     - Mixed TIFF/EXIF and MakerNote table coverage
     - Older model tables, preview/correction subtables, and private RAW
       payloads
     - Prioritize tables that affect crop, color, lens correction,
       orientation, or transfer safety
   * - Apple, DJI, Google, FLIR
     - Live-vendor source-processing classification exists for
       rendered-transfer safety
     - Computational, thermal, radiometric, and shot-log interpretation depth
     - Add decode only for stable fields that hosts can use safely
   * - Rare and legacy RAW families
     - Raw-preservation-first
     - Native container and MakerNote depth
     - Preserve raw blocks, then add support only when validation inputs and
       stable structure are available

Priority
--------

1. Keep writer safety explicit: decoded MakerNote sub-IFDs are not used to
   reconstruct vendor MakerNote blobs; the original raw MakerNote payload is
   preserved when available.
2. Continue high-visibility native read gaps: more model-specific RAF native
   sections, long-tail CRW/CIFF private tables, and deeper X3F section
   interpretation.
3. Deepen BMFF interpretation for CR3, HEIF, and AVIF metadata graphs.
4. Add X3F image-processing section decode only when the fields can be named,
   typed, and safety-classified.
5. Continue vendor MakerNote table work for fields that affect crop, color,
   orientation, lens correction, or safe transfer decisions.
