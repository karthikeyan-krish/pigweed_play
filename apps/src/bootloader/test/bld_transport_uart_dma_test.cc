#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "test_stubs.h"

extern "C" {
#include "bld_protocol.h"
#include "bld_transport_uart_dma.h"
}

namespace {

uint32_t g_now_ms = 0;
int g_rx_start_calls = 0;
int g_tx_calls = 0;
int g_disable_dma_calls = 0;
int g_rx_start_result = 0;
int g_tx_result = 0;
uint32_t g_last_tx_timeout = 0;
uint16_t g_last_tx_len = 0;
std::vector<uint8_t> g_last_tx_bytes;

int FakeUartRxStart(void* uart, uint8_t* buf, uint16_t len) {
  (void)uart;
  (void)buf;
  (void)len;
  g_rx_start_calls++;
  return g_rx_start_result;
}

int FakeUartTxBlocking(void* uart,
                       uint8_t* buf,
                       uint16_t len,
                       uint32_t timeout_ms) {
  (void)uart;
  g_tx_calls++;
  g_last_tx_timeout = timeout_ms;
  g_last_tx_len = len;
  g_last_tx_bytes.assign(buf, buf + len);
  return g_tx_result;
}

void FakeDmaDisableIt(void* dma_rx) {
  (void)dma_rx;
  g_disable_dma_calls++;
}

uint32_t FakeNowMs(void* time_ctx) {
  (void)time_ctx;
  return g_now_ms++;
}

const bld_uart_dma_ll_ops kUartOps = {
    .rx_start = FakeUartRxStart,
    .tx_blocking = FakeUartTxBlocking,
    .disable_dma_it = FakeDmaDisableIt,
    .now_ms = FakeNowMs,
};

class UartDmaTransportTest : public ::testing::Test {
 protected:
  void SetUp() override {
    g_now_ms = 0;
    g_rx_start_calls = 0;
    g_tx_calls = 0;
    g_disable_dma_calls = 0;
    g_rx_start_result = 0;
    g_tx_result = 0;
    g_last_tx_timeout = 0;
    g_last_tx_len = 0;
    g_last_tx_bytes.clear();

    ctx.uart = &fake_uart;
    ctx.dma_rx_handle = &fake_dma;
    ctx.time_ctx = nullptr;
    ctx.ops = &kUartOps;
    ctx.w = 0;
    ctx.r = 0;
    std::memset(ctx.dma_rx, 0, sizeof(ctx.dma_rx));
    std::memset(ctx.ring, 0, sizeof(ctx.ring));
  }

  int fake_uart = 0;
  int fake_dma = 0;
  bld_uart_dma_ctx ctx{};
};

TEST_F(UartDmaTransportTest, MakeReturnsExpectedCallbacksAndContext) {
  const bld_transport transport = bld_transport_uart_dma_make(&ctx);

  EXPECT_TRUE(transport.parse != nullptr);
  EXPECT_TRUE(transport.send != nullptr);
  EXPECT_TRUE(transport.now_ms != nullptr);
  EXPECT_EQ(transport.ctx, &ctx);
}

TEST_F(UartDmaTransportTest,
       StartBeginsReceiveAndDisablesHalfTransferInterrupt) {
  bld_uart_dma_start(&ctx);

  EXPECT_EQ(g_rx_start_calls, 1);
  EXPECT_EQ(g_disable_dma_calls, 1);
}

TEST_F(UartDmaTransportTest, RxEventPushesBytesIntoRingAndRestartsReceive) {
  ctx.dma_rx[0] = 0x11;
  ctx.dma_rx[1] = 0x22;
  ctx.dma_rx[2] = 0x33;

  bld_uart_dma_on_rx_event(&ctx, 3);

  EXPECT_EQ(ctx.w, 3u);
  EXPECT_EQ(ctx.r, 0u);
  EXPECT_EQ(ctx.ring[0], 0x11u);
  EXPECT_EQ(ctx.ring[1], 0x22u);
  EXPECT_EQ(ctx.ring[2], 0x33u);
  EXPECT_EQ(g_rx_start_calls, 1);
  EXPECT_EQ(g_disable_dma_calls, 1);
}

TEST_F(UartDmaTransportTest, RxEventClampsOversizedSizeToChunkLimit) {
  for (uint32_t i = 0; i < BLD_UART_DMA_RX_CHUNK; ++i) {
    ctx.dma_rx[i] = static_cast<uint8_t>(i & 0xFFu);
  }

  bld_uart_dma_on_rx_event(&ctx,
                           static_cast<uint16_t>(BLD_UART_DMA_RX_CHUNK + 10u));

  EXPECT_EQ(ctx.w, static_cast<uint32_t>(BLD_UART_DMA_RX_CHUNK));
  EXPECT_EQ(ctx.r, 0u);
  EXPECT_EQ(g_rx_start_calls, 1);
  EXPECT_EQ(g_disable_dma_calls, 1);
}

TEST_F(UartDmaTransportTest, OnErrorRestartsReceive) {
  bld_uart_dma_on_error(&ctx);

  EXPECT_EQ(g_rx_start_calls, 1);
  EXPECT_EQ(g_disable_dma_calls, 1);
}

TEST_F(UartDmaTransportTest, ParseExtractsFrameFromRing) {
  const auto frame = test::MakeCmdFrame(BLD_CMD_QUERY);
  std::memcpy(ctx.ring, frame.data(), frame.size());
  ctx.w = static_cast<uint32_t>(frame.size());

  const bld_transport transport = bld_transport_uart_dma_make(&ctx);
  uint8_t out[64] = {};

  const int len = transport.parse(out, sizeof(out), 10, transport.ctx);

  ASSERT_EQ(len, static_cast<int>(frame.size()));
  EXPECT_EQ(std::memcmp(out, frame.data(), frame.size()), 0);
  EXPECT_EQ(ctx.r, static_cast<uint32_t>(frame.size()));
}

TEST_F(UartDmaTransportTest, ParseSkipsGarbageBeforeSof) {
  const auto frame = test::MakeCmdFrame(BLD_CMD_QUERY);

  ctx.ring[0] = 0x00;
  ctx.ring[1] = 0x11;
  ctx.ring[2] = 0x22;
  std::memcpy(&ctx.ring[3], frame.data(), frame.size());
  ctx.w = static_cast<uint32_t>(3 + frame.size());

  const bld_transport transport = bld_transport_uart_dma_make(&ctx);
  uint8_t out[64] = {};

  const int len = transport.parse(out, sizeof(out), 10, transport.ctx);

  ASSERT_EQ(len, static_cast<int>(frame.size()));
  EXPECT_EQ(std::memcmp(out, frame.data(), frame.size()), 0);
  EXPECT_EQ(ctx.r, static_cast<uint32_t>(3 + frame.size()));
}

TEST_F(UartDmaTransportTest, ParseRejectsOversizeFrame) {
  uint8_t bad[8] = {};
  bad[0] = BLD_SOF;
  bad[1] = BLD_PKT_CMD;

  const uint16_t huge_len = 1000u;
  std::memcpy(&bad[2], &huge_len, sizeof(huge_len));

  std::memcpy(ctx.ring, bad, sizeof(bad));
  ctx.w = static_cast<uint32_t>(sizeof(bad));

  const bld_transport transport = bld_transport_uart_dma_make(&ctx);
  uint8_t out[16] = {};

  const int len = transport.parse(out, sizeof(out), 10, transport.ctx);

  EXPECT_EQ(len, 0);
  EXPECT_EQ(ctx.r, 5u);
}

TEST_F(UartDmaTransportTest, ParseReturnsZeroOnTimeoutWhenNoFrameAvailable) {
  const bld_transport transport = bld_transport_uart_dma_make(&ctx);
  uint8_t out[64] = {};

  const int len = transport.parse(out, sizeof(out), 0, transport.ctx);

  EXPECT_EQ(len, 0);
}

TEST_F(UartDmaTransportTest, ParseReturnsErrorWhenNowMsOpIsMissing) {
  bld_uart_dma_ll_ops ops = kUartOps;
  ops.now_ms = nullptr;
  ctx.ops = &ops;

  const bld_transport transport = bld_transport_uart_dma_make(&ctx);
  uint8_t out[64] = {};

  EXPECT_LT(transport.parse(out, sizeof(out), 0, transport.ctx), 0);
}

TEST_F(UartDmaTransportTest, SendUsesBlockingTxOp) {
  const bld_transport transport = bld_transport_uart_dma_make(&ctx);
  uint8_t data[3] = {0xAA, 0xBB, 0xCC};

  ASSERT_EQ(transport.send(data, sizeof(data), transport.ctx), 0);
  EXPECT_EQ(g_tx_calls, 1);
  EXPECT_EQ(g_last_tx_len, 3u);
  ASSERT_EQ(g_last_tx_bytes.size(), 3u);
  EXPECT_EQ(g_last_tx_bytes[0], 0xAAu);
  EXPECT_EQ(g_last_tx_bytes[1], 0xBBu);
  EXPECT_EQ(g_last_tx_bytes[2], 0xCCu);
}

}  // namespace
