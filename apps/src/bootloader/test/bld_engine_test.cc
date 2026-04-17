#include "bld_engine.h"

#include <bld_meta.h>
#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "bld_config.h"
#include "test_stubs.h"

namespace {

bld_status_frame LastStatus(const test::FakeTransportCtx& ctx) {
  return test::ReadStruct<bld_status_frame>(ctx.last_sent);
}

bld_meta_frame LastMeta(const test::FakeTransportCtx& ctx) {
  return test::ReadStruct<bld_meta_frame>(ctx.last_sent);
}

bld_boot_control MakeEmptyBootCtrl() {
  bld_boot_control ctrl{};
  ctrl.active_slot = BLD_SLOT_ID_NONE;
  ctrl.confirmed_slot = BLD_SLOT_ID_NONE;
  ctrl.pending_slot = BLD_SLOT_ID_NONE;
  ctrl.reserved0 = 0u;

  ctrl.slots[BLD_SLOT_ID_A].state = BLD_SLOT_STATE_EMPTY;
  ctrl.slots[BLD_SLOT_ID_A].boot_attempts_left = 0u;

  ctrl.slots[BLD_SLOT_ID_B].state = BLD_SLOT_STATE_EMPTY;
  ctrl.slots[BLD_SLOT_ID_B].boot_attempts_left = 0u;
  return ctrl;
}

}  // namespace

class BldEngineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    test::ResetGlobalFakes();

    slot_a_ctx.bytes.resize(BLD_SLOT_A_SIZE, 0xFF);
    slot_b_ctx.bytes.resize(BLD_SLOT_B_SIZE, 0xFF);
    meta_ctx.bytes.resize(128u, 0xFF);

    slot_a_storage = test::MakeFakeStorage(&slot_a_ctx);
    slot_b_storage = test::MakeFakeStorage(&slot_b_ctx);
    meta_storage = test::MakeFakeStorage(&meta_ctx);
    transport = test::MakeFakeTransport(&transport_ctx);
  }

  void InitEngine() {
    ASSERT_EQ(bld_engine_init(&engine,
                              &transport,
                              &slot_a_storage,
                              &slot_b_storage,
                              &meta_storage),
              0);
  }

  void WriteBootCtrl(const bld_boot_control& ctrl) {
    ASSERT_EQ(bld_meta_write_boot_control(&meta_storage, &ctrl), 0);
  }

  bld_boot_control ReadBootCtrl() {
    bld_boot_control ctrl{};
    EXPECT_EQ(bld_meta_read_boot_control(&meta_storage, &ctrl), 0);
    return ctrl;
  }

  test::FakeStorageCtx slot_a_ctx;
  test::FakeStorageCtx slot_b_ctx;
  test::FakeStorageCtx meta_ctx;
  test::FakeTransportCtx transport_ctx;

  bld_storage slot_a_storage{};
  bld_storage slot_b_storage{};
  bld_storage meta_storage{};
  bld_transport transport{};
  bld_engine engine{};
};

TEST_F(BldEngineTest, InitRejectsNullArguments) {
  EXPECT_LT(
      bld_engine_init(
          nullptr, &transport, &slot_a_storage, &slot_b_storage, &meta_storage),
      0);
  EXPECT_LT(
      bld_engine_init(
          &engine, nullptr, &slot_a_storage, &slot_b_storage, &meta_storage),
      0);
  EXPECT_LT(bld_engine_init(
                &engine, &transport, nullptr, &slot_b_storage, &meta_storage),
            0);
  EXPECT_LT(bld_engine_init(
                &engine, &transport, &slot_a_storage, nullptr, &meta_storage),
            0);
  EXPECT_LT(bld_engine_init(
                &engine, &transport, &slot_a_storage, &slot_b_storage, nullptr),
            0);
}

TEST_F(BldEngineTest, InitSucceedsAndStartsIdleWhenMetaIsInvalid) {
  InitEngine();

  EXPECT_EQ(engine.state, BLD_STATE_IDLE);
  EXPECT_EQ(engine.target_slot, BLD_SLOT_ID_NONE);
  EXPECT_EQ(engine.boot_ctrl.active_slot, BLD_SLOT_ID_NONE);
  EXPECT_EQ(engine.boot_ctrl.confirmed_slot, BLD_SLOT_ID_NONE);
  EXPECT_EQ(engine.boot_ctrl.pending_slot, BLD_SLOT_ID_NONE);
}

TEST_F(BldEngineTest, QueryCommandInIdleReturnsOkStatus) {
  InitEngine();
  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_QUERY);

  bld_engine_poll(&engine, 25u);

  ASSERT_EQ(transport_ctx.send_calls, 1);
  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.type, BLD_PKT_STATUS);
  EXPECT_EQ(status.status, BLD_ST_OK);
  EXPECT_EQ(status.state, BLD_STATE_IDLE);
  EXPECT_EQ(status.detail, 0u);
}

TEST_F(BldEngineTest, MetaCommandSendsBootControlFrame) {
  auto ctrl = MakeEmptyBootCtrl();
  ctrl.active_slot = BLD_SLOT_ID_A;
  ctrl.confirmed_slot = BLD_SLOT_ID_A;
  ctrl.pending_slot = BLD_SLOT_ID_B;

  ctrl.slots[BLD_SLOT_ID_A].version = 11u;
  ctrl.slots[BLD_SLOT_ID_A].size = 333u;
  ctrl.slots[BLD_SLOT_ID_A].crc32 = 0x11223344u;
  ctrl.slots[BLD_SLOT_ID_A].state = BLD_SLOT_STATE_CONFIRMED;

  ctrl.slots[BLD_SLOT_ID_B].version = 12u;
  ctrl.slots[BLD_SLOT_ID_B].size = 444u;
  ctrl.slots[BLD_SLOT_ID_B].crc32 = 0x55667788u;
  ctrl.slots[BLD_SLOT_ID_B].state = BLD_SLOT_STATE_PENDING;
  ctrl.slots[BLD_SLOT_ID_B].boot_attempts_left = 3u;

  WriteBootCtrl(ctrl);
  InitEngine();

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_META);
  bld_engine_poll(&engine, 10u);

  ASSERT_EQ(transport_ctx.send_calls, 1);
  const auto frame = LastMeta(transport_ctx);

  EXPECT_EQ(frame.type, BLD_PKT_META);
  EXPECT_EQ(frame.active_slot, BLD_SLOT_ID_A);
  EXPECT_EQ(frame.confirmed_slot, BLD_SLOT_ID_A);
  EXPECT_EQ(frame.pending_slot, BLD_SLOT_ID_B);

  EXPECT_EQ(frame.slot_a.version, 11u);
  EXPECT_EQ(frame.slot_a.size, 333u);
  EXPECT_EQ(frame.slot_a.crc32, 0x11223344u);
  EXPECT_EQ(frame.slot_a.state, BLD_SLOT_STATE_CONFIRMED);

  EXPECT_EQ(frame.slot_b.version, 12u);
  EXPECT_EQ(frame.slot_b.size, 444u);
  EXPECT_EQ(frame.slot_b.crc32, 0x55667788u);
  EXPECT_EQ(frame.slot_b.state, BLD_SLOT_STATE_PENDING);
  EXPECT_EQ(frame.slot_b.boot_attempts_left, 3u);
}

TEST_F(BldEngineTest, StartCommandChoosesOtherThanActiveSlot) {
  auto ctrl = MakeEmptyBootCtrl();
  ctrl.active_slot = BLD_SLOT_ID_A;
  ctrl.confirmed_slot = BLD_SLOT_ID_A;
  ctrl.slots[BLD_SLOT_ID_A].state = BLD_SLOT_STATE_CONFIRMED;

  WriteBootCtrl(ctrl);
  InitEngine();

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_START);
  bld_engine_poll(&engine, 1u);

  EXPECT_EQ(engine.state, BLD_STATE_WAIT_HEADER);
  EXPECT_EQ(engine.target_slot, BLD_SLOT_ID_B);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_OK);
  EXPECT_EQ(status.detail, static_cast<uint32_t>(BLD_SLOT_ID_B));
}

TEST_F(BldEngineTest, StartCommandDefaultsToSlotAWhenNoActiveOrConfirmedSlot) {
  InitEngine();

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_START);
  bld_engine_poll(&engine, 1u);

  EXPECT_EQ(engine.state, BLD_STATE_WAIT_HEADER);
  EXPECT_EQ(engine.target_slot, BLD_SLOT_ID_A);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_OK);
  EXPECT_EQ(status.detail, static_cast<uint32_t>(BLD_SLOT_ID_A));
}

TEST_F(BldEngineTest, HeaderErasesTargetSlotAndMovesToReceiveData) {
  InitEngine();

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_START);
  bld_engine_poll(&engine, 1u);
  ASSERT_EQ(engine.state, BLD_STATE_WAIT_HEADER);

  transport_ctx.next_frame = test::MakeHeaderFrame(128u, 0x12345678u, 7u);
  bld_engine_poll(&engine, 1u);

  EXPECT_EQ(slot_a_ctx.erase_calls + slot_b_ctx.erase_calls, 1);

  test::FakeStorageCtx* target_ctx =
      (engine.target_slot == BLD_SLOT_ID_A) ? &slot_a_ctx : &slot_b_ctx;

  EXPECT_EQ(target_ctx->last_erase_offset, 0u);
  EXPECT_EQ(target_ctx->last_erase_size, 128u);

  EXPECT_EQ(engine.session.expected_seq, 0u);
  EXPECT_EQ(engine.session.received_size, 0u);
  EXPECT_EQ(engine.session.image_size, 128u);
  EXPECT_EQ(engine.session.image_crc32, 0x12345678u);
  EXPECT_EQ(engine.session.image_version, 7u);
  EXPECT_EQ(engine.state, BLD_STATE_RECV_DATA);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_OK);
}

TEST_F(BldEngineTest, OversizedHeaderReturnsTooLargeAndResetsToIdle) {
  InitEngine();

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_START);
  bld_engine_poll(&engine, 1u);

  const uint32_t too_large = (engine.target_slot == BLD_SLOT_ID_A)
                                 ? (BLD_SLOT_A_SIZE + 1u)
                                 : (BLD_SLOT_B_SIZE + 1u);

  transport_ctx.next_frame = test::MakeHeaderFrame(too_large, 0x1234u, 1u);
  bld_engine_poll(&engine, 1u);

  EXPECT_EQ(engine.state, BLD_STATE_IDLE);
  EXPECT_EQ(engine.target_slot, BLD_SLOT_ID_NONE);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_TOO_LARGE);
  EXPECT_EQ(status.detail, too_large);
}

TEST_F(BldEngineTest, DataFrameWritesChunkAndAdvancesSequence) {
  InitEngine();

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_START);
  bld_engine_poll(&engine, 1u);

  transport_ctx.next_frame = test::MakeHeaderFrame(4u, 0xB63CFBCBu, 1u);
  bld_engine_poll(&engine, 1u);

  const uint8_t payload[4] = {1u, 2u, 3u, 4u};
  transport_ctx.next_frame = test::MakeDataFrame(0u, payload, sizeof(payload));
  bld_engine_poll(&engine, 1u);

  test::FakeStorageCtx* target_ctx =
      (engine.target_slot == BLD_SLOT_ID_A) ? &slot_a_ctx : &slot_b_ctx;

  EXPECT_EQ(target_ctx->write_calls, 1);
  EXPECT_EQ(target_ctx->last_write_offset, 0u);
  EXPECT_EQ(target_ctx->last_write_len, sizeof(payload));

  EXPECT_EQ(engine.session.received_size, 4u);
  EXPECT_EQ(engine.session.expected_seq, 1u);
  EXPECT_EQ(engine.state, BLD_STATE_WAIT_END);

  EXPECT_EQ(target_ctx->bytes[0], 1u);
  EXPECT_EQ(target_ctx->bytes[3], 4u);
}

TEST_F(BldEngineTest, DataFrameWithWrongSequenceReturnsSeqErr) {
  InitEngine();

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_START);
  bld_engine_poll(&engine, 1u);

  transport_ctx.next_frame = test::MakeHeaderFrame(4u, 0xB63CFBCBu, 1u);
  bld_engine_poll(&engine, 1u);

  const uint8_t payload[2] = {1u, 2u};
  transport_ctx.next_frame = test::MakeDataFrame(1u, payload, sizeof(payload));
  bld_engine_poll(&engine, 1u);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_SEQ_ERR);
  EXPECT_EQ(status.detail, 0u);
}

TEST_F(BldEngineTest, EndCommandWithValidImageSetsPendingAndReturnsIdle) {
  const std::array<uint8_t, 4> image = {1u, 2u, 3u, 4u};
  const uint32_t crc =
      bld_crc32_ieee(image.data(), image.size(), BLD_CRC32_INITIAL);

  InitEngine();

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_START);
  bld_engine_poll(&engine, 1u);
  const auto target_slot = engine.target_slot;

  transport_ctx.next_frame =
      test::MakeHeaderFrame(static_cast<uint32_t>(image.size()), crc, 77u);
  bld_engine_poll(&engine, 1u);

  transport_ctx.next_frame = test::MakeDataFrame(
      0u, image.data(), static_cast<uint16_t>(image.size()));
  bld_engine_poll(&engine, 1u);

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_END);
  bld_engine_poll(&engine, 1u);

  EXPECT_EQ(engine.state, BLD_STATE_IDLE);
  EXPECT_EQ(engine.target_slot, BLD_SLOT_ID_NONE);

  const auto ctrl = ReadBootCtrl();
  EXPECT_EQ(ctrl.pending_slot, target_slot);
  EXPECT_EQ(ctrl.slots[target_slot].version, 77u);
  EXPECT_EQ(ctrl.slots[target_slot].size, image.size());
  EXPECT_EQ(ctrl.slots[target_slot].crc32, crc);
  EXPECT_EQ(ctrl.slots[target_slot].state, BLD_SLOT_STATE_PENDING);
  EXPECT_EQ(ctrl.slots[target_slot].boot_attempts_left, BLD_MAX_BOOT_ATTEMPTS);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_OK);
}

TEST_F(BldEngineTest, EndCommandWithBadStoredImageCrcReturnsBadCrc) {
  const std::array<uint8_t, 4> image = {1u, 2u, 3u, 4u};
  const uint32_t wrong_crc = 0x12345678u;

  InitEngine();

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_START);
  bld_engine_poll(&engine, 1u);

  transport_ctx.next_frame =
      test::MakeHeaderFrame(static_cast<uint32_t>(image.size()), wrong_crc, 9u);
  bld_engine_poll(&engine, 1u);

  transport_ctx.next_frame = test::MakeDataFrame(
      0u, image.data(), static_cast<uint16_t>(image.size()));
  bld_engine_poll(&engine, 1u);

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_END);
  bld_engine_poll(&engine, 1u);

  EXPECT_EQ(engine.state, BLD_STATE_ERROR);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_BAD_CRC);
  EXPECT_EQ(status.detail, wrong_crc);
}

TEST_F(BldEngineTest, BootDecideAndJumpUsesPendingSlotAndDecrementsAttempts) {
  const std::array<uint8_t, 4> image = {9u, 8u, 7u, 6u};
  const uint32_t crc =
      bld_crc32_ieee(image.data(), image.size(), BLD_CRC32_INITIAL);

  std::copy(image.begin(), image.end(), slot_a_ctx.bytes.begin());

  auto ctrl = MakeEmptyBootCtrl();
  ctrl.pending_slot = BLD_SLOT_ID_A;
  ctrl.slots[BLD_SLOT_ID_A].version = 1u;
  ctrl.slots[BLD_SLOT_ID_A].size = image.size();
  ctrl.slots[BLD_SLOT_ID_A].crc32 = crc;
  ctrl.slots[BLD_SLOT_ID_A].state = BLD_SLOT_STATE_PENDING;
  ctrl.slots[BLD_SLOT_ID_A].boot_attempts_left = 3u;

  WriteBootCtrl(ctrl);
  InitEngine();

  ASSERT_EQ(bld_engine_boot_decide_and_jump(&engine), 0);
  EXPECT_EQ(test::g_last_jump_image_base, BLD_SLOT_A_BASE);

  const auto updated = ReadBootCtrl();
  EXPECT_EQ(updated.pending_slot, BLD_SLOT_ID_A);
  EXPECT_EQ(updated.slots[BLD_SLOT_ID_A].boot_attempts_left, 2u);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_OK);
  EXPECT_EQ(status.detail, static_cast<uint32_t>(BLD_SLOT_ID_A));
}

TEST_F(BldEngineTest,
       BootDecideAndJumpFallsBackToConfirmedSlotWhenPendingIsBad) {
  const std::array<uint8_t, 4> confirmed_image = {5u, 6u, 7u, 8u};
  const uint32_t confirmed_crc = bld_crc32_ieee(
      confirmed_image.data(), confirmed_image.size(), BLD_CRC32_INITIAL);

  std::copy(
      confirmed_image.begin(), confirmed_image.end(), slot_b_ctx.bytes.begin());

  auto ctrl = MakeEmptyBootCtrl();
  ctrl.confirmed_slot = BLD_SLOT_ID_B;
  ctrl.pending_slot = BLD_SLOT_ID_A;

  ctrl.slots[BLD_SLOT_ID_A].version = 10u;
  ctrl.slots[BLD_SLOT_ID_A].size = 4u;
  ctrl.slots[BLD_SLOT_ID_A].crc32 = 0x11112222u;
  ctrl.slots[BLD_SLOT_ID_A].state = BLD_SLOT_STATE_PENDING;
  ctrl.slots[BLD_SLOT_ID_A].boot_attempts_left = 2u;

  ctrl.slots[BLD_SLOT_ID_B].version = 20u;
  ctrl.slots[BLD_SLOT_ID_B].size = confirmed_image.size();
  ctrl.slots[BLD_SLOT_ID_B].crc32 = confirmed_crc;
  ctrl.slots[BLD_SLOT_ID_B].state = BLD_SLOT_STATE_CONFIRMED;

  WriteBootCtrl(ctrl);
  InitEngine();

  ASSERT_EQ(bld_engine_boot_decide_and_jump(&engine), 0);
  EXPECT_EQ(test::g_last_jump_image_base, BLD_SLOT_B_BASE);

  const auto updated = ReadBootCtrl();
  EXPECT_EQ(updated.slots[BLD_SLOT_ID_A].state, BLD_SLOT_STATE_BAD);
  EXPECT_EQ(updated.pending_slot, BLD_SLOT_ID_NONE);
  EXPECT_EQ(updated.confirmed_slot, BLD_SLOT_ID_B);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_OK);
  EXPECT_EQ(status.detail, static_cast<uint32_t>(BLD_SLOT_ID_B));
}

TEST_F(BldEngineTest, BootDecideAndJumpReturnsErrorWhenNothingBootable) {
  auto ctrl = MakeEmptyBootCtrl();
  WriteBootCtrl(ctrl);
  InitEngine();

  EXPECT_LT(bld_engine_boot_decide_and_jump(&engine), 0);
  EXPECT_EQ(test::g_last_jump_image_base, 0u);

  ASSERT_EQ(transport_ctx.send_calls, 1);
  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_BOOT_ERR);
  EXPECT_EQ(status.detail, 0u);
}

TEST_F(BldEngineTest, AbortCommandResetsSessionAndReturnsIdle) {
  InitEngine();

  engine.state = BLD_STATE_RECV_DATA;
  engine.target_slot = BLD_SLOT_ID_B;
  engine.session.expected_seq = 5u;
  engine.session.received_size = 99u;
  engine.session.image_size = 123u;

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_ABORT);
  bld_engine_poll(&engine, 1u);

  EXPECT_EQ(engine.state, BLD_STATE_IDLE);
  EXPECT_EQ(engine.target_slot, BLD_SLOT_ID_NONE);
  EXPECT_EQ(engine.session.expected_seq, 0u);
  EXPECT_EQ(engine.session.received_size, 0u);
  EXPECT_EQ(engine.session.image_size, 0u);

  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_OK);
}

TEST_F(BldEngineTest, PollRejectsFrameWithBadCrc) {
  InitEngine();

  transport_ctx.next_frame = test::MakeCmdFrame(BLD_CMD_QUERY);
  transport_ctx.next_frame[sizeof(bld_cmd_frame) - 2u] ^= 0xFFu;

  bld_engine_poll(&engine, 1u);

  ASSERT_EQ(transport_ctx.send_calls, 1);
  const auto status = LastStatus(transport_ctx);
  EXPECT_EQ(status.status, BLD_ST_BAD_CRC);
}
