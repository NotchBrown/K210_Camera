#include "kendryte-standalone-sdk/lib/freertos/include/FreeRTOS.h"
#include "kendryte-standalone-sdk/lib/freertos/include/task.h"
#include <Arduino.h>

#include "app_log.h"
#include "app_manager.h"
#include "lvgl_runtime.h"
#include "sd_hw.h"
#include "sd_storage_service.h"


void setup() {
    Serial.begin(115200);


    char sd_msg[96] = "";
    bool sd_boot_ok = sd_hw_mount(sd_msg, sizeof(sd_msg));
    APP_LOGI("BootSD: mount=%d msg=%s", sd_boot_ok ? 1 : 0, sd_msg);

    BaseType_t ok1 = xTaskCreate(lvgl_runtime_task, "ui", 6144, NULL, tskIDLE_PRIORITY + 2, NULL);
//    BaseType_t ok2 = xTaskCreate(lvgl_monitor_task, "mon", 2048, NULL, tskIDLE_PRIORITY + 1, NULL);
    BaseType_t ok3 = xTaskCreate(sd_storage_service_task, "sd_storage_service", 6144, NULL, tskIDLE_PRIORITY + 3, NULL);
    BaseType_t ok4 = xTaskCreate(app_manager_service_task, "svc", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
    /* Test task is started from UI Test button */
    if (ok1 != pdPASS || ok3 != pdPASS || ok4 != pdPASS) {
        APP_LOGE("xTaskCreate failed: ok1=%d ok3=%d ok4=%d", ok1, ok3, ok4);
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
