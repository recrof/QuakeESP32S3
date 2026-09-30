/**
 * @file main.h
 * @brief ESP32-S3 replacement of the MG24 main.h (global configuration).
 */
#ifndef MAIN_H
#define MAIN_H
#include <stdbool.h>
#include <stdint.h>
#include "macros.h"
#include "board_config.h"
// Test configuration
#define DISABLE_CACHING_TEXTURE_TO_FLASH  0
#define TEST_DISABLE_ASYNCH_LOAD          0
// Feature config
#define FAST_CPU_SMALL_FLASH              0
#define CORRECT_TABLE_ERROR               1
#define SPI_FLASH_32BIT_ADDRESS           1
#define DEBUG_OUT_PRINTF                  1
//
unsigned int I_GetTimeMicrosecs(void);
void NVIC_SystemReset(void);
#endif
