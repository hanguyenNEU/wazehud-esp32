#pragma once
/* Stub cho simulator: PSRAM = malloc thường. */
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define MALLOC_CAP_INTERNAL 0
static inline void *heap_caps_malloc(size_t size, unsigned caps) { (void)caps; return malloc(size); }
static inline void heap_caps_free(void *p) { free(p); }
