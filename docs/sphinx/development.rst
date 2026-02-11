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

Python binding entry points:

- ``Document.export_names(...)``
- ``Document.oiio_attributes(...)``
- ``Document.ocio_metadata_tree(...)``
- ``Document.dump_xmp_sidecar(...)`` (lossless or portable via format switch)

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
