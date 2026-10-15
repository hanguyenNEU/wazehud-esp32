#include "anim_bg.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <string.h>

/* Chia màn thành ô 40×40 để gom vùng đổi: invalidate từng đoạn lẻ sẽ tràn
   bộ đệm vùng của LVGL (32) và nó vẽ lại cả màn. */
#define TILE 40

static const char *TAG = "anim_bg";

static struct {
    const anim_bg_t *bg;
    uint16_t *pixels;
    lv_image_dsc_t dsc;
    lv_obj_t *image;
    lv_timer_t *timer;
    uint16_t frame; /* khung đang hiện */
} s;

static void apply_frame(uint16_t frame)
{
    const anim_bg_t *bg = s.bg;
    const uint16_t *p = bg->delta + bg->frame_offsets[frame];
    const uint16_t *end = bg->delta + bg->frame_offsets[frame + 1];

    int cols = (bg->w + TILE - 1) / TILE;
    int rows = (bg->h + TILE - 1) / TILE;
    uint8_t dirty[(320 / TILE + 1) * (240 / TILE + 1)] = {0};
    if (cols * rows > (int)sizeof(dirty)) return; /* ảnh lớn hơn màn: không hỗ trợ */

    while (p + 3 <= end) {
        uint32_t start = ((uint32_t)p[0] << 16) | p[1];
        uint16_t len = p[2];
        p += 3;
        if (p + len > end || start + len > (uint32_t)bg->w * bg->h) break; /* dữ liệu hỏng */
        memcpy(s.pixels + start, p, len * sizeof(uint16_t));
        p += len;

        int y = start / bg->w;
        int x0 = start % bg->w;
        int x1 = x0 + len - 1;
        for (int c = x0 / TILE; c <= x1 / TILE; ++c) dirty[(y / TILE) * cols + c] = 1;
    }

    /* Gộp các ô liền nhau trên cùng hàng ô thành một vùng. */
    lv_area_t origin;
    lv_obj_get_coords(s.image, &origin);
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            if (!dirty[r * cols + c]) continue;
            int c_end = c;
            while (c_end + 1 < cols && dirty[r * cols + c_end + 1]) ++c_end;
            lv_area_t area = {
                .x1 = origin.x1 + c * TILE,
                .y1 = origin.y1 + r * TILE,
                .x2 = origin.x1 + LV_MIN((c_end + 1) * TILE, bg->w) - 1,
                .y2 = origin.y1 + LV_MIN((r + 1) * TILE, bg->h) - 1,
            };
            lv_obj_invalidate_area(s.image, &area);
            c = c_end;
        }
    }
}

static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s.pixels || !s.image) return;
    s.frame = (uint16_t)((s.frame + 1) % s.bg->frame_count);
    apply_frame(s.frame);
}

lv_obj_t *anim_bg_start(lv_obj_t *parent, const anim_bg_t *bg)
{
    anim_bg_stop();
    if (!bg) return NULL;
    s.bg = bg;
    s.frame = 0;

    size_t bytes = (size_t)bg->w * bg->h * sizeof(uint16_t);
    /* Ảnh tĩnh (1 khung): vẽ thẳng từ `base`, khỏi chép thêm 150 KB. */
    if (bg->frame_count > 1) {
        s.pixels = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s.pixels) memcpy(s.pixels, bg->base, bytes);
        else ESP_LOGW(TAG, "không cấp được %u B PSRAM — hiện ảnh tĩnh", (unsigned)bytes);
    }

    s.dsc = (lv_image_dsc_t){
        .header.magic = LV_IMAGE_HEADER_MAGIC,
        .header.cf = LV_COLOR_FORMAT_RGB565,
        .header.w = bg->w,
        .header.h = bg->h,
        .header.stride = bg->w * sizeof(uint16_t),
        .data_size = bytes,
        .data = s.pixels ? (const uint8_t *)s.pixels : (const uint8_t *)bg->base,
    };
    s.image = lv_image_create(parent);
    lv_image_set_src(s.image, &s.dsc);
    lv_obj_set_pos(s.image, 0, 0);
    lv_obj_remove_flag(s.image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_move_background(s.image); /* luôn nằm dưới chữ, kể cả khi đổi ảnh */

    if (s.pixels && bg->frame_count > 1)
        s.timer = lv_timer_create(tick_cb, bg->frame_ms, NULL);
    return s.image;
}

void anim_bg_stop(void)
{
    if (s.timer) lv_timer_delete(s.timer);
    s.timer = NULL;
    /* Xoá luôn image — nó trỏ vào buffer sắp giải phóng; đổi ảnh nền giữa
       chừng (vuốt dọc) mà để lại là vẽ bộ nhớ đã free. */
    if (s.image) lv_obj_delete(s.image);
    s.image = NULL;
    if (s.pixels) heap_caps_free(s.pixels);
    s.pixels = NULL;
    s.bg = NULL;
}
