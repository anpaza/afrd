// nano ZIP-compatible CRC32 implementation

#include "crc32.h"

static uint32_t crc32_table [256];

void crc32_init ()
{
	for (unsigned i = 0; i < 256; i++) {
		uint32_t val = i;
		for (unsigned j = 0; j < 8; j++)
			val = (val & 1) ? (val >> 1) ^ 0xedb88320 : (val >> 1);
		crc32_table [i] = val;
	}
}

static inline uint32_t crc32_update_byte (uint32_t crc, uint8_t val)
{
	return crc32_table [(crc ^ val) & 0xff] ^ (crc >> 8);
}

uint32_t crc32_update (uint32_t crc, const void *data, unsigned size)
{
	const uint8_t *cur = (const uint8_t *)data;
	const uint8_t *end = cur + size;
	while (cur < end)
		crc = crc32_update_byte (crc, *cur++);
	return crc;
}

uint32_t crc32_finish (uint32_t crc)
{
	return ~crc;
}
