#include <lvgl.h>

#include "app_fonts.h"
#include "app_log.h"
#include "app_manager.h"
#include "screen_file_manager.h"

static lv_obj_t *s_clock_label = NULL;
static lv_timer_t *s_refresh_timer = NULL;

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

static void apply_operation_btnm_style(lv_obj_t *btnm) {
    lv_obj_set_style_border_width(btnm, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(btnm, lv_color_hex(0xc9c9c9), LV_PART_MAIN);
    lv_obj_set_style_border_side(btnm, LV_BORDER_SIDE_FULL, LV_PART_MAIN);
    lv_obj_set_style_pad_row(btnm, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(btnm, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btnm, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btnm, 4, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_TRANSP, LV_PART_MAIN);

    lv_obj_set_style_border_width(btnm, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(btnm, lv_color_hex(0xc9c9c9), LV_PART_ITEMS);
    lv_obj_set_style_border_side(btnm, LV_BORDER_SIDE_FULL, LV_PART_ITEMS);
    lv_obj_set_style_text_color(btnm, lv_color_hex(0xffffff), LV_PART_ITEMS);
    lv_obj_set_style_text_font(btnm, app_font_ui(), LV_PART_ITEMS);
    lv_obj_set_style_radius(btnm, 4, LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(btnm, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(btnm, lv_color_hex(0x2195f6), LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(btnm, 0, LV_PART_ITEMS);
}

static void refresh_footer_clock(void) {
    if (!s_clock_label) {
        return;
    }

    app_datetime_t dt;
    app_manager_get_datetime(&dt);
    lv_label_set_text_fmt(s_clock_label, "%d:%02d", (int)dt.hour, (int)dt.minute);
}

static void timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    refresh_footer_clock();
}

static void back_home_cb(lv_event_t *event) {
    LV_UNUSED(event);
    APP_LOGI("FileManager: Home button clicked, navigate to HOME");
    app_manager_navigate_to(SCREEN_ID_HOME);
}

static void screen_delete_cb(lv_event_t *event) {
    LV_UNUSED(event);
    if (s_refresh_timer) {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    s_clock_label = NULL;
}

lv_obj_t *screen_file_manager_create(void) {
    APP_LOGI("FileManager: create start");
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

    lv_obj_t *tab_browser = lv_tabview_add_tab(tabview, "Browser");
    lv_obj_t *tab_manage = lv_tabview_add_tab(tabview, "Manage");

    lv_obj_t *list = lv_list_create(tab_browser);
    lv_obj_set_pos(list, -10, -10);
    lv_obj_set_size(list, 300, 160);
    lv_obj_set_style_pad_all(list, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(list, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(list, lv_color_hex(0xe1e6ee), LV_PART_MAIN);
    lv_obj_set_style_radius(list, 3, LV_PART_MAIN);
    lv_list_add_button(list, LV_SYMBOL_SD_CARD, "SD");

    lv_obj_t *menu = lv_menu_create(tab_manage);
    lv_obj_set_pos(menu, -10, -10);
    lv_obj_set_size(menu, 300, 160);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(menu, 0, LV_PART_MAIN);

    lv_obj_t *sidebar = lv_menu_page_create(menu, "Manage");
    lv_menu_set_sidebar_page(menu, sidebar);
    lv_obj_set_style_margin_hor(sidebar, 5, LV_PART_MAIN);
    lv_obj_set_style_margin_ver(sidebar, 5, LV_PART_MAIN);
    lv_obj_set_style_border_width(sidebar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(sidebar, 10, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(sidebar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sidebar, lv_color_hex(0xf6f6f6), LV_PART_MAIN);

    lv_obj_t *op_file_page = lv_menu_page_create(menu, NULL);
    lv_obj_t *op_file_cont = lv_menu_cont_create(op_file_page);
    lv_obj_set_layout(op_file_cont, LV_LAYOUT_NONE);

    lv_obj_t *entry_file = lv_menu_cont_create(sidebar);
    lv_obj_t *entry_file_label = lv_label_create(entry_file);
    lv_label_set_text(entry_file_label, "File");
    lv_menu_set_load_page_event(menu, entry_file, op_file_page);

    lv_obj_t *label_from = lv_label_create(op_file_cont);
    lv_obj_set_pos(label_from, 10, 10);
    lv_label_set_text(label_from, "From:");

    lv_obj_t *ta_from = lv_textarea_create(op_file_cont);
    lv_obj_set_pos(ta_from, 20, 30);
    lv_obj_set_size(ta_from, 160, 30);
    lv_textarea_set_one_line(ta_from, false);
    lv_textarea_set_text(ta_from, "");

    lv_obj_t *label_to = lv_label_create(op_file_cont);
    lv_obj_set_pos(label_to, 10, 70);
    lv_label_set_text(label_to, "To:");

    lv_obj_t *ta_to = lv_textarea_create(op_file_cont);
    lv_obj_set_pos(ta_to, 20, 90);
    lv_obj_set_size(ta_to, 160, 30);
    lv_textarea_set_one_line(ta_to, false);
    lv_textarea_set_text(ta_to, "");

    static const char *file_ops[] = {"Copy", "Cut", "\n", "New (To)", "\n", "Delete (From)", ""};
    lv_obj_t *btnm_file = lv_buttonmatrix_create(op_file_cont);
    lv_obj_set_pos(btnm_file, 20, 130);
    lv_obj_set_size(btnm_file, 160, 100);
    lv_buttonmatrix_set_map(btnm_file, file_ops);
    apply_operation_btnm_style(btnm_file);

    lv_obj_t *op_folder_page = lv_menu_page_create(menu, NULL);
    lv_obj_t *op_folder_cont = lv_menu_cont_create(op_folder_page);
    lv_obj_set_layout(op_folder_cont, LV_LAYOUT_NONE);

    lv_obj_t *entry_folder = lv_menu_cont_create(sidebar);
    lv_obj_t *entry_folder_label = lv_label_create(entry_folder);
    lv_label_set_text(entry_folder_label, "Folder");
    lv_menu_set_load_page_event(menu, entry_folder, op_folder_page);

    lv_obj_t *label_fold_from = lv_label_create(op_folder_cont);
    lv_obj_set_pos(label_fold_from, 10, 10);
    lv_label_set_text(label_fold_from, "From:");

    lv_obj_t *ta_fold_from = lv_textarea_create(op_folder_cont);
    lv_obj_set_pos(ta_fold_from, 20, 30);
    lv_obj_set_size(ta_fold_from, 160, 30);
    lv_textarea_set_one_line(ta_fold_from, false);
    lv_textarea_set_text(ta_fold_from, "");

    lv_obj_t *label_fold_to = lv_label_create(op_folder_cont);
    lv_obj_set_pos(label_fold_to, 10, 70);
    lv_label_set_text(label_fold_to, "To:");

    lv_obj_t *ta_fold_to = lv_textarea_create(op_folder_cont);
    lv_obj_set_pos(ta_fold_to, 20, 90);
    lv_obj_set_size(ta_fold_to, 160, 30);
    lv_textarea_set_one_line(ta_fold_to, false);
    lv_textarea_set_text(ta_fold_to, "");

    static const char *folder_ops[] = {"Copy", "Cut", "\n", "New (To)", "\n", "Delete (From)", ""};
    lv_obj_t *btnm_folder = lv_buttonmatrix_create(op_folder_cont);
    lv_obj_set_pos(btnm_folder, 20, 130);
    lv_obj_set_size(btnm_folder, 160, 100);
    lv_buttonmatrix_set_map(btnm_folder, folder_ops);
    apply_operation_btnm_style(btnm_folder);

    lv_menu_set_page(menu, op_file_page);

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
    lv_obj_set_style_text_font(btn_back, app_font_ui(), LV_PART_MAIN);
    lv_obj_set_style_text_align(btn_back, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(btn_back, 0, LV_PART_MAIN);
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
    lv_obj_move_foreground(btn_back);

    s_refresh_timer = lv_timer_create(timer_cb, 1000, NULL);
    refresh_footer_clock();
    APP_LOGI("FileManager: create done");
    return scr;
}
