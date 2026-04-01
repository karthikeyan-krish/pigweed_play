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
	uint32_t image_version;
	uint32_t image_size;
	uint32_t image_crc32;
	uint8_t state;
	uint16_t reserved[3];
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
	record->image_version = 0u;
	record->image_size = 0u;
	record->image_crc32 = 0u;
	record->state = (uint8_t)BLD_IMAGE_STATE_EMPTY;
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

static int bld_meta_record_validate(const struct bld_meta_record *record)
{
	if (record == NULL) {
		return BLD_META_ERR;
	}
	if (record->magic != BLD_META_MAGIC) {
		return BLD_META_ERR;
	}
	if (record->state != (uint8_t)BLD_IMAGE_STATE_EMPTY &&
	    record->state != (uint8_t)BLD_IMAGE_STATE_READY &&
	    record->state != (uint8_t)BLD_IMAGE_STATE_CORRUPTED) {
		return BLD_META_ERR;
	}
	if (record->record_crc32 != bld_meta_crc32(record)) {
		return BLD_META_ERR;
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

	return bld_meta_record_validate(out);
}

static int bld_meta_record_write(const struct bld_storage *meta_storage,
				 const struct bld_meta_record *record)
{
	struct bld_meta_record tmp;

	if (bld_meta_storage_valid(meta_storage) != BLD_META_OK ||
	    record == NULL) {
		return BLD_META_ERR;
	}

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

int bld_meta_read_info(const struct bld_storage *meta_storage,
		       struct bld_image_info *out)
{
	struct bld_meta_record record;

	if (bld_meta_storage_valid(meta_storage) != BLD_META_OK ||
	    out == NULL) {
		return BLD_META_ERR;
	}

	if (bld_meta_record_read(meta_storage, &record) != BLD_META_OK) {
		return BLD_META_ERR;
	}

	out->version = record.image_version;
	out->size = record.image_size;
	out->crc32 = record.image_crc32;
	out->state = (enum bld_image_state)record.state;
	return BLD_META_OK;
}

int bld_meta_write_info(const struct bld_storage *meta_storage,
			uint32_t version, uint32_t size, uint32_t crc32)
{
	struct bld_meta_record record;

	if (bld_meta_storage_valid(meta_storage) != BLD_META_OK ||
	    version == 0u || size == 0u || crc32 == 0u) {
		return BLD_META_ERR;
	}

	if (bld_meta_record_load_or_default(meta_storage, &record) !=
	    BLD_META_OK) {
		return BLD_META_ERR;
	}

	record.image_version = version;
	record.image_size = size;
	record.image_crc32 = crc32;
	record.state = (uint8_t)BLD_IMAGE_STATE_READY;

	return bld_meta_record_write(meta_storage, &record);
}

int bld_meta_set_state(const struct bld_storage *meta_storage,
		       enum bld_image_state state)
{
	struct bld_meta_record record;

	if (bld_meta_storage_valid(meta_storage) != BLD_META_OK) {
		return BLD_META_ERR;
	}

	if (state != BLD_IMAGE_STATE_EMPTY && state != BLD_IMAGE_STATE_READY &&
	    state != BLD_IMAGE_STATE_CORRUPTED) {
		return BLD_META_ERR;
	}

	if (bld_meta_record_load_or_default(meta_storage, &record) !=
	    BLD_META_OK) {
		return BLD_META_ERR;
	}

	record.state = (uint8_t)state;
	return bld_meta_record_write(meta_storage, &record);
}