#include <Arduino.h>
#include "kendryte-standalone-sdk/lib/freertos/include/FreeRTOS.h"
#include "kendryte-standalone-sdk/lib/freertos/include/task.h"
#include <lvgl.h>

#include "app_fonts.h"
#include "app_log.h"
#include "app_manager.h"
#include "display_port.h"
#include "lvgl_runtime.h"
#include "touch_cst836u.h"

static uint32_t app_tick(void) {
    return millis();
}

static void lvgl_touch_read(lv_indev_t *indev, lv_indev_data_t *data) {
    uint16_t x = 0;
    uint16_t y = 0;
    bool pressed = false;
    LV_UNUSED(indev);

    touch_cst836u_read(&x, &y, &pressed);
    data->point.x = (int16_t)x;
    data->point.y = (int16_t)y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

#if LV_USE_LOG
static void lvgl_log_cb(lv_log_level_t level, const char *buf) {
    switch (level) {
        case LV_LOG_LEVEL_TRACE:
        case LV_LOG_LEVEL_INFO:
            APP_LOGI("LVGL: %s", buf);
            break;
        case LV_LOG_LEVEL_WARN:
            APP_LOGW("LVGL: %s", buf);
            break;
        case LV_LOG_LEVEL_ERROR:
        case LV_LOG_LEVEL_USER:
        default:
            APP_LOGE("LVGL: %s", buf);
            break;
    }
}
#endif

void lvgl_runtime_init(void) {
    lv_init();
    lv_tick_set_cb(app_tick);

    /* 打印 FreeRTOS heap 状态以验证 LVGL 是否使用 heap_4 */
#if defined(configTOTAL_HEAP_SIZE)
    APP_LOGI("RTOS heap total=%u free=%u", (unsigned)configTOTAL_HEAP_SIZE, (unsigned)xPortGetFreeHeapSize());
#else
    APP_LOGI("RTOS heap free=%u", (unsigned)xPortGetFreeHeapSize());
#endif

#if LV_USE_LOG
    lv_log_register_print_cb(lvgl_log_cb);
#endif

    app_fonts_init();

    if (!display_port_init()) {
        APP_LOGE("Display init failed");
        while (1) {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    touch_cst836u_init();

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_read);

    app_manager_init();
}

void lvgl_runtime_task(void *arg) {
    LV_UNUSED(arg);

    lvgl_runtime_init();
    APP_LOGI("RTOS UI task started");

    for (;;) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void lvgl_monitor_task(void *arg) {
    LV_UNUSED(arg);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
