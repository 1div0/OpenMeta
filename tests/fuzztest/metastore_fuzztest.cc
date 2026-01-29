#include "openmeta/meta_edit.h"

#include "fuzztest/fuzztest.h"
#include "gtest/gtest.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace openmeta {

struct Op final {
    uint8_t kind    = 0;
    uint16_t tag    = 0;
    uint32_t value  = 0;
    uint32_t target = 0;
};

static MetaKey
make_exif_key(ByteSpan ifd, uint16_t tag) noexcept
{
    MetaKey key;
    key.kind              = MetaKeyKind::ExifTag;
    key.data.exif_tag.ifd = ifd;
    key.data.exif_tag.tag = tag;
    return key;
}


static void
verify_block_order(const MetaStore& store, BlockId block)
{
    const std::span<const EntryId> ids = store.entries_in_block(block);
    uint32_t last_order                = 0;
    bool first                         = true;

    for (size_t i = 0; i < ids.size(); ++i) {
        const EntryId id = ids[i];
        const Entry& e   = store.entry(id);
        ASSERT_FALSE(any(e.flags, EntryFlags::Deleted));
        ASSERT_EQ(e.origin.block, block);
        if (!first) {
            ASSERT_LE(last_order, e.origin.order_in_block);
        }
        last_order = e.origin.order_in_block;
        first      = false;
    }
}


static void
verify_lookup_tags(const MetaStore& store, std::span<const uint16_t> tags)
{
    MetaKeyView view;
    view.kind              = MetaKeyKind::ExifTag;
    view.data.exif_tag.ifd = "ifd0Id";

    for (size_t i = 0; i < tags.size(); ++i) {
        view.data.exif_tag.tag             = tags[i];
        const std::span<const EntryId> ids = store.find_all(view);
        for (size_t j = 0; j < ids.size(); ++j) {
            const EntryId id = ids[j];
            const Entry& e   = store.entry(id);
            ASSERT_FALSE(any(e.flags, EntryFlags::Deleted));
            ASSERT_EQ(e.key.kind, MetaKeyKind::ExifTag);
            ASSERT_EQ(e.key.data.exif_tag.tag, tags[i]);
        }
    }
}


static void
meta_store_op_stream(const std::vector<Op>& ops)
{
    MetaStore store;
    const BlockId block      = store.add_block(BlockInfo {});
    const ByteSpan ifd_store = store.arena().append_string("ifd0Id");

    const size_t base_count     = std::min<size_t>(ops.size(), 16U);
    uint16_t sample_tags_buf[8] = {};
    uint32_t sample_count       = 0;

    for (size_t i = 0; i < base_count; ++i) {
        if (sample_count < 8U) {
            sample_tags_buf[sample_count] = ops[i].tag;
            ++sample_count;
        }
        Entry e;
        e.key                   = make_exif_key(ifd_store, ops[i].tag);
        e.value                 = make_u32(ops[i].value);
        e.origin.block          = block;
        e.origin.order_in_block = static_cast<uint32_t>(i);
        store.add_entry(e);
    }
    store.finalize();

    MetaEdit edits[2];
    const ByteSpan ifd_edit0 = edits[0].arena().append_string("ifd0Id");
    const ByteSpan ifd_edit1 = edits[1].arena().append_string("ifd0Id");
    (void)ifd_edit0;
    (void)ifd_edit1;

    const std::span<const Entry> base_entries = store.entries();
    for (size_t i = base_count; i < ops.size(); ++i) {
        MetaEdit& edit          = edits[(i - base_count) & 1U];
        const ByteSpan ifd_edit = (&edit == &edits[0]) ? ifd_edit0 : ifd_edit1;

        const uint8_t op_kind = static_cast<uint8_t>(ops[i].kind % 3U);

        if (op_kind == 0U) {
            Entry add;
            add.key                   = make_exif_key(ifd_edit, ops[i].tag);
            add.value                 = make_u32(ops[i].value);
            add.origin.block          = block;
            add.origin.order_in_block = static_cast<uint32_t>(i);
            edit.add_entry(add);
            continue;
        }

        if (base_entries.empty()) {
            continue;
        }
        const EntryId target = static_cast<EntryId>(ops[i].target
                                                    % base_entries.size());

        if (op_kind == 1U) {
            edit.set_value(target, make_u32(ops[i].value));
            continue;
        }

        edit.tombstone(target);
    }

    const std::span<const uint16_t> sample_tags(sample_tags_buf, sample_count);
    const std::span<const MetaEdit> edit_span(edits, 2);

    MetaStore updated = commit(store, edit_span);
    verify_block_order(updated, block);
    verify_lookup_tags(updated, sample_tags);

    MetaStore compacted = compact(updated);
    verify_block_order(compacted, block);
    verify_lookup_tags(compacted, sample_tags);
}


FUZZ_TEST(MetaStoreFuzz, meta_store_op_stream)
    .WithDomains(
        fuzztest::VectorOf(
            fuzztest::StructOf<Op>(fuzztest::InRange<uint8_t>(
                                       0, std::numeric_limits<uint8_t>::max()),
                                   fuzztest::InRange<uint16_t>(
                                       0, std::numeric_limits<uint16_t>::max()),
                                   fuzztest::Arbitrary<uint32_t>(),
                                   fuzztest::Arbitrary<uint32_t>()))
            .WithMaxSize(64));

}  // namespace openmeta
