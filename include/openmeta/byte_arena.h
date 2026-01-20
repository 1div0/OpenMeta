#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace openmeta {

struct ByteSpan final {
  uint32_t offset = 0;
  uint32_t size = 0;
};

class ByteArena final {
public:
  ByteArena() = default;

  void clear() noexcept;
  void reserve(size_t size_bytes);

  ByteSpan append(std::span<const std::byte> bytes);
  ByteSpan append_string(std::string_view text);
  ByteSpan allocate(uint32_t size_bytes, uint32_t alignment);

  std::span<const std::byte> bytes() const noexcept;
  std::span<std::byte> bytes_mut() noexcept;
  std::span<const std::byte> span(ByteSpan view) const noexcept;
  std::span<std::byte> span_mut(ByteSpan view) noexcept;

private:
  std::vector<std::byte> buffer_;
};

} // namespace openmeta
