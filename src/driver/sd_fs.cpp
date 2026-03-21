#include "sd_fs.h"

#include "sd_hw.h"

#include <SD.h>

#include <stdio.h>
#include <string.h>

#include "app_log.h"

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

// forward declaration for fallback open
static bool sd_open_by_path(SdFile *out, const char *path, int oflag);

bool is_dir_path(const char *path, bool *is_dir) {
    if (is_dir) {
        *is_dir = false;
    }
    File f = fs().open(path);
    if (!f) {
        APP_LOGW("sd_fs: fs().open failed path=%s, trying SdFile", path ? path : "(null)");
        SdFile s;
        if (sd_open_by_path(&s, path, O_READ)) {
            bool d = s.isDir();
            s.close();
            if (is_dir) *is_dir = d;
            APP_LOGI("sd_fs: sd_open_by_path succeeded path=%s is_dir=%d", path ? path : "(null)", d ? 1 : 0);
            return true;
        }
        APP_LOGE("sd_fs: sd_open_by_path also failed path=%s", path ? path : "(null)");
        return false;
    }
    bool d = f.isDirectory();
    f.close();
    if (is_dir) {
        *is_dir = d;
    }
    return true;
}

// Low-level existence check using SdFile traversal from root (avoids SDClass path quirks)
static bool sd_path_exists(const char *path) {
    if (!path) return false;
    SdFile *root = sd_hw_root_file();
    if (!root) return false;

    char norm[128];
    if (!normalize_path(path, norm, sizeof(norm))) {
        return false;
    }

    if (strcmp(norm, "/") == 0) {
        return true;
    }

    SdFile d1;
    SdFile d2;
    SdFile *parent = root;
    SdFile *child = &d1;
    SdFile *current = root;

    unsigned int offset = 0;
    unsigned int depth = 0;
    char comp[13];

    while (true) {
        if (depth++ > 64U) {
            if (parent != root) parent->close();
            return false;
        }
        int n = 0;
        if (norm[offset] == '/') {
            offset++;
        }
        while (norm[offset] != '\0' && norm[offset] != '/' && n < 12) {
            comp[n++] = norm[offset++];
        }
        comp[n] = '\0';
        if (n == 0) break;

        if (!child->open(*parent, comp, O_READ)) {
            if (parent != root) parent->close();
            return false;
        }

        if (parent != root) parent->close();

        current = child;
        parent = child;
        child = (child == &d1) ? &d2 : &d1;
    }

    bool exists = current != NULL && current->isOpen();
    if (current != root) {
        current->close();
    }
    return exists;
}

// forward declaration for fallback open
static bool sd_open_by_path(SdFile *out, const char *path, int oflag);

// Open a file by absolute path into out. oflag uses SdFile/O_ flags.
static bool sd_open_by_path(SdFile *out, const char *path, int oflag) {
    if (!out || !path) return false;
    SdFile *root = sd_hw_root_file();
    if (!root) return false;

    char norm[128];
    if (!normalize_path(path, norm, sizeof(norm))) return false;
    if (strcmp(norm, "/") == 0) return false;

    SdFile d1;
    SdFile d2;
    SdFile *parent = root;
    SdFile *child = &d1;

    unsigned int offset = 0;
    unsigned int depth = 0;
    char comp[13];

    // Traverse to parent of final component
    const char *last = norm + strlen(norm);
    while (true) {
        if (depth++ > 64U) {
            if (parent != root) parent->close();
            return false;
        }
        int n = 0;
        if (norm[offset] == '/') offset++;
        unsigned int start = offset;
        while (norm[offset] != '\0' && norm[offset] != '/' && n < 12) {
            comp[n++] = norm[offset++];
        }
        comp[n] = '\0';
        if (norm[offset] == '\0') {
            // final component: open it with provided flags relative to parent
            bool ok = child->open(*parent, comp, oflag);
            if (parent != root) parent->close();
            if (!ok) {
                // ensure child closed
                child->close();
                return false;
            }
            // move constructed child into out
            *out = *child;
            return true;
        }

        if (!child->open(*parent, comp, O_READ)) {
            if (parent != root) parent->close();
            return false;
        }
        if (parent != root) parent->close();
        parent = child;
        child = (child == &d1) ? &d2 : &d1;
    }
}

// Open parent directory and return final component name in `name`.
// Caller must not close parent if parent == root; otherwise caller closes parent when done.
static SdFile *sd_open_parent(const char *path, char *name, size_t name_len) {
    if (!path || !name || name_len == 0) return NULL;
    SdFile *root = sd_hw_root_file();
    if (!root) return NULL;

    char norm[128];
    if (!normalize_path(path, norm, sizeof(norm))) return NULL;
    if (strcmp(norm, "/") == 0) return NULL;

    SdFile d1;
    SdFile d2;
    SdFile *parent = root;
    SdFile *child = &d1;

    unsigned int offset = 0;
    unsigned int depth = 0;
    char comp[13];

    // Walk components until final; keep parent open
    while (true) {
        if (depth++ > 64U) {
            if (parent != root) parent->close();
            return NULL;
        }
        int n = 0;
        if (norm[offset] == '/') offset++;
        unsigned int start = offset;
        while (norm[offset] != '\0' && norm[offset] != '/' && n < 12) {
            comp[n++] = norm[offset++];
        }
        comp[n] = '\0';
        if (norm[offset] == '\0') {
            // comp is final component name; parent points to directory containing it
            snprintf(name, name_len, "%s", comp);
            return parent;
        }

        if (!child->open(*parent, comp, O_READ)) {
            if (parent != root) parent->close();
            return NULL;
        }
        if (parent != root) parent->close();
        parent = child;
        child = (child == &d1) ? &d2 : &d1;
    }
}

bool copy_file_data(const char *from, const char *to) {
    SdFile src;
    APP_LOGI("sd_fs: copy start from=%s to=%s", from ? from : "(null)", to ? to : "(null)");
    if (!sd_open_by_path(&src, from, O_READ)) {
        APP_LOGE("sd_fs: open src failed from=%s", from ? from : "(null)");
        return false;
    }

    // open destination for write (create/truncate)
    SdFile dst;
    if (!sd_open_by_path(&dst, to, O_RDWR | O_CREAT | O_TRUNC)) {
        APP_LOGE("sd_fs: open dst failed to=%s", to ? to : "(null)");
        src.close();
        return false;
    }

    uint8_t buf[512];
    bool ok = true;
    uint32_t total = 0;
    while (true) {
        int n = src.read(buf, (uint32_t)sizeof(buf));
        if (n < 0) {
            APP_LOGE("sd_fs: read error from=%s ret=%d total=%u", from, n, total);
            ok = false;
            break;
        }
        if (n == 0) break;
        int w = dst.write(buf, (uint32_t)n);
        if (w != n) {
            APP_LOGE("sd_fs: write error to=%s want=%d wrote=%d total=%u", to, n, w, total);
            ok = false;
            break;
        }
        total += (uint32_t)n;
    }

    dst.close();
    src.close();
    APP_LOGI("sd_fs: copy finished bytes=%u ok=%d", total, ok ? 1 : 0);
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

    if (!sd_path_exists(to) && !fs().mkdir(to)) {
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
        APP_LOGE("sd_fs: remove_tree is_dir_path failed path=%s", path ? path : "(null)");
        return false;
    }

    if (!is_dir) {
        // try low-level removal via parent
        char name[128];
        SdFile *parent = sd_open_parent(path, name, sizeof(name));
        if (!parent) {
            APP_LOGI("sd_fs: remove_tree fallback fs().remove path=%s", path ? path : "(null)");
            bool r = fs().remove(path);
            APP_LOGI("sd_fs: remove_tree fs().remove result=%d path=%s", r ? 1 : 0, path ? path : "(null)");
            return r;
        }
        APP_LOGI("sd_fs: remove_tree using SdFile::remove parent=%p name=%s", (void*)parent, name);
        bool ok = SdFile::remove(parent, name) != 0;
        APP_LOGI("sd_fs: remove_tree SdFile::remove result=%d name=%s", ok ? 1 : 0, name);
        if (ok) {
            if (parent != sd_hw_root_file()) parent->close();
            return true;
        }

        // Retry by reopening parent directory with write access
        if (parent != sd_hw_root_file()) parent->close();
        // compute parent path
        char parent_path[128];
        parent_path[0] = '\0';
        if (path && path[0] == '/') {
            // copy path and strip final component
            snprintf(parent_path, sizeof(parent_path), "%s", path);
            char *last = strrchr(parent_path, '/');
            if (last && last != parent_path) {
                *last = '\0';
            } else {
                // parent is root
                snprintf(parent_path, sizeof(parent_path), "/");
            }
        }

        SdFile parent2;
        if (parent_path[0] != '\0' && sd_open_by_path(&parent2, parent_path, O_RDWR)) {
            APP_LOGI("sd_fs: remove_tree retry with writable parent=%s", parent_path);
            bool ok2 = SdFile::remove(&parent2, name) != 0;
            APP_LOGI("sd_fs: remove_tree retry result=%d name=%s", ok2 ? 1 : 0, name);
            parent2.close();
            return ok2;
        }

        return false;
    }

    File dir = fs().open(path);
    if (!dir || !dir.isDirectory()) {
        dir.close();
        return false;
    }

    File child;
    char child_name[64];
    bool ok = true;
    while (true) {
        child = dir.openNextFile();
        if (!child) {
            break;
        }

        snprintf(child_name, sizeof(child_name), "%s", child.name());
        bool child_is_dir = child.isDirectory();
        child.close();

        if (child_name[0] == '\0' || strcmp(child_name, ".") == 0 || strcmp(child_name, "..") == 0) {
            continue;
        }

        char full[192];
        path_join(path, child_name, full, sizeof(full));
        bool child_ok;
        if (child_is_dir) {
            child_ok = remove_tree(full);
        } else {
            APP_LOGI("sd_fs: remove_tree removing file=%s", full);
            child_ok = fs().remove(full);
            APP_LOGI("sd_fs: remove_tree fs().remove result=%d file=%s", child_ok ? 1 : 0, full);
        }
        if (!child_ok) {
            APP_LOGE("sd_fs: remove_tree child failed full=%s", full);
            ok = false;
            break;
        }
    }
    dir.close();

    if (!ok) {
        APP_LOGE("sd_fs: remove_tree failed for path=%s", path ? path : "(null)");
        return false;
    }
    // remove directory via parent
    char name[128];
    SdFile *parent = sd_open_parent(path, name, sizeof(name));
    if (!parent) {
        return fs().rmdir(path);
    }
    bool r = SdFile::remove(parent, name) != 0;
    if (parent != sd_hw_root_file()) parent->close();
    return r;
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

    uint32_t total = sd_hw_total_kb();
    if (total_kb) {
        *total_kb = total;
    }
    if (free_kb) {
        *free_kb = sd_hw_free_kb();
    }

    if (msg && msg_len > 0) {
        snprintf(msg, msg_len, "SD mounted");
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
        unsigned int depth = 0;
        char comp[13];

        while (true) {
            if (depth++ > 16U) {
                if (parent != root) {
                    parent->close();
                }
                set_text(out, out_len, "Path too deep");
                return false;
            }
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
            snprintf(line, sizeof(line), "DIR %s", name);
        } else {
            snprintf(line, sizeof(line), "FILE %s", name);
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

    if (sd_path_exists(norm)) {
        set_text(msg, msg_len, "Path already exists");
        return true;
    }

    // try low-level mkdir via parent
    char name[128];
    SdFile *parent = sd_open_parent(norm, name, sizeof(name));
    bool ok = false;
    if (parent) {
        ok = parent->makeDir(parent, name) != 0;
        if (parent != sd_hw_root_file()) parent->close();
    } else {
        ok = fs().mkdir(norm);
    }

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

    if (!sd_path_exists(norm)) {
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

    if (!sd_path_exists(src)) {
        APP_LOGE("sd_fs: copy src not found=%s", src);
        set_text(msg, msg_len, "Source not found");
        return false;
    }

    if (sd_path_exists(dst)) {
        APP_LOGE("sd_fs: copy dst exists=%s", dst);
        set_text(msg, msg_len, "Destination exists");
        return false;
    }

    APP_LOGI("sd_fs: copy_tree start src=%s dst=%s", src, dst);
    bool ok = copy_tree(src, dst);
    APP_LOGI("sd_fs: copy_tree end src=%s dst=%s ok=%d", src, dst, ok ? 1 : 0);
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

    if (!sd_path_exists(src)) {
        set_text(msg, msg_len, "Source not found");
        return false;
    }

    if (sd_path_exists(dst)) {
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

    // try low-level create/truncate using SdFile
    SdFile file;
    bool ok = sd_open_by_path(&file, norm, O_RDWR | O_CREAT | O_TRUNC);
    if (ok) file.close();
    set_text(msg, msg_len, ok ? "touch ok" : "touch failed");
    return ok;
}

bool sd_fs_exists(const char *path) {
    if (!path) return false;
    if (!sd_hw_is_mounted()) return false;
    char norm[128];
    if (!normalize_path(path, norm, sizeof(norm))) return false;
    return sd_path_exists(norm);
}
