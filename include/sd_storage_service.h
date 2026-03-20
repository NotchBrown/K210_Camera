#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SD_STORAGE_STATE_NO_CARD = 1,
    SD_STORAGE_STATE_CARD_NOT_INIT = 2,
    SD_STORAGE_STATE_INIT_FAILED = 3,
    SD_STORAGE_STATE_INIT_OK_IDLE = 4,
    SD_STORAGE_STATE_INIT_OK_BUSY = 5,
    SD_STORAGE_STATE_INIT_OK_TASK_FAILED = 6,
    SD_STORAGE_STATE_INIT_OK_TASK_SUCCESS = 7,
} sd_storage_state_t;

typedef enum {
    SD_STORAGE_OP_NONE = 0,
    SD_STORAGE_OP_CHECK,
    SD_STORAGE_OP_FORMAT,
    SD_STORAGE_OP_LIST_ROOT,
    SD_STORAGE_OP_LIST_DIR,
    SD_STORAGE_OP_MKDIR,
    SD_STORAGE_OP_DELETE,
    SD_STORAGE_OP_COPY,
    SD_STORAGE_OP_RENAME,
    SD_STORAGE_OP_TOUCH_FILE,
} sd_storage_op_t;

typedef struct {
    sd_storage_state_t state;
    bool card_present;
    bool initialized;
    bool busy;
    bool checked;
    bool available;
    uint32_t total_kb;
    uint32_t free_kb;
    char message[96];
} sd_storage_status_t;

typedef void (*sd_storage_callback_t)(bool ok, const char *result, void *user_data);

void sd_storage_service_task(void *arg);
void sd_storage_service_get_status(sd_storage_status_t *status);

void sd_storage_service_request_check(void);
void sd_storage_service_request_format(void);

bool sd_storage_service_list_root(char *out, uint32_t out_len);
bool sd_storage_service_list_dir(const char *path, char *out, uint32_t out_len);
bool sd_storage_service_mkdir(const char *path, char *msg, uint32_t msg_len);
bool sd_storage_service_delete(const char *path, char *msg, uint32_t msg_len);
bool sd_storage_service_copy(const char *from, const char *to, char *msg, uint32_t msg_len);
bool sd_storage_service_rename(const char *from, const char *to, char *msg, uint32_t msg_len);
bool sd_storage_service_touch_file(const char *path, char *msg, uint32_t msg_len);

bool sd_storage_service_list_root_async(char *out, uint32_t out_len, sd_storage_callback_t callback, void *user_data);
bool sd_storage_service_list_dir_async(const char *path, char *out, uint32_t out_len, sd_storage_callback_t callback, void *user_data);
bool sd_storage_service_mkdir_async(const char *path, char *msg, uint32_t msg_len, sd_storage_callback_t callback, void *user_data);
bool sd_storage_service_delete_async(const char *path, char *msg, uint32_t msg_len, sd_storage_callback_t callback, void *user_data);
bool sd_storage_service_copy_async(const char *from, const char *to, char *msg, uint32_t msg_len, sd_storage_callback_t callback, void *user_data);
bool sd_storage_service_rename_async(const char *from, const char *to, char *msg, uint32_t msg_len, sd_storage_callback_t callback, void *user_data);
bool sd_storage_service_touch_file_async(const char *path, char *msg, uint32_t msg_len, sd_storage_callback_t callback, void *user_data);
