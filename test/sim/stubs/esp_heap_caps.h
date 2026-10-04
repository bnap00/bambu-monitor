#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
static inline void *heap_caps_malloc(size_t s, int) { return malloc(s); }
static inline void heap_caps_free(void *p) { free(p); }
static inline void *heap_caps_realloc(void *p, size_t s, int) { return realloc(p, s); }
