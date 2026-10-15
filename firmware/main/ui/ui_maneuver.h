#pragma once

/* Mũi tên dẫn đường vẽ bằng vector cho các ngã mà glyph Material không diễn tả
 * nổi: vòng xuyến (vòng tròn, nhánh vào, lối ra đúng hướng, số lối ra ở tâm) và
 * ngã tách nhánh 13–16 (đường chính mờ, nhánh cần vào sáng). Các ngã khác vẫn
 * dùng glyph font. */

#include "lvgl.h"
#include <stdbool.h>

#define UI_MANEUVER_SIZE 64 /* khớp cỡ glyph font_icon_64 mà nó thay chỗ */

lv_obj_t *ui_maneuver_create(lv_obj_t *parent);

/* Vẽ ngã rẽ `turn` (§4.1) với số lối ra `exit`. Trả false nếu ngã này không
 * thuộc loại vẽ bằng vector — khi đó canvas bị ẩn và bên gọi dùng glyph. */
bool ui_maneuver_draw(lv_obj_t *canvas, int turn, int exit, lv_color_t fg,
                      lv_color_t dim, lv_color_t bg);

/* Góc lối ra vòng xuyến theo quy ước LVGL (0° = phải, tăng theo chiều kim đồng
 * hồ, 90° = xuống). Tách ra để test host kiểm được mà không cần màn hình. */
int ui_maneuver_roundabout_exit_angle(int turn, int exit);
