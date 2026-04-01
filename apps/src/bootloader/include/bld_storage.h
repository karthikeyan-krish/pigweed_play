#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Storage backend operations.
 *
 * All offsets are relative to the storage region represented by the storage
 * instance. Backends translate offsets to backend-specific physical addresses.
 *
 * All functions return 0 on success and a negative value on failure.
 * ctx - Backend-specific context owned by the caller.
 */
struct bld_storage {
	int (*erase)(const struct bld_storage *self, uint32_t offset,
		     uint32_t size);
	int (*write)(const struct bld_storage *self, uint32_t offset,
		     const uint8_t *data, uint32_t len);
	int (*read)(const struct bld_storage *self, uint32_t offset,
		    uint8_t *out, uint32_t len);
	const void *ctx;
};

#ifdef __cplusplus
}
#endif