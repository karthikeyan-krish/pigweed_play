#include <bld_meta.h>
#include <gtest/gtest.h>

#include "test_stubs.h"

namespace {

struct PackedMetaRecord {
  uint32_t magic;
  uint8_t active_slot;
  uint8_t confirmed_slot;
  uint8_t pending_slot;
  uint8_t reserved0;
  bld_slot_info slots[2];
  uint32_t record_crc32;
} __attribute__((packed));

uint32_t MetaRecordCrc(const PackedMetaRecord& record) {
  return bld_crc32_ieee(
      &record, sizeof(record) - sizeof(record.record_crc32), BLD_CRC32_INITIAL);
}

PackedMetaRecord MakeDefaultValidRecord() {
  PackedMetaRecord r{};
  r.magic = BLD_META_MAGIC;
  r.active_slot = static_cast<uint8_t>(BLD_SLOT_ID_NONE);
  r.confirmed_slot = static_cast<uint8_t>(BLD_SLOT_ID_NONE);
  r.pending_slot = static_cast<uint8_t>(BLD_SLOT_ID_NONE);
  r.reserved0 = 0u;

  r.slots[BLD_SLOT_ID_A] = {};
  r.slots[BLD_SLOT_ID_A].state = static_cast<uint8_t>(BLD_SLOT_STATE_EMPTY);
  r.slots[BLD_SLOT_ID_A].boot_attempts_left = 0u;

  r.slots[BLD_SLOT_ID_B] = {};
  r.slots[BLD_SLOT_ID_B].state = static_cast<uint8_t>(BLD_SLOT_STATE_EMPTY);
  r.slots[BLD_SLOT_ID_B].boot_attempts_left = 0u;

  r.record_crc32 = MetaRecordCrc(r);
  return r;
}

}  // namespace

class BldMetaTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ctx.bytes.resize(sizeof(PackedMetaRecord), 0xFF);
    storage = test::MakeFakeStorage(&ctx);
  }

  test::FakeStorageCtx ctx;
  bld_storage storage{};
};

TEST_F(BldMetaTest, ReadBootControlFailsForInvalidRecord) {
  bld_boot_control ctrl{};
  EXPECT_LT(bld_meta_read_boot_control(&storage, &ctrl), 0);
}

TEST_F(BldMetaTest, ReadBootControlSucceedsForValidRecord) {
  PackedMetaRecord record = MakeDefaultValidRecord();
  record.active_slot = static_cast<uint8_t>(BLD_SLOT_ID_A);
  record.confirmed_slot = static_cast<uint8_t>(BLD_SLOT_ID_A);
  record.pending_slot = static_cast<uint8_t>(BLD_SLOT_ID_NONE);

  record.slots[BLD_SLOT_ID_A].version = 7u;
  record.slots[BLD_SLOT_ID_A].size = 128u;
  record.slots[BLD_SLOT_ID_A].crc32 = 0x12345678u;
  record.slots[BLD_SLOT_ID_A].state =
      static_cast<uint8_t>(BLD_SLOT_STATE_CONFIRMED);
  record.slots[BLD_SLOT_ID_A].boot_attempts_left = 0u;

  record.record_crc32 = MetaRecordCrc(record);
  memcpy(ctx.bytes.data(), &record, sizeof(record));

  bld_boot_control ctrl{};
  ASSERT_EQ(bld_meta_read_boot_control(&storage, &ctrl), 0);
  EXPECT_EQ(ctrl.active_slot, BLD_SLOT_ID_A);
  EXPECT_EQ(ctrl.confirmed_slot, BLD_SLOT_ID_A);
  EXPECT_EQ(ctrl.pending_slot, BLD_SLOT_ID_NONE);
  EXPECT_EQ(ctrl.slots[BLD_SLOT_ID_A].version, 7u);
  EXPECT_EQ(ctrl.slots[BLD_SLOT_ID_A].size, 128u);
  EXPECT_EQ(ctrl.slots[BLD_SLOT_ID_A].crc32, 0x12345678u);
  EXPECT_EQ(ctrl.slots[BLD_SLOT_ID_A].state, BLD_SLOT_STATE_CONFIRMED);
}

TEST_F(BldMetaTest, WriteBootControlStoresFieldsAndCrc) {
  bld_boot_control ctrl{};
  ctrl.active_slot = BLD_SLOT_ID_A;
  ctrl.confirmed_slot = BLD_SLOT_ID_A;
  ctrl.pending_slot = BLD_SLOT_ID_NONE;
  ctrl.reserved0 = 0u;

  ctrl.slots[BLD_SLOT_ID_A].version = 9u;
  ctrl.slots[BLD_SLOT_ID_A].size = 512u;
  ctrl.slots[BLD_SLOT_ID_A].crc32 = 0xAABBCCDDu;
  ctrl.slots[BLD_SLOT_ID_A].state = BLD_SLOT_STATE_CONFIRMED;
  ctrl.slots[BLD_SLOT_ID_A].boot_attempts_left = 0u;

  ctrl.slots[BLD_SLOT_ID_B].state = BLD_SLOT_STATE_EMPTY;
  ctrl.slots[BLD_SLOT_ID_B].boot_attempts_left = 0u;

  ASSERT_EQ(bld_meta_write_boot_control(&storage, &ctrl), 0);
  EXPECT_EQ(ctx.erase_calls, 1);
  EXPECT_EQ(ctx.write_calls, 1);

  PackedMetaRecord written{};
  memcpy(&written, ctx.bytes.data(), sizeof(written));
  EXPECT_EQ(written.magic, BLD_META_MAGIC);
  EXPECT_EQ(written.active_slot, BLD_SLOT_ID_A);
  EXPECT_EQ(written.confirmed_slot, BLD_SLOT_ID_A);
  EXPECT_EQ(written.pending_slot, BLD_SLOT_ID_NONE);
  EXPECT_EQ(written.slots[BLD_SLOT_ID_A].version, 9u);
  EXPECT_EQ(written.slots[BLD_SLOT_ID_A].size, 512u);
  EXPECT_EQ(written.slots[BLD_SLOT_ID_A].crc32, 0xAABBCCDDu);
  EXPECT_EQ(written.slots[BLD_SLOT_ID_A].state, BLD_SLOT_STATE_CONFIRMED);
  EXPECT_EQ(written.record_crc32, MetaRecordCrc(written));
}

TEST_F(BldMetaTest, SetPendingUpdatesSelectedSlot) {
  ASSERT_EQ(
      bld_meta_set_pending(&storage, BLD_SLOT_ID_B, 3u, 64u, 0x55AA55AAu, 5u),
      0);

  PackedMetaRecord updated{};
  memcpy(&updated, ctx.bytes.data(), sizeof(updated));
  EXPECT_EQ(updated.pending_slot, BLD_SLOT_ID_B);
  EXPECT_EQ(updated.slots[BLD_SLOT_ID_B].version, 3u);
  EXPECT_EQ(updated.slots[BLD_SLOT_ID_B].size, 64u);
  EXPECT_EQ(updated.slots[BLD_SLOT_ID_B].crc32, 0x55AA55AAu);
  EXPECT_EQ(updated.slots[BLD_SLOT_ID_B].state, BLD_SLOT_STATE_PENDING);
  EXPECT_EQ(updated.slots[BLD_SLOT_ID_B].boot_attempts_left, 5u);
  EXPECT_EQ(updated.record_crc32, MetaRecordCrc(updated));
}

TEST_F(BldMetaTest, SetPendingRejectsZeroFields) {
  EXPECT_LT(bld_meta_set_pending(&storage, BLD_SLOT_ID_A, 0u, 1u, 1u, 1u), 0);
  EXPECT_LT(bld_meta_set_pending(&storage, BLD_SLOT_ID_A, 1u, 0u, 1u, 1u), 0);
  EXPECT_LT(bld_meta_set_pending(&storage, BLD_SLOT_ID_A, 1u, 1u, 0u, 1u), 0);
  EXPECT_LT(bld_meta_set_pending(&storage, BLD_SLOT_ID_A, 1u, 1u, 1u, 0u), 0);
}

TEST_F(BldMetaTest, ConfirmSlotPromotesPendingSlot) {
  PackedMetaRecord record = MakeDefaultValidRecord();
  record.pending_slot = static_cast<uint8_t>(BLD_SLOT_ID_B);
  record.slots[BLD_SLOT_ID_B].version = 10u;
  record.slots[BLD_SLOT_ID_B].size = 100u;
  record.slots[BLD_SLOT_ID_B].crc32 = 0x11112222u;
  record.slots[BLD_SLOT_ID_B].state =
      static_cast<uint8_t>(BLD_SLOT_STATE_PENDING);
  record.slots[BLD_SLOT_ID_B].boot_attempts_left = 3u;
  record.record_crc32 = MetaRecordCrc(record);
  memcpy(ctx.bytes.data(), &record, sizeof(record));

  ASSERT_EQ(bld_meta_confirm_slot(&storage), 0);

  PackedMetaRecord updated{};
  memcpy(&updated, ctx.bytes.data(), sizeof(updated));
  EXPECT_EQ(updated.active_slot, BLD_SLOT_ID_B);
  EXPECT_EQ(updated.confirmed_slot, BLD_SLOT_ID_B);
  EXPECT_EQ(updated.pending_slot, BLD_SLOT_ID_NONE);
  EXPECT_EQ(updated.slots[BLD_SLOT_ID_B].state, BLD_SLOT_STATE_CONFIRMED);
  EXPECT_EQ(updated.slots[BLD_SLOT_ID_B].boot_attempts_left, 0u);
}

TEST_F(BldMetaTest, DecrementPendingAttemptsReducesCounter) {
  ASSERT_EQ(bld_meta_set_pending(&storage, BLD_SLOT_ID_A, 1u, 2u, 3u, 4u), 0);

  uint8_t attempts_left = 0u;
  ASSERT_EQ(bld_meta_decrement_pending_attempts(&storage, &attempts_left), 0);
  EXPECT_EQ(attempts_left, 3u);

  PackedMetaRecord updated{};
  memcpy(&updated, ctx.bytes.data(), sizeof(updated));
  EXPECT_EQ(updated.slots[BLD_SLOT_ID_A].boot_attempts_left, 3u);
}

TEST_F(BldMetaTest, MarkSlotBadClearsPendingIfNeeded) {
  ASSERT_EQ(bld_meta_set_pending(&storage, BLD_SLOT_ID_A, 1u, 2u, 3u, 4u), 0);

  ASSERT_EQ(bld_meta_mark_slot_bad(&storage, BLD_SLOT_ID_A), 0);

  PackedMetaRecord updated{};
  memcpy(&updated, ctx.bytes.data(), sizeof(updated));
  EXPECT_EQ(updated.slots[BLD_SLOT_ID_A].state, BLD_SLOT_STATE_BAD);
  EXPECT_EQ(updated.slots[BLD_SLOT_ID_A].boot_attempts_left, 0u);
  EXPECT_EQ(updated.pending_slot, BLD_SLOT_ID_NONE);
}
