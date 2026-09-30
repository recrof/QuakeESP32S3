/**
 * @file esp_graphics.c
 * @brief Frame buffer refresh and boot-time text console for the ESP32-S3
 * Quake port. Port of the MG24 graphics.c (Nicola Wrachien).
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#include <string.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_heap_caps.h"
#include "quakedef.h"
#include "graphics.h"
#include "display.h"
#include "esp_touch.h"

#define FONT_HEIGHT 8
#define FONT_WIDTH 8
extern const uint8_t font8x8_basic[128][8];
//
#define LE2BE16(x) ((uint16_t)(((x >> 8) & 0xFF) | (x << 8)))
#define RGB(r, g, b) (((r >> 3) << (6 + 5)) | ((g >> 2) << 5) | ((b >> 3) << (0)))
#define MAX_STRING_SIZE ( SCREEN_WIDTH / FONT_WIDTH + 1)
#define DISPLAY_TASK_PRIORITY   (configMAX_PRIORITIES - 2)
#define DISPLAY_TASK_CORE       0
//
static const uint16_t palette16[] =    // a gift who those who recognize this palette!
{
    LE2BE16(RGB(170, 170, 170)),
    LE2BE16(RGB(0, 0, 0)),
    LE2BE16(RGB(255, 255, 255)),
    LE2BE16(RGB(86, 119, 170)),
};
displayData_t displayData;
static uint16_t *lineBuffers[DISPLAY_MAX_QUEUED];
static TaskHandle_t displayTaskHandle;
static uint8_t penColor = 1;
static uint8_t penBackground = 0;
static uint8_t line = 0;

static uint8_t *getLine(int y)
{
    if (y < DRAW_BUFFER_HEIGHT)
    {
        return &displayData.displayFrameBuffer[0][y * SCREEN_WIDTH];
    }
    return (uint8_t*) aux_buffer + SCREEN_WIDTH * (y - DRAW_BUFFER_HEIGHT);
}

#if DISPLAY_STRETCH_TO_FULL_HEIGHT
// 320x200 is stretched to 320x240 (every 5th line repeated): the 4:3 aspect
// ratio Quake was designed for.
#define OUT_LINES(srcLines)     (((srcLines) * LCD_HEIGHT + SCREEN_HEIGHT - 1) / SCREEN_HEIGHT)
#define SRC_LINE(outLine)       ((outLine) * SCREEN_HEIGHT / LCD_HEIGHT)
#define OUT_FIRST_LINE          0
#else
#define OUT_LINES(srcLines)     (srcLines)
#define SRC_LINE(outLine)       (outLine)
#define OUT_FIRST_LINE          ((LCD_HEIGHT - SCREEN_HEIGHT) / 2)
#endif
#define OUT_LINES_PER_CHUNK     (PIXELS_PER_DMA_LINE / SCREEN_WIDTH)

static void displayTask(void *arg)
{
    (void) arg;
    while (1)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int srcLines = displayData.updateAll ? SCREEN_HEIGHT : DRAW_BUFFER_HEIGHT;
        int outLines = OUT_LINES(srcLines);
        bool overlay = touchOverlayVisible();
        displaySetWindow(OUT_FIRST_LINE, outLines);
        for (int outY = 0, k = 0; outY < outLines; outY += OUT_LINES_PER_CHUNK, k++)
        {
            uint16_t *buffer = lineBuffers[k % DISPLAY_MAX_QUEUED];
            if (k >= DISPLAY_MAX_QUEUED)
            {
                displayWaitOne();       // the buffer we are going to fill must have been sent
            }
            const uint16_t *pal = (const uint16_t*) displayData.pPalette;
            int n = outLines - outY < OUT_LINES_PER_CHUNK ? outLines - outY : OUT_LINES_PER_CHUNK;
            int lastSrc = 0;
            for (int l = 0; l < n; l++)
            {
                lastSrc = SRC_LINE(outY + l);
                const uint8_t *src = getLine(lastSrc);
                uint16_t *dst = buffer + l * SCREEN_WIDTH;
                for (int i = 0; i < SCREEN_WIDTH; i += 4)
                {
                    uint32_t p = *(const uint32_t*) &src[i];
                    dst[i] = pal[p & 0xFF];
                    dst[i + 1] = pal[(p >> 8) & 0xFF];
                    dst[i + 2] = pal[(p >> 16) & 0xFF];
                    dst[i + 3] = pal[p >> 24];
                }
                if (overlay && outY + l < OUT_LINES(DRAW_BUFFER_HEIGHT))    // status bar lines are not always refreshed
                {
                    touchDrawOverlay(dst, OUT_FIRST_LINE + outY + l);
                }
            }
            // source lines up to lastSrc consumed: the renderer can overwrite them
            displayData.displayDmaLineBuffersSent = (lastSrc + 1) / (PIXELS_PER_DMA_LINE / SCREEN_WIDTH);
            displaySendPixels(buffer, n * SCREEN_WIDTH, outY + n >= outLines);
        }
        displayWaitAll();
        displayData.displayDmaLineBuffersSent = NUMBER_OF_DMA_LINES + 1;
        displayData.dmaBusy = 0;
    }
}

void initGraphics(void)
{
    memset(&displayData, 0, sizeof(displayData));
    displayData.pPalette = (uint16_t*) palette16;
    displayData.displayDmaLineBuffersSent = NUMBER_OF_DMA_LINES + 1;
    for (int i = 0; i < DISPLAY_MAX_QUEUED; i++)
    {
        lineBuffers[i] = heap_caps_malloc(PIXELS_PER_DMA_LINE * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        assert(lineBuffers[i]);
    }
    // stack in PSRAM: internal RAM is scarce, and this task never writes to flash
    xTaskCreatePinnedToCoreWithCaps(displayTask, "display", 3072, NULL, DISPLAY_TASK_PRIORITY, &displayTaskHandle, DISPLAY_TASK_CORE, MALLOC_CAP_SPIRAM);
    setDisplayPen(1, 0);
}

void startDisplayRefresh(uint8_t bufferNumber, int updateAll)
{
    (void) bufferNumber;
    if (!displayIsReady())
    {
        return;
    }
    while (displayData.dmaBusy)
    {
    }
    displayData.updateAll = updateAll;
    displayData.displayDmaLineBuffersSent = 0;
    displayData.dmaBusy = 1;
    xTaskNotifyGive(displayTaskHandle);
}

static void setPixel(unsigned int x, unsigned int y, int c)
{
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT)
    {
        return;
    }
    getLine(y)[x] = c;
}

static void displayPutChar(char c, int x, int y)
{
    for (int cy = 0; cy < FONT_HEIGHT; cy++)
    {
        uint8_t fb = font8x8_basic[0x7F & c][cy];
        for (int cx = 0; cx < FONT_WIDTH; cx++)
        {
            setPixel(x + cx, y + cy, (fb & 1) ? penColor : penBackground);
            fb >>= 1;
        }
    }
}

void setDisplayPen(int color, int background)
{
    penColor = color;
    penBackground = background;
}

static void displayVPrintf(int x, int y, const char *format, va_list va)
{
    char outString[MAX_STRING_SIZE];
    vsnprintf(outString, MAX_STRING_SIZE, format, va);
    for (int i = 0; i < MAX_STRING_SIZE && x < SCREEN_WIDTH && outString[i] > 0; i++)
    {
        displayPutChar(outString[i], x, y);
        x += FONT_WIDTH;
    }
}

void displayPrintf(int x, int y, const char *format, ...)
{
    va_list va;
    va_start(va, format);
    displayVPrintf(x, y, format, va);
    va_end(va);
}

void displayPrintln(bool update, const char *format, ...)
{
    int y;
    while (displayData.dmaBusy)
    {
    }
    if (line < SCREEN_HEIGHT / FONT_HEIGHT)
    {
        y = line * FONT_HEIGHT;
        line++;
    }
    else
    {
        // scroll up
        y = SCREEN_HEIGHT - FONT_HEIGHT;
        for (int yy = FONT_HEIGHT; yy < SCREEN_HEIGHT; yy++)
        {
            memcpy(getLine(yy - FONT_HEIGHT), getLine(yy), SCREEN_WIDTH);
        }
        for (int yy = y; yy < SCREEN_HEIGHT; yy++)
        {
            memset(getLine(yy), penBackground, SCREEN_WIDTH);
        }
    }
    va_list va;
    va_start(va, format);
    displayVPrintf(0, y, format, va);
    va_end(va);
    // echo to the serial console as well
    va_start(va, format);
    vprintf(format, va);
    va_end(va);
    printf("\n");
    if (update)
    {
        startDisplayRefresh(displayData.workingBuffer, 1);
        while (displayData.dmaBusy)
        {
        }
    }
}
