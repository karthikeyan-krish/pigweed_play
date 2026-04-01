#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include "bld_storage_flash.h"
}

namespace {

struct FakeFlashHw {
  std::vector<uint8_t> mem;
  uint32_t base_addr = 0;

  int unlock_result = 0;
  int lock_result = 0;
  int read_result = 0;
  int erase_result = 0;
  int program_result = 0;

  int unlock_calls = 0;
  int lock_calls = 0;
  int read_calls = 0;
  int erase_calls = 0;
  int program_calls = 0;

  uint32_t last_read_addr = 0;
  uint32_t last_read_len = 0;

  uint32_t last_erase_bank = 0;
  uint32_t last_erase_first_page = 0;
  uint32_t last_erase_num_pages = 0;

  uint32_t second_erase_bank = 0;
  uint32_t second_erase_first_page = 0;
  uint32_t second_erase_num_pages = 0;

  uint32_t last_program_addr = 0;
  uint64_t last_program_data = 0;
};

int FakeFlashUnlock(void* hw) {
  auto* ctx = static_cast<FakeFlashHw*>(hw);
  ctx->unlock_calls++;
  return ctx->unlock_result;
}

int FakeFlashLock(void* hw) {
  auto* ctx = static_cast<FakeFlashHw*>(hw);
  ctx->lock_calls++;
  return ctx->lock_result;
}

int FakeFlashRead(void* hw, uint32_t addr, uint8_t* out, uint32_t len) {
  auto* ctx = static_cast<FakeFlashHw*>(hw);
  ctx->read_calls++;
  ctx->last_read_addr = addr;
  ctx->last_read_len = len;

  if (ctx->read_result != 0) {
    return ctx->read_result;
  }

  if (addr < ctx->base_addr) {
    return -1;
  }

  const uint32_t offset = addr - ctx->base_addr;
  if (offset + len > ctx->mem.size()) {
    return -1;
  }

  std::memcpy(out, ctx->mem.data() + offset, len);
  return 0;
}

int FakeFlashErasePages(void* hw,
                        uint32_t bank,
                        uint32_t first_page,
                        uint32_t num_pages) {
  auto* ctx = static_cast<FakeFlashHw*>(hw);
  ctx->erase_calls++;

  if (ctx->erase_calls == 1) {
    ctx->last_erase_bank = bank;
    ctx->last_erase_first_page = first_page;
    ctx->last_erase_num_pages = num_pages;
  } else if (ctx->erase_calls == 2) {
    ctx->second_erase_bank = bank;
    ctx->second_erase_first_page = first_page;
    ctx->second_erase_num_pages = num_pages;
  }

  return ctx->erase_result;
}

int FakeFlashProgramDoubleword(void* hw, uint32_t addr, uint64_t data) {
  auto* ctx = static_cast<FakeFlashHw*>(hw);
  ctx->program_calls++;
  ctx->last_program_addr = addr;
  ctx->last_program_data = data;

  if (ctx->program_result != 0) {
    return ctx->program_result;
  }

  if (addr < ctx->base_addr) {
    return -1;
  }

  const uint32_t offset = addr - ctx->base_addr;
  if (offset + sizeof(uint64_t) > ctx->mem.size()) {
    return -1;
  }

  std::memcpy(ctx->mem.data() + offset, &data, sizeof(data));
  return 0;
}

const bld_flash_ops kFlashOps = {
    .unlock = FakeFlashUnlock,
    .lock = FakeFlashLock,
    .read = FakeFlashRead,
    .erase_pages = FakeFlashErasePages,
    .program_doubleword = FakeFlashProgramDoubleword,
};

class BldStorageStm32l4Test : public ::testing::Test {
 protected:
  void SetUp() override {
    hw.mem.resize(0x4000u, 0xFF);
    hw.base_addr = 0x08020000u;

    ctx = {
        .region_base = 0x08020000u,
        .region_size = 0x4000u,
        .page_size = 0x800u,
        .flash_base = 0x08000000u,
        .flash_bank_size = 0x100000u,
        .flash_page_size = 0x800u,
        .flash_bank1 = 0x01u,
        .flash_bank2 = 0x02u,
        .ops = &kFlashOps,
        .hw = &hw,
    };

    ASSERT_EQ(bld_storage_flash_init(&storage, &ctx), 0);
  }

  FakeFlashHw hw{};
  bld_storage storage{};
  bld_storage_flash_ctx ctx{};
};

TEST(BldStorageStm32l4InitTest, InitSetsFunctionPointersAndContext) {
  bld_storage storage{};
  FakeFlashHw hw{};
  const bld_storage_flash_ctx ctx{
      .region_base = 0x08020000u,
      .region_size = 0x1000u,
      .page_size = 0x800u,
      .flash_base = 0x08000000u,
      .flash_bank_size = 0x100000u,
      .flash_page_size = 0x800u,
      .flash_bank1 = 0x01u,
      .flash_bank2 = 0x02u,
      .ops = &kFlashOps,
      .hw = &hw,
  };

  ASSERT_EQ(bld_storage_flash_init(&storage, &ctx), 0);
  EXPECT_TRUE(storage.erase != nullptr);
  EXPECT_TRUE(storage.write != nullptr);
  EXPECT_TRUE(storage.read != nullptr);
  EXPECT_EQ(storage.ctx, &ctx);
}

TEST(BldStorageStm32l4InitTest, InitRejectsNullArguments) {
  bld_storage storage{};
  FakeFlashHw hw{};
  const bld_storage_flash_ctx ctx{
      .region_base = 0x08020000u,
      .region_size = 0x1000u,
      .page_size = 0x800u,
      .flash_base = 0x08000000u,
      .flash_bank_size = 0x100000u,
      .flash_page_size = 0x800u,
      .flash_bank1 = 0x01u,
      .flash_bank2 = 0x02u,
      .ops = &kFlashOps,
      .hw = &hw,
  };

  EXPECT_LT(bld_storage_flash_init(nullptr, &ctx), 0);
  EXPECT_LT(bld_storage_flash_init(&storage, nullptr), 0);
}

TEST_F(BldStorageStm32l4Test, ReadFailsIfReadOpIsNull) {
  bld_flash_ops ops = kFlashOps;
  ops.read = nullptr;
  ctx.ops = &ops;

  EXPECT_LT(bld_storage_flash_init(&storage, &ctx), 0);
}

TEST_F(BldStorageStm32l4Test, InitRejectsMissingUnlockOp) {
  bld_flash_ops ops = kFlashOps;
  ops.unlock = nullptr;
  ctx.ops = &ops;
  EXPECT_LT(bld_storage_flash_init(&storage, &ctx), 0);
}

TEST_F(BldStorageStm32l4Test, InitRejectsMissingLockOp) {
  bld_flash_ops ops = kFlashOps;
  ops.lock = nullptr;
  ctx.ops = &ops;
  EXPECT_LT(bld_storage_flash_init(&storage, &ctx), 0);
}

TEST_F(BldStorageStm32l4Test, InitRejectsMissingErasePagesOp) {
  bld_flash_ops ops = kFlashOps;
  ops.erase_pages = nullptr;
  ctx.ops = &ops;
  EXPECT_LT(bld_storage_flash_init(&storage, &ctx), 0);
}

TEST_F(BldStorageStm32l4Test, InitRejectsMissingProgramDoublewordOp) {
  bld_flash_ops ops = kFlashOps;
  ops.program_doubleword = nullptr;
  ctx.ops = &ops;
  EXPECT_LT(bld_storage_flash_init(&storage, &ctx), 0);
}

TEST_F(BldStorageStm32l4Test, ReadFailsForOutOfRangeAccess) {
  uint8_t out[8] = {};
  EXPECT_LT(storage.read(&storage, ctx.region_size - 4u, out, 8u), 0);
}

TEST_F(BldStorageStm32l4Test, WriteFailsForOutOfRangeAccess) {
  const uint8_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_LT(storage.write(&storage, ctx.region_size - 4u, data, 8u), 0);
}

TEST_F(BldStorageStm32l4Test, EraseFailsForOutOfRangeAccess) {
  EXPECT_LT(storage.erase(&storage, ctx.region_size - 0x400u, 0x800u), 0);
}

TEST_F(BldStorageStm32l4Test, AlignedEraseSucceeds) {
  ASSERT_EQ(storage.erase(&storage, 0u, 0x800u), 0);

  EXPECT_EQ(hw.unlock_calls, 1);
  EXPECT_EQ(hw.lock_calls, 1);
  EXPECT_EQ(hw.erase_calls, 1);
  EXPECT_EQ(hw.last_erase_bank, ctx.flash_bank1);
  EXPECT_EQ(hw.last_erase_first_page,
            64u);  // 0x08020000 / 0x800 page offset from flash_base
  EXPECT_EQ(hw.last_erase_num_pages, 1u);
}

TEST_F(BldStorageStm32l4Test, WritePadsPartialDoublewordWithFF) {
  const uint8_t data[3] = {0x11, 0x22, 0x33};

  ASSERT_EQ(storage.write(&storage, 0u, data, sizeof(data)), 0);

  EXPECT_EQ(hw.unlock_calls, 1);
  EXPECT_EQ(hw.lock_calls, 1);
  EXPECT_EQ(hw.program_calls, 1);
  EXPECT_EQ(hw.last_program_addr, ctx.region_base);

  const std::array<uint8_t, 8> expected = {
      0x11, 0x22, 0x33, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  std::array<uint8_t, 8> actual{};
  std::memcpy(
      actual.data(), &hw.last_program_data, sizeof(hw.last_program_data));
  EXPECT_EQ(actual, expected);
}

TEST_F(BldStorageStm32l4Test, CrossBankEraseCallsEraseTwice) {
  ctx.region_base = ctx.flash_base + ctx.flash_bank_size - 0x800u;
  ctx.region_size = 0x2000u;
  ASSERT_EQ(bld_storage_flash_init(&storage, &ctx), 0);

  ASSERT_EQ(storage.erase(&storage, 0u, 0x1000u), 0);

  EXPECT_EQ(hw.erase_calls, 2);

  EXPECT_EQ(hw.last_erase_bank, ctx.flash_bank1);
  EXPECT_EQ(hw.last_erase_num_pages, 1u);

  EXPECT_EQ(hw.second_erase_bank, ctx.flash_bank2);
  EXPECT_EQ(hw.second_erase_num_pages, 1u);
}

}  // namespace
