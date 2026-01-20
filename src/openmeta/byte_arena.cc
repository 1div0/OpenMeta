#include "openmeta/byte_arena.h"

#include <cstring>

namespace openmeta {

static uint32_t align_up_u32(uint32_t value, uint32_t alignment) noexcept
{
  if (alignment <= 1U) {
    return value;
  }
  const uint32_t mask = alignment - 1U;
  return (value + mask) & ~mask;
}

void ByteArena::clear() noexcept
{
  buffer_.clear();
}

void ByteArena::reserve(size_t size_bytes)
{
  buffer_.reserve(size_bytes);
}

ByteSpan ByteArena::append(std::span<const std::byte> bytes)
{
  const uint32_t offset = static_cast<uint32_t>(buffer_.size());
  buffer_.resize(buffer_.size() + bytes.size());
  if (!bytes.empty()) {
    std::memcpy(buffer_.data() + offset, bytes.data(), bytes.size());
  }
  return ByteSpan{offset, static_cast<uint32_t>(bytes.size())};
}

ByteSpan ByteArena::append_string(std::string_view text)
{
  const std::span<const std::byte> bytes(
    reinterpret_cast<const std::byte*>(text.data()),
    text.size());
  return append(bytes);
}

ByteSpan ByteArena::allocate(uint32_t size_bytes, uint32_t alignment)
{
  const uint32_t start = align_up_u32(
    static_cast<uint32_t>(buffer_.size()),
    alignment);
  if (start > buffer_.size()) {
    buffer_.resize(start, std::byte{0});
  }
  const uint32_t offset = start;
  buffer_.resize(static_cast<size_t>(start) + size_bytes);
  return ByteSpan{offset, size_bytes};
}

std::span<const std::byte> ByteArena::bytes() const noexcept
{
  return std::span<const std::byte>(buffer_.data(), buffer_.size());
}

std::span<std::byte> ByteArena::bytes_mut() noexcept
{
  return std::span<std::byte>(buffer_.data(), buffer_.size());
}

std::span<const std::byte> ByteArena::span(ByteSpan view) const noexcept
{
  const std::span<const std::byte> all = bytes();
  if (view.offset > all.size()) {
    return std::span<const std::byte>();
  }
  const size_t end = static_cast<size_t>(view.offset) + view.size;
  if (end > all.size()) {
    return std::span<const std::byte>();
  }
  return all.subspan(view.offset, view.size);
}

std::span<std::byte> ByteArena::span_mut(ByteSpan view) noexcept
{
  const std::span<std::byte> all = bytes_mut();
  if (view.offset > all.size()) {
    return std::span<std::byte>();
  }
  const size_t end = static_cast<size_t>(view.offset) + view.size;
  if (end > all.size()) {
    return std::span<std::byte>();
  }
  return all.subspan(view.offset, view.size);
}

} // namespace openmeta
