#pragma once

#include "lvgl.h"
#include <stdint.h>

/* Ảnh nền động cho màn CLOCK — dữ liệu sinh bằng tools/anim_bg_to_c.py.
 *
 * Khung đầu nằm nguyên trong flash; các khung sau là chênh lệch so với khung
 * trước (xem định dạng trong tool). Player chép khung đầu ra PSRAM rồi áp
 * delta theo timer, chỉ invalidate vùng thực sự đổi. Không cấp phát được
 * PSRAM thì hiện ảnh tĩnh thẳng từ flash. */

typedef struct {
    uint16_t w, h;
    uint16_t frame_count;
    uint16_t frame_ms;
    const uint16_t *base;
    const uint16_t *delta;
    const uint32_t *frame_offsets; /* frame_count + 1 phần tử */
} anim_bg_t;

/* Tạo image phủ (0,0) trong parent và bắt đầu chạy. Một instance tại một
   thời điểm — gọi lại sẽ dừng cái cũ. */
lv_obj_t *anim_bg_start(lv_obj_t *parent, const anim_bg_t *bg);

/* Dừng timer, xoá image, giải phóng buffer. Gọi TRƯỚC khi xóa cây chứa
   image (page exit). */
void anim_bg_stop(void);
