#pragma once

#include "hud_config.h"
#include "lvgl.h"

/* Trang Cài đặt — sáu field + calibration + thông tin firmware (§6).
 *
 * Auto-save từng mục khi thao tác kết thúc (slider RELEASED, nút CLICKED),
 * không có draft toàn màn. Một request local outstanding; trong lúc pending
 * khóa mục đang ghi. Slider chỉ cập nhật SỐ tại UI lúc kéo — brightness thật
 * áp sau commit thành công (snapshot → main.c). CONFLICT → hiện lại giá trị
 * từ snapshot mới, không tự ghi đè. */

typedef void (*ui_settings_patch_cb_t)(const hud_config_patch_t *patch, void *user);
typedef void (*ui_settings_cal_cb_t)(void *user);
/* Bấm nút cập nhật firmware — main quyết định kiểm tra hay cài. */
typedef void (*ui_settings_update_cb_t)(void *user);

void ui_settings_set_handlers(ui_settings_patch_cb_t patch_cb,
                              ui_settings_cal_cb_t cal_cb, void *user);
void ui_settings_set_snapshot(const hud_config_snapshot_t *snap);
void ui_settings_set_result(const hud_config_result_t *result);
void ui_settings_set_firmware(const char *version);
void ui_settings_set_update_handler(ui_settings_update_cb_t cb, void *user);
/* Dòng trạng thái + chữ trên nút cập nhật; giữ lại qua lần vào/ra trang.
   busy = khoá nút trong lúc đang kiểm tra. */
void ui_settings_set_update_status(const char *status, const char *button, bool busy);

void ui_settings_page_enter(lv_obj_t *parent);
void ui_settings_page_exit(void);
