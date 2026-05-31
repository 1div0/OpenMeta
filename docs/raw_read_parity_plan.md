# RAW Read Parity Plan

This page tracks the public read-path work needed to move OpenMeta closer to
ExifTool-level camera RAW coverage. It is about decoding and interpretation,
not writer policy.

OpenMeta should keep a conservative rule here: preserve raw payloads whenever a
family is only partially understood, and only promote fields to structured
entries when their location, type, byte order, and meaning are stable enough to
test.

## Validation Method

RAW parity work is compared against ExifTool output with normalized values,
group names, and intentional-difference notes. Each new lane should add:

- a block-discovery test for the native container or embedded metadata carrier
- a structured decode test for stable tags
- a display-name or semantic-group test when the tag is user-visible
- a transfer-safety test when the decoded value is source-specific
- a compare note for unsupported or intentionally raw-only payloads

## Family Gap Matrix

| Family | Current lane | Main gap versus ExifTool | Next work |
| --- | --- | --- | --- |
| DNG and TIFF-based RAW | Strong baseline through TIFF/EXIF/IFD, DNG tags, XMP, ICC, and MakerNote payloads | Long-tail vendor private tables and model-specific MakerNote fields | Keep adding named tables only when they are stable and safety-classified |
| Nikon NEF/NRW | Strong TIFF/EXIF path plus expanded Nikon MakerNote tables and normalized Nikon Capture crop bounds | Model-specific encrypted/custom-setting tables and less common correction records | Add focused Nikon tables with byte-order/version gates and safety buckets |
| Sony ARW/SR2/SRF | Strong TIFF/EXIF path plus Sony RAW/source-processing classification and panorama crop-margin interpretation | Older SRF/SR2 private structures and model-specific private tables | Extend native SR2/SRF table naming and keep raw payload preservation as the fallback |
| Canon CR2 | Strong TIFF/EXIF path plus Canon MakerNote, normalized aspect/crop geometry, and crop/aspect/color-data classification | Long-tail Canon custom functions and per-model color/correction tables | Continue table-by-table decode with rendered-transfer safety coverage |
| Canon CR3 | Bounded BMFF plus EXIF/XMP/ICC/CR3 maker metadata, including item/property associations, item relations, and item groups | Remaining BMFF scene-graph semantics and CR3-specific private records | Continue BMFF metadata graph interpretation before broad CR3 private-table work |
| Canon CRW/CIFF | Partial native lane: recursive CIFF directories, stable scalar/subtable decoding, common native names, and derived EXIF bridge | Older Canon private tables and long-tail legacy records | Continue table-by-table decode only where stable validation data exists |
| Fujifilm RAF | Partial native lane: header-declared preview-JPEG EXIF/XMP discovery, FujiIFD/TIFF follow path, RAF header fields, RAF directory geometry tags, RAFData geometry projection, normalized raw crop/zoom rectangles, and standalone XMP fallback | Model-specific RAF tables and less common native sections outside the stable carrier/header/directory subset | Extend native RAF section inventory table-by-table, with broader color/correction safety buckets before transfer use |
| Sigma X3F | Partial native lane: header fields, known PROP properties, section-directory JPEG metadata follow path, and legacy embedded-EXIF fallback | Deeper image-processing/compression sections and model-specific private records | Add X3F native sections only when they expose stable user-visible fields or transfer-safety inputs |
| Panasonic, Olympus, Pentax, Kodak, Minolta, Samsung, Ricoh | Mixed TIFF/EXIF and MakerNote table coverage | Older model tables, preview/correction subtables, and private RAW payloads | Prioritize tables that affect crop, color, lens correction, orientation, or transfer safety |
| Apple, DJI, Google, FLIR | Live-vendor source-processing classification exists for rendered-transfer safety | Computational, thermal, radiometric, and shot-log interpretation depth | Add decode only for stable fields that hosts can use safely |
| Rare and legacy RAW families | Raw-preservation-first | Native container and MakerNote depth | Preserve raw blocks, then add support only when validation inputs and stable structure are available |

## Priority

1. Keep writer safety explicit: decoded MakerNote sub-IFDs are not used to
   reconstruct vendor MakerNote blobs; the original raw MakerNote payload is
   preserved when available.
2. Continue high-visibility native read gaps: more model-specific RAF native
   sections, long-tail CRW/CIFF private tables, and deeper X3F section
   interpretation.
3. Deepen remaining BMFF interpretation for CR3, HEIF, and AVIF metadata
   graphs beyond current relations, item groups, and primary-item summaries.
4. Add X3F image-processing section decode only when the fields can be named,
   typed, and safety-classified.
5. Continue vendor MakerNote table work for fields that affect crop, color,
   orientation, lens correction, or safe transfer decisions.

## RAW Curve Applicability

RAW curve and LUT metadata should not be treated as automatically active just
because the tag is present. Some formats may store curve-like metadata in both
compressed and uncompressed variants, while only one raw storage path actually
uses it.

OpenMeta now exposes a conservative applicability scaffold for RAW-processing
concept candidates:
- `MetadataRawDataEncoding` describes the host or decoder view of stored raw
  pixels, such as uncompressed, packed, lossless-compressed, lossy-compressed,
  rendered, or unknown.
- `MetadataRawDataDescriptor` is the public carrier for dimensions,
  channel-count, bit-depth, compression code, and storage encoding when a host
  can provide them.
- `MetadataRawApplicabilityState` marks current concept candidates as unknown,
  applicable to stored raw samples, conditional on raw encoding, or not
  applicable to stored raw samples.

The current resolver only marks curve/LUT-like RAW roles as conditional on raw
encoding. It does not yet prove that a vendor curve is active for a specific
file. Future interpretation work should bind curve/LUT entries to the raw data
descriptor that records the relevant plane/blob, compression or packing mode,
sample layout, offsets/byte counts when available, and the decoder stage where
the curve applies.

Verification should require more than tag-name comparison: decoder-source
tracing, runtime branch confirmation, metadata mutation/removal tests, and raw
pixel-buffer diffs across compressed and uncompressed samples should be used
before OpenMeta promotes a curve/LUT from present metadata to an active
raw-processing operation.
