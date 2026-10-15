#include "ui_input.h"

#include "board.h"
#include "ui_router.h"

#include "lvgl.h"
#include <string.h>

/* Ngưỡng gốc cho 320px; scale khoảng cách theo W/320, cạnh tối thiểu 16px,
   slop tối thiểu 8px (§4.1). */
static gesture_config_t base_config(void)
{
    int scale_w = DISPLAY_W > 0 ? DISPLAY_W : 320;
    gesture_config_t cfg = {
        .edge_width = 20 * scale_w / 320,
        .tap_slop = 10 * scale_w / 320,
        .lock_distance = 16 * scale_w / 320,
        .swipe_min = 48 * scale_w / 320,
        .axis_ratio = 1.8f,
        .swipe_max_ms = 700,
        .hold_ms = 3000,
        .raw_slop = 10,
    };
    if (cfg.edge_width < 16) cfg.edge_width = 16;
    if (cfg.tap_slop < 8) cfg.tap_slop = 8;
    if (cfg.lock_distance < 10) cfg.lock_distance = 10;
    if (cfg.swipe_min < 24) cfg.swipe_min = 24;
    return cfg;
}

int ui_input_edge_width(void)
{
    gesture_config_t cfg = base_config();
    return cfg.edge_width;
}

static lv_indev_t *indev;
static ui_input_swipe_cb_t swipe_action;
static ui_input_recovery_cb_t recovery_action;
static ui_input_activity_cb_t activity_action;
static void *action_user;
static ui_input_vswipe_cb_t vswipe_action;
static void *vswipe_user;

void ui_input_set_vswipe_handler(ui_input_vswipe_cb_t vswipe, void *user)
{
    vswipe_action = vswipe;
    vswipe_user = user;
}

/* Contact hiện tại */
static bool contact_down;
static bool contact_consumed;   /* wake-consume: chặn mọi output tới khi nhả */
static bool system_claimed;     /* classifier đã chiếm: đã cancel LVGL press */
static bool gesture_claimed_widget; /* indev PRESSED hit-test ra slider/list */

static bool ancestor_is_scrollable_list(lv_obj_t *obj)
{
    lv_obj_t *cur = obj;
    while (cur) {
        if (cur == ui_router_root(UI_ROOT_PAGE)) break;
        if (cur == ui_router_root(UI_ROOT_CLOCK)) break;
        if (cur == ui_router_root(UI_ROOT_NAVIGATION)) break;
        /* Scroll list: scrollable với scroll_dir chỉ dọc. Slider tự là một
           lớp riêng; cả hai đều thuộc "widget" theo phân xử. */
        if (lv_obj_has_flag(cur, LV_OBJ_FLAG_SCROLLABLE)) {
            lv_dir_t dir = lv_obj_get_scroll_dir(cur);
            if (dir == LV_DIR_VER) return true;
        }
        cur = lv_obj_get_parent(cur);
    }
    return false;
}

/* Hook indev PRESSED: LVGL đã hit-test xong target — phân loại widget.
   LVGL 9.6 gửi indev event qua cùng cơ chế lv_event; current_target là indev. */
static void indev_event_cb(lv_event_t *event)
{
    lv_indev_t *ev_indev = (lv_indev_t *)lv_event_get_current_target(event);
    if (ev_indev != indev) return;

    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        lv_obj_t *target = lv_indev_get_active_obj();
        if (target) {
            /* Bàn phím/ô nhập: gõ lệch ngang không được thành vuốt đổi màn. */
            if (lv_obj_check_type(target, &lv_slider_class)
                || lv_obj_has_class(target, &lv_buttonmatrix_class)
                || lv_obj_check_type(target, &lv_textarea_class)
                || ancestor_is_scrollable_list(target)) {
                gesture_claimed_widget = true;
                gesture_claim_widget();
            }
        }
    }
}

void ui_input_init(lv_indev_t *ev_indev)
{
    indev = ev_indev;
    gesture_config_t cfg = base_config();
    gesture_init(&cfg);
    if (indev) {
        lv_indev_add_event_cb(indev, indev_event_cb, LV_EVENT_PRESSED, NULL);
    }
}

void ui_input_set_handlers(ui_input_swipe_cb_t swipe,
                           ui_input_recovery_cb_t recovery,
                           ui_input_activity_cb_t activity, void *user)
{
    swipe_action = swipe;
    recovery_action = recovery;
    activity_action = activity;
    action_user = user;
}

static void cancel_lvgl_press(void)
{
    if (!indev) return;
    /* Reset press đang có (widget nguồn không được CLICKED) rồi chờ release —
       classifier vẫn nhận mẫu vật lý vì touch driver gửi trực tiếp. */
    lv_indev_reset(indev, NULL);
    lv_indev_wait_release(indev);
}

void ui_input_feed(const gesture_sample_t *sample)
{
    bool locked = ui_router_input_locked();

    if (sample->event == GESTURE_EVENT_DOWN) {
        contact_down = true;
        contact_consumed = false;
        system_claimed = false;
        gesture_claimed_widget = false;
        if (activity_action && activity_action(action_user)) {
            contact_consumed = true;
            gesture_reset();
            if (!locked) cancel_lvgl_press();
            return;
        }
    }

    gesture_output_t output = gesture_feed(sample);

    if (contact_consumed) return;

    switch (output) {
    case GESTURE_NONE:
        break;
    case GESTURE_SWIPE_RIGHT:
    case GESTURE_SWIPE_LEFT:
        if (!locked && swipe_action) {
            swipe_action(output == GESTURE_SWIPE_RIGHT, action_user);
        }
        break;
    case GESTURE_CANCEL_CONTACT:
        if (!locked && !system_claimed) {
            system_claimed = true;
            cancel_lvgl_press();
        }
        break;
    case GESTURE_RECOVERY_REQUEST:
        if (!locked && recovery_action) recovery_action(action_user);
        break;
    case GESTURE_SWIPE_UP:
    case GESTURE_SWIPE_DOWN:
        if (!locked && vswipe_action) vswipe_action(output == GESTURE_SWIPE_UP, vswipe_user);
        break;
    }

    if (sample->event == GESTURE_EVENT_UP) {
        contact_down = false;
        contact_consumed = false;
        system_claimed = false;
        gesture_claimed_widget = false;
    }
}
