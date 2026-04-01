#pragma once

#include <stdint.h>

#include "bld_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Flash operations.
 */
struct bld_flash_ops {
	int (*unlock)(void *hw);
	int (*lock)(void *hw);
	int (*read)(void *hw, uint32_t addr, uint8_t *out, uint32_t len);
	int (*erase_pages)(void *hw, uint32_t bank, uint32_t first_page,
			   uint32_t num_pages);
	int (*program_doubleword)(void *hw, uint32_t addr, uint64_t data);
};

/*
 * STM32L4 internal flash storage context.
 *
 * Each context describes one flash partition.
 */
struct bld_storage_flash_ctx {
	uint32_t region_base;
	uint32_t region_size;
	uint32_t page_size;
	uint32_t flash_base;
	uint32_t flash_bank_size;
	uint32_t flash_page_size;
	uint32_t flash_bank1;
	uint32_t flash_bank2;
	const struct bld_flash_ops *ops;
	void *hw;
};

int bld_storage_flash_init(struct bld_storage *storage,
			   const struct bld_storage_flash_ctx *ctx);

#ifdef __cplusplus
}
#endif