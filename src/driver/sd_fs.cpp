#include "sd_fs.h"

#include "sd_hw.h"

#include <SdFat.h>

#include <stdio.h>
#include <string.h>

namespace {

void set_text(char *out, uint32_t out_len, const char *text) {
    if (!out || out_len == 0) {
        return;
    }
    snprintf(out, out_len, "%s", text ? text : "");
}

bool ensure_mounted(char *msg, uint32_t msg_len) {
    if (sd_hw_is_mounted()) {
        return true;
    }
    return sd_hw_mount(msg, msg_len);
}

bool normalize_path(const char *in, char *out, size_t out_len) {
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';

    if (!in || in[0] == '\0') {
        snprintf(out, out_len, "%s", "/");
        return true;
    }

    const char *p = in;
    while (*p == ' ') {
        p++;
    }

    if (*p == '\0') {
        snprintf(out, out_len, "%s", "/");
        return true;
    }

    if (strncmp(p, "/SD/", 4) == 0) {
        p += 3;
    } else if (strcmp(p, "/SD") == 0) {
        p = "/";
    }

    if (*p != '/') {
        snprintf(out, out_len, "/%s", p);
    } else {
        snprintf(out, out_len, "%s", p);
    }

    size_t n = strlen(out);
    while (n > 1 && out[n - 1] == '/') {
        out[n - 1] = '\0';
        n--;
    }
    return true;
}

bool path_join(const char *base, const char *name, char *out, size_t out_len) {
    if (!base || !name || !out || out_len == 0) {
        return false;
    }
    if (strcmp(base, "/") == 0) {
        snprintf(out, out_len, "/%s", name);
    } else {
        snprintf(out, out_len, "%s/%s", base, name);
    }
    return true;
}

bool append_line(char *out, uint32_t out_len, const char *line) {
    if (!out || out_len == 0 || !line) {
        return false;
    }
    size_t used = strlen(out);
    size_t add = strlen(line);
    if (used + add + 2 > out_len) {
        return false;
    }
    memcpy(out + used, line, add);
    out[used + add] = '\n';
    out[used + add + 1] = '\0';
    return true;
}

bool is_dir_path(const char *path, bool *is_dir) {
    if (is_dir) {
        *is_dir = false;
    }
    SdFile f;
    if (!f.open(path, O_RDONLY)) {
        return false;
    }
    bool d = f.isDir();
    f.close();
    if (is_dir) {
        *is_dir = d;
    }
    return true;
}

bool copy_file_data(const char *from, const char *to) {
    SdFile src;
    if (!src.open(from, O_RDONLY)) {
        return false;
    }

    SdFile dst;
    if (!dst.open(to, O_WRONLY | O_CREAT | O_TRUNC)) {
        src.close();
        return false;
    }

    uint8_t buf[1024];
    bool ok = true;
    while (true) {
        int n = src.read(buf, sizeof(buf));
        if (n < 0) {
            ok = false;
            break;
        }
        if (n == 0) {
            break;
        }
        if (dst.write(buf, (size_t)n) != (size_t)n) {
            ok = false;
            break;
        }
    }

    dst.close();
    src.close();
    return ok;
}

bool copy_tree(const char *from, const char *to) {
    bool is_dir = false;
    if (!is_dir_path(from, &is_dir)) {
        return false;
    }

    if (!is_dir) {
        return copy_file_data(from, to);
    }

    SdFat &fs = sd_hw_fs();
    if (!fs.exists(to) && !fs.mkdir(to, true)) {
        return false;
    }

    SdFile dir;
    if (!dir.open(from, O_RDONLY) || !dir.isDir()) {
        dir.close();
        return false;
    }

    SdFile child;
    char name[96];
    bool ok = true;
    while (child.openNext(&dir, O_RDONLY)) {
        name[0] = '\0';
        child.getName(name, sizeof(name));

        if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            child.close();
            continue;
        }

        char src_child[192];
        char dst_child[192];
        path_join(from, name, src_child, sizeof(src_child));
        path_join(to, name, dst_child, sizeof(dst_child));

        child.close();
        if (!copy_tree(src_child, dst_child)) {
            ok = false;
            break;
        }
    }

    dir.close();
    return ok;
}

bool remove_tree(const char *path) {
    bool is_dir = false;
    if (!is_dir_path(path, &is_dir)) {
        return false;
    }

    SdFat &fs = sd_hw_fs();
    if (!is_dir) {
        return fs.remove(path);
    }

    SdFile dir;
    if (!dir.open(path, O_RDONLY) || !dir.isDir()) {
        dir.close();
        return false;
    }

    SdFile child;
    char name[96];
    bool ok = true;
    while (child.openNext(&dir, O_RDONLY)) {
        name[0] = '\0';
        child.getName(name, sizeof(name));
        if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            child.close();
            continue;
        }

        char full[192];
        path_join(path, name, full, sizeof(full));
        child.close();
        if (!remove_tree(full)) {
            ok = false;
            break;
        }
    }
    dir.close();

    if (!ok) {
        return false;
    }
    return fs.rmdir(path);
}

}  // namespace

bool sd_fs_check(uint32_t *total_kb, uint32_t *free_kb, char *msg, uint32_t msg_len) {
    if (total_kb) {
        *total_kb = 0;
    }
    if (free_kb) {
        *free_kb = 0;
    }

    if (!ensure_mounted(msg, msg_len)) {
        return false;
    }

    SdFat &fs = sd_hw_fs();
    uint32_t total = sd_hw_total_kb();
    uint32_t free_space = 0;

    uint32_t fcc = fs.freeClusterCount();
    uint32_t bpc = fs.bytesPerCluster();
    if (fcc > 0 && bpc > 0) {
        free_space = (uint32_t)(((uint64_t)fcc * (uint64_t)bpc) / 1024ULL);
    }

    if (total_kb) {
        *total_kb = total;
    }
    if (free_kb) {
        *free_kb = free_space;
    }

    uint32_t used = (total >= free_space) ? (total - free_space) : 0;
    if (msg && msg_len > 0) {
        snprintf(msg, msg_len, "SD OK T:%luKB U:%luKB F:%luKB",
                 (unsigned long)total, (unsigned long)used, (unsigned long)free_space);
    }
    return true;
}

bool sd_fs_format(uint32_t *total_kb, uint32_t *free_kb, char *msg, uint32_t msg_len) {
    if (!ensure_mounted(msg, msg_len)) {
        return false;
    }

    SdFat &fs = sd_hw_fs();
    if (!fs.format()) {
        set_text(msg, msg_len, "Format failed");
        return false;
    }

    sd_hw_unmount();
    if (!sd_hw_mount(msg, msg_len)) {
        return false;
    }
    return sd_fs_check(total_kb, free_kb, msg, msg_len);
}

bool sd_fs_list_root(char *out, uint32_t out_len) {
    return sd_fs_list_dir("/", out, out_len);
}

bool sd_fs_list_dir(const char *path, char *out, uint32_t out_len) {
    if (!out || out_len == 0) {
        return false;
    }
    out[0] = '\0';

    char mount_msg[96];
    if (!ensure_mounted(mount_msg, sizeof(mount_msg))) {
        set_text(out, out_len, mount_msg);
        return false;
    }

    char norm[128];
    normalize_path(path, norm, sizeof(norm));

    SdFile dir;
    if (!dir.open(norm, O_RDONLY) || !dir.isDir()) {
        dir.close();
        set_text(out, out_len, "Open dir failed");
        return false;
    }

    SdFile entry;
    char name[96];
    bool ok = true;
    while (entry.openNext(&dir, O_RDONLY)) {
        name[0] = '\0';
        entry.getName(name, sizeof(name));
        if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            entry.close();
            continue;
        }

        char line[128];
        if (entry.isDir()) {
            snprintf(line, sizeof(line), "DIR /%s", name);
        } else {
            snprintf(line, sizeof(line), "FILE /%s", name);
        }
        entry.close();

        if (!append_line(out, out_len, line)) {
            ok = false;
            break;
        }
    }
    dir.close();

    if (!ok) {
        set_text(out, out_len, "List buffer too small");
    }
    return ok;
}

bool sd_fs_mkdir(const char *path, char *msg, uint32_t msg_len) {
    char norm[128];
    normalize_path(path, norm, sizeof(norm));

    if (strcmp(norm, "/") == 0) {
        set_text(msg, msg_len, "Root already exists");
        return true;
    }

    if (!ensure_mounted(msg, msg_len)) {
        return false;
    }

    SdFat &fs = sd_hw_fs();
    if (fs.exists(norm)) {
        set_text(msg, msg_len, "Path already exists");
        return true;
    }

    bool ok = fs.mkdir(norm, true);
    set_text(msg, msg_len, ok ? "mkdir ok" : "mkdir failed");
    return ok;
}

bool sd_fs_delete(const char *path, char *msg, uint32_t msg_len) {
    char norm[128];
    normalize_path(path, norm, sizeof(norm));

    if (strcmp(norm, "/") == 0) {
        set_text(msg, msg_len, "Delete root is forbidden");
        return false;
    }

    if (!ensure_mounted(msg, msg_len)) {
        return false;
    }

    SdFat &fs = sd_hw_fs();
    if (!fs.exists(norm)) {
        set_text(msg, msg_len, "Path not found");
        return false;
    }

    bool ok = remove_tree(norm);
    set_text(msg, msg_len, ok ? "delete ok" : "delete failed");
    return ok;
}

bool sd_fs_copy(const char *from, const char *to, char *msg, uint32_t msg_len) {
    char src[128];
    char dst[128];
    normalize_path(from, src, sizeof(src));
    normalize_path(to, dst, sizeof(dst));

    if (strcmp(src, "/") == 0) {
        set_text(msg, msg_len, "Copy from root is forbidden");
        return false;
    }

    if (strcmp(dst, "/") == 0) {
        set_text(msg, msg_len, "Invalid destination");
        return false;
    }

    if (!ensure_mounted(msg, msg_len)) {
        return false;
    }

    SdFat &fs = sd_hw_fs();
    if (!fs.exists(src)) {
        set_text(msg, msg_len, "Source not found");
        return false;
    }
    if (fs.exists(dst)) {
        set_text(msg, msg_len, "Destination already exists");
        return false;
    }

    bool ok = copy_tree(src, dst);
    set_text(msg, msg_len, ok ? "copy ok" : "copy failed");
    return ok;
}

bool sd_fs_rename(const char *from, const char *to, char *msg, uint32_t msg_len) {
    char src[128];
    char dst[128];
    normalize_path(from, src, sizeof(src));
    normalize_path(to, dst, sizeof(dst));

    if (strcmp(src, "/") == 0 || strcmp(dst, "/") == 0) {
        set_text(msg, msg_len, "Invalid rename path");
        return false;
    }

    if (!ensure_mounted(msg, msg_len)) {
        return false;
    }

    SdFat &fs = sd_hw_fs();
    if (!fs.exists(src)) {
        set_text(msg, msg_len, "Source not found");
        return false;
    }
    if (fs.exists(dst)) {
        set_text(msg, msg_len, "Destination exists");
        return false;
    }

    bool ok = fs.rename(src, dst);
    set_text(msg, msg_len, ok ? "rename ok" : "rename failed");
    return ok;
}

bool sd_fs_touch_file(const char *path, char *msg, uint32_t msg_len) {
    char norm[128];
    normalize_path(path, norm, sizeof(norm));
    if (strcmp(norm, "/") == 0) {
        set_text(msg, msg_len, "Invalid file path");
        return false;
    }

    if (!ensure_mounted(msg, msg_len)) {
        return false;
    }

    SdFile f;
    bool ok = f.open(norm, O_WRONLY | O_CREAT | O_APPEND);
    if (ok) {
        f.close();
    }
    set_text(msg, msg_len, ok ? "touch ok" : "touch failed");
    return ok;
}
