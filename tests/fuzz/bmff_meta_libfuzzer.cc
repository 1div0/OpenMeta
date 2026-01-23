#include "openmeta/container_scan.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <vector>

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
append_u16be(std::vector<std::byte>* out, uint16_t v)
{
    out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
    out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
}

static void
append_u32be(std::vector<std::byte>* out, uint32_t v)
{
    out->push_back(std::byte { static_cast<uint8_t>((v >> 24) & 0xFF) });
    out->push_back(std::byte { static_cast<uint8_t>((v >> 16) & 0xFF) });
    out->push_back(std::byte { static_cast<uint8_t>((v >> 8) & 0xFF) });
    out->push_back(std::byte { static_cast<uint8_t>((v >> 0) & 0xFF) });
}

static void
append_fourcc(std::vector<std::byte>* out, uint32_t f)
{
    append_u32be(out, f);
}

static void
append_fullbox_header(std::vector<std::byte>* out, uint8_t version)
{
    out->push_back(std::byte { version });
    out->push_back(std::byte { 0x00 });
    out->push_back(std::byte { 0x00 });
    out->push_back(std::byte { 0x00 });
}

static void
append_bmff_box(std::vector<std::byte>* out, uint32_t type,
                std::span<const std::byte> payload)
{
    append_u32be(out, static_cast<uint32_t>(8 + payload.size()));
    append_fourcc(out, type);
    out->insert(out->end(), payload.begin(), payload.end());
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

    std::vector<std::byte> idat_payload;
    // Prefix with a plausible BMFF Exif TIFF offset.
    append_u32be(&idat_payload, 4);
    idat_payload.insert(idat_payload.end(),
                        reinterpret_cast<const std::byte*>(data),
                        reinterpret_cast<const std::byte*>(data + size));

    std::vector<std::byte> infe_payload;
    append_fullbox_header(&infe_payload, 2);
    append_u16be(&infe_payload, 1);  // item_ID
    append_u16be(&infe_payload, 0);  // protection
    append_fourcc(&infe_payload, fourcc('E', 'x', 'i', 'f'));
    infe_payload.push_back(std::byte { 'e' });
    infe_payload.push_back(std::byte { 'x' });
    infe_payload.push_back(std::byte { 'i' });
    infe_payload.push_back(std::byte { 'f' });
    infe_payload.push_back(std::byte { 0x00 });

    std::vector<std::byte> infe_box;
    append_bmff_box(&infe_box, fourcc('i', 'n', 'f', 'e'), infe_payload);

    std::vector<std::byte> iinf_payload;
    append_fullbox_header(&iinf_payload, 2);
    append_u32be(&iinf_payload, 1);  // entry_count
    iinf_payload.insert(iinf_payload.end(), infe_box.begin(), infe_box.end());

    std::vector<std::byte> iinf_box;
    append_bmff_box(&iinf_box, fourcc('i', 'i', 'n', 'f'), iinf_payload);

    std::vector<std::byte> iloc_payload;
    append_fullbox_header(&iloc_payload, 1);
    iloc_payload.push_back(std::byte { 0x44 });  // off_size=4, len_size=4
    iloc_payload.push_back(std::byte { 0x00 });  // base=0, idx=0
    append_u16be(&iloc_payload, 1);              // item_count
    append_u16be(&iloc_payload, 1);              // item_ID
    append_u16be(&iloc_payload, 1);              // construction_method=1 (idat)
    append_u16be(&iloc_payload, 0);              // data_reference_index
    append_u16be(&iloc_payload, 1);              // extent_count
    append_u32be(&iloc_payload, 0);              // extent_offset
    append_u32be(&iloc_payload, static_cast<uint32_t>(idat_payload.size()));

    std::vector<std::byte> iloc_box;
    append_bmff_box(&iloc_box, fourcc('i', 'l', 'o', 'c'), iloc_payload);

    std::vector<std::byte> idat_box;
    append_bmff_box(&idat_box, fourcc('i', 'd', 'a', 't'), idat_payload);

    std::vector<std::byte> meta_payload;
    append_fullbox_header(&meta_payload, 0);
    meta_payload.insert(meta_payload.end(), iinf_box.begin(), iinf_box.end());
    meta_payload.insert(meta_payload.end(), iloc_box.begin(), iloc_box.end());
    meta_payload.insert(meta_payload.end(), idat_box.begin(), idat_box.end());

    std::vector<std::byte> meta_box;
    append_bmff_box(&meta_box, fourcc('m', 'e', 't', 'a'), meta_payload);

    std::vector<std::byte> ftyp_payload;
    append_fourcc(&ftyp_payload, fourcc('h', 'e', 'i', 'c'));
    append_u32be(&ftyp_payload, 0);
    append_fourcc(&ftyp_payload, fourcc('m', 'i', 'f', '1'));

    std::vector<std::byte> file;
    append_bmff_box(&file, fourcc('f', 't', 'y', 'p'), ftyp_payload);
    file.insert(file.end(), meta_box.begin(), meta_box.end());

    ContainerBlockRef blocks_buf[64] = {};
    const std::span<ContainerBlockRef> blocks(blocks_buf, 64);

    const ScanResult res = scan_bmff(file, blocks);
    if (res.written > 64U) {
        fuzz_trap();
    }

    verify_ranges(file,
                  std::span<const ContainerBlockRef>(blocks_buf, res.written));
    return 0;
}
