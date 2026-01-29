#include "openmeta/container_scan.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

namespace openmeta {

[[noreturn]] static void
fuzz_trap() noexcept
{
#if defined(__clang__) || defined(__GNUC__)
    __builtin_trap();
#else
    std::abort();
#endif
}


static void
verify_ranges(std::span<const std::byte> bytes,
              std::span<const ContainerBlockRef> blocks) noexcept
{
    const uint64_t size = static_cast<uint64_t>(bytes.size());
    for (size_t i = 0; i < blocks.size(); ++i) {
        const ContainerBlockRef& b = blocks[i];
        if (b.outer_offset > size || b.outer_size > size
            || b.outer_offset + b.outer_size > size) {
            fuzz_trap();
        }
        if (b.data_offset > size || b.data_size > size
            || b.data_offset + b.data_size > size) {
            fuzz_trap();
        }
        if (b.data_offset < b.outer_offset) {
            fuzz_trap();
        }
        if (b.data_offset + b.data_size > b.outer_offset + b.outer_size) {
            fuzz_trap();
        }
    }
}

}  // namespace openmeta

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    using namespace openmeta;

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               data),
                                           size);

    ContainerBlockRef blocks_buf[64] = {};
    const std::span<ContainerBlockRef> blocks(blocks_buf, 64);

    const ScanResult res = scan_auto(bytes, blocks);
    if (res.written > 64U) {
        fuzz_trap();
    }
    verify_ranges(bytes,
                  std::span<const ContainerBlockRef>(blocks_buf, res.written));
    return 0;
}
