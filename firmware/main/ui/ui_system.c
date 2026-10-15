#include "ui_system.h"
#include "ui_router.h"
#include "board.h"
#define W DISPLAY_W
#define H DISPLAY_H
LV_FONT_DECLARE(font_vn_16);
LV_FONT_DECLARE(font_vn_20);
LV_FONT_DECLARE(font_vn_22);
static struct { lv_obj_t *ota, *ota_title, *ota_l1, *ota_l2, *ota_bar; } ota;
static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    return obj;
}
static lv_obj_t *cal_layer, *cal_dot, *cal_text;

/* Màn hiệu chỉnh cảm ứng: chấm tròn tại (x,y) do CODE chọn (không phải do
   phép biến đổi toạ độ cảm ứng — lúc này phép biến đổi đó CHÍNH LÀ thứ đang
   được đo), kèm 1 dòng hướng dẫn. Bên gọi (touch_es3c28p.c) tự đọc thẳng
   thanh ghi I2C khi đang hiển thị màn này để lấy toạ độ thô ứng với đúng
   điểm (x,y) này, không đi qua LVGL indev/read_touch() — nhờ vậy việc hiệu
   chỉnh không phụ thuộc vào chính công thức nó đang tính ra. */
void ui_show_touch_cal(const char *msg, int x, int y)
{
    if (!cal_layer) {
        cal_layer = lv_obj_create(ui_router_root(UI_ROOT_SYSTEM));
        lv_obj_set_size(cal_layer, W, H);
        lv_obj_set_pos(cal_layer, 0, 0);
        lv_obj_set_style_bg_color(cal_layer, lv_color_hex(0x090B10), 0);
        lv_obj_set_style_bg_opa(cal_layer, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cal_layer, 0, 0);
        lv_obj_set_style_radius(cal_layer, 0, 0);
        lv_obj_set_style_pad_all(cal_layer, 0, 0);
        lv_obj_remove_flag(cal_layer, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(cal_layer, LV_OBJ_FLAG_CLICKABLE);

        cal_dot = lv_obj_create(cal_layer);
        lv_obj_set_size(cal_dot, 18, 18);
        lv_obj_set_style_radius(cal_dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(cal_dot, lv_color_hex(0x2ECC71), 0);
        lv_obj_set_style_bg_opa(cal_dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(cal_dot, 0, 0);
        lv_obj_remove_flag(cal_dot, LV_OBJ_FLAG_CLICKABLE);

        cal_text = label(cal_layer, &font_vn_20, lv_color_white());
    }
    lv_obj_remove_flag(cal_layer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(cal_layer);
    lv_obj_set_pos(cal_dot, x - 9, y - 9);
    lv_obj_move_foreground(cal_dot);
    lv_label_set_text(cal_text, msg);
    lv_obj_align(cal_text, LV_ALIGN_BOTTOM_MID, 0, -14);
}

void ui_hide_touch_cal(void)
{
    if (cal_layer) lv_obj_add_flag(cal_layer, LV_OBJ_FLAG_HIDDEN);
}

void ui_show_ota(const char *line1, const char *line2, int percent)
{

    if (!ota.ota) {
        ota.ota = lv_obj_create(ui_router_root(UI_ROOT_SYSTEM));
        lv_obj_set_size(ota.ota, W, H);
        lv_obj_set_pos(ota.ota, 0, 0);
        lv_obj_set_style_bg_color(ota.ota, lv_color_hex(0x090B10), 0);
        lv_obj_set_style_bg_opa(ota.ota, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ota.ota, 0, 0);
        lv_obj_set_style_radius(ota.ota, 0, 0);
        lv_obj_remove_flag(ota.ota, LV_OBJ_FLAG_SCROLLABLE);

        ota.ota_title = label(ota.ota, &font_vn_22, lv_color_white());
        lv_label_set_text(ota.ota_title, "Cập nhật firmware");
        lv_obj_align(ota.ota_title, LV_ALIGN_TOP_MID, 0, 14);

        ota.ota_l1 = label(ota.ota, &font_vn_16, lv_color_white());
        lv_obj_align(ota.ota_l1, LV_ALIGN_TOP_MID, 0, 52);

        ota.ota_l2 = label(ota.ota, &font_vn_16, lv_color_hex(0x9CA3AF));
        lv_obj_align(ota.ota_l2, LV_ALIGN_TOP_MID, 0, 76);

        lv_obj_t *track = lv_obj_create(ota.ota);
        lv_obj_set_size(track, W - 48, 8);
        lv_obj_align(track, LV_ALIGN_BOTTOM_MID, 0, -24);
        lv_obj_set_style_bg_color(track, lv_color_hex(0x22262B), 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(track, 0, 0);
        lv_obj_set_style_radius(track, 4, 0);
        lv_obj_set_style_pad_all(track, 0, 0);
        lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);

        ota.ota_bar = lv_obj_create(track);
        lv_obj_set_size(ota.ota_bar, 0, 8);
        lv_obj_set_pos(ota.ota_bar, 0, 0);
        lv_obj_set_style_bg_color(ota.ota_bar, lv_color_hex(0x2F6FED), 0);
        lv_obj_set_style_bg_opa(ota.ota_bar, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ota.ota_bar, 0, 0);
        lv_obj_set_style_radius(ota.ota_bar, 4, 0);
    }

    lv_obj_move_foreground(ota.ota);
    lv_label_set_text(ota.ota_l1, line1 ? line1 : "");
    lv_label_set_text(ota.ota_l2, line2 ? line2 : "");
    lv_obj_align(ota.ota_l1, LV_ALIGN_TOP_MID, 0, 52);
    lv_obj_align(ota.ota_l2, LV_ALIGN_TOP_MID, 0, 76);
    lv_obj_set_width(ota.ota_bar, percent < 0 ? 0 : (W - 48) * percent / 100);
}

/* --- Màn chờ lúc khởi động --------------------------------------------------
   Trước đây là dải màu tự kiểm tra (display_selftest() ở board_*.c, vẽ thẳng
   trước khi LVGL chiếm màn) để soi hình học lúc dựng driver — hết cần thiết
   vì hướng màn đã xác nhận đúng lâu rồi, nay thay hẳn bằng logo sản phẩm cho
   người dùng thật. Logo tĩnh, không cần lv_gif (ảnh nhúng ARGB8888 sinh bằng
   tools/png_to_c.py từ tweak/branding/icon.png). */

LV_IMAGE_DECLARE(logo_splash);

uint32_t ui_show_welcome(void)
{
    lv_obj_t *layer = lv_obj_create(ui_router_root(UI_ROOT_SYSTEM));
    lv_obj_set_size(layer, W, H);
    lv_obj_set_pos(layer, 0, 0);
    lv_obj_set_style_bg_color(layer, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(layer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(layer, 0, 0);
    lv_obj_set_style_radius(layer, 0, 0);
    lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_move_foreground(layer);

    lv_obj_t *img = lv_image_create(layer);
    lv_image_set_src(img, &logo_splash);
    lv_obj_center(img);

    const uint32_t hold_ms = 1500;
    lv_obj_delete_delayed(layer, hold_ms);
    return hold_ms;
}
