#pragma once

#include <stdint.h>

bool storage_service_check(uint32_t *total_kb, uint32_t *free_kb,
                           char *msg, uint32_t msg_len);
bool storage_service_format(uint32_t *total_kb, uint32_t *free_kb,
                            char *msg, uint32_t msg_len);
bool storage_service_list_root(char *out, uint32_t out_len);
bool storage_service_list_dir(const char *path, char *out, uint32_t out_len);
bool storage_service_mkdir(const char *path, char *msg, uint32_t msg_len);
bool storage_service_delete(const char *path, char *msg, uint32_t msg_len);
bool storage_service_copy(const char *from, const char *to, char *msg, uint32_t msg_len);
bool storage_service_rename(const char *from, const char *to, char *msg, uint32_t msg_len);
bool storage_service_touch_file(const char *path, char *msg, uint32_t msg_len);
