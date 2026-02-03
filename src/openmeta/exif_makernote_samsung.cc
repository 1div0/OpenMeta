#include "exif_tiff_decode_internal.h"

#include <cstdint>
#include <cstring>

namespace openmeta::exif_internal {

namespace {

    static bool read_u32le_at(std::span<const std::byte> bytes, uint64_t off,
                              uint32_t* out) noexcept
    {
        return read_u32le(bytes, off, out);
    }


    static bool read_u16le_at(std::span<const std::byte> bytes, uint64_t off,
                              uint16_t* out) noexcept
    {
        return read_u16le(bytes, off, out);
    }


    static void samsung_add_entry(std::string_view ifd_name, uint16_t tag,
                                  MetaValue value, BlockId block,
                                  uint32_t order_in_block, MetaStore& store,
                                  const ExifDecodeLimits& limits,
                                  ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty() || block == kInvalidBlockId) {
            return;
        }
        if (status_out
            && (status_out->entries_decoded + 1U) > limits.max_total_entries) {
            update_status(status_out, ExifDecodeStatus::LimitExceeded);
            return;
        }

        Entry e;
        e.key                   = make_exif_tag_key(store.arena(), ifd_name, tag);
        e.origin.block           = block;
        e.origin.order_in_block  = order_in_block;
        e.origin.wire_type       = WireType { WireFamily::Other, 0 };
        e.origin.wire_count      = value.count;
        e.value                  = value;

        (void)store.add_entry(e);
        if (status_out) {
            status_out->entries_decoded += 1;
        }
    }


    static bool decode_samsung_ifd(std::span<const std::byte> maker_note_bytes,
                                   uint64_t ifd_off,
                                   std::string_view ifd_name, MetaStore& store,
                                   const ExifDecodeOptions& options,
                                   ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty()) {
            return false;
        }
        if (ifd_off > maker_note_bytes.size()) {
            return false;
        }
        if (ifd_off + 4U > maker_note_bytes.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }

        uint32_t entry_count32 = 0;
        if (!read_u32le_at(maker_note_bytes, ifd_off, &entry_count32)
            || entry_count32 == 0U) {
            return true;
        }

        const uint64_t entry_count = entry_count32;
        if (entry_count > options.limits.max_entries_per_ifd) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            return true;
        }

        const uint64_t entries_off = ifd_off + 4U;
        const uint64_t entry_size  = 12U;
        const uint64_t next_off_pos
            = entries_off + entry_count * entry_size;
        if (next_off_pos + 4U > maker_note_bytes.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }

        const uint64_t base = next_off_pos + 4U;
        if (base > maker_note_bytes.size()) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
            return true;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return true;
        }

        // SamsungIFD: uses little-endian entries; out-of-line value offsets are
        // relative to the end of the IFD (base).
        TiffConfig cfg;
        cfg.le      = true;
        cfg.bigtiff = false;

        for (uint64_t i = 0; i < entry_count; ++i) {
            const uint64_t eoff = entries_off + i * entry_size;

            uint16_t tag  = 0;
            uint16_t type = 0;
            if (!read_u16le_at(maker_note_bytes, eoff + 0, &tag)
                || !read_u16le_at(maker_note_bytes, eoff + 2, &type)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return true;
            }

            uint32_t count32        = 0;
            uint32_t value_or_off32 = 0;
            if (!read_u32le_at(maker_note_bytes, eoff + 4, &count32)
                || !read_u32le_at(maker_note_bytes, eoff + 8, &value_or_off32)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return true;
            }
            const uint64_t count = count32;

            const uint64_t unit = tiff_type_size(type);
            if (unit == 0) {
                continue;
            }
            if (count > (UINT64_MAX / unit)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                continue;
            }
            const uint64_t value_bytes = count * unit;

            const uint64_t inline_cap = 4;
            const bool inline_value   = (value_bytes <= inline_cap);

            uint64_t value_off = 0;
            if (inline_value) {
                value_off = eoff + 8U;
            } else {
                value_off = base + static_cast<uint64_t>(value_or_off32);
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = static_cast<uint32_t>(i);
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type };
            entry.origin.wire_count     = count32;

            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return true;
            }

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
                entry.value = decode_tiff_value(cfg, maker_note_bytes, type,
                                                count, value_off, value_bytes,
                                                store.arena(), options.limits,
                                                status_out);
            }

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }

        return true;
    }


    static void decode_samsung_picturewizard(std::string_view mk_type2_ifd0,
                                             bool le, MetaStore& store,
                                             const ExifDecodeOptions& options,
                                             ExifDecodeResult* status_out) noexcept
    {
        if (mk_type2_ifd0.empty()) {
            return;
        }

        char scratch[64];
        const std::string_view pw_ifd
            = make_mk_subtable_ifd_token(options.tokens.ifd_prefix,
                                         "picturewizard", 0,
                                         std::span<char>(scratch));
        if (pw_ifd.empty()) {
            return;
        }

        const ByteArena& arena               = store.arena();
        const std::span<const Entry> entries = store.entries();

        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (e.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            if (e.key.data.exif_tag.tag != 0x0021) {  // PictureWizard
                continue;
            }
            if (arena_string(arena, e.key.data.exif_tag.ifd) != mk_type2_ifd0) {
                continue;
            }
            if (e.value.kind != MetaValueKind::Bytes
                && !(e.value.kind == MetaValueKind::Array
                     && (e.value.elem_type == MetaElementType::U8
                         || e.value.elem_type == MetaElementType::U16))) {
                continue;
            }

            const std::span<const std::byte> raw = arena.span(e.value.data.span);
            if (raw.size() < 10U) {
                continue;
            }

            uint16_t v[5];
            if (e.value.kind == MetaValueKind::Array
                && e.value.elem_type == MetaElementType::U16
                && e.value.count >= 5U) {
                // Already decoded from the TIFF directory into native u16s.
                for (uint32_t j = 0; j < 5; ++j) {
                    uint16_t vv = 0;
                    std::memcpy(&vv, raw.data() + j * 2U, 2U);
                    v[j] = vv;
                }
            } else {
                for (uint32_t j = 0; j < 5; ++j) {
                    uint16_t vv = 0;
                    if (!read_u16_endian(le, raw, uint64_t(j) * 2ULL, &vv)) {
                        return;
                    }
                    v[j] = vv;
                }
            }

            const uint16_t tags_out[5] = { 0x0000, 0x0001, 0x0002, 0x0003,
                                           0x0004 };
            const MetaValue vals_out[5] = { make_u16(v[0]), make_u16(v[1]),
                                            make_u16(v[2]), make_u16(v[3]),
                                            make_u16(v[4]) };
            emit_bin_dir_entries(pw_ifd, store,
                                 std::span<const uint16_t>(tags_out, 5),
                                 std::span<const MetaValue>(vals_out, 5),
                                 options.limits, status_out);
        }
    }


    static bool decode_samsung_stmn(std::span<const std::byte> maker_note_bytes,
                                    std::string_view mk_ifd0, MetaStore& store,
                                    const ExifDecodeOptions& options,
                                    ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty()) {
            return false;
        }
        if (maker_note_bytes.size() < 16U) {
            return false;
        }
        if (!match_bytes(maker_note_bytes, 0, "STMN", 4)) {
            return false;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return true;
        }

        // STMN main block:
        // - tag 0: MakerNoteVersion (8 bytes)
        // - tag 2: PreviewImageStart (u32le)
        // - tag 3: PreviewImageLength (u32le)
        {
            const MetaValue v = make_fixed_ascii_text(
                store.arena(), maker_note_bytes.subspan(0, 8));
            samsung_add_entry(mk_ifd0, 0x0000, v, block, 0, store,
                              options.limits, status_out);
        }
        {
            uint32_t off = 0;
            if (read_u32le_at(maker_note_bytes, 8, &off)) {
                samsung_add_entry(mk_ifd0, 0x0002, make_u32(off), block, 1,
                                  store, options.limits, status_out);
            } else if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
        }
        {
            uint32_t len = 0;
            if (read_u32le_at(maker_note_bytes, 12, &len)) {
                samsung_add_entry(mk_ifd0, 0x0003, make_u32(len), block, 2,
                                  store, options.limits, status_out);
            } else if (status_out) {
                update_status(status_out, ExifDecodeStatus::Malformed);
            }
        }

        // Some Samsung models store an embedded SamsungIFD at +44.
        if (maker_note_bytes.size() >= 48U && u8(maker_note_bytes[44]) != 0U
            && u8(maker_note_bytes[45]) == 0U && u8(maker_note_bytes[46]) == 0U
            && u8(maker_note_bytes[47]) == 0U) {
            char scratch[64];
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(options.tokens.ifd_prefix, "ifd", 0,
                                             std::span<char>(scratch));
            (void)decode_samsung_ifd(maker_note_bytes, 44U, ifd_name, store,
                                     options, status_out);
        }

        return true;
    }

}  // namespace

bool decode_samsung_makernote(const TiffConfig& parent_cfg,
                              std::span<const std::byte> tiff_bytes,
                              uint64_t maker_note_off,
                              uint64_t maker_note_bytes,
                              std::string_view mk_ifd0, MetaStore& store,
                              const ExifDecodeOptions& options,
                              ExifDecodeResult* status_out) noexcept
{
    if (mk_ifd0.empty()) {
        return false;
    }
    if (maker_note_off > tiff_bytes.size()) {
        return false;
    }
    if (maker_note_bytes > (tiff_bytes.size() - maker_note_off)) {
        return false;
    }

    const std::span<const std::byte> mn
        = tiff_bytes.subspan(static_cast<size_t>(maker_note_off),
                             static_cast<size_t>(maker_note_bytes));

    // 1) Samsung STMN MakerNote (fixed-layout binary).
    if (decode_samsung_stmn(mn, mk_ifd0, store, options, status_out)) {
        return true;
    }

    // 2) Samsung Type2 MakerNote (classic TIFF-IFD without a header).
    TiffConfig mn_cfg = parent_cfg;
    mn_cfg.bigtiff    = false;
    if (!looks_like_classic_ifd(mn_cfg, mn, 0, options.limits)) {
        mn_cfg.le = !mn_cfg.le;
    }
    if (!looks_like_classic_ifd(mn_cfg, mn, 0, options.limits)) {
        return false;
    }

    char scratch[64];
    const std::string_view mk_type2_ifd0
        = make_mk_subtable_ifd_token(options.tokens.ifd_prefix, "type2", 0,
                                     std::span<char>(scratch));
    if (mk_type2_ifd0.empty()) {
        return true;
    }

    decode_classic_ifd_no_header(mn_cfg, mn, 0, mk_type2_ifd0, store, options,
                                 status_out, EntryFlags::None);
    decode_samsung_picturewizard(mk_type2_ifd0, mn_cfg.le, store, options,
                                 status_out);
    return true;
}

}  // namespace openmeta::exif_internal
