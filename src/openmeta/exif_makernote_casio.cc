#include "exif_tiff_decode_internal.h"

namespace openmeta::exif_internal {

bool decode_casio_makernote(std::span<const std::byte> maker_note_bytes,
                            std::string_view mk_ifd0, MetaStore& store,
                            const ExifDecodeOptions& options,
                            ExifDecodeResult* status_out) noexcept
{
    if (mk_ifd0.empty()) {
        return false;
    }
    if (maker_note_bytes.size() < 8) {
        return false;
    }
    if (!match_bytes(maker_note_bytes, 0, "QVC\0", 4)) {
        return false;
    }

    uint32_t entry_count32 = 0;
    if (!read_u32be(maker_note_bytes, 4, &entry_count32)) {
        return false;
    }
    const uint64_t entry_count = entry_count32;
    if (entry_count == 0 || entry_count > options.limits.max_entries_per_ifd) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return true;
    }

    const uint64_t entries_off = 8;
    if (entry_count > (UINT64_MAX / 12ULL)) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::Malformed);
        }
        return true;
    }
    const uint64_t table_bytes = entry_count * 12ULL;
    if (entries_off + table_bytes > maker_note_bytes.size()) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::Malformed);
        }
        return true;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return true;
    }

    TiffConfig cfg;
    cfg.le      = false;
    cfg.bigtiff = false;

    for (uint32_t i = 0; i < entry_count32; ++i) {
        const uint64_t eoff = entries_off + uint64_t(i) * 12ULL;

        uint16_t tag  = 0;
        uint16_t type = 0;
        if (!read_u16be(maker_note_bytes, eoff + 0, &tag)
            || !read_u16be(maker_note_bytes, eoff + 2, &type)) {
            return true;
        }

        uint32_t count32        = 0;
        uint32_t value_or_off32 = 0;
        if (!read_u32be(maker_note_bytes, eoff + 4, &count32)
            || !read_u32be(maker_note_bytes, eoff + 8, &value_or_off32)) {
            return true;
        }
        const uint64_t count = count32;

        const uint64_t unit = tiff_type_size(type);
        if (unit == 0) {
            continue;
        }
        if (count > (UINT64_MAX / unit)) {
            continue;
        }
        const uint64_t value_bytes = count * unit;
        if (value_bytes > options.limits.max_value_bytes) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            continue;
        }

        const uint64_t inline_cap      = 4;
        const uint64_t value_field_off = eoff + 8;
        const uint64_t value_off       = (value_bytes <= inline_cap)
                                             ? value_field_off
                                             : value_or_off32;

        if (value_off + value_bytes > maker_note_bytes.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            continue;
        }

        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return true;
        }

        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), mk_ifd0, tag);
        entry.origin.block          = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
        entry.origin.wire_count     = count32;
        entry.value = decode_tiff_value(cfg, maker_note_bytes, type, count,
                                        value_off, value_bytes, store.arena(),
                                        options.limits, status_out);

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }

    return true;
}

}  // namespace openmeta::exif_internal

