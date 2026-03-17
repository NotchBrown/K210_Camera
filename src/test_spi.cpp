#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

// SD卡引脚定义
#define SD_CS    SDCARD_CSX_PIN
#define SD_SCLK  SDCARD_CLK_PIN
#define SD_MISO  SDCARD_MIS_PIN
#define SD_MOSI  SDCARD_MOS_PIN

// 创建自定义SPI对象
SPIClass sd_spi(SPI1, SD_SCLK, SD_MISO, SD_MOSI, -1);

// 使用SD库的底层类
Sd2Card card(sd_spi);
SdVolume volume;
SdFile root;

// 测试SD卡
void test_sd_card() {
    Serial.println("\n=== SD卡测试开始 ===");
    
    // 初始化SD卡
    Serial.println("[SD] 初始化SD卡...");
    if (!card.init(SPI_HALF_SPEED, SD_CS)) {
        Serial.println("[SD] 初始化失败！");
        Serial.println("[SD] 检查：");
        Serial.println("[SD] * 卡是否插入？");
        Serial.println("[SD] * 接线是否正确？");
        Serial.println("[SD] * 引脚定义是否正确？");
        Serial.println("=== SD卡测试结束 ===\n");
        return;
    }
    
    Serial.println("[SD] 初始化成功！");
    
    // 打印卡类型
    Serial.println();
    Serial.print("[SD] 卡类型: ");
    switch (card.type()) {
        case SD_CARD_TYPE_SD1:
            Serial.println("SD1");
            break;
        case SD_CARD_TYPE_SD2:
            Serial.println("SD2");
            break;
        case SD_CARD_TYPE_SDHC:
            Serial.println("SDHC");
            break;
        default:
            Serial.println("未知");
    }
    
    // 初始化卷
    if (!volume.init(card)) {
        Serial.println("[SD] 无法找到FAT16/FAT32分区！");
        Serial.println("[SD] 请确保卡已格式化！");
        Serial.println("=== SD卡测试结束 ===\n");
        return;
    }
    
    // 打印卷信息
    Serial.print("[SD] 簇数: ");
    Serial.println(volume.clusterCount());
    Serial.print("[SD] 每簇块数: ");
    Serial.println(volume.blocksPerCluster());
    Serial.print("[SD] 总块数: ");
    Serial.println(volume.blocksPerCluster() * volume.clusterCount());
    Serial.println();
    
    // 打印卷大小
    uint32_t volumesize;
    Serial.print("[SD] 卷类型: FAT");
    Serial.println(volume.fatType(), DEC);
    
    volumesize = volume.blocksPerCluster();
    volumesize *= volume.clusterCount();
    volumesize /= 2;
    Serial.print("[SD] 卷大小 (Kb): ");
    Serial.println(volumesize);
    Serial.print("[SD] 卷大小 (Mb): ");
    volumesize /= 1024;
    Serial.println(volumesize);
    Serial.print("[SD] 卷大小 (Gb): ");
    Serial.println((float)volumesize / 1024.0);
    
    // 列出根目录内容
    Serial.println("\n[SD] 卡上的文件 (名称, 日期和大小): ");
    root.openRoot(volume);
    root.ls(LS_R | LS_DATE | LS_SIZE);
    
    Serial.println("=== SD卡测试结束 ===\n");
}
