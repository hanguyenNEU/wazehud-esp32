#include "ui_menu.h"

#include "board.h"
#include "ui_router.h"
#include "esp_log.h"

LV_FONT_DECLARE(font_vn_20)
LV_FONT_DECLARE(font_vn_26)

#define MARGIN 12
#define GAP 8
#define COLS 3

typedef enum {
    ACTION_CLOCK = 0,
    ACTION_WIFI,
    ACTION_NAV,
    ACTION_SETTINGS,
    ACTION_TRIP,
} menu_action_t;

static void menu_debug(lv_event_t *event)
{
    ESP_LOGI("menu", "btn evt %d target=%p", (int)lv_event_get_code(event),
             (void *)lv_event_get_target(event));
}

static void menu_clicked(lv_event_t *event)
{
    ESP_LOGI("menu", "CLICKED!");
    menu_action_t action = (menu_action_t)(intptr_t)lv_event_get_user_data(event);
    switch (action) {
    case ACTION_CLOCK:    ui_router_request_screen(UI_SCREEN_CLOCK); break;
    case ACTION_WIFI:     ui_router_request_screen(UI_SCREEN_WIFI); break;
    case ACTION_NAV:      ui_router_request_screen(UI_SCREEN_NAV); break;
    case ACTION_SETTINGS: ui_router_request_screen(UI_SCREEN_SETTINGS); break;
    case ACTION_TRIP:     ui_router_request_screen(UI_SCREEN_TRIP); break;
    }
}

static lv_obj_t *menu_button(lv_obj_t *parent, const char *text, lv_color_t bg,
                             menu_action_t action)
{
    lv_obj_t *button = lv_obj_create(parent);
    lv_obj_set_style_bg_color(button, bg, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x475569), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_radius(button, 14, 0);
    lv_obj_set_style_pad_all(button, 0, 0);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(button, menu_clicked, LV_EVENT_CLICKED, (void *)(intptr_t)action);
    lv_obj_add_event_cb(button, menu_debug, LV_EVENT_ALL, NULL);

    lv_obj_t *caption = lv_label_create(button);
    lv_obj_set_style_text_font(caption, &font_vn_20, 0);
    lv_obj_set_style_text_color(caption, lv_color_white(), 0);
    lv_label_set_text(caption, text);
    lv_obj_add_flag(caption, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(caption);
    return button;
}

void ui_menu_page_enter(lv_obj_t *parent)
{
    /* Lưới 3×2 (5 mục): 320×240 → ô 93×70, vẫn trên ngưỡng chạm 40px. */
    int w = DISPLAY_W, h = DISPLAY_H;
    int margin = MARGIN, gap = GAP;
    if (w < 200) { margin = 6; gap = 6; }
    int title_h = h >= 200 ? 56 : 40;
    int avail_w = w - 2 * margin;
    int cell_w = (avail_w - (COLS - 1) * gap) / COLS;
    int cell_h = (h - title_h - margin - gap - 24) / 2;
    if (cell_w < 40) cell_w = 40;
    if (cell_h < 40) cell_h = 40;

    lv_obj_t *title = lv_label_create(parent);
    lv_obj_set_style_text_font(title, &font_vn_26, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Menu");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    static const struct {
        const char *text;
        lv_color_t color;
        menu_action_t action;
    } items[] = {
        {"Đồng hồ", LV_COLOR_MAKE(0x31, 0x57, 0xC8), ACTION_CLOCK},
        {"Dẫn đường", LV_COLOR_MAKE(0xE8, 0x5D, 0x1E), ACTION_NAV},
        {"Chuyến đi", LV_COLOR_MAKE(0x0E, 0x7C, 0x86), ACTION_TRIP},
        {"Wi-Fi", LV_COLOR_MAKE(0x19, 0x87, 0x54), ACTION_WIFI},
        {"Cài đặt", LV_COLOR_MAKE(0x6F, 0x42, 0xC1), ACTION_SETTINGS},
    };

    for (int i = 0; i < (int)(sizeof(items) / sizeof(items[0])); ++i) {
        int col = i % COLS;
        int row = i / COLS;
        lv_obj_t *button = menu_button(parent, items[i].text, items[i].color,
                                       items[i].action);
        lv_obj_set_size(button, cell_w, cell_h);
        lv_obj_t *caption = lv_obj_get_child(button, 0);
        lv_obj_set_width(caption, cell_w - 12);
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(button, margin + col * (cell_w + gap),
                       title_h + row * (cell_h + gap));
    }

    lv_obj_t *hint = lv_label_create(parent);
    lv_obj_set_style_text_font(hint, &font_vn_20, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x8E8E93), 0);
    lv_label_set_text(hint, "Vuốt trái để dẫn đường");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
}

void ui_menu_page_exit(void)
{
    /* Toàn bộ cây nằm trong page_root — router tự lv_obj_clean() sau khi exit,
       nên không cần giữ con trỏ. Chỉ phải dọn state ngoài widget tree. */
}
