#include "sd_hw.h"

#include <Arduino.h>
#include <SPI.h>

#include <stdio.h>

#include "kendryte-standalone-sdk/lib/drivers/include/fpioa.h"

#include "app_log.h"
#include "sd_maix_probe.h"

static SPIClass s_sd_spi(SPI1);
static SPIClass s_default_spi(SPI);
static SdFat s_sd;
static bool s_mounted = false;
static bool s_spi_inited = false;

static bool sd_card_present(void) {
    pinMode(APP_SD_TF_DET_PIN, INPUT_PULLUP);
    int level = digitalRead(APP_SD_TF_DET_PIN);
    return (APP_SD_TF_DET_ACTIVE_LOW != 0) ? (level == LOW) : (level == HIGH);
}

static void sd_log_raw_pins_once(void) {
    static bool done = false;
    if (done) {
        return;
    }
    done = true;

    const uint8_t pins[] = {31U, 6U, 10U, 28U, 7U, 9U, 8U, 26U};
    for (size_t i = 0; i < sizeof(pins) / sizeof(pins[0]); ++i) {
        pinMode(pins[i], INPUT_PULLUP);
    }

    APP_LOGI("SDHW: raw pin sample det31=%d cs6=%d cs10=%d cs28=%d miso7=%d miso9=%d sck8=%d sck26=%d",
             digitalRead(31),
             digitalRead(6),
             digitalRead(10),
             digitalRead(28),
             digitalRead(7),
             digitalRead(9),
             digitalRead(8),
             digitalRead(26));
}

typedef struct {
    SPIClass *spi;
    bool force_spi1_mux;
    uint8_t sclk;
    uint8_t miso;
    uint8_t mosi;
    uint8_t cs;
    uint8_t cs_gpiohs;
    uint32_t freq;
    const char *name;
} sd_try_cfg_t;

static void sd_apply_spi1_mux(const sd_try_cfg_t *cfg) {
    if (!cfg || !cfg->force_spi1_mux) {
        return;
    }

    // Align with vendor sdcard.c pin muxing for SPI1 + GPIOHS CS.
    fpioa_set_function(cfg->sclk, FUNC_SPI1_SCLK);
    fpioa_set_function(cfg->mosi, FUNC_SPI1_D0);
    fpioa_set_function(cfg->miso, FUNC_SPI1_D1);
    fpioa_set_function(cfg->cs, (fpioa_function_t)(FUNC_GPIOHS0 + cfg->cs_gpiohs));
}

static void sd_spi_warmup(SPIClass *spi, uint8_t cs, uint32_t hz) {
    if (!spi) {
        return;
    }

    // Match vendor init sequence: keep CS high and send >=80 clocks at low speed.
    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);
    spi->beginTransaction(SPISettings(hz, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 10; ++i) {
        spi->transfer(0xFF);
    }
    spi->endTransaction();
}

static bool try_mount_once(const sd_try_cfg_t *cfg, char *msg, uint32_t msg_len) {
    if (!cfg || !cfg->spi) {
        return false;
    }

    sd_apply_spi1_mux(cfg);
    cfg->spi->begin(cfg->sclk, cfg->miso, cfg->mosi);
    sd_spi_warmup(cfg->spi, cfg->cs, 200000UL);

    // Quick electrical hint: sample MISO with CS high/low before CMD0.
    uint8_t hi_sample = 0xFF;
    uint8_t lo_sample = 0xFF;
    pinMode(cfg->cs, OUTPUT);
    digitalWrite(cfg->cs, HIGH);
    cfg->spi->beginTransaction(SPISettings(100000UL, MSBFIRST, SPI_MODE0));
    hi_sample = cfg->spi->transfer(0xFF);
    cfg->spi->endTransaction();
    digitalWrite(cfg->cs, LOW);
    cfg->spi->beginTransaction(SPISettings(100000UL, MSBFIRST, SPI_MODE0));
    lo_sample = cfg->spi->transfer(0xFF);
    cfg->spi->endTransaction();
    digitalWrite(cfg->cs, HIGH);

    APP_LOGI("SDHW: try %s sclk=%u miso=%u mosi=%u cs=%u f=%lu",
             cfg->name ? cfg->name : "<null>",
             (unsigned)cfg->sclk, (unsigned)cfg->miso,
             (unsigned)cfg->mosi, (unsigned)cfg->cs,
             (unsigned long)cfg->freq);
    APP_LOGI("SDHW: line-sample %s miso_hi=0x%02x miso_lo=0x%02x cs_gpiohs=%u",
             cfg->name ? cfg->name : "<null>",
             (unsigned)hi_sample,
             (unsigned)lo_sample,
             (unsigned)cfg->cs_gpiohs);

    SdSpiConfig spi_cfg(cfg->cs, DEDICATED_SPI, SD_SCK_HZ(cfg->freq), cfg->spi);
    if (s_sd.begin(spi_cfg)) {
        if (msg && msg_len > 0) {
            snprintf(msg, msg_len, "SD mounted (%s)", cfg->name ? cfg->name : "ok");
        }
        APP_LOGI("SDHW: mount ok via %s", cfg->name ? cfg->name : "<null>");
        return true;
    }

    uint32_t ec = 0;
    uint32_t ed = 0;
    if (s_sd.card()) {
        ec = s_sd.card()->errorCode();
        ed = s_sd.card()->errorData();
    }
    APP_LOGW("SDHW: try %s failed ec=0x%02lx ed=0x%08lx",
             cfg->name ? cfg->name : "<null>",
             (unsigned long)ec, (unsigned long)ed);
    s_sd.end();
    return false;
}

static void set_msg(char *msg, uint32_t msg_len, const char *text) {
    if (!msg || msg_len == 0) {
        return;
    }
    snprintf(msg, msg_len, "%s", text ? text : "");
}

SdFat &sd_hw_fs(void) {
    return s_sd;
}

bool sd_hw_mount(char *msg, uint32_t msg_len) {
    if (s_mounted) {
        set_msg(msg, msg_len, "SD already mounted");
        return true;
    }

    if (!s_spi_inited) {
        s_spi_inited = true;
        APP_LOGI("SDHW: mount start");
    }

    sd_log_raw_pins_once();

    if (!sd_card_present()) {
        set_msg(msg, msg_len, "TF not detected (DET)");
        APP_LOGW("SDHW: TF_DET indicates no card");
        return false;
    }

    sd_maix_probe_once();
    const sd_maix_probe_result_t *probe = sd_maix_probe_get_result();

    if (probe && probe->ok) {
        sd_try_cfg_t hit = {
            &s_sd_spi,
            true,
            probe->sclk,
            probe->miso,
            probe->mosi,
            probe->cs,
            probe->cs,
            probe->hz,
            "SPI1-maix-hit"
        };
        if (try_mount_once(&hit, msg, msg_len)) {
            s_mounted = true;
            return true;
        }
    }

    sd_try_cfg_t tries[] = {
        // CS GPIOHS remap scan for primary wiring (some firmwares bind CS to a fixed GPIOHS index).
        {&s_sd_spi, true, 8U, 9U, 7U, 6U, 0U, 200000UL, "SPI1-canmv-cs6-ghs0-200k"},
        {&s_sd_spi, true, 8U, 9U, 7U, 6U, 6U, 200000UL, "SPI1-canmv-cs6-ghs6-200k"},
        {&s_sd_spi, true, 8U, 9U, 7U, 6U, 10U, 200000UL, "SPI1-canmv-cs6-ghs10-200k"},

        // Vendor CanMV default profile from /flash/config.json template.
        {&s_sd_spi, true, 8U, 9U, 7U, 6U, 6U, 200000UL, "SPI1-canmv-200k"},
        {&s_sd_spi, true, 8U, 9U, 7U, 6U, 6U, 400000UL, "SPI1-canmv-400k"},
        {&s_sd_spi, true, 8U, 9U, 7U, 6U, 6U, 1000000UL, "SPI1-canmv-1MHz"},
        {&s_sd_spi, true, 8U, 9U, 7U, 6U, 6U, APP_SD_SPI_FREQ_HZ, "SPI1-canmv-fast"},

        // App-configured profile.
        {&s_sd_spi, true, (uint8_t)APP_SD_SCLK_PIN, (uint8_t)APP_SD_MISO_PIN, (uint8_t)APP_SD_MOSI_PIN, (uint8_t)APP_SD_CS_PIN, (uint8_t)APP_SD_CS_PIN, 200000UL, "SPI1-app-200k"},
        {&s_sd_spi, true, (uint8_t)APP_SD_SCLK_PIN, (uint8_t)APP_SD_MISO_PIN, (uint8_t)APP_SD_MOSI_PIN, (uint8_t)APP_SD_CS_PIN, (uint8_t)APP_SD_CS_PIN, 400000UL, "SPI1-app-400k"},
        {&s_sd_spi, true, (uint8_t)APP_SD_SCLK_PIN, (uint8_t)APP_SD_MISO_PIN, (uint8_t)APP_SD_MOSI_PIN, (uint8_t)APP_SD_CS_PIN, (uint8_t)APP_SD_CS_PIN, 1000000UL, "SPI1-app-1MHz"},
        {&s_sd_spi, true, (uint8_t)APP_SD_SCLK_PIN, (uint8_t)APP_SD_MISO_PIN, (uint8_t)APP_SD_MOSI_PIN, (uint8_t)APP_SD_CS_PIN, (uint8_t)APP_SD_CS_PIN, APP_SD_SPI_FREQ_HZ, "SPI1-app-fast"},

        // Swapped data lines for boards where D0/D1 naming is opposite.
        {&s_sd_spi, true, (uint8_t)APP_SD_SCLK_PIN, (uint8_t)APP_SD_MOSI_PIN, (uint8_t)APP_SD_MISO_PIN, (uint8_t)APP_SD_CS_PIN, (uint8_t)APP_SD_CS_PIN, 200000UL, "SPI1-app-swap-200k"},
        {&s_sd_spi, true, (uint8_t)APP_SD_SCLK_PIN, (uint8_t)APP_SD_MOSI_PIN, (uint8_t)APP_SD_MISO_PIN, (uint8_t)APP_SD_CS_PIN, (uint8_t)APP_SD_CS_PIN, 400000UL, "SPI1-app-swap-400k"},
        {&s_sd_spi, true, (uint8_t)APP_SD_SCLK_PIN, (uint8_t)APP_SD_MOSI_PIN, (uint8_t)APP_SD_MISO_PIN, (uint8_t)APP_SD_CS_PIN, (uint8_t)APP_SD_CS_PIN, 1000000UL, "SPI1-app-swap-1MHz"},

        // Fallback to default SPI instance with app profile.
        {&s_default_spi, false, (uint8_t)APP_SD_SCLK_PIN, (uint8_t)APP_SD_MISO_PIN, (uint8_t)APP_SD_MOSI_PIN, (uint8_t)APP_SD_CS_PIN, 0U, 200000UL, "SPI-def-app-200k"},
        {&s_default_spi, false, (uint8_t)APP_SD_SCLK_PIN, (uint8_t)APP_SD_MISO_PIN, (uint8_t)APP_SD_MOSI_PIN, (uint8_t)APP_SD_CS_PIN, 0U, 400000UL, "SPI-def-app-400k"},
        {&s_default_spi, false, (uint8_t)APP_SD_SCLK_PIN, (uint8_t)APP_SD_MISO_PIN, (uint8_t)APP_SD_MOSI_PIN, (uint8_t)APP_SD_CS_PIN, 0U, 1000000UL, "SPI-def-app-1MHz"},
    };

    for (size_t i = 0; i < sizeof(tries) / sizeof(tries[0]); ++i) {
        if (try_mount_once(&tries[i], msg, msg_len)) {
            s_mounted = true;
            return true;
        }
    }

    if (msg && msg_len > 0) {
        snprintf(msg, msg_len, "SD mount failed (all tries)");
    }
    APP_LOGE("SDHW: SD mount failed (all tries)");
    s_mounted = false;
    return false;
}

void sd_hw_unmount(void) {
    if (s_mounted) {
        s_sd.end();
        s_mounted = false;
    }
}

bool sd_hw_is_mounted(void) {
    return s_mounted;
}

uint32_t sd_hw_total_kb(void) {
    if (!s_mounted || !s_sd.card()) {
        return 0;
    }

    const uint64_t sectors = (uint64_t)s_sd.card()->sectorCount();
    return (uint32_t)((sectors * 512ULL) / 1024ULL);
}
