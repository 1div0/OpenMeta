#include "openmeta/console_format.h"

#include <cstdio>

namespace openmeta {

bool
append_console_escaped_ascii(std::string_view s, uint32_t max_bytes,
                             std::string* out) noexcept
{
    bool dangerous   = false;
    const uint32_t n = (max_bytes == 0U || s.size() < max_bytes)
                           ? static_cast<uint32_t>(s.size())
                           : max_bytes;

    out->reserve(out->size() + static_cast<size_t>(n));
    for (uint32_t i = 0; i < n; ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (c == '\\' || c == '"') {
            out->push_back('\\');
            out->push_back(static_cast<char>(c));
            continue;
        }
        if (c == '\n') {
            out->append("\\n");
            dangerous = true;
            continue;
        }
        if (c == '\r') {
            out->append("\\r");
            dangerous = true;
            continue;
        }
        if (c == '\t') {
            out->append("\\t");
            dangerous = true;
            continue;
        }
        if (c < 0x20U || c == 0x7FU || c >= 0x80U) {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "\\x%02X",
                          static_cast<unsigned>(c));
            out->append(buf);
            dangerous = true;
            continue;
        }
        out->push_back(static_cast<char>(c));
    }
    if (n < s.size()) {
        out->append("...");
        dangerous = true;
    }
    return dangerous;
}

void
append_hex_bytes(std::span<const std::byte> bytes, uint32_t max_bytes,
                 std::string* out) noexcept
{
    const uint32_t n = (max_bytes == 0U || bytes.size() < max_bytes)
                           ? static_cast<uint32_t>(bytes.size())
                           : max_bytes;

    out->reserve(out->size() + static_cast<size_t>(n) * 2U);
    for (uint32_t i = 0; i < n; ++i) {
        const unsigned v = static_cast<unsigned>(
            static_cast<uint8_t>(bytes[i]));
        char buf[4];
        std::snprintf(buf, sizeof(buf), "%02X", v);
        out->append(buf);
    }
    if (n < bytes.size()) {
        out->append("...");
    }
}

}  // namespace openmeta

