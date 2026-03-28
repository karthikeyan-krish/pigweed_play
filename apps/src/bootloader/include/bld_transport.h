#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Bootloader transport interface.
 *
 * This abstraction allows the bootloader engine to communicate over
 * different physical links (UART, CAN, USB, etc.) without knowing
 * transport-specific details.
 */
struct bld_transport {
	int (*parse)(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms,
		     void *ctx);
	int (*send)(uint8_t *buf, uint16_t len, void *ctx);
	uint32_t (*now_ms)(void *ctx);
	void *ctx;
};

#ifdef __cplusplus
}
#endif