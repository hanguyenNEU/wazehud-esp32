#include "ui_trip.h"

#include "board.h"
#include "ui_router.h"

#include <stdio.h>

LV_FONT_DECLARE(font_vn_16)
LV_FONT_DECLARE(font_vn_26)
LV_FONT_DECLARE(font_dist_30)

#define HEADER_H 44
#define SPANS 4

static trip_stats_t model;
static bool have_model;

/* Widgets sống: [0] hôm nay, [1] hôm qua, [2] tháng này, [3] tháng trước. */
static lv_obj_t *caption[SPANS], *km_label[SPANS], *detail_label[SPANS];

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    return obj;
}

/* "638,7 km" — font số khoảng cách chỉ có 0-9 , k m và dấu cách. */
static void fmt_km(char *out, size_t cap, float km)
{
    if (km < 0) { out[0] = '\0'; return; }
    int tenths = (int)(km * 10.0f + 0.5f);
    if (tenths >= 10000) snprintf(out, cap, "%d km", tenths / 10); /* ≥1000 km: bỏ lẻ */
    else snprintf(out, cap, "%d,%d km", tenths / 10, tenths % 10);
}

/* "1h05p · 42 km/h" — thẻ chỉ rộng ~130 px nên viết gọn. */
static void fmt_detail(char *out, size_t cap, const trip_span_t *s)
{
    if (s->km < 0) { snprintf(out, cap, "Không có dữ liệu"); return; }
    if (s->mins <= 0) { snprintf(out, cap, "Chưa chạy"); return; }
    float avg = s->km / (s->mins / 60.0f);
    snprintf(out, cap, "%dh%02dp · %d km/h", s->mins / 60, s->mins % 60, (int)(avg + 0.5f));
}

static void render(void)
{
    if (!km_label[0]) return;
    const trip_span_t *spans[SPANS] = {&model.today, &model.yesterday, &model.this_month,
                                       &model.prev_month};
    char buf[48];

    if (have_model && model.month) {
        lv_label_set_text_fmt(caption[2], "Tháng %d", model.month);
        lv_label_set_text_fmt(caption[3], "Tháng %d", model.last_month);
    }
    for (int i = 0; i < SPANS; ++i) {
        if (!have_model) {
            lv_label_set_text(km_label[i], "");
            lv_label_set_text(detail_label[i], i == 0 ? "Chờ dữ liệu từ Waze" : "");
            continue;
        }
        fmt_km(buf, sizeof(buf), spans[i]->km);
        lv_label_set_text(km_label[i], buf);
        fmt_detail(buf, sizeof(buf), spans[i]);
        lv_label_set_text(detail_label[i], buf);
    }
}

void ui_trip_set(const trip_stats_t *stats)
{
    model = *stats;
    have_model = true;
    render();
}

static void back_cb(lv_event_t *event)
{
    (void)event;
    ui_router_request_screen(UI_SCREEN_MENU);
}

static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h, uint32_t accent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x161A22), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj, lv_color_hex(accent), 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_radius(obj, 10, 0);
    lv_obj_set_style_pad_all(obj, 8, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static void fill(lv_obj_t *box, int i, const char *title, uint32_t accent)
{
    caption[i] = label(box, &font_vn_16, lv_color_hex(accent));
    lv_label_set_text(caption[i], title);
    lv_obj_align(caption[i], LV_ALIGN_TOP_LEFT, 0, 0);

    km_label[i] = label(box, &font_dist_30, lv_color_white());
    lv_obj_align(km_label[i], LV_ALIGN_LEFT_MID, 0, 2);

    detail_label[i] = label(box, &font_vn_16, lv_color_hex(0x9CA3AF));
    lv_label_set_long_mode(detail_label[i], LV_LABEL_LONG_DOT);
    /* Cố định 1 dòng: LONG_DOT chỉ cắt "…" khi chiều cao không tự giãn. */
    lv_obj_set_size(detail_label[i], LV_PCT(100), lv_font_get_line_height(&font_vn_16));
    lv_obj_align(detail_label[i], LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

void ui_trip_page_enter(lv_obj_t *parent)
{
    int w = DISPLAY_W, h = DISPLAY_H;

    lv_obj_t *back = lv_obj_create(parent);
    lv_obj_set_size(back, 64, 36);
    lv_obj_set_pos(back, 8, 4);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x202631), 0);
    lv_obj_set_style_bg_color(back, lv_color_hex(0x475569), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(back, 0, 0);
    lv_obj_set_style_radius(back, 10, 0);
    lv_obj_remove_flag(back, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_caption = label(back, &font_vn_16, lv_color_white());
    lv_label_set_text(back_caption, "Menu");
    lv_obj_add_flag(back_caption, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(back_caption);

    lv_obj_t *title = label(parent, &font_vn_26, lv_color_white());
    lv_label_set_text(title, "Chuyến đi");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    /* Lưới 2×2: hàng trên hôm nay / hôm qua, hàng dưới hai tháng. */
    static const struct { const char *title; uint32_t accent; } cells[SPANS] = {
        {"Hôm nay", 0x5EA9FF},
        {"Hôm qua", 0x8E8E93},
        {"Tháng này", 0x32D74B},
        {"Tháng trước", 0xF5C451},
    };
    int margin = 8, gap = 8;
    int top = HEADER_H + 4;
    int cell_w = (w - 2 * margin - gap) / 2;
    int cell_h = (h - top - margin - gap) / 2;
    for (int i = 0; i < SPANS; ++i) {
        int x = margin + (i % 2) * (cell_w + gap);
        int y = top + (i / 2) * (cell_h + gap);
        lv_obj_t *box = card(parent, x, y, cell_w, cell_h, cells[i].accent);
        fill(box, i, cells[i].title, cells[i].accent);
    }

    render();
}

void ui_trip_page_exit(void)
{
    for (int i = 0; i < SPANS; ++i) caption[i] = km_label[i] = detail_label[i] = NULL;
}
