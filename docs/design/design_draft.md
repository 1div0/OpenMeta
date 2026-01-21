# Design Draft (Storage-Agnostic Data Blocks)

This is a concrete draft for OpenMeta’s core metadata storage. It aligns with `SKILL.md` constraints (no exceptions/RTTI, deterministic, preallocation, explicit phases).

## 1. Architecture Overview

OpenMeta is split into:
- **Container adapters**: parse/emit container blocks (JPEG APP segments, TIFF IFD chains, PNG chunks, BMFF/JXL boxes…).
- **Codecs**: decode/encode payloads inside blocks (EXIF/TIFF, XMP packet, IPTC-IIM, ICC, Photoshop IRB, JUMBF…).
- **MetaStore** (this doc): storage-agnostic in-memory representation of keys/values + provenance + indices.

The registry (`registry/`) provides clean-room catalogs of keys and cross-family mapping rules.
Raw extracts and merged mapping inputs are generated in the private `OpenMeta-internal` repo.

## 2. Core Concepts

### 2.1 MetaKey
A normalized identifier for one “logical tag/property/dataset/key”, independent of where it was stored.

Key families (initial):
- `exif.tag` (includes MPF + MakerNotes: `ifd + tag`)
- `iptc.dataset` (`record + dataset`)
- `xmp.property` (`schema_ns + property_path`)
- `icc.header_field`, `icc.tag`, `photoshop.irb`, `geotiff.key`, `printim.field`, `jumbf.field`, `jumbf.cbor_key`

Keys must be comparable and sortable deterministically (no hash-iteration dependence).

### 2.2 Entry (supports duplicates)
An entry is a single occurrence of a key/value in the store. Multiple entries may share the same key.

Each `Entry` carries:
- `key`
- `value`
- `flags` (`Deleted`, `Dirty`, `Derived`, …)
- `origin` (always present; see below)
- optional `derivation` (present when generated/synced)

### 2.3 Origin (always stored)
`Origin` records “where did this come from” and enables lossless round-trip and edit-mode behavior.

Recommended fields (draft):
- `block_id`: which container block/IFD the value came from
- `order_in_block`: stable order index within that block (preserves wire order)
- `wire_type`: the original type token/code (e.g. TIFF type 2 ASCII, 5 RATIONAL, EXIF 129 UTF‑8)
- `wire_count`: original component count
- optional `wire_byte_range`: offset/size within the decoded block payload (if known)

### 2.4 Derivation (optional)
Only for values created/modified by OpenMeta transforms:
- `rule_id`: mapping rule identity (from Exiv2 mapping or MWG logic)
- `derived_from`: source key(s) or entry id(s) used to compute it

Derivation should not be used for identity/lookup; it’s metadata for sync workflows and debugging.

## 3. Value Representation

### 3.1 Requirements
- Support TIFF/EXIF numeric types, arrays, rationals, floats, raw bytes, and text.
- Preserve wire-level type/count to enable in-place writes and faithful rewrites.
- Avoid per-entry heap allocations in hot paths; use arenas + small inline storage.

### 3.2 ValueType (draft)
Include at least:
- Scalars: `u8/i8/u16/i16/u32/i32/u64/i64/f32/f64`
- Rationals: `urational{u32 n,u32 d}`, `srational{i32 n,i32 d}`
- Blobs: `bytes`
- Text: `text` with `TextEncoding` (`ascii`, `utf8`, `utf16le`, `unknown`)

Notes:
- “real (two uint32)” maps to TIFF/EXIF `RATIONAL` (numerator/denominator).
- EXIF DC-008 (2023) introduces **UTF‑8 type 129** (NUL-terminated; count includes NUL). Store both decoded encoding and the preferred wire type.

### 3.3 Storage
Two backing stores (both preallocated during build/commit phases):
- `ByteArena`: contiguous bytes for blobs/text/arrays.
- `StringTable` (optional): interns schema URIs and XMP property paths to stable ids to avoid repeated storage.

`ValueRef` is a small POD:
- inline union for small scalars
- `{offset,size}` into `ByteArena` for arrays/blobs/text

## 4. Indices & Views (Deterministic)

### 4.1 Block view (wire order)
For each parsed container block, keep a `span<EntryId>` in original order (duplicates allowed).
This drives “Edit mode” (preserve order, minimal diffs).

### 4.2 Key index (duplicates)
Build a read-only index at `finalize()`:
- representation: sorted vector of `(MetaKey, span<EntryId>)`
- lookup: binary search by key → span of all matching entries

This avoids unordered-map non-determinism and supports duplicates naturally.

### 4.3 Canonical views (optional)
Additional sorted views may be built on demand:
- by tag id within an IFD
- by key family (EXIF/IPTC/XMP)

These never mutate entries; they only reorder `EntryId` lists.

## 5. Concurrency Model

### 5.1 Phases
1. **Build/Decode**: container adapters + codecs populate entries and arenas; resizing allowed.
2. **Freeze**: indices built; store becomes immutable.
3. **Parallel Edit**: no structural mutations to base store.
4. **Commit**: apply deltas deterministically, optionally `compact()`, then freeze the new store.

### 5.2 Parallel edits via thread-local overlays (recommended default)
Each thread uses `MetaEdit`:
- reads see `base + local_delta`
- writes go to local delta only
- new entries are visible only to that thread until commit

Benefits:
- no shared writes → no locks/atomics required
- deterministic commit order can be enforced

## 6. Deletion, Rehash, Compact

### 6.1 Tombstones
“Delete” sets `EntryFlags::Deleted` (or records a delete op in delta). Iteration and lookup skip deleted entries.

### 6.2 `rehash()`
Rebuild indices without changing entry storage. Useful after applying tombstones or changing values that affect lookup policies.

### 6.3 `compact()`
Rebuild a new store:
- drops deleted entries
- repacks arenas
- rebuilds all indices

This is the only time entry ids/handles may change. APIs should treat `compact()` as an explicit invalidation boundary.

## 7. Write Policies & Tag Ordering

### 7.1 Modes
- **Edit/In-place mode:** preserve wire order and avoid moving unrelated data when possible.
- **Replace/New mode:** may rewrite metadata structures; default ordering may be canonical.

### 7.2 Ordering policy (write-time option)
Provide an enum-driven policy (no lambdas/`std::function`):
- `PreserveWire`
- `TagIdAscending`
- `CustomRankTable` (caller supplies `span<KeyRank{MetaKey key, uint32 rank}>`)

Implementation notes:
- sorting is applied to an `EntryId` list, not the entries themselves
- comparator is stable; tie-breaker uses `Origin.order_in_block` for deterministic duplicate handling

## 8. Cross-Family Sync & Linking

### 8.1 Inputs
- Exiv2 conversion rules: `OpenMeta-internal/internals/registry/_generated/merged/mappings/exiv2.jsonl`
- ExifTool MWG rules: `OpenMeta-internal/internals/registry/_generated/merged/mwg/tags.jsonl`

### 8.2 Operations (draft)
- `generate_xmp_from_exif_iptc(...)`: creates XMP entries with `Derivation{rule_id, derived_from}`
- `sync_mwg(...)`: resolves MWG “preferred source” and applies WriteAlso fanout deterministically
- `analyze_links(...)`: optional, emits link edges when file already contains equivalent copies

Linking should be key-based (stable across compaction), not pointer-based.

## 9. API Sketch (non-normative)

```cpp
// Read-only store
struct MetaStore;

// Thread-local edit overlay
struct MetaEdit {
  // add/set/delete operations recorded locally
};

// Deterministic merge of multiple edits into a new frozen store
MetaStore commit(const MetaStore& base, span<const MetaEdit> edits);
```

This draft intentionally avoids locking in exact names/signatures until the first prototype under `src/`.
