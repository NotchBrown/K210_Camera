#include <lvgl.h>
#include "kendryte-standalone-sdk/lib/freertos/include/FreeRTOS.h"
#include "kendryte-standalone-sdk/lib/freertos/include/semphr.h"
#include "kendryte-standalone-sdk/lib/freertos/include/task.h"

#include <string.h>

#include "app_log.h"
#include "app_manager.h"
#include "rtc_driver.h"
#include "screen_camera.h"
#include "screen_file_manager.h"
#include "screen_home.h"
#include "screen_settings.h"

/* ── 全局状态 ────────────────────────────────────────────── */
static app_screen_id_t s_current_id  = SCREEN_ID_HOME;
static bool            s_initialized = false;
static SemaphoreHandle_t s_lock = NULL;

static bool           s_rtc_ready = false;
static app_profile_t  s_profile = { "User", "1145141919810" };
static app_camera_settings_t s_camera_settings = {
    2, 3, 0,
    true, true, true, false,
    false, false, false
};
static app_camera_state_t s_camera_state = { false, false, 0, 0 };
static app_system_status_t s_system_status = {
    false, false, 0, 0, "Storage is not checked yet."
};

static volatile bool s_storage_check_req = false;
static volatile bool s_storage_format_req = false;

static void state_lock(void) {
    if (s_lock) {
        (void)xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void state_unlock(void) {
    if (s_lock) {
        (void)xSemaphoreGive(s_lock);
    }
}

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

/* ── 公开 API ────────────────────────────────────────────── */
void app_manager_get_datetime(app_datetime_t *dt) {
    static const app_datetime_t k_default_dt = { 2025, 1, 1, 12, 0, 0 };

    if (rtc_read_datetime(dt)) {
        return;
    }

    *dt = k_default_dt;
}

void app_manager_set_datetime(const app_datetime_t *dt) {
    app_datetime_t normalized = *dt;
    normalize_datetime(&normalized);
    (void)rtc_write_datetime(&normalized);
}

void app_manager_get_profile(app_profile_t *profile) {
    state_lock();
    *profile = s_profile;
    state_unlock();
}

void app_manager_set_profile(const app_profile_t *profile) {
    state_lock();
    s_profile = *profile;
    s_profile.name[sizeof(s_profile.name) - 1] = '\0';
    s_profile.phone[sizeof(s_profile.phone) - 1] = '\0';
    state_unlock();
}

void app_manager_get_camera_settings(app_camera_settings_t *settings) {
    state_lock();
    *settings = s_camera_settings;
    state_unlock();
}

void app_manager_set_camera_settings(const app_camera_settings_t *settings) {
    app_camera_settings_t s = *settings;
    if (s.capture_res_index > 5) s.capture_res_index = 5;
    if (s.agc_ceiling_index > 6) s.agc_ceiling_index = 6;
    if (s.ae_level < -2) s.ae_level = -2;
    if (s.ae_level > 2) s.ae_level = 2;

    state_lock();
    s_camera_settings = s;
    state_unlock();
}

void app_manager_get_camera_state(app_camera_state_t *state) {
    state_lock();
    *state = s_camera_state;
    state_unlock();
}

bool app_manager_camera_toggle_preview(void) {
    bool ret;
    state_lock();
    s_camera_state.preview_running = !s_camera_state.preview_running;
    if (!s_camera_state.preview_running) {
        s_camera_state.recording = false;
        s_camera_state.record_seconds = 0;
    }
    ret = s_camera_state.preview_running;
    state_unlock();
    return ret;
}

bool app_manager_camera_toggle_record(void) {
    bool ret;
    state_lock();
    if (!s_camera_state.preview_running) {
        state_unlock();
        return false;
    }
    s_camera_state.recording = !s_camera_state.recording;
    if (!s_camera_state.recording) {
        s_camera_state.record_seconds = 0;
    }
    ret = s_camera_state.recording;
    state_unlock();
    return ret;
}

bool app_manager_camera_take_snapshot(void) {
    bool ok = false;
    state_lock();
    if (s_camera_state.preview_running) {
        s_camera_state.snapshot_count++;
        ok = true;
    }
    state_unlock();
    return ok;
}

void app_manager_get_system_status(app_system_status_t *status) {
    state_lock();
    *status = s_system_status;
    state_unlock();
}

void app_manager_request_storage_check(void) {
    state_lock();
    s_storage_check_req = true;
    lv_snprintf(s_system_status.storage_message, sizeof(s_system_status.storage_message),
                "Checking storage...");
    state_unlock();
}

void app_manager_request_storage_format(void) {
    state_lock();
    s_storage_format_req = true;
    lv_snprintf(s_system_status.storage_message, sizeof(s_system_status.storage_message),
                "Formatting storage...");
    state_unlock();
}

void app_manager_navigate_to(app_screen_id_t id) {
    lv_obj_t *new_scr = NULL;
    lv_obj_t *old_scr = lv_screen_active();

    APP_LOGI("AppMgr: navigate from %d to %d", (int)s_current_id, (int)id);

    switch (id) {
        case SCREEN_ID_HOME:
            new_scr = screen_home_create();
            break;
        case SCREEN_ID_SETTINGS:
            new_scr = screen_settings_create();
            break;
        case SCREEN_ID_CAMERA:
            new_scr = screen_camera_create();
            break;
        case SCREEN_ID_FILE_MANAGER:
            new_scr = screen_file_manager_create();
            break;
        default: return;
    }

    if (new_scr == NULL) {
        APP_LOGE("AppMgr: create screen %d failed", (int)id);
        return;
    }

    if (!s_initialized) {
        s_initialized = true;
    }

    lv_screen_load(new_scr);
    if (old_scr && old_scr != new_scr && lv_obj_is_valid(old_scr)) {
        lv_obj_delete_async(old_scr);
    }
    s_current_id = id;
    APP_LOGI("AppMgr: screen load done id=%d", (int)id);
}

void app_manager_init(void) {
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        APP_LOGE("AppMgr: mutex create failed");
    }

    rtc_bootstrap();
    APP_LOGI("AppMgr: init done, load home");
    app_manager_navigate_to(SCREEN_ID_HOME);
}

void app_manager_service_task(void *arg) {
    LV_UNUSED(arg);
    TickType_t last = xTaskGetTickCount();
    uint32_t elapsed_ms = 0;

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(200));
        elapsed_ms += 200U;

        state_lock();

        if (s_storage_format_req) {
            s_storage_format_req = false;
            s_system_status.storage_checked = true;
            s_system_status.storage_available = true;
            s_system_status.storage_total_kb = 32768;
            s_system_status.storage_free_kb = 32000;
            lv_snprintf(s_system_status.storage_message, sizeof(s_system_status.storage_message),
                        "Format done: total=%luKB free=%luKB",
                        (unsigned long)s_system_status.storage_total_kb,
                        (unsigned long)s_system_status.storage_free_kb);
        } else if (s_storage_check_req) {
            s_storage_check_req = false;
            s_system_status.storage_checked = true;
            s_system_status.storage_available = true;
            s_system_status.storage_total_kb = 32768;
            s_system_status.storage_free_kb = 24576;
            lv_snprintf(s_system_status.storage_message, sizeof(s_system_status.storage_message),
                        "SD OK: total=%luKB free=%luKB",
                        (unsigned long)s_system_status.storage_total_kb,
                        (unsigned long)s_system_status.storage_free_kb);
        }

        if (elapsed_ms >= 1000U) {
            elapsed_ms -= 1000U;
            if (s_camera_state.preview_running && s_camera_state.recording) {
                s_camera_state.record_seconds++;
            }
        }

        state_unlock();
    }
}
