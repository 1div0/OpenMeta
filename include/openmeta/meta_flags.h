#pragma once

#include <cstdint>

namespace openmeta {

enum class EntryFlags : uint8_t {
    None    = 0,
    Deleted = 1U << 0U,
    Dirty   = 1U << 1U,
    Derived = 1U << 2U,
};

constexpr EntryFlags
operator|(EntryFlags a, EntryFlags b) noexcept
{
    return static_cast<EntryFlags>(static_cast<uint8_t>(a)
                                   | static_cast<uint8_t>(b));
}

constexpr EntryFlags
operator&(EntryFlags a, EntryFlags b) noexcept
{
    return static_cast<EntryFlags>(static_cast<uint8_t>(a)
                                   & static_cast<uint8_t>(b));
}

constexpr EntryFlags&
operator|=(EntryFlags& a, EntryFlags b) noexcept
{
    a = a | b;
    return a;
}

constexpr bool
any(EntryFlags flags, EntryFlags test) noexcept
{
    return static_cast<uint8_t>(flags & test) != 0;
}

}  // namespace openmeta
