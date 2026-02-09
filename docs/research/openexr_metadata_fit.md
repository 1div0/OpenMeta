# OpenEXR Metadata Fit for OpenMeta Core

Status: baseline contract (v1)

## Goal

Assess whether the current OpenMeta metadata core can represent OpenEXR metadata
well enough for read/edit workflows, and identify required API/model changes for
good interoperability with OpenEXR and OpenImageIO.

## What OpenEXR Metadata Looks Like

OpenEXR metadata is a per-part attribute map (name + typed value), not EXIF-like
numeric tag tables. Attribute types are strongly typed in the SDK and include:

- Scalars and enums (`int`, `float`, `double`, compression, lineOrder, envmap)
- Geometric/math types (`v2*`, `v3*`, `m33*`, `m44*`, `box2*`)
- Structured payloads (`chlist`, `preview`, `tiledesc`, `timecode`, `keycode`)
- Strings and string vectors
- Rational and opaque/custom attribute types

OpenEXR C APIs expose:

- Count/list/get attribute by name or index (file order or sorted order)
- Explicit typed getters/setters for built-in attribute types
- Header write lifecycle (`declare/set` then `write_header`)
- In-place header update mode with size constraints

## OpenImageIO / OpenColorIO Integration Signals

- OpenImageIO treats EXR metadata as arbitrary key/value attributes and maps many
  EXR names to generic names (`DateTime`, `Copyright`, etc.) while preserving
  EXR-specific names under `openexr:*`.
- OpenImageIO reports EXIF/IPTC support for EXR via arbitrary metadata transport,
  not because EXR natively stores EXIF/IPTC blocks.
- OpenColorIO metadata model is mostly string-based transform/config metadata; it
  is useful for selected color-related fields, not as a full EXR container model.

## Fit Against Current OpenMeta Core

Current OpenMeta strengths:

- `MetaStore` already supports duplicate keys, per-block origin, deterministic
  indexing, and binary-search lookups.
- `MetaValue` already supports scalar, array, bytes, text, and rational values.
- Existing block model can represent per-part metadata boundaries.

Current implementation snapshot:

- `MetaKeyKind::ExrAttribute` is available (`part_index` + `name`).
- `decode_exr_header(...)` is implemented and wired into `simple_meta_read`
  fallback path (metadata-only header decode).
- Common EXR scalar/vector/matrix types decode to typed `MetaValue`; unknown and
  complex payloads are preserved as raw bytes.
- Unknown/custom EXR attrs can preserve original type name in
  `Origin::wire_type_name` (optional decode behavior).
- `Origin::wire_type` carries EXR type code (`WireFamily::Other`), and
  `wire_count` currently stores attribute byte size.

Remaining gaps for production-level EXR workflows:

- Persisting original EXR `type_name` for unknown/custom attrs (beyond numeric code).
- Full canonical policy for complex structured attrs (`chlist`, `preview`,
  `stringvector`) and helper views.
- EXR-aware write/edit lifecycle, including in-place header size constraints.
- Optional EXR scanner integration in `container_scan` (today handled by
  fallback decode in `simple_meta_read`).

## Design Direction (v1)

1. Add EXR key space and provenance:
   - `MetaKeyKind::ExrAttribute` with fields: `part_index`, `name`.
   - Reserve block `format/container` code for EXR header metadata blocks.

2. Add EXR wire typing:
   - Extend wire metadata to carry EXR type id + `type_name` string when needed.
   - Keep unknown/custom attributes as raw bytes + declared type name.

3. Define canonical value encodings:
   - vectors/matrices/boxes as typed arrays with fixed element counts.
   - `chlist`, `preview`, and other complex structs as bytes + structured decode
     helpers (opt-in) to keep core storage compact and lossless.

4. Add EXR decode entry point:
   - `decode_exr_header(...)` that fills `MetaStore` from EXR parts/attributes.
   - Keep image pixel I/O out of scope; metadata only.

5. Add integration adapters:
   - OpenImageIO adapter: map EXR attrs to OIIO-style names plus `openexr:*`.
   - OpenColorIO adapter: expose selected color metadata subset only.

Canonical encoding details for compound EXR values are tracked in:
`OpenMeta/docs/design/exr_canonical_value_encoding.md`.

## Practical Conclusion

OpenMeta core is close enough to support EXR metadata without redesigning the
store. Main work is key-space/type modeling and a container-specific EXR decode
frontend with clear canonical encoding rules for EXR compound attribute types.
