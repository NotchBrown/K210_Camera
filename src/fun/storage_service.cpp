#include "storage_service.h"

#include "app_log.h"
#include "sd_fs.h"

bool storage_service_check(uint32_t *total_kb, uint32_t *free_kb,
                           char *msg, uint32_t msg_len) {
    APP_LOGI("StorageSvc: check enter");
    bool ok = sd_fs_check(total_kb, free_kb, msg, msg_len);
    APP_LOGI("StorageSvc: check leave ok=%d", ok ? 1 : 0);
    return ok;
}

bool storage_service_format(uint32_t *total_kb, uint32_t *free_kb,
                            char *msg, uint32_t msg_len) {
    APP_LOGI("StorageSvc: format enter");
    bool ok = sd_fs_format(total_kb, free_kb, msg, msg_len);
    APP_LOGI("StorageSvc: format leave ok=%d", ok ? 1 : 0);
    return ok;
}

bool storage_service_list_root(char *out, uint32_t out_len) {
    return sd_fs_list_root(out, out_len);
}

bool storage_service_list_dir(const char *path, char *out, uint32_t out_len) {
    return sd_fs_list_dir(path, out, out_len);
}

bool storage_service_mkdir(const char *path, char *msg, uint32_t msg_len) {
    APP_LOGI("StorageSvc: mkdir %s", path ? path : "<null>");
    return sd_fs_mkdir(path, msg, msg_len);
}

bool storage_service_delete(const char *path, char *msg, uint32_t msg_len) {
    APP_LOGI("StorageSvc: delete %s", path ? path : "<null>");
    return sd_fs_delete(path, msg, msg_len);
}

bool storage_service_copy(const char *from, const char *to, char *msg, uint32_t msg_len) {
    APP_LOGI("StorageSvc: copy %s -> %s", from ? from : "<null>", to ? to : "<null>");
    return sd_fs_copy(from, to, msg, msg_len);
}

bool storage_service_rename(const char *from, const char *to, char *msg, uint32_t msg_len) {
    APP_LOGI("StorageSvc: rename %s -> %s", from ? from : "<null>", to ? to : "<null>");
    return sd_fs_rename(from, to, msg, msg_len);
}

bool storage_service_touch_file(const char *path, char *msg, uint32_t msg_len) {
    APP_LOGI("StorageSvc: touch %s", path ? path : "<null>");
    return sd_fs_touch_file(path, msg, msg_len);
}
