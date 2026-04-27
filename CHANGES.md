# OpenMeta Changes

## 0.4.7 - 2026-04-27

Changes compared with `0.4.6`.

### Added

- Added bounded foreign top-level BMFF `meta` merge support for parseable
  HEIF/AVIF/CR3-style item graphs. OpenMeta can now merge prepared
  Exif/XMP/JUMBF/C2PA item metadata into an existing foreign `meta` graph
  instead of appending a second competing `meta` box.
- Added BMFF XMP replacement and strip support for foreign item graphs that
  satisfy the bounded primary-item contract: a single parseable `iinf`,
  `iloc` version 0/1/2, `pitm`, and at most one `idat`.
- Added bounded BMFF ICC property merge support for foreign `meta` graphs.
  Existing ICC `colr/prof` and `colr/rICC` properties are removed from
  `iprp/ipco`, existing `ipma` associations are compacted/remapped, and the
  transferred `colr/prof` property is associated with the primary item.
- Added public CMake cache options for external BMFF usability checks:
  `OPENMETA_BMFF_HEIF_TEST_TARGET`, `OPENMETA_BMFF_AVIF_TEST_TARGET`,
  `OPENMETA_BMFF_CR3_TEST_TARGET`, and `OPENMETA_FFMPEG_EXECUTABLE`.
- Added external image-usability gate coverage for BMFF ICC and XMP transfer
  on configured HEIF/AVIF/CR3 targets when local tools can validate them.

### Changed

- BMFF edit/apply now preserves non-`meta` top-level boxes while rebuilding
  supported foreign `meta` item/property graphs in place.
- BMFF embedded-XMP strip mode no longer requires an OpenMeta-authored
  metadata `meta` box when the foreign graph is parseable and has a valid
  primary-item relationship.
- BMFF summary output and gate checks now cover both `bmff_item mime/xmp` and
  `bmff_property colr/prof` transfer results.
- Public transfer docs now describe the updated BMFF preserve/replace/strip
  contract and the configured-target validation limits.

### Fixed

- Fixed duplicate or stale BMFF ICC associations by replacing prior ICC
  properties and remapping `ipma` instead of adding competing property
  entries.
- Fixed supported foreign BMFF XMP strip/replacement paths that previously
  failed even when the target graph had enough structure for a safe bounded
  rewrite.
- Fixed Nikon MakerNote FlashInfo decoding for `0107`/`0108` and
  `0300`/`0301` layouts by emitting Flash Group A/B/C control-mode fields
  with ExifTool-compatible contextual names.

### Tests And Validation

- Added focused BMFF API tests for foreign Exif/XMP item replacement, XMP
  strip, ICC property replacement, existing `ipma` merge/remap, `iloc`
  rebasing, and fail-safe rejection of unsupported foreign graphs.
- Extended Nikon MakerNote tests for Flash Group A/B/C control-mode decoding.
- Extended the public transfer release and image-usability gates to accept
  configured BMFF target files and optional ffmpeg decode fallback.
- Verified the public release with the unit test suite, transfer release gate,
  external image-usability gate, documentation build, and whitespace checks.
