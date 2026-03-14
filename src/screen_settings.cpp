/*
 * screen_settings.cpp
 * 系统设置界面 — 日期与时间调节
 *
 * 布局 (320×240 横屏)：
 *   ┌─[← 返回]──[系统设置]──[✓ 保存]─┐  38px 顶栏
 *   │  ┌───────────────────────────┐  │
 *   │  │ 日期                      │  │
 *   │  │  [2025] /  [01] /  [01]  │  │  滚轮卡片（可滚动）
 *   │  └───────────────────────────┘  │
 *   │  ┌───────────────────────────┐  │
 *   │  │ 时间                      │  │
 *   │  │    [00]   :   [00]       │  │
 *   │  └───────────────────────────┘  │
 *   └──────────────────────────────────┘
 *
 * 滚轮控件 (lv_roller)，3行可见，手指上下滑动选择值。
 * 保存后日期时间写回 app_manager；返回不保存。
 */
#include <lvgl.h>
#include <string.h>

#include "app_manager.h"
#include "screen_settings.h"

/* ── 页面私有 roller 句柄 ─────────────────────────────────── */
static lv_obj_t *s_year_roller  = NULL;
static lv_obj_t *s_month_roller = NULL;
static lv_obj_t *s_day_roller   = NULL;
static lv_obj_t *s_hour_roller  = NULL;
static lv_obj_t *s_min_roller   = NULL;

/* ── 事件回调 ─────────────────────────────────────────────── */
static void back_cb(lv_event_t *e) {
    LV_UNUSED(e);
    app_manager_navigate_to(SCREEN_ID_HOME);
}

static void save_cb(lv_event_t *e) {
    LV_UNUSED(e);
    app_datetime_t dt;
    dt.year   = (int16_t)(2020 + (int)lv_roller_get_selected(s_year_roller));
    dt.month  = (int8_t)(1    + (int)lv_roller_get_selected(s_month_roller));
    dt.day    = (int8_t)(1    + (int)lv_roller_get_selected(s_day_roller));
    dt.hour   = (int8_t)(lv_roller_get_selected(s_hour_roller));
    dt.minute = (int8_t)(lv_roller_get_selected(s_min_roller));
    dt.second = 0;
    app_manager_set_datetime(&dt);
    app_manager_navigate_to(SCREEN_ID_HOME);
}

/* ── 辅助：创建一个带样式的 lv_roller ───────────────────────── */
static lv_obj_t *make_roller(lv_obj_t *parent, const char *opts, lv_coord_t w) {
    lv_obj_t *r = lv_roller_create(parent);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(r, 3);
    lv_obj_set_width(r, w);

    /* 背景 & 文字 */
    lv_obj_set_style_bg_color(r, lv_color_hex(APP_COL_ELEM), 0);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(r, lv_color_hex(APP_COL_TEXT), 0);
    lv_obj_set_style_border_color(r, lv_color_hex(APP_COL_ELEM), 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_radius(r, 8, 0);

    /* 选中行高亮 */
    lv_obj_set_style_bg_color(r, lv_color_hex(APP_COL_ACCENT), LV_PART_SELECTED);
    lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
    lv_obj_set_style_text_color(r, lv_color_hex(0xFFFFFF), LV_PART_SELECTED);

    return r;
}

/* ── 辅助：分隔符 label ──────────────────────────────────── */
static void make_sep(lv_obj_t *parent, const char *text) {
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(APP_COL_MUTED), 0);
}

/* ── 辅助：卡片容器（flex 列） ───────────────────────────── */
static lv_obj_t *make_card(lv_obj_t *parent, const char *title) {
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(APP_COL_SURFACE), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 10, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START,
                                LV_FLEX_ALIGN_START,
                                LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* 卡片标题 */
    lv_obj_t *lbl = lv_label_create(card);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(APP_COL_MUTED), 0);

    return card;
}

/* ── 辅助：滚轮行容器（flex 行，居中对齐） ───────────────────── */
static lv_obj_t *make_roller_row(lv_obj_t *parent) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START,
                               LV_FLEX_ALIGN_CENTER,
                               LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 6, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    return row;
}

/* ── 创建并返回系统设置屏幕对象 ───────────────────────────────── */
lv_obj_t *screen_settings_create(void) {
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

    /* 返回按钮（左） */
    lv_obj_t *back_btn = lv_button_create(hdr);
    lv_obj_set_size(back_btn, 72, 28);
    lv_obj_align(back_btn, LV_ALIGN_LEFT_MID, 8, 0);
    lv_obj_set_style_bg_color(back_btn, lv_color_hex(APP_COL_ELEM), 0);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_set_style_border_width(back_btn, 0, 0);
    lv_obj_set_style_shadow_width(back_btn, 0, 0);
    lv_obj_add_event_cb(back_btn, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, LV_SYMBOL_LEFT " 返回");
    lv_obj_set_style_text_color(back_lbl, lv_color_hex(APP_COL_TEXT), 0);
    lv_obj_center(back_lbl);

    /* 标题（中） */
    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, "系统设置");
    lv_obj_set_style_text_color(title, lv_color_hex(APP_COL_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

    /* 保存按钮（右） */
    lv_obj_t *save_btn = lv_button_create(hdr);
    lv_obj_set_size(save_btn, 72, 28);
    lv_obj_align(save_btn, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_obj_set_style_bg_color(save_btn, lv_color_hex(APP_COL_ACCENT), 0);
    lv_obj_set_style_radius(save_btn, 6, 0);
    lv_obj_set_style_border_width(save_btn, 0, 0);
    lv_obj_set_style_shadow_width(save_btn, 0, 0);
    lv_obj_add_event_cb(save_btn, save_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *save_lbl = lv_label_create(save_btn);
    lv_label_set_text(save_lbl, LV_SYMBOL_OK " 保存");
    lv_obj_set_style_text_color(save_lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(save_lbl);

    /* ── 内容区（顶栏以下，竖向滚动） ──────────────────────── */
    lv_obj_t *content = lv_obj_create(scr);
    lv_obj_set_size(content, 320, 202);
    lv_obj_set_pos(content, 0, 38);
    lv_obj_set_style_bg_color(content, lv_color_hex(APP_COL_BG), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_radius(content, 0, 0);
    lv_obj_set_style_pad_hor(content, 10, 0);
    lv_obj_set_style_pad_top(content, 8, 0);
    lv_obj_set_style_pad_bottom(content, 8, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(content, LV_FLEX_ALIGN_START,
                                   LV_FLEX_ALIGN_START,
                                   LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(content, 8, 0);

    /* ────────────────────────────────────────────────
     * 日期卡片
     *   年 (2020-2035)  /  月 (01-12)  /  日 (01-31)
     * ──────────────────────────────────────────────── */
    lv_obj_t *date_card = make_card(content, "日期");
    lv_obj_t *date_row  = make_roller_row(date_card);

    /* 年份选项字符串 */
    char year_buf[100] = "";
    for (int y = 2020; y <= 2035; y++) {
        char tmp[8];
        lv_snprintf(tmp, sizeof(tmp), (y == 2020) ? "%d" : "\n%d", y);
        strncat(year_buf, tmp, sizeof(year_buf) - strlen(year_buf) - 1);
    }
    s_year_roller = make_roller(date_row, year_buf, 82);
    make_sep(date_row, "/");

    s_month_roller = make_roller(date_row,
        "01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12", 52);
    make_sep(date_row, "/");

    /* 日期选项字符串 */
    char day_buf[100] = "";
    for (int d = 1; d <= 31; d++) {
        char tmp[6];
        lv_snprintf(tmp, sizeof(tmp), (d == 1) ? "%02d" : "\n%02d", d);
        strncat(day_buf, tmp, sizeof(day_buf) - strlen(day_buf) - 1);
    }
    s_day_roller = make_roller(date_row, day_buf, 52);

    /* ────────────────────────────────────────────────
     * 时间卡片
     *   时 (00-23)  :  分 (00-59)
     * ──────────────────────────────────────────────── */
    lv_obj_t *time_card = make_card(content, "时间");
    lv_obj_t *time_row  = make_roller_row(time_card);

    /* 小时选项字符串 */
    char hour_buf[150] = "";
    for (int h = 0; h <= 23; h++) {
        char tmp[6];
        lv_snprintf(tmp, sizeof(tmp), (h == 0) ? "%02d" : "\n%02d", h);
        strncat(hour_buf, tmp, sizeof(hour_buf) - strlen(hour_buf) - 1);
    }
    s_hour_roller = make_roller(time_row, hour_buf, 82);
    make_sep(time_row, ":");

    /* 分钟选项字符串 */
    char min_buf[200] = "";
    for (int m = 0; m <= 59; m++) {
        char tmp[6];
        lv_snprintf(tmp, sizeof(tmp), (m == 0) ? "%02d" : "\n%02d", m);
        strncat(min_buf, tmp, sizeof(min_buf) - strlen(min_buf) - 1);
    }
    s_min_roller = make_roller(time_row, min_buf, 82);

    /* ── 将滚轮定位到当前保存的日期时间 ─────────────────────── */
    app_datetime_t dt;
    app_manager_get_datetime(&dt);

    lv_roller_set_selected(s_year_roller,  (uint32_t)(dt.year  - 2020), LV_ANIM_OFF);
    lv_roller_set_selected(s_month_roller, (uint32_t)(dt.month - 1),    LV_ANIM_OFF);
    lv_roller_set_selected(s_day_roller,   (uint32_t)(dt.day   - 1),    LV_ANIM_OFF);
    lv_roller_set_selected(s_hour_roller,  (uint32_t)(dt.hour),          LV_ANIM_OFF);
    lv_roller_set_selected(s_min_roller,   (uint32_t)(dt.minute),        LV_ANIM_OFF);

    return scr;
}
