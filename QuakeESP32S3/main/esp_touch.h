/**
 * @file esp_touch.h
 * @brief On-screen touch controls for the ESP32-S3 Quake port.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#ifndef ESP_TOUCH_H
#define ESP_TOUCH_H
#include <stdint.h>
#include <stdbool.h>
// Starts the touch polling task (if the board has a touch controller)
void touchInit(void);
// Virtual gamepad buttons (esp_input.c GP_* bit mask) held through touch
uint32_t touchGetButtons(void);
// Movement stick, -127..127 (x: right, y: down = backwards)
void touchGetStick(float *x, float *y);
// Look movement in pixels since the last call
void touchTakeLook(float *dx, float *dy);
// Hides the overlay (e.g. when a Bluetooth controller is connected) until the screen is touched again
void touchHideOverlay(void);
// Display task: whether the control overlay must be drawn, and draws it on one LCD line (RGB565 big endian)
bool touchOverlayVisible(void);
void touchDrawOverlay(uint16_t *line, int y);
#endif
