#pragma once

#include "lvgl.h"
#include "trip_link.h"

/* Trang "Chuyến đi" (Menu → Chuyến đi): quãng đường + thời gian chạy hôm nay,
 * hôm qua, tháng này và tháng trước — tweak WazeHUD tự tính trên iPhone từ
 * GPS của Waze, gửi qua kênh tin 8a7e0012 (trip_link.h). */

void ui_trip_page_enter(lv_obj_t *parent);
void ui_trip_page_exit(void);

/* LVGL task: số liệu mới từ tweak. */
void ui_trip_set(const trip_stats_t *stats);
