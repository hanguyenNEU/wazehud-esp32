#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define WIFI_MANAGER_MAX_SLOTS 3
#define WIFI_MANAGER_MAX_SCAN 20
#define WIFI_MANAGER_SSID_MAX 32

typedef enum {
    WIFI_MANAGER_IDLE = 0,
    WIFI_MANAGER_CONNECTING,
    WIFI_MANAGER_CONNECTED,
    WIFI_MANAGER_DISCONNECTED,
    WIFI_MANAGER_SCANNING,
    WIFI_MANAGER_ERROR,
} wifi_manager_status_t;

typedef struct {
    char ssid[WIFI_MANAGER_SSID_MAX + 1];
    int8_t rssi;
    bool open; /* không mật khẩu */
} wifi_manager_scan_entry_t;

typedef void (*wifi_manager_status_cb_t)(wifi_manager_status_t status, void *user);
typedef void (*wifi_manager_scan_cb_t)(const wifi_manager_scan_entry_t *entries,
                                       size_t count, void *user);

void wifi_manager_set_callbacks(wifi_manager_status_cb_t status_cb,
                                wifi_manager_scan_cb_t scan_cb, void *user);
void wifi_manager_init(void);
void wifi_manager_save_and_connect(const char *ssid, const char *password);
/* Chỉ ghi vào NVS, không đụng tới esp_wifi — dùng khi wifi đang ở chế độ
   AP-only (đã stop_for_ota) nên set_config/connect() sẽ không hợp lệ; bên
   gọi tự esp_restart() sau đó để wifi_manager_init() nạp lại bình thường. */
void wifi_manager_save_credentials(const char *ssid, const char *password);
/* Trả ESP_OK khi scan đã khởi động; lỗi kết thúc busy và GIỮ link state. */
esp_err_t wifi_manager_scan(void);
void wifi_manager_stop_for_ota(void);

/* Link state tách khỏi scan state (§7): GOT_IP đặt, DISCONNECTED xóa;
   scan start/done không đổi 2 giá trị này. */
bool wifi_manager_is_connected(void);
bool wifi_manager_scan_in_progress(void);
