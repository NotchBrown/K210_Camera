#pragma once

#include <stdint.h>

bool sd_fs_check(uint32_t *total_kb, uint32_t *free_kb, char *msg, uint32_t msg_len);
bool sd_fs_format(uint32_t *total_kb, uint32_t *free_kb, char *msg, uint32_t msg_len);

bool sd_fs_list_root(char *out, uint32_t out_len);
bool sd_fs_list_dir(const char *path, char *out, uint32_t out_len);

bool sd_fs_mkdir(const char *path, char *msg, uint32_t msg_len);
bool sd_fs_delete(const char *path, char *msg, uint32_t msg_len);
bool sd_fs_copy(const char *from, const char *to, char *msg, uint32_t msg_len);
bool sd_fs_rename(const char *from, const char *to, char *msg, uint32_t msg_len);
bool sd_fs_touch_file(const char *path, char *msg, uint32_t msg_len);
