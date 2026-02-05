#include "openmeta/geotiff_key_names.h"

namespace openmeta {
namespace {

    struct GeotiffKeyNameEntry final {
        uint16_t key_id = 0;
        const char* name = nullptr;
    };

#include "geotiff_key_names_generated.inc"

    static std::string_view find_key_name(uint16_t key_id) noexcept
    {
        const uint32_t count
            = sizeof(kGeotiffKeys) / sizeof(kGeotiffKeys[0]);
        uint32_t lo = 0;
        uint32_t hi = count;
        while (lo < hi) {
            const uint32_t mid = lo + (hi - lo) / 2;
            const uint16_t cur = kGeotiffKeys[mid].key_id;
            if (cur < key_id) {
                lo = mid + 1;
            } else {
                hi = mid;
            }
        }
        if (lo < count && kGeotiffKeys[lo].key_id == key_id
            && kGeotiffKeys[lo].name) {
            return kGeotiffKeys[lo].name;
        }
        return {};
    }

}  // namespace

std::string_view
geotiff_key_name(uint16_t key_id) noexcept
{
    return find_key_name(key_id);
}

}  // namespace openmeta

