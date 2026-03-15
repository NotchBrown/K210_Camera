#pragma once

#include <stdint.h>

/* ── 屏幕 ID ────────────────────────────────────────────── */
typedef enum {
    SCREEN_ID_HOME     = 0,
    SCREEN_ID_SETTINGS = 1,
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

/* ── API ────────────────────────────────────────────────── */
void app_manager_init(void);
void app_manager_navigate_to(app_screen_id_t id);
void app_manager_get_datetime(app_datetime_t *dt);
void app_manager_set_datetime(const app_datetime_t *dt);
void app_manager_get_profile(app_profile_t *profile);
void app_manager_set_profile(const app_profile_t *profile);
