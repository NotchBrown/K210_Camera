#pragma once

#include <Arduino.h>
#include <stdarg.h>

#ifndef APP_LOG_LEVEL
#define APP_LOG_LEVEL 3
#endif

#define APP_LOG_LEVEL_NONE 0
#define APP_LOG_LEVEL_ERR  1
#define APP_LOG_LEVEL_WARN 2
#define APP_LOG_LEVEL_INFO 3
#define APP_LOG_LEVEL_DBG  4

void app_log_printf(const char *level_tag, const char *fmt, ...);

#define APP_LOGE(fmt, ...) do { if (APP_LOG_LEVEL >= APP_LOG_LEVEL_ERR)  { app_log_printf("E", fmt, ##__VA_ARGS__); } } while (0)
#define APP_LOGW(fmt, ...) do { if (APP_LOG_LEVEL >= APP_LOG_LEVEL_WARN) { app_log_printf("W", fmt, ##__VA_ARGS__); } } while (0)
#define APP_LOGI(fmt, ...) do { if (APP_LOG_LEVEL >= APP_LOG_LEVEL_INFO) { app_log_printf("I", fmt, ##__VA_ARGS__); } } while (0)
#define APP_LOGD(fmt, ...) do { if (APP_LOG_LEVEL >= APP_LOG_LEVEL_DBG)  { app_log_printf("D", fmt, ##__VA_ARGS__); } } while (0)
