// SPDX-License-Identifier: Apache-2.0

#include "openmeta/jumbf_decode.h"

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

    JumbfDecodeOptions options;
    options.detect_c2pa                 = true;
    options.decode_cbor                 = true;
    options.verify_c2pa                 = false;
    options.limits.max_input_bytes      = 2ULL * 1024ULL * 1024ULL;
    options.limits.max_box_depth        = 64;
    options.limits.max_boxes            = 1U << 16;
    options.limits.max_entries          = 200000;
    options.limits.max_cbor_depth       = 64;
    options.limits.max_cbor_items       = 200000;
    options.limits.max_cbor_key_bytes   = 1024;
    options.limits.max_cbor_text_bytes  = 256U * 1024U;
    options.limits.max_cbor_bytes_bytes = 256U * 1024U;

    (void)decode_jumbf_payload(bytes, store, EntryFlags::None, options);
    store.finalize();

    if (size > 0U && (data[0] & 1U) != 0U) {
        MetaStore verify_store;
        JumbfDecodeOptions verify_options          = options;
        verify_options.verify_c2pa                 = true;
        verify_options.verify_backend              = C2paVerifyBackend::Auto;
        verify_options.limits.max_input_bytes      = 512ULL * 1024ULL;
        verify_options.limits.max_cbor_bytes_bytes = 64U * 1024U;
        (void)decode_jumbf_payload(bytes, verify_store, EntryFlags::None,
                                   verify_options);
        verify_store.finalize();
    }
    return 0;
}
