#include "bld_confirm.h"

#include <string.h>

#include "bld_meta.h"
#include "bld_storage.h"
#include "bld_storage_flash.h"
#include "bld_config.h"
#include "stm32l4xx_hal.h"
#include "stm32l4xx_hal_flash_ex.h"

/* Flash wrapper functions */
/* Flash hardware wrapper functions */
static int stm32_flash_unlock(void *hw)
{
	(void)hw;
	return (HAL_FLASH_Unlock() == HAL_OK) ? 0 : -1;
}

static int stm32_flash_lock(void *hw)
{
	(void)hw;
	return (HAL_FLASH_Lock() == HAL_OK) ? 0 : -1;
}

static int stm32_hal_read(void *hw, uint32_t addr, uint8_t *out, uint32_t len)
{
	(void)hw;
	memcpy(out, (const void *)(uintptr_t)addr, len);
	return 0;
}

static int stm32_flash_erase_pages(void *hw, uint32_t bank, uint32_t first_page,
				   uint32_t num_pages)
{
	uint32_t page_error = 0;
	FLASH_EraseInitTypeDef erase;

	(void)hw;
	memset(&erase, 0, sizeof(erase));
	erase.TypeErase = FLASH_TYPEERASE_PAGES;
	erase.Banks = bank;
	erase.Page = first_page;
	erase.NbPages = num_pages;

	return (HAL_FLASHEx_Erase(&erase, &page_error) == HAL_OK) ? 0 : -1;
}

static int stm32_flash_program_doubleword(void *hw, uint32_t addr,
					  uint64_t data)
{
	(void)hw;
	return (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, data) ==
		HAL_OK) ?
		       0 :
		       -1;
}

int bld_confirm_running_image(void)
{
	struct bld_storage meta_storage;

	static const struct bld_flash_ops g_flash_ops = {
		.unlock = stm32_flash_unlock,
		.lock = stm32_flash_lock,
		.read = stm32_hal_read,
		.erase_pages = stm32_flash_erase_pages,
		.program_doubleword = stm32_flash_program_doubleword,
	};

	const struct bld_storage_flash_ctx meta_ctx = {
		.region_base = BLD_META_BASE,
		.region_size = BLD_META_SIZE,
		.page_size = 2048u,
		.flash_base = FLASH_BASE,
		.flash_bank_size = FLASH_BANK_SIZE,
		.flash_page_size = FLASH_PAGE_SIZE,
		.flash_bank1 = FLASH_BANK_1,
		.flash_bank2 = FLASH_BANK_2,
		.ops = &g_flash_ops,
		.hw = NULL,
	};

	if (bld_storage_flash_init(&meta_storage, &meta_ctx) != 0) {
		return -1;
	}

	return bld_meta_confirm_slot(&meta_storage);
}