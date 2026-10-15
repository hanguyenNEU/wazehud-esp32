#pragma once
#include "lvgl.h"
#include <stdint.h>
/* Call only from the LVGL context or while holding its lock. */
uint32_t ui_show_welcome(void);
void ui_show_ota(const char *line1, const char *line2, int percent);
void ui_show_touch_cal(const char *msg, int x, int y);
void ui_hide_touch_cal(void);
