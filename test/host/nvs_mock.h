/* NVS mock trong RAM cho host test — cùng chữ ký với nvs.h của ESP-IDF,
 * có thêm failure injection (nvs_mock_fail_next) để test lỗi ghi. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NVS_NOT_FOUND 0x1102

typedef int nvs_handle_t;
typedef int nvs_open_mode_t;
#define NVS_READONLY 1
#define NVS_READWRITE 2

typedef enum { MK_I32, MK_U8, MK_U32, MK_STR } mock_kind_t;

typedef struct {
    char ns[16];
    char key[16];
    mock_kind_t kind;
    int32_t i32;
    uint8_t u8;
    uint32_t u32;
    char str[16];
} mock_entry_t;

static mock_entry_t mock_store[64];
static int mock_count;
static bool nvs_mock_fail_next; /* vụ ghi/commit kế tiếp trả lỗi */

static void nvs_mock_reset(void)
{
    mock_count = 0;
    nvs_mock_fail_next = false;
}

static mock_entry_t *mock_find(const char *ns, const char *key)
{
    for (int i = 0; i < mock_count; ++i)
        if (!strcmp(mock_store[i].ns, ns) && !strcmp(mock_store[i].key, key))
            return &mock_store[i];
    return NULL;
}

static esp_err_t nvs_open(const char *ns, nvs_open_mode_t mode, nvs_handle_t *out)
{
    (void)ns;
    (void)mode;
    *out = 1;
    return ESP_OK;
}

static esp_err_t nvs_get_i32(nvs_handle_t h, const char *key, int32_t *out)
{
    (void)h;
    mock_entry_t *e = mock_find("hud_cfg", key);
    if (!e || e->kind != MK_I32) return ESP_ERR_NVS_NOT_FOUND;
    *out = e->i32;
    return ESP_OK;
}

static esp_err_t nvs_get_u8(nvs_handle_t h, const char *key, uint8_t *out)
{
    (void)h;
    mock_entry_t *e = mock_find("hud_cfg", key);
    if (!e || e->kind != MK_U8) return ESP_ERR_NVS_NOT_FOUND;
    *out = e->u8;
    return ESP_OK;
}

static esp_err_t nvs_get_u32(nvs_handle_t h, const char *key, uint32_t *out)
{
    (void)h;
    mock_entry_t *e = mock_find("hud_cfg", key);
    if (!e || e->kind != MK_U32) return ESP_ERR_NVS_NOT_FOUND;
    *out = e->u32;
    return ESP_OK;
}

static esp_err_t nvs_get_str(nvs_handle_t h, const char *key, char *out, size_t *len)
{
    (void)h;
    mock_entry_t *e = mock_find("hud_cfg", key);
    if (!e || e->kind != MK_STR) return ESP_ERR_NVS_NOT_FOUND;
    size_t n = strlen(e->str);
    if (n >= *len) n = *len - 1;
    memcpy(out, e->str, n);
    out[n] = '\0';
    *len = n;
    return ESP_OK;
}

static bool mock_fail_once(void)
{
    if (!nvs_mock_fail_next) return false;
    nvs_mock_fail_next = false;
    return true;
}

static esp_err_t nvs_set_i32(nvs_handle_t h, const char *key, int32_t value)
{
    (void)h;
    if (mock_fail_once()) return 0x101;
    mock_entry_t *e = mock_find("hud_cfg", key);
    if (!e) { e = &mock_store[mock_count++]; strcpy(e->ns, "hud_cfg"); strcpy(e->key, key); }
    e->kind = MK_I32; e->i32 = value;
    return ESP_OK;
}

static esp_err_t nvs_set_u8(nvs_handle_t h, const char *key, uint8_t value)
{
    (void)h;
    if (mock_fail_once()) return 0x101;
    mock_entry_t *e = mock_find("hud_cfg", key);
    if (!e) { e = &mock_store[mock_count++]; strcpy(e->ns, "hud_cfg"); strcpy(e->key, key); }
    e->kind = MK_U8; e->u8 = value;
    return ESP_OK;
}

static esp_err_t nvs_set_u32(nvs_handle_t h, const char *key, uint32_t value)
{
    (void)h;
    if (mock_fail_once()) return 0x101;
    mock_entry_t *e = mock_find("hud_cfg", key);
    if (!e) { e = &mock_store[mock_count++]; strcpy(e->ns, "hud_cfg"); strcpy(e->key, key); }
    e->kind = MK_U32; e->u32 = value;
    return ESP_OK;
}

static esp_err_t nvs_set_str(nvs_handle_t h, const char *key, const char *value)
{
    (void)h;
    if (mock_fail_once()) return 0x101;
    mock_entry_t *e = mock_find("hud_cfg", key);
    if (!e) { e = &mock_store[mock_count++]; strcpy(e->ns, "hud_cfg"); strcpy(e->key, key); }
    e->kind = MK_STR;
    snprintf(e->str, sizeof(e->str), "%s", value);
    return ESP_OK;
}

static esp_err_t nvs_commit(nvs_handle_t h)
{
    (void)h;
    if (mock_fail_once()) return 0x101;
    return ESP_OK;
}

static void nvs_close(nvs_handle_t h) { (void)h; }
