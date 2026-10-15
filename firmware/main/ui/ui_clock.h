#pragma once

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

/* Màn CLOCK (§2.1, §5):
 *   - Nền pixel-art động (anim_bg); HH và MM là 2 label canh hai bên dấu ":"
 *     vẽ bằng 2 chấm nhấp nháy theo giây (không hiện số giây).
 *   - Đồng hồ nhỏ + "T7 - dd/mm/yyyy · ÂL dd/mm" chung một khung ở đáy.
 *   - Cold boot chưa đồng bộ: "--:--:--", "Chưa đồng bộ giờ", ngày "—".
 *   - Chỉ đổi label khi giây thay đổi; lunar chỉ tính lại khi ngày VN đổi. */

void ui_clock_page_enter(lv_obj_t *parent);
void ui_clock_page_exit(void);

/* main.c tick 250ms đẩy giờ đã decode (unix + tz + trạng thái đồng bộ).
   Timezone điện thoại KHÁC UTC+7 thì CLOCK thêm nhãn "(VN)" sau ngày âm. */
void ui_clock_set_time(int64_t unix_seconds, int tz_minutes, bool synced);

/* Ảnh nền màn Đồng hồ (NULL = ảnh mặc định nhúng sẵn). Đổi ngay nếu đang
   hiện, nhớ cho lần mở sau. Con trỏ phải sống tới khi đổi ảnh khác. */
#include "anim_bg.h"
void ui_clock_set_background(const anim_bg_t *bg);

/* Dòng nhắc ngắn (~2 giây) trên màn Đồng hồ. */
void ui_clock_show_hint(const char *text);
