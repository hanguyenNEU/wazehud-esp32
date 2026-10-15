/* Bố cục HUD cho màn 320×170.
 *
 * Toạ độ dưới đây tính từ kích thước glyph thật của font đã sinh, không phải
 * ước lượng: chữ số tốc độ là 43×81 px, icon rẽ 40 px, dòng tên đường 29 px.
 * Vùng hiển thị vật lý chỉ 42,6 × 22,6 mm nên gần như không có chỗ thừa —
 * mỗi phần tử thêm vào đều phải giành chỗ của một phần tử khác.
 *
 * Thứ tự ưu tiên khi tranh chỗ, theo mức độ khẩn cấp khi lái:
 *   tốc độ > ngã rẽ > cảnh báo > chỉ dẫn làn > tên đường > ETA
 */

#include "board.h"   /* PHẢI đứng trước mọi #if dùng DISPLAY_H */
#include "ui_navigator.h"
#include "ui_icons.h"
#include "ui_maneuver.h"
#include "hud_alerts.h"
#include "hud_alert_bitmaps.h"
#include "ui_router.h"
#include "ui_clock.h"
#include "ui_menu.h"
#include "ui_settings.h"
#include "ui_trip.h"
#include "ui_wifi.h"

#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ui";

LV_FONT_DECLARE(font_speed_112)
LV_FONT_DECLARE(font_limit_40)
LV_FONT_DECLARE(font_dist_30)
LV_FONT_DECLARE(font_vn_22)
LV_FONT_DECLARE(font_vn_16)
LV_FONT_DECLARE(font_icon_44)
LV_FONT_DECLARE(font_icon_26)
LV_FONT_DECLARE(font_circle_70)
LV_FONT_DECLARE(font_circle_54)
LV_FONT_DECLARE(font_circle_100)
LV_FONT_DECLARE(font_circle_82)
LV_FONT_DECLARE(font_dist_40)
LV_FONT_DECLARE(font_vn_26)
LV_FONT_DECLARE(font_vn_20)
LV_FONT_DECLARE(font_icon_64)
LV_FONT_DECLARE(font_circle_78)
LV_FONT_DECLARE(font_circle_62)
LV_FONT_DECLARE(font_dist_34)

/* Cỡ chữ và hình học của bố cục Waze đổi theo bo mạch. Màn 2.8" rộng gấp 2,6
 * lần nên phóng to hết cỡ: chữ số tốc độ đạt 1,03 độ góc nhìn ở 60 cm, thay vì
 * 0,48 độ trên màn 1.9". Đó là khác biệt giữa "liếc là đọc được" và "phải nhìn".
 */
  /* ES3C28P 320x240, bố cục Waze. Hai tầng, ngăn bởi vạch ngang y=140:
   *
   *   tầng trên  | rẽ + khoảng cách | tốc độ | biển | cảnh báo (1 lớn + 2 nhỏ) |
   *              | dãy làn (x0–236)                  |                        |
   *   tầng dưới  | ETA / phút / km  ║ đường sắp vào · "Rồi ↰" · đường đang đi  |
   *
   * Mỗi dữ liệu có chỗ riêng, không phần tử nào phải nhường chỗ cho phần tử
   * khác trong trường hợp thường gặp. Ngoại lệ duy nhất: >9 làn thì dãy làn
   * chiếm cả bề ngang và hai cảnh báo phụ tạm ẩn (xem render_lanes). */
  #define F_LIMIT   font_limit_40
  #define F_SPEED   font_circle_70
  #define F_TURN    font_icon_64
  #define F_DIST    font_dist_34      /* chỉ có số, ",", "k", "m" — đủ cho khoảng cách */
  #define F_STREET  font_vn_22
  #define F_INFO    font_vn_20
  #define F_LANES   font_icon_26      /* 12 làn x 26 = 312 px, vừa khít 320 */
  #define F_SUB     font_vn_20
  #define F_SPEED_BADGE font_limit_40
  /* Cột rẽ x0–100. */
  #define WZ_TURN_X 18
  #define WZ_TURN_Y 4
  #define WZ_DIST_X 0
  #define WZ_DIST_Y 72
  #define WZ_DIST_W 100
  /* Tốc độ x100–182, biển x182–236. */
  #define WZ_SPEED_X 100
  #define WZ_SPEED_Y 10
  #define WZ_SPEED_W 82
  #define WZ_SPEED_H 58
  #define WZ_UNIT_Y 70
  #define WZ_LIMIT_D 54
  #define WZ_LIMIT_X 182
  #define WZ_LIMIT_Y 14
  #define WZ_RING_BORDER 5
  /* Hình xe đã bỏ khỏi bố cục; macro còn giữ vì nó bật các widget dùng chung. */
  #define WZ_CAR_X 16
  #define WZ_CAR_Y 108
  /* Dãy làn ngay dưới khoảng cách/tốc độ, trên vạch chia. */
  #define WZ_LANES_X 0
  #define WZ_LANES_Y 106
  #define WZ_LANES_W 236
  #define WZ_LANES_MAX_NARROW 9  /* 236 / 26 */
  /* ≤5 làn nằm cạnh tên đường sắp tới (x100–236), nhường cột trái cho nó. */
  #define WZ_LANES_SIDE_X 100
  #define WZ_LANES_SIDE_W 136
  #define WZ_LANES_MAX_SIDE 5
  /* Tên đường sắp rẽ vào, ngay dưới mũi tên + khoảng cách — đọc liền một cụm
     "rẽ trái · 180 m · vào Nguyễn Trãi" như banner của Waze. */
  #define WZ_NEXT_STREET_X 0
  #define WZ_NEXT_STREET_Y 106
  #define WZ_NEXT_STREET_W 100
  /* Không dẫn đường: đồng hồ lớn + thứ/ngày ở cột trái tầng dưới. */
  #define WZ_BIG_CLOCK_X 6
  #define WZ_BIG_CLOCK_Y 150
  #define WZ_BIG_DATE_Y 200
  /* Cột cảnh báo x240–320: 1 icon lớn + 2 icon nhỏ, mỗi cái kèm khoảng cách. */
  #define WZ_ALERT_COL_X 238
  #define WZ_ALERT_COL_W 82
  #define WZ_ALERT_X 246  /* 246+44 = 290: chừa chữ trạng thái "W B" ở x≥292 */
  #define WZ_ALERT_Y 4
  #define WZ_ALERT_DIST_Y 50
  #define WZ_ALERT_NAME_Y 68
  #define WZ_ALERT2_X1 244
  #define WZ_ALERT2_X2 284
  #define WZ_ALERT2_Y 92
  #define WZ_ALERT2_DIST_Y 118
  #define WZ_DIVIDER_Y 140
  /* Tầng dưới: cột trái ETA, cột phải đường + "Rồi" + đồng hồ. */
  #define WZ_ETA_LABEL_X 8
  #define WZ_ETA_LABEL_Y 152
  #define WZ_ETA_X 46
  #define WZ_ETA_Y 146
  #define WZ_REMAIN_X 8
  #define WZ_REMAIN_MIN_Y 180
  #define WZ_REMAIN_DIST_Y 207
  #define WZ_REMAIN_W 108
  #define WZ_COL_DIVIDER_X 120
  #define WZ_STREET_X 128
  #define WZ_STREET_Y 148
  #define WZ_STREET_W 188
  #define WZ_NEXT_Y 180
  #define WZ_CUR_STREET_Y 212
  #define WZ_CUR_STREET_W 112
  #define WZ_SUB_Y 180
  #define WZ_INFO_Y 180
  #define WZ_CLOCK_TIME_W 70
  #define WZ_CLOCK_TIME_H 26
  #define WZ_CLOCK_TIME_X 246
  #define WZ_CLOCK_TIME_Y 207
  #define WZ_CLOCK_DATE_W 102
  #define WZ_CLOCK_DATE_H 23
  #define WZ_CLOCK_DATE_X 198
  #define WZ_CLOCK_DATE_Y 189


#define W DISPLAY_W
#define H DISPLAY_H

/* Cột trái: icon rẽ + khoảng cách. */
#define COL_L_X 0
  /* 86 px quá hẹp cho "200 m" ở font_dist_40 — chuỗi bị ngắt thành hai dòng. */
  #define COL_L_W 112
  #define BAND_LANES_Y 96
  /* Icon rẽ cao 64 px từ y=4, nên khoảng cách phải xuống dưới 68. */
  #define BAND_DIST_Y 74

/* Biển giới hạn tốc độ, hình tròn bên phải. */
  /* ES3C28P: cao 240 chứ không phải 170, nên các mốc của bố cục dải phải giãn
     ra. Giữ nguyên số cũ thì một phần ba dưới màn bỏ trống. */
  #define LIMIT_D 78
  #define BAND_Y   124
  #define STREET_Y 136
  #define INFO_Y   180

#define LIMIT_X (W - LIMIT_D - 4)
#define LIMIT_Y 12

/* Số tốc độ căn phải, mọc dần sang trái khi lên 3 chữ số. */
#define SPEED_R (LIMIT_X - 8)

typedef struct {
    lv_color_t bg, speed, speed_over, dim, muted, street, alert, avg, ring, ring_fill,
        ring_text;
} palette_t;

/* Ban đêm cố tình KHÔNG tô trắng đặc lòng biển giới hạn: một đĩa trắng 64 px
 * ngay tầm mắt lúc trời tối gây loá. Giữ vành đỏ để vẫn nhận ra là biển báo. */
static const palette_t THEME_NIGHT = {
    .bg = LV_COLOR_MAKE(0x00, 0x00, 0x00),
    .speed = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .speed_over = LV_COLOR_MAKE(0xFF, 0x45, 0x3A),
    .dim = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .muted = LV_COLOR_MAKE(0x6E, 0x6E, 0x73),
    .street = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .alert = LV_COLOR_MAKE(0xFF, 0xB3, 0x00),
    .avg = LV_COLOR_MAKE(0xFF, 0xB3, 0x00),
    .ring = LV_COLOR_MAKE(0xE5, 0x39, 0x35),
    .ring_fill = LV_COLOR_MAKE(0x10, 0x10, 0x12),
    .ring_text = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
};

static const palette_t THEME_DAY = {
    .bg = LV_COLOR_MAKE(0x00, 0x00, 0x00),
    .speed = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .speed_over = LV_COLOR_MAKE(0xFF, 0x3B, 0x30),
    .dim = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .muted = LV_COLOR_MAKE(0x8E, 0x8E, 0x93),
    .street = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .alert = LV_COLOR_MAKE(0xFF, 0xC1, 0x07),
    .avg = LV_COLOR_MAKE(0xFF, 0xC1, 0x07),
    .ring = LV_COLOR_MAKE(0xD3, 0x2F, 0x2F),
    .ring_fill = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF),
    .ring_text = LV_COLOR_MAKE(0x00, 0x00, 0x00),
};

static struct {
    lv_obj_t *screen;
    lv_obj_t *turn, *turn_canvas, *exit_badge, *dist;
    lv_obj_t *next_label, *next_icon, *street_cur, *street_next;
    lv_obj_t *big_clock, *big_hh, *big_colon, *big_mm, *big_date;
    lv_obj_t *remain_time, *remain_dist;
    lv_obj_t *speed, *speed_bubble;
    lv_obj_t *speed_unit;
    lv_obj_t *ring, *limit;
    lv_obj_t *clock_time_box, *clock_date_box, *clock_time, *clock_date;
    lv_obj_t *lanes;
    lv_obj_t *divider, *eta_divider, *street, *street_sub, *info, *info_icon;
    lv_obj_t *eta_label, *eta_value;
    lv_obj_t *alert_sign;
    lv_obj_t *alert_images[HUD_VISIBLE_ALERTS];
    lv_obj_t *alert_fallback[HUD_VISIBLE_ALERTS];
    lv_obj_t *alert_distances[HUD_VISIBLE_ALERTS];
    lv_obj_t *alert_details[HUD_VISIBLE_ALERTS];
    lv_obj_t *avg_bar;
    lv_obj_t *overlay, *overlay_icon, *overlay_text, *cached_badge;
    lv_obj_t *overlay_clock, *overlay_hh, *overlay_mm, *overlay_date;
    lv_obj_t *ble_dot, *wifi_dot;
} ui;

static ui_theme_t cfg_theme = UI_THEME_AUTO;
static ui_layout_t cfg_layout = UI_LAYOUT_WAZE;
static bool cfg_alerts = true;
static int cfg_alert_dist = 800;
static int local_hour = -1;
static bool clock_blocked_by_alert = false;
static char local_time_text[8];
static char local_date_text[24];
/* Ngày đầy đủ cho màn chờ: "Thứ Hai, 13/10/2026". */
static char local_date_long[40];
static ui_link_t link_state = UI_LINK_ADVERTISING;
/* Dãy làn đang chiếm cả bề ngang (bố cục Waze, >9 làn) ⇒ ẩn cảnh báo phụ. */
static bool lanes_wide = false;
/* Dãy làn đang chiếm cột trái (6+ làn) ⇒ tên đường sắp tới dưới mũi tên ẩn. */
static bool lanes_cover_left = false;
/* Bố cục Waze khi KHÔNG dẫn đường: đồng hồ lớn thay cột ETA. */
static bool idle_clock = false;
/* Dòng dưới cùng đổi chỗ theo bố cục, nên render_info không được
   hardcode toạ độ. */
static int info_x = 4, info_y = INFO_Y;

static const palette_t *palette(void)
{
    if (cfg_theme == UI_THEME_DAY) return &THEME_DAY;
    if (cfg_theme == UI_THEME_NIGHT) return &THEME_NIGHT;
    /* Chưa biết giờ thì chọn ban đêm: chói mắt nguy hiểm hơn là hơi tối. */
    if (local_hour < 0) return &THEME_NIGHT;
    return (local_hour >= 6 && local_hour < 18) ? &THEME_DAY : &THEME_NIGHT;
}

static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, color, 0);
    lv_label_set_text(obj, "");
    return obj;
}

static lv_obj_t *pill(lv_obj_t *parent, int w, int h)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

/* --- Hai bố cục, cùng một bộ widget -------------------------------------- */

#define LIMIT_D_WAZE 94
#define SPEED_D_WAZE 76

static void place_band(const palette_t *p)
{
    lv_obj_add_flag(ui.street_next, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.big_clock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.big_date, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.remain_time, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.remain_dist, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.street_cur, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.eta_divider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.eta_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.eta_value, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.speed_unit, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(ui.turn, COL_L_X + 22, 4);
    lv_obj_set_pos(ui.turn_canvas, COL_L_X + 22, 4);
    lv_obj_set_pos(ui.exit_badge, COL_L_X + 58, 6);
    lv_obj_set_width(ui.dist, COL_L_W);
    lv_obj_set_pos(ui.dist, COL_L_X, BAND_DIST_Y);

    lv_obj_set_style_bg_opa(ui.speed_bubble, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.speed_bubble, 0, 0);
    lv_obj_set_style_radius(ui.speed_bubble, 0, 0);
    lv_obj_set_size(ui.speed_bubble, SPEED_R, 92);
    lv_obj_set_pos(ui.speed_bubble, 0, 6);
    lv_obj_set_style_text_font(ui.speed, &font_speed_112, 0);
    lv_obj_align(ui.speed, LV_ALIGN_TOP_RIGHT, 0, 0);

    lv_obj_set_size(ui.ring, LIMIT_D, LIMIT_D);
    lv_obj_set_pos(ui.ring, LIMIT_X, LIMIT_Y);
    lv_obj_set_style_border_width(ui.ring, 5, 0);
    lv_obj_set_style_text_font(ui.limit, &font_limit_40, 0);
    lv_obj_center(ui.limit);

    lv_obj_set_style_text_font(ui.lanes, &font_icon_26, 0);
    lv_obj_set_width(ui.lanes, W - COL_L_W - 8);
    lv_obj_set_style_text_align(ui.lanes, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui.lanes, COL_L_W + 4, BAND_LANES_Y);

    lv_obj_remove_flag(ui.divider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(ui.street, W - 8);
    lv_obj_set_pos(ui.street, 4, STREET_Y);
    lv_obj_set_style_text_font(ui.street, &font_vn_22, 0);
    info_x = 4;
    info_y = INFO_Y;
    (void)p;
}

/* Bố cục theo HUD V2: hai vòng tròn chồng nhau ở giữa, ngã rẽ dồn sang trái,
   làn và tên đường sang phải. Đẹp hơn nhưng số nhỏ đi đáng kể — 51 px thay vì
   81 px — vì đường tròn ăn mất diện tích mà chữ nhật không tốn. */
static void place_waze(const palette_t *p)
{
    /* --- tầng trên: rẽ --- */
    lv_obj_set_style_text_font(ui.turn, &F_TURN, 0);
    lv_obj_set_pos(ui.turn, WZ_TURN_X, WZ_TURN_Y);
    lv_obj_set_pos(ui.turn_canvas, WZ_TURN_X, WZ_TURN_Y);
    lv_obj_set_pos(ui.exit_badge, WZ_TURN_X + 36, WZ_TURN_Y);
    lv_obj_set_style_text_font(ui.dist, &F_DIST, 0);
    lv_obj_set_width(ui.dist, WZ_DIST_W);
    lv_obj_set_pos(ui.dist, WZ_DIST_X, WZ_DIST_Y);

    /* --- tốc độ: số trần, không vòng; biển giới hạn là phần tử duy nhất có
       màu nên mắt bắt ngay đâu là "giới hạn", đâu là "đang chạy". --- */
    lv_obj_set_style_radius(ui.speed_bubble, 0, 0);
    lv_obj_set_style_bg_opa(ui.speed_bubble, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ui.speed_bubble, 0, 0);
    lv_obj_set_size(ui.speed_bubble, WZ_SPEED_W, WZ_SPEED_H);
    lv_obj_set_pos(ui.speed_bubble, WZ_SPEED_X, WZ_SPEED_Y);
    lv_obj_set_style_text_font(ui.speed, &F_SPEED, 0);
    lv_obj_set_width(ui.speed_unit, WZ_SPEED_W);
    lv_obj_set_style_text_align(ui.speed_unit, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui.speed_unit, WZ_SPEED_X, WZ_UNIT_Y);
    lv_obj_remove_flag(ui.speed_unit, LV_OBJ_FLAG_HIDDEN);

    lv_obj_set_size(ui.ring, WZ_LIMIT_D, WZ_LIMIT_D);
    lv_obj_set_pos(ui.ring, WZ_LIMIT_X, WZ_LIMIT_Y);
    lv_obj_set_style_border_width(ui.ring, WZ_RING_BORDER, 0);
    lv_obj_set_style_text_font(ui.limit, &F_LIMIT, 0);
    lv_obj_center(ui.limit);

    /* --- làn: ôm cả bề ngang dưới cột rẽ + tốc độ, căn giữa. --- */
    lv_obj_set_style_text_font(ui.lanes, &F_LANES, 0);
    lv_obj_set_width(ui.lanes, WZ_LANES_W);
    lv_obj_set_style_text_align(ui.lanes, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui.lanes, WZ_LANES_X, WZ_LANES_Y);

#ifdef WZ_CAR_X
    lv_obj_set_pos(ui.alert_sign, WZ_ALERT_X, WZ_ALERT_Y);
    lv_obj_set_style_text_font(ui.street_sub, &F_SUB, 0);
    lv_obj_set_width(ui.street_sub, WZ_STREET_W);
    lv_obj_set_pos(ui.street_sub, WZ_STREET_X, WZ_SUB_Y);
#endif

    /* --- vạch chia --- */
    lv_obj_set_pos(ui.divider, 4, WZ_DIVIDER_Y);
    lv_obj_set_width(ui.divider, W - 8);
    lv_obj_remove_flag(ui.divider, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(ui.eta_divider, 2, H - WZ_DIVIDER_Y - 12);
    lv_obj_set_pos(ui.eta_divider, WZ_COL_DIVIDER_X, WZ_DIVIDER_Y + 6);
    lv_obj_remove_flag(ui.eta_divider, LV_OBJ_FLAG_HIDDEN);

    /* --- tầng dưới trái: ETA, phút còn lại, km còn lại --- */
    lv_obj_set_style_text_font(ui.eta_label, &font_vn_16, 0);
    lv_obj_set_pos(ui.eta_label, WZ_ETA_LABEL_X, WZ_ETA_LABEL_Y);
    lv_obj_set_style_text_font(ui.eta_value, &font_vn_26, 0);
    lv_obj_set_pos(ui.eta_value, WZ_ETA_X, WZ_ETA_Y);
    lv_obj_set_width(ui.remain_time, WZ_REMAIN_W);
    lv_obj_set_pos(ui.remain_time, WZ_REMAIN_X, WZ_REMAIN_MIN_Y);
    lv_obj_set_width(ui.remain_dist, WZ_REMAIN_W);
    lv_obj_set_pos(ui.remain_dist, WZ_REMAIN_X, WZ_REMAIN_DIST_Y);

    /* --- tầng dưới phải: đường sắp vào, "Rồi ↰", đường đang đi --- */
    lv_obj_set_style_text_font(ui.street, &F_STREET, 0);
    lv_obj_set_width(ui.street, WZ_STREET_W);
    lv_obj_set_pos(ui.street, WZ_STREET_X, WZ_STREET_Y);
    lv_obj_set_pos(ui.next_label, WZ_STREET_X, WZ_NEXT_Y + 2);
    lv_obj_set_pos(ui.next_icon, WZ_STREET_X + 38, WZ_NEXT_Y);
    lv_obj_set_width(ui.street_cur, WZ_CUR_STREET_W);
    lv_obj_set_pos(ui.street_cur, WZ_STREET_X, WZ_CUR_STREET_Y);
    lv_obj_set_width(ui.street_next, WZ_NEXT_STREET_W);
    lv_obj_set_pos(ui.street_next, WZ_NEXT_STREET_X, WZ_NEXT_STREET_Y);
    lv_obj_set_pos(ui.big_clock, WZ_BIG_CLOCK_X, WZ_BIG_CLOCK_Y);
    lv_obj_set_width(ui.big_date, WZ_COL_DIVIDER_X - WZ_BIG_CLOCK_X);
    lv_obj_set_pos(ui.big_date, WZ_BIG_CLOCK_X, WZ_BIG_DATE_Y);

    lv_obj_set_style_text_font(ui.info, &F_INFO, 0);
    info_x = WZ_STREET_X;
    info_y = WZ_INFO_Y;
    (void)p;
}

static void place(void)
{
    const palette_t *p = palette();
    if (cfg_layout == UI_LAYOUT_WAZE) place_waze(p);
    else place_band(p);

    lv_obj_set_pos(ui.clock_time_box, WZ_CLOCK_TIME_X, WZ_CLOCK_TIME_Y);
    lv_obj_set_size(ui.clock_time_box, WZ_CLOCK_TIME_W, WZ_CLOCK_TIME_H);
    lv_obj_set_pos(ui.clock_date_box, WZ_CLOCK_DATE_X, WZ_CLOCK_DATE_Y);
    lv_obj_set_size(ui.clock_date_box, WZ_CLOCK_DATE_W, WZ_CLOCK_DATE_H);
    lv_obj_set_style_bg_opa(ui.clock_time_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_font(ui.clock_time, &font_vn_22, 0);
    lv_obj_set_style_text_color(ui.clock_time, p->dim, 0);
    lv_obj_center(ui.clock_time);
}

void ui_create(void)
{
    const palette_t *p = palette();
    ui_router_init();
    ui.screen = lv_screen_active();
    lv_obj_set_style_bg_color(ui.screen, p->bg, 0);
    lv_obj_set_style_bg_opa(ui.screen, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ui.screen, 0, 0);
    lv_obj_remove_flag(ui.screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Cây HUD nằm TRONG navigation_root — move_foreground nội bộ không thể
       vượt root để che Menu/Clock (§3.1). Chấm trạng thái tách riêng
       status_root, nổi trên mọi page. */
    lv_obj_t *nav = ui_router_root(UI_ROOT_NAVIGATION);
    lv_obj_t *status = ui_router_root(UI_ROOT_STATUS);

    /* --- cột trái: hướng rẽ --- */
    ui.turn = label(nav, &font_icon_44, p->speed);
    lv_obj_set_pos(ui.turn, COL_L_X + 22, 4);

    /* Vòng xuyến vẽ bằng vector, chồng đúng chỗ glyph rẽ (xem ui_maneuver.h). */
    ui.turn_canvas = ui_maneuver_create(nav);
    lv_obj_set_pos(ui.turn_canvas, COL_L_X + 22, 4);

    ui.exit_badge = label(nav, &font_vn_16, p->bg);
    lv_obj_set_style_bg_color(ui.exit_badge, p->speed, 0);
    lv_obj_set_style_bg_opa(ui.exit_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ui.exit_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(ui.exit_badge, 5, 0);
    lv_obj_set_style_pad_ver(ui.exit_badge, 1, 0);
    lv_obj_set_pos(ui.exit_badge, COL_L_X + 58, 6);
    lv_obj_add_flag(ui.exit_badge, LV_OBJ_FLAG_HIDDEN);

    ui.dist = label(nav, &font_dist_30, p->speed);
    lv_obj_set_width(ui.dist, COL_L_W);
    lv_obj_set_style_text_align(ui.dist, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(ui.dist, COL_L_X, BAND_DIST_Y);

    /* --- giữa: tốc độ ---
       Bố cục Waze bọc số trong một vòng tròn xanh; bố cục dải thì bubble trong
       suốt và số nằm trực tiếp trên nền. Dùng chung một widget cho cả hai. */
    ui.speed_bubble = lv_obj_create(nav);
    lv_obj_set_style_border_width(ui.speed_bubble, 0, 0);
    lv_obj_set_style_pad_all(ui.speed_bubble, 0, 0);
    lv_obj_set_style_bg_opa(ui.speed_bubble, LV_OPA_TRANSP, 0);
    lv_obj_remove_flag(ui.speed_bubble, LV_OBJ_FLAG_SCROLLABLE);

    ui.speed = label(ui.speed_bubble, &font_speed_112, p->speed);
    ui.speed_unit = label(nav, &font_vn_16, p->dim);
    lv_label_set_text(ui.speed_unit, "km/h");
    lv_obj_set_style_text_align(ui.speed, LV_TEXT_ALIGN_RIGHT, 0);

    /* --- phải: biển giới hạn --- */
    ui.ring = lv_obj_create(nav);
    lv_obj_set_size(ui.ring, LIMIT_D, LIMIT_D);
    lv_obj_set_pos(ui.ring, LIMIT_X, LIMIT_Y);
    lv_obj_set_style_radius(ui.ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ui.ring, 5, 0);
    lv_obj_set_style_border_color(ui.ring, p->ring, 0);
    lv_obj_set_style_bg_color(ui.ring, p->ring_fill, 0);
    lv_obj_set_style_bg_opa(ui.ring, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(ui.ring, 0, 0);
    lv_obj_remove_flag(ui.ring, LV_OBJ_FLAG_SCROLLABLE);

    ui.limit = label(ui.ring, &font_limit_40, p->ring_text);
    lv_obj_center(ui.limit);

    ui.clock_time_box = pill(nav, WZ_CLOCK_TIME_W, WZ_CLOCK_TIME_H);
    ui.clock_time = label(ui.clock_time_box, &font_vn_26, lv_color_black());
    lv_obj_center(ui.clock_time);
    lv_obj_add_flag(ui.clock_time_box, LV_OBJ_FLAG_HIDDEN);

    ui.clock_date_box = pill(nav, WZ_CLOCK_DATE_W, WZ_CLOCK_DATE_H);
    ui.clock_date = label(ui.clock_date_box, &font_vn_20, lv_color_black());
    lv_obj_center(ui.clock_date);
    lv_obj_add_flag(ui.clock_date_box, LV_OBJ_FLAG_HIDDEN);

    /* --- chỉ dẫn làn --- */
    ui.lanes = label(nav, &font_icon_26, p->muted);
    lv_obj_set_style_text_font(ui.lanes, &font_icon_26, 0);
    lv_obj_set_pos(ui.lanes, COL_L_W + 4, 82);
    lv_obj_add_flag(ui.lanes, LV_OBJ_FLAG_HIDDEN);

    /* --- phần dưới --- */
    ui.divider = lv_obj_create(nav);
    lv_obj_set_size(ui.divider, W, 1);
    lv_obj_set_pos(ui.divider, 0, BAND_Y);
    lv_obj_set_style_bg_color(ui.divider, p->muted, 0);
    lv_obj_set_style_bg_opa(ui.divider, LV_OPA_40, 0);
    lv_obj_set_style_border_width(ui.divider, 0, 0);
    lv_obj_set_style_radius(ui.divider, 0, 0);

    ui.eta_divider = lv_obj_create(nav);
    lv_obj_set_style_bg_color(ui.eta_divider, p->muted, 0);
    lv_obj_set_style_bg_opa(ui.eta_divider, LV_OPA_80, 0);
    lv_obj_set_style_border_width(ui.eta_divider, 0, 0);
    lv_obj_set_style_radius(ui.eta_divider, 0, 0);
    lv_obj_add_flag(ui.eta_divider, LV_OBJ_FLAG_HIDDEN);

    ui.eta_label = label(nav, &font_vn_20, p->dim);
    lv_label_set_text(ui.eta_label, "ETA");
    lv_obj_add_flag(ui.eta_label, LV_OBJ_FLAG_HIDDEN);

    ui.eta_value = label(nav, &font_vn_26, p->speed);
    lv_obj_add_flag(ui.eta_value, LV_OBJ_FLAG_HIDDEN);

    ui.street = label(nav, &font_vn_22, p->street);
    lv_obj_set_width(ui.street, W - 8);
    lv_obj_set_pos(ui.street, 4, STREET_Y);
    /* Tên đường có thể tới 40 code point mà bề ngang chỉ vừa ~22 ký tự,
       nên cho chạy vòng thay vì cắt cụt. */
    lv_label_set_long_mode(ui.street, LV_LABEL_LONG_SCROLL_CIRCULAR);

    /* "Rồi ↰": ngã rẽ ngay sau ngã kế tiếp (trn2), để người lái chuẩn bị làn
       sớm khi hai ngã sát nhau. */
    ui.next_label = label(nav, &font_vn_20, p->dim);
    lv_label_set_text(ui.next_label, "Rồi");
    lv_obj_add_flag(ui.next_label, LV_OBJ_FLAG_HIDDEN);
    ui.next_icon = label(nav, &font_icon_26, p->speed);
    lv_obj_add_flag(ui.next_icon, LV_OBJ_FLAG_HIDDEN);

    /* Phút / km còn lại dưới ETA, và tên đường đang đi (mờ) dưới "Rồi". */
    ui.remain_time = label(nav, &font_vn_20, p->speed);
    lv_obj_add_flag(ui.remain_time, LV_OBJ_FLAG_HIDDEN);
    ui.remain_dist = label(nav, &font_vn_20, p->dim);
    lv_obj_add_flag(ui.remain_dist, LV_OBJ_FLAG_HIDDEN);
    ui.street_next = label(nav, &font_vn_20, p->street);
    lv_obj_set_style_text_align(ui.street_next, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(ui.street_next, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_add_flag(ui.street_next, LV_OBJ_FLAG_HIDDEN);

    /* Font số lớn chỉ có chữ số (không có ":"), nên đồng hồ lớn ghép 3 nhãn
       trong một hàng flex: giờ · ":" · phút. */
    ui.big_clock = lv_obj_create(nav);
    lv_obj_remove_style_all(ui.big_clock);
    lv_obj_set_size(ui.big_clock, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ui.big_clock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.big_clock, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(ui.big_clock, LV_OBJ_FLAG_SCROLLABLE);
    ui.big_hh = label(ui.big_clock, &font_circle_54, p->speed);
    ui.big_colon = label(ui.big_clock, &font_vn_26, p->speed);
    lv_label_set_text(ui.big_colon, ":");
    lv_obj_set_style_pad_hor(ui.big_colon, 2, 0);
    ui.big_mm = label(ui.big_clock, &font_circle_54, p->speed);
    lv_obj_add_flag(ui.big_clock, LV_OBJ_FLAG_HIDDEN);
    ui.big_date = label(nav, &font_vn_22, p->dim);
    lv_obj_add_flag(ui.big_date, LV_OBJ_FLAG_HIDDEN);

    ui.street_cur = label(nav, &font_vn_16, p->muted);
    lv_label_set_long_mode(ui.street_cur, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_add_flag(ui.street_cur, LV_OBJ_FLAG_HIDDEN);

    ui.info_icon = label(nav, &font_icon_26, p->alert);
    lv_obj_set_pos(ui.info_icon, 4, INFO_Y - 3);
    lv_obj_add_flag(ui.info_icon, LV_OBJ_FLAG_HIDDEN);

    ui.info = label(nav, &font_vn_16, p->dim);
    lv_label_set_long_mode(ui.info, LV_LABEL_LONG_SCROLL_CIRCULAR);

    for (int i = 0; i < HUD_VISIBLE_ALERTS; ++i) {
        ui.alert_images[i] = lv_image_create(nav);
        ui.alert_fallback[i] = label(nav, &font_icon_26, p->alert);
        ui.alert_distances[i] = label(nav, &font_vn_16, p->alert);
        ui.alert_details[i] = label(nav, &font_vn_16, p->dim);
        lv_label_set_long_mode(ui.alert_details[i], LV_LABEL_LONG_SCROLL_CIRCULAR);
        lv_obj_add_flag(ui.alert_images[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.alert_fallback[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.alert_distances[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.alert_details[i], LV_OBJ_FLAG_HIDDEN);
    }

#ifdef WZ_CAR_X
    ui.street_sub = label(nav, &F_SUB, p->dim);
    lv_obj_add_flag(ui.street_sub, LV_OBJ_FLAG_HIDDEN);

    /* Ô cảnh báo ở cột phải: dùng chung cho MỌI thứ cần cảnh báo — camera đèn
       đỏ, cấm vượt, sắp giảm tốc độ, công an, tai nạn... Icon đổi theo cảnh
       báo đang hiện hành, chứ không phải một biển cố định. */
    ui.alert_sign = label(nav, &font_icon_44, p->alert);
    lv_obj_add_flag(ui.alert_sign, LV_OBJ_FLAG_HIDDEN);
#endif
    lv_obj_set_pos(ui.info, 4, INFO_Y);

    ui.avg_bar = lv_obj_create(nav);
    lv_obj_set_size(ui.avg_bar, 0, 3);
    lv_obj_set_pos(ui.avg_bar, 0, H - 3);
    lv_obj_set_style_bg_color(ui.avg_bar, p->avg, 0);
    lv_obj_set_style_bg_opa(ui.avg_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui.avg_bar, 0, 0);
    lv_obj_set_style_radius(ui.avg_bar, 0, 0);
    lv_obj_add_flag(ui.avg_bar, LV_OBJ_FLAG_HIDDEN);

    /* --- màn chờ khi chưa có liên kết ---
       Phủ KÍN (không mờ một nửa): HUD trước đây lộ số 0 km/h và biển giới hạn
       cũ phía sau, trông như đang chạy. Bố cục một cột căn giữa:
           [ᛒ̸ Chờ kết nối...]      ← trạng thái, màu theo trạng thái
              19 : 54              ← đồng hồ lớn
          Thứ Hai, 13/10/2026      */
    ui.overlay = lv_obj_create(nav);
    lv_obj_set_size(ui.overlay, W, H);
    lv_obj_set_pos(ui.overlay, 0, 0);
    lv_obj_set_style_bg_color(ui.overlay, p->bg, 0);
    lv_obj_set_style_bg_opa(ui.overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui.overlay, 0, 0);
    lv_obj_set_style_radius(ui.overlay, 0, 0);
    lv_obj_set_style_pad_all(ui.overlay, 0, 0);
    lv_obj_set_style_pad_row(ui.overlay, 10, 0);
    lv_obj_set_flex_flow(ui.overlay, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(ui.overlay, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(ui.overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *link_row = lv_obj_create(ui.overlay);
    lv_obj_remove_style_all(link_row);
    lv_obj_set_size(link_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(link_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(link_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(link_row, 6, 0);
    ui.overlay_icon = label(link_row, &font_icon_26, p->muted);
    lv_label_set_text(ui.overlay_icon, ICO_NO_LINK);
    ui.overlay_text = label(link_row, &font_vn_22, p->muted);

    /* Font số lớn không có ":" — dấu hai chấm vẽ bằng hai chấm tròn. */
    ui.overlay_clock = lv_obj_create(ui.overlay);
    lv_obj_remove_style_all(ui.overlay_clock);
    lv_obj_set_size(ui.overlay_clock, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ui.overlay_clock, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ui.overlay_clock, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ui.overlay_clock, 8, 0);
    ui.overlay_hh = label(ui.overlay_clock, &font_circle_100, p->speed);
    lv_obj_t *colon = lv_obj_create(ui.overlay_clock);
    lv_obj_remove_style_all(colon);
    lv_obj_set_size(colon, 10, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(colon, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(colon, 20, 0);
    for (int i = 0; i < 2; ++i) {
        lv_obj_t *dot = lv_obj_create(colon);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 10, 10);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, p->speed, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    }
    ui.overlay_mm = label(ui.overlay_clock, &font_circle_100, p->speed);
    ui.overlay_date = label(ui.overlay, &font_vn_22, p->dim);


    /* Chữ trạng thái "W"/"B" ở góc trên-phải, nằm trong status_root nên luôn
       nổi trên mọi page, giống thanh trạng thái điện thoại. Dùng chữ cái thay
       vì chấm màu trần vì 2 chấm không phân biệt được WiFi/BLE; tô màu CHỮ
       theo trạng thái thay vì phải sinh thêm icon font. */
    ui.wifi_dot = label(status, &font_vn_16, lv_color_hex(0x4B5563));
    lv_label_set_text(ui.wifi_dot, "W");
    lv_obj_set_pos(ui.wifi_dot, W - 28, 2);
    lv_obj_remove_flag(ui.wifi_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(ui.wifi_dot);

    /* Icon nguồn dự phòng cạnh "W B": chưa nối Waze nhưng đang hiện dữ liệu
       khác — mũi tên định vị tím = GPS từ app CarDash, ổ lưu xanh dương = bộ
       nhớ đường qua WiFi. Thay cho dòng chữ ở đáy màn từng đè lên tên đường. */
    ui.cached_badge = label(status, &lv_font_montserrat_14, lv_color_hex(0xA78BFA));
    lv_label_set_text(ui.cached_badge, LV_SYMBOL_GPS);
    lv_obj_set_pos(ui.cached_badge, W - 48, 4);
    lv_obj_remove_flag(ui.cached_badge, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(ui.cached_badge, LV_OBJ_FLAG_HIDDEN);

    ui.ble_dot = label(status, &font_vn_16, lv_color_hex(0x4B5563));
    lv_label_set_text(ui.ble_dot, "B");
    lv_obj_set_pos(ui.ble_dot, W - 14, 2);
    lv_obj_remove_flag(ui.ble_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_foreground(ui.ble_dot);

    place();
    ui_set_link(UI_LINK_ADVERTISING);
    ui_set_wifi_link(UI_WIFI_OFF);

    /* Đăng ký các page; màn ĐẦU TIÊN là CLOCK (không hiện HUD một frame rồi
       mới phủ CLOCK — ui_create chạy trước vòng render đầu tiên). */
    static const ui_page_t clock_page = {
        .name = "clock", .enter = ui_clock_page_enter, .exit = ui_clock_page_exit};
    static const ui_page_t menu_page_def = {
        .name = "menu", .enter = ui_menu_page_enter, .exit = ui_menu_page_exit};
    static const ui_page_t wifi_page_def = {
        .name = "wifi", .enter = ui_wifi_page_enter, .exit = ui_wifi_page_exit};
    static const ui_page_t settings_page_def = {
        .name = "settings", .enter = ui_settings_page_enter, .exit = ui_settings_page_exit};
    ui_router_register_page(UI_SCREEN_CLOCK, &clock_page);
    ui_router_register_page(UI_SCREEN_MENU, &menu_page_def);
    ui_router_register_page(UI_SCREEN_WIFI, &wifi_page_def);
    ui_router_register_page(UI_SCREEN_SETTINGS, &settings_page_def);
    static const ui_page_t trip_page_def = {
        .name = "trip", .enter = ui_trip_page_enter, .exit = ui_trip_page_exit};
    ui_router_register_page(UI_SCREEN_TRIP, &trip_page_def);
    ui_router_boot_screen(UI_SCREEN_CLOCK);
}

static void apply_palette(void)
{
    const palette_t *p = palette();
    lv_obj_set_style_bg_color(ui.screen, p->bg, 0);
    lv_obj_set_style_text_color(ui.turn, p->speed, 0);
    lv_obj_set_style_text_color(ui.dist, p->speed, 0);
    lv_obj_set_style_text_color(ui.next_label, p->dim, 0);
    lv_obj_set_style_text_color(ui.next_icon, p->speed, 0);
    lv_obj_set_style_text_color(ui.remain_time, p->speed, 0);
    lv_obj_set_style_text_color(ui.remain_dist, p->dim, 0);
    lv_obj_set_style_text_color(ui.street_cur, p->muted, 0);
    lv_obj_set_style_text_color(ui.street_next, p->street, 0);
    lv_obj_set_style_text_color(ui.big_hh, p->speed, 0);
    lv_obj_set_style_text_color(ui.big_colon, p->speed, 0);
    lv_obj_set_style_text_color(ui.big_mm, p->speed, 0);
    lv_obj_set_style_text_color(ui.big_date, p->dim, 0);
    lv_obj_set_style_bg_color(ui.exit_badge, p->speed, 0);
    lv_obj_set_style_text_color(ui.exit_badge, p->bg, 0);
    lv_obj_set_style_border_color(ui.ring, p->ring, 0);
    /* Bố cục Waze giữ biển trắng chữ đen kể cả ban đêm — đó là thứ làm nó
       nhận ra ngay là biển báo giao thông. Bố cục dải thì đổi sang nền tối để
       đỡ loá, vì ở đó vòng tròn nhỏ hơn và nằm sát mép. */
    bool waze = cfg_layout == UI_LAYOUT_WAZE;
    lv_obj_set_style_bg_color(ui.ring, waze ? THEME_DAY.ring_fill : p->ring_fill, 0);
    lv_obj_set_style_text_color(ui.limit, waze ? THEME_DAY.ring_text : p->ring_text, 0);
    lv_obj_set_style_text_color(ui.street, p->street, 0);
    lv_obj_set_style_bg_color(ui.avg_bar, p->avg, 0);
    lv_obj_set_style_bg_color(ui.overlay, p->bg, 0);
    lv_obj_set_style_bg_color(ui.clock_time_box, lv_color_white(), 0);
    lv_obj_set_style_bg_color(ui.clock_date_box, lv_color_white(), 0);
    lv_obj_set_style_text_color(ui.clock_time, lv_color_black(), 0);
    lv_obj_set_style_text_color(ui.clock_date, lv_color_black(), 0);
    lv_obj_set_style_text_color(ui.clock_time, p->dim, 0);
    lv_obj_set_style_text_color(ui.eta_label, p->dim, 0);
    lv_obj_set_style_text_color(ui.eta_value, p->speed, 0);
    lv_obj_set_style_text_color(ui.speed_unit, p->dim, 0);
}

void ui_set_theme(ui_theme_t theme)
{
    cfg_theme = theme;
    apply_palette();
}

void ui_set_layout(ui_layout_t layout)
{
    if (layout == cfg_layout) return;
    cfg_layout = layout;
    place();
    apply_palette();
}

void ui_set_alerts(bool enabled, int distance_threshold_m)
{
    cfg_alerts = enabled;
    cfg_alert_dist = distance_threshold_m;
}

/* WiFi: mô hình + page đã chuyển sang ui_wifi.c (§3.2); facade giữ tên cũ. */
void ui_set_local_hour(int hour)
{
    if (hour == local_hour) return;
    local_hour = hour;
    if (cfg_theme == UI_THEME_AUTO) apply_palette();
}

static void render_clock_widget(void)
{
    bool big = idle_clock && local_time_text[0];
    if (big) {
        /* "HH:MM" → hai nửa cho hai nhãn font số. */
        char hh[3] = {local_time_text[0], local_time_text[1], 0};
        lv_label_set_text(ui.big_hh, hh);
        lv_label_set_text(ui.big_mm, local_time_text + 3);
        lv_label_set_text(ui.big_date, local_date_text);
        lv_obj_remove_flag(ui.big_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui.big_date, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.big_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.big_date, LV_OBJ_FLAG_HIDDEN);
    }
    if (!local_time_text[0] || big) {
        lv_obj_add_flag(ui.clock_time_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.clock_date_box, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_label_set_text(ui.clock_time, local_time_text);
    lv_label_set_text(ui.clock_date, local_date_text);
    if (clock_blocked_by_alert) {
        lv_obj_add_flag(ui.clock_time_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.clock_date_box, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_remove_flag(ui.clock_time_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.clock_date_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui.clock_date_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ui.clock_time_box);
    lv_obj_move_foreground(ui.clock_date_box);
}

static void set_clock_blocked_by_alert(bool blocked)
{
    if (clock_blocked_by_alert == blocked) return;
    clock_blocked_by_alert = blocked;
    render_clock_widget();
}

/* Đồng hồ của màn chờ. Chưa có giờ (điện thoại chưa đồng bộ, không có NTP)
   thì ẩn hẳn đồng hồ + ngày, chỉ còn dòng trạng thái ở giữa màn. */
static void render_overlay_clock(void)
{
    if (!ui.overlay_clock) return;
    if (!local_time_text[0]) {
        lv_obj_add_flag(ui.overlay_clock, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.overlay_date, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char hh[3] = {local_time_text[0], local_time_text[1], 0};
    lv_label_set_text(ui.overlay_hh, hh);
    lv_label_set_text(ui.overlay_mm, local_time_text + 3);
    lv_label_set_text(ui.overlay_date, local_date_long);
    lv_obj_remove_flag(ui.overlay_clock, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(ui.overlay_date, LV_OBJ_FLAG_HIDDEN);
}

void ui_set_local_datetime(int hour, int minute, int wday, int mday, int month, int year)
{
    if (hour < 0 || minute < 0) {
        local_time_text[0] = '\0';
        local_date_text[0] = '\0';
        local_date_long[0] = '\0';
        render_clock_widget();
        render_overlay_clock();
        return;
    }
    static const char *day_names[] = {"Chủ nhật", "Thứ Hai", "Thứ Ba", "Thứ Tư",
                                      "Thứ Năm", "Thứ Sáu", "Thứ Bảy"};
    if (wday >= 0 && wday < 7)
        snprintf(local_date_long, sizeof(local_date_long), "%s, %02d/%02d/%04d",
                 day_names[wday], mday % 100, month % 100, year % 10000);
    else
        snprintf(local_date_long, sizeof(local_date_long), "%02d/%02d/%04d",
                 mday % 100, month % 100, year % 10000);

    static const char *days[] = {"CN", "T2", "T3", "T4", "T5", "T6", "T7"};
    const char *day = (wday >= 0 && wday < 7) ? days[wday] : "";

    char time_buf[sizeof(local_time_text)];
    char date_buf[sizeof(local_date_text)];
    /* Clock chỉ cần 2 chữ số; chặn % 100 để "%02d" không thể tràn time_buf
       (GCC 15 của IDF 6.x coi -Wformat-truncation là lỗi). */
    snprintf(time_buf, sizeof(time_buf), "%02d:%02d", hour % 100, minute % 100);
    snprintf(date_buf, sizeof(date_buf), "%s %02d/%02d/%02d",
             day, mday, month, year % 100);

    if (!strcmp(time_buf, local_time_text) && !strcmp(date_buf, local_date_text))
        return;

    strcpy(local_time_text, time_buf);
    strcpy(local_date_text, date_buf);
    render_clock_widget();
    render_overlay_clock();
}

void ui_set_link(ui_link_t link)
{
    link_state = link;
    if (ui.ble_dot) {
        lv_color_t c = link == UI_LINK_LIVE        ? lv_color_hex(0x2ECC71)  /* xanh: đang nhận */
                       : link == UI_LINK_CONNECTED  ? lv_color_hex(0xF1C40F) /* vàng: nối rồi, chưa có state */
                       : link == UI_LINK_STALE      ? lv_color_hex(0xE74C3C) /* đỏ: mất tín hiệu */
                       : link == UI_LINK_GPS        ? lv_color_hex(0xA78BFA) /* tím: GPS từ app */
                                                     : lv_color_hex(0x4B5563); /* xám: chưa ai kết nối */
        lv_obj_set_style_text_color(ui.ble_dot, c, 0);
    }
    if (ui.cached_badge) {
        if (link == UI_LINK_GPS)
            lv_obj_remove_flag(ui.cached_badge, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(ui.cached_badge, LV_OBJ_FLAG_HIDDEN);
    }
    if (link == UI_LINK_LIVE || link == UI_LINK_GPS) {
        lv_obj_add_flag(ui.overlay, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(ui.overlay, LV_OBJ_FLAG_HIDDEN);
    /* Font chữ không có "…" (U+2026), nên dùng ba dấu chấm thường. Màu icon
       khớp với chữ "B" ở góc: xám = chờ, vàng = đang bắt tay, đỏ = mất. */
    lv_label_set_text(ui.overlay_text,
                      link == UI_LINK_STALE      ? "Mất kết nối..."
                      : link == UI_LINK_CONNECTED ? "Đang bắt tay..."
                                                  : "Chờ kết nối...");
    lv_color_t c = link == UI_LINK_STALE      ? lv_color_hex(0xE74C3C)
                   : link == UI_LINK_CONNECTED ? lv_color_hex(0xF1C40F)
                                               : palette()->muted;
    lv_obj_set_style_text_color(ui.overlay_icon, c, 0);
    render_overlay_clock();
}

void ui_set_wifi_link(ui_wifi_link_t state)
{
    if (!ui.wifi_dot) return;
    lv_color_t c = state == UI_WIFI_CONNECTED   ? lv_color_hex(0x2ECC71)  /* xanh: đã kết nối */
                   : state == UI_WIFI_CONNECTING ? lv_color_hex(0xF1C40F) /* vàng: đang kết nối/quét */
                   : state == UI_WIFI_ERROR       ? lv_color_hex(0xE74C3C) /* đỏ: lỗi/mất kết nối */
                                                   : lv_color_hex(0x4B5563); /* xám: chưa cấu hình */
    lv_obj_set_style_text_color(ui.wifi_dot, c, 0);
}

/* Khoảng cách tới ngã rẽ: dưới 1 km hiển thị mét làm tròn, trên thì đổi sang km
   một chữ số thập phân — đúng cách bảng chỉ đường ngoài thực tế vẫn ghi. */
static void format_distance(int metres, char *out, size_t size)
{
    if (metres < 0) {
        out[0] = '\0';
    } else if (metres >= 1000) {
        snprintf(out, size, "%d,%d km", metres / 1000, (metres % 1000) / 100);
    } else {
        int rounded = metres >= 100 ? (metres / 10) * 10 : metres;
        snprintf(out, size, "%d m", rounded);
    }
}

/* Mỗi làn vẽ bằng icon mũi tên tương ứng hướng Waze khuyến nghị. Làn không được
   khuyến nghị (selected == 0) vẫn vẽ nhưng mờ đi — biết mình đang ở làn sai
   quan trọng ngang với biết làn nào đúng.

   Cả dãy nằm chung một nhãn nên phải dùng recolor của LVGL để tô từng làn một
   màu; trước đây mọi làn ra cùng một màu, tức là phần "mờ đi" chưa từng chạy —
   chỉ lộ ra khi có dữ liệu làn thật từ chuyến 2026-09-02. */
static void render_lanes(const hud_state_t *s)
{
    bool have_lanes = s->lanes_present && s->lane_count > 0;
    lanes_wide = false;
    lanes_cover_left = false;
    if (!have_lanes) {
        lv_obj_add_flag(ui.lanes, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (cfg_layout == UI_LAYOUT_WAZE) {
        /* Tới 9 làn vừa dưới cột rẽ + tốc độ. Nhiều hơn thì dãy làn chiếm cả bề
           ngang (12 × 26 = 312 px) và render_info ẩn hai cảnh báo phụ cùng hàng:
           sắp vào nút giao lớn thì chọn đúng làn quan trọng hơn cảnh báo xa. */
        lanes_wide = s->lane_count > WZ_LANES_MAX_NARROW;
        bool side = s->lane_count <= WZ_LANES_MAX_SIDE;
        lanes_cover_left = !side;
        lv_obj_set_width(ui.lanes, lanes_wide ? W : side ? WZ_LANES_SIDE_W : WZ_LANES_W);
        lv_obj_set_x(ui.lanes, side ? WZ_LANES_SIDE_X : WZ_LANES_X);
    }

    /* "#RRGGBB ...#" là cú pháp đổi màu trong chuỗi của LVGL, nên mỗi làn có màu
       riêng dù cả dãy nằm chung một nhãn. 9 byte cho thẻ mở, 4 cho icon, 1 cho
       thẻ đóng. */
    char buf[HUD_LANES_MAX * 14 + 1] = {0};
    const palette_t *p = palette();
    for (int i = 0; i < s->lane_count; ++i) {
        uint8_t mask = s->lanes[i].selected ? s->lanes[i].selected
                                            : s->lanes[i].directions;
        const char *icon = ICO_STRAIGHT;
        if (mask & 0x04) icon = ICO_LEFT;
        else if (mask & 0x20) icon = ICO_RIGHT;
        else if (mask & 0x02) icon = ICO_SLIGHT_LEFT;
        else if (mask & 0x10) icon = ICO_SLIGHT_RIGHT;
        else if (mask & 0x08) icon = ICO_SHARP_LEFT;
        else if (mask & 0x40) icon = ICO_SHARP_RIGHT;
        else if (mask & 0x80) icon = ICO_UTURN_LEFT;

        lv_color_t c = s->lanes[i].selected ? p->street : p->muted;
        char part[16];
        snprintf(part, sizeof(part), "#%02X%02X%02X %s#",
                 c.red, c.green, c.blue, icon);
        strncat(buf, part, sizeof(buf) - strlen(buf) - 1);
    }
    lv_label_set_recolor(ui.lanes, true);
    lv_label_set_text(ui.lanes, buf);
    lv_obj_remove_flag(ui.lanes, LV_OBJ_FLAG_HIDDEN);
}

/* Dòng dưới cùng phải tranh chỗ giữa ba thứ. Thứ tự ưu tiên:
     vùng cấm vượt > cảnh báo gần > ETA
   Cấm vượt xếp trên vì nó là ràng buộc đang có hiệu lực ngay lúc này, còn cảnh
   báo chỉ là thứ sắp tới. */
static void render_info(const hud_state_t *s)
{
    const palette_t *p = palette();
    char buf[96];
    hud_alert_entry_t alerts[HUD_VISIBLE_ALERTS];
    int count = hud_select_alerts(s, cfg_alerts, cfg_alert_dist, alerts);

    for (int i = 0; i < HUD_VISIBLE_ALERTS; ++i) {
        lv_obj_add_flag(ui.alert_images[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.alert_fallback[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.alert_distances[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.alert_details[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_width(ui.info, W - info_x - 4);
    if (cfg_layout == UI_LAYOUT_WAZE) {
        lv_obj_add_flag(ui.alert_sign, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(ui.street, WZ_STREET_W);
        set_clock_blocked_by_alert(false);
        {
            lv_obj_add_flag(ui.info, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.info_icon, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ui.eta_label, "ETA");
            lv_label_set_text(ui.eta_value, s->nav && s->eta[0] ? s->eta : "--:--");
            /* Chỉ `nav` quyết định có chuyến. Producer có thể còn giữ eta/rm/rmin
               cũ sau khi dừng dẫn đường; coi đó là "đang có chuyến" thì cột ETA
               vẽ đè lên đồng hồ lớn của chế độ rảnh (idle_clock = !nav). */
            bool trip = s->nav;
            if (trip) {
                lv_obj_remove_flag(ui.eta_label, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui.eta_value, LV_OBJ_FLAG_HIDDEN);
                lv_obj_remove_flag(ui.eta_divider, LV_OBJ_FLAG_HIDDEN);
            } else {
                /* Không có chuyến: cột trái là đồng hồ lớn (render_clock_widget);
                   chỉ khi chưa có giờ thì cột trống và bỏ luôn vạch dọc. */
                lv_obj_add_flag(ui.eta_label, LV_OBJ_FLAG_HIDDEN);
                lv_obj_add_flag(ui.eta_value, LV_OBJ_FLAG_HIDDEN);
                if (local_time_text[0]) lv_obj_remove_flag(ui.eta_divider, LV_OBJ_FLAG_HIDDEN);
                else lv_obj_add_flag(ui.eta_divider, LV_OBJ_FLAG_HIDDEN);
            }
            /* Phút / km còn lại: hai dòng riêng dưới ETA thay vì dồn một dòng,
               để cả hai đọc được ở cỡ 20. */
            if (trip && s->remain_min > 0) {
                if (s->remain_min >= 60)
                    snprintf(buf, sizeof(buf), "%d giờ %02d", s->remain_min / 60, s->remain_min % 60);
                else
                    snprintf(buf, sizeof(buf), "%d phút", s->remain_min);
                lv_label_set_text(ui.remain_time, buf);
                lv_obj_remove_flag(ui.remain_time, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(ui.remain_time, LV_OBJ_FLAG_HIDDEN);
            }
            if (trip && s->remain_m > 0) {
                format_distance(s->remain_m, buf, sizeof(buf));
                lv_label_set_text(ui.remain_dist, buf);
                lv_obj_remove_flag(ui.remain_dist, LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(ui.remain_dist, LV_OBJ_FLAG_HIDDEN);
            }
            render_clock_widget();
            lv_obj_add_flag(ui.avg_bar, LV_OBJ_FLAG_HIDDEN);
            if (s->avg_zone) {
                int pct = s->avg_pct < 0 ? 0 : s->avg_pct > 100 ? 100 : s->avg_pct;
                lv_obj_set_width(ui.avg_bar, W * pct / 100);
                lv_obj_remove_flag(ui.avg_bar, LV_OBJ_FLAG_HIDDEN);
            }
            if (lanes_wide && count > 1) count = 1;
            for (int i = 0; i < count; ++i) {
                const hud_alert_entry_t *a = &alerts[i];
                bool large = i == 0;
                int x = i == 0 ? WZ_ALERT_X : i == 1 ? WZ_ALERT2_X1 : WZ_ALERT2_X2;
                int y = large ? WZ_ALERT_Y : WZ_ALERT2_Y;
                int size = large ? 44 : 26;
                int text_w = large ? WZ_ALERT_COL_W : WZ_ALERT_COL_W / 2;
                int text_x = large ? WZ_ALERT_COL_X : x + size / 2 - text_w / 2;
                const lv_image_dsc_t *bitmap = hud_alert_bitmap(a->kind,
                    a->has_value ? a->value : -1, a->traffic_severity, large);
                if (bitmap) {
                    lv_image_set_src(ui.alert_images[i], bitmap);
                    lv_obj_set_pos(ui.alert_images[i], x, y);
                    lv_obj_remove_flag(ui.alert_images[i], LV_OBJ_FLAG_HIDDEN);
                } else {
                    lv_obj_t *fallback = ui.alert_fallback[i];
                    bool speed_sign = a->kind == HUD_ALERT_SPEED_DROP
                                      && a->has_value && a->value > 0;
                    lv_obj_set_size(fallback, size, size);
                    lv_obj_set_pos(fallback, x, y);
                    lv_obj_set_style_text_font(fallback, speed_sign ? (large ? &font_vn_26 : &font_vn_16)
                                              : large ? &font_icon_44 : &font_icon_26, 0);
                    lv_obj_set_style_text_align(fallback, LV_TEXT_ALIGN_CENTER, 0);
                    lv_obj_set_style_radius(fallback, LV_RADIUS_CIRCLE, 0);
                    lv_obj_set_style_border_width(fallback, speed_sign ? 2 : 0, 0);
                    lv_obj_set_style_border_color(fallback, lv_color_hex(0xFF3030), 0);
                    lv_obj_set_style_bg_color(fallback, lv_color_white(), 0);
                    lv_obj_set_style_bg_opa(fallback, speed_sign ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
                    lv_obj_set_style_text_color(fallback, speed_sign ? lv_color_black() : p->alert, 0);
                    lv_obj_set_style_pad_top(fallback, speed_sign ? (size - (large ? font_vn_26.line_height : font_vn_16.line_height)) / 2 : 0, 0);
                    if (speed_sign) snprintf(buf, sizeof(buf), "%d", a->value);
                    else snprintf(buf, sizeof(buf), "%s", ICO_WARNING);
                    lv_label_set_text(fallback, buf);
                    lv_obj_remove_flag(fallback, LV_OBJ_FLAG_HIDDEN);
                }
                if (a->distance < 0) snprintf(buf, sizeof(buf), "--");
                else if (a->distance >= 1000)
                    snprintf(buf, sizeof(buf), "%d.%dkm", a->distance / 1000, (a->distance % 1000) / 100);
                else snprintf(buf, sizeof(buf), "%dm", a->distance);
                lv_obj_t *distance = ui.alert_distances[i];
                lv_obj_set_pos(distance, text_x, large ? WZ_ALERT_DIST_Y : WZ_ALERT2_DIST_Y);
                lv_obj_set_width(distance, text_w);
                lv_obj_set_style_text_align(distance, LV_TEXT_ALIGN_CENTER, 0);
                lv_label_set_long_mode(distance, LV_LABEL_LONG_SCROLL_CIRCULAR);
                lv_obj_set_style_text_color(distance, a->distance >= 0 && a->distance <= 200
                    ? p->speed_over : p->alert, 0);
                lv_label_set_text(distance, buf);
                lv_obj_remove_flag(distance, LV_OBJ_FLAG_HIDDEN);

                if (a->kind == HUD_ALERT_TRAFFIC_JAM && a->traffic_delay_min >= 0)
                    snprintf(buf, sizeof(buf), "+%d ph", a->traffic_delay_min);
                else if (a->kind == HUD_ALERT_SPEED_DROP && a->has_value
                         && ui_limit_change(s->limit, a->value)[0])
                    snprintf(buf, sizeof(buf), "%s · %s", ui_limit_change(s->limit, a->value), ui_alert_name(a->kind));
                else snprintf(buf, sizeof(buf), "%s", ui_alert_name(a->kind));
                lv_obj_set_pos(ui.alert_details[i], text_x, WZ_ALERT_NAME_Y);
                lv_obj_set_width(ui.alert_details[i], text_w);
                lv_obj_set_style_text_align(ui.alert_details[i], LV_TEXT_ALIGN_CENTER, 0);
                lv_label_set_text(ui.alert_details[i], buf);
                if (large) lv_obj_remove_flag(ui.alert_details[i], LV_OBJ_FLAG_HIDDEN);
            }
            return;
        }
    }

    if (s->avg_zone) {
        set_clock_blocked_by_alert(true);
        snprintf(buf, sizeof(buf), "Cấm vượt · còn %d m · %d km/h",
                 s->avg_left, s->avg_speed);
        lv_label_set_text(ui.info, buf);
        lv_obj_set_style_text_color(ui.info, p->avg, 0);
        lv_label_set_text(ui.info_icon, ICO_NO_PASSING);
        lv_obj_set_style_text_color(ui.info_icon, p->avg, 0);
        lv_obj_set_pos(ui.info_icon, info_x, info_y - 3);
        lv_obj_remove_flag(ui.info_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_pos(ui.info, info_x + 30, info_y);
        lv_obj_set_width(ui.info, W - info_x - 34);

        int width = s->avg_pct <= 0 ? 0 : (s->avg_pct >= 100 ? W : W * s->avg_pct / 100);
        lv_obj_set_width(ui.avg_bar, width);
        lv_obj_remove_flag(ui.avg_bar, LV_OBJ_FLAG_HIDDEN);
#ifdef WZ_ALERT_X
        /* Ô cảnh báo là mỏ neo thị giác; dòng chữ bên dưới nói còn bao xa. */
        if (cfg_layout == UI_LAYOUT_WAZE) {
            lv_label_set_text(ui.alert_sign, ICO_NO_PASSING);
            lv_obj_set_style_text_color(ui.alert_sign, p->avg, 0);
            lv_obj_remove_flag(ui.alert_sign, LV_OBJ_FLAG_HIDDEN);
        }
#endif
        return;
    }
    lv_obj_add_flag(ui.avg_bar, LV_OBJ_FLAG_HIDDEN);


    bool show_alert = count > 0;
    if (show_alert) {
        const hud_alert_entry_t *a = &alerts[0];
        set_clock_blocked_by_alert(true);
        char dist[16];
        format_distance(a->distance, dist, sizeof(dist));
        if (a->kind == HUD_ALERT_SPEED_DROP && a->has_value
            && ui_limit_change(s->limit, a->value)[0])
            snprintf(buf, sizeof(buf), "%s · %s · %s · %d km/h",
                     ui_limit_change(s->limit, a->value), ui_alert_name(a->kind), dist, a->value);
        else if (a->has_value)
            snprintf(buf, sizeof(buf), "%s · %s · %d km/h",
                     ui_alert_name(a->kind), dist, a->value);
        else
            snprintf(buf, sizeof(buf), "%s · %s", ui_alert_name(a->kind), dist);
        lv_label_set_text(ui.info, buf);
        lv_obj_set_style_text_color(ui.info, p->alert, 0);
        lv_label_set_text(ui.info_icon, ui_alert_icon(a->kind));
        lv_obj_set_style_text_color(ui.info_icon, p->alert, 0);
        lv_obj_set_pos(ui.info_icon, info_x, info_y - 3);
        lv_obj_remove_flag(ui.info_icon, LV_OBJ_FLAG_HIDDEN);
        if (a->kind == HUD_ALERT_SPEED_DROP && a->has_value && a->value > 0) {
            lv_obj_t *sign = ui.alert_fallback[0];
            lv_obj_set_size(sign, 26, 26);
            lv_obj_set_pos(sign, info_x, info_y - 3);
            lv_obj_set_style_text_font(sign, &font_vn_16, 0);
            lv_obj_set_style_text_align(sign, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_radius(sign, LV_RADIUS_CIRCLE, 0);
            lv_obj_set_style_border_width(sign, 2, 0);
            lv_obj_set_style_border_color(sign, p->ring, 0);
            lv_obj_set_style_bg_color(sign, lv_color_white(), 0);
            lv_obj_set_style_bg_opa(sign, LV_OPA_COVER, 0);
            lv_obj_set_style_text_color(sign, lv_color_black(), 0);
            lv_obj_set_style_pad_top(sign, (26 - font_vn_16.line_height) / 2, 0);
            snprintf(buf, sizeof(buf), "%d", a->value);
            lv_label_set_text(sign, buf);
            lv_obj_remove_flag(sign, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(ui.info_icon, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_set_pos(ui.info, info_x + 30, info_y);
        lv_obj_set_width(ui.info, W - info_x - 34);
#ifdef WZ_ALERT_X
        if (cfg_layout == UI_LAYOUT_WAZE) {
            lv_label_set_text(ui.alert_sign, ui_alert_icon(a->kind));
            lv_obj_set_style_text_color(ui.alert_sign, p->alert, 0);
            lv_obj_remove_flag(ui.alert_sign, LV_OBJ_FLAG_HIDDEN);
        }
#endif
        return;
    }

#ifdef WZ_ALERT_X
    lv_obj_add_flag(ui.alert_sign, LV_OBJ_FLAG_HIDDEN);
#endif
    set_clock_blocked_by_alert(false);
    lv_obj_add_flag(ui.info_icon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(ui.info, info_x, info_y);
    lv_obj_set_style_text_color(ui.info, p->dim, 0);

    buf[0] = '\0';
    if (s->nav) {
        char remain[16];
        format_distance(s->remain_m, remain, sizeof(remain));
        if (s->eta[0] && s->remain_min > 0)
            snprintf(buf, sizeof(buf), "%s · %d phút · %s", s->eta,
                     s->remain_min, remain);
        else if (s->eta[0])
            snprintf(buf, sizeof(buf), "%s · %s", s->eta, remain);
    }
    lv_label_set_text(ui.info, buf);
}

void ui_update(const hud_state_t *s)
{
    const palette_t *p = palette();
    char buf[32];

    /* Tốc độ: vượt tốc thì đổi màu chứ không thêm ký hiệu — mắt bắt màu nhanh
       hơn bắt hình khi đang liếc. */
    snprintf(buf, sizeof(buf), "%d", s->speed);
    lv_label_set_text(ui.speed, buf);
    if (cfg_layout == UI_LAYOUT_WAZE)
        lv_obj_set_style_text_font(ui.speed, s->speed >= 100 ? &font_circle_54 : &font_circle_70, 0);
    /* §3 nói `over` chỉ bật khi vượt giới hạn CỘNG biên độ dung sai của mod
       (Waze mặc định +5 km/h), nên chạy 61 ở đường 60 vẫn là `over=0`. Trên
       HUD thì nên đỏ ngay khi qua vạch: người lái cần biết mình đang quá tốc
       độ, chứ không phải biết khi nào Waze quyết định kêu. Vẫn tôn trọng
       `over` để không bỏ sót trường hợp producer bật nó vì lý do khác. */
    bool over_limit = s->over || (s->limit > 0 && s->speed > s->limit);
    lv_color_t speed_color = over_limit ? p->speed_over : p->speed;
    lv_obj_set_style_text_color(ui.speed, speed_color, 0);
    if (cfg_layout == UI_LAYOUT_WAZE)
        lv_obj_set_style_border_color(ui.speed_bubble, speed_color, 0);
    lv_obj_update_layout(ui.speed);
    if (cfg_layout == UI_LAYOUT_WAZE) lv_obj_center(ui.speed);
    else lv_obj_align(ui.speed, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* lim = 0 nghĩa là chưa biết ⇒ ẩn hẳn biển, không hiện số 0. */
    if (s->limit > 0) {
        snprintf(buf, sizeof(buf), "%d", s->limit);
        lv_label_set_text(ui.limit, buf);
        /* Biển 54 px chỉ vừa 2 chữ số cỡ 40; 100/120 trên cao tốc chuyển sang
           chữ condensed, không thì tràn khỏi vành đỏ. */
        if (cfg_layout == UI_LAYOUT_WAZE) {
            lv_obj_set_style_text_font(ui.limit, s->limit >= 100 ? &font_dist_34 : &F_LIMIT, 0);
            lv_obj_center(ui.limit);
        }
        lv_obj_remove_flag(ui.ring, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.ring, LV_OBJ_FLAG_HIDDEN);
    }

    /* Vòng xuyến vẽ vector (có số lối ra ở tâm); các ngã khác dùng glyph. */
    bool drawn = ui_maneuver_draw(ui.turn_canvas, s->turn, s->exit, p->speed, p->muted, p->bg);
    lv_label_set_text(ui.turn, drawn ? "" : ui_turn_icon(s->turn));

    char dist[16];
    format_distance(s->distance, dist, sizeof(dist));
    lv_label_set_text(ui.dist, dist);

    bool roundabout = s->turn == HUD_TURN_ROUNDABOUT
                      || s->turn == HUD_TURN_ROUNDABOUT_LEFT
                      || s->turn == HUD_TURN_ROUNDABOUT_RIGHT
                      || s->turn == HUD_TURN_ROUNDABOUT_STRAIGHT;
    if (roundabout && s->exit > 0 && !drawn) {
        snprintf(buf, sizeof(buf), "%d", s->exit);
        lv_label_set_text(ui.exit_badge, buf);
        lv_obj_remove_flag(ui.exit_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.exit_badge, LV_OBJ_FLAG_HIDDEN);
    }

    /* Đang dẫn đường thì tên đường KẾ TIẾP quan trọng hơn đường hiện tại — đó
       là đường sắp rẽ vào. Không dẫn đường thì hiện đường đang đi. */
    /* Bố cục Waze: đường sắp tới đã có chỗ riêng dưới mũi tên, nên dòng lớn ở
       tầng dưới luôn là đường ĐANG đi (thiếu thì mới lấy đường sắp tới).
       Bố cục dải không có chỗ thứ hai: đang dẫn đường thì ưu tiên đường sắp tới. */
    const char *street;
    if (cfg_layout == UI_LAYOUT_WAZE)
        street = s->street[0] ? s->street : s->street_next;
    else
        street = (s->nav && s->street_next[0]) ? s->street_next : s->street;
    idle_clock = cfg_layout == UI_LAYOUT_WAZE && !s->nav;
    lv_label_set_long_mode(ui.street, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(ui.street, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_text(ui.street, street);
#ifdef WZ_CAR_X
    lv_obj_add_flag(ui.street_sub, LV_OBJ_FLAG_HIDDEN);
#endif

    /* trn2 chỉ có nghĩa khi đang dẫn đường; mã lạ thì ẩn chứ không đoán (§4.1).
       Bố cục dải không còn chỗ trống nên chỉ hiện ở bố cục Waze. */
    /* Đường đang đi chỉ hiện khi dòng trên đang là đường SẮP vào; không dẫn
       đường thì dòng trên đã là đường hiện tại rồi. */
    bool show_cur = false; /* đường hiện tại đã lên dòng lớn */
    if (show_cur) {
        lv_label_set_text(ui.street_cur, s->street);
        lv_obj_remove_flag(ui.street_cur, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.street_cur, LV_OBJ_FLAG_HIDDEN);
    }

    bool show_next = cfg_layout == UI_LAYOUT_WAZE && s->nav && hud_turn_known(s->turn_next);
    if (show_next) {
        lv_label_set_text(ui.next_icon, ui_turn_icon(s->turn_next));
        lv_obj_remove_flag(ui.next_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ui.next_icon, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.next_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui.next_icon, LV_OBJ_FLAG_HIDDEN);
    }

    render_lanes(s);

    /* Đường sắp tới dưới mũi tên; nhường chỗ khi dãy làn chiếm cột trái — lúc
       đó sắp vào nút giao, chọn đúng làn quan trọng hơn đọc tên đường. Thiếu
       `st` thì `st2` đã nằm ở dòng lớn, không lặp lại. */
    bool show_next_street = cfg_layout == UI_LAYOUT_WAZE && s->nav && s->street_next[0]
                            && s->street[0] && !lanes_cover_left;
    if (show_next_street) {
        lv_label_set_text(ui.street_next, s->street_next);
        lv_obj_remove_flag(ui.street_next, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(ui.street_next, LV_OBJ_FLAG_HIDDEN);
    }

    render_info(s);
}

