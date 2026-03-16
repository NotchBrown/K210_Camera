#pragma once

#include <stdint.h>

/* ── 屏幕 ID ────────────────────────────────────────────── */
typedef enum {
    SCREEN_ID_HOME     = 0,
    SCREEN_ID_SETTINGS = 1,
    SCREEN_ID_CAMERA   = 2,
    SCREEN_ID_FILE_MANAGER = 3,
} app_screen_id_t;

/* ── 全局日期时间 ─────────────────────────────────────────── */
typedef struct {
    int16_t year;    /* 2020 ~ 2035 */
    int8_t  month;   /* 1 ~ 12      */
    int8_t  day;     /* 1 ~ 31      */
    int8_t  hour;    /* 0 ~ 23      */
    int8_t  minute;  /* 0 ~ 59      */
    int8_t  second;  /* 0 ~ 59      */
} app_datetime_t;

typedef struct {
    char name[32];
    char phone[20];
} app_profile_t;

typedef struct {
    uint8_t capture_res_index; /* 0..5 */
    uint8_t agc_ceiling_index; /* 0..6 */
    int8_t  ae_level;          /* -2..2 */
    bool agc;
    bool aec;
    bool awb;
    bool awb_gain;
    bool test_mode;
    bool h_mirror;
    bool v_flip;
} app_camera_settings_t;

typedef struct {
    bool preview_running;
    bool recording;
    uint32_t record_seconds;
    uint32_t snapshot_count;
} app_camera_state_t;

typedef struct {
    bool storage_checked;
    bool storage_available;
    uint32_t storage_total_kb;
    uint32_t storage_free_kb;
    char storage_message[96];
} app_system_status_t;

/* ── API ────────────────────────────────────────────────── */
void app_manager_init(void);
void app_manager_navigate_to(app_screen_id_t id);
void app_manager_get_datetime(app_datetime_t *dt);
void app_manager_set_datetime(const app_datetime_t *dt);
void app_manager_get_profile(app_profile_t *profile);
void app_manager_set_profile(const app_profile_t *profile);

void app_manager_get_camera_settings(app_camera_settings_t *settings);
void app_manager_set_camera_settings(const app_camera_settings_t *settings);
void app_manager_get_camera_state(app_camera_state_t *state);
bool app_manager_camera_toggle_preview(void);
bool app_manager_camera_toggle_record(void);
bool app_manager_camera_take_snapshot(void);

void app_manager_get_system_status(app_system_status_t *status);
void app_manager_request_storage_check(void);
void app_manager_request_storage_format(void);
bool app_manager_storage_list_root(char *out, uint32_t out_len);
bool app_manager_storage_list_dir(const char *path, char *out, uint32_t out_len);
bool app_manager_storage_mkdir(const char *path, char *msg, uint32_t msg_len);
bool app_manager_storage_delete(const char *path, char *msg, uint32_t msg_len);
bool app_manager_storage_copy(const char *from, const char *to, char *msg, uint32_t msg_len);
bool app_manager_storage_rename(const char *from, const char *to, char *msg, uint32_t msg_len);
bool app_manager_storage_touch_file(const char *path, char *msg, uint32_t msg_len);

/* Wake up the background service task immediately (non-blocking) */
void app_manager_wakeup(void);
/* 后台任务：独立于 LVGL 线程处理硬件/状态刷新 */
void app_manager_service_task(void *arg);
