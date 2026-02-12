Developer Notes
===============

Repository layout (public):

- ``src/include/openmeta/``: public headers
- ``src/openmeta/``: implementation
- ``src/tools/``: CLI tools
- ``src/python/``: Python bindings and helper scripts
- ``tests/``: unit tests and fuzz targets

EXIF + MakerNotes (code organization)
-------------------------------------

- Core EXIF/TIFF decoding: ``src/openmeta/exif_tiff_decode.cc``
- CRW/CIFF decode + derived EXIF bridge: ``src/openmeta/crw_ciff_decode.cc``
- Vendor MakerNote decoders: ``src/openmeta/exif_makernote_*.cc``
  (Canon, Nikon, Sony, Olympus, Pentax, Casio, Panasonic, Kodak, Ricoh, Samsung, FLIR, etc.)
- Shared internal-only helpers: ``src/openmeta/exif_tiff_decode_internal.h``
  (not installed)
- Unit tests for MakerNote paths: ``tests/makernote_decode_test.cc``

Internal helper conventions (used by vendor decoders):

- ``read_classic_ifd_entry(...)`` + ``ClassicIfdEntry``: parse a single 12-byte classic TIFF IFD entry.
- ``resolve_classic_ifd_value_ref(...)`` + ``ClassicIfdValueRef``: compute the value location/size for a classic IFD entry (inline vs out-of-line), using ``MakerNoteLayout`` + ``OffsetPolicy``.
- ``MakerNoteLayout`` + ``OffsetPolicy``: makes "value offsets are relative to X" explicit for vendor formats. ``OffsetPolicy`` supports both the common unsigned base (default) and a signed base for vendors that require it (eg Canon).
- ``ExifContext``: a small, decode-time cache for frequently accessed EXIF values.
- MakerNote tag-name tables are generated from ``registry/exif/makernotes/*.jsonl`` and looked up via binary search (``exif_makernote_tag_names.cc``).

Interop adapters
----------------

- Core traversal API: ``src/include/openmeta/interop_export.h``
- OIIO adapter (flat name/value): ``src/include/openmeta/oiio_adapter.h``
- OCIO adapter (namespace tree): ``src/include/openmeta/ocio_adapter.h``

Notes:

- ``ExportNamePolicy::ExifToolAlias`` and ``ExportNamePolicy::Spec`` are both
  covered by interop tests and used for split-parity workflows.
- OIIO export keeps numeric unknown names (``Exif_0x....``) and
  ``Exif:MakerNote`` when values are empty so parity checks remain stable.

Python binding entry points:

- ``Document.export_names(...)``
- ``Document.oiio_attributes(...)``
- ``Document.unsafe_oiio_attributes(...)``
- ``Document.oiio_attributes_typed(...)``
- ``Document.unsafe_oiio_attributes_typed(...)``
- ``Document.ocio_metadata_tree(...)``
- ``Document.unsafe_ocio_metadata_tree(...)``
- ``Document.dump_xmp_sidecar(...)`` (lossless or portable via format switch)

C++ adapter entry points:

- safe API: ``collect_oiio_attributes_safe(..., InteropSafetyError*)``
- unsafe API: ``collect_oiio_attributes(...)``
- ``collect_oiio_attributes(..., const OiioAdapterRequest&)`` in
  ``openmeta/oiio_adapter.h`` (stable flat request API)
- ``collect_oiio_attributes(..., const OiioAdapterOptions&)`` (advanced/legacy shape)
- safe typed API: ``collect_oiio_attributes_typed_safe(..., InteropSafetyError*)``
- unsafe typed API: ``collect_oiio_attributes_typed(...)``
- ``collect_oiio_attributes_typed(..., const OiioAdapterRequest&)`` (typed values)
- ``collect_oiio_attributes_typed(..., const OiioAdapterOptions&)`` (typed values)
  typed payload model: ``OiioTypedValue`` / ``OiioTypedAttribute``

Python typed behavior:

- ``Document.oiio_attributes(...)`` is safe-by-default and raises on unsafe
  raw byte payloads; use ``Document.unsafe_oiio_attributes(...)`` for
  legacy/raw fallback output.
- ``Document.oiio_attributes_typed(...)`` decodes text values to Python ``str``
  in safe mode and raises on unsafe/invalid text bytes.
- ``Document.unsafe_oiio_attributes_typed(...)`` returns raw text bytes for
  explicit unsafe workflows.
- ``Document.ocio_metadata_tree(...)`` is safe-by-default and raises on unsafe
  raw byte payloads; use ``Document.unsafe_ocio_metadata_tree(...)`` for
  legacy/raw fallback output.
- safe API: ``build_ocio_metadata_tree_safe(..., InteropSafetyError*)``
- unsafe API: ``build_ocio_metadata_tree(...)``
- ``build_ocio_metadata_tree(..., const OcioAdapterRequest&)`` in
  ``openmeta/ocio_adapter.h`` (stable flat request API)
- ``build_ocio_metadata_tree(..., const OcioAdapterOptions&)`` (advanced/legacy shape)

C++ XMP sidecar entry points:

- ``dump_xmp_sidecar(..., const XmpSidecarRequest&)`` in
  ``openmeta/xmp_dump.h`` (stable flat request API)
- ``dump_xmp_sidecar(..., const XmpSidecarOptions&)`` (advanced/legacy shape)

Optional dependencies
---------------------

OpenMeta's core scanning and EXIF/TIFF decoding do not require third-party
libraries. Some metadata payloads are compressed or structured; these optional
dependencies let OpenMeta decode more content:

- **Expat** (``OPENMETA_WITH_EXPAT``): parses XMP RDF/XML packets (embedded
  blocks and ``.xmp`` sidecars) using a streaming parser with strict limits.
- **zlib** (``OPENMETA_WITH_ZLIB``): inflates Deflate-compressed payloads such
  as PNG ``iCCP`` (ICC profiles) and compressed text/XMP chunks (``iTXt``,
  ``zTXt``).
- **Brotli** (``OPENMETA_WITH_BROTLI``): decompresses JPEG XL ``brob`` "compressed
  metadata" boxes so wrapped metadata payloads can be decoded.

CLI tool
--------

``metaread`` prints a human-readable dump of blocks and decoded entries
(EXIF/TIFF-IFD tags, XMP properties, IPTC-IIM datasets, ICC profile fields/tags,
and Photoshop IRB resource blocks). Output is ASCII-only and truncated by
default to reduce terminal injection risk.

Python
------

Python bindings use nanobind. The wheel also ships helper scripts as
``openmeta.python.*`` modules.

.. code-block:: bash

   python3 -m openmeta.python.metaread file.jpg
   python3 -m openmeta.python.metadump --format portable file.jpg
   python3 -m openmeta.python.metadump --format portable --portable-include-existing-xmp --xmp-sidecar file.jpg

Documentation build
-------------------

Sphinx docs require:

- ``doxygen``
- Python packages listed in ``docs/requirements.txt``

.. code-block:: bash

   uv pip install -r docs/requirements.txt
   cmake -S . -B build -DOPENMETA_BUILD_SPHINX_DOCS=ON
   cmake --build build --target openmeta_docs_sphinx
