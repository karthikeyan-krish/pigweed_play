#pragma once
#include <stdint.h>

#include "bld_storage.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BLD_META_MAGIC (0xB00710ADu)

enum bld_slot_id {
	BLD_SLOT_ID_A = 0,
	BLD_SLOT_ID_B = 1,
	BLD_SLOT_ID_NONE = 0xFF,
};

/*
 * EMPTY - no image in that slot
 *
 * VALID - known-good image, but not the active confirmed one anymore
 *
 * PENDING - newly installed image, not yet trusted,
 * waiting for trial boot + app confirmation
 *
 * CONFIRMED - currently accepted good image
 *
 * BAD - should not be booted
 */
enum bld_slot_state {
	BLD_SLOT_STATE_EMPTY = 0,
	BLD_SLOT_STATE_VALID = 1,
	BLD_SLOT_STATE_PENDING = 2,
	BLD_SLOT_STATE_CONFIRMED = 3,
	BLD_SLOT_STATE_BAD = 4,
};

struct bld_slot_info {
	uint32_t version;
	uint32_t size;
	uint32_t crc32;
	uint8_t state;
	uint8_t boot_attempts_left;
	uint16_t reserved;
};

struct bld_boot_control {
	uint8_t active_slot;
	uint8_t confirmed_slot;
	uint8_t pending_slot;
	uint8_t reserved0;
	struct bld_slot_info slots[2];
};

int bld_meta_init(const struct bld_storage *meta_storage);

int bld_meta_read_boot_control(const struct bld_storage *meta_storage,
			       struct bld_boot_control *out);

int bld_meta_write_boot_control(const struct bld_storage *meta_storage,
				const struct bld_boot_control *ctrl);

int bld_meta_set_pending(const struct bld_storage *meta_storage,
			 enum bld_slot_id slot, uint32_t version, uint32_t size,
			 uint32_t crc32, uint8_t attempts);

int bld_meta_confirm_slot(const struct bld_storage *meta_storage);

int bld_meta_mark_slot_bad(const struct bld_storage *meta_storage,
			   enum bld_slot_id slot);

int bld_meta_decrement_pending_attempts(const struct bld_storage *meta_storage,
					uint8_t *attempts_left_after);

#ifdef __cplusplus
}
#endif