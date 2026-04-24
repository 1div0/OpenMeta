Interop API
===========

Goal
----

Define a small export interface from ``MetaStore`` that is easy to adapt to:

- flat host metadata conventions.
- OpenEXR header attributes.
- OpenColorIO ``FormatMetadata`` trees.

Stable API (v1)
---------------

Contract constant: ``kInteropExportContractVersion == 1``.
For broader adoption status across public APIs, see :doc:`api_stability`.
For the stable flat host naming contract, see :doc:`flat_host_mapping`.
For deterministic host compatibility baselines, see :doc:`compatibility_dump`.
For generated XMP merge and writeback precedence, see
:doc:`xmp_sync_policy`.

.. code-block:: cpp

   enum class ExportNameStyle : uint8_t {
       Canonical,   // exif:ifd0:0x010F
       XmpPortable, // tiff:Make, exif:ExposureTime
       FlatHost    // Make, Exif:ExposureTime, GPS:Latitude
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
- the flat host-attribute style keeps EXR attribute aliases where practical
  (for example ``owner -> Copyright``, ``capDate -> DateTime``), while
  non-standard EXR attributes remain under ``openexr:*``.

Integration Surfaces
--------------------

- host-owned metadata surface: ``visit_metadata(...)`` in
  ``openmeta/interop_export.h``.
  This is the intended base for custom host metadata mappings.
- OCIO export-only adapter: ``build_ocio_metadata_tree(...)`` in
  ``openmeta/ocio_adapter.h``.
  Stable request model: ``OcioAdapterRequest``.
  Strict safe variant: ``build_ocio_metadata_tree_safe(...)`` with
  ``InteropSafetyError``.
- EXR host-apply adapter: ``build_exr_attribute_batch(...)`` in
  ``openmeta/exr_adapter.h``.
  It exports one owned EXR-native attribute batch from ``MetaStore`` for
  OpenEXR header workflows.
  ``build_exr_attribute_part_spans(...)`` groups that batch by part, and
  ``build_exr_attribute_part_views(...)`` exposes zero-copy grouped per-part
  views,
  ``replay_exr_attribute_batch(...)`` replays the grouped batch through
  explicit host callbacks.
  Unknown/custom attrs remain opaque only when the original EXR type name is
  preserved in ``Origin::wire_type_name``.

Reference Tests
---------------

- ``tests/interop_export_test.cc``
- ``tests/ocio_adapter_test.cc``
- ``tests/exr_adapter_test.cc``

Python Entry Points
-------------------

- ``Document.export_names(style=..., include_makernotes=...)``
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
