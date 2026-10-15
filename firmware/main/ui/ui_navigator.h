#pragma once

#include "hud_state.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Giao diện HUD. Chỉ phụ thuộc LVGL và hud_state.h — không có gì của ESP-IDF,
 * nhờ vậy cùng file này chạy được trong simulator SDL trên máy tính. */

typedef enum {
    UI_THEME_AUTO = 0, /* theo giờ địa phương lấy từ hi.unix + hi.tz */
    UI_THEME_DAY,
    UI_THEME_NIGHT,
} ui_theme_t;

typedef enum {
    UI_LAYOUT_WAZE = 0, /* mặc định: vòng tròn chồng nhau, phong cách HUD V2 */
    UI_LAYOUT_SPEED,    /* dải ngang, chữ số tốc độ lớn nhất có thể */
} ui_layout_t;

typedef enum {
    UI_LINK_ADVERTISING = 0, /* chưa ai kết nối */
    UI_LINK_CONNECTED,       /* đã kết nối, chưa có state */
    UI_LINK_LIVE,            /* đang nhận */
    UI_LINK_STALE,           /* quá 3 giây không có message (§5) */
    UI_LINK_GPS,             /* chưa nối Waze, đang có GPS từ app CarDash trên
                                 iPhone chính (tốc độ; lim/tên đường nếu bộ
                                 nhớ đường có) — huy hiệu "GPS điện thoại". */
} ui_link_t;

typedef enum {
    UI_WIFI_OFF = 0,   /* chưa cấu hình / idle */
    UI_WIFI_CONNECTING,
    UI_WIFI_CONNECTED,
    UI_WIFI_ERROR,
} ui_wifi_link_t;


void ui_create(void);

/* Vẽ lại toàn bộ theo snapshot. Gọi từ task LVGL, không gọi từ callback GATT. */
void ui_update(const hud_state_t *state);

void ui_set_link(ui_link_t link);

/* Chấm trạng thái WiFi ở góc trên-phải màn HUD (chấm BLE dùng chung cơ chế
   với ui_set_link ở trên). */
void ui_set_wifi_link(ui_wifi_link_t state);

/* Các thiết lập do M6 đẩy xuống qua cfg_*. */
void ui_set_theme(ui_theme_t theme);
void ui_set_layout(ui_layout_t layout);
void ui_set_alerts(bool enabled, int distance_threshold_m);


/* Dùng cho UI_THEME_AUTO: giờ địa phương 0..23, -1 nghĩa là chưa biết. */
void ui_set_local_hour(int hour);

/* Dùng cho màn chờ khi không dẫn đường và Waze không cung cấp tên đường. */
void ui_set_local_datetime(int hour, int minute, int wday, int mday, int month, int year);
