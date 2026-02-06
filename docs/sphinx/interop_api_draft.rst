Interop API (Draft)
===================

.. warning::

   **Draft**: This interface is intentionally unstable while we refactor.
   Expect signature and naming changes across multiple iterations.

Goal
----

Define a small export interface from ``MetaStore`` that is easy to adapt to:

- OpenImageIO ``ImageSpec`` metadata conventions.
- OpenEXR header attributes.
- OpenColorIO ``FormatMetadata`` trees.

Current Draft API (v1)
----------------------

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

Adapter Boundaries (Draft)
--------------------------

- OIIO adapter: map ``ExportItem`` to ``ImageSpec::attribute(...)`` using
  ``ExportNameStyle::Oiio``.
- OpenEXR adapter: map scalar/text values to typed EXR attributes; preserve
  unsupported values as ``BytesAttribute`` or opaque bytes.
- OCIO adapter: map to hierarchical ``FormatMetadata`` nodes/attributes from
  ``ExportNameStyle::XmpPortable`` or ``Canonical``.

Refactor Checkpoints
--------------------

Status
------

Implemented in draft form:

1. ``visit_metadata(...)`` with ``Canonical``, ``XmpPortable``, and ``Oiio``
   name styles.
2. ``include_makernotes`` policy switch in the shared export path.
3. Draft adapters:
   - ``collect_oiio_attributes(...)`` in ``openmeta/oiio_adapter.h``
   - ``build_ocio_metadata_tree(...)`` in ``openmeta/ocio_adapter.h``
4. Unit tests:
   - ``tests/interop_export_test.cc``
   - ``tests/oiio_adapter_test.cc``
   - ``tests/ocio_adapter_test.cc``
5. Python binding wrappers:
   - ``Document.export_names(...)``
   - ``Document.oiio_attributes(...)``
   - ``Document.ocio_metadata_tree(...)``

Still draft / planned follow-ups:

- Expand typed conversion beyond text-form export for direct SDK bridging.
- Add corpus parity automation over cached tool outputs for adapter names/values.
- Review naming policy against real OIIO/OCIO integration points.
