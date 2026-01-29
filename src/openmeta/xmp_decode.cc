#include "openmeta/xmp_decode.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT
#    include <expat.h>
#endif

namespace openmeta {
namespace {

    static bool contains_byte(std::span<const std::byte> bytes,
                              unsigned char needle) noexcept
    {
        for (size_t i = 0; i < bytes.size(); ++i) {
            if (static_cast<unsigned char>(std::to_integer<uint8_t>(bytes[i]))
                == needle) {
                return true;
            }
        }
        return false;
    }

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT

    static constexpr std::string_view kRdfNs
        = "http://www.w3.org/1999/02/22-rdf-syntax-ns#";
    static constexpr std::string_view kXmlNs
        = "http://www.w3.org/XML/1998/namespace";

    struct NameParts final {
        std::string_view uri;
        std::string_view local;
    };

    static NameParts split_name(std::string_view name) noexcept
    {
        const size_t sep = name.find('|');
        if (sep == std::string_view::npos) {
            return NameParts { std::string_view {}, name };
        }
        return NameParts { name.substr(0, sep), name.substr(sep + 1) };
    }


    static bool is_ascii_ws(char c) noexcept
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }


    static std::string_view trim_ascii_ws(std::string_view s) noexcept
    {
        size_t b = 0;
        while (b < s.size() && is_ascii_ws(s[b])) {
            b += 1;
        }
        size_t e = s.size();
        while (e > b && is_ascii_ws(s[e - 1])) {
            e -= 1;
        }
        return s.substr(b, e - b);
    }


    static void merge_status(XmpDecodeResult* out, XmpDecodeStatus in) noexcept
    {
        if (!out || in == XmpDecodeStatus::Ok) {
            return;
        }
        if (out->status == XmpDecodeStatus::LimitExceeded) {
            return;
        }
        if (in == XmpDecodeStatus::LimitExceeded) {
            out->status = in;
            return;
        }
        if (out->status == XmpDecodeStatus::Malformed) {
            return;
        }
        if (in == XmpDecodeStatus::Malformed) {
            out->status = in;
            return;
        }
        if (out->status == XmpDecodeStatus::OutputTruncated) {
            return;
        }
        if (in == XmpDecodeStatus::OutputTruncated) {
            out->status = in;
            return;
        }
        if (out->status == XmpDecodeStatus::Unsupported) {
            return;
        }
        if (in == XmpDecodeStatus::Unsupported) {
            out->status = in;
            return;
        }
        if (out->status == XmpDecodeStatus::Ok) {
            out->status = in;
            return;
        }
    }


    struct Frame final {
        bool is_rdf               = false;
        bool is_description       = false;
        bool is_array_container   = false;
        bool is_li                = false;
        bool is_nonrdf            = false;
        bool contributed_to_path  = false;
        bool had_child_element    = false;
        bool emitted_resource_val = false;
        bool text_truncated       = false;
        uint32_t path_len_before  = 0;
        uint32_t li_counter       = 0;  // used for Seq/Bag/Alt
        std::string text;
    };

    struct Ctx final {
        MetaStore* store = nullptr;
        BlockId block    = kInvalidBlockId;
        EntryFlags flags = EntryFlags::None;
        XmpDecodeOptions options;
        XmpDecodeResult result;

        XML_Parser parser = nullptr;

        uint32_t description_depth = 0;

        uint64_t total_value_bytes = 0;
        uint32_t order_in_block    = 0;

        std::string path;
        std::string root_schema_ns;

        std::vector<Frame> stack;
    };

    static bool should_stop(const Ctx* ctx) noexcept
    {
        return !ctx || !ctx->parser
               || ctx->result.status == XmpDecodeStatus::LimitExceeded
               || ctx->result.status == XmpDecodeStatus::Malformed;
    }


    static void stop_parser(Ctx* ctx, XmpDecodeStatus status) noexcept
    {
        if (!ctx) {
            return;
        }
        merge_status(&ctx->result, status);
        if (ctx->parser) {
            XML_StopParser(ctx->parser, XML_FALSE);
        }
    }


    static bool path_append_segment(Ctx* ctx, std::string_view seg,
                                    bool use_slash) noexcept
    {
        if (!ctx) {
            return false;
        }
        if (seg.empty()) {
            return true;
        }

        const uint32_t max_path = ctx->options.limits.max_path_bytes;
        uint64_t needed         = ctx->path.size();
        if (use_slash && !ctx->path.empty()) {
            needed += 1;
        }
        needed += seg.size();
        if (max_path != 0U && needed > max_path) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }

        if (use_slash && !ctx->path.empty()) {
            ctx->path.push_back('/');
        }
        ctx->path.append(seg.data(), seg.size());
        return true;
    }


    static bool path_append_index(Ctx* ctx, uint32_t index) noexcept
    {
        if (!ctx) {
            return false;
        }

        char buf[32];
        std::snprintf(buf, sizeof(buf), "[%u]", static_cast<unsigned>(index));
        const std::string_view s(buf, std::strlen(buf));

        const uint32_t max_path = ctx->options.limits.max_path_bytes;
        const uint64_t needed   = static_cast<uint64_t>(ctx->path.size())
                                + static_cast<uint64_t>(s.size());
        if (max_path != 0U && needed > max_path) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }
        ctx->path.append(s.data(), s.size());
        return true;
    }


    static Frame* find_nearest_array_container(Ctx* ctx) noexcept
    {
        if (!ctx) {
            return nullptr;
        }
        for (size_t i = ctx->stack.size(); i > 0; --i) {
            Frame& f = ctx->stack[i - 1];
            if (f.is_array_container) {
                return &f;
            }
        }
        return nullptr;
    }


    static bool emit_property_text(Ctx* ctx, std::string_view schema_ns,
                                   std::string_view property_path,
                                   std::string_view value) noexcept
    {
        if (!ctx || !ctx->store) {
            return false;
        }
        if (schema_ns.empty() || property_path.empty()) {
            return false;
        }
        if (ctx->result.entries_decoded >= ctx->options.limits.max_properties) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return false;
        }

        const uint64_t max_total = ctx->options.limits.max_total_value_bytes;
        if (max_total != 0U) {
            const uint64_t add = static_cast<uint64_t>(value.size());
            if (add > (UINT64_MAX - ctx->total_value_bytes)
                || ctx->total_value_bytes + add > max_total) {
                stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
                return false;
            }
        }

        Entry entry;
        entry.key   = make_xmp_property_key(ctx->store->arena(), schema_ns,
                                            property_path);
        entry.value = make_text(ctx->store->arena(), value, TextEncoding::Utf8);
        entry.origin.block          = ctx->block;
        entry.origin.order_in_block = ctx->order_in_block;
        entry.origin.wire_type      = WireType { WireFamily::Other, 0 };
        entry.origin.wire_count     = static_cast<uint32_t>(value.size());
        entry.flags                 = ctx->flags;

        (void)ctx->store->add_entry(entry);
        ctx->result.entries_decoded += 1;
        ctx->order_in_block += 1;
        ctx->total_value_bytes += static_cast<uint64_t>(value.size());
        return true;
    }


    static void XMLCALL start_element(void* user_data, const XML_Char* name_c,
                                      const XML_Char** atts)
    {
        Ctx* ctx = reinterpret_cast<Ctx*>(user_data);
        if (should_stop(ctx) || !name_c) {
            return;
        }

        if (ctx->stack.size() >= ctx->options.limits.max_depth) {
            stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
            return;
        }

        if (!ctx->stack.empty()) {
            ctx->stack.back().had_child_element = true;
        }

        const std::string_view name(name_c, std::strlen(name_c));
        const NameParts parts = split_name(name);
        const bool is_rdf     = (parts.uri == kRdfNs);
        const bool is_xml     = (parts.uri == kXmlNs);
        const bool is_desc    = is_rdf && (parts.local == "Description");
        const bool is_seq     = is_rdf
                            && (parts.local == "Seq" || parts.local == "Bag"
                                || parts.local == "Alt");
        const bool is_li = is_rdf && (parts.local == "li");

        Frame frame;
        frame.is_rdf             = is_rdf;
        frame.is_description     = is_desc;
        frame.is_array_container = is_seq;
        frame.is_li              = is_li;
        frame.is_nonrdf          = (!is_rdf && !is_xml);
        frame.path_len_before    = static_cast<uint32_t>(ctx->path.size());

        // Enter rdf:Description scope.
        if (frame.is_description) {
            ctx->description_depth += 1;
        }

        // If inside an rdf:Description and we see a non-rdf element, treat it as a
        // property path component.
        if (ctx->description_depth > 0 && frame.is_nonrdf) {
            const bool is_root = ctx->path.empty();
            if (is_root) {
                ctx->root_schema_ns.assign(parts.uri.data(), parts.uri.size());
            }
            if (!path_append_segment(ctx, parts.local, true)) {
                return;
            }
            frame.contributed_to_path = true;

            // If the property uses rdf:resource, emit it as the value immediately.
            if (atts) {
                for (int i = 0; atts[i] && atts[i + 1]; i += 2) {
                    const std::string_view an(atts[i], std::strlen(atts[i]));
                    const NameParts ap = split_name(an);
                    if (ap.uri == kRdfNs && ap.local == "resource") {
                        const std::string_view av(atts[i + 1],
                                                  std::strlen(atts[i + 1]));
                        const std::string_view trimmed = trim_ascii_ws(av);
                        (void)emit_property_text(ctx, ctx->root_schema_ns,
                                                 ctx->path, trimmed);
                        frame.emitted_resource_val = true;
                        break;
                    }
                }
            }
        }

        // Array item: append an index to the current property path.
        if (ctx->description_depth > 0 && frame.is_li && !ctx->path.empty()) {
            Frame* container = find_nearest_array_container(ctx);
            if (container) {
                if (container->li_counter == UINT32_MAX) {
                    stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
                    return;
                }
                container->li_counter += 1;
                frame.path_len_before = static_cast<uint32_t>(ctx->path.size());
                frame.contributed_to_path = true;
                if (!path_append_index(ctx, container->li_counter)) {
                    return;
                }
            }
        }

        ctx->stack.push_back(std::move(frame));

        // Decode attributes on rdf:Description as top-level properties.
        if (ctx->description_depth > 0 && is_desc
            && ctx->options.decode_description_attributes && atts) {
            for (int i = 0; atts[i] && atts[i + 1]; i += 2) {
                const std::string_view an(atts[i], std::strlen(atts[i]));
                const NameParts ap = split_name(an);
                if (ap.uri.empty() || ap.local.empty()) {
                    continue;
                }
                if (ap.uri == kRdfNs || ap.uri == kXmlNs) {
                    continue;
                }
                const std::string_view av(atts[i + 1],
                                          std::strlen(atts[i + 1]));
                const std::string_view trimmed = trim_ascii_ws(av);
                (void)emit_property_text(ctx, ap.uri, ap.local, trimmed);
            }
        }
    }


    static void XMLCALL end_element(void* user_data, const XML_Char* /*name_c*/)
    {
        Ctx* ctx = reinterpret_cast<Ctx*>(user_data);
        if (should_stop(ctx)) {
            return;
        }
        if (ctx->stack.empty()) {
            stop_parser(ctx, XmpDecodeStatus::Malformed);
            return;
        }

        Frame frame = std::move(ctx->stack.back());
        ctx->stack.pop_back();

        // Emit element/li text values (leaf-only).
        if (ctx->description_depth > 0 && !ctx->path.empty()
            && !frame.emitted_resource_val && !frame.had_child_element) {
            if (frame.is_li || frame.is_nonrdf) {
                const std::string_view trimmed = trim_ascii_ws(frame.text);
                if (!trimmed.empty()) {
                    (void)emit_property_text(ctx, ctx->root_schema_ns,
                                             ctx->path, trimmed);
                }
            }
        }

        // Restore the property path.
        if (frame.contributed_to_path) {
            if (frame.path_len_before <= ctx->path.size()) {
                ctx->path.resize(frame.path_len_before);
            } else {
                stop_parser(ctx, XmpDecodeStatus::Malformed);
                return;
            }
            if (ctx->path.empty()) {
                ctx->root_schema_ns.clear();
            }
        }

        if (frame.is_description) {
            if (ctx->description_depth == 0) {
                stop_parser(ctx, XmpDecodeStatus::Malformed);
                return;
            }
            ctx->description_depth -= 1;
        }
    }


    static void XMLCALL char_data(void* user_data, const XML_Char* s, int len)
    {
        Ctx* ctx = reinterpret_cast<Ctx*>(user_data);
        if (should_stop(ctx) || !s || len <= 0) {
            return;
        }
        if (ctx->stack.empty()) {
            return;
        }

        Frame& frame = ctx->stack.back();
        if (ctx->description_depth == 0 || ctx->path.empty()) {
            return;
        }
        if (!frame.is_li && !frame.is_nonrdf) {
            return;
        }
        if (frame.emitted_resource_val) {
            return;
        }

        const uint32_t max_val   = ctx->options.limits.max_value_bytes;
        const uint64_t max_total = ctx->options.limits.max_total_value_bytes;

        const uint32_t want = static_cast<uint32_t>(len);
        const uint32_t have = static_cast<uint32_t>(frame.text.size());
        uint32_t avail      = 0;
        if (max_val == 0U || have < max_val) {
            avail = (max_val == 0U) ? want : (max_val - have);
        }
        uint32_t take = want;
        if (avail < take) {
            take                 = avail;
            frame.text_truncated = true;
            merge_status(&ctx->result, XmpDecodeStatus::OutputTruncated);
        }

        if (take == 0) {
            return;
        }

        if (max_total != 0U) {
            const uint64_t add = static_cast<uint64_t>(take);
            if (add > (UINT64_MAX - ctx->total_value_bytes)
                || ctx->total_value_bytes + add > max_total) {
                stop_parser(ctx, XmpDecodeStatus::LimitExceeded);
                return;
            }
        }

        frame.text.append(s, static_cast<size_t>(take));
    }

#endif  // OPENMETA_HAS_EXPAT

}  // namespace

XmpDecodeResult
decode_xmp_packet(std::span<const std::byte> xmp_bytes, MetaStore& store,
                  EntryFlags flags, const XmpDecodeOptions& options) noexcept
{
    XmpDecodeResult result;

    if (xmp_bytes.empty()
        || !contains_byte(xmp_bytes, static_cast<unsigned char>('<'))) {
        result.status = XmpDecodeStatus::Unsupported;
        return result;
    }

    const uint64_t max_in = options.limits.max_input_bytes;
    if (max_in != 0U && xmp_bytes.size() > max_in) {
        result.status = XmpDecodeStatus::LimitExceeded;
        return result;
    }

#if defined(OPENMETA_HAS_EXPAT) && OPENMETA_HAS_EXPAT
    Ctx ctx;
    ctx.store                  = &store;
    ctx.block                  = store.add_block(BlockInfo {});
    ctx.flags                  = flags;
    ctx.options                = options;
    ctx.result.status          = XmpDecodeStatus::Ok;
    ctx.result.entries_decoded = 0;
    ctx.path.reserve(options.limits.max_path_bytes);
    ctx.stack.reserve(options.limits.max_depth);

    ctx.parser = XML_ParserCreateNS(nullptr, '|');
    if (!ctx.parser) {
        result.status = XmpDecodeStatus::Malformed;
        return result;
    }

    XML_SetUserData(ctx.parser, &ctx);
    XML_SetElementHandler(ctx.parser, &start_element, &end_element);
    XML_SetCharacterDataHandler(ctx.parser, &char_data);

    const char* data = reinterpret_cast<const char*>(xmp_bytes.data());
    const int size   = (xmp_bytes.size() > static_cast<size_t>(INT32_MAX))
                           ? INT32_MAX
                           : static_cast<int>(xmp_bytes.size());

    if (xmp_bytes.size() > static_cast<size_t>(INT32_MAX)) {
        stop_parser(&ctx, XmpDecodeStatus::LimitExceeded);
    } else {
        const XML_Status st = XML_Parse(ctx.parser, data, size, XML_TRUE);
        if (st == XML_STATUS_ERROR) {
            // Treat "not XML" as Unsupported, otherwise Malformed.
            const enum XML_Error err = XML_GetErrorCode(ctx.parser);
            if (err == XML_ERROR_SYNTAX || err == XML_ERROR_NO_ELEMENTS) {
                merge_status(&ctx.result, XmpDecodeStatus::Unsupported);
            } else {
                merge_status(&ctx.result, XmpDecodeStatus::Malformed);
            }
        }
    }

    XML_ParserFree(ctx.parser);
    ctx.parser = nullptr;

    result = ctx.result;
    return result;
#else
    (void)store;
    (void)flags;
    (void)options;
    result.status = XmpDecodeStatus::Unsupported;
    return result;
#endif
}

}  // namespace openmeta
