#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdint.h>

bool sd_init();
bool sd_read_block(uint32_t block, uint8_t* buffer);
bool sd_write_block(uint32_t block, const uint8_t* buffer);
uint32_t sd_get_card_size();

#endif // SD_CARD_H
