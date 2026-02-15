#include "openmeta/jumbf_decode.h"

#include "openmeta/container_scan.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace openmeta {
namespace {

    struct BmffBox final {
        uint64_t offset      = 0;
        uint64_t size        = 0;
        uint64_t header_size = 0;
        uint32_t type        = 0;
    };

    struct DecodeContext final {
        MetaStore* store = nullptr;
        BlockId block    = kInvalidBlockId;
        EntryFlags flags = EntryFlags::None;
        JumbfDecodeOptions options;
        JumbfDecodeResult result;
        uint32_t order_in_block = 0;
        bool c2pa_emitted       = false;
    };

    static constexpr uint8_t u8(std::byte b) noexcept
    {
        return static_cast<uint8_t>(b);
    }


    static bool read_u16be(std::span<const std::byte> bytes, uint64_t offset,
                           uint16_t* out) noexcept
    {
        if (!out || offset + 2U > bytes.size()) {
            return false;
        }
        *out = static_cast<uint16_t>(u8(bytes[offset + 0U]) << 8U)
               | static_cast<uint16_t>(u8(bytes[offset + 1U]) << 0U);
        return true;
    }


    static bool read_u32be(std::span<const std::byte> bytes, uint64_t offset,
                           uint32_t* out) noexcept
    {
        if (!out || offset + 4U > bytes.size()) {
            return false;
        }
        uint32_t value = 0;
        value |= static_cast<uint32_t>(u8(bytes[offset + 0U])) << 24U;
        value |= static_cast<uint32_t>(u8(bytes[offset + 1U])) << 16U;
        value |= static_cast<uint32_t>(u8(bytes[offset + 2U])) << 8U;
        value |= static_cast<uint32_t>(u8(bytes[offset + 3U])) << 0U;
        *out = value;
        return true;
    }


    static bool read_u64be(std::span<const std::byte> bytes, uint64_t offset,
                           uint64_t* out) noexcept
    {
        if (!out || offset + 8U > bytes.size()) {
            return false;
        }
        uint64_t value = 0;
        for (uint32_t index = 0; index < 8U; ++index) {
            value = (value << 8U)
                    | static_cast<uint64_t>(u8(bytes[offset + index]));
        }
        *out = value;
        return true;
    }


    static bool parse_bmff_box(std::span<const std::byte> bytes,
                               uint64_t offset, uint64_t parent_end,
                               BmffBox* out) noexcept
    {
        if (!out || offset + 8U > parent_end || parent_end > bytes.size()) {
            return false;
        }

        uint32_t size32 = 0;
        uint32_t type   = 0;
        if (!read_u32be(bytes, offset + 0U, &size32)
            || !read_u32be(bytes, offset + 4U, &type)) {
            return false;
        }

        uint64_t header_size = 8U;
        uint64_t box_size    = static_cast<uint64_t>(size32);
        if (size32 == 1U) {
            uint64_t size64 = 0;
            if (!read_u64be(bytes, offset + 8U, &size64)) {
                return false;
            }
            header_size = 16U;
            box_size    = size64;
        } else if (size32 == 0U) {
            box_size = parent_end - offset;
        }

        if (box_size < header_size) {
            return false;
        }
        if (offset > parent_end || box_size > parent_end - offset) {
            return false;
        }
        if (offset > bytes.size() || box_size > bytes.size() - offset) {
            return false;
        }

        out->offset      = offset;
        out->size        = box_size;
        out->header_size = header_size;
        out->type        = type;
        return true;
    }


    static bool looks_like_bmff_sequence(std::span<const std::byte> bytes,
                                         uint64_t begin, uint64_t end) noexcept
    {
        if (begin >= end || end > bytes.size()) {
            return false;
        }
        BmffBox box;
        return parse_bmff_box(bytes, begin, end, &box);
    }


    static bool is_printable_ascii(uint8_t c) noexcept
    {
        return c >= 0x20U && c <= 0x7EU;
    }


    static std::string fourcc_to_text(uint32_t value)
    {
        char out[5];
        out[0] = static_cast<char>((value >> 24U) & 0xFFU);
        out[1] = static_cast<char>((value >> 16U) & 0xFFU);
        out[2] = static_cast<char>((value >> 8U) & 0xFFU);
        out[3] = static_cast<char>((value >> 0U) & 0xFFU);
        out[4] = '\0';
        if (is_printable_ascii(static_cast<uint8_t>(out[0]))
            && is_printable_ascii(static_cast<uint8_t>(out[1]))
            && is_printable_ascii(static_cast<uint8_t>(out[2]))
            && is_printable_ascii(static_cast<uint8_t>(out[3]))) {
            return std::string(out, out + 4);
        }
        char hex[16];
        std::snprintf(hex, sizeof(hex), "0x%08X", static_cast<unsigned>(value));
        return std::string(hex);
    }


    static bool has_entry_room(DecodeContext* ctx) noexcept
    {
        if (!ctx) {
            return false;
        }
        const uint32_t max_entries = ctx->options.limits.max_entries;
        if (max_entries != 0U && ctx->result.entries_decoded >= max_entries) {
            ctx->result.status = JumbfDecodeStatus::LimitExceeded;
            return false;
        }
        return true;
    }


    static bool emit_field_text(DecodeContext* ctx, std::string_view field,
                                std::string_view value,
                                EntryFlags extra_flags) noexcept
    {
        if (!ctx || !ctx->store || !has_entry_room(ctx)) {
            return false;
        }
        Entry entry;
        entry.key          = make_jumbf_field_key(ctx->store->arena(), field);
        entry.value        = make_text(ctx->store->arena(), value,
                                       TextEncoding::Ascii);
        entry.origin.block = ctx->block;
        entry.origin.order_in_block = ctx->order_in_block++;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = 1U;
        entry.flags                 = ctx->flags | extra_flags;
        (void)ctx->store->add_entry(entry);
        ctx->result.entries_decoded += 1U;
        return true;
    }


    static bool emit_field_u64(DecodeContext* ctx, std::string_view field,
                               uint64_t value, EntryFlags extra_flags) noexcept
    {
        if (!ctx || !ctx->store || !has_entry_room(ctx)) {
            return false;
        }
        Entry entry;
        entry.key          = make_jumbf_field_key(ctx->store->arena(), field);
        entry.value        = make_u64(value);
        entry.origin.block = ctx->block;
        entry.origin.order_in_block = ctx->order_in_block++;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = 1U;
        entry.flags                 = ctx->flags | extra_flags;
        (void)ctx->store->add_entry(entry);
        ctx->result.entries_decoded += 1U;
        return true;
    }


    static bool emit_field_u8(DecodeContext* ctx, std::string_view field,
                              uint8_t value, EntryFlags extra_flags) noexcept
    {
        if (!ctx || !ctx->store || !has_entry_room(ctx)) {
            return false;
        }
        Entry entry;
        entry.key          = make_jumbf_field_key(ctx->store->arena(), field);
        entry.value        = make_u8(value);
        entry.origin.block = ctx->block;
        entry.origin.order_in_block = ctx->order_in_block++;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = 1U;
        entry.flags                 = ctx->flags | extra_flags;
        (void)ctx->store->add_entry(entry);
        ctx->result.entries_decoded += 1U;
        return true;
    }


    static bool emit_cbor_value(DecodeContext* ctx, std::string_view key,
                                const MetaValue& value) noexcept
    {
        if (!ctx || !ctx->store || !has_entry_room(ctx)) {
            return false;
        }
        Entry entry;
        entry.key          = make_jumbf_cbor_key(ctx->store->arena(), key);
        entry.value        = value;
        entry.origin.block = ctx->block;
        entry.origin.order_in_block = ctx->order_in_block++;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = 1U;
        entry.flags                 = ctx->flags;
        (void)ctx->store->add_entry(entry);
        ctx->result.entries_decoded += 1U;
        return true;
    }


    static void make_child_path(std::string_view parent, uint32_t child_index,
                                std::string* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        if (parent.empty()) {
            out->append("box.");
        } else {
            out->append(parent.data(), parent.size());
            out->push_back('.');
        }
        out->append(std::to_string(static_cast<unsigned>(child_index)));
    }


    static void make_field_key(std::string_view path, std::string_view suffix,
                               std::string* out) noexcept
    {
        if (!out) {
            return;
        }
        out->clear();
        out->append(path.data(), path.size());
        out->push_back('.');
        out->append(suffix.data(), suffix.size());
    }


    static bool append_c2pa_marker(DecodeContext* ctx,
                                   std::string_view marker_path) noexcept
    {
        if (!ctx || ctx->c2pa_emitted) {
            return true;
        }
        if (!emit_field_u8(ctx, "c2pa.detected", 1U, EntryFlags::Derived)) {
            return false;
        }
        if (!marker_path.empty()) {
            if (!emit_field_text(ctx, "c2pa.marker_path", marker_path,
                                 EntryFlags::Derived)) {
                return false;
            }
        }
        ctx->c2pa_emitted = true;
        return true;
    }

    static std::string_view arena_string_view(const ByteArena& arena,
                                              ByteSpan span) noexcept
    {
        const std::span<const std::byte> bytes = arena.span(span);
        return std::string_view(reinterpret_cast<const char*>(bytes.data()),
                                bytes.size());
    }

    static bool cbor_path_separator(char c) noexcept
    {
        return c == '.' || c == '[' || c == ']' || c == '@';
    }

    static bool cbor_key_has_segment(std::string_view key,
                                     std::string_view segment) noexcept
    {
        if (key.empty() || segment.empty()) {
            return false;
        }
        size_t pos = 0U;
        while (true) {
            pos = key.find(segment, pos);
            if (pos == std::string_view::npos) {
                return false;
            }
            const size_t end = pos + segment.size();
            const bool left_ok
                = (pos == 0U) || cbor_path_separator(key[pos - 1U]);
            const bool right_ok
                = (end >= key.size()) || cbor_path_separator(key[end]);
            if (left_ok && right_ok) {
                return true;
            }
            pos += 1U;
        }
    }

    static bool bytes_all_ascii_printable(std::span<const std::byte> bytes) noexcept
    {
        for (const std::byte b : bytes) {
            const uint8_t c = u8(b);
            if (c < 0x20U || c > 0x7EU) {
                return false;
            }
        }
        return true;
    }

    static bool append_c2pa_semantic_fields(DecodeContext* ctx) noexcept
    {
        if (!ctx || !ctx->store) {
            return false;
        }

        uint64_t cbor_key_count      = 0U;
        uint64_t assertion_key_hits  = 0U;
        bool has_manifest            = false;
        bool has_claim               = false;
        bool has_assertions          = false;
        bool has_signature           = false;
        bool has_claim_generator     = false;
        std::string claim_generator;

        const std::span<const Entry> entries = ctx->store->entries();
        for (const Entry& e : entries) {
            if (e.origin.block != ctx->block
                || e.key.kind != MetaKeyKind::JumbfCborKey) {
                continue;
            }
            cbor_key_count += 1U;

            const std::string_view key = arena_string_view(
                ctx->store->arena(), e.key.data.jumbf_cbor_key.key);
            if (cbor_key_has_segment(key, "manifest")
                || cbor_key_has_segment(key, "manifests")) {
                has_manifest = true;
            }
            if (cbor_key_has_segment(key, "claim")
                || cbor_key_has_segment(key, "claims")) {
                has_claim = true;
            }
            if (cbor_key_has_segment(key, "assertion")
                || cbor_key_has_segment(key, "assertions")) {
                has_assertions = true;
                assertion_key_hits += 1U;
            }
            if (cbor_key_has_segment(key, "signature")
                || cbor_key_has_segment(key, "signatures")) {
                has_signature = true;
            }

            if (!has_claim_generator
                && cbor_key_has_segment(key, "claim_generator")
                && e.value.kind == MetaValueKind::Text) {
                const std::span<const std::byte> text = ctx->store->arena().span(
                    e.value.data.span);
                if (bytes_all_ascii_printable(text)) {
                    claim_generator.assign(
                        reinterpret_cast<const char*>(text.data()),
                        text.size());
                    has_claim_generator = true;
                }
            }
        }

        if (cbor_key_count == 0U) {
            return true;
        }

        if (has_manifest || has_claim || has_assertions || has_signature) {
            if (!append_c2pa_marker(ctx, "cbor.semantic")) {
                return false;
            }
        }

        if (!emit_field_u64(ctx, "c2pa.semantic.cbor_key_count", cbor_key_count,
                            EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.semantic.manifest_present",
                           has_manifest ? 1U : 0U, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.semantic.claim_present",
                           has_claim ? 1U : 0U, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.semantic.assertion_present",
                           has_assertions ? 1U : 0U, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u8(ctx, "c2pa.semantic.signature_present",
                           has_signature ? 1U : 0U, EntryFlags::Derived)) {
            return false;
        }
        if (!emit_field_u64(ctx, "c2pa.semantic.assertion_key_hits",
                            assertion_key_hits, EntryFlags::Derived)) {
            return false;
        }
        if (has_claim_generator) {
            if (!emit_field_text(ctx, "c2pa.semantic.claim_generator",
                                 claim_generator, EntryFlags::Derived)) {
                return false;
            }
        }
        return true;
    }


    static bool ascii_icase_contains(std::span<const std::byte> bytes,
                                     std::string_view needle,
                                     uint32_t max_bytes) noexcept
    {
        if (needle.empty() || bytes.empty()) {
            return false;
        }
        const size_t haystack_size = (max_bytes != 0U
                                      && bytes.size() > max_bytes)
                                         ? static_cast<size_t>(max_bytes)
                                         : bytes.size();
        if (haystack_size < needle.size()) {
            return false;
        }
        for (size_t i = 0; i + needle.size() <= haystack_size; ++i) {
            bool match = true;
            for (size_t j = 0; j < needle.size(); ++j) {
                uint8_t a = u8(bytes[i + j]);
                uint8_t b = static_cast<uint8_t>(needle[j]);
                if (a >= 'A' && a <= 'Z') {
                    a = static_cast<uint8_t>(a + 32U);
                }
                if (b >= 'A' && b <= 'Z') {
                    b = static_cast<uint8_t>(b + 32U);
                }
                if (a != b) {
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


    static bool bytes_valid_utf8(std::span<const std::byte> bytes) noexcept
    {
        size_t index = 0;
        while (index < bytes.size()) {
            const uint8_t c0 = u8(bytes[index]);
            if ((c0 & 0x80U) == 0U) {
                index += 1U;
                continue;
            }

            uint32_t needed    = 0U;
            uint32_t min_cp    = 0U;
            uint32_t codepoint = 0U;

            if ((c0 & 0xE0U) == 0xC0U) {
                needed    = 1U;
                min_cp    = 0x80U;
                codepoint = c0 & 0x1FU;
            } else if ((c0 & 0xF0U) == 0xE0U) {
                needed    = 2U;
                min_cp    = 0x800U;
                codepoint = c0 & 0x0FU;
            } else if ((c0 & 0xF8U) == 0xF0U) {
                needed    = 3U;
                min_cp    = 0x10000U;
                codepoint = c0 & 0x07U;
            } else {
                return false;
            }

            if (index + needed >= bytes.size()) {
                return false;
            }

            for (uint32_t j = 0U; j < needed; ++j) {
                const uint8_t c = u8(bytes[index + 1U + j]);
                if ((c & 0xC0U) != 0x80U) {
                    return false;
                }
                codepoint = (codepoint << 6U)
                            | static_cast<uint32_t>(c & 0x3FU);
            }

            if (codepoint < min_cp || codepoint > 0x10FFFFU
                || (codepoint >= 0xD800U && codepoint <= 0xDFFFU)) {
                return false;
            }
            index += static_cast<size_t>(needed + 1U);
        }
        return true;
    }


    static bool sanitize_cbor_path_segment(std::span<const std::byte> bytes,
                                           uint32_t max_output_bytes,
                                           std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        if (bytes.empty()) {
            out->assign("_");
            return true;
        }

        const size_t limit = (max_output_bytes != 0U
                              && bytes.size() > max_output_bytes)
                                 ? static_cast<size_t>(max_output_bytes)
                                 : bytes.size();
        for (size_t i = 0; i < limit; ++i) {
            const uint8_t c    = u8(bytes[i]);
            const bool allowed = (c >= 'a' && c <= 'z')
                                 || (c >= 'A' && c <= 'Z')
                                 || (c >= '0' && c <= '9') || c == '_'
                                 || c == '-' || c == '.';
            if (allowed) {
                out->push_back(static_cast<char>(c));
            } else {
                out->push_back('_');
            }
        }
        if (out->empty()) {
            out->assign("_");
        }
        return true;
    }

    struct CborHead final {
        uint8_t major = 0U;
        uint8_t addl  = 0U;
        uint64_t arg  = 0U;
        bool indefinite = false;
    };

    static bool read_cbor_head(std::span<const std::byte> bytes, uint64_t* pos,
                               CborHead* out) noexcept
    {
        if (!pos || !out || *pos >= bytes.size()) {
            return false;
        }
        const uint8_t ib = u8(bytes[*pos]);
        *pos += 1U;
        out->major = static_cast<uint8_t>((ib >> 5U) & 0x07U);
        out->addl  = static_cast<uint8_t>(ib & 0x1FU);
        out->indefinite = false;

        if (out->addl <= 23U) {
            out->arg = static_cast<uint64_t>(out->addl);
            return true;
        }
        if (out->addl == 24U) {
            if (*pos + 1U > bytes.size()) {
                return false;
            }
            out->arg = static_cast<uint64_t>(u8(bytes[*pos]));
            *pos += 1U;
            return true;
        }
        if (out->addl == 25U) {
            uint16_t value = 0;
            if (!read_u16be(bytes, *pos, &value)) {
                return false;
            }
            out->arg = static_cast<uint64_t>(value);
            *pos += 2U;
            return true;
        }
        if (out->addl == 26U) {
            uint32_t value = 0;
            if (!read_u32be(bytes, *pos, &value)) {
                return false;
            }
            out->arg = static_cast<uint64_t>(value);
            *pos += 4U;
            return true;
        }
        if (out->addl == 27U) {
            uint64_t value = 0;
            if (!read_u64be(bytes, *pos, &value)) {
                return false;
            }
            out->arg = value;
            *pos += 8U;
            return true;
        }
        if (out->addl == 31U) {
            out->indefinite = true;
            out->arg        = 0U;
            // Break marker (0xFF) is not a data item and must be handled by
            // callers that parse indefinite-length containers.
            return out->major != 7U;
        }
        // Unsupported: reserved addl values.
        return false;
    }


    static bool cbor_item_budget_take(DecodeContext* ctx) noexcept
    {
        if (!ctx) {
            return false;
        }
        ctx->result.cbor_items += 1U;
        const uint32_t max_items = ctx->options.limits.max_cbor_items;
        if (max_items != 0U && ctx->result.cbor_items > max_items) {
            ctx->result.status = JumbfDecodeStatus::LimitExceeded;
            return false;
        }
        return true;
    }


    static bool cbor_depth_ok(DecodeContext* ctx, uint32_t depth) noexcept
    {
        if (!ctx) {
            return false;
        }
        const uint32_t max_depth = ctx->options.limits.max_cbor_depth;
        if (max_depth != 0U && depth > max_depth) {
            ctx->result.status = JumbfDecodeStatus::LimitExceeded;
            return false;
        }
        return true;
    }

    static bool read_cbor_text(std::span<const std::byte> cbor, uint64_t* pos,
                               uint64_t len,
                               std::span<const std::byte>* out) noexcept
    {
        if (!out || !pos) {
            return false;
        }
        if (*pos > cbor.size() || len > cbor.size() - *pos) {
            return false;
        }
        *out = cbor.subspan(static_cast<size_t>(*pos),
                            static_cast<size_t>(len));
        *pos += len;
        return true;
    }

    static bool cbor_peek_break(std::span<const std::byte> cbor,
                                uint64_t pos) noexcept
    {
        if (pos >= cbor.size()) {
            return false;
        }
        return u8(cbor[pos]) == 0xFFU;
    }

    static bool cbor_consume_break(std::span<const std::byte> cbor,
                                   uint64_t* pos) noexcept
    {
        if (!pos || !cbor_peek_break(cbor, *pos)) {
            return false;
        }
        *pos += 1U;
        return true;
    }

    static const char* cbor_major_suffix(uint8_t major) noexcept
    {
        switch (major) {
            case 0U:
                return "u";
            case 1U:
                return "n";
            case 2U:
                return "bytes";
            case 3U:
                return "text";
            case 4U:
                return "arr";
            case 5U:
                return "map";
            case 6U:
                return "tag";
            case 7U:
                return "simple";
            default:
                return "key";
        }
    }

    static bool assign_synth_cbor_key(uint32_t map_index,
                                      std::string_view suffix,
                                      uint32_t max_output_bytes,
                                      std::string* out) noexcept
    {
        if (!out) {
            return false;
        }
        out->clear();
        out->reserve(24U + suffix.size());
        out->append("k");
        out->append(
            std::to_string(static_cast<unsigned long long>(map_index)));
        out->push_back('_');
        out->append(suffix.data(), suffix.size());
        if (max_output_bytes != 0U
            && out->size() > static_cast<size_t>(max_output_bytes)) {
            out->resize(static_cast<size_t>(max_output_bytes));
        }
        if (out->empty()) {
            out->assign("_");
        }
        return true;
    }

    static bool skip_cbor_item(DecodeContext* ctx, std::span<const std::byte> cbor,
                               uint64_t* pos, uint32_t depth) noexcept;

    static bool skip_cbor_item_from_head(DecodeContext* ctx,
                                         std::span<const std::byte> cbor,
                                         uint64_t* pos, uint32_t depth,
                                         const CborHead& head) noexcept
    {
        if (!ctx || !pos || !cbor_depth_ok(ctx, depth)) {
            return false;
        }

        if (head.major == 0U || head.major == 1U || head.major == 7U) {
            return true;
        }

        if (head.major == 2U || head.major == 3U) {
            if (!head.indefinite) {
                const uint64_t max_len
                    = (head.major == 2U)
                          ? ctx->options.limits.max_cbor_bytes_bytes
                          : ctx->options.limits.max_cbor_text_bytes;
                if (max_len != 0U && head.arg > max_len) {
                    ctx->result.status = JumbfDecodeStatus::LimitExceeded;
                    return false;
                }
                if (*pos > cbor.size() || head.arg > cbor.size() - *pos) {
                    return false;
                }
                *pos += head.arg;
                return true;
            }

            uint64_t total_len = 0U;
            while (true) {
                if (cbor_peek_break(cbor, *pos)) {
                    return cbor_consume_break(cbor, pos);
                }
                CborHead chunk;
                if (!read_cbor_head(cbor, pos, &chunk)
                    || !cbor_item_budget_take(ctx)) {
                    return false;
                }
                if (chunk.major != head.major || chunk.indefinite) {
                    return false;
                }
                if (total_len > UINT64_MAX - chunk.arg) {
                    return false;
                }
                total_len += chunk.arg;
                const uint64_t max_len
                    = (head.major == 2U)
                          ? ctx->options.limits.max_cbor_bytes_bytes
                          : ctx->options.limits.max_cbor_text_bytes;
                if (max_len != 0U && total_len > max_len) {
                    ctx->result.status = JumbfDecodeStatus::LimitExceeded;
                    return false;
                }
                if (*pos > cbor.size() || chunk.arg > cbor.size() - *pos) {
                    return false;
                }
                *pos += chunk.arg;
            }
        }

        if (head.major == 4U) {
            if (!head.indefinite) {
                for (uint64_t index = 0; index < head.arg; ++index) {
                    if (!skip_cbor_item(ctx, cbor, pos, depth + 1U)) {
                        return false;
                    }
                }
                return true;
            }
            while (true) {
                if (cbor_peek_break(cbor, *pos)) {
                    return cbor_consume_break(cbor, pos);
                }
                if (!skip_cbor_item(ctx, cbor, pos, depth + 1U)) {
                    return false;
                }
            }
        }

        if (head.major == 5U) {
            if (!head.indefinite) {
                for (uint64_t pair = 0; pair < head.arg; ++pair) {
                    if (!skip_cbor_item(ctx, cbor, pos, depth + 1U)
                        || !skip_cbor_item(ctx, cbor, pos, depth + 1U)) {
                        return false;
                    }
                }
                return true;
            }
            while (true) {
                if (cbor_peek_break(cbor, *pos)) {
                    return cbor_consume_break(cbor, pos);
                }
                if (!skip_cbor_item(ctx, cbor, pos, depth + 1U)
                    || !skip_cbor_item(ctx, cbor, pos, depth + 1U)) {
                    return false;
                }
            }
        }

        if (head.major == 6U) {
            if (head.indefinite) {
                return false;
            }
            return skip_cbor_item(ctx, cbor, pos, depth + 1U);
        }

        return false;
    }

    static bool skip_cbor_item(DecodeContext* ctx, std::span<const std::byte> cbor,
                               uint64_t* pos, uint32_t depth) noexcept
    {
        if (!ctx || !pos || !cbor_depth_ok(ctx, depth)) {
            return false;
        }
        CborHead head;
        if (!read_cbor_head(cbor, pos, &head) || !cbor_item_budget_take(ctx)) {
            return false;
        }
        return skip_cbor_item_from_head(ctx, cbor, pos, depth, head);
    }

    static uint32_t cbor_half_to_f32_bits(uint16_t half_bits) noexcept
    {
        const uint32_t sign
            = static_cast<uint32_t>(half_bits & 0x8000U) << 16U;
        uint32_t exp  = static_cast<uint32_t>((half_bits >> 10U) & 0x1FU);
        uint32_t frac = static_cast<uint32_t>(half_bits & 0x03FFU);

        if (exp == 0U) {
            if (frac == 0U) {
                return sign;
            }

            int32_t shift = 0;
            while ((frac & 0x0400U) == 0U) {
                frac <<= 1U;
                shift += 1;
            }
            frac &= 0x03FFU;
            exp = static_cast<uint32_t>(127 - 15 - shift + 1);
            return sign | (exp << 23U) | (frac << 13U);
        }

        if (exp == 31U) {
            return sign | 0x7F800000U | (frac << 13U);
        }

        exp = exp + static_cast<uint32_t>(127 - 15);
        return sign | (exp << 23U) | (frac << 13U);
    }

    static bool append_cbor_chunk(std::span<const std::byte> bytes,
                                  uint32_t max_total,
                                  std::vector<std::byte>* out) noexcept
    {
        if (!out) {
            return false;
        }
        const size_t old_size = out->size();
        if (bytes.size() > SIZE_MAX - old_size) {
            return false;
        }
        const size_t new_size = old_size + bytes.size();
        if (max_total != 0U && new_size > static_cast<size_t>(max_total)) {
            return false;
        }
        out->insert(out->end(), bytes.begin(), bytes.end());
        return true;
    }

    static bool read_cbor_byte_or_text_payload(DecodeContext* ctx,
                                               std::span<const std::byte> cbor,
                                               uint64_t* pos,
                                               const CborHead& head,
                                               std::vector<std::byte>* out) noexcept
    {
        if (!ctx || !pos || !out) {
            return false;
        }
        out->clear();

        const uint32_t max_total
            = (head.major == 2U) ? ctx->options.limits.max_cbor_bytes_bytes
                                 : ctx->options.limits.max_cbor_text_bytes;
        if (!head.indefinite) {
            std::span<const std::byte> payload;
            if (!read_cbor_text(cbor, pos, head.arg, &payload)) {
                return false;
            }
            if (!append_cbor_chunk(payload, max_total, out)) {
                if (max_total != 0U) {
                    ctx->result.status = JumbfDecodeStatus::LimitExceeded;
                }
                return false;
            }
            return true;
        }

        while (true) {
            if (cbor_peek_break(cbor, *pos)) {
                return cbor_consume_break(cbor, pos);
            }
            CborHead chunk;
            if (!read_cbor_head(cbor, pos, &chunk) || !cbor_item_budget_take(ctx)) {
                return false;
            }
            if (chunk.major != head.major || chunk.indefinite) {
                return false;
            }
            std::span<const std::byte> payload;
            if (!read_cbor_text(cbor, pos, chunk.arg, &payload)) {
                return false;
            }
            if (!append_cbor_chunk(payload, max_total, out)) {
                if (max_total != 0U) {
                    ctx->result.status = JumbfDecodeStatus::LimitExceeded;
                }
                return false;
            }
        }
    }


    static bool parse_cbor_key(DecodeContext* ctx,
                               std::span<const std::byte> cbor, uint64_t* pos,
                               uint32_t depth, uint32_t map_index,
                               std::string* out) noexcept
    {
        if (!ctx || !pos || !out) {
            return false;
        }
        if (!cbor_depth_ok(ctx, depth)) {
            return false;
        }

        CborHead head;
        if (!read_cbor_head(cbor, pos, &head) || !cbor_item_budget_take(ctx)) {
            return false;
        }

        if (head.major == 3U) {
            std::vector<std::byte> text_bytes;
            if (!read_cbor_byte_or_text_payload(ctx, cbor, pos, head,
                                                &text_bytes)) {
                return false;
            }
            return sanitize_cbor_path_segment(
                std::span<const std::byte>(text_bytes.data(), text_bytes.size()),
                ctx->options.limits.max_cbor_key_bytes, out);
        }

        if (head.major == 0U) {
            out->assign(
                std::to_string(static_cast<unsigned long long>(head.arg)));
            return true;
        }

        if (head.major == 1U) {
            out->assign("n");
            out->append(
                std::to_string(static_cast<unsigned long long>(head.arg)));
            return true;
        }

        if (head.major == 7U) {
            if (head.indefinite) {
                return false;
            }
            if (head.addl == 20U) {
                out->assign("false");
                return true;
            }
            if (head.addl == 21U) {
                out->assign("true");
                return true;
            }
            if (head.addl == 22U) {
                out->assign("null");
                return true;
            }
            if (head.addl == 23U) {
                out->assign("undefined");
                return true;
            }
            out->assign("simple");
            return true;
        }

        if (!skip_cbor_item_from_head(ctx, cbor, pos, depth + 1U, head)) {
            return false;
        }
        return assign_synth_cbor_key(map_index, cbor_major_suffix(head.major),
                                     ctx->options.limits.max_cbor_key_bytes,
                                     out);
    }

    static bool parse_cbor_item(DecodeContext* ctx,
                                std::span<const std::byte> cbor, uint64_t* pos,
                                uint32_t depth, std::string_view path) noexcept;


    static bool parse_cbor_item(DecodeContext* ctx,
                                std::span<const std::byte> cbor, uint64_t* pos,
                                uint32_t depth, std::string_view path) noexcept
    {
        if (!ctx || !pos || !cbor_depth_ok(ctx, depth)) {
            return false;
        }

        CborHead head;
        if (!read_cbor_head(cbor, pos, &head) || !cbor_item_budget_take(ctx)) {
            return false;
        }

        if (head.major == 0U) {
            return emit_cbor_value(ctx, path, make_u64(head.arg));
        }

        if (head.major == 1U) {
            if (head.arg >= static_cast<uint64_t>(INT64_MAX)) {
                const std::string value
                    = "-(1+"
                      + std::to_string(
                          static_cast<unsigned long long>(head.arg))
                      + ")";
                return emit_cbor_value(ctx, path,
                                       make_text(ctx->store->arena(), value,
                                                 TextEncoding::Ascii));
            }
            const int64_t value = -1 - static_cast<int64_t>(head.arg);
            return emit_cbor_value(ctx, path, make_i64(value));
        }

        if (head.major == 2U) {
            std::vector<std::byte> data_bytes;
            if (!read_cbor_byte_or_text_payload(ctx, cbor, pos, head,
                                                &data_bytes)) {
                return false;
            }
            return emit_cbor_value(
                ctx, path, make_bytes(
                               ctx->store->arena(),
                               std::span<const std::byte>(data_bytes.data(),
                                                          data_bytes.size())));
        }

        if (head.major == 3U) {
            std::vector<std::byte> text_bytes;
            if (!read_cbor_byte_or_text_payload(ctx, cbor, pos, head,
                                                &text_bytes)) {
                return false;
            }
            const std::span<const std::byte> text(text_bytes.data(),
                                                  text_bytes.size());
            if (bytes_valid_utf8(text)) {
                const std::string_view text_sv(reinterpret_cast<const char*>(
                                                   text.data()),
                                               text.size());
                return emit_cbor_value(ctx, path,
                                       make_text(ctx->store->arena(), text_sv,
                                                 TextEncoding::Utf8));
            }
            return emit_cbor_value(ctx, path,
                                   make_bytes(ctx->store->arena(), text));
        }

        if (head.major == 4U) {
            uint64_t index = 0U;
            while (true) {
                if (head.indefinite && cbor_peek_break(cbor, *pos)) {
                    return cbor_consume_break(cbor, pos);
                }
                if (!head.indefinite && index >= head.arg) {
                    return true;
                }
                std::string child_path;
                child_path.reserve(path.size() + 24U);
                child_path.append(path.data(), path.size());
                child_path.push_back('[');
                child_path.append(
                    std::to_string(static_cast<unsigned long long>(index)));
                child_path.push_back(']');
                if (!parse_cbor_item(ctx, cbor, pos, depth + 1U, child_path)) {
                    return false;
                }
                index += 1U;
            }
        }

        if (head.major == 5U) {
            uint64_t map_index = 0U;
            while (true) {
                if (head.indefinite && cbor_peek_break(cbor, *pos)) {
                    return cbor_consume_break(cbor, pos);
                }
                if (!head.indefinite && map_index >= head.arg) {
                    return true;
                }
                std::string key_segment;
                if (!parse_cbor_key(ctx, cbor, pos, depth + 1U,
                                    static_cast<uint32_t>(map_index),
                                    &key_segment)) {
                    return false;
                }

                std::string child_path;
                child_path.reserve(path.size() + key_segment.size() + 1U);
                child_path.append(path.data(), path.size());
                if (!child_path.empty()) {
                    child_path.push_back('.');
                }
                child_path.append(key_segment.data(), key_segment.size());

                if (!parse_cbor_item(ctx, cbor, pos, depth + 1U, child_path)) {
                    return false;
                }
                map_index += 1U;
            }
        }

        if (head.major == 6U) {
            if (head.indefinite) {
                return false;
            }
            std::string tag_field(path);
            tag_field.append(".@tag");
            if (!emit_cbor_value(ctx, tag_field, make_u64(head.arg))) {
                return false;
            }
            return parse_cbor_item(ctx, cbor, pos, depth + 1U, path);
        }

        if (head.major == 7U) {
            if (head.indefinite) {
                return false;
            }
            if (head.addl <= 19U) {
                return emit_cbor_value(
                    ctx, path, make_u8(static_cast<uint8_t>(head.addl)));
            }
            if (head.addl == 20U) {
                return emit_cbor_value(ctx, path, make_u8(0U));
            }
            if (head.addl == 21U) {
                return emit_cbor_value(ctx, path, make_u8(1U));
            }
            if (head.addl == 22U) {
                return emit_cbor_value(ctx, path,
                                       make_text(ctx->store->arena(), "null",
                                                 TextEncoding::Ascii));
            }
            if (head.addl == 23U) {
                return emit_cbor_value(ctx, path,
                                       make_text(ctx->store->arena(),
                                                 "undefined",
                                                 TextEncoding::Ascii));
            }
            if (head.addl == 24U) {
                return emit_cbor_value(
                    ctx, path, make_u8(static_cast<uint8_t>(head.arg & 0xFFU)));
            }
            if (head.addl == 25U) {
                return emit_cbor_value(ctx, path,
                                       make_f32_bits(cbor_half_to_f32_bits(
                                           static_cast<uint16_t>(
                                               head.arg & 0xFFFFU))));
            }
            if (head.addl == 26U) {
                return emit_cbor_value(ctx, path,
                                       make_f32_bits(static_cast<uint32_t>(
                                           head.arg & 0xFFFFFFFFU)));
            }
            if (head.addl == 27U) {
                return emit_cbor_value(ctx, path, make_f64_bits(head.arg));
            }
            const std::string simple_text
                = "simple("
                  + std::to_string(
                      static_cast<unsigned long long>(head.addl))
                  + ")";
            return emit_cbor_value(ctx, path,
                                   make_text(ctx->store->arena(), simple_text,
                                             TextEncoding::Ascii));
        }

        return false;
    }


    static bool decode_cbor_payload(DecodeContext* ctx,
                                    std::span<const std::byte> cbor_payload,
                                    std::string_view path_prefix) noexcept
    {
        if (!ctx) {
            return false;
        }
        uint64_t offset = 0U;
        while (offset < cbor_payload.size()) {
            if (!parse_cbor_item(ctx, cbor_payload, &offset, 0U, path_prefix)) {
                return false;
            }
        }
        return true;
    }


    static bool decode_jumbf_boxes(DecodeContext* ctx,
                                   std::span<const std::byte> bytes,
                                   uint64_t begin, uint64_t end, uint32_t depth,
                                   std::string_view parent_path) noexcept
    {
        if (!ctx || !ctx->store) {
            return false;
        }

        const uint32_t max_depth = ctx->options.limits.max_box_depth;
        if (max_depth != 0U && depth > max_depth) {
            ctx->result.status = JumbfDecodeStatus::LimitExceeded;
            return false;
        }

        uint64_t offset      = begin;
        uint32_t child_index = 0U;
        while (offset < end) {
            BmffBox box;
            if (!parse_bmff_box(bytes, offset, end, &box)) {
                return false;
            }

            ctx->result.boxes_decoded += 1U;
            const uint32_t max_boxes = ctx->options.limits.max_boxes;
            if (max_boxes != 0U && ctx->result.boxes_decoded > max_boxes) {
                ctx->result.status = JumbfDecodeStatus::LimitExceeded;
                return false;
            }

            std::string box_path;
            make_child_path(parent_path, child_index, &box_path);
            child_index += 1U;

            const uint64_t payload_off  = box.offset + box.header_size;
            const uint64_t payload_size = box.size - box.header_size;
            const std::span<const std::byte> payload
                = bytes.subspan(static_cast<size_t>(payload_off),
                                static_cast<size_t>(payload_size));

            std::string field_key;
            make_field_key(box_path, "type", &field_key);
            if (!emit_field_text(ctx, field_key, fourcc_to_text(box.type),
                                 EntryFlags::Derived)) {
                return false;
            }
            make_field_key(box_path, "size", &field_key);
            if (!emit_field_u64(ctx, field_key, box.size, EntryFlags::Derived)) {
                return false;
            }
            make_field_key(box_path, "payload_size", &field_key);
            if (!emit_field_u64(ctx, field_key, payload_size,
                                EntryFlags::Derived)) {
                return false;
            }
            make_field_key(box_path, "offset", &field_key);
            if (!emit_field_u64(ctx, field_key, box.offset,
                                EntryFlags::Derived)) {
                return false;
            }

            if (ctx->options.detect_c2pa) {
                if (box.type == fourcc('c', '2', 'p', 'a')) {
                    if (!append_c2pa_marker(ctx, box_path)) {
                        return false;
                    }
                } else if (box.type == fourcc('j', 'u', 'm', 'd')) {
                    if (ascii_icase_contains(payload, "c2pa", 4096U)) {
                        if (!append_c2pa_marker(ctx, box_path)) {
                            return false;
                        }
                    }
                }
            }

            if (ctx->options.decode_cbor
                && box.type == fourcc('c', 'b', 'o', 'r')) {
                std::string cbor_prefix(box_path);
                cbor_prefix.append(".cbor");
                if (!decode_cbor_payload(ctx, payload, cbor_prefix)) {
                    return false;
                }
            }

            if (looks_like_bmff_sequence(bytes, payload_off,
                                         payload_off + payload_size)) {
                if (!decode_jumbf_boxes(ctx, bytes, payload_off,
                                        payload_off + payload_size, depth + 1U,
                                        box_path)) {
                    return false;
                }
            }

            offset += box.size;
            if (box.size == 0U) {
                break;
            }
        }
        return true;
    }

}  // namespace

JumbfDecodeResult
decode_jumbf_payload(std::span<const std::byte> bytes, MetaStore& store,
                     EntryFlags flags,
                     const JumbfDecodeOptions& options) noexcept
{
    JumbfDecodeResult out;
    out.status = JumbfDecodeStatus::Unsupported;

    if (options.limits.max_input_bytes != 0U
        && bytes.size() > options.limits.max_input_bytes) {
        out.status = JumbfDecodeStatus::LimitExceeded;
        return out;
    }

    if (!looks_like_bmff_sequence(bytes, 0U, bytes.size())) {
        return out;
    }

    DecodeContext ctx;
    ctx.store         = &store;
    ctx.flags         = flags;
    ctx.options       = options;
    ctx.result.status = JumbfDecodeStatus::Ok;
    ctx.block         = store.add_block(BlockInfo {});
    if (ctx.block == kInvalidBlockId) {
        out.status = JumbfDecodeStatus::LimitExceeded;
        return out;
    }

    if (!decode_jumbf_boxes(&ctx, bytes, 0U, bytes.size(), 0U,
                            std::string_view {})) {
        if (ctx.result.status == JumbfDecodeStatus::Ok) {
            ctx.result.status = JumbfDecodeStatus::Malformed;
        }
        return ctx.result;
    }

    if (!append_c2pa_semantic_fields(&ctx)) {
        if (ctx.result.status == JumbfDecodeStatus::Ok) {
            ctx.result.status = JumbfDecodeStatus::Malformed;
        }
    }

    return ctx.result;
}

}  // namespace openmeta
