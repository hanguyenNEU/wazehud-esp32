#include "ui_router.h"

#include <string.h>

static lv_obj_t *roots[UI_ROOT_COUNT];
static const ui_page_t *pages[UI_SCREEN_COUNT];
static ui_screen_t current_screen = UI_SCREEN_CLOCK;
static ui_mode_t current_mode = UI_MODE_NORMAL;
static ui_screen_t screen_before_calibration = UI_SCREEN_CLOCK;
static uint32_t generation = 1;

/* Pending request: một slot cố định — không cấp phát job mỗi lần move. */
static struct {
    bool valid;
    uint32_t gen;
    ui_screen_t screen;
} pending;
static struct {
    bool valid;
    uint32_t gen;
    ui_mode_t mode;
} pending_mode;
static lv_timer_t *pending_timer;

static bool screen_is_page(ui_screen_t s)
{
    return s == UI_SCREEN_MENU || s == UI_SCREEN_WIFI || s == UI_SCREEN_SETTINGS
           || s == UI_SCREEN_TRIP;
}

static bool screen_is_nav(ui_screen_t s)
{
    return s == UI_SCREEN_NAV;
}

static bool nav_allowed(void)
{
    /* Provisioning: không khởi tạo đủ năm cây màn, không điều hướng sang NAV.
       Calibration/OTA: khóa điều hướng hoàn toàn. */
    return current_mode == UI_MODE_NORMAL;
}

static void process_cb(lv_timer_t *timer);

void ui_router_init(void)
{
    if (roots[0]) return;
    memset(pages, 0, sizeof(pages));

    lv_obj_t *screen = lv_screen_active();

    for (int i = 0; i < UI_ROOT_COUNT; ++i) {
        lv_obj_t *root = lv_obj_create(screen);
        lv_obj_set_size(root, LV_PCT(100), LV_PCT(100));
        lv_obj_set_pos(root, 0, 0);
        lv_obj_set_style_bg_opa(root, LV_OPA_TRANSP, 0);
        if (i == UI_ROOT_CLOCK || i == UI_ROOT_PAGE) {
            lv_obj_set_style_bg_color(root, lv_color_black(), 0);
            lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
        }
        lv_obj_set_style_border_width(root, 0, 0);
        lv_obj_set_style_radius(root, 0, 0);
        lv_obj_set_style_pad_all(root, 0, 0);
        lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        /* Root KHÔNG clickable: full-screen trên nhau, clickable sẽ nuốt hết
           touch của widget lớp dưới (status_root chặn nút của page). Widget
           con vẫn nhận sự kiện bình thường — hit test duyệt con trước cha. */
        lv_obj_remove_flag(root, LV_OBJ_FLAG_CLICKABLE);
        roots[i] = root;
    }

    /* Pending request được xử lý bởi MỘT timer cố định (§3.1: "dùng một
       pending request cố định, không cấp phát một job mỗi lần move") — không
       dùng lv_async_call vì nó cấp phát job mới cho từng request. */
    pending_timer = lv_timer_create(process_cb, 30, NULL);
}

lv_obj_t *ui_router_root(ui_root_t root)
{
    return roots[root];
}

uint32_t ui_router_generation(void) { return generation; }
ui_screen_t ui_router_screen(void) { return current_screen; }
ui_mode_t ui_router_mode(void) { return current_mode; }
bool ui_router_input_locked(void) { return current_mode != UI_MODE_NORMAL; }

static void enter_screen(ui_screen_t screen)
{
    /* Rời màn cũ: dọn page cũ. Exit cb chỉ NULL pointer/dọn state ngoài cây;
       WIDGET thì xóa tại đây — nếu không, mỗi lần vào CLOCK lại chồng thêm
       một bộ label mới (rò ~6 widget/lần). */
    if (screen_is_page(current_screen)) {
        if (pages[current_screen]) pages[current_screen]->exit();
        lv_obj_clean(roots[UI_ROOT_PAGE]);
    }
    if (current_screen == UI_SCREEN_CLOCK) {
        if (pages[UI_SCREEN_CLOCK]) pages[UI_SCREEN_CLOCK]->exit();
        lv_obj_clean(roots[UI_ROOT_CLOCK]);
    }

    if (screen_is_nav(screen)) {
        lv_obj_add_flag(roots[UI_ROOT_CLOCK], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(roots[UI_ROOT_PAGE], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(roots[UI_ROOT_NAVIGATION], LV_OBJ_FLAG_HIDDEN);
        current_screen = UI_SCREEN_NAV;
        return;
    }

    lv_obj_add_flag(roots[UI_ROOT_NAVIGATION], LV_OBJ_FLAG_HIDDEN);

    if (screen == UI_SCREEN_CLOCK) {
        lv_obj_add_flag(roots[UI_ROOT_PAGE], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(roots[UI_ROOT_CLOCK], LV_OBJ_FLAG_HIDDEN);
        if (pages[UI_SCREEN_CLOCK]) pages[UI_SCREEN_CLOCK]->enter(roots[UI_ROOT_CLOCK]);
        current_screen = UI_SCREEN_CLOCK;
        return;
    }

    /* MENU/WIFI/SETTINGS: page_root. */
    lv_obj_add_flag(roots[UI_ROOT_CLOCK], LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(roots[UI_ROOT_PAGE]);
    lv_obj_remove_flag(roots[UI_ROOT_PAGE], LV_OBJ_FLAG_HIDDEN);
    if (pages[screen]) pages[screen]->enter(roots[UI_ROOT_PAGE]);
    current_screen = screen;
}

static void apply_mode(ui_mode_t mode)
{
    if (mode == current_mode) return;
    ui_mode_t previous_mode = current_mode;
    ++generation;
    if (mode == UI_MODE_CALIBRATION && current_mode == UI_MODE_NORMAL) {
        screen_before_calibration = current_screen;
    }
    current_mode = mode;
    if (mode == UI_MODE_NORMAL && previous_mode == UI_MODE_CALIBRATION) {
        /* Trả về màn trước calibration nếu đang ở calibration. */
        if (screen_before_calibration != current_screen) {
            pending.valid = true;
            pending.gen = generation;
            pending.screen = screen_before_calibration;
        }
    }
}

static void process_cb(lv_timer_t *timer)
{
    (void)timer;
    /* Mode change trước — nó vô hiệu pending screen cũ. */
    if (pending_mode.valid) {
        if (pending_mode.gen == generation) apply_mode(pending_mode.mode);
        pending_mode.valid = false;
        return;
    }
    if (!pending.valid) return;
    if (pending.gen != generation) {
        pending.valid = false;
        return;
    }
    ui_screen_t target = pending.screen;
    pending.valid = false;
    if (!nav_allowed() || target == current_screen) return;
    enter_screen(target);
}

void ui_router_process(void)
{
    process_cb(NULL);
}

void ui_router_boot_screen(ui_screen_t screen)
{
    if (!roots[0]) return;
    pending.valid = false;
    enter_screen(screen);
}

void ui_router_request_screen(ui_screen_t screen)
{
    if (!roots[0] || screen < 0 || screen >= UI_SCREEN_COUNT || !nav_allowed()
        || pending_mode.valid) return;
    pending.valid = true;
    pending.gen = generation;
    pending.screen = screen;
}

void ui_router_swipe(bool rightward)
{
    ui_screen_t target = current_screen;
    if (rightward) {
        /* Vuốt phải: CLOCK/WIFI/NAV/SETTINGS → MENU; MENU giữ. */
        if (current_screen != UI_SCREEN_MENU) target = UI_SCREEN_MENU;
    } else {
        /* Vuốt trái: mọi màn → NAVIGATION; NAV giữ. */
        if (current_screen != UI_SCREEN_NAV) target = UI_SCREEN_NAV;
    }
    ui_router_request_screen(target);
}

void ui_router_set_mode(ui_mode_t mode)
{
    /* Caller owns the LVGL context/lock. Defer tree changes until dispatch ends. */
    if (!roots[0]) return;
    pending_mode.gen = generation;
    pending_mode.mode = mode;
    pending_mode.valid = true;
}

void ui_router_return_from_calibration(void)
{
    if (current_mode != UI_MODE_CALIBRATION) return;
    ui_screen_t back = screen_before_calibration;
    ui_router_set_mode(UI_MODE_NORMAL);
    ui_router_request_screen(back);
}

void ui_router_register_page(ui_screen_t screen, const ui_page_t *page)
{
    pages[screen] = page;
}
