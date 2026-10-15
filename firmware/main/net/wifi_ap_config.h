#pragma once

#include "sdkconfig.h"

/* Phát 1 SoftAP riêng + phục vụ 1 trang HTML tại địa chỉ AP mặc định để
   người dùng điền SSID/mật khẩu WiFi từ trình duyệt điện thoại — thay cho
   bàn phím ảo trên màn cảm ứng nhỏ (không đáng tin cậy, xem lịch sử debug
   trong ui/touch_es3c28p.c). Mirror theo đúng cách net/ota.c đã làm để phát
   AP cho cập nhật firmware — cùng 1 kiểu SoftAP+httpd đã chạy tốt trên
   phần cứng này. */

#define WIFI_CFG_AP_SSID "WazeHUD-WiFi"
#define WIFI_CFG_AP_PASS CONFIG_HUD_AP_PASSWORD /* WPA2 cần tối thiểu 8 ký tự */
#define WIFI_CFG_AP_IP "192.168.4.1"

typedef enum {
    WIFI_CFG_STARTING = 0,
    WIFI_CFG_AP_READY,
    WIFI_CFG_CLIENT,
    WIFI_CFG_SAVED,
} wifi_cfg_status_t;

typedef void (*wifi_cfg_progress_cb_t)(wifi_cfg_status_t status);

/* ssid_hint: điền sẵn vào ô SSID trên trang (rỗng/NULL nếu chưa chọn mạng
   nào trên màn HUD) — người dùng vẫn sửa được. Sau khi lưu, thiết bị tự
   esp_restart() để wifi_manager_init() nạp lại STA bình thường. */
void wifi_ap_config_start(const char *ssid_hint, wifi_cfg_progress_cb_t cb);
