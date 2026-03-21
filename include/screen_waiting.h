#pragma once

#include <lvgl.h>

void screen_waiting_show(const char *message);
void screen_waiting_close(void);
void screen_waiting_set_message(const char *message);