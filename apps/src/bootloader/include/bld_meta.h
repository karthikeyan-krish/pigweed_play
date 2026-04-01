#pragma once
#include <stdint.h>

#include "bld_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLD_META_MAGIC (0xB00710ADu)

enum bld_image_state {
	BLD_IMAGE_STATE_EMPTY = 0,
	BLD_IMAGE_STATE_READY = 1,
	BLD_IMAGE_STATE_CORRUPTED = 2,
};

/*
 * Public view of image metadata.
 *
 * This structure represents the logical metadata used by the bootloader
 * and application. The on-flash metadata layout is private to the metadata
 * module implementation.
 */
struct bld_image_info {
	uint32_t version;
	uint32_t size;
	uint32_t crc32;
	enum bld_image_state state;
};

/*
 * Reads metadata from persistent storage.
 *
 * Returns 0 on success, negative value on failure.
 */
int bld_meta_read_info(const struct bld_storage *meta_storage,
		       struct bld_image_info *out);

/*
 * Writes image metadata.
 *
 * This stores version, size and CRC and typically sets state to READY.
 *
 * Returns 0 on success, negative value on failure.
 */
int bld_meta_write_info(const struct bld_storage *meta_storage,
			uint32_t version, uint32_t size, uint32_t crc32);

/*
 * Updates only the image state field.
 *
 * The existing metadata is read, modified and written back.
 *
 * Returns 0 on success, negative value on failure.
 */
int bld_meta_set_state(const struct bld_storage *meta_storage,
		       enum bld_image_state state);

#ifdef __cplusplus
}
#endif