#pragma once

#include "kendryte-standalone-sdk/lib/freertos/include/FreeRTOS.h"
#include "kendryte-standalone-sdk/lib/freertos/include/task.h"

void lvgl_runtime_init(void);
void lvgl_runtime_task(void *arg);
void lvgl_monitor_task(void *arg);
