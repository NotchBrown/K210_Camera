#include <lvgl.h>
#include <Sipeed_ST7789.h>
#include <lcd.h>
#include <st7789.h>

#include "app_config.h"
#include "display_port.h"

SPIClass g_spi(SPI0);
Sipeed_ST7789 g_lcd(240, 320, g_spi);

static lv_display_t * s_disp = NULL;
static volatile uint32_t s_flush_count = 0;

#define DRAW_BUF_SIZE (TFT_HOR_RES * TFT_VER_RES / 10 * (LV_COLOR_DEPTH / 8))
static uint32_t s_draw_buf[DRAW_BUF_SIZE / 4];
static uint16_t s_line_buf[TFT_HOR_RES];

static inline uint16_t swap_u16(uint16_t value) {
  return (uint16_t)((value >> 8) | (value << 8));
}

static void disp_flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
  uint32_t w = lv_area_get_width(area);
  uint32_t h = lv_area_get_height(area);
  uint16_t * src = (uint16_t *)px_map;

  s_flush_count++;

  for(uint32_t row = 0; row < h; row++) {
    for(uint32_t col = 0; col < w; col++) {
      s_line_buf[col] = swap_u16(src[row * w + col]);
    }

    lcd_set_area((uint16_t)area->x1, (uint16_t)(area->y1 + row), (uint16_t)area->x2, (uint16_t)(area->y1 + row));
    tft_write_byte((uint8_t *)s_line_buf, w * 2);
  }

  lv_display_flush_ready(disp);
}

bool display_port_init() {
  g_lcd.begin(TFT_SPI_FREQ, COLOR_WHITE);
  g_lcd.setRotation(TFT_ROTATION);

  /* Force disable panel inversion. */
  tft_write_command(INVERSION_DISPALY_ON);

  s_disp = lv_display_create(TFT_HOR_RES, TFT_VER_RES);
  if(s_disp == NULL) return false;

  lv_display_set_flush_cb(s_disp, disp_flush_cb);
  lv_display_set_buffers(s_disp, s_draw_buf, NULL, sizeof(s_draw_buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
  lv_display_set_default(s_disp);

  return true;
}

lv_display_t * display_port_get() {
  return s_disp;
}

uint32_t display_port_get_flush_count() {
  return s_flush_count;
}
