/*
 * screen_home.cpp
 * 主菜单界面 — KaiCam 启动页
 *
 * 布局 (320×240 横屏)：
 *   ┌─[KaiCam]──[日期]──[时间]─┐  38px 顶栏
 *   │                          │
 *   │   ┌──────────────────┐   │
 *   │   │  ⚙  系统设置      │   │  56px 主按钮
 *   │   └──────────────────┘   │
 *   │   更多功能即将推出         │
 *   └──────────────────────────┘
 */
#include <lvgl.h>

#include "app_manager.h"
#include "screen_home.h"

/* 页面私有状态 */
static lv_obj_t   *s_time_label  = NULL;
static lv_obj_t   *s_date_label  = NULL;
static lv_timer_t *s_clock_timer = NULL;

/* ── 每秒刷新顶栏时钟 ─────────────────────────────────────── */
static void clock_cb(lv_timer_t *t) {
    LV_UNUSED(t);
    if (!s_time_label) return;

    app_datetime_t dt;
    app_manager_get_datetime(&dt);

    char buf[20];
    lv_snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                (int)dt.hour, (int)dt.minute, (int)dt.second);
    lv_label_set_text(s_time_label, buf);

    lv_snprintf(buf, sizeof(buf), "%04d/%02d/%02d",
                (int)dt.year, (int)dt.month, (int)dt.day);
    lv_label_set_text(s_date_label, buf);
}

/* ── 跳转系统设置（销毁本页 timer 再切屏） ──────────────────── */
static void go_settings_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (s_clock_timer) {
        lv_timer_delete(s_clock_timer);
        s_clock_timer = NULL;
    }
    s_time_label = NULL;
    s_date_label = NULL;
    app_manager_navigate_to(SCREEN_ID_SETTINGS);
}

/* ── 创建并返回主菜单屏幕对象 ────────────────────────────────── */
lv_obj_t *screen_home_create(void) {
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(APP_COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* ── 顶栏 ───────────────────────────────────────────────── */
    lv_obj_t *hdr = lv_obj_create(scr);
    lv_obj_set_size(hdr, 320, 38);
    lv_obj_set_pos(hdr, 0, 0);
    lv_obj_set_style_bg_color(hdr, lv_color_hex(APP_COL_SURFACE), 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    /* App 名称（左） */
    lv_obj_t *app_name = lv_label_create(hdr);
    lv_label_set_text(app_name, "KaiCam");
    lv_obj_set_style_text_color(app_name, lv_color_hex(APP_COL_TEXT), 0);
    lv_obj_align(app_name, LV_ALIGN_LEFT_MID, 12, 0);

    /* 日期（中） */
    s_date_label = lv_label_create(hdr);
    lv_label_set_text(s_date_label, "----/--/--");
    lv_obj_set_style_text_color(s_date_label, lv_color_hex(APP_COL_MUTED), 0);
    lv_obj_align(s_date_label, LV_ALIGN_CENTER, 0, 0);

    /* 时间（右） */
    s_time_label = lv_label_create(hdr);
    lv_label_set_text(s_time_label, "--:--:--");
    lv_obj_set_style_text_color(s_time_label, lv_color_hex(APP_COL_MUTED), 0);
    lv_obj_align(s_time_label, LV_ALIGN_RIGHT_MID, -12, 0);

    /* ── 主按钮区域 ─────────────────────────────────────────── */
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_size(btn, 240, 56);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, -12);
    lv_obj_set_style_bg_color(btn, lv_color_hex(APP_COL_ACCENT), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, go_settings_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, LV_SYMBOL_SETTINGS "  系统设置");
    lv_obj_set_style_text_color(btn_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(btn_lbl);

    /* 提示文字（按钮下方） */
    lv_obj_t *hint = lv_label_create(scr);
    lv_label_set_text(hint, "更多功能即将推出");
    lv_obj_set_style_text_color(hint, lv_color_hex(APP_COL_ELEM), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 44);

    /* ── 启动时钟 timer ─────────────────────────────────────── */
    s_clock_timer = lv_timer_create(clock_cb, 1000, NULL);
    clock_cb(NULL);   /* 立即刷新，避免首帧显示占位符 */

    return scr;
}
