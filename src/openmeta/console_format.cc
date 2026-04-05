// SPDX-License-Identifier: Apache-2.0

#include "openmeta/console_format.h"

#include <cstdio>

namespace openmeta {
namespace {

    static size_t capped_count(size_t size, uint32_t max_bytes) noexcept
    {
        if (max_bytes != 0U && size > static_cast<size_t>(max_bytes)) {
            return static_cast<size_t>(max_bytes);
        }
        return size;
    }


    static bool checked_add_size(size_t a, size_t b, size_t* out) noexcept
    {
        if (!out || b > (SIZE_MAX - a)) {
            return false;
        }
        *out = a + b;
        return true;
    }


    static bool checked_mul2_size(size_t value, size_t* out) noexcept
    {
        if (!out || value > (SIZE_MAX / 2U)) {
            return false;
        }
        *out = value * 2U;
        return true;
    }


    static bool reserve_extra(std::string* out, size_t extra) noexcept
    {
        if (!out) {
            return false;
        }
        size_t total = 0;
        if (!checked_add_size(out->size(), extra, &total)) {
            return false;
        }
        out->reserve(total);
        return true;
    }

}  // namespace

bool
append_console_escaped_ascii(std::string_view s, uint32_t max_bytes,
                             std::string* out) noexcept
{
    bool dangerous = false;
    const size_t n = capped_count(s.size(), max_bytes);

    if (!reserve_extra(out, n)) {
        return true;
    }
    for (size_t i = 0; i < n; ++i) {
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
    const size_t n       = capped_count(bytes.size(), max_bytes);
    size_t reserve_bytes = 0;
    if (!checked_mul2_size(n, &reserve_bytes)
        || !reserve_extra(out, reserve_bytes)) {
        return;
    }

    for (size_t i = 0; i < n; ++i) {
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
