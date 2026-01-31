#include "exif_tiff_decode_internal.h"

#include <array>
#include <cstring>

namespace openmeta::exif_internal {

static int8_t
nikon_to_i8(uint8_t v) noexcept
{
    // Avoid implementation-defined conversions from uint8_t -> int8_t.
    return (v <= 0x7FU) ? static_cast<int8_t>(v)
                        : static_cast<int8_t>(static_cast<int>(v) - 256);
}

static bool
nikon_parse_u32_dec(std::string_view s, uint32_t* out) noexcept
{
    if (!out) {
        return false;
    }

    size_t i = 0;
    while (i < s.size() && s[i] == ' ') {
        i += 1;
    }
    if (i == s.size()) {
        return false;
    }

    uint64_t v = 0;
    for (; i < s.size(); ++i) {
        const char c = s[i];
        if (c < '0' || c > '9') {
            return false;
        }
        v = v * 10ULL + static_cast<uint64_t>(c - '0');
        if (v > 0xFFFFFFFFULL) {
            return false;
        }
    }
    *out = static_cast<uint32_t>(v);
    return true;
}

static bool
find_first_exif_u32_value(const MetaStore& store, std::string_view ifd,
                          uint16_t tag, uint32_t* out) noexcept
{
    if (!out) {
        return false;
    }

    const ByteArena& arena               = store.arena();
    const std::span<const Entry> entries = store.entries();

    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry& e = entries[i];
        if (e.key.kind != MetaKeyKind::ExifTag) {
            continue;
        }
        if (e.key.data.exif_tag.tag != tag) {
            continue;
        }
        if (arena_string(arena, e.key.data.exif_tag.ifd) != ifd) {
            continue;
        }
        if (e.value.kind != MetaValueKind::Scalar || e.value.count != 1) {
            continue;
        }

        if (e.value.elem_type == MetaElementType::U32
            || e.value.elem_type == MetaElementType::U16
            || e.value.elem_type == MetaElementType::U8) {
            *out = static_cast<uint32_t>(e.value.data.u64);
            return true;
        }
    }
    return false;
}

static constexpr uint8_t kNikonDecryptXlat0[256] = {
    0xC1u, 0xBFu, 0x6Du, 0x0Du, 0x59u, 0xC5u, 0x13u, 0x9Du, 0x83u, 0x61u, 0x6Bu,
    0x4Fu, 0xC7u, 0x7Fu, 0x3Du, 0x3Du, 0x53u, 0x59u, 0xE3u, 0xC7u, 0xE9u, 0x2Fu,
    0x95u, 0xA7u, 0x95u, 0x1Fu, 0xDFu, 0x7Fu, 0x2Bu, 0x29u, 0xC7u, 0x0Du, 0xDFu,
    0x07u, 0xEFu, 0x71u, 0x89u, 0x3Du, 0x13u, 0x3Du, 0x3Bu, 0x13u, 0xFBu, 0x0Du,
    0x89u, 0xC1u, 0x65u, 0x1Fu, 0xB3u, 0x0Du, 0x6Bu, 0x29u, 0xE3u, 0xFBu, 0xEFu,
    0xA3u, 0x6Bu, 0x47u, 0x7Fu, 0x95u, 0x35u, 0xA7u, 0x47u, 0x4Fu, 0xC7u, 0xF1u,
    0x59u, 0x95u, 0x35u, 0x11u, 0x29u, 0x61u, 0xF1u, 0x3Du, 0xB3u, 0x2Bu, 0x0Du,
    0x43u, 0x89u, 0xC1u, 0x9Du, 0x9Du, 0x89u, 0x65u, 0xF1u, 0xE9u, 0xDFu, 0xBFu,
    0x3Du, 0x7Fu, 0x53u, 0x97u, 0xE5u, 0xE9u, 0x95u, 0x17u, 0x1Du, 0x3Du, 0x8Bu,
    0xFBu, 0xC7u, 0xE3u, 0x67u, 0xA7u, 0x07u, 0xF1u, 0x71u, 0xA7u, 0x53u, 0xB5u,
    0x29u, 0x89u, 0xE5u, 0x2Bu, 0xA7u, 0x17u, 0x29u, 0xE9u, 0x4Fu, 0xC5u, 0x65u,
    0x6Du, 0x6Bu, 0xEFu, 0x0Du, 0x89u, 0x49u, 0x2Fu, 0xB3u, 0x43u, 0x53u, 0x65u,
    0x1Du, 0x49u, 0xA3u, 0x13u, 0x89u, 0x59u, 0xEFu, 0x6Bu, 0xEFu, 0x65u, 0x1Du,
    0x0Bu, 0x59u, 0x13u, 0xE3u, 0x4Fu, 0x9Du, 0xB3u, 0x29u, 0x43u, 0x2Bu, 0x07u,
    0x1Du, 0x95u, 0x59u, 0x59u, 0x47u, 0xFBu, 0xE5u, 0xE9u, 0x61u, 0x47u, 0x2Fu,
    0x35u, 0x7Fu, 0x17u, 0x7Fu, 0xEFu, 0x7Fu, 0x95u, 0x95u, 0x71u, 0xD3u, 0xA3u,
    0x0Bu, 0x71u, 0xA3u, 0xADu, 0x0Bu, 0x3Bu, 0xB5u, 0xFBu, 0xA3u, 0xBFu, 0x4Fu,
    0x83u, 0x1Du, 0xADu, 0xE9u, 0x2Fu, 0x71u, 0x65u, 0xA3u, 0xE5u, 0x07u, 0x35u,
    0x3Du, 0x0Du, 0xB5u, 0xE9u, 0xE5u, 0x47u, 0x3Bu, 0x9Du, 0xEFu, 0x35u, 0xA3u,
    0xBFu, 0xB3u, 0xDFu, 0x53u, 0xD3u, 0x97u, 0x53u, 0x49u, 0x71u, 0x07u, 0x35u,
    0x61u, 0x71u, 0x2Fu, 0x43u, 0x2Fu, 0x11u, 0xDFu, 0x17u, 0x97u, 0xFBu, 0x95u,
    0x3Bu, 0x7Fu, 0x6Bu, 0xD3u, 0x25u, 0xBFu, 0xADu, 0xC7u, 0xC5u, 0xC5u, 0xB5u,
    0x8Bu, 0xEFu, 0x2Fu, 0xD3u, 0x07u, 0x6Bu, 0x25u, 0x49u, 0x95u, 0x25u, 0x49u,
    0x6Du, 0x71u, 0xC7u,
};

static constexpr uint8_t kNikonDecryptXlat1[256] = {
    0xA7u, 0xBCu, 0xC9u, 0xADu, 0x91u, 0xDFu, 0x85u, 0xE5u, 0xD4u, 0x78u, 0xD5u,
    0x17u, 0x46u, 0x7Cu, 0x29u, 0x4Cu, 0x4Du, 0x03u, 0xE9u, 0x25u, 0x68u, 0x11u,
    0x86u, 0xB3u, 0xBDu, 0xF7u, 0x6Fu, 0x61u, 0x22u, 0xA2u, 0x26u, 0x34u, 0x2Au,
    0xBEu, 0x1Eu, 0x46u, 0x14u, 0x68u, 0x9Du, 0x44u, 0x18u, 0xC2u, 0x40u, 0xF4u,
    0x7Eu, 0x5Fu, 0x1Bu, 0xADu, 0x0Bu, 0x94u, 0xB6u, 0x67u, 0xB4u, 0x0Bu, 0xE1u,
    0xEAu, 0x95u, 0x9Cu, 0x66u, 0xDCu, 0xE7u, 0x5Du, 0x6Cu, 0x05u, 0xDAu, 0xD5u,
    0xDFu, 0x7Au, 0xEFu, 0xF6u, 0xDBu, 0x1Fu, 0x82u, 0x4Cu, 0xC0u, 0x68u, 0x47u,
    0xA1u, 0xBDu, 0xEEu, 0x39u, 0x50u, 0x56u, 0x4Au, 0xDDu, 0xDFu, 0xA5u, 0xF8u,
    0xC6u, 0xDAu, 0xCAu, 0x90u, 0xCAu, 0x01u, 0x42u, 0x9Du, 0x8Bu, 0x0Cu, 0x73u,
    0x43u, 0x75u, 0x05u, 0x94u, 0xDEu, 0x24u, 0xB3u, 0x80u, 0x34u, 0xE5u, 0x2Cu,
    0xDCu, 0x9Bu, 0x3Fu, 0xCAu, 0x33u, 0x45u, 0xD0u, 0xDBu, 0x5Fu, 0xF5u, 0x52u,
    0xC3u, 0x21u, 0xDAu, 0xE2u, 0x22u, 0x72u, 0x6Bu, 0x3Eu, 0xD0u, 0x5Bu, 0xA8u,
    0x87u, 0x8Cu, 0x06u, 0x5Du, 0x0Fu, 0xDDu, 0x09u, 0x19u, 0x93u, 0xD0u, 0xB9u,
    0xFCu, 0x8Bu, 0x0Fu, 0x84u, 0x60u, 0x33u, 0x1Cu, 0x9Bu, 0x45u, 0xF1u, 0xF0u,
    0xA3u, 0x94u, 0x3Au, 0x12u, 0x77u, 0x33u, 0x4Du, 0x44u, 0x78u, 0x28u, 0x3Cu,
    0x9Eu, 0xFDu, 0x65u, 0x57u, 0x16u, 0x94u, 0x6Bu, 0xFBu, 0x59u, 0xD0u, 0xC8u,
    0x22u, 0x36u, 0xDBu, 0xD2u, 0x63u, 0x98u, 0x43u, 0xA1u, 0x04u, 0x87u, 0x86u,
    0xF7u, 0xA6u, 0x26u, 0xBBu, 0xD6u, 0x59u, 0x4Du, 0xBFu, 0x6Au, 0x2Eu, 0xAAu,
    0x2Bu, 0xEFu, 0xE6u, 0x78u, 0xB6u, 0x4Eu, 0xE0u, 0x2Fu, 0xDCu, 0x7Cu, 0xBEu,
    0x57u, 0x19u, 0x32u, 0x7Eu, 0x2Au, 0xD0u, 0xB8u, 0xBAu, 0x29u, 0x00u, 0x3Cu,
    0x52u, 0x7Du, 0xA8u, 0x49u, 0x3Bu, 0x2Du, 0xEBu, 0x25u, 0x49u, 0xFAu, 0xA3u,
    0xAAu, 0x39u, 0xA7u, 0xC5u, 0xA7u, 0x50u, 0x11u, 0x36u, 0xFBu, 0xC6u, 0x67u,
    0x4Au, 0xF5u, 0xA5u, 0x12u, 0x65u, 0x7Eu, 0xB0u, 0xDFu, 0xAFu, 0x4Eu, 0xB3u,
    0x61u, 0x7Fu, 0x2Fu,
};

static bool
nikon_decrypt(std::span<const std::byte> enc, uint32_t serial_key,
              uint32_t shutter_count, std::span<std::byte> out) noexcept
{
    if (enc.size() != out.size()) {
        return false;
    }

    const uint8_t serial8 = static_cast<uint8_t>(serial_key & 0xFFu);
    const uint8_t key     = static_cast<uint8_t>(
        (shutter_count >> 0) ^ (shutter_count >> 8) ^ (shutter_count >> 16)
        ^ (shutter_count >> 24));

    const uint8_t ci0 = kNikonDecryptXlat0[serial8];
    uint8_t cj        = kNikonDecryptXlat1[key];
    uint8_t ck        = 0x60u;

    for (size_t i = 0; i < enc.size(); ++i) {
        const uint32_t prod = static_cast<uint32_t>(ci0)
                              * static_cast<uint32_t>(ck);
        cj = static_cast<uint8_t>((static_cast<uint32_t>(cj) + prod) & 0xFFu);
        ck = static_cast<uint8_t>((static_cast<uint32_t>(ck) + 1U) & 0xFFu);
        out[i] = std::byte { static_cast<uint8_t>(u8(enc[i]) ^ cj) };
    }
    return true;
}

static void
decode_nikon_bin_dir_entries(std::string_view ifd_name, MetaStore& store,
                             std::span<const uint16_t> tags,
                             std::span<const MetaValue> values,
                             const ExifDecodeLimits& limits,
                             ExifDecodeResult* status_out) noexcept
{
    emit_bin_dir_entries(ifd_name, store, tags, values, limits, status_out);
}


static void
decode_nikon_settings_dir(std::string_view ifd_name,
                          std::span<const std::byte> raw, MetaStore& store,
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
        entry.key          = make_exif_tag_key(store.arena(), ifd_name, tag);
        entry.origin.block = block;
        entry.origin.order_in_block = i;
        entry.origin.wire_type      = WireType { WireFamily::Tiff, type_be };
        entry.origin.wire_count     = 1;
        entry.flags |= EntryFlags::Derived;

        switch (type_be) {
        case 1: entry.value = make_u8(static_cast<uint8_t>(val32)); break;
        case 3: entry.value = make_u16(static_cast<uint16_t>(val32)); break;
        case 4: entry.value = make_u32(val32); break;
        case 8:
            entry.value = make_i16(
                static_cast<int16_t>(static_cast<uint16_t>(val32)));
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


void
decode_nikon_binary_subdirs(std::string_view mk_ifd0, MetaStore& store, bool le,
                            const ExifDecodeOptions& options,
                            ExifDecodeResult* status_out) noexcept
{
    if (mk_ifd0.empty()) {
        return;
    }

    uint32_t serial_key     = 0;
    uint32_t shutter_count  = 0;
    bool have_serial        = false;
    bool have_shutter_count = false;
    {
        const std::string_view serial_s
            = find_first_exif_text_value(store, mk_ifd0, 0x001d);
        have_serial        = nikon_parse_u32_dec(serial_s, &serial_key);
        have_shutter_count = find_first_exif_u32_value(store, mk_ifd0, 0x00a7,
                                                       &shutter_count);
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
        case 0x002B:  // DistortInfo
        case 0x002C:  // UnknownInfo
        case 0x0032:  // UnknownInfo2
        case 0x004E:  // NikonSettings
        case 0x0091:  // ShotInfoUnknown
        case 0x0097:  // ColorBalanceUnknown2
        case 0x0098:  // LensData
        case 0x00A8:  // FlashInfo
        case 0x00B0:  // MultiExposure
        case 0x00B7:  // AFInfo2
        case 0x00B8:  // FileInfo
        case 0x00B9:  // AFTune
        case 0x00BB:  // RetouchInfo
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
    uint32_t idx_distortinfo    = 0;
    uint32_t idx_unknowninfo    = 0;
    uint32_t idx_unknowninfo2   = 0;
    uint32_t idx_settings       = 0;
    uint32_t idx_shotinfo       = 0;
    uint32_t idx_colorbalance   = 0;
    uint32_t idx_lensdata       = 0;
    uint32_t idx_flashinfo      = 0;
    uint32_t idx_multiexposure  = 0;
    uint32_t idx_afinfo2        = 0;
    uint32_t idx_fileinfo       = 0;
    uint32_t idx_aftune         = 0;
    uint32_t idx_retouchinfo    = 0;

    for (uint32_t i = 0; i < cand_count; ++i) {
        const uint16_t tag                       = cands[i].tag;
        const ByteSpan raw_span                  = cands[i].value.data.span;
        const std::span<const std::byte> raw_src = store.arena().span(raw_span);
        if (raw_src.empty()) {
            continue;
        }

        const std::string_view mk_prefix = "mk_nikon";

        if (tag == 0x001F) {  // VRInfo
            if (raw_src.size() < 7) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "vrinfo", idx_vrinfo++,
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
                                      std::span<const std::byte>(raw.data(), 4)),
                make_u8(vr_enabled),
                make_u8(vr_mode),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
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
                store.arena(), std::span<const std::byte>(raw.data() + 4, 20));
            const MetaValue base = make_fixed_ascii_text(
                store.arena(), std::span<const std::byte>(raw.data() + 24, 20));

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
                ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
                std::span<const MetaValue>(vals_out, out_count), options.limits,
                status_out);
            continue;
        }

        if (tag == 0x002B) {  // DistortInfo
            if (raw_src.size() < 5) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "distortinfo",
                                             idx_distortinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());

            const uint16_t tags_out[]  = { 0x0000, 0x0004 };
            const MetaValue vals_out[] = {
                make_fixed_ascii_text(store.arena(),
                                      std::span<const std::byte>(ver_bytes)),
                make_u8(u8(raw_src[4])),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
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
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
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
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x00A8) {  // FlashInfo
            if (raw_src.size() < 4) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());
            const std::string_view ver = std::string_view(
                reinterpret_cast<const char*>(ver_bytes.data()),
                ver_bytes.size());

            std::string_view subtable = "flashinfo0100";
            if (ver == "0100") {
                subtable = "flashinfo0100";
            } else if (ver == "0102") {
                subtable = "flashinfo0102";
            } else if (ver == "0103") {
                subtable = "flashinfo0103";
            } else if (ver == "0105") {
                // ExifTool reports version 0105 but uses the 0103 layout.
                subtable = "flashinfo0103";
            } else if (ver == "0106") {
                subtable = "flashinfo0106";
            } else if (ver == "0107") {
                subtable = "flashinfo0107";
            }

            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, subtable,
                                             idx_flashinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            uint16_t tags_out[24];
            MetaValue vals_out[24];
            uint32_t out_count = 0;

            tags_out[out_count] = 0x0000;
            vals_out[out_count]
                = make_fixed_ascii_text(store.arena(),
                                        std::span<const std::byte>(ver_bytes));
            out_count += 1;

            if (raw_src.size() > 4) {
                tags_out[out_count] = 0x0004;
                vals_out[out_count] = make_u8(u8(raw_src[4]));
                out_count += 1;
            }

            if (raw_src.size() >= 8) {
                std::array<uint8_t, 2> fw;
                fw[0]               = u8(raw_src[6]);
                fw[1]               = u8(raw_src[7]);
                tags_out[out_count] = 0x0006;
                vals_out[out_count]
                    = make_u8_array(store.arena(),
                                    std::span<const uint8_t>(fw));
                out_count += 1;
            }

            if (raw_src.size() > 8) {
                tags_out[out_count] = 0x0008;
                vals_out[out_count] = make_u8(u8(raw_src[8]));
                out_count += 1;
            }

            const uint16_t u8_tags[] = { 0x000c, 0x000d, 0x000e, 0x000f,
                                         0x0010 };
            for (size_t k = 0; k < sizeof(u8_tags) / sizeof(u8_tags[0]); ++k) {
                const uint16_t t    = u8_tags[k];
                const uint64_t off  = t;
                const uint64_t need = off + 1;
                if (need > raw_src.size()) {
                    continue;
                }
                tags_out[out_count] = t;
                vals_out[out_count] = make_u8(u8(raw_src[off]));
                out_count += 1;
            }

            const uint16_t i8_tags[]
                = { 0x000a, 0x0013, 0x0014, 0x0015, 0x001b,
                    0x001d, 0x0027, 0x0028, 0x0029, 0x002a };
            for (size_t k = 0; k < sizeof(i8_tags) / sizeof(i8_tags[0]); ++k) {
                const uint16_t t    = i8_tags[k];
                const uint64_t off  = t;
                const uint64_t need = off + 1;
                if (need > raw_src.size()) {
                    continue;
                }
                tags_out[out_count] = t;
                vals_out[out_count] = make_i8(nikon_to_i8(u8(raw_src[off])));
                out_count += 1;
            }

            decode_nikon_bin_dir_entries(
                ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
                std::span<const MetaValue>(vals_out, out_count), options.limits,
                status_out);
            continue;
        }

        if (tag == 0x00B0) {  // MultiExposure
            if (raw_src.size() < 16) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "multiexposure",
                                             idx_multiexposure++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());

            uint32_t mode  = 0;
            uint32_t shots = 0;
            uint32_t gain  = 0;
            if (!(le ? read_u32le(raw_src, 4, &mode)
                     : read_u32be(raw_src, 4, &mode))
                || !(le ? read_u32le(raw_src, 8, &shots)
                        : read_u32be(raw_src, 8, &shots))
                || !(le ? read_u32le(raw_src, 12, &gain)
                        : read_u32be(raw_src, 12, &gain))) {
                continue;
            }

            const uint16_t tags_out[]  = { 0x0000, 0x0001, 0x0002, 0x0003 };
            const MetaValue vals_out[] = {
                make_fixed_ascii_text(store.arena(),
                                      std::span<const std::byte>(ver_bytes)),
                make_u32(mode),
                make_u32(shots),
                make_u32(gain),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
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
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
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
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
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
                                      std::span<const std::byte>(raw.data(), 4)),
                make_fixed_ascii_text(store.arena(),
                                      std::span<const std::byte>(raw.data() + 4,
                                                                 5)),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x0097) {  // ColorBalance*
            if (raw_src.size() < 4) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());
            const std::string_view ver = std::string_view(
                reinterpret_cast<const char*>(ver_bytes.data()),
                ver_bytes.size());

            // ColorBalance2/4 carry WB_*Levels (tag 0) and are typically
            // encrypted. Decrypt and decode those common tables.
            if ((ver == "0102" || ver == "0205" || ver == "0213"
                 || ver == "0219")
                && have_serial && have_shutter_count && raw_src.size() >= 12) {
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "colorbalance2",
                                                 idx_colorbalance++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 8> dec;
                const std::span<const std::byte> enc = raw_src.subspan(4, 8);
                if (nikon_decrypt(enc, serial_key, shutter_count,
                                  std::span<std::byte>(dec.data(),
                                                       dec.size()))) {
                    uint16_t levels[4];
                    for (uint32_t k = 0; k < 4; ++k) {
                        uint16_t v = 0;
                        (void)read_u16_endian(
                            le,
                            std::span<const std::byte>(dec.data(), dec.size()),
                            uint64_t(k) * 2ULL, &v);
                        levels[k] = v;
                    }
                    const uint16_t tags_out[] = { 0x0000 };
                    const MetaValue vals_out[]
                        = { make_u16_array(store.arena(),
                                           std::span<const uint16_t>(levels)) };
                    decode_nikon_bin_dir_entries(
                        ifd_name, store, std::span<const uint16_t>(tags_out),
                        std::span<const MetaValue>(vals_out), options.limits,
                        status_out);
                    continue;
                }
            }

            if ((ver == "0209" || ver == "0211" || ver == "0215"
                 || ver == "0217")
                && have_serial && have_shutter_count && raw_src.size() >= 12) {
                const std::string_view ifd_name
                    = make_mk_subtable_ifd_token(mk_prefix, "colorbalance4",
                                                 idx_colorbalance++,
                                                 std::span<char>(sub_ifd_buf));
                if (ifd_name.empty()) {
                    continue;
                }

                std::array<std::byte, 8> dec;
                const std::span<const std::byte> enc = raw_src.subspan(4, 8);
                if (nikon_decrypt(enc, serial_key, shutter_count,
                                  std::span<std::byte>(dec.data(),
                                                       dec.size()))) {
                    uint16_t levels[4];
                    for (uint32_t k = 0; k < 4; ++k) {
                        uint16_t v = 0;
                        (void)read_u16_endian(
                            le,
                            std::span<const std::byte>(dec.data(), dec.size()),
                            uint64_t(k) * 2ULL, &v);
                        levels[k] = v;
                    }
                    const uint16_t tags_out[] = { 0x0000 };
                    const MetaValue vals_out[]
                        = { make_u16_array(store.arena(),
                                           std::span<const uint16_t>(levels)) };
                    decode_nikon_bin_dir_entries(
                        ifd_name, store, std::span<const uint16_t>(tags_out),
                        std::span<const MetaValue>(vals_out), options.limits,
                        status_out);
                    continue;
                }
            }

            // Fallback: expose only the version string.
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "colorbalanceunknown2",
                                             idx_colorbalance++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }
            const uint16_t tags_out[]  = { 0x0000 };
            const MetaValue vals_out[] = { make_fixed_ascii_text(
                store.arena(), std::span<const std::byte>(ver_bytes.data(),
                                                          ver_bytes.size())) };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
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
            if (ver == "0204") {
                subtable = "lensdata0204";
            } else if (ver == "0400") {
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

            if (subtable == "lensdata0204" && have_serial && have_shutter_count
                && raw_src.size() >= 20) {
                std::array<std::byte, 16> dec;
                const std::span<const std::byte> enc = raw_src.subspan(4, 16);
                if (nikon_decrypt(enc, serial_key, shutter_count,
                                  std::span<std::byte>(dec.data(),
                                                       dec.size()))) {
                    uint16_t tags_out[16];
                    MetaValue vals_out[16];
                    uint32_t out_count = 0;

                    tags_out[out_count] = 0x0000;
                    vals_out[out_count] = make_fixed_ascii_text(
                        store.arena(),
                        std::span<const std::byte>(ver_bytes.data(),
                                                   ver_bytes.size()));
                    out_count += 1;

                    const uint16_t want[] = { 0x0004, 0x0005, 0x0008, 0x000a,
                                              0x000b, 0x000c, 0x000d, 0x000e,
                                              0x000f, 0x0010, 0x0011, 0x0012,
                                              0x0013 };
                    for (uint32_t wi = 0; wi < sizeof(want) / sizeof(want[0]);
                         ++wi) {
                        const uint16_t t = want[wi];
                        if (t < 4 || t >= 20) {
                            continue;
                        }
                        if (out_count
                            >= sizeof(tags_out) / sizeof(tags_out[0])) {
                            break;
                        }
                        tags_out[out_count] = t;
                        vals_out[out_count] = make_u8(
                            u8(dec[static_cast<size_t>(t - 4)]));
                        out_count += 1;
                    }

                    decode_nikon_bin_dir_entries(
                        ifd_name, store,
                        std::span<const uint16_t>(tags_out, out_count),
                        std::span<const MetaValue>(vals_out, out_count),
                        options.limits, status_out);
                    continue;
                }
            }

            {
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
            }
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

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());
            const std::string_view ver = std::string_view(
                reinterpret_cast<const char*>(ver_bytes.data()),
                ver_bytes.size());

            std::string_view subtable = "afinfo2v0100";
            if (ver == "0100") {
                subtable = "afinfo2v0100";
            } else if (ver == "0101") {
                subtable = "afinfo2v0101";
            } else if (ver == "0200") {
                subtable = "afinfo2v0200";
            } else if (ver == "0300") {
                subtable = "afinfo2v0300";
            } else if (ver == "0400") {
                subtable = "afinfo2v0400";
            }

            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, subtable, idx_afinfo2++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            uint16_t tags_out[32];
            MetaValue vals_out[32];
            uint32_t out_count = 0;

            tags_out[out_count] = 0x0000;
            vals_out[out_count]
                = make_fixed_ascii_text(store.arena(),
                                        std::span<const std::byte>(ver_bytes));
            out_count += 1;

            for (uint16_t t = 0x0004; t <= 0x0007; ++t) {
                const uint64_t off  = t;
                const uint64_t need = off + 1;
                if (need > raw_src.size()) {
                    continue;
                }
                tags_out[out_count] = t;
                vals_out[out_count] = make_u8(u8(raw_src[off]));
                out_count += 1;
            }

            // AFPointsUsed (variable length; ExifTool uses 5 bytes in
            // AFInfo2Version=0100).
            if (raw_src.size() >= 0x0008 + 5) {
                tags_out[out_count] = 0x0008;
                vals_out[out_count] = make_bytes(store.arena(),
                                                 raw_src.subspan(0x0008, 5));
                out_count += 1;
            }

            // AFInfo2Version=0100 fields (u16), best-effort for other
            // versions too when present.
            const uint16_t u16_tags[] = { 0x0010, 0x0012, 0x0014,
                                          0x0016, 0x0018, 0x001a };
            for (size_t k = 0; k < sizeof(u16_tags) / sizeof(u16_tags[0]);
                 ++k) {
                const uint16_t t    = u16_tags[k];
                const uint64_t off  = t;
                const uint64_t need = off + 2;
                if (need > raw_src.size()) {
                    continue;
                }
                uint16_t v16 = 0;
                if (!read_u16_endian(le, raw_src, off, &v16)) {
                    continue;
                }
                tags_out[out_count] = t;
                vals_out[out_count] = make_u16(v16);
                out_count += 1;
            }

            if (raw_src.size() > 0x001c) {
                tags_out[out_count] = 0x001c;
                vals_out[out_count] = make_u8(u8(raw_src[0x001c]));
                out_count += 1;
            }

            decode_nikon_bin_dir_entries(
                ifd_name, store, std::span<const uint16_t>(tags_out, out_count),
                std::span<const MetaValue>(vals_out, out_count), options.limits,
                status_out);
            continue;
        }

        if (tag == 0x00B8) {  // FileInfo
            if (raw_src.size() < 10) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "fileinfo",
                                             idx_fileinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());

            uint16_t card = 0;
            uint16_t dir  = 0;
            uint16_t file = 0;
            if (!read_u16_endian(le, raw_src, 4, &card)
                || !read_u16_endian(le, raw_src, 6, &dir)
                || !read_u16_endian(le, raw_src, 8, &file)) {
                continue;
            }

            const uint16_t tags_out[]  = { 0x0000, 0x0002, 0x0003, 0x0004 };
            const MetaValue vals_out[] = {
                make_fixed_ascii_text(store.arena(),
                                      std::span<const std::byte>(ver_bytes)),
                make_u16(card),
                make_u16(dir),
                make_u16(file),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x00B9) {  // AFTune
            if (raw_src.size() < 4) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "aftune", idx_aftune++,
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
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }

        if (tag == 0x00BB) {  // RetouchInfo
            if (raw_src.size() < 6) {
                continue;
            }
            const std::string_view ifd_name
                = make_mk_subtable_ifd_token(mk_prefix, "retouchinfo",
                                             idx_retouchinfo++,
                                             std::span<char>(sub_ifd_buf));
            if (ifd_name.empty()) {
                continue;
            }

            std::array<std::byte, 4> ver_bytes;
            std::memcpy(ver_bytes.data(), raw_src.data(), ver_bytes.size());

            const int8_t processing = nikon_to_i8(u8(raw_src[5]));

            const uint16_t tags_out[]  = { 0x0000, 0x0005 };
            const MetaValue vals_out[] = {
                make_fixed_ascii_text(store.arena(),
                                      std::span<const std::byte>(ver_bytes)),
                make_i8(processing),
            };
            decode_nikon_bin_dir_entries(ifd_name, store,
                                         std::span<const uint16_t>(tags_out),
                                         std::span<const MetaValue>(vals_out),
                                         options.limits, status_out);
            continue;
        }
    }
}

}  // namespace openmeta::exif_internal
