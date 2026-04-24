# API Stability

This page defines the adoption status for public OpenMeta APIs.
Python bindings mirror these labels unless a Python wrapper documents a
different status.

## Stability Levels

| Level | Meaning |
| --- | --- |
| Stable | Intended for downstream use. Breaking changes require a new contract version, a compatibility path, or a documented migration. |
| Experimental | Public and tested, but the exact shape or semantics may still evolve while the surrounding workflow is being hardened. |
| Internal | Publicly visible only because it is part of a lower-level implementation surface. Do not build new downstream integrations on it unless another doc names it as supported. |

## Host-Facing API Map

| API surface | Header | Stability | Notes |
| --- | --- | --- | --- |
| Runtime capability query: `metadata_capability(...)` | `openmeta/metadata_capabilities.h` | Stable | v1 query contract for read, structured decode, transfer preparation, target edit, and raw-preservation status by format/family. |
| Compatibility dumps: `dump_metadata_compatibility(...)`, `dump_transfer_compatibility(...)` | `openmeta/compatibility_dump.h` | Stable | Stable v1 line-oriented compatibility dump contract. See [compatibility_dump.md](compatibility_dump.md). |
| XMP sync and writeback policy enums: `XmpConflictPolicy`, existing-carrier precedence enums, `XmpWritebackMode`, destination carrier modes | `openmeta/xmp_dump.h`, `openmeta/metadata_transfer.h` | Stable | Stable bounded writer policy for generated portable XMP. See [xmp_sync_policy.md](xmp_sync_policy.md). |
| Generic metadata traversal: `visit_metadata(...)`, `MetadataSink`, `ExportOptions`, `ExportItem` | `openmeta/interop_export.h` | Stable | v1 traversal contract. Borrowed names are valid only during `MetadataSink::on_item(...)`. |
| `ExportNameStyle::Canonical` and `ExportNameStyle::XmpPortable` | `openmeta/interop_export.h` | Stable | Stable naming modes for key-space-aware and portable exports. |
| `ExportNameStyle::FlatHost` | `openmeta/interop_export.h` | Stable | Stable v1 flat host naming contract. See [flat_host_mapping.md](flat_host_mapping.md). |
| Source snapshot type and read helpers: `TransferSourceSnapshot`, `read_transfer_source_snapshot_file(...)`, `read_transfer_source_snapshot_bytes(...)`, `build_transfer_source_snapshot(...)` | `openmeta/metadata_transfer.h` | Experimental | Current snapshots are decoded-store-backed and do not preserve raw source packets for passthrough. Const reuse is safe when callers do not mutate the snapshot and do not share returned result objects across writers. |
| Fileless preparation: `prepare_metadata_for_target_snapshot(...)` | `openmeta/metadata_transfer.h` | Experimental | Intended for hosts that already decoded metadata and want to prepare transfer artifacts without reopening the source file. |
| Snapshot execution: `execute_prepared_transfer_snapshot(...)` | `openmeta/metadata_transfer.h` | Experimental | Intended for deferred save/writeback from a reusable decoded source snapshot. |
| Bundle execution: `execute_prepared_transfer_bundle(...)` | `openmeta/metadata_transfer.h` | Experimental | Intended for hosts that already own a prepared bundle and destination bytes. Treat bundles as immutable except through documented patch helpers. |
| Adapter-view execution: `build_prepared_transfer_adapter_view(...)`, `emit_prepared_transfer_adapter_view(...)` | `openmeta/metadata_transfer.h` | Experimental | Target-neutral operation view for host-owned encoders and writers. Route and dispatch details may still evolve. |
| Generated transfer payload internals, route strings, low-level package chunks, and diagnostic counters not documented by a stable API page | `openmeta/metadata_transfer.h` | Internal | These fields may be useful for tests and diagnostics, but they are not a compatibility contract for downstream integrations. |

## Practical Guidance

Use stable APIs for normal application integrations. Use experimental APIs when
they match a real workflow and the integration can track OpenMeta releases.
Avoid internal surfaces unless you are contributing to OpenMeta itself or
writing a test that is intentionally tied to implementation details.
