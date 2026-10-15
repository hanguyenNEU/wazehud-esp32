#pragma once

#include "gesture_router.h"
#include "lvgl.h"
#include <stdbool.h>

/* Cầu nối gesture ↔ LVGL (§3.2, §4):
 *   - Nhận mẫu vật lý từ touch driver (cùng mẫu driver cho LVGL indev).
 *   - Theo dõi target đang pressed qua indev event hook chuẩn của LVGL +
 *     metadata widget (slider/scroll-list) — không dựa vào pointer nội bộ.
 *   - Khi classifier chiếm quyền hệ thống: lv_indev_reset + lv_indev_wait_release
 *     để widget nguồn KHÔNG nhận CLICKED và màn đích không nhận release giả.
 *   - Output swipe/recovery chuyển cho handler do main.c đăng ký. */

typedef void (*ui_input_swipe_cb_t)(bool rightward, void *user);
typedef void (*ui_input_recovery_cb_t)(void *user);
/* Contact bắt đầu hợp lệ: note activity; trả true nếu contact bị "consume"
 * (wake từ dim) — ui_input sẽ chặn mọi output của contact này tới lúc nhả. */
typedef bool (*ui_input_activity_cb_t)(void *user);

void ui_input_init(lv_indev_t *indev);
void ui_input_feed(const gesture_sample_t *sample);
void ui_input_set_handlers(ui_input_swipe_cb_t swipe,
                           ui_input_recovery_cb_t recovery,
                           ui_input_activity_cb_t activity, void *user);

/* Vuốt dọc (lên = true). Chỉ báo, không hủy press của LVGL — list cuộn vẫn
   cuộn bình thường; handler tự lọc theo màn đang mở. */
typedef void (*ui_input_vswipe_cb_t)(bool upward, void *user);
void ui_input_set_vswipe_handler(ui_input_vswipe_cb_t vswipe, void *user);

/* Dải cạnh đã scale theo board (touch driver dùng để đặt in_edge). */
int ui_input_edge_width(void);
