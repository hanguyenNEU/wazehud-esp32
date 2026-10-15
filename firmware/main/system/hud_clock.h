#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

/* Đồng hồ — sync từ HLP `hi.unix` + `hi.tz` (§5).
 *
 *   - hud_clock_sync(): gọi từ protocol task — CHỈ copy vào mailbox (unix,
 *     tz, monotonic anchor lúc nhận) để không cộng sai thời gian queue.
 *   - hud_clock_poll(): gọi từ LVGL tick — áp sync pending. Validate:
 *     unix hữu hạn trong khoảng hợp lý; tz trong -12h..+14h.
 *   - Chưa sync: get_local() trả false → UI hiện "--:--:--".
 *   - Mất BLE vẫn đếm tiếp bằng monotonic; không ghi NVS mỗi giây, không
 *     hứa chính xác như RTC có pin. */

void hud_clock_sync(int64_t unix_seconds, int tz_minutes);
void hud_clock_poll(void);

bool hud_clock_get_local(struct tm *out);
bool hud_clock_synced(void);
int hud_clock_tz_minutes(void);
/* unix hiện tại theo monotonic — cho lịch âm (UTC instant). */
int64_t hud_clock_unix_now(void);

/* Pure helper (không đụng state) cho host test + simulator. */
bool hud_clock_compute(int64_t unix_anchor, int64_t anchor_us, int tz_minutes,
                       int64_t now_us, struct tm *out);

/* Test hook: thay nguồn monotonic (host test không có esp_timer). */
void hud_clock_set_now_fn(int64_t (*now_fn)(void));
