#include "app_log.h"

#include "kendryte-standalone-sdk/lib/freertos/include/FreeRTOS.h"
#include "kendryte-standalone-sdk/lib/freertos/include/semphr.h"

#include <stdarg.h>
#include <stdio.h>

namespace {

static SemaphoreHandle_t s_log_lock = NULL;
static bool s_log_lock_create_attempted = false;

static void ensure_log_lock(void) {
    if (s_log_lock != NULL) {
        return;
    }

    if (s_log_lock_create_attempted) {
        return;
    }

    s_log_lock_create_attempted = true;
    s_log_lock = xSemaphoreCreateMutex();
}

static void write_line(const char *level_tag, const char *msg) {
    const char *tag = level_tag ? level_tag : "I";
    const char *text = msg ? msg : "";
    Serial.printf("[%s] %s\r\n", tag, text);
}

} // namespace

void app_log_printf(const char *level_tag, const char *fmt, ...) {
    char buf[256];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "", args);
    va_end(args);

    ensure_log_lock();

    if (s_log_lock != NULL) {
        if (xSemaphoreTake(s_log_lock, pdMS_TO_TICKS(20)) == pdTRUE) {
            write_line(level_tag, buf);
            (void)xSemaphoreGive(s_log_lock);
            return;
        }
    }

    write_line(level_tag, buf);
}
