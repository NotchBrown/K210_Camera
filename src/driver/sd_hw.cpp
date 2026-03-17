#include "sd_hw.h"

#include <Arduino.h>
#include <SPI.h>

#include <stdio.h>

#include "app_log.h"

// Follow test_spi wiring: SPI1 custom pins + low-level Sd2Card/SdVolume path.
static SPIClass s_sd_spi(SPI1, APP_SD_SCLK_PIN, APP_SD_MISO_PIN, APP_SD_MOSI_PIN, -1);
static SDClass s_sd_fs(s_sd_spi);
static Sd2Card s_card(s_sd_spi);
static SdVolume s_volume;
static SdFile s_root;
static bool s_mounted = false;
static bool s_spi_inited = false;
static void set_msg(char *msg, uint32_t msg_len, const char *text) {
    if (!msg || msg_len == 0) {
        return;
    }
    snprintf(msg, msg_len, "%s", text ? text : "");
}

bool sd_hw_card_present(void) {
    pinMode(APP_SD_TF_DET_PIN, INPUT_PULLUP);
    int level = digitalRead(APP_SD_TF_DET_PIN);
    return (APP_SD_TF_DET_ACTIVE_LOW != 0) ? (level == LOW) : (level == HIGH);
}

SDClass &sd_hw_fs(void) {
    return s_sd_fs;
}

SdFile *sd_hw_root_file(void) {
    return s_root.isOpen() ? &s_root : NULL;
}

bool sd_hw_mount(char *msg, uint32_t msg_len) {
    if (s_mounted) {
        set_msg(msg, msg_len, "SD already mounted");
        return true;
    }

    if (!s_spi_inited) {
        pinMode(APP_SD_CS_PIN, OUTPUT);
        digitalWrite(APP_SD_CS_PIN, HIGH);
        s_sd_spi.begin();
        s_spi_inited = true;
        APP_LOGI("SDHW: spi init ok sclk=%d miso=%d mosi=%d cs=%d",
                 APP_SD_SCLK_PIN, APP_SD_MISO_PIN, APP_SD_MOSI_PIN, APP_SD_CS_PIN);
    }

    if (!sd_hw_card_present()) {
        set_msg(msg, msg_len, "TF not detected (DET)");
        APP_LOGW("SDHW: TF_DET indicates no card");
        return false;
    }

    if (!s_card.init(SPI_HALF_SPEED, APP_SD_CS_PIN)) {
        set_msg(msg, msg_len, "Sd2Card init failed");
        APP_LOGE("SDHW: Sd2Card init failed");
        s_mounted = false;
        return false;
    }
    APP_LOGI("SDHW: Sd2Card ok type=%d", (int)s_card.type());

    if (!s_volume.init(s_card)) {
        set_msg(msg, msg_len, "FAT volume init failed");
        APP_LOGE("SDHW: volume init failed");
        s_mounted = false;
        return false;
    }
    APP_LOGI("SDHW: volume init ok FAT=%d", (int)s_volume.fatType());

    if (s_root.isOpen()) {
        s_root.close();
    }
    if (!s_root.openRoot(s_volume)) {
        set_msg(msg, msg_len, "openRoot failed");
        APP_LOGE("SDHW: openRoot failed");
        s_mounted = false;
        return false;
    }

    s_mounted = true;
    set_msg(msg, msg_len, "SD mount ok");
    APP_LOGI("SDHW: mount ok");
    return true;
}

void sd_hw_unmount(void) {
    if (s_root.isOpen()) {
        s_root.close();
    }
    s_mounted = false;
}

bool sd_hw_is_mounted(void) {
    return s_mounted;
}

uint32_t sd_hw_total_kb(void) {
    if (!s_mounted) {
        return 0;
    }
    uint32_t kb = s_volume.blocksPerCluster();
    kb *= s_volume.clusterCount();
    kb /= 2U;
    return kb;
}
