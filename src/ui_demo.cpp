#include <lvgl.h>

#include "ui_demo.h"

static lv_obj_t * s_card = NULL;
static lv_obj_t * s_bar = NULL;
static lv_obj_t * s_meter = NULL;
static lv_obj_t * s_status = NULL;
static lv_obj_t * s_dot = NULL;

static void ui_tick_cb(lv_timer_t * t) {
  LV_UNUSED(t);
  static int32_t v = 0;
  static int32_t dir = 1;
  static const lv_palette_t kPaletteSeq[5] = {
    LV_PALETTE_RED,
    LV_PALETTE_ORANGE,
    LV_PALETTE_YELLOW,
    LV_PALETTE_GREEN,
    LV_PALETTE_BLUE
  };

  v += dir * 3;
  if(v >= 100) {
    v = 100;
    dir = -1;
  }
  if(v <= 0) {
    v = 0;
    dir = 1;
  }

  lv_bar_set_value(s_bar, v, LV_ANIM_OFF);
  lv_arc_set_value(s_meter, v);

  lv_color_t c = lv_palette_main(kPaletteSeq[(v / 20) % 5]);
  lv_obj_set_style_bg_color(s_dot, c, 0);

  char txt[48];
  lv_snprintf(txt, sizeof(txt), "signal %d%%", (int)v);
  lv_label_set_text(s_status, txt);
}

void ui_demo_create() {
  lv_obj_t * scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(0xF4F7FB), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

  s_card = lv_obj_create(scr);
  lv_obj_set_size(s_card, 300, 200);
  lv_obj_center(s_card);
  lv_obj_set_style_radius(s_card, 16, 0);
  lv_obj_set_style_bg_color(s_card, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(s_card, 0, 0);
  lv_obj_set_style_shadow_width(s_card, 18, 0);
  lv_obj_set_style_shadow_opa(s_card, LV_OPA_20, 0);
  lv_obj_set_layout(s_card, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(s_card, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(s_card, 16, 0);
  lv_obj_set_style_pad_row(s_card, 10, 0);

  lv_obj_t * title = lv_label_create(s_card);
  lv_label_set_text(title, "K210 Touch Panel Demo");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0x1F2937), 0);

  s_status = lv_label_create(s_card);
  lv_label_set_text(s_status, "signal 0%");
  lv_obj_set_style_text_color(s_status, lv_color_hex(0x475569), 0);

  s_bar = lv_bar_create(s_card);
  lv_obj_set_size(s_bar, 260, 14);
  lv_bar_set_range(s_bar, 0, 100);
  lv_bar_set_value(s_bar, 0, LV_ANIM_OFF);
  lv_obj_set_style_radius(s_bar, 10, LV_PART_MAIN);
  lv_obj_set_style_radius(s_bar, 10, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_bar, lv_color_hex(0xE2E8F0), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_bar, lv_color_hex(0x0EA5E9), LV_PART_INDICATOR);

  lv_obj_t * row = lv_obj_create(s_card);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, 260, 84);
  lv_obj_set_layout(row, LV_LAYOUT_FLEX);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 14, 0);

  s_meter = lv_arc_create(row);
  lv_obj_set_size(s_meter, 84, 84);
  lv_arc_set_range(s_meter, 0, 100);
  lv_arc_set_value(s_meter, 0);
  lv_obj_set_style_arc_width(s_meter, 8, LV_PART_MAIN);
  lv_obj_set_style_arc_width(s_meter, 8, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(s_meter, lv_color_hex(0xCBD5E1), LV_PART_MAIN);
  lv_obj_set_style_arc_color(s_meter, lv_color_hex(0x0EA5E9), LV_PART_INDICATOR);
  lv_obj_remove_style(s_meter, NULL, LV_PART_KNOB);

  lv_obj_t * panel = lv_obj_create(row);
  lv_obj_set_size(panel, 162, 84);
  lv_obj_set_style_radius(panel, 12, 0);
  lv_obj_set_style_bg_color(panel, lv_color_hex(0xF8FAFC), 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(0xE2E8F0), 0);
  lv_obj_set_style_border_width(panel, 1, 0);

  lv_obj_t * ptxt = lv_label_create(panel);
  lv_label_set_text(ptxt, "tap screen to test touch");
  lv_obj_align(ptxt, LV_ALIGN_TOP_LEFT, 10, 10);
  lv_obj_set_style_text_color(ptxt, lv_color_hex(0x334155), 0);

  s_dot = lv_obj_create(panel);
  lv_obj_set_size(s_dot, 16, 16);
  lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(s_dot, 0, 0);
  lv_obj_set_style_bg_color(s_dot, lv_palette_main(LV_PALETTE_RED), 0);
  lv_obj_align(s_dot, LV_ALIGN_BOTTOM_RIGHT, -10, -10);

  lv_timer_create(ui_tick_cb, 60, NULL);
}
