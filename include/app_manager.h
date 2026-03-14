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

/* ── 共用调色板（hex 字面量，供各屏幕直接使用） ─────────────── */
#define APP_COL_BG      0x0F172A   /* 深夜蓝背景  */
#define APP_COL_SURFACE 0x1E293B   /* 卡片/顶栏   */
#define APP_COL_ELEM    0x334155   /* 次要元素    */
#define APP_COL_ACCENT  0x3B82F6   /* 主蓝色      */
#define APP_COL_TEXT    0xF1F5F9   /* 主文字      */
#define APP_COL_MUTED   0x64748B   /* 辅助文字    */

/* ── API ────────────────────────────────────────────────── */
void app_manager_init(void);
void app_manager_navigate_to(app_screen_id_t id);
void app_manager_get_datetime(app_datetime_t *dt);
void app_manager_set_datetime(const app_datetime_t *dt);
