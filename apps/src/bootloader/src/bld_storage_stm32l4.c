#include "bld_storage_stm32l4.h"

#include "stm32l4xx_hal.h"
#include "stm32l4xx_hal_flash.h"
#include "stm32l4xx_hal_flash_ex.h"

#include <string.h>
#include <stdbool.h>

#define BLD_STORAGE_OK 0
#define BLD_STORAGE_ERR -1

#define BLD_STM32L4_FLASH_ERASED_BYTE 0xFFu
#define BLD_STM32L4_DOUBLEWORD_SIZE 8u
#define BLD_STM32L4_DOUBLEWORD_ALIGN_MASK (BLD_STM32L4_DOUBLEWORD_SIZE - 1u)

static inline uint32_t align_up(uint32_t size, uint32_t align)
{
	return ((size + align - 1u) / align) * align;
}

static bool range_valid(const struct bld_storage_stm32l4_ctx *ctx,
			uint32_t offset, uint32_t len)
{
	if (ctx == NULL) {
		return false;
	}
	if (offset > ctx->region_size) {
		return false;
	}
	if (len > (ctx->region_size - offset)) {
		return false;
	}
	return true;
}

static uint32_t abs_addr(const struct bld_storage_stm32l4_ctx *ctx,
			 uint32_t offset)
{
	return ctx->region_base + offset;
}

static uint32_t flash_get_bank(uint32_t addr)
{
#if defined(FLASH_BANK_2)
	if (addr < (FLASH_BASE + FLASH_BANK_SIZE))
		return FLASH_BANK_1;
	return FLASH_BANK_2;
#else
	(void)addr;
	return FLASH_BANK_1;
#endif
}

static uint32_t flash_get_page_in_bank(uint32_t addr)
{
	uint32_t bank = flash_get_bank(addr);
	uint32_t bank_base = (bank == FLASH_BANK_1) ?
				     FLASH_BASE :
				     (FLASH_BASE + FLASH_BANK_SIZE);
	return (addr - bank_base) / FLASH_PAGE_SIZE;
}

static int stm32l4_erase(const struct bld_storage *self, uint32_t offset,
			 uint32_t size)
{
	if (self == NULL || self->ctx == NULL || size == 0u) {
		return BLD_STORAGE_ERR;
	}

	const struct bld_storage_stm32l4_ctx *ctx =
		(const struct bld_storage_stm32l4_ctx *)self->ctx;

	if (!range_valid(ctx, offset, size)) {
		return BLD_STORAGE_ERR;
	}

	uint32_t aligned_size = align_up(size, ctx->page_size);

	uint32_t addr = abs_addr(ctx, offset);

	if ((addr % ctx->page_size) != 0u ||
	    (aligned_size % ctx->page_size) != 0u) {
		return BLD_STORAGE_ERR;
	}

	uint32_t end = addr + aligned_size - 1u;
	uint32_t start_bank = flash_get_bank(addr);
	uint32_t end_bank = flash_get_bank(end);

	HAL_StatusTypeDef st;
	FLASH_EraseInitTypeDef erase = { 0 };
	uint32_t page_error = 0;

	st = HAL_FLASH_Unlock();
	if (st != HAL_OK) {
		return BLD_STORAGE_ERR;
	}

	erase.TypeErase = FLASH_TYPEERASE_PAGES;

	if (start_bank == end_bank) {
		uint32_t first = flash_get_page_in_bank(addr);
		uint32_t last = flash_get_page_in_bank(end);
		erase.Banks = start_bank;
		erase.Page = first;
		erase.NbPages = (last - first) + 1u;
		st = HAL_FLASHEx_Erase(&erase, &page_error);
	} else {
#if defined(FLASH_BANK_2)
		uint32_t bank1_end = (FLASH_BASE + FLASH_BANK_SIZE) - 1u;
		uint32_t first = flash_get_page_in_bank(addr);
		uint32_t last = flash_get_page_in_bank(bank1_end);
		erase.Banks = FLASH_BANK_1;
		erase.Page = first;
		erase.NbPages = (last - first) + 1u;
		st = HAL_FLASHEx_Erase(&erase, &page_error);

		if (st == HAL_OK) {
			uint32_t bank2_base = FLASH_BASE + FLASH_BANK_SIZE;
			first = flash_get_page_in_bank(bank2_base);
			last = flash_get_page_in_bank(end);
			erase.Banks = FLASH_BANK_2;
			erase.Page = first;
			erase.NbPages = (last - first) + 1u;
			st = HAL_FLASHEx_Erase(&erase, &page_error);
		}
#else
		st = HAL_ERROR;
#endif
	}

	(void)HAL_FLASH_Lock();
	return (st == HAL_OK) ? BLD_STORAGE_OK : BLD_STORAGE_ERR;
}

static int stm32l4_write(const struct bld_storage *self, uint32_t offset,
			 const uint8_t *data, uint32_t len)
{
	if (self == NULL || self->ctx == NULL || data == NULL || len == 0) {
		return BLD_STORAGE_ERR;
	}

	const struct bld_storage_stm32l4_ctx *ctx =
		(const struct bld_storage_stm32l4_ctx *)self->ctx;

	if (!range_valid(ctx, offset, len)) {
		return BLD_STORAGE_ERR;
	}

	uint32_t addr = abs_addr(ctx, offset);

	HAL_StatusTypeDef st = HAL_FLASH_Unlock();
	if (st != HAL_OK)
		return BLD_STORAGE_ERR;

	uint32_t i = 0;
	while (i < len) {
		// program as 64-bit doubleword; pad with 0xFF
		uint8_t chunk[BLD_STM32L4_DOUBLEWORD_SIZE];
		memset(chunk, BLD_STM32L4_FLASH_ERASED_BYTE, sizeof(chunk));
		uint32_t remaining = len - i;
		uint32_t cpy = (remaining >= 8u) ? 8u : remaining;
		memcpy(chunk, &data[i], cpy);

		if ((addr & BLD_STM32L4_DOUBLEWORD_ALIGN_MASK) != 0u) {
			(void)HAL_FLASH_Lock();
			return BLD_STORAGE_ERR;
		}

		uint64_t doubleword = 0u;
		memcpy(&doubleword, chunk, sizeof(doubleword));
		st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr,
				       doubleword);
		if (st != HAL_OK) {
			(void)HAL_FLASH_Lock();
			return BLD_STORAGE_ERR;
		}

		addr += 8u;
		i += cpy;
	}

	(void)HAL_FLASH_Lock();
	return BLD_STORAGE_OK;
}

static int stm32l4_read(const struct bld_storage *self, uint32_t offset,
			uint8_t *out, uint32_t len)
{
	if (self == NULL || self->ctx == NULL || out == NULL || len == 0) {
		return BLD_STORAGE_ERR;
	}

	const struct bld_storage_stm32l4_ctx *ctx =
		(const struct bld_storage_stm32l4_ctx *)self->ctx;

	if (!range_valid(ctx, offset, len)) {
		return BLD_STORAGE_ERR;
	}

	memcpy(out, (const void *)abs_addr(ctx, offset), len);
	return BLD_STORAGE_OK;
}

int bld_storage_stm32l4_init(struct bld_storage *storage,
			     const struct bld_storage_stm32l4_ctx *ctx)
{
	if (storage == NULL || ctx == NULL) {
		return BLD_STORAGE_ERR;
	}

	storage->erase = stm32l4_erase, storage->write = stm32l4_write,
	storage->read = stm32l4_read, storage->ctx = ctx;
	return BLD_STORAGE_OK;
}