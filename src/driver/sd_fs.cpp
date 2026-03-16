#include "sd_fs.h"

#include "app_log.h"
#include "sd_hw.h"

#include <Arduino.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>

namespace {

#ifndef APP_SD_FS_LOG_ENABLE
#define APP_SD_FS_LOG_ENABLE 1
#endif

#if APP_SD_FS_LOG_ENABLE
#define SD_FS_LOGI(fmt, ...) APP_LOGI("SDFS: " fmt, ##__VA_ARGS__)
#define SD_FS_LOGE(fmt, ...) APP_LOGE("SDFS: " fmt, ##__VA_ARGS__)
#else
#define SD_FS_LOGI(...) do {} while (0)
#define SD_FS_LOGE(...) do {} while (0)
#endif

static SDClass &fs() {
    return sd_hw_fs();
}

static void set_msg(char *msg, uint32_t msg_len, const char *text) {
    if (!msg || msg_len == 0) {
        return;
    }
    snprintf(msg, msg_len, "%s", text ? text : "");
}

static bool normalize_path(const char *in, char *out, size_t out_len) {
    if (!in || !out || out_len < 2) {
        return false;
    }

    while (*in == ' ') {
        in++;
    }

    if (*in == '\0') {
        out[0] = '/';
        out[1] = '\0';
        return true;
    }

    if (*in != '/') {
        if (out_len < 3) {
            return false;
        }
        out[0] = '/';
        out[1] = '\0';
        strncat(out, in, out_len - strlen(out) - 1);
    } else {
        snprintf(out, out_len, "%s", in);
    }

    size_t n = strlen(out);
    while (n > 1 && out[n - 1] == '/') {
        out[n - 1] = '\0';
        n--;
    }

    return true;
}

static bool join_path(const char *base, const char *name, char *out, size_t out_len) {
    if (!base || !name || !out || out_len == 0) {
        return false;
    }

    if (strcmp(base, "/") == 0) {
        return snprintf(out, out_len, "/%s", name) > 0 && strlen(out) < out_len;
    }

    return snprintf(out, out_len, "%s/%s", base, name) > 0 && strlen(out) < out_len;
}

static bool mount_if_needed() {
    SD_FS_LOGI("mount_if_needed enter");
    bool ok = sd_hw_mount();
    SD_FS_LOGI("mount det=%d mounted=%d", sd_hw_card_present() ? 1 : 0, ok ? 1 : 0);
    SD_FS_LOGI("mount_if_needed leave ok=%d", ok ? 1 : 0);
    return ok;
}

static bool remove_tree(const char *path) {
    File node = fs().open(path);
    if (!node) {
        return false;
    }

    bool is_dir = node.isDirectory();
    if (!is_dir) {
        node.close();
        return fs().remove(path);
    }

    File child = node.openNextFile();
    while (child) {
        char child_path[128];
        const char *name = child.name();
        bool child_is_dir = child.isDirectory();
        child.close();

        if (!join_path(path, name, child_path, sizeof(child_path))) {
            node.close();
            return false;
        }

        bool ok = child_is_dir ? remove_tree(child_path) : fs().remove(child_path);
        if (!ok) {
            node.close();
            return false;
        }

        child = node.openNextFile();
    }

    node.close();
    if (strcmp(path, "/") == 0) {
        return true;
    }

    return fs().rmdir(path);
}

static bool copy_file_path(const char *src, const char *dst) {
    File in = fs().open(src, FILE_READ);
    if (!in) {
        return false;
    }

    File out = fs().open(dst, FILE_WRITE);
    if (!out) {
        in.close();
        return false;
    }

    uint8_t buf[256];
    while (true) {
        long n = in.read(buf, (uint32_t)sizeof(buf));
        if (n < 0) {
            in.close();
            out.close();
            return false;
        }
        if (n == 0) {
            break;
        }
        if ((int)out.write(buf, (size_t)n) != n) {
            in.close();
            out.close();
            return false;
        }
    }

    out.flush();
    in.close();
    out.close();
    return true;
}

static bool copy_tree(const char *src, const char *dst) {
    File node = fs().open(src);
    if (!node) {
        return false;
    }

    bool is_dir = node.isDirectory();
    if (!is_dir) {
        node.close();
        return copy_file_path(src, dst);
    }

    if (!fs().mkdir(dst)) {
        node.close();
        return false;
    }

    File child = node.openNextFile();
    while (child) {
        char src_child[128];
        char dst_child[128];
        const char *name = child.name();
        child.close();

        if (!join_path(src, name, src_child, sizeof(src_child))) {
            node.close();
            return false;
        }
        if (!join_path(dst, name, dst_child, sizeof(dst_child))) {
            node.close();
            return false;
        }

        if (!copy_tree(src_child, dst_child)) {
            node.close();
            return false;
        }
        child = node.openNextFile();
    }

    node.close();
    return true;
}

static bool list_path(const char *path, char *out, uint32_t out_len) {
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';

    char norm[128];
    if (!normalize_path(path ? path : "/", norm, sizeof(norm))) {
        snprintf(out, out_len, "Invalid path");
        return false;
    }

    File dir = fs().open(norm);
    if (!dir || !dir.isDirectory()) {
        if (dir) {
            dir.close();
        }
        snprintf(out, out_len, "Open dir failed: %s", norm);
        return false;
    }

    uint32_t count = 0;
    File child = dir.openNextFile();
    while (child) {
        const char *name = child.name();
        bool is_dir = child.isDirectory();
        char line[96];
        snprintf(line, sizeof(line), "%s /%s\n", is_dir ? "DIR " : "FILE", name);

        if (strlen(out) + strlen(line) + 1 >= out_len) {
            child.close();
            break;
        }

        strcat(out, line);
        count++;
        child.close();
        child = dir.openNextFile();
    }
    dir.close();

    if (count == 0) {
        snprintf(out, out_len, "(empty)");
    }

    return true;
}

} // namespace

bool sd_fs_check(uint32_t *total_kb, uint32_t *free_kb, char *msg, uint32_t msg_len) {
    SD_FS_LOGI("check enter");
    if (!mount_if_needed()) {
        set_msg(msg, msg_len, sd_hw_last_error());
        SD_FS_LOGE("check mount failed: %s", sd_hw_last_error());
        return false;
    }

    File root = fs().open("/");
    if (!root || !root.isDirectory()) {
        if (root) {
            root.close();
        }
        set_msg(msg, msg_len, "SD root open failed");
        SD_FS_LOGE("check failed: root open");
        return false;
    }

    uint32_t entries = 0;
    File child = root.openNextFile();
    while (child) {
        entries++;
        child.close();
        child = root.openNextFile();
    }
    root.close();

    if (total_kb) {
        *total_kb = 0;
    }
    if (free_kb) {
        *free_kb = 0;
    }

    set_msg(msg, msg_len, "SD root OK");
    SD_FS_LOGI("check ok entries=%lu", (unsigned long)entries);
    return true;
}

bool sd_fs_format(uint32_t *total_kb, uint32_t *free_kb, char *msg, uint32_t msg_len) {
    SD_FS_LOGI("format enter");
    if (!mount_if_needed()) {
        set_msg(msg, msg_len, sd_hw_last_error());
        SD_FS_LOGE("format mount failed: %s", sd_hw_last_error());
        return false;
    }

    if (!remove_tree("/")) {
        set_msg(msg, msg_len, "SD wipe failed");
        return false;
    }

    set_msg(msg, msg_len, "Wipe done");
    if (total_kb) {
        *total_kb = 0;
    }
    if (free_kb) {
        *free_kb = 0;
    }
    return true;
}

bool sd_fs_list_root(char *out, uint32_t out_len) {
    if (!mount_if_needed()) {
        if (out && out_len > 0) {
            snprintf(out, out_len, "%s", sd_hw_last_error());
        }
        return false;
    }

    return list_path("/", out, out_len);
}

bool sd_fs_list_dir(const char *path, char *out, uint32_t out_len) {
    if (!mount_if_needed()) {
        if (out && out_len > 0) {
            snprintf(out, out_len, "%s", sd_hw_last_error());
        }
        return false;
    }

    return list_path(path, out, out_len);
}

bool sd_fs_mkdir(const char *path, char *msg, uint32_t msg_len) {
    char norm[128];
    if (!normalize_path(path, norm, sizeof(norm))) {
        set_msg(msg, msg_len, "Invalid path");
        return false;
    }

    if (!mount_if_needed()) {
        set_msg(msg, msg_len, sd_hw_last_error());
        return false;
    }

    if (strcmp(norm, "/") == 0) {
        set_msg(msg, msg_len, "Cannot create root");
        return false;
    }

    if (fs().exists(norm)) {
        set_msg(msg, msg_len, "Path already exists");
        return false;
    }

    if (!fs().mkdir(norm)) {
        set_msg(msg, msg_len, "mkdir failed");
        return false;
    }

    set_msg(msg, msg_len, "mkdir ok");
    return true;
}

bool sd_fs_delete(const char *path, char *msg, uint32_t msg_len) {
    char norm[128];
    if (!normalize_path(path, norm, sizeof(norm))) {
        set_msg(msg, msg_len, "Invalid path");
        return false;
    }

    if (!mount_if_needed()) {
        set_msg(msg, msg_len, sd_hw_last_error());
        return false;
    }

    if (strcmp(norm, "/") == 0) {
        set_msg(msg, msg_len, "Cannot delete root");
        return false;
    }

    File node = fs().open(norm);
    if (!node) {
        set_msg(msg, msg_len, "Path not found");
        return false;
    }
    bool is_dir = node.isDirectory();
    node.close();

    bool ok = false;
    if (is_dir) {
        ok = remove_tree(norm);
    } else {
        ok = fs().remove(norm);
    }

    if (!ok) {
        set_msg(msg, msg_len, "Delete failed");
        return false;
    }

    set_msg(msg, msg_len, "Delete ok");
    return true;
}

bool sd_fs_copy(const char *from, const char *to, char *msg, uint32_t msg_len) {
    char src[128];
    char dst[128];

    if (!normalize_path(from, src, sizeof(src)) || !normalize_path(to, dst, sizeof(dst))) {
        set_msg(msg, msg_len, "Invalid path");
        return false;
    }

    if (!mount_if_needed()) {
        set_msg(msg, msg_len, sd_hw_last_error());
        return false;
    }

    if (strcmp(src, "/") == 0 || strcmp(dst, "/") == 0) {
        set_msg(msg, msg_len, "Root copy not allowed");
        return false;
    }

    if (!fs().exists(src)) {
        set_msg(msg, msg_len, "Source not found");
        return false;
    }

    if (fs().exists(dst)) {
        set_msg(msg, msg_len, "Destination exists");
        return false;
    }

    if (!copy_tree(src, dst)) {
        set_msg(msg, msg_len, "Copy failed");
        return false;
    }

    set_msg(msg, msg_len, "Copy ok");
    return true;
}

bool sd_fs_rename(const char *from, const char *to, char *msg, uint32_t msg_len) {
    char src[128];
    char dst[128];

    if (!normalize_path(from, src, sizeof(src)) || !normalize_path(to, dst, sizeof(dst))) {
        set_msg(msg, msg_len, "Invalid path");
        return false;
    }

    if (!mount_if_needed()) {
        set_msg(msg, msg_len, sd_hw_last_error());
        return false;
    }

    if (strcmp(src, "/") == 0 || strcmp(dst, "/") == 0) {
        set_msg(msg, msg_len, "Root rename not allowed");
        return false;
    }

    if (!fs().exists(src)) {
        set_msg(msg, msg_len, "Source not found");
        return false;
    }

    if (fs().exists(dst)) {
        set_msg(msg, msg_len, "Destination exists");
        return false;
    }

    if (!copy_tree(src, dst)) {
        set_msg(msg, msg_len, "Move(copy) failed");
        return false;
    }

    File src_node = fs().open(src);
    if (!src_node) {
        set_msg(msg, msg_len, "Move source reopen failed");
        return false;
    }
    bool is_dir = src_node.isDirectory();
    src_node.close();

    bool del_ok = is_dir ? remove_tree(src) : fs().remove(src);
    if (!del_ok) {
        set_msg(msg, msg_len, "Move cleanup failed");
        return false;
    }

    set_msg(msg, msg_len, "Move ok");
    return true;
}

bool sd_fs_touch_file(const char *path, char *msg, uint32_t msg_len) {
    char norm[128];
    if (!normalize_path(path, norm, sizeof(norm))) {
        set_msg(msg, msg_len, "Invalid path");
        return false;
    }

    if (!mount_if_needed()) {
        set_msg(msg, msg_len, sd_hw_last_error());
        return false;
    }

    if (strcmp(norm, "/") == 0) {
        set_msg(msg, msg_len, "Invalid file path");
        return false;
    }

    if (fs().exists(norm)) {
        set_msg(msg, msg_len, "Path already exists");
        return false;
    }

    File f = fs().open(norm, FILE_WRITE);
    if (!f) {
        set_msg(msg, msg_len, "Create file failed");
        return false;
    }
    f.close();

    set_msg(msg, msg_len, "Create file ok");
    return true;
}
