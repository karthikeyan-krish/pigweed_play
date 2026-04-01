#include "test_stubs.h"

#include <algorithm>
#include <cstring>

namespace test {

uint32_t g_fake_tick = 0;
int g_uart_receive_to_idle_calls = 0;
int g_uart_transmit_calls = 0;
int g_dma_disable_it_calls = 0;
int g_flash_unlock_calls = 0;
int g_flash_lock_calls = 0;
int g_flash_program_calls = 0;
int g_flash_erase_calls = 0;
HAL_StatusTypeDef g_uart_receive_to_idle_result = HAL_OK;
HAL_StatusTypeDef g_uart_transmit_result = HAL_OK;
HAL_StatusTypeDef g_flash_unlock_result = HAL_OK;
HAL_StatusTypeDef g_flash_lock_result = HAL_OK;
HAL_StatusTypeDef g_flash_program_result = HAL_OK;
HAL_StatusTypeDef g_flash_erase_result = HAL_OK;
uint32_t g_last_jump_image_base = 0;
uint32_t g_last_uart_tx_timeout = 0;
uint16_t g_last_uart_tx_len = 0;
std::vector<uint8_t> g_last_uart_tx_bytes;
uint64_t g_last_flash_program_data = 0;
uint32_t g_last_flash_program_addr = 0;

void ResetGlobalFakes() {
  g_fake_tick = 0;
  g_uart_receive_to_idle_calls = 0;
  g_uart_transmit_calls = 0;
  g_dma_disable_it_calls = 0;
  g_flash_unlock_calls = 0;
  g_flash_lock_calls = 0;
  g_flash_program_calls = 0;
  g_flash_erase_calls = 0;
  g_uart_receive_to_idle_result = HAL_OK;
  g_uart_transmit_result = HAL_OK;
  g_flash_unlock_result = HAL_OK;
  g_flash_lock_result = HAL_OK;
  g_flash_program_result = HAL_OK;
  g_flash_erase_result = HAL_OK;
  g_last_jump_image_base = 0;
  g_last_uart_tx_timeout = 0;
  g_last_uart_tx_len = 0;
  g_last_uart_tx_bytes.clear();
  g_last_flash_program_data = 0;
  g_last_flash_program_addr = 0;
}

int FakeStorageErase(const bld_storage* self, uint32_t offset, uint32_t size) {
  auto* ctx = static_cast<FakeStorageCtx*>(const_cast<void*>(self->ctx));
  ctx->erase_calls++;
  ctx->last_erase_offset = offset;
  ctx->last_erase_size = size;
  if (ctx->erase_result != 0) {
    return ctx->erase_result;
  }
  if (offset + size > ctx->bytes.size()) {
    return -1;
  }
  std::fill(
      ctx->bytes.begin() + offset, ctx->bytes.begin() + offset + size, 0xFF);
  return 0;
}

int FakeStorageWrite(const bld_storage* self,
                     uint32_t offset,
                     const uint8_t* data,
                     uint32_t len) {
  auto* ctx = static_cast<FakeStorageCtx*>(const_cast<void*>(self->ctx));
  ctx->write_calls++;
  ctx->last_write_offset = offset;
  ctx->last_write_len = len;
  if (ctx->write_result != 0) {
    return ctx->write_result;
  }
  if (offset + len > ctx->bytes.size()) {
    return -1;
  }
  memcpy(ctx->bytes.data() + offset, data, len);
  return 0;
}

int FakeStorageRead(const bld_storage* self,
                    uint32_t offset,
                    uint8_t* out,
                    uint32_t len) {
  auto* ctx = static_cast<FakeStorageCtx*>(const_cast<void*>(self->ctx));
  ctx->read_calls++;
  ctx->last_read_offset = offset;
  ctx->last_read_len = len;
  if (ctx->read_result != 0) {
    return ctx->read_result;
  }
  if (offset + len > ctx->bytes.size()) {
    return -1;
  }
  memcpy(out, ctx->bytes.data() + offset, len);
  return 0;
}

struct bld_storage MakeFakeStorage(FakeStorageCtx* ctx) {
  bld_storage storage{};
  storage.erase = FakeStorageErase;
  storage.write = FakeStorageWrite;
  storage.read = FakeStorageRead;
  storage.ctx = ctx;
  return storage;
}

int FakeParse(uint8_t* buf, uint16_t max_len, uint32_t timeout_ms, void* ctx) {
  auto* fctx = static_cast<FakeTransportCtx*>(ctx);
  fctx->parse_calls++;
  fctx->last_timeout_ms = timeout_ms;
  if (fctx->parse_result_override != -9999) {
    return fctx->parse_result_override;
  }
  if (fctx->next_frame.empty()) {
    return 0;
  }
  if (fctx->next_frame.size() > max_len) {
    return -1;
  }
  memcpy(buf, fctx->next_frame.data(), fctx->next_frame.size());
  return static_cast<int>(fctx->next_frame.size());
}

int FakeSend(uint8_t* buf, uint16_t len, void* ctx) {
  auto* fctx = static_cast<FakeTransportCtx*>(ctx);
  fctx->send_calls++;
  fctx->last_sent.assign(buf, buf + len);
  return 0;
}

uint32_t FakeNowMs(void* ctx) {
  (void)ctx;
  return g_fake_tick;
}

bld_transport MakeFakeTransport(FakeTransportCtx* ctx) {
  bld_transport t{};
  t.parse = FakeParse;
  t.send = FakeSend;
  t.now_ms = FakeNowMs;
  t.ctx = ctx;
  return t;
}

uint32_t FrameCrc(const uint8_t* data, uint32_t size) {
  return bld_crc32_ieee(data, size, BLD_CRC32_INITIAL);
}

std::vector<uint8_t> MakeCmdFrame(uint8_t cmd) {
  bld_cmd_frame frame{};
  frame.sof = BLD_SOF;
  frame.type = BLD_PKT_CMD;
  frame.len = 4;
  frame.cmd = cmd;
  frame.crc32 =
      FrameCrc(reinterpret_cast<const uint8_t*>(&frame),
               sizeof(frame) - sizeof(frame.crc32) - sizeof(frame.eof));
  frame.eof = BLD_EOF;
  std::vector<uint8_t> out(sizeof(frame));
  memcpy(out.data(), &frame, sizeof(frame));
  return out;
}

std::vector<uint8_t> MakeHeaderFrame(uint32_t image_size,
                                     uint32_t image_crc32,
                                     uint32_t version) {
  bld_header_frame frame{};
  frame.sof = BLD_SOF;
  frame.type = BLD_PKT_HEADER;
  frame.len = 12;
  frame.image_size = image_size;
  frame.image_crc32 = image_crc32;
  frame.version = version;
  frame.crc32 =
      FrameCrc(reinterpret_cast<const uint8_t*>(&frame),
               sizeof(frame) - sizeof(frame.crc32) - sizeof(frame.eof));
  frame.eof = BLD_EOF;
  std::vector<uint8_t> out(sizeof(frame));
  memcpy(out.data(), &frame, sizeof(frame));
  return out;
}

std::vector<uint8_t> MakeDataFrame(uint32_t seq,
                                   const uint8_t* payload,
                                   uint16_t payload_len) {
  const uint16_t len_field = static_cast<uint16_t>(6 + payload_len);
  const size_t total = 1 + 1 + 2 + 4 + 2 + payload_len + 4 + 1;
  std::vector<uint8_t> out(total, 0);
  out[0] = BLD_SOF;
  out[1] = BLD_PKT_DATA;
  memcpy(out.data() + 2, &len_field, sizeof(len_field));
  memcpy(out.data() + 4, &seq, sizeof(seq));
  memcpy(out.data() + 8, &payload_len, sizeof(payload_len));
  memcpy(out.data() + 10, payload, payload_len);
  const uint32_t crc = FrameCrc(out.data(), static_cast<uint32_t>(total - 5));
  memcpy(out.data() + total - 5, &crc, sizeof(crc));
  out[total - 1] = BLD_EOF;
  return out;
}

}  // namespace test

extern "C" {

void bld_jump_to_image(uint32_t image_base) {
  test::g_last_jump_image_base = image_base;
}

}  // extern "C"
