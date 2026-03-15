#include <lvgl.h>
#include <string.h>

#include "app_manager.h"
#include "rtc_driver.h"
#include "screen_home.h"
#include "screen_settings.h"

/* ── 全局状态 ────────────────────────────────────────────── */
static app_screen_id_t s_current_id  = SCREEN_ID_HOME;
static bool            s_initialized = false;

static bool           s_rtc_ready = false;
static app_profile_t  s_profile = { "张三", "13800000000" };

static bool is_leap_year(int16_t year) {
    return ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
}

static int8_t days_in_month(int16_t year, int8_t month) {
    static const int8_t k_days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month == 2) {
        return (int8_t)(is_leap_year(year) ? 29 : 28);
    }
    if (month < 1 || month > 12) {
        return 31;
    }
    return k_days[month - 1];
}

static void normalize_datetime(app_datetime_t *dt) {
    if (dt->year < 2000) dt->year = 2000;
    if (dt->year > 2099) dt->year = 2099;
    if (dt->month < 1) dt->month = 1;
    if (dt->month > 12) dt->month = 12;

    int8_t max_day = days_in_month(dt->year, dt->month);
    if (dt->day < 1) dt->day = 1;
    if (dt->day > max_day) dt->day = max_day;

    if (dt->hour < 0) dt->hour = 0;
    if (dt->hour > 23) dt->hour = 23;
    if (dt->minute < 0) dt->minute = 0;
    if (dt->minute > 59) dt->minute = 59;
    if (dt->second < 0) dt->second = 0;
    if (dt->second > 59) dt->second = 59;
}

static bool rtc_time_is_reasonable(const app_datetime_t *dt) {
    if (dt->year < 2020 || dt->year > 2099) return false;
    if (dt->month < 1 || dt->month > 12) return false;
    if (dt->day < 1 || dt->day > days_in_month(dt->year, dt->month)) return false;
    if (dt->hour < 0 || dt->hour > 23) return false;
    if (dt->minute < 0 || dt->minute > 59) return false;
    if (dt->second < 0 || dt->second > 59) return false;
    return true;
}

static bool rtc_read_datetime(app_datetime_t *dt) {
    if (!s_rtc_ready) return false;
    if (!rtc_driver_get_datetime(dt)) return false;
    normalize_datetime(dt);
    return rtc_time_is_reasonable(dt);
}

static bool rtc_write_datetime(const app_datetime_t *dt) {
    if (!s_rtc_ready) return false;

    app_datetime_t normalized = *dt;
    normalize_datetime(&normalized);
    return rtc_driver_set_datetime(&normalized);
}

static void rtc_bootstrap(void) {
    static const app_datetime_t k_default_dt = { 2025, 1, 1, 12, 0, 0 };

    s_rtc_ready = rtc_driver_init();
    if (!s_rtc_ready) {
        return;
    }

    app_datetime_t current_dt;
    if (!rtc_read_datetime(&current_dt)) {
        rtc_write_datetime(&k_default_dt);
    }
}

static void sec_timer_cb(lv_timer_t *t) {
    LV_UNUSED(t);
    if (!s_rtc_ready) {
        return;
    }
}

/* ── 公开 API ────────────────────────────────────────────── */
void app_manager_get_datetime(app_datetime_t *dt) {
    if (!rtc_read_datetime(dt)) {
        static const app_datetime_t k_fallback_dt = { 2025, 1, 1, 12, 0, 0 };
        *dt = k_fallback_dt;
    }
}

void app_manager_set_datetime(const app_datetime_t *dt) {
    app_datetime_t normalized = *dt;
    normalize_datetime(&normalized);

    if (!rtc_write_datetime(&normalized)) {
        return;
    }
}

void app_manager_get_profile(app_profile_t *profile) {
    *profile = s_profile;
}

void app_manager_set_profile(const app_profile_t *profile) {
    s_profile = *profile;
    s_profile.name[sizeof(s_profile.name) - 1] = '\0';
    s_profile.phone[sizeof(s_profile.phone) - 1] = '\0';
}

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
    rtc_bootstrap();
    lv_timer_create(sec_timer_cb, 1000, NULL);
    app_manager_navigate_to(SCREEN_ID_HOME);
}
