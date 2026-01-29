#include <cstdint>
#include <string_view>

namespace openmeta {
namespace {

    struct MakerNoteTagNameEntry final {
        uint16_t tag = 0;
        const char* name = nullptr;
    };

    struct MakerNoteTableMap final {
        const char* key = nullptr;
        const MakerNoteTagNameEntry* entries = nullptr;
        uint32_t count = 0;
    };

#include "exif_makernote_tag_names_generated.inc"

    static bool is_ascii_digit(char c) noexcept
    {
        return c >= '0' && c <= '9';
    }

    static std::string_view maker_note_vendor_token(std::string_view ifd) noexcept
    {
        if (!ifd.starts_with("mk_")) {
            return {};
        }
        const std::string_view rest = ifd.substr(3);
        if (rest.empty()) {
            return {};
        }

        size_t end = 0;
        while (end < rest.size()) {
            const char c = rest[end];
            if (c == '_' || is_ascii_digit(c)) {
                break;
            }
            end += 1;
        }
        if (end == 0) {
            return {};
        }
        return rest.substr(0, end);
    }

    static const MakerNoteTableMap* find_table(std::string_view key) noexcept
    {
        for (uint32_t i = 0; i < sizeof(kMakerNoteMainTables) / sizeof(kMakerNoteMainTables[0]);
             ++i) {
            const MakerNoteTableMap& t = kMakerNoteMainTables[i];
            if (!t.key) {
                continue;
            }
            if (key == t.key) {
                return &t;
            }
        }
        return nullptr;
    }

    static std::string_view find_tag_name(const MakerNoteTagNameEntry* entries,
                                          uint32_t count,
                                          uint16_t tag) noexcept
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

}  // namespace

std::string_view
makernote_tag_name(std::string_view ifd, uint16_t tag) noexcept
{
    const std::string_view vendor = maker_note_vendor_token(ifd);
    if (vendor.empty()) {
        return {};
    }

    // Current MakerNote decode tokens use a few short aliases. Convert them to
    // canonical table keys used by the registry.
    std::string_view vendor_key = vendor;
    if (vendor_key == "fuji") {
        vendor_key = "fujifilm";
    }

    char table_key_buf[64];
    const std::string_view prefix = "makernote:";
    const std::string_view suffix = ":main";

    if (prefix.size() + vendor_key.size() + suffix.size() >= sizeof(table_key_buf)) {
        return {};
    }

    size_t n = 0;
    for (size_t i = 0; i < prefix.size(); ++i) {
        table_key_buf[n++] = prefix[i];
    }
    for (size_t i = 0; i < vendor_key.size(); ++i) {
        table_key_buf[n++] = vendor_key[i];
    }
    for (size_t i = 0; i < suffix.size(); ++i) {
        table_key_buf[n++] = suffix[i];
    }
    const std::string_view table_key(table_key_buf, n);

    const MakerNoteTableMap* table = find_table(table_key);
    if (!table) {
        return {};
    }
    return find_tag_name(table->entries, table->count, tag);
}

}  // namespace openmeta

