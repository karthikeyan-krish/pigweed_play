#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

extern "C" {
#include "bld_boot.h"
#include "bld_crc32.h"
#include "bld_meta.h"
#include "bld_protocol.h"
#include "bld_storage.h"
#include "bld_storage_flash.h"
#include "bld_transport.h"
#include "bld_transport_uart_dma.h"
}

// Minimal HAL/CMSIS-style types and constants used by tests.
extern "C" {

typedef int HAL_StatusTypeDef;
static constexpr HAL_StatusTypeDef HAL_OK = 0;
static constexpr HAL_StatusTypeDef HAL_ERROR = 1;

struct DMA_HandleTypeDef {
  int dummy;
};

struct UART_HandleTypeDef {
  DMA_HandleTypeDef* hdmarx;
};

void bld_jump_to_image(uint32_t image_base);
}

// Match names used by the production code.
#define DMA_IT_HT 0x01u
#define __HAL_DMA_DISABLE_IT(hdma, mask) test_dma_disable_it((hdma), (mask))

#define FLASH_TYPEPROGRAM_DOUBLEWORD 0u
#define FLASH_TYPEERASE_PAGES 0u
#define FLASH_BANK_1 1u
#define FLASH_BASE 0x08000000u
#define FLASH_PAGE_SIZE 2048u

struct FLASH_EraseInitTypeDef {
  uint32_t TypeErase;
  uint32_t Banks;
  uint32_t Page;
  uint32_t NbPages;
};

namespace test {

struct FakeStorageCtx {
  std::vector<uint8_t> bytes;
  int erase_result = 0;
  int write_result = 0;
  int read_result = 0;
  int erase_calls = 0;
  int write_calls = 0;
  int read_calls = 0;
  uint32_t last_erase_offset = 0;
  uint32_t last_erase_size = 0;
  uint32_t last_write_offset = 0;
  uint32_t last_write_len = 0;
  uint32_t last_read_offset = 0;
  uint32_t last_read_len = 0;
};

int FakeStorageErase(const bld_storage* self, uint32_t offset, uint32_t size);
int FakeStorageWrite(const bld_storage* self,
                     uint32_t offset,
                     const uint8_t* data,
                     uint32_t len);
int FakeStorageRead(const bld_storage* self,
                    uint32_t offset,
                    uint8_t* out,
                    uint32_t len);

bld_storage MakeFakeStorage(FakeStorageCtx* ctx);

struct FakeTransportCtx {
  std::vector<uint8_t> next_frame;
  int parse_result_override = -9999;
  int parse_calls = 0;
  int send_calls = 0;
  uint32_t last_timeout_ms = 0;
  std::vector<uint8_t> last_sent;
};

int FakeParse(uint8_t* buf, uint16_t max_len, uint32_t timeout_ms, void* ctx);
int FakeSend(uint8_t* buf, uint16_t len, void* ctx);
uint32_t FakeNowMs(void* ctx);

bld_transport MakeFakeTransport(FakeTransportCtx* ctx);

uint32_t FrameCrc(const uint8_t* data, uint32_t size);
std::vector<uint8_t> MakeCmdFrame(uint8_t cmd);
std::vector<uint8_t> MakeHeaderFrame(uint32_t image_size,
                                     uint32_t image_crc32,
                                     uint32_t version);
std::vector<uint8_t> MakeDataFrame(uint32_t seq,
                                   const uint8_t* payload,
                                   uint16_t payload_len);

template <typename T>
T ReadStruct(const std::vector<uint8_t>& bytes, size_t offset = 0) {
  T out{};
  memcpy(&out, bytes.data() + offset, sizeof(T));
  return out;
}

extern uint32_t g_fake_tick;
extern int g_uart_receive_to_idle_calls;
extern int g_uart_transmit_calls;
extern int g_dma_disable_it_calls;
extern int g_flash_unlock_calls;
extern int g_flash_lock_calls;
extern int g_flash_program_calls;
extern int g_flash_erase_calls;
extern HAL_StatusTypeDef g_uart_receive_to_idle_result;
extern HAL_StatusTypeDef g_uart_transmit_result;
extern HAL_StatusTypeDef g_flash_unlock_result;
extern HAL_StatusTypeDef g_flash_lock_result;
extern HAL_StatusTypeDef g_flash_program_result;
extern HAL_StatusTypeDef g_flash_erase_result;
extern uint32_t g_last_jump_image_base;
extern uint32_t g_last_uart_tx_timeout;
extern uint16_t g_last_uart_tx_len;
extern std::vector<uint8_t> g_last_uart_tx_bytes;
extern uint64_t g_last_flash_program_data;
extern uint32_t g_last_flash_program_addr;

void ResetGlobalFakes();

}  // namespace test
