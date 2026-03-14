#ifndef TOUCH_CST836U_H
#define TOUCH_CST836U_H

#include <stdint.h>

bool touch_cst836u_init();
bool touch_cst836u_read(uint16_t * x, uint16_t * y, bool * pressed);

#endif
