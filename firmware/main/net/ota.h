#pragma once

#include <stdbool.h>
#include "sdkconfig.h"

/* Cập nhật firmware qua WiFi.
 *
 * Thiết bị tự phát một SoftAP rồi mở trang web nhận file .bin — không cần
 * server, không cần chứng chỉ TLS, không cần Internet. Hợp với bối cảnh thật:
 * HUD gắn cố định trên taplo, khi cần cập nhật thì đỗ xe, mở điện thoại, nối
 * vào WiFi của chính nó và kéo file lên.
 */

#define OTA_AP_SSID "WazeHUD-OTA"
#define OTA_AP_PASS CONFIG_HUD_AP_PASSWORD /* WPA2 cần tối thiểu 8 ký tự */
#define OTA_AP_IP   "192.168.4.1"

typedef enum {
    OTA_IDLE = 0,
    OTA_AP_READY,   /* đã phát WiFi, đang chờ ai đó nối vào */
    OTA_CLIENT,     /* có thiết bị nối vào AP */
    OTA_RECEIVING,  /* đang nhận firmware */
    OTA_DONE,       /* xong, sắp khởi động lại */
    OTA_FAILED,
} ota_status_t;

typedef void (*ota_progress_cb_t)(ota_status_t status, int percent,
                                  const char *detail);

/* Bật chế độ cập nhật. Không quay lại được — muốn thoát phải khởi động lại. */
void ota_start_portal(ota_progress_cb_t cb);

/* Xác nhận bản firmware đang chạy là tốt, huỷ lịch tự lùi bản.
 * Gọi sau khi đã chạy ổn định, không gọi ngay lúc khởi động. */
void ota_mark_valid(void);
