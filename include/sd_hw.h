#pragma once

#include <stdint.h>

#include <SD.h>

// Default TF card wiring for Yehuo K210.
#ifndef APP_SD_CS_PIN
#define APP_SD_CS_PIN 6
#endif

#ifndef APP_SD_MOSI_PIN
#define APP_SD_MOSI_PIN 7
#endif

#ifndef APP_SD_SCLK_PIN
#define APP_SD_SCLK_PIN 8
#endif

#ifndef APP_SD_MISO_PIN
#define APP_SD_MISO_PIN 9
#endif

// TF card detect pin on Yehuo board.
#ifndef APP_SD_TF_DET_PIN
#define APP_SD_TF_DET_PIN 31
#endif

// TF_DET is active-low on Yehuo (0 = card inserted).
#ifndef APP_SD_TF_DET_ACTIVE_LOW
#define APP_SD_TF_DET_ACTIVE_LOW 1
#endif

// Keep a conservative SPI speed for stable startup.
#ifndef APP_SD_SPI_FREQ_HZ
#define APP_SD_SPI_FREQ_HZ 4000000UL
#endif

bool sd_hw_mount(char *msg, uint32_t msg_len);
void sd_hw_unmount(void);
bool sd_hw_is_mounted(void);
bool sd_hw_card_present(void);
uint32_t sd_hw_total_kb(void);
uint32_t sd_hw_free_kb(void);

SDClass &sd_hw_fs(void);
SdFile *sd_hw_root_file(void);
