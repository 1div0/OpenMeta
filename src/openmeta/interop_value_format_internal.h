#pragma once

#include "openmeta/meta_value.h"

#include <cstdint>
#include <string>

namespace openmeta::interop_internal {

bool
format_value_for_text(const ByteArena& arena, const MetaValue& value,
                      uint32_t max_value_bytes, std::string* out) noexcept;

}  // namespace openmeta::interop_internal
