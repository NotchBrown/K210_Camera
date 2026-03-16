#include <Arduino.h>
#include "kendryte-standalone-sdk/lib/drivers/include/fpioa.h"
#include "kendryte-standalone-sdk/lib/drivers/include/spi.h"
#include "app_log.h"

// SD卡SPI引脚定义（使用SPI1）
#define SD_SPI_NUM SPI_DEVICE_1
#define SD_CS_PIN 6  // 根据实际硬件连接调整
#define SD_CLK_PIN 8
#define SD_MISO_PIN 9
#define SD_MOSI_PIN 7

// SD卡命令定义
#define CMD0     0x00  // 复位命令
#define CMD8     0x08  // 发送接口条件
#define CMD17    0x11  // 读单个扇区
#define CMD24    0x18  // 写单个扇区
#define CMD55    0x37  // 应用命令前缀
#define ACMD41   0x29  // 初始化命令
#define CMD58    0x3a  // 读取OCR

// 响应标志
#define R1_IDLE_STATE 0x01
#define R1_ILLEGAL_COMMAND 0x04
#define R1_READY_STATE 0x00

// 数据令牌
#define DATA_START_BLOCK 0xFE
#define DATA_RES_ACCEPTED 0x05

class SDCard {
public:
    bool init() {
        // 初始化FPIOA映射
        fpioa_set_function(SD_CS_PIN, FUNC_SPI1_SS0);
        fpioa_set_function(SD_CLK_PIN, FUNC_SPI1_SCLK);
        fpioa_set_function(SD_MISO_PIN, FUNC_SPI1_D0);
        fpioa_set_function(SD_MOSI_PIN, FUNC_SPI1_D1);
        
        // 初始化SPI
        spi_init(SD_SPI_NUM, SPI_WORK_MODE_0, SPI_FF_STANDARD, 8, 0);
        spi_set_clk_rate(SD_SPI_NUM, 400000); // 初始化时使用低速度
        
        // 片选引脚初始化
        pinMode(SD_CS_PIN, OUTPUT);
        digitalWrite(SD_CS_PIN, HIGH);
        
        // 发送至少74个时钟周期
        for (int i = 0; i < 10; i++) {
            spi_send_data_standard(SD_SPI_NUM, SPI_CHIP_SELECT_0, NULL, 0, NULL, 0);
        }
        
        // 进入空闲状态
        if (sendCommand(CMD0, 0) != R1_IDLE_STATE) {
            APP_LOGE("SD card not in idle state");
            return false;
        }
        
        // 检查SD卡版本
        uint8_t response[4];
        if (sendCommand(CMD8, 0x1AA, response) & R1_ILLEGAL_COMMAND) {
            // SD1.0卡
            cardType = 1;
        } else {
            // SD2.0+卡
            cardType = 2;
            if (response[3] != 0xAA) {
                APP_LOGE("SD card voltage not supported");
                return false;
            }
        }
        
        // 初始化SD卡
        uint32_t arg = cardType == 2 ? 0x40000000 : 0;
        uint8_t status;
        for (int i = 0; i < 100; i++) {
            status = sendAcmd(ACMD41, arg);
            if (status == R1_READY_STATE) {
                break;
            }
            delay(10);
        }
        
        if (status != R1_READY_STATE) {
            APP_LOGE("SD card initialization failed");
            return false;
        }
        
        // 检查是否为SDHC卡
        if (cardType == 2) {
            if (sendCommand(CMD58, 0, response) == R1_READY_STATE) {
                if ((response[0] & 0xC0) == 0xC0) {
                    cardType = 3; // SDHC
                }
            }
        }
        
        // 提高SPI速度
        spi_set_clk_rate(SD_SPI_NUM, 8000000);
        
        APP_LOGI("SD card initialized successfully, type: %d", cardType);
        return true;
    }
    
    bool readBlock(uint32_t block, uint8_t* buffer) {
        if (cardType == 3) {
            // SDHC卡直接使用块地址
        } else {
            // SD1.0/2.0卡需要转换为字节地址
            block <<= 9;
        }
        
        if (sendCommand(CMD17, block) != R1_READY_STATE) {
            APP_LOGE("Read block command failed");
            return false;
        }
        
        // 等待数据开始令牌
        uint8_t token;
        for (int i = 0; i < 1000; i++) {
            token = spiReceive();
            if (token == DATA_START_BLOCK) {
                break;
            }
            if (i == 999) {
                APP_LOGE("Data start token not received");
                return false;
            }
        }
        
        // 读取512字节数据
        for (int i = 0; i < 512; i++) {
            buffer[i] = spiReceive();
        }
        
        // 读取2字节CRC（忽略）
        spiReceive();
        spiReceive();
        
        return true;
    }
    
    bool writeBlock(uint32_t block, const uint8_t* buffer) {
        if (cardType == 3) {
            // SDHC卡直接使用块地址
        } else {
            // SD1.0/2.0卡需要转换为字节地址
            block <<= 9;
        }
        
        if (sendCommand(CMD24, block) != R1_READY_STATE) {
            APP_LOGE("Write block command failed");
            return false;
        }
        
        // 发送数据开始令牌
        spiSend(DATA_START_BLOCK);
        
        // 发送512字节数据
        for (int i = 0; i < 512; i++) {
            spiSend(buffer[i]);
        }
        
        // 发送2字节CRC（使用0xFF）
        spiSend(0xFF);
        spiSend(0xFF);
        
        // 读取响应
        uint8_t response = spiReceive();
        if ((response & 0x1F) != DATA_RES_ACCEPTED) {
            APP_LOGE("Write block failed, response: 0x%02X", response);
            return false;
        }
        
        // 等待写入完成
        while (spiReceive() != 0xFF) {
            ;
        }
        
        return true;
    }
    
    uint32_t getCardSize() {
        // 这里可以实现获取卡容量的功能
        // 暂时返回一个默认值
        return 32 * 1024 * 1024; // 32MB
    }
    
private:
    uint8_t cardType;
    
    uint8_t sendCommand(uint8_t cmd, uint32_t arg, uint8_t* response = NULL) {
        digitalWrite(SD_CS_PIN, LOW);
        
        // 发送命令
        spiSend(cmd | 0x40);
        
        // 发送参数
        for (int i = 24; i >= 0; i -= 8) {
            spiSend((arg >> i) & 0xFF);
        }
        
        // 发送CRC
        uint8_t crc = 0xFF;
        if (cmd == CMD0) crc = 0x95;
        if (cmd == CMD8) crc = 0x87;
        spiSend(crc);
        
        // 读取响应
        uint8_t status;
        for (int i = 0; i < 10; i++) {
            status = spiReceive();
            if (!(status & 0x80)) {
                break;
            }
        }
        
        // 读取额外的响应数据
        if (response && cmd == CMD8) {
            for (int i = 0; i < 4; i++) {
                response[i] = spiReceive();
            }
        }
        
        digitalWrite(SD_CS_PIN, HIGH);
        return status;
    }
    
    uint8_t sendAcmd(uint8_t cmd, uint32_t arg) {
        sendCommand(CMD55, 0);
        return sendCommand(cmd, arg);
    }
    
    void spiSend(uint8_t data) {
        uint8_t tx_buf[1] = {data};
        uint8_t rx_buf[1];
        spi_send_data_standard(SD_SPI_NUM, SPI_CHIP_SELECT_0, tx_buf, 1, NULL, 0);
    }
    
    uint8_t spiReceive() {
        uint8_t tx_buf[1] = {0xFF};
        uint8_t rx_buf[1];
        spi_receive_data_standard(SD_SPI_NUM, SPI_CHIP_SELECT_0, tx_buf, 1, rx_buf, 1);
        return rx_buf[0];
    }
};

SDCard sd_card;

bool sd_init() {
    return sd_card.init();
}

bool sd_read_block(uint32_t block, uint8_t* buffer) {
    return sd_card.readBlock(block, buffer);
}

bool sd_write_block(uint32_t block, const uint8_t* buffer) {
    return sd_card.writeBlock(block, buffer);
}

uint32_t sd_get_card_size() {
    return sd_card.getCardSize();
}
