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

Proposed Core API (Draft)
-------------------------

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

1. Add ``visit_metadata(...)`` with ``Canonical`` naming only.
2. Add ``Oiio`` naming mode for parity checks vs. OIIO naming conventions.
3. Add typed conversion helpers (int/float/rational/text/bytes).
4. Add adapter modules (kept outside core decode path).
5. Promote from draft after two stable iterations of API+tests.

