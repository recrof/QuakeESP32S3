/**
 * @file extMemory.h
 * @brief ESP32-S3 external memory: same API as the MG24 interleaved SPI flash
 * wrapper, but backed by the PAK file on the SD card (through a PSRAM block
 * cache) and by a flash partition for savegames/settings.
 *
 * External addresses may be passed either tagged with EXT_FLASH_BASE or as raw
 * offsets (the engine does both, e.g. for the savegame area). Everything is
 * masked with SPI_ADDRESS_MASK.
 *
 * Virtual layout:
 *  - [4, 4 + PAK size)                     : PAK file on the SD card
 *  - [EXT_FLASH_VIRTUAL_SIZE - nvm, end)   : "quakenvm" flash partition
 *
 * Asynchronous reads of the MG24 (DMA) complete synchronously here.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#ifndef SRC_EXTMEMORY_H_
#define SRC_EXTMEMORY_H_
#include <stdint.h>
#include <string.h>
#include "memory_defs.h"
#include "board_config.h"

#define HAS_EXT_FLASH                          1
#define EXT_MEM_BYTE_TIME                      1
#define EXT_MEM_ACCESS_TIME                    65
#define EXT_MEMORY_HEADER_SIZE                 0
#define EXT_MEMORY_READ_ALIGN_SIZE             4
#define PAK_FILE_OFFSET                        4     // PAK_ADDRESS - EXT_FLASH_BASE

// Implemented in esp_extmem.c
int extMemInit(void);
void *extMemRead(uint32_t offset, void *dest, uint32_t length);
uint8_t extMemReadByteSlow(uint32_t offset);
void extMemErase(uint32_t address, uint32_t size);
void extMemProgram(uint32_t address, uint8_t *buffer, uint32_t size);
void extMemWrite(uint32_t address, void *buffer, uint32_t size);
uint32_t extMemGetPakSize(void);
uint32_t extMemGetCacheMisses(void);
//
extern uint32_t currentExtAddress;
extern uint32_t extCacheLastBlock;          // last PAK block accessed (file offset / EXT_CACHE_BLOCK_SIZE)
extern const uint8_t *extCacheLastData;     // its data
extern uint8_t extMemAsynchByte;

static inline void extMemSetCurrentAddress(uint32_t address)
{
    currentExtAddress = address & SPI_ADDRESS_MASK;
}
static inline uint8_t extMemGetByteFromAddress(const void *addr)
{
    uint32_t offset = (uint32_t) addr & SPI_ADDRESS_MASK;
    currentExtAddress = offset + 1;
    uint32_t fileOffset = offset - PAK_FILE_OFFSET;
    if (fileOffset / EXT_CACHE_BLOCK_SIZE == extCacheLastBlock)
    {
        return extCacheLastData[fileOffset & (EXT_CACHE_BLOCK_SIZE - 1)];
    }
    return extMemReadByteSlow(offset);
}
static inline uint8_t extMemGetByteFromCurrentAddress(void)
{
    return extMemGetByteFromAddress((const void*) currentExtAddress);
}
static inline short extMemFlashGetShortFromAddress(const void *addr)
{
    short s;
    uint32_t offset = (uint32_t) addr & SPI_ADDRESS_MASK;
    extMemRead(offset, &s, sizeof(s));
    currentExtAddress = offset + sizeof(s);
    return s;
}
static inline void* extMemGetDataFromCurrentAddress(void *dest, unsigned int length)
{
    extMemRead(currentExtAddress, dest, length);
    currentExtAddress += length;
    return dest;
}
static inline void* extMemGetDataFromAddress(void *dest, const void *src, unsigned int length)
{
    extMemSetCurrentAddress((uint32_t) src);
    return extMemGetDataFromCurrentAddress(dest, length);
}
static inline int extMemGetSize(void)
{
    return EXT_FLASH_VIRTUAL_SIZE;
}
/**
 * Emulates the MG24 DMA read: data is read starting from the word aligned
 * address, and the returned pointer points to the requested byte.
 */
static inline void* extMemStartAsynchDataRead(uint32_t address, void *dest, uint32_t cnt)
{
    address &= SPI_ADDRESS_MASK;
    uint32_t alignment = address & 3;
    extMemRead(address & ~3, dest, (alignment + cnt + 3) & ~3);
    currentExtAddress = address + cnt;
    return (uint8_t*) dest + alignment;
}
static inline void extMemAsynchReadByteFromAddress(uint32_t address)
{
    extMemAsynchByte = extMemGetByteFromAddress((const void*) address);
}
static inline void extMemWaitAsynchDataRead(void)
{
}
static inline int extMemHasAsynchDataReadFinished(void)
{
    return 1;
}
static inline void extMemStopDMA(void)
{
}
static inline int extMemGetRemainingBytes(void)
{
    return 0;
}
static inline void extMemRestoreInterface(void)
{
}
// Direct MG24 driver calls used by some (normally disabled) engine code paths.
static inline void interleavedSpiFlashAsynchReadByteDMA(const void *address)
{
    extMemAsynchReadByteFromAddress((uint32_t) address);
}
static inline uint8_t interleavedSpiFlashGetAsynchReadByteDMA(void)
{
    return extMemAsynchByte;
}
static inline uint8_t interleavedSpiFlashReadByteDMA(const void *address)
{
    return extMemGetByteFromAddress(address);
}
#endif /* SRC_EXTMEMORY_H_ */
