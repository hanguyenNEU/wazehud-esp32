#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Thống kê chuyến đi từ tweak WazeHUD (WHTripLog) qua BLE, characteristic
 * 8a7e0012-… (CARDASH_MSG trong hlp_ble.c). Tweak tự tính trên iPhone từ GPS
 * của Waze; một bản tin JSON có thể dài hơn 1 lần ghi BLE nên chia nhiều
 * mảnh: mảnh đầu bắt đầu bằng byte 0x02, mảnh cuối kết thúc '\n'. Ví dụ:
 *   {"t":"trip","m":10,"lm":9,"today":[68.6,115],"yday":[12.3,30],
 *    "month":[638.7,1087],"prev":[1520.4,2551]}
 *   [km, phút chạy]; thiếu/null = không có dữ liệu. */

typedef struct {
    float km;   /* < 0: không có dữ liệu */
    int mins;
} trip_span_t;

typedef struct {
    int month;       /* tháng hiện tại 1..12 */
    int last_month;
    trip_span_t today, yesterday, this_month, prev_month;
} trip_stats_t;

void trip_link_init(void);
/* Task NimBLE: ghép mảnh, đủ dòng thì đẩy vào mailbox. */
void trip_link_feed(uint16_t conn, const uint8_t *data, size_t length);
/* Task NimBLE, BLE_GAP_EVENT_DISCONNECT: nhả bộ đệm nếu central này đang giữ. */
void trip_link_disconnected(uint16_t conn);
/* LVGL tick: true + điền out nếu có số liệu mới. */
bool trip_link_take(trip_stats_t *out);
