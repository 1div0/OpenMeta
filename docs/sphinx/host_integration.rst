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
OIIO-style typed attributes     ``collect_oiio_attributes_typed(...)``
JPEG/JXL/WebP/PNG/JP2/BMFF      ``prepare_metadata_for_target_file(...)`` +
encoder path                    adapter view or backend emitter
Adobe DNG SDK objects/files     ``dng_sdk_adapter.h``
=============================== ===========================================================

There is no public fuzzy-search API yet. Query through exact keys and build
your own display or search layer on top.

Read and query
--------------

The basic read path is covered in :doc:`quick_start`. Once you have a
``MetaStore``, the main lookup API is exact-key lookup through
``MetaStore::find_all(...)``.

OIIO-style typed attributes
---------------------------

Use this when the host wants flattened typed attributes instead of raw
container payloads.

.. code-block:: cpp

   #include "openmeta/oiio_adapter.h"

   std::vector<openmeta::OiioTypedAttribute> attrs;
   openmeta::OiioAdapterRequest request;
   request.include_makernotes = true;

   openmeta::collect_oiio_attributes_typed(store, &attrs, request);

This gives you ``name + typed value`` pairs that are easy to map onto an
OpenImageIO ``ImageSpec`` or a similar metadata API.

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
batch of EXR-style attributes that your host can apply through OpenEXR, OIIO,
or its own EXR writer.

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
