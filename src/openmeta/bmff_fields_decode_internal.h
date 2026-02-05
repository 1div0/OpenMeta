#pragma once

#include "openmeta/meta_store.h"

#include <cstddef>
#include <span>

namespace openmeta {

// Internal-only (non-installed) helpers for ISO-BMFF (HEIF/AVIF/CR3) container
// derived fields. This intentionally does not expose a public API yet; the
// values are surfaced via simple_meta_read().

namespace bmff_internal {

    void decode_bmff_derived_fields(std::span<const std::byte> file_bytes,
                                    MetaStore& store) noexcept;

}  // namespace bmff_internal

}  // namespace openmeta

