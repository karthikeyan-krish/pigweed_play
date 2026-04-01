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
 *   flash base -> bootloader -> metadata -> application slot
 */

#define BLD_FLASH_BASE 0x08000000u
#define BLD_BOOTLOADER_SIZE (126u * KB_TO_BYTES)
#define BLD_META_BASE (BLD_FLASH_BASE + BLD_BOOTLOADER_SIZE)
#define BLD_META_SIZE (2u * KB_TO_BYTES)
#define BLD_SLOT_BASE (BLD_FLASH_BASE + BLD_BOOTLOADER_SIZE + BLD_META_SIZE)
#define BLD_SLOT_SIZE (896u * KB_TO_BYTES)

/*
 * Maximum number of boot attempts before the image is considered invalid.
 */
#define BLD_MAX_BOOT_ATTEMPTS 3u

#ifdef __cplusplus
}
#endif