#include "bld_storage_flash.h"

#include <string.h>
#include <stdbool.h>
#include <stddef.h>

#define BLD_STORAGE_OK 0
#define BLD_STORAGE_ERR (-1)

#define BLD_STM32L4_FLASH_ERASED_BYTE 0xFFu
#define BLD_STM32L4_DOUBLEWORD_SIZE 8u
#define BLD_STM32L4_DOUBLEWORD_ALIGN_MASK (BLD_STM32L4_DOUBLEWORD_SIZE - 1u)

static inline uint32_t align_up(uint32_t size, uint32_t align)
{
	return ((size + align - 1u) / align) * align;
}

static bool range_valid(const struct bld_storage_flash_ctx *ctx,
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

static uint32_t abs_addr(const struct bld_storage_flash_ctx *ctx,
			 uint32_t offset)
{
	return ctx->region_base + offset;
}

static uint32_t flash_get_bank(const struct bld_storage_flash_ctx *ctx,
			       uint32_t addr)
{
	if (ctx->flash_bank2 == 0u || ctx->flash_bank_size == 0u) {
		return ctx->flash_bank1;
	}

	if (addr < (ctx->flash_base + ctx->flash_bank_size)) {
		return ctx->flash_bank1;
	}
	return ctx->flash_bank2;
}

static uint32_t flash_get_page_in_bank(const struct bld_storage_flash_ctx *ctx,
				       uint32_t addr)
{
	uint32_t bank = flash_get_bank(ctx, addr);
	uint32_t bank_base = (bank == ctx->flash_bank1) ?
				     ctx->flash_base :
				     (ctx->flash_base + ctx->flash_bank_size);
	return (addr - bank_base) / ctx->flash_page_size;
}

static int stm32l4_erase(const struct bld_storage *self, uint32_t offset,
			 uint32_t size)
{
	const struct bld_storage_flash_ctx *ctx;
	uint32_t aligned_size;
	uint32_t addr;
	uint32_t end;
	uint32_t start_bank;
	uint32_t end_bank;

	if (self == NULL || self->ctx == NULL || size == 0u) {
		return BLD_STORAGE_ERR;
	}

	ctx = (const struct bld_storage_flash_ctx *)self->ctx;

	if (ctx->ops == NULL || ctx->ops->unlock == NULL ||
	    ctx->ops->lock == NULL || ctx->ops->erase_pages == NULL) {
		return BLD_STORAGE_ERR;
	}

	if (!range_valid(ctx, offset, size)) {
		return BLD_STORAGE_ERR;
	}

	aligned_size = align_up(size, ctx->page_size);
	addr = abs_addr(ctx, offset);

	if ((addr % ctx->page_size) != 0u ||
	    (aligned_size % ctx->page_size) != 0u) {
		return BLD_STORAGE_ERR;
	}

	end = addr + aligned_size - 1u;
	start_bank = flash_get_bank(ctx, addr);
	end_bank = flash_get_bank(ctx, end);

	if (ctx->ops->unlock(ctx->hw) != 0) {
		return BLD_STORAGE_ERR;
	}

	if (start_bank == end_bank) {
		uint32_t first = flash_get_page_in_bank(ctx, addr);
		uint32_t last = flash_get_page_in_bank(ctx, end);
		if (ctx->ops->erase_pages(ctx->hw, start_bank, first,
					  (last - first) + 1u) != 0) {
			(void)ctx->ops->lock(ctx->hw);
			return BLD_STORAGE_ERR;
		}
	} else {
		uint32_t bank1_end;
		uint32_t first;
		uint32_t last;
		uint32_t bank2_base;

		if (ctx->flash_bank2 == 0u || ctx->flash_bank_size == 0u) {
			(void)ctx->ops->lock(ctx->hw);
			return BLD_STORAGE_ERR;
		}

		bank1_end = (ctx->flash_base + ctx->flash_bank_size) - 1u;

		first = flash_get_page_in_bank(ctx, addr);
		last = flash_get_page_in_bank(ctx, bank1_end);
		if (ctx->ops->erase_pages(ctx->hw, ctx->flash_bank1, first,
					  (last - first) + 1u) != 0) {
			(void)ctx->ops->lock(ctx->hw);
			return BLD_STORAGE_ERR;
		}

		bank2_base = ctx->flash_base + ctx->flash_bank_size;
		first = flash_get_page_in_bank(ctx, bank2_base);
		last = flash_get_page_in_bank(ctx, end);
		if (ctx->ops->erase_pages(ctx->hw, ctx->flash_bank2, first,
					  (last - first) + 1u) != 0) {
			(void)ctx->ops->lock(ctx->hw);
			return BLD_STORAGE_ERR;
		}
	}

	(void)ctx->ops->lock(ctx->hw);
	return BLD_STORAGE_OK;
}

static int stm32l4_write(const struct bld_storage *self, uint32_t offset,
			 const uint8_t *data, uint32_t len)
{
	const struct bld_storage_flash_ctx *ctx;
	uint32_t addr;
	uint32_t i;

	if (self == NULL || self->ctx == NULL || data == NULL || len == 0u) {
		return BLD_STORAGE_ERR;
	}

	ctx = (const struct bld_storage_flash_ctx *)self->ctx;

	if (ctx->ops == NULL || ctx->ops->unlock == NULL ||
	    ctx->ops->lock == NULL || ctx->ops->program_doubleword == NULL) {
		return BLD_STORAGE_ERR;
	}

	if (!range_valid(ctx, offset, len)) {
		return BLD_STORAGE_ERR;
	}

	addr = abs_addr(ctx, offset);

	if (ctx->ops->unlock(ctx->hw) != 0) {
		return BLD_STORAGE_ERR;
	}

	i = 0u;
	while (i < len) {
		uint8_t chunk[BLD_STM32L4_DOUBLEWORD_SIZE];
		uint32_t remaining = len - i;
		uint32_t cpy = (remaining >= 8u) ? 8u : remaining;
		uint64_t doubleword = 0u;

		memset(chunk, BLD_STM32L4_FLASH_ERASED_BYTE, sizeof(chunk));
		memcpy(chunk, &data[i], cpy);

		if ((addr & BLD_STM32L4_DOUBLEWORD_ALIGN_MASK) != 0u) {
			(void)ctx->ops->lock(ctx->hw);
			return BLD_STORAGE_ERR;
		}

		memcpy(&doubleword, chunk, sizeof(doubleword));
		if (ctx->ops->program_doubleword(ctx->hw, addr, doubleword) !=
		    0) {
			(void)ctx->ops->lock(ctx->hw);
			return BLD_STORAGE_ERR;
		}

		addr += 8u;
		i += cpy;
	}

	(void)ctx->ops->lock(ctx->hw);
	return BLD_STORAGE_OK;
}

static int stm32l4_read(const struct bld_storage *self, uint32_t offset,
			uint8_t *out, uint32_t len)
{
	const struct bld_storage_flash_ctx *ctx;
	uint32_t addr;

	if (self == NULL || self->ctx == NULL || out == NULL || len == 0u) {
		return BLD_STORAGE_ERR;
	}

	ctx = (const struct bld_storage_flash_ctx *)self->ctx;

	if (ctx->ops == NULL || ctx->ops->read == NULL) {
		return BLD_STORAGE_ERR;
	}

	if (!range_valid(ctx, offset, len)) {
		return BLD_STORAGE_ERR;
	}

	addr = abs_addr(ctx, offset);

	if (ctx->ops->read(ctx->hw, addr, out, len) != 0) {
		return BLD_STORAGE_ERR;
	}

	return BLD_STORAGE_OK;
}

int bld_storage_flash_init(struct bld_storage *storage,
			   const struct bld_storage_flash_ctx *ctx)
{
	if (storage == NULL || ctx == NULL || ctx->ops == NULL ||
	    ctx->ops->unlock == NULL || ctx->ops->lock == NULL ||
	    ctx->ops->read == NULL || ctx->ops->erase_pages == NULL ||
	    ctx->ops->program_doubleword == NULL) {
		return BLD_STORAGE_ERR;
	}

	storage->erase = stm32l4_erase;
	storage->write = stm32l4_write;
	storage->read = stm32l4_read;
	storage->ctx = ctx;
	return BLD_STORAGE_OK;
}