#include <Arduino.h>
#include <Wire.h>

#include "app_config.h"
#include "touch_cst836u.h"

static bool s_touch_inited = false;

#define CST_RAW_MAX_X 240
#define CST_RAW_MAX_Y 320

static bool cst_read_regs(uint8_t reg, uint8_t * data, uint8_t len) {
  Wire.beginTransmission(CTP_I2C_ADDR);
  Wire.write(reg);
  if(Wire.endTransmission(false) != 0) return false;

  uint8_t got = Wire.requestFrom((uint8_t)CTP_I2C_ADDR, len);
  if(got != len) return false;

  for(uint8_t i = 0; i < len; i++) {
    data[i] = (uint8_t)Wire.read();
  }
  return true;
}

bool touch_cst836u_init() {
  pinMode(CTP_RST_PIN, OUTPUT);
  pinMode(CTP_INT_PIN, INPUT_PULLUP);

  digitalWrite(CTP_RST_PIN, LOW);
  delay(10);
  digitalWrite(CTP_RST_PIN, HIGH);
  delay(120);

  Wire.begin((uint8_t)CTP_SDA_PIN, (uint8_t)CTP_SCL_PIN, (uint32_t)CTP_I2C_FREQ);
  s_touch_inited = true;
  return true;
}

bool touch_cst836u_read(uint16_t * x, uint16_t * y, bool * pressed) {
  if(!s_touch_inited) {
    *pressed = false;
    return false;
  }

  uint8_t fingers = 0;
  if(!cst_read_regs(0x02, &fingers, 1)) {
    *pressed = false;
    return false;
  }

  if((fingers & 0x0F) == 0) {
    *pressed = false;
    return true;
  }

  uint8_t p[4] = {0};
  if(!cst_read_regs(0x03, p, 4)) {
    *pressed = false;
    return false;
  }

  uint16_t raw_x = (uint16_t)(((p[0] & 0x0F) << 8) | p[1]);
  uint16_t raw_y = (uint16_t)(((p[2] & 0x0F) << 8) | p[3]);

  if(raw_x >= CST_RAW_MAX_X) raw_x = CST_RAW_MAX_X - 1;
  if(raw_y >= CST_RAW_MAX_Y) raw_y = CST_RAW_MAX_Y - 1;

  /* CST836U native panel space is typically portrait (240x320).
   * Convert to landscape screen space (320x240):
   * screen_x <- raw_y, screen_y <- flipped raw_x
   */
  uint16_t tx = (uint16_t)((raw_y * TFT_HOR_RES) / CST_RAW_MAX_Y);
  uint16_t ty = (uint16_t)((((CST_RAW_MAX_X - 1) - raw_x) * TFT_VER_RES) / CST_RAW_MAX_X);

  if(tx >= TFT_HOR_RES) tx = TFT_HOR_RES - 1;
  if(ty >= TFT_VER_RES) ty = TFT_VER_RES - 1;

#if TFT_ROTATION == 2
  ty = (TFT_VER_RES - 1) - ty;
#endif

  *x = tx;
  *y = ty;
  *pressed = true;
  return true;
}
