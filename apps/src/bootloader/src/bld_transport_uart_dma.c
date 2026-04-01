#include "bld_transport_uart_dma.h"
#include "bld_protocol.h"

#include <string.h>

#define BLD_TRANSPORT_OK 0
#define BLD_TRANSPORT_ERR (-1)

#define BLD_UART_TX_TIMEOUT_MS 2000u

#define BLD_FRAME_SOF_SIZE 1u
#define BLD_FRAME_TYPE_SIZE 1u
#define BLD_FRAME_LEN_SIZE 2u
#define BLD_FRAME_HEADER_SIZE \
	(BLD_FRAME_SOF_SIZE + BLD_FRAME_TYPE_SIZE + BLD_FRAME_LEN_SIZE)
#define BLD_FRAME_CRC32_SIZE 4u
#define BLD_FRAME_EOF_SIZE 1u
#define BLD_FRAME_FIXED_OVERHEAD \
	(BLD_FRAME_HEADER_SIZE + BLD_FRAME_CRC32_SIZE + BLD_FRAME_EOF_SIZE)

static inline uint32_t ring_count(const struct bld_uart_dma_ctx *ctx)
{
	return (ctx == NULL) ? 0u : (uint32_t)(ctx->w - ctx->r);
}

static inline uint32_t ring_free(const struct bld_uart_dma_ctx *ctx)
{
	return (ctx == NULL) ? 0u :
			       ((uint32_t)BLD_UART_RING_SIZE - ring_count(ctx));
}

static int ring_push(struct bld_uart_dma_ctx *ctx, const uint8_t *data,
		     uint32_t len)
{
	if (ctx == NULL || data == NULL) {
		return BLD_TRANSPORT_ERR;
	}

	if (len == 0u) {
		return BLD_TRANSPORT_OK;
	}

	if (len > ring_free(ctx)) {
		return BLD_TRANSPORT_ERR;
	}

	for (uint32_t i = 0u; i < len; ++i) {
		ctx->ring[ctx->w % BLD_UART_RING_SIZE] = data[i];
		ctx->w++;
	}

	return BLD_TRANSPORT_OK;
}

static int ring_peek(const struct bld_uart_dma_ctx *ctx, uint32_t offset,
		     uint8_t *out)
{
	if (ctx == NULL || out == NULL) {
		return BLD_TRANSPORT_ERR;
	}

	if (offset >= ring_count(ctx)) {
		return BLD_TRANSPORT_ERR;
	}

	*out = ctx->ring[(ctx->r + offset) % BLD_UART_RING_SIZE];
	return BLD_TRANSPORT_OK;
}

static int ring_read(struct bld_uart_dma_ctx *ctx, uint8_t *out, uint32_t len)
{
	if (ctx == NULL || out == NULL) {
		return BLD_TRANSPORT_ERR;
	}

	if (len > ring_count(ctx)) {
		return BLD_TRANSPORT_ERR;
	}

	for (uint32_t i = 0u; i < len; ++i) {
		out[i] = ctx->ring[ctx->r % BLD_UART_RING_SIZE];
		ctx->r++;
	}

	return BLD_TRANSPORT_OK;
}

static void ring_drop(struct bld_uart_dma_ctx *ctx, uint32_t len)
{
	uint32_t available;

	if (ctx == NULL || len == 0u) {
		return;
	}

	available = ring_count(ctx);
	if (len > available) {
		len = available;
	}
	ctx->r += len;
}

void bld_uart_dma_start(struct bld_uart_dma_ctx *ctx)
{
	if (ctx == NULL || ctx->uart == NULL || ctx->ops == NULL ||
	    ctx->ops->rx_start == NULL) {
		return;
	}

	(void)ctx->ops->rx_start(ctx->uart, ctx->dma_rx, BLD_UART_DMA_RX_CHUNK);

	if (ctx->dma_rx_handle != NULL && ctx->ops->disable_dma_it != NULL) {
		ctx->ops->disable_dma_it(ctx->dma_rx_handle);
	}
}

void bld_uart_dma_on_rx_event(struct bld_uart_dma_ctx *ctx, uint16_t size)
{
	if (ctx == NULL || ctx->uart == NULL || ctx->ops == NULL ||
	    ctx->ops->rx_start == NULL) {
		return;
	}

	if (size > BLD_UART_DMA_RX_CHUNK) {
		size = BLD_UART_DMA_RX_CHUNK;
	}

	if (size > 0u) {
		(void)ring_push(ctx, ctx->dma_rx, size);
	}

	(void)ctx->ops->rx_start(ctx->uart, ctx->dma_rx, BLD_UART_DMA_RX_CHUNK);

	if (ctx->dma_rx_handle != NULL && ctx->ops->disable_dma_it != NULL) {
		ctx->ops->disable_dma_it(ctx->dma_rx_handle);
	}
}

void bld_uart_dma_on_error(struct bld_uart_dma_ctx *ctx)
{
	if (ctx == NULL || ctx->uart == NULL || ctx->ops == NULL ||
	    ctx->ops->rx_start == NULL) {
		return;
	}

	(void)ctx->ops->rx_start(ctx->uart, ctx->dma_rx, BLD_UART_DMA_RX_CHUNK);

	if (ctx->dma_rx_handle != NULL && ctx->ops->disable_dma_it != NULL) {
		ctx->ops->disable_dma_it(ctx->dma_rx_handle);
	}
}

static uint32_t uart_dma_now_ms(void *ctx)
{
	struct bld_uart_dma_ctx *uart_ctx = (struct bld_uart_dma_ctx *)ctx;

	if (uart_ctx == NULL || uart_ctx->ops == NULL ||
	    uart_ctx->ops->now_ms == NULL) {
		return 0u;
	}

	return uart_ctx->ops->now_ms(uart_ctx->time_ctx);
}

static int uart_dma_send_bytes(uint8_t *buf, uint16_t len, void *ctx)
{
	struct bld_uart_dma_ctx *uart_ctx = (struct bld_uart_dma_ctx *)ctx;

	if (uart_ctx == NULL || uart_ctx->uart == NULL ||
	    uart_ctx->ops == NULL || uart_ctx->ops->tx_blocking == NULL ||
	    buf == NULL || len == 0u) {
		return BLD_TRANSPORT_ERR;
	}

	return (uart_ctx->ops->tx_blocking(uart_ctx->uart, buf, len,
					   BLD_UART_TX_TIMEOUT_MS) == 0) ?
		       BLD_TRANSPORT_OK :
		       BLD_TRANSPORT_ERR;
}

/*
 * Parses one complete protocol frame from the software ring buffer.
 *
 * Frame format:
 *   SOF | TYPE | LEN | PAYLOAD | CRC32 | EOF
 *
 * LEN is the payload length in bytes. CRC validation is deferred to the
 * bootloader engine; this parser validates only framing and buffer bounds.
 *
 * Returns -
 *   > 0 : complete frame length in bytes
 *   = 0 : no frame available / timeout
 *   < 0 : transport/parser error
 */
static int uart_dma_parse_frame(uint8_t *out, uint16_t max_len,
				uint32_t timeout_ms, void *ctx)
{
	struct bld_uart_dma_ctx *uart_ctx = (struct bld_uart_dma_ctx *)ctx;
	uint32_t start_ms;

	if (uart_ctx == NULL || out == NULL ||
	    max_len < BLD_FRAME_FIXED_OVERHEAD || uart_ctx->ops == NULL ||
	    uart_ctx->ops->now_ms == NULL) {
		return -1;
	}

	start_ms = uart_ctx->ops->now_ms(uart_ctx->time_ctx);
	while ((uart_ctx->ops->now_ms(uart_ctx->time_ctx) - start_ms) <=
	       timeout_ms) {
		uint8_t sof;
		uint8_t len_lo;
		uint8_t len_hi;
		uint8_t eof;
		uint16_t payload_len;
		uint32_t total_len;

		if (ring_count(uart_ctx) < BLD_FRAME_HEADER_SIZE) {
			continue;
		}

		if (ring_peek(uart_ctx, 0u, &sof) != BLD_TRANSPORT_OK) {
			continue;
		}

		if (sof != BLD_SOF) {
			ring_drop(uart_ctx, 1u);
			continue;
		}

		if (ring_peek(uart_ctx, 2u, &len_lo) != BLD_TRANSPORT_OK) {
			continue;
		}
		if (ring_peek(uart_ctx, 3u, &len_hi) != BLD_TRANSPORT_OK) {
			continue;
		}

		payload_len = (uint16_t)len_lo | ((uint16_t)len_hi << 8);
		total_len = (uint32_t)payload_len + BLD_FRAME_FIXED_OVERHEAD;

		if (total_len > max_len) {
			ring_drop(uart_ctx, 1u);
			continue;
		}

		if (ring_count(uart_ctx) < total_len) {
			continue;
		}

		if (ring_peek(uart_ctx, total_len - 1u, &eof) !=
		    BLD_TRANSPORT_OK) {
			continue;
		}

		if (eof != BLD_EOF) {
			ring_drop(uart_ctx, 1u);
			continue;
		}

		if (ring_read(uart_ctx, out, total_len) != BLD_TRANSPORT_OK) {
			return -1;
		}
		return (uint16_t)total_len;
	}
	/* Timeout or no frame */
	return 0;
}

struct bld_transport bld_transport_uart_dma_make(struct bld_uart_dma_ctx *ctx)
{
	struct bld_transport transport;

	transport.parse = uart_dma_parse_frame;
	transport.send = uart_dma_send_bytes;
	transport.now_ms = uart_dma_now_ms;
	transport.ctx = ctx;
	return transport;
}