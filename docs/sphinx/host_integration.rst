Host Integration
================

This guide is for applications that already own the image or container side
and want OpenMeta to handle metadata.

OpenMeta is not an image encoder. The normal pattern is:

- decode metadata from one source file
- query or edit it in ``MetaStore``
- hand prepared metadata to your own writer, encoder, or SDK

If you want the shortest end-to-end examples first, start with
:doc:`quick_start`.

Pick the integration path
-------------------------

Use the narrowest public API that matches your host:

=============================== ===========================================================
Host owns                       Use
=============================== ===========================================================
Existing target file/template   ``execute_prepared_transfer_file(...)`` +
                                ``persist_prepared_transfer_file_result(...)``
EXR writer                      ``build_exr_attribute_batch_from_file(...)``
Host-owned metadata object      ``visit_metadata(...)``
JPEG/JXL/WebP/PNG/JP2/BMFF      ``prepare_metadata_for_target_file(...)`` +
encoder path                    adapter view or backend emitter
Adobe DNG SDK objects/files     ``dng_sdk_adapter.h``
=============================== ===========================================================

There is no public fuzzy-search API yet. Query through exact keys and build
your own display or search layer on top.

Adapter classes
---------------

OpenMeta splits host integration surfaces deliberately:

- export-only naming/traversal surface:
  ``visit_metadata(...)`` for host-owned metadata mapping layers
- export-only adapter:
  ``build_ocio_metadata_tree(...)`` for OCIO-style metadata trees
- host-apply adapter:
  ``build_exr_attribute_batch(...)`` for EXR/OpenEXR header workflows
- direct bridge:
  ``dng_sdk_adapter.h`` for applications that already use Adobe DNG SDK
  objects
- narrow translator:
  ``libraw_adapter.h`` for orientation mapping into LibRaw flip space

Read and query
--------------

The basic read path is covered in :doc:`quick_start`. Once you have a
``MetaStore``, the main lookup API is exact-key lookup through
``MetaStore::find_all(...)``.

Generic host metadata traversal
-------------------------------

Use the traversal API when your application owns the metadata object model and
needs deterministic exported names plus the original ``Entry``.

.. code-block:: cpp

   #include "openmeta/interop_export.h"

   class MyMetadataSink final : public openmeta::MetadataSink {
   public:
       void on_item(const openmeta::ExportItem& item) noexcept override
       {
           // Map item.name + item.entry into your host metadata object.
       }
   };

   openmeta::ExportOptions options;
   options.style              = openmeta::ExportNameStyle::FlatHost;
   options.name_policy        = openmeta::ExportNamePolicy::ExifToolAlias;
   options.include_makernotes = true;

   MyMetadataSink sink;
   openmeta::visit_metadata(store, options, sink);

This keeps host-specific object ownership and write behavior outside OpenMeta.

EXR attribute batches
---------------------

This is the cleanest host-adapter path in OpenMeta today.

.. code-block:: cpp

   #include "openmeta/exr_adapter.h"

   openmeta::ExrAdapterBatch batch;
   openmeta::BuildExrAttributeBatchFileOptions options;

   openmeta::BuildExrAttributeBatchFileResult result =
       openmeta::build_exr_attribute_batch_from_file(
           "source.jpg", &batch, options);

   for (const openmeta::ExrAdapterAttribute& attr : batch.attributes) {
       // Forward attr.name, attr.type_name, and attr.value to your EXR writer.
   }

OpenMeta does not need OpenEXR headers for this path. It exports a neutral
batch of EXR-style attributes that your host can apply through OpenEXR or its
own EXR writer.

Host-owned JPEG or JXL output
-----------------------------

There are two public patterns for encoder-owned output:

- implement a backend emitter such as ``JpegTransferEmitter`` or
  ``JxlTransferEmitter``
- build an adapter view and consume one normalized list of operations

Adapter-view pattern
~~~~~~~~~~~~~~~~~~~~

Use this when you want one target-neutral operation list.

.. code-block:: cpp

   #include "openmeta/metadata_transfer.h"

   class MySink final : public openmeta::TransferAdapterSink {
   public:
       openmeta::TransferStatus
       emit_op(const openmeta::PreparedTransferAdapterOp& op,
               std::span<const std::byte> payload) noexcept override
       {
           // Dispatch on op.kind and forward payload into your backend.
           return openmeta::TransferStatus::Ok;
       }
   };

   openmeta::PrepareTransferFileOptions prepare;
   prepare.prepare.target_format = openmeta::TransferTargetFormat::Jxl;

   openmeta::PrepareTransferFileResult prepared =
       openmeta::prepare_metadata_for_target_file("source.jpg", prepare);

   openmeta::PreparedTransferAdapterView view;
   openmeta::build_prepared_transfer_adapter_view(
       prepared.bundle, &view, openmeta::EmitTransferOptions {});

   MySink sink;
   openmeta::emit_prepared_transfer_adapter_view(prepared.bundle, view, sink);

Backend-emitter pattern
~~~~~~~~~~~~~~~~~~~~~~~

Use this when your host already looks like one OpenMeta backend.

.. code-block:: cpp

   #include "openmeta/metadata_transfer.h"

   class MyJpegEmitter final : public openmeta::JpegTransferEmitter {
   public:
       openmeta::TransferStatus
       write_app_marker(uint8_t marker_code,
                        std::span<const std::byte> payload) noexcept override
       {
           // Write one APPn marker into your JPEG output path.
           return openmeta::TransferStatus::Ok;
       }
   };

   openmeta::PrepareTransferFileOptions prepare;
   prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;

   openmeta::PrepareTransferFileResult prepared =
       openmeta::prepare_metadata_for_target_file("source.jpg", prepare);

   openmeta::PreparedTransferExecutionPlan plan;
   openmeta::compile_prepared_transfer_execution(
       prepared.bundle, openmeta::EmitTransferOptions {}, &plan);

   MyJpegEmitter emitter;
   openmeta::emit_prepared_transfer_compiled(prepared.bundle, plan, emitter);

For JXL, implement ``JxlTransferEmitter::set_icc_profile(...)``,
``add_box(...)``, and ``close_boxes(...)``.

OpenMeta does not ship a TurboJPEG-specific wrapper yet. The intended
integration path is still through ``JpegTransferEmitter`` or the adapter view.

Edit an existing target file
----------------------------

If your host already has a target file or template on disk, use the file
helper instead of building your own writer path.

.. code-block:: cpp

   #include "openmeta/metadata_transfer.h"

   openmeta::ExecutePreparedTransferFileOptions exec_options;
   exec_options.prepare.prepare.target_format =
       openmeta::TransferTargetFormat::Tiff;
   exec_options.edit_target_path = "rendered.tif";

   openmeta::ExecutePreparedTransferFileResult exec =
       openmeta::execute_prepared_transfer_file("source.jpg", exec_options);

   openmeta::PersistPreparedTransferFileOptions persist;
   persist.output_path = "rendered_with_meta.tif";
   persist.overwrite_output = true;

   openmeta::PersistPreparedTransferFileResult saved =
       openmeta::persist_prepared_transfer_file_result(exec, persist);

Read once, save later
~~~~~~~~~~~~~~~~~~~~~

If your host already decoded source metadata during the initial load, keep a
decoded source snapshot and execute the later save without reopening the
source file.

.. code-block:: cpp

   #include "openmeta/metadata_transfer.h"

   openmeta::ReadTransferSourceSnapshotFileResult snapshot =
       openmeta::read_transfer_source_snapshot_file("source.jpg");

   openmeta::ExecutePreparedTransferSnapshotOptions options;
   options.prepare.target_format = openmeta::TransferTargetFormat::Tiff;
   options.edit_target_path      = "target.tif";
   options.execute.edit_apply    = true;

   openmeta::ExecutePreparedTransferFileResult result =
       openmeta::execute_prepared_transfer_snapshot(
           snapshot.snapshot, options);

Python mirrors that same host-facing snapshot flow:

.. code-block:: python

   from pathlib import Path

   import openmeta

   snapshot_info = openmeta.read_transfer_source_snapshot_file("source.jpg")
   snapshot = snapshot_info["snapshot"]

   result = openmeta.transfer_snapshot_file(
       snapshot,
       target_format=openmeta.TransferTargetFormat.Tiff,
       edit_target_path="target.tif",
       target_bytes=Path("target.tif").read_bytes(),
       output_path="edited.tif",
   )

Current source snapshots are decoded-store-backed. They are intended for the
normal EXIF/XMP/ICC/IPTC transfer flow, not raw source-packet passthrough.
If the host still owns the bundle/execution split, the lower-level
``prepare_metadata_for_target_snapshot(...)`` entry point remains available.
If the host already has a decoded ``MetaStore``, build a reusable snapshot with
``build_transfer_source_snapshot(store)``. If it already owns the source bytes
in memory, use ``read_transfer_source_snapshot_bytes(bytes)`` instead of the
file-path reader.
In Python, a previously decoded ``Document`` can be turned into a reusable
snapshot through ``doc.build_transfer_source_snapshot()`` or
``openmeta.build_transfer_source_snapshot(doc)``.
If it also owns the destination bytes in memory, call the overload
``execute_prepared_transfer_snapshot(snapshot, target_bytes, options)``.
If it already holds a prepared bundle, use
``execute_prepared_transfer_bundle(bundle, target_bytes, options)`` instead.
Snapshot execution supports the same existing-sidecar merge and destination
carrier-precedence controls as the file helper; when loading an existing
sidecar it defaults to ``edit_target_path`` unless
``xmp_existing_sidecar_base_path`` is set explicitly.
For embedded-only writeback with sidecar cleanup and no filesystem path, set
``xmp_existing_destination_sidecar_state`` explicitly so OpenMeta can return a
cleanup decision without guessing a sidecar location.
Python now exposes those same split path/state controls directly:
``xmp_existing_sidecar_base_path``, ``xmp_sidecar_base_path``,
``xmp_existing_destination_embedded_path``, and
``xmp_existing_destination_sidecar_state``.

Query runtime capabilities
--------------------------

Hosts can ask OpenMeta what the current build supports before wiring format
menus, warnings, or integration feature flags.

.. code-block:: cpp

   #include "openmeta/metadata_capabilities.h"

   openmeta::MetadataCapability cap = openmeta::metadata_capability(
       openmeta::TransferTargetFormat::Avif,
       openmeta::MetadataCapabilityFamily::Xmp);

   if (openmeta::metadata_capability_available(cap.target_edit)) {
       // The current build can edit AVIF XMP within the reported support level.
   }

Each operation reports one of ``unsupported``, ``supported``, ``bounded``, or
``disabled``. ``bounded`` means the capability exists within OpenMeta's
documented contract, not that it is arbitrary metadata-editor parity.
``disabled`` is used for compile-time-disabled support such as XMP decode when
XML support is not available.

Python exposes the same query:

.. code-block:: python

   cap = openmeta.metadata_capability(
       openmeta.TransferTargetFormat.Avif,
       openmeta.MetadataCapabilityFamily.Xmp,
   )
   print(cap["target_edit_name"])

Optional Adobe DNG SDK bridge
-----------------------------

If OpenMeta was built with ``OPENMETA_WITH_DNG_SDK_ADAPTER=ON``, you can use
the optional SDK bridge in two ways.

Update an existing DNG file
~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: cpp

   #include "openmeta/dng_sdk_adapter.h"

   openmeta::ApplyDngSdkMetadataFileResult result =
       openmeta::update_dng_sdk_file_from_file("source.jpg", "target.dng");

Apply onto existing SDK objects
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. code-block:: cpp

   #include "openmeta/dng_sdk_adapter.h"
   #include "openmeta/metadata_transfer.h"

   openmeta::PrepareTransferFileOptions prepare;
   prepare.prepare.target_format = openmeta::TransferTargetFormat::Dng;

   openmeta::PrepareTransferFileResult prepared =
       openmeta::prepare_metadata_for_target_file("source.jpg", prepare);

   openmeta::DngSdkAdapterOptions adapter;
   openmeta::apply_prepared_dng_sdk_metadata(
       prepared.bundle, host, negative, adapter);

This bridge is for applications that already use the Adobe DNG SDK. OpenMeta
still does not encode pixels or invent raw-image structure.

Related pages
-------------

- :doc:`quick_start`
- :doc:`interop_api`
- :doc:`development`
- :doc:`api`
