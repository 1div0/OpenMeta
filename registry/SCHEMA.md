# Registry Schema (v1)

OpenMeta registry files use **JSON Lines** (`.jsonl`): each line is an
independent JSON object with a required discriminator field: `kind`.

Conventions:
- Numeric tag IDs are stored as hex strings like `"0x010f"` (lowercase `0x`).
- FourCC values are stored as 4-character strings (may include spaces).
- Unknown fields should be ignored by readers for forward compatibility.

## `kind: "container.block"`
Describes where metadata may live at the *container* level (segments, chunks,
boxes, IFD links, etc.).

Required fields:
- `kind`: `"container.block"`
- `format`: container format id (e.g. `"jpeg"`, `"png"`, `"tiff"`, `"jp2"`, `"jxl"`)
- `container`: storage unit type (e.g. `"segment"`, `"chunk"`, `"box"`, `"tag"`, `"ifd"`)
- `id`: container identifier (e.g. `"APP1"`, `"iTXt"`, `"Exif"`, `"0x02BC"`, `"uuid"`)
- `payload`: logical payload id (e.g. `"exif"`, `"xmp"`, `"iptc_iim"`, `"icc"`)

Optional fields:
- `name`: short human identifier
- `ifd`: IFD name when `container: "tag"` (e.g. `"IFD0"`, `"ExifIFD"`)
- `marker`: marker code for marker-based formats (e.g. `"0xFFE1"`)
- `signature`: payload preamble/signature string when applicable
- `uuid_hex`: 16-byte UUID as 32 lowercase hex chars (for UUID/UUID-box payloads)
- `encoding`: object with optional hints:
  - `compression`: `"none" | "deflate" | "brotli" | ...`
  - `chunking`: token describing segmentation scheme (e.g. ICC seq/total, extended XMP)

## `kind: "exif.tag"`
Describes a TIFF/EXIF-style numeric tag (including maker-note namespaces).

Required fields:
- `kind`: `"exif.tag"`
- `ifd`: IFD / tag-space id (e.g. `"IFD0"`, `"ExifIFD"`, `"GPSIFD"`, `"MakerNote:Canon"`)
- `tag`: hex string (e.g. `"0x010f"`)
- `name`: canonical-ish identifier (e.g. `"Make"`)

Optional fields:
- `type`: value type token (e.g. `"ascii"`, `"short"`, `"rational"`, `"undefined"`, `"utf8"`)
- `count`: component count (`0` = any, `-1` = unknown)

## `kind: "iptc.dataset"`
Describes an IPTC-IIM dataset.

Required fields:
- `kind`: `"iptc.dataset"`
- `record`: integer
- `dataset`: integer
- `name`: identifier (e.g. `"ObjectName"`)

Optional fields: `type`, `count`.

## `kind: "xmp.property"`
Describes an XMP property scoped by schema namespace.

Required fields:
- `kind`: `"xmp.property"`
- `schema_ns`: namespace URI
- `prefix`: preferred prefix (when known)
- `property`: schema-relative property path

Optional fields: `value_type`, `array_type` (`"bag" | "seq" | "alt" | "none"`).

## `kind: "icc.header_field"`
Describes a field in the fixed ICC profile header (byte offsets).

Required fields: `kind`, `offset` (integer byte offset), `name` (identifier).
Optional fields: `type`, `count`.

## `kind: "icc.tag"`
Describes an ICC profile tag signature (the 4-byte key in the ICC tag table).

Required fields: `kind`, `signature` (4CC string), `name` (identifier).
Optional fields: `type`.

## `kind: "photoshop.irb"`
Describes a Photoshop Image Resource Block (IRB) resource id.

Required fields: `kind`, `resource_id` (integer), `name` (identifier).
Optional fields: `type`.

## `kind: "geotiff.key"`
Describes a GeoTIFF GeoKey identifier.

Required fields: `kind`, `key_id` (integer), `name` (identifier).
Optional fields: `type`.

## `kind: "printim.field"`
Describes a PrintIM field identifier.

Required fields: `kind`, `field` (string key), `name` (identifier).
Optional fields: `type`.

## `kind: "jumbf.field"`
Describes a JUMBF/JUMD field identifier.

Required fields: `kind`, `field` (string key), `name` (identifier).
Optional fields: `type`.

## `kind: "jumbf.cbor_key"`
Describes a CBOR map key used inside JUMBF-based payloads (e.g. C2PA).

Required fields: `kind`, `key` (string key), `name` (identifier).
Optional fields: `category`, `type`.

## `kind: "mapping.rule"`
Describes a storage-agnostic mapping rule between metadata families.

Required fields:
- `kind`: `"mapping.rule"`
- `from`: key object (EXIF/IPTC/XMP shape)
- `to`: key object (EXIF/IPTC/XMP shape)

Key object shapes:
- EXIF: `{ "kind": "exif.tag", "ifd": "...", "tag": "0x010f" }`
- IPTC: `{ "kind": "iptc.dataset", "record": 2, "dataset": 5 }`
- XMP: `{ "kind": "xmp.property", "schema_ns": "...", "property": "..." }`

Optional fields: `scope`, `strategy`, `direction` (`"both" | "from_to" | "to_from"`).

## `kind: "mwg.tag"`
Describes composite tag behavior (preferred read order + write fanout).

Required fields: `kind`, `name`, `desire` (ordered list of tag identifiers).
Optional fields: `write_also` (list), `require` (single prerequisite identifier).
