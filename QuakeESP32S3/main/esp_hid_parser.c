/**
 * @file esp_hid_parser.c
 * @brief Minimal HID report descriptor parser (input reports only).
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#include <string.h>
#include "esp_hid_parser.h"

#define MAX_REPORT_IDS      16
#define MAX_GLOBAL_STACK    4

typedef struct
{
    uint16_t usagePage;
    int32_t logicalMin;
    int32_t logicalMax;
    uint8_t reportSize;
    uint8_t reportId;
    uint16_t reportCount;
} hidGlobals_t;

static uint32_t getUnsigned(const uint8_t *p, int size)
{
    uint32_t v = 0;
    for (int i = 0; i < size; i++)
    {
        v |= (uint32_t) p[i] << (8 * i);
    }
    return v;
}

static int32_t getSigned(const uint8_t *p, int size)
{
    uint32_t v = getUnsigned(p, size);
    if (size > 0 && size < 4 && (v & (1u << (8 * size - 1))))
    {
        v |= ~0u << (8 * size);
    }
    return (int32_t) v;
}

int hidParseReportMap(uint8_t mapIndex, const uint8_t *desc, int len, hidField_t *fields, int numFields, int maxFields)
{
    hidGlobals_t g = { 0 };
    hidGlobals_t stack[MAX_GLOBAL_STACK];
    int stackDepth = 0;
    // local items
    uint16_t usages[HID_FIELD_MAX_USAGES];
    int numUsages = 0;
    uint32_t usageMin = 0, usageMax = 0;
    bool hasRange = false;
    uint16_t localPage = 0;
    // bit offset of each report ID
    uint8_t ids[MAX_REPORT_IDS];
    uint16_t offsets[MAX_REPORT_IDS];
    int numIds = 0;
    //
    int pos = 0;
    while (pos < len)
    {
        uint8_t prefix = desc[pos++];
        if (prefix == 0xFE)
        {
            // long item: skip
            if (pos + 1 >= len)
                break;
            pos += 2 + desc[pos];
            continue;
        }
        int size = prefix & 3;
        if (size == 3)
            size = 4;
        if (pos + size > len)
            break;
        const uint8_t *data = &desc[pos];
        pos += size;
        uint8_t type = (prefix >> 2) & 3;
        uint8_t tag = prefix >> 4;
        uint32_t uval = getUnsigned(data, size);
        switch (type)
        {
            case 0:     // main
                if (tag == 0x8)      // input
                {
                    // find bit offset of this report ID
                    int idIdx;
                    for (idIdx = 0; idIdx < numIds && ids[idIdx] != g.reportId; idIdx++)
                        ;
                    if (idIdx == numIds)
                    {
                        if (numIds == MAX_REPORT_IDS)
                            break;
                        ids[numIds] = g.reportId;
                        offsets[numIds] = 0;
                        numIds++;
                    }
                    if (!(uval & HID_FLAG_CONSTANT) && g.reportSize && g.reportCount && numFields < maxFields)
                    {
                        hidField_t *f = &fields[numFields++];
                        memset(f, 0, sizeof(*f));
                        f->mapIndex = mapIndex;
                        f->reportId = g.reportId;
                        f->flags = uval;
                        f->bitSize = g.reportSize;
                        f->bitOffset = offsets[idIdx];
                        f->count = g.reportCount;
                        f->usagePage = localPage ? localPage : g.usagePage;
                        f->logicalMin = g.logicalMin;
                        f->logicalMax = g.logicalMax;
                        // some descriptors declare 0..-1 as "unsigned max"
                        if (f->logicalMax < f->logicalMin && f->bitSize < 32)
                        {
                            f->logicalMax = (int32_t) ((1u << f->bitSize) - 1);
                        }
                        if (hasRange)
                        {
                            f->usageMin = usageMin;
                            f->usageMax = usageMax;
                        }
                        f->numUsages = numUsages;
                        memcpy(f->usages, usages, numUsages * sizeof(usages[0]));
                    }
                    offsets[idIdx] += g.reportSize * g.reportCount;
                }
                // any main item clears the locals
                numUsages = 0;
                hasRange = false;
                usageMin = usageMax = 0;
                localPage = 0;
                break;
            case 1:     // global
                switch (tag)
                {
                    case 0x0:
                        g.usagePage = uval;
                        break;
                    case 0x1:
                        g.logicalMin = getSigned(data, size);
                        break;
                    case 0x2:
                        g.logicalMax = getSigned(data, size);
                        break;
                    case 0x7:
                        g.reportSize = uval;
                        break;
                    case 0x8:
                        g.reportId = uval;
                        break;
                    case 0x9:
                        g.reportCount = uval;
                        break;
                    case 0xA:
                        if (stackDepth < MAX_GLOBAL_STACK)
                            stack[stackDepth++] = g;
                        break;
                    case 0xB:
                        if (stackDepth > 0)
                            g = stack[--stackDepth];
                        break;
                    default:
                        break;
                }
                break;
            case 2:     // local
                if (size == 4 && (tag == 0x0 || tag == 0x1 || tag == 0x2))
                {
                    localPage = uval >> 16;
                    uval &= 0xFFFF;
                }
                switch (tag)
                {
                    case 0x0:
                        if (numUsages < HID_FIELD_MAX_USAGES)
                            usages[numUsages++] = uval;
                        break;
                    case 0x1:
                        usageMin = uval;
                        hasRange = true;
                        break;
                    case 0x2:
                        usageMax = uval;
                        hasRange = true;
                        break;
                    default:
                        break;
                }
                break;
            default:
                break;
        }
    }
    return numFields;
}

int32_t hidGetFieldValue(const hidField_t *f, int i, const uint8_t *data, int len)
{
    uint32_t bit = f->bitOffset + i * f->bitSize;
    uint32_t v = 0;
    for (int b = 0; b < f->bitSize && b < 32; b++, bit++)
    {
        if ((bit >> 3) < (uint32_t) len && (data[bit >> 3] & (1 << (bit & 7))))
        {
            v |= 1u << b;
        }
    }
    if (f->logicalMin < 0 && f->bitSize < 32 && (v & (1u << (f->bitSize - 1))))
    {
        v |= ~0u << f->bitSize;
    }
    return (int32_t) v;
}

uint16_t hidGetVariableUsage(const hidField_t *f, int i)
{
    if (i < f->numUsages)
    {
        return f->usages[i];
    }
    if (f->usageMin || f->usageMax)
    {
        uint32_t u = f->usageMin + i;
        return u <= f->usageMax ? u : 0;
    }
    return f->numUsages ? f->usages[f->numUsages - 1] : 0;
}
