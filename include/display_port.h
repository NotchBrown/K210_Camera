#ifndef DISPLAY_PORT_H
#define DISPLAY_PORT_H

#include <lvgl.h>

bool display_port_init();
lv_display_t * display_port_get();
uint32_t display_port_get_flush_count();
uint32_t display_port_get_last_flush_us();

#endif
