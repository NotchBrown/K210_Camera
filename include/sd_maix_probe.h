#pragma once

#include <stdint.h>

typedef struct {
	bool ok;
	uint8_t sclk;
	uint8_t miso;
	uint8_t mosi;
	uint8_t cs;
	uint32_t hz;
} sd_maix_probe_result_t;

void sd_maix_probe_once(void);
const sd_maix_probe_result_t *sd_maix_probe_get_result(void);
