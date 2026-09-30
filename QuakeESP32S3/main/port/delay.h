#ifndef SRC_DELAY_H_
#define SRC_DELAY_H_
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static inline void delay(uint32_t milliseconds)
{
    vTaskDelay(pdMS_TO_TICKS(milliseconds) ? pdMS_TO_TICKS(milliseconds) : 1);
}
#endif
