Quick Start
===========

This guide is for the usual OpenMeta use case: a C++ application that needs
to read, query, prepare, and transfer image metadata.

OpenMeta is a metadata engine, not an image encoder. The normal pattern is:

- read metadata from an existing file
- query it through ``MetaStore``
- build or patch entries when needed
- inject metadata into an existing target file or template
- prepare metadata artifacts for a host API such as EXR or another
  host-owned metadata layer

If you already own the encoder or output container, continue with
:doc:`host_integration`.

Add OpenMeta to CMake
---------------------

If OpenMeta is installed as a package:

.. code-block:: cmake

   find_package(OpenMeta CONFIG REQUIRED)

   add_executable(my_app main.cc)
   target_link_libraries(my_app PRIVATE OpenMeta::openmeta)

If you build OpenMeta from source in the same workspace, any normal
``add_subdirectory(...)`` or install-and-find-package workflow is fine. The
public target alias is ``OpenMeta::openmeta``.

Build
-----

Library and tools:

.. code-block:: bash

   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build

Tests:

.. code-block:: bash

   cmake -S . -B build-tests -G Ninja \
     -DCMAKE_BUILD_TYPE=Release \
     -DOPENMETA_BUILD_TESTS=ON
   cmake --build build-tests
   ctest --test-dir build-tests

If your optional dependencies live in a custom prefix, pass that prefix
through ``CMAKE_PREFIX_PATH``.

Read one file into ``MetaStore``
--------------------------------

.. code-block:: cpp

   #include "openmeta/simple_meta.h"

   #include <array>
   #include <cstddef>
   #include <span>
   #include <vector>

   std::vector<std::byte> file_bytes = load_file_somehow("file.jpg");

   openmeta::MetaStore store;
   std::array<openmeta::ContainerBlockRef, 256> blocks {};
   std::array<openmeta::ExifIfdRef, 512> ifds {};
   std::vector<std::byte> payload(1 << 20);
   std::array<uint32_t, 1024> payload_indices {};

   openmeta::SimpleMetaDecodeOptions options;
   openmeta::SimpleMetaResult read = openmeta::simple_meta_read(
       std::span<const std::byte>(file_bytes.data(), file_bytes.size()),
       store,
       blocks,
       ifds,
       payload,
       payload_indices,
       options);

   store.finalize();

Notes:

- ``simple_meta_read(...)`` appends decoded entries to ``MetaStore``.
- Call ``store.finalize()`` before exact-key lookups.
- The caller owns the scratch buffers.

Query metadata by exact key
---------------------------

OpenMeta does not currently ship a fuzzy-search layer. The main lookup API is
exact-key lookup through ``MetaStore::find_all(...)``.

.. code-block:: cpp

   #include "openmeta/meta_key.h"

   openmeta::MetaKeyView key;
   key.kind = openmeta::MetaKeyKind::ExifTag;
   key.data.exif_tag.ifd = "ifd0";
   key.data.exif_tag.tag = 0x010F;  // Make

   for (openmeta::EntryId id : store.find_all(key)) {
       const openmeta::Entry& entry = store.entry(id);
       // Inspect entry.value and entry.origin here.
   }

Build a ``MetaStore`` manually
------------------------------

.. code-block:: cpp

   #include "openmeta/meta_key.h"
   #include "openmeta/meta_store.h"
   #include "openmeta/meta_value.h"

   openmeta::MetaStore store;

   const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});

   openmeta::Entry entry;
   entry.key = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x010F);
   entry.value = openmeta::make_text(
       store.arena(), "Vendor", openmeta::TextEncoding::Ascii);
   entry.origin.block = block;
   entry.origin.order_in_block = 0;

   store.add_entry(entry);
   store.finalize();

Export XMP
----------

.. code-block:: cpp

   #include "openmeta/xmp_dump.h"

   std::vector<std::byte> xmp_bytes;
   openmeta::XmpSidecarRequest request;
   request.format = openmeta::XmpSidecarFormat::Portable;
   request.include_exif = true;
   request.include_iptc = true;

   openmeta::XmpDumpResult dump =
       openmeta::dump_xmp_sidecar(store, &xmp_bytes, request);

Copy metadata into an existing target
-------------------------------------

.. code-block:: cpp

   #include "openmeta/metadata_transfer.h"

   openmeta::ExecutePreparedTransferFileOptions options;
   options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
   options.edit_target_path = "rendered.jpg";

   openmeta::ExecutePreparedTransferFileResult exec =
       openmeta::execute_prepared_transfer_file("source.jpg", options);

   openmeta::PersistPreparedTransferFileOptions persist;
   persist.output_path = "rendered_with_meta.jpg";
   persist.overwrite_output = true;

   openmeta::PersistPreparedTransferFileResult saved =
       openmeta::persist_prepared_transfer_file_result(exec, persist);

Use the same pattern for ``Tiff``, ``Dng``, ``Png``, ``Webp``, ``Jp2``,
``Jxl``, and bounded BMFF targets such as ``Heif``, ``Avif``, and ``Cr3``.

Read once and reuse later
~~~~~~~~~~~~~~~~~~~~~~~~~

If your application already decoded source metadata earlier, keep a decoded
source snapshot and execute the later save without reopening the source file.

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

Python exposes the same reusable snapshot flow for host code:

.. code-block:: python

   from pathlib import Path

   import openmeta

   snapshot_info = openmeta.read_transfer_source_snapshot_file("source.jpg")
   snapshot = snapshot_info["snapshot"]

   result = openmeta.transfer_snapshot_probe(
       snapshot,
       target_format=openmeta.TransferTargetFormat.Tiff,
       edit_target_path="target.tif",
       target_bytes=Path("target.tif").read_bytes(),
   )

Current source snapshots are decoded-store-backed. They are intended for the
common EXIF/XMP/ICC/IPTC transfer workflow, not raw source-packet passthrough.
If the host still owns the bundle/execution split, the lower-level
``prepare_metadata_for_target_snapshot(...)`` entry point remains available.
If the host already has a decoded ``MetaStore``, build a reusable snapshot with
``build_transfer_source_snapshot(store)``. If it already owns the source bytes
in memory, use ``read_transfer_source_snapshot_bytes(bytes)`` instead of the
file-path reader.
In Python, if the host already has ``doc = openmeta.read(...)``, call
``doc.build_transfer_source_snapshot()`` or
``openmeta.build_transfer_source_snapshot(doc)`` instead of reopening the
file.
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
The Python snapshot helpers intentionally cover the core transfer/edit/persist
path. The older ``transfer_probe(...)`` / ``unsafe_transfer_probe(...)``
entry points remain the broader artifact-dump/debug surface.

Next steps
----------

- :doc:`host_integration`
- :doc:`interop_api`
- :doc:`development`
- :doc:`api`
