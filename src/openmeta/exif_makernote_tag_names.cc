#include <cstdint>
#include <string_view>

namespace openmeta {
namespace {

    struct MakerNoteTagNameEntry final {
        uint16_t tag     = 0;
        const char* name = nullptr;
    };

    struct MakerNoteTableMap final {
        const char* key                      = nullptr;
        const MakerNoteTagNameEntry* entries = nullptr;
        uint32_t count                       = 0;
    };

#include "exif_makernote_tag_names_generated.inc"

    static bool is_ascii_digit(char c) noexcept { return c >= '0' && c <= '9'; }


    struct MkIfdParts final {
        std::string_view vendor;
        std::string_view subtable;
    };

    static MkIfdParts parse_mk_ifd_token(std::string_view ifd) noexcept
    {
        MkIfdParts out;
        if (!ifd.starts_with("mk_")) {
            return out;
        }
        std::string_view rest = ifd.substr(3);
        if (rest.empty()) {
            return out;
        }

        // Strip trailing numeric index suffix (e.g. mk_canon0, mk_casio_type2_0).
        size_t end = rest.size();
        while (end > 0 && is_ascii_digit(rest[end - 1])) {
            end -= 1;
        }
        rest = rest.substr(0, end);

        // Optional '_' delimiter before the index.
        while (!rest.empty() && rest.back() == '_') {
            rest = rest.substr(0, rest.size() - 1);
        }
        if (rest.empty()) {
            return out;
        }

        const size_t sep = rest.find('_');
        if (sep == std::string_view::npos) {
            out.vendor = rest;
            return out;
        }
        if (sep == 0 || sep + 1 >= rest.size()) {
            return out;
        }
        out.vendor   = rest.substr(0, sep);
        out.subtable = rest.substr(sep + 1);
        return out;
    }


    static int compare_key_to_cstr(std::string_view a,
                                   const char* b) noexcept
    {
        // Lexicographic compare of a string_view to a NUL-terminated string.
        // Avoids strlen() on every comparison.
        size_t i = 0;
        for (; i < a.size(); ++i) {
            const char bc = b ? b[i] : '\0';
            if (bc == '\0') {
                // b shorter -> a greater.
                return 1;
            }
            const char ac = a[i];
            if (ac < bc) {
                return -1;
            }
            if (ac > bc) {
                return 1;
            }
        }
        // a exhausted.
        const char bc = b ? b[i] : '\0';
        return (bc == '\0') ? 0 : -1;
    }


    static const MakerNoteTableMap* find_table(std::string_view key) noexcept
    {
        const uint32_t count
            = static_cast<uint32_t>(sizeof(kMakerNoteTables)
                                    / sizeof(kMakerNoteTables[0]));

        uint32_t lo = 0;
        uint32_t hi = count;
        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2U;
            const MakerNoteTableMap& t = kMakerNoteTables[mid];
            if (!t.key) {
                // Generated tables should never include null keys, but don't
                // crash if they do.
                return nullptr;
            }
            const int cmp = compare_key_to_cstr(key, t.key);
            if (cmp == 0) {
                return &t;
            }
            if (cmp < 0) {
                hi = mid;
            } else {
                lo = mid + 1U;
            }
        }
        return nullptr;
    }


    static std::string_view find_tag_name(const MakerNoteTagNameEntry* entries,
                                          uint32_t count, uint16_t tag) noexcept
    {
        if (!entries || count == 0) {
            return {};
        }

        uint32_t lo = 0;
        uint32_t hi = count;
        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2;
            const uint16_t cur = entries[mid].tag;
            if (cur < tag) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }

        if (lo < count && entries[lo].tag == tag && entries[lo].name) {
            return entries[lo].name;
        }
        return {};
    }


    static const MakerNoteTableMap* try_table(std::string_view vendor_key,
                                              std::string_view table, char* buf,
                                              size_t buf_size) noexcept
    {
        if (!buf || buf_size == 0) {
            return nullptr;
        }

        const std::string_view prefix = "makernote:";
        const std::string_view sep    = ":";

        if (table.empty()) {
            return nullptr;
        }
        if (prefix.size() + vendor_key.size() + sep.size() + table.size()
            >= buf_size) {
            return nullptr;
        }

        size_t n = 0;
        for (size_t i = 0; i < prefix.size(); ++i) {
            buf[n++] = prefix[i];
        }
        for (size_t i = 0; i < vendor_key.size(); ++i) {
            buf[n++] = vendor_key[i];
        }
        for (size_t i = 0; i < sep.size(); ++i) {
            buf[n++] = sep[i];
        }
        for (size_t i = 0; i < table.size(); ++i) {
            buf[n++] = table[i];
        }
        return find_table(std::string_view(buf, n));
    }

}  // namespace

std::string_view
makernote_tag_name(std::string_view ifd, uint16_t tag) noexcept
{
    const MkIfdParts parts = parse_mk_ifd_token(ifd);
    if (parts.vendor.empty()) {
        return {};
    }

    // Current MakerNote decode tokens use a few short aliases. Convert them to
    // canonical table keys used by the registry.
    std::string_view vendor_key = parts.vendor;
    if (vendor_key == "fuji") {
        vendor_key = "fujifilm";
    }

    char table_key_buf[96];

    const MakerNoteTableMap* table = nullptr;
    if (!parts.subtable.empty()) {
        table = try_table(vendor_key, parts.subtable, table_key_buf,
                          sizeof(table_key_buf));
    }
    if (!table) {
        table = try_table(vendor_key, "main", table_key_buf,
                          sizeof(table_key_buf));
    }
    if (!table) {
        return {};
    }
    return find_tag_name(table->entries, table->count, tag);
}

}  // namespace openmeta
