/*
 * main.cpp — KaiCam 启动入口
 *
 * 职责：硬件初始化 + LVGL 初始化 + 启动 app_manager
 * 业务逻辑全部委托给各 screen_*.cpp 层。
 */
#include "kendryte-standalone-sdk/lib/freertos/include/FreeRTOS.h"
#include "kendryte-standalone-sdk/lib/freertos/include/task.h"
#include <Arduino.h>

#include "app_log.h"
#include "lvgl_runtime.h"

void setup() {
    Serial.begin(115200);
    delay(300);

    BaseType_t ok1 = xTaskCreate(lvgl_runtime_task, "ui", 6144, NULL, tskIDLE_PRIORITY + 2, NULL);
    BaseType_t ok2 = xTaskCreate(lvgl_monitor_task, "mon", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);

    if (ok1 != pdPASS || ok2 != pdPASS) {
        APP_LOGE("xTaskCreate failed");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    APP_LOGI("Starting FreeRTOS scheduler");
    vTaskStartScheduler();

    APP_LOGE("Scheduler exited unexpectedly");
}

void loop() {
    delay(1000);
}
