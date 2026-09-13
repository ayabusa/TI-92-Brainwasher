#ifndef CRC32_H
#define CRC32_H

#include <stdint.h>

unsigned int crc32(const uint8_t *buffer, uint32_t len);

#endif
