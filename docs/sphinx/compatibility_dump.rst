Compatibility Dump Contract
===========================

OpenMeta exposes a deterministic text dump for downstream compatibility tests.
The goal is to let host projects compare names, value shapes, origins, and
transfer decisions without keeping binary metadata packet baselines.

Contract constant:

.. code-block:: cpp

   openmeta::kCompatibilityDumpContractVersion == 1

Python exposes the same version as:

.. code-block:: python

   openmeta.COMPATIBILITY_DUMP_CONTRACT_VERSION == 1

Metadata dump
-------------

Use ``dump_metadata_compatibility(...)`` for C++:

.. code-block:: cpp

   #include "openmeta/compatibility_dump.h"

   openmeta::MetadataCompatibilityDumpOptions options;
   options.style = openmeta::ExportNameStyle::FlatHost;

   std::string out;
   openmeta::dump_metadata_compatibility(store, options, &out);

Python ``Document`` and ``TransferSourceSnapshot`` expose the same metadata
dump:

.. code-block:: python

   doc = openmeta.read("source.jpg")
   text = doc.compatibility_dump()

The v1 metadata dump is line-oriented ASCII text:

.. code-block:: text

   openmeta.compat.metadata version=1 ...
   entry index=0 name="Make" key_kind="exif_tag" value_kind="text" ...

Each emitted entry is based on ``visit_metadata(...)``, so ordering, duplicate
handling, and naming follow the selected ``ExportNameStyle``. By default the
dump uses the stable ``FlatHost`` contract.

Metadata entry lines include:

- exported name
- key family
- value kind
- scalar or array element type
- text encoding
- count
- optional safe value text
- optional origin block/order/wire fields
- optional per-entry flags

Text and byte values are escaped with the same terminal-safe ASCII rules used
by other OpenMeta diagnostic output. Large values are bounded by
``max_value_bytes``.

Transfer dump
-------------

Use ``dump_transfer_compatibility(...)`` for C++ transfer/writeback decisions:

.. code-block:: cpp

   std::string out;
   openmeta::TransferCompatibilityDumpOptions options;
   openmeta::dump_transfer_compatibility(
       result, persisted_or_null, options, &out);

The v1 transfer dump is line-oriented ASCII text:

.. code-block:: text

   openmeta.compat.transfer version=1 target_format="png" ...
   policy index=0 subject="xmp_iptc_projection" ...
   block index=0 kind="xmp" route="png:itxt-xmp" ...
   execute edit_requested=true ...
   writeback xmp_sidecar_requested=true ...
   persist status="ok" ...

Transfer dump lines include:

- target format and high-level read/prepare status
- prepared policy decisions
- prepared block kind, route, order, and payload size
- execute/edit status and output sizes
- XMP sidecar request, output, and cleanup decisions
- optional persisted file/sidecar/cleanup result

The dump intentionally records decisions and sizes, not full binary payloads.
This keeps downstream tests stable across equivalent packet serialization
changes.

Stability
---------

This is a stable v1 contract. Future releases may add fields or lines, but
existing v1 field names and meanings should not change without a new contract
version or a documented compatibility path.
