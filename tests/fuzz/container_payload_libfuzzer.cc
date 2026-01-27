#include "openmeta/container_payload.h"
#include "openmeta/container_scan.h"

#include <cstddef>
#include <cstdint>
#include <span>

extern "C" int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    using namespace openmeta;

    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(
                                               data),
                                           size);

    ContainerBlockRef blocks_buf[64] = {};
    const std::span<ContainerBlockRef> blocks(blocks_buf, 64);
    const ScanResult scan = scan_auto(bytes, blocks);

    std::byte out_buf[4096]   = {};
    uint32_t scratch_buf[256] = {};

    PayloadOptions opts;
    opts.decompress = false;

    const uint32_t n = (scan.written < 64U) ? scan.written : 64U;
    for (uint32_t i = 0; i < n; ++i) {
        (void)extract_payload(bytes,
                              std::span<const ContainerBlockRef>(blocks_buf, n),
                              i, std::span<std::byte>(out_buf, sizeof(out_buf)),
                              std::span<uint32_t>(scratch_buf, 256), opts);
    }

    return 0;
}
