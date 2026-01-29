#include "openmeta/meta_value.h"

#include <cstring>

namespace openmeta {

MetaValue
make_u8(uint8_t value) noexcept
{
    MetaValue v;
    v.kind      = MetaValueKind::Scalar;
    v.elem_type = MetaElementType::U8;
    v.count     = 1;
    v.data.u64  = value;
    return v;
}


MetaValue
make_i8(int8_t value) noexcept
{
    MetaValue v;
    v.kind      = MetaValueKind::Scalar;
    v.elem_type = MetaElementType::I8;
    v.count     = 1;
    v.data.i64  = value;
    return v;
}


MetaValue
make_u16(uint16_t value) noexcept
{
    MetaValue v;
    v.kind      = MetaValueKind::Scalar;
    v.elem_type = MetaElementType::U16;
    v.count     = 1;
    v.data.u64  = value;
    return v;
}


MetaValue
make_i16(int16_t value) noexcept
{
    MetaValue v;
    v.kind      = MetaValueKind::Scalar;
    v.elem_type = MetaElementType::I16;
    v.count     = 1;
    v.data.i64  = value;
    return v;
}


MetaValue
make_u32(uint32_t value) noexcept
{
    MetaValue v;
    v.kind      = MetaValueKind::Scalar;
    v.elem_type = MetaElementType::U32;
    v.count     = 1;
    v.data.u64  = value;
    return v;
}


MetaValue
make_i32(int32_t value) noexcept
{
    MetaValue v;
    v.kind      = MetaValueKind::Scalar;
    v.elem_type = MetaElementType::I32;
    v.count     = 1;
    v.data.i64  = value;
    return v;
}


MetaValue
make_u64(uint64_t value) noexcept
{
    MetaValue v;
    v.kind      = MetaValueKind::Scalar;
    v.elem_type = MetaElementType::U64;
    v.count     = 1;
    v.data.u64  = value;
    return v;
}


MetaValue
make_i64(int64_t value) noexcept
{
    MetaValue v;
    v.kind      = MetaValueKind::Scalar;
    v.elem_type = MetaElementType::I64;
    v.count     = 1;
    v.data.i64  = value;
    return v;
}


MetaValue
make_f32_bits(uint32_t bits) noexcept
{
    MetaValue v;
    v.kind          = MetaValueKind::Scalar;
    v.elem_type     = MetaElementType::F32;
    v.count         = 1;
    v.data.f32_bits = bits;
    return v;
}


MetaValue
make_f64_bits(uint64_t bits) noexcept
{
    MetaValue v;
    v.kind          = MetaValueKind::Scalar;
    v.elem_type     = MetaElementType::F64;
    v.count         = 1;
    v.data.f64_bits = bits;
    return v;
}


MetaValue
make_urational(uint32_t numer, uint32_t denom) noexcept
{
    MetaValue v;
    v.kind      = MetaValueKind::Scalar;
    v.elem_type = MetaElementType::URational;
    v.count     = 1;
    v.data.ur   = URational { numer, denom };
    return v;
}


MetaValue
make_srational(int32_t numer, int32_t denom) noexcept
{
    MetaValue v;
    v.kind      = MetaValueKind::Scalar;
    v.elem_type = MetaElementType::SRational;
    v.count     = 1;
    v.data.sr   = SRational { numer, denom };
    return v;
}


MetaValue
make_bytes(ByteArena& arena, std::span<const std::byte> bytes)
{
    MetaValue v;
    v.kind      = MetaValueKind::Bytes;
    v.count     = static_cast<uint32_t>(bytes.size());
    v.data.span = arena.append(bytes);
    return v;
}


MetaValue
make_text(ByteArena& arena, std::string_view text, TextEncoding encoding)
{
    MetaValue v;
    v.kind          = MetaValueKind::Text;
    v.text_encoding = encoding;
    v.count         = static_cast<uint32_t>(text.size());
    v.data.span     = arena.append_string(text);
    return v;
}


MetaValue
make_array(ByteArena& arena, MetaElementType elem_type,
           std::span<const std::byte> raw_elements, uint32_t element_size)
{
    MetaValue v;
    v.kind                     = MetaValueKind::Array;
    v.elem_type                = elem_type;
    const uint32_t total_bytes = static_cast<uint32_t>(raw_elements.size());
    if (element_size > 0U) {
        v.count = total_bytes / element_size;
    }
    v.data.span = arena.append(raw_elements);
    return v;
}


static MetaValue
make_array_copy(ByteArena& arena, MetaElementType elem_type, const void* data,
                size_t count, size_t element_size, size_t alignment)
{
    MetaValue v;
    v.kind      = MetaValueKind::Array;
    v.elem_type = elem_type;
    v.count     = static_cast<uint32_t>(count);

    const size_t size_bytes = count * element_size;
    const ByteSpan span     = arena.allocate(static_cast<uint32_t>(size_bytes),
                                             static_cast<uint32_t>(alignment));
    const std::span<std::byte> dst = arena.span_mut(span);
    if (!dst.empty() && size_bytes > 0U) {
        std::memcpy(dst.data(), data, size_bytes);
    }
    v.data.span = span;
    return v;
}


MetaValue
make_u8_array(ByteArena& arena, std::span<const uint8_t> values)
{
    return make_array_copy(arena, MetaElementType::U8, values.data(),
                           values.size(), sizeof(uint8_t), alignof(uint8_t));
}


MetaValue
make_i8_array(ByteArena& arena, std::span<const int8_t> values)
{
    return make_array_copy(arena, MetaElementType::I8, values.data(),
                           values.size(), sizeof(int8_t), alignof(int8_t));
}


MetaValue
make_u16_array(ByteArena& arena, std::span<const uint16_t> values)
{
    return make_array_copy(arena, MetaElementType::U16, values.data(),
                           values.size(), sizeof(uint16_t), alignof(uint16_t));
}


MetaValue
make_i16_array(ByteArena& arena, std::span<const int16_t> values)
{
    return make_array_copy(arena, MetaElementType::I16, values.data(),
                           values.size(), sizeof(int16_t), alignof(int16_t));
}


MetaValue
make_u32_array(ByteArena& arena, std::span<const uint32_t> values)
{
    return make_array_copy(arena, MetaElementType::U32, values.data(),
                           values.size(), sizeof(uint32_t), alignof(uint32_t));
}


MetaValue
make_i32_array(ByteArena& arena, std::span<const int32_t> values)
{
    return make_array_copy(arena, MetaElementType::I32, values.data(),
                           values.size(), sizeof(int32_t), alignof(int32_t));
}


MetaValue
make_u64_array(ByteArena& arena, std::span<const uint64_t> values)
{
    return make_array_copy(arena, MetaElementType::U64, values.data(),
                           values.size(), sizeof(uint64_t), alignof(uint64_t));
}


MetaValue
make_i64_array(ByteArena& arena, std::span<const int64_t> values)
{
    return make_array_copy(arena, MetaElementType::I64, values.data(),
                           values.size(), sizeof(int64_t), alignof(int64_t));
}


MetaValue
make_f32_bits_array(ByteArena& arena, std::span<const uint32_t> bits)
{
    return make_array_copy(arena, MetaElementType::F32, bits.data(),
                           bits.size(), sizeof(uint32_t), alignof(uint32_t));
}


MetaValue
make_f64_bits_array(ByteArena& arena, std::span<const uint64_t> bits)
{
    return make_array_copy(arena, MetaElementType::F64, bits.data(),
                           bits.size(), sizeof(uint64_t), alignof(uint64_t));
}


MetaValue
make_urational_array(ByteArena& arena, std::span<const URational> values)
{
    return make_array_copy(arena, MetaElementType::URational, values.data(),
                           values.size(), sizeof(URational),
                           alignof(URational));
}


MetaValue
make_srational_array(ByteArena& arena, std::span<const SRational> values)
{
    return make_array_copy(arena, MetaElementType::SRational, values.data(),
                           values.size(), sizeof(SRational),
                           alignof(SRational));
}

}  // namespace openmeta
