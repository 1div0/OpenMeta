Interop API
===========

Goal
----

Define a small export interface from ``MetaStore`` that is easy to adapt to:

- OpenImageIO ``ImageSpec`` metadata conventions.
- OpenEXR header attributes.
- OpenColorIO ``FormatMetadata`` trees.

Stable API (v1)
---------------

Contract constant: ``kInteropExportContractVersion == 1``.

.. code-block:: cpp

   enum class ExportNameStyle : uint8_t {
       Canonical,   // exif:ifd0:0x010F
       XmpPortable, // tiff:Make, exif:ExposureTime
       Oiio         // Make, Exif:ExposureTime, GPS:Latitude
   };

   struct ExportOptions final {
       ExportNameStyle style = ExportNameStyle::Canonical;
       bool include_origin = false;
       bool include_flags = false;
       bool include_makernotes = true;
   };

   struct ExportItem final {
       std::string_view name;
       const Entry* entry = nullptr;
       const Origin* origin = nullptr; // only when include_origin=true
       EntryFlags flags = EntryFlags::None; // only when include_flags=true
   };

   class MetadataSink {
   public:
       virtual ~MetadataSink() = default;
       virtual void on_item(const ExportItem& item) = 0;
   };

   void visit_metadata(const MetaStore& store, const ExportOptions& options,
                       MetadataSink& sink) noexcept;

Naming Contract
---------------

- ``Canonical``: stable key-space-aware names for lossless workflows.
- ``XmpPortable``: normalized names intended for cross-tool interchange.
- ``Oiio``: OIIO-friendly names for drop-in metadata attribute use.
  EXR attributes map common OpenEXR names to OIIO-style names where practical
  (for example ``owner -> Copyright``, ``capDate -> DateTime``), while
  non-standard EXR attributes remain under ``openexr:*``.

Adapters
--------

- OIIO adapter: ``collect_oiio_attributes(...)`` in ``openmeta/oiio_adapter.h``.
  Stable request model: ``OiioAdapterRequest``.
  Typed variant: ``collect_oiio_attributes_typed(...)``.
  ``OiioTypedValue`` keeps original MetaValue typing (scalar/array/bytes/text)
  for host integrations that need non-string metadata values.
  Strict safe variants: ``collect_oiio_attributes_safe(...)`` and
  ``collect_oiio_attributes_typed_safe(...)`` with ``InteropSafetyError``.
- OCIO adapter: ``build_ocio_metadata_tree(...)`` in ``openmeta/ocio_adapter.h``.
  Stable request model: ``OcioAdapterRequest``.
  Strict safe variant: ``build_ocio_metadata_tree_safe(...)`` with
  ``InteropSafetyError``.
- EXR flows use ``MetaKeyKind::ExrAttribute`` and map through ``Canonical`` or
  OIIO-style names depending on target API.

Reference Tests
---------------

- ``tests/interop_export_test.cc``
- ``tests/oiio_adapter_test.cc``
- ``tests/ocio_adapter_test.cc``

Python Entry Points
-------------------

- ``Document.export_names(style=..., include_makernotes=...)``
- ``Document.oiio_attributes(...)``
- ``Document.unsafe_oiio_attributes(...)``
- ``Document.oiio_attributes_typed(...)``
- ``Document.unsafe_oiio_attributes_typed(...)``
- ``Document.ocio_metadata_tree(...)``
- ``Document.unsafe_ocio_metadata_tree(...)``
- ``Document.dump_xmp_sidecar(format=...)``

XMP Sidecar Export API
----------------------

``openmeta/xmp_dump.h`` exposes a stable flat request model for wrappers and
adapters:

- ``XmpSidecarRequest`` (format + limits + portable/lossless switches)
- ``dump_xmp_sidecar(const MetaStore&, std::vector<std::byte>*, const XmpSidecarRequest&)``

The nested ``XmpSidecarOptions`` API remains available for advanced callers.
