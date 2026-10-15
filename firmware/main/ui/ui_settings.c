#include "ui_settings.h"

#include "board.h"
#include "ui_router.h"

#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(font_vn_16)
LV_FONT_DECLARE(font_vn_20)
LV_FONT_DECLARE(font_vn_26)

#define EDGE 20 /* slider không nằm trong dải cạnh (ưu tiên swipe) */

static ui_settings_patch_cb_t patch_action;
static ui_settings_cal_cb_t cal_action;
static void *action_user;

static hud_config_snapshot_t snapshot; /* mô hình hiển thị (copy, UI sở hữu) */
static bool have_snapshot;
static uint32_t request_id_gen;
static uint32_t outstanding_id;
static hud_config_field_t outstanding_field;
static bool has_outstanding;

static char firmware_text[32] = "—";
static ui_settings_update_cb_t update_action;
static void *update_user;
static char update_status_text[64];
static char update_button_text[32] = "Kiểm tra cập nhật";
static bool update_busy;

/* Widgets sống */
static lv_obj_t *bright_slider, *bright_value;
static lv_obj_t *theme_btn, *layout_btn, *alerts_btn;
static lv_obj_t *dist_value, *dim_value;
static lv_obj_t *save_status;
static lv_obj_t *update_btn, *update_status;
static lv_obj_t *controls;

static void refresh_pending(void)
{
    if (!controls) return;
    for (uint32_t i = 0; i < lv_obj_get_child_count(controls); ++i) {
        lv_obj_t *child = lv_obj_get_child(controls, i);
        if (has_outstanding) lv_obj_add_state(child, LV_STATE_DISABLED);
        else lv_obj_remove_state(child, LV_STATE_DISABLED);
    }
}

static const char *theme_label(void)
{
    if (!strcmp(snapshot.value.theme, "day")) return "Ban ngày";
    if (!strcmp(snapshot.value.theme, "night")) return "Ban đêm";
    return "Tự động";
}

static const char *layout_label(void)
{
    return !strcmp(snapshot.value.layout, "speed") ? "Dải ngang" : "Vòng tròn";
}

static const char *alerts_label(void)
{
    return snapshot.value.alerts ? "Bật" : "Tắt";
}

void ui_settings_set_handlers(ui_settings_patch_cb_t patch_cb,
                              ui_settings_cal_cb_t cal_cb, void *user)
{
    patch_action = patch_cb;
    cal_action = cal_cb;
    action_user = user;
}

void ui_settings_set_snapshot(const hud_config_snapshot_t *snap)
{
    if (!snap) return;
    snapshot = *snap;
    have_snapshot = true;
    if (bright_value) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", snapshot.value.brightness);
        lv_label_set_text(bright_value, buf);
    }
    if (bright_slider) lv_slider_set_value(bright_slider, snapshot.value.brightness,
                                           LV_ANIM_OFF);
    if (theme_btn) lv_label_set_text(lv_obj_get_child(theme_btn, 0), theme_label());
    if (layout_btn) lv_label_set_text(lv_obj_get_child(layout_btn, 0), layout_label());
    if (alerts_btn) lv_label_set_text(lv_obj_get_child(alerts_btn, 0), alerts_label());
    if (dist_value) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d m", snapshot.value.alert_dist);
        lv_label_set_text(dist_value, buf);
    }
    if (dim_value) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d ph", snapshot.value.dim_idle);
        lv_label_set_text(dim_value, buf);
    }
}

void ui_settings_set_result(const hud_config_result_t *result)
{
    if (!result || !has_outstanding) return;
    /* UI chỉ cho một request outstanding — kết quả luôn của request đang chờ. */
    if (result->request_id != outstanding_id) return;
    has_outstanding = false;
    refresh_pending();

    const char *msg = "Đã lưu";
    if (result->kind == HUD_CONFIG_CONFLICT) msg = "Đã đổi từ nơi khác";
    else if (result->kind == HUD_CONFIG_INVALID) msg = "Giá trị không hợp lệ";
    else if (result->kind == HUD_CONFIG_STORAGE_ERROR) msg = "Lưu thất bại";
    else if (result->kind == HUD_CONFIG_BUSY) msg = "Bận, thử lại";
    if (save_status) lv_label_set_text(save_status, msg);

    /* Mọi kết quả đều kèm snapshot mới nhất — hiện lại đúng trạng thái thật. */
    ui_settings_set_snapshot(&result->snapshot);
}

void ui_settings_set_firmware(const char *version)
{
    snprintf(firmware_text, sizeof(firmware_text), "%s", version ? version : "—");
}

void ui_settings_set_update_handler(ui_settings_update_cb_t cb, void *user)
{
    update_action = cb;
    update_user = user;
}

static void refresh_update(void)
{
    if (update_status) lv_label_set_text(update_status, update_status_text);
    if (!update_btn) return;
    lv_label_set_text(lv_obj_get_child(update_btn, 0), update_button_text);
    if (update_busy) lv_obj_add_state(update_btn, LV_STATE_DISABLED);
    else lv_obj_remove_state(update_btn, LV_STATE_DISABLED);
}

void ui_settings_set_update_status(const char *status, const char *button, bool busy)
{
    snprintf(update_status_text, sizeof(update_status_text), "%s", status ? status : "");
    if (button) snprintf(update_button_text, sizeof(update_button_text), "%s", button);
    update_busy = busy;
    refresh_update();
}

static void submit(hud_config_field_t field, const hud_config_t *value)
{
    if (!patch_action || !have_snapshot) return;
    if (has_outstanding) return; /* một request outstanding; nút đã bị khóa */

    hud_config_patch_t patch = {
        .field = field,
        .value = *value,
        .expected_revision = snapshot.revision,
        .request_id = ++request_id_gen,
    };
    outstanding_id = patch.request_id;
    outstanding_field = field;
    has_outstanding = true;
    refresh_pending();
    if (save_status) lv_label_set_text(save_status, "Đang lưu…");
    patch_action(&patch, action_user);
}

/* ---- Controls ---- */

static void bright_changed(lv_event_t *event)
{
    if (!bright_value) return;
    char buf[16];
    snprintf(buf, sizeof(buf), "%d%%",
             (int)lv_slider_get_value(lv_event_get_target(event)));
    lv_label_set_text(bright_value, buf);
}

static void bright_released(lv_event_t *event)
{
    if (!have_snapshot || has_outstanding) return;
    hud_config_t value = snapshot.value;
    value.brightness = ((lv_slider_get_value(lv_event_get_target(event)) + 2) / 5) * 5;
    submit(HUD_CONFIG_FIELD_BRIGHTNESS, &value);
}

static void cycle_theme(lv_event_t *event)
{
    (void)event;
    if (!have_snapshot || has_outstanding) return;
    hud_config_t value = snapshot.value;
    if (!strcmp(value.theme, "auto")) strcpy(value.theme, "day");
    else if (!strcmp(value.theme, "day")) strcpy(value.theme, "night");
    else strcpy(value.theme, "auto");
    submit(HUD_CONFIG_FIELD_THEME, &value);
}

static void cycle_layout(lv_event_t *event)
{
    (void)event;
    if (!have_snapshot || has_outstanding) return;
    hud_config_t value = snapshot.value;
    strcpy(value.layout, !strcmp(value.layout, "waze") ? "speed" : "waze");
    submit(HUD_CONFIG_FIELD_LAYOUT, &value);
}

static void toggle_alerts(lv_event_t *event)
{
    (void)event;
    if (!have_snapshot || has_outstanding) return;
    hud_config_t value = snapshot.value;
    value.alerts = !value.alerts;
    submit(HUD_CONFIG_FIELD_ALERTS, &value);
}

static void step_alert_dist(lv_event_t *event)
{
    int step = (int)(intptr_t)lv_event_get_user_data(event);
    if (!have_snapshot || has_outstanding) return;
    hud_config_t value = snapshot.value;
    int v = value.alert_dist + step;
    if (v < 200) v = 200;
    if (v > 2000) v = 2000;
    if (v == value.alert_dist) return;
    value.alert_dist = v;
    submit(HUD_CONFIG_FIELD_ALERT_DIST, &value);
}

static void step_dim_idle(lv_event_t *event)
{
    int step = (int)(intptr_t)lv_event_get_user_data(event);
    if (!have_snapshot || has_outstanding) return;
    hud_config_t value = snapshot.value;
    int v = value.dim_idle + step;
    if (v < 0) v = 0;
    if (v > 30) v = 30;
    if (v == value.dim_idle) return;
    value.dim_idle = v;
    submit(HUD_CONFIG_FIELD_DIM_IDLE, &value);
}

static void start_calibration(lv_event_t *event)
{
    (void)event;
    if (cal_action) cal_action(action_user);
}

static void update_clicked(lv_event_t *event)
{
    (void)event;
    if (!update_busy && update_action) update_action(update_user);
}

static void back_cb(lv_event_t *event)
{
    (void)event;
    ui_router_request_screen(UI_SCREEN_MENU);
}

/* ---- Bố cục ---- */

static lv_obj_t *row_label(lv_obj_t *parent, const char *text, int y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &font_vn_16, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, EDGE, y);
    return label;
}

static lv_obj_t *small_button(lv_obj_t *parent, const char *text, int x, int y,
                              int w, lv_event_cb_t cb, intptr_t user_data)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, 40);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x202631), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(obj, lv_color_hex(0x475569), LV_STATE_PRESSED);
    lv_obj_set_style_opa(obj, LV_OPA_50, LV_STATE_DISABLED);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, 8, 0);
    lv_obj_set_style_pad_all(obj, 0, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(obj, cb, LV_EVENT_CLICKED, (void *)user_data);
    lv_obj_t *caption = lv_label_create(obj);
    lv_obj_set_style_text_font(caption, &font_vn_16, 0);
    lv_obj_set_style_text_color(caption, lv_color_white(), 0);
    lv_label_set_text(caption, text);
    lv_obj_add_flag(caption, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(caption);
    return obj;
}

static lv_obj_t *value_button(lv_obj_t *parent, const char *text, int x, int y,
                              int w, lv_event_cb_t cb)
{
    return small_button(parent, text, x, y, w, cb, 0);
}

void ui_settings_page_enter(lv_obj_t *parent)
{
    int w = DISPLAY_W, h = DISPLAY_H;

    lv_obj_t *title = lv_label_create(parent);
    lv_obj_set_style_text_font(title, &font_vn_26, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_label_set_text(title, "Cài đặt");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 2);

    small_button(parent, "Menu", 12, 4, 64, back_cb, (intptr_t)0);

    lv_obj_t *firmware = lv_label_create(parent);
    lv_obj_set_style_text_font(firmware, &font_vn_16, 0);
    lv_obj_set_style_text_color(firmware, lv_color_hex(0x8E8E93), 0);
    lv_label_set_text(firmware, firmware_text);
    lv_obj_align(firmware, LV_ALIGN_TOP_RIGHT, -36, 30);

    /* Nội dung trong list scroll DỌC (root vẫn không scroll; list khóa trục
       dọc theo §4.2). Trên 320×240 vừa hết, màn thấp hơn thì cuộn. */
    lv_obj_t *content = lv_obj_create(parent);
    controls = content;
    lv_obj_set_size(content, w, h - 52);
    lv_obj_set_pos(content, 0, 52);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(content, 0, 0);
    lv_obj_set_style_pad_all(content, 0, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);

    int y = 4;
    int row_h = 48;

    /* 1. Độ sáng — slider ngoài dải edge. */
    row_label(content, "Độ sáng", y + 5);
    bright_slider = lv_slider_create(content);
    lv_obj_set_size(bright_slider, w - 2 * EDGE - 60, 10);
    lv_obj_set_pos(bright_slider, EDGE, y + 32);
    lv_slider_set_range(bright_slider, 10, 100);
    lv_obj_add_event_cb(bright_slider, bright_changed, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(bright_slider, bright_released, LV_EVENT_RELEASED, NULL);
    bright_value = row_label(content, "80%", y + 5);
    lv_obj_set_pos(bright_value, w - EDGE - 46, y + 5);
    y += 64;

    /* 2. Giao diện */
    row_label(content, "Giao diện", y + 5);
    theme_btn = value_button(content, theme_label(), w - EDGE - 130, y, 110, cycle_theme);
    y += row_h;

    /* 3. Bố cục */
    row_label(content, "Bố cục", y + 5);
    layout_btn = value_button(content, layout_label(), w - EDGE - 130, y, 110, cycle_layout);
    y += row_h;

    /* 4. Cảnh báo */
    row_label(content, "Cảnh báo", y + 5);
    alerts_btn = value_button(content, alerts_label(), w - EDGE - 130, y, 110, toggle_alerts);
    y += row_h;

    /* 5. Ngưỡng cảnh báo — bước 100, hiện giá trị nguyên không làm tròn. */
    row_label(content, "Ngưỡng cảnh báo", y);
    y += 24;
    small_button(content, "-100", EDGE, y + 2, 52, step_alert_dist, (intptr_t)-100);
    dist_value = row_label(content, "800 m", y + 5);
    lv_obj_set_style_text_color(dist_value, lv_color_hex(0x2ECC71), 0);
    lv_obj_set_width(dist_value, 70);
    lv_obj_set_style_text_align(dist_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(dist_value, w - EDGE - 122, y + 5);
    small_button(content, "+100", w - EDGE - 50, y + 2, 50, step_alert_dist, (intptr_t)100);
    y += row_h;

    /* 6. Tắt màn */
    row_label(content, "Tắt màn", y + 5);
    y += 24;
    small_button(content, "-1", EDGE, y + 2, 52, step_dim_idle, (intptr_t)-1);
    dim_value = row_label(content, "5 ph", y + 5);
    lv_obj_set_style_text_color(dim_value, lv_color_hex(0x2ECC71), 0);
    lv_obj_set_width(dim_value, 70);
    lv_obj_set_style_text_align(dim_value, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(dim_value, w - EDGE - 122, y + 5);
    small_button(content, "+1", w - EDGE - 50, y + 2, 50, step_dim_idle, (intptr_t)1);
    y += row_h + 4;

    /* Hiệu chỉnh cảm ứng + dòng kết quả lưu. */
    small_button(content, "Hiệu chỉnh cảm ứng", w - EDGE - 170, y, 170,
                 start_calibration, 0);
    save_status = row_label(content, "", y + 46);
    y += 80;

    /* Cập nhật firmware qua Internet (cần WiFi đã nối). */
    row_label(content, "Firmware", y + 10);
    update_btn = small_button(content, update_button_text, w - EDGE - 170, y, 170,
                              update_clicked, 0);
    update_status = row_label(content, "", y + 46);
    lv_obj_set_width(update_status, w - 2 * EDGE);
    lv_label_set_long_mode(update_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_color(update_status, lv_color_hex(0x9CA3AF), 0);
    refresh_update();

    /* Hiện giá trị từ snapshot mới nhất. */
    ui_settings_set_snapshot(&snapshot);
    if (save_status) lv_label_set_text(save_status, "");
    refresh_pending();
}

void ui_settings_page_exit(void)
{
    controls = NULL;
    bright_slider = NULL;
    bright_value = NULL;
    theme_btn = NULL;
    layout_btn = NULL;
    alerts_btn = NULL;
    dist_value = NULL;
    dim_value = NULL;
    save_status = NULL;
    update_btn = NULL;
    update_status = NULL;
}
