/* P6 — Repeated navigation: 500 lần chuyển màn xen kẽ + cập nhật HUD.
 * Widget/timer count ổn định, heap không giảm liên tục (theo dõi largest
 * block ngoài total free — allocator cache không đòi hỏi bằng nhau tuyệt đối). */
#include "../../firmware/main/ui/ui_navigator.c"
#include "ui_clock.h"
#include "ui_router.h"
#include "ui_settings.h"
#include "ui_wifi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t draw_buf1[320 * 40 * 2];
static uint8_t draw_buf2[320 * 40 * 2];
static lv_draw_buf_t dbuf1, dbuf2;

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    (void)area;
    (void)px;
    lv_display_flush_ready(disp);
}

static uint32_t fake_tick;
static uint32_t tick_cb(void) { return fake_tick; }

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

    ui_create();
    ui_set_link(UI_LINK_LIVE);

    lv_mem_monitor_t mem;
    lv_mem_monitor(&mem);
    uint32_t free0 = mem.free_size;
    uint32_t big0 = mem.free_biggest_size;
    uint32_t used0 = mem.total_size - mem.free_size;
    printf("warmup: used=%u free=%u biggest=%u\n", used0, free0, big0);

    /* Warmup 50 vòng để allocator cache ổn định. */
    for (int i = 0; i < 50; ++i) {
        ui_router_request_screen((i % 2) ? UI_SCREEN_MENU : UI_SCREEN_NAV);
        for (int k = 0; k < 3; ++k) { fake_tick += 30; lv_timer_handler(); }
    }
    for (int k = 0; k < 5; ++k) { fake_tick += 30; lv_timer_handler(); }

    ui_screen_t screens[] = {
        UI_SCREEN_CLOCK, UI_SCREEN_MENU, UI_SCREEN_WIFI,
        UI_SCREEN_NAV, UI_SCREEN_SETTINGS, UI_SCREEN_TRIP,
    };
    const int nscreens = (int)(sizeof(screens) / sizeof(screens[0]));
    hud_state_t s;
    hud_state_reset(&s);
    s.limit = 60;
    s.speed = 45;
    s.nav = true;
    snprintf(s.street, sizeof(s.street), "%s", "Đường thử nghiệm dài");
    ui_wifi_scan_entry_t scan[3];
    for (int i = 0; i < 3; ++i) {
        snprintf(scan[i].ssid, sizeof(scan[i].ssid), "WifiNha-%d", i);
        scan[i].rssi = -40 - i * 10;
    }

    int loop = 0;
    for (; loop < 1000; ++loop) {
        ui_router_request_screen(screens[(loop * 7 + loop / 3) % nscreens]);
        ui_update(&s);           /* cập nhật HUD xen kẽ khi ẩn */
        ui_clock_set_time(1727280000 + loop, 420, true);
        ui_wifi_set_scan_list(scan, 3);
        for (int k = 0; k < 3; ++k) { fake_tick += 30; lv_timer_handler(); }
        if (loop % 200 == 199) {
            lv_mem_monitor(&mem);
            printf("  loop %d: used=%u free=%u biggest=%u\n", loop + 1,
                   mem.total_size - mem.free_size, mem.free_size, mem.free_biggest_size);
        }
    }
    for (int k = 0; k < 10; ++k) { fake_tick += 30; lv_timer_handler(); }

    /* Sau warmup, chạy thêm 100 vòng cho cache của LVGL lắng rồi MỚI lấy
       mốc so sánh — mốc đầu tiên (trước bất kỳ render/page nào) thấp giả tạo. */
    for (int i = 0; i < 100; ++i) {
        ui_router_request_screen(screens[(i * 7 + i / 3) % nscreens]);
        ui_update(&s);
        ui_clock_set_time(1727280000 + i, 420, true);
        ui_wifi_set_scan_list(scan, 3);
        for (int k = 0; k < 3; ++k) { fake_tick += 30; lv_timer_handler(); }
    }
    lv_mem_monitor(&mem);
    used0 = mem.total_size - mem.free_size;
    big0 = mem.free_biggest_size;
    free0 = mem.free_size;
    printf("baseline after settle: used=%u free=%u biggest=%u\n", used0, free0, big0);

    /* Measure actual work BETWEEN the two samples, ending on the same page. */
    ui_screen_t baseline_screen = ui_router_screen();
    unsigned children0 = lv_obj_get_child_count(ui_router_root(UI_ROOT_PAGE));
    for (int i = 0; i < 1000; ++i) {
        ui_router_request_screen(screens[i % nscreens]);
        ui_update(&s);
        ui_clock_set_time(1727280000 + i, 420, true);
        ui_wifi_set_scan_list(scan, 3);
        for (int k = 0; k < 3; ++k) { fake_tick += 30; lv_timer_handler(); }
    }
    ui_router_request_screen(baseline_screen);
    for (int k = 0; k < 10; ++k) { fake_tick += 30; lv_timer_handler(); }
    assert(lv_obj_get_child_count(ui_router_root(UI_ROOT_PAGE)) == children0);
    lv_mem_monitor(&mem);
    uint32_t used1 = mem.total_size - mem.free_size;
    uint32_t free1 = mem.free_size;
    uint32_t big1 = mem.free_biggest_size;
    printf("after %d switches: used=%u free=%u biggest=%u\n", loop, used1, free1, big1);

    /* Tiêu chí: dùng thêm không quá 8KB (không leak tuyến tính) và largest
       block không co hơn 4KB — so với MỐC SAU WARMUP vì cấp phát đầu tiên của
       LVGL (layer/style cache, render buffer) làm co largest block một lần
       rồi ổn định; allocator cache khiến số free dao động, không đòi bằng
       nhau tuyệt đối (§9 kế hoạch). */
    int64_t used_growth = (int64_t)used1 - (int64_t)used0;
    printf("used growth: %lld B\n", (long long)used_growth);
    assert(used_growth < 8192);
    assert((int64_t)big0 - (int64_t)big1 < 4096);

    /* Widget/timer count: ổn định sau vòng cuối. */
    lv_obj_t *page = ui_router_root(UI_ROOT_PAGE);
    printf("final screen=%d page children=%u\n", (int)ui_router_screen(),
           (unsigned)lv_obj_get_child_count(page));

    puts("MEMORY: 1000 measured switches without leak trend");
    return 0;
}
