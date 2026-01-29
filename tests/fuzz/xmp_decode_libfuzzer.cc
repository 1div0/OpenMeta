#include "openmeta/xmp_decode.h"

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

    MetaStore store;

    XmpDecodeOptions options;
    options.limits.max_depth             = 128;
    options.limits.max_properties        = 200000;
    options.limits.max_input_bytes       = 1ULL * 1024ULL * 1024ULL;
    options.limits.max_path_bytes        = 1024;
    options.limits.max_value_bytes       = 256U * 1024U;
    options.limits.max_total_value_bytes = 4ULL * 1024ULL * 1024ULL;

    (void)decode_xmp_packet(bytes, store, EntryFlags::None, options);
    store.finalize();
    return 0;
}
