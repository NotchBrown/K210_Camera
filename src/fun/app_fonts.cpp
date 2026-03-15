#include <lvgl.h>

#include "app_fonts.h"

static const lv_font_t *s_font_ui = &lv_font_montserrat_14;

void app_fonts_init(void) {
    s_font_ui = &lv_font_montserrat_14;
}

const lv_font_t *app_font_ui(void) {
    return s_font_ui;
}
