/* Simulator HUD trên máy tính.
 *
 * Chạy ĐÚNG file ui_navigator.c, hud_decode.c và hlp_core.c sẽ nạp vào ESP32, chỉ
 * thay màn hình vật lý bằng cửa sổ SDL. Nhờ vậy chỉnh bố cục mất 2 giây thay vì
 * 20 giây build–nạp–khởi động lại, và làm được khi chưa có phần cứng.
 *
 *   ./sim/build/hudsim <file.hlp|file.jsonl> [--zoom 3] [--loop]
 *   ./sim/build/hudsim <file> --mm            (cửa sổ đúng cỡ vật lý màn thật)
 *   ./sim/build/hudsim <file> --mm --ppi 218  (nếu màn Mac khác 254 PPI)
 *
 * Phím: khoảng trắng tạm dừng · d/n/a đổi theme · l đổi bố cục · q thoát
 */

#include "lvgl.h"
#include "hlp_core.h"
#include "hud_decode.h"
#include "board.h"
#include "ui_navigator.h"
#include "ui_system.h"
#include "ui_router.h"
#include "ui_trip.h"
#include "ui_clock.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FRAMES 20000

static hud_state_t frames[MAX_FRAMES];
static uint32_t frame_ts[MAX_FRAMES];
static int frame_count;

static hud_state_t scratch;

static uint32_t tick_cb(void) { return SDL_GetTicks(); }

/* Ghi ảnh BMP 24-bit. Đủ để soi bố cục mà không kéo thêm thư viện ảnh nào. */
static void write_bmp(const char *path, const uint8_t *rgb888, int w, int h)
{
    FILE *fh = fopen(path, "wb");
    if (!fh) { perror(path); return; }
    int row = (w * 3 + 3) & ~3;
    int size = 54 + row * h;
    uint8_t hdr[54] = {0};
    hdr[0] = 'B'; hdr[1] = 'M';
    hdr[2] = size; hdr[3] = size >> 8; hdr[4] = size >> 16; hdr[5] = size >> 24;
    hdr[10] = 54; hdr[14] = 40;
    hdr[18] = w; hdr[19] = w >> 8;
    hdr[22] = h; hdr[23] = h >> 8;   /* dương = lưu từ dưới lên */
    hdr[26] = 1; hdr[28] = 24;
    fwrite(hdr, 1, 54, fh);
    uint8_t pad[3] = {0};
    for (int y = h - 1; y >= 0; --y) {
        for (int x = 0; x < w; ++x) {
            /* LVGL lưu RGB888 theo thứ tự B,G,R trong bộ nhớ — trùng luôn
               thứ tự BMP mong đợi, nên ghi thẳng, đảo là hỏng màu. */
            fwrite(rgb888 + (y * w + x) * 3, 1, 3, fh);
        }
        fwrite(pad, 1, row - w * 3, fh);
    }
    fclose(fh);
}

static void shoot(lv_display_t *disp, const char *path)
{
    lv_refr_now(disp);
    lv_draw_buf_t *snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB888);
    if (!snap) { fprintf(stderr, "snapshot lỗi\n"); return; }
    write_bmp(path, snap->data, DISPLAY_W, DISPLAY_H);
    lv_draw_buf_destroy(snap);
}

static void collect(const char *line, size_t length, void *user)
{
    (void)user;
    if (frame_count >= MAX_FRAMES) return;
    if (!hud_decode_state(line, length, &scratch)) return;
    frames[frame_count] = scratch;
    frame_ts[frame_count] = scratch.ts;
    frame_count++;
}

/* File .jsonl là bản thu của tools/fake_esp32.py, mỗi dòng bọc message HLP
   trong {"ms":…,"line":…}. File khác coi như đã là byte HLP thô. */
static int load(const char *path)
{
    FILE *fh = fopen(path, "rb");
    if (!fh) {
        perror(path);
        return -1;
    }
    bool jsonl = strstr(path, ".jsonl") != NULL;

    hlp_rx_t rx;
    hlp_rx_init(&rx, collect, NULL);

    if (!jsonl) {
        uint8_t buf[64];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), fh)) > 0) hlp_rx_feed(&rx, buf, n);
    } else {
        char *line = NULL;
        size_t cap = 0;
        ssize_t len;
        while ((len = getline(&line, &cap, fh)) > 0) {
            /* JSON cho phép khoảng trắng sau dấu hai chấm, và json.dumps của
               Python thêm một dấu cách — bản trước tìm đúng "line":" nên bỏ
               qua sạch mọi capture do fake_esp32.py ghi ra. */
            const char *start = strstr(line, "\"line\"");
            if (!start) continue;
            start += 6;
            while (*start == ' ' || *start == '\t') ++start;
            if (*start != ':') continue;
            ++start;
            while (*start == ' ' || *start == '\t') ++start;
            if (*start != '"') continue;
            ++start;
            /* Bỏ escape của JSON: chỉ gặp \" và \\ trong dữ liệu thật. */
            char out[1024];
            size_t o = 0;
            for (const char *c = start; *c && o < sizeof(out) - 2; ++c) {
                if (*c == '\\' && c[1]) {
                    ++c;
                    out[o++] = (*c == 'n') ? '\n' : (*c == 't') ? '\t' : *c;
                } else if (*c == '"') {
                    break;
                } else {
                    out[o++] = *c;
                }
            }
            out[o++] = '\n';
            hlp_rx_feed(&rx, (const uint8_t *)out, o);
        }
        free(line);
    }
    fclose(fh);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "dùng: %s <file.hlp|file.jsonl> [--zoom N] [--mm] [--ppi N] [--loop]\n",
                argv[0]);
        return 2;
    }
    float zoom = 3.0f;
    /* --mm dựng cửa sổ đúng bằng kích thước vật lý của panel, để đánh giá xem
       liếc mắt có đọc được không — thứ mà --zoom 3 không trả lời được.
       Mặc định 254 PPI là màn Liquid Retina XDR của MacBook Pro 14"/16".
       macOS vẽ Retina ở tỉ lệ 2, và cửa sổ SDL tính bằng ĐIỂM chứ không phải
       pixel, nên chia đôi. Màn khác thì truyền --ppi và --scale. */
    bool physical = false;
    float ppi = 254.0f, backing = 2.0f;
    bool loop = false;
    const char *shot_dir = NULL;
    const char *menu_shot_dir = NULL;
    bool welcome = false;
    bool screens = false;
    /* --link wait|handshake|stale: giữ HUD ở màn chờ để soi bố cục của nó. */
    ui_link_t link = UI_LINK_LIVE;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--zoom") && i + 1 < argc) zoom = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--mm")) physical = true;
        else if (!strcmp(argv[i], "--ppi") && i + 1 < argc) ppi = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) backing = (float)atof(argv[++i]);
        else if (!strcmp(argv[i], "--loop")) loop = true;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot_dir = argv[++i];
        else if (!strcmp(argv[i], "--welcome")) welcome = true;
        else if (!strcmp(argv[i], "--screens")) screens = true;
        else if (!strcmp(argv[i], "--link") && i + 1 < argc) {
            const char *v = argv[++i];
            link = !strcmp(v, "wait")        ? UI_LINK_ADVERTISING
                   : !strcmp(v, "handshake") ? UI_LINK_CONNECTED
                   : !strcmp(v, "stale")     ? UI_LINK_STALE
                                             : UI_LINK_LIVE;
        }
        else if (!strcmp(argv[i], "--menu-shot") && i + 1 < argc) menu_shot_dir = argv[++i];
    }

    if (load(argv[1]) != 0) return 1;
    if (frame_count == 0) {
        fprintf(stderr, "không dựng lại được message trạng thái nào\n");
        return 1;
    }
    printf("nạp %d message trạng thái từ %s\n", frame_count, argv[1]);

    lv_init();
    lv_tick_set_cb(tick_cb);

    if (physical) {
        float points_per_inch = ppi / backing;
        float want_points = DISPLAY_MM_W / 25.4f * points_per_inch;
        zoom = want_points / (float)DISPLAY_W;
        printf("cỡ vật lý: %.1f x %.1f mm (%.0f PPI panel) → cửa sổ %.0f x %.0f điểm, zoom %.3f\n",
               DISPLAY_MM_W, DISPLAY_MM_H,
               DISPLAY_W / (DISPLAY_MM_W / 25.4f),
               want_points, DISPLAY_MM_H / 25.4f * points_per_inch, zoom);
        printf("  (giả định màn Mac %.0f PPI, Retina x%.0f — sai thì dùng --ppi/--scale)\n",
               ppi, backing);
    }

    lv_display_t *disp = lv_sdl_window_create(DISPLAY_W, DISPLAY_H);
    lv_sdl_window_set_zoom(disp, zoom);
    char title[64];
    snprintf(title, sizeof(title), "Waze HUD — %dx%d", DISPLAY_W, DISPLAY_H);
    lv_sdl_window_set_title(disp, title);

    ui_create();
    if (welcome) ui_show_welcome();
    ui_set_link(link);
    ui_set_local_hour(20); /* mặc định xem ở chế độ ban đêm */

    /* Chụp hoạt ảnh chào mừng: để LVGL chạy thật rồi bắt từng khoảnh khắc. */
    if (shot_dir && welcome) {
        char path[512];
        for (int i = 0; i < 10; ++i) {
            uint32_t until = SDL_GetTicks() + 150;
            while (SDL_GetTicks() < until) { lv_timer_handler(); SDL_Delay(5); }
            snprintf(path, sizeof(path), "%s/welcome_%02d.bmp", shot_dir, i);
            shoot(disp, path);
        }
        printf("đã chụp 10 khoảnh khắc hoạt ảnh\n");
        return 0;
    }

    /* Chế độ test menu: gọi thẳng ui_router_swipe() — đúng API mà
       touch_es3c28p.c gọi trên phần cứng thật khi vuốt — để soi layout
       DRIVE/HOME bằng ảnh chụp, khỏi phải nạp firmware thật lên board rồi
       vuốt thử. Chạm nút (Cài đặt/WiFi/Quay lại/...) giờ do LVGL tự hit-test
       qua indev chuẩn (menu_event_cb trong ui_navigator.c) — muốn test đường đó
       trong simulator cần mô phỏng thêm chuột SDL (lv_sdl_mouse_create +
       SDL_PushEvent toạ độ đích), chưa làm ở đây; xem layout để đối chiếu
       toạ độ bằng mắt là đủ cho mục đích hiện tại. */
    if (menu_shot_dir) {
        char path[512];
        int shot_n = 0;
        #define MENU_SHOOT(label) do { \
            lv_timer_handler(); \
            snprintf(path, sizeof(path), "%s/%02d_%s.bmp", menu_shot_dir, shot_n++, label); \
            shoot(disp, path); \
            printf("%s -> %s\n", label, path); \
        } while (0)

        ui_router_request_screen(UI_SCREEN_NAV);
        MENU_SHOOT("drive");

        ui_router_swipe(true);           /* dx=+200: NAV -> MENU */
        MENU_SHOOT("menu");

        ui_router_swipe(false);          /* dx=-200: MENU -> NAV */
        MENU_SHOOT("menu_back_to_nav_negative_swipe");

        #undef MENU_SHOOT
        return 0;
    }

    /* Chế độ chụp: xuất một loạt khung ra BMP rồi thoát, để soi bố cục bằng mắt
       mà không cần mở cửa sổ. */
    if (shot_dir && !screens) {
        ui_router_boot_screen(UI_SCREEN_NAV);
        ui_set_local_datetime(19, 54, 1, 7, 9, 2026);
        int picks[] = {0, frame_count / 6, frame_count / 3, frame_count / 2,
                       frame_count * 2 / 3, frame_count * 5 / 6, frame_count - 1};
        char path[512];
        for (unsigned i = 0; i < sizeof(picks) / sizeof(picks[0]); ++i) {
            int f = picks[i];
            if (f < 0 || f >= frame_count) continue;
            ui_set_theme(UI_THEME_NIGHT);

            ui_set_layout(UI_LAYOUT_SPEED);
            ui_update(&frames[f]);
            snprintf(path, sizeof(path), "%s/frame_%02u_band.bmp", shot_dir, i);
            shoot(disp, path);

            ui_set_layout(UI_LAYOUT_WAZE);
            ui_update(&frames[f]);
            snprintf(path, sizeof(path), "%s/frame_%02u_waze.bmp", shot_dir, i);
            shoot(disp, path);
            printf("khung %d/%d → %s\n", f, frame_count, path);
        }
        ui_set_theme(UI_THEME_AUTO);
        return 0;
    }

    /* Chế độ chụp 5 màn (kiến trúc mới): CLOCK/MENU/WIFI/SETTINGS/NAV — router
       chuyển màn qua lv_async_call nên phải chạy lv_timer_handler giữa các
       request. Dùng để đối chiếu layout các màn mới (P6). */
    if (shot_dir && screens) {
        char path[512];
        #define SCREEN_SHOOT(label) do { \
            for (int k = 0; k < 20; ++k) { \
                uint32_t until = SDL_GetTicks() + 30; \
                while (SDL_GetTicks() < until) { lv_timer_handler(); SDL_Delay(3); } \
            } \
            snprintf(path, sizeof(path), "%s/screen_%s.bmp", shot_dir, label); \
            shoot(disp, path); \
            printf("%s -> %s\n", label, path); \
        } while (0)

        ui_clock_set_time(1727280000, 420, true); /* 25/9/2024 17:00 VN */
        ui_set_local_datetime(19, 54, 1, 7, 9, 2026); /* khớp baseline NAV */
        SCREEN_SHOOT("clock");
        ui_router_request_screen(UI_SCREEN_MENU);
        SCREEN_SHOOT("menu");
        ui_router_request_screen(UI_SCREEN_WIFI);
        SCREEN_SHOOT("wifi");
        ui_router_request_screen(UI_SCREEN_SETTINGS);
        SCREEN_SHOOT("settings");
        ui_router_request_screen(UI_SCREEN_TRIP);
        SCREEN_SHOOT("trip_empty");
        ui_trip_set(&(trip_stats_t){.month = 10, .last_month = 9,
                                    .today = {68.6f, 115}, .yesterday = {12.3f, 30},
                                    .this_month = {638.7f, 1087}, .prev_month = {1520.4f, 2551}});
        SCREEN_SHOOT("trip");
        ui_router_request_screen(UI_SCREEN_NAV);
        ui_update(&frames[0]); /* cùng input như baseline frame_00 */
        SCREEN_SHOOT("nav");
        #undef SCREEN_SHOOT
        return 0;
    }

    int index = 0;
    bool paused = false;
    bool waze_layout = true;
    uint32_t next_at = SDL_GetTicks();

    ui_update(&frames[0]);

    for (;;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) return 0;
            if (ev.type != SDL_KEYDOWN) continue;
            switch (ev.key.keysym.sym) {
            case SDLK_q:     return 0;
            case SDLK_SPACE: paused = !paused; break;
            case SDLK_d:     ui_set_theme(UI_THEME_DAY); break;
            case SDLK_n:     ui_set_theme(UI_THEME_NIGHT); break;
            case SDLK_a:     ui_set_theme(UI_THEME_AUTO); break;
            case SDLK_l:
                waze_layout = !waze_layout;
                ui_set_layout(waze_layout ? UI_LAYOUT_WAZE : UI_LAYOUT_SPEED);
                ui_update(&frames[index]);
                break;
            case SDLK_RIGHT:
                index = (index + 1) % frame_count;
                ui_update(&frames[index]);
                break;
            case SDLK_LEFT:
                index = (index + frame_count - 1) % frame_count;
                ui_update(&frames[index]);
                break;
            default: break;
            }
        }

        uint32_t now = SDL_GetTicks();
        if (!paused && now >= next_at && frame_count > 1) {
            int next = index + 1;
            if (next >= frame_count) {
                if (!loop) { paused = true; next = index; }
                else next = 0;
            }
            /* Phát lại đúng nhịp gốc: `ts` là uptime bên phát tính bằng ms.
               Chặn trên 1 giây để lúc nhảy vòng hoặc đổi phiên không đứng hình. */
            int32_t delta = (int32_t)(frame_ts[next] - frame_ts[index]);
            if (delta < 0 || delta > 1000) delta = 250;
            index = next;
            ui_update(&frames[index]);
            next_at = now + (uint32_t)delta;
        }

        lv_timer_handler();
        SDL_Delay(5);
    }
}
