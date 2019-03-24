// nano ZIP-compatible CRC32 implementation

#ifndef __CRC32_H__
#define __CRC32_H__

#include <stdint.h>

/// start value for CRC32
#define CRC32_START	((uint32_t)0xffffffff)

/// precompute the CRC32 table
extern void crc32_init ();
/// update the CRC32 by memory block contents
extern uint32_t crc32_update (uint32_t crc, const void *data, unsigned size);
/// return the final CRC32 value
extern uint32_t crc32_finish (uint32_t crc);

#endif /* __CRC32_H__ */
