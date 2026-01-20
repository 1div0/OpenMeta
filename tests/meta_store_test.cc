#include "openmeta/meta_edit.h"

#include <gtest/gtest.h>

namespace openmeta {

TEST(MetaStoreTest, SupportsDuplicateKeys)
{
  MetaStore store;
  const BlockId block = store.add_block(BlockInfo{});

  Entry e1;
  e1.key = make_exif_tag_key(store.arena(), "ifd0Id", 0x010f);
  e1.value = make_text(store.arena(), "Canon", TextEncoding::Ascii);
  e1.origin.block = block;
  e1.origin.order_in_block = 0;
  e1.origin.wire_type = WireType{WireFamily::Tiff, 2};
  e1.origin.wire_count = 6;
  store.add_entry(e1);

  Entry e2;
  e2.key = make_exif_tag_key(store.arena(), "ifd0Id", 0x010f);
  e2.value = make_text(store.arena(), "CANON", TextEncoding::Ascii);
  e2.origin.block = block;
  e2.origin.order_in_block = 1;
  e2.origin.wire_type = WireType{WireFamily::Tiff, 2};
  e2.origin.wire_count = 6;
  store.add_entry(e2);

  store.finalize();

  MetaKeyView key;
  key.kind = MetaKeyKind::ExifTag;
  key.data.exif_tag.ifd = "ifd0Id";
  key.data.exif_tag.tag = 0x010f;

  const std::span<const EntryId> ids = store.find_all(key);
  ASSERT_EQ(ids.size(), 2U);
  EXPECT_EQ(ids[0], 0U);
  EXPECT_EQ(ids[1], 1U);
}

TEST(MetaStoreTest, TombstonesHideEntriesFromLookup)
{
  MetaStore store;
  const BlockId block = store.add_block(BlockInfo{});

  Entry e;
  e.key = make_exif_tag_key(store.arena(), "ifd0Id", 0x010f);
  e.value = make_text(store.arena(), "Canon", TextEncoding::Ascii);
  e.origin.block = block;
  store.add_entry(e);
  store.finalize();

  MetaEdit edit;
  edit.tombstone(0);

  MetaStore updated = commit(store, std::span<const MetaEdit>(&edit, 1));
  EXPECT_TRUE(any(updated.entry(0).flags, EntryFlags::Deleted));
  EXPECT_TRUE(any(updated.entry(0).flags, EntryFlags::Dirty));

  MetaKeyView key;
  key.kind = MetaKeyKind::ExifTag;
  key.data.exif_tag.ifd = "ifd0Id";
  key.data.exif_tag.tag = 0x010f;

  EXPECT_TRUE(updated.find_all(key).empty());
}

TEST(MetaStoreTest, CommitAppendsNewEntry)
{
  MetaStore store;
  const BlockId block = store.add_block(BlockInfo{});

  Entry e;
  e.key = make_exif_tag_key(store.arena(), "ifd0Id", 0x010f);
  e.value = make_text(store.arena(), "Canon", TextEncoding::Ascii);
  e.origin.block = block;
  e.origin.order_in_block = 10;
  store.add_entry(e);
  store.finalize();

  MetaEdit edit;
  Entry added;
  added.key = make_exif_tag_key(edit.arena(), "ifd0Id", 0x0110);
  added.value = make_text(edit.arena(), "EOS", TextEncoding::Ascii);
  added.origin.block = block;
  added.origin.order_in_block = 5;
  edit.add_entry(added);

  MetaStore updated = commit(store, std::span<const MetaEdit>(&edit, 1));
  ASSERT_EQ(updated.entries().size(), 2U);

  MetaKeyView key_model;
  key_model.kind = MetaKeyKind::ExifTag;
  key_model.data.exif_tag.ifd = "ifd0Id";
  key_model.data.exif_tag.tag = 0x0110;
  ASSERT_EQ(updated.find_all(key_model).size(), 1U);

  const std::span<const EntryId> ids = updated.entries_in_block(block);
  ASSERT_EQ(ids.size(), 2U);
  EXPECT_EQ(ids[0], 1U);
  EXPECT_EQ(ids[1], 0U);
}

TEST(MetaStoreTest, BlockEntriesAreOrderedByOrigin)
{
  MetaStore store;
  const BlockId block = store.add_block(BlockInfo{});

  Entry e0;
  e0.key = make_exif_tag_key(store.arena(), "ifd0Id", 0x010f);
  e0.value = make_text(store.arena(), "A", TextEncoding::Ascii);
  e0.origin.block = block;
  e0.origin.order_in_block = 10;
  store.add_entry(e0);

  Entry e1;
  e1.key = make_exif_tag_key(store.arena(), "ifd0Id", 0x0110);
  e1.value = make_text(store.arena(), "B", TextEncoding::Ascii);
  e1.origin.block = block;
  e1.origin.order_in_block = 0;
  store.add_entry(e1);

  Entry e2;
  e2.key = make_exif_tag_key(store.arena(), "ifd0Id", 0x0111);
  e2.value = make_text(store.arena(), "C", TextEncoding::Ascii);
  e2.origin.block = block;
  e2.origin.order_in_block = 5;
  store.add_entry(e2);

  store.finalize();

  const std::span<const EntryId> ids = store.entries_in_block(block);
  ASSERT_EQ(ids.size(), 3U);
  EXPECT_EQ(ids[0], 1U);
  EXPECT_EQ(ids[1], 2U);
  EXPECT_EQ(ids[2], 0U);
}

TEST(MetaStoreTest, PreservesWireTypeUtf8_129)
{
  MetaStore store;
  const BlockId block = store.add_block(BlockInfo{});

  Entry e;
  e.key = make_exif_tag_key(store.arena(), "ifd0Id", 0x010e);
  e.value = make_text(store.arena(), "Привет", TextEncoding::Utf8);
  e.origin.block = block;
  e.origin.order_in_block = 0;
  e.origin.wire_type = WireType{WireFamily::Tiff, 129};
  e.origin.wire_count = static_cast<uint32_t>(std::string_view("Привет").size());
  store.add_entry(e);
  store.finalize();

  EXPECT_EQ(store.entry(0).origin.wire_type.code, 129U);
  EXPECT_EQ(store.entry(0).value.text_encoding, TextEncoding::Utf8);
}

} // namespace openmeta
