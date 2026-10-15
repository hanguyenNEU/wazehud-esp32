#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Trang Wi-Fi — chức năng hiện có của ui_navigator.c chuyển sang đây (§2.1, §7).
 *
 * Mô hình (scan entries, trạng thái quét, trạng thái liên kết, SSID đang
 * chọn) nằm NGOÀI widget tree; main.c đẩy dữ liệu qua các setter. Scan list
 * mới tới trong lúc người dùng đang press/scroll được giữ pending và áp sau
 * release — không ghi đè danh sách giữa lúc chạm (§7). */

#define UI_WIFI_MAX_SCAN 20
typedef struct {
    char ssid[33];
    int8_t rssi;
    bool open; /* không mật khẩu: chọn là lưu luôn */
} ui_wifi_scan_entry_t;
typedef void (*ui_wifi_save_cb_t)(const char *ssid, const char *password, void *user);
typedef void (*ui_wifi_scan_cb_t)(void *user);
typedef void (*ui_wifi_phone_cb_t)(const char *ssid_hint, void *user);

void ui_wifi_page_enter(lv_obj_t *parent);
void ui_wifi_page_exit(void);

void ui_wifi_set_callbacks(ui_wifi_save_cb_t save_cb, ui_wifi_scan_cb_t scan_cb, void *user);
void ui_wifi_set_phone_callback(ui_wifi_phone_cb_t phone_cb, void *user);

/* Trạng thái LIÊN KẾT — độc lập với trạng thái quét (§7). */
void ui_wifi_set_status(const char *status);
/* Trạng thái QUÉT: NULL/"": không quét; khác: hiện dòng này (vd "Đang quét..."). */
void ui_wifi_set_scan_status(const char *status);
void ui_wifi_set_scan_list(const ui_wifi_scan_entry_t *entries, size_t count);
