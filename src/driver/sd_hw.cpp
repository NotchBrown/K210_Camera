#include "sd_hw.h"

#include "app_log.h"

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

namespace {

#ifndef APP_SD_FS_LOG_ENABLE
#define APP_SD_FS_LOG_ENABLE 1
#endif

#if APP_SD_FS_LOG_ENABLE
#define SD_HW_LOGI(fmt, ...) APP_LOGI("SDHW: " fmt, ##__VA_ARGS__)
#define SD_HW_LOGW(fmt, ...) APP_LOGW("SDHW: " fmt, ##__VA_ARGS__)
#define SD_HW_LOGE(fmt, ...) APP_LOGE("SDHW: " fmt, ##__VA_ARGS__)
#else
#define SD_HW_LOGI(...) do {} while (0)
#define SD_HW_LOGW(...) do {} while (0)
#define SD_HW_LOGE(...) do {} while (0)
#endif

static bool s_mounted = false;
static bool s_det_inited = false;
static bool s_spi_inited = false;
static char s_last_error[96] = "Not checked";

/* TF wiring configured by macros in sd_hw.h */
static const int TF_DET_PIN = APP_SD_TF_DET_PIN;
static const int TF_CS_PIN = APP_SD_TF_CS_PIN;

/* Dedicated SD bus and filesystem instance (must not use display SPI0). */
static SPIClass s_tf_spi((spi_id_t)APP_SD_SPI_BUS);
static SDClass s_tf_sd(s_tf_spi);

} // namespace

SDClass &sd_hw_fs(void) {
    return s_tf_sd;
}

bool sd_hw_card_present(void) {
    if (!s_det_inited) {
        pinMode(TF_DET_PIN, INPUT_PULLUP);
        s_det_inited = true;
    }

    int level = digitalRead(TF_DET_PIN);
    SD_HW_LOGI("det pin=%d level=%d", TF_DET_PIN, level);
    return (APP_SD_TF_DET_ACTIVE_LOW != 0) ? (level == LOW) : (level == HIGH);
}

bool sd_hw_mount(void) {
    SD_HW_LOGI("mount begin card_present=%d", sd_hw_card_present() ? 1 : 0);
    
    if (s_mounted) {
        if (!sd_hw_card_present()) {
            s_mounted = false;
            snprintf(s_last_error, sizeof(s_last_error), "Card removed");
            SD_HW_LOGW("card removed after mounted");
            return false;
        }
        SD_HW_LOGI("already mounted");
        snprintf(s_last_error, sizeof(s_last_error), "Already mounted");
        return true;
    }

    if (!sd_hw_card_present()) {
        snprintf(s_last_error, sizeof(s_last_error), "Card not detected");
        SD_HW_LOGW("card not detected");
        return false;
    }

    /* Setup CS pin */
    pinMode(TF_CS_PIN, OUTPUT);
    digitalWrite(TF_CS_PIN, HIGH);

    if (!s_spi_inited) {
        SD_HW_LOGI("spi begin bus=%d sclk=%d miso=%d mosi=%d cs=%d",
                   (int)APP_SD_SPI_BUS,
                   (int)APP_SD_TF_SCLK_PIN,
                   (int)APP_SD_TF_MISO_PIN,
                   (int)APP_SD_TF_MOSI_PIN,
                   TF_CS_PIN);
        s_tf_spi.begin(APP_SD_TF_SCLK_PIN,
                       APP_SD_TF_MISO_PIN,
                       APP_SD_TF_MOSI_PIN,
                       TF_CS_PIN);
        s_spi_inited = true;
    }

    SD_HW_LOGI("SD.begin start bus=%d freq=%lu cs=%d",
               (int)APP_SD_SPI_BUS,
               (unsigned long)APP_SD_SPI_FREQ_HZ,
               TF_CS_PIN);
    if (!s_tf_sd.begin((uint32_t)APP_SD_SPI_FREQ_HZ, (uint8_t)TF_CS_PIN)) {
        s_mounted = false;
        snprintf(s_last_error, sizeof(s_last_error), "SD.begin failed");
        SD_HW_LOGE("SD.begin failed freq=%lu", (unsigned long)APP_SD_SPI_FREQ_HZ);
        return false;
    }

    if (!s_tf_sd.exists("/")) {
        s_mounted = false;
        snprintf(s_last_error, sizeof(s_last_error), "SD root invalid");
        SD_HW_LOGE("SD root invalid after begin");
        return false;
    }

    SD_HW_LOGI("SD.begin ok");
    s_mounted = true;
    snprintf(s_last_error, sizeof(s_last_error), "Mount success");
    SD_HW_LOGI("mount success");
    return true;
}

bool sd_hw_is_mounted(void) {
    return s_mounted;
}

const char *sd_hw_last_error(void) {
    return s_last_error;
}

int sd_hw_tf_det_pin(void) { return TF_DET_PIN; }
int sd_hw_tf_cs_pin(void) { return TF_CS_PIN; }
