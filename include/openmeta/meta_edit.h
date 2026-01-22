#pragma once

#include "openmeta/meta_store.h"

#include <span>
#include <vector>

namespace openmeta {

enum class EditOpKind : uint8_t {
    AddEntry,
    SetValue,
    Tombstone,
};

struct EditOp final {
    EditOpKind kind = EditOpKind::AddEntry;
    EntryId target  = kInvalidEntryId;
    Entry entry;
    MetaValue value;
};

class MetaEdit final {
public:
    MetaEdit() = default;

    ByteArena& arena() noexcept;
    const ByteArena& arena() const noexcept;

    void reserve_ops(size_t count);

    void add_entry(const Entry& entry);
    void set_value(EntryId target, const MetaValue& value);
    void tombstone(EntryId target);

    std::span<const EditOp> ops() const noexcept;

private:
    ByteArena arena_;
    std::vector<EditOp> ops_;
};

MetaStore
commit(const MetaStore& base, std::span<const MetaEdit> edits);
MetaStore
compact(const MetaStore& base);

}  // namespace openmeta
