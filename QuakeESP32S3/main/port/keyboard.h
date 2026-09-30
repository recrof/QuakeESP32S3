/**
 * @file keyboard.h
 * @brief ESP32-S3 input interface (BLE HID keyboards/gamepads + optional GPIO buttons).
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>
#include <stdbool.h>
#include "main.h"
void initKeyboard(void);
// Raw bitmask of the "virtual" gamepad buttons, see INPUT_BTN_* in esp_input.h
void getKeys(uint16_t *keys);
void getAnalogInput(int32_t *lx, int32_t *ly, int32_t *rx, int32_t *ry);
#endif
