#pragma once

#include "openmeta/interop_export.h"
#include "openmeta/meta_value.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace openmeta::interop_internal {

enum class SafeTextStatus : uint8_t {
    Ok,
    Empty,
    Error,
};

void
set_safety_error(InteropSafetyError* error, InteropSafetyReason reason,
                 std::string_view field_name, std::string_view key_path,
                 std::string_view message) noexcept;

SafeTextStatus
decode_text_to_utf8_safe(std::span<const std::byte> bytes,
                         TextEncoding encoding, std::string_view field_name,
                         std::string_view key_path, std::string* out,
                         InteropSafetyError* error) noexcept;

std::string
format_safety_error_message(const InteropSafetyError& error);

}  // namespace openmeta::interop_internal
