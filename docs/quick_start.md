# Quick Start

This guide is for the normal OpenMeta use case: a C++ application that needs
to read, query, prepare, and transfer image metadata.

OpenMeta is a metadata engine, not an image encoder. In practice that means:

- read metadata from an existing file
- query it through `MetaStore`
- build new metadata entries when needed
- inject metadata into an existing target file or template
- prepare metadata artifacts for a host API such as EXR or another
  host-owned metadata layer

If you need the full support matrix, see
[metadata_support.md](metadata_support.md). If you need the detailed target
contract, see [metadata_transfer_plan.md](metadata_transfer_plan.md).

If you already own the encoder or host API, see
[host_integration.md](host_integration.md).

## 1. Add OpenMeta To Your CMake Project

If OpenMeta is installed as a package:

```cmake
find_package(OpenMeta CONFIG REQUIRED)

add_executable(my_app main.cc)
target_link_libraries(my_app PRIVATE OpenMeta::openmeta)
```

If you build OpenMeta from source in the same workspace, any normal
`add_subdirectory(...)` or install-and-find-package workflow is fine. The
public target alias is `OpenMeta::openmeta`.

## 2. Build OpenMeta

Library and tools:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Tests:

```bash
cmake -S . -B build-tests -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DOPENMETA_BUILD_TESTS=ON
cmake --build build-tests
ctest --test-dir build-tests
```

If your optional dependencies live in a custom prefix, pass that prefix
through `CMAKE_PREFIX_PATH`.

Optional Adobe DNG SDK bridge:

```bash
cmake -S . -B build-dng -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DOPENMETA_WITH_DNG_SDK_ADAPTER=ON \
  -DOPENMETA_USE_LIBCXX=ON \
  -DCMAKE_PREFIX_PATH='<dng-sdk-prefix>;<deps-prefix>'
cmake --build build-dng
```

## 3. Read One File Into `MetaStore`

This is the basic C++ read path.

```cpp
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
const openmeta::SimpleMetaResult read = openmeta::simple_meta_read(
    std::span<const std::byte>(file_bytes.data(), file_bytes.size()),
    store,
    blocks,
    ifds,
    payload,
    payload_indices,
    options);

store.finalize();
```

Notes:

- `simple_meta_read(...)` appends decoded entries to `MetaStore`.
- Call `store.finalize()` before exact-key lookups.
- The caller owns the scratch buffers. That is intentional.

## 4. Query Metadata By Exact Key

OpenMeta does not currently ship a fuzzy-search layer. The main lookup API is
exact-key lookup through `MetaStore::find_all(...)`.

```cpp
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"

openmeta::MetaKeyView make_key;
make_key.kind = openmeta::MetaKeyKind::ExifTag;
make_key.data.exif_tag.ifd = "ifd0";
make_key.data.exif_tag.tag = 0x010F;  // Make

for (openmeta::EntryId id : store.find_all(make_key)) {
    const openmeta::Entry& entry = store.entry(id);
    // Inspect entry.value and entry.origin here.
}
```

Use exact keys for:

- EXIF tags
- XMP properties
- EXR attributes
- ICC tags
- IPTC datasets

If you need fuzzy or text-oriented search, build that on top of `MetaStore`
using your own index or display-name layer.

## 5. Build A `MetaStore` Manually

This is useful when your application creates or edits metadata directly.

```cpp
#include "openmeta/meta_key.h"
#include "openmeta/meta_store.h"
#include "openmeta/meta_value.h"

openmeta::MetaStore store;

const openmeta::BlockId block = store.add_block(openmeta::BlockInfo {});

openmeta::Entry make_entry;
make_entry.key = openmeta::make_exif_tag_key(store.arena(), "ifd0", 0x010F);
make_entry.value = openmeta::make_text(
    store.arena(), "Vendor", openmeta::TextEncoding::Ascii);
make_entry.origin.block = block;
make_entry.origin.order_in_block = 0;

store.add_entry(make_entry);
store.finalize();
```

The key/value helpers live in:

- [meta_key.h](../src/include/openmeta/meta_key.h)
- [meta_value.h](../src/include/openmeta/meta_value.h)

## 6. Export XMP From `MetaStore`

For portable sidecar output:

```cpp
#include "openmeta/xmp_dump.h"

std::vector<std::byte> xmp_bytes;
openmeta::XmpSidecarRequest request;
request.format = openmeta::XmpSidecarFormat::Portable;
request.include_exif = true;
request.include_iptc = true;

const openmeta::XmpDumpResult dump =
    openmeta::dump_xmp_sidecar(store, &xmp_bytes, request);
```

For lossless OpenMeta-native sidecars, switch `request.format` to
`XmpSidecarFormat::Lossless`.

## 7. Copy Metadata Into An Existing Target File

This is the common export workflow:

- one source file provides metadata
- one target file or template already owns the pixel container
- OpenMeta edits the target metadata

```cpp
#include "openmeta/metadata_transfer.h"

openmeta::ExecutePreparedTransferFileOptions options;
options.prepare.prepare.target_format = openmeta::TransferTargetFormat::Jpeg;
options.edit_target_path = "rendered.jpg";

const openmeta::ExecutePreparedTransferFileResult exec =
    openmeta::execute_prepared_transfer_file("source.jpg", options);

openmeta::PersistPreparedTransferFileOptions persist;
persist.output_path = "rendered_with_meta.jpg";
persist.overwrite_output = true;

const openmeta::PersistPreparedTransferFileResult saved =
    openmeta::persist_prepared_transfer_file_result(exec, persist);
```

Use the same pattern for other file-backed targets:

- `TransferTargetFormat::Tiff`
- `TransferTargetFormat::Dng`
- `TransferTargetFormat::Png`
- `TransferTargetFormat::Webp`
- `TransferTargetFormat::Jp2`
- `TransferTargetFormat::Jxl`
- bounded BMFF targets such as `Heif`, `Avif`, and `Cr3`

### Read Once And Reuse Later

If your application already decoded source metadata earlier, keep a decoded
source snapshot and execute the later save without reopening the source file:

```cpp
#include "openmeta/metadata_transfer.h"

const openmeta::ReadTransferSourceSnapshotFileResult snapshot =
    openmeta::read_transfer_source_snapshot_file("source.jpg");

openmeta::ExecutePreparedTransferSnapshotOptions options;
options.prepare.target_format = openmeta::TransferTargetFormat::Tiff;
options.edit_target_path      = "target.tif";
options.execute.edit_apply    = true;

const openmeta::ExecutePreparedTransferFileResult result =
    openmeta::execute_prepared_transfer_snapshot(
        snapshot.snapshot, options);
```

Python exposes the same reusable snapshot flow for host code:

```python
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
```

Current source snapshots are decoded-store-backed. They are meant for the
common EXIF/XMP/ICC/IPTC transfer workflow, not raw source-packet passthrough.
If the host owns the final file write path, the lower-level
`prepare_metadata_for_target_snapshot(...)` entry point remains available.
If the host already has a decoded `MetaStore`, build a reusable snapshot with
`build_transfer_source_snapshot(store)`. If it already owns the source bytes in
memory, use `read_transfer_source_snapshot_bytes(bytes)` instead of the
file-path reader.
In Python, if the host already has `doc = openmeta.read(...)`, call
`doc.build_transfer_source_snapshot()` or
`openmeta.build_transfer_source_snapshot(doc)` instead of reopening the file.
If it also owns the destination bytes in memory, call the overload
`execute_prepared_transfer_snapshot(snapshot, target_bytes, options)`.
If it already holds a prepared bundle, use
`execute_prepared_transfer_bundle(bundle, target_bytes, options)` instead.
Snapshot execution supports the same existing-sidecar merge and destination
carrier-precedence controls as the file helper; when loading an existing
sidecar it defaults to `edit_target_path` unless
`xmp_existing_sidecar_base_path` is set explicitly.
For embedded-only writeback with sidecar cleanup and no filesystem path, set
`xmp_existing_destination_sidecar_state` explicitly so OpenMeta can return a
cleanup decision without guessing a sidecar location.
The Python snapshot helpers intentionally cover the core transfer/edit/persist
path. The older `transfer_probe(...)` / `unsafe_transfer_probe(...)` entry
points remain the broader artifact-dump/debug surface.

## 8. Check Runtime Capabilities

Use the capability API before enabling format/family operations in a host UI
or integration layer.

```cpp
#include "openmeta/metadata_capabilities.h"

const openmeta::MetadataCapability cap = openmeta::metadata_capability(
    openmeta::TransferTargetFormat::Avif,
    openmeta::MetadataCapabilityFamily::Xmp);

if (openmeta::metadata_capability_available(cap.target_edit)) {
    // The current build can edit AVIF XMP within the reported support level.
}
```

Each operation reports `unsupported`, `supported`, `bounded`, or `disabled`.
`bounded` means the capability exists within OpenMeta's documented contract.
`disabled` is used for compile-time-disabled support such as XMP decode when
XML support is not available.

Python exposes the same query:

```python
cap = openmeta.metadata_capability(
    openmeta.TransferTargetFormat.Avif,
    openmeta.MetadataCapabilityFamily.Xmp,
)
print(cap["target_edit_name"])
```

## 9. Prepare Metadata For Host-Owned Encoders

Some applications do not want a file helper. They already own the encoder or
the output container and just want OpenMeta to prepare metadata bytes.

### EXR Host API

EXR is the cleanest host-adapter example.

```cpp
#include "openmeta/exr_adapter.h"

openmeta::ExrAdapterBatch batch;
openmeta::BuildExrAttributeBatchFileOptions options;

const openmeta::BuildExrAttributeBatchFileResult result =
    openmeta::build_exr_attribute_batch_from_file(
        "source.jpg", &batch, options);

for (const openmeta::ExrAdapterAttribute& attr : batch.attributes) {
    // Forward attr.name, attr.type_name, and attr.value to your EXR writer.
}
```

Use this when the host already writes EXR files through OpenEXR or its own EXR
code.

### Generic Host Metadata Export

If your application owns the metadata object model, use the generic traversal
API and build the mapping in the host layer.

```cpp
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
```

### JPEG / JXL / Other Encoder-Owned Outputs

For host-owned encoders, the transfer core exposes two patterns:

1. implement a backend emitter such as `JpegTransferEmitter` or
   `JxlTransferEmitter`
2. or build a target-neutral adapter view and consume explicit operations

Minimal JPEG/JXL-style pattern:

```cpp
#include "openmeta/metadata_transfer.h"

openmeta::PrepareTransferFileOptions prepare;
prepare.prepare.target_format = openmeta::TransferTargetFormat::Jxl;

openmeta::PrepareTransferFileResult prepared =
    openmeta::prepare_metadata_for_target_file("source.jpg", prepare);

openmeta::PreparedTransferExecutionPlan plan;
openmeta::compile_prepared_transfer_execution(
    prepared.bundle, openmeta::EmitTransferOptions {}, &plan);

// Then either:
// - implement openmeta::JxlTransferEmitter and call
//   emit_prepared_transfer_compiled(...)
// - or build_prepared_transfer_adapter_view(...) and feed the ops into your
//   own backend abstraction
```

OpenMeta does not currently ship a TurboJPEG-specific wrapper, but the JPEG
transfer path is designed for that kind of integration through
`JpegTransferEmitter` and the adapter-view APIs.

For fuller C++ host-side examples, see
[host_integration.md](host_integration.md).

## 10. Optional Adobe DNG SDK Bridge

If OpenMeta was built with `OPENMETA_WITH_DNG_SDK_ADAPTER=ON`, you can update
an existing DNG file through the Adobe SDK bridge:

```cpp
#include "openmeta/dng_sdk_adapter.h"

openmeta::ApplyDngSdkMetadataFileResult result =
    openmeta::update_dng_sdk_file_from_file("source.jpg", "target.dng");
```

This is for applications that already use the Adobe DNG SDK. It is not a raw
image encoder.

## 11. CLI And Python Are Convenience Layers

The CLI and Python bindings are useful, but they are thin layers over the same
public C++ APIs.

CLI:

```bash
./build/metaread file.jpg
./build/metatransfer --source-meta source.jpg --target-jpeg rendered.jpg --output rendered_with_meta.jpg --force
```

Python:

```bash
PYTHONPATH=build-py/python python3 - <<'PY'
import openmeta

doc = openmeta.read("file.jpg")
print(doc.entry_count)
PY
```

## 12. Pick The Next Doc

- Detailed read support:
  [metadata_support.md](metadata_support.md)
- Target-by-target transfer status:
  [metadata_transfer_plan.md](metadata_transfer_plan.md)
- Host-side encoder and SDK integration:
  [host_integration.md](host_integration.md)
- Build, test, and deeper API notes:
  [development.md](development.md)
