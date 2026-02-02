#include "exif_tiff_decode_internal.h"

namespace openmeta::exif_internal {

namespace {

static void
decode_casio_u8_table(std::string_view ifd_name,
                      std::span<const std::byte> raw, MetaStore& store,
                      const ExifDecodeOptions& options,
                      ExifDecodeResult* status_out) noexcept
{
    if (ifd_name.empty() || raw.empty()) {
        return;
    }
    if (raw.size() > options.limits.max_entries_per_ifd) {
        if (status_out) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
        }
        return;
    }

    const BlockId block = store.add_block(BlockInfo {});
    if (block == kInvalidBlockId) {
        return;
    }

    uint32_t order = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (i > 0xFFFFu) {
            break;
        }
        if (status_out
            && (status_out->entries_decoded + 1U)
                   > options.limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        Entry entry;
        entry.key = make_exif_tag_key(store.arena(), ifd_name,
                                      static_cast<uint16_t>(i));
        entry.origin.block          = block;
        entry.origin.order_in_block = order++;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, 1 };
        entry.origin.wire_count     = 1;
        entry.flags |= EntryFlags::Derived;
        entry.value = make_u8(u8(raw[i]));

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }
}

static bool casio_faceinfo1_bytes(std::span<const std::byte> raw) noexcept
{
    if (raw.size() >= 2 && u8(raw[0]) == 0x00 && u8(raw[1]) == 0x00) {
        return true;
    }
    if (raw.size() >= 5 && u8(raw[1]) == 0x02 && u8(raw[2]) == 0x80
        && u8(raw[3]) == 0x01 && u8(raw[4]) == 0xE0) {
        return true;
    }
    return false;
}

static bool casio_faceinfo2_bytes(std::span<const std::byte> raw) noexcept
{
    return raw.size() >= 2 && u8(raw[0]) == 0x02 && u8(raw[1]) == 0x01;
}

static void
decode_casio_binary_subdirs(std::string_view mk_ifd0, MetaStore& store,
                            const ExifDecodeOptions& options,
                            ExifDecodeResult* status_out) noexcept
{
    if (mk_ifd0.empty()) {
        return;
    }

    const ByteArena& arena               = store.arena();
    const std::span<const Entry> entries = store.entries();

    uint32_t idx_faceinfo1 = 0;
    uint32_t idx_faceinfo2 = 0;

    char sub_ifd_buf[96];

    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        if (e.key.kind != MetaKeyKind::ExifTag) {
            continue;
        }
        if (arena_string(arena, e.key.data.exif_tag.ifd) != mk_ifd0) {
            continue;
        }
        if (e.key.data.exif_tag.tag != 0x2089) {
            continue;
        }
        if (e.value.kind != MetaValueKind::Bytes
            && e.value.kind != MetaValueKind::Array) {
            continue;
        }

        const ByteSpan raw_span                  = e.value.data.span;
        const std::span<const std::byte> raw_src = arena.span(raw_span);
        if (raw_src.empty()) {
            continue;
        }

        const std::string_view mk_prefix = "mk_casio";

        if (casio_faceinfo1_bytes(raw_src)) {
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "faceinfo1", idx_faceinfo1++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_casio_u8_table(ifd_name, raw_src, store, options,
                                  status_out);
            continue;
        }

        if (casio_faceinfo2_bytes(raw_src)) {
            const std::string_view ifd_name = make_mk_subtable_ifd_token(
                mk_prefix, "faceinfo2", idx_faceinfo2++,
                std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            decode_casio_u8_table(ifd_name, raw_src, store, options,
                                  status_out);
            continue;
        }
    }
}

}  // namespace

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

        const uint64_t inline_cap      = 4;
        const uint64_t value_field_off = eoff + 8;
        const uint64_t value_off       = (value_bytes <= inline_cap)
                                             ? value_field_off
                                             : value_or_off32;

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

        if (value_bytes > options.limits.max_value_bytes) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            entry.flags |= EntryFlags::Truncated;
        } else if (value_off + value_bytes > maker_note_bytes.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            entry.flags |= EntryFlags::Unreadable;
        } else {
            entry.value = decode_tiff_value(cfg, maker_note_bytes, type, count,
                                            value_off, value_bytes,
                                            store.arena(), options.limits,
                                            status_out);
        }

        (void)store.add_entry(entry);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }

    decode_casio_binary_subdirs(mk_ifd0, store, options, status_out);

    return true;
}

}  // namespace openmeta::exif_internal
