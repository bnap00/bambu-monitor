#pragma once
// LVGL heap hooks: keep widget memory in PSRAM so internal RAM stays free for TLS.
#include <stddef.h>
#include <esp_heap_caps.h>

static inline void *lv_psram_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static inline void lv_psram_free(void *p)
{
    heap_caps_free(p);
}

static inline void *lv_psram_realloc(void *p, size_t size)
{
    return heap_caps_realloc(p, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}
