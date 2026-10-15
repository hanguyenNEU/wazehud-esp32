#include "ui_wifi.h"

#include "board.h"
#include "ui_router.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_vn_16)
LV_FONT_DECLARE(font_vn_20)
LV_FONT_DECLARE(font_vn_26)

/* Luồng: vào trang → tự quét → danh sách mạng → chạm 1 mạng →
   (mạng mở: lưu luôn) màn mật khẩu với ô nhập + bàn phím → Lưu.
   Hai view (LIST / PASSWORD) dựng sẵn trong page, chỉ ẩn/hiện — không xoá
   cây giữa lúc LVGL đang dispatch sự kiện chạm. */

#define HEADER_H 44
#define ROW_H 40
#define PASSWORD_MAX 64

/* ---- Mô hình (ngoài widget tree) ---- */
static ui_wifi_save_cb_t save_action;
static ui_wifi_scan_cb_t scan_action;
static ui_wifi_phone_cb_t phone_action;
static void *action_user;

static char status_text[64] = "Chưa kết nối";
static char scan_status_text[32] = "";
static ui_wifi_scan_entry_t scan_entries[UI_WIFI_MAX_SCAN];
static size_t scan_count;
static bool scanned_once;
static char selected_ssid[33];

/* Scan list mới tới trong lúc press/scroll: giữ pending, áp sau release. */
static bool list_has_pending;
static ui_wifi_scan_entry_t pending_entries[UI_WIFI_MAX_SCAN];
static size_t pending_count;

/* ---- Widgets sống (chỉ hợp lệ khi page đang hiển thị) ---- */
static lv_obj_t *view_list;
static lv_obj_t *view_pwd;
static lv_obj_t *scan_status_label;
static lv_obj_t *status_label;
static lv_obj_t *list_obj;
static lv_obj_t *pwd_ssid_label;
static lv_obj_t *pwd_area;
static lv_obj_t *pwd_show_caption;
static lv_obj_t *keyboard;
static lv_timer_t *scan_timer;

static void select_cb(lv_event_t *event);
static void free_ssid_cb(lv_event_t *event);

void ui_wifi_set_callbacks(ui_wifi_save_cb_t save_cb, ui_wifi_scan_cb_t scan_cb, void *user)
{
    save_action = save_cb;
    scan_action = scan_cb;
    action_user = user;
}

void ui_wifi_set_phone_callback(ui_wifi_phone_cb_t phone_cb, void *user)
{
    phone_action = phone_cb;
    action_user = user;
}

void ui_wifi_set_status(const char *status)
{
    if (!status) status = "";
    snprintf(status_text, sizeof(status_text), "%s", status);
    if (status_label) lv_label_set_text(status_label, status_text);
}

static void refresh_empty_hint(void);

void ui_wifi_set_scan_status(const char *status)
{
    if (!status) status = "";
    if (!strcmp(scan_status_text, status)) return; /* gọi mỗi tick — khỏi vẽ lại */
    snprintf(scan_status_text, sizeof(scan_status_text), "%s", status);
    if (scan_status_label) lv_label_set_text(scan_status_label, scan_status_text);
    refresh_empty_hint();
}

/* ---- Helpers ---- */

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    return obj;
}

static lv_obj_t *button(lv_obj_t *parent, const char *text, int x, int y, int w,
                        int h, lv_color_t bg, lv_event_cb_t cb)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_bg_color(obj, bg, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x475569), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *caption = label(obj, &font_vn_16, lv_color_white());
    lv_label_set_text(caption, text);
    lv_obj_add_flag(caption, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(caption);
    return obj;
}

static lv_obj_t *view(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, LV_PCT(100), LV_PCT(100));
    lv_obj_set_pos(obj, 0, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 0, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static const char *signal_text(int rssi)
{
    if (rssi >= -60) return "Mạnh";
    if (rssi >= -72) return "Khá";
    return "Yếu";
}

/* ---- Chuyển view ---- */

static void show_list(void)
{
    if (!view_list) return;
    if (keyboard) lv_keyboard_set_textarea(keyboard, NULL);
    lv_obj_add_flag(view_pwd, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view_list, LV_OBJ_FLAG_HIDDEN);
}

static void show_password(const char *ssid)
{
    if (!view_pwd) return;
    snprintf(selected_ssid, sizeof(selected_ssid), "%s", ssid);
    lv_label_set_text(pwd_ssid_label, selected_ssid);
    lv_textarea_set_text(pwd_area, "");
    lv_textarea_set_password_mode(pwd_area, true);
    lv_label_set_text(pwd_show_caption, "Hiện");
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(keyboard, pwd_area);
    lv_obj_add_state(pwd_area, LV_STATE_FOCUSED);
    lv_obj_add_flag(view_list, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(view_pwd, LV_OBJ_FLAG_HIDDEN);
}

static void save(const char *ssid, const char *password)
{
    if (!ssid[0] || !save_action) return;
    save_action(ssid, password, action_user);
    ui_wifi_set_status("Đang kết nối...");
    show_list();
}

/* ---- Danh sách ---- */

static void refresh_empty_hint(void)
{
    if (!list_obj || scan_count) return;
    lv_obj_t *hint = lv_obj_get_child(list_obj, 0);
    if (!hint) return;
    lv_label_set_text(hint, scan_status_text[0] ? "Đang quét mạng..."
                            : scanned_once  ? "Không tìm thấy mạng nào"
                                            : "Chạm \"Quét lại\" để tìm mạng");
}

static void apply_entries(const ui_wifi_scan_entry_t *entries, size_t count)
{
    scan_count = count > UI_WIFI_MAX_SCAN ? UI_WIFI_MAX_SCAN : count;
    if (scan_count && entries != scan_entries)
        memcpy(scan_entries, entries, scan_count * sizeof(*entries));
    list_has_pending = false;
    if (!list_obj) return;

    lv_obj_clean(list_obj);
    if (!scan_count) {
        lv_obj_t *hint = label(list_obj, &font_vn_16, lv_color_hex(0x8E8E93));
        lv_obj_set_width(hint, LV_PCT(100));
        lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_style_pad_top(hint, 24, 0);
        refresh_empty_hint();
        return;
    }
    for (size_t i = 0; i < scan_count; ++i) {
        lv_obj_t *row = lv_obj_create(list_obj);
        lv_obj_set_size(row, LV_PCT(100), ROW_H);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x202631), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0x475569), LV_STATE_PRESSED);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_radius(row, 6, 0);
        lv_obj_set_style_pad_hor(row, 10, 0);
        lv_obj_set_style_pad_ver(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        /* Giữ SSID theo STRING (copy), không theo index vào mảng có thể bị
           ghi đè khi người dùng còn nhấn (§3.1). Byte đầu = cờ mạng mở. */
        size_t len = strlen(scan_entries[i].ssid);
        char *data = lv_malloc(len + 2);
        if (!data) { lv_obj_delete(row); break; }
        data[0] = scan_entries[i].open ? 1 : 0;
        memcpy(data + 1, scan_entries[i].ssid, len + 1);
        lv_obj_add_event_cb(row, select_cb, LV_EVENT_CLICKED, data);
        lv_obj_add_event_cb(row, free_ssid_cb, LV_EVENT_DELETE, data);

        lv_obj_t *name = label(row, &font_vn_16, lv_color_white());
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, LV_PCT(70));
        lv_label_set_text(name, scan_entries[i].ssid);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, 0);
        lv_obj_add_flag(name, LV_OBJ_FLAG_EVENT_BUBBLE);

        lv_obj_t *info = label(row, &font_vn_16, lv_color_hex(0x8E8E93));
        char text[24];
        snprintf(text, sizeof(text), "%s%s", scan_entries[i].open ? "Mở · " : "",
                 signal_text(scan_entries[i].rssi));
        lv_label_set_text(info, text);
        lv_obj_align(info, LV_ALIGN_RIGHT_MID, 0, 0);
        lv_obj_add_flag(info, LV_OBJ_FLAG_EVENT_BUBBLE);
    }
}

void ui_wifi_set_scan_list(const ui_wifi_scan_entry_t *entries, size_t count)
{
    size_t n = count > UI_WIFI_MAX_SCAN ? UI_WIFI_MAX_SCAN : count;
    scanned_once = true;
    if (list_obj) {
        /* Đang chạm/cuộn danh sách: áp sau release để không phá cây giữa
           lúc LVGL đang xử lý input trên đó. */
        list_has_pending = true;
        pending_count = n;
        if (n) memcpy(pending_entries, entries, n * sizeof(*entries));
        return;
    }
    apply_entries(entries, n);
}

static void free_ssid_cb(lv_event_t *event)
{
    lv_free(lv_event_get_user_data(event));
}

static void select_cb(lv_event_t *event)
{
    const char *data = lv_event_get_user_data(event);
    if (!data) return;
    bool open = data[0] == 1;
    const char *ssid = data + 1;
    if (open) {
        snprintf(selected_ssid, sizeof(selected_ssid), "%s", ssid);
        save(selected_ssid, "");
        return;
    }
    show_password(ssid);
}

static void apply_pending_scan(lv_timer_t *timer)
{
    (void)timer;
    if (!list_obj || !list_has_pending || lv_obj_is_scrolling(list_obj)) return;
    for (lv_indev_t *input = lv_indev_get_next(NULL); input; input = lv_indev_get_next(input))
        if (lv_indev_get_state(input) == LV_INDEV_STATE_PRESSED) return;
    apply_entries(pending_entries, pending_count);
}

/* ---- Callbacks ---- */

static void back_cb(lv_event_t *event)
{
    (void)event;
    ui_router_request_screen(UI_SCREEN_MENU);
}

static void scan_cb(lv_event_t *event)
{
    (void)event;
    if (scan_action) scan_action(action_user);
}

static void pwd_back_cb(lv_event_t *event)
{
    (void)event;
    show_list();
}

static void pwd_save_cb(lv_event_t *event)
{
    (void)event;
    save(selected_ssid, lv_textarea_get_text(pwd_area));
}

static void pwd_show_cb(lv_event_t *event)
{
    (void)event;
    bool hidden = lv_textarea_get_password_mode(pwd_area);
    lv_textarea_set_password_mode(pwd_area, !hidden);
    lv_label_set_text(pwd_show_caption, hidden ? "Ẩn" : "Hiện");
}

static void keyboard_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY) save(selected_ssid, lv_textarea_get_text(pwd_area));
    else if (code == LV_EVENT_CANCEL) show_list();
}

/* ---- Dựng page ---- */

static void build_list_view(lv_obj_t *parent, int w, int h)
{
    view_list = view(parent);

    button(view_list, "Menu", 8, 4, 64, 36, lv_color_hex(0x202631), back_cb);
    lv_obj_t *title = label(view_list, &font_vn_26, lv_color_white());
    lv_label_set_text(title, "WiFi");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);
    button(view_list, "Quét lại", w - 88, 4, 80, 36, lv_color_hex(0x3157C8), scan_cb);

    status_label = label(view_list, &font_vn_16, lv_color_hex(0xA9B4C2));
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(status_label, w - 130);
    lv_label_set_text(status_label, status_text);
    lv_obj_set_pos(status_label, 12, HEADER_H + 2);

    /* Dòng QUÉT độc lập trạng thái liên kết (§7). */
    scan_status_label = label(view_list, &font_vn_16, lv_color_hex(0xF1C40F));
    lv_label_set_text(scan_status_label, scan_status_text);
    lv_obj_align(scan_status_label, LV_ALIGN_TOP_RIGHT, -12, HEADER_H + 2);

    int list_y = HEADER_H + 26;
    list_obj = lv_obj_create(view_list);
    lv_obj_set_size(list_obj, w - 16, h - list_y - 4);
    lv_obj_set_pos(list_obj, 8, list_y);
    lv_obj_set_style_bg_opa(list_obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list_obj, 0, 0);
    lv_obj_set_style_pad_all(list_obj, 0, 0);
    lv_obj_set_style_pad_row(list_obj, 6, 0);
    lv_obj_set_flex_flow(list_obj, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(list_obj, LV_DIR_VER);
    lv_obj_remove_flag(list_obj, LV_OBJ_FLAG_CLICKABLE);
}

static void build_password_view(lv_obj_t *parent, int w, int h)
{
    view_pwd = view(parent);

    button(view_pwd, "Quay lại", 6, 4, 84, 32, lv_color_hex(0x202631), pwd_back_cb);
    pwd_ssid_label = label(view_pwd, &font_vn_16, lv_color_white());
    lv_label_set_long_mode(pwd_ssid_label, LV_LABEL_LONG_DOT);
    lv_obj_set_width(pwd_ssid_label, w - 96 - 76);
    lv_obj_set_style_text_align(pwd_ssid_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(pwd_ssid_label, 96, 12);
    button(view_pwd, "Lưu", w - 70, 4, 64, 32, lv_color_hex(0x198754), pwd_save_cb);

    pwd_area = lv_textarea_create(view_pwd);
    lv_textarea_set_one_line(pwd_area, true);
    lv_textarea_set_max_length(pwd_area, PASSWORD_MAX);
    lv_textarea_set_password_bullet(pwd_area, "*"); /* font_vn không có ký tự • */
    lv_textarea_set_placeholder_text(pwd_area, "Mật khẩu");
    lv_obj_set_style_text_font(pwd_area, &font_vn_16, 0);
    lv_obj_set_size(pwd_area, w - 12 - 6 - 60, 36);
    lv_obj_set_pos(pwd_area, 6, 40);

    lv_obj_t *show = button(view_pwd, "Hiện", w - 66, 40, 60, 36,
                            lv_color_hex(0x2E3542), pwd_show_cb);
    pwd_show_caption = lv_obj_get_child(show, 0);

    /* Bàn phím chiếm phần dưới; phím OK = Lưu, phím đóng = Quay lại. */
    int kb_y = 80;
    keyboard = lv_keyboard_create(view_pwd);
    lv_obj_set_size(keyboard, w, h - kb_y);
    /* lv_keyboard_create tự align BOTTOM_MID — set_pos sẽ thành offset từ
       đáy và đẩy nửa bàn phím ra ngoài màn. Align lại tường minh. */
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_pad_all(keyboard, 2, 0);
    lv_obj_set_style_pad_gap(keyboard, 3, 0);
    lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_CANCEL, NULL);

    lv_obj_add_flag(view_pwd, LV_OBJ_FLAG_HIDDEN);
}

void ui_wifi_page_enter(lv_obj_t *parent)
{
    int w = DISPLAY_W, h = DISPLAY_H;

    build_list_view(parent, w, h);
    build_password_view(parent, w, h);

    scan_timer = lv_timer_create(apply_pending_scan, 50, NULL);
    list_has_pending = false;

    /* Dựng lại danh sách từ mô hình đã có, rồi tự quét mới. */
    apply_entries(scan_entries, scan_count);
    if (scan_action) scan_action(action_user);
}

void ui_wifi_page_exit(void)
{
    if (scan_timer) lv_timer_delete(scan_timer);
    scan_timer = NULL;
    if (list_has_pending) {
        scan_count = pending_count;
        memcpy(scan_entries, pending_entries, scan_count * sizeof(*scan_entries));
    }
    view_list = NULL;
    view_pwd = NULL;
    scan_status_label = NULL;
    status_label = NULL;
    list_obj = NULL;
    pwd_ssid_label = NULL;
    pwd_area = NULL;
    pwd_show_caption = NULL;
    keyboard = NULL;
    list_has_pending = false;
}
