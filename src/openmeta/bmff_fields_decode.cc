#include "bmff_fields_decode_internal.h"

#include "openmeta/container_scan.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace openmeta {
namespace {

    static constexpr uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
    }


    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2 > bytes.size()) {
            return false;
        }
        const uint16_t v = static_cast<uint16_t>(u8(bytes[offset + 0]) << 8)
                           | static_cast<uint16_t>(u8(bytes[offset + 1]) << 0);
        *out = v;
        return true;
    }


    static bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4 > bytes.size()) {
            return false;
        }
        uint32_t v = 0;
        v |= static_cast<uint32_t>(u8(bytes[offset + 0])) << 24;
        v |= static_cast<uint32_t>(u8(bytes[offset + 1])) << 16;
        v |= static_cast<uint32_t>(u8(bytes[offset + 2])) << 8;
        v |= static_cast<uint32_t>(u8(bytes[offset + 3])) << 0;
        *out = v;
        return true;
    }


    static bool read_u64be(std::span<const std::byte> bytes, uint64_t offset,
                           uint64_t* out) noexcept
    {
        if (!out || offset + 8 > bytes.size()) {
            return false;
        }
        uint64_t v = 0;
        for (uint32_t i = 0; i < 8; ++i) {
            v = (v << 8) | static_cast<uint64_t>(u8(bytes[offset + i]));
        }
        *out = v;
        return true;
    }


    struct BmffBox final {
        uint64_t offset      = 0;
        uint64_t size        = 0;
        uint64_t header_size = 0;
        uint32_t type        = 0;
        bool has_uuid        = false;
        std::array<std::byte, 16> uuid {};
    };

    static bool parse_bmff_box(std::span<const std::byte> bytes,
                               uint64_t offset, uint64_t parent_end,
                               BmffBox* out) noexcept
    {
        if (!out) {
            return false;
        }
        if (offset + 8 > parent_end || offset + 8 > bytes.size()) {
            return false;
        }
        uint32_t size32 = 0;
        uint32_t type   = 0;
        if (!read_u32be(bytes, offset + 0, &size32)
            || !read_u32be(bytes, offset + 4, &type)) {
            return false;
        }

        uint64_t header_size = 8;
        uint64_t box_size    = size32;
        if (size32 == 1) {
            uint64_t size64 = 0;
            if (!read_u64be(bytes, offset + 8, &size64)) {
                return false;
            }
            header_size = 16;
            box_size    = size64;
        } else if (size32 == 0) {
            box_size = parent_end - offset;
        }

        if (box_size < header_size) {
            return false;
        }
        if (offset + box_size > parent_end
            || offset + box_size > bytes.size()) {
            return false;
        }

        bool has_uuid = false;
        std::array<std::byte, 16> uuid {};
        if (type == fourcc('u', 'u', 'i', 'd')) {
            if (header_size + 16 > box_size) {
                return false;
            }
            has_uuid                = true;
            const uint64_t uuid_off = offset + header_size;
            if (uuid_off + 16 > bytes.size()) {
                return false;
            }
            for (uint32_t i = 0; i < 16; ++i) {
                uuid[i] = bytes[uuid_off + i];
            }
            header_size += 16;
        }

        out->offset      = offset;
        out->size        = box_size;
        out->header_size = header_size;
        out->type        = type;
        out->has_uuid    = has_uuid;
        out->uuid        = uuid;
        return true;
    }


    static bool bmff_is_container_box(uint32_t type) noexcept
    {
        switch (type) {
        case fourcc('m', 'o', 'o', 'v'):
        case fourcc('t', 'r', 'a', 'k'):
        case fourcc('m', 'd', 'i', 'a'):
        case fourcc('m', 'i', 'n', 'f'):
        case fourcc('s', 't', 'b', 'l'):
        case fourcc('e', 'd', 't', 's'):
        case fourcc('d', 'i', 'n', 'f'):
        case fourcc('u', 'd', 't', 'a'): return true;
        default: return false;
        }
    }


    static void bmff_note_brand(uint32_t brand, bool* is_heif, bool* is_avif,
                                bool* is_cr3) noexcept
    {
        if (brand == fourcc('c', 'r', 'x', ' ')
            || brand == fourcc('C', 'R', '3', ' ')) {
            *is_cr3 = true;
        }

        if (brand == fourcc('a', 'v', 'i', 'f')
            || brand == fourcc('a', 'v', 'i', 's')) {
            *is_avif = true;
        }

        if (brand == fourcc('m', 'i', 'f', '1')
            || brand == fourcc('m', 's', 'f', '1')
            || brand == fourcc('h', 'e', 'i', 'c')
            || brand == fourcc('h', 'e', 'i', 'x')
            || brand == fourcc('h', 'e', 'v', 'c')
            || brand == fourcc('h', 'e', 'v', 'x')) {
            *is_heif = true;
        }
    }


    static bool bmff_parse_ftyp(std::span<const std::byte> bytes,
                                const BmffBox& ftyp,
                                ContainerFormat* out_format,
                                uint32_t* out_major_brand,
                                uint32_t* out_minor_version,
                                std::array<uint32_t, 32>* out_compat_brands,
                                uint32_t* out_compat_count) noexcept
    {
        if (!out_format || !out_major_brand || !out_minor_version
            || !out_compat_brands || !out_compat_count) {
            return false;
        }

        const uint64_t payload_off  = ftyp.offset + ftyp.header_size;
        const uint64_t payload_size = ftyp.size - ftyp.header_size;
        if (payload_size < 8) {
            return false;
        }

        uint32_t major_brand = 0;
        uint32_t minor_ver   = 0;
        if (!read_u32be(bytes, payload_off + 0, &major_brand)
            || !read_u32be(bytes, payload_off + 4, &minor_ver)) {
            return false;
        }

        bool is_heif = false;
        bool is_avif = false;
        bool is_cr3  = false;
        bmff_note_brand(major_brand, &is_heif, &is_avif, &is_cr3);

        std::array<uint32_t, 32> compat {};
        uint32_t compat_count = 0;
        const uint64_t brands_off = payload_off + 8;
        const uint64_t brands_end = payload_off + payload_size;
        for (uint64_t off = brands_off; off + 4 <= brands_end; off += 4) {
            uint32_t brand = 0;
            if (!read_u32be(bytes, off, &brand)) {
                return false;
            }
            bmff_note_brand(brand, &is_heif, &is_avif, &is_cr3);
            if (compat_count < compat.size()) {
                compat[compat_count++] = brand;
            }
        }

        ContainerFormat fmt = ContainerFormat::Unknown;
        if (is_cr3) {
            fmt = ContainerFormat::Cr3;
        } else if (is_avif) {
            fmt = ContainerFormat::Avif;
        } else if (is_heif) {
            fmt = ContainerFormat::Heif;
        } else {
            return false;
        }

        *out_format        = fmt;
        *out_major_brand   = major_brand;
        *out_minor_version = minor_ver;
        *out_compat_brands = compat;
        *out_compat_count  = compat_count;
        return true;
    }


    static void emit_u32_field(MetaStore& store, BlockId block, uint32_t order,
                               std::string_view field, uint32_t value) noexcept
    {
        Entry e;
        e.key                   = make_bmff_field_key(store.arena(), field);
        e.value                 = make_u32(value);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    static void emit_u16_field(MetaStore& store, BlockId block, uint32_t order,
                               std::string_view field, uint16_t value) noexcept
    {
        Entry e;
        e.key                   = make_bmff_field_key(store.arena(), field);
        e.value                 = make_u16(value);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    static void emit_u8_field(MetaStore& store, BlockId block, uint32_t order,
                              std::string_view field, uint8_t value) noexcept
    {
        Entry e;
        e.key                   = make_bmff_field_key(store.arena(), field);
        e.value                 = make_u8(value);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    static void emit_text_field(MetaStore& store, BlockId block, uint32_t order,
                                std::string_view field,
                                std::string_view value) noexcept
    {
        Entry e;
        e.key                   = make_bmff_field_key(store.arena(), field);
        e.value                 = make_text(store.arena(), value,
                                            TextEncoding::Ascii);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = 1;
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    static void emit_u32_array_field(MetaStore& store, BlockId block,
                                     uint32_t order, std::string_view field,
                                     std::span<const uint32_t> values) noexcept
    {
        Entry e;
        e.key                   = make_bmff_field_key(store.arena(), field);
        e.value                 = make_u32_array(store.arena(), values);
        e.origin.block          = block;
        e.origin.order_in_block = order;
        e.origin.wire_type      = WireType { WireFamily::Other, 0 };
        e.origin.wire_count     = static_cast<uint32_t>(values.size());
        e.flags                 = EntryFlags::Derived;
        (void)store.add_entry(e);
    }


    struct IspeProp final {
        uint32_t index  = 0;  // 1-based ipco index
        uint32_t width  = 0;
        uint32_t height = 0;
    };

    struct U8Prop final {
        uint32_t index = 0;  // 1-based ipco index
        uint8_t value = 0;
    };

    enum class AuxSemantic : uint8_t {
        Unknown   = 0,
        Alpha     = 1,
        Depth     = 2,
        Disparity = 3,
        Matte     = 4,
    };

    struct AuxCProp final {
        uint32_t index = 0;  // 1-based ipco index
        AuxSemantic semantic = AuxSemantic::Unknown;
        std::array<char, 96> aux_type {};
        uint16_t aux_type_len = 0;
        std::array<std::byte, 32> aux_subtype {};
        uint16_t aux_subtype_len = 0;
        uint16_t aux_subtype_total_len = 0;
        bool aux_subtype_truncated = false;
    };

    struct AuxItemInfo final {
        uint32_t item_id = 0;
        AuxSemantic semantic = AuxSemantic::Unknown;
        std::array<char, 96> aux_type {};
        uint16_t aux_type_len = 0;
        std::array<std::byte, 32> aux_subtype {};
        uint16_t aux_subtype_len = 0;
        uint16_t aux_subtype_total_len = 0;
        bool aux_subtype_truncated = false;
    };

    struct ItemRefEdge final {
        uint32_t ref_type     = 0;
        uint32_t from_item_id = 0;
        uint32_t to_item_id   = 0;
    };


    struct PrimaryProps final {
        bool have_item_id = false;
        uint32_t item_id  = 0;

        bool have_width_height = false;
        uint32_t width         = 0;
        uint32_t height        = 0;

        bool have_rotation = false;
        uint16_t rotation_degrees = 0;

        bool have_mirror = false;
        uint8_t mirror   = 0;

        std::array<ItemRefEdge, 512> iref_edges {};
        uint32_t iref_edge_count = 0;
        uint32_t iref_edge_total = 0;
        bool iref_truncated      = false;

        std::array<uint32_t, 128> primary_auxl_item_ids {};
        uint32_t primary_auxl_count = 0;
        std::array<AuxSemantic, 128> primary_auxl_semantics {};
        std::array<uint32_t, 128> primary_alpha_item_ids {};
        uint32_t primary_alpha_count = 0;
        std::array<uint32_t, 128> primary_depth_item_ids {};
        uint32_t primary_depth_count = 0;
        std::array<uint32_t, 128> primary_disparity_item_ids {};
        uint32_t primary_disparity_count = 0;
        std::array<uint32_t, 128> primary_matte_item_ids {};
        uint32_t primary_matte_count = 0;

        std::array<uint32_t, 128> primary_dimg_item_ids {};
        uint32_t primary_dimg_count = 0;
        std::array<uint32_t, 128> primary_thmb_item_ids {};
        uint32_t primary_thmb_count = 0;
        std::array<uint32_t, 128> primary_cdsc_item_ids {};
        uint32_t primary_cdsc_count = 0;

        std::array<AuxItemInfo, 256> aux_items {};
        uint32_t aux_item_count = 0;
    };

    static void push_primary_rel(std::span<uint32_t> out, uint32_t* io_count,
                                 uint32_t value) noexcept
    {
        if (!io_count) {
            return;
        }
        if (*io_count < out.size()) {
            out[*io_count] = value;
            *io_count += 1;
        }
    }

    static void push_primary_rel_unique(std::span<uint32_t> out,
                                        uint32_t* io_count,
                                        uint32_t value) noexcept
    {
        if (!io_count) {
            return;
        }
        for (uint32_t i = 0; i < *io_count && i < out.size(); ++i) {
            if (out[i] == value) {
                return;
            }
        }
        push_primary_rel(out, io_count, value);
    }

    static uint8_t ascii_to_lower(uint8_t c) noexcept
    {
        if (c >= static_cast<uint8_t>('A')
            && c <= static_cast<uint8_t>('Z')) {
            return static_cast<uint8_t>(c + 0x20U);
        }
        return c;
    }

    static bool ascii_ieq(std::string_view a, std::string_view b) noexcept
    {
        if (a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            const uint8_t ac = ascii_to_lower(static_cast<uint8_t>(a[i]));
            const uint8_t bc = ascii_to_lower(static_cast<uint8_t>(b[i]));
            if (ac != bc) {
                return false;
            }
        }
        return true;
    }

    static bool ascii_icontains(std::string_view hay,
                                std::string_view needle) noexcept
    {
        if (needle.empty()) {
            return true;
        }
        if (hay.size() < needle.size()) {
            return false;
        }
        const size_t stop = hay.size() - needle.size();
        for (size_t i = 0; i <= stop; ++i) {
            bool match = true;
            for (size_t j = 0; j < needle.size(); ++j) {
                const uint8_t hc
                    = ascii_to_lower(static_cast<uint8_t>(hay[i + j]));
                const uint8_t nc
                    = ascii_to_lower(static_cast<uint8_t>(needle[j]));
                if (hc != nc) {
                    match = false;
                    break;
                }
            }
            if (match) {
                return true;
            }
        }
        return false;
    }

    static AuxSemantic classify_auxc_type(std::string_view aux_type) noexcept
    {
        if (aux_type.empty()) {
            return AuxSemantic::Unknown;
        }

        if (ascii_ieq(aux_type, "urn:mpeg:hevc:2015:auxid:1")
            || ascii_icontains(aux_type, ":aux:alpha")
            || ascii_ieq(aux_type, "urn:mpeg:mpegb:cicp:systems:auxiliary:alpha")) {
            return AuxSemantic::Alpha;
        }
        if (ascii_ieq(aux_type, "urn:mpeg:hevc:2015:auxid:2")
            || ascii_icontains(aux_type, ":aux:depth")
            || ascii_icontains(aux_type, "depth")) {
            return AuxSemantic::Depth;
        }
        if (ascii_ieq(aux_type, "urn:mpeg:hevc:2015:auxid:3")
            || ascii_icontains(aux_type, ":aux:disparity")
            || ascii_icontains(aux_type, "disparity")) {
            return AuxSemantic::Disparity;
        }
        if (ascii_icontains(aux_type, "portraitmatte")
            || ascii_icontains(aux_type, ":aux:matte")
            || ascii_icontains(aux_type, "matte")) {
            return AuxSemantic::Matte;
        }
        return AuxSemantic::Unknown;
    }

    static std::string_view aux_semantic_name(AuxSemantic semantic) noexcept
    {
        switch (semantic) {
        case AuxSemantic::Unknown: return "unknown";
        case AuxSemantic::Alpha: return "alpha";
        case AuxSemantic::Depth: return "depth";
        case AuxSemantic::Disparity: return "disparity";
        case AuxSemantic::Matte: return "matte";
        }
        return "unknown";
    }

    static uint32_t find_primary_auxl_index(const PrimaryProps& out,
                                            uint32_t item_id) noexcept
    {
        for (uint32_t i = 0; i < out.primary_auxl_count; ++i) {
            if (out.primary_auxl_item_ids[i] == item_id) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    static uint32_t find_aux_item_index(const PrimaryProps& out,
                                        uint32_t item_id) noexcept
    {
        for (uint32_t i = 0; i < out.aux_item_count; ++i) {
            if (out.aux_items[i].item_id == item_id) {
                return i;
            }
        }
        return UINT32_MAX;
    }

    static const AuxItemInfo* find_aux_item_info(const PrimaryProps& out,
                                                 uint32_t item_id) noexcept
    {
        const uint32_t idx = find_aux_item_index(out, item_id);
        if (idx == UINT32_MAX || idx >= out.aux_items.size()) {
            return nullptr;
        }
        return &out.aux_items[idx];
    }

    static AuxSemantic find_aux_item_semantic(const PrimaryProps& out,
                                              uint32_t item_id) noexcept
    {
        const AuxItemInfo* info = find_aux_item_info(out, item_id);
        if (!info) {
            return AuxSemantic::Unknown;
        }
        return info->semantic;
    }

    static uint32_t upsert_aux_item(PrimaryProps* out,
                                    uint32_t item_id) noexcept
    {
        if (!out) {
            return UINT32_MAX;
        }
        uint32_t idx = find_aux_item_index(*out, item_id);
        if (idx == UINT32_MAX) {
            if (out->aux_item_count >= out->aux_items.size()) {
                return UINT32_MAX;
            }
            idx = out->aux_item_count;
            out->aux_items[idx]          = AuxItemInfo {};
            out->aux_items[idx].item_id  = item_id;
            out->aux_item_count += 1;
        }
        return idx;
    }

    static void set_aux_item_semantic(PrimaryProps* out, uint32_t item_id,
                                      AuxSemantic semantic) noexcept
    {
        if (!out || semantic == AuxSemantic::Unknown) {
            return;
        }
        const uint32_t idx = upsert_aux_item(out, item_id);
        if (idx == UINT32_MAX || idx >= out->aux_items.size()) {
            return;
        }
        if (out->aux_items[idx].semantic == AuxSemantic::Unknown) {
            out->aux_items[idx].semantic = semantic;
        }
    }

    static void set_aux_item_type(PrimaryProps* out, uint32_t item_id,
                                  std::string_view aux_type) noexcept
    {
        if (!out || aux_type.empty()) {
            return;
        }
        const uint32_t idx = upsert_aux_item(out, item_id);
        if (idx == UINT32_MAX || idx >= out->aux_items.size()) {
            return;
        }
        AuxItemInfo& info = out->aux_items[idx];
        if (info.aux_type_len != 0) {
            return;
        }
        const size_t max_copy
            = (aux_type.size() < info.aux_type.size())
                  ? aux_type.size()
                  : info.aux_type.size();
        for (size_t i = 0; i < max_copy; ++i) {
            info.aux_type[i] = aux_type[i];
        }
        info.aux_type_len = static_cast<uint16_t>(max_copy);
    }

    static void set_aux_item_subtype(PrimaryProps* out, uint32_t item_id,
                                     std::span<const std::byte> subtype,
                                     uint16_t total_len,
                                     bool truncated) noexcept
    {
        if (!out) {
            return;
        }
        const uint32_t idx = upsert_aux_item(out, item_id);
        if (idx == UINT32_MAX || idx >= out->aux_items.size()) {
            return;
        }
        AuxItemInfo& info = out->aux_items[idx];
        if (info.aux_subtype_total_len != 0) {
            return;
        }
        const size_t max_copy
            = (subtype.size() < info.aux_subtype.size())
                  ? subtype.size()
                  : info.aux_subtype.size();
        for (size_t i = 0; i < max_copy; ++i) {
            info.aux_subtype[i] = subtype[i];
        }
        info.aux_subtype_len = static_cast<uint16_t>(max_copy);
        info.aux_subtype_total_len = total_len;
        info.aux_subtype_truncated = truncated;
    }

    static char hex_digit(uint8_t v) noexcept
    {
        static constexpr char kHex[] = "0123456789ABCDEF";
        return kHex[v & 0x0F];
    }

    static bool bytes_are_printable_ascii(
        std::span<const std::byte> bytes) noexcept
    {
        if (bytes.empty()) {
            return false;
        }
        for (size_t i = 0; i < bytes.size(); ++i) {
            const uint8_t c = u8(bytes[i]);
            if (c < 0x20U || c > 0x7EU) {
                return false;
            }
        }
        return true;
    }

    static std::string bytes_to_hex_string(
        std::span<const std::byte> bytes) noexcept
    {
        std::string out;
        out.reserve(bytes.size() * 2U + 2U);
        out.push_back('0');
        out.push_back('x');
        for (size_t i = 0; i < bytes.size(); ++i) {
            const uint8_t b = u8(bytes[i]);
            out.push_back(hex_digit(static_cast<uint8_t>(b >> 4)));
            out.push_back(hex_digit(b));
        }
        return out;
    }

    static std::string bytes16_to_uuid_string(
        std::span<const std::byte> bytes) noexcept
    {
        std::string out;
        out.reserve(36U);
        for (size_t i = 0; i < 16; ++i) {
            const uint8_t b = u8(bytes[i]);
            out.push_back(hex_digit(static_cast<uint8_t>(b >> 4)));
            out.push_back(hex_digit(b));
            if (i == 3U || i == 5U || i == 7U || i == 9U) {
                out.push_back('-');
            }
        }
        return out;
    }

    struct AuxSubtypeInterpretation final {
        std::string_view kind;
        bool has_u32 = false;
        uint32_t u32 = 0;
        bool has_text = false;
        std::array<char, 80> text {};
        uint16_t text_len = 0;
    };

    static AuxSubtypeInterpretation interpret_aux_subtype(
        std::span<const std::byte> subtype, uint16_t total_len,
        bool truncated) noexcept
    {
        AuxSubtypeInterpretation out {};
        if (total_len == 0U) {
            out.kind = "none";
            return out;
        }
        if (subtype.empty()) {
            out.kind = "bytes";
            return out;
        }

        if (total_len == 1U && subtype.size() >= 1U) {
            out.kind    = "u8";
            out.has_u32 = true;
            out.u32     = static_cast<uint32_t>(u8(subtype[0]));
            return out;
        }
        if (total_len == 2U && subtype.size() >= 2U) {
            out.kind    = "u16be";
            out.has_u32 = true;
            out.u32     = (static_cast<uint32_t>(u8(subtype[0])) << 8)
                      | static_cast<uint32_t>(u8(subtype[1]));
            return out;
        }
        if (total_len == 4U && subtype.size() >= 4U) {
            if (bytes_are_printable_ascii(subtype.first(4U))) {
                out.kind     = "fourcc";
                out.has_text = true;
                for (size_t i = 0; i < 4U; ++i) {
                    out.text[i] = static_cast<char>(u8(subtype[i]));
                }
                out.text_len = 4U;
                return out;
            }
            out.kind    = "u32be";
            out.has_u32 = true;
            out.u32     = (static_cast<uint32_t>(u8(subtype[0])) << 24)
                      | (static_cast<uint32_t>(u8(subtype[1])) << 16)
                      | (static_cast<uint32_t>(u8(subtype[2])) << 8)
                      | static_cast<uint32_t>(u8(subtype[3]));
            return out;
        }
        if (total_len == 16U && subtype.size() >= 16U) {
            out.kind = "uuid";
            const std::string uuid = bytes16_to_uuid_string(subtype.first(16U));
            const size_t n = (uuid.size() < out.text.size()) ? uuid.size()
                                                             : out.text.size();
            for (size_t i = 0; i < n; ++i) {
                out.text[i] = uuid[i];
            }
            out.text_len  = static_cast<uint16_t>(n);
            out.has_text  = (n != 0U);
            return out;
        }

        if (!truncated && static_cast<size_t>(total_len) == subtype.size()
            && bytes_are_printable_ascii(subtype)) {
            out.kind = "ascii";
            const size_t n = (subtype.size() < out.text.size()) ? subtype.size()
                                                                 : out.text.size();
            for (size_t i = 0; i < n; ++i) {
                out.text[i] = static_cast<char>(u8(subtype[i]));
            }
            out.text_len = static_cast<uint16_t>(n);
            out.has_text = (n != 0U);
            return out;
        }

        out.kind = "bytes";
        return out;
    }

    static void set_primary_auxl_semantic(PrimaryProps* out, uint32_t item_id,
                                          AuxSemantic semantic) noexcept
    {
        if (!out || semantic == AuxSemantic::Unknown) {
            return;
        }
        const uint32_t idx = find_primary_auxl_index(*out, item_id);
        if (idx == UINT32_MAX || idx >= out->primary_auxl_semantics.size()) {
            return;
        }
        if (out->primary_auxl_semantics[idx] != AuxSemantic::Unknown) {
            return;
        }
        out->primary_auxl_semantics[idx] = semantic;
        switch (semantic) {
        case AuxSemantic::Alpha:
            push_primary_rel_unique(out->primary_alpha_item_ids,
                                    &out->primary_alpha_count, item_id);
            break;
        case AuxSemantic::Depth:
            push_primary_rel_unique(out->primary_depth_item_ids,
                                    &out->primary_depth_count, item_id);
            break;
        case AuxSemantic::Disparity:
            push_primary_rel_unique(out->primary_disparity_item_ids,
                                    &out->primary_disparity_count, item_id);
            break;
        case AuxSemantic::Matte:
            push_primary_rel_unique(out->primary_matte_item_ids,
                                    &out->primary_matte_count, item_id);
            break;
        case AuxSemantic::Unknown: break;
        }
    }

    static void add_primary_item_ref(PrimaryProps* out, uint32_t ref_type,
                                     uint32_t to_item_id) noexcept
    {
        if (!out) {
            return;
        }
        if (ref_type == fourcc('a', 'u', 'x', 'l')) {
            push_primary_rel(out->primary_auxl_item_ids,
                             &out->primary_auxl_count, to_item_id);
        } else if (ref_type == fourcc('d', 'i', 'm', 'g')) {
            push_primary_rel(out->primary_dimg_item_ids,
                             &out->primary_dimg_count, to_item_id);
        } else if (ref_type == fourcc('t', 'h', 'm', 'b')) {
            push_primary_rel(out->primary_thmb_item_ids,
                             &out->primary_thmb_count, to_item_id);
        } else if (ref_type == fourcc('c', 'd', 's', 'c')) {
            push_primary_rel(out->primary_cdsc_item_ids,
                             &out->primary_cdsc_count, to_item_id);
        }
    }

    static bool append_iref_edge(PrimaryProps* out, uint32_t ref_type,
                                 uint32_t from_item_id,
                                 uint32_t to_item_id) noexcept
    {
        if (!out) {
            return false;
        }
        if (out->iref_edge_total == UINT32_MAX) {
            return false;
        }
        out->iref_edge_total += 1;
        if (out->iref_edge_count < out->iref_edges.size()) {
            out->iref_edges[out->iref_edge_count]
                = ItemRefEdge { ref_type, from_item_id, to_item_id };
            out->iref_edge_count += 1;
        } else {
            out->iref_truncated = true;
        }

        if (out->have_item_id && from_item_id == out->item_id) {
            add_primary_item_ref(out, ref_type, to_item_id);
        }
        return true;
    }


    static bool bmff_parse_pitm(std::span<const std::byte> bytes,
                                const BmffBox& pitm,
                                uint32_t* out_item_id) noexcept
    {
        if (!out_item_id) {
            return false;
        }
        const uint64_t payload_off  = pitm.offset + pitm.header_size;
        const uint64_t payload_size = pitm.size - pitm.header_size;
        if (payload_size < 6) {
            return false;
        }

        const uint8_t version = u8(bytes[payload_off + 0]);
        if (version == 0) {
            uint16_t id16 = 0;
            if (!read_u16be(bytes, payload_off + 4, &id16)) {
                return false;
            }
            *out_item_id = static_cast<uint32_t>(id16);
            return true;
        }
        if (version == 1) {
            if (payload_size < 8) {
                return false;
            }
            uint32_t id32 = 0;
            if (!read_u32be(bytes, payload_off + 4, &id32)) {
                return false;
            }
            *out_item_id = id32;
            return true;
        }

        return false;
    }


    static void bmff_collect_ipco_props(
        std::span<const std::byte> bytes, const BmffBox& ipco,
        std::array<IspeProp, 64>* out_ispe, uint32_t* out_ispe_count,
        std::array<U8Prop, 64>* out_irot, uint32_t* out_irot_count,
        std::array<U8Prop, 64>* out_imir, uint32_t* out_imir_count,
        std::array<AuxCProp, 64>* out_auxc,
        uint32_t* out_auxc_count) noexcept
    {
        if (!out_ispe || !out_ispe_count || !out_irot || !out_irot_count
            || !out_imir || !out_imir_count || !out_auxc
            || !out_auxc_count) {
            return;
        }

        *out_ispe_count = 0;
        *out_irot_count = 0;
        *out_imir_count = 0;
        *out_auxc_count = 0;

        const uint64_t payload_off = ipco.offset + ipco.header_size;
        const uint64_t payload_end = ipco.offset + ipco.size;
        if (payload_off > payload_end || payload_end > bytes.size()) {
            return;
        }

        uint64_t off               = payload_off;
        uint32_t prop_index        = 1;
        const uint32_t kMaxBoxes   = 1U << 16;
        uint32_t seen              = 0;
        while (off + 8 <= payload_end) {
            seen += 1;
            if (seen > kMaxBoxes) {
                return;
            }

            BmffBox child;
            if (!parse_bmff_box(bytes, off, payload_end, &child)) {
                break;
            }

            const uint64_t child_payload_off = child.offset + child.header_size;
            const uint64_t child_payload_size = child.size - child.header_size;
            if (child_payload_off <= bytes.size()
                && child_payload_size <= bytes.size() - child_payload_off) {
                if (child.type == fourcc('i', 's', 'p', 'e')) {
                    if (child_payload_size >= 12) {
                        uint32_t width  = 0;
                        uint32_t height = 0;
                        if (read_u32be(bytes, child_payload_off + 4, &width)
                            && read_u32be(bytes, child_payload_off + 8,
                                          &height)) {
                            if (*out_ispe_count < out_ispe->size()) {
                                (*out_ispe)[*out_ispe_count]
                                    = IspeProp { prop_index, width, height };
                                *out_ispe_count += 1;
                            }
                        }
                    }
                } else if (child.type == fourcc('i', 'r', 'o', 't')) {
                    if (child_payload_size >= 1) {
                        const uint8_t rot = u8(bytes[child_payload_off]) & 0x03;
                        if (*out_irot_count < out_irot->size()) {
                            (*out_irot)[*out_irot_count]
                                = U8Prop { prop_index, rot };
                            *out_irot_count += 1;
                        }
                    }
                } else if (child.type == fourcc('i', 'm', 'i', 'r')) {
                    if (child_payload_size >= 1) {
                        const uint8_t dir = u8(bytes[child_payload_off]);
                        if (*out_imir_count < out_imir->size()) {
                            (*out_imir)[*out_imir_count]
                                = U8Prop { prop_index, dir };
                            *out_imir_count += 1;
                        }
                    }
                } else if (child.type == fourcc('a', 'u', 'x', 'C')) {
                    if (child_payload_size >= 5) {
                        uint64_t p = child_payload_off + 4;
                        const uint64_t e = child_payload_off
                                           + child_payload_size;
                        while (p < e && bytes[p] != std::byte { 0x00 }) {
                            p += 1;
                        }
                        if (p < e) {
                            const uint64_t type_off = child_payload_off + 4;
                            const uint64_t type_len = p - type_off;
                            if (type_len > 0
                                && type_off <= bytes.size()
                                && type_len <= bytes.size() - type_off) {
                                const char* s = reinterpret_cast<const char*>(
                                    bytes.data() + type_off);
                                const std::string_view aux_type(s,
                                                                type_len);
                                const AuxSemantic semantic
                                    = classify_auxc_type(aux_type);
                                if (*out_auxc_count < out_auxc->size()) {
                                    AuxCProp prop {};
                                    prop.index    = prop_index;
                                    prop.semantic = semantic;

                                    const size_t type_copy
                                        = (aux_type.size() < prop.aux_type.size())
                                              ? aux_type.size()
                                              : prop.aux_type.size();
                                    for (size_t ti = 0; ti < type_copy; ++ti) {
                                        prop.aux_type[ti] = aux_type[ti];
                                    }
                                    prop.aux_type_len
                                        = static_cast<uint16_t>(type_copy);

                                    const uint64_t subtype_off = p + 1;
                                    if (subtype_off <= e) {
                                        const uint64_t subtype_len_u64
                                            = e - subtype_off;
                                        const size_t subtype_len
                                            = static_cast<size_t>(
                                                subtype_len_u64);
                                        const size_t subtype_copy
                                            = (subtype_len
                                               < prop.aux_subtype.size())
                                                  ? subtype_len
                                                  : prop.aux_subtype.size();
                                        for (size_t si = 0; si < subtype_copy;
                                             ++si) {
                                            prop.aux_subtype[si]
                                                = bytes[subtype_off + si];
                                        }
                                        prop.aux_subtype_len
                                            = static_cast<uint16_t>(
                                                subtype_copy);
                                        prop.aux_subtype_total_len
                                            = (subtype_len > 0xFFFFU)
                                                  ? 0xFFFFU
                                                  : static_cast<uint16_t>(
                                                      subtype_len);
                                        prop.aux_subtype_truncated
                                            = (subtype_copy < subtype_len);
                                    }
                                    (*out_auxc)[*out_auxc_count] = prop;
                                    *out_auxc_count += 1;
                                }
                            }
                        }
                    }
                }
            }

            off += child.size;
            if (child.size == 0) {
                break;
            }
            if (prop_index == UINT32_MAX) {
                break;
            }
            prop_index += 1;
        }
    }


    static const IspeProp* find_ispe(std::span<const IspeProp> props,
                                     uint32_t index) noexcept
    {
        for (size_t i = 0; i < props.size(); ++i) {
            if (props[i].index == index) {
                return &props[i];
            }
        }
        return nullptr;
    }

    static const U8Prop* find_u8(std::span<const U8Prop> props,
                                 uint32_t index) noexcept
    {
        for (size_t i = 0; i < props.size(); ++i) {
            if (props[i].index == index) {
                return &props[i];
            }
        }
        return nullptr;
    }

    static const AuxCProp* find_auxc(std::span<const AuxCProp> props,
                                     uint32_t index) noexcept
    {
        for (size_t i = 0; i < props.size(); ++i) {
            if (props[i].index == index) {
                return &props[i];
            }
        }
        return nullptr;
    }

    static bool is_primary_auxl_item(const PrimaryProps& out,
                                     uint32_t item_id) noexcept
    {
        return find_primary_auxl_index(out, item_id) != UINT32_MAX;
    }


    static void bmff_apply_ipma_primary(
        std::span<const std::byte> bytes, const BmffBox& ipma,
        uint32_t primary_item_id, std::span<const IspeProp> ispe,
        std::span<const U8Prop> irot, std::span<const U8Prop> imir,
        std::span<const AuxCProp> auxc,
        PrimaryProps* out) noexcept
    {
        if (!out) {
            return;
        }

        const uint64_t payload_off  = ipma.offset + ipma.header_size;
        const uint64_t payload_size = ipma.size - ipma.header_size;
        if (payload_size < 8) {
            return;
        }

        const uint8_t version = u8(bytes[payload_off + 0]);

        uint32_t entry_count = 0;
        if (!read_u32be(bytes, payload_off + 4, &entry_count)) {
            return;
        }
        if (entry_count == 0) {
            return;
        }

        uint64_t off = payload_off + 8;
        const uint64_t end = payload_off + payload_size;
        const uint32_t kMaxEntries = 1U << 16;
        const uint32_t take_entries = (entry_count < kMaxEntries)
                                          ? entry_count
                                          : kMaxEntries;
        for (uint32_t i = 0; i < take_entries; ++i) {
            uint32_t item_id = 0;
            if (version < 1) {
                if (off + 2 > end) {
                    return;
                }
                uint16_t id16 = 0;
                if (!read_u16be(bytes, off, &id16)) {
                    return;
                }
                item_id = static_cast<uint32_t>(id16);
                off += 2;
            } else {
                if (off + 4 > end) {
                    return;
                }
                if (!read_u32be(bytes, off, &item_id)) {
                    return;
                }
                off += 4;
            }

            if (off + 1 > end) {
                return;
            }
            const uint8_t assoc_count = u8(bytes[off]);
            off += 1;

            const bool is_primary = (item_id == primary_item_id);
            const bool is_primary_aux
                = (!is_primary && is_primary_auxl_item(*out, item_id));

            if (version < 1) {
                for (uint32_t j = 0; j < assoc_count; ++j) {
                    if (off + 1 > end) {
                        return;
                    }
                    const uint8_t v = u8(bytes[off]);
                    off += 1;
                    const uint32_t prop_index = static_cast<uint32_t>(v & 0x7F);
                    if (prop_index != 0U) {
                        if (is_primary) {
                            if (const IspeProp* p = find_ispe(ispe, prop_index)) {
                                out->have_width_height = true;
                                out->width             = p->width;
                                out->height            = p->height;
                            }
                            if (const U8Prop* p = find_u8(irot, prop_index)) {
                                out->have_rotation     = true;
                                out->rotation_degrees
                                    = static_cast<uint16_t>(p->value) * 90U;
                            }
                            if (const U8Prop* p = find_u8(imir, prop_index)) {
                                out->have_mirror = true;
                                out->mirror      = p->value;
                            }
                        }
                        if (is_primary_aux) {
                            if (const AuxCProp* p = find_auxc(auxc, prop_index)) {
                                set_aux_item_semantic(out, item_id,
                                                      p->semantic);
                                if (p->aux_type_len > 0) {
                                    set_aux_item_type(
                                        out, item_id,
                                        std::string_view(p->aux_type.data(),
                                                         p->aux_type_len));
                                }
                                if (p->aux_subtype_len > 0
                                    || p->aux_subtype_total_len > 0) {
                                    set_aux_item_subtype(
                                        out, item_id,
                                        std::span<const std::byte>(
                                            p->aux_subtype.data(),
                                            p->aux_subtype_len),
                                        p->aux_subtype_total_len,
                                        p->aux_subtype_truncated);
                                }
                                set_primary_auxl_semantic(out, item_id,
                                                          p->semantic);
                            }
                        } else if (const AuxCProp* p = find_auxc(auxc,
                                                                 prop_index)) {
                            set_aux_item_semantic(out, item_id, p->semantic);
                            if (p->aux_type_len > 0) {
                                set_aux_item_type(
                                    out, item_id,
                                    std::string_view(p->aux_type.data(),
                                                     p->aux_type_len));
                            }
                            if (p->aux_subtype_len > 0
                                || p->aux_subtype_total_len > 0) {
                                set_aux_item_subtype(
                                    out, item_id,
                                    std::span<const std::byte>(
                                        p->aux_subtype.data(),
                                        p->aux_subtype_len),
                                    p->aux_subtype_total_len,
                                    p->aux_subtype_truncated);
                            }
                        }
                    }
                }
            } else {
                for (uint32_t j = 0; j < assoc_count; ++j) {
                    if (off + 2 > end) {
                        return;
                    }
                    uint16_t v = 0;
                    if (!read_u16be(bytes, off, &v)) {
                        return;
                    }
                    off += 2;
                    const uint32_t prop_index
                        = static_cast<uint32_t>(v & 0x7FFF);
                    if (prop_index != 0U) {
                        if (is_primary) {
                            if (const IspeProp* p = find_ispe(ispe, prop_index)) {
                                out->have_width_height = true;
                                out->width             = p->width;
                                out->height            = p->height;
                            }
                            if (const U8Prop* p = find_u8(irot, prop_index)) {
                                out->have_rotation     = true;
                                out->rotation_degrees
                                    = static_cast<uint16_t>(p->value) * 90U;
                            }
                            if (const U8Prop* p = find_u8(imir, prop_index)) {
                                out->have_mirror = true;
                                out->mirror      = p->value;
                            }
                        }
                        if (is_primary_aux) {
                            if (const AuxCProp* p = find_auxc(auxc, prop_index)) {
                                set_aux_item_semantic(out, item_id,
                                                      p->semantic);
                                if (p->aux_type_len > 0) {
                                    set_aux_item_type(
                                        out, item_id,
                                        std::string_view(p->aux_type.data(),
                                                         p->aux_type_len));
                                }
                                if (p->aux_subtype_len > 0
                                    || p->aux_subtype_total_len > 0) {
                                    set_aux_item_subtype(
                                        out, item_id,
                                        std::span<const std::byte>(
                                            p->aux_subtype.data(),
                                            p->aux_subtype_len),
                                        p->aux_subtype_total_len,
                                        p->aux_subtype_truncated);
                                }
                                set_primary_auxl_semantic(out, item_id,
                                                          p->semantic);
                            }
                        } else if (const AuxCProp* p = find_auxc(auxc,
                                                                 prop_index)) {
                            set_aux_item_semantic(out, item_id, p->semantic);
                            if (p->aux_type_len > 0) {
                                set_aux_item_type(
                                    out, item_id,
                                    std::string_view(p->aux_type.data(),
                                                     p->aux_type_len));
                            }
                            if (p->aux_subtype_len > 0
                                || p->aux_subtype_total_len > 0) {
                                set_aux_item_subtype(
                                    out, item_id,
                                    std::span<const std::byte>(
                                        p->aux_subtype.data(),
                                        p->aux_subtype_len),
                                    p->aux_subtype_total_len,
                                    p->aux_subtype_truncated);
                            }
                        }
                    }
                }
            }
        }
    }

    static bool bmff_collect_iref_edges(std::span<const std::byte> bytes,
                                        const BmffBox& iref,
                                        PrimaryProps* out) noexcept
    {
        if (!out) {
            return false;
        }

        const uint64_t payload_off = iref.offset + iref.header_size;
        const uint64_t payload_end = iref.offset + iref.size;
        if (payload_off + 4 > payload_end) {
            return false;
        }

        const uint8_t version = u8(bytes[payload_off + 0]);
        if (version > 1) {
            return false;
        }

        uint64_t off             = payload_off + 4;  // skip FullBox header
        const uint32_t kMaxBoxes = 1U << 16;
        uint32_t seen            = 0;
        const uint32_t kMaxRefsPerBox = 1U << 14;
        const uint32_t kMaxTotalRefs  = 1U << 18;
        while (off + 8 <= payload_end) {
            seen += 1;
            if (seen > kMaxBoxes) {
                return false;
            }

            BmffBox child;
            if (!parse_bmff_box(bytes, off, payload_end, &child)) {
                break;
            }

            const uint64_t child_payload_off = child.offset + child.header_size;
            const uint64_t child_payload_end = child.offset + child.size;
            if (child_payload_off > child_payload_end
                || child_payload_end > bytes.size()) {
                return false;
            }

            uint64_t p = child_payload_off;
            uint32_t from_item_id = 0;
            if (version == 0) {
                uint16_t from16 = 0;
                if (!read_u16be(bytes, p, &from16)) {
                    return false;
                }
                from_item_id = static_cast<uint32_t>(from16);
                p += 2;
            } else {
                if (!read_u32be(bytes, p, &from_item_id)) {
                    return false;
                }
                p += 4;
            }

            uint16_t ref_count = 0;
            if (!read_u16be(bytes, p, &ref_count)) {
                return false;
            }
            p += 2;
            if (ref_count > kMaxRefsPerBox) {
                return false;
            }

            for (uint32_t i = 0; i < ref_count; ++i) {
                uint32_t to_item_id = 0;
                if (version == 0) {
                    uint16_t to16 = 0;
                    if (!read_u16be(bytes, p, &to16)) {
                        return false;
                    }
                    to_item_id = static_cast<uint32_t>(to16);
                    p += 2;
                } else {
                    if (!read_u32be(bytes, p, &to_item_id)) {
                        return false;
                    }
                    p += 4;
                }

                if (!append_iref_edge(out, child.type, from_item_id,
                                      to_item_id)) {
                    return false;
                }
                if (out->iref_edge_total > kMaxTotalRefs) {
                    return false;
                }
            }

            off += child.size;
            if (child.size == 0) {
                break;
            }
        }
        return true;
    }


    static bool bmff_decode_meta_primary(std::span<const std::byte> bytes,
                                         const BmffBox& meta,
                                         PrimaryProps* out) noexcept
    {
        if (!out) {
            return false;
        }
        *out = PrimaryProps {};

        const uint64_t payload_off  = meta.offset + meta.header_size;
        const uint64_t payload_size = meta.size - meta.header_size;
        if (payload_size < 4) {
            return false;
        }

        BmffBox pitm {};
        BmffBox iprp {};
        BmffBox iref {};
        bool has_pitm = false;
        bool has_iprp = false;
        bool has_iref = false;

        uint64_t child_off       = payload_off + 4;  // FullBox header.
        const uint64_t child_end = meta.offset + meta.size;
        const uint32_t kMaxBoxes = 1U << 16;
        uint32_t seen            = 0;
        while (child_off + 8 <= child_end) {
            seen += 1;
            if (seen > kMaxBoxes) {
                return false;
            }

            BmffBox child;
            if (!parse_bmff_box(bytes, child_off, child_end, &child)) {
                break;
            }

            if (child.type == fourcc('p', 'i', 't', 'm')) {
                pitm     = child;
                has_pitm = true;
            } else if (child.type == fourcc('i', 'p', 'r', 'p')) {
                iprp     = child;
                has_iprp = true;
            } else if (child.type == fourcc('i', 'r', 'e', 'f')) {
                iref     = child;
                has_iref = true;
            }

            child_off += child.size;
            if (child.size == 0) {
                break;
            }
        }

        if (!has_pitm) {
            return false;
        }
        uint32_t primary_id = 0;
        if (!bmff_parse_pitm(bytes, pitm, &primary_id)) {
            return false;
        }
        out->have_item_id = true;
        out->item_id      = primary_id;

        if (has_iref) {
            if (!bmff_collect_iref_edges(bytes, iref, out)) {
                return false;
            }
        }

        if (!has_iprp) {
            return true;
        }

        BmffBox ipco {};
        BmffBox ipma {};
        bool has_ipco = false;
        bool has_ipma = false;

        const uint64_t iprp_payload_off = iprp.offset + iprp.header_size;
        const uint64_t iprp_payload_end = iprp.offset + iprp.size;
        if (iprp_payload_off > iprp_payload_end
            || iprp_payload_end > bytes.size()) {
            return true;
        }

        uint64_t off             = iprp_payload_off;
        const uint32_t kMaxBoxes2 = 1U << 16;
        uint32_t seen2            = 0;
        while (off + 8 <= iprp_payload_end) {
            seen2 += 1;
            if (seen2 > kMaxBoxes2) {
                break;
            }

            BmffBox child;
            if (!parse_bmff_box(bytes, off, iprp_payload_end, &child)) {
                break;
            }

            if (child.type == fourcc('i', 'p', 'c', 'o')) {
                ipco     = child;
                has_ipco = true;
            } else if (child.type == fourcc('i', 'p', 'm', 'a')) {
                ipma     = child;
                has_ipma = true;
            }

            off += child.size;
            if (child.size == 0) {
                break;
            }
        }

        if (!has_ipma) {
            return true;
        }

        std::array<IspeProp, 64> ispe {};
        std::array<U8Prop, 64> irot {};
        std::array<U8Prop, 64> imir {};
        std::array<AuxCProp, 64> auxc {};
        uint32_t ispe_count = 0;
        uint32_t irot_count = 0;
        uint32_t imir_count = 0;
        uint32_t auxc_count = 0;
        if (has_ipco) {
            bmff_collect_ipco_props(bytes, ipco, &ispe, &ispe_count, &irot,
                                    &irot_count, &imir, &imir_count, &auxc,
                                    &auxc_count);
        }

        bmff_apply_ipma_primary(
            bytes, ipma, primary_id,
            std::span<const IspeProp>(ispe.data(), ispe_count),
            std::span<const U8Prop>(irot.data(), irot_count),
            std::span<const U8Prop>(imir.data(), imir_count),
            std::span<const AuxCProp>(auxc.data(), auxc_count), out);
        return true;
    }


    struct ScanCtx final {
        std::span<const std::byte> bytes;
        MetaStore* store      = nullptr;
        BlockId block         = kInvalidBlockId;
        uint32_t* order       = nullptr;
        bool meta_done        = false;
        ContainerFormat format = ContainerFormat::Unknown;
        uint32_t seen_boxes   = 0;
    };


    static void bmff_scan_for_meta(std::span<const std::byte> bytes,
                                   uint64_t offset, uint64_t end,
                                   uint32_t depth, ScanCtx* ctx) noexcept
    {
        if (!ctx || ctx->meta_done) {
            return;
        }

        const uint32_t kMaxDepth = 16;
        const uint32_t kMaxBoxes = 1U << 16;
        if (depth > kMaxDepth) {
            return;
        }

        while (offset + 8 <= end) {
            ctx->seen_boxes += 1;
            if (ctx->seen_boxes > kMaxBoxes) {
                return;
            }

            BmffBox box;
            if (!parse_bmff_box(bytes, offset, end, &box)) {
                break;
            }

            if (box.type == fourcc('m', 'e', 't', 'a')) {
                PrimaryProps p {};
                if (bmff_decode_meta_primary(bytes, box, &p)
                    && p.have_item_id) {
                    emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                   "meta.primary_item_id", p.item_id);
                    if (p.have_width_height) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.width", p.width);
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.height", p.height);
                    }
                    if (p.have_rotation) {
                        emit_u16_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "primary.rotation_degrees",
                                       p.rotation_degrees);
                    }
                    if (p.have_mirror) {
                        emit_u8_field(*ctx->store, ctx->block, (*ctx->order)++,
                                      "primary.mirror", p.mirror);
                    }
                    if (p.iref_edge_total > 0) {
                        emit_u32_field(*ctx->store, ctx->block, (*ctx->order)++,
                                       "iref.edge_count", p.iref_edge_total);
                        if (p.iref_truncated) {
                            emit_u8_field(*ctx->store, ctx->block,
                                          (*ctx->order)++,
                                          "iref.edge_truncated", 1);
                        }
                        for (uint32_t i = 0; i < p.iref_edge_count; ++i) {
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++, "iref.ref_type",
                                           p.iref_edges[i].ref_type);
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "iref.from_item_id",
                                           p.iref_edges[i].from_item_id);
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++, "iref.to_item_id",
                                           p.iref_edges[i].to_item_id);
                            if (p.iref_edges[i].ref_type
                                == fourcc('a', 'u', 'x', 'l')) {
                                emit_u32_field(
                                    *ctx->store, ctx->block, (*ctx->order)++,
                                    "iref.auxl.from_item_id",
                                    p.iref_edges[i].from_item_id);
                                emit_u32_field(
                                    *ctx->store, ctx->block, (*ctx->order)++,
                                    "iref.auxl.to_item_id",
                                    p.iref_edges[i].to_item_id);
                                emit_text_field(
                                    *ctx->store, ctx->block, (*ctx->order)++,
                                    "iref.auxl.semantic",
                                    aux_semantic_name(find_aux_item_semantic(
                                        p, p.iref_edges[i].to_item_id)));
                                if (const AuxItemInfo* info
                                    = find_aux_item_info(
                                        p, p.iref_edges[i].to_item_id)) {
                                    if (info->aux_type_len > 0) {
                                        emit_text_field(
                                            *ctx->store, ctx->block,
                                            (*ctx->order)++,
                                            "iref.auxl.type",
                                            std::string_view(
                                                info->aux_type.data(),
                                                info->aux_type_len));
                                    }
                                    if (info->aux_subtype_len > 0) {
                                        const AuxSubtypeInterpretation interp
                                            = interpret_aux_subtype(
                                                std::span<const std::byte>(
                                                    info->aux_subtype.data(),
                                                    info->aux_subtype_len),
                                                info->aux_subtype_total_len,
                                                info->aux_subtype_truncated);
                                        emit_text_field(
                                            *ctx->store, ctx->block,
                                            (*ctx->order)++,
                                            "iref.auxl.subtype_kind",
                                            interp.kind);
                                        if (interp.has_text) {
                                            emit_text_field(
                                                *ctx->store, ctx->block,
                                                (*ctx->order)++,
                                                "iref.auxl.subtype_text",
                                                std::string_view(
                                                    interp.text.data(),
                                                    interp.text_len));
                                        }
                                        if (interp.has_u32) {
                                            emit_u32_field(
                                                *ctx->store, ctx->block,
                                                (*ctx->order)++,
                                                "iref.auxl.subtype_u32",
                                                interp.u32);
                                        }
                                        const std::string hex
                                            = bytes_to_hex_string(
                                                std::span<const std::byte>(
                                                    info->aux_subtype.data(),
                                                    info->aux_subtype_len));
                                        emit_text_field(
                                            *ctx->store, ctx->block,
                                            (*ctx->order)++,
                                            "iref.auxl.subtype_hex", hex);
                                    }
                                }
                            }
                        }
                        for (uint32_t i = 0; i < p.aux_item_count; ++i) {
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++, "aux.item_id",
                                           p.aux_items[i].item_id);
                            emit_text_field(*ctx->store, ctx->block,
                                            (*ctx->order)++, "aux.semantic",
                                            aux_semantic_name(p.aux_items[i].semantic));
                            if (p.aux_items[i].aux_type_len > 0) {
                                emit_text_field(
                                    *ctx->store, ctx->block, (*ctx->order)++,
                                    "aux.type",
                                    std::string_view(
                                        p.aux_items[i].aux_type.data(),
                                        p.aux_items[i].aux_type_len));
                            }
                            if (p.aux_items[i].aux_subtype_len > 0) {
                                const AuxSubtypeInterpretation interp
                                    = interpret_aux_subtype(
                                        std::span<const std::byte>(
                                            p.aux_items[i].aux_subtype.data(),
                                            p.aux_items[i].aux_subtype_len),
                                        p.aux_items[i].aux_subtype_total_len,
                                        p.aux_items[i].aux_subtype_truncated);
                                emit_text_field(
                                    *ctx->store, ctx->block, (*ctx->order)++,
                                    "aux.subtype_kind", interp.kind);
                                if (interp.has_text) {
                                    emit_text_field(
                                        *ctx->store, ctx->block,
                                        (*ctx->order)++, "aux.subtype_text",
                                        std::string_view(interp.text.data(),
                                                         interp.text_len));
                                }
                                if (interp.has_u32) {
                                    emit_u32_field(
                                        *ctx->store, ctx->block,
                                        (*ctx->order)++, "aux.subtype_u32",
                                        interp.u32);
                                }
                                const std::string hex = bytes_to_hex_string(
                                    std::span<const std::byte>(
                                        p.aux_items[i].aux_subtype.data(),
                                        p.aux_items[i].aux_subtype_len));
                                emit_text_field(*ctx->store, ctx->block,
                                                (*ctx->order)++,
                                                "aux.subtype_hex", hex);
                                emit_u32_field(
                                    *ctx->store, ctx->block, (*ctx->order)++,
                                    "aux.subtype_len",
                                    static_cast<uint32_t>(
                                        p.aux_items[i].aux_subtype_total_len));
                                if (p.aux_items[i].aux_subtype_truncated) {
                                    emit_u8_field(
                                        *ctx->store, ctx->block,
                                        (*ctx->order)++,
                                        "aux.subtype_truncated", 1);
                                }
                            }
                        }
                        for (uint32_t i = 0; i < p.primary_auxl_count; ++i) {
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "primary.auxl_item_id",
                                           p.primary_auxl_item_ids[i]);
                            emit_text_field(*ctx->store, ctx->block,
                                            (*ctx->order)++,
                                            "primary.auxl_semantic",
                                            aux_semantic_name(
                                                p.primary_auxl_semantics[i]));
                        }
                        for (uint32_t i = 0; i < p.primary_alpha_count; ++i) {
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "primary.alpha_item_id",
                                           p.primary_alpha_item_ids[i]);
                        }
                        for (uint32_t i = 0; i < p.primary_depth_count; ++i) {
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "primary.depth_item_id",
                                           p.primary_depth_item_ids[i]);
                        }
                        for (uint32_t i = 0;
                             i < p.primary_disparity_count; ++i) {
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "primary.disparity_item_id",
                                           p.primary_disparity_item_ids[i]);
                        }
                        for (uint32_t i = 0; i < p.primary_matte_count; ++i) {
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "primary.matte_item_id",
                                           p.primary_matte_item_ids[i]);
                        }
                        for (uint32_t i = 0; i < p.primary_dimg_count; ++i) {
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "primary.dimg_item_id",
                                           p.primary_dimg_item_ids[i]);
                        }
                        for (uint32_t i = 0; i < p.primary_thmb_count; ++i) {
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "primary.thmb_item_id",
                                           p.primary_thmb_item_ids[i]);
                        }
                        for (uint32_t i = 0; i < p.primary_cdsc_count; ++i) {
                            emit_u32_field(*ctx->store, ctx->block,
                                           (*ctx->order)++,
                                           "primary.cdsc_item_id",
                                           p.primary_cdsc_item_ids[i]);
                        }
                    }
                    ctx->meta_done = true;
                    return;
                }
            } else if (bmff_is_container_box(box.type)) {
                const uint64_t child_off = box.offset + box.header_size;
                const uint64_t child_end = box.offset + box.size;
                if (child_off < child_end && child_end <= bytes.size()) {
                    bmff_scan_for_meta(bytes, child_off, child_end, depth + 1,
                                       ctx);
                    if (ctx->meta_done) {
                        return;
                    }
                }
            }

            offset += box.size;
            if (box.size == 0) {
                break;
            }
        }
    }

}  // namespace


namespace bmff_internal {

    void decode_bmff_derived_fields(std::span<const std::byte> file_bytes,
                                    MetaStore& store) noexcept
    {
        BmffBox ftyp;
        if (!parse_bmff_box(file_bytes, 0, file_bytes.size(), &ftyp)) {
            return;
        }
        if (ftyp.type != fourcc('f', 't', 'y', 'p')) {
            return;
        }

        ContainerFormat fmt = ContainerFormat::Unknown;
        uint32_t major_brand = 0;
        uint32_t minor_version = 0;
        std::array<uint32_t, 32> compat {};
        uint32_t compat_count = 0;
        if (!bmff_parse_ftyp(file_bytes, ftyp, &fmt, &major_brand,
                             &minor_version, &compat, &compat_count)) {
            return;
        }

        const BlockId block = store.add_block(BlockInfo {});
        if (block == kInvalidBlockId) {
            return;
        }

        uint32_t order = 0;
        emit_u32_field(store, block, order++, "ftyp.major_brand", major_brand);
        emit_u32_field(store, block, order++, "ftyp.minor_version",
                       minor_version);
        if (compat_count > 0) {
            emit_u32_array_field(
                store, block, order++, "ftyp.compat_brands",
                std::span<const uint32_t>(compat.data(), compat_count));
        }

        ScanCtx ctx;
        ctx.bytes   = file_bytes;
        ctx.store   = &store;
        ctx.block   = block;
        ctx.order   = &order;
        ctx.format  = fmt;
        ctx.meta_done = false;
        bmff_scan_for_meta(file_bytes, 0, file_bytes.size(), 0, &ctx);
    }

}  // namespace bmff_internal

}  // namespace openmeta
