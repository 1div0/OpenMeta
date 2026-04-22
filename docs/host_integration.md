# Host Integration

This guide is for applications that already own the image/container side and
want OpenMeta to handle metadata.

OpenMeta is not an image encoder. The usual pattern is:

- decode metadata from one source file
- query or edit it in `MetaStore`
- hand prepared metadata to your own writer, encoder, or SDK

If you want the shortest end-to-end examples first, start with
[quick_start.md](quick_start.md).

## Pick The Integration Path

Use the narrowest public API that matches your host:

| Host owns | Use |
| --- | --- |
| Existing target file or template | `execute_prepared_transfer_file(...)` + `persist_prepared_transfer_file_result(...)` |
| EXR writer | `build_exr_attribute_batch_from_file(...)` |
| Host-owned metadata object model | `visit_metadata(...)` |
| JPEG/JXL/WebP/PNG/JP2/BMFF encoder path | `prepare_metadata_for_target_file(...)` + adapter view or backend emitter |
| Adobe DNG SDK objects/files | `dng_sdk_adapter.h` |

There is no public fuzzy-search API yet. Query through exact keys and build
your own display or search layer on top.

## Adapter Classes

OpenMeta splits host integration surfaces deliberately:

- export-only naming/traversal surface:
  `visit_metadata(...)` for host-owned metadata mapping layers
- export-only adapter:
  `build_ocio_metadata_tree(...)` for OCIO-style metadata trees
- host-apply adapter:
  `build_exr_attribute_batch(...)` for EXR/OpenEXR header workflows
- direct bridge:
  `dng_sdk_adapter.h` for applications that already use Adobe DNG SDK objects
- narrow translator:
  `libraw_adapter.h` for orientation mapping into LibRaw flip space

## 1. Read Into `MetaStore`

```cpp
#include "openmeta/simple_meta.h"

#include <array>
#include <cstddef>
#include <span>
#include <vector>

std::vector<std::byte> file_bytes = load_file_somehow("input.jpg");

openmeta::MetaStore store;
std::array<openmeta::ContainerBlockRef, 256> blocks {};
std::array<openmeta::ExifIfdRef, 512> ifds {};
std::vector<std::byte> payload(1 << 20);
std::array<uint32_t, 1024> payload_indices {};

openmeta::SimpleMetaDecodeOptions options;
openmeta::SimpleMetaResult result = openmeta::simple_meta_read(
    std::span<const std::byte>(file_bytes.data(), file_bytes.size()),
    store,
    blocks,
    ifds,
    payload,
    payload_indices,
    options);

store.finalize();
```

The caller owns the scratch buffers. That is deliberate: the API stays
deterministic and easy to reuse in hot paths.

## 2. Query By Exact Key

```cpp
#include "openmeta/meta_key.h"

openmeta::MetaKeyView key;
key.kind = openmeta::MetaKeyKind::ExifTag;
key.data.exif_tag.ifd = "ifd0";
key.data.exif_tag.tag = 0x010F;  // Make

for (openmeta::EntryId id : store.find_all(key)) {
    const openmeta::Entry& entry = store.entry(id);
    // Inspect entry.value and entry.origin.
}
```

This is the public lookup model today. If you need fuzzy search, substring
search, or ExifTool-style display lookup, add that in your application layer.

## 3. Generic Host Metadata Traversal

Use the traversal API when your application owns the metadata object model and
needs deterministic exported names plus the original `Entry`.

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

This keeps host-specific object ownership and write behavior outside OpenMeta.

## 4. Build An EXR Attribute Batch

This is the cleanest host-adapter path in OpenMeta today.

```cpp
#include "openmeta/exr_adapter.h"

openmeta::ExrAdapterBatch batch;
openmeta::BuildExrAttributeBatchFileOptions options;

openmeta::BuildExrAttributeBatchFileResult result =
    openmeta::build_exr_attribute_batch_from_file(
        "source.jpg", &batch, options);

for (const openmeta::ExrAdapterAttribute& attr : batch.attributes) {
    // Forward attr.name, attr.type_name, and attr.value to your EXR writer.
}
```

OpenMeta does not need OpenEXR headers for this path. It exports a neutral
batch of EXR-style attributes that your host can apply through OpenEXR or its
own EXR writer.

## 5. Feed A Host-Owned JPEG Or JXL Encoder

There are two public patterns for encoder-owned output:

- implement a backend emitter such as `JpegTransferEmitter` or
  `JxlTransferEmitter`
- build an adapter view and consume one normalized list of operations

### Adapter-View Pattern

Use this when you want one target-neutral operation list.

```cpp
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
```

This is a good fit when your host already has its own abstraction for
"metadata op + bytes".

### Backend-Emitter Pattern

Use this when your host already looks like one OpenMeta backend.

```cpp
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
```

For JXL, implement `JxlTransferEmitter::set_icc_profile(...)`,
`add_box(...)`, and `close_boxes(...)`.

OpenMeta does not ship a TurboJPEG-specific wrapper yet. The intended
integration path is still through `JpegTransferEmitter` or the adapter view.

## 6. Edit An Existing Target File

If your host already has a target file or template on disk, use the file
helper instead of building your own writer path.

```cpp
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
```

This path is usually simpler than a custom adapter when the container already
exists.

## 7. Use The Optional Adobe DNG SDK Bridge

If OpenMeta was built with `OPENMETA_WITH_DNG_SDK_ADAPTER=ON`, you can use the
optional SDK bridge in two ways.

### Update An Existing DNG File

```cpp
#include "openmeta/dng_sdk_adapter.h"

openmeta::ApplyDngSdkMetadataFileResult result =
    openmeta::update_dng_sdk_file_from_file("source.jpg", "target.dng");
```

### Apply Onto Existing SDK Objects

```cpp
#include "openmeta/dng_sdk_adapter.h"
#include "openmeta/metadata_transfer.h"

openmeta::PrepareTransferFileOptions prepare;
prepare.prepare.target_format = openmeta::TransferTargetFormat::Dng;

openmeta::PrepareTransferFileResult prepared =
    openmeta::prepare_metadata_for_target_file("source.jpg", prepare);

openmeta::DngSdkAdapterOptions adapter;
openmeta::apply_prepared_dng_sdk_metadata(
    prepared.bundle, host, negative, adapter);
```

This bridge is for applications that already use the Adobe DNG SDK. OpenMeta
still does not encode pixels or invent raw-image structure.

## 8. Build `MetaStore` Yourself

If your application creates metadata directly, build the store first and then
reuse the same export and transfer APIs.

```cpp
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
```

## Related Docs

- [quick_start.md](quick_start.md)
- [metadata_support.md](metadata_support.md)
- [metadata_transfer_plan.md](metadata_transfer_plan.md)
- [development.md](development.md)
