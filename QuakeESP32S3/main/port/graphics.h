/**
 * @file graphics.h
 * @brief ESP32-S3 display refresh interface (same API as the MG24 one).
 *
 * The 8-bit frame buffer (320 x 152 3D view, the status bar area lives in the
 * tail of the z-buffer) is converted through the palette to RGB565 and sent by
 * a display task running on the other core. As on the MG24, the renderer may
 * start drawing the next frame while the previous one is being sent:
 * displayDmaLineBuffersSent tells how many PIXELS_PER_DMA_LINE chunks have
 * already been consumed.
 */
#ifndef SRC_GRAPHICS_H_
#define SRC_GRAPHICS_H_
#include <stdbool.h>
#include <stdint.h>
#include "display.h"
#include "quakedef.h"
#define PIXELS_PER_DMA_LINE         (SCREEN_WIDTH * 4)      // 4 display lines per chunk
#define NUMBER_OF_DMA_LINES         (SCREEN_WIDTH * SCREEN_HEIGHT / PIXELS_PER_DMA_LINE)

typedef struct
{
    volatile uint16_t * pPalette;
    uint8_t displayFrameBuffer[1][SCREEN_WIDTH * DRAW_BUFFER_HEIGHT];
    volatile uint16_t displayDmaLineBuffersSent; // how many chunks have been consumed
    uint8_t workingBuffer;
    uint8_t displayMode;
    volatile uint8_t updateAll;
    volatile uint8_t dmaBusy;
} displayData_t;
extern displayData_t displayData;
void displayPrintf(int x, int y, const char * format, ...);
void startDisplayRefresh(uint8_t bufferNumber, int updateAll);
void setDisplayPen(int color, int background);
void displayPrintln(bool update, const char * format, ...);
void initGraphics(void);
static inline void waitForDisplayDMA(unsigned int y)
{
    if (y >= SCREEN_HEIGHT)
        return;
    while ((y + 1) * SCREEN_WIDTH >= displayData.displayDmaLineBuffersSent * PIXELS_PER_DMA_LINE);
}
#endif /* SRC_GRAPHICS_H_ */
