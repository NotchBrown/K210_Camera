#include <lvgl.h>

#include "app_fonts.h"

static lv_obj_t *s_overlay = NULL;
static lv_obj_t *s_panel = NULL;
static lv_obj_t *s_message_label = NULL;
static lv_obj_t *s_spinner = NULL;

void screen_waiting_close(void);
void screen_waiting_set_message(const char *message);

static void screen_delete_cb(lv_event_t *event) {
    LV_UNUSED(event);
    s_overlay = NULL;
    s_panel = NULL;
    s_message_label = NULL;
    s_spinner = NULL;
}

static void build_popup(const char *message) {
    screen_waiting_close();

    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_overlay, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_overlay, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_70, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(0x101820), LV_PART_MAIN);
    lv_obj_add_event_cb(s_overlay, screen_delete_cb, LV_EVENT_DELETE, NULL);

    s_panel = lv_obj_create(s_overlay);
    lv_obj_set_size(s_panel, 220, 120);
    lv_obj_center(s_panel);
    lv_obj_clear_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(s_panel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(s_panel, 16, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_panel, lv_color_hex(0x1c2630), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_panel, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(s_panel, 14, LV_PART_MAIN);
    lv_obj_set_style_pad_row(s_panel, 10, LV_PART_MAIN);

    s_spinner = lv_spinner_create(s_panel);
    lv_obj_set_size(s_spinner, 56, 56);
    lv_obj_set_style_arc_width(s_spinner, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_spinner, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_spinner, lv_color_hex(0x32414f), LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_spinner, lv_color_hex(0x7cc7ff), LV_PART_INDICATOR);
    lv_obj_center(s_spinner);
    lv_spinner_set_anim_params(s_spinner, 600, 180);

    s_message_label = lv_label_create(s_panel);
    lv_obj_set_width(s_message_label, 190);
    lv_obj_set_style_text_font(s_message_label, app_font_ui(), LV_PART_MAIN);
    lv_obj_set_style_text_color(s_message_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_text_align(s_message_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_long_mode(s_message_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(s_message_label, message ? message : "Please wait...");
    lv_obj_align_to(s_message_label, s_spinner, LV_ALIGN_OUT_BOTTOM_MID, 0, 14);
}

void screen_waiting_show(const char *message) {
    build_popup(message);
}

void screen_waiting_close(void) {
    if (s_overlay && lv_obj_is_valid(s_overlay)) {
        lv_obj_delete(s_overlay);
    }
    s_overlay = NULL;
    s_panel = NULL;
    s_message_label = NULL;
    s_spinner = NULL;
}

void screen_waiting_set_message(const char *message) {
    if (!s_message_label) {
        return;
    }

    lv_label_set_text(s_message_label, message ? message : "Please wait...");
}