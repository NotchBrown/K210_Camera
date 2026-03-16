#pragma once

#include <stdint.h>
#include <SD.h>

#ifndef APP_SD_SPI_BUS
#define APP_SD_SPI_BUS SPI1
#endif

#ifndef APP_SD_TF_DET_PIN
#define APP_SD_TF_DET_PIN 31
#endif

#ifndef APP_SD_TF_CS_PIN
#define APP_SD_TF_CS_PIN 6
#endif

#ifndef APP_SD_TF_MOSI_PIN
#define APP_SD_TF_MOSI_PIN 7
#endif

#ifndef APP_SD_TF_SCLK_PIN
#define APP_SD_TF_SCLK_PIN 8
#endif

#ifndef APP_SD_TF_MISO_PIN
#define APP_SD_TF_MISO_PIN 9
#endif

#ifndef APP_SD_TF_DET_ACTIVE_LOW
#define APP_SD_TF_DET_ACTIVE_LOW 1
#endif

#ifndef APP_SD_SPI_FREQ_HZ
#define APP_SD_SPI_FREQ_HZ 4000000
#endif

bool sd_hw_card_present(void);
bool sd_hw_mount(void);
bool sd_hw_is_mounted(void);
const char *sd_hw_last_error(void);
SDClass &sd_hw_fs(void);

int sd_hw_tf_det_pin(void);
int sd_hw_tf_cs_pin(void);
