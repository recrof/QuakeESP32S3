/**
 * @file esp_touch.c
 * @brief On-screen touch controls for the ESP32-S3 Quake port.
 *
 * Playing (LCD coordinates, 320x240 landscape):
 * - left half: virtual stick, centered where the finger lands (move / strafe)
 * - right half: drag to turn / look
 * - FIRE, JUMP and NEXT WEAPON buttons on the right (dragging from FIRE also looks)
 * - MENU button in the top left corner (escape)
 * Menus / console: swipe up/down/left/right = arrow keys, tap = enter,
 * tap on the MENU corner = escape.
 * The buttons are drawn over the 3D view once the screen has been touched.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_timer.h"
#include "quakedef.h"
#include "display.h"
#include "esp_board.h"
#include "esp_input.h"
#include "esp_touch.h"
#include "board_config.h"

#define TOUCH_POLL_MS           10
#define STICK_RADIUS            40      // pixels for full deflection
#define LOOK_SPLIT_X            (LCD_WIDTH / 2)
#define HIT_SLOP                10      // extra radius accepted around buttons
#define TAP_MAX_MOVE            12
#define TAP_MAX_US              400000
#define SWIPE_STEP              24      // pixels per arrow key in menus

typedef enum
{
    ROLE_NONE, ROLE_STICK, ROLE_LOOK, ROLE_FIRE, ROLE_JUMP, ROLE_WEAPON, ROLE_MENU, ROLE_MENU_NAV
} touchRole_t;

typedef struct
{
    bool active;
    bool seen;                  // present in the latest report
    bool dragged;               // menus: moved enough to be a swipe
    uint8_t id;
    touchRole_t role;
    int16_t x, y;               // current position
    int16_t x0, y0;             // stick center / swipe reference
    int16_t lastX, lastY;
    int64_t downTime;
} touchSlot_t;

typedef struct
{
    int16_t x, y, r;
    uint8_t button;             // GP_* bit
    touchRole_t role;
} touchButton_t;

// button layout (LCD coordinates, kept inside the 3D view, above the status bar)
static const touchButton_t buttons[] =
{
    { 268, 146, 30, GP_RT, ROLE_FIRE },
    { 294, 88, 20, GP_A, ROLE_JUMP },
    { 292, 30, 16, GP_X, ROLE_WEAPON },
};
#define MENU_X0                 6
#define MENU_Y0                 6
#define MENU_X1                 46
#define MENU_Y1                 34

static touchSlot_t slots[BOARD_TOUCH_MAX_POINTS];
static portMUX_TYPE touchMux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t touchButtons;
static float lookDX, lookDY;
static volatile bool overlayVisible;

static bool inMenuArea(int x, int y)
{
    return x < MENU_X1 + HIT_SLOP && y < MENU_Y1 + HIT_SLOP;
}

static const touchButton_t *buttonAt(int x, int y)
{
    for (int i = 0; i < (int) (sizeof(buttons) / sizeof(buttons[0])); i++)
    {
        int dx = x - buttons[i].x, dy = y - buttons[i].y, r = buttons[i].r + HIT_SLOP;
        if (dx * dx + dy * dy <= r * r)
        {
            return &buttons[i];
        }
    }
    return NULL;
}

static void tapKey(int key)
{
    inputPushKey(key, true);
    inputPushKey(key, false);
}

static void touchDown(touchSlot_t *s)
{
    s->x0 = s->lastX = s->x;
    s->y0 = s->lastY = s->y;
    s->dragged = false;
    s->downTime = esp_timer_get_time();
    if (key_dest != key_game)
    {
        s->role = inMenuArea(s->x, s->y) ? ROLE_MENU : ROLE_MENU_NAV;
        return;
    }
    const touchButton_t *b = buttonAt(s->x, s->y);
    if (inMenuArea(s->x, s->y))
        s->role = ROLE_MENU;
    else if (b)
        s->role = b->role;
    else
        s->role = s->x < LOOK_SPLIT_X ? ROLE_STICK : ROLE_LOOK;
}

static void touchMove(touchSlot_t *s)
{
    switch (s->role)
    {
        case ROLE_LOOK:
        case ROLE_FIRE:
            portENTER_CRITICAL(&touchMux);
            lookDX += s->x - s->lastX;
            lookDY += s->y - s->lastY;
            portEXIT_CRITICAL(&touchMux);
            break;
        case ROLE_STICK:
        {
            // drag the stick center along when going past the edge
            float dx = s->x - s->x0, dy = s->y - s->y0;
            float d = sqrtf(dx * dx + dy * dy);
            if (d > STICK_RADIUS)
            {
                s->x0 = s->x - dx * STICK_RADIUS / d;
                s->y0 = s->y - dy * STICK_RADIUS / d;
            }
            break;
        }
        case ROLE_MENU_NAV:
        {
            int dx = s->x - s->x0, dy = s->y - s->y0;
            if (abs(dx) > TAP_MAX_MOVE || abs(dy) > TAP_MAX_MOVE)
            {
                s->dragged = true;
            }
            if (abs(dy) >= SWIPE_STEP && abs(dy) >= abs(dx))
            {
                tapKey(dy < 0 ? K_UPARROW : K_DOWNARROW);
                s->x0 = s->x;
                s->y0 = s->y;
            }
            else if (abs(dx) >= SWIPE_STEP)
            {
                tapKey(dx < 0 ? K_LEFTARROW : K_RIGHTARROW);
                s->x0 = s->x;
                s->y0 = s->y;
            }
            break;
        }
        default:
            break;
    }
    s->lastX = s->x;
    s->lastY = s->y;
}

static void touchUp(touchSlot_t *s)
{
    bool tap = !s->dragged && esp_timer_get_time() - s->downTime < TAP_MAX_US;
    if (s->role == ROLE_MENU_NAV && tap)
    {
        tapKey(K_ENTER);
    }
    else if (s->role == ROLE_MENU && key_dest != key_game && tap)
    {
        tapKey(K_ESCAPE);
    }
    s->active = false;
    s->role = ROLE_NONE;
}

static void updateButtons(void)
{
    uint32_t b = 0;
    for (int i = 0; i < BOARD_TOUCH_MAX_POINTS; i++)
    {
        if (!slots[i].active)
            continue;
        switch (slots[i].role)
        {
            case ROLE_FIRE: b |= 1u << GP_RT; break;
            case ROLE_JUMP: b |= 1u << GP_A; break;
            case ROLE_WEAPON: b |= 1u << GP_X; break;
            case ROLE_MENU:
                if (key_dest == key_game)
                    b |= 1u << GP_START;
                break;
            default: break;
        }
    }
    touchButtons = b;
}

static void touchTask(void *arg)
{
    boardTouchPoint_t points[BOARD_TOUCH_MAX_POINTS];
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));
        int n = boardTouchRead(points);
        if (n < 0)
            continue;
        for (int i = 0; i < BOARD_TOUCH_MAX_POINTS; i++)
        {
            slots[i].seen = false;
        }
        for (int p = 0; p < n; p++)
        {
            touchSlot_t *s = NULL, *free = NULL;
            for (int i = 0; i < BOARD_TOUCH_MAX_POINTS; i++)
            {
                if (slots[i].active && slots[i].id == points[p].id)
                    s = &slots[i];
                else if (!slots[i].active && !free)
                    free = &slots[i];
            }
            if (!s && !free)
                continue;
            if (!s)
            {
                s = free;
                s->active = true;
                s->id = points[p].id;
                s->x = points[p].x;
                s->y = points[p].y;
                touchDown(s);
                overlayVisible = true;
            }
            else
            {
                s->x = points[p].x;
                s->y = points[p].y;
                touchMove(s);
            }
            s->seen = true;
        }
        for (int i = 0; i < BOARD_TOUCH_MAX_POINTS; i++)
        {
            if (slots[i].active && !slots[i].seen)
                touchUp(&slots[i]);
        }
        updateButtons();
    }
}

void touchInit(void)
{
#if TOUCH_CONTROLS_ENABLED
    if (boardHasTouch())
    {
        // stack in PSRAM: this task never writes to flash
        xTaskCreatePinnedToCoreWithCaps(touchTask, "touch", 3072, NULL, 4, NULL, 0, MALLOC_CAP_SPIRAM);
    }
#endif
}

uint32_t touchGetButtons(void)
{
    return touchButtons;
}

void touchGetStick(float *x, float *y)
{
    *x = *y = 0;
    for (int i = 0; i < BOARD_TOUCH_MAX_POINTS; i++)
    {
        const touchSlot_t *s = &slots[i];
        if (s->active && s->role == ROLE_STICK)
        {
            float dx = (float) (s->x - s->x0) * 127 / STICK_RADIUS;
            float dy = (float) (s->y - s->y0) * 127 / STICK_RADIUS;
            *x = dx < -127 ? -127 : (dx > 127 ? 127 : dx);
            *y = dy < -127 ? -127 : (dy > 127 ? 127 : dy);
            return;
        }
    }
}

void touchTakeLook(float *dx, float *dy)
{
    portENTER_CRITICAL(&touchMux);
    *dx = lookDX;
    *dy = lookDY;
    lookDX = lookDY = 0;
    portEXIT_CRITICAL(&touchMux);
}

void touchHideOverlay(void)
{
    overlayVisible = false;
}

bool touchOverlayVisible(void)
{
    return overlayVisible && key_dest == key_game;
}

// ---------------------------------------------------------------------------
// Overlay drawing (display task), pixels are RGB565 big endian
// ---------------------------------------------------------------------------
static inline uint16_t swap16(uint16_t v)
{
    return (v >> 8) | (v << 8);
}

static void lightenSpan(uint16_t *line, int x0, int x1, int strength)
{
    if (x0 < 0)
        x0 = 0;
    if (x1 >= LCD_WIDTH)
        x1 = LCD_WIDTH - 1;
    for (int x = x0; x <= x1; x++)
    {
        uint16_t v = swap16(line[x]);
        // 50% (strength 1) or 25% (strength 2) towards white
        v = strength == 1 ? ((v & 0xF7DE) >> 1) + 0x7BEF : ((v & 0xE79C) >> 2) * 3 + 0x39E7;
        line[x] = swap16(v);
    }
}

static void drawCircle(uint16_t *line, int y, int cx, int cy, int r, bool filled)
{
    const int thickness = 2;
    int dy = y - cy;
    if (dy < -r || dy > r)
        return;
    int wo = (int) sqrtf((float) (r * r - dy * dy));
    int ri = r - thickness;
    int wi = (dy >= -ri && dy <= ri) ? (int) sqrtf((float) (ri * ri - dy * dy)) : -1;
    if (filled)
    {
        lightenSpan(line, cx - wo, cx + wo, 2);
    }
    else if (wi < 0)
    {
        lightenSpan(line, cx - wo, cx + wo, 1);
    }
    else
    {
        lightenSpan(line, cx - wo, cx - wi - 1, 1);
        lightenSpan(line, cx + wi + 1, cx + wo, 1);
    }
}

void touchDrawOverlay(uint16_t *line, int y)
{
    uint32_t pressed = touchButtons;
    for (int i = 0; i < (int) (sizeof(buttons) / sizeof(buttons[0])); i++)
    {
        drawCircle(line, y, buttons[i].x, buttons[i].y, buttons[i].r, (pressed >> buttons[i].button) & 1);
    }
    // menu button: outline and three bars
    if (y >= MENU_Y0 && y <= MENU_Y1)
    {
        if (y <= MENU_Y0 + 1 || y >= MENU_Y1 - 1)
        {
            lightenSpan(line, MENU_X0, MENU_X1, 1);
        }
        else
        {
            lightenSpan(line, MENU_X0, MENU_X0 + 1, 1);
            lightenSpan(line, MENU_X1 - 1, MENU_X1, 1);
            int r = y - MENU_Y0;
            if (r == 9 || r == 10 || r == 14 || r == 15 || r == 19 || r == 20)
            {
                lightenSpan(line, MENU_X0 + 10, MENU_X1 - 10, 1);
            }
        }
    }
    // stick: base ring and knob
    for (int i = 0; i < BOARD_TOUCH_MAX_POINTS; i++)
    {
        const touchSlot_t *s = &slots[i];
        if (s->active && s->role == ROLE_STICK)
        {
            drawCircle(line, y, s->x0, s->y0, STICK_RADIUS, false);
            float kx, ky;
            touchGetStick(&kx, &ky);
            drawCircle(line, y, s->x0 + (int) (kx * STICK_RADIUS / 127), s->y0 + (int) (ky * STICK_RADIUS / 127), 12, true);
        }
    }
}
