#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KB_TO_BYTES 1024u

/*
 * Flash memory layout configuration
 *
 * These macros define the bootloader-reserved region, metadata region, and
 * application image slot in internal flash.
 *
 * The default layout is:
 *   flash base -> bootloader -> metadata -> app slot_A -> app slot_B
 */

#define BLD_FLASH_BASE 0x08000000u

#define BLD_BOOTLOADER_SIZE (128u * KB_TO_BYTES)
#define BLD_FLASH_BANK_SIZE (512u * KB_TO_BYTES)
#define BLD_FLASH_PAGE_SIZE (2u * KB_TO_BYTES)

#define BLD_META_BASE (BLD_FLASH_BASE + BLD_BOOTLOADER_SIZE)
#define BLD_META_SIZE (8u * KB_TO_BYTES)

#define BLD_SLOT_A_BASE (BLD_META_BASE + BLD_META_SIZE)
#define BLD_SLOT_A_SIZE (376u * KB_TO_BYTES)

#define BLD_SLOT_B_BASE (BLD_FLASH_BASE + BLD_FLASH_BANK_SIZE)
#define BLD_SLOT_B_SIZE (376u * KB_TO_BYTES)

/*
 * Maximum number of boot attempts before the image is considered invalid.
 */
#define BLD_MAX_BOOT_ATTEMPTS 3u

#ifdef __cplusplus
}
#endif