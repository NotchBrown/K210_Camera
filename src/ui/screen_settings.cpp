#include <lvgl.h>
#include <stdlib.h>
#include <string.h>

#include "app_fonts.h"
#include "app_manager.h"
#include "screen_settings.h"

static lv_obj_t *s_clock_label = NULL;
static lv_obj_t *s_calendar = NULL;
static lv_calendar_date_t s_selected_date[1];
static lv_obj_t *s_roller_hour = NULL;
static lv_obj_t *s_roller_min = NULL;
static lv_obj_t *s_roller_sec = NULL;

static lv_obj_t *s_ta_name = NULL;
static lv_obj_t *s_ta_phone = NULL;
static lv_obj_t *s_kb = NULL;

static lv_timer_t *s_refresh_timer = NULL;
static app_profile_t s_profile;

static void refresh_footer_clock(void) {
    app_datetime_t dt;
    app_manager_get_datetime(&dt);
    if (!s_clock_label) {
        return;
    }

    char buf[16];
    lv_snprintf(buf, sizeof(buf), "%d:%02d", (int)dt.hour, (int)dt.minute);
    lv_label_set_text(s_clock_label, buf);
}

static void clock_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    refresh_footer_clock();
}

static void back_home_cb(lv_event_t *event) {
    LV_UNUSED(event);
    app_manager_navigate_to(SCREEN_ID_HOME);
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

static int roller_read_int(lv_obj_t *roller) {
    char buf[8];
    lv_roller_get_selected_str(roller, buf, sizeof(buf));
    return atoi(buf);
}

static void calendar_changed_cb(lv_event_t *event) {
    LV_UNUSED(event);
    if (!s_calendar) {
        return;
    }

    lv_calendar_date_t date;
    if (lv_calendar_get_pressed_date(s_calendar, &date) != LV_RESULT_OK) {
        return;
    }

    s_selected_date[0] = date;
    lv_calendar_set_highlighted_dates(s_calendar, s_selected_date, 1);
}

static void apply_date_cb(lv_event_t *event) {
    LV_UNUSED(event);
    app_datetime_t dt;
    app_manager_get_datetime(&dt);
    dt.year = (int16_t)s_selected_date[0].year;
    dt.month = (int8_t)s_selected_date[0].month;
    dt.day = (int8_t)s_selected_date[0].day;
    app_manager_set_datetime(&dt);
    refresh_footer_clock();
}

static void apply_time_cb(lv_event_t *event) {
    LV_UNUSED(event);
    app_datetime_t dt;
    app_manager_get_datetime(&dt);
    dt.hour = (int8_t)roller_read_int(s_roller_hour);
    dt.minute = (int8_t)roller_read_int(s_roller_min);
    dt.second = (int8_t)roller_read_int(s_roller_sec);
    app_manager_set_datetime(&dt);
    refresh_footer_clock();
}

static void keyboard_event_cb(lv_event_t *event) {
#if LV_USE_KEYBOARD
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_target(event);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
#else
    LV_UNUSED(event);
#endif
}

static void textarea_event_cb(lv_event_t *event) {
#if LV_USE_KEYBOARD
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(event);
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(event);

    if (!kb) {
        return;
    }

    if (code == LV_EVENT_FOCUSED) {
        if (lv_indev_get_type(lv_indev_get_act()) != LV_INDEV_TYPE_KEYPAD) {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_READY) {
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(ta, LV_STATE_FOCUSED);
        lv_indev_reset(NULL, ta);
    } else if (code == LV_EVENT_DEFOCUSED) {
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
    }
#else
    LV_UNUSED(event);
#endif
}

static void save_profile_cb(lv_event_t *event) {
    LV_UNUSED(event);
    lv_snprintf(s_profile.name, sizeof(s_profile.name), "%s", lv_textarea_get_text(s_ta_name));
    lv_snprintf(s_profile.phone, sizeof(s_profile.phone), "%s", lv_textarea_get_text(s_ta_phone));
    app_manager_set_profile(&s_profile);
}

static void build_general_tab(lv_obj_t *tab) {
    lv_obj_t *menu = lv_menu_create(tab);
    lv_obj_set_pos(menu, -10, -10);
    lv_obj_set_size(menu, 300, 160);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(menu, 0, LV_PART_MAIN);

    lv_obj_t *sidebar = lv_menu_page_create(menu, "Settings");
    lv_menu_set_sidebar_page(menu, sidebar);
    lv_obj_set_style_margin_hor(sidebar, 5, LV_PART_MAIN);
    lv_obj_set_style_margin_ver(sidebar, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(sidebar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sidebar, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sidebar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0xf6f6f6), LV_PART_MAIN);

    lv_obj_t *date_page = lv_menu_page_create(menu, NULL);
    lv_obj_t *date_cont = lv_menu_cont_create(date_page);
    lv_obj_set_layout(date_cont, LV_LAYOUT_NONE);

    lv_obj_t *entry_data = lv_menu_cont_create(sidebar);
    lv_obj_t *entry_data_label = lv_label_create(entry_data);
    lv_label_set_text(entry_data_label, "Data");
    lv_menu_set_load_page_event(menu, entry_data, date_page);

    s_calendar = lv_calendar_create(date_cont);
    lv_obj_set_pos(s_calendar, 6, 0);
    lv_obj_set_size(s_calendar, 186, 160);

    app_datetime_t dt;
    app_manager_get_datetime(&dt);
    lv_calendar_set_today_date(s_calendar, dt.year, dt.month, dt.day);
    lv_calendar_set_showed_date(s_calendar, dt.year, dt.month);
    s_selected_date[0].year = dt.year;
    s_selected_date[0].month = dt.month;
    s_selected_date[0].day = dt.day;
    lv_calendar_set_highlighted_dates(s_calendar, s_selected_date, 1);
    lv_calendar_header_arrow_create(s_calendar);
    lv_obj_add_event_cb(s_calendar, calendar_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *btn_apply_date = lv_button_create(date_cont);
    lv_obj_set_pos(btn_apply_date, 110, 170);
    lv_obj_set_size(btn_apply_date, 80, 30);
    lv_obj_set_style_bg_color(btn_apply_date, lv_color_hex(0x2195f6), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_apply_date, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_apply_date, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_apply_date, apply_date_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_apply_date_label = lv_label_create(btn_apply_date);
    lv_label_set_text(btn_apply_date_label, "Apply");
    lv_obj_center(btn_apply_date_label);

    lv_obj_t *time_page = lv_menu_page_create(menu, NULL);
    lv_obj_t *time_cont = lv_menu_cont_create(time_page);
    lv_obj_set_layout(time_cont, LV_LAYOUT_NONE);

    lv_obj_t *entry_time = lv_menu_cont_create(sidebar);
    lv_obj_t *entry_time_label = lv_label_create(entry_time);
    lv_label_set_text(entry_time_label, "Time");
    lv_menu_set_load_page_event(menu, entry_time, time_page);

    s_roller_hour = lv_roller_create(time_cont);
    lv_obj_set_pos(s_roller_hour, 9, 35);
    lv_obj_set_width(s_roller_hour, 50);
    lv_roller_set_options(s_roller_hour,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23",
        LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_hour, 3);

    s_roller_min = lv_roller_create(time_cont);
    lv_obj_set_pos(s_roller_min, 70, 35);
    lv_obj_set_width(s_roller_min, 50);
    lv_roller_set_options(s_roller_min,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59",
        LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_min, 3);

    s_roller_sec = lv_roller_create(time_cont);
    lv_obj_set_pos(s_roller_sec, 130, 35);
    lv_obj_set_width(s_roller_sec, 50);
    lv_roller_set_options(s_roller_sec,
        "00\n01\n02\n03\n04\n05\n06\n07\n08\n09\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24\n25\n26\n27\n28\n29\n30\n31\n32\n33\n34\n35\n36\n37\n38\n39\n40\n41\n42\n43\n44\n45\n46\n47\n48\n49\n50\n51\n52\n53\n54\n55\n56\n57\n58\n59",
        LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(s_roller_sec, 3);

    app_manager_get_datetime(&dt);
    lv_roller_set_selected(s_roller_hour, dt.hour, LV_ANIM_OFF);
    lv_roller_set_selected(s_roller_min, dt.minute, LV_ANIM_OFF);
    lv_roller_set_selected(s_roller_sec, dt.second, LV_ANIM_OFF);

    lv_obj_t *label_hour = lv_label_create(time_cont);
    lv_obj_set_pos(label_hour, 10, 10);
    lv_obj_set_size(label_hour, 50, 16);
    lv_label_set_text(label_hour, "Hour");

    lv_obj_t *label_min = lv_label_create(time_cont);
    lv_obj_set_pos(label_min, 70, 10);
    lv_obj_set_size(label_min, 50, 16);
    lv_label_set_text(label_min, "Min");

    lv_obj_t *label_sec = lv_label_create(time_cont);
    lv_obj_set_pos(label_sec, 130, 10);
    lv_obj_set_size(label_sec, 50, 16);
    lv_label_set_text(label_sec, "Sec");

    lv_obj_t *btn_apply_time = lv_button_create(time_cont);
    lv_obj_set_pos(btn_apply_time, 110, 170);
    lv_obj_set_size(btn_apply_time, 80, 30);
    lv_obj_set_style_bg_color(btn_apply_time, lv_color_hex(0x2195f6), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_apply_time, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_apply_time, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_apply_time, apply_time_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_apply_time_label = lv_label_create(btn_apply_time);
    lv_label_set_text(btn_apply_time_label, "Apply");
    lv_obj_center(btn_apply_time_label);

    lv_menu_set_page(menu, date_page);
}

static void build_user_tab(lv_obj_t *tab) {
    lv_obj_t *card = lv_obj_create(tab);
    lv_obj_set_pos(card, 10, 10);
    lv_obj_set_size(card, 280, 150);
    lv_obj_set_style_radius(card, 8, LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(0xe1e6ee), LV_PART_MAIN);
    lv_obj_set_style_shadow_width(card, 0, LV_PART_MAIN);

    lv_obj_t *label_name = lv_label_create(card);
    lv_obj_set_pos(label_name, 0, 0);
    lv_label_set_text(label_name, "Name");

    s_ta_name = lv_textarea_create(card);
    lv_obj_set_pos(s_ta_name, 0, 20);
    lv_obj_set_size(s_ta_name, 250, 30);
    lv_textarea_set_one_line(s_ta_name, true);
    lv_textarea_set_text(s_ta_name, s_profile.name);

    lv_obj_t *label_phone = lv_label_create(card);
    lv_obj_set_pos(label_phone, 0, 60);
    lv_label_set_text(label_phone, "Phone");

    s_ta_phone = lv_textarea_create(card);
    lv_obj_set_pos(s_ta_phone, 0, 80);
    lv_obj_set_size(s_ta_phone, 250, 30);
    lv_textarea_set_one_line(s_ta_phone, true);
    lv_textarea_set_accepted_chars(s_ta_phone, "0123456789+");
    lv_textarea_set_text(s_ta_phone, s_profile.phone);

    lv_obj_t *btn_save = lv_button_create(card);
    lv_obj_set_pos(btn_save, 175, 115);
    lv_obj_set_size(btn_save, 75, 28);
    lv_obj_add_event_cb(btn_save, save_profile_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_save_label = lv_label_create(btn_save);
    lv_label_set_text(btn_save_label, "Save");
    lv_obj_center(btn_save_label);

#if LV_USE_KEYBOARD
    lv_obj_add_event_cb(s_ta_name, textarea_event_cb, LV_EVENT_ALL, s_kb);
    lv_obj_add_event_cb(s_ta_phone, textarea_event_cb, LV_EVENT_ALL, s_kb);
#endif
}

static void build_system_tab(lv_obj_t *tab) {
    lv_obj_t *label = lv_label_create(tab);
    lv_obj_center(label);
    lv_label_set_text(label, "System page is not migrated yet.");
}

static void screen_delete_cb(lv_event_t *event) {
    LV_UNUSED(event);
    if (s_refresh_timer) {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }

    if (s_kb) {
        lv_obj_delete(s_kb);
        s_kb = NULL;
    }

    s_clock_label = NULL;
    s_calendar = NULL;
    s_roller_hour = NULL;
    s_roller_min = NULL;
    s_roller_sec = NULL;
    s_ta_name = NULL;
    s_ta_phone = NULL;
    s_kb = NULL;
}

lv_obj_t *screen_settings_create(void) {
    app_manager_get_profile(&s_profile);

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 320, 240);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_font(scr, app_font_ui(), LV_PART_MAIN);
    lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, NULL);

#if LV_USE_KEYBOARD
    s_kb = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_kb, 320, 100);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_kb, keyboard_event_cb, LV_EVENT_ALL, NULL);
#endif

    s_clock_label = lv_label_create(scr);
    lv_obj_set_pos(s_clock_label, 270, 210);
    lv_obj_set_size(s_clock_label, 50, 30);
    lv_obj_set_style_text_align(s_clock_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_clock_label, app_font_ui(), LV_PART_MAIN);

    lv_obj_t *tabview = lv_tabview_create(scr);
    lv_obj_set_pos(tabview, 0, 0);
    lv_obj_set_size(tabview, 320, 210);
    lv_obj_clear_flag(tabview, LV_OBJ_FLAG_SCROLLABLE);
    lv_tabview_set_tab_bar_position(tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tabview, 30);
    apply_tabview_style(tabview);

    lv_obj_t *tab_general = lv_tabview_add_tab(tabview, "General");
    lv_obj_t *tab_user = lv_tabview_add_tab(tabview, "User");
    lv_obj_t *tab_system = lv_tabview_add_tab(tabview, "System");

    build_general_tab(tab_general);
    build_user_tab(tab_user);
    build_system_tab(tab_system);

    lv_obj_t *btn_back = lv_button_create(scr);
    lv_obj_set_pos(btn_back, 0, 210);
    lv_obj_set_size(btn_back, 80, 30);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_back, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(btn_back, lv_color_hex(0x2F92DA), LV_PART_MAIN);
    lv_obj_set_style_border_side(btn_back, LV_BORDER_SIDE_NONE, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn_back, 0, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(btn_back, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_side(btn_back, LV_BORDER_SIDE_NONE, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_side(btn_back, LV_BORDER_SIDE_NONE, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(btn_back, LV_BORDER_SIDE_NONE, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn_back, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(btn_back, back_home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_back_label = lv_label_create(btn_back);
    lv_label_set_text(btn_back_label, LV_SYMBOL_HOME " Home");
    lv_obj_center(btn_back_label);

    s_refresh_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    refresh_footer_clock();

    return scr;
}
