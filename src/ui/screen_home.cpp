#include <lvgl.h>

#include "app_fonts.h"
#include "app_log.h"
#include "app_manager.h"
#include "screen_home.h"

LV_IMAGE_DECLARE(_settings_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24_RGB565_24x24);
LV_IMAGE_DECLARE(_photo_camera_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24_RGB565_24x24);
LV_IMAGE_DECLARE(_folder_open_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24_RGB565_24x24);
LV_IMAGE_DECLARE(_code_blocks_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24_RGB565_24x24);
LV_IMAGE_DECLARE(_terminal_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24_RGB565_24x24);
LV_IMAGE_DECLARE(_sensor_occupied_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24_RGB565_24x24);

static lv_obj_t *s_home_date = NULL;
static lv_obj_t *s_home_clock = NULL;
static lv_obj_t *s_home_analog = NULL;
static lv_obj_t *s_hour_needle = NULL;
static lv_obj_t *s_min_needle = NULL;
static lv_obj_t *s_sec_needle = NULL;
static lv_obj_t *s_home_tip = NULL;
static lv_obj_t *s_home_calendar = NULL;
static lv_timer_t *s_refresh_timer = NULL;

static void close_home_calendar(void) {
    if (s_home_calendar && lv_obj_is_valid(s_home_calendar)) {
        lv_obj_delete(s_home_calendar);
        s_home_calendar = NULL;
    }
    lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
}

static void apply_tabview_style(lv_obj_t *tabview) {
    lv_obj_set_style_bg_opa(tabview, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tabview, lv_color_hex(0xeaeff3), LV_PART_MAIN);
    lv_obj_set_style_border_width(tabview, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(tabview, 0, LV_PART_MAIN);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0xffffff), LV_PART_MAIN);
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

static void update_home_datetime_ui(void) {
    app_datetime_t dt;
    app_manager_get_datetime(&dt);

    if (s_home_date) {
        char date_buf[20];
        lv_snprintf(date_buf, sizeof(date_buf), "%04d/%02d/%02d", (int)dt.year, (int)dt.month, (int)dt.day);
        lv_label_set_text(s_home_date, date_buf);
    }

    if (s_home_clock) {
        char time_buf[16];
        lv_snprintf(time_buf, sizeof(time_buf), "%d:%02d", (int)dt.hour, (int)dt.minute);
        lv_label_set_text(s_home_clock, time_buf);
    }

    if (s_home_analog && s_hour_needle && s_min_needle && s_sec_needle) {
        lv_scale_set_line_needle_value(s_home_analog, s_hour_needle, 30, ((dt.hour % 12) * 5) + (dt.minute / 12));
        lv_scale_set_line_needle_value(s_home_analog, s_min_needle, 40, dt.minute);
        lv_scale_set_line_needle_value(s_home_analog, s_sec_needle, 60, dt.second);
    }
}

static void home_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    update_home_datetime_ui();
}

static void show_home_tip(const char *msg) {
    if (!s_home_tip) {
        return;
    }
    lv_label_set_text(s_home_tip, msg);
}

static void app_settings_cb(lv_event_t *event) {
    LV_UNUSED(event);
    close_home_calendar();
    APP_LOGI("Home: Settings button clicked, navigate to SETTINGS");
    app_manager_navigate_to(SCREEN_ID_SETTINGS);
}

static void app_camera_cb(lv_event_t *event) {
    LV_UNUSED(event);
    close_home_calendar();
    APP_LOGI("Home: Camera button clicked, navigate to CAMERA");
    app_manager_navigate_to(SCREEN_ID_CAMERA);
}

static void app_file_manager_cb(lv_event_t *event) {
    LV_UNUSED(event);
    close_home_calendar();
    APP_LOGI("Home: FileManager button clicked, navigate to FILE_MANAGER");
    app_manager_navigate_to(SCREEN_ID_FILE_MANAGER);
}

static void app_not_migrated_cb(lv_event_t *event) {
    LV_UNUSED(event);
    show_home_tip("This app page is not migrated yet.");
}

static void calendar_pick_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_VALUE_CHANGED || !s_home_calendar || !s_home_date) {
        return;
    }

    lv_calendar_date_t date;
    if (lv_calendar_get_pressed_date(s_home_calendar, &date) != LV_RESULT_OK) {
        return;
    }

    app_datetime_t dt;
    app_manager_get_datetime(&dt);
    dt.year = (int16_t)date.year;
    dt.month = (int8_t)date.month;
    dt.day = (int8_t)date.day;
    app_manager_set_datetime(&dt);

    close_home_calendar();
    update_home_datetime_ui();
}

static void open_date_calendar_cb(lv_event_t *event) {
    LV_UNUSED(event);
    if (s_home_calendar) {
        return;
    }

    app_datetime_t dt;
    app_manager_get_datetime(&dt);

    lv_obj_add_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    s_home_calendar = lv_calendar_create(lv_layer_top());
    lv_obj_set_size(s_home_calendar, 256, 192);
    lv_obj_center(s_home_calendar);
    lv_calendar_set_today_date(s_home_calendar, dt.year, dt.month, dt.day);
    lv_calendar_set_showed_date(s_home_calendar, dt.year, dt.month);
    static lv_calendar_date_t highlighted[1];
    highlighted[0].year = dt.year;
    highlighted[0].month = dt.month;
    highlighted[0].day = dt.day;
    lv_calendar_set_highlighted_dates(s_home_calendar, highlighted, 1);
    lv_calendar_header_arrow_create(s_home_calendar);
    lv_obj_add_event_cb(s_home_calendar, calendar_pick_cb, LV_EVENT_VALUE_CHANGED, NULL);
}

static void build_start_tab(lv_obj_t *tab_start) {
    static const char *hour_ticks[] = {"12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", NULL};

    s_home_analog = lv_scale_create(tab_start);
    lv_obj_set_pos(s_home_analog, 0, 0);
    lv_obj_set_size(s_home_analog, 120, 120);
    lv_scale_set_mode(s_home_analog, LV_SCALE_MODE_ROUND_INNER);
    lv_scale_set_angle_range(s_home_analog, 360U);
    lv_scale_set_range(s_home_analog, 0U, 60U);
    lv_scale_set_rotation(s_home_analog, 270U);
    lv_scale_set_total_tick_count(s_home_analog, 61);
    lv_scale_set_major_tick_every(s_home_analog, 5);
    lv_scale_set_text_src(s_home_analog, hour_ticks);

    lv_obj_set_style_radius(s_home_analog, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_home_analog, 129, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_home_analog, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_home_analog, 0, LV_PART_MAIN);

    lv_obj_set_style_text_color(s_home_analog, lv_color_hex(0x000000), LV_PART_INDICATOR);
    lv_obj_set_style_text_font(s_home_analog, app_font_ui(), LV_PART_INDICATOR);
    lv_obj_set_style_line_width(s_home_analog, 2, LV_PART_INDICATOR);
    lv_obj_set_style_line_color(s_home_analog, lv_color_hex(0x000000), LV_PART_INDICATOR);
    lv_obj_set_style_length(s_home_analog, 8, LV_PART_INDICATOR);

    lv_obj_set_style_line_width(s_home_analog, 1, LV_PART_ITEMS);
    lv_obj_set_style_line_color(s_home_analog, lv_color_hex(0x000000), LV_PART_ITEMS);
    lv_obj_set_style_length(s_home_analog, 3, LV_PART_ITEMS);

    s_hour_needle = lv_line_create(s_home_analog);
    lv_obj_set_style_line_width(s_hour_needle, 4, LV_PART_MAIN);
    lv_obj_set_style_line_color(s_hour_needle, lv_color_hex(0x098D6B), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(s_hour_needle, true, LV_PART_MAIN);

    s_min_needle = lv_line_create(s_home_analog);
    lv_obj_set_style_line_width(s_min_needle, 3, LV_PART_MAIN);
    lv_obj_set_style_line_color(s_min_needle, lv_color_hex(0x2F92DA), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(s_min_needle, true, LV_PART_MAIN);

    s_sec_needle = lv_line_create(s_home_analog);
    lv_obj_set_style_line_width(s_sec_needle, 2, LV_PART_MAIN);
    lv_obj_set_style_line_color(s_sec_needle, lv_color_hex(0xBEAF14), LV_PART_MAIN);
    lv_obj_set_style_line_rounded(s_sec_needle, true, LV_PART_MAIN);

    s_home_date = lv_label_create(tab_start);
    lv_obj_set_pos(s_home_date, 0, 125);
    lv_obj_set_size(s_home_date, 120, 26);
    lv_obj_set_style_text_align(s_home_date, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_home_date, app_font_ui(), LV_PART_MAIN);
    lv_obj_add_flag(s_home_date, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_home_date, open_date_calendar_cb, LV_EVENT_CLICKED, NULL);
}

static void build_apps_tab(lv_obj_t *tab_apps) {
    lv_obj_t *list = lv_list_create(tab_apps);
    lv_obj_set_pos(list, 0, 0);
    lv_obj_set_size(list, 290, 150);
    lv_obj_set_style_pad_all(list, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(list, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(list, lv_color_hex(0xe1e6ee), LV_PART_MAIN);
    lv_obj_set_style_radius(list, 3, LV_PART_MAIN);

    lv_obj_t *item0 = lv_list_add_button(list, &_settings_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24_RGB565_24x24, "Settings");
    lv_obj_t *item1 = lv_list_add_button(list, &_photo_camera_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24_RGB565_24x24, "Camera");
    lv_obj_t *item2 = lv_list_add_button(list, &_folder_open_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24_RGB565_24x24, "File Manager");
    lv_obj_t *item3 = lv_list_add_button(list, &_code_blocks_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24_RGB565_24x24, "Basic Interpreter");
    lv_obj_t *item4 = lv_list_add_button(list, &_terminal_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24_RGB565_24x24, "Terminal");
    lv_obj_t *item5 = lv_list_add_button(list, &_sensor_occupied_24dp_1F1F1F_FILL0_wght400_GRAD0_opsz24_RGB565_24x24, "Face Detection (Haar)");

    lv_obj_add_event_cb(item0, app_settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(item1, app_camera_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(item2, app_file_manager_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(item3, app_not_migrated_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(item4, app_not_migrated_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(item5, app_not_migrated_cb, LV_EVENT_CLICKED, NULL);

    s_home_tip = lv_label_create(tab_apps);
    lv_obj_set_pos(s_home_tip, 0, 158);
    lv_obj_set_size(s_home_tip, 290, 26);
    lv_obj_set_style_text_align(s_home_tip, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_home_tip, app_font_ui(), LV_PART_MAIN);
    lv_label_set_text(s_home_tip, "");
}

static void screen_delete_cb(lv_event_t *event) {
    LV_UNUSED(event);
    APP_LOGI("Home: screen delete start");
    if (s_refresh_timer) {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    close_home_calendar();

    s_home_date = NULL;
    s_home_clock = NULL;
    s_home_analog = NULL;
    s_hour_needle = NULL;
    s_min_needle = NULL;
    s_sec_needle = NULL;
    s_home_tip = NULL;
    APP_LOGI("Home: screen delete done");
}

lv_obj_t *screen_home_create(void) {
    APP_LOGI("Home: create start");
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 320, 240);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(scr, app_font_ui(), LV_PART_MAIN);
    lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *tabview = lv_tabview_create(scr);
    lv_obj_set_pos(tabview, 0, 0);
    lv_obj_set_size(tabview, 320, 210);
    lv_obj_clear_flag(tabview, LV_OBJ_FLAG_SCROLLABLE);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 30);
    apply_tabview_style(tabview);

    lv_obj_t *tab_start = lv_tabview_add_tab(tabview, "Start");
    lv_obj_t *tab_apps = lv_tabview_add_tab(tabview, "Application");

    build_start_tab(tab_start);
    build_apps_tab(tab_apps);

    s_home_clock = lv_label_create(scr);
    lv_obj_set_pos(s_home_clock, 270, 210);
    lv_obj_set_size(s_home_clock, 50, 30);
    lv_obj_set_style_text_align(s_home_clock, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_home_clock, app_font_ui(), LV_PART_MAIN);
    lv_obj_move_foreground(s_home_clock);

    s_refresh_timer = lv_timer_create(home_timer_cb, 1000, NULL);
    update_home_datetime_ui();
    APP_LOGI("Home: create done");
    return scr;
}
