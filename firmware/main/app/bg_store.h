#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Ảnh nền tự chọn cho màn Đồng hồ — 1 ô duy nhất trong phân vùng `storage`
 * (1 MB, trước đây bỏ trống). Ảnh mới ghi đè ảnh cũ; ảnh mặc định (động)
 * vẫn nhúng trong firmware.
 *
 * Bố cục phân vùng:
 *   0x0000  header {magic "CDB1", w, h, size, crc32 của pixel}
 *   0x1000  pixel RGB565 320×240 (thứ tự byte của CPU, như anim_bg)
 * Ghi: xoá header → ghi pixel → ghi header SAU CÙNG. Mất điện/mất BLE giữa
 * chừng thì header không hợp lệ → coi như không có ảnh, không hiện ảnh rác.
 *
 * Nhận từ app CarDash qua characteristic CARDASH_BG (hlp_ble.c), JPEG đúng
 * 320×240 do app/web đã cắt + nén sẵn. Mỗi lần ghi BLE mở đầu bằng 1 byte lệnh:
 *   0x01 BEGIN  u32 tổng số byte JPEG
 *   0x02 DATA   u32 offset, rồi dữ liệu (offset phải nối tiếp, lệch là huỷ)
 *   0x03 END    u32 crc32 (zlib) của cả file JPEG
 * Kết quả báo lại app bằng notify trên cùng characteristic: "ok" hoặc
 * "err:<lý do>". Số nguyên little-endian. */

#define BG_W 320
#define BG_H 240

void bg_store_init(void);

/* Có ảnh tự chọn hợp lệ trong flash không. */
bool bg_store_has_custom(void);

/* Đọc ảnh vào dst (BG_W*BG_H uint16_t). false nếu không có/hỏng. */
bool bg_store_load(uint16_t *dst);

/* Task NimBLE: 1 lần ghi BLE. Giải mã + ghi flash chạy ở task riêng. */
typedef void (*bg_store_reply_cb_t)(const char *msg);
void bg_store_feed(const uint8_t *data, size_t length, bg_store_reply_cb_t reply);

/* LVGL tick: true một lần sau mỗi lần lưu ảnh mới thành công. */
bool bg_store_take_updated(void);
