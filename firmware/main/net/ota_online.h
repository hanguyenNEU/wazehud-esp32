#pragma once

#include "esp_err.h"
#include <stdbool.h>

/* Cập nhật firmware qua Internet (WiFi STA đã nối).
 *
 * Server chỉ là file tĩnh trên trang WazeHUD:
 *   ota.json = {"version":"0.2.0","url":"https://…/WazeHUD-fw-v0.2.0-ota.bin",
 *               "notes":"…"}
 * `url` là ảnh APP (bản -ota.bin), không phải bản full ghi ở 0x0. Ghi vào
 * vùng OTA còn lại rồi khởi động lại; bản mới lỗi thì bootloader tự lùi
 * (ota_mark_valid() chỉ xác nhận sau khi chạy ổn định). */

/* Cấu hình qua menuconfig (main/Kconfig.projbuild); rỗng = tắt tính năng. */
#include "sdkconfig.h"
#define OTA_MANIFEST_URL CONFIG_HUD_OTA_MANIFEST_URL
#define OTA_ONLINE_ENABLED (OTA_MANIFEST_URL[0] != '\0')

typedef struct {
    bool newer;          /* bản trên server mới hơn bản đang chạy */
    char version[16];
    char url[192];
    char notes[96];
} ota_online_info_t;

/* Tải ota.json và so phiên bản. Chặn tới khi xong (vài giây) — gọi từ task
   riêng, không gọi trong LVGL task. */
esp_err_t ota_online_check(ota_online_info_t *out);

typedef void (*ota_online_progress_cb_t)(int percent);

/* Tải và ghi firmware từ `url`. Thành công thì khởi động lại, không trả về;
   trả về nghĩa là lỗi (vùng đang chạy không bị đụng tới). */
esp_err_t ota_online_install(const char *url, ota_online_progress_cb_t cb);
