#pragma once

#include <stdint.h>

#include "bld_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * STM32L4 internal flash storage context.
 *
 * Each context describes one flash partition.
 */
struct bld_storage_stm32l4_ctx {
	uint32_t region_base;
	uint32_t region_size;
	uint32_t page_size;
};

int bld_storage_stm32l4_init(struct bld_storage *storage,
			     const struct bld_storage_stm32l4_ctx *ctx);

#ifdef __cplusplus
}
#endif