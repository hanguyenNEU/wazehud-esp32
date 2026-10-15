#pragma once

/* Router màn hình — một LVGL screen, các root container cố định (§3.1).
 *
 * Trật tự z (tạo theo thứ tự này):
 *   1. navigation_root  — cây HUD, tạo một lần, ẩn khi không ở NAV
 *   2. clock_root       — đồng hồ toàn màn
 *   3. page_root        — MENU/WIFI/SETTINGS, chỉ một page được dựng
 *   4. status_root      — chấm BLE/Wi-Fi, không nhận touch
 *   5. system_root      — calibration/OTA/provisioning, ưu tiên cao nhất
 *
 * Mọi request đổi màn được defer tới điểm an toàn trong LVGL (timer cố định),
 * không xóa cây object trong indev read callback. Chỉ một pending request cố
 * định; mode change vô hiệu request cũ. Request tới chính màn hiện tại là
 * no-op. Page destruction dọn timer/callback/user data của page đó.
 */

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UI_SCREEN_CLOCK = 0,
    UI_SCREEN_MENU,
    UI_SCREEN_WIFI,
    UI_SCREEN_NAV,
    UI_SCREEN_SETTINGS,
    UI_SCREEN_TRIP,      /* Thống kê chuyến đi — page_root */
    UI_SCREEN_COUNT,
} ui_screen_t;

typedef enum {
    UI_ROOT_NAVIGATION = 0,
    UI_ROOT_CLOCK,
    UI_ROOT_PAGE,
    UI_ROOT_STATUS,
    UI_ROOT_SYSTEM,
    UI_ROOT_COUNT,
} ui_root_t;

typedef enum {
    UI_MODE_NORMAL = 0,
    UI_MODE_CALIBRATION,       /* khóa swipe + widget thường; touch cho hiệu chỉnh */
    UI_MODE_WIFI_PROVISIONING, /* chỉ hướng dẫn AP; không điều hướng sang NAV */
    UI_MODE_OTA,               /* khóa swipe/settings/calibration */
} ui_mode_t;

/* Page lifecycle: enter dựng cây trong parent, exit dọn sạch (timers,
 * callbacks, user data, con trỏ về NULL). exit có thể gọi nhiều lần. */
typedef struct {
    const char *name;
    void (*enter)(lv_obj_t *parent);
    void (*exit)(void);
} ui_page_t;

void ui_router_init(void);

lv_obj_t *ui_router_root(ui_root_t root);
uint32_t ui_router_generation(void);
ui_screen_t ui_router_screen(void);
ui_mode_t ui_router_mode(void);

/* Đổi màn — an toàn gọi từ bất kỳ context LVGL nào (kể cả indev read cb).
   No-op nếu trùng màn hiện tại hoặc mode đang khóa điều hướng. */
void ui_router_request_screen(ui_screen_t screen);

/* Dựng màn ĐẦU TIÊN đồng bộ — chỉ gọi lúc boot (trước vòng render đầu), để
   không hiện HUD một frame rồi mới phủ CLOCK (§2.1). */
void ui_router_boot_screen(ui_screen_t screen);

/* Vuốt có hướng theo bảng đã chốt (§2.1). Giữ nguyên màn khi đích là chính nó. */
void ui_router_swipe(bool rightward);

/* Caller must own the LVGL context/lock, including callers from other tasks.
   Đổi mode; vô hiệu pending request. Khi vào CALIBRATION giữ màn trước để trả
   về khi thoát; set_mode(NORMAL) tự trả về màn đó nếu đang ở CALIBRATION. */
void ui_router_set_mode(ui_mode_t mode);
void ui_router_return_from_calibration(void);

/* Xử lý pending request — do router tự nối qua timer, không cần gọi
   ngoài. Công khai chỉ để test đồng bộ. */
void ui_router_process(void);

/* true khi mode hiện tại khóa input thường (touch driver không đọc bus). */
bool ui_router_input_locked(void);

/* Đăng ký page cho CLOCK/MENU/WIFI/SETTINGS (NAV dùng navigation_root). */
void ui_router_register_page(ui_screen_t screen, const ui_page_t *page);
