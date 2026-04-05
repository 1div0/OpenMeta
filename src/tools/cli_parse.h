// SPDX-License-Identifier: Apache-2.0

#ifndef OPENMETA_TOOLS_CLI_PARSE_H
#define OPENMETA_TOOLS_CLI_PARSE_H

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace openmeta {
namespace tool_parse {

    inline bool parse_decimal_u64(const char* s, uint64_t* out) noexcept
    {
        if (!s || !*s || !out) {
            return false;
        }
        if (s[0] < '0' || s[0] > '9') {
            return false;
        }

        errno                = 0;
        char* end            = nullptr;
        unsigned long long v = std::strtoull(s, &end, 10);
        if (!end || *end != '\0' || errno == ERANGE) {
            return false;
        }
        if (v > static_cast<unsigned long long>(
                (std::numeric_limits<uint64_t>::max)())) {
            return false;
        }
        *out = static_cast<uint64_t>(v);
        return true;
    }


    inline bool parse_decimal_u32(const char* s, uint32_t* out) noexcept
    {
        uint64_t v = 0;
        if (!parse_decimal_u64(s, &v)) {
            return false;
        }
        if (v > static_cast<uint64_t>((std::numeric_limits<uint32_t>::max)())) {
            return false;
        }
        *out = static_cast<uint32_t>(v);
        return true;
    }

}  // namespace tool_parse
}  // namespace openmeta

#endif
