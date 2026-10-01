/**
 * @file esp_audio.c
 * @brief Audio output for the ESP32-S3 Quake port.
 *
 * The engine mixes 8-bit unsigned stereo frames in the circular audioBuffer,
 * starting AUDIO_BUFFER_DELAY frames ahead of the current play position
 * (on the MG24 this was the LDMA source address). Here a task plays the ring
 * buffer through I2S (standard or PDM) and advances the play position.
 * Without audio hardware, the play position simply follows the time.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 */
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "audio.h"
#include "board_config.h"
#include "esp_board.h"
#if AUDIO_OUTPUT == AUDIO_OUTPUT_I2S || AUDIO_OUTPUT == AUDIO_OUTPUT_ES8311
#include "driver/i2s_std.h"
#elif AUDIO_OUTPUT == AUDIO_OUTPUT_PDM
#include "driver/i2s_pdm.h"
#endif

#define AUDIO_CHUNK_FRAMES      64      // must be (much) less than AUDIO_BUFFER_DELAY
#define AUDIO_TASK_PRIORITY     (configMAX_PRIORITIES - 3)
#define AUDIO_TASK_CORE         0

static const char *TAG = "audio";
int8_t audioBuffer[AUDIO_BUFFER_LENGTH * 2];
static volatile uint32_t playIndex;

#if AUDIO_OUTPUT != AUDIO_OUTPUT_NONE
static i2s_chan_handle_t txChannel;

static void audioTask(void *arg)
{
    (void) arg;
    int16_t out[AUDIO_CHUNK_FRAMES * 2];
    const uint8_t *in = (const uint8_t*) audioBuffer;
    while (1)
    {
        uint32_t idx = playIndex;
        for (int i = 0; i < AUDIO_CHUNK_FRAMES; i++)
        {
            uint32_t f = ((idx + i) & (AUDIO_BUFFER_LENGTH - 1)) * 2;
            out[2 * i] = (int16_t) ((in[f] - 128) << AUDIO_SAMPLE_SHIFT);
            out[2 * i + 1] = (int16_t) ((in[f + 1] - 128) << AUDIO_SAMPLE_SHIFT);
        }
        playIndex = (idx + AUDIO_CHUNK_FRAMES) & (AUDIO_BUFFER_LENGTH - 1);
        size_t written;
        i2s_channel_write(txChannel, out, sizeof(out), &written, portMAX_DELAY);
    }
}
#endif

uint32_t audioGetPlayIndex(void)
{
#if AUDIO_OUTPUT == AUDIO_OUTPUT_NONE
    return (uint32_t) ((esp_timer_get_time() * AUDIO_SAMPLE_RATE) / 1000000) & (AUDIO_BUFFER_LENGTH - 1);
#else
    return playIndex;
#endif
}

void initAudio(void)
{
    memset(audioBuffer, 0x80, sizeof(audioBuffer));
#if AUDIO_OUTPUT != AUDIO_OUTPUT_NONE
    if (AUDIO_PIN_DOUT < 0)
    {
        ESP_LOGW(TAG, "Audio pins not configured, see board_config.h");
        return;
    }
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chanCfg.dma_desc_num = 3;
    chanCfg.dma_frame_num = AUDIO_CHUNK_FRAMES;
    chanCfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chanCfg, &txChannel, NULL));
#if AUDIO_OUTPUT == AUDIO_OUTPUT_I2S || AUDIO_OUTPUT == AUDIO_OUTPUT_ES8311
    i2s_std_config_t stdCfg =
    {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
        {
            .mclk = AUDIO_PIN_MCLK,
            .bclk = AUDIO_PIN_BCLK,
            .ws = AUDIO_PIN_WS,
            .dout = AUDIO_PIN_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    stdCfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(txChannel, &stdCfg));
#else
    i2s_pdm_tx_config_t pdmCfg =
    {
        .clk_cfg = I2S_PDM_TX_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_PDM_TX_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
        {
            .clk = AUDIO_PDM_PIN_CLK,
            .dout = AUDIO_PIN_DOUT,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_pdm_tx_mode(txChannel, &pdmCfg));
#endif
    ESP_ERROR_CHECK(i2s_channel_enable(txChannel));
#if AUDIO_OUTPUT == AUDIO_OUTPUT_ES8311
    boardAudioCodecInit();      // needs MCLK running
#endif
    xTaskCreatePinnedToCoreWithCaps(audioTask, "audio", 3072, NULL, AUDIO_TASK_PRIORITY, NULL, AUDIO_TASK_CORE, MALLOC_CAP_SPIRAM);
#endif
}
