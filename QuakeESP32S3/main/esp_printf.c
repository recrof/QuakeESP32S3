/**
 * @file esp_printf.c
 * @brief Builds the MG24Quake tiny printf (printf.c, MIT license,
 * Marco Paland) for the engine, with output to the ESP32 console.
 */
#include "printf.h"                         // port header (port/printf.h)
#include "printf.c"

void _putchar(char character)
{
    putchar(character);
}
