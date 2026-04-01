#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLD_CRC32_INITIAL 0u

uint32_t bld_crc32_ieee(const void *data, size_t len, uint32_t seed);

#ifdef __cplusplus
}
#endif