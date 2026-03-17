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
#include "storage_service.h"

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

static volatile bool s_storage_check_req = false;
static volatile bool s_storage_format_req = false;
static TaskHandle_t s_service_task_handle = NULL;

typedef enum {
    STORAGE_OP_NONE = 0,
    STORAGE_OP_LIST_ROOT,
    STORAGE_OP_LIST_DIR,
    STORAGE_OP_MKDIR,
    STORAGE_OP_DELETE,
    STORAGE_OP_COPY,
    STORAGE_OP_RENAME,
    STORAGE_OP_TOUCH_FILE,
} storage_op_t;

typedef struct {
    bool pending;
    bool result;
    storage_op_t op;
    TaskHandle_t requester;
    char path1[128];
    char path2[128];
    char *out;
    uint32_t out_len;
} storage_request_t;

static storage_request_t s_storage_req = { false, false, STORAGE_OP_NONE, NULL, "", "", NULL, 0 };

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

static bool dispatch_storage_op(storage_op_t op,
                                const char *path1,
                                const char *path2,
                                char *out,
                                uint32_t out_len) {
    switch (op) {
        case STORAGE_OP_LIST_ROOT:
            return storage_service_list_root(out, out_len);
        case STORAGE_OP_LIST_DIR:
            return storage_service_list_dir(path1, out, out_len);
        case STORAGE_OP_MKDIR:
            return storage_service_mkdir(path1, out, out_len);
        case STORAGE_OP_DELETE:
            return storage_service_delete(path1, out, out_len);
        case STORAGE_OP_COPY:
            return storage_service_copy(path1, path2, out, out_len);
        case STORAGE_OP_RENAME:
            return storage_service_rename(path1, path2, out, out_len);
        case STORAGE_OP_TOUCH_FILE:
            return storage_service_touch_file(path1, out, out_len);
        case STORAGE_OP_NONE:
        default:
            if (out && out_len > 0) {
                snprintf(out, out_len, "%s", "Unsupported op");
            }
            return false;
    }
}

static bool request_storage_op(storage_op_t op,
                               const char *path1,
                               const char *path2,
                               char *out,
                               uint32_t out_len) {
    TaskHandle_t self = xTaskGetCurrentTaskHandle();

    if (!s_service_task_handle || self == s_service_task_handle) {
        return dispatch_storage_op(op, path1, path2, out, out_len);
    }

    state_lock();
    if (s_storage_req.pending) {
        state_unlock();
        if (out && out_len > 0) {
            snprintf(out, out_len, "%s", "Storage service busy");
        }
        return false;
    }

    s_storage_req.pending = true;
    s_storage_req.result = false;
    s_storage_req.op = op;
    s_storage_req.requester = self;
    s_storage_req.out = out;
    s_storage_req.out_len = out_len;
    snprintf(s_storage_req.path1, sizeof(s_storage_req.path1), "%s", path1 ? path1 : "");
    snprintf(s_storage_req.path2, sizeof(s_storage_req.path2), "%s", path2 ? path2 : "");
    state_unlock();

    app_manager_wakeup();

    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) == 0) {
        state_lock();
        s_storage_req.pending = false;
        s_storage_req.op = STORAGE_OP_NONE;
        s_storage_req.requester = NULL;
        s_storage_req.out = NULL;
        s_storage_req.out_len = 0;
        state_unlock();

        if (out && out_len > 0) {
            snprintf(out, out_len, "%s", "Storage op timeout");
        }
        return false;
    }

    state_lock();
    bool ok = s_storage_req.result;
    s_storage_req.op = STORAGE_OP_NONE;
    s_storage_req.requester = NULL;
    s_storage_req.out = NULL;
    s_storage_req.out_len = 0;
    state_unlock();
    return ok;
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
    state_lock();
    *status = s_system_status;
    state_unlock();
}

void app_manager_request_storage_check(void) {
    state_lock();
    s_storage_check_req = true;
    s_system_status.storage_checked = false;
    snprintf(s_system_status.storage_message, sizeof(s_system_status.storage_message),
             "%s", "Checking storage...");
    state_unlock();
    APP_LOGI("AppMgr: request check queued");
    app_manager_wakeup();
}

void app_manager_request_storage_format(void) {
    state_lock();
    s_storage_format_req = true;
    s_system_status.storage_checked = false;
    snprintf(s_system_status.storage_message, sizeof(s_system_status.storage_message),
             "%s", "Formatting storage...");
    state_unlock();
    APP_LOGI("AppMgr: request format queued");
    app_manager_wakeup();
}

bool app_manager_storage_list_root(char *out, uint32_t out_len) {
    return request_storage_op(STORAGE_OP_LIST_ROOT, NULL, NULL, out, out_len);
}

bool app_manager_storage_list_dir(const char *path, char *out, uint32_t out_len) {
    return request_storage_op(STORAGE_OP_LIST_DIR, path, NULL, out, out_len);
}

bool app_manager_storage_mkdir(const char *path, char *msg, uint32_t msg_len) {
    return request_storage_op(STORAGE_OP_MKDIR, path, NULL, msg, msg_len);
}

bool app_manager_storage_delete(const char *path, char *msg, uint32_t msg_len) {
    return request_storage_op(STORAGE_OP_DELETE, path, NULL, msg, msg_len);
}

bool app_manager_storage_copy(const char *from, const char *to, char *msg, uint32_t msg_len) {
    return request_storage_op(STORAGE_OP_COPY, from, to, msg, msg_len);
}

bool app_manager_storage_rename(const char *from, const char *to, char *msg, uint32_t msg_len) {
    return request_storage_op(STORAGE_OP_RENAME, from, to, msg, msg_len);
}

bool app_manager_storage_touch_file(const char *path, char *msg, uint32_t msg_len) {
    return request_storage_op(STORAGE_OP_TOUCH_FILE, path, NULL, msg, msg_len);
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
    
    s_system_status.storage_checked = false;
    s_system_status.storage_available = false;
    s_system_status.storage_total_kb = 0;
    s_system_status.storage_free_kb = 0;
    snprintf(s_system_status.storage_message, sizeof(s_system_status.storage_message),
             "%s", "Storage auto-check pending...");
    s_storage_check_req = true;
    
    APP_LOGI("AppMgr: init done, load home");
    app_manager_navigate_to(SCREEN_ID_HOME);
}

void app_manager_service_task(void *arg) {
    LV_UNUSED(arg);
    uint32_t elapsed_ms = 0;
    uint32_t storage_retry_ms = 0;
    bool first_check_pending = true;
    APP_LOGI("AppMgr: service task started");
    s_service_task_handle = xTaskGetCurrentTaskHandle();

    for (;;) {
        /* wait for notification or timeout (200ms) */
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(200));
        elapsed_ms += 200U;

        bool do_format = false;
        bool do_check = false;
        bool storage_available = false;

        state_lock();
        if (s_storage_format_req) {
            s_storage_format_req = false;
            do_format = true;
        } else if (s_storage_check_req) {
            s_storage_check_req = false;
            do_check = true;
        }
        storage_available = s_system_status.storage_available;
        state_unlock();

        // Run first check immediately after task start.
        if (!do_format && !do_check && first_check_pending) {
            first_check_pending = false;
            do_check = true;
            APP_LOGI("AppMgr: first storage check");
        }

        // Auto-retry storage check in background until available.
        if (!do_format && !do_check && !storage_available) {
            storage_retry_ms += 200U;
            if (storage_retry_ms >= 3000U) {
                storage_retry_ms = 0;
                do_check = true;
                state_lock();
                snprintf(s_system_status.storage_message, sizeof(s_system_status.storage_message),
                         "%s", "Auto checking storage...");
                state_unlock();
                APP_LOGI("AppMgr: auto storage check");
            }
        }

        if (do_format) {
            APP_LOGI("AppMgr: start storage format");
            uint32_t total_kb = 0;
            uint32_t free_kb = 0;
            char message[96];
            bool ok = storage_service_format(&total_kb, &free_kb, message, sizeof(message));

            state_lock();
            s_system_status.storage_checked = true;
            s_system_status.storage_available = ok;
            s_system_status.storage_total_kb = total_kb;
            s_system_status.storage_free_kb = free_kb;
            snprintf(s_system_status.storage_message, sizeof(s_system_status.storage_message),
                     "%s", message);
            state_unlock();
            APP_LOGI("Storage format done: ok=%d msg=%s", ok ? 1 : 0, message);
        } else if (do_check) {
            APP_LOGI("AppMgr: start storage check");
            uint32_t total_kb = 0;
            uint32_t free_kb = 0;
            char message[96];
            bool ok = storage_service_check(&total_kb, &free_kb, message, sizeof(message));

            state_lock();
            s_system_status.storage_checked = true;
            s_system_status.storage_available = ok;
            s_system_status.storage_total_kb = total_kb;
            s_system_status.storage_free_kb = free_kb;
            snprintf(s_system_status.storage_message, sizeof(s_system_status.storage_message),
                     "%s", message);
            state_unlock();
            APP_LOGI("Storage check done: ok=%d msg=%s", ok ? 1 : 0, message);
            if (ok) {
                storage_retry_ms = 0;
            }
        }

        storage_request_t req_local = { false, false, STORAGE_OP_NONE, NULL, "", "", NULL, 0 };
        bool do_req = false;

        state_lock();
        if (s_storage_req.pending) {
            req_local = s_storage_req;
            s_storage_req.pending = false;
            do_req = true;
        }
        state_unlock();

        if (do_req) {
            bool ok = dispatch_storage_op(req_local.op,
                                          req_local.path1,
                                          req_local.path2,
                                          req_local.out,
                                          req_local.out_len);

            state_lock();
            s_storage_req.result = ok;
            state_unlock();

            if (req_local.requester) {
                xTaskNotifyGive(req_local.requester);
            }
        }

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

void app_manager_wakeup(void) {
    if (s_service_task_handle) {
        xTaskNotifyGive(s_service_task_handle);
    }
}
