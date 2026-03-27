#include <lvgl.h>

#include "app_fonts.h"
#include "app_log.h"
#include "app_manager.h"
#include "screen_terminal.h"

static lv_obj_t *s_root = NULL;
static lv_obj_t *s_clock_label = NULL;
static lv_timer_t *s_clock_timer = NULL;

static void style_primary_button(lv_obj_t *btn) {
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2195f6), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn, app_font_ui(), LV_PART_MAIN | LV_STATE_DEFAULT);
}

static void style_roller(lv_obj_t *roller) {
    lv_obj_set_style_radius(roller, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(roller, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(roller, lv_color_hex(0x333333), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(roller, app_font_ui(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(roller, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(roller, lv_color_hex(0xe6e6e6), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(roller, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(roller, lv_color_hex(0x2195f6), LV_PART_SELECTED | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(roller, lv_color_hex(0xffffff), LV_PART_SELECTED | LV_STATE_DEFAULT);
}

static void style_textarea(lv_obj_t *ta) {
    lv_obj_set_style_text_color(ta, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(ta, app_font_ui(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(ta, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(ta, lv_color_hex(0xe6e6e6), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(ta, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(ta, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(ta, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_left(ta, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_right(ta, 4, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x2195f6), LV_PART_SCROLLBAR | LV_STATE_DEFAULT);
}

static void style_checkbox(lv_obj_t *cb) {
    lv_obj_set_style_text_color(cb, lv_color_hex(0x0D3055), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cb, app_font_ui(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_letter_space(cb, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cb, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(cb, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    lv_obj_set_style_pad_all(cb, 3, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(cb, 2, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(cb, lv_color_hex(0x2195f6), LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(cb, 6, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(cb, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(cb, lv_color_hex(0xffffff), LV_PART_INDICATOR | LV_STATE_DEFAULT);
}

static void back_home_cb(lv_event_t *event) {
    LV_UNUSED(event);
    APP_LOGI("Terminal: Home button clicked");
    app_manager_navigate_to(SCREEN_ID_HOME);
}

static void refresh_footer_clock(void) {
    app_datetime_t dt;
    app_manager_get_datetime(&dt);
    if (!s_clock_label) return;

    char buf[16];
    lv_snprintf(buf, sizeof(buf), "%d:%02d", (int)dt.hour, (int)dt.minute);
    lv_label_set_text(s_clock_label, buf);
}

static void clock_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    refresh_footer_clock();
}

static void screen_delete_cb(lv_event_t *event) {
    LV_UNUSED(event);
    if (s_clock_timer) {
        lv_timer_del(s_clock_timer);
        s_clock_timer = NULL;
    }
    s_clock_label = NULL;
    s_root = NULL;
}

lv_obj_t *screen_terminal_create(void) {
    if (s_root && lv_obj_is_valid(s_root)) {
        return s_root;
    }

    s_root = lv_obj_create(NULL);
    lv_obj_set_size(s_root, 320, 240);
    lv_obj_set_scrollbar_mode(s_root, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(s_root, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_root, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_add_event_cb(s_root, screen_delete_cb, LV_EVENT_DELETE, NULL);

    /* footer home button and clock */
    lv_obj_t *btn_home = lv_btn_create(s_root);
    lv_obj_set_pos(btn_home, 0, 210);
    lv_obj_set_size(btn_home, 80, 30);
    lv_obj_set_style_bg_opa(btn_home, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(btn_home, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(btn_home, lv_color_hex(0x2F92DA), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_side(btn_home, LV_BORDER_SIDE_NONE, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(btn_home, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(btn_home, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(btn_home, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(btn_home, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(btn_home, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(btn_home, app_font_ui(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_event_cb(btn_home, back_home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_home = lv_label_create(btn_home);
    lv_label_set_text(lbl_home, "" LV_SYMBOL_HOME " Home");
    lv_obj_center(lbl_home);

    lv_obj_t *lbl_clock = lv_label_create(s_root);
    lv_obj_set_pos(lbl_clock, 270, 210);
    lv_obj_set_size(lbl_clock, 50, 30);
    lv_label_set_text(lbl_clock, "11:25");
    lv_obj_set_style_text_align(lbl_clock, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl_clock, app_font_ui(), LV_PART_MAIN);
    lv_obj_set_style_pad_top(lbl_clock, 7, LV_PART_MAIN);
    s_clock_label = lbl_clock;
    refresh_footer_clock();
    s_clock_timer = lv_timer_create(clock_timer_cb, 1000, NULL);

    /* Tabview */
    lv_obj_t *tv = lv_tabview_create(s_root);
    lv_obj_set_pos(tv, 0, 0);
    lv_obj_set_size(tv, 320, 210);
    lv_obj_clear_flag(tv, LV_OBJ_FLAG_SCROLLABLE);
    lv_tabview_set_tab_bar_position(tv, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(tv, 30);
    lv_obj_set_style_bg_opa(tv, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(tv, lv_color_hex(0x4d4d4d), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(tv, app_font_ui(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(tv, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(tv, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(tv, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_t *tab_start = lv_tabview_add_tab(tv, "Start");
    lv_obj_t *tab_text = lv_tabview_add_tab(tv, "Text");
    lv_obj_t *tab_hex = lv_tabview_add_tab(tv, "Hex");
    lv_obj_t *tab_other = lv_tabview_add_tab(tv, "Other");

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tv);

    /* Apply Settings-like tabview style */
    lv_obj_set_style_bg_opa(tv, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(tv, lv_color_hex(0xeaeff3), LV_PART_MAIN);
    lv_obj_set_style_border_width(tv, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(tv, 0, LV_PART_MAIN);

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

    lv_obj_move_foreground(lbl_clock);
    lv_obj_move_foreground(btn_home);

    /* Start tab: rollers and controls */
    lv_obj_t *lbl_speed = lv_label_create(tab_start);
    lv_label_set_text(lbl_speed, "" LV_SYMBOL_CHARGE " Serial Speed");
    lv_obj_set_pos(lbl_speed, 0, 0);

    lv_obj_t *roller_speed = lv_roller_create(tab_start);
    lv_obj_set_pos(roller_speed, 0, 20);
    lv_obj_set_width(roller_speed, 100);
    lv_roller_set_options(roller_speed, "1200\n2400\n4800\n9600\n14400\n19200\n38400\n115200", LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_speed, 2);
    style_roller(roller_speed);

    lv_obj_t *lbl_stop = lv_label_create(tab_start);
    lv_label_set_text(lbl_stop, "" LV_SYMBOL_PAUSE " Stop Bit");
    lv_obj_set_pos(lbl_stop, 115, 0);
    lv_obj_t *roller_stop = lv_roller_create(tab_start);
    lv_obj_set_pos(roller_stop, 115, 20);
    lv_obj_set_width(roller_stop, 70);
    lv_roller_set_options(roller_stop, "1 Bit\n1.5 Bit\n2 Bit", LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_stop, 2);
    style_roller(roller_stop);

    lv_obj_t *lbl_data = lv_label_create(tab_start);
    lv_label_set_text(lbl_data, "" LV_SYMBOL_FILE " Data Bit");
    lv_obj_set_pos(lbl_data, 200, 0);
    lv_obj_t *roller_data = lv_roller_create(tab_start);
    lv_obj_set_pos(roller_data, 200, 20);
    lv_obj_set_width(roller_data, 70);
    lv_roller_set_options(roller_data, "8 Bit\n7 Bit\n6 Bit\n5 Bit", LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_data, 2);
    style_roller(roller_data);

    lv_obj_t *lbl_check = lv_label_create(tab_start);
    lv_label_set_text(lbl_check, "" LV_SYMBOL_REFRESH " Check Bit");
    lv_obj_set_pos(lbl_check, 0, 90);
    lv_obj_t *roller_check = lv_roller_create(tab_start);
    lv_obj_set_pos(roller_check, 0, 110);
    lv_obj_set_width(roller_check, 70);
    lv_roller_set_options(roller_check, "None\nOdd\nEven", LV_ROLLER_MODE_INFINITE);
    lv_roller_set_visible_row_count(roller_check, 2);
    style_roller(roller_check);

    lv_obj_t *btn_service = lv_btn_create(tab_start);
    lv_obj_set_pos(btn_service, 150, 120);
    lv_obj_set_size(btn_service, 120, 30);
    style_primary_button(btn_service);
    lv_obj_t *lbl_service = lv_label_create(btn_service);
    lv_label_set_text(lbl_service, "Start Service");
    lv_obj_center(lbl_service);

    /* Text tab */
    lv_obj_t *ta_send = lv_textarea_create(tab_text);
    lv_obj_set_pos(ta_send, -10, -10);
    lv_obj_set_size(ta_send, 230, 34);
    lv_textarea_set_text(ta_send, "Hello World");
    lv_textarea_set_one_line(ta_send, false);
    lv_textarea_set_max_length(ta_send, 128);
    style_textarea(ta_send);

    lv_obj_t *btn_send = lv_btn_create(tab_text);
    lv_obj_set_pos(btn_send, 230, -10);
    lv_obj_set_size(btn_send, 50, 34);
    style_primary_button(btn_send);
    lv_obj_t *lbl_send = lv_label_create(btn_send);
    lv_label_set_text(lbl_send, "Send");
    lv_obj_center(lbl_send);

    lv_obj_t *lbl_ring = lv_label_create(tab_text);
    lv_label_set_text(lbl_ring, "Ring Buffer View");
    lv_obj_set_pos(lbl_ring, -10, 35);

    /* Hex tab */
    lv_obj_t *ta_hex = lv_textarea_create(tab_hex);
    lv_obj_set_pos(ta_hex, -10, -10);
    lv_obj_set_size(ta_hex, 230, 34);
    lv_textarea_set_text(ta_hex, "ABCD");
    lv_textarea_set_accepted_chars(ta_hex, "0123456789abcdeABCDE");
    lv_textarea_set_max_length(ta_hex, 128);
    style_textarea(ta_hex);

    lv_obj_t *btn_send_hex = lv_btn_create(tab_hex);
    lv_obj_set_pos(btn_send_hex, 230, -10);
    lv_obj_set_size(btn_send_hex, 50, 34);
    style_primary_button(btn_send_hex);
    lv_obj_t *lbl_send_hex = lv_label_create(btn_send_hex);
    lv_label_set_text(lbl_send_hex, "Send");
    lv_obj_center(lbl_send_hex);

    lv_obj_t *lbl_hex_ring = lv_label_create(tab_hex);
    lv_label_set_text(lbl_hex_ring, "Ring Buffer View");
    lv_obj_set_pos(lbl_hex_ring, -10, 35);

    /* Other tab: simple menu-like layout */
    lv_obj_t *menu = lv_menu_create(tab_other);
    lv_obj_set_pos(menu, -10, -10);
    lv_obj_set_size(menu, 300, 160);
    lv_obj_set_scrollbar_mode(menu, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_bg_opa(menu, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(menu, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(menu, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_shadow_width(menu, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

    /* sidebar page */
    lv_obj_t *sidebar = lv_menu_page_create(menu, "Serial");
    lv_menu_set_sidebar_page(menu, sidebar);
    lv_obj_set_scrollbar_mode(sidebar, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_margin_hor(sidebar, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_margin_ver(sidebar, 5, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(sidebar, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(sidebar, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(sidebar, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0xf6f6f6), LV_PART_MAIN | LV_STATE_DEFAULT);

    /* TX subpage */
    lv_obj_t *subpage1 = lv_menu_page_create(menu, NULL);
    lv_obj_t *subpage1_cont = lv_menu_cont_create(subpage1);
    lv_obj_set_layout(subpage1_cont, LV_LAYOUT_NONE);
    lv_obj_t *cont1 = lv_menu_cont_create(sidebar);
    lv_obj_t *cont1_lbl = lv_label_create(cont1);
    lv_label_set_text(cont1_lbl, "TX");
    lv_obj_set_size(cont1_lbl, LV_PCT(100), LV_SIZE_CONTENT);
    lv_label_set_long_mode(cont1_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(cont1, lv_color_hex(0x151212), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cont1, app_font_ui(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(cont1, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(cont1, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(cont1, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(cont1, lv_color_hex(0x9ab700), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(cont1, LV_OPA_60, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(cont1, lv_color_hex(0x19a5ff), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(cont1, 5, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_scrollbar_mode(subpage1, LV_SCROLLBAR_MODE_OFF);
    lv_menu_set_load_page_event(menu, cont1, subpage1);

    /* RX subpage */
    lv_obj_t *subpage2 = lv_menu_page_create(menu, NULL);
    lv_obj_t *subpage2_cont = lv_menu_cont_create(subpage2);
    lv_obj_set_layout(subpage2_cont, LV_LAYOUT_NONE);
    lv_obj_t *cont2 = lv_menu_cont_create(sidebar);
    lv_obj_t *cont2_lbl = lv_label_create(cont2);
    lv_label_set_text(cont2_lbl, "RX");
    lv_obj_set_size(cont2_lbl, LV_PCT(100), LV_SIZE_CONTENT);
    lv_label_set_long_mode(cont2_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_color(cont2, lv_color_hex(0x151212), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(cont2, app_font_ui(), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(cont2, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_top(cont2, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_bottom(cont2, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(cont2, lv_color_hex(0x9ab700), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(cont2, LV_OPA_60, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(cont2, lv_color_hex(0x19a5ff), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(cont2, 5, LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_scrollbar_mode(subpage2, LV_SCROLLBAR_MODE_OFF);
    lv_menu_set_load_page_event(menu, cont2, subpage2);

    /* TX controls */
    lv_obj_t *cb_tx_head = lv_checkbox_create(subpage1_cont);
    lv_obj_set_pos(cb_tx_head, 10, 10);
    lv_checkbox_set_text(cb_tx_head, "TX Head");
    style_checkbox(cb_tx_head);

    lv_obj_t *ta_tx_head = lv_textarea_create(subpage1_cont);
    lv_obj_set_pos(ta_tx_head, 20, 35);
    lv_obj_set_size(ta_tx_head, 120, 34);
    lv_textarea_set_text(ta_tx_head, "FF");
    lv_textarea_set_one_line(ta_tx_head, true);
    lv_textarea_set_accepted_chars(ta_tx_head, "0123456789abcdeABCDE");
    lv_textarea_set_max_length(ta_tx_head, 32);
    style_textarea(ta_tx_head);

    lv_obj_t *cb_tx_modbus = lv_checkbox_create(subpage1_cont);
    lv_obj_set_pos(cb_tx_modbus, 10, 80);
    lv_checkbox_set_text(cb_tx_modbus, "Modbus CRC-16");
    style_checkbox(cb_tx_modbus);

    lv_obj_t *cb_tx_tail = lv_checkbox_create(subpage1_cont);
    lv_obj_set_pos(cb_tx_tail, 10, 105);
    lv_checkbox_set_text(cb_tx_tail, "TX Tail");
    style_checkbox(cb_tx_tail);

    lv_obj_t *ta_tx_tail = lv_textarea_create(subpage1_cont);
    lv_obj_set_pos(ta_tx_tail, 20, 130);
    lv_obj_set_size(ta_tx_tail, 120, 34);
    lv_textarea_set_text(ta_tx_tail, "FF");
    lv_textarea_set_one_line(ta_tx_tail, true);
    lv_textarea_set_accepted_chars(ta_tx_tail, "0123456789abcdeABCDE");
    lv_textarea_set_max_length(ta_tx_tail, 32);
    style_textarea(ta_tx_tail);

    lv_obj_t *btn_tx_apply = lv_btn_create(subpage1_cont);
    lv_obj_set_pos(btn_tx_apply, 100, 180);
    lv_obj_set_size(btn_tx_apply, 60, 30);
    style_primary_button(btn_tx_apply);
    lv_obj_t *btn_tx_apply_label = lv_label_create(btn_tx_apply);
    lv_label_set_text(btn_tx_apply_label, "Apply");
    lv_obj_center(btn_tx_apply_label);

    /* RX controls */
    lv_obj_t *cb_rx_head = lv_checkbox_create(subpage2_cont);
    lv_obj_set_pos(cb_rx_head, 10, 10);
    lv_checkbox_set_text(cb_rx_head, "RX Head");
    style_checkbox(cb_rx_head);

    lv_obj_t *ta_rx_head = lv_textarea_create(subpage2_cont);
    lv_obj_set_pos(ta_rx_head, 20, 35);
    lv_obj_set_size(ta_rx_head, 120, 34);
    lv_textarea_set_text(ta_rx_head, "FF");
    lv_textarea_set_one_line(ta_rx_head, true);
    lv_textarea_set_accepted_chars(ta_rx_head, "0123456789abcdeABCDE");
    lv_textarea_set_max_length(ta_rx_head, 32);
    style_textarea(ta_rx_head);

    lv_obj_t *cb_rx_modbus = lv_checkbox_create(subpage2_cont);
    lv_obj_set_pos(cb_rx_modbus, 10, 80);
    lv_checkbox_set_text(cb_rx_modbus, "Modbus CRC-16");
    style_checkbox(cb_rx_modbus);

    lv_obj_t *cb_rx_tail = lv_checkbox_create(subpage2_cont);
    lv_obj_set_pos(cb_rx_tail, 10, 105);
    lv_checkbox_set_text(cb_rx_tail, "RX Tail");
    style_checkbox(cb_rx_tail);

    lv_obj_t *ta_rx_tail = lv_textarea_create(subpage2_cont);
    lv_obj_set_pos(ta_rx_tail, 20, 130);
    lv_obj_set_size(ta_rx_tail, 120, 34);
    lv_textarea_set_text(ta_rx_tail, "FF");
    lv_textarea_set_one_line(ta_rx_tail, true);
    lv_textarea_set_accepted_chars(ta_rx_tail, "0123456789abcdeABCDE");
    lv_textarea_set_max_length(ta_rx_tail, 32);
    style_textarea(ta_rx_tail);

    lv_obj_t *btn_rx_apply = lv_btn_create(subpage2_cont);
    lv_obj_set_pos(btn_rx_apply, 100, 180);
    lv_obj_set_size(btn_rx_apply, 60, 30);
    style_primary_button(btn_rx_apply);
    lv_obj_t *btn_rx_apply_label = lv_label_create(btn_rx_apply);
    lv_label_set_text(btn_rx_apply_label, "Apply");
    lv_obj_center(btn_rx_apply_label);

    return s_root;
}
