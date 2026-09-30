/**
 * @file audio.h
 * @brief ESP32-S3 audio output. The engine mixes into audioBuffer, a circular
 * buffer of AUDIO_BUFFER_LENGTH unsigned 8-bit stereo frames, which is played
 * continuously at AUDIO_SAMPLE_RATE.
 */
#ifndef SRC_PWM_AUDIO_H_
#define SRC_PWM_AUDIO_H_
#include <stdint.h>
#include "main.h"
#define AUDIO_SAMPLE_RATE               11025
#define AUDIO_BUFFER_LENGTH             (1024)
#define AUDIO_BUFFER_DELAY              (200)   // ~18 ms of safety margin ahead of the play position
typedef struct
{
    uint16_t lastAudioBufferIdx;
    uint16_t offset;
    uint8_t sfxIdx;
    int8_t volumeLeft;
    int8_t volumeRight;
} soundChannel_t;
void initAudio(void);
// Index (in stereo frames) of the next frame that will be played
uint32_t audioGetPlayIndex(void);
extern int8_t audioBuffer[AUDIO_BUFFER_LENGTH * 2];
#endif
