/**
 * @file display.h
 * @brief ESP32-S3 SPI LCD driver (ST7789 / ILI9341, 320x240 landscape).
 */
#ifndef DISPLAYH
#define DISPLAYH
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "board_config.h"
#define LCD_WIDTH   320
#define LCD_HEIGHT  240
void displayInit(void);
// false if the display pins are not configured: nothing is sent then
bool displayIsReady(void);
// Sets the drawing window to the full width, lines [firstLine, firstLine + numLines), and starts RAM write.
void displaySetWindow(int firstLine, int numLines);
// Queues a DMA transfer of RGB565 (big endian) pixels. At most DISPLAY_MAX_QUEUED transfers can be pending.
#define DISPLAY_MAX_QUEUED  2
// last: true for the last transfer of the frame
void displaySendPixels(const uint16_t *data, size_t numPixels, bool last);
// Waits for the oldest queued transfer. Returns 0 if nothing was pending.
int displayWaitOne(void);
// Waits for all queued transfers
void displayWaitAll(void);
#endif
