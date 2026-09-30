/**
 * @file esp_display.c
 * @brief LCD driver for 320x240 panels (ESP32-S3 Quake port):
 * - ST7789 / ILI9341 on 4-wire SPI (D/C pin),
 * - NV3031B on QSPI (Wio Tracker L2): commands are sent as the single line
 *   frame 0x02 0x00 <cmd> 0x00 followed by the parameters, pixel data as
 *   0x32 0x00 0x2C 0x00 followed by quad data, with CS held low.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "display.h"

#define CMD_SWRESET     0x01
#define CMD_SLPOUT      0x11
#define CMD_NORON       0x13
#define CMD_INVOFF      0x20
#define CMD_INVON       0x21
#define CMD_DISPON      0x29
#define CMD_CASET       0x2A
#define CMD_RASET       0x2B
#define CMD_RAMWR       0x2C
#define CMD_MADCTL      0x36
#define CMD_COLMOD      0x3A
#define CMD_DELAY       0x80        // flag in the argument count: a delay (ms) follows the arguments
#define CMD_END         0xFF
#define PIN_BIT(p)      ((p) >= 0 ? 1ULL << ((p) & 63) : 0)    // unused pins are -1
#define IS_QSPI         (DISPLAY_CONTROLLER == DISPLAY_CONTROLLER_NV3031B)
#define QSPI_CMD_WRITE_REG      0x02
#define QSPI_CMD_WRITE_PIXELS   0x32

static const char *TAG = "display";
static spi_device_handle_t spi;
static spi_transaction_ext_t queuedTrans[DISPLAY_MAX_QUEUED];
static int queuedHead, queuedCount;
static bool ready;
static bool firstPixels;            // QSPI: next pixel transfer must send the write header
static bool busAcquired;

bool displayIsReady(void)
{
    return ready;
}

#if !IS_QSPI
static void spiPreTransferCallback(spi_transaction_t *t)
{
    gpio_set_level(DISPLAY_PIN_DC, (int) t->user);
}
#endif

static void sendCommand(uint8_t cmd, const uint8_t *data, int len)
{
    spi_transaction_t t = { 0 };
#if IS_QSPI
    t.cmd = QSPI_CMD_WRITE_REG;
    t.addr = cmd << 8;
    t.length = len * 8;
    if (len <= 4)
    {
        t.flags = len ? SPI_TRANS_USE_TXDATA : 0;
        if (len)
        {
            memcpy(t.tx_data, data, len);
        }
    }
    else
    {
        t.tx_buffer = data;
    }
    spi_device_polling_transmit(spi, &t);
#else
    t.length = 8;
    t.flags = SPI_TRANS_USE_TXDATA;
    t.tx_data[0] = cmd;
    t.user = (void*) 0;
    spi_device_polling_transmit(spi, &t);
    if (len)
    {
        memset(&t, 0, sizeof(t));
        t.length = len * 8;
        if (len <= 4)
        {
            t.flags = SPI_TRANS_USE_TXDATA;
            memcpy(t.tx_data, data, len);
        }
        else
        {
            t.tx_buffer = data;
        }
        t.user = (void*) 1;
        spi_device_polling_transmit(spi, &t);
    }
#endif
}

static void executeCommands(const uint8_t *cmds)
{
    while (*cmds != CMD_END)
    {
        uint8_t cmd = *cmds++;
        uint8_t numArgs = *cmds++;
        int hasDelay = numArgs & CMD_DELAY;
        numArgs &= ~CMD_DELAY;
        sendCommand(cmd, cmds, numArgs);
        cmds += numArgs;
        if (hasDelay)
        {
            vTaskDelay(pdMS_TO_TICKS(*cmds++));
        }
    }
}

#if DISPLAY_CONTROLLER == DISPLAY_CONTROLLER_NV3031B
// From the LovyanGFX Panel_NV3031B driver
static const uint8_t initCommands[] =
{
    0xFD, 2, 0x06, 0x08,                             // unlock factory registers
    0x60, 1, 0x0C,
    0x61, 2, 0x07, 0x04,
    0xB4, 1, 0x01,
    0xB1, 3, 0x0F, 0x02, 0x03,
    0xB5, 4, 0x02, 0x02, 0x0A, 0x14,
    0xB6, 5, 0x44, 0x01, 0x9F, 0x00, 0x02,
    0xDF, 1, 0x11,
    0x67, 1, 0x21,
    0x68, 4, 0x90, 0x4F, 0x27, 0x21,
    0xE1, 2, 0x20, 0x69,
    0xE4, 2, 0x69, 0x20,
    0xE2, 6, 0x10, 0x12, 0x12, 0x30, 0x39, 0x3F,
    0xE5, 6, 0x3F, 0x33, 0x2D, 0x12, 0x12, 0x10,
    0xE0, 8, 0x06, 0x06, 0x0B, 0x12, 0x11, 0x11, 0x0E, 0x19,
    0xE3, 8, 0x19, 0x13, 0x14, 0x14, 0x14, 0x12, 0x08, 0x05,
    0xE6, 2, 0x00, 0xFF,
    0xE7, 6, 0x01, 0x04, 0x03, 0x03, 0x00, 0x12,
    0xE8, 3, 0x00, 0x70, 0x00,
    0xEC, 1, 0x54,
    0xFD, 2, 0xFA, 0xFC,                             // lock factory registers
    CMD_COLMOD, 1, 0x55,                             // 16 bit color
    CMD_SLPOUT, CMD_DELAY, 120,
    CMD_MADCTL, 1, DISPLAY_MADCTL,
    DISPLAY_INVERT_COLORS ? CMD_INVON : CMD_INVOFF, 0,
    CMD_DISPON, 1, 0x00,                             // NV303x needs a dummy parameter
    CMD_END
};
#elif DISPLAY_CONTROLLER == DISPLAY_CONTROLLER_ILI9341
static const uint8_t initCommands[] =
{
    CMD_SWRESET, CMD_DELAY, 150,
    0xCF, 3, 0x00, 0xC1, 0x30,                  // power control B
    0xED, 4, 0x64, 0x03, 0x12, 0x81,            // power on sequence
    0xE8, 3, 0x85, 0x00, 0x78,                  // driver timing A
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,      // power control A
    0xF7, 1, 0x20,                              // pump ratio
    0xEA, 2, 0x00, 0x00,                        // driver timing B
    0xC0, 1, 0x23,                              // power control 1
    0xC1, 1, 0x10,                              // power control 2
    0xC5, 2, 0x3E, 0x28,                        // VCOM control 1
    0xC7, 1, 0x86,                              // VCOM control 2
    CMD_MADCTL, 1, DISPLAY_MADCTL,
    CMD_COLMOD, 1, 0x55,                        // 16 bit color
    0xB1, 2, 0x00, 0x18,                        // frame rate 79 Hz
    0xB6, 3, 0x08, 0x82, 0x27,                  // display function control
    0xF2, 1, 0x00,                              // 3 gamma off
    0x26, 1, 0x01,                              // gamma curve
    DISPLAY_INVERT_COLORS ? CMD_INVON : CMD_INVOFF, 0,
    CMD_SLPOUT, CMD_DELAY, 120,
    CMD_DISPON, CMD_DELAY, 10,
    CMD_END
};
#else
static const uint8_t initCommands[] =
{
    CMD_SWRESET, CMD_DELAY, 150,
    CMD_SLPOUT, CMD_DELAY, 120,
    CMD_COLMOD, 1 | CMD_DELAY, 0x55, 10,        // 16 bit color
    CMD_MADCTL, 1, DISPLAY_MADCTL,
    DISPLAY_INVERT_COLORS ? CMD_INVON : CMD_INVOFF, CMD_DELAY, 10,
    CMD_NORON, CMD_DELAY, 10,
    CMD_DISPON, CMD_DELAY, 10,
    CMD_END
};
#endif

void displaySetWindow(int firstLine, int numLines)
{
    uint16_t x0 = DISPLAY_X_OFFSET, x1 = DISPLAY_X_OFFSET + LCD_WIDTH - 1;
    uint16_t y0 = DISPLAY_Y_OFFSET + firstLine, y1 = y0 + numLines - 1;
    uint8_t caset[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t raset[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    sendCommand(CMD_CASET, caset, 4);
    sendCommand(CMD_RASET, raset, 4);
#if IS_QSPI
    // the pixel transfers keep CS low across several transactions: lock the bus
    spi_device_acquire_bus(spi, portMAX_DELAY);
    busAcquired = true;
    firstPixels = true;
#else
    sendCommand(CMD_RAMWR, NULL, 0);
#endif
}

void displaySendPixels(const uint16_t *data, size_t numPixels, bool last)
{
    if (queuedCount == DISPLAY_MAX_QUEUED)
    {
        displayWaitOne();
    }
    spi_transaction_ext_t *t = &queuedTrans[(queuedHead + queuedCount) % DISPLAY_MAX_QUEUED];
    memset(t, 0, sizeof(*t));
    t->base.length = numPixels * 16;
    t->base.tx_buffer = data;
#if IS_QSPI
    t->base.flags = SPI_TRANS_MODE_QIO;
    if (!last)
    {
        t->base.flags |= SPI_TRANS_CS_KEEP_ACTIVE;
    }
    if (firstPixels)
    {
        t->base.cmd = QSPI_CMD_WRITE_PIXELS;
        t->base.addr = CMD_RAMWR << 8;
        firstPixels = false;
    }
    else
    {
        // continuation: no command/address phase
        t->base.flags |= SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR;
        t->command_bits = 0;
        t->address_bits = 0;
    }
#else
    (void) last;
    t->base.user = (void*) 1;
#endif
    spi_device_queue_trans(spi, &t->base, portMAX_DELAY);
    queuedCount++;
}

int displayWaitOne(void)
{
    if (!queuedCount)
    {
        return 0;
    }
    spi_transaction_t *t;
    spi_device_get_trans_result(spi, &t, portMAX_DELAY);
    queuedHead = (queuedHead + 1) % DISPLAY_MAX_QUEUED;
    queuedCount--;
    return 1;
}

void displayWaitAll(void)
{
    while (displayWaitOne());
    if (busAcquired)
    {
        spi_device_release_bus(spi);
        busAcquired = false;
    }
}

void displayInit(void)
{
    if (DISPLAY_PIN_MOSI < 0 || DISPLAY_PIN_SCLK < 0 || (!IS_QSPI && DISPLAY_PIN_DC < 0))
    {
        ESP_LOGE(TAG, "Display pins not configured, see board_config.h");
        return;
    }
    gpio_config_t io = { 0 };
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = PIN_BIT(DISPLAY_PIN_DC) | PIN_BIT(DISPLAY_PIN_RST) | PIN_BIT(DISPLAY_PIN_BACKLIGHT);
    if (io.pin_bit_mask)
    {
        gpio_config(&io);
    }
    //
    spi_bus_config_t bus = { 0 };
    bus.mosi_io_num = DISPLAY_PIN_MOSI;
    bus.sclk_io_num = DISPLAY_PIN_SCLK;
    bus.max_transfer_sz = LCD_WIDTH * 8 * sizeof(uint16_t);
#if IS_QSPI
    bus.miso_io_num = DISPLAY_PIN_IO1;
    bus.quadwp_io_num = DISPLAY_PIN_IO2;
    bus.quadhd_io_num = DISPLAY_PIN_IO3;
    bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
#else
    bus.miso_io_num = -1;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
#if SD_MODE == SD_MODE_SPI
    if (SD_SPI_HOST == DISPLAY_SPI_HOST)
    {
        // the SD card shares this bus (see mountSdCard())
        bus.miso_io_num = SD_PIN_MISO;
    }
#endif
#endif
    ESP_ERROR_CHECK(spi_bus_initialize(DISPLAY_SPI_HOST, &bus, SPI_DMA_CH_AUTO));
    spi_device_interface_config_t dev = { 0 };
    dev.clock_speed_hz = DISPLAY_SPI_CLOCK_HZ;
    dev.spics_io_num = DISPLAY_PIN_CS;
    dev.queue_size = DISPLAY_MAX_QUEUED;
#if IS_QSPI
    dev.mode = 3;
    dev.command_bits = 8;
    dev.address_bits = 24;
    dev.flags = SPI_DEVICE_HALFDUPLEX;
#else
    dev.mode = 0;
    dev.pre_cb = spiPreTransferCallback;
    dev.flags = SPI_DEVICE_NO_DUMMY;
#endif
    ESP_ERROR_CHECK(spi_bus_add_device(DISPLAY_SPI_HOST, &dev, &spi));
    //
    if (DISPLAY_PIN_RST >= 0)
    {
        gpio_set_level(DISPLAY_PIN_RST, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
        gpio_set_level(DISPLAY_PIN_RST, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }
#if IS_QSPI
    sendCommand(0x00, NULL, 0);     // NOP frame to flush the QSPI interface
#endif
    executeCommands(initCommands);
    // clear the whole panel
    uint16_t *black = heap_caps_calloc(LCD_WIDTH * 8, sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (black)
    {
        displaySetWindow(0, LCD_HEIGHT);
        for (int y = 0; y < LCD_HEIGHT; y += 8)
        {
            displaySendPixels(black, LCD_WIDTH * 8, y + 8 >= LCD_HEIGHT);
        }
        displayWaitAll();
        free(black);
    }
    if (DISPLAY_PIN_BACKLIGHT >= 0)
    {
        gpio_set_level(DISPLAY_PIN_BACKLIGHT, DISPLAY_BACKLIGHT_ON_LEVEL);
    }
    ready = true;
}
