#pragma once

/* Bộ phân loại cử chỉ thuần C — không LVGL, không ESP-IDF, để chạy được cả
 * trên firmware, simulator lẫn host test.
 *
 * Máy trạng thái:
 *
 *   IDLE → TRACKING → (SYSTEM_CANDIDATE | WIDGET_OWNED) → WAIT_RELEASE → IDLE
 *
 *   - TRACKING: chưa đủ chuyển động để khoá hướng. Tap/hold thuộc LVGL.
 *   - SYSTEM_CANDIDATE: ngang đủ ngưỡng → contact bị hệ thống chiếm; bên gọi
 *     phải cancel widget đang pressed (lv_indev_reset + wait_release), rồi
 *     classifier đợi release ổn định mới trả SWIPE đúng một lần.
 *   - WIDGET_OWNED: slider/list đã claim (từ DOWN hoặc qua gesture_claim_widget)
 *     — không bao giờ trả swipe cho contact này.
 *   - WAIT_RELEASE: đã đủ ngưỡng ngang, chờ nhả tay ổn định để kiểm duration.
 *
 * Quy tắc đã chốt (§4 kế hoạch UI):
 *   - Swipe ngang từ nền/button thường vẫn nhận được ở giữa màn.
 *   - Swipe ngang bắt đầu trong dải cạnh luôn ưu tiên hệ thống.
 *   - Dải cạnh không ăn tap hoặc scroll dọc.
 *   - Kéo dọc khóa widget; đường chéo chưa đủ tỉ lệ không chuyển màn.
 *   - Kéo quá lâu (swipe_max_ms) sau khi đã chiếm quyền → hủy contact,
 *     không trả lại thành tap.
 *   - Giữ tay ≥ hold_ms với raw displacement nhỏ, chưa bị claim, rồi NHẢ tay
 *     → RECOVERY_REQUEST (đường phục hồi khi mapping sai hoàn toàn).
 */

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    GESTURE_EVENT_DOWN = 0,
    GESTURE_EVENT_MOVE,
    GESTURE_EVENT_UP,
    GESTURE_EVENT_CANCEL, /* lỗi liên tục / multi-touch / mode change */
} gesture_event_t;

typedef struct {
    gesture_event_t event;
    uint32_t time_ms; /* monotonic ms của sample */
    int x, y;         /* toạ độ màn ĐÃ hiệu chỉnh */
    int raw_x, raw_y; /* toạ độ raw — cho recovery khi mapping sai; -1 nếu không có */
    bool in_edge;     /* contact nằm trong dải cạnh trái/phải */
} gesture_sample_t;

typedef enum {
    GESTURE_NONE = 0,
    GESTURE_SWIPE_RIGHT,      /* vuốt sang phải */
    GESTURE_SWIPE_LEFT,       /* vuốt sang trái */
    GESTURE_CANCEL_CONTACT,   /* hủy contact, chặn click/swipe */
    GESTURE_RECOVERY_REQUEST, /* giữ tay 3s rồi nhả → hiệu chỉnh lại */
    GESTURE_SWIPE_UP,         /* vuốt dọc lên — báo lúc nhả; widget cuộn vẫn
                                 nhận kéo như thường, bên gọi tự quyết dùng
                                 hay bỏ (vd chỉ màn Đồng hồ đổi ảnh nền) */
    GESTURE_SWIPE_DOWN,
} gesture_output_t;

typedef struct {
    int edge_width;          /* dải cạnh ưu tiên */
    int tap_slop;            /* jitter nhỏ còn là tap/hold */
    int lock_distance;       /* quãng bắt đầu khoá hướng */
    int swipe_min;           /* quãng tối thiểu thành swipe */
    float axis_ratio;        /* ngang khi |dx| >= ratio * |dy| */
    uint32_t swipe_max_ms;   /* kéo chậm quá không thành điều hướng */
    uint32_t hold_ms;        /* giữ để arm recovery */
    int raw_slop;            /* raw displacement tối đa khi giữ */
} gesture_config_t;

void gesture_init(const gesture_config_t *cfg);

/* Cho từng mẫu vật lý; trả output cần xử lý. Mẫu lỗi ngắn KHÔNG đưa vào đây —
 * driver tự giữ contact (xem touch_es3c28p.c). */
gesture_output_t gesture_feed(const gesture_sample_t *sample);

/* LVGL đã hit-test ra widget slider/scroll-list cho contact hiện tại → widget
 * chiếm quyền từ giờ, trừ khi hệ thống đã chiếm trước. */
void gesture_claim_widget(void);

/* Hủy contact đang theo dõi (mode change, wake-consume...). */
void gesture_reset(void);

bool gesture_contact_active(void);
