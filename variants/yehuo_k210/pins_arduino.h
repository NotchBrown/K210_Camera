#ifndef _VARIANT_BOARD_YEHUO_K210
#define _VARIANT_BOARD_YEHUO_K210

#include <stdint.h>

#define RISCV
#include "platform.h"

#include "Arduino.h"
#include "pwm.h"

#ifdef __cplusplus
#include "UARTClass.h"
extern class UARTHSClass Serial;
extern class UARTClass Serial1;
extern class UARTClass Serial2;
extern class UARTClass Serial3;
#endif

/* BOARD PIN DEFINE - Yehuo K210 Custom pinout */

/* UART */
#define RX1                  4
#define TX1                  5

#define RX0                  4
#define TX0                  5

/* TFT LCD */
#define TFT_CSX_PIN         28
#define TFT_RST_PIN         29
#define TFT_DCX_PIN         27
#define TFT_CLK_PIN         26

/* SD Card */
#define SDCARD_CLK_PIN       8
#define SDCARD_MIS_PIN       9
#define SDCARD_MOS_PIN       7
#define SDCARD_CSX_PIN       6

/* Camera */
#define CAMERA_PCLK_PIN     23
#define CAMERA_XCLK_PIN     22
#define CAMERA_HSYNC_PIN    20
#define CAMERA_VSYNC_PIN    18
#define CAMERA_PWDN_PIN     21
#define CAMERA_RST_PIN      19
#define CAMERA_SDA_PIN      24
#define CAMERA_SCL_PIN      25

/* Flash FS */
#define FLASH_FS_ADDR      0xD00000
#define FLASH_FS_SIZE      0x300000

/* I2C */
#define SDA                24
#define SCL                25

/* SPI */
#define SPI0_CS1           28
#define SPI0_MISO          26
#define SPI0_SCLK          26
#define SPI0_MOSI           9
#define SPI0_CS0            6

#define MD_PIN_MAP(fpio)   (fpio)
#define ORG_PIN_MAP(org_pin)    (org_pin)

static const uint8_t SS   = SPI0_CS0 ;
static const uint8_t MOSI = SPI0_MOSI;
static const uint8_t MISO = SPI0_MISO;
static const uint8_t SCK  = SPI0_SCLK;

typedef struct _pwm_fpio_set_t{
    pwm_channel_number_t channel;
    pwm_device_number_t device;
    uint8_t inuse;
}pwm_fpio_set_t;

#define VARIANT_NUM_GPIOHS (32)
#define VARIANT_NUM_GPIO   ( 8)
#define VARIANT_NUM_PWM    (12)
#define VARIANT_NUM_I2C    ( 3)
#define VARIANT_NUM_SPI    ( 3)
#define VARIANT_NUM_UART   ( 3)

#endif

