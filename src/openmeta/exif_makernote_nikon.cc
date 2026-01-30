#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>

namespace openmeta::exif_internal {

    static void decode_nikon_bin_dir_entries(
        std::string_view ifd_name, MetaStore& store,
        std::span<const uint16_t> tags, std::span<const MetaValue> values,
        const ExifDecodeLimits& limits, ExifDecodeResult* status_out) noexcept
    {
        emit_bin_dir_entries(ifd_name, store, tags, values, limits, status_out);
    }


    static void decode_nikon_settings_dir(std::string_view ifd_name,
                                          std::span<const std::byte> raw,
                                          MetaStore& store,
                                          const ExifDecodeOptions& options,
                                          ExifDecodeResult* status_out) noexcept
    {
        if (ifd_name.empty()) {
            return;
        }
        if (raw.size() < 24) {
            return;
        }
        if ((raw.size() % 8U) != 0U) {
            return;
        }

        uint32_t rec_count = 0;
        if (!read_u32le(raw, 20, &rec_count)) {
            return;
        }
        if (rec_count == 0) {
            return;
        }
        if (rec_count > options.limits.max_entries_per_ifd) {
            if (status_out) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
            }
            return;
        }

        const uint64_t rec_bytes = uint64_t(rec_count) * 8ULL;
        if (rec_bytes > (raw.size() - 24ULL)) {
            return;
        }
        if (24ULL + rec_bytes != raw.size()) {
            return;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        for (uint32_t i = 0; i < rec_count; ++i) {
            if (status_out
                && (status_out->entries_decoded + 1U)
                       > options.limits.max_total_entries) {
                update_status(status_out, ExifDecodeStatus::LimitExceeded);
                return;
            }

            const uint64_t off = 24ULL + uint64_t(i) * 8ULL;
            uint16_t tag       = 0;
            uint16_t type_be   = 0;
            uint32_t val32     = 0;
            if (!read_u16le(raw, off + 0, &tag)
                || !read_u16be(raw, off + 2, &type_be)
                || !read_u32le(raw, off + 4, &val32)) {
                if (status_out) {
                    update_status(status_out, ExifDecodeStatus::Malformed);
                }
                return;
            }

            Entry entry;
            entry.key = make_exif_tag_key(store.arena(), ifd_name, tag);
            entry.origin.block          = block;
            entry.origin.order_in_block = i;
            entry.origin.wire_type      = WireType { WireFamily::Tiff, type_be };
            entry.origin.wire_count     = 1;
            entry.flags |= EntryFlags::Derived;

            switch (type_be) {
            case 1: entry.value = make_u8(static_cast<uint8_t>(val32)); break;
            case 3: entry.value = make_u16(static_cast<uint16_t>(val32)); break;
            case 4: entry.value = make_u32(val32); break;
            case 8:
                entry.value = make_i16(static_cast<int16_t>(
                    static_cast<uint16_t>(val32)));
                break;
            case 9: entry.value = make_i32(static_cast<int32_t>(val32)); break;
            default: entry.value = make_u32(val32); break;
            }

            (void)store.add_entry(entry);
            if (status_out) {
                status_out->entries_decoded += 1;
            }
        }
    }


void decode_nikon_binary_subdirs(std::string_view mk_ifd0, MetaStore& store,
                                 bool le, const ExifDecodeOptions& options,
                                 ExifDecodeResult* status_out) noexcept
    {
        if (mk_ifd0.empty()) {
            return;
        }

        struct Candidate final {
            uint16_t tag = 0;
            MetaValue value;
        };

        Candidate cands[32];
        uint32_t cand_count = 0;

        const ByteArena& arena               = store.arena();
        const std::span<const Entry> entries = store.entries();

        for (size_t i = 0; i < entries.size(); ++i) {
            const Entry& e = entries[i];
            if (e.key.kind != MetaKeyKind::ExifTag) {
                continue;
            }
            if (arena_string(arena, e.key.data.exif_tag.ifd) != mk_ifd0) {
                continue;
            }
            if (e.value.kind != MetaValueKind::Bytes) {
                continue;
            }
            const uint16_t tag = e.key.data.exif_tag.tag;
            switch (tag) {
            case 0x001F:  // VRInfo
            case 0x0023:  // PictureControlData
            case 0x00BD:  // PictureControlData (alt)
            case 0x0024:  // WorldTime
            case 0x0025:  // ISOInfo
            case 0x002C:  // UnknownInfo
            case 0x0032:  // UnknownInfo2
            case 0x004E:  // NikonSettings
            case 0x0091:  // ShotInfoUnknown
            case 0x0097:  // ColorBalanceUnknown2
            case 0x0098:  // LensData
            case 0x00B7:  // AFInfo2
            case 0x00B9:  // AFTune
                break;
            default: continue;
            }
            if (cand_count < sizeof(cands) / sizeof(cands[0])) {
                cands[cand_count].tag   = tag;
                cands[cand_count].value = e.value;
                cand_count += 1;
            }
        }

        char sub_ifd_buf[96];

        uint32_t idx_vrinfo         = 0;
        uint32_t idx_picturecontrol = 0;
        uint32_t idx_worldtime      = 0;
        uint32_t idx_isoinfo        = 0;
        uint32_t idx_unknowninfo    = 0;
        uint32_t idx_unknowninfo2   = 0;
        uint32_t idx_settings       = 0;
        uint32_t idx_shotinfo       = 0;
        uint32_t idx_colorbalance   = 0;
        uint32_t idx_lensdata       = 0;
        uint32_t idx_afinfo2        = 0;
        uint32_t idx_aftune         = 0;

        for (uint32_t i = 0; i < cand_count; ++i) {
            const uint16_t tag                       = cands[i].tag;
            const ByteSpan raw_span                  = cands[i].value.data.span;
            const std::span<const std::byte> raw_src = store.arena().span(
                raw_span);
            if (raw_src.empty()) {
                continue;
            }

            const std::string_view mk_prefix = "mk_nikon";

            if (tag == 0x001F) {  // VRInfo
                if (raw_src.size() < 7) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "vrinfo",
                                                 idx_vrinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 8> raw;
                const size_t n = (raw_src.size() < raw.size()) ? raw_src.size()
                                                               : raw.size();
                std::memcpy(raw.data(), raw_src.data(), n);

                uint8_t vr_enabled = 0;
                uint8_t vr_mode    = 0;
                if (n > 4) {
                    vr_enabled = u8(raw[4]);
                }
                if (n > 6) {
                    vr_mode = u8(raw[6]);
                }

                const uint16_t tags_out[]  = { 0x0000, 0x0004, 0x0006 };
                const MetaValue vals_out[] = {
                    make_fixed_ascii_text(store.arena(),
                                          std::span<const std::byte>(raw.data(),
                                                                     4)),
                    make_u8(vr_enabled),
                    make_u8(vr_mode),
                };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x0023 || tag == 0x00BD) {  // PictureControlData
                if (raw_src.size() < 52) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "picturecontrol",
                                                 idx_picturecontrol++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 58> raw;
                const size_t n = (raw_src.size() < raw.size()) ? raw_src.size()
                                                               : raw.size();
                std::memcpy(raw.data(), raw_src.data(), n);

                const MetaValue version = make_fixed_ascii_text(
                    store.arena(), std::span<const std::byte>(raw.data(), 4));
                const MetaValue name = make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(raw.data() + 4, 20));
                const MetaValue base = make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(raw.data() + 24, 20));

                const uint8_t adjust       = (n > 48) ? u8(raw[48]) : 0;
                const uint8_t quick_adjust = (n > 49) ? u8(raw[49]) : 0;

                MetaValue extras[8];
                uint16_t extra_tags[8];
                uint32_t extra_count = 0;
                for (uint16_t t = 0x0032; t <= 0x0039; ++t) {
                    const uint64_t off = t;
                    if (off >= n) {
                        break;
                    }
                    extra_tags[extra_count] = t;
                    extras[extra_count]     = make_u8(u8(raw[off]));
                    extra_count += 1;
                    if (extra_count == 8) {
                        break;
                    }
                }

                uint16_t tags_out[13];
                MetaValue vals_out[13];
                uint32_t out_count = 0;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = version;
                out_count += 1;
                tags_out[out_count] = 0x0004;
                vals_out[out_count] = name;
                out_count += 1;
                tags_out[out_count] = 0x0018;
                vals_out[out_count] = base;
                out_count += 1;
                tags_out[out_count] = 0x0030;
                vals_out[out_count] = make_u8(adjust);
                out_count += 1;
                tags_out[out_count] = 0x0031;
                vals_out[out_count] = make_u8(quick_adjust);
                out_count += 1;

                for (uint32_t k = 0; k < extra_count; ++k) {
                    tags_out[out_count] = extra_tags[k];
                    vals_out[out_count] = extras[k];
                    out_count += 1;
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            if (tag == 0x0024) {  // WorldTime
                if (raw_src.size() < 4) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "worldtime",
                                                 idx_worldtime++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 4> raw;
                std::memcpy(raw.data(), raw_src.data(), raw.size());

                int16_t tz = 0;
                if (!read_i16_endian(le, raw, 0, &tz)) {
                    continue;
                }
                const uint8_t dst = u8(raw[2]);
                const uint8_t fmt = u8(raw[3]);

                const uint16_t tags_out[]  = { 0x0000, 0x0002, 0x0003 };
                const MetaValue vals_out[] = { make_i16(tz), make_u8(dst),
                                               make_u8(fmt) };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x0025) {  // ISOInfo
                if (raw_src.size() < 12) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "isoinfo",
                                                 idx_isoinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 14> raw;
                const size_t n = (raw_src.size() < raw.size()) ? raw_src.size()
                                                               : raw.size();
                std::memcpy(raw.data(), raw_src.data(), n);

                uint16_t iso_expansion  = 0;
                uint16_t iso_expansion2 = 0;
                if (!read_u16_endian(le, raw, 4, &iso_expansion)
                    || !read_u16_endian(le, raw, 10, &iso_expansion2)) {
                    continue;
                }
                const uint8_t iso  = u8(raw[0]);
                const uint8_t iso2 = u8(raw[6]);

                const uint16_t tags_out[]  = { 0x0000, 0x0004, 0x0006, 0x000A };
                const MetaValue vals_out[] = {
                    make_u8(iso),
                    make_u16(iso_expansion),
                    make_u8(iso2),
                    make_u16(iso_expansion2),
                };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x002C) {  // UnknownInfo
                if (raw_src.size() < 4) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "unknowninfo",
                                                 idx_unknowninfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                std::array<std::byte, 4> raw;
                std::memcpy(raw.data(), raw_src.data(), raw.size());
                const uint16_t tags_out[]  = { 0x0000 };
                const MetaValue vals_out[] = { make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(raw.data(), raw.size())) };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x0032) {  // UnknownInfo2
                if (raw_src.size() < 4) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "unknowninfo2",
                                                 idx_unknowninfo2++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                std::array<std::byte, 4> raw;
                std::memcpy(raw.data(), raw_src.data(), raw.size());
                const uint16_t tags_out[]  = { 0x0000 };
                const MetaValue vals_out[] = { make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(raw.data(), raw.size())) };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x0091) {  // ShotInfoUnknown
                if (raw_src.size() < 9) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "shotinfo",
                                                 idx_shotinfo++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 9> raw;
                std::memcpy(raw.data(), raw_src.data(), raw.size());

                const uint16_t tags_out[]  = { 0x0000, 0x0004 };
                const MetaValue vals_out[] = {
                    make_fixed_ascii_text(store.arena(),
                                          std::span<const std::byte>(raw.data(),
                                                                     4)),
                    make_fixed_ascii_text(
                        store.arena(),
                        std::span<const std::byte>(raw.data() + 4, 5)),
                };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x0097) {  // ColorBalanceUnknown2
                if (raw_src.size() < 4) {
                    continue;
                }
                const std::string_view ifd_name = make_mk_subtable_ifd_token(
                    mk_prefix, "colorbalanceunknown2", idx_colorbalance++,
                    std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }
                std::array<std::byte, 4> raw;
                std::memcpy(raw.data(), raw_src.data(), raw.size());
                const uint16_t tags_out[]  = { 0x0000 };
                const MetaValue vals_out[] = { make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(raw.data(), raw.size())) };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }

            if (tag == 0x0098) {  // LensData
                if (raw_src.size() < 4) {
                    continue;
                }
                std::array<std::byte, 4> ver_bytes;
                std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());
                const std::string_view ver = std::string_view(
                    reinterpret_cast<const char*>(ver_bytes.data()),
                    ver_bytes.size());

                std::string_view subtable = "lensdataunknown";
                uint16_t lens_model_tag   = 0;
                uint64_t lens_model_off   = 0;
                uint64_t lens_model_bytes = 0;
                if (ver == "0400") {
                    subtable         = "lensdata0400";
                    lens_model_tag   = 0x018a;
                    lens_model_off   = 0x018a;
                    lens_model_bytes = 64;
                } else if (ver == "0402") {
                    subtable         = "lensdata0402";
                    lens_model_tag   = 0x018b;
                    lens_model_off   = 0x018b;
                    lens_model_bytes = 64;
                } else if (ver == "0403") {
                    subtable         = "lensdata0403";
                    lens_model_tag   = 0x02ac;
                    lens_model_off   = 0x02ac;
                    lens_model_bytes = 64;
                }

                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, subtable,
                                                 idx_lensdata++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                uint16_t tags_out[2];
                MetaValue vals_out[2];
                uint32_t out_count = 0;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(),
                    std::span<const std::byte>(ver_bytes.data(),
                                               ver_bytes.size()));
                out_count += 1;

                if (lens_model_tag != 0 && lens_model_off < raw_src.size()
                    && lens_model_off + lens_model_bytes <= raw_src.size()) {
                    std::array<std::byte, 64> model_bytes;
                    std::memcpy(model_bytes.data(),
                                raw_src.data()
                                    + static_cast<size_t>(lens_model_off),
                                model_bytes.size());
                    tags_out[out_count] = lens_model_tag;
                    vals_out[out_count] = make_fixed_ascii_text(
                        store.arena(),
                        std::span<const std::byte>(model_bytes.data(),
                                                   model_bytes.size()));
                    out_count += 1;
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            if (tag == 0x004E) {  // NikonSettings
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token("mk_nikonsettings", "main",
                                                 idx_settings++,
                                                 std::span<char>(sub_ifd_buf));
                decode_nikon_settings_dir(ifd_name, raw_src, store, options,
                                          status_out);
                continue;
            }

            if (tag == 0x00B7) {  // AFInfo2
                if (raw_src.size() < 9) {
                    continue;
                }

                std::array<std::byte, 25> raw;
                const size_t n = (raw_src.size() < raw.size()) ? raw_src.size()
                                                               : raw.size();
                std::memcpy(raw.data(), raw_src.data(), n);
                const std::string_view ver = std::string_view(
                    reinterpret_cast<const char*>(raw.data()), 4);
                std::string_view subtable = "afinfo";
                if (ver == "0200") {
                    subtable = "afinfo2v0200";
                }

                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, subtable,
                                                 idx_afinfo2++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                MetaValue points;
                if (n >= 25) {
                    points
                        = make_bytes(store.arena(),
                                     std::span<const std::byte>(raw.data() + 8,
                                                                17));
                }

                uint16_t tags_out[6];
                MetaValue vals_out[6];
                uint32_t out_count = 0;

                tags_out[out_count] = 0x0000;
                vals_out[out_count] = make_fixed_ascii_text(
                    store.arena(), std::span<const std::byte>(raw.data(), 4));
                out_count += 1;
                tags_out[out_count] = 0x0004;
                vals_out[out_count] = make_u8((n > 4) ? u8(raw[4]) : 0);
                out_count += 1;
                tags_out[out_count] = 0x0005;
                vals_out[out_count] = make_u8((n > 5) ? u8(raw[5]) : 0);
                out_count += 1;
                tags_out[out_count] = 0x0006;
                vals_out[out_count] = make_u8((n > 6) ? u8(raw[6]) : 0);
                out_count += 1;
                tags_out[out_count] = 0x0007;
                vals_out[out_count] = make_u8((n > 7) ? u8(raw[7]) : 0);
                out_count += 1;

                if (points.kind != MetaValueKind::Empty) {
                    tags_out[out_count] = 0x0008;
                    vals_out[out_count] = points;
                    out_count += 1;
                }

                decode_nikon_bin_dir_entries(
                    ifd_name, store,
                    std::span<const uint16_t>(tags_out, out_count),
                    std::span<const MetaValue>(vals_out, out_count),
                    options.limits, status_out);
                continue;
            }

            if (tag == 0x00B9) {  // AFTune
                if (raw_src.size() < 4) {
                    continue;
                }
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "aftune",
                                                 idx_aftune++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 4> raw;
                std::memcpy(raw.data(), raw_src.data(), raw.size());

                const uint16_t tags_out[]  = { 0x0000, 0x0001, 0x0002, 0x0003 };
                const MetaValue vals_out[] = {
                    make_u8(u8(raw[0])),
                    make_u8(u8(raw[1])),
                    make_i8(static_cast<int8_t>(u8(raw[2]))),
                    make_i8(static_cast<int8_t>(u8(raw[3]))),
                };
                decode_nikon_bin_dir_entries(
                    ifd_name, store, std::span<const uint16_t>(tags_out),
                    std::span<const MetaValue>(vals_out), options.limits,
                    status_out);
                continue;
            }
        }
    }

}  // namespace openmeta::exif_internal
