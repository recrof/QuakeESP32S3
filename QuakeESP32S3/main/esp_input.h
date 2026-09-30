/**
 * @file esp_input.h
 * @brief Input handling for the ESP32-S3 Quake port: BLE HID keyboards,
 * gamepads and mice, touch controls, plus optional GPIO buttons.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#ifndef ESP_INPUT_H
#define ESP_INPUT_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Virtual gamepad buttons
enum
{
    GP_A, GP_B, GP_X, GP_Y, GP_LB, GP_RB, GP_LT, GP_RT, GP_SELECT, GP_START, GP_HOME, GP_L3, GP_R3,
    GP_UP, GP_DOWN, GP_LEFT, GP_RIGHT,
    GP_NUM_BUTTONS
};
// Queues a Quake key event (any task)
void inputPushKey(int key, bool down);
// Called by the BLE HID host (any task)
void inputHidOpened(const uint8_t *const *maps, const uint16_t *mapLengths, int numMaps);
void inputHidReport(uint8_t mapIndex, uint8_t reportId, const uint8_t *data, int len);
void inputHidClosed(void);
// Returns true if at least one input report field was found in the report maps
bool inputHidHasFields(void);
#endif
