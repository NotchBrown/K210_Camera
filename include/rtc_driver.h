#pragma once

#include <stdbool.h>

#include "app_manager.h"

bool rtc_driver_init(void);
bool rtc_driver_get_datetime(app_datetime_t *dt);
bool rtc_driver_set_datetime(const app_datetime_t *dt);
