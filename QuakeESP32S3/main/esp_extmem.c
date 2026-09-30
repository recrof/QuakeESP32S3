/**
 * @file esp_extmem.c
 * @brief External memory for the ESP32-S3 Quake port.
 *
 * The converted PAK (tens of MB) does not fit in flash nor in PSRAM, so it is
 * read from the SD card in EXT_CACHE_BLOCK_SIZE blocks, which are kept in a
 * large PSRAM cache (CLOCK replacement). After a level has been played for a
 * few seconds, virtually all accesses hit the cache.
 *
 * Savegames and settings live in the "quakenvm" flash partition, mapped at the
 * end of the virtual external memory, exactly where the MG24 port kept them
 * in the SPI flash.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_partition.h"
#include "extMemory.h"
#include "board_config.h"

#define NVM_PARTITION_LABEL     "quakenvm"
#define NO_SLOT                 0xFFFF
#define NO_BLOCK                0xFFFFFFFF
#define FLASH_SECTOR_SIZE       4096

static const char *TAG = "extmem";

uint32_t currentExtAddress;
uint32_t extCacheLastBlock = NO_BLOCK;
const uint8_t *extCacheLastData;
uint8_t extMemAsynchByte;

static int pakFile = -1;             // POSIX fd: whole blocks per read() call (stdio unbuffered reads byte by byte)
static uint32_t pakSize;
static uint32_t numBlocks;
static uint16_t *blockToSlot;        // PAK block -> cache slot
static uint32_t *slotToBlock;        // cache slot -> PAK block
static uint8_t *slotReferenced;      // CLOCK "second chance" bits
static uint8_t *slotData;            // PSRAM cache data
static uint32_t numSlots;
static uint32_t clockHand;
static uint8_t *bounceBuffer;        // internal DMA-capable buffer for SD reads
static uint32_t cacheMisses;

static const esp_partition_t *nvmPartition;
static uint32_t nvmStart;            // virtual offset where the partition begins
// Flash writes cannot be done from a task whose stack is in PSRAM (the game task
// stack may be), so they are executed by a small worker task.
typedef struct
{
    bool erase;
    uint32_t offset;
    const void *buffer;
    uint32_t size;
} nvmRequest_t;
static const uint8_t *nvmMapped;     // memory mapped view of the partition, for reads
static QueueHandle_t nvmQueue;
static SemaphoreHandle_t nvmDone;

static void nvmTask(void *arg)
{
    nvmRequest_t r;
    while (1)
    {
        xQueueReceive(nvmQueue, &r, portMAX_DELAY);
        esp_err_t err = r.erase ? esp_partition_erase_range(nvmPartition, r.offset, r.size)
            : esp_partition_write(nvmPartition, r.offset, r.buffer, r.size);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Flash %s error %d", r.erase ? "erase" : "write", err);
        }
        xSemaphoreGive(nvmDone);
    }
}

static void nvmExecute(bool erase, uint32_t offset, const void *buffer, uint32_t size)
{
    nvmRequest_t r = { erase, offset, buffer, size };
    xQueueSend(nvmQueue, &r, portMAX_DELAY);
    xSemaphoreTake(nvmDone, portMAX_DELAY);
}

uint32_t extMemGetCacheMisses(void)
{
    return cacheMisses;
}

uint32_t extMemGetPakSize(void)
{
    return pakSize;
}

int extMemInit(void)
{
    pakFile = open(SD_PAK_PATH, O_RDONLY);
    if (pakFile < 0)
    {
        ESP_LOGE(TAG, "Cannot open %s", SD_PAK_PATH);
        return -1;
    }
    struct stat st;
    if (stat(SD_PAK_PATH, &st) != 0 || st.st_size <= 0)
    {
        ESP_LOGE(TAG, "Cannot get size of %s", SD_PAK_PATH);
        return -1;
    }
    pakSize = st.st_size;
    numBlocks = (pakSize + EXT_CACHE_BLOCK_SIZE - 1) / EXT_CACHE_BLOCK_SIZE;
    //
    bounceBuffer = heap_caps_malloc(EXT_CACHE_BLOCK_SIZE, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    blockToSlot = heap_caps_malloc(numBlocks * sizeof(*blockToSlot), MALLOC_CAP_SPIRAM);
    if (!bounceBuffer || !blockToSlot)
    {
        ESP_LOGE(TAG, "Out of memory");
        return -1;
    }
    for (uint32_t i = 0; i < numBlocks; i++)
    {
        blockToSlot[i] = NO_SLOT;
    }
    // use as much PSRAM as possible for the cache
    size_t freePsram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    if (freePsram < EXT_CACHE_PSRAM_RESERVE + 64 * EXT_CACHE_BLOCK_SIZE)
    {
        ESP_LOGE(TAG, "Not enough PSRAM for the PAK cache (%u bytes free)", (unsigned) freePsram);
        return -1;
    }
    numSlots = (freePsram - EXT_CACHE_PSRAM_RESERVE) / (EXT_CACHE_BLOCK_SIZE + sizeof(*slotToBlock) + sizeof(*slotReferenced));
    if (numSlots > NO_SLOT - 1)
    {
        numSlots = NO_SLOT - 1;
    }
    if (numSlots > numBlocks)
    {
        numSlots = numBlocks;       // the whole PAK fits!
    }
    slotData = heap_caps_malloc(numSlots * EXT_CACHE_BLOCK_SIZE, MALLOC_CAP_SPIRAM);
    slotToBlock = heap_caps_malloc(numSlots * sizeof(*slotToBlock), MALLOC_CAP_SPIRAM);
    slotReferenced = heap_caps_calloc(numSlots, sizeof(*slotReferenced), MALLOC_CAP_SPIRAM);
    if (!slotData || !slotToBlock || !slotReferenced)
    {
        ESP_LOGE(TAG, "Cannot allocate PAK cache");
        return -1;
    }
    for (uint32_t i = 0; i < numSlots; i++)
    {
        slotToBlock[i] = NO_BLOCK;
    }
    ESP_LOGI(TAG, "PAK: %u bytes, cache: %u x %u bytes (%u%% of the PAK)", (unsigned) pakSize, (unsigned) numSlots,
        EXT_CACHE_BLOCK_SIZE, (unsigned) (100ULL * numSlots / numBlocks));
    //
    nvmPartition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, NVM_PARTITION_LABEL);
    if (!nvmPartition)
    {
        ESP_LOGE(TAG, "Partition %s not found: savegames and settings will not be saved", NVM_PARTITION_LABEL);
    }
    else
    {
        nvmStart = EXT_FLASH_VIRTUAL_SIZE - nvmPartition->size;
        esp_partition_mmap_handle_t handle;
        if (esp_partition_mmap(nvmPartition, 0, nvmPartition->size, ESP_PARTITION_MMAP_DATA, (const void**) &nvmMapped, &handle) != ESP_OK)
        {
            ESP_LOGE(TAG, "Cannot map partition %s", NVM_PARTITION_LABEL);
            nvmPartition = NULL;
            return 0;
        }
        nvmQueue = xQueueCreate(1, sizeof(nvmRequest_t));
        nvmDone = xSemaphoreCreateBinary();
        xTaskCreatePinnedToCore(nvmTask, "nvm", 3072, NULL, 10, NULL, 0);
    }
    return 0;
}

static const uint8_t* getBlock(uint32_t block)
{
    uint32_t slot = blockToSlot[block];
    if (slot != NO_SLOT)
    {
        slotReferenced[slot] = 1;
        return &slotData[slot * EXT_CACHE_BLOCK_SIZE];
    }
    // miss: find a victim (CLOCK)
    while (1)
    {
        slot = clockHand;
        clockHand = clockHand + 1 < numSlots ? clockHand + 1 : 0;
        if (!slotReferenced[slot])
        {
            break;
        }
        slotReferenced[slot] = 0;
    }
    if (slotToBlock[slot] != NO_BLOCK)
    {
        blockToSlot[slotToBlock[slot]] = NO_SLOT;
        if (extCacheLastBlock == slotToBlock[slot])
        {
            extCacheLastBlock = NO_BLOCK;
        }
    }
    uint8_t *data = &slotData[slot * EXT_CACHE_BLOCK_SIZE];
    uint32_t position = block * EXT_CACHE_BLOCK_SIZE;
    uint32_t size = pakSize - position < EXT_CACHE_BLOCK_SIZE ? pakSize - position : EXT_CACHE_BLOCK_SIZE;
    ssize_t rd = 0;
    if (lseek(pakFile, position, SEEK_SET) == (off_t) position)
    {
        rd = read(pakFile, bounceBuffer, size);
        if (rd < 0)
        {
            rd = 0;
        }
    }
    if (rd != (ssize_t) size)
    {
        ESP_LOGE(TAG, "SD read error at %u (%u/%u)", (unsigned) position, (unsigned) rd, (unsigned) size);
    }
    memcpy(data, bounceBuffer, rd);
    memset(data + rd, 0xFF, EXT_CACHE_BLOCK_SIZE - rd);
    slotToBlock[slot] = block;
    blockToSlot[block] = slot;
    slotReferenced[slot] = 1;
    cacheMisses++;
    return data;
}

uint8_t extMemReadByteSlow(uint32_t offset)
{
    uint8_t b;
    extMemRead(offset, &b, 1);
    return b;
}

void* extMemRead(uint32_t offset, void *dest, uint32_t length)
{
    uint8_t *d = dest;
    offset &= SPI_ADDRESS_MASK;
    while (length)
    {
        uint32_t fileOffset = offset - PAK_FILE_OFFSET;
        uint32_t chunk;
        if (offset >= PAK_FILE_OFFSET && fileOffset < pakSize)
        {
            uint32_t block = fileOffset / EXT_CACHE_BLOCK_SIZE;
            uint32_t inBlock = fileOffset & (EXT_CACHE_BLOCK_SIZE - 1);
            const uint8_t *data = getBlock(block);
            extCacheLastBlock = block;
            extCacheLastData = data;
            chunk = EXT_CACHE_BLOCK_SIZE - inBlock;
            if (chunk > length)
            {
                chunk = length;
            }
            memcpy(d, data + inBlock, chunk);
        }
        else if (nvmPartition && offset >= nvmStart)
        {
            chunk = length;
            if (offset + chunk > EXT_FLASH_VIRTUAL_SIZE)
            {
                chunk = EXT_FLASH_VIRTUAL_SIZE - offset;
            }
            memcpy(d, nvmMapped + (offset - nvmStart), chunk);     // the cache is invalidated after writes
        }
        else
        {
            // unmapped: behave like erased flash. Stop at the next mapped region.
            chunk = length;
            if (offset < PAK_FILE_OFFSET && offset + chunk > PAK_FILE_OFFSET)
            {
                chunk = PAK_FILE_OFFSET - offset;
            }
            if (nvmPartition && offset < nvmStart && offset + chunk > nvmStart)
            {
                chunk = nvmStart - offset;
            }
            memset(d, 0xFF, chunk);
        }
        d += chunk;
        offset += chunk;
        length -= chunk;
    }
    return dest;
}

static int nvmRange(uint32_t *address, uint32_t size)
{
    *address &= SPI_ADDRESS_MASK;
    if (!nvmPartition || *address < nvmStart || *address + size > EXT_FLASH_VIRTUAL_SIZE)
    {
        ESP_LOGE(TAG, "Write outside the savegame area: 0x%08x, %u bytes", (unsigned) *address, (unsigned) size);
        return 0;
    }
    *address -= nvmStart;
    return 1;
}

void extMemErase(uint32_t address, uint32_t size)
{
    if (!size)
    {
        return;
    }
    uint32_t start = address & ~(FLASH_SECTOR_SIZE - 1);
    size = ((address + size + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1)) - start;
    if (nvmRange(&start, size))
    {
        nvmExecute(true, start, NULL, size);
    }
}

void extMemProgram(uint32_t address, uint8_t *buffer, uint32_t size)
{
    if (size && nvmRange(&address, size))
    {
        nvmExecute(false, address, buffer, size);
    }
}

void extMemWrite(uint32_t address, void *buffer, uint32_t size)
{
    extMemErase(address, size);
    extMemProgram(address, buffer, size);
}
