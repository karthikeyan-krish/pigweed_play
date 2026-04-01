#include <bld_meta.h>
#include <gtest/gtest.h>

#include "test_stubs.h"

namespace {

struct PackedMetaRecord {
  uint32_t magic;
  uint32_t image_version;
  uint32_t image_size;
  uint32_t image_crc32;
  uint8_t state;
  uint16_t reserved[3];
  uint32_t record_crc32;
} __attribute__((packed));

uint32_t MetaRecordCrc(const PackedMetaRecord& record) {
  return bld_crc32_ieee(
      &record, sizeof(record) - sizeof(record.record_crc32), BLD_CRC32_INITIAL);
}

PackedMetaRecord MakeValidRecord(uint32_t version,
                                 uint32_t size,
                                 uint32_t crc32,
                                 uint8_t state) {
  PackedMetaRecord r{};
  r.magic = BLD_META_MAGIC;
  r.image_version = version;
  r.image_size = size;
  r.image_crc32 = crc32;
  r.state = state;
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

TEST_F(BldMetaTest, ReadInfoFailsForInvalidRecord) {
  bld_image_info info{};
  EXPECT_LT(bld_meta_read_info(&storage, &info), 0);
}

TEST_F(BldMetaTest, ReadInfoSucceedsForValidRecord) {
  const PackedMetaRecord record =
      MakeValidRecord(7, 128, 0x12345678u, BLD_IMAGE_STATE_READY);
  memcpy(ctx.bytes.data(), &record, sizeof(record));

  bld_image_info info{};
  ASSERT_EQ(bld_meta_read_info(&storage, &info), 0);
  EXPECT_EQ(info.version, 7u);
  EXPECT_EQ(info.size, 128u);
  EXPECT_EQ(info.crc32, 0x12345678u);
  EXPECT_EQ(info.state, BLD_IMAGE_STATE_READY);
}

TEST_F(BldMetaTest, WriteInfoStoresReadyStateAndFields) {
  ASSERT_EQ(bld_meta_write_info(&storage, 9, 512, 0xAABBCCDDu), 0);
  EXPECT_EQ(ctx.erase_calls, 1);
  EXPECT_EQ(ctx.write_calls, 1);

  PackedMetaRecord written{};
  memcpy(&written, ctx.bytes.data(), sizeof(written));
  EXPECT_EQ(written.magic, BLD_META_MAGIC);
  EXPECT_EQ(written.image_version, 9u);
  EXPECT_EQ(written.image_size, 512u);
  EXPECT_EQ(written.image_crc32, 0xAABBCCDDu);
  EXPECT_EQ(written.state, BLD_IMAGE_STATE_READY);
  EXPECT_EQ(written.record_crc32, MetaRecordCrc(written));
}

TEST_F(BldMetaTest, WriteInfoRejectsZeroFields) {
  EXPECT_LT(bld_meta_write_info(&storage, 0, 1, 1), 0);
  EXPECT_LT(bld_meta_write_info(&storage, 1, 0, 1), 0);
  EXPECT_LT(bld_meta_write_info(&storage, 1, 1, 0), 0);
}

TEST_F(BldMetaTest, SetStateUpdatesExistingRecord) {
  const PackedMetaRecord record =
      MakeValidRecord(3, 64, 0x55AA55AAu, BLD_IMAGE_STATE_READY);
  memcpy(ctx.bytes.data(), &record, sizeof(record));

  ASSERT_EQ(bld_meta_set_state(&storage, BLD_IMAGE_STATE_EMPTY), 0);

  PackedMetaRecord updated{};
  memcpy(&updated, ctx.bytes.data(), sizeof(updated));
  EXPECT_EQ(updated.image_version, 3u);
  EXPECT_EQ(updated.image_size, 64u);
  EXPECT_EQ(updated.image_crc32, 0x55AA55AAu);
  EXPECT_EQ(updated.state, BLD_IMAGE_STATE_EMPTY);
  EXPECT_EQ(updated.record_crc32, MetaRecordCrc(updated));

  EXPECT_EQ(bld_meta_set_state(&storage, BLD_IMAGE_STATE_CORRUPTED), 0);
  memcpy(&updated, ctx.bytes.data(), sizeof(updated));
  EXPECT_EQ(updated.image_version, 3u);
  EXPECT_EQ(updated.image_size, 64u);
  EXPECT_EQ(updated.image_crc32, 0x55AA55AAu);
  EXPECT_EQ(updated.state, BLD_IMAGE_STATE_CORRUPTED);
  EXPECT_EQ(updated.record_crc32, MetaRecordCrc(updated));
}
