#include "sd_hw.h"
#include "app_log.h"

#include <Arduino.h>
#include <SPI.h>
#include <string.h>

/* SD卡命令集 */
#define SD_CMD0      0x00  /* GO_IDLE_STATE */
#define SD_CMD1      0x01  /* SEND_OP_COND */
#define SD_CMD8      0x08  /* SEND_IF_COND */
#define SD_CMD9      0x09  /* SEND_CSD */
#define SD_CMD10     0x0A  /* SEND_CID */
#define SD_CMD12     0x0C  /* STOP_TRANSMISSION */
#define SD_CMD13     0x0D  /* SEND_STATUS */
#define SD_CMD16     0x10  /* SET_BLOCKLEN */
#define SD_CMD17     0x11  /* READ_SINGLE_BLOCK */
#define SD_CMD18     0x12  /* READ_MULTIPLE_BLOCK */
#define SD_CMD23     0x17  /* SET_BLOCK_COUNT */
#define SD_CMD24     0x18  /* WRITE_BLOCK */
#define SD_CMD25     0x19  /* WRITE_MULTIPLE_BLOCK */
#define SD_CMD41     0x29  /* SEND_OP_COND (ACMD) */
#define SD_CMD55     0x37  /* APP_CMD */
#define SD_CMD58     0x3A  /* READ_OCR */
#define SD_CMD59     0x3B  /* CRC_ON_OFF */

/* 响应值 */
#define SD_R1_READY            0x00
#define SD_R1_IDLE_STATE       0x01
#define SD_R1_ERASE_RESET      0x02
#define SD_R1_ILLEGAL_COMMAND  0x04
#define SD_R1_CRC_ERROR        0x08
#define SD_R1_ERASE_SEQ_ERROR  0x10
#define SD_R1_ADDRESS_ERROR    0x20
#define SD_R1_PARAM_ERROR      0x40

/* 数据令牌 */
#define SD_START_DATA_SINGLE_BLOCK_READ  0xFE
#define SD_START_DATA_MULTIPLE_BLOCK_READ 0xFE
#define SD_START_DATA_SINGLE_BLOCK_WRITE 0xFE
#define SD_START_DATA_MULTIPLE_BLOCK_WRITE 0xFC
#define SD_STOP_TRAN_TOKEN 0xFD
#define SD_DATA_RES_MASK  0x1F
#define SD_DATA_RES_ACCEPTED 0x05

namespace {

/* 缓存错误消息 */
static char s_error_msg[96] = "Not initialized";
static bool s_initialized = false;
static bool s_mounted = false;
static int s_card_type = 0;  /* 0=unknown, 1=SDv1, 2=SDv2, 3=SDHC/SDXC */

/* SPI实例 */
static SPIClass s_spi((spi_id_t)APP_SD_SPI_BUS);
static const int CS_PIN = APP_SD_CS_PIN;
static const int SCLK_PIN = APP_SD_SCLK_PIN;
static const int MOSI_PIN = APP_SD_MOSI_PIN;
static const int MISO_PIN = APP_SD_MISO_PIN;
static const int DET_PIN = APP_SD_DET_PIN;

/* 辅助函数 */
static void set_error(const char *msg) {
    if (msg) {
        snprintf(s_error_msg, sizeof(s_error_msg), "%s", msg);
    }
}

/* SPI操作 */
static void cs_low(void) {
    digitalWrite(CS_PIN, LOW);
}

static void cs_high(void) {
    digitalWrite(CS_PIN, HIGH);
}

/* 发送一个字节 */
static uint8_t spi_txrx(uint8_t data) {
    return s_spi.transfer(data);
}

/* 发送多个字节（全FF） */
static void spi_send_clocks(int n) {
    for (int i = 0; i < n; i++) {
        spi_txrx(0xFF);
    }
}

/* 读取一个字节 */
static uint8_t spi_read(void) {
    return spi_txrx(0xFF);
}

/* 发送命令 */
static uint8_t send_cmd(uint8_t cmd, uint32_t arg, uint8_t crc) {
    /* CS低 */
    cs_low();
    
    /* 等待卡就绪（MISO = 0xFF） */
    uint32_t timeout = 100;
    while (timeout--) {
        if (spi_read() == 0xFF) {
            break;
        }
    }
    if (timeout == 0) {
        cs_high();
        APP_LOGW("SD: timeout waiting for card ready before cmd 0x%02X", cmd);
        return 0xFF;
    }
    
    /* 发送命令 */
    spi_txrx(0x40 | cmd);         /* start bit + command */
    spi_txrx((uint8_t)(arg >> 24));
    spi_txrx((uint8_t)(arg >> 16));
    spi_txrx((uint8_t)(arg >> 8));
    spi_txrx((uint8_t)arg);
    spi_txrx(crc);                /* CRC + stop bit */
    
    /* 获取响应 */
    uint8_t response;
    timeout = 100;
    do {
        response = spi_read();
    } while ((response & 0x80) && timeout--);
    
    cs_high();
    return response;
}

/* 发送带参数的命令（计算CRC） */
static uint8_t send_cmd_simple(uint8_t cmd, uint32_t arg) {
    /* 简化版本，不验证CRC */
    return send_cmd(cmd, arg, 0x01);
}

/* 应用命令前缀 */
static uint8_t send_acmd(uint8_t cmd, uint32_t arg) {
    send_cmd_simple(SD_CMD55, 0);
    return send_cmd_simple(cmd, arg);
}

/* 初始化SD卡 */
static bool init_card(void) {
    APP_LOGI("SD: card init start");
    
    s_card_type = 0;
    
    /* 1. SPI初始化（低速） */
    s_spi.setClockDivider(SPI_CLOCK_DIV256);  /* ~400kHz */
    
    pinMode(CS_PIN, OUTPUT);
    digitalWrite(CS_PIN, HIGH);
    
    /* 2. 发送至少74个时钟周期 */
    spi_send_clocks(80);
    APP_LOGI("SD: sent 80 clocks");
    
    /* 3. GO_IDLE_STATE (CMD0) */
    uint8_t resp = send_cmd(SD_CMD0, 0, 0x95);
    if (!(resp & SD_R1_IDLE_STATE)) {
        APP_LOGE("SD: CMD0 failed, resp=0x%02X", resp);
        set_error("CMD0 failed");
        return false;
    }
    APP_LOGI("SD: CMD0 OK");
    
    /* 4. SEND_IF_COND (CMD8) - 检查SD版本 */
    resp = send_cmd(SD_CMD8, 0x1AA, 0x87);
    bool is_sdhc = false;
    
    if ((resp & SD_R1_ILLEGAL_COMMAND) == 0) {
        /* SD v2 */
        uint8_t r4[4];
        cs_low();
        for (int i = 0; i < 4; i++) {
            r4[i] = spi_read();
        }
        cs_high();
        
        if (r4[3] != 0xAA) {
            APP_LOGE("SD: invalid voltage range");
            set_error("Unsupported voltage");
            return false;
        }
        is_sdhc = true;
        APP_LOGI("SD: detected SDv2");
    } else {
        /* SD v1 */
        APP_LOGI("SD: detected SDv1");
    }
    
    /* 5. 初始化 (ACMD41 with/without HCS) */
    uint32_t init_arg = is_sdhc ? 0x40000000 : 0x00000000;
    uint32_t init_timeout = 1000;
    
    while (init_timeout--) {
        resp = send_acmd(SD_CMD41, init_arg);
        if (resp == SD_R1_READY) {
            break;
        }
        delay(5);
    }
    
    if (resp != SD_R1_READY) {
        APP_LOGE("SD: initialization timeout, resp=0x%02X", resp);
        set_error("Initialization timeout");
        return false;
    }
    APP_LOGI("SD: ACMD41 OK");
    
    /* 6. 检查SDHC (CMD58) */
    s_card_type = is_sdhc ? 2 : 1;
    
    resp = send_cmd_simple(SD_CMD58, 0);
    if (resp == SD_R1_READY) {
        cs_low();
        uint8_t ocr[4];
        for (int i = 0; i < 4; i++) {
            ocr[i] = spi_read();
        }
        cs_high();
        
        /* CCS (bit 30 of OCR) */
        if (ocr[0] & 0x40) {
            s_card_type = 3;  /* SDHC/SDXC */
            APP_LOGI("SD: detected SDHC/SDXC");
        }
    }
    
    /* 7. 设置块长度 (CMD16 - 非SDHC) */
    if (s_card_type != 3) {
        resp = send_cmd_simple(SD_CMD16, 512);
        if (resp != SD_R1_READY) {
            APP_LOGW("SD: CMD16 failed");
        }
    }
    
    /* 8. 关闭CRC校验 (CMD59) */
    resp = send_cmd_simple(SD_CMD59, 0);
    APP_LOGI("SD: CMD59 (CRC off) resp=0x%02X", resp);
    
    /* 9. 提升SPI速度 */
    s_spi.setClockDivider(SPI_CLOCK_DIV16);  /* ~10MHz */
    
    APP_LOGI("SD: card init success, type=%d", s_card_type);
    set_error("Initialized");
    return true;
}

} /* namespace */

/* 初始化 */
bool sd_hw_init(void) {
    if (s_initialized) {
        return true;
    }
    
    APP_LOGI("SD: HW init start");
    
    try {
        s_spi.begin(SCLK_PIN, MISO_PIN, MOSI_PIN, CS_PIN);
        s_initialized = true;
        set_error("Initialized");
        APP_LOGI("SD: SPI initialized");
        return true;
    } catch (...) {
        APP_LOGE("SD: SPI begin failed");
        set_error("SPI init failed");
        return false;
    }
}

/* 检测卡 */
bool sd_hw_card_present(void) {
    if (!s_initialized) {
        return false;
    }
    
    /* 检查DET引脚 */
    pinMode(DET_PIN, INPUT_PULLUP);
    bool present = (digitalRead(DET_PIN) == LOW);
    
    APP_LOGI("SD: card_present=%d", present ? 1 : 0);
    return present;
}

/* 挂载 */
bool sd_hw_mount(void) {
    if (!s_initialized) {
        set_error("Not initialized");
        APP_LOGE("SD: not initialized");
        return false;
    }
    
    if (s_mounted) {
        set_error("Already mounted");
        return true;
    }
    
    if (!sd_hw_card_present()) {
        set_error("Card not detected");
        APP_LOGW("SD: card not present");
        return false;
    }
    
    if (!init_card()) {
        return false;
    }
    
    s_mounted = true;
    set_error("Mounted successfully");
    APP_LOGI("SD: mounted successfully");
    return true;
}

/* 卸载 */
void sd_hw_unmount(void) {
    cs_high();
    s_mounted = false;
    APP_LOGI("SD: unmounted");
}

/* 检查挂载状态 */
bool sd_hw_is_mounted(void) {
    return s_mounted;
}

/* 获取错误信息 */
const char *sd_hw_last_error(void) {
    return s_error_msg;
}

/* 获取总容量 (返回KB) */
uint32_t sd_hw_get_total_kb(void) {
    if (!s_mounted) {
        return 0;
    }
    /* TODO: 从卡获取CSD/CID */
    return 0;
}

/* 获取可用空间 (返回KB) */
uint32_t sd_hw_get_free_kb(void) {
    if (!s_mounted) {
        return 0;
    }
    /* TODO: 从卡获取实际容量 */
    return 0;
}
