#include "bld_crc32.h"

#include <stdint.h>
#include <stddef.h>

#define CRC32_REFLECTED_POLY 0xEDB88320u

static const uint32_t crc32_lsb_mask = 1u;
static const uint8_t bits_per_byte = 8u;

/* Standard CRC-32 (IEEE 802.3), poly 0x04C11DB7 reflected => 0xEDB88320 */
uint32_t bld_crc32_ieee(const void *data, size_t len, uint32_t seed)
{
	const uint8_t *p = (const uint8_t *)data;
	uint32_t crc = ~seed;

	for (size_t i = 0; i < len; ++i) {
		crc ^= p[i];
		for (uint8_t b = 0; b < bits_per_byte; ++b) {
			uint32_t mask =
				(uint32_t) - (int)(crc & crc32_lsb_mask);
			crc = (crc >> 1) ^ (CRC32_REFLECTED_POLY & mask);
		}
	}
	return ~crc;
}