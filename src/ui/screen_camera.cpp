#include <lvgl.h>

#include "app_fonts.h"
#include "app_log.h"
#include "app_manager.h"
#include "screen_camera.h"

static lv_obj_t *s_clock_label = NULL;
static lv_obj_t *s_btn_take = NULL;
static lv_obj_t *s_btn_take_label = NULL;
static lv_obj_t *s_btn_record = NULL;
static lv_obj_t *s_btn_record_label = NULL;
static lv_obj_t *s_record_time = NULL;
static lv_timer_t *s_refresh_timer = NULL;

static void apply_tabview_style(lv_obj_t *tabview) {
    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(tabview, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(tabview, 0, LV_PART_MAIN);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(tab_bar, 0, LV_PART_MAIN);

    lv_obj_set_style_text_color(tab_bar, lv_color_hex(0x4d4d4d), LV_PART_ITEMS);
    lv_obj_set_style_text_font(tab_bar, app_font_ui(), LV_PART_ITEMS);

    lv_obj_set_style_text_color(tab_bar, lv_color_hex(0x2195f6), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tab_bar, 4, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(tab_bar, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2195f6), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tab_bar, 60, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x2195f6), LV_PART_ITEMS | LV_STATE_CHECKED);
}

static void refresh_camera_ui(void) {
    app_datetime_t dt;
    app_manager_get_datetime(&dt);

    if (s_clock_label) {
        char clock_buf[16];
        lv_snprintf(clock_buf, sizeof(clock_buf), "%d:%02d", (int)dt.hour, (int)dt.minute);
        lv_label_set_text(s_clock_label, clock_buf);
    }

    app_camera_state_t state;
    app_manager_get_camera_state(&state);

    if (s_btn_record_label) {
        lv_label_set_text(s_btn_record_label, state.recording ? " " LV_SYMBOL_STOP " " : " " LV_SYMBOL_PLAY " ");
    }

    if (s_record_time) {
        uint32_t sec = state.record_seconds;
        uint32_t h = sec / 3600U;
        uint32_t m = (sec % 3600U) / 60U;
        uint32_t s = sec % 60U;
        lv_label_set_text_fmt(s_record_time, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    }

    if (s_btn_take) {
        if (state.preview_running) {
            lv_obj_clear_state(s_btn_take, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_btn_take, LV_STATE_DISABLED);
        }
    }
}

static void camera_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    refresh_camera_ui();
}

static void back_home_cb(lv_event_t *event) {
    LV_UNUSED(event);
    APP_LOGI("Camera: Home button clicked, navigate to HOME");
    app_manager_navigate_to(SCREEN_ID_HOME);
}

static void take_snapshot_cb(lv_event_t *event) {
    LV_UNUSED(event);
    bool ok = app_manager_camera_take_snapshot();
    APP_LOGI("Camera: snapshot result=%d", (int)ok);
}

static void record_toggle_cb(lv_event_t *event) {
    LV_UNUSED(event);
    app_camera_state_t state;
    app_manager_get_camera_state(&state);
    if (!state.preview_running) {
        (void)app_manager_camera_toggle_preview();
    }
    bool recording = app_manager_camera_toggle_record();
    APP_LOGI("Camera: record toggled, recording=%d", (int)recording);
    refresh_camera_ui();
}

static void screen_delete_cb(lv_event_t *event) {
    LV_UNUSED(event);
    APP_LOGI("Camera: screen delete start");
    if (s_refresh_timer) {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }

    s_clock_label = NULL;
    s_btn_take = NULL;
    s_btn_take_label = NULL;
    s_btn_record = NULL;
    s_btn_record_label = NULL;
    s_record_time = NULL;
    APP_LOGI("Camera: screen delete done");
}

lv_obj_t *screen_camera_create(void) {
    APP_LOGI("Camera: create start");
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 320, 240);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(scr, app_font_ui(), LV_PART_MAIN);
    lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *camera_view = lv_obj_create(scr);
    lv_obj_set_pos(camera_view, 0, 0);
    lv_obj_set_size(camera_view, 320, 240);
    lv_obj_clear_flag(camera_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(camera_view, 0, LV_PART_MAIN);
    lv_obj_set_style_border_width(camera_view, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_color(camera_view, lv_color_hex(0xf5f7fa), LV_PART_MAIN);


    // Removed placeholder and preview button to align with design

    lv_obj_t *tabview = lv_tabview_create(scr);
    lv_obj_set_pos(tabview, 0, 0);
    lv_obj_set_size(tabview, 320, 210);
    lv_obj_clear_flag(tabview, LV_OBJ_FLAG_SCROLLABLE);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 30);
    apply_tabview_style(tabview);

    lv_obj_t *tab_snap = lv_tabview_add_tab(tabview, "Snap");
    lv_obj_t *tab_record = lv_tabview_add_tab(tabview, "Record");

    s_btn_take = lv_button_create(tab_snap);
    lv_obj_set_pos(s_btn_take, 230, 100);
    lv_obj_set_size(s_btn_take, 40, 40);
    lv_obj_set_style_bg_opa(s_btn_take, 129, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_btn_take, lv_color_hex(0x2F92DA), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_btn_take, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_take, 25, LV_PART_MAIN);
    lv_obj_add_event_cb(s_btn_take, take_snapshot_cb, LV_EVENT_CLICKED, NULL);
    s_btn_take_label = lv_label_create(s_btn_take);
    lv_label_set_text(s_btn_take_label, " " LV_SYMBOL_OK " ");
    lv_obj_center(s_btn_take_label);

    s_btn_record = lv_button_create(tab_record);
    lv_obj_set_pos(s_btn_record, 230, 100);
    lv_obj_set_size(s_btn_record, 40, 40);
    lv_obj_set_style_bg_opa(s_btn_record, 129, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_btn_record, lv_color_hex(0x2F92DA), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_btn_record, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(s_btn_record, 25, LV_PART_MAIN);
    lv_obj_add_event_cb(s_btn_record, record_toggle_cb, LV_EVENT_CLICKED, NULL);
    s_btn_record_label = lv_label_create(s_btn_record);
    lv_label_set_text(s_btn_record_label, " " LV_SYMBOL_PLAY " ");
    lv_obj_center(s_btn_record_label);

    s_record_time = lv_label_create(tab_record);
    lv_obj_set_pos(s_record_time, 0, 0);
    lv_obj_set_size(s_record_time, 100, 16);
    lv_obj_set_style_text_align(s_record_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_record_time, 66, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_record_time, lv_color_hex(0x2195f6), LV_PART_MAIN);
    lv_label_set_text(s_record_time, "00:00:00");

    lv_obj_t *btn_back = lv_button_create(scr);
    lv_obj_set_pos(btn_back, 0, 210);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_back, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0x2F92DA), LV_PART_MAIN);
    lv_obj_set_style_border_side(btn_back, LV_BORDER_SIDE_NONE, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn_back, back_home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_back_label = lv_label_create(btn_back);
    lv_label_set_text(btn_back_label, LV_SYMBOL_HOME " Home");
    lv_obj_center(btn_back_label);

    s_clock_label = lv_label_create(scr);
    lv_obj_set_pos(s_clock_label, 270, 210);
    lv_obj_set_size(s_clock_label, 50, 30);
    lv_obj_set_style_text_align(s_clock_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_clock_label, app_font_ui(), LV_PART_MAIN);
    lv_obj_move_foreground(s_clock_label);

    (void)app_manager_camera_toggle_preview();

    s_refresh_timer = lv_timer_create(camera_timer_cb, 1000, NULL);
    refresh_camera_ui();

    APP_LOGI("Camera: create done");
    return scr;
}
