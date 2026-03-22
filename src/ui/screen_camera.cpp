#include <lvgl.h>

#include <string.h>
#include <stdio.h>

#include <Sipeed_OV2640.h>

#include "sd_fs.h"
#include "sd_hw.h"

#include "kendryte-standalone-sdk/lib/freertos/include/FreeRTOS.h"
#include "kendryte-standalone-sdk/lib/freertos/include/task.h"

#include "app_fonts.h"
#include "app_log.h"
#include "app_manager.h"
#include "screen_camera.h"

static const uint16_t k_preview_width = 320;
static const uint16_t k_preview_height = 240;
static const uint32_t k_preview_pixels = (uint32_t)k_preview_width * (uint32_t)k_preview_height;
static uint16_t s_src_width = 320;
static uint16_t s_src_height = 240;

static lv_obj_t *s_clock_label = NULL;
static lv_obj_t *s_btn_take = NULL;
static lv_obj_t *s_btn_take_label = NULL;
static lv_obj_t *s_btn_record = NULL;
static lv_obj_t *s_btn_record_label = NULL;
static lv_obj_t *s_record_time = NULL;
static lv_obj_t *s_preview_image = NULL;
static lv_obj_t *s_tabview = NULL;
static lv_timer_t *s_refresh_timer = NULL;
static lv_timer_t *s_present_timer = NULL;

static Sipeed_OV2640 s_camera_vga(FRAMESIZE_VGA, PIXFORMAT_RGB565);
static Sipeed_OV2640 s_camera_qvga(FRAMESIZE_QVGA, PIXFORMAT_RGB565);
static Sipeed_OV2640 s_camera_qqvga(FRAMESIZE_QQVGA, PIXFORMAT_RGB565);
static Sipeed_OV2640 *s_camera = NULL;
static lv_image_dsc_t s_preview_dscs[2];
static uint16_t s_preview_bufs[2][320 * 240] __attribute__((aligned(64)));
static volatile uint8_t s_preview_front_index = 0;
static volatile uint8_t s_preview_ready_index = 0;
static volatile uint32_t s_preview_ready_seq = 0;
static uint32_t s_preview_displayed_seq = 0;
static uint32_t s_preview_last_frame_ms = 0;
static volatile bool s_preview_stop_requested = false;
static volatile bool s_preview_task_alive = false;
static TaskHandle_t s_preview_task = NULL;
static uint16_t s_preview_period_ms = 83;
static bool s_preview_first_frame_logged = false;

static bool preview_task_start(void);
static void preview_task_stop(void);

static framerate_t map_ov2640_framerate(uint16_t fps) {
    if (fps <= 5U) {
        return FRAMERATE_2FPS;
    }
    if (fps <= 11U) {
        return FRAMERATE_8FPS;
    }
    if (fps <= 22U) {
        return FRAMERATE_15FPS;
    }
    if (fps <= 45U) {
        return FRAMERATE_30FPS;
    }
    return FRAMERATE_60FPS;
}

static uint16_t map_frame_rate_index_to_fps(uint8_t index) {
    switch (index) {
        case 0: return 2;
        case 1: return 8;
        case 2: return 15;
        case 3: return 30;
        case 4: return 60;
        default: return 15;
    }
}

static void init_preview_placeholder(void) {
    for (uint16_t y = 0; y < k_preview_height; y++) {
        for (uint16_t x = 0; x < k_preview_width; x++) {
            uint16_t r = (uint16_t)(((uint32_t)x * 31U) / (k_preview_width - 1U));
            uint16_t g = (uint16_t)(((uint32_t)y * 63U) / (k_preview_height - 1U));
            uint16_t b = (uint16_t)((((uint32_t)x + (uint32_t)y) * 31U) / (k_preview_width + k_preview_height - 2U));
            uint16_t c = (uint16_t)((r << 11) | (g << 5) | b);
            s_preview_bufs[0][(uint32_t)y * k_preview_width + x] = c;
            s_preview_bufs[1][(uint32_t)y * k_preview_width + x] = c;
        }
    }
}

static void map_capture_profile(uint8_t index, framesize_t *frame_size, uint16_t *fps) {
    if (!frame_size || !fps) {
        return;
    }

    switch (index) {
        case 0:
            *frame_size = FRAMESIZE_VGA;
            *fps = 15;
            break;
        case 1:
            *frame_size = FRAMESIZE_QVGA;
            *fps = 15;
            break;
        case 2:
            *frame_size = FRAMESIZE_QQVGA;
            *fps = 15;
            break;
        default:
            *frame_size = FRAMESIZE_QVGA;
            *fps = 15;
            break;
    }
}

static void map_capture_dimensions(uint8_t index, uint16_t *width, uint16_t *height) {
    if (!width || !height) {
        return;
    }

    switch (index) {
        case 0:
            *width = 640;
            *height = 480;
            break;
        case 1:
            *width = 320;
            *height = 240;
            break;
        case 2:
            *width = 160;
            *height = 120;
            break;
        default:
            *width = 320;
            *height = 240;
            break;
    }
}

static bool open_dir_by_path(const char *path, SdFile *out_dir) {
    if (!path || !out_dir) {
        return false;
    }

    SdFile *root = sd_hw_root_file();
    if (!root) {
        return false;
    }

    if (strcmp(path, "/") == 0) {
        return false;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%s", path);

    SdFile cur_a;
    SdFile cur_b;
    SdFile *parent = root;
    SdFile *child = &cur_a;

    char *ctx = NULL;
    char *token = strtok_r(buf, "/", &ctx);
    while (token) {
        if (!child->open(*parent, token, O_READ)) {
            if (parent != root) {
                parent->close();
            }
            return false;
        }

        if (!child->isDir()) {
            child->close();
            if (parent != root) {
                parent->close();
            }
            return false;
        }

        if (parent != root) {
            parent->close();
        }
        parent = child;
        child = (child == &cur_a) ? &cur_b : &cur_a;

        token = strtok_r(NULL, "/", &ctx);
    }

    if (parent == root) {
        return false;
    }

    *out_dir = *parent;
    return true;
}

static bool open_new_photo_file(SdFile *out_file, uint32_t *out_index, char *out_path, size_t out_path_len) {
    if (!out_file || !out_index || !out_path || out_path_len == 0U) {
        return false;
    }

    for (uint32_t index = 0; index < 100000000UL; index++) {
        char path[64];
        snprintf(path, sizeof(path), "/DCMI/PHOTO/%lu.BMP", (unsigned long)index);
        if (sd_fs_exists(path)) {
            continue;
        }

        SdFile photo_dir;
        if (!open_dir_by_path("/DCMI/PHOTO", &photo_dir)) {
            return false;
        }

        char name[16];
        snprintf(name, sizeof(name), "%lu.BMP", (unsigned long)index);
        bool ok = out_file->open(photo_dir, name, O_RDWR | O_CREAT | O_EXCL);
        photo_dir.close();
        if (!ok) {
            continue;
        }

        snprintf(out_path, out_path_len, "%s", path);
        *out_index = index;
        return true;
    }

    return false;
}

static void write_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
}

static void write_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}

static bool sdfile_write_all(SdFile *file, const uint8_t *data, uint32_t len) {
    if (!file || !data) {
        return false;
    }

    uint32_t off = 0;
    while (off < len) {
        uint16_t chunk = (uint16_t)((len - off) > 512U ? 512U : (len - off));
        int wrote = file->write(data + off, chunk);
        if (wrote <= 0) {
            return false;
        }
        off += (uint32_t)wrote;
    }
    return true;
}

static bool write_bmp_header_rgb565(SdFile *file, uint16_t width, uint16_t height) {
    uint32_t pixel_bytes = (uint32_t)width * (uint32_t)height * 2U;
    uint32_t data_offset = 14U + 40U + 12U;
    uint32_t file_size = data_offset + pixel_bytes;

    uint8_t header[66];
    memset(header, 0, sizeof(header));

    header[0] = 'B';
    header[1] = 'M';
    write_le32(&header[2], file_size);
    write_le32(&header[10], data_offset);

    write_le32(&header[14], 40U);
    write_le32(&header[18], (uint32_t)width);
    write_le32(&header[22], (uint32_t)(-(int32_t)height));
    write_le16(&header[26], 1U);
    write_le16(&header[28], 16U);
    write_le32(&header[30], 3U);  // BI_BITFIELDS
    write_le32(&header[34], pixel_bytes);

    write_le32(&header[54], 0xF800U);
    write_le32(&header[58], 0x07E0U);
    write_le32(&header[62], 0x001FU);

    return sdfile_write_all(file, header, sizeof(header));
}

static bool write_bmp_pixels_from_be565(SdFile *file, const uint8_t *raw_be, uint16_t width, uint16_t height) {
    if (!file || !raw_be) {
        return false;
    }

    uint32_t total = (uint32_t)width * (uint32_t)height * 2U;
    uint32_t off = 0;
    uint8_t out[512];

    while (off < total) {
        uint32_t chunk = total - off;
        if (chunk > sizeof(out)) {
            chunk = sizeof(out);
        }
        if ((chunk & 1U) != 0U) {
            chunk--;
        }

        for (uint32_t i = 0; i < chunk; i += 2U) {
            out[i] = raw_be[off + i + 1U];
            out[i + 1U] = raw_be[off + i];
        }

        if (!sdfile_write_all(file, out, chunk)) {
            return false;
        }
        off += chunk;
    }
    return true;
}

static bool save_snapshot_to_sd(char *saved_path, size_t saved_path_len) {
    if (!saved_path || saved_path_len == 0U) {
        return false;
    }

    if (!s_camera) {
        APP_LOGE("Camera: snapshot save failed, camera not ready");
        return false;
    }

    app_camera_settings_t cfg;
    app_manager_get_camera_settings(&cfg);

    uint16_t capture_w = 320;
    uint16_t capture_h = 240;
    map_capture_dimensions(cfg.capture_res_index, &capture_w, &capture_h);
    uint32_t frame_bytes = (uint32_t)capture_w * (uint32_t)capture_h * 2U;

    bool worker_was_running = s_preview_task_alive;
    if (worker_was_running) {
        preview_task_stop();
    }

    bool ok = false;
    const uint8_t *raw = NULL;
    SdFile out;
    bool out_opened = false;
    uint32_t file_index = 0;
    char photo_path[64] = {0};
    bool hdr_ok = false;
    bool pix_ok = false;

    char mount_msg[96];
    if (!sd_hw_is_mounted() && !sd_hw_mount(mount_msg, sizeof(mount_msg))) {
        APP_LOGE("Camera: SD mount failed: %s", mount_msg);
        goto cleanup;
    }

    char fs_msg[96];
    if (!sd_fs_mkdir("/DCMI", fs_msg, sizeof(fs_msg))) {
        APP_LOGE("Camera: mkdir /DCMI failed: %s", fs_msg);
        goto cleanup;
    }
    if (!sd_fs_mkdir("/DCMI/PHOTO", fs_msg, sizeof(fs_msg))) {
        APP_LOGE("Camera: mkdir /DCMI/PHOTO failed: %s", fs_msg);
        goto cleanup;
    }

    raw = s_camera->snapshot();
    if (!raw) {
        APP_LOGE("Camera: snapshot capture failed");
        goto cleanup;
    }

    if (!open_new_photo_file(&out, &file_index, photo_path, sizeof(photo_path))) {
        APP_LOGE("Camera: failed to allocate photo file name");
        goto cleanup;
    }
    out_opened = true;

    hdr_ok = write_bmp_header_rgb565(&out, capture_w, capture_h);
    if (hdr_ok) {
        pix_ok = write_bmp_pixels_from_be565(&out, raw, capture_w, capture_h);
    }
    out.close();
    out_opened = false;
    if (!hdr_ok || !pix_ok) {
        APP_LOGE("Camera: photo write failed idx=%lu frame_bytes=%lu",
                 (unsigned long)file_index,
                 (unsigned long)frame_bytes);
        goto cleanup;
    }

    snprintf(saved_path, saved_path_len, "%s", photo_path);
    APP_LOGI("Camera: photo saved path=%s size=%lu res=%ux%u fmt_idx=%u",
             saved_path,
             (unsigned long)frame_bytes,
             (unsigned)capture_w,
             (unsigned)capture_h,
             (unsigned)cfg.pix_format_index);
    ok = true;

cleanup:
    if (out_opened) {
        out.close();
    }
    if (worker_was_running) {
        if (!preview_task_start()) {
            APP_LOGE("Camera: preview task restart failed");
        }
    }
    return ok;
}

static void apply_tabview_style(lv_obj_t *tabview) {
    lv_obj_set_style_bg_opa(tabview, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(tabview, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(tabview, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tabview, 0, LV_PART_MAIN);

    lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tabview);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(tab_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tab_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(tab_bar, lv_color_hex(0x4d4d4d), LV_PART_ITEMS);
    lv_obj_set_style_text_font(tab_bar, app_font_ui(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(tab_bar, lv_color_hex(0x2195f6), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(tab_bar, 4, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_side(tab_bar, LV_BORDER_SIDE_BOTTOM, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_border_color(tab_bar, lv_color_hex(0x2195f6), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(tab_bar, 72, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(tab_bar, lv_color_hex(0x2195f6), LV_PART_ITEMS | LV_STATE_CHECKED);
}

static void apply_tab_transparent(lv_obj_t *obj) {
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
}

static void apply_overlay_button_style(lv_obj_t *btn) {
    lv_obj_set_style_bg_opa(btn, 168, LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1b75bb), LV_PART_MAIN);
    lv_obj_set_style_border_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xffffff), LV_PART_MAIN);
}

static void set_action_button_visibility(uint32_t active_tab) {
    if (s_btn_take) {
        if (active_tab == 0U) {
            lv_obj_clear_flag(s_btn_take, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_btn_take, LV_OBJ_FLAG_HIDDEN);
        }
    }

    if (s_btn_record) {
        if (active_tab == 1U) {
            lv_obj_clear_flag(s_btn_record, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_btn_record, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void camera_shutdown(void) {
    if (s_camera) {
        (void)s_camera->run(false);
        s_camera->end();
        s_camera = NULL;
    }
}

static bool camera_boot_from_settings(void) {
    app_camera_settings_t cfg;
    framesize_t requested_frame = FRAMESIZE_QVGA;
    uint16_t requested_fps = 15;

    app_manager_get_camera_settings(&cfg);
    map_capture_profile(cfg.capture_res_index, &requested_frame, &requested_fps);
    requested_fps = map_frame_rate_index_to_fps(cfg.frame_rate_index);

    if (cfg.pix_format_index != 0U) {
        APP_LOGW("Camera: requested pix format idx=%u, runtime preview uses RGB565 path", (unsigned)cfg.pix_format_index);
    }

    Sipeed_OV2640 *candidate = &s_camera_qvga;
    if (requested_frame == FRAMESIZE_VGA) {
        candidate = &s_camera_vga;
        s_src_width = 640;
        s_src_height = 480;
    } else if (requested_frame == FRAMESIZE_QQVGA) {
        candidate = &s_camera_qqvga;
        s_src_width = 160;
        s_src_height = 120;
    } else {
        candidate = &s_camera_qvga;
        s_src_width = 320;
        s_src_height = 240;
    }

    camera_shutdown();
    s_camera = candidate;

    if (!s_camera->begin()) {
        APP_LOGE("Camera: begin failed");
        camera_shutdown();
        return false;
    }

    bool ok_fps = s_camera->setFrameRate(map_ov2640_framerate(requested_fps));
    bool ok_gc = s_camera->setGainCeiling((gainceiling_t)cfg.agc_ceiling_index);
    bool ok_agc = s_camera->setAutoGain(cfg.agc);
    bool ok_aec = s_camera->setAutoExposure(cfg.aec);
    bool ok_awb = s_camera->setAutoWhiteBalance(cfg.awb);
    bool ok_hm = s_camera->setHMirror(cfg.h_mirror);
    bool ok_vf = s_camera->setVFlip(cfg.v_flip);
    bool ok_cb = s_camera->setColorBar(cfg.color_bar);
    bool ok_b = s_camera->setBrightnessLevel(cfg.brightness_level);
    bool ok_c = s_camera->setContrastLevel(cfg.contrast_level);
    bool ok_s = s_camera->setSaturationLevel(cfg.saturation_level);

    APP_LOGI("Camera: apply settings res=%u fmt=%u fps_idx=%u gc=%u b=%d c=%d s=%d agc=%d aec=%d awb=%d cb=%d hm=%d vf=%d",
             (unsigned)cfg.capture_res_index,
             (unsigned)cfg.pix_format_index,
             (unsigned)cfg.frame_rate_index,
             (unsigned)cfg.agc_ceiling_index,
             (int)cfg.brightness_level,
             (int)cfg.contrast_level,
             (int)cfg.saturation_level,
             (int)cfg.agc,
             (int)cfg.aec,
             (int)cfg.awb,
             (int)cfg.color_bar,
             (int)cfg.h_mirror,
             (int)cfg.v_flip);
    APP_LOGI("Camera: apply result fps=%d gc=%d agc=%d aec=%d awb=%d hm=%d vf=%d cb=%d b=%d c=%d s=%d",
             (int)ok_fps,
             (int)ok_gc,
             (int)ok_agc,
             (int)ok_aec,
             (int)ok_awb,
             (int)ok_hm,
             (int)ok_vf,
             (int)ok_cb,
             (int)ok_b,
             (int)ok_c,
             (int)ok_s);

    APP_LOGW("Camera: OV2640 is fixed-focus; true autofocus is not supported in this sensor");

    /* Screen is 320x240 and there is no hardware composition layer.
     * Force preview to QVGA so capture can stay on the DVP DMA path and UI remains responsive. */
    uint16_t preview_fps = requested_fps;
    if (s_src_width == 640U) {
        if (preview_fps > 12U) preview_fps = 12U;
    } else if (s_src_width == 160U) {
        if (preview_fps > 20U) preview_fps = 20U;
    } else {
        if (preview_fps > 24U) preview_fps = 24U;
    }
    if (preview_fps < 8U) {
        preview_fps = 8U;
    }
    s_preview_period_ms = (uint16_t)(1000U / preview_fps);
    if (s_preview_period_ms < 66U) {
        s_preview_period_ms = 66U;
    }

    APP_LOGI("Camera: preview init ok src=%ux%u res_idx=%u fps_idx=%u fps_req=%u fps_preview=%u",
             (unsigned)s_src_width,
             (unsigned)s_src_height,
             (unsigned)cfg.capture_res_index,
             (unsigned)cfg.frame_rate_index,
             (unsigned)requested_fps,
             (unsigned)(1000U / s_preview_period_ms));
    return true;
}

static void set_preview_running(bool enable) {
    app_camera_state_t state;
    app_manager_get_camera_state(&state);
    if (state.preview_running != enable) {
        (void)app_manager_camera_toggle_preview();
    }
    if (s_camera) {
        (void)s_camera->run(enable);
    }
}

static void publish_preview_buffer(uint8_t buffer_index) {
    s_preview_ready_index = buffer_index;
    s_preview_ready_seq++;
    s_preview_last_frame_ms = millis();
    if (!s_preview_first_frame_logged) {
        s_preview_first_frame_logged = true;
        APP_LOGI("Camera: first frame ready");
    }
}

static void copy_qvga_frame_be_to_le(const uint8_t *src, uint16_t *dst) {
    if (!src || !dst) {
        return;
    }

    if ((((uintptr_t)src | (uintptr_t)dst) & 0x3U) == 0U) {
        const uint32_t *src32 = (const uint32_t *)src;
        uint32_t *dst32 = (uint32_t *)dst;
        for (uint32_t i = 0; i < (k_preview_pixels / 2U); i++) {
            dst32[i] = __builtin_bswap32(src32[i]);
        }
        return;
    }

    for (uint32_t i = 0; i < k_preview_pixels; i++) {
        uint16_t hi = src[i * 2U];
        uint16_t lo = src[i * 2U + 1U];
        dst[i] = (uint16_t)((hi << 8) | lo);
    }
}

static void upscale_qqvga_be_to_qvga(const uint8_t *src, uint16_t *dst) {
    if (!src || !dst) {
        return;
    }

    for (uint16_t y = 0; y < 120U; y++) {
        uint32_t row0 = (uint32_t)(y * 2U) * 320U;
        uint32_t row1 = row0 + 320U;
        for (uint16_t x = 0; x < 160U; x++) {
            uint32_t si = ((uint32_t)y * 160U + x) * 2U;
            uint16_t hi = src[si];
            uint16_t lo = src[si + 1U];
            uint16_t px = (uint16_t)((hi << 8) | lo);
            uint32_t dx = (uint32_t)x * 2U;
            dst[row0 + dx] = px;
            dst[row0 + dx + 1U] = px;
            dst[row1 + dx] = px;
            dst[row1 + dx + 1U] = px;
        }
    }
}

static void downscale_vga_be_to_qvga(const uint8_t *src, uint16_t *dst) {
    if (!src || !dst) {
        return;
    }

    for (uint16_t y = 0; y < 240U; y++) {
        uint32_t sy = (uint32_t)y * 2U;
        uint32_t drow = (uint32_t)y * 320U;
        for (uint16_t x = 0; x < 320U; x++) {
            uint32_t sx = (uint32_t)x * 2U;
            uint32_t si = (sy * 640U + sx) * 2U;
            uint16_t hi = src[si];
            uint16_t lo = src[si + 1U];
            dst[drow + x] = (uint16_t)((hi << 8) | lo);
        }
    }
}

static void preview_task_entry(void *arg) {
    LV_UNUSED(arg);

    TickType_t last_wake = xTaskGetTickCount();
    uint32_t frame_count = 0;
    uint32_t stat_t0 = millis();

    s_preview_task_alive = true;
    APP_LOGI("Camera: preview task started");

    for (;;) {
        if (s_preview_stop_requested) {
            break;
        }

        if (!s_camera) {
            vTaskDelay(pdMS_TO_TICKS(20));
            last_wake = xTaskGetTickCount();
            continue;
        }

        uint8_t *raw = s_camera->snapshot();
        if (s_preview_stop_requested) {
            break;
        }
        if (!raw) {
            APP_LOGW("Camera: snapshot timeout");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        uint8_t back_index = (uint8_t)(1U - s_preview_front_index);
        if (s_src_width == 160U && s_src_height == 120U) {
            upscale_qqvga_be_to_qvga(raw, s_preview_bufs[back_index]);
        } else if (s_src_width == 640U && s_src_height == 480U) {
            downscale_vga_be_to_qvga(raw, s_preview_bufs[back_index]);
        } else {
            copy_qvga_frame_be_to_le(raw, s_preview_bufs[back_index]);
        }
        publish_preview_buffer(back_index);

        frame_count++;
        if (millis() - stat_t0 >= 2000U) {
            APP_LOGI("Camera: preview worker fps=%u", (unsigned)(frame_count / 2U));
            frame_count = 0;
            stat_t0 = millis();
        }

        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(s_preview_period_ms));
    }

    s_preview_task_alive = false;
    s_preview_task = NULL;
    APP_LOGI("Camera: preview task stopped");
    vTaskDelete(NULL);
}

static bool preview_task_start(void) {
    if (s_preview_task) {
        return true;
    }

    s_preview_stop_requested = false;
    s_preview_ready_seq = 0;
    s_preview_displayed_seq = 0;
    s_preview_front_index = 0;
    s_preview_ready_index = 0;
    s_preview_last_frame_ms = millis();
    s_preview_first_frame_logged = false;

    BaseType_t ok = xTaskCreate(preview_task_entry,
                                "cam_prev",
                                4096,
                                NULL,
                                tskIDLE_PRIORITY + 1,
                                &s_preview_task);
    if (ok != pdPASS) {
        s_preview_task = NULL;
        APP_LOGE("Camera: preview task create failed");
        return false;
    }

    return true;
}

static void preview_task_stop(void) {
    s_preview_stop_requested = true;

    for (uint8_t i = 0; i < 20 && s_preview_task_alive; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    if (s_preview_task) {
        APP_LOGW("Camera: forcing preview task delete");
        vTaskDelete(s_preview_task);
        s_preview_task = NULL;
        s_preview_task_alive = false;
    }
}

static void refresh_camera_ui(void) {
    app_datetime_t dt;
    app_manager_get_datetime(&dt);

    if (s_clock_label) {
        char clock_buf[16];
        lv_snprintf(clock_buf, sizeof(clock_buf), "%d:%02d", (int)dt.hour, (int)dt.minute);
        lv_label_set_text(s_clock_label, clock_buf);
    }

    app_camera_state_t state;
    app_manager_get_camera_state(&state);

    if (s_btn_record_label) {
        lv_label_set_text(s_btn_record_label, state.recording ? LV_SYMBOL_STOP " Stop" : LV_SYMBOL_PLAY " Rec");
    }

    if (s_record_time) {
        uint32_t sec = state.record_seconds;
        uint32_t h = sec / 3600U;
        uint32_t m = (sec % 3600U) / 60U;
        uint32_t s = sec % 60U;
        lv_label_set_text_fmt(s_record_time, "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    }

    if (s_btn_take) {
        if (state.preview_running) {
            lv_obj_clear_state(s_btn_take, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_btn_take, LV_STATE_DISABLED);
        }
    }
}

static void preview_present_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);

    if (!s_preview_image) {
        return;
    }

    uint32_t ready_seq = s_preview_ready_seq;
    if (ready_seq == s_preview_displayed_seq) {
        if (!s_preview_task_alive && s_camera) {
            uint32_t now = millis();
            if ((now - s_preview_last_frame_ms) >= 120U) {
                uint8_t *raw = s_camera->snapshot();
                if (raw) {
                    if (s_src_width == 160U && s_src_height == 120U) {
                        upscale_qqvga_be_to_qvga(raw, s_preview_bufs[s_preview_front_index]);
                    } else if (s_src_width == 640U && s_src_height == 480U) {
                        downscale_vga_be_to_qvga(raw, s_preview_bufs[s_preview_front_index]);
                    } else {
                        copy_qvga_frame_be_to_le(raw, s_preview_bufs[s_preview_front_index]);
                    }
                    s_preview_last_frame_ms = now;
                    lv_image_set_src(s_preview_image, &s_preview_dscs[s_preview_front_index]);
                    lv_obj_invalidate(s_preview_image);
                }
            }
        }
        return;
    }

    s_preview_front_index = s_preview_ready_index;
    lv_image_set_src(s_preview_image, &s_preview_dscs[s_preview_front_index]);
    s_preview_displayed_seq = ready_seq;
    lv_obj_invalidate(s_preview_image);
}

static void camera_timer_cb(lv_timer_t *timer) {
    LV_UNUSED(timer);
    refresh_camera_ui();
}

static void back_home_cb(lv_event_t *event) {
    LV_UNUSED(event);
    APP_LOGI("Camera: Home button clicked, navigate to HOME");
    app_manager_navigate_to(SCREEN_ID_HOME);
}

static void take_snapshot_cb(lv_event_t *event) {
    LV_UNUSED(event);
    char saved_path[64];
    bool ok = save_snapshot_to_sd(saved_path, sizeof(saved_path));
    if (ok) {
        (void)app_manager_camera_take_snapshot();
    }
    APP_LOGI("Camera: snapshot result=%d path=%s", (int)ok, ok ? saved_path : "-");
}

static void record_toggle_cb(lv_event_t *event) {
    LV_UNUSED(event);
    app_camera_state_t state;
    app_manager_get_camera_state(&state);
    if (!state.preview_running) {
        set_preview_running(true);
    }
    bool recording = app_manager_camera_toggle_record();
    APP_LOGI("Camera: record toggled, recording=%d", (int)recording);
    refresh_camera_ui();
}

static void tabview_changed_cb(lv_event_t *event) {
    lv_obj_t *tabview = lv_event_get_target_obj(event);
    set_action_button_visibility(lv_tabview_get_tab_active(tabview));
}

static void screen_delete_cb(lv_event_t *event) {
    LV_UNUSED(event);
    APP_LOGI("Camera: screen delete start");

    if (s_present_timer) {
        lv_timer_delete(s_present_timer);
        s_present_timer = NULL;
    }

    if (s_refresh_timer) {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }

    set_preview_running(false);
    preview_task_stop();
    camera_shutdown();

    s_clock_label = NULL;
    s_btn_take = NULL;
    s_btn_take_label = NULL;
    s_btn_record = NULL;
    s_btn_record_label = NULL;
    s_record_time = NULL;
    s_preview_image = NULL;
    s_tabview = NULL;

    APP_LOGI("Camera: screen delete done");
}

lv_obj_t *screen_camera_create(void) {
    APP_LOGI("Camera: create start (entered from Home > Application > Camera)");

    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, 320, 240);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_text_font(scr, app_font_ui(), LV_PART_MAIN);
    lv_obj_add_event_cb(scr, screen_delete_cb, LV_EVENT_DELETE, NULL);

    init_preview_placeholder();
    for (uint8_t i = 0; i < 2; i++) {
        s_preview_dscs[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_preview_dscs[i].header.cf = LV_COLOR_FORMAT_RGB565;
        s_preview_dscs[i].header.stride = 320 * 2;
        s_preview_dscs[i].header.flags = 0;
        s_preview_dscs[i].header.w = 320;
        s_preview_dscs[i].header.h = 240;
        s_preview_dscs[i].data_size = sizeof(s_preview_bufs[0]);
        s_preview_dscs[i].data = (const uint8_t *)s_preview_bufs[i];
    }

    s_preview_image = lv_image_create(scr);
    lv_obj_set_pos(s_preview_image, 0, 0);
    lv_obj_set_size(s_preview_image, 320, 240);
    lv_image_set_src(s_preview_image, &s_preview_dscs[0]);
    lv_obj_move_background(s_preview_image);

    s_tabview = lv_tabview_create(scr);
    lv_obj_set_pos(s_tabview, 0, 0);
    lv_obj_set_size(s_tabview, 320, 210);
    lv_obj_clear_flag(s_tabview, LV_OBJ_FLAG_SCROLLABLE);
    lv_tabview_set_tab_bar_position(s_tabview, LV_DIR_TOP);
    lv_tabview_set_tab_bar_size(s_tabview, 30);
    apply_tabview_style(s_tabview);
    apply_tab_transparent(lv_tabview_get_content(s_tabview));
    lv_obj_add_event_cb(s_tabview, tabview_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *tab_snap = lv_tabview_add_tab(s_tabview, "Snap");
    lv_obj_t *tab_record = lv_tabview_add_tab(s_tabview, "Record");
    apply_tab_transparent(tab_snap);
    apply_tab_transparent(tab_record);

    lv_obj_t *snap_hint = lv_label_create(tab_snap);
    lv_label_set_text(snap_hint, "Live preview");
    lv_obj_set_pos(snap_hint, 12, 42);
    lv_obj_set_style_text_color(snap_hint, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(snap_hint, 80, LV_PART_MAIN);
    lv_obj_set_style_bg_color(snap_hint, lv_color_hex(0x000000), LV_PART_MAIN);

    s_record_time = lv_label_create(tab_record);
    lv_obj_set_pos(s_record_time, 12, 42);
    lv_obj_set_size(s_record_time, 110, 18);
    lv_obj_set_style_text_align(s_record_time, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_record_time, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_record_time, 96, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_record_time, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_label_set_text(s_record_time, "00:00:00");

    lv_obj_t *btn_back = lv_button_create(scr);
    lv_obj_set_pos(btn_back, 0, 210);
    lv_obj_set_size(btn_back, 90, 30);
    apply_overlay_button_style(btn_back);
    lv_obj_add_event_cb(btn_back, back_home_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_back_label = lv_label_create(btn_back);
    lv_label_set_text(btn_back_label, LV_SYMBOL_HOME " Home");
    lv_obj_center(btn_back_label);

    s_btn_take = lv_button_create(scr);
    lv_obj_set_pos(s_btn_take, 230, 210);
    lv_obj_set_size(s_btn_take, 90, 30);
    apply_overlay_button_style(s_btn_take);
    lv_obj_add_event_cb(s_btn_take, take_snapshot_cb, LV_EVENT_CLICKED, NULL);
    s_btn_take_label = lv_label_create(s_btn_take);
    lv_label_set_text(s_btn_take_label, LV_SYMBOL_IMAGE " Snap");
    lv_obj_center(s_btn_take_label);

    s_btn_record = lv_button_create(scr);
    lv_obj_set_pos(s_btn_record, 230, 210);
    lv_obj_set_size(s_btn_record, 90, 30);
    apply_overlay_button_style(s_btn_record);
    lv_obj_add_event_cb(s_btn_record, record_toggle_cb, LV_EVENT_CLICKED, NULL);
    s_btn_record_label = lv_label_create(s_btn_record);
    lv_label_set_text(s_btn_record_label, LV_SYMBOL_PLAY " Rec");
    lv_obj_center(s_btn_record_label);

    s_clock_label = lv_label_create(scr);
    lv_obj_set_pos(s_clock_label, 252, 6);
    lv_obj_set_size(s_clock_label, 62, 18);
    lv_obj_set_style_text_align(s_clock_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_clock_label, lv_color_hex(0xffffff), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_clock_label, 96, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_clock_label, lv_color_hex(0x000000), LV_PART_MAIN);

    lv_obj_move_foreground(s_tabview);
    lv_obj_move_foreground(btn_back);
    lv_obj_move_foreground(s_btn_take);
    lv_obj_move_foreground(s_btn_record);
    lv_obj_move_foreground(s_clock_label);

    set_action_button_visibility(0U);

    bool camera_ok = camera_boot_from_settings();
    bool worker_ok = false;
    if (camera_ok) {
        set_preview_running(true);
        worker_ok = preview_task_start();
        if (!worker_ok) {
            APP_LOGW("Camera: preview worker unavailable, fallback capture enabled");
        }
    }

    if (!camera_ok) {
        set_preview_running(false);
    }

    s_present_timer = lv_timer_create(preview_present_timer_cb, 15, NULL);
    s_refresh_timer = lv_timer_create(camera_timer_cb, 1000, NULL);
    refresh_camera_ui();

    APP_LOGI("Camera: create done");
    return scr;
}