#include <lvgl.h>
#include <string.h>

#include "app_fonts.h"
#include "app_log.h"
#include "app_manager.h"
#include "screen_file_manager.h"

static lv_obj_t *s_clock_label = NULL;
static lv_timer_t *s_refresh_timer = NULL;
static lv_obj_t *s_file_list = NULL;
static lv_obj_t *s_browser_path_label = NULL;
static lv_obj_t *s_file_ta_from = NULL;
static lv_obj_t *s_file_ta_to = NULL;
static lv_obj_t *s_folder_ta_from = NULL;
static lv_obj_t *s_folder_ta_to = NULL;
static lv_obj_t *s_op_result_label = NULL;
static char s_browser_fs_path[128] = "";
static bool s_last_storage_available = false;
static bool s_last_storage_checked = false;

static void refresh_file_list(void);
static void browser_item_cb(lv_event_t *event);

static void set_op_result(const char *msg, bool is_error) {
    if (!s_op_result_label) {
        return;
    }
    lv_label_set_text(s_op_result_label, msg ? msg : "");
    lv_obj_set_style_text_color(s_op_result_label,
                                is_error ? lv_color_hex(0xb00020) : lv_color_hex(0x1a7f37),
                                LV_PART_MAIN);
}

static bool browser_btn_is_dir(lv_obj_t *btn) {
    lv_obj_t *icon = lv_obj_get_child(btn, 0);
    if (!icon) {
        return false;
    }
    const char *icon_txt = lv_label_get_text(icon);
    if (!icon_txt) {
        return false;
    }
    return strcmp(icon_txt, LV_SYMBOL_DIRECTORY) == 0 || strcmp(icon_txt, LV_SYMBOL_SD_CARD) == 0;
}

static void browser_item_full_path(const char *item_text, char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';
    if (!item_text || item_text[0] == '\0') {
        return;
    }

    if (strcmp(item_text, "/SD/") == 0 || strcmp(item_text, "...") == 0) {
        return;
    }

    if (item_text[0] == '/') {
        if (strcmp(s_browser_fs_path, "/") == 0) {
            lv_snprintf(out, out_len, "%s", item_text);
        } else {
            lv_snprintf(out, out_len, "%s%s", s_browser_fs_path, item_text);
        }
    }
}

static void normalize_user_path(const char *input,
                                const char *base,
                                char *out,
                                size_t out_len) {
    if (!out || out_len == 0) {
        return;
    }
    out[0] = '\0';

    if (!input) {
        return;
    }

    while (*input == ' ') {
        input++;
    }

    if (*input == '\0') {
        return;
    }

    if (strncmp(input, "/SD/", 4) == 0) {
        lv_snprintf(out, out_len, "%s", input + 3);
    } else if (strcmp(input, "/SD") == 0) {
        lv_snprintf(out, out_len, "%s", "/");
    } else if (input[0] == '/') {
        lv_snprintf(out, out_len, "%s", input);
    } else {
        const char *base_path = (base && base[0]) ? base : "/";
        if (strcmp(base_path, "/") == 0) {
            lv_snprintf(out, out_len, "/%s", input);
        } else {
            lv_snprintf(out, out_len, "%s/%s", base_path, input);
        }
    }

    size_t n = strlen(out);
    while (n > 1 && out[n - 1] == '/') {
        out[n - 1] = '\0';
        n--;
    }
}

static const char *browser_btn_text(lv_obj_t *btn) {
    lv_obj_t *label = lv_obj_get_child(btn, 1);
    if (!label) {
        return NULL;
    }
    return lv_label_get_text(label);
}

static void browser_set_path(const char *path) {
    lv_snprintf(s_browser_fs_path, sizeof(s_browser_fs_path), "%s", path ? path : "");
}

static void browser_set_path_label(void) {
    if (!s_browser_path_label) {
        return;
    }

    if (s_browser_fs_path[0] == '\0') {
        lv_label_set_text(s_browser_path_label, "Path: /SD");
        return;
    }

    lv_label_set_text_fmt(s_browser_path_label, "Path: /SD%s", s_browser_fs_path);
}

static void browser_to_parent_path(void) {
    if (s_browser_fs_path[0] == '\0') {
        return;
    }

    if (strcmp(s_browser_fs_path, "/") == 0) {
        browser_set_path("");
        return;
    }

    char *last = strrchr(s_browser_fs_path, '/');
    if (!last || last == s_browser_fs_path) {
        browser_set_path("/");
        return;
    }

    *last = '\0';
}

static bool browser_append_dir_item(const char *name) {
    char next_path[128];
    if (!name || name[0] != '/') {
        return false;
    }

    if (strcmp(s_browser_fs_path, "/") == 0) {
        lv_snprintf(next_path, sizeof(next_path), "%s", name);
    } else {
        lv_snprintf(next_path, sizeof(next_path), "%s%s", s_browser_fs_path, name);
    }

    lv_obj_t *btn = lv_list_add_button(s_file_list, LV_SYMBOL_DIRECTORY, name);
    lv_obj_add_event_cb(btn, browser_item_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(btn, browser_item_cb, LV_EVENT_LONG_PRESSED, NULL);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    APP_LOGI("FileManager: add dir %s", next_path);
    return true;
}

static void browser_item_cb(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_LONG_PRESSED) {
        return;
    }

    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(event);
    const char *text = browser_btn_text(btn);
    if (!text) {
        return;
    }

    if (code == LV_EVENT_LONG_PRESSED) {
        char full_path[128];
        browser_item_full_path(text, full_path, sizeof(full_path));
        if (full_path[0] == '\0') {
            return;
        }

        bool is_dir = browser_btn_is_dir(btn);
        if (is_dir) {
            if (s_folder_ta_from) lv_textarea_set_text(s_folder_ta_from, full_path);
            if (s_folder_ta_to) lv_textarea_set_text(s_folder_ta_to, full_path);
        } else {
            if (s_file_ta_from) lv_textarea_set_text(s_file_ta_from, full_path);
            if (s_file_ta_to) lv_textarea_set_text(s_file_ta_to, full_path);
        }

        APP_LOGI("FileManager: long-press copied %s to %s fields",
                 full_path,
                 is_dir ? "Folder" : "File");
        return;
    }

    if (strcmp(text, "/SD/") == 0) {
        browser_set_path("/");
        APP_LOGI("FileManager: enter /SD/");
        refresh_file_list();
        return;
    }

    if (strcmp(text, "...") == 0) {
        browser_to_parent_path();
        APP_LOGI("FileManager: back to %s", s_browser_fs_path[0] ? s_browser_fs_path : "<root>");
        refresh_file_list();
        return;
    }

    if (text[0] == '/') {
        char next_path[128];
        if (strcmp(s_browser_fs_path, "/") == 0) {
            lv_snprintf(next_path, sizeof(next_path), "%s", text);
        } else {
            lv_snprintf(next_path, sizeof(next_path), "%s%s", s_browser_fs_path, text);
        }
        browser_set_path(next_path);
        APP_LOGI("FileManager: enter dir %s", s_browser_fs_path);
        refresh_file_list();
        return;
    }

    APP_LOGI("FileManager: file selected %s", text);
}

static void append_browser_lines(const char *text) {
    if (!s_file_list || !text) {
        return;
    }

    const char *cursor = text;
    while (*cursor) {
        const char *line_end = strchr(cursor, '\n');
        size_t len = line_end ? (size_t)(line_end - cursor) : strlen(cursor);

        if (len > 0U) {
            char line[96];
            if (len >= sizeof(line)) {
                len = sizeof(line) - 1U;
            }
            memcpy(line, cursor, len);
            line[len] = '\0';

            if (strncmp(line, "DIR ", 4) == 0) {
                (void)browser_append_dir_item(line + 4);
            } else if (strncmp(line, "FILE", 4) == 0) {
                lv_obj_t *btn = lv_list_add_button(s_file_list, LV_SYMBOL_FILE, line + 5);
                lv_obj_add_event_cb(btn, browser_item_cb, LV_EVENT_CLICKED, NULL);
                lv_obj_add_event_cb(btn, browser_item_cb, LV_EVENT_LONG_PRESSED, NULL);
            } else {
                lv_obj_t *btn = lv_list_add_button(s_file_list, LV_SYMBOL_WARNING, line);
                lv_obj_add_event_cb(btn, browser_item_cb, LV_EVENT_CLICKED, NULL);
            }
        }

        if (!line_end) {
            break;
        }
        cursor = line_end + 1;
    }
}

static void refresh_file_list(void) {
    if (!s_file_list) {
        return;
    }

    lv_obj_clean(s_file_list);
    browser_set_path_label();

    app_system_status_t status;
    app_manager_get_system_status(&status);
    if (!status.storage_checked) {
        lv_list_add_button(s_file_list, LV_SYMBOL_REFRESH, "Checking storage...");
        return;
    }

    if (status.storage_available) {
        if (s_browser_fs_path[0] == '\0') {
            lv_obj_t *btn_root = lv_list_add_button(s_file_list, LV_SYMBOL_SD_CARD, "/SD/");
            lv_obj_add_event_cb(btn_root, browser_item_cb, LV_EVENT_CLICKED, NULL);
            lv_obj_add_event_cb(btn_root, browser_item_cb, LV_EVENT_LONG_PRESSED, NULL);
            return;
        }

        if (s_browser_fs_path[0] != '\0') {
            lv_obj_t *btn_back = lv_list_add_button(s_file_list, LV_SYMBOL_LEFT, "...");
            lv_obj_add_event_cb(btn_back, browser_item_cb, LV_EVENT_CLICKED, NULL);
        }

        char listing[1024];
        if (app_manager_storage_list_dir(s_browser_fs_path, listing, sizeof(listing))) {
            append_browser_lines(listing);
        } else {
            lv_obj_t *btn = lv_list_add_button(s_file_list, LV_SYMBOL_WARNING, "List failed");
            lv_obj_add_event_cb(btn, browser_item_cb, LV_EVENT_CLICKED, NULL);
            APP_LOGE("FileManager: list failed path=%s", s_browser_fs_path);
        }
    } else {
        lv_obj_t *btn = lv_list_add_button(s_file_list, LV_SYMBOL_WARNING, "SD card not available");
        lv_obj_add_event_cb(btn, browser_item_cb, LV_EVENT_CLICKED, NULL);
    }
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

    app_system_status_t status;
    app_manager_get_system_status(&status);
    if (status.storage_available != s_last_storage_available ||
        status.storage_checked != s_last_storage_checked) {
        s_last_storage_available = status.storage_available;
        s_last_storage_checked = status.storage_checked;
        APP_LOGI("FileManager: storage availability changed=%d", status.storage_available ? 1 : 0);
        refresh_file_list();
    }
}

static void back_home_cb(lv_event_t *event) {
    LV_UNUSED(event);
    APP_LOGI("FileManager: Home button clicked, navigate to HOME");
    app_manager_navigate_to(SCREEN_ID_HOME);
}

static void file_operation_cb(lv_event_t *event) {
    lv_obj_t *btnm = (lv_obj_t *)lv_event_get_target(event);
    uint32_t id = lv_buttonmatrix_get_selected_button(btnm);
    bool is_folder_op = (lv_event_get_user_data(event) != NULL);
    
    const char *const *btn_map = lv_buttonmatrix_get_map(btnm);
    if (btn_map && btn_map[id]) {
        const char *btn_text = btn_map[id];
        if (btn_text[0] == '\0' || strcmp(btn_text, "\n") == 0) {
            return;
        }

        lv_obj_t *ta_from = is_folder_op ? s_folder_ta_from : s_file_ta_from;
        lv_obj_t *ta_to = is_folder_op ? s_folder_ta_to : s_file_ta_to;
        const char *from = ta_from ? lv_textarea_get_text(ta_from) : "";
        const char *to = ta_to ? lv_textarea_get_text(ta_to) : "";
        char from_norm[128] = "";
        char to_norm[128] = "";
        char msg[128] = "";
        bool ok = false;

        normalize_user_path(from, s_browser_fs_path, from_norm, sizeof(from_norm));
        normalize_user_path(to, s_browser_fs_path, to_norm, sizeof(to_norm));

        APP_LOGI("FileManager: op=%s type=%s from=%s to=%s",
                 btn_text,
                 is_folder_op ? "folder" : "file",
                 from_norm,
                 to_norm);

        if (strcmp(btn_text, "Delete (From)") == 0 || strcmp(btn_text, "Cut") == 0 || strcmp(btn_text, "Copy") == 0) {
            if (from_norm[0] == '\0' || strcmp(from_norm, "/") == 0) {
                lv_snprintf(msg, sizeof(msg), "%s", "Invalid From path");
                APP_LOGE("FileManager: invalid from path");
                set_op_result(msg, true);
                return;
            }
        }
        if (strcmp(btn_text, "New (To)") == 0 || strcmp(btn_text, "Cut") == 0 || strcmp(btn_text, "Copy") == 0) {
            if (to_norm[0] == '\0' || strcmp(to_norm, "/") == 0) {
                lv_snprintf(msg, sizeof(msg), "%s", "Invalid To path");
                APP_LOGE("FileManager: invalid to path");
                set_op_result(msg, true);
                return;
            }
        }

        if (strcmp(btn_text, "Delete (From)") == 0) {
            ok = app_manager_storage_delete(from_norm, msg, sizeof(msg));
        } else if (strcmp(btn_text, "New (To)") == 0) {
            if (is_folder_op) {
                ok = app_manager_storage_mkdir(to_norm, msg, sizeof(msg));
            } else {
                ok = app_manager_storage_touch_file(to_norm, msg, sizeof(msg));
            }
        } else if (strcmp(btn_text, "Cut") == 0) {
            ok = app_manager_storage_rename(from_norm, to_norm, msg, sizeof(msg));
        } else if (strcmp(btn_text, "Copy") == 0) {
            ok = app_manager_storage_copy(from_norm, to_norm, msg, sizeof(msg));
        } else {
            lv_snprintf(msg, sizeof(msg), "%s", "Unsupported operation");
            ok = false;
        }

        if (ok) {
            APP_LOGI("FileManager: op ok: %s", msg);
            set_op_result(msg[0] ? msg : "Operation success", false);
            refresh_file_list();
        } else {
            APP_LOGE("FileManager: op failed: %s", msg);
            set_op_result(msg[0] ? msg : "Operation failed", true);
        }
    }
}

static void screen_delete_cb(lv_event_t *event) {
    LV_UNUSED(event);
    if (s_refresh_timer) {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    s_clock_label = NULL;
    s_file_list = NULL;
    s_browser_path_label = NULL;
    s_file_ta_from = NULL;
    s_file_ta_to = NULL;
    s_folder_ta_from = NULL;
    s_folder_ta_to = NULL;
    s_op_result_label = NULL;
    s_last_storage_available = false;
    s_last_storage_checked = false;
    s_browser_fs_path[0] = '\0';
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

    s_file_list = lv_list_create(tab_browser);
    lv_obj_set_pos(s_file_list, -10, -10);
    lv_obj_set_size(s_file_list, 300, 160);
    lv_obj_set_style_pad_all(s_file_list, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_file_list, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_file_list, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_border_width(s_file_list, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_file_list, lv_color_hex(0xe1e6ee), LV_PART_MAIN);
    lv_obj_set_style_radius(s_file_list, 3, LV_PART_MAIN);

    s_browser_path_label = lv_label_create(tab_browser);
    lv_obj_set_pos(s_browser_path_label, -8, 152);
    lv_obj_set_size(s_browser_path_label, 294, 14);
    lv_obj_set_style_text_font(s_browser_path_label, app_font_ui(), LV_PART_MAIN);
    lv_label_set_long_mode(s_browser_path_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_browser_path_label, "Path: /SD");

    app_manager_request_storage_check();
    s_last_storage_checked = false;
    s_last_storage_available = false;
    browser_set_path("/");
    
    // 刷新文件列表
    refresh_file_list();

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

    s_file_ta_from = lv_textarea_create(op_file_cont);
    lv_obj_set_pos(s_file_ta_from, 20, 30);
    lv_obj_set_size(s_file_ta_from, 160, 30);
    lv_textarea_set_one_line(s_file_ta_from, true);
    lv_textarea_set_text(s_file_ta_from, "");

    lv_obj_t *label_to = lv_label_create(op_file_cont);
    lv_obj_set_pos(label_to, 10, 70);
    lv_label_set_text(label_to, "To:");

    s_file_ta_to = lv_textarea_create(op_file_cont);
    lv_obj_set_pos(s_file_ta_to, 20, 90);
    lv_obj_set_size(s_file_ta_to, 160, 30);
    lv_textarea_set_one_line(s_file_ta_to, true);
    lv_textarea_set_text(s_file_ta_to, "");

    s_op_result_label = lv_label_create(op_file_cont);
    lv_obj_set_pos(s_op_result_label, 10, 122);
    lv_obj_set_size(s_op_result_label, 180, 16);
    lv_label_set_long_mode(s_op_result_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(s_op_result_label, "");

    static const char *file_ops[] = {"Copy", "Cut", "\n", "New (To)", "\n", "Delete (From)", ""};
    lv_obj_t *btnm_file = lv_buttonmatrix_create(op_file_cont);
    lv_obj_set_pos(btnm_file, 20, 142);
    lv_obj_set_size(btnm_file, 160, 88);
    lv_buttonmatrix_set_map(btnm_file, file_ops);
    apply_operation_btnm_style(btnm_file);
    lv_obj_add_event_cb(btnm_file, file_operation_cb, LV_EVENT_VALUE_CHANGED, NULL);

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

    s_folder_ta_from = lv_textarea_create(op_folder_cont);
    lv_obj_set_pos(s_folder_ta_from, 20, 30);
    lv_obj_set_size(s_folder_ta_from, 160, 30);
    lv_textarea_set_one_line(s_folder_ta_from, true);
    lv_textarea_set_text(s_folder_ta_from, "");

    lv_obj_t *label_fold_to = lv_label_create(op_folder_cont);
    lv_obj_set_pos(label_fold_to, 10, 70);
    lv_label_set_text(label_fold_to, "To:");

    s_folder_ta_to = lv_textarea_create(op_folder_cont);
    lv_obj_set_pos(s_folder_ta_to, 20, 90);
    lv_obj_set_size(s_folder_ta_to, 160, 30);
    lv_textarea_set_one_line(s_folder_ta_to, true);
    lv_textarea_set_text(s_folder_ta_to, "");

    static const char *folder_ops[] = {"Copy", "Cut", "\n", "New (To)", "\n", "Delete (From)", ""};
    lv_obj_t *btnm_folder = lv_buttonmatrix_create(op_folder_cont);
    lv_obj_set_pos(btnm_folder, 20, 130);
    lv_obj_set_size(btnm_folder, 160, 100);
    lv_buttonmatrix_set_map(btnm_folder, folder_ops);
    apply_operation_btnm_style(btnm_folder);
    lv_obj_add_event_cb(btnm_folder, file_operation_cb, LV_EVENT_VALUE_CHANGED, (void *)1);

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
    refresh_file_list();
    APP_LOGI("FileManager: create done");
    return scr;
}
