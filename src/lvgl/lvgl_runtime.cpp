#include <Arduino.h>
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
    uint32_t last_flush = 0;
    APP_LOGI("RTOS monitor task started");

    for (;;) {
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        uint32_t flush_now = display_port_get_flush_count();
        uint32_t flush_delta = flush_now - last_flush;
        uint32_t last_us = display_port_get_last_flush_us();
        last_flush = flush_now;

        APP_LOGI("RTOS heap=%u(min=%u), LVGL used=%u%% free=%u, flush/s=%u, last_flush_us=%u",
                 (unsigned int)xPortGetFreeHeapSize(),
                 (unsigned int)xPortGetMinimumEverFreeHeapSize(),
                 (unsigned int)mon.used_pct,
                 (unsigned int)mon.free_size,
                 (unsigned int)flush_delta,
                 (unsigned int)last_us);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
