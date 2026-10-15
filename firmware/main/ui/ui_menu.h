#pragma once

#include "lvgl.h"

/* MENU 2×2: Đồng hồ / Dẫn đường / Wi-Fi / Cài đặt (§2.1).
 * 320×240 dùng ô 144×70, margin 12, gap 8; vùng chạm không dưới 40px. */
void ui_menu_page_enter(lv_obj_t *parent);
void ui_menu_page_exit(void);
