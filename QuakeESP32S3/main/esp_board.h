/**
 * @file esp_board.h
 * @brief Board specific bring-up (power rails, GPIO expanders, backlight, codecs).
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#ifndef ESP_BOARD_H
#define ESP_BOARD_H
#include <stdbool.h>
#include <stdint.h>
#define BOARD_TOUCH_MAX_POINTS  5
typedef struct
{
    uint8_t id;             // tracking id, stable while the finger stays down
    int16_t x, y;           // LCD coordinates (landscape, 320x240)
} boardTouchPoint_t;
// Powers the peripherals and releases the display reset. Called before displayInit().
void boardInit(void);
// Turns the backlight on. Called after the display has been initialized and cleared.
void boardBacklightOn(void);
// Configures the audio codec (the I2S clocks must already be running) and enables the amplifier.
void boardAudioCodecInit(void);
// State of a board button not wired to an ESP32 GPIO (L2: WAKE, on the expander).
bool boardWakeButtonPressed(void);
// True if a touch controller was found.
bool boardHasTouch(void);
// Reads the touch points. Returns -1 if there is no new data, else the number of
// points currently down (0 = all released).
int boardTouchRead(boardTouchPoint_t *points);
#endif
