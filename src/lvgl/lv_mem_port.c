#include <stdint.h>
#include <string.h>

#include "kendryte-standalone-sdk/lib/freertos/include/FreeRTOS.h"
#include "kendryte-standalone-sdk/lib/freertos/include/task.h"
#include <lvgl.h>

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

typedef struct {
    size_t size;
    uint32_t magic;
} lv_port_block_hdr_t;

#define LV_PORT_MEM_MAGIC 0x4C564D45u

static inline lv_port_block_hdr_t *hdr_from_ptr(void *p) {
    return ((lv_port_block_hdr_t *)p) - 1;
}

void lv_mem_init(void) {
}

void lv_mem_deinit(void) {
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) {
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool) {
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size) {
    size_t total = sizeof(lv_port_block_hdr_t) + size;
    lv_port_block_hdr_t *hdr = (lv_port_block_hdr_t *)pvPortMalloc(total);
    if (hdr == NULL) {
        return NULL;
    }

    hdr->size = size;
    hdr->magic = LV_PORT_MEM_MAGIC;
    return (void *)(hdr + 1);
}

void *lv_realloc_core(void *p, size_t new_size) {
    if (p == NULL) {
        return lv_malloc_core(new_size);
    }

    lv_port_block_hdr_t *old_hdr = hdr_from_ptr(p);
    if (old_hdr->magic != LV_PORT_MEM_MAGIC) {
        return NULL;
    }

    void *new_p = lv_malloc_core(new_size);
    if (new_p == NULL) {
        return NULL;
    }

    size_t copy_size = old_hdr->size < new_size ? old_hdr->size : new_size;
    if (copy_size > 0) {
        memcpy(new_p, p, copy_size);
    }

    vPortFree(old_hdr);
    return new_p;
}

void lv_free_core(void *p) {
    if (p == NULL) {
        return;
    }

    lv_port_block_hdr_t *hdr = hdr_from_ptr(p);
    if (hdr->magic != LV_PORT_MEM_MAGIC) {
        return;
    }

    hdr->magic = 0;
    vPortFree(hdr);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon_p) {
    size_t total = (size_t)configTOTAL_HEAP_SIZE;
    size_t free_size = xPortGetFreeHeapSize();
    size_t used_size = total > free_size ? total - free_size : 0;

    mon_p->total_size = total;
    mon_p->free_size = free_size;
    mon_p->free_biggest_size = free_size;
    mon_p->used_cnt = 0;
    mon_p->free_cnt = 0;
    mon_p->max_used = 0;
    mon_p->used_pct = total ? (uint8_t)((used_size * 100U) / total) : 0;
    mon_p->frag_pct = 0;
}

lv_result_t lv_mem_test_core(void) {
    void *p = lv_malloc_core(64);
    if (p == NULL) {
        return LV_RESULT_INVALID;
    }

    lv_free_core(p);
    return LV_RESULT_OK;
}

#endif
