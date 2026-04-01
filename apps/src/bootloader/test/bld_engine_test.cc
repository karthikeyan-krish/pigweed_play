#include "bld_engine.h"

#include <bld_meta.h>
#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "bld_config.h"
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

PackedMetaRecord MakeMetaRecord(uint32_t version,
                                uint32_t size,
                                uint32_t crc32,
                                uint8_t state) {
  PackedMetaRecord r{};
  r.magic = BLD_META_MAGIC;
  r.image_version = version;
  r.image_size = size;
  r.image_crc32 = crc32;
  r.state = state;
  r.record_crc32 =
      bld_crc32_ieee(&r, sizeof(r) - sizeof(r.record_crc32), BLD_CRC32_INITIAL);
  return r;
}

bld_status_frame LastStatus(const test::FakeTransportCtx& ctx) {
  return test::ReadStruct<bld_status_frame>(ctx.last_sent);
}

bld_meta_frame LastMeta(const test::FakeTransportCtx& ctx) {
  return test::ReadStruct<bld_meta_frame>(ctx.last_sent);
}

uint32_t PackedMetaRecordCrc(const PackedMetaRecord& record) {
  return bld_crc32_ieee(
      &record, sizeof(record) - sizeof(record.record_crc32), BLD_CRC32_INITIAL);
}
}  // namespace

class BldEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test::ResetGlobalFakes();
    meta_ctx.bytes.resize(64, 0xFF);
    slot_ctx.bytes.resize(2048, 0xFF);
    meta_storage = test::MakeFakeStorage(&meta_ctx);
    slot_storage = test::MakeFakeStorage(&slot_ctx);
    transport = test::MakeFakeTransport(&transport_ctx);
  }

  void InitEngine() {
    ASSERT_EQ(
        bld_engine_init(&engine, &transport, &slot_storage, &meta_storage), 0);
  }

  test::FakeStorageCtx meta_ctx;
  test::FakeStorageCtx slot_ctx;
  test::FakeTransportCtx transport_ctx;
  bld_storage meta_storage{};
  bld_storage slot_storage{};
  bld_transport transport{};
  bld_engine engine{};
};

TEST_F(BldEngineTest, InitRejectsNullArguments) {
  EXPECT_LT(bld_engine_init(nullptr, &transport, &slot_storage, &meta_storage),
            0);
  EXPECT_LT(bld_engine_init(&engine, nullptr, &slot_storage, &meta_storage), 0);
  EXPECT_LT(bld_engine_init(&engine, &transport, nullptr, &meta_storage), 0);
  EXPECT_LT(bld_engine_init(&engine, &transport, &slot_storage, nullptr), 0);
}

TEST_F(BldEngineTest, InitFallsBackToEmptyImageWhenMetaReadFails) {
  ASSERT_EQ(bld_engine_init(&engine, &transport, &slot_storage, &meta_storage),
            0);
  EXPECT_EQ(engine.state, BLD_STATE_IDLE);
  EXPECT_EQ(engine.image_info.state, BLD_IMAGE_STATE_EMPTY);
}

TEST_F(BldEngineTest, QueryCommandInIdleReturnsOkStatus) {
  InitEngine();
  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_QUERY);

  bld_engine_poll(&engine, 25);

  ASSERT_EQ(transport_ctx.send_calls, 1);
  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_OK);
  EXPECT_EQ(status.state, BLD_STATE_IDLE);
}

TEST_F(BldEngineTest, MetaCommandSendsMetadataFrame) {
  const auto meta = MakeMetaRecord(11, 333, 0x11223344u, BLD_IMAGE_STATE_READY);
  memcpy(meta_ctx.bytes.data(), &meta, sizeof(meta));
  InitEngine();
  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_META);

  bld_engine_poll(&engine, 10);

  ASSERT_EQ(transport_ctx.send_calls, 1);
  const auto frame = LastMeta(transport_ctx);
  EXPECT_EQ(frame.type, BLD_PKT_META);
  EXPECT_EQ(frame.image_version, 11u);
  EXPECT_EQ(frame.image_size, 333u);
  EXPECT_EQ(frame.image_crc32, 0x11223344u);
  EXPECT_EQ(frame.state, BLD_IMAGE_STATE_READY);
}

TEST_F(BldEngineTest, OversizedHeaderReturnsTooLargeAndResetsToIdle) {
  InitEngine();
  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_START);
  bld_engine_poll(&engine, 1);

  transport_ctx.next_frame =
      test::MakeHeaderFrame(BLD_SLOT_SIZE + 1u, 0x1234u, 1);
  bld_engine_poll(&engine, 1);

  EXPECT_EQ(engine.state, BLD_STATE_IDLE);
  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_TOO_LARGE);
}

TEST_F(BldEngineTest, DataFrameWritesChunkAndAdvancesSequence) {
  InitEngine();
  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_START);
  bld_engine_poll(&engine, 1);
  transport_ctx.next_frame = test::MakeHeaderFrame(4, 0xB63CFBCBu, 1);
  bld_engine_poll(&engine, 1);

  const uint8_t payload[4] = {1, 2, 3, 4};
  transport_ctx.next_frame = test::MakeDataFrame(0, payload, sizeof(payload));
  bld_engine_poll(&engine, 1);

  EXPECT_EQ(slot_ctx.write_calls, 1);
  EXPECT_EQ(engine.session.received_size, 4u);
  EXPECT_EQ(engine.session.expected_seq, 1u);
  EXPECT_EQ(engine.state, BLD_STATE_WAIT_END);
  EXPECT_EQ(slot_ctx.bytes[0], 1u);
  EXPECT_EQ(slot_ctx.bytes[3], 4u);
}

TEST_F(BldEngineTest, DataFrameWithWrongSequenceReturnsSeqErr) {
  InitEngine();
  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_START);
  bld_engine_poll(&engine, 1);
  transport_ctx.next_frame = test::MakeHeaderFrame(4, 0xB63CFBCBu, 1);
  bld_engine_poll(&engine, 1);

  const uint8_t payload[2] = {1, 2};
  transport_ctx.next_frame = test::MakeDataFrame(1, payload, sizeof(payload));
  bld_engine_poll(&engine, 1);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_SEQ_ERR);
  EXPECT_EQ(status.detail, 0u);
}

TEST_F(BldEngineTest, EndCommandWithValidImageWritesMetaAndReturnsIdle) {
  const std::array<uint8_t, 4> image = {1, 2, 3, 4};
  const uint32_t crc =
      bld_crc32_ieee(image.data(), image.size(), BLD_CRC32_INITIAL);

  InitEngine();
  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_START);
  bld_engine_poll(&engine, 1);
  transport_ctx.next_frame = test::MakeHeaderFrame(image.size(), crc, 77);
  bld_engine_poll(&engine, 1);
  transport_ctx.next_frame = test::MakeDataFrame(0, image.data(), image.size());
  bld_engine_poll(&engine, 1);
  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_END);
  bld_engine_poll(&engine, 1);

  EXPECT_EQ(engine.state, BLD_STATE_IDLE);
  EXPECT_EQ(engine.image_info.version, 77u);
  EXPECT_EQ(engine.image_info.size, image.size());
  EXPECT_EQ(engine.image_info.crc32, crc);
  EXPECT_EQ(engine.image_info.state, BLD_IMAGE_STATE_READY);
}

TEST_F(BldEngineTest, BootDecideAndJumpWithReadyImageSendsStatusAndJumps) {
  const std::array<uint8_t, 4> image = {9, 8, 7, 6};
  const uint32_t crc =
      bld_crc32_ieee(image.data(), image.size(), BLD_CRC32_INITIAL);
  std::copy(image.begin(), image.end(), slot_ctx.bytes.begin());
  const auto meta = MakeMetaRecord(1, image.size(), crc, BLD_IMAGE_STATE_READY);
  memcpy(meta_ctx.bytes.data(), &meta, sizeof(meta));

  InitEngine();

  ASSERT_EQ(bld_engine_boot_decide_and_jump(&engine), 0);
  EXPECT_EQ(transport_ctx.send_calls, 1);
  EXPECT_EQ(test::g_last_jump_image_base, BLD_SLOT_BASE);
}

TEST_F(BldEngineTest, BootDecideAndJumpPersistsCorruptedMetaOnBadImageCrc) {
  const std::array<uint8_t, 4> image = {9, 8, 7, 6};
  std::copy(image.begin(), image.end(), slot_ctx.bytes.begin());

  const auto meta =
      MakeMetaRecord(1, image.size(), 0x12345678u, BLD_IMAGE_STATE_READY);
  memcpy(meta_ctx.bytes.data(), &meta, sizeof(meta));

  InitEngine();

  EXPECT_LT(bld_engine_boot_decide_and_jump(&engine), 0);
  EXPECT_EQ(engine.image_info.state, BLD_IMAGE_STATE_CORRUPTED);

  PackedMetaRecord stored{};
  memcpy(&stored, meta_ctx.bytes.data(), sizeof(stored));

  EXPECT_EQ(stored.magic, BLD_META_MAGIC);
  EXPECT_EQ(stored.image_version, 1u);
  EXPECT_EQ(stored.image_size, image.size());
  EXPECT_EQ(stored.image_crc32, 0x12345678u);
  EXPECT_EQ(stored.state, BLD_IMAGE_STATE_CORRUPTED);
  EXPECT_EQ(stored.record_crc32, PackedMetaRecordCrc(stored));
}

TEST_F(BldEngineTest, BootDecideAndJumpstateCorruptedReturnsErr) {
  const std::array<uint8_t, 4> image = {9, 8, 7, 6};
  std::copy(image.begin(), image.end(), slot_ctx.bytes.begin());
  const auto meta =
      MakeMetaRecord(1, image.size(), 0x12345678u, BLD_IMAGE_STATE_CORRUPTED);
  memcpy(meta_ctx.bytes.data(), &meta, sizeof(meta));

  InitEngine();

  EXPECT_LT(bld_engine_boot_decide_and_jump(&engine), 0);
  EXPECT_EQ(engine.image_info.state, BLD_IMAGE_STATE_CORRUPTED);
  EXPECT_EQ(transport_ctx.send_calls, 1);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_BOOT_ERR);
  EXPECT_EQ(status.detail, BLD_IMAGE_STATE_CORRUPTED);
}

TEST_F(BldEngineTest, PollRejectsFrameWithBadCrc) {
  InitEngine();
  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_QUERY);
  transport_ctx.next_frame[sizeof(bld_cmd_frame) - 2] ^= 0xFF;

  bld_engine_poll(&engine, 1);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_BAD_CRC);
}

TEST_F(BldEngineTest, PollIgnoresNegativeParseResult) {
  InitEngine();

  transport_ctx.parse_result_override = -1;

  bld_engine_poll(&engine, 10);

  EXPECT_EQ(transport_ctx.parse_calls, 1);
  EXPECT_EQ(transport_ctx.send_calls, 0);
  EXPECT_EQ(engine.state, BLD_STATE_IDLE);
}
