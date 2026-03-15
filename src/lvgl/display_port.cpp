#include <lvgl.h>
#include <Sipeed_ST7789.h>
#include <lcd.h>
#include <st7789.h>

#include "app_config.h"
#include "app_log.h"
#include "display_port.h"

SPIClass g_spi(SPI0);
Sipeed_ST7789 g_lcd(240, 320, g_spi);

static lv_display_t *s_disp = NULL;
static volatile uint32_t s_flush_count = 0;
static volatile uint32_t s_last_flush_us = 0;

#define FRAME_PIXELS (TFT_HOR_RES * TFT_VER_RES)
static lv_color_t s_draw_buf_1[FRAME_PIXELS];
static lv_color_t s_draw_buf_2[FRAME_PIXELS];
static uint16_t s_dma_tx_buf[FRAME_PIXELS];

static inline uint16_t swap_u16(uint16_t value) {
    return (uint16_t)((value >> 8) | (value << 8));
}

static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    LV_UNUSED(disp);

    uint32_t t0 = micros();
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    uint32_t px_cnt = w * h;
    uint16_t *src = (uint16_t *)px_map;

    for (uint32_t i = 0; i < px_cnt; i++) {
        s_dma_tx_buf[i] = swap_u16(src[i]);
    }

    lcd_set_area((uint16_t)area->x1, (uint16_t)area->y1, (uint16_t)area->x2, (uint16_t)area->y2);
    tft_write_byte((uint8_t *)s_dma_tx_buf, px_cnt * 2);

    s_flush_count++;
    s_last_flush_us = micros() - t0;
    lv_display_flush_ready(disp);
}

bool display_port_init() {
    g_lcd.begin(TFT_SPI_FREQ, COLOR_WHITE);
    g_lcd.setRotation(TFT_ROTATION);

    tft_write_command(INVERSION_DISPALY_ON);

    s_disp = lv_display_create(TFT_HOR_RES, TFT_VER_RES);
    if (s_disp == NULL) return false;

    lv_display_set_flush_cb(s_disp, disp_flush_cb);
    lv_display_set_buffers(s_disp, s_draw_buf_1, s_draw_buf_2, sizeof(s_draw_buf_1), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_default(s_disp);

    APP_LOGI("Display init done: DMA flush + dual buffer (%u KB x2)", (unsigned int)(sizeof(s_draw_buf_1) / 1024));
    return true;
}

lv_display_t *display_port_get() {
    return s_disp;
}

uint32_t display_port_get_flush_count() {
    return s_flush_count;
}

uint32_t display_port_get_last_flush_us() {
    return s_last_flush_us;
}
