#include "ui_clock.h"

#include "anim_bg.h"
#include "board.h"
#include "lunar_calendar.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_circle_62)
LV_FONT_DECLARE(font_vn_16)

extern const anim_bg_t bg_forest_city;

static const char *const WEEKDAYS[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};

/* Giờ và phút là 2 label riêng canh hai bên dấu ":" vẽ bằng 2 chấm — font số
   không có glyph ':' (fallback sang font chữ thì ra dấu chấm nhỏ xíu). */
static lv_obj_t *hour_label, *minute_label, *colon_dots[2];
static lv_obj_t *date_label;
static lv_obj_t *page_parent, *hint_pill, *hint_label;
static lv_timer_t *hint_timer;
static const anim_bg_t *background; /* NULL = mặc định */

static const anim_bg_t *current_background(void)
{
    return background ? background : &bg_forest_city;
}

void ui_clock_set_background(const anim_bg_t *bg)
{
    background = bg;
    if (!page_parent) return;
    const anim_bg_t *b = current_background();
    if (b->w == DISPLAY_W && b->h == DISPLAY_H) anim_bg_start(page_parent, b);
    else anim_bg_stop();
}

static void hint_hide_cb(lv_timer_t *timer)
{
    (void)timer;
    hint_timer = NULL;
    if (hint_pill) lv_obj_add_flag(hint_pill, LV_OBJ_FLAG_HIDDEN);
}

void ui_clock_show_hint(const char *text)
{
    if (!hint_pill) return;
    lv_label_set_text(hint_label, text);
    lv_obj_remove_flag(hint_pill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(hint_pill);
    if (hint_timer) lv_timer_reset(hint_timer);
    else {
        hint_timer = lv_timer_create(hint_hide_cb, 2200, NULL);
        lv_timer_set_repeat_count(hint_timer, 1);
    }
}

/* Cache lịch âm theo ngày VN — chỉ tính lại khi ngày đổi (§5). */
static int cached_vn_y = -1, cached_vn_m = -1, cached_vn_d = -1;
static lunar_date_t cached_lunar;
static bool cached_lunar_valid;
static int last_second = -1;

static void update_lunar(int y, int m, int d, int64_t unix_s)
{
    int vh, vm, vs, vw;
    lunar_civil_from_unix(unix_s, 420, &y, &m, &d, &vh, &vm, &vs, &vw);

    if (y == cached_vn_y && m == cached_vn_m && d == cached_vn_d) return;
    cached_vn_y = y;
    cached_vn_m = m;
    cached_vn_d = d;
    cached_lunar_valid = lunar_from_unix(unix_s, &cached_lunar);
}

void ui_clock_set_time(int64_t unix_seconds, int tz_minutes, bool synced)
{
    if (!hour_label) return;

    if (!synced) {
        /* Cold boot / mất đồng bộ: placeholder, không lấy build time hay NVS
           cũ làm giờ thật. */
        lv_label_set_text(hour_label, "--");
        lv_label_set_text(minute_label, "--");
        lv_label_set_text(date_label, "Chưa đồng bộ giờ");
        cached_vn_y = -1;
        last_second = -1;
        return;
    }

    int y, m, d, h, mi, s, wday;
    lunar_civil_from_unix(unix_seconds, tz_minutes, &y, &m, &d, &h, &mi, &s, &wday);
    if (s == last_second) return; /* tick 250ms — chỉ vẽ lại khi sang giây mới */
    last_second = s;

    char buf[64];
    snprintf(buf, sizeof(buf), "%02d", h);
    lv_label_set_text(hour_label, buf);
    snprintf(buf, sizeof(buf), "%02d", mi);
    lv_label_set_text(minute_label, buf);

    /* Dấu ":" nhấp nháy theo giây thay cho số giây. */
    lv_opa_t opa = (s & 1) ? LV_OPA_40 : LV_OPA_COVER;
    for (int i = 0; i < 2; ++i) lv_obj_set_style_bg_opa(colon_dots[i], opa, 0);

    /* Dương lịch + âm lịch chung một dòng; lunar chỉ tính lại khi ngày VN đổi. */
    update_lunar(y, m, d, unix_seconds);
    int n = snprintf(buf, sizeof(buf), "%s - %02d/%02d/%d", WEEKDAYS[wday], d, m, y);
    if (cached_lunar_valid && n > 0 && (size_t)n < sizeof(buf))
        snprintf(buf + n, sizeof(buf) - n, " · ÂL %02d/%02d%s%s",
                 cached_lunar.day, cached_lunar.month,
                 cached_lunar.leap_month ? "n" : "", tz_minutes == 420 ? "" : " (VN)");
    lv_label_set_text(date_label, buf);
}

/* Nền tối mờ bo tròn sau chữ để đọc được trên ảnh pixel-art nhiều chi tiết. */
static lv_obj_t *pill(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x05070D), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_50, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 16, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    return obj;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    return obj;
}

void ui_clock_page_enter(lv_obj_t *parent)
{
    int w = DISPLAY_W, h = DISPLAY_H;
    bool big = h >= 240;

    /* Ảnh nền (mặc định: rừng + thành phố đêm động; hoặc ảnh tải lên từ
       app — vuốt dọc để đổi) nằm dưới cùng; chữ vẽ đè lên. */
    page_parent = parent;
    ui_clock_set_background(background);

    /* --- Khối giờ nhỏ + ngày dương/âm ở đáy, chừa trọn màn cho ảnh nền --- */
    const lv_font_t *digits = &font_circle_62;
    const lv_font_t *date_font = &font_vn_16;
    int digit_h = lv_font_get_line_height(digits);
    int top = big ? 6 : 4;
    int date_y = top + digit_h + 2;
    int box_w = big ? 204 : w - 24;
    int box_h = date_y + lv_font_get_line_height(date_font) + (big ? 8 : 6);
    lv_obj_t *box = pill(parent);
    lv_obj_set_size(box, box_w, box_h);
    lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, big ? -8 : -4);

    int gap = big ? 12 : 9; /* nửa khoảng trống quanh dấu ":" */
    hour_label = label(box, digits, lv_color_white());
    lv_obj_set_width(hour_label, box_w / 2 - gap);
    lv_obj_set_style_text_align(hour_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(hour_label, "--");
    lv_obj_set_pos(hour_label, 0, top);

    minute_label = label(box, digits, lv_color_white());
    lv_obj_set_width(minute_label, box_w / 2 - gap);
    lv_obj_set_style_text_align(minute_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(minute_label, "--");
    lv_obj_set_pos(minute_label, box_w / 2 + gap, top);

    int dot = big ? 6 : 5;
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *d = lv_obj_create(box);
        lv_obj_set_size(d, dot, dot);
        lv_obj_set_style_radius(d, 2, 0);
        lv_obj_set_style_border_width(d, 0, 0);
        lv_obj_set_style_bg_color(d, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
        lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(d, box_w / 2 - dot / 2, top + digit_h * (i ? 64 : 30) / 100 - dot / 2);
        colon_dots[i] = d;
    }

    date_label = label(box, date_font, lv_color_hex(0xFCD34D));
    lv_obj_set_width(date_label, box_w);
    lv_obj_set_style_text_align(date_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(date_label, "Chưa đồng bộ giờ");
    lv_obj_set_pos(date_label, 0, date_y);

    /* Dòng nhắc giữa màn (vd "Chưa có ảnh nền…"), ẩn sẵn. */
    hint_pill = pill(parent);
    lv_obj_set_size(hint_pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hint_pill, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(hint_pill, 12, 0);
    lv_obj_set_style_pad_ver(hint_pill, 6, 0);
    lv_obj_align(hint_pill, LV_ALIGN_CENTER, 0, big ? -20 : -15);
    hint_label = label(hint_pill, &font_vn_16, lv_color_white());
    lv_obj_add_flag(hint_pill, LV_OBJ_FLAG_HIDDEN);

    last_second = -1;
}

void ui_clock_page_exit(void)
{
    anim_bg_stop();
    if (hint_timer) lv_timer_delete(hint_timer);
    hint_timer = NULL;
    page_parent = NULL;
    hint_pill = NULL;
    hint_label = NULL;
    hour_label = NULL;
    minute_label = NULL;
    colon_dots[0] = colon_dots[1] = NULL;
    date_label = NULL;
    cached_vn_y = -1;
    last_second = -1;
}
