#include <Arduino.h>
#include <SD.h>
#include "app_log.h"

#define SD_CS_PIN 26  // 与sd_card.cpp中保持一致

bool fs_init() {
    if (!SD.begin(SD_CS_PIN)) {
        return false;
    }
    return true;
}

bool fs_exists(const char* path) {
    return SD.exists(path);
}

bool fs_mkdir(const char* path) {
    return SD.mkdir(path);
}

bool fs_remove(const char* path) {
    return SD.remove(path);
}

bool fs_rmdir(const char* path) {
    return SD.rmdir(path);
}

SDFile fs_open(const char* path, uint8_t mode) {
    return SD.open(path, mode);
}

void fs_list_dir(const char* dir_path, void (*callback)(const char*, bool, uint32_t)) {
    SDFile dir = SD.open(dir_path);
    if (!dir) {
        APP_LOGE("FS: Failed to open directory: %s", dir_path);
        return;
    }
    
    if (!dir.isDirectory()) {
        APP_LOGE("FS: %s is not a directory", dir_path);
        dir.close();
        return;
    }
    
    SDFile file;
    while (file = dir.openNextFile()) {
        if (callback) {
            callback(file.name(), file.isDirectory(), file.size());
        }
        file.close();
    }
    
    dir.close();
}

uint32_t fs_get_free_space() {
    // 这里可以实现获取SD卡剩余空间的功能
    // 暂时返回一个默认值
    return 25 * 1024 * 1024; // 25MB
}

uint32_t fs_get_total_space() {
    // 这里可以实现获取SD卡总空间的功能
    // 暂时返回一个默认值
    return 32 * 1024 * 1024; // 32MB
}
