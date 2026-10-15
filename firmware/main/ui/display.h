#pragma once

#include <stdint.h>
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"

#include "board.h"

#define RGB565(r, g, b) \
    ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define COLOR_BLACK  RGB565(0, 0, 0)
#define COLOR_WHITE  RGB565(255, 255, 255)
#define COLOR_RED    RGB565(255, 0, 0)
#define COLOR_GREEN  RGB565(0, 255, 0)
#define COLOR_BLUE   RGB565(0, 0, 255)
#define COLOR_YELLOW RGB565(255, 255, 0)
#define COLOR_GREY   RGB565(90, 90, 90)

void display_init(void);

/* Handle để esp_lvgl_port gắn LVGL vào panel. Chỉ hợp lệ sau display_init(). */
esp_lcd_panel_io_handle_t display_io(void);
esp_lcd_panel_handle_t display_panel(void);
void display_set_backlight(int percent);
void display_fill(uint16_t color);
void display_fill_rect(int x, int y, int w, int h, uint16_t color);

/* Ảnh kiểm tra hình học: dải màu + khung viền + 4 dấu góc.
   Thiếu một dấu góc nghĩa là offset cột chưa đúng. */
void display_selftest(void);
