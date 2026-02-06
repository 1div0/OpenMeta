# DRAFT: Canonical EXR Value Encoding in OpenMeta

Status: draft (working contract; expected to evolve)

## Scope

This defines how OpenEXR attribute values should be represented in `MetaValue`
and `Origin/WireType` so decode/edit/write logic stays deterministic and
lossless.

Keying is handled separately by `MetaKeyKind::ExrAttribute` with:

- `part_index` (EXR part id)
- `name` (attribute name)

## Wire Type Contract (Draft)

- `Origin::wire_type.family = WireFamily::Other`
- `Origin::wire_type.code = exr_attribute_type_t` numeric value
- `Origin::wire_count` stores logical element count when meaningful, else byte
  count for opaque payloads

For unknown/user-defined EXR attribute types, preserve:

- raw payload bytes in `MetaValueKind::Bytes`
- type string in side metadata (planned follow-up in decode path)

## Canonical Mapping Table (Draft)

- `EXR_ATTR_INT` -> `Scalar + I32`
- `EXR_ATTR_FLOAT` -> `Scalar + F32`
- `EXR_ATTR_DOUBLE` -> `Scalar + F64`
- `EXR_ATTR_RATIONAL` -> `Scalar + SRational` (`num`, `denom`)
- `EXR_ATTR_STRING` -> `Text + Utf8` (best effort, fallback `Unknown`)
- `EXR_ATTR_STRING_VECTOR` -> `Array + Bytes` with packed UTF-8 strings
  (temporary), planned structured helper API for element iteration

- `EXR_ATTR_V2I/V3I` -> `Array + I32` (count 2/3)
- `EXR_ATTR_V2F/V3F` -> `Array + F32` (count 2/3)
- `EXR_ATTR_V2D/V3D` -> `Array + F64` (count 2/3)
- `EXR_ATTR_M33F` -> `Array + F32` (count 9, row-major)
- `EXR_ATTR_M33D` -> `Array + F64` (count 9, row-major)
- `EXR_ATTR_M44F` -> `Array + F32` (count 16, row-major)
- `EXR_ATTR_M44D` -> `Array + F64` (count 16, row-major)
- `EXR_ATTR_BOX2I` -> `Array + I32` (count 4: `min.x,min.y,max.x,max.y`)
- `EXR_ATTR_BOX2F` -> `Array + F32` (count 4: `min.x,min.y,max.x,max.y`)

- Enum-like attrs (`compression`, `lineorder`, `envmap`, `deepImageState`) ->
  `Scalar + U8`

- Struct/blob attrs (`chlist`, `preview`, `tiledesc`, `timecode`, `keycode`,
  `bytes`, `opaque`) -> `Bytes` for lossless core storage; typed helper decode
  APIs should be layered on top.

## Rationale

1. Keep `MetaStore` compact and storage-agnostic.
2. Guarantee lossless round-trip for all EXR attributes, including unknown ones.
3. Avoid hard-coding large per-type structs in core until a stable EXR read/write
   adapter lands.

## Open Questions

1. Whether `string_vector` should become first-class `Array + Text` rather than a
   packed-bytes representation.
2. Where to attach EXR `type_name` for unknown/custom attrs without bloating all
   entries.
3. Whether `chlist` should get a dedicated typed view API in v1 or remain bytes in
   core with helper parsing only.
