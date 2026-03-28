#pragma once
#include "bld_transport.h"
#include "stm32l4xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * UART DMA receive chunk size in bytes.
 */
#ifndef BLD_UART_DMA_RX_CHUNK
#define BLD_UART_DMA_RX_CHUNK 256u
#endif

/*
 * Software ring buffer size in bytes.
 */
#ifndef BLD_UART_RING_SIZE
#define BLD_UART_RING_SIZE 4096u
#endif

/*
 * UART DMA transport context.
 *
 * The DMA engine receives data into dma_rx. Completed DMA chunks are copied
 * into the software ring buffer, which is then consumed by the transport
 * frame parser.
 */
struct bld_uart_dma_ctx {
	UART_HandleTypeDef *huart;
	uint8_t dma_rx[BLD_UART_DMA_RX_CHUNK];
	uint8_t ring[BLD_UART_RING_SIZE];
	volatile uint32_t w;
	volatile uint32_t r;
};

/*
 * Starts UART receive-to-idle DMA reception.
 */
void bld_uart_dma_start(struct bld_uart_dma_ctx *ctx);

/*
 * Handles a UART DMA receive event.
 *
 * This function must be called from the corresponding HAL receive callback.
 */
void bld_uart_dma_on_rx_event(struct bld_uart_dma_ctx *ctx, uint16_t size);

/*
 * Handles a UART/DMA error and restarts reception.
 */
void bld_uart_dma_on_error(struct bld_uart_dma_ctx *ctx);

/*
 * Creates a bootloader transport backed by UART DMA + software ring buffer.
 */
struct bld_transport bld_transport_uart_dma_make(struct bld_uart_dma_ctx *ctx);

#ifdef __cplusplus
}
#endif
