/**
 * @file memory_defs.h
 * @brief ESP32-S3 replacement of the MG24 memory region definitions.
 *
 * External memory (the PAK file and the savegame area) is not memory mapped:
 * it is addressed with "virtual" pointers in the range EXT_FLASH_BASE ... and
 * it is always accessed through the extMem*() functions (see extMemory.h).
 * No data pointer on the ESP32-S3 falls into this range (data lives in
 * 0x3C000000-0x3FFFFFFF).
 */
#ifndef MEMORY_DEFS_H_
#define MEMORY_DEFS_H_
#include <stdint.h>

#define EXT_FLASH_BASE              0x40000000UL
#define EXT_FLASH_VIRTUAL_SIZE      0x10000000UL                // 256 MB of virtual external memory
#define SPI_ADDRESS_MASK            (EXT_FLASH_VIRTUAL_SIZE - 1)
//
#define FLASH_BLOCK_SIZE            8192
// The MG24 used the internal flash after the code as cache. Here the cache is a PSRAM buffer.
#define __flashSize                 ((uint32_t *) 0)
#define FLASH_CODE_SIZE             0
//
// The PAK file is mapped at offset 4 of the external memory, like on the MG24.
#define PAK_ADDRESS                 ((uint8_t*)(EXT_FLASH_BASE + 4))
//
static inline int isOnExternalFlash(const void *a)
{
    return ((uint32_t) a - EXT_FLASH_BASE) < EXT_FLASH_VIRTUAL_SIZE;
}
#endif
