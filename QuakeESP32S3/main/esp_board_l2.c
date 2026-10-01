/**
 * @file esp_board_l2.c
 * @brief Seeed Wio Tracker L2 bring-up: PCA9555 GPIO expander, LP5814
 * backlight driver and ES8311 codec, all on the I2C bus (SDA 47, SCL 48).
 *
 * Expander pins (linear index = 8 * port + bit) and the power-up sequence
 * follow the Meshtastic firmware for this board.
 * Other boards get empty implementations.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#include "esp_board.h"
#include "board_config.h"

#if BOARD == BOARD_WIO_TRACKER_L2
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "display.h"

#define I2C_FREQ_HZ             100000
#define PCA9555_ADDR            0x21
#define LP5814_ADDR             0x2C
#define ES8311_ADDR             0x18
#define GT911_ADDR              0x5D        // selected by holding INT low during reset
#define GT911_ADDR_ALT          0x14
#define GT911_I2C_FREQ_HZ       400000
// GT911 registers (16 bit addresses)
#define GT911_REG_CONFIG_XMAX   0x8048
#define GT911_REG_PRODUCT_ID    0x8140
#define GT911_REG_STATUS        0x814E
#define GT911_REG_POINTS        0x814F
#define GT911_POINT_SIZE        8
// PCA9555 registers
#define PCA_INPUT0              0x00
#define PCA_OUTPUT0             0x02
#define PCA_CONFIG0             0x06
// expander pins
#define EXP_WAKE_BUTTON         0
#define EXP_I2C_INT             1
#define EXP_SD_DETECT           2
#define EXP_TP_INT              3
#define EXP_LCD_CS              4
#define EXP_LCD_PWR_EN          5
#define EXP_LCD_RST             6
#define EXP_GROVE_PWR_EN        7
#define EXP_TP_RST              8
#define EXP_GNSS_RST            9
#define EXP_LED_USER            10
#define EXP_OTG_EN              11
#define EXP_PA_PWR_EN           12
#define EXP_GNSS_PWR_EN         13
#define EXP_SD_PWR_EN           14
#define EXP_BAT_ADC_EN          15
#define BUTTON_POLL_MS          20

static const char *TAG = "board";
static i2c_master_bus_handle_t i2cBus;
static i2c_master_dev_handle_t pca, lp5814, es8311, gt911;
static int touchMaxX, touchMaxY;        // touch controller resolution (native orientation)
static uint16_t pcaOutput = 0xFFFF;     // power-on state: all outputs high
static uint16_t pcaConfig = 0xFFFF;     // power-on state: all inputs
static volatile bool wakePressed;
static bool ready;

static esp_err_t writeReg(i2c_master_dev_handle_t dev, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { reg, value };
    return i2c_master_transmit(dev, buf, 2, 50);
}

static esp_err_t pcaWrite16(uint8_t reg, uint16_t value)
{
    uint8_t buf[3] = { reg, value & 0xFF, value >> 8 };
    return i2c_master_transmit(pca, buf, 3, 50);
}

static void expanderOutput(int pin, int level)
{
    // preload the output latch, then make the pin an output
    if (level)
        pcaOutput |= 1 << pin;
    else
        pcaOutput &= ~(1 << pin);
    pcaWrite16(PCA_OUTPUT0, pcaOutput);
    if (pcaConfig & (1 << pin))
    {
        pcaConfig &= ~(1 << pin);
        pcaWrite16(PCA_CONFIG0, pcaConfig);
    }
}

static void expanderInput(int pin)
{
    pcaConfig |= 1 << pin;
    pcaWrite16(PCA_CONFIG0, pcaConfig);
}

static void buttonTask(void *arg)
{
    while (1)
    {
        uint8_t reg = PCA_INPUT0, in[2];
        if (i2c_master_transmit_receive(pca, &reg, 1, in, 2, 50) == ESP_OK)
        {
            wakePressed = !(in[0] & (1 << EXP_WAKE_BUTTON));     // active low
        }
        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_MS));
    }
}

static i2c_master_dev_handle_t addDeviceFreq(uint8_t address, uint32_t freq)
{
    i2c_device_config_t cfg =
    {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = freq,
    };
    i2c_master_dev_handle_t dev = NULL;
    i2c_master_bus_add_device(i2cBus, &cfg, &dev);
    return dev;
}

static i2c_master_dev_handle_t addDevice(uint8_t address)
{
    return addDeviceFreq(address, I2C_FREQ_HZ);
}

static esp_err_t gt911Read(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t addr[2] = { reg >> 8, reg & 0xFF };
    return i2c_master_transmit_receive(gt911, addr, 2, data, len, 50);
}

static esp_err_t gt911Write8(uint16_t reg, uint8_t value)
{
    uint8_t buf[3] = { reg >> 8, reg & 0xFF, value };
    return i2c_master_transmit(gt911, buf, 3, 50);
}

static void touchInit(void)
{
    // reset with INT low: selects address 0x5D. INT is then released (input).
    expanderOutput(EXP_TP_INT, 0);
    expanderOutput(EXP_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    expanderOutput(EXP_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    expanderInput(EXP_TP_INT);
    vTaskDelay(pdMS_TO_TICKS(60));
    uint8_t address = GT911_ADDR;
    if (i2c_master_probe(i2cBus, address, 50) != ESP_OK)
    {
        address = GT911_ADDR_ALT;
        if (i2c_master_probe(i2cBus, address, 50) != ESP_OK)
        {
            ESP_LOGE(TAG, "GT911 touch controller not found");
            return;
        }
    }
    gt911 = addDeviceFreq(address, GT911_I2C_FREQ_HZ);
    char id[5] = { 0 };
    uint8_t res[4] = { 0 };
    gt911Read(GT911_REG_PRODUCT_ID, (uint8_t *) id, 4);
    gt911Read(GT911_REG_CONFIG_XMAX, res, 4);
    touchMaxX = res[0] | (res[1] << 8);
    touchMaxY = res[2] | (res[3] << 8);
    if (touchMaxX <= 0 || touchMaxY <= 0)
    {
        touchMaxX = TOUCH_NATIVE_WIDTH;
        touchMaxY = TOUCH_NATIVE_HEIGHT;
    }
    ESP_LOGI(TAG, "Touch controller GT%s at 0x%02x, %dx%d", id, address, touchMaxX, touchMaxY);
}

void boardInit(void)
{
    i2c_master_bus_config_t busCfg =
    {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&busCfg, &i2cBus));
    pca = addDevice(PCA9555_ADDR);
    lp5814 = addDevice(LP5814_ADDR);
    es8311 = addDevice(ES8311_ADDR);
    if (i2c_master_probe(i2cBus, PCA9555_ADDR, 50) != ESP_OK)
    {
        ESP_LOGE(TAG, "PCA9555 expander not found");
        return;
    }
    // peripherals we do not use: off
    expanderOutput(EXP_OTG_EN, 0);
    expanderOutput(EXP_PA_PWR_EN, 0);           // enabled once the codec is configured
    expanderOutput(EXP_GNSS_PWR_EN, 0);
    expanderOutput(EXP_GNSS_RST, 1);
    expanderOutput(EXP_BAT_ADC_EN, 0);
    expanderOutput(EXP_GROVE_PWR_EN, 0);
    expanderOutput(EXP_LED_USER, 0);
    expanderOutput(EXP_TP_INT, 0);
    expanderOutput(EXP_TP_RST, 0);              // touch controller in reset (see touchInit())
    // SD card power
    expanderOutput(EXP_SD_PWR_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    // display power and reset
    expanderOutput(EXP_LCD_PWR_EN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    expanderOutput(EXP_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(5));
    expanderOutput(EXP_LCD_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    expanderOutput(EXP_LCD_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    expanderOutput(EXP_LCD_CS, 1);
    touchInit();
    ready = true;
    xTaskCreatePinnedToCore(buttonTask, "buttons", 2560, NULL, 3, NULL, 0);
}

void boardBacklightOn(void)
{
    if (!ready || i2c_master_probe(i2cBus, LP5814_ADDR, 50) != ESP_OK)
    {
        ESP_LOGE(TAG, "LP5814 backlight driver not found");
        return;
    }
    writeReg(lp5814, 0x00, 0x01);           // chip enable
    writeReg(lp5814, 0x01, 0x01);           // 51 mA max current
    writeReg(lp5814, 0x02, 0x00);           // outputs off while configuring
    writeReg(lp5814, 0x04, 0x4E);           // dimming mode
    writeReg(lp5814, 0x05, 0xF0);           // engine mode
    for (int i = 0; i < 4; i++)
    {
        writeReg(lp5814, 0x14 + i, 200);    // DC current
    }
    writeReg(lp5814, 0x02, 0x0F);           // enable the 4 channels
    writeReg(lp5814, 0x0F, 0x55);           // latch
    vTaskDelay(pdMS_TO_TICKS(5));
    for (int i = 0; i < 4; i++)
    {
        writeReg(lp5814, 0x18 + i, 200);    // PWM (brightness)
    }
}

void boardAudioCodecInit(void)
{
    if (!ready || i2c_master_probe(i2cBus, ES8311_ADDR, 50) != ESP_OK)
    {
        ESP_LOGE(TAG, "ES8311 codec not found");
        return;
    }
    // ES8311 as I2S slave, MCLK = 256 * 11025 Hz from the MCLK pin, 16 bit Philips I2S, DAC only.
    // Register values from the ESP-ADF driver (coefficients for 2822400 Hz / 11025 Hz).
    static const uint8_t init[][2] =
    {
        { 0x44, 0x08 }, { 0x44, 0x08 },     // I2C noise immunity (written twice)
        { 0x01, 0x30 }, { 0x02, 0x00 }, { 0x03, 0x10 }, { 0x16, 0x24 }, { 0x04, 0x10 }, { 0x05, 0x00 },
        { 0x0B, 0x00 }, { 0x0C, 0x00 }, { 0x10, 0x1F }, { 0x11, 0x7F },
        { 0x00, 0x80 },                     // slave mode
        { 0x01, 0x3F },                     // clocks on, MCLK from pin, not inverted
        { 0x02, 0x00 },                     // pre_div 1, pre_multi 1
        { 0x05, 0x00 },                     // adc_div 1, dac_div 1
        { 0x03, 0x10 },                     // single speed, adc_osr
        { 0x04, 0x20 },                     // dac_osr
        { 0x07, 0x00 }, { 0x08, 0xFF },     // LRCK divider
        { 0x06, 0x03 },                     // BCLK divider 4, not inverted
        { 0x13, 0x10 }, { 0x1B, 0x0A }, { 0x1C, 0x6A },
        { 0x09, 0x0C },                     // DAC serial port: I2S, 16 bit, enabled
        { 0x0A, 0x4C },                     // ADC serial port: disabled
        { 0x17, 0xBF }, { 0x0E, 0x02 }, { 0x12, 0x00 }, { 0x14, 0x1A }, { 0x0D, 0x01 }, { 0x15, 0x40 },
        { 0x37, 0x08 }, { 0x45, 0x00 }, { 0x44, 0x58 },
        { 0x32, 0xA3 },                     // DAC volume -14 dB (0.5 dB steps, 0xBF = 0 dB)
        { 0x31, 0x00 },                     // DAC unmute
    };
    int failures = 0;
    for (size_t i = 0; i < sizeof(init) / sizeof(init[0]); i++)
    {
        esp_err_t err = ESP_FAIL;
        for (int retry = 0; retry < 3 && err != ESP_OK; retry++)
        {
            err = writeReg(es8311, init[i][0], init[i][1]);
        }
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "ES8311 write reg 0x%02x failed: %s", init[i][0], esp_err_to_name(err));
            failures++;
        }
    }
    if (failures)
    {
        ESP_LOGE(TAG, "ES8311 configuration: %d failed writes", failures);
    }
    expanderOutput(EXP_PA_PWR_EN, 1);       // speaker amplifier on
}

bool boardWakeButtonPressed(void)
{
    return wakePressed;
}

bool boardHasTouch(void)
{
    return gt911 != NULL;
}

int boardTouchRead(boardTouchPoint_t *points)
{
    uint8_t status;
    if (!gt911 || gt911Read(GT911_REG_STATUS, &status, 1) != ESP_OK || !(status & 0x80))
    {
        return -1;      // no new data
    }
    int n = status & 0x0F;
    if (n > BOARD_TOUCH_MAX_POINTS)
        n = BOARD_TOUCH_MAX_POINTS;
    uint8_t buf[BOARD_TOUCH_MAX_POINTS * GT911_POINT_SIZE];
    if (n && gt911Read(GT911_REG_POINTS, buf, n * GT911_POINT_SIZE) != ESP_OK)
    {
        n = -1;
    }
    gt911Write8(GT911_REG_STATUS, 0);
    for (int i = 0; i < n; i++)
    {
        const uint8_t *p = &buf[i * GT911_POINT_SIZE];
        int rx = p[1] | (p[2] << 8);
        int ry = p[3] | (p[4] << 8);
        // native (portrait) -> LCD landscape coordinates
#if TOUCH_SWAP_XY
        int x = ry * LCD_WIDTH / touchMaxY;
        int y = rx * LCD_HEIGHT / touchMaxX;
#else
        int x = rx * LCD_WIDTH / touchMaxX;
        int y = ry * LCD_HEIGHT / touchMaxY;
#endif
#if TOUCH_INVERT_X
        x = LCD_WIDTH - 1 - x;
#endif
#if TOUCH_INVERT_Y
        y = LCD_HEIGHT - 1 - y;
#endif
        points[i].id = p[0];
        points[i].x = x < 0 ? 0 : (x >= LCD_WIDTH ? LCD_WIDTH - 1 : x);
        points[i].y = y < 0 ? 0 : (y >= LCD_HEIGHT ? LCD_HEIGHT - 1 : y);
#if TOUCH_DEBUG
        ESP_LOGI(TAG, "touch id %d raw %d,%d -> %d,%d", p[0], rx, ry, points[i].x, points[i].y);
#endif
    }
    return n;
}

#elif BOARD == BOARD_GENERIC
void boardInit(void)
{
}

void boardBacklightOn(void)
{
}

void boardAudioCodecInit(void)
{
}

bool boardWakeButtonPressed(void)
{
    return false;
}

bool boardHasTouch(void)
{
    return false;
}

int boardTouchRead(boardTouchPoint_t *points)
{
    (void) points;
    return -1;
}
#endif
