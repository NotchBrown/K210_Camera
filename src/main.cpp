/*
 * main.cpp — KaiCam 启动入口
 *
 * 职责：硬件初始化 + LVGL 初始化 + 启动 app_manager
 * 业务逻辑全部委托给各 screen_*.cpp 层。
 */
#include <Arduino.h>
#include <lvgl.h>

#include "display_port.h"
#include "touch_cst836u.h"
#include "app_manager.h"

static uint32_t app_tick(void) { return millis(); }

static void lvgl_touch_read(lv_indev_t *indev, lv_indev_data_t *data) {
    uint16_t x = 0, y = 0;
    bool pressed = false;
    LV_UNUSED(indev);
    touch_cst836u_read(&x, &y, &pressed);
    data->point.x = (int16_t)x;
    data->point.y = (int16_t)y;
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

void setup() {
    Serial.begin(115200);
    delay(300);

    lv_init();
    lv_tick_set_cb(app_tick);

    if (!display_port_init()) {
        Serial.println("[ERR] Display init failed");
        while (1) { delay(1000); }
    }

    touch_cst836u_init();

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_read);

    app_manager_init();   /* 加载主菜单 */
    Serial.println("KaiCam ready");
}

void loop() {
    lv_timer_handler();
    delay(5);
}
