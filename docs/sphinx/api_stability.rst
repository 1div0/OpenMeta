API Stability
=============

This page defines the adoption status for public OpenMeta APIs.
Python bindings mirror these labels unless a Python wrapper documents a
different status.

Stability levels
----------------

.. list-table::
   :header-rows: 1
   :widths: 20 80

   * - Level
     - Meaning
   * - Stable
     - Intended for downstream use. Breaking changes require a new contract
       version, a compatibility path, or a documented migration.
   * - Experimental
     - Public and tested, but the exact shape or semantics may still evolve
       while the surrounding workflow is being hardened.
   * - Internal
     - Publicly visible only because it is part of a lower-level
       implementation surface. Do not build new downstream integrations on it
       unless another doc names it as supported.

Host-facing API map
-------------------

.. list-table::
   :header-rows: 1
   :widths: 24 24 14 38

   * - API surface
     - Header
     - Stability
     - Notes
   * - Runtime capability query: ``metadata_capability(...)``
     - ``openmeta/metadata_capabilities.h``
     - Stable
     - v1 query contract for read, structured decode, transfer preparation,
       target edit, and raw-preservation status by format/family.
   * - Compatibility dumps: ``dump_metadata_compatibility(...)``,
       ``dump_transfer_compatibility(...)``
     - ``openmeta/compatibility_dump.h``
     - Stable
     - Stable v1 line-oriented compatibility dump contract. See
       :doc:`compatibility_dump`.
   * - XMP sync and writeback policy enums: ``XmpConflictPolicy``,
       existing-carrier precedence enums, ``XmpWritebackMode``, destination
       carrier modes
     - ``openmeta/xmp_dump.h``, ``openmeta/metadata_transfer.h``
     - Stable
     - Stable bounded writer policy for generated portable XMP. See
       :doc:`xmp_sync_policy`.
   * - Generic metadata traversal: ``visit_metadata(...)``,
       ``MetadataSink``, ``ExportOptions``, ``ExportItem``
     - ``openmeta/interop_export.h``
     - Stable
     - v1 traversal contract. Borrowed names are valid only during
       ``MetadataSink::on_item(...)``.
   * - ``ExportNameStyle::Canonical`` and
       ``ExportNameStyle::XmpPortable``
     - ``openmeta/interop_export.h``
     - Stable
     - Stable naming modes for key-space-aware and portable exports.
   * - ``ExportNameStyle::FlatHost``
     - ``openmeta/interop_export.h``
     - Stable
     - Stable v1 flat host naming contract. See :doc:`flat_host_mapping`.
   * - EXIF/TIFF orientation helpers: ``interpret_exif_orientation(...)``,
       ``exif_orientation_name(...)``,
       ``exif_orientation_rotation_degrees_cw(...)``,
       ``exif_orientation_rotation_only(...)``
     - ``openmeta/orientation.h``
     - Stable
     - Small utility contract for user-facing orientation labels, clockwise
       rotation degrees, mirrored-state detection, dimension-swap detection,
       and rotation-only fallbacks. Python exposes the same helpers through
       thin scalar/dictionary wrappers.
   * - EXIF/TIFF/DNG numeric value names:
       ``exif_tag_numeric_value_name(...)`` and focused helpers
     - ``openmeta/exif_value_names.h``
     - Stable
     - Small helper contract for common enum-like TIFF/EXIF/DNG numeric values
       such as compression, photometric interpretation, planar configuration,
       exposure program, metering mode, light source, flash, color space, white
       balance, scene capture type, gain control, CFA layout, and DNG
       calibration illuminants. Unknown values return an empty string and
       remain lossless numeric metadata.
   * - Semantic metadata query: ``query_metadata(...)``,
       ``query_crop_metadata(...)``, focused query helpers, and
       ``metadata_query_fuzzy_search_available()``
     - ``openmeta/metadata_query.h``
     - Experimental
     - Query contract for inspection matches plus normalized candidates.
       Current coverage includes crop/active-area/border margins,
       exposure/gain, white balance, color, lens correction, orientation, and
       RAW/source-processing metadata across standard tags, selected DNG tags,
       Fujifilm RAF raw crop/zoom rectangles, Canon aspect/crop metadata,
       Nikon Capture crop bounds, Sony panorama crop margins, fuzzy XMP paths,
       and vendor RAW-processing classification. Matches report ``exact_match``,
       ``fuzzy_match``, and ``fuzzy_score`` so tools can label exact results
       separately from RapidFuzz near-miss hits.
       ``OPENMETA_ENABLE_RAPIDFUZZ=ON`` adds optional near-miss
       XMP/property-path scoring. Grouped candidates
       include ``matrix_set``, ``vector_set``, and ``table`` shapes for related
       non-crop metadata, including RAW black/white levels, linearization,
       CFA/sensor layout, source geometry, raw-storage identifiers, and
       source-processing buckets, and per-family vendor MakerNote/RAW
       white-balance, color, raw-storage, sensor, and source-processing groups.
       Python ``Document`` and ``TransferSourceSnapshot`` mirror this as thin
       dictionary-returning wrappers.
   * - Structured metadata interpretation records:
       ``interpret_metadata(...)`` and ``interpret_metadata_query(...)``
     - ``openmeta/metadata_interpretation.h``
     - Experimental
     - Thin structured projection over semantic query candidates. Records
       carry query class, semantic kind, normalized shape, confidence, source
       entry ids, and normalized origin/size/rect/margins/value arrays where
       available. Current scope covers orientation, geometry/crop/border
       including Fujifilm RAF, Canon, Nikon Capture, and Sony panorama
       geometry patterns, exposure/gain, color/white-balance, lens-correction,
       and RAW/source-processing records, and grouped vendor-family table/vector
       records where
       classification supports them. Python ``Document`` and
       ``TransferSourceSnapshot`` expose matching dictionary wrappers.
   * - Cross-family concept resolution:
       ``resolve_metadata_concepts(...)`` and
       ``resolve_metadata_concept(...)``
     - ``openmeta/metadata_concepts.h``
     - Experimental
     - First bounded resolver for duplicated host-facing concepts. Current
       scope reports candidates, candidate source entries, source families,
       preferred entries, normalized numeric/text keys, full normalized value
       vectors, transfer hints, normalized date/time fields, date/time
       precision, timezone kind, normalized geometry fields, and same-role
       conflicts for
       orientation, date/time, color/profile, GPS, geometry, lens-correction,
       and RAW-processing evidence across EXIF, XMP, IPTC, ICC, PNG text, and
       query-backed interpretation records where applicable. Geometry
       candidates cover crop, active area, border, and sensor geometry with
       canonical origin, size, rect, and margin fields when available,
       including normalized DNG, Phase One/Leaf, Fujifilm RAF, Canon, Nikon
       Capture, and Sony panorama geometry patterns.
       Candidate transfer hints distinguish ``safe``, ``source_bound``,
       ``rendered_unsafe``, and ``requires_target_image_spec`` evidence, with
       compatible-file and rendered-image safety booleans.
       Color/white-balance, lens-correction, and RAW-processing concepts
       preserve grouped matrix/vector/table values for host inspection; they
       do not make source-bound values safe to serialize into rendered targets.
       GPS date/time is combined from ``GPSDateStamp`` plus ``GPSTimeStamp``
       when both entries exist, and GPS altitude candidates expose
       altitude-reference code plus below-sea-level state when reference
       metadata is present; ``metadata_concept_gps_altitude_reference_name(...)``
       provides a stable display token for the reference code. It is intended
       for inspection UI and host policy decisions; it does not rewrite
       metadata or hide ambiguity. Python ``Document`` and
       ``TransferSourceSnapshot`` expose matching dictionary wrappers.
   * - Transfer concept diagnostics:
       ``transfer_concept_diagnostics_from_store(...)``,
       ``transfer_concept_diagnostic_message(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Preflight view over concept candidates for ``TransferSafetyMode``.
       Each diagnostic reports concept kind/role, transfer hint,
       keep/drop/requires-target-image-spec action, reason token, severity
       token, default message text, conflict flag, source entries,
       compatible/rendered safety booleans, and GPS altitude-reference
       presentation fields. Intended for UI previews and host policy messages
       before calling ``prepare_metadata_for_target(...)``; it does not replace
       the actual transfer filter. Python ``Document`` and
       ``TransferSourceSnapshot`` expose ``transfer_concept_diagnostics(...)``
       dictionaries with ``severity_name`` and ``message`` fields.
   * - Vendor RAW-processing summaries:
       ``vendor_raw_processing_from_store(...)``,
       ``classify_vendor_raw_processing_field(...)``
     - ``openmeta/vendor_raw_processing.h``
     - Experimental
     - Conservative grouped source-RAW/source-processing field summaries for decoded Sony,
       Canon, Nikon, Fujifilm, Pentax, Panasonic, Olympus, Kodak, Minolta,
       Sigma, Samsung, Ricoh, Apple, DJI, Google, FLIR, Casio, Sanyo,
       KyoceraRaw, Reconyx, HP, JVC, GE, Motorola, Nintendo, and Microsoft
       MakerNotes, including vendor-private, computational, thermal, preview,
       face-geometry, stitch/panorama, Apple computational capture/HDR/motion,
       DJI pose/thermal, Google HDR+/shot-log, pixel-shift/multi-shot/
       composite/auto-lighting source processing, and FLIR
       radiometric/raw-value buckets. Intended for audit/UI and
       rendered-transfer safety decisions. Direct field classification also
       recognizes decoded Phase One/Leaf RAW-processing tags; use the dedicated
       Phase One/Leaf helpers for normalized geometry and processing summaries.
       Do not write vendor RAW/source-processing values into rendered targets.
   * - Transfer safety audit:
       ``transfer_safety_audit_from_store(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Preflight summary of source entries and entries filtered or invalidated
       by ``TransferSafetyMode``, including
       Sony/Canon/Nikon/Fujifilm/Pentax/Panasonic/Olympus/Kodak/Minolta/
       Sigma/Samsung/Ricoh/Apple/DJI/Google/FLIR/Casio/Sanyo/KyoceraRaw/
       Reconyx/HP/JVC/GE/Motorola/Nintendo/Microsoft RAW/source-processing
       buckets. Intended for diagnostics and host UI before preparing
       rendered-image transfers.
   * - Raw-carrier passthrough audit:
       ``raw_carrier_passthrough_audit_from_snapshot(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Diagnostic preflight for opt-in raw carriers. Reports candidate
       carriers and primary block reasons such as missing payload, target
       incompatibility, safety filtering, content-bound C2PA, explicit profile
       policy, missing decoded-entry links, or unsupported carrier kind. Does
       Hosts can call it directly before enabling snapshot passthrough.
   * - Source snapshot type and read helpers:
       ``TransferSourceSnapshot``,
       ``read_transfer_source_snapshot_file(...)``,
       ``read_transfer_source_snapshot_bytes(...)``,
       ``build_transfer_source_snapshot(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Current snapshots are decoded-store-backed by default. Opt-in raw
       carriers preserve bounded source payload/provenance records and
       snapshot-local decoded entry ids for host diagnostics and bounded
       passthrough decisions. Const reuse is safe when callers do not mutate
       the snapshot and do not share returned result objects across writers.
   * - Fileless preparation:
       ``prepare_metadata_for_target_snapshot(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Intended for hosts that already decoded metadata and want to prepare
       transfer artifacts without reopening the source file.
       ``TransferRawCarrierPassthroughMode::WhenSafe`` is an opt-in snapshot
       mode; the current writer path only reuses eligible non-C2PA JUMBF and
       draft unsigned C2PA invalidation carriers for JPEG, JXL, and BMFF
       targets, plus draft unsigned C2PA invalidation carriers for WebP.
   * - Snapshot execution:
       ``execute_prepared_transfer_snapshot(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Intended for deferred save/writeback from a reusable decoded source
       snapshot.
   * - Bundle execution:
       ``execute_prepared_transfer_bundle(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Intended for hosts that already own a prepared bundle and destination
       bytes. Treat bundles as immutable except through documented patch
       helpers.
   * - Adapter-view execution:
       ``build_prepared_transfer_adapter_view(...)``,
       ``emit_prepared_transfer_adapter_view(...)``
     - ``openmeta/metadata_transfer.h``
     - Experimental
     - Target-neutral operation view for host-owned encoders and writers.
       Route and dispatch details may still evolve.
   * - Generated transfer payload internals, route strings, low-level package
       chunks, and diagnostic counters not documented by a stable API page
     - ``openmeta/metadata_transfer.h``
     - Internal
     - These fields may be useful for tests and diagnostics, but they are not a
       compatibility contract for downstream integrations.

Practical guidance
------------------

Use stable APIs for normal application integrations. Use experimental APIs when
they match a real workflow and the integration can track OpenMeta releases.
Avoid internal surfaces unless you are contributing to OpenMeta itself or
writing a test that is intentionally tied to implementation details.
