#include "gesture_router.h"

#include <stdlib.h>
#include <string.h>

typedef enum {
    G_IDLE = 0,
    G_TRACKING,
    G_SYSTEM, /* ngang đã chiếm quyền, chờ release ổn định */
    G_WIDGET, /* widget chiếm quyền — không bao giờ trả swipe */
    G_CONSUMED, /* contact bị hủy (lỗi/multi-touch/wake) — chờ nhả thật */
} gstate_t;

static gesture_config_t cfg = {
    .edge_width = 20,
    .tap_slop = 10,
    .lock_distance = 16,
    .swipe_min = 48,
    .axis_ratio = 1.8f,
    .swipe_max_ms = 700,
    .hold_ms = 3000,
    .raw_slop = 10,
};

static struct {
    gstate_t state;
    uint32_t t0_ms;
    int x0, y0;
    int raw_x0, raw_y0;
    int last_x, last_y;
    int max_excursion;   /* quãng lớn nhất theo hướng đã khóa */
    int locked_dir;      /* 0 chưa khóa, +1 phải, -1 trái, 2 dọc */
    bool hold_armed;
} g;

void gesture_init(const gesture_config_t *config)
{
    if (config) cfg = *config;
    gesture_reset();
}

void gesture_reset(void)
{
    memset(&g, 0, sizeof(g));
}

bool gesture_contact_active(void)
{
    return g.state != G_IDLE;
}

void gesture_claim_widget(void)
{
    /* "Khi đã WIDGET_OWNED không đổi lại SYSTEM trong cùng contact" — nhưng
       SYSTEM đã chiếm trước thì giữ SYSTEM (edge ưu tiên khi hướng ngang). */
    if (g.state == G_TRACKING) g.state = G_WIDGET;
}

static void update_hold(const gesture_sample_t *s)
{
    if (g.state != G_TRACKING || g.locked_dir != 0) return;
    if (s->time_ms - g.t0_ms < cfg.hold_ms) return;
    if (s->raw_x < 0 || s->raw_y < 0) return; /* không có raw thì không arm */
    int rdx = abs(s->raw_x - g.raw_x0);
    int rdy = abs(s->raw_y - g.raw_y0);
    if (rdx <= cfg.raw_slop && rdy <= cfg.raw_slop) g.hold_armed = true;
}

gesture_output_t gesture_feed(const gesture_sample_t *s)
{
    switch (s->event) {
    case GESTURE_EVENT_CANCEL:
        /* Hủy contact; nếu đang giữa contact thì chặn mọi thứ tới lúc nhả thật.
           Driver tự suppress mẫu tiếp theo cho tới RELEASE thật. */
        if (g.state != G_IDLE) {
            g.state = G_CONSUMED;
            return GESTURE_CANCEL_CONTACT;
        }
        return GESTURE_NONE;

    case GESTURE_EVENT_DOWN:
        gesture_reset();
        g.state = G_TRACKING;
        g.t0_ms = s->time_ms;
        g.x0 = g.last_x = s->x;
        g.y0 = g.last_y = s->y;
        g.raw_x0 = s->raw_x;
        g.raw_y0 = s->raw_y;
        return GESTURE_NONE;

    case GESTURE_EVENT_MOVE: {
        if (g.state == G_WIDGET) {
            /* Vẫn theo dõi toạ độ để nhận ra vuốt dọc lúc nhả. */
            g.last_x = s->x; g.last_y = s->y;
            return GESTURE_NONE;
        }
        if (g.state == G_CONSUMED || g.state == G_IDLE)
            return GESTURE_NONE;
        if (g.state == G_SYSTEM) {
            /* Đã chiếm quyền: theo dõi reversal + timeout. */
            int dx = s->x - g.x0;
            int ex = dx * g.locked_dir;
            if (ex < 0) ex = -ex; /* đổi hướng đáng kể = hủy */
            int forward = dx * g.locked_dir;
            if (forward < g.max_excursion - cfg.swipe_min) {
                g.state = G_CONSUMED;
                return GESTURE_CANCEL_CONTACT;
            }
            if (forward > g.max_excursion) g.max_excursion = forward;
            if (s->time_ms - g.t0_ms > cfg.swipe_max_ms) {
                /* Quá lâu sau khi đã chiếm quyền: hủy, không trả lại thành tap. */
                g.state = G_CONSUMED;
                return GESTURE_CANCEL_CONTACT;
            }
            g.last_x = s->x; g.last_y = s->y;
            return GESTURE_NONE;
        }

        /* G_TRACKING: khoá hướng khi đủ lock_distance. */
        int dx = s->x - g.x0;
        int dy = s->y - g.y0;
        int adx = dx < 0 ? -dx : dx;
        int ady = dy < 0 ? -dy : dy;
        if (adx < cfg.lock_distance && ady < cfg.lock_distance) {
            g.last_x = s->x; g.last_y = s->y;
            update_hold(s);
            return GESTURE_NONE;
        }
        bool horizontal = (float)adx >= cfg.axis_ratio * (float)ady;
        if (!horizontal) {
            g.locked_dir = 2;
            g.state = G_WIDGET; /* kéo dọc → widget/scroll giữ */
            g.last_x = s->x; g.last_y = s->y;
            return GESTURE_NONE;
        }
        /* Ngang: hệ thống chiếm — kể cả giữa màn; cạnh chỉ là ưu tiên sớm. */
        g.locked_dir = dx > 0 ? 1 : -1;
        g.max_excursion = adx;
        g.last_x = s->x; g.last_y = s->y;
        if (adx >= cfg.swipe_min) {
            /* Đủ quãng: chiếm contact, bên gọi cancel widget đang pressed,
               rồi đợi release ổn định. */
            g.state = G_SYSTEM;
            return GESTURE_CANCEL_CONTACT;
        }
        /* Chưa đủ quãng tối thiểu nhưng hướng đã khóa — vẫn thuộc hệ thống.
           Không trả output vội: quyết định ở release. */
        return GESTURE_NONE;
    }

    case GESTURE_EVENT_UP:
        if (g.state == G_TRACKING) {
            bool armed = g.hold_armed;
            gesture_reset();
            if (armed) return GESTURE_RECOVERY_REQUEST;
            return GESTURE_NONE; /* tap: LVGL tự xử lý */
        }
        if (g.state == G_SYSTEM) {
            int dir = g.locked_dir;
            bool ok_duration = s->time_ms - g.t0_ms <= cfg.swipe_max_ms;
            int forward = (g.last_x - g.x0) * dir;
            bool ok_excursion = forward >= cfg.swipe_min;
            gesture_reset();
            if (!ok_duration || !ok_excursion) return GESTURE_NONE;
            return dir > 0 ? GESTURE_SWIPE_RIGHT : GESTURE_SWIPE_LEFT;
        }
        /* WIDGET: nhả bình thường, LVGL tự xử lý — nhưng nếu là một nhát
           vuốt dọc dứt khoát (đủ quãng, đúng trục, đủ nhanh) thì báo thêm. */
        if (g.state == G_WIDGET) {
            int dx = g.last_x - g.x0, dy = g.last_y - g.y0;
            int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
            bool vswipe = ady >= cfg.swipe_min && (float)ady >= cfg.axis_ratio * (float)adx
                          && s->time_ms - g.t0_ms <= cfg.swipe_max_ms;
            gesture_reset();
            if (vswipe) return dy < 0 ? GESTURE_SWIPE_UP : GESTURE_SWIPE_DOWN;
            return GESTURE_NONE;
        }
        if (g.state == G_CONSUMED) gesture_reset();
        return GESTURE_NONE;
    }
    return GESTURE_NONE;
}
