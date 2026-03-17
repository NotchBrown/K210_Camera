#include "sd_fs.h"

#include "sd_hw.h"

#include <SD.h>

#include <stdio.h>
#include <string.h>

namespace {

void set_text(char *out, uint32_t out_len, const char *text) {
    if (!out || out_len == 0) {
        return;
    }
    snprintf(out, out_len, "%s", text ? text : "");
}

SDClass &fs(void) {
    return sd_hw_fs();
}

bool ensure_mounted(char *msg, uint32_t msg_len) {
    if (sd_hw_is_mounted()) {
        return true;
    }
    return sd_hw_mount(msg, msg_len);
}

bool normalize_path(const char *in, char *out, size_t out_len) {
    if (!out || out_len < 2) {
        return false;
    }

    if (!in || in[0] == '\0') {
        snprintf(out, out_len, "/");
        return true;
    }

    while (*in == ' ') {
        in++;
    }

    if (*in == '\0') {
        snprintf(out, out_len, "/");
        return true;
    }

    if (strncmp(in, "/SD/", 4) == 0) {
        in += 3;
    } else if (strcmp(in, "/SD") == 0) {
        in = "/";
    }

    if (*in != '/') {
        snprintf(out, out_len, "/%s", in);
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
    File f = fs().open(path);
    if (!f) {
        return false;
    }
    bool d = f.isDirectory();
    f.close();
    if (is_dir) {
        *is_dir = d;
    }
    return true;
}

bool copy_file_data(const char *from, const char *to) {
    File src = fs().open(from, FILE_READ);
    if (!src) {
        return false;
    }

    if (fs().exists(to)) {
        (void)fs().remove(to);
    }

    File dst = fs().open(to, FILE_WRITE);
    if (!dst) {
        src.close();
        return false;
    }

    uint8_t buf[512];
    bool ok = true;
    while (true) {
        int n = src.read(buf, (uint32_t)sizeof(buf));
        if (n < 0) {
            ok = false;
            break;
        }
        if (n == 0) {
            break;
        }
        if ((int)dst.write(buf, (uint32_t)n) != n) {
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

    if (!fs().exists(to) && !fs().mkdir(to)) {
        return false;
    }

    File dir = fs().open(from);
    if (!dir || !dir.isDirectory()) {
        dir.close();
        return false;
    }

    File child;
    char name[64];
    bool ok = true;
    while (true) {
        child = dir.openNextFile();
        if (!child) {
            break;
        }

        snprintf(name, sizeof(name), "%s", child.name());
        bool child_is_dir = child.isDirectory();
        child.close();

        if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        char src_child[192];
        char dst_child[192];
        path_join(from, name, src_child, sizeof(src_child));
        path_join(to, name, dst_child, sizeof(dst_child));

        bool child_ok = child_is_dir ? copy_tree(src_child, dst_child) : copy_file_data(src_child, dst_child);
        if (!child_ok) {
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

    if (!is_dir) {
        return fs().remove(path);
    }

    File dir = fs().open(path);
    if (!dir || !dir.isDirectory()) {
        dir.close();
        return false;
    }

    File child;
    char name[64];
    bool ok = true;
    while (true) {
        child = dir.openNextFile();
        if (!child) {
            break;
        }

        snprintf(name, sizeof(name), "%s", child.name());
        bool child_is_dir = child.isDirectory();
        child.close();

        if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        char full[192];
        path_join(path, name, full, sizeof(full));
        bool child_ok = child_is_dir ? remove_tree(full) : fs().remove(full);
        if (!child_ok) {
            ok = false;
            break;
        }
    }
    dir.close();

    if (!ok) {
        return false;
    }
    return fs().rmdir(path);
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

    if (msg && msg_len > 0) {
        snprintf(msg, msg_len, "SD mounted (capacity unknown)");
    }
    return true;
}

bool sd_fs_format(uint32_t *total_kb, uint32_t *free_kb, char *msg, uint32_t msg_len) {
    if (total_kb) {
        *total_kb = 0;
    }
    if (free_kb) {
        *free_kb = 0;
    }
    set_text(msg, msg_len, "Format unsupported in SD(k210)");
    return false;
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
    if (!normalize_path(path, norm, sizeof(norm))) {
        set_text(out, out_len, "Invalid path");
        return false;
    }

    SdFile *root = sd_hw_root_file();
    if (!root) {
        set_text(out, out_len, "Root not ready");
        return false;
    }

    SdFile d1;
    SdFile d2;
    SdFile *parent = root;
    SdFile *child = &d1;
    SdFile *current = root;

    if (strcmp(norm, "/") != 0) {
        unsigned int offset = 0;
        char comp[13];

        while (true) {
            int n = 0;
            if (norm[offset] == '/') {
                offset++;
            }
            while (norm[offset] != '\0' && norm[offset] != '/' && n < 12) {
                comp[n++] = norm[offset++];
            }
            comp[n] = '\0';

            if (n == 0) {
                break;
            }

            if (!child->open(*parent, comp, O_READ)) {
                if (parent != root) {
                    parent->close();
                }
                set_text(out, out_len, "Open dir failed");
                return false;
            }

            if (parent != root) {
                parent->close();
            }

            current = child;
            parent = child;
            child = (child == &d1) ? &d2 : &d1;
        }

        if (!current->isDir()) {
            current->close();
            set_text(out, out_len, "Open dir failed");
            return false;
        }
    }

    current->rewind();

    dir_t p;
    char name[13];
    char line[128];
    bool ok = true;
    while (current->readDir(&p) > 0) {
        if (p.name[0] == DIR_NAME_FREE) {
            break;
        }
        if (p.name[0] == DIR_NAME_DELETED || p.name[0] == '.') {
            continue;
        }
        if (!DIR_IS_FILE_OR_SUBDIR(&p)) {
            continue;
        }

        current->dirName(p, name);
        if (DIR_IS_SUBDIR(&p)) {
            snprintf(line, sizeof(line), "DIR /%s", name);
        } else {
            snprintf(line, sizeof(line), "FILE /%s", name);
        }

        if (!append_line(out, out_len, line)) {
            ok = false;
            break;
        }
    }

    if (current != root) {
        current->close();
    } else {
        current->rewind();
    }

    if (!ok) {
        set_text(out, out_len, "List buffer too small");
    }
    return ok;
}

bool sd_fs_mkdir(const char *path, char *msg, uint32_t msg_len) {
    char norm[128];
    if (!normalize_path(path, norm, sizeof(norm))) {
        set_text(msg, msg_len, "Invalid path");
        return false;
    }

    if (strcmp(norm, "/") == 0) {
        set_text(msg, msg_len, "Root already exists");
        return true;
    }

    if (!ensure_mounted(msg, msg_len)) {
        return false;
    }

    if (fs().exists(norm)) {
        set_text(msg, msg_len, "Path already exists");
        return true;
    }

    bool ok = fs().mkdir(norm);
    set_text(msg, msg_len, ok ? "mkdir ok" : "mkdir failed");
    return ok;
}

bool sd_fs_delete(const char *path, char *msg, uint32_t msg_len) {
    char norm[128];
    if (!normalize_path(path, norm, sizeof(norm))) {
        set_text(msg, msg_len, "Invalid path");
        return false;
    }

    if (strcmp(norm, "/") == 0) {
        set_text(msg, msg_len, "Delete root is forbidden");
        return false;
    }

    if (!ensure_mounted(msg, msg_len)) {
        return false;
    }

    if (!fs().exists(norm)) {
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
    if (!normalize_path(from, src, sizeof(src)) || !normalize_path(to, dst, sizeof(dst))) {
        set_text(msg, msg_len, "Invalid path");
        return false;
    }

    if (strcmp(src, "/") == 0 || strcmp(dst, "/") == 0) {
        set_text(msg, msg_len, "Root copy not allowed");
        return false;
    }

    if (!ensure_mounted(msg, msg_len)) {
        return false;
    }

    if (!fs().exists(src)) {
        set_text(msg, msg_len, "Source not found");
        return false;
    }

    if (fs().exists(dst)) {
        set_text(msg, msg_len, "Destination exists");
        return false;
    }

    bool ok = copy_tree(src, dst);
    set_text(msg, msg_len, ok ? "copy ok" : "copy failed");
    return ok;
}

bool sd_fs_rename(const char *from, const char *to, char *msg, uint32_t msg_len) {
    char src[128];
    char dst[128];
    if (!normalize_path(from, src, sizeof(src)) || !normalize_path(to, dst, sizeof(dst))) {
        set_text(msg, msg_len, "Invalid path");
        return false;
    }

    if (strcmp(src, "/") == 0 || strcmp(dst, "/") == 0) {
        set_text(msg, msg_len, "Root rename not allowed");
        return false;
    }

    if (!ensure_mounted(msg, msg_len)) {
        return false;
    }

    if (!fs().exists(src)) {
        set_text(msg, msg_len, "Source not found");
        return false;
    }

    if (fs().exists(dst)) {
        set_text(msg, msg_len, "Destination exists");
        return false;
    }

    bool ok = copy_tree(src, dst) && remove_tree(src);
    set_text(msg, msg_len, ok ? "rename ok" : "rename failed");
    return ok;
}

bool sd_fs_touch_file(const char *path, char *msg, uint32_t msg_len) {
    char norm[128];
    if (!normalize_path(path, norm, sizeof(norm))) {
        set_text(msg, msg_len, "Invalid path");
        return false;
    }

    if (strcmp(norm, "/") == 0) {
        set_text(msg, msg_len, "Invalid file path");
        return false;
    }

    if (!ensure_mounted(msg, msg_len)) {
        return false;
    }

    File f = fs().open(norm, FILE_WRITE);
    bool ok = (bool)f;
    if (ok) {
        f.close();
    }

    set_text(msg, msg_len, ok ? "touch ok" : "touch failed");
    return ok;
}
