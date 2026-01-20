#pragma once

#include "openmeta/byte_arena.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace openmeta {

struct URational final {
  uint32_t numer = 0;
  uint32_t denom = 1;
};

struct SRational final {
  int32_t numer = 0;
  int32_t denom = 1;
};

enum class MetaValueKind : uint8_t {
  Empty,
  Scalar,
  Array,
  Bytes,
  Text,
};

enum class MetaElementType : uint8_t {
  U8,
  I8,
  U16,
  I16,
  U32,
  I32,
  U64,
  I64,
  F32,
  F64,
  URational,
  SRational,
};

enum class TextEncoding : uint8_t {
  Unknown,
  Ascii,
  Utf8,
  Utf16LE,
  Utf16BE,
};

struct MetaValue final {
  MetaValueKind kind = MetaValueKind::Empty;
  MetaElementType elem_type = MetaElementType::U8;
  TextEncoding text_encoding = TextEncoding::Unknown;
  uint32_t count = 0;

  union Data {
    uint64_t u64;
    int64_t i64;
    uint32_t f32_bits;
    uint64_t f64_bits;
    URational ur;
    SRational sr;
    ByteSpan span;

    Data() noexcept
      : u64(0)
    {
    }
  } data;
};

MetaValue make_u8(uint8_t value) noexcept;
MetaValue make_i8(int8_t value) noexcept;
MetaValue make_u16(uint16_t value) noexcept;
MetaValue make_i16(int16_t value) noexcept;
MetaValue make_u32(uint32_t value) noexcept;
MetaValue make_i32(int32_t value) noexcept;
MetaValue make_u64(uint64_t value) noexcept;
MetaValue make_i64(int64_t value) noexcept;
MetaValue make_f32_bits(uint32_t bits) noexcept;
MetaValue make_f64_bits(uint64_t bits) noexcept;
MetaValue make_urational(uint32_t numer, uint32_t denom) noexcept;
MetaValue make_srational(int32_t numer, int32_t denom) noexcept;

MetaValue make_bytes(ByteArena& arena, std::span<const std::byte> bytes);
MetaValue make_text(ByteArena& arena,
  std::string_view text,
  TextEncoding encoding);

MetaValue make_array(ByteArena& arena,
  MetaElementType elem_type,
  std::span<const std::byte> raw_elements,
  uint32_t element_size);

MetaValue make_u8_array(ByteArena& arena, std::span<const uint8_t> values);
MetaValue make_i8_array(ByteArena& arena, std::span<const int8_t> values);
MetaValue make_u16_array(ByteArena& arena, std::span<const uint16_t> values);
MetaValue make_i16_array(ByteArena& arena, std::span<const int16_t> values);
MetaValue make_u32_array(ByteArena& arena, std::span<const uint32_t> values);
MetaValue make_i32_array(ByteArena& arena, std::span<const int32_t> values);
MetaValue make_u64_array(ByteArena& arena, std::span<const uint64_t> values);
MetaValue make_i64_array(ByteArena& arena, std::span<const int64_t> values);
MetaValue make_f32_bits_array(ByteArena& arena, std::span<const uint32_t> bits);
MetaValue make_f64_bits_array(ByteArena& arena, std::span<const uint64_t> bits);
MetaValue make_urational_array(ByteArena& arena,
  std::span<const URational> values);
MetaValue make_srational_array(ByteArena& arena,
  std::span<const SRational> values);

} // namespace openmeta
