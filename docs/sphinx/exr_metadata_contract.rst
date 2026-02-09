EXR Metadata Contract
=====================

Goal
----

Define a stable representation for OpenEXR header metadata in ``MetaStore`` so
read/export workflows are deterministic and lossless.

Contract constant: ``kExrCanonicalEncodingVersion == 1``.

Key Space
---------

Each EXR attribute is keyed as ``MetaKeyKind::ExrAttribute`` with:

- ``part_index``
- attribute ``name``

Wire Contract
-------------

For EXR entries:

- ``Origin::wire_type.family = WireFamily::Other``
- ``Origin::wire_type.code = EXR type code``
- ``Origin::wire_count = raw attribute byte size``

Unknown/custom EXR attribute types are preserved as:

- ``MetaValueKind::Bytes`` raw payload
- optional ``Origin::wire_type_name`` with original EXR type string

Canonical Value Mapping
-----------------------

- ``int`` -> scalar ``I32``
- ``float`` -> scalar ``F32``
- ``double`` -> scalar ``F64``
- ``rational`` -> scalar ``SRational``
- ``string`` -> text (ASCII/UTF-8/Unknown best-effort)
- ``v2*``/``v3*`` -> numeric arrays
- ``m33*``/``m44*`` -> numeric arrays (row-major)
- ``box2*`` -> arrays ``[min.x, min.y, max.x, max.y]``
- ``compression``/``lineOrder``/``envmap``/``deepImageState`` -> scalar ``U8``
- ``timecode`` -> ``U32[2]``
- ``keycode`` -> ``I32[7]``
- ``tiledesc`` -> ``U32[3]`` (x size, y size, mode)
- complex payloads (for example ``chlist``, ``preview``, ``stringvector``)
  remain bytes in core storage

Reference
---------

- Decoder API: ``openmeta/exr_decode.h``
- EXR tests: ``tests/exr_decode_test.cc``
- Interop naming: ``interop_api.rst``
