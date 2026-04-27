Writer Target Contract
======================

This page defines the bounded preserve/replace contract for OpenMeta transfer
writers.

It is scoped to metadata transfer and metadata-only target edits. OpenMeta
does not claim full arbitrary metadata-editor parity, pixel transcoding, or
byte-for-byte preservation of rewritten metadata structures.

For generated XMP merge, precedence, sidecar output, and sidecar cleanup
rules, see :doc:`xmp_sync_policy`.

Common Rules
------------

Managed metadata families are replaced only when OpenMeta has a prepared block
for that family or an explicit strip policy targets that family.

Unmanaged data is preserved as source ranges where the target edit path can
parse the container safely. If parsing finds a structure outside the bounded
contract, the edit fails with an unsupported or malformed result instead of
guessing.

Managed XMP writeback has three modes:

- ``EmbeddedOnly``: generated XMP remains in the managed embedded carrier.
- ``SidecarOnly``: generated embedded XMP blocks are suppressed and returned
  as sidecar output.
- ``EmbeddedAndSidecar``: generated XMP is written to both the embedded
  carrier and sidecar output.

Destination embedded-XMP stripping is supported only for sidecar-only
writeback on JPEG, TIFF/DNG, PNG, WebP, JP2, JXL, and bounded BMFF targets.
Destination sidecar cleanup is supported only for embedded writeback.

Image-dependent metadata is target-owned. When source and target images may
differ in dimensions, channel count, sample type, compression, orientation,
colorspace, or strip/tile storage layout, host code must preserve or provide
target-correct image buffer specs from the actual output image. Use
``PrepareTransferRequest::target_image_spec`` when the host knows target
dimensions, orientation, samples-per-pixel, bit depth, sample format,
photometric interpretation, planar configuration, compression, or EXIF color
space. OpenMeta filters source EXIF/XMP image-layout fields during prepared
transfer rather than copying stale source width/height, channel/layout,
color-space aliases, or source-local storage offsets into another file. ICC
transfer should be enabled only when the host knows that the source profile is
valid for the target pixel buffer; otherwise the host writer should keep or
inject the target profile. The Python binding exposes the same structure as
``openmeta.TransferTargetImageSpec``; the C++ and Python ``metatransfer``
command wrappers expose matching ``--target-*`` flags for smoke tests and
file-helper integration checks.

Target Summary
--------------

.. list-table::
   :header-rows: 1
   :widths: 14 28 38 20

   * - Target
     - Managed embedded carriers
     - Preserve/replace rule
     - Main limits
   * - ``JPEG``
     - APP1 EXIF, APP1 XMP, APP2 ICC, APP13 IPTC/IRB, bounded APP11
       JUMBF/C2PA
     - Replace matching recognized leading metadata segments; preserve
       unknown leading segments and image scan data.
     - Not a general JPEG marker editor.
   * - ``TIFF``
     - root TIFF tags, EXIF/GPS/Interop IFDs, bounded page/SubIFD chains, XMP
       tag 700, IPTC 33723, ICC 34675
     - Rewrite bounded IFD structures and append new metadata tail data;
       preserve unrelated root tags and bounded downstream tails.
     - Not arbitrary nested-IFD graph rewrite.
   * - ``DNG``
     - same as TIFF plus DNG target-mode policy and minimal ``DNGVersion``
       synthesis
     - Uses the TIFF-family rewrite contract; preserves DNG core target tags
       when merging non-DNG source metadata into an existing DNG target.
     - Not a full DNG-specific rewrite engine.
   * - ``PNG``
     - ``eXIf``, XMP ``iTXt``, ``iCCP``
     - Insert prepared chunks after ``IHDR``; replace matching managed chunks;
       preserve unrelated chunks.
     - Requires valid PNG with terminal ``IEND``.
   * - ``WebP``
     - ``EXIF``, XMP RIFF chunk, ``ICCP``, bounded ``C2PA``
     - Replace matching managed RIFF chunks; preserve unrelated chunks; patch
       ``VP8X`` feature bits.
     - EXIF/XMP/ICC edits require an existing ``VP8X`` chunk.
   * - ``JP2``
     - top-level ``Exif``, top-level XMP ``xml`` box, ``jp2h/colr`` ICC
     - Replace matching top-level metadata boxes; rewrite ``jp2h`` only to
       replace/insert ``colr``; preserve unrelated boxes and unrelated
       ``jp2h`` children.
     - Does not synthesize ``jp2h``; requires one existing ``jp2h`` for ICC.
   * - ``JXL``
     - top-level ``Exif``, XMP ``xml`` box, ``jumb``, ``c2pa``
     - Replace matching top-level boxes; preserve signature and non-managed
       boxes; classify ``jumb`` as generic JUMBF or C2PA.
     - ICC is encoder handoff only; file edit emits uncompressed prepared
       metadata boxes.
   * - ``HEIF`` / ``AVIF`` / ``CR3``
     - BMFF metadata items (``Exif``, XMP ``mime``, JUMBF/C2PA), bounded
       ``colr/prof`` ICC properties, plus OpenMeta-authored metadata-only
       ``meta`` boxes
     - Preserve non-``meta`` top-level boxes; replace prior
       OpenMeta-authored metadata ``meta``; merge/replace/strip item metadata
       in parseable foreign top-level ``meta`` graphs by extending
       ``iinf``/``iloc``/``idat``/``iref``; replace prior ICC ``colr``
       properties and remap ``iprp``/``ipco``/``ipma`` for bounded ICC.
     - Does not rewrite arbitrary BMFF scene/property graphs; 32-bit item-id
       insertion requires existing ``iloc`` v2.
   * - ``EXR``
     - safe string header attributes through the EXR transfer emitter or
       adapter batch
     - No file rewrite contract today; host applies prepared attributes
       through its own EXR writer.
     - Attribute-emitter target, not a file edit path.

JPEG
----

The JPEG edit path scans leading metadata marker segments before image scan
data.

OpenMeta replaces a leading segment when:

- the segment route matches a prepared block route
- sidecar-only writeback requests embedded XMP stripping and the segment is
  APP1 XMP
- JUMBF or C2PA policy resolves to removal for matching APP11 carriers

Unknown APP/COM segments and all scan data after the leading metadata region
are preserved. If same-size replacements are available, OpenMeta can use the
in-place edit plan; otherwise it performs a metadata rewrite while preserving
the image data range.

TIFF And DNG
------------

TIFF-family editing supports classic TIFF and BigTIFF.

Managed updates include:

- root IFD metadata tags such as XMP tag 700, IPTC tag 33723, and ICC tag
  34675
- EXIF payload materialized as bounded TIFF-family IFD structures
- EXIF/GPS/Interop pointer regeneration
- bounded preview-page chain replacement with downstream tail preservation
- bounded ``SubIFD`` replacement with downstream auxiliary-tail preservation
- preserving an existing target ``InteropIFD`` when a replaced ``ExifIFD``
  omits its own interop child

The active root image IFD remains target-owned for pixel layout and local byte
storage. Source EXIF values for root image width/height, sample layout,
compression, orientation, strip/tile offsets, strip/tile byte counts, and
thumbnail/JPEG interchange offsets are not allowed to replace the target's
active image structure during metadata transfer. For replaced preview-page and
``SubIFD`` structures, OpenMeta also strips source-local strip/tile/JPEG
storage offsets and preserves the corresponding target-local storage fields
when a matching target child exists.

The same target-owned rule is applied before packaging EXIF/XMP metadata for
JPEG, PNG, WebP, JP2, JXL, BMFF-family targets, and EXR string attributes.
Source descriptive tags such as make/model/date/GPS/copyright continue to
transfer; source image-buffer facts do not become target image facts unless a
host supplies target-correct values through ``target_image_spec`` or another
writer-specific path.

Unrelated root IFD tags are preserved. Rewritten structures may be repacked
into new offsets appended to the file; offset identity is not part of the
contract.

DNG uses the same TIFF-family rewrite contract. ``ExistingTarget`` and
``TemplateTarget`` require a backing target container. ``MinimalFreshScaffold``
can emit a minimal metadata scaffold without an existing DNG target. When a
non-DNG source is merged into an existing DNG target, OpenMeta preserves
existing target DNG core tags within the bounded DNG policy layer.

PNG
---

The PNG edit path requires a valid PNG signature, an ``IHDR`` chunk, and a
terminal ``IEND`` chunk.

Prepared ``eXIf``, XMP ``iTXt``, and ``iCCP`` chunks are inserted immediately
after ``IHDR``. Existing chunks from those managed families are removed when a
replacement or strip policy targets that family. XMP matching is limited to
``iTXt`` chunks using the ``XML:com.adobe.xmp`` keyword.

All unrelated chunks are preserved. Bytes following the terminal ``IEND`` are
preserved as part of the kept terminal range.

WebP
----

The WebP edit path requires a valid RIFF/WebP stream whose RIFF size consumes
the input bytes.

Prepared ``EXIF``, XMP, ``ICCP``, and bounded ``C2PA`` chunks replace
existing chunks from the same managed family. Unrelated chunks are preserved.
When EXIF, XMP, or ICC is present after rewrite, OpenMeta patches the existing
``VP8X`` feature flags to match the final metadata state.

Prepared WebP ``EXIF`` chunks carry the TIFF byte stream directly. They do
not include the JPEG APP1 ``Exif\0\0`` preamble.

The current WebP edit contract requires an existing ``VP8X`` chunk for EXIF,
XMP, or ICC metadata edits. It does not synthesize ``VP8X``.

JP2
---

The JP2 edit path requires a valid JP2 signature box and file-type box.

Prepared top-level ``Exif`` and XMP ``xml`` boxes replace existing top-level
boxes from the same managed family. Unrelated top-level boxes are preserved.

ICC update uses the bounded ``jp2h/colr`` route. OpenMeta rewrites the
existing ``jp2h`` box to replace existing ``colr`` children with the prepared
ICC ``colr`` payload, while preserving unrelated ``jp2h`` children. If no
``colr`` child exists, the prepared ``colr`` child is inserted. The contract
requires one existing ``jp2h`` box and does not synthesize a new ``jp2h`` box.

JXL
---

The JXL edit path requires a valid JXL container.

Prepared top-level ``Exif``, XMP ``xml``, ``jumb``, and ``c2pa`` boxes replace
existing boxes from the same managed family. Unrelated top-level boxes are
preserved, including the JXL signature box. Generic JUMBF and C2PA are
distinguished so a generic ``jumb`` replacement does not accidentally remove a
C2PA carrier, and the C2PA path does not remove unrelated generic JUMBF.

When Brotli support is available, the same family classification can inspect
compressed ``brob`` metadata boxes by real type. The file edit path emits
prepared metadata boxes directly; the ``jxl:icc-profile`` route remains an
encoder-side handoff and is not serialized by the file edit path.

HEIF / AVIF / CR3
-----------------

The bounded BMFF edit path preserves non-``meta`` top-level boxes as source
ranges.

When the target has no foreign top-level ``meta`` box, OpenMeta writes one
metadata-only top-level ``meta`` box using the public bounded contract. That
box can contain prepared Exif, XMP, JUMBF, C2PA, and ICC ``colr`` property
payloads. A prior OpenMeta-authored metadata ``meta`` box is removed and
replaced by the newly prepared box.

When the target already has a foreign top-level ``meta`` box, OpenMeta does
not append a second competing ``meta`` graph. For parseable HEIF/AVIF-style
item graphs it merges, replaces, or strips bounded Exif/XMP/JUMBF/C2PA
metadata items in the existing ``meta`` by extending ``iinf``, ``iloc``,
``idat``, and ``iref`` with ``cdsc`` references to the primary item. This
constrained merge requires a single parseable ``iinf``, ``iloc`` version
0/1/2, ``pitm``, and at most one ``idat``. For ``iloc`` version 0/1 targets,
inserted item IDs remain in the 16-bit item-id space. For ``iloc`` version 2
targets, OpenMeta can allocate 32-bit item IDs and uses ``infe`` version 3 plus
``iref`` version 1 when a newly inserted item needs that wider ID space. If the
existing graph has exhausted the usable item-id space or mixes item-table widths
outside this shape, the edit fails instead of truncating IDs.

For bounded ICC transfer, OpenMeta removes prior ICC ``colr/prof`` and
``colr/rICC`` properties from ``iprp/ipco``, compacts/remaps existing ``ipma``
associations, appends the transferred ``colr/prof`` property, and associates it
with the primary item. Arbitrary non-ICC property replacement and broader BMFF
scene/property graph rewriting remain out of scope.

If sidecar-only writeback asks to strip embedded XMP, OpenMeta can remove XMP
from its own OpenMeta-authored metadata ``meta`` box and from parseable
foreign top-level ``meta`` item graphs that satisfy the same bounded
primary-item contract. Foreign graphs without ``pitm``, with unsupported
``iloc``, with multiple competing item tables, or otherwise outside that
bounded shape are reported as unsupported instead of being guessed.

EXR
---

EXR is a transfer target and host-emitter path, not a file rewrite path.

OpenMeta prepares safe flattened string attributes for transfer and exposes
the EXR attribute batch helpers for host-owned EXR writers. Existing EXR file
metadata preservation is therefore owned by the host writer, not by an
OpenMeta file edit helper.

Non-Goals
---------

The writer contract does not promise:

- arbitrary metadata editing for every carrier a format can contain
- preserving byte offsets of rewritten TIFF-family structures
- rewriting arbitrary existing BMFF item/property graphs
- synthesizing missing JP2 ``jp2h`` or WebP ``VP8X`` structures
- file-level EXR metadata rewrite
- signed C2PA rewrite or trust-policy parity beyond the bounded staged
  handoff paths
