/**
 * @file board_config.h
 * @brief Hardware configuration for the ESP32-S3 Quake port.
 *
 * Select the board with BOARD. For a custom board, use BOARD_GENERIC and fill
 * in the GPIO numbers. A pin set to -1 is "not connected".
 * Pins to avoid on ESP32-S3 modules: GPIO26-32 (flash), GPIO33-37 (octal PSRAM,
 * on N8R8/N16R8 modules), GPIO19/20 (USB), GPIO0/3/45/46 (strapping).
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_GENERIC                   0
#define BOARD_WIO_TRACKER_L2            1       // Seeed Wio Tracker L2 / L2 Pro
#define BOARD_LILYGO_TDECK              2       // LilyGO T-Deck / T-Deck Plus
// Also selectable at build time: idf.py -B build-tdeck -DBOARD=BOARD_LILYGO_TDECK build
#ifndef BOARD
#define BOARD                           BOARD_WIO_TRACKER_L2
#endif

#define DISPLAY_CONTROLLER_ST7789       0       // SPI + D/C pin
#define DISPLAY_CONTROLLER_ILI9341      1       // SPI + D/C pin
#define DISPLAY_CONTROLLER_NV3031B      2       // QSPI, no D/C pin

#define SD_MODE_SPI                     0
#define SD_MODE_SDMMC_1BIT              1
#define SD_MODE_SDMMC_4BIT              2

#define AUDIO_OUTPUT_NONE               0
#define AUDIO_OUTPUT_I2S                1       // I2S DAC/amplifier (MAX98357A, PCM5102...)
#define AUDIO_OUTPUT_PDM                2       // one pin + RC filter / amplifier
#define AUDIO_OUTPUT_ES8311             3       // I2S + ES8311 codec configured through I2C

#if BOARD == BOARD_WIO_TRACKER_L2
// -----------------------------------------------------------------------------
// Seeed Wio Tracker L2: ESP32-S3 (16 MB flash, 8 MB octal PSRAM), 3.2" 320x240
// NV3031B QSPI IPS panel, microSD (1-bit SDMMC), ES8311 codec, PCA9555 GPIO
// expander and LP5814 backlight driver on I2C. See esp_board_l2.c.
// -----------------------------------------------------------------------------
#define DISPLAY_CONTROLLER              DISPLAY_CONTROLLER_NV3031B
#define DISPLAY_SPI_HOST                SPI3_HOST
#define DISPLAY_SPI_CLOCK_HZ            (40 * 1000 * 1000)
#define DISPLAY_PIN_SCLK                42
#define DISPLAY_PIN_MOSI                41      // QSPI IO0
#define DISPLAY_PIN_IO1                 40
#define DISPLAY_PIN_IO2                 39
#define DISPLAY_PIN_IO3                 38
#define DISPLAY_PIN_CS                  46
#define DISPLAY_PIN_DC                  -1
#define DISPLAY_PIN_RST                 -1      // on the GPIO expander
#define DISPLAY_PIN_BACKLIGHT           -1      // LP5814 on I2C
#define DISPLAY_BACKLIGHT_ON_LEVEL      1
#define DISPLAY_MADCTL                  0xA0    // landscape (MY | MV), RGB
#define DISPLAY_INVERT_COLORS           0
#define DISPLAY_X_OFFSET                0
#define DISPLAY_Y_OFFSET                0

#define SD_MODE                         SD_MODE_SDMMC_1BIT
#define SD_SDMMC_CLOCK_KHZ              40000
#define SD_SPI_HOST                     SPI2_HOST   // unused
#define SD_SPI_CLOCK_KHZ                20000       // unused
#define SD_PIN_MOSI                     3       // CMD
#define SD_PIN_MISO                     1       // D0
#define SD_PIN_SCLK                     2       // CLK
#define SD_PIN_CS                       -1
#define SD_PIN_D1                       -1
#define SD_PIN_D2                       -1
#define SD_PIN_D3                       -1

#define AUDIO_OUTPUT                    AUDIO_OUTPUT_ES8311
#define AUDIO_PIN_MCLK                  10
#define AUDIO_PIN_BCLK                  11
#define AUDIO_PIN_WS                    12
#define AUDIO_PIN_DOUT                  16
#define AUDIO_PDM_PIN_CLK               -1

#define BOARD_I2C_SDA                   47
#define BOARD_I2C_SCL                   48

// GT911 touch controller (I2C). Its native orientation is portrait: these map it
// to the landscape LCD coordinates.
#define TOUCH_NATIVE_WIDTH              240     // used if the controller does not report its resolution
#define TOUCH_NATIVE_HEIGHT             320
#define TOUCH_SWAP_XY                   1
#define TOUCH_INVERT_X                  1
#define TOUCH_INVERT_Y                  0
#define TOUCH_DEBUG                     0       // 1: log raw coordinates

// The only buttons: BOOT (GPIO0, direct) and WAKE (on the expander). They are
// handled in esp_board_l2.c; play with a Bluetooth LE gamepad or keyboard.
#define BUTTON_PIN_UP                   -1
#define BUTTON_PIN_DOWN                 -1
#define BUTTON_PIN_LEFT                 -1
#define BUTTON_PIN_RIGHT                -1
#define BUTTON_PIN_FIRE                 -1
#define BUTTON_PIN_JUMP                 -1
#define BUTTON_PIN_WEAPON               -1
#define BUTTON_PIN_MENU                 0       // BOOT button: menu / escape

#elif BOARD == BOARD_LILYGO_TDECK
// -----------------------------------------------------------------------------
// LilyGO T-Deck: ESP32-S3 (16 MB flash, 8 MB octal PSRAM), 2.8" 320x240 ST7789,
// microSD on the display SPI bus, MAX98357A I2S amplifier, ESP32-C3 keyboard
// controller (I2C) and trackball. See esp_board_tdeck.c.
// -----------------------------------------------------------------------------
#define DISPLAY_CONTROLLER              DISPLAY_CONTROLLER_ST7789
#define DISPLAY_SPI_HOST                SPI2_HOST
#define DISPLAY_SPI_CLOCK_HZ            (40 * 1000 * 1000)
#define DISPLAY_PIN_MOSI                41
#define DISPLAY_PIN_SCLK                40
#define DISPLAY_PIN_CS                  12
#define DISPLAY_PIN_DC                  11
#define DISPLAY_PIN_RST                 -1
#define DISPLAY_PIN_BACKLIGHT           42
#define DISPLAY_BACKLIGHT_ON_LEVEL      1
#define DISPLAY_MADCTL                  0x60    // landscape (MX | MV), RGB
#define DISPLAY_INVERT_COLORS           1
#define DISPLAY_X_OFFSET                0
#define DISPLAY_Y_OFFSET                0

// SD card on the display SPI bus (the LoRa radio is on it too, kept deselected)
#define SD_MODE                         SD_MODE_SPI
#define SD_SPI_HOST                     SPI2_HOST
#define SD_SPI_CLOCK_KHZ                40000   // 20000 is used if mounting fails
#define SD_SDMMC_CLOCK_KHZ              20000   // unused
#define SD_PIN_MOSI                     41
#define SD_PIN_MISO                     38
#define SD_PIN_SCLK                     40
#define SD_PIN_CS                       39
#define SD_PIN_D1                       -1
#define SD_PIN_D2                       -1
#define SD_PIN_D3                       -1

#define AUDIO_OUTPUT                    AUDIO_OUTPUT_I2S
#define AUDIO_PIN_MCLK                  -1
#define AUDIO_PIN_BCLK                  7
#define AUDIO_PIN_WS                    5
#define AUDIO_PIN_DOUT                  6
#define AUDIO_PDM_PIN_CLK               -1
#define AUDIO_SAMPLE_SHIFT              5       // the amplifier has no volume control: -18 dB

#define BOARD_I2C_SDA                   18
#define BOARD_I2C_SCL                   8
#define BOARD_PIN_POWER_ON              10      // peripherals power (keyboard, display, radio...)
#define BOARD_PIN_RADIO_CS              9
#define TRACKBALL_PIN_UP                3
#define TRACKBALL_PIN_DOWN              15
#define TRACKBALL_PIN_LEFT              1
#define TRACKBALL_PIN_RIGHT             2

// Trackball click (BOOT, GPIO0): fire in game, select in menus. The keyboard is
// handled in esp_board_tdeck.c.
#define BUTTON_PIN_UP                   -1
#define BUTTON_PIN_DOWN                 -1
#define BUTTON_PIN_LEFT                 -1
#define BUTTON_PIN_RIGHT                -1
#define BUTTON_PIN_FIRE                 0
#define BUTTON_PIN_JUMP                 -1
#define BUTTON_PIN_WEAPON               -1
#define BUTTON_PIN_MENU                 -1

#else
// -----------------------------------------------------------------------------
// Generic board: 320x240 SPI panel (ST7789 or ILI9341), driven in landscape mode.
// Quake renders 320x200; the image is centered vertically.
// -----------------------------------------------------------------------------
#define DISPLAY_CONTROLLER              DISPLAY_CONTROLLER_ST7789
#define DISPLAY_SPI_HOST                SPI2_HOST
#define DISPLAY_SPI_CLOCK_HZ            (40 * 1000 * 1000)  // try 80 MHz if your panel/wiring allows it
#define DISPLAY_PIN_MOSI                -1      // TODO: set
#define DISPLAY_PIN_SCLK                -1      // TODO: set
#define DISPLAY_PIN_CS                  -1      // -1 if CS is tied low
#define DISPLAY_PIN_DC                  -1      // TODO: set
#define DISPLAY_PIN_RST                 -1      // -1 if not connected (software reset is used)
#define DISPLAY_PIN_BACKLIGHT           -1      // -1 if always on
#define DISPLAY_BACKLIGHT_ON_LEVEL      1
// Orientation / color tweaks. MADCTL is the memory access control register value
// for landscape (320 wide) mode. Common values: 0x60, 0xA0, 0x20, 0xE0 (add 0x08 for BGR panels).
#define DISPLAY_MADCTL                  0x60
#define DISPLAY_INVERT_COLORS           1       // most ST7789 IPS panels need 1, most ILI9341 need 0
#define DISPLAY_X_OFFSET                0       // column offset of the visible area in the controller RAM
#define DISPLAY_Y_OFFSET                0       // row offset of the visible area in the controller RAM

// SD card: holds the converted PAK file (see README). SDMMC (1 or 4 bit, faster) or SPI mode.
#define SD_MODE                         SD_MODE_SPI
// SPI mode pins. The SD card may share the display SPI bus: if SD_SPI_HOST equals
// DISPLAY_SPI_HOST, MOSI/SCLK must be the same as the display ones.
#define SD_SPI_HOST                     SPI3_HOST
#define SD_SPI_CLOCK_KHZ                20000
#define SD_SDMMC_CLOCK_KHZ              20000   // SDMMC modes: 40000 (high speed) works with short wiring
#define SD_PIN_MOSI                     -1      // TODO: set (SPI mode: MOSI, SDMMC mode: CMD)
#define SD_PIN_MISO                     -1      // TODO: set (SPI mode: MISO, SDMMC mode: D0)
#define SD_PIN_SCLK                     -1      // TODO: set (SPI mode: SCLK, SDMMC mode: CLK)
#define SD_PIN_CS                       -1      // TODO: set (SPI mode only)
#define SD_PIN_D1                       -1      // SDMMC 4-bit only
#define SD_PIN_D2                       -1
#define SD_PIN_D3                       -1

// Audio: 11025 Hz stereo.
#define AUDIO_OUTPUT                    AUDIO_OUTPUT_NONE
#define AUDIO_PIN_MCLK                  -1
#define AUDIO_PIN_BCLK                  -1      // I2S only
#define AUDIO_PIN_WS                    -1      // I2S only (LRCLK)
#define AUDIO_PIN_DOUT                  -1      // I2S data out, or PDM data out
#define AUDIO_PDM_PIN_CLK               -1      // PDM clock, usually -1 (not needed for a filtered speaker output)

// Optional GPIO buttons (active low, internal pull-up). -1 = not present.
#define BUTTON_PIN_UP                   -1
#define BUTTON_PIN_DOWN                 -1
#define BUTTON_PIN_LEFT                 -1
#define BUTTON_PIN_RIGHT                -1
#define BUTTON_PIN_FIRE                 -1
#define BUTTON_PIN_JUMP                 -1
#define BUTTON_PIN_WEAPON               -1
#define BUTTON_PIN_MENU                 -1
#endif

#ifndef AUDIO_SAMPLE_SHIFT
#define AUDIO_SAMPLE_SHIFT              8       // 8 bit mixer samples -> 16 bit output; lower = quieter (6 dB per step)
#endif

#define SD_MOUNT_POINT                  "/sd"
#define SD_PAK_PATH                     SD_MOUNT_POINT "/PAK0.PAK"   // converted PAK (MCUPackConverter output)

// Bluetooth LE HID host (keyboards, gamepads). ESP32-S3 only supports BLE:
// Bluetooth Classic-only devices cannot be used.
#define BLE_HID_ENABLED                 1
#define BLE_HID_DEBUG                   0       // 1: log every nearby advertiser while scanning

// Stretch Quake's 320x200 to the full 320x240 panel (4:3, as on the original
// monitors). 0: 1:1 pixels, black bars above and below.
#define DISPLAY_STRETCH_TO_FULL_HEIGHT  1

// Touch controls (boards with a touch controller, see boardTouchRead())
#define TOUCH_CONTROLS_ENABLED          1

// Memory
#define INTERNAL_FLASH_CACHE_SIZE       (1024 * 1024)       // PSRAM area emulating the MG24 internal flash (skin/model cache)
#define EXT_CACHE_BLOCK_SIZE            8192                // PAK read granularity from SD (power of 2)
#define EXT_CACHE_PSRAM_RESERVE         (512 * 1024)        // PSRAM left free for other uses

#endif // BOARD_CONFIG_H
