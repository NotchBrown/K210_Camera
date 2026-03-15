#pragma once

#include <Arduino.h>

#ifndef APP_LOG_LEVEL
#define APP_LOG_LEVEL 3
#endif

#define APP_LOG_LEVEL_NONE 0
#define APP_LOG_LEVEL_ERR  1
#define APP_LOG_LEVEL_WARN 2
#define APP_LOG_LEVEL_INFO 3
#define APP_LOG_LEVEL_DBG  4

#define APP_LOGE(fmt, ...) do { if (APP_LOG_LEVEL >= APP_LOG_LEVEL_ERR)  { Serial.printf("[E] " fmt "\r\n", ##__VA_ARGS__); } } while (0)
#define APP_LOGW(fmt, ...) do { if (APP_LOG_LEVEL >= APP_LOG_LEVEL_WARN) { Serial.printf("[W] " fmt "\r\n", ##__VA_ARGS__); } } while (0)
#define APP_LOGI(fmt, ...) do { if (APP_LOG_LEVEL >= APP_LOG_LEVEL_INFO) { Serial.printf("[I] " fmt "\r\n", ##__VA_ARGS__); } } while (0)
#define APP_LOGD(fmt, ...) do { if (APP_LOG_LEVEL >= APP_LOG_LEVEL_DBG)  { Serial.printf("[D] " fmt "\r\n", ##__VA_ARGS__); } } while (0)
