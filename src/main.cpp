#include "kendryte-standalone-sdk/lib/freertos/include/FreeRTOS.h"
#include "kendryte-standalone-sdk/lib/freertos/include/task.h"
#include <Arduino.h>

#include "app_log.h"
#include "app_manager.h"
#include "lvgl_runtime.h"

void setup() {
    Serial.begin(115200);
    delay(300);

    BaseType_t ok1 = xTaskCreate(lvgl_runtime_task, "ui", 6144, NULL, tskIDLE_PRIORITY + 2, NULL);
    BaseType_t ok2 = xTaskCreate(lvgl_monitor_task, "mon", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);
    BaseType_t ok3 = xTaskCreate(app_manager_service_task, "svc", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);

    if (ok1 != pdPASS || ok2 != pdPASS || ok3 != pdPASS) {
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
