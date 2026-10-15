#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* GPS từ app CarDash (iPhone không jailbreak) qua BLE — nguồn dự phòng khi
 * tweak Waze chưa kết nối.
 *
 * App ghi 1 dòng JSON gọn mỗi giây vào characteristic GPS của service CarDash
 * (hlp_ble.c), ví dụ:
 *   {"t":"gps","lat":21.02851,"lon":105.85412,"spd":42.5,"hdg":181,
 *    "acc":5,"unix":1791570429,"tz":420}
 *   spd: km/h (-1 = không rõ) · hdg: độ (-1 = không rõ) · acc: mét.
 *
 * Kèm bản tin "bộ nhớ đường" app tra sẵn trên máy (gửi khi đổi + mỗi 5 s):
 *   {"t":"road","lim":60,"alr":0,"st":"Quốc lộ 5"}   lim/alr 0 = không có.
 *
 * gps_link_feed() chạy trong task NimBLE — chỉ chép byte vào mailbox.
 * gps_link_take() chạy trong LVGL tick — parse bản mới nhất. */

typedef struct {
    double lat, lon;
    float speed_kmh;  /* < 0: không rõ */
    float heading;    /* < 0: không rõ */
    float accuracy_m;
    int64_t unix_s;   /* 0: không có */
    int tz_min;
    int64_t received_us;
} gps_fix_t;

typedef struct {
    int lim;
    int alr;
    char st[128];
    int64_t received_us;
} gps_road_t;

void gps_link_init(void);
void gps_link_feed(const uint8_t *data, size_t length);
/* true + điền out nếu có bản tin mới từ lần gọi trước. */
bool gps_link_take(gps_fix_t *out);
bool gps_link_take_road(gps_road_t *out);
