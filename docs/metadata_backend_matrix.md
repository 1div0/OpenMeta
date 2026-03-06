# Metadata Backend Matrix (Draft)

Date: March 5, 2026

## Goal

Define one OpenMeta write/transfer contract that can target multiple container
backends without per-backend metadata logic duplication.

## Capability Matrix

| Backend | Native metadata write primitives | Best use in OpenMeta |
| --- | --- | --- |
| libjpeg-turbo | `jpeg_write_marker`, `jpeg_write_m_header`, `jpeg_write_m_byte`, `jpeg_write_icc_profile` | Primary JPEG metadata emitter (APP1/APP2/APP13 direct control) |
| libtiff | `TIFFSetField`, `TIFFCreateEXIFDirectory`, `TIFFCreateGPSDirectory`, `TIFFWriteCustomDirectory`, `TIFFMergeFieldInfo` | Primary TIFF metadata emitter (native tag and IFD path) |
| libjxl | `JxlEncoderUseBoxes`, `JxlEncoderAddBox`, `JxlEncoderCloseBoxes` | Primary JXL metadata emitter (`Exif`, `xml `, `jumb`, optional `brob`) |
| OpenImageIO | Plugin-level writes use backend APIs (JPEG/TIFF/JXL/OpenEXR) | Optional adapter layer when host app already writes through OIIO |
| OpenEXR | `Header::insert`, typed attributes, `OpaqueAttribute` for unknown attr types | EXR header attribute path (not EXIF block packaging) |

## Container-Level Notes

### JPEG (libjpeg-turbo)

- Write EXIF as APP1 (`Exif\\0\\0` + TIFF payload).
- Write XMP as APP1 (`http://ns.adobe.com/xap/1.0/\\0` + packet).
- Write IPTC/IRB as APP13 (`Photoshop 3.0` resource block layout).
- Write ICC as APP2 chunk chain (`ICC_PROFILE` header + sequence index/count).
- OpenMeta should own marker ordering policy and payload splitting limits.

### TIFF (libtiff)

- EXIF/GPS should be written as directories and linked from root IFD pointers.
- XMP uses `TIFFTAG_XMLPACKET` (700).
- IPTC uses `TIFFTAG_RICHTIFFIPTC` / `TIFFTAG_EP_IPTC_NAA` (33723 alias).
- Photoshop IRB uses `TIFFTAG_PHOTOSHOP` (34377).
- ICC uses `TIFFTAG_ICCPROFILE` (34675).
- OpenMeta should own EXIF serializer policy before `TIFFSetField`.

### JXL (libjxl)

- Metadata requires container boxes enabled.
- Add `Exif`, `xml `, `jumb` boxes through `JxlEncoderAddBox`.
- `Exif` box content must include the 4-byte TIFF header offset prefix.
- Optional Brotli-compressed box storage (`brob`) is backend-controlled.
- OpenMeta should prepare box payloads once and reuse in emit path.

### EXR (OpenEXR)

- EXR metadata is typed header attributes, not EXIF/TIFF IFD blocks.
- Use typed attributes for semantic fields.
- Unknown attribute types can be preserved as opaque attributes.
- OpenMeta EXR path should remain an attribute adapter, not block repackaging.

## Unified Workaround (Single Contract)

Use one two-phase pipeline for all backends:

1. `prepare_metadata_for_target(source_store, target_format, profile)`
   - Build target-ready block payloads once.
   - Build deterministic write order.
   - Build optional fixed-size patch map for per-frame time fields.
2. `emit_prepared_bundle(bundle, writer_target, frame_patch)`
   - Emit prebuilt payloads through backend-specific write calls.
   - Apply optional fixed-width time patch before final write.

This keeps heavy work out of hot per-frame loops.

## Backend Interface Shape (Draft)

Define narrow backend interfaces:

- `JpegWriterBackend::write_app_block(app_marker, bytes)`
- `TiffWriterBackend::set_tag(tag_id, bytes_or_scalar)` plus
  `commit_exif_directory(offset_ref)`
- `JxlWriterBackend::add_box(type4cc, bytes, compress)`
- `ExrWriterBackend::set_attribute(name, typed_or_opaque_value)`

OpenMeta core emits backend-neutral prepared payloads. Backends only perform
container call mapping.

## Policy Defaults (Draft)

- No-edits transfer mode: preserve payloads when legal for target container.
- EXIF pointer tags are regenerated for target container layout.
- MakerNote and C2PA/JUMBF preserve/drop policy is explicit and deterministic.
- Safety limits are enforced before backend calls (size, truncation, malformed).

## Recommended Integration Order

1. JPEG and TIFF direct backends (highest transfer value).
2. JXL box emitter parity for EXIF/XMP/JUMBF.
3. OIIO bridge as optional host integration layer.
4. EXR attribute adapter for EXR-native metadata workflows.

This order aligns with fast transfer requirements and minimal overhead in
high-FPS pipelines.
