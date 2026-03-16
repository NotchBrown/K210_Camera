#ifndef FS_MANAGER_H
#define FS_MANAGER_H

#include <stdint.h>
#include <Arduino.h>
#include <SD.h>

bool fs_init();
bool fs_exists(const char* path);
bool fs_mkdir(const char* path);
bool fs_remove(const char* path);
bool fs_rmdir(const char* path);
SDFile fs_open(const char* path, uint8_t mode);
void fs_list_dir(const char* dir_path, void (*callback)(const char*, bool, uint32_t));
uint32_t fs_get_free_space();
uint32_t fs_get_total_space();

#endif // FS_MANAGER_H
