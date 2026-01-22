#pragma once

#include "openmeta/byte_arena.h"
#include "openmeta/meta_flags.h"
#include "openmeta/meta_key.h"
#include "openmeta/meta_value.h"

#include <cstdint>
#include <span>
#include <vector>

namespace openmeta {

class MetaEdit;

using BlockId = uint32_t;
using EntryId = uint32_t;

static constexpr BlockId kInvalidBlockId = 0xffffffffU;
static constexpr EntryId kInvalidEntryId = 0xffffffffU;

enum class WireFamily : uint8_t {
    None,
    Tiff,
    Other,
};

struct WireType final {
    WireFamily family = WireFamily::None;
    uint16_t code     = 0;
};

struct Origin final {
    BlockId block           = kInvalidBlockId;
    uint32_t order_in_block = 0;
    WireType wire_type;
    uint32_t wire_count = 0;
};

struct Entry final {
    MetaKey key;
    MetaValue value;
    Origin origin;
    EntryFlags flags = EntryFlags::None;
};

struct BlockInfo final {
    uint32_t format    = 0;
    uint32_t container = 0;
    uint32_t id        = 0;
};

struct KeySpan final {
    uint32_t start = 0;
    uint32_t count = 0;
    EntryId repr   = kInvalidEntryId;
};

struct BlockSpan final {
    uint32_t start = 0;
    uint32_t count = 0;
};

class MetaStore final {
public:
    MetaStore() = default;

    // Build phase (not thread-safe, not allowed after finalize).
    BlockId add_block(const BlockInfo& info);
    EntryId add_entry(const Entry& entry);

    ByteArena& arena() noexcept;
    const ByteArena& arena() const noexcept;

    void finalize();
    void rehash();

    uint32_t block_count() const noexcept;
    const BlockInfo& block_info(BlockId id) const noexcept;

    std::span<const Entry> entries() const noexcept;
    const Entry& entry(EntryId id) const noexcept;

    std::span<const EntryId> entries_in_block(BlockId block) const noexcept;
    std::span<const EntryId> find_all(const MetaKeyView& key) const noexcept;

private:
    friend MetaStore commit(const MetaStore& base,
                            std::span<const MetaEdit> edits);
    friend MetaStore compact(const MetaStore& base);

    void rebuild_block_index();
    void rebuild_key_index();
    void clear_indices() noexcept;

    ByteArena arena_;
    std::vector<Entry> entries_;
    std::vector<BlockInfo> blocks_;

    std::vector<EntryId> entries_by_block_;
    std::vector<BlockSpan> block_spans_;

    std::vector<EntryId> entries_by_key_;
    std::vector<KeySpan> key_spans_;

    bool finalized_ = false;
};

}  // namespace openmeta
