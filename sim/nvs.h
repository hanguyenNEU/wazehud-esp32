#pragma once
#include <stddef.h>

/* Simulator only: device NVS is unavailable and no credentials are persisted. */
typedef int nvs_handle_t;
#define ESP_OK 0
#define NVS_READONLY 0
#define NVS_READWRITE 1
static inline int nvs_open(const char *name, int mode, nvs_handle_t *handle)
{ (void)name; (void)mode; (void)handle; return -1; }
static inline int nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *size)
{ (void)h; (void)key; (void)out; (void)size; return -1; }
static inline int nvs_set_str(nvs_handle_t h, const char *key, const char *value)
{ (void)h; (void)key; (void)value; return -1; }
static inline int nvs_commit(nvs_handle_t h) { (void)h; return -1; }
static inline void nvs_close(nvs_handle_t h) { (void)h; }
