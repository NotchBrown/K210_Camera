#include "sd_maix_probe.h"

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "app_log.h"

namespace {

static bool s_done = false;
static sd_maix_probe_result_t s_result = {false, 0, 0, 0, 0, 0};

typedef struct {
    SPIClass *spi;
    const char *spi_name;
    uint8_t sclk;
    uint8_t miso;
    uint8_t mosi;
    const char *name;
} probe_bus_t;

static uint8_t sd_cmd0_probe(SPIClass *spi, uint8_t cs, uint32_t hz) {
    if (!spi) {
        return 0xFF;
    }

    pinMode(cs, OUTPUT);
    digitalWrite(cs, HIGH);

    spi->beginTransaction(SPISettings(hz, MSBFIRST, SPI_MODE0));
    for (int i = 0; i < 10; ++i) {
        spi->transfer(0xFF);
    }

    digitalWrite(cs, LOW);
    const uint8_t cmd0[6] = {0x40, 0x00, 0x00, 0x00, 0x00, 0x95};
    for (int i = 0; i < 6; ++i) {
        spi->transfer(cmd0[i]);
    }

    uint8_t r1 = 0xFF;
    for (int i = 0; i < 16; ++i) {
        r1 = spi->transfer(0xFF);
        if (r1 != 0xFF) {
            break;
        }
    }

    digitalWrite(cs, HIGH);
    spi->transfer(0xFF);
    spi->endTransaction();
    return r1;
}

}  // namespace

void sd_maix_probe_once(void) {
    if (s_done) {
        return;
    }
    s_done = true;

    // Control-path probe: Yehuo documented TF wiring is SPI1 with CS=6.
    static SPIClass probe_spi1(SPI1);
    static SDClass probe_sd1(probe_spi1);

    const probe_bus_t buses[] = {
        {&probe_spi1, "SPI1", 8U, 9U, 7U, "canmv"},
        {&probe_spi1, "SPI1", 8U, 7U, 9U, "canmv-swap"},
    };
    const uint8_t cs_candidates[] = {6U};
    const uint32_t hz_candidates[] = {100000UL, 200000UL, 400000UL};

    APP_LOGI("SDHW: maix-scan start");
    for (size_t bi = 0; bi < sizeof(buses) / sizeof(buses[0]); ++bi) {
        for (size_t ci = 0; ci < sizeof(cs_candidates) / sizeof(cs_candidates[0]); ++ci) {
            for (size_t hi = 0; hi < sizeof(hz_candidates) / sizeof(hz_candidates[0]); ++hi) {
                uint8_t cs = cs_candidates[ci];
                uint32_t hz = hz_candidates[hi];
                SDClass *probe_sd = &probe_sd1;

                buses[bi].spi->begin(buses[bi].sclk, buses[bi].miso, buses[bi].mosi, cs);
                bool ok = probe_sd->begin(hz, cs);
                uint8_t r1 = sd_cmd0_probe(buses[bi].spi, cs, hz);
                APP_LOGI("SDHW: maix-scan %s/%s sclk=%u miso=%u mosi=%u cs=%u f=%lu -> %s",
                         buses[bi].spi_name,
                         buses[bi].name,
                         (unsigned)buses[bi].sclk,
                         (unsigned)buses[bi].miso,
                         (unsigned)buses[bi].mosi,
                         (unsigned)cs,
                         (unsigned long)hz,
                         ok ? "ok" : "fail");
                APP_LOGI("SDHW: maix-cmd0 %s/%s cs=%u f=%lu r1=0x%02x",
                         buses[bi].spi_name,
                         buses[bi].name,
                         (unsigned)cs,
                         (unsigned long)hz,
                         (unsigned)r1);
                probe_sd->end();

                if (ok) {
                    s_result.ok = true;
                    s_result.sclk = buses[bi].sclk;
                    s_result.miso = buses[bi].miso;
                    s_result.mosi = buses[bi].mosi;
                    s_result.cs = cs;
                    s_result.hz = hz;
                    APP_LOGI("SDHW: maix-probe hit sclk=%u miso=%u mosi=%u cs=%u f=%lu",
                             (unsigned)s_result.sclk,
                             (unsigned)s_result.miso,
                             (unsigned)s_result.mosi,
                             (unsigned)s_result.cs,
                             (unsigned long)s_result.hz);
                    return;
                }
            }
        }
    }

    APP_LOGI("SDHW: maix-scan done");
    APP_LOGW("SDHW: maix-probe failed");
}

const sd_maix_probe_result_t *sd_maix_probe_get_result(void) {
    return &s_result;
}
