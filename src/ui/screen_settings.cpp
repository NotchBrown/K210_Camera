#include <lvgl.h>
#include <stdlib.h>
#include <string.h>

#include "app_fonts.h"
#include "app_log.h"
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
static lv_obj_t *s_storage_status_label = NULL;

static lv_obj_t *s_dd_cap_res = NULL;
static lv_obj_t *s_dd_pix_fmt = NULL;
static lv_obj_t *s_dd_frame_rate = NULL;
static lv_obj_t *s_dd_agc_ceiling = NULL;
static lv_obj_t *s_dd_brightness = NULL;
static lv_obj_t *s_dd_contrast = NULL;
static lv_obj_t *s_dd_saturation = NULL;
static lv_obj_t *s_cb_agc = NULL;
static lv_obj_t *s_cb_aec = NULL;
static lv_obj_t *s_cb_awb = NULL;
static lv_obj_t *s_cb_color_bar = NULL;
static lv_obj_t *s_cb_h_mirror = NULL;
static lv_obj_t *s_cb_v_flip = NULL;

static lv_timer_t *s_refresh_timer = NULL;
static lv_timer_t *s_storage_timer = NULL;
static app_profile_t s_profile;

static void refresh_storage_text(void);

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

    if (s_storage_status_label) {
        refresh_storage_text();
    }
}

static void back_home_cb(lv_event_t *event) {
    LV_UNUSED(event);
#if LV_USE_KEYBOARD
    if (s_kb) {
        lv_keyboard_set_textarea(s_kb, NULL);
        lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
    }
#endif
    APP_LOGI("Settings: Home button clicked, navigate to HOME");
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

static void textarea_event_cb(lv_event_t *event) {
#if LV_USE_KEYBOARD
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *ta = (lv_obj_t *)lv_event_get_target(event);
    lv_obj_t *kb = (lv_obj_t *)lv_event_get_user_data(event);

    if (!kb) {
        return;
    }

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        lv_indev_t *act_indev = lv_indev_get_act();
        lv_indev_type_t indev_type = act_indev ? lv_indev_get_type(act_indev) : LV_INDEV_TYPE_NONE;

        APP_LOGI("Settings: textarea focused, indev=%p type=%d", (void *)act_indev, (int)indev_type);
        if (indev_type != LV_INDEV_TYPE_KEYPAD) {
            lv_keyboard_set_textarea(kb, ta);
            lv_obj_move_foreground(kb);
            lv_obj_remove_flag(kb, LV_OBJ_FLAG_HIDDEN);
        }
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        APP_LOGI("Settings: textarea ready, hide keyboard");
        lv_keyboard_set_textarea(kb, NULL);
        lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(ta, LV_STATE_FOCUSED);
        lv_indev_reset(NULL, ta);
    } else if (code == LV_EVENT_DEFOCUSED) {
        APP_LOGI("Settings: textarea defocused, detach keyboard");
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
    APP_LOGI("Settings: profile saved");
}

static void refresh_storage_text(void) {
    if (!s_storage_status_label) {
        return;
    }
    app_system_status_t status;
    app_manager_get_system_status(&status);

    char buf[128];
    if (status.storage_checked) {
        if (status.storage_available) {
            lv_snprintf(buf, sizeof(buf), "SD Card: Mounted\nTotal: %d MB\nFree: %d MB\n\n%s",
                       (int)(status.storage_total_kb / 1024), 
                       (int)(status.storage_free_kb / 1024), 
                       status.storage_message);
        } else {
            lv_snprintf(buf, sizeof(buf), "SD Card: Not Mounted\n\n%s", status.storage_message);
        }
    } else {
        lv_snprintf(buf, sizeof(buf), "%s", status.storage_message);
    }
    lv_label_set_text(s_storage_status_label, buf);
}

static void storage_check_cb(lv_event_t *event) {
    LV_UNUSED(event);
    APP_LOGI("Settings: storage check requested");
    app_manager_request_storage_check();
}

static void storage_format_cb(lv_event_t *event) {
    LV_UNUSED(event);
    APP_LOGI("Settings: storage format requested");
    app_manager_request_storage_format();
}

static bool checkbox_is_checked(lv_obj_t *cb) {
    return lv_obj_has_state(cb, LV_STATE_CHECKED);
}

static uint8_t roller_selected_safe_u8(lv_obj_t *roller, uint8_t fallback) {
    if (!roller) {
        return fallback;
    }
    uint16_t idx = lv_roller_get_selected(roller);
    uint16_t cnt = lv_roller_get_option_count(roller);
    if (cnt == 0U || idx >= cnt) {
        return fallback;
    }
    return (uint8_t)idx;
}

static void load_camera_settings_to_ui(void) {
    app_camera_settings_t cfg;
    app_manager_get_camera_settings(&cfg);

    if (s_dd_cap_res) {
        uint16_t cnt = lv_roller_get_option_count(s_dd_cap_res);
        uint16_t idx = cfg.capture_res_index;
        if (cnt == 0U) idx = 0U;
        else if (idx >= cnt) idx = (uint16_t)(cnt - 1U);
        lv_roller_set_selected(s_dd_cap_res, idx, LV_ANIM_OFF);
    }
    if (s_dd_pix_fmt) {
        uint16_t cnt = lv_roller_get_option_count(s_dd_pix_fmt);
        uint16_t idx = cfg.pix_format_index;
        if (cnt == 0U) idx = 0U;
        else if (idx >= cnt) idx = (uint16_t)(cnt - 1U);
        lv_roller_set_selected(s_dd_pix_fmt, idx, LV_ANIM_OFF);
    }
    if (s_dd_frame_rate) {
        uint16_t cnt = lv_roller_get_option_count(s_dd_frame_rate);
        uint16_t idx = cfg.frame_rate_index;
        if (cnt == 0U) idx = 0U;
        else if (idx >= cnt) idx = (uint16_t)(cnt - 1U);
        lv_roller_set_selected(s_dd_frame_rate, idx, LV_ANIM_OFF);
    }
    if (s_dd_agc_ceiling) {
        uint16_t cnt = lv_roller_get_option_count(s_dd_agc_ceiling);
        uint16_t idx = cfg.agc_ceiling_index;
        if (cnt == 0U) idx = 0U;
        else if (idx >= cnt) idx = (uint16_t)(cnt - 1U);
        lv_roller_set_selected(s_dd_agc_ceiling, idx, LV_ANIM_OFF);
    }
    if (s_dd_brightness) {
        lv_roller_set_selected(s_dd_brightness, (uint16_t)(cfg.brightness_level + 2), LV_ANIM_OFF);
    }
    if (s_dd_contrast) {
        lv_roller_set_selected(s_dd_contrast, (uint16_t)(cfg.contrast_level + 2), LV_ANIM_OFF);
    }
    if (s_dd_saturation) {
        lv_roller_set_selected(s_dd_saturation, (uint16_t)(cfg.saturation_level + 2), LV_ANIM_OFF);
    }

    if (s_cb_agc) {
        if (cfg.agc) lv_obj_add_state(s_cb_agc, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_cb_agc, LV_STATE_CHECKED);
    }
    if (s_cb_aec) {
        if (cfg.aec) lv_obj_add_state(s_cb_aec, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_cb_aec, LV_STATE_CHECKED);
    }
    if (s_cb_awb) {
        if (cfg.awb) lv_obj_add_state(s_cb_awb, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_cb_awb, LV_STATE_CHECKED);
    }
    if (s_cb_color_bar) {
        if (cfg.color_bar) lv_obj_add_state(s_cb_color_bar, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_cb_color_bar, LV_STATE_CHECKED);
    }
    if (s_cb_h_mirror) {
        if (cfg.h_mirror) lv_obj_add_state(s_cb_h_mirror, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_cb_h_mirror, LV_STATE_CHECKED);
    }
    if (s_cb_v_flip) {
        if (cfg.v_flip) lv_obj_add_state(s_cb_v_flip, LV_STATE_CHECKED);
        else lv_obj_clear_state(s_cb_v_flip, LV_STATE_CHECKED);
    }
}

static void apply_camera_settings_cb(lv_event_t *event) {
    LV_UNUSED(event);
    app_camera_settings_t cfg;
    app_manager_get_camera_settings(&cfg);

    cfg.capture_res_index = roller_selected_safe_u8(s_dd_cap_res, cfg.capture_res_index);
    cfg.pix_format_index = roller_selected_safe_u8(s_dd_pix_fmt, cfg.pix_format_index);
    cfg.frame_rate_index = roller_selected_safe_u8(s_dd_frame_rate, cfg.frame_rate_index);
    cfg.agc_ceiling_index = roller_selected_safe_u8(s_dd_agc_ceiling, cfg.agc_ceiling_index);
    if (s_dd_brightness) {
        cfg.brightness_level = (int8_t)roller_read_int(s_dd_brightness);
    }
    if (s_dd_contrast) {
        cfg.contrast_level = (int8_t)roller_read_int(s_dd_contrast);
    }
    if (s_dd_saturation) {
        cfg.saturation_level = (int8_t)roller_read_int(s_dd_saturation);
    }
    cfg.agc = checkbox_is_checked(s_cb_agc);
    cfg.aec = checkbox_is_checked(s_cb_aec);
    cfg.awb = checkbox_is_checked(s_cb_awb);
    cfg.color_bar = checkbox_is_checked(s_cb_color_bar);
    cfg.h_mirror = checkbox_is_checked(s_cb_h_mirror);
    cfg.v_flip = checkbox_is_checked(s_cb_v_flip);

    app_manager_set_camera_settings(&cfg);
    APP_LOGI("Settings: camera settings applied res=%u fmt=%u fps=%u agc_ceiling=%u b=%d c=%d s=%d agc=%d aec=%d awb=%d color_bar=%d hm=%d vf=%d",
             (unsigned)cfg.capture_res_index,
             (unsigned)cfg.pix_format_index,
             (unsigned)cfg.frame_rate_index,
             (unsigned)cfg.agc_ceiling_index,
             (int)cfg.brightness_level,
             (int)cfg.contrast_level,
             (int)cfg.saturation_level,
             (int)cfg.agc,
             (int)cfg.aec,
             (int)cfg.awb,
             (int)cfg.color_bar,
             (int)cfg.h_mirror,
             (int)cfg.v_flip);
}

static void camera_widget_debug_cb(lv_event_t *event) {
    lv_obj_t *obj = lv_event_get_target_obj(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_VALUE_CHANGED) {
        return;
    }

    if (obj == s_dd_cap_res) {
        APP_LOGI("Settings: ui res idx=%u", (unsigned)lv_roller_get_selected(s_dd_cap_res));
    } else if (obj == s_dd_pix_fmt) {
        APP_LOGI("Settings: ui pix fmt idx=%u", (unsigned)lv_roller_get_selected(s_dd_pix_fmt));
    } else if (obj == s_dd_frame_rate) {
        APP_LOGI("Settings: ui fps idx=%u", (unsigned)lv_roller_get_selected(s_dd_frame_rate));
    } else if (obj == s_dd_agc_ceiling) {
        APP_LOGI("Settings: ui agc_ceiling idx=%u", (unsigned)lv_roller_get_selected(s_dd_agc_ceiling));
    } else if (obj == s_dd_brightness) {
        APP_LOGI("Settings: ui brightness=%d", roller_read_int(s_dd_brightness));
    } else if (obj == s_dd_contrast) {
        APP_LOGI("Settings: ui contrast=%d", roller_read_int(s_dd_contrast));
    } else if (obj == s_dd_saturation) {
        APP_LOGI("Settings: ui saturation=%d", roller_read_int(s_dd_saturation));
    } else if (obj == s_cb_agc) {
        APP_LOGI("Settings: ui agc=%d", (int)checkbox_is_checked(s_cb_agc));
    } else if (obj == s_cb_aec) {
        APP_LOGI("Settings: ui aec=%d", (int)checkbox_is_checked(s_cb_aec));
    } else if (obj == s_cb_awb) {
        APP_LOGI("Settings: ui awb=%d", (int)checkbox_is_checked(s_cb_awb));
    } else if (obj == s_cb_color_bar) {
        APP_LOGI("Settings: ui color_bar=%d", (int)checkbox_is_checked(s_cb_color_bar));
    }
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

    lv_obj_t *person_page = lv_menu_page_create(menu, NULL);
    lv_obj_t *person_cont = lv_menu_cont_create(person_page);
    lv_obj_set_layout(person_cont, LV_LAYOUT_NONE);

    lv_obj_t *entry_person = lv_menu_cont_create(sidebar);
    lv_obj_t *entry_person_label = lv_label_create(entry_person);
    lv_label_set_text(entry_person_label, "Person");
    lv_menu_set_load_page_event(menu, entry_person, person_page);

    lv_obj_t *label_name = lv_label_create(person_cont);
    lv_obj_set_pos(label_name, 10, 10);
    lv_label_set_text(label_name, "Name");

    s_ta_name = lv_textarea_create(person_cont);
    lv_obj_set_pos(s_ta_name, 10, 28);
    lv_obj_set_size(s_ta_name, 180, 36);
    lv_textarea_set_one_line(s_ta_name, true);
    lv_textarea_set_max_length(s_ta_name, 31);
    lv_obj_set_scrollbar_mode(s_ta_name, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_text(s_ta_name, s_profile.name);

    lv_obj_t *label_phone = lv_label_create(person_cont);
    lv_obj_set_pos(label_phone, 10, 74);
    lv_label_set_text(label_phone, "Phone");

    s_ta_phone = lv_textarea_create(person_cont);
    lv_obj_set_pos(s_ta_phone, 10, 92);
    lv_obj_set_size(s_ta_phone, 180, 36);
    lv_textarea_set_one_line(s_ta_phone, true);
    lv_textarea_set_max_length(s_ta_phone, 19);
    lv_textarea_set_accepted_chars(s_ta_phone, "+-0123456789()");
    lv_obj_set_scrollbar_mode(s_ta_phone, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_text(s_ta_phone, s_profile.phone);

    lv_obj_t *btn_save = lv_button_create(person_cont);
    lv_obj_set_pos(btn_save, 120, 160);
    lv_obj_set_size(btn_save, 75, 28);
    lv_obj_add_event_cb(btn_save, save_profile_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_save_label = lv_label_create(btn_save);
    lv_label_set_text(btn_save_label, "Apply");
    lv_obj_center(btn_save_label);

    lv_menu_set_page(menu, person_page);

#if LV_USE_KEYBOARD
    lv_obj_add_event_cb(s_ta_name, textarea_event_cb, LV_EVENT_ALL, s_kb);
    lv_obj_add_event_cb(s_ta_phone, textarea_event_cb, LV_EVENT_ALL, s_kb);
#endif
}

static void build_system_tab(lv_obj_t *tab) {
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

    lv_obj_t *info_page = lv_menu_page_create(menu, NULL);
    lv_obj_t *info_cont = lv_menu_cont_create(info_page);
    lv_obj_set_layout(info_cont, LV_LAYOUT_NONE);

    lv_obj_t *entry_info = lv_menu_cont_create(sidebar);
    lv_obj_t *entry_info_label = lv_label_create(entry_info);
    lv_label_set_text(entry_info_label, "Info");
    lv_menu_set_load_page_event(menu, entry_info, info_page);

    lv_obj_t *title = lv_label_create(info_cont);
    lv_obj_set_pos(title, 10, 10);
    lv_obj_set_size(title, 180, 15);
    lv_label_set_text(title, "Eady K210 PDA v0.0.1");
    lv_obj_set_style_text_font(title, app_font_ui(), LV_PART_MAIN);

    lv_obj_t *info = lv_label_create(info_cont);
    lv_obj_set_pos(info, 10, 35);
    lv_obj_set_size(info, 180, 200);
    lv_obj_add_flag(info, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(info,
        "A Personal Digital Assistant powered by K210 chip\n\n"
        "Framework: Maixduino\n"
        "OS: FreeRTOS\n"
        "UI: LVGL 9.x\n\n"
        "For details, please refer to README and LICENSE.");
    lv_obj_set_style_text_font(info, app_font_ui(), LV_PART_MAIN);
    lv_obj_set_style_text_line_space(info, 3, LV_PART_MAIN);

    lv_obj_t *storage_page = lv_menu_page_create(menu, NULL);
    lv_obj_t *storage_cont = lv_menu_cont_create(storage_page);
    lv_obj_set_layout(storage_cont, LV_LAYOUT_NONE);

    lv_obj_t *entry_storage = lv_menu_cont_create(sidebar);
    lv_obj_t *entry_storage_label = lv_label_create(entry_storage);
    lv_label_set_text(entry_storage_label, "SD FS");
    lv_menu_set_load_page_event(menu, entry_storage, storage_page);

    s_storage_status_label = lv_label_create(storage_cont);
    lv_obj_set_pos(s_storage_status_label, 10, 10);
    lv_obj_set_size(s_storage_status_label, 185, 100);
    lv_label_set_text(s_storage_status_label, "Storage is not checked yet.");
    lv_obj_set_style_text_font(s_storage_status_label, app_font_ui(), LV_PART_MAIN);

    lv_obj_t *btn_check = lv_button_create(storage_cont);
    lv_obj_set_pos(btn_check, 10, 120);
    lv_obj_set_size(btn_check, 64, 24);
    lv_obj_set_style_bg_color(btn_check, lv_color_hex(0x2195f6), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_check, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_check, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_check, storage_check_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_check_label = lv_label_create(btn_check);
    lv_label_set_text(btn_check_label, "Check");
    lv_obj_center(btn_check_label);

    lv_obj_t *btn_format = lv_button_create(storage_cont);
    lv_obj_set_pos(btn_format, 10, 170);
    lv_obj_set_size(btn_format, 64, 24);
    lv_obj_set_style_bg_color(btn_format, lv_color_hex(0x2195f6), LV_PART_MAIN);
    lv_obj_set_style_radius(btn_format, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_format, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_format, storage_format_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_format_label = lv_label_create(btn_format);
    lv_label_set_text(btn_format_label, "Format");
    lv_obj_center(btn_format_label);

    lv_menu_set_page(menu, storage_page);
    refresh_storage_text();
}

static void build_apps_tab(lv_obj_t *tab) {
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

    lv_obj_t *camera_page = lv_menu_page_create(menu, NULL);
    lv_obj_t *camera_cont = lv_menu_cont_create(camera_page);
    lv_obj_set_layout(camera_cont, LV_LAYOUT_NONE);
    lv_obj_set_size(camera_cont, lv_pct(100), 520);
    lv_obj_set_style_bg_opa(camera_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(camera_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(camera_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(camera_cont, 0, LV_PART_MAIN);

    lv_obj_t *entry_camera = lv_menu_cont_create(sidebar);
    lv_obj_t *entry_camera_label = lv_label_create(entry_camera);
    lv_label_set_text(entry_camera_label, "Camera");
    lv_menu_set_load_page_event(menu, entry_camera, camera_page);

    lv_obj_t *label_cam_res = lv_label_create(camera_cont);
    lv_obj_set_pos(label_cam_res, 10, 10);
    lv_label_set_text(label_cam_res, "Resolution");

    s_dd_cap_res = lv_roller_create(camera_cont);
    lv_obj_set_pos(s_dd_cap_res, 20, 30);
    lv_obj_set_size(s_dd_cap_res, 80, 48);
    lv_roller_set_visible_row_count(s_dd_cap_res, 2);
    lv_roller_set_options(s_dd_cap_res,
        "640*480\n"
        "320*240\n"
        "160*120",
        LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(s_dd_cap_res, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *label_cam_b = lv_label_create(camera_cont);
    lv_obj_set_pos(label_cam_b, 110, 10);
    lv_label_set_text(label_cam_b, "Brightness");

    s_dd_brightness = lv_roller_create(camera_cont);
    lv_obj_set_pos(s_dd_brightness, 120, 30);
    lv_obj_set_size(s_dd_brightness, 50, 48);
    lv_roller_set_visible_row_count(s_dd_brightness, 2);
    lv_roller_set_options(s_dd_brightness, "-2\n-1\n0\n1\n2", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(s_dd_brightness, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *label_cam_fmt = lv_label_create(camera_cont);
    lv_obj_set_pos(label_cam_fmt, 10, 100);
    lv_label_set_text(label_cam_fmt, "Format");

    s_dd_pix_fmt = lv_roller_create(camera_cont);
    lv_obj_set_pos(s_dd_pix_fmt, 20, 120);
    lv_obj_set_size(s_dd_pix_fmt, 80, 48);
    lv_roller_set_visible_row_count(s_dd_pix_fmt, 2);
    lv_roller_set_options(s_dd_pix_fmt, "RGB565\nYUV422", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(s_dd_pix_fmt, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *label_cam_c = lv_label_create(camera_cont);
    lv_obj_set_pos(label_cam_c, 110, 100);
    lv_label_set_text(label_cam_c, "Contrast");

    s_dd_contrast = lv_roller_create(camera_cont);
    lv_obj_set_pos(s_dd_contrast, 120, 120);
    lv_obj_set_size(s_dd_contrast, 50, 48);
    lv_roller_set_visible_row_count(s_dd_contrast, 2);
    lv_roller_set_options(s_dd_contrast, "-2\n-1\n0\n1\n2", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(s_dd_contrast, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *label_cam_fr = lv_label_create(camera_cont);
    lv_obj_set_pos(label_cam_fr, 10, 190);
    lv_label_set_text(label_cam_fr, "Frame Rate");

    s_dd_frame_rate = lv_roller_create(camera_cont);
    lv_obj_set_pos(s_dd_frame_rate, 20, 210);
    lv_obj_set_size(s_dd_frame_rate, 80, 48);
    lv_roller_set_visible_row_count(s_dd_frame_rate, 2);
    lv_roller_set_options(s_dd_frame_rate, "2 FPS\n8 FPS\n15 FPS\n30 FPS\n60 FPS", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(s_dd_frame_rate, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *label_cam_s = lv_label_create(camera_cont);
    lv_obj_set_pos(label_cam_s, 110, 190);
    lv_label_set_text(label_cam_s, "Saturation");

    s_dd_saturation = lv_roller_create(camera_cont);
    lv_obj_set_pos(s_dd_saturation, 120, 210);
    lv_obj_set_size(s_dd_saturation, 50, 48);
    lv_roller_set_visible_row_count(s_dd_saturation, 2);
    lv_roller_set_options(s_dd_saturation, "-2\n-1\n0\n1\n2", LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(s_dd_saturation, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_cb_h_mirror = lv_checkbox_create(camera_cont);
    lv_obj_set_pos(s_cb_h_mirror, 20, 300);
    lv_checkbox_set_text(s_cb_h_mirror, "HMirror");
    lv_obj_add_event_cb(s_cb_h_mirror, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_cb_v_flip = lv_checkbox_create(camera_cont);
    lv_obj_set_pos(s_cb_v_flip, 120, 300);
    lv_checkbox_set_text(s_cb_v_flip, "VFlip");
    lv_obj_add_event_cb(s_cb_v_flip, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_cb_agc = lv_checkbox_create(camera_cont);
    lv_obj_set_pos(s_cb_agc, 20, 330);
    lv_checkbox_set_text(s_cb_agc, "AutoGain");
    lv_obj_add_event_cb(s_cb_agc, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_cb_awb = lv_checkbox_create(camera_cont);
    lv_obj_set_pos(s_cb_awb, 120, 330);
    lv_checkbox_set_text(s_cb_awb, "AWB");
    lv_obj_add_event_cb(s_cb_awb, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_cb_aec = lv_checkbox_create(camera_cont);
    lv_obj_set_pos(s_cb_aec, 20, 360);
    lv_checkbox_set_text(s_cb_aec, "AutoExposure");
    lv_obj_add_event_cb(s_cb_aec, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *label_cam_gc = lv_label_create(camera_cont);
    lv_obj_set_pos(label_cam_gc, 20, 385);
    lv_label_set_text(label_cam_gc, "Gain Ceiling");

    s_dd_agc_ceiling = lv_roller_create(camera_cont);
    lv_obj_set_pos(s_dd_agc_ceiling, 57, 403);
    lv_obj_set_size(s_dd_agc_ceiling, 70, 48);
    lv_roller_set_visible_row_count(s_dd_agc_ceiling, 2);
    lv_roller_set_options(s_dd_agc_ceiling,
        "2\n4\n8\n16\n32\n64\n128",
        LV_ROLLER_MODE_NORMAL);
    lv_obj_add_event_cb(s_dd_agc_ceiling, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_cb_color_bar = lv_checkbox_create(camera_cont);
    lv_obj_set_pos(s_cb_color_bar, 20, 470);
    lv_checkbox_set_text(s_cb_color_bar, "ColorBar");
    lv_obj_add_event_cb(s_cb_color_bar, camera_widget_debug_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *btn_apply_cam = lv_button_create(camera_cont);
    lv_obj_set_pos(btn_apply_cam, 120, 490);
    lv_obj_set_size(btn_apply_cam, 64, 24);
    lv_obj_set_style_bg_color(btn_apply_cam, lv_color_hex(0x2195f6), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn_apply_cam, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn_apply_cam, 5, LV_PART_MAIN);
    lv_obj_add_event_cb(btn_apply_cam, apply_camera_settings_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_apply_cam_label = lv_label_create(btn_apply_cam);
    lv_label_set_text(btn_apply_cam_label, "Apply");
    lv_obj_center(btn_apply_cam_label);

    lv_menu_set_page(menu, camera_page);
    load_camera_settings_to_ui();
}

static void screen_delete_cb(lv_event_t *event) {
    LV_UNUSED(event);
    APP_LOGI("Settings: screen delete start");
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
    s_storage_status_label = NULL;
    s_dd_cap_res = NULL;
    s_dd_pix_fmt = NULL;
    s_dd_frame_rate = NULL;
    s_dd_agc_ceiling = NULL;
    s_dd_brightness = NULL;
    s_dd_contrast = NULL;
    s_dd_saturation = NULL;
    s_cb_agc = NULL;
    s_cb_aec = NULL;
    s_cb_awb = NULL;
    s_cb_color_bar = NULL;
    s_cb_h_mirror = NULL;
    s_cb_v_flip = NULL;
    s_kb = NULL;
    APP_LOGI("Settings: screen delete done");
}

lv_obj_t *screen_settings_create(void) {
    APP_LOGI("Settings: create start");
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
    lv_obj_set_size(s_kb, 320, 110);
    lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
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
    lv_obj_t *tab_apps = lv_tabview_add_tab(tabview, "Apps");

    build_general_tab(tab_general);
    build_user_tab(tab_user);
    build_system_tab(tab_system);
    build_apps_tab(tab_apps);

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

    lv_obj_move_foreground(s_clock_label);
    lv_obj_move_foreground(btn_back);

    s_refresh_timer = lv_timer_create(clock_timer_cb, 1000, NULL);
    refresh_footer_clock();

    APP_LOGI("Settings: create done");
    return scr;
}
