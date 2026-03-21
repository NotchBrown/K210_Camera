#include "sd_storage_service.h"

#include <stdio.h>
#include <string.h>

#include "kendryte-standalone-sdk/lib/freertos/include/FreeRTOS.h"
#include "kendryte-standalone-sdk/lib/freertos/include/queue.h"
#include "kendryte-standalone-sdk/lib/freertos/include/semphr.h"
#include "kendryte-standalone-sdk/lib/freertos/include/task.h"

#include "app_log.h"
#include "sd_fs.h"
#include "sd_hw.h"

namespace {

#ifndef SD_STORAGE_QUEUE_LEN
#define SD_STORAGE_QUEUE_LEN 8
#endif

#ifndef SD_STORAGE_DEFAULT_RETRIES
#define SD_STORAGE_DEFAULT_RETRIES 2
#endif

#ifndef SD_STORAGE_WAIT_TIMEOUT_MS
#define SD_STORAGE_WAIT_TIMEOUT_MS 1200
#endif

typedef struct {
    sd_storage_op_t op;
    TaskHandle_t requester;
    char path1[128];
    char path2[128];
    char *out;
    uint32_t out_len;
    bool *result_ptr;
    uint8_t retries;
    uint8_t attempt;
} sd_storage_req_t;

static QueueHandle_t s_queue = NULL;
static SemaphoreHandle_t s_status_lock = NULL;
static TaskHandle_t s_task = NULL;
static volatile bool s_deferred_check_req = false;
static volatile bool s_deferred_format_req = false;
static sd_storage_status_t s_status = {
    SD_STORAGE_STATE_NO_CARD, false, false, false, false, false, 0, 0, "Storage waiting card"
};

static void set_text(char *out, uint32_t out_len, const char *text) {
    if (!out || out_len == 0) {
        return;
    }
    snprintf(out, out_len, "%s", text ? text : "");
}

static void status_lock(void) {
    if (s_status_lock) {
        (void)xSemaphoreTake(s_status_lock, portMAX_DELAY);
    }
}

static void status_unlock(void) {
    if (s_status_lock) {
        (void)xSemaphoreGive(s_status_lock);
    }
}

static void status_set_state(sd_storage_state_t state, const char *msg) {
    status_lock();
    sd_storage_state_t old = s_status.state;
    s_status.state = state;
    s_status.busy = (state == SD_STORAGE_STATE_INIT_OK_BUSY);
    if (msg) {
        snprintf(s_status.message, sizeof(s_status.message), "%s", msg);
    }
    status_unlock();

    if (old != state) {
        APP_LOGI("SDSvc: state %d -> %d (%s)", (int)old, (int)state, msg ? msg : "");
    }
}

static void status_sync_card_flags(bool card_present, bool initialized) {
    status_lock();
    s_status.card_present = card_present;
    s_status.initialized = initialized;
    if (!initialized) {
        s_status.available = false;
    }
    status_unlock();
}

static void status_update_check_result(bool ok, uint32_t total_kb, uint32_t free_kb, const char *msg) {
    status_lock();
    s_status.checked = true;
    s_status.available = ok;
    s_status.total_kb = total_kb;
    s_status.free_kb = free_kb;
    snprintf(s_status.message, sizeof(s_status.message), "%s", msg ? msg : (ok ? "Storage ready" : "Storage unavailable"));
    status_unlock();
}

static bool do_op(sd_storage_req_t *req) {
    if (!req) {
        return false;
    }

    uint32_t total_kb = 0;
    uint32_t free_kb = 0;
    char info[96] = "";

    switch (req->op) {
        case SD_STORAGE_OP_CHECK: {
            bool ok = sd_fs_check(&total_kb, &free_kb, info, sizeof(info));
            status_update_check_result(ok, total_kb, free_kb, info);
            set_text(req->out, req->out_len, info);
            return ok;
        }
        case SD_STORAGE_OP_FORMAT: {
            bool ok = sd_fs_format(&total_kb, &free_kb, info, sizeof(info));
            status_update_check_result(ok, total_kb, free_kb, info);
            set_text(req->out, req->out_len, info);
            return ok;
        }
        case SD_STORAGE_OP_LIST_ROOT:
            return sd_fs_list_root(req->out, req->out_len);
        case SD_STORAGE_OP_LIST_DIR:
            return sd_fs_list_dir(req->path1, req->out, req->out_len);
        case SD_STORAGE_OP_MKDIR:
            return sd_fs_mkdir(req->path1, req->out, req->out_len);
        case SD_STORAGE_OP_DELETE:
            return sd_fs_delete(req->path1, req->out, req->out_len);
        case SD_STORAGE_OP_COPY:
            return sd_fs_copy(req->path1, req->path2, req->out, req->out_len);
        case SD_STORAGE_OP_RENAME:
            return sd_fs_rename(req->path1, req->path2, req->out, req->out_len);
        case SD_STORAGE_OP_TOUCH_FILE:
            return sd_fs_touch_file(req->path1, req->out, req->out_len);
        case SD_STORAGE_OP_NONE:
        default:
            set_text(req->out, req->out_len, "Unsupported op");
            return false;
    }
}

static bool enqueue_request(const sd_storage_req_t *req, uint32_t timeout_ms) {
    if (!s_queue || !req) {
        return false;
    }
    return xQueueSend(s_queue, req, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static bool request_sync(sd_storage_op_t op,
                         const char *path1,
                         const char *path2,
                         char *out,
                         uint32_t out_len,
                         uint8_t retries) {
    if (!s_queue || !s_task) {
        set_text(out, out_len, "Storage service not ready");
        return false;
    }

    bool result = false;
    sd_storage_req_t req = { SD_STORAGE_OP_NONE, NULL, "", "", NULL, 0, NULL, 0, 0 };
    req.op = op;
    req.requester = xTaskGetCurrentTaskHandle();
    req.out = out;
    req.out_len = out_len;
    req.result_ptr = &result;
    req.retries = retries;
    snprintf(req.path1, sizeof(req.path1), "%s", path1 ? path1 : "");
    snprintf(req.path2, sizeof(req.path2), "%s", path2 ? path2 : "");

    if (!enqueue_request(&req, 500U)) {
        set_text(out, out_len, "Storage queue busy");
        return false;
    }

    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(SD_STORAGE_WAIT_TIMEOUT_MS)) == 0) {
        set_text(out, out_len, "Storage op timeout");
        return false;
    }

    return result;
}

static void request_async(sd_storage_op_t op, const char *msg) {
    if (!s_queue) {
        if (op == SD_STORAGE_OP_CHECK) {
            s_deferred_check_req = true;
        } else if (op == SD_STORAGE_OP_FORMAT) {
            s_deferred_format_req = true;
        }
        APP_LOGW("SDSvc: queue not ready, deferred op=%d", (int)op);
        return;
    }

    if (op == SD_STORAGE_OP_CHECK) {
        status_lock();
        s_status.checked = false;
        s_status.available = false;
        s_status.total_kb = 0;
        s_status.free_kb = 0;
        snprintf(s_status.message, sizeof(s_status.message), "%s", msg ? msg : "Checking storage...");
        status_unlock();
    }

    sd_storage_req_t req = { SD_STORAGE_OP_NONE, NULL, "", "", NULL, 0, NULL, 0, 0 };
    req.op = op;
    req.retries = SD_STORAGE_DEFAULT_RETRIES;
    if (!enqueue_request(&req, 0)) {
        APP_LOGW("SDSvc: async request dropped op=%d", (int)op);
        return;
    }
    (void)msg;
}

}  // namespace

void sd_storage_service_task(void *arg) {
    (void)arg;

    s_queue = xQueueCreate(SD_STORAGE_QUEUE_LEN, sizeof(sd_storage_req_t));
    s_status_lock = xSemaphoreCreateMutex();
    s_task = xTaskGetCurrentTaskHandle();

    if (!s_queue || !s_status_lock) {
        APP_LOGE("SDSvc: init failed q=%p lock=%p", s_queue, s_status_lock);
        vTaskDelete(NULL);
        return;
    }

    APP_LOGI("SDSvc: task started");

    if (s_deferred_check_req) {
        s_deferred_check_req = false;
        request_async(SD_STORAGE_OP_CHECK, "Checking storage...");
    }
    if (s_deferred_format_req) {
        s_deferred_format_req = false;
        request_async(SD_STORAGE_OP_FORMAT, "Formatting storage...");
    }

    sd_storage_req_t active = { SD_STORAGE_OP_NONE, NULL, "", "", NULL, 0, NULL, 0, 0 };
    bool has_active = false;

    for (;;) {
        bool card_present = sd_hw_card_present();
        sd_storage_state_t state;
        status_lock();
        state = s_status.state;
        status_unlock();

        status_sync_card_flags(card_present, sd_hw_is_mounted());

        switch (state) {
            case SD_STORAGE_STATE_NO_CARD:
                if (card_present) {
                    status_set_state(SD_STORAGE_STATE_CARD_NOT_INIT, "Card detected, init...");
                }
                break;

            case SD_STORAGE_STATE_CARD_NOT_INIT: {
                char msg[96];
                bool ok = sd_hw_mount(msg, sizeof(msg));
                if (ok) {
                    status_set_state(SD_STORAGE_STATE_INIT_OK_IDLE, "Storage initialized");
                    status_update_check_result(true, sd_hw_total_kb(), 0, "Storage initialized");
                } else {
                    status_set_state(SD_STORAGE_STATE_INIT_FAILED, msg);
                }
                break;
            }

            case SD_STORAGE_STATE_INIT_FAILED:
                sd_hw_unmount();
                status_set_state(SD_STORAGE_STATE_NO_CARD, "Init failed, waiting card");
                break;

            case SD_STORAGE_STATE_INIT_OK_IDLE:
                if (!card_present) {
                    sd_hw_unmount();
                    status_set_state(SD_STORAGE_STATE_NO_CARD, "Card removed");
                    break;
                }
                if (!has_active) {
                    if (xQueueReceive(s_queue, &active, pdMS_TO_TICKS(100)) == pdTRUE) {
                        has_active = true;
                        APP_LOGI("SDSvc: dequeued op=%d retry=%u", (int)active.op, (unsigned)active.retries);
                        status_set_state(SD_STORAGE_STATE_INIT_OK_BUSY, "Processing storage op...");
                    }
                }
                break;

            case SD_STORAGE_STATE_INIT_OK_BUSY: {
                if (!has_active) {
                    status_set_state(SD_STORAGE_STATE_INIT_OK_IDLE, "Storage idle");
                    break;
                }

                if (!card_present) {
                    set_text(active.out, active.out_len, "Card removed");
                    if (active.result_ptr) {
                        *(active.result_ptr) = false;
                    }
                    status_set_state(SD_STORAGE_STATE_INIT_OK_TASK_FAILED, "Card removed");
                    break;
                }

                bool ok = do_op(&active);
                if (active.result_ptr) {
                    *(active.result_ptr) = ok;
                }
                APP_LOGI("SDSvc: op done=%d op=%d attempt=%u", ok ? 1 : 0, (int)active.op, (unsigned)active.attempt);
                status_set_state(ok ? SD_STORAGE_STATE_INIT_OK_TASK_SUCCESS : SD_STORAGE_STATE_INIT_OK_TASK_FAILED,
                                 ok ? "Storage op success" : "Storage op failed");
                break;
            }

            case SD_STORAGE_STATE_INIT_OK_TASK_FAILED:
                if (has_active && active.attempt < active.retries) {
                    active.attempt++;
                    APP_LOGW("SDSvc: retry op=%d attempt=%u/%u",
                             (int)active.op,
                             (unsigned)active.attempt,
                             (unsigned)active.retries);
                    status_set_state(SD_STORAGE_STATE_INIT_OK_BUSY, "Retry storage op...");
                } else {
                    if (has_active && active.requester) {
                        xTaskNotifyGive(active.requester);
                    }
                    has_active = false;
                    status_set_state(SD_STORAGE_STATE_INIT_OK_IDLE, "Storage idle");
                }
                break;

            case SD_STORAGE_STATE_INIT_OK_TASK_SUCCESS:
                if (has_active && active.requester) {
                    xTaskNotifyGive(active.requester);
                }
                has_active = false;
                status_set_state(SD_STORAGE_STATE_INIT_OK_IDLE, "Storage idle");
                break;

            default:
                status_set_state(SD_STORAGE_STATE_NO_CARD, "Storage state reset");
                break;
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void sd_storage_service_get_status(sd_storage_status_t *status) {
    if (!status) {
        return;
    }

    status_lock();
    *status = s_status;
    status_unlock();
}

void sd_storage_service_request_check(void) {
    request_async(SD_STORAGE_OP_CHECK, "Checking storage...");
}

void sd_storage_service_request_format(void) {
    request_async(SD_STORAGE_OP_FORMAT, "Formatting storage...");
}

bool sd_storage_service_list_root(char *out, uint32_t out_len) {
    return request_sync(SD_STORAGE_OP_LIST_ROOT, NULL, NULL, out, out_len, 0);
}

bool sd_storage_service_list_dir(const char *path, char *out, uint32_t out_len) {
    return request_sync(SD_STORAGE_OP_LIST_DIR, path, NULL, out, out_len, 0);
}

bool sd_storage_service_mkdir(const char *path, char *msg, uint32_t msg_len) {
    return request_sync(SD_STORAGE_OP_MKDIR, path, NULL, msg, msg_len, SD_STORAGE_DEFAULT_RETRIES);
}

bool sd_storage_service_delete(const char *path, char *msg, uint32_t msg_len) {
    return request_sync(SD_STORAGE_OP_DELETE, path, NULL, msg, msg_len, SD_STORAGE_DEFAULT_RETRIES);
}

bool sd_storage_service_copy(const char *from, const char *to, char *msg, uint32_t msg_len) {
    return request_sync(SD_STORAGE_OP_COPY, from, to, msg, msg_len, SD_STORAGE_DEFAULT_RETRIES);
}

bool sd_storage_service_rename(const char *from, const char *to, char *msg, uint32_t msg_len) {
    return request_sync(SD_STORAGE_OP_RENAME, from, to, msg, msg_len, SD_STORAGE_DEFAULT_RETRIES);
}

bool sd_storage_service_touch_file(const char *path, char *msg, uint32_t msg_len) {
    return request_sync(SD_STORAGE_OP_TOUCH_FILE, path, NULL, msg, msg_len, SD_STORAGE_DEFAULT_RETRIES);
}

static bool invoke_async_result(bool ok, const char *result, sd_storage_callback_t callback, void *user_data) {
    if (callback) {
        callback(ok, result, user_data);
    }
    return ok;
}

bool sd_storage_service_list_root_async(char *out, uint32_t out_len, sd_storage_callback_t callback, void *user_data) {
    bool ok = sd_storage_service_list_root(out, out_len);
    return invoke_async_result(ok, out, callback, user_data);
}

bool sd_storage_service_list_dir_async(const char *path, char *out, uint32_t out_len, sd_storage_callback_t callback, void *user_data) {
    bool ok = sd_storage_service_list_dir(path, out, out_len);
    return invoke_async_result(ok, out, callback, user_data);
}

bool sd_storage_service_mkdir_async(const char *path, char *msg, uint32_t msg_len, sd_storage_callback_t callback, void *user_data) {
    bool ok = sd_storage_service_mkdir(path, msg, msg_len);
    return invoke_async_result(ok, msg, callback, user_data);
}

bool sd_storage_service_delete_async(const char *path, char *msg, uint32_t msg_len, sd_storage_callback_t callback, void *user_data) {
    bool ok = sd_storage_service_delete(path, msg, msg_len);
    return invoke_async_result(ok, msg, callback, user_data);
}

bool sd_storage_service_copy_async(const char *from, const char *to, char *msg, uint32_t msg_len, sd_storage_callback_t callback, void *user_data) {
    bool ok = sd_storage_service_copy(from, to, msg, msg_len);
    return invoke_async_result(ok, msg, callback, user_data);
}

bool sd_storage_service_rename_async(const char *from, const char *to, char *msg, uint32_t msg_len, sd_storage_callback_t callback, void *user_data) {
    bool ok = sd_storage_service_rename(from, to, msg, msg_len);
    return invoke_async_result(ok, msg, callback, user_data);
}

bool sd_storage_service_touch_file_async(const char *path, char *msg, uint32_t msg_len, sd_storage_callback_t callback, void *user_data) {
    bool ok = sd_storage_service_touch_file(path, msg, msg_len);
    return invoke_async_result(ok, msg, callback, user_data);
}
