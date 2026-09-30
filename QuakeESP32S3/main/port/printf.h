/**
 * @file printf.h
 * @brief ESP32-S3: the engine uses the same tiny printf as the MG24 port
 * (main/printf.c, built by esp_printf.c). Besides being what the
 * engine was tested with, it prints NULL strings as empty strings, which the
 * engine relies on (newlib would crash).
 */
#ifndef _PRINTF_H_
#define _PRINTF_H_
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include "main.h"

#ifdef __cplusplus
extern "C" {
#endif
void _putchar(char character);
int printf_(const char* format, ...);
int sprintf_(char* buffer, const char* format, ...);
int snprintf_(char* buffer, size_t count, const char* format, ...);
int vsnprintf_(char* buffer, size_t count, const char* format, va_list va);
int vprintf_(const char* format, va_list va);
int fctprintf(void (*out)(char character, void* arg), void* arg, const char* format, ...);
#ifdef __cplusplus
}
#endif

#define printf      printf_
#define sprintf     sprintf_
#define snprintf    snprintf_
#define vsnprintf   vsnprintf_
#define vprintf     vprintf_
#endif  // _PRINTF_H_
