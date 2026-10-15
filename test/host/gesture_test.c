/* Boundary test cho gesture_router — các ca đã chốt ở §9 kế hoạch:
   tap/jitter, sát ngưỡng, dọc/chéo, nhanh/chậm, đảo hướng, hold-recovery. */
#include "../../firmware/main/system/gesture_router.c"
#include <assert.h>
#include <stdio.h>

static gesture_output_t feed(gesture_event_t ev, uint32_t ms, int x, int y,
                             int rx, int ry, bool edge)
{
    gesture_sample_t s = {
        .event = ev, .time_ms = ms, .x = x, .y = y,
        .raw_x = rx, .raw_y = ry, .in_edge = edge,
    };
    return gesture_feed(&s);
}

static gesture_output_t feed_cal(gesture_event_t ev, uint32_t ms, int x, int y, bool edge)
{
    return feed(ev, ms, x, y, x, y, edge);
}

int main(void)
{
    gesture_config_t cfg = {
        .edge_width = 20, .tap_slop = 10, .lock_distance = 16,
        .swipe_min = 48, .axis_ratio = 1.8f, .swipe_max_ms = 700,
        .hold_ms = 3000, .raw_slop = 10,
    };
    gesture_init(&cfg);

    /* 1. Tap + jitter nhỏ: không swipe, không cancel. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 100, 100, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 50, 105, 103, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 100, 103, 108, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_UP, 150, 103, 108, false) == GESTURE_NONE);
    puts("ok tap + jitter");

    /* 2. Swipe ngang giữa màn, đủ ngưỡng, trong thời gian: candidate cancel
       lúc đủ 48px, SWIPE_RIGHT lúc nhả. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 40, 120, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 30, 70, 120, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 60, 100, 122, false) == GESTURE_CANCEL_CONTACT);
    assert(feed_cal(GESTURE_EVENT_MOVE, 90, 130, 120, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_UP, 120, 130, 120, false) == GESTURE_SWIPE_RIGHT);
    puts("ok swipe right mid-screen");

    /* 3. Swipe trái sát ngưỡng 48px đúng hạn. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 260, 60, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 40, 212, 60, false) == GESTURE_CANCEL_CONTACT);
    assert(feed_cal(GESTURE_EVENT_UP, 80, 212, 60, false) == GESTURE_SWIPE_LEFT);
    puts("ok swipe left at threshold");

    /* 4. 47px: không đủ — không swipe, không cancel giữa chừng. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 100, 60, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 50, 147, 60, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_UP, 100, 147, 60, false) == GESTURE_NONE);
    puts("ok 47px = no swipe");

    /* 5. Kéo dọc: widget giữ (không cancel contact, không swipe ngang). Nhát
       dọc dứt khoát (đủ quãng, đúng trục, đủ nhanh) báo thêm SWIPE_UP/DOWN
       lúc nhả; ngắn hoặc chậm thì không. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 100, 100, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 40, 102, 160, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 80, 98, 200, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_UP, 120, 98, 200, false) == GESTURE_SWIPE_DOWN);
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 100, 200, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 60, 101, 120, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_UP, 100, 101, 120, false) == GESTURE_SWIPE_UP);
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 100, 100, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 40, 100, 140, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_UP, 80, 100, 140, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 100, 100, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 400, 100, 200, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_UP, 800, 100, 200, false) == GESTURE_NONE);
    puts("ok vertical = widget owned (+ vswipe on release)");

    /* 6. Chéo chưa đủ tỉ lệ (dx=30, dy=20: 30 < 1.8*20=36): không ngang. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 100, 100, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 50, 130, 120, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_UP, 90, 130, 120, false) == GESTURE_NONE);
    puts("ok diagonal below ratio");

    /* 7. Chéo đủ tỉ lệ (dx=60, dy=20): ngang. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 100, 100, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 60, 160, 120, false) == GESTURE_CANCEL_CONTACT);
    assert(feed_cal(GESTURE_EVENT_UP, 100, 160, 120, false) == GESTURE_SWIPE_RIGHT);
    puts("ok diagonal above ratio");

    /* 8. Kéo quá chậm: quá 700ms sau khi chiếm quyền → hủy, không swipe,
       không trả lại tap. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 40, 60, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 400, 100, 60, false) == GESTURE_CANCEL_CONTACT);
    assert(feed_cal(GESTURE_EVENT_MOVE, 800, 140, 60, false) == GESTURE_CANCEL_CONTACT);
    assert(feed_cal(GESTURE_EVENT_UP, 900, 140, 60, false) == GESTURE_NONE);
    puts("ok slow drag cancelled");

    /* 9. Đảo hướng đáng kể sau khi chiếm quyền: hủy, không swipe. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 40, 60, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 40, 130, 60, false) == GESTURE_CANCEL_CONTACT);
    assert(feed_cal(GESTURE_EVENT_MOVE, 80, 60, 60, false) == GESTURE_CANCEL_CONTACT);
    assert(feed_cal(GESTURE_EVENT_UP, 100, 60, 60, false) == GESTURE_NONE);
    puts("ok reversal cancelled");

    /* 10. Widget claim từ giữa contact: không đổi thành system. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 100, 100, false) == GESTURE_NONE);
    gesture_claim_widget();
    assert(feed_cal(GESTURE_EVENT_MOVE, 40, 160, 100, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_UP, 80, 160, 100, false) == GESTURE_NONE);
    puts("ok widget claim keeps ownership");

    /* 11. Edge + ngang: ưu tiên hệ thống kể cả khi đã claim widget? Không —
       claim sau SYSTEM thì giữ SYSTEM; claim TRƯỚC thì widget giữ (luật:
       WIDGET không đổi lại SYSTEM). Edge chỉ ảnh hưởng qua in_edge ở caller. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 5, 100, true) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 50, 70, 100, true) == GESTURE_CANCEL_CONTACT);
    assert(feed_cal(GESTURE_EVENT_UP, 90, 70, 100, true) == GESTURE_SWIPE_RIGHT);
    puts("ok edge horizontal priority");

    /* 12. Edge + dọc: không ăn scroll dọc (không cancel contact); nhả tay
       vẫn chỉ báo vswipe như ca 5. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 5, 100, true) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_MOVE, 50, 7, 180, true) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_UP, 90, 7, 180, true) == GESTURE_SWIPE_DOWN);
    puts("ok edge vertical not stolen");

    /* 13. CANCEL (lỗi/multi-touch) giữa contact: hủy, chặn tới release. */
    assert(feed_cal(GESTURE_EVENT_DOWN, 0, 100, 100, false) == GESTURE_NONE);
    assert(feed_cal(GESTURE_EVENT_CANCEL, 30, 100, 100, false) == GESTURE_CANCEL_CONTACT);
    assert(feed_cal(GESTURE_EVENT_UP, 60, 100, 100, false) == GESTURE_NONE);
    puts("ok error cancel");

    /* 14. Hold 3s raw displacement nhỏ rồi nhả: RECOVERY_REQUEST; còn giữ tay
       mà lê tay xa (raw > slop) thì không arm. */
    assert(feed(GESTURE_EVENT_DOWN, 0, 100, 100, 100, 100, false) == GESTURE_NONE);
    assert(feed(GESTURE_EVENT_MOVE, 2000, 102, 101, 102, 101, false) == GESTURE_NONE);
    assert(feed(GESTURE_EVENT_MOVE, 3100, 101, 102, 101, 102, false) == GESTURE_NONE);
    assert(feed(GESTURE_EVENT_UP, 3200, 101, 102, 101, 102, false) == GESTURE_RECOVERY_REQUEST);
    assert(feed(GESTURE_EVENT_DOWN, 0, 100, 100, 100, 100, false) == GESTURE_NONE);
    assert(feed(GESTURE_EVENT_MOVE, 3100, 140, 100, 140, 100, false) == GESTURE_NONE);
    assert(feed(GESTURE_EVENT_UP, 3200, 140, 100, 140, 100, false) == GESTURE_NONE);
    puts("ok hold recovery arm/release");

    /* 15. CANCEL lúc IDLE: vô hại. */
    gesture_reset();
    assert(feed_cal(GESTURE_EVENT_CANCEL, 0, 0, 0, false) == GESTURE_NONE);
    puts("ok cancel at idle");

    puts("GESTURE: all boundary tests passed");
    return 0;
}
