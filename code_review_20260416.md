# OpenMeta C++ Library — Deep Code Review

**Date:** 2026-04-16  
**Scope:** Full library source (`src/openmeta/`, `src/include/openmeta/`)  
**Baseline:** SKILL.md (C++20, no exceptions, no RTTI, no lambdas, explicit ownership,
no hidden allocations in hot paths, deterministic output)  
**Codebase size:** ~110 K lines across 65 source and 35 header files

---

## Executive Summary

OpenMeta is a well-hardened metadata processing library. Bounds checking,
resource limits, loop/cycle detection, and XML bomb protection are present
throughout the read path. The write/transfer path (`metadata_transfer.cc`) is
the youngest and riskiest surface.

This review found **5 critical**, **17 high**, **24 medium**, and **17 low**
findings across five review areas:

| Area | Critical | High | Medium | Low |
|------|----------|------|--------|-----|
| Core infrastructure | 0 | 1 | 7 | 5 |
| Container scan / decode | 1 | 1 | 7 | 6 |
| Transfer / edit / export | 3 | 4 | 4 | 3 |
| Makernote decoders / adapters | 1 | 6 | 6 | 5 |
| Specialized decoders (BMFF, JUMBF, EXR, CRW…) | 0 | 5 | 10 | 6 |
| **Total** | **5** | **17** | **24** | **17** |

The three most impactful items are:

1. **JPEG segment `uint16_t` overflow** — can produce corrupted output files.
2. **EXIF IFD `uint32_t` cursor overflow** — can cause a heap buffer overflow
   during EXIF serialization.
3. **BigTIFF `entry_count` overflow before cap** — can cause reads at wrong
   offsets in crafted files.

---

## Table of Contents

1. [Critical Findings](#1-critical-findings)
2. [High Findings](#2-high-findings)
3. [Medium Findings](#3-medium-findings)
4. [Low Findings](#4-low-findings)
5. [Maintenance Hotspots](#5-maintenance-hotspots)
6. [SKILL.md Compliance Summary](#6-skillmd-compliance-summary)
7. [Positive Observations](#7-positive-observations)

---

## 1. Critical Findings

### C1 — JPEG Segment Integer Overflow → Corrupted Output

| | |
|---|---|
| **File** | `metadata_transfer.cc:1486, 1653, 1681` |
| **Category** | Vulnerability |

Three functions cast `payload.size() + 2U` to `uint16_t` without bounds
checking. JPEG segments have a 16-bit length field (max 65535, payload max
65533). If payload exceeds 65533 bytes, the cast silently wraps, producing a
corrupt segment header. Downstream parsers will misinterpret the file.

**Fix:** `if (payload.size() > 65533U) return error;` before each cast.

---

### C2 — EXIF IFD Offset `uint32_t` Overflow → Corrupt EXIF Layout / OOB Writes

| | |
|---|---|
| **File** | `metadata_transfer.cc:6110–6229` |
| **Category** | Vulnerability |

The EXIF serializer accumulates IFD directory sizes and value offsets in
`uint32_t cursor` / `data_cursor` without overflow checks. If either sum wraps,
the computed `dir_offset` / `value_offset` fields become wrong before the final
`tiff_bytes` allocation. The later `std::memcpy` calls are guarded by
size checks, but the directory-writing path still performs direct writes using
the wrapped offsets, so the practical risk is corrupt EXIF layout and
out-of-bounds writes during TIFF directory construction. The `align_to_2`
helper (line 5936: `(v+1U) & ~1U`) also wraps when `v == UINT32_MAX`.

**Fix:** Add checked-add helpers. Validate `data_cursor` before allocation.
Also validate the final APP1 payload against the JPEG APP1 segment limit
(`tiff_bytes.size() + 6 <= 65533`, so TIFF bytes `<= 65527`).

---

### C3 — No APP1 64 KB Size Limit Check

| | |
|---|---|
| **File** | `metadata_transfer.cc:6438–6442` |
| **Category** | Vulnerability |

After building the TIFF blob it is assembled into an APP1 payload with a 6-byte
prefix but never checked against the JPEG APP1 64 KB segment limit. Combined
with C1, an oversized APP1 payload silently produces a corrupted JPEG.

**Fix:** `if (tiff_bytes.size() + 6 > 65533) return error;`

---

### C4 — BigTIFF `entry_count * 20` Overflow Before Cap

| | |
|---|---|
| **File** | `container_scan.cc:4325–4336` |
| **Category** | Vulnerability |

`entry_count` is read as uncapped `uint64_t`. The product
`entries_off + entry_count * 20` is computed before the `> 0x10000` cap at line
4342. A crafted BigTIFF with huge `entry_count` causes silent 64-bit
wraparound, passing the bounds check and reading from an incorrect offset.

**Fix:** Move the `entry_count > 0x10000` cap to immediately after reading
`n64`.

---

### C5 — Sony Integer Truncation → Silent Wrong Offset

| | |
|---|---|
| **File** | `exif_makernote_sony.cc:1566` |
| **Category** | Vulnerability |

```cpp
const uint16_t abs_off = uint16_t(meter_off + r.off);
```

If `meter_off + r.off > 65535`, the cast silently wraps. The subsequent bounds
check uses the truncated value, passing it. The read then occurs at a
completely wrong offset within the makernote blob.

**Fix:** Compute in `uint64_t`, bounds-check before any narrowing cast.

---

## 2. High Findings

### H1 — SKILL.md Violation: Lambdas (6 sites)

| Files | Lines |
|-------|-------|
| `container_scan.cc` | 707–782 (2 lambdas) |
| `metadata_transfer.cc` | 15301, 15318, 15331 (3 lambdas) |
| `exif_makernote_canon.cc` | 1709 (1 lambda) |

SKILL.md line 57: "Lambdas are forbidden." All six instances use `[&]`
captures.

**Fix:** Extract to named `static` free functions taking parameters explicitly.

---

### H2 — SKILL.md Violation: Virtual Functions in `MetadataSink`

| | |
|---|---|
| **File** | `interop_export.h:92–96` |
| **Category** | SKILL-violation |

```cpp
class MetadataSink {
public:
    virtual ~MetadataSink() = default;
    virtual void on_item(const ExportItem& item) noexcept = 0;
};
```

This is the only virtual interface found. Six subclasses exist in the former
flat interop export adapter (4) and `ocio_adapter.cc` (2).

**Fix:** Replace with a function-pointer + context struct.

---

### H3 — SKILL.md Violation: `std::unordered_map/set` in `xmp_dump.cc`

| | |
|---|---|
| **File** | `xmp_dump.cc:18, 5277–5695` |
| **Category** | SKILL-violation |

16+ `std::unordered_map/set` type aliases exist in the portable XMP generation
path. In the reviewed code these containers are used as claim/lookup tables,
not as emitted-order iteration sources, so this review did **not** confirm a
current nondeterministic output bug from hash iteration order alone. The issue
here is primarily SKILL.md compliance and allocation behavior, not a proven
correctness failure.

**Fix:** If strict SKILL.md compliance is required, replace the hash tables
with deterministic lookup structures (for example sorted vectors or flat maps)
while preserving the current explicit output-order vectors.

---

### H4 — SKILL.md Violation: `std::vector` Allocations in Hot Decode Paths

| File | Lines | Detail |
|------|-------|--------|
| `exif_makernote_nikon.cc` | 2837–2843, 3111–3117 | `std::vector<std::byte>` in decryption loops |
| `exr_decode.cc` | 363–378, 509–529 | `std::vector<uint32_t>.resize(n)` per attribute |
| `jumbf_decode.cc` | ~10656, 10720–10733 | `std::vector<std::byte>` and `std::string` per CBOR item |
| `bmff_fields_decode.cc` | 386–440 | `std::string` with `.append()` in loops |
| `ccm_query.cc` | ~1055, 1078, 1180 | `emplace_back()`/`push_back()` beyond `reserve` |

**Fix:** Use arena-backed or stack buffers with size caps; increase `reserve`
to safe upper bounds.

---

### H5 — Nikon/Sony Decryption Span Underflow When `size < 4`

| | |
|---|---|
| **File** | `exif_makernote_nikon.cc:2843, 3117` |
| **Category** | Vulnerability |

```cpp
std::span<std::byte>(dec.data() + 4, dec.size() - 4);
```

If `raw_src.size() < 4`, `dec.size() - 4` underflows to `SIZE_MAX`.

**Fix:** `if (raw_src.size() < 4) continue;`

---

### H6 — Sony Face-Info Tag Overflow

| | |
|---|---|
| **File** | `exif_makernote_sony.cc:2039` |
| **Category** | Vulnerability |

`uint16_t(face_len * i)` — `face_len * i` is `uint16_t × uint16_t` promoted
to `int`. If the product exceeds 65535, it wraps, producing colliding tag IDs.

**Fix:** Compute in `uint32_t`, clamp or reject.

---

### H7 — Olympus Sub-IFD Off-by-One OOB Read

| | |
|---|---|
| **File** | `exif_makernote_olympus.cc:98–109` |
| **Category** | Vulnerability |

`sub_ifd_off >= mn.size()` is checked, but IFD parse needs at least 2 bytes
for the entry count. A value of `mn.size() - 1` passes the check but causes a
1-byte OOB read.

**Fix:** Require `sub_ifd_off + 2 <= mn.size()`.

---

### H8 — `ExportItem::name` string_view Lifetime Fragility

| | |
|---|---|
| **File** | `interop_export.cc:~1281` |
| **Category** | Memory |

`ExportItem::name` is a `string_view` pointing into a local `std::string`
reused per-loop-iteration. The view is valid only during `sink.on_item()`. Any
sink that stores the `ExportItem` beyond the callback scope triggers
use-after-free.

**Fix:** Document the contract prominently, or pass owned data.

---

### H9 — `size_t` Truncation on 32-bit Targets

| | |
|---|---|
| **Files** | `jumbf_decode.cc:~10865`, `geotiff_decode.cc:~305` |
| **Category** | Vulnerability |

`static_cast<size_t>(payload_off)` from `uint64_t` — on 32-bit systems
`size_t` is 32 bits, silently truncating values > 4 GB.

**Fix:** `if (val > SIZE_MAX) return false;` before cast.

---

### H10 — `simple_meta_read` — 585-Line Function

| | |
|---|---|
| **File** | `simple_meta.cc:832–1416` |
| **Category** | Maintenance |

Block-type dispatch with 12+ branches nested 4–5 levels deep. Extremely hard
to review, test, or modify safely.

**Fix:** Extract each block-kind handler into a separate `static` function.

---

### H11 — Inconsistent Checked Arithmetic in ICC

| | |
|---|---|
| **File** | `icc_interpret.cc:836–837 vs 1224–1229` |
| **Category** | Vulnerability |

The `ncl2` tag properly uses `checked_mul_u64`/`checked_add_u64`. The `curv`
tag computes `12ULL + pair_count * 4ULL` unchecked. Currently safe (uint16
inputs), but a maintenance trap if types are widened.

**Fix:** Use checked arithmetic uniformly.

---

### H12 — Unchecked `std::vector` Growth in CCM Query

| | |
|---|---|
| **File** | `ccm_query.cc:~1055, 1078, 1180` |
| **Category** | SKILL-violation |

`dng_ifds.emplace_back()` and `out->push_back(field)` can trigger
reallocation. `reserve(8)` is called but > 8 IFDs cause hot-path allocation.

**Fix:** Use bounded `std::array` or increase reserve to a safe upper bound.

---

## 3. Medium Findings

### M1 — Systematic `offset + N > size` Wrap Pattern

| | |
|---|---|
| **Files** | `container_scan.cc:89,102,151…`, `icc_decode.cc:16,34,44`, `iptc_iim_decode.cc:17`, `photoshop_irb_decode.cc:35,57` |
| **Category** | Vulnerability (defense-in-depth) |

All binary read helpers use `offset + N > size`. If `offset` is close to
`SIZE_MAX`, the addition wraps.

**Fix:** `offset > size || N > size - offset` form throughout.

---

### M2 — `logical_offset + data_size` Overflow in Payload Reassembly

| | |
|---|---|
| **File** | `container_payload.cc:662, 690` |
| **Category** | Vulnerability |

Attacker-controlled `ContainerBlockRef` values can wrap the sum, producing a
small `max_end` and silently truncating the reassembled payload.

**Fix:** Add overflow guard before addition.

---

### M3 — BigTIFF `ifd0` Offset Overflow in Format Detection

| | |
|---|---|
| **File** | `container_scan.cc:286` |
| **Category** | Vulnerability |

`offset + ifd0 < bytes.size()` wraps if `ifd0 ≈ UINT64_MAX`.

**Fix:** `return ifd0 < bytes.size() && offset <= bytes.size() - ifd0;`

---

### M4 — `std::string` Allocations Under `-fno-exceptions`

| | |
|---|---|
| **Files** | `photoshop_irb_decode.cc:291,463,480,634,702`, `xmp_decode.cc:314,331` |
| **Category** | SKILL-violation / UB |

`std::string` operations can throw `std::bad_alloc`. With `-fno-exceptions`,
allocation failure is UB. Sizes are driven by attacker-controlled input.

**Fix:** Accept as initialization-phase code and document, or pre-validate
sizes.

---

### M5 — RAF XMP `needed` Count Undercount

| | |
|---|---|
| **File** | `container_scan.cc:4680–4681` |
| **Category** | Logic bug |

When TIFF sub-scan was already `OutputTruncated`, `res.needed = res.written + 1`
loses the original `needed` from the TIFF scan.

**Fix:** `res.needed += 1;`

---

### M6 — XMP `total_value_bytes` Stale During `char_data` Accumulation

| | |
|---|---|
| **File** | `xmp_decode.cc:793–798` |
| **Category** | Logic bug |

`total_value_bytes` is not updated during text accumulation, so the limit check
uses a stale value. The limit is correctly enforced at emit time, but
intermediate memory pressure is not bounded.

---

### M7 — Visited Array vs `max_ifds` Mismatch in EXIF Decoder

| | |
|---|---|
| **File** | `exif_tiff_decode.cc:4322–4323` |
| **Category** | Vulnerability (config-dependent) |

`visited_offs[256]` is fixed-size but `max_ifds` is user-configurable. Setting
`max_ifds > 256` causes early termination without processing all IFDs.

**Fix:** Clamp `max_ifds` to array capacity, or document the 256 ceiling.

---

### M8 — `ByteArena::allocate()` Does Not Validate Power-of-Two Alignment

| | |
|---|---|
| **File** | `byte_arena.cc:108–130` |
| **Category** | Vulnerability |

`align_up_size` uses `alignment - 1` as a bitmask, which only works when
`alignment` is a power of two. A non-power-of-two alignment produces wrong
results and can cause overlapping allocations.

**Fix:** `if (alignment & (alignment - 1)) return ByteSpan{};`

---

### M9 — `MetaStore::add_block/add_entry` Overflow at `UINT32_MAX`

| | |
|---|---|
| **File** | `meta_store.cc:43–63` |
| **Category** | Vulnerability |

`static_cast<BlockId>(blocks_.size())` truncates if the vector exceeds
`UINT32_MAX`. With `kInvalidBlockId = 0xFFFFFFFF`, reaching that size returns
the sentinel or wraps.

**Fix:** `if (blocks_.size() >= kInvalidBlockId) return kInvalidBlockId;`

---

### M10 — `make_array()` Truncates Count on Non-Aligned Byte Size

| | |
|---|---|
| **File** | `meta_value.cc:225–226` |
| **Category** | Vulnerability |

If `raw_elements.size()` is not evenly divisible by `element_size`, the element
count silently truncates. The byte span and element count become inconsistent.

**Fix:** Reject non-aligned sizes or document the truncation.

---

### M11 — `merge_exif_status` Missing Null Check

| | |
|---|---|
| **File** | `simple_meta.cc:659` |
| **Category** | Vulnerability |

`merge_exif_status` dereferences `*out` without null check. Its siblings
`merge_xmp_status` and `merge_jumbf_status` do check `!out`.

**Fix:** Add `if (!out) return;`

---

### M12 — C-Style Float Casts in `simple_meta.cc`

| | |
|---|---|
| **File** | `simple_meta.cc:465–467` |
| **Category** | SKILL-violation |

`float(od_raw)` is a functional-style cast. SKILL.md requires C++-style casts.

**Fix:** `static_cast<float>(od_raw)`

---

### M13 — JUMBF `OutputTruncated` Misclassified as `LimitExceeded`

| | |
|---|---|
| **File** | `simple_meta.cc:948–949` |
| **Category** | Incorrect error mapping |

EXIF and XMP correctly map `OutputTruncated → OutputTruncated`. JUMBF maps it
to `LimitExceeded` instead.

---

### M14 — CIFF Depth Limit (32) Inconsistent with BMFF (16)

| | |
|---|---|
| **File** | `crw_ciff_decode.cc:965` |
| **Category** | DoS |

CRW allows depth 32 while BMFF uses 16. A crafted CIFF can stack 32 recursive
calls.

**Fix:** Align to 16 or document the rationale.

---

### M15 — Inconsistent CBOR Depth Limits in JUMBF

| | |
|---|---|
| **File** | `jumbf_decode.cc:~2408 vs ~10234` |
| **Category** | Maintenance |

`cbor_lite_skip_from_head` hardcodes `depth > 64U` while `cbor_depth_ok` uses
the configurable `max_cbor_depth`. Skip and parse may diverge.

---

### M16 — Indefinite-Length CBOR Array Without Explicit Iteration Cap

| | |
|---|---|
| **File** | `jumbf_decode.cc:~10690–10708` |
| **Category** | DoS |

`while (true)` for indefinite arrays relies solely on item budget. A malformed
stream without a break byte burns the full budget.

**Fix:** Add explicit guard: `if (index > max_items) return false;`

---

### M17 — `validate.cc` Null `string_view` Data → UB in `assign`

| | |
|---|---|
| **File** | `validate.cc:65–70` |
| **Category** | Vulnerability |

`issue.category.assign(category.data(), category.size())` — if `data()` is
`nullptr` (valid for default-constructed `string_view`), passing it to
`assign(const char*, size_t)` is UB.

**Fix:** `issue.category.assign(category.begin(), category.end());`

---

### M18 — `std::to_string()` Heap Allocations

| | |
|---|---|
| **File** | `metadata_transfer.cc:745, 895, ~10 more` |
| **Category** | SKILL-violation |

`std::to_string()` allocates a heap string per call. The one at line 745 is in
a loop.

**Fix:** `snprintf` into a stack buffer.

---

### M19 — `snprintf` Return Value Unchecked in `console_format.cc`

| | |
|---|---|
| **File** | `console_format.cc:86–90, 119` |
| **Category** | Vulnerability |

`out->append(buf)` trusts null-termination without checking `snprintf` return.

**Fix:** `out->append(buf, static_cast<size_t>(n))` after validating `n > 0`.

---

### M20 — `append_console_escaped_ascii` Ambiguous Return Semantics

| | |
|---|---|
| **File** | `console_format.cc:54–101` |
| **Category** | Maintenance |

Returns `true` on allocation failure (line 62) but `dangerous` on success.
Caller cannot distinguish error from "escape needed."

---

### M21 — `CreateFileA` Does Not Support UTF-8 Paths on Windows

| | |
|---|---|
| **File** | `mapped_file.cc:72` |
| **Category** | Vulnerability |

Files with non-ASCII paths will fail to open on Windows.

**Fix:** Use `CreateFileW` with a UTF-8 → UTF-16 conversion.

---

### M22 — `MappedFile` Move Constructor Relies on Default Member Initializers

| | |
|---|---|
| **File** | `mapped_file.cc:30–33` |
| **Category** | Vulnerability |

The move constructor body `*this = std::move(other)` calls `operator=`, which
calls `close()`. The safety of `close()` depends on default member initializers
matching the "closed" state — a subtle invariant.

**Fix:** Add a comment, or explicitly initialize before delegating.

---

### M23 — Dangling `string_view` from Arena in CCM

| | |
|---|---|
| **File** | `ccm_query.cc:101–107` |
| **Category** | Memory |

Returns `std::string_view` into arena memory. Currently safe but a lifetime
trap.

**Fix:** Add `/// @note valid only while arena is alive` doc comment.

---

### M24 — LibRaw Adapter Inconsistent Overflow Checks

| | |
|---|---|
| **File** | `libraw_adapter.cc:435–450` |
| **Category** | Vulnerability |

`payload.resize()` has an overflow guard but `blocks.resize()` and
`ifds.resize()` do not.

**Fix:** Apply the same guard uniformly.

---

## 4. Low Findings

| # | File | Issue |
|---|------|-------|
| L1 | `byte_arena.h:32` | `ByteArena` is a `class` not a POD `struct` (justified) |
| L2 | `meta_store.h:134` | `MetaStore` is a `class` (justified by lifecycle invariant) |
| L3 | `meta_value.cc:225` | `make_array` with `element_size==0` → inconsistent state |
| L4 | `meta_key.cc:197` | No `default` case in `compare_key` switch |
| L5 | `resource_policy.h:81–236` | ~155 lines of `inline` functions in header |
| L6 | `container_payload.cc:212` | Redundant double `reinterpret_cast` |
| L7 | `container_payload.cc:128,228,306,367` | `max_output_bytes=0` disables decompression bomb limit |
| L8 | `iptc_iim_decode.cc:130`, `photoshop_irb_decode.cc:1050` | `wire_count` narrowing casts |
| L9 | `interop_safety_internal.cc:282–293` | `noexcept` + `std::string::assign()` → UB in mixed envs |
| L10 | `exif_makernote_nikon.cc:150–312` | 30+ one-liner model-check wrappers |
| L11 | `exif_makernote_tag_names.cc`, `exif_tag_names.cc` | ~22 near-identical placeholder-synthesis functions |
| L12 | All makernote files | Magic offsets without provenance comments |
| L13 | `exif_makernote_sony.cc` | Fixed stack arrays without `static_assert` on size |
| L14 | `bmff_fields_decode.cc` | `1U << 16` magic constant repeated 6× |
| L15 | `jumbf_decode.cc` | Hardcoded fourcc magic values without named constants |
| L16 | `icc_interpret.cc` | ICC header offsets (84U, 32U, 116U) undocumented |
| L17 | `crw_ciff_decode.cc:739, 760` | Signed integer literals where unsigned expected |

---

## 5. Maintenance Hotspots

### Top Files by Size (Complexity Proxy)

| File | Lines | Risk |
|------|------:|------|
| `metadata_transfer.cc` | 24,007 | **Very high** — monolith spanning 8+ container formats, EXIF/ICC/IPTC/JUMBF/C2PA serialization, file I/O, and XMP sidecar logic |
| `xmp_dump.cc` | 12,074 | High — portable XMP generation with `unordered_map` determinism risk |
| `jumbf_decode.cc` | 11,274 | High — dual CBOR walkers that must stay in sync |
| `exif_tiff_decode.cc` | 5,039 | Moderate — well-structured but large |
| `exif_makernote_nikon.cc` | 5,142 | Moderate — long but repetitive |
| `container_scan.cc` | 4,875 | Moderate — 612-line `bmff_emit_items_from_iloc` function |

### Highest-Impact Refactors

1. **Split `metadata_transfer.cc`** into per-format modules (`jpeg_rewrite.cc`,
   `tiff_rewrite.cc`, `png_rewrite.cc`, etc.) and shared helpers
   (`exif_pack.cc`, `icc_pack.cc`). This single file is the #1 maintenance
   risk in the codebase.

2. **Extract duplicated binary-read helpers** (`read_u16_be`, `read_u32_be`,
   etc.) into a shared internal header. Currently copy-pasted across
   `icc_decode.cc`, `iptc_iim_decode.cc`, `photoshop_irb_decode.cc`,
   `exif_tiff_decode.cc`.

3. **Unify write/serialize pairs** in `metadata_transfer.cc`. Each container
   format has paired `write_*` / `serialize_*` functions with near-identical
   logic (one writes to `TransferByteWriter`, one to `std::vector<std::byte>`).
   Bugs like C1 must be fixed in multiple places.

4. **Extract `simple_meta_read` dispatch** (585 lines, 12+ block-kind
   branches) into per-kind handler functions.

5. **Consolidate makernote safe-multiply guards** — the pattern
   `if (count > UINT64_MAX / unit) continue;` is duplicated dozens of times.
   Extract to a shared `safe_mul_u64()` helper.

6. **Replace O(n²) insertion sort** in `container_payload.cc:39–78` and O(n²)
   JUMBF normalization in `container_scan.cc:664–836` — potential DoS with
   large block counts.

---

## 6. SKILL.md Compliance Summary

| Rule | Status | Violations |
|------|--------|------------|
| No exceptions | ✅ Compliant | `-fno-exceptions` build; some `std::string` UB risk (M4) |
| No RTTI | ✅ Compliant | None found |
| No lambdas | ❌ 6 violations | H1 (6 sites across 3 files) |
| No virtual functions | ❌ 1 interface | H2 (`MetadataSink` + 6 subclasses) |
| No hidden alloc in hot paths | ❌ ~20 sites | H4, H12, M4, M18 |
| Deterministic output | ⚠️ Risk | H3 (`unordered_map` iteration in XMP) |
| C++-style casts only | ❌ 1 site | M12 (`float()` cast) |
| Prefer `struct` / POD | ⚠️ Justified | L1, L2 (encapsulation for RAII types) |
| No `std::function`/`std::bind` | ✅ Compliant | None found |
| No ranges/views/coroutines | ✅ Compliant | None found |
| No iostreams | ✅ Compliant | None found |
| `std::vector` frozen after init | ⚠️ Mostly | H4 violations in decode loops |

---

## 7. Positive Observations

- **Read path is well-hardened.** Bounds checking, resource limits (max IFDs,
  max entries, max depth), cycle detection (visited offset arrays), and XML
  bomb protection are consistently present.
- **`preview_extract.cc` is exemplary.** IFD traversal has cycle detection
  (line 575–598), a hard cap of 256 IFDs, and all offset+size calculations use
  the subtraction-based overflow guard (`len > file_bytes.size() - off`).
- **`meta_flags.h` is fully SKILL-compliant.** Header-only, `constexpr` free
  functions, `enum class` with bitwise ops, no allocations.
- **Checked arithmetic exists.** `checked_mul_u64`/`checked_add_u64` are used
  in several decoders (ICC `ncl2`, BMFF). The pattern just needs to be applied
  uniformly.
- **`ByteArena` alias detection** correctly handles self-referencing appends
  via offset remembering + `memmove`.
- **Thread-local tag-name buffers** avoid heap allocations for display-name
  synthesis — a good pattern for the library's use case, though the implicit
  lifetime contract should be documented.
- **EXIF MakerNote coverage is broad** with vendor-specific decoders for 17
  manufacturers, including encrypted Nikon/Sony blobs.

---

*Report generated by automated deep review of 110K LOC across 100 source files.*
