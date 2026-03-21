#include <lvgl.h>
#include "kendryte-standalone-sdk/lib/freertos/include/FreeRTOS.h"
#include "kendryte-standalone-sdk/lib/freertos/include/semphr.h"
#include "kendryte-standalone-sdk/lib/freertos/include/task.h"

#include <string.h>
#include <stdio.h>

#include "app_log.h"
#include "app_manager.h"
#include "rtc_driver.h"
#include "screen_camera.h"
#include "screen_file_manager.h"
#include "screen_home.h"
#include "screen_settings.h"
#include "screen_waiting.h"
#include "sd_storage_service.h"

/* ── 全局状态 ────────────────────────────────────────────── */
static app_screen_id_t s_current_id  = SCREEN_ID_HOME;
static bool            s_initialized = false;
static SemaphoreHandle_t s_lock = NULL;

static bool           s_rtc_ready = false;
static app_profile_t  s_profile = { "User", "1145141919810" };
static app_camera_settings_t s_camera_settings = {
    2, 3, 0, 32,
    true, true, true, false,
    false, false, false,
    true, true
};
static app_camera_state_t s_camera_state = { false, false, 0, 0 };
static app_system_status_t s_system_status = {
    false, false, 0, 0, "Storage is not checked yet."
};

typedef enum {
    WAITING_MODE_NONE = 0,
    WAITING_MODE_NAVIGATE = 1,
    WAITING_MODE_STORAGE_CHECK = 2,
} waiting_mode_t;

static const uint32_t k_waiting_navigate_delay_ms = 80U;
static const uint32_t k_waiting_storage_start_delay_ms = 120U;
static const uint32_t k_waiting_storage_min_visible_ms = 280U;

static waiting_mode_t s_waiting_mode = WAITING_MODE_NONE;
static app_screen_id_t s_waiting_target_id = SCREEN_ID_HOME;
static char s_waiting_message[96] = "Please wait...";
static lv_timer_t *s_waiting_timer = NULL;
static uint32_t s_waiting_started_ms = 0;
static bool s_waiting_storage_request_sent = false;

static void load_screen(app_screen_id_t id);
static void start_waiting(waiting_mode_t mode, app_screen_id_t target_id, const char *message);
static void waiting_timer_cb(lv_timer_t *timer);

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

static void stop_waiting_timer(void) {
    if (s_waiting_timer) {
        lv_timer_delete(s_waiting_timer);
        s_waiting_timer = NULL;
    }
    s_waiting_mode = WAITING_MODE_NONE;
    s_waiting_started_ms = 0;
    s_waiting_storage_request_sent = false;
}

static void load_screen(app_screen_id_t id) {
    lv_obj_t *new_scr = NULL;
    lv_obj_t *old_scr = lv_screen_active();

    APP_LOGI("AppMgr: load screen from %d to %d", (int)s_current_id, (int)id);

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
        default:
            return;
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

static void start_waiting(waiting_mode_t mode, app_screen_id_t target_id, const char *message) {
    stop_waiting_timer();
    s_waiting_mode = mode;
    s_waiting_target_id = target_id;
    s_waiting_started_ms = lv_tick_get();
    s_waiting_storage_request_sent = false;
    snprintf(s_waiting_message, sizeof(s_waiting_message), "%s", message ? message : "Please wait...");

    screen_waiting_show(s_waiting_message);

    s_waiting_timer = lv_timer_create(waiting_timer_cb, 40, NULL);
}

static void waiting_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);

    uint32_t elapsed_ms = lv_tick_elaps(s_waiting_started_ms);

    if (s_waiting_mode == WAITING_MODE_NAVIGATE) {
        if (elapsed_ms >= k_waiting_navigate_delay_ms) {
            stop_waiting_timer();
            screen_waiting_close();
            load_screen(s_waiting_target_id);
        }
        return;
    }

    if (s_waiting_mode == WAITING_MODE_STORAGE_CHECK) {
        if (!s_waiting_storage_request_sent && elapsed_ms >= k_waiting_storage_start_delay_ms) {
            s_waiting_storage_request_sent = true;
            screen_waiting_set_message("Checking SD space...");
            sd_storage_service_request_check();
        }

        if (s_waiting_storage_request_sent && elapsed_ms >= k_waiting_storage_min_visible_ms) {
            app_system_status_t status;
            app_manager_get_system_status(&status);
            if (status.storage_checked) {
                stop_waiting_timer();
                screen_waiting_close();
            }
        }
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

    APP_LOGI("AppMgr: camera settings saved res=%u agc_ceiling=%u ae=%d edge_val=%u edge_enh=%d edge_auto=%d agc=%d aec=%d awb=%d awb_gain=%d test=%d hm=%d vf=%d",
             (unsigned)s.capture_res_index,
             (unsigned)s.agc_ceiling_index,
             (int)s.ae_level,
             (unsigned)s.edge_val,
             (int)s.edge_enh,
             (int)s.edge_auto,
             (int)s.agc,
             (int)s.aec,
             (int)s.awb,
             (int)s.awb_gain,
             (int)s.test_mode,
             (int)s.h_mirror,
             (int)s.v_flip);
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
    sd_storage_status_t ss;
    sd_storage_service_get_status(&ss);

    state_lock();
    s_system_status.storage_checked = ss.checked;
    s_system_status.storage_available = ss.available;
    s_system_status.storage_total_kb = ss.total_kb;
    s_system_status.storage_free_kb = ss.free_kb;
    snprintf(s_system_status.storage_message, sizeof(s_system_status.storage_message),
             "%s", ss.message);
    *status = s_system_status;
    state_unlock();
}

void app_manager_request_storage_check(void) {
    APP_LOGI("AppMgr: request check queued");
    start_waiting(WAITING_MODE_STORAGE_CHECK, SCREEN_ID_SETTINGS, "Preparing SD check...");
}

void app_manager_request_storage_format(void) {
    APP_LOGI("AppMgr: request format queued");
    sd_storage_service_request_format();
}

bool app_manager_storage_list_root(char *out, uint32_t out_len) {
    return sd_storage_service_list_root(out, out_len);
}

bool app_manager_storage_list_dir(const char *path, char *out, uint32_t out_len) {
    return sd_storage_service_list_dir(path, out, out_len);
}

bool app_manager_storage_mkdir(const char *path, char *msg, uint32_t msg_len) {
    return sd_storage_service_mkdir(path, msg, msg_len);
}

bool app_manager_storage_delete(const char *path, char *msg, uint32_t msg_len) {
    return sd_storage_service_delete(path, msg, msg_len);
}

bool app_manager_storage_copy(const char *from, const char *to, char *msg, uint32_t msg_len) {
    return sd_storage_service_copy(from, to, msg, msg_len);
}

bool app_manager_storage_rename(const char *from, const char *to, char *msg, uint32_t msg_len) {
    return sd_storage_service_rename(from, to, msg, msg_len);
}

bool app_manager_storage_touch_file(const char *path, char *msg, uint32_t msg_len) {
    return sd_storage_service_touch_file(path, msg, msg_len);
}

bool app_manager_storage_list_root_async(char *out, uint32_t out_len, app_storage_callback_t callback, void *user_data) {
    return sd_storage_service_list_root_async(out, out_len, callback, user_data);
}

bool app_manager_storage_list_dir_async(const char *path, char *out, uint32_t out_len, app_storage_callback_t callback, void *user_data) {
    return sd_storage_service_list_dir_async(path, out, out_len, callback, user_data);
}

bool app_manager_storage_mkdir_async(const char *path, char *msg, uint32_t msg_len, app_storage_callback_t callback, void *user_data) {
    return sd_storage_service_mkdir_async(path, msg, msg_len, callback, user_data);
}

bool app_manager_storage_delete_async(const char *path, char *msg, uint32_t msg_len, app_storage_callback_t callback, void *user_data) {
    return sd_storage_service_delete_async(path, msg, msg_len, callback, user_data);
}

bool app_manager_storage_copy_async(const char *from, const char *to, char *msg, uint32_t msg_len, app_storage_callback_t callback, void *user_data) {
    return sd_storage_service_copy_async(from, to, msg, msg_len, callback, user_data);
}

bool app_manager_storage_rename_async(const char *from, const char *to, char *msg, uint32_t msg_len, app_storage_callback_t callback, void *user_data) {
    return sd_storage_service_rename_async(from, to, msg, msg_len, callback, user_data);
}

bool app_manager_storage_touch_file_async(const char *path, char *msg, uint32_t msg_len, app_storage_callback_t callback, void *user_data) {
    return sd_storage_service_touch_file_async(path, msg, msg_len, callback, user_data);
}

void app_manager_navigate_to(app_screen_id_t id) {
    APP_LOGI("AppMgr: navigate from %d to %d", (int)s_current_id, (int)id);
    start_waiting(WAITING_MODE_NAVIGATE, id, "Loading page...");
}

void app_manager_init(void) {
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        APP_LOGE("AppMgr: mutex create failed");
    }

    rtc_bootstrap();
    
    s_system_status.storage_checked = false;
    s_system_status.storage_available = false;
    s_system_status.storage_total_kb = 0;
    s_system_status.storage_free_kb = 0;
    snprintf(s_system_status.storage_message, sizeof(s_system_status.storage_message),
             "%s", "Storage not checked. Open Settings > SD FS and tap Check.");
    
    APP_LOGI("AppMgr: init done, load home");
    load_screen(SCREEN_ID_HOME);
}

void app_manager_service_task(void *arg) {
    LV_UNUSED(arg);
    uint32_t elapsed_ms = 0;
    APP_LOGI("AppMgr: service task started");

    for (;;) {
        /* wait for notification or timeout (200ms) */
        vTaskDelay(pdMS_TO_TICKS(200));
        elapsed_ms += 200U;

        state_lock();
        if (elapsed_ms >= 1000U) {
            elapsed_ms -= 1000U;
            if (s_camera_state.preview_running && s_camera_state.recording) {
                s_camera_state.record_seconds++;
            }
        }
        state_unlock();
    }
}
