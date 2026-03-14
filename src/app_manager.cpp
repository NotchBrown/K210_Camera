#include <lvgl.h>

#include "app_manager.h"
#include "screen_home.h"
#include "screen_settings.h"

/* ── 全局状态 ────────────────────────────────────────────── */
static app_screen_id_t s_current_id  = SCREEN_ID_HOME;
static bool            s_initialized = false;

static app_datetime_t s_dt = { 2025, 1, 1, 0, 0, 0 };

/* ── 秒级 tick（每秒由 LVGL timer 驱动） ─────────────────── */
static void sec_timer_cb(lv_timer_t *t) {
    LV_UNUSED(t);
    s_dt.second++;
    if (s_dt.second >= 60) { s_dt.second = 0; s_dt.minute++; }
    if (s_dt.minute >= 60) { s_dt.minute = 0; s_dt.hour++;   }
    if (s_dt.hour   >= 24) { s_dt.hour   = 0;                }
}

/* ── 公开 API ────────────────────────────────────────────── */
void app_manager_get_datetime(app_datetime_t *dt) { *dt = s_dt; }
void app_manager_set_datetime(const app_datetime_t *dt) { s_dt = *dt; }

void app_manager_navigate_to(app_screen_id_t id) {
    lv_obj_t *new_scr = NULL;

    switch (id) {
        case SCREEN_ID_HOME:     new_scr = screen_home_create();     break;
        case SCREEN_ID_SETTINGS: new_scr = screen_settings_create(); break;
        default: return;
    }

    lv_screen_load_anim_t anim;
    if (!s_initialized) {
        anim = LV_SCREEN_LOAD_ANIM_NONE;   /* 首次加载无动画 */
        s_initialized = true;
    } else if (id > s_current_id) {
        anim = LV_SCREEN_LOAD_ANIM_MOVE_LEFT;   /* 向前：右滑入 */
    } else {
        anim = LV_SCREEN_LOAD_ANIM_MOVE_RIGHT;  /* 返回：左滑入 */
    }

    lv_screen_load_anim(new_scr, anim, 250, 0, true);
    s_current_id = id;
}

void app_manager_init(void) {
    lv_timer_create(sec_timer_cb, 1000, NULL);   /* 全局秒针 */
    app_manager_navigate_to(SCREEN_ID_HOME);
}
