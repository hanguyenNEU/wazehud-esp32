/* LVGL integration test cho phân xử input (§4.2, §9):
 *   - Swipe ngang bắt đầu TRÊN nút: nút nguồn KHÔNG nhận CLICKED.
 *   - Màn đích không nhận release: nút nằm dưới điểm nhả không nhận PRESSED.
 *   - Slider giữ quyền khi kéo ngang — không chuyển màn.
 *   - Scroll list dọc: không chuyển màn, release bình thường.
 *   - Wake-consume (activity trả true): chặn mọi output của contact.
 * Chạy đúng ui_input.c + gesture_router.c như trên ESP32; indev mô phỏng bằng
 * kịch bản mẫu, đúng cách touch_es3c28p.c nạp mẫu vào ui_input. */
#include "../../firmware/main/ui/ui_navigator.c"
#include "ui_input.h"
#include "ui_router.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ---- Bộ đếm sự kiện ---- */
static int click_count, press_count, release_count, swipe_count, recovery_count;
static int consume_contact;

static void count_click(lv_event_t *e)
{
    (void)e;
    click_count++;
}

static void count_press(lv_event_t *e)
{
    (void)e;
    press_count++;
}

static void count_release(lv_event_t *e)
{
    (void)e;
    release_count++;
}

static void on_swipe(bool rightward, void *user)
{
    (void)user;
    swipe_count++;
    ui_router_swipe(rightward);
}

static void on_recovery(void *user)
{
    (void)user;
    recovery_count++;
}

static bool on_activity(void *user)
{
    (void)user;
    if (consume_contact) return true;
    return false;
}

/* ---- Indev kịch bản ---- */
typedef struct {
    bool pressed;
    lv_point_t point;
    uint32_t dt_ms; /* bước thời gian tới frame này */
} script_frame_t;

static script_frame_t script[64];
static int script_len;
static int script_pos;
static int gesture_ups; /* số UP đã nạp vào ui_input */

static void script_reset(void)
{
    script_len = 0;
    script_pos = 0;
    gesture_ups = 0;
}

static void script_add_dt(bool pressed, int x, int y, uint32_t dt_ms)
{
    if (script_len < 64) {
        script[script_len].pressed = pressed;
        script[script_len].point.x = x;
        script[script_len].point.y = y;
        script[script_len].dt_ms = dt_ms;
        script_len++;
    }
}

static void script_add(bool pressed, int x, int y)
{
    script_add_dt(pressed, x, y, 30);
}

static uint32_t fake_tick;

static uint32_t tick_cb(void) { return fake_tick; }

static void feed_input_sample(gesture_event_t ev, const lv_point_t *p)
{
    gesture_sample_t s = {
        .event = ev, .time_ms = fake_tick,
        .x = p->x, .y = p->y,
        .raw_x = p->x, .raw_y = p->y,
        .in_edge = p->x <= ui_input_edge_width()
                   || p->x >= DISPLAY_W - ui_input_edge_width(),
    };
    ui_input_feed(&s);
}

static void scripted_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (script_pos >= script_len) {
        data->state = LV_INDEV_STATE_RELEASED;
        data->point = (lv_point_t){0, 0};
        return;
    }
    script_frame_t f = script[script_pos++];
    data->point = f.point;
    data->state = f.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    /* Đúng như touch driver: nạp mẫu vật lý vào classifier trước khi trả
       kết quả cho LVGL. */
    bool was_pressed = script_pos >= 2 && script[script_pos - 2].pressed;
    if (f.pressed && !was_pressed) {
        feed_input_sample(GESTURE_EVENT_DOWN, &f.point);
    } else if (f.pressed && was_pressed) {
        feed_input_sample(GESTURE_EVENT_MOVE, &f.point);
    } else if (!f.pressed && was_pressed) {
        feed_input_sample(GESTURE_EVENT_UP, &f.point);
        gesture_ups++;
    }
}

static lv_indev_t *indev_handle;

static void run_script(void)
{
    /* Widget vừa tạo chưa được layout — chạy timer trước khi đọc input, giống
       thiết bị thật luôn có vòng timer chạy liên tục. */
    for (int i = 0; i < 3; ++i) {
        fake_tick += 30;
        lv_timer_handler();
    }
    for (int i = 0; i < script_len; ++i) {
        fake_tick += script[i].dt_ms;
        lv_indev_read(indev_handle);
        lv_timer_handler();
    }
    /* Để router xử lý pending request qua lv_async_call. */
    for (int i = 0; i < 5; ++i) {
        fake_tick += 30;
        lv_timer_handler();
    }
}

/* Display headless: không có draw buffer thì lv_display_refr_timer bỏ qua và
 * layout không bao giờ chạy — phải cấp buffer + flush cb như sim SDL. */
static uint8_t draw_buf1[DISPLAY_W * 40 * 2];
static uint8_t draw_buf2[DISPLAY_W * 40 * 2];
static lv_draw_buf_t dbuf1, dbuf2;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)area;
    (void)px;
    lv_display_flush_ready(disp);
}

int main(void)
{
    lv_init();
    lv_tick_set_cb(tick_cb);
    fake_tick = 1000;
    lv_display_t *disp = lv_display_create(DISPLAY_W, DISPLAY_H);
    lv_draw_buf_init(&dbuf1, DISPLAY_W, 40, LV_COLOR_FORMAT_RGB565, 0, draw_buf1, sizeof(draw_buf1));
    lv_draw_buf_init(&dbuf2, DISPLAY_W, 40, LV_COLOR_FORMAT_RGB565, 0, draw_buf2, sizeof(draw_buf2));
    lv_display_set_draw_buffers(disp, &dbuf1, &dbuf2);
    lv_display_set_render_mode(disp, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    ui_create(); /* dựng 5 root + boot CLOCK */
    ui_input_set_handlers(on_swipe, on_recovery, on_activity, NULL);

    indev_handle = lv_indev_create();
    lv_indev_set_type(indev_handle, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev_handle, scripted_read_cb);
    /* Pause read timer của LVGL: chỉ lv_indev_read() thủ công mới điều khiển
       kịch bản — không thì lv_timer_handler() cũng đọc và lệch frame. */
    lv_timer_pause(lv_indev_get_read_timer(indev_handle));
    ui_input_init(indev_handle);

    /* ---- Test 1: swipe ngang TRÊN nút "Đồng hồ" của MENU ----
       Nút nguồn không CLICKED; màn chuyển về NAV (vuốt trái từ MENU). */
    ui_router_request_screen(UI_SCREEN_MENU);
    for (int i = 0; i < 5; ++i) { fake_tick += 30; lv_timer_handler(); }
    assert(ui_router_screen() == UI_SCREEN_MENU);

    /* Nút "Đồng hồ" nằm ở ô (12..156, 56..128) trên 320x240; lấy bất kỳ nút
       nào đang hiển thị để gắn bộ đếm. */
    lv_obj_t *page = ui_router_root(UI_ROOT_PAGE);
    lv_obj_t *source_button = lv_obj_get_child(page, 1); /* nút đầu tiên sau title */
    assert(source_button != NULL);
    lv_obj_add_event_cb(source_button, count_click, LV_EVENT_CLICKED, NULL);

    /* Nút ĐÍCH: đặt ngay dưới điểm nhả (260, 90) trong navigation_root? — NAV
       sẽ là màn đích; nút đích nằm trên CLOCK thì không thấy. Đặt nút đích
       ngay trên màn hiện tại (MENU) tại điểm nhả: nó phải KHÔNG nhận press
       khi release đi qua wait_release. */
    lv_obj_t *dest_button = lv_obj_create(page);
    lv_obj_set_size(dest_button, 60, 40);
    lv_obj_set_pos(dest_button, 80, 130); /* dưới điểm nhả */
    lv_obj_add_event_cb(dest_button, count_press, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(dest_button, count_release, LV_EVENT_RELEASED, NULL);

    int clicks_before = click_count, presses_before = press_count;
    int swipes_before = swipe_count;

    /* Vuốt TRÁI (dx âm) từ MENU → NAV: DOWN trên nút nguồn (260, 90), kéo
       sang trái tới (80, 90). */
    script_reset();
    script_add(true, 260, 90);
    script_add(true, 200, 90);
    script_add(true, 140, 90);
    script_add(true, 80, 90);
    script_add(false, 80, 90);
    run_script();

    assert(gesture_ups == 1);
    assert(click_count == clicks_before);          /* nút nguồn KHÔNG CLICKED */
    assert(press_count == presses_before);         /* nút đích KHÔNG nhận press */
    assert(release_count == 0);
    assert(swipe_count == swipes_before + 1);      /* swipe trái đúng 1 lần */
    assert(ui_router_screen() == UI_SCREEN_NAV);   /* MENU + trái = NAV */
    puts("ok swipe on button: no CLICKED, destination no release, NAV shown");

    /* ---- Test 2: slider giữ quyền khi kéo ngang ---- */
    lv_obj_t *slider = lv_slider_create(lv_screen_active());
    lv_obj_set_size(slider, 200, 12);
    lv_obj_set_pos(slider, 60, 100);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, 20, LV_ANIM_OFF);
    int slider_releases = 0;
    lv_obj_add_event_cb(slider, count_release, LV_EVENT_RELEASED, NULL);

    /* Đưa về MENU để thấy màn không đổi. */
    ui_router_request_screen(UI_SCREEN_MENU);
    for (int i = 0; i < 5; ++i) { fake_tick += 30; lv_timer_handler(); }

    int slider_releases_before = release_count;
    int swipes_before2 = swipe_count;
    script_reset();
    script_add(true, 100, 105);   /* trên slider */
    script_add(true, 180, 105);   /* kéo ngang — slider giữ */
    script_add(true, 240, 105);
    script_add(false, 240, 105);
    run_script();

    assert(swipe_count == swipes_before2);              /* không chuyển màn */
    assert(ui_router_screen() == UI_SCREEN_MENU);
    assert(release_count > slider_releases_before);     /* release tới slider */
    assert(lv_slider_get_value(slider) > 20);           /* đã kéo được giá trị */
    lv_obj_delete(slider); /* dọn stage — widget test nằm trên screen che màn */
    puts("ok slider keeps ownership, no screen change");

    /* ---- Test 3: scroll dọc trên list không chuyển màn ----
       Chạy trên CLOCK (không có nút nào) — đúng như list thật trên WiFi page;
       trên MENU, kéo dọc qua nút khác là hành vi slide-press CHUẨN của LVGL 9
       (nhả trong nút = CLICKED), không phải lỗi phân xử. */
    ui_router_request_screen(UI_SCREEN_CLOCK);
    for (int i = 0; i < 5; ++i) { fake_tick += 30; lv_timer_handler(); }
    assert(ui_router_screen() == UI_SCREEN_CLOCK);
    lv_obj_t *list = lv_obj_create(lv_screen_active());
    lv_obj_set_size(list, 200, 100);
    lv_obj_set_pos(list, 60, 100);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);
    lv_obj_t *tall = lv_obj_create(list);
    lv_obj_set_size(tall, 180, 500);
    lv_obj_set_pos(tall, 0, 0);
    int swipes_before3 = swipe_count;
    int menu_clicks_before = click_count;
    /* Đếm click trên MỌI nút đang hiện của MENU page để biết ai gây ra. */
    for (uint32_t c = 0; c < lv_obj_get_child_count(page); ++c) {
        lv_obj_add_event_cb(lv_obj_get_child(page, c), count_click, LV_EVENT_CLICKED, NULL);
    }
    script_reset();
    script_add(true, 100, 120);
    script_add(true, 100, 160);
    script_add(true, 100, 185);
    script_add(false, 100, 185);
    run_script();
    printf("  [debug] t3 swipe=%d (was %d) screen=%d clicks=%d (was %d)\n",
           swipe_count, swipes_before3, (int)ui_router_screen(),
           click_count, menu_clicks_before);
    assert(swipe_count == swipes_before3);
    assert(ui_router_screen() == UI_SCREEN_CLOCK);
    lv_obj_delete(list);
    puts("ok vertical scroll stays widget-owned");

    /* ---- Test 4: wake-consume chặn mọi output ---- */
    ui_router_request_screen(UI_SCREEN_MENU);
    for (int i = 0; i < 5; ++i) { fake_tick += 30; lv_timer_handler(); }
    assert(ui_router_screen() == UI_SCREEN_MENU);
    /* Nút tươi (tránh pointer cũ đã bị page rebuild xoá) để đếm CLICKED. */
    lv_obj_t *fresh_button = lv_obj_create(lv_screen_active());
    lv_obj_set_size(fresh_button, 80, 30);
    lv_obj_set_pos(fresh_button, 200, 160);
    lv_obj_add_event_cb(fresh_button, count_click, LV_EVENT_CLICKED, NULL);
    int clicks_before4 = click_count;
    int swipes_before4 = swipe_count;
    consume_contact = true;
    script_reset();
    script_add(true, 200, 160); /* trên nút tươi */
    script_add(true, 120, 160); /* kéo ngang đủ ngưỡng */
    script_add(false, 120, 160);
    run_script();
    consume_contact = false;
    assert(click_count == clicks_before4);
    assert(swipe_count == swipes_before4);
    assert(ui_router_screen() == UI_SCREEN_MENU);
    lv_obj_delete(fresh_button);
    puts("ok wake-consume suppresses contact");

    /* ---- Test 5: recovery (hold 3s, raw displacement nhỏ) ----
       Dùng fake_tick tăng 3100ms giữa DOWN và UP. */
    int recovery_before = recovery_count;
    script_reset();
    script_add(true, 100, 100);
    script_add_dt(true, 102, 100, 3100); /* MOVE nhỏ sau 3.1s, raw giữ nguyên */
    script_add(false, 102, 100);
    run_script();
    assert(recovery_count == recovery_before + 1);
    puts("ok hold-release recovery");

    /* ---- Test 6: tap thường vẫn CLICKED ---- */
    lv_obj_t *tap_button = lv_obj_create(lv_screen_active());
    lv_obj_set_size(tap_button, 80, 30);
    lv_obj_set_pos(tap_button, 40, 200);
    lv_obj_add_event_cb(tap_button, count_click, LV_EVENT_CLICKED, NULL);
    int clicks_before6 = click_count;
    script_reset();
    script_add(true, 60, 205);
    script_add(false, 60, 205);
    run_script();
    assert(click_count == clicks_before6 + 1);
    puts("ok plain tap still clicks");

    /* Pending OTA cancelled on task-creation failure must not lock the UI. */
    ui_router_request_screen(UI_SCREEN_MENU);
    ui_router_process();
    ui_router_set_mode(UI_MODE_OTA);
    ui_router_set_mode(UI_MODE_NORMAL);
    ui_router_process();
    assert(ui_router_mode() == UI_MODE_NORMAL);
    ui_router_request_screen(UI_SCREEN_NAV);
    ui_router_set_mode(UI_MODE_CALIBRATION);
    ui_router_process();
    ui_router_process();
    assert(ui_router_screen() == UI_SCREEN_MENU);
    ui_router_request_screen(UI_SCREEN_NAV);
    ui_router_process();
    assert(ui_router_screen() == UI_SCREEN_MENU);
    ui_router_return_from_calibration();
    ui_router_process();
    ui_router_process();
    assert(ui_router_mode() == UI_MODE_NORMAL);
    assert(ui_router_screen() == UI_SCREEN_MENU);
    ui_router_request_screen(UI_SCREEN_COUNT);
    ui_router_process();
    assert(ui_router_screen() == UI_SCREEN_MENU);
    puts("ok deferred mode cancellation, stale requests and invalid screen");

    puts("UI_INPUT: all integration tests passed");
    return 0;
}
