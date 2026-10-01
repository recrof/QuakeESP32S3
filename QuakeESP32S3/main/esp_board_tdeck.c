/**
 * @file esp_board_tdeck.c
 * @brief LilyGO T-Deck bring-up and input: peripheral power, keyboard (ESP32-C3
 * controller at I2C address 0x55) and trackball.
 *
 * The keyboard firmware is switched to raw mode (command 0x03, keyboard firmware
 * from June 2025 on), where reading 5 bytes returns the key matrix (one byte per
 * column, one bit per row): keys can then be held, e.g. WASD to move. Older
 * keyboard firmware only reports one character per key press: keys are then
 * released automatically shortly after.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#include "esp_board.h"
#include "board_config.h"

#if BOARD == BOARD_LILYGO_TDECK
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "quakedef.h"
#include "esp_input.h"

#define I2C_FREQ_HZ             100000
#define KEYBOARD_ADDR           0x55
#define KEYBOARD_CMD_RAW_MODE   0x03
#define KEYBOARD_COLS           5
#define KEYBOARD_ROWS           7
#define KEYBOARD_POLL_MS        15
#define KEYBOARD_TAP_MS         120     // legacy firmware: key release delay
#define TRACKBALL_LOOK_PIXELS   6.0f    // look movement per trackball edge (touch pixel units)
#define TRACKBALL_MENU_STEPS    2       // trackball edges per arrow key in menus

// special keys of the matrix
enum
{
    KB_NONE = 0, KB_SYM = 0x100, KB_ALT, KB_MIC, KB_SHIFT, KB_ENTER, KB_BACKSPACE, KB_SPEAKER
};
// key matrix [column][row], from the LilyGO keyboard firmware
static const uint16_t keyMap[KEYBOARD_COLS][KEYBOARD_ROWS] =
{
    { 'q', 'w', KB_SYM, 'a', KB_ALT, ' ', KB_MIC },
    { 'e', 's', 'd', 'p', 'x', 'z', KB_SHIFT },
    { 'r', 'g', 't', KB_SHIFT, 'v', 'c', 'f' },
    { 'u', 'h', 'y', KB_ENTER, 'b', 'n', 'j' },
    { 'o', 'l', 'i', KB_BACKSPACE, KB_SPEAKER, 'm', 'k' },
};
// characters with the symbol key held (0: none)
static const char symbolMap[KEYBOARD_COLS][KEYBOARD_ROWS] =
{
    { '#', '1', 0, '*', 0, 0, '0' },
    { '2', '4', '5', '@', '8', '7', 0 },
    { '3', '/', '(', 0, '?', '9', '6' },
    { '_', ':', ')', 0, '!', ',', ';' },
    { '+', '"', '-', 0, 0, '.', '\'' },
};

static const char *TAG = "tdeck";
static i2c_master_bus_handle_t i2cBus;
static i2c_master_dev_handle_t keyboard;
static bool rawMode;
static uint8_t sentKey[KEYBOARD_COLS][KEYBOARD_ROWS];     // key sent on press, to release the same key
static uint8_t oldMatrix[KEYBOARD_COLS];
static portMUX_TYPE trackballMux = portMUX_INITIALIZER_UNLOCKED;
static volatile int trackballX, trackballY;               // edges counted by the interrupts
static float lookDX, lookDY;

void boardInit(void)
{
    // peripherals power on, all the SPI chip selects high
    gpio_config_t io = { 0 };
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask = (1ULL << BOARD_PIN_POWER_ON) | (1ULL << BOARD_PIN_RADIO_CS) | (1ULL << SD_PIN_CS) | (1ULL << DISPLAY_PIN_CS);
    gpio_config(&io);
    gpio_set_level(BOARD_PIN_RADIO_CS, 1);
    gpio_set_level(SD_PIN_CS, 1);
    gpio_set_level(DISPLAY_PIN_CS, 1);
    gpio_set_level(BOARD_PIN_POWER_ON, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
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
    i2c_device_config_t devCfg =
    {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = KEYBOARD_ADDR,
        .scl_speed_hz = I2C_FREQ_HZ,
    };
    i2c_master_bus_add_device(i2cBus, &devCfg, &keyboard);
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

// ---------------------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------------------
// Quake key for a matrix key, chosen when it is pressed
static uint8_t gameKey(int col, int row, bool symHeld)
{
    bool inGame = key_dest == key_game;
    uint16_t k = keyMap[col][row];
    if (symHeld && symbolMap[col][row])
    {
        return symbolMap[col][row];         // digits select the weapons in game
    }
    switch (k)
    {
        case KB_SYM: return 0;
        case KB_ALT: return K_ESCAPE;       // menu
        case KB_MIC: return '`';            // console
        case KB_SHIFT: return K_SHIFT;      // run
        case KB_ENTER: return K_ENTER;
        case KB_BACKSPACE: return K_BACKSPACE;
        case KB_SPEAKER: return K_TAB;      // scores
        default: break;
    }
    if (inGame)
    {
        switch (k)
        {
            case 'w': return K_UPARROW;     // forward
            case 's': return K_DOWNARROW;   // back
            case 'a': return ',';           // strafe left
            case 'd': return '.';           // strafe right
            case 'q': return '/';           // next weapon
            case 'e': return K_CTRL;        // fire
            default: break;
        }
    }
    return (uint8_t) k;
}

static void handleRawMatrix(const uint8_t *matrix)
{
    bool symHeld = matrix[0] & (1 << 2);
    for (int c = 0; c < KEYBOARD_COLS; c++)
    {
        uint8_t changed = matrix[c] ^ oldMatrix[c];
        for (int r = 0; r < KEYBOARD_ROWS; r++)
        {
            if (!(changed & (1 << r)))
                continue;
            if (matrix[c] & (1 << r))
            {
                sentKey[c][r] = gameKey(c, r, symHeld);
                inputPushKey(sentKey[c][r], true);
            }
            else if (sentKey[c][r])
            {
                inputPushKey(sentKey[c][r], false);
                sentKey[c][r] = 0;
            }
        }
        oldMatrix[c] = matrix[c];
    }
}

// legacy firmware: one character per key press
static uint8_t legacyKey(uint8_t ch)
{
    switch (ch)
    {
        case 0x0D: return K_ENTER;
        case 0x08: return K_BACKSPACE;
        default: break;
    }
    if (key_dest == key_game)
    {
        switch (ch)
        {
            case 'w': return K_UPARROW;
            case 's': return K_DOWNARROW;
            case 'a': return ',';
            case 'd': return '.';
            case 'q': return '/';
            case 'e': return K_CTRL;
            default: break;
        }
    }
    if (ch >= 'A' && ch <= 'Z')
        return ch - 'A' + 'a';
    return ch;
}

static bool detectRawMode(void)
{
    uint8_t cmd = KEYBOARD_CMD_RAW_MODE;
    if (i2c_master_transmit(keyboard, &cmd, 1, 50) != ESP_OK)
        return false;
    vTaskDelay(pdMS_TO_TICKS(20));
    // in raw mode bit 7 is never set (7 rows); the old firmware sends one byte, then 0xFF
    for (int i = 0; i < 3; i++)
    {
        uint8_t m[KEYBOARD_COLS];
        if (i2c_master_receive(keyboard, m, KEYBOARD_COLS, 50) != ESP_OK)
            return false;
        for (int c = 0; c < KEYBOARD_COLS; c++)
        {
            if (m[c] & 0x80)
                return false;
        }
        vTaskDelay(pdMS_TO_TICKS(KEYBOARD_POLL_MS));
    }
    return true;
}

// ---------------------------------------------------------------------------
// Trackball
// ---------------------------------------------------------------------------
static void IRAM_ATTR trackballIsr(void *arg)
{
    int pin = (int) arg;
    portENTER_CRITICAL_ISR(&trackballMux);
    if (pin == TRACKBALL_PIN_UP)
        trackballY--;
    else if (pin == TRACKBALL_PIN_DOWN)
        trackballY++;
    else if (pin == TRACKBALL_PIN_LEFT)
        trackballX--;
    else
        trackballX++;
    portEXIT_CRITICAL_ISR(&trackballMux);
}

static void trackballInit(void)
{
    static const int pins[] = { TRACKBALL_PIN_UP, TRACKBALL_PIN_DOWN, TRACKBALL_PIN_LEFT, TRACKBALL_PIN_RIGHT };
    gpio_config_t io = { 0 };
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en = GPIO_PULLUP_ENABLE;
    io.intr_type = GPIO_INTR_ANYEDGE;
    for (int i = 0; i < 4; i++)
        io.pin_bit_mask |= 1ULL << pins[i];
    gpio_config(&io);
    gpio_install_isr_service(0);
    for (int i = 0; i < 4; i++)
        gpio_isr_handler_add(pins[i], trackballIsr, (void *) pins[i]);
}

static void trackballPoll(void)
{
    static int menuX, menuY;
    int dx, dy;
    portENTER_CRITICAL(&trackballMux);
    dx = trackballX;
    dy = trackballY;
    trackballX = trackballY = 0;
    portEXIT_CRITICAL(&trackballMux);
    if (key_dest == key_game)
    {
        menuX = menuY = 0;
        portENTER_CRITICAL(&trackballMux);
        lookDX += dx * TRACKBALL_LOOK_PIXELS;
        lookDY += dy * TRACKBALL_LOOK_PIXELS;
        portEXIT_CRITICAL(&trackballMux);
        return;
    }
    // menus: arrow keys
    menuX += dx;
    menuY += dy;
    while (menuY <= -TRACKBALL_MENU_STEPS || menuY >= TRACKBALL_MENU_STEPS)
    {
        int key = menuY < 0 ? K_UPARROW : K_DOWNARROW;
        inputPushKey(key, true);
        inputPushKey(key, false);
        menuY += menuY < 0 ? TRACKBALL_MENU_STEPS : -TRACKBALL_MENU_STEPS;
        menuX = 0;
    }
    while (menuX <= -TRACKBALL_MENU_STEPS || menuX >= TRACKBALL_MENU_STEPS)
    {
        int key = menuX < 0 ? K_LEFTARROW : K_RIGHTARROW;
        inputPushKey(key, true);
        inputPushKey(key, false);
        menuX += menuX < 0 ? TRACKBALL_MENU_STEPS : -TRACKBALL_MENU_STEPS;
    }
}

// ---------------------------------------------------------------------------
static void inputTask(void *arg)
{
    (void) arg;
    // the keyboard controller boots after the power on: wait for it
    for (int i = 0; i < 20 && i2c_master_probe(i2cBus, KEYBOARD_ADDR, 50) != ESP_OK; i++)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    rawMode = detectRawMode();
    ESP_LOGI(TAG, "Keyboard: %s", rawMode ? "raw mode (keys can be held)" :
        "character mode (old keyboard firmware: keys are released automatically)");
    uint8_t tapKey = 0;
    int tapTicks = 0;
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(KEYBOARD_POLL_MS));
        trackballPoll();
        if (rawMode)
        {
            uint8_t m[KEYBOARD_COLS];
            if (i2c_master_receive(keyboard, m, KEYBOARD_COLS, 50) == ESP_OK)
            {
                handleRawMatrix(m);
            }
            continue;
        }
        if (tapKey && --tapTicks <= 0)
        {
            inputPushKey(tapKey, false);
            tapKey = 0;
        }
        uint8_t ch;
        if (i2c_master_receive(keyboard, &ch, 1, 50) == ESP_OK && ch && ch != 0xFF)
        {
            if (tapKey)
            {
                inputPushKey(tapKey, false);
            }
            tapKey = legacyKey(ch);
            inputPushKey(tapKey, true);
            tapTicks = KEYBOARD_TAP_MS / KEYBOARD_POLL_MS;
        }
    }
}

void boardInputInit(void)
{
    trackballInit();
    // stack in PSRAM: this task never writes to flash
    xTaskCreatePinnedToCoreWithCaps(inputTask, "tdeck_input", 3072, NULL, 4, NULL, 0, MALLOC_CAP_SPIRAM);
}

void boardTakeLook(float *dx, float *dy)
{
    portENTER_CRITICAL(&trackballMux);
    *dx = lookDX;
    *dy = lookDY;
    lookDX = lookDY = 0;
    portEXIT_CRITICAL(&trackballMux);
}
#endif
