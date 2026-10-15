#include "ui_maneuver.h"
#include "hud_state.h"

#include "esp_heap_caps.h"
#include <math.h>
#include <stdio.h>

LV_FONT_DECLARE(font_vn_16)

/* Hình học trong ô 64×64. Tâm vòng hơi lệch lên để nhánh vào phía dưới đủ dài
 * mà mắt vẫn nhận ra đó là "đường mình đang đi tới". */
#define CX 32
#define CY 31
#define STROKE 7
#define RING_R 18          /* bán kính NGOÀI — quy ước của lv_draw_arc */
#define RING_MID 15        /* tâm nét vòng = RING_R - STROKE/2 */
#define TIP_R 30           /* đỉnh mũi tên: 31-30 = 1 px cách mép trên */
#define HEAD_LEN 9
#define HEAD_HALF 8
#define ENTRY_ANGLE 90     /* nhánh vào luôn từ dưới lên */

/* Nhánh tách (giữ làn / lối ra): điểm tách và độ dài nhánh trước đầu mũi tên. */
#define FORK_Y 40
#define FORK_LEN 19

/* Một canvas RGB565 duy nhất (8 KB): chỉ ô ngã rẽ chính dùng tới. Buffer nằm
 * ở PSRAM, KHÔNG được khai báo static: RAM trong chỉ còn khối liền ~31 KB mà
 * BLE controller cần đúng 30 KB lúc khởi động — bản static 8 KB làm BLE_INIT
 * "Malloc failed" rồi reboot vòng (HUD nhấp nháy). */
static lv_draw_buf_t maneuver_buf;

int ui_maneuver_roundabout_exit_angle(int turn, int exit)
{
    /* VN đi bên phải nên vòng xuyến chạy NGƯỢC chiều kim đồng hồ: từ nhánh vào
       phía dưới, xe qua bên phải trước, rồi phía trên, rồi bên trái. */
    switch (turn) {
    case HUD_TURN_ROUNDABOUT_RIGHT:    return 0;
    case HUD_TURN_ROUNDABOUT_STRAIGHT: return 270;
    case HUD_TURN_ROUNDABOUT_LEFT:     return 180;
    case HUD_TURN_ROUNDABOUT:
        /* Waze không cho hướng, chỉ cho số lối ra: đoán theo thứ tự gặp lối ra
           trên vòng 4 nhánh. Con số vẫn in ở tâm nên đoán lệch không đánh lừa. */
        switch (exit) {
        case 1:  return 0;
        case 3:  return 180;
        /* Lối thứ 4 trên vòng 4 nhánh là quay đầu: nằm sát bên trái nhánh vào.
           135° chứ không phải 90° để mũi tên không đè lên nhánh vào. */
        case 4:  return 135;
        default: return 270;
        }
    default:
        return -1;
    }
}

static lv_point_precise_t polar_from(float ox, float oy, int angle_deg, float r)
{
    float a = (float)angle_deg * (float)M_PI / 180.0f;
    return (lv_point_precise_t){
        .x = (lv_value_precise_t)(ox + r * cosf(a)),
        .y = (lv_value_precise_t)(oy + r * sinf(a)),
    };
}

static lv_point_precise_t polar(int angle_deg, float r)
{
    return polar_from(CX, CY, angle_deg, r);
}

static void arc(lv_layer_t *layer, int from, int to, lv_color_t color)
{
    lv_draw_arc_dsc_t dsc;
    lv_draw_arc_dsc_init(&dsc);
    dsc.center.x = CX;
    dsc.center.y = CY;
    dsc.radius = RING_R;
    dsc.width = STROKE;
    /* lv_draw_arc chạy theo chiều kim đồng hồ từ start tới end. */
    dsc.start_angle = from;
    dsc.end_angle = to;
    dsc.color = color;
    lv_draw_arc(layer, &dsc);
}

static void line(lv_layer_t *layer, lv_point_precise_t a, lv_point_precise_t b, lv_color_t color)
{
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.p1 = a;
    dsc.p2 = b;
    dsc.width = STROKE;
    dsc.color = color;
    dsc.round_start = 1;
    dsc.round_end = 1;
    lv_draw_line(layer, &dsc);
}

/* Đầu mũi tên có đỉnh ở `tip`, hướng theo `angle_deg` (quy ước LVGL). */
static void arrow_head(lv_layer_t *layer, lv_point_precise_t tip, int angle_deg, lv_color_t color)
{
    lv_point_precise_t base = polar_from(tip.x, tip.y, angle_deg + 180, HEAD_LEN);
    float a = (float)angle_deg * (float)M_PI / 180.0f;
    float px = -sinf(a) * HEAD_HALF, py = cosf(a) * HEAD_HALF;
    lv_draw_triangle_dsc_t head;
    lv_draw_triangle_dsc_init(&head);
    head.p[0] = tip;
    head.p[1] = (lv_point_precise_t){base.x + (lv_value_precise_t)px, base.y + (lv_value_precise_t)py};
    head.p[2] = (lv_point_precise_t){base.x - (lv_value_precise_t)px, base.y - (lv_value_precise_t)py};
    head.color = color;
    lv_draw_triangle(layer, &head);
}

static void draw_roundabout(lv_layer_t *layer, int exit_angle, int exit, lv_color_t fg,
                            lv_color_t dim)
{
    /* Phần vòng KHÔNG đi qua tô mờ: chiều kim đồng hồ từ nhánh vào tới lối ra. */
    arc(layer, ENTRY_ANGLE, exit_angle, dim);
    /* Quãng đi thật (ngược chiều kim đồng hồ từ dưới tới lối ra) bằng đúng cung
       theo chiều kim đồng hồ từ lối ra về nhánh vào. */
    arc(layer, exit_angle, ENTRY_ANGLE, fg);

    /* Nhánh vào. */
    line(layer, (lv_point_precise_t){CX, UI_MANEUVER_SIZE - 3},
         (lv_point_precise_t){CX, CY + RING_MID}, fg);

    /* Nhánh ra + đầu mũi tên. */
    line(layer, polar(exit_angle, RING_MID), polar(exit_angle, TIP_R - HEAD_LEN + 1), fg);
    arrow_head(layer, polar(exit_angle, TIP_R), exit_angle, fg);

    if (exit > 0 && exit < 10) {
        char text[4];
        snprintf(text, sizeof(text), "%d", exit);
        lv_draw_label_dsc_t label;
        lv_draw_label_dsc_init(&label);
        label.text = text;
        label.text_local = 1;
        label.font = &font_vn_16;
        label.color = fg;
        label.align = LV_TEXT_ALIGN_CENTER;
        int h = font_vn_16.line_height;
        lv_area_t area = {CX - 10, CY - h / 2, CX + 10, CY - h / 2 + h};
        lv_draw_label(layer, &label, &area);
    }
}

/* Ngã tách: đường chính đi thẳng tô mờ, nhánh cần vào tách 45° sang `side`
   (-1 trái, +1 phải). Waze iOS chỉ có MỘT ảnh cho cả lối ra, giữ làn lẫn rẽ
   chếch ở chỗ tách nhánh (xem tweak hlpTurnFromImageName), nên một hình chung
   cho 13–16 trung thực hơn là giả vờ phân biệt được. */
static void draw_fork(lv_layer_t *layer, int side, lv_color_t fg, lv_color_t dim)
{
    lv_point_precise_t split = {CX, FORK_Y};
    line(layer, split, (lv_point_precise_t){CX, 4}, dim);
    line(layer, (lv_point_precise_t){CX, UI_MANEUVER_SIZE - 3}, split, fg);
    int angle = side > 0 ? 315 : 225;
    line(layer, split, polar_from(split.x, split.y, angle, FORK_LEN), fg);
    arrow_head(layer, polar_from(split.x, split.y, angle, FORK_LEN + HEAD_LEN - 1), angle, fg);
}

static int fork_side(int turn)
{
    switch (turn) {
    case HUD_TURN_KEEP_LEFT:
    case HUD_TURN_EXIT_LEFT:  return -1;
    case HUD_TURN_KEEP_RIGHT:
    case HUD_TURN_EXIT_RIGHT: return 1;
    default:                  return 0;
    }
}

lv_obj_t *ui_maneuver_create(lv_obj_t *parent)
{
    lv_obj_t *canvas = lv_canvas_create(parent);
    lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    uint32_t stride = lv_draw_buf_width_to_stride(UI_MANEUVER_SIZE, LV_COLOR_FORMAT_RGB565);
    uint32_t size = stride * UI_MANEUVER_SIZE;
    void *pixels = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    /* Thiếu PSRAM thì chỉ mất hình vector: ui_maneuver_draw trả false và bên
       gọi quay về glyph font như trước. */
    if (!pixels) return canvas;
    lv_draw_buf_init(&maneuver_buf, UI_MANEUVER_SIZE, UI_MANEUVER_SIZE,
                     LV_COLOR_FORMAT_RGB565, stride, pixels, size);
    lv_canvas_set_draw_buf(canvas, &maneuver_buf);
    return canvas;
}

bool ui_maneuver_draw(lv_obj_t *canvas, int turn, int exit, lv_color_t fg,
                      lv_color_t dim, lv_color_t bg)
{
    int angle = ui_maneuver_roundabout_exit_angle(turn, exit);
    int side = fork_side(turn);
    if ((angle < 0 && !side) || !maneuver_buf.data) {
        lv_obj_add_flag(canvas, LV_OBJ_FLAG_HIDDEN);
        return false;
    }

    lv_canvas_fill_bg(canvas, bg, LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    if (angle >= 0) draw_roundabout(&layer, angle, exit, fg, dim);
    else draw_fork(&layer, side, fg, dim);
    lv_canvas_finish_layer(canvas, &layer);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_HIDDEN);
    return true;
}
