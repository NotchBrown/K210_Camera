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

static bool ci_equal(const char *a, const char *b) {
    if (!a || !b) return false;
    while (*a && *b) {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
        if (ca != cb) return false;
        ++a;
        ++b;
    }
    return *a == *b;
}

// forward declaration for fallback open
static bool sd_open_by_path(SdFile *out, const char *path, int oflag);

// Recursively create directories using SdFile-level APIs so created dirs
// are visible to SdFile traversal. Returns true on success.
static bool sd_mkdir_recursive(const char *path) {
    if (!path) return false;
    char norm[128];
    if (!normalize_path(path, norm, sizeof(norm))) return false;
    if (strcmp(norm, "/") == 0) return true;

    SdFile *root = sd_hw_root_file();
    if (!root) return false;

    SdFile d1;
    SdFile d2;
    SdFile *parent = root;
    SdFile *child = &d1;

    unsigned int offset = 0;
    unsigned int depth = 0;
    char comp[13];

    while (true) {
        if (depth++ > 64U) {
            if (parent != root) parent->close();
            return false;
        }
        int n = 0;
        if (norm[offset] == '/') offset++;
        while (norm[offset] != '\0' && norm[offset] != '/' && n < 12) {
            comp[n++] = norm[offset++];
        }
        comp[n] = '\0';
        if (n == 0) break;

        // Try opening existing child
        if (child->open(*parent, comp, O_READ)) {
            if (parent != root) parent->close();
            parent = child;
            child = (child == &d1) ? &d2 : &d1;
            continue;
        }

        // Child does not exist: attempt to create directory entry using SdFile API
        APP_LOGI("sd_fs: sd_mkdir_recursive creating component=%s", comp);
        bool mkok = false;
        // Try SdFile::makeDir on parent using our child object
        mkok = child->makeDir(parent, comp) != 0;
        APP_LOGI("sd_fs: sd_mkdir_recursive makeDir result=%d comp=%s", mkok ? 1 : 0, comp);
        if (!mkok) {
            // Fallback: create accumulated path via SDClass mkdir (no leading '/')
            char acc[128];
            // build accumulated path up to current component
            size_t a = 0;
            unsigned int i = 0;
            while (norm[i] == '/') i++;
            while (i <= offset && norm[i] != '\0' && a + 1 < sizeof(acc)) {
                acc[a++] = norm[i++];
            }
            acc[a] = '\0';
            char mkp[128];
            if (acc[0] == '/') snprintf(mkp, sizeof(mkp), "%s", acc + 1); else snprintf(mkp, sizeof(mkp), "%s", acc);
            mkok = fs().mkdir(mkp);
            APP_LOGI("sd_fs: sd_mkdir_recursive fs().mkdir fallback result=%d path=%s", mkok ? 1 : 0, mkp);
        }

        // Ensure child is open (makeDir leaves child open; fallback mkdir won't)
        if (!child->isOpen()) {
            if (!child->open(*parent, comp, O_READ)) {
                if (parent != root) parent->close();
                return false;
            }
        }

        if (parent != root) parent->close();
        parent = child;
        child = (child == &d1) ? &d2 : &d1;
    }

    if (parent != root) parent->close();
    return true;
}

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
    while (true) {
        if (depth++ > 64U) {
            if (parent != root) parent->close();
            return false;
        }
        int n = 0;
        if (norm[offset] == '/') offset++;
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
// Fills `out_parent` when parent is not root and sets `out_is_root` accordingly.
// Caller must close `out_parent` if `out_is_root` is false.
static bool sd_open_parent(const char *path, SdFile *out_parent, bool *out_is_root, char *name, size_t name_len) {
    if (!path || !name || name_len == 0 || !out_is_root || !out_parent) return false;
    SdFile *root = sd_hw_root_file();
    if (!root) return false;

    char norm[128];
    if (!normalize_path(path, norm, sizeof(norm))) return false;
    if (strcmp(norm, "/") == 0) return false;

    // find last '/' to split parent path and final component name
    char *last = strrchr(norm, '/');
    if (!last) return false;
    const char *final = last + 1;
    if (final[0] == '\0') return false;
    // copy final name
    snprintf(name, name_len, "%s", final);

    if (last == norm) {
        // parent is root
        *out_is_root = true;
        return true;
    }

    // build parent path (without trailing)
    char parent_path[128];
    size_t len = (size_t)(last - norm);
    if (len >= sizeof(parent_path)) return false;
    memcpy(parent_path, norm, len);
    parent_path[len] = '\0';

    // open parent using sd_open_by_path to get a real SdFile
    if (!sd_open_by_path(out_parent, parent_path, O_READ)) {
        return false;
    }
    *out_is_root = false;
    return true;
}

bool copy_file_data(const char *from, const char *to) {
    SdFile src;
    APP_LOGI("sd_fs: copy start from=%s to=%s", from ? from : "(null)", to ? to : "(null)");
    if (!sd_open_by_path(&src, from, O_READ)) {
        APP_LOGE("sd_fs: open src failed from=%s", from ? from : "(null)");
        return false;
    }

    // open destination for write (create/truncate)
    // Ensure destination parent exists
    char parent_path[128];
    parent_path[0] = '\0';
    if (to && to[0] == '/') {
        snprintf(parent_path, sizeof(parent_path), "%s", to);
        char *last = strrchr(parent_path, '/');
        if (last) {
            if (last == parent_path) {
                // parent is root
                parent_path[1] = '\0';
                parent_path[0] = '/';
            } else {
                *last = '\0';
            }
        }
    }
            if (parent_path[0] != '\0' && !sd_path_exists(parent_path)) {
                APP_LOGI("sd_fs: copy_file_data creating parent dir=%s", parent_path);
                bool mk = sd_mkdir_recursive(parent_path);
                APP_LOGI("sd_fs: copy_file_data mkdir result=%d path=%s", mk ? 1 : 0, parent_path);
            }

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
    // Ensure destination directory exists using low-level API
    if (!sd_path_exists(to)) {
        // create destination directory tree using SdFile-aware helper
        bool mk = sd_mkdir_recursive(to);
        if (!mk && !sd_path_exists(to)) {
            return false;
        }
    }

    SdFile dir;
    if (!sd_open_by_path(&dir, from, O_READ) || !dir.isDir()) {
        if (dir.isOpen()) dir.close();
        return false;
    }

    dir.rewind();
    dir_t p;
    char name[64];
    bool ok = true;
    while (dir.readDir(&p) > 0) {
        if (p.name[0] == DIR_NAME_FREE) break;
        if (p.name[0] == DIR_NAME_DELETED || p.name[0] == '.') continue;
        if (!DIR_IS_FILE_OR_SUBDIR(&p)) continue;
        dir.dirName(p, name);
        if (name[0] == '\0' || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        char src_child[192];
        char dst_child[192];
        path_join(from, name, src_child, sizeof(src_child));
        path_join(to, name, dst_child, sizeof(dst_child));

        bool child_ok = DIR_IS_SUBDIR(&p) ? copy_tree(src_child, dst_child) : copy_file_data(src_child, dst_child);
        if (!child_ok) {
            ok = false;
            break;
        }
    }
    if (dir.isOpen()) dir.close();
    return ok;
}

static bool remove_entry_from_parent(SdFile *parent, const char *name) {
    if (!parent || !name || !name[0]) return false;

    if (SdFile::remove(parent, name) != 0) {
        return true;
    }

    // Fallback: find canonical entry name in directory and remove by that name.
    parent->rewind();
    dir_t p;
    char entry_name[13];
    while (parent->readDir(&p) > 0) {
        if (p.name[0] == DIR_NAME_FREE) break;
        if (p.name[0] == DIR_NAME_DELETED || p.name[0] == '.') continue;
        if (!DIR_IS_FILE_OR_SUBDIR(&p)) continue;

        parent->dirName(p, entry_name);
        if (!ci_equal(entry_name, name)) continue;

        bool ok = SdFile::remove(parent, entry_name) != 0;
        parent->rewind();
        return ok;
    }
    parent->rewind();
    return false;
}

static bool remove_leaf_path(const char *path, bool is_dir_hint) {
    char name[128];
    SdFile parent_obj;
    bool parent_is_root = false;
    if (!sd_open_parent(path, &parent_obj, &parent_is_root, name, sizeof(name))) {
        return is_dir_hint ? fs().rmdir(path) : fs().remove(path);
    }

    SdFile *parent = parent_is_root ? sd_hw_root_file() : &parent_obj;
    if (!parent) {
        if (!parent_is_root && parent_obj.isOpen()) parent_obj.close();
        return false;
    }

    bool ok = remove_entry_from_parent(parent, name);
    if (!parent_is_root && parent_obj.isOpen()) parent_obj.close();
    if (ok) return true;

    // High-level fallback is still useful for name format and directory semantics.
    return is_dir_hint ? fs().rmdir(path) : fs().remove(path);
}

bool remove_tree(const char *path) {
    bool is_dir = false;
    if (!is_dir_path(path, &is_dir)) {
        APP_LOGE("sd_fs: remove_tree is_dir_path failed path=%s", path ? path : "(null)");
        return false;
    }

    if (!is_dir) {
        bool ok = remove_leaf_path(path, false);
        APP_LOGI("sd_fs: remove_tree file remove result=%d path=%s", ok ? 1 : 0, path ? path : "(null)");
        return ok;
    }

    SdFile dir;
    if (!sd_open_by_path(&dir, path, O_READ) || !dir.isDir()) {
        if (dir.isOpen()) dir.close();
        return false;
    }

    dir.rewind();
    dir_t p;
    char child_name[64];
    bool ok = true;
    while (dir.readDir(&p) > 0) {
        if (p.name[0] == DIR_NAME_FREE) break;
        if (p.name[0] == DIR_NAME_DELETED || p.name[0] == '.') continue;
        if (!DIR_IS_FILE_OR_SUBDIR(&p)) continue;
        dir.dirName(p, child_name);
        if (child_name[0] == '\0' || strcmp(child_name, ".") == 0 || strcmp(child_name, "..") == 0) continue;

        char full[192];
        path_join(path, child_name, full, sizeof(full));
        bool child_ok;
        if (DIR_IS_SUBDIR(&p)) {
            child_ok = remove_tree(full);
        } else {
            APP_LOGI("sd_fs: remove_tree removing file=%s", full);
            child_ok = remove_leaf_path(full, false);
            APP_LOGI("sd_fs: remove_tree low-level remove result=%d file=%s", child_ok ? 1 : 0, full);
        }
        if (!child_ok) {
            APP_LOGE("sd_fs: remove_tree child failed full=%s", full);
            ok = false;
            break;
        }
    }
    if (dir.isOpen()) dir.close();

    if (!ok) {
        APP_LOGE("sd_fs: remove_tree failed for path=%s", path ? path : "(null)");
        return false;
    }
    // Prefer directory-instance removal first; this handles empty-dir semantics better.
    SdFile dir_self;
    if (sd_open_by_path(&dir_self, path, O_READ)) {
        bool rd = dir_self.rmDir();
        dir_self.close();
        if (rd) {
            return true;
        }
    }

    return remove_leaf_path(path, true);
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

    // Try to create the directory tree using SdFile-aware helper (handles short/long names)
    bool ok = sd_mkdir_recursive(norm);
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
        // More robust existence check using SdFile open to avoid SDClass path quirks
        SdFile tmp;
        if (sd_open_by_path(&tmp, dst, O_READ)) {
            bool isd = tmp.isDir();
            tmp.close();
            APP_LOGE("sd_fs: copy dst exists=%s is_dir=%d", dst, isd ? 1 : 0);
        } else {
            APP_LOGE("sd_fs: copy dst exists (sd_path_exists true but open failed) dst=%s", dst);
        }
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
