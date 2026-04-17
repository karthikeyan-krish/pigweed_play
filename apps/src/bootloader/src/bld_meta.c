#include "bld_meta.h"
#include "bld_crc32.h"
#include "bld_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BLD_META_OK 0
#define BLD_META_ERR (-1)

struct __attribute__((packed)) bld_meta_record {
	uint32_t magic;
	uint8_t active_slot;
	uint8_t confirmed_slot;
	uint8_t pending_slot;
	uint8_t reserved0;
	struct bld_slot_info slots[2];
	uint32_t record_crc32;
};

static uint32_t bld_meta_crc32(const struct bld_meta_record *record)
{
	return bld_crc32_ieee(record,
			      sizeof(*record) - sizeof(record->record_crc32),
			      BLD_CRC32_INITIAL);
}

static void bld_meta_record_init_default(struct bld_meta_record *record)
{
	memset(record, 0, sizeof(*record));
	record->magic = BLD_META_MAGIC;
	record->active_slot = (uint8_t)BLD_SLOT_ID_NONE;
	record->confirmed_slot = (uint8_t)BLD_SLOT_ID_NONE;
	record->pending_slot = (uint8_t)BLD_SLOT_ID_NONE;

	struct bld_slot_info empty_slot;

	memset(&empty_slot, 0, sizeof(empty_slot));
	empty_slot.state = (uint8_t)BLD_SLOT_STATE_EMPTY;
	empty_slot.boot_attempts_left = 0u;

	record->slots[BLD_SLOT_ID_A] = empty_slot;
	record->slots[BLD_SLOT_ID_B] = empty_slot;

	record->record_crc32 = bld_meta_crc32(record);
}

static int bld_meta_storage_valid(const struct bld_storage *meta_storage)
{
	if (meta_storage == NULL) {
		return BLD_META_ERR;
	}
	if (meta_storage->ctx == NULL) {
		return BLD_META_ERR;
	}
	if (meta_storage->read == NULL) {
		return BLD_META_ERR;
	}
	if (meta_storage->write == NULL) {
		return BLD_META_ERR;
	}
	if (meta_storage->erase == NULL) {
		return BLD_META_ERR;
	}
	return BLD_META_OK;
}

static int bld_meta_slot_id_valid(uint8_t slot)
{
	return (slot == BLD_SLOT_ID_A || slot == BLD_SLOT_ID_B ||
		slot == BLD_SLOT_ID_NONE) ?
		       BLD_META_OK :
		       BLD_META_ERR;
}

static int bld_meta_slot_state_valid(uint8_t state)
{
	return (state == BLD_SLOT_STATE_EMPTY ||
		state == BLD_SLOT_STATE_VALID ||
		state == BLD_SLOT_STATE_PENDING ||
		state == BLD_SLOT_STATE_CONFIRMED ||
		state == BLD_SLOT_STATE_BAD) ?
		       BLD_META_OK :
		       BLD_META_ERR;
}

static int bld_meta_record_validate(const struct bld_meta_record *record)
{
	if (record == NULL) {
		return BLD_META_ERR;
	}
	if (record->magic != BLD_META_MAGIC) {
		return BLD_META_ERR;
	}
	if (bld_meta_slot_id_valid(record->active_slot) != BLD_META_OK ||
	    bld_meta_slot_id_valid(record->confirmed_slot) != BLD_META_OK ||
	    bld_meta_slot_id_valid(record->pending_slot) != BLD_META_OK) {
		return BLD_META_ERR;
	}
	for (uint8_t i = 0u; i < 2u; ++i) {
		if (bld_meta_slot_state_valid(record->slots[i].state) !=
		    BLD_META_OK) {
			return BLD_META_ERR;
		}
	}
	return BLD_META_OK;
}

static int bld_meta_record_read(const struct bld_storage *meta_storage,
				struct bld_meta_record *out)
{
	if (meta_storage->read(meta_storage, 0u, (uint8_t *)out,
			       sizeof(*out)) != 0) {
		return BLD_META_ERR;
	}

	if (bld_meta_record_validate(out) != BLD_META_OK) {
		return BLD_META_ERR;
	}

	if (out->record_crc32 != bld_meta_crc32(out)) {
		return BLD_META_ERR;
	}

	return BLD_META_OK;
}

static int bld_meta_record_write(const struct bld_storage *meta_storage,
				 const struct bld_meta_record *record)
{
	struct bld_meta_record tmp;
	tmp = *record;
	tmp.record_crc32 = bld_meta_crc32(&tmp);

	if (meta_storage->erase(meta_storage, 0u, sizeof(tmp)) != 0) {
		return BLD_META_ERR;
	}

	if (meta_storage->write(meta_storage, 0u, (const uint8_t *)&tmp,
				sizeof(tmp)) != 0) {
		return BLD_META_ERR;
	}

	return BLD_META_OK;
}

static int
bld_meta_record_load_or_default(const struct bld_storage *meta_storage,
				struct bld_meta_record *out)
{
	if (bld_meta_record_read(meta_storage, out) == BLD_META_OK) {
		return BLD_META_OK;
	}

	bld_meta_record_init_default(out);
	return BLD_META_OK;
}

int bld_meta_read_boot_control(const struct bld_storage *meta_storage,
			       struct bld_boot_control *out)
{
	struct bld_meta_record record;

	if (bld_meta_storage_valid(meta_storage) != BLD_META_OK ||
	    out == NULL) {
		return BLD_META_ERR;
	}

	if (bld_meta_record_read(meta_storage, &record) != BLD_META_OK) {
		return BLD_META_ERR;
	}

	out->active_slot = record.active_slot;
	out->confirmed_slot = record.confirmed_slot;
	out->pending_slot = record.pending_slot;
	out->reserved0 = record.reserved0;
	out->slots[0] = record.slots[0];
	out->slots[1] = record.slots[1];
	return BLD_META_OK;
}

int bld_meta_write_boot_control(const struct bld_storage *meta_storage,
				const struct bld_boot_control *ctrl)
{
	struct bld_meta_record record;

	if (bld_meta_storage_valid(meta_storage) != BLD_META_OK ||
	    ctrl == NULL) {
		return BLD_META_ERR;
	}

	record.magic = BLD_META_MAGIC;
	record.active_slot = ctrl->active_slot;
	record.confirmed_slot = ctrl->confirmed_slot;
	record.pending_slot = ctrl->pending_slot;
	record.reserved0 = ctrl->reserved0;
	record.slots[0] = ctrl->slots[0];
	record.slots[1] = ctrl->slots[1];

	if (bld_meta_record_validate(&record) != BLD_META_OK) {
		return BLD_META_ERR;
	}

	return bld_meta_record_write(meta_storage, &record);
}

int bld_meta_set_pending(const struct bld_storage *meta_storage,
			 enum bld_slot_id slot, uint32_t version, uint32_t size,
			 uint32_t crc32, uint8_t attempts)
{
	struct bld_meta_record record;

	if (bld_meta_storage_valid(meta_storage) != BLD_META_OK) {
		return BLD_META_ERR;
	}

	if (slot != BLD_SLOT_ID_A && slot != BLD_SLOT_ID_B) {
		return BLD_META_ERR;
	}

	if (version == 0u || size == 0u || crc32 == 0u || attempts == 0u) {
		return BLD_META_ERR;
	}

	if (bld_meta_record_load_or_default(meta_storage, &record) !=
	    BLD_META_OK) {
		return BLD_META_ERR;
	}

	record.slots[slot].version = version;
	record.slots[slot].size = size;
	record.slots[slot].crc32 = crc32;
	record.slots[slot].state = (uint8_t)BLD_SLOT_STATE_PENDING;
	record.slots[slot].boot_attempts_left = attempts;

	record.pending_slot = (uint8_t)slot;

	return bld_meta_record_write(meta_storage, &record);
}

int bld_meta_confirm_slot(const struct bld_storage *meta_storage)
{
	struct bld_meta_record record;
	enum bld_slot_id slot;

	if (bld_meta_storage_valid(meta_storage) != BLD_META_OK) {
		return BLD_META_ERR;
	}

	if (bld_meta_record_read(meta_storage, &record) != BLD_META_OK) {
		return BLD_META_ERR;
	}

	slot = record.pending_slot;

	if (slot != BLD_SLOT_ID_A && slot != BLD_SLOT_ID_B) {
		return BLD_META_ERR;
	}

	record.slots[slot].state = (uint8_t)BLD_SLOT_STATE_CONFIRMED;
	record.slots[slot].boot_attempts_left = 0u;
	record.active_slot = (uint8_t)slot;
	record.confirmed_slot = (uint8_t)slot;

	if (record.pending_slot == (uint8_t)slot) {
		record.pending_slot = (uint8_t)BLD_SLOT_ID_NONE;
	}

	for (uint8_t i = 0u; i < 2u; ++i) {
		if (i == (uint8_t)slot) {
			continue;
		}

		if (record.slots[i].state ==
		    (uint8_t)BLD_SLOT_STATE_CONFIRMED) {
			record.slots[i].state = (uint8_t)BLD_SLOT_STATE_VALID;
		}
	}

	return bld_meta_record_write(meta_storage, &record);
}

static int
bld_meta_slot_is_selectable_for_active(const struct bld_meta_record *record,
				       uint8_t slot)
{
	return (record->slots[slot].state ==
			(uint8_t)BLD_SLOT_STATE_CONFIRMED ||
		record->slots[slot].state == (uint8_t)BLD_SLOT_STATE_VALID ||
		record->slots[slot].state == (uint8_t)BLD_SLOT_STATE_PENDING);
}

static int
bld_meta_slot_is_selectable_for_confirmed(const struct bld_meta_record *record,
					  uint8_t slot)
{
	return (record->slots[slot].state ==
			(uint8_t)BLD_SLOT_STATE_CONFIRMED ||
		record->slots[slot].state == (uint8_t)BLD_SLOT_STATE_VALID);
}

int bld_meta_mark_slot_bad(const struct bld_storage *meta_storage,
			   enum bld_slot_id slot)
{
	struct bld_meta_record record;
	uint8_t other_slot;

	if (slot != BLD_SLOT_ID_A && slot != BLD_SLOT_ID_B) {
		return BLD_META_ERR;
	}

	if (bld_meta_record_load_or_default(meta_storage, &record) !=
	    BLD_META_OK) {
		return BLD_META_ERR;
	}

	other_slot = (slot == BLD_SLOT_ID_A) ? (uint8_t)BLD_SLOT_ID_B :
					       (uint8_t)BLD_SLOT_ID_A;

	record.slots[slot].state = (uint8_t)BLD_SLOT_STATE_BAD;
	record.slots[slot].boot_attempts_left = 0u;

	if (record.pending_slot == (uint8_t)slot) {
		record.pending_slot = (uint8_t)BLD_SLOT_ID_NONE;
	}

	if (record.confirmed_slot == (uint8_t)slot) {
		record.confirmed_slot =
			bld_meta_slot_is_selectable_for_confirmed(&record,
								  other_slot) ?
				other_slot :
				(uint8_t)BLD_SLOT_ID_NONE;
	}

	if (record.active_slot == (uint8_t)slot) {
		record.active_slot = bld_meta_slot_is_selectable_for_active(
					     &record, other_slot) ?
					     other_slot :
					     (uint8_t)BLD_SLOT_ID_NONE;
	}

	return bld_meta_record_write(meta_storage, &record);
}

int bld_meta_decrement_pending_attempts(const struct bld_storage *meta_storage,
					uint8_t *attempts_left_after)
{
	struct bld_meta_record record;
	uint8_t slot;

	if (bld_meta_storage_valid(meta_storage) != BLD_META_OK ||
	    attempts_left_after == NULL) {
		return BLD_META_ERR;
	}

	if (bld_meta_record_load_or_default(meta_storage, &record) !=
	    BLD_META_OK) {
		return BLD_META_ERR;
	}

	slot = record.pending_slot;
	if (slot == (uint8_t)BLD_SLOT_ID_NONE ||
	    slot > (uint8_t)BLD_SLOT_ID_B) {
		return BLD_META_ERR;
	}

	if (record.slots[slot].state != (uint8_t)BLD_SLOT_STATE_PENDING) {
		return BLD_META_ERR;
	}

	if (record.slots[slot].boot_attempts_left > 0u) {
		record.slots[slot].boot_attempts_left -= 1u;
	}

	*attempts_left_after = record.slots[slot].boot_attempts_left;

	return bld_meta_record_write(meta_storage, &record);
}