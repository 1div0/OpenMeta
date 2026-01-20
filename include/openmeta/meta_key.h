#pragma once

#include "openmeta/byte_arena.h"

#include <cstdint>
#include <string_view>

namespace openmeta {

enum class MetaKeyKind : uint8_t {
  ExifTag,
  IptcDataset,
  XmpProperty,
  IccHeaderField,
  IccTag,
  PhotoshopIrb,
  GeotiffKey,
  PrintImField,
  JumbfField,
  JumbfCborKey,
};

struct MetaKey final {
  MetaKeyKind kind = MetaKeyKind::ExifTag;

  union Data {
    struct ExifTag final {
      ByteSpan ifd;
      uint16_t tag = 0;
    } exif_tag;

    struct IptcDataset final {
      uint16_t record = 0;
      uint16_t dataset = 0;
    } iptc_dataset;

    struct XmpProperty final {
      ByteSpan schema_ns;
      ByteSpan property_path;
    } xmp_property;

    struct IccHeaderField final {
      uint32_t offset = 0;
    } icc_header_field;

    struct IccTag final {
      uint32_t signature = 0;
    } icc_tag;

    struct PhotoshopIrb final {
      uint16_t resource_id = 0;
    } photoshop_irb;

    struct GeotiffKey final {
      uint16_t key_id = 0;
    } geotiff_key;

    struct PrintImField final {
      ByteSpan field;
    } printim_field;

    struct JumbfField final {
      ByteSpan field;
    } jumbf_field;

    struct JumbfCborKey final {
      ByteSpan key;
    } jumbf_cbor_key;

    Data() noexcept
      : exif_tag()
    {
    }
  } data;
};

struct MetaKeyView final {
  MetaKeyKind kind = MetaKeyKind::ExifTag;

  union Data {
    struct ExifTag final {
      std::string_view ifd;
      uint16_t tag = 0;
    } exif_tag;

    struct IptcDataset final {
      uint16_t record = 0;
      uint16_t dataset = 0;
    } iptc_dataset;

    struct XmpProperty final {
      std::string_view schema_ns;
      std::string_view property_path;
    } xmp_property;

    struct IccHeaderField final {
      uint32_t offset = 0;
    } icc_header_field;

    struct IccTag final {
      uint32_t signature = 0;
    } icc_tag;

    struct PhotoshopIrb final {
      uint16_t resource_id = 0;
    } photoshop_irb;

    struct GeotiffKey final {
      uint16_t key_id = 0;
    } geotiff_key;

    struct PrintImField final {
      std::string_view field;
    } printim_field;

    struct JumbfField final {
      std::string_view field;
    } jumbf_field;

    struct JumbfCborKey final {
      std::string_view key;
    } jumbf_cbor_key;

    Data() noexcept
      : exif_tag()
    {
    }
  } data;
};

MetaKey make_exif_tag_key(ByteArena& arena,
  std::string_view ifd,
  uint16_t tag);
MetaKey make_iptc_dataset_key(uint16_t record, uint16_t dataset) noexcept;
MetaKey make_xmp_property_key(ByteArena& arena,
  std::string_view schema_ns,
  std::string_view property_path);
MetaKey make_icc_header_field_key(uint32_t offset) noexcept;
MetaKey make_icc_tag_key(uint32_t signature) noexcept;
MetaKey make_photoshop_irb_key(uint16_t resource_id) noexcept;
MetaKey make_geotiff_key(uint16_t key_id) noexcept;
MetaKey make_printim_field_key(ByteArena& arena, std::string_view field);
MetaKey make_jumbf_field_key(ByteArena& arena, std::string_view field);
MetaKey make_jumbf_cbor_key(ByteArena& arena, std::string_view key);

int compare_key(const ByteArena& arena, const MetaKey& a, const MetaKey& b) noexcept;
int compare_key_view(const ByteArena& arena,
  const MetaKeyView& a,
  const MetaKey& b) noexcept;

} // namespace openmeta

