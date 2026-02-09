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

Adapters
--------

- OIIO adapter: ``collect_oiio_attributes(...)`` in ``openmeta/oiio_adapter.h``.
- OCIO adapter: ``build_ocio_metadata_tree(...)`` in ``openmeta/ocio_adapter.h``.
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
- ``Document.ocio_metadata_tree(...)``
