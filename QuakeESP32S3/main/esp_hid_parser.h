/**
 * @file esp_hid_parser.h
 * @brief Minimal HID report descriptor parser (input reports only).
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#ifndef ESP_HID_PARSER_H
#define ESP_HID_PARSER_H
#include <stdint.h>
#include <stdbool.h>

#define HID_FIELD_MAX_USAGES    16
#define HID_FLAG_CONSTANT       0x01
#define HID_FLAG_VARIABLE       0x02
#define HID_FLAG_RELATIVE       0x04

#define HID_PAGE_GENERIC_DESKTOP    0x01
#define HID_PAGE_SIMULATION         0x02
#define HID_PAGE_KEYBOARD           0x07
#define HID_PAGE_BUTTON             0x09
#define HID_PAGE_CONSUMER           0x0C

typedef struct
{
    uint8_t mapIndex;
    uint8_t reportId;
    uint8_t flags;
    uint8_t bitSize;
    uint16_t bitOffset;
    uint16_t count;
    uint16_t usagePage;
    uint16_t usageMin;          // usage range (arrays, or variables declared with usage min/max)
    uint16_t usageMax;
    uint8_t numUsages;          // explicit usage list (variables)
    uint16_t usages[HID_FIELD_MAX_USAGES];
    int32_t logicalMin;
    int32_t logicalMax;
} hidField_t;

/**
 * Parses a report map and appends the input fields to fields[].
 * @return the new number of fields
 */
int hidParseReportMap(uint8_t mapIndex, const uint8_t *desc, int len, hidField_t *fields, int numFields, int maxFields);
/**
 * Extracts element i of field f from report data (without report ID byte).
 */
int32_t hidGetFieldValue(const hidField_t *f, int i, const uint8_t *data, int len);
/**
 * Usage of element i of a variable field.
 */
uint16_t hidGetVariableUsage(const hidField_t *f, int i);
#endif
