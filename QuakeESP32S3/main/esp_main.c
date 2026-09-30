/**
 * @file esp_main.c
 * @brief Entry point of the ESP32-S3 Quake port.
 *
 * Based on the MG24Quake port by Nicola Wrachien (next-hack).
 *
 * Core usage:
 *  - core 1: the game (Host_Frame loop), never yields.
 *  - core 0: display refresh, audio, Bluetooth.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#include <stdio.h>
#include <stdarg.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "quakedef.h"
#include "graphics.h"
#include "display.h"
#include "keyboard.h"
#include "esp_board.h"
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#endif
#include "board_config.h"

#define GAME_TASK_STACK_SIZE    (40 * 1024)
#define GAME_TASK_PRIORITY      5
#define GAME_TASK_CORE          1

static const char *TAG = "quake";

void bleHidInit(void);

void NVIC_SystemReset(void)
{
    esp_restart();
}

unsigned int I_GetTimeMicrosecs(void)
{
    return (unsigned int) esp_timer_get_time();
}

float Sys_FloatTime(void)
{
    return esp_timer_get_time() * (1.0f / 1000000.0f);
}

void Sys_Error(char *error, ...)
{
    char msg[128];
    va_list argptr;
    va_start(argptr, error);
    vsnprintf(msg, sizeof(msg), error, argptr);
    va_end(argptr);
    printf("Error: %s\n", msg);
    // show the error on the display
    displayPrintln(1, "Error: %s", msg);
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static void haltWithMessage(const char *msg)
{
    displayPrintln(1, "%s", msg);
    displayPrintln(1, "Halted.");
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static esp_err_t mountSdCard(void)
{
    esp_vfs_fat_sdmmc_mount_config_t mountConfig =
    {
        .format_if_mount_failed = false,
        .max_files = 4,
        .allocation_unit_size = 16 * 1024,
    };
    sdmmc_card_t *card;
    esp_err_t ret;
#if SD_MODE == SD_MODE_SPI
    if (SD_PIN_MOSI < 0 || SD_PIN_MISO < 0 || SD_PIN_SCLK < 0 || SD_PIN_CS < 0)
    {
        ESP_LOGE(TAG, "SD pins not configured, see board_config.h");
        return ESP_ERR_INVALID_ARG;
    }
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;
    host.max_freq_khz = SD_SPI_CLOCK_KHZ;
    if (SD_SPI_HOST != DISPLAY_SPI_HOST)
    {
        spi_bus_config_t bus =
        {
            .mosi_io_num = SD_PIN_MOSI,
            .miso_io_num = SD_PIN_MISO,
            .sclk_io_num = SD_PIN_SCLK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = EXT_CACHE_BLOCK_SIZE,
        };
        ret = spi_bus_initialize(SD_SPI_HOST, &bus, SDSPI_DEFAULT_DMA);
        if (ret != ESP_OK)
        {
            return ret;
        }
    }
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = SD_PIN_CS;
    slot.host_id = SD_SPI_HOST;
    ret = esp_vfs_fat_sdspi_mount(SD_MOUNT_POINT, &host, &slot, &mountConfig, &card);
#else
    if (SD_PIN_MOSI < 0 || SD_PIN_MISO < 0 || SD_PIN_SCLK < 0)
    {
        ESP_LOGE(TAG, "SD pins not configured, see board_config.h");
        return ESP_ERR_INVALID_ARG;
    }
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SD_SDMMC_CLOCK_KHZ;
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.clk = SD_PIN_SCLK;
    slot.cmd = SD_PIN_MOSI;
    slot.d0 = SD_PIN_MISO;
#if SD_MODE == SD_MODE_SDMMC_4BIT
    slot.width = 4;
    slot.d1 = SD_PIN_D1;
    slot.d2 = SD_PIN_D2;
    slot.d3 = SD_PIN_D3;
#else
    slot.width = 1;
#endif
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    ret = esp_vfs_fat_sdmmc_mount(SD_MOUNT_POINT, &host, &slot, &mountConfig, &card);
#endif
    if (ret == ESP_OK)
    {
        sdmmc_card_print_info(stdout, card);
    }
    return ret;
}

static void gameTask(void *arg)
{
    (void) arg;
    float time, oldtime, newtime;
    Z_Init();
    Cvar_Init();
    memset(_g, 0, sizeof(global_data_t));
    Host_Init(NULL);
    oldtime = Sys_FloatTime() - 0.1f;
    float statsTime = oldtime;
    int statsFrames = host_framecount;
    while (1)
    {
        if (Sys_FloatTime() - statsTime >= 5.0f)
        {
            // heartbeat on the serial console
            float now = Sys_FloatTime();
            printf("fps: %d.%d, PAK cache misses: %u\n", (int) ((host_framecount - statsFrames) / (now - statsTime)),
                (int) (10 * (host_framecount - statsFrames) / (now - statsTime)) % 10, (unsigned) extMemGetCacheMisses());
            statsTime = now;
            statsFrames = host_framecount;
        }
        // find time spent rendering last frame
        newtime = Sys_FloatTime();
        time = newtime - oldtime;
        if (time > sys_ticrate * 2)
            oldtime = newtime;
        else
            oldtime += time;
        Host_Frame(time);
    }
}

void app_main(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    // give a USB serial monitor a moment to (re)connect, so the boot log is not lost
    for (int i = 0; i < 20 && !usb_serial_jtag_is_connected(); i++)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelay(pdMS_TO_TICKS(500));
#endif
    boardInit();
    displayInit();
    initGraphics();
    displayPrintln(0, "Quake on ESP32-S3");
    displayPrintln(0, "Based on MG24Quake by Nicola Wrachien");
    displayPrintln(1, "Build date %s %s", __DATE__, __TIME__);
    boardBacklightOn();
    if (!esp_psram_is_initialized())
    {
        haltWithMessage("PSRAM not found: it is required!");
    }
    displayPrintln(1, "PSRAM: %u kB free", (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    displayPrintln(1, "Internal RAM: %u kB free", (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
    displayPrintln(1, "Memzone size %u bytes.", (unsigned) getStaticZoneSize());
    //
    displayPrintln(1, "Mounting SD card...");
    esp_err_t ret = mountSdCard();
    if (ret != ESP_OK)
    {
        displayPrintln(1, "SD card error: %s", esp_err_to_name(ret));
        haltWithMessage("Insert a FAT SD card with PAK0.PAK");
    }
    if (extMemInit() != 0)
    {
        displayPrintln(1, "Cannot use %s", SD_PAK_PATH);
        haltWithMessage("Copy the converted PAK (MCUPackConverter) as PAK0.PAK");
    }
    displayPrintln(1, "PAK size: %u bytes", (unsigned) extMemGetPakSize());
    //
    initKeyboard();
    displayPrintln(1, "Starting Bluetooth LE HID host...");
    bleHidInit();
    //
    displayPrintln(1, "Free RAM: internal %u kB, PSRAM %u kB", (unsigned) (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
        (unsigned) (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    displayPrintln(1, "Starting game...");
    // The stack should be in internal RAM for speed, but PSRAM works too.
    if (xTaskCreatePinnedToCoreWithCaps(gameTask, "quake", GAME_TASK_STACK_SIZE, NULL, GAME_TASK_PRIORITY, NULL,
        GAME_TASK_CORE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) != pdPASS)
    {
        ESP_LOGW(TAG, "Not enough internal RAM for the game stack, using PSRAM");
        xTaskCreatePinnedToCoreWithCaps(gameTask, "quake", GAME_TASK_STACK_SIZE, NULL, GAME_TASK_PRIORITY, NULL,
            GAME_TASK_CORE, MALLOC_CAP_SPIRAM);
    }
}
