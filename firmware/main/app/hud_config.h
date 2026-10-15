#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Cấu hình thiết bị, công bố lên app Waze Mod bằng message `cfg_*` (§2.6).
 *
 * File này hiện thực đúng giao diện `hlp_device_config.h` của SDK nhưng thay 5
 * field mẫu bằng thiết lập thật của sản phẩm. Bản gốc vẫn nằm ở
 * `hlp/hlp_device_config.c` để đối chiếu, chỉ không nằm trong build.
 *
 * §6 kế hoạch UI:
 *   - UI KHÔNG nhận pointer mutable active — chỉ nhận snapshot (copy).
 *   - Local patch (UI chỉnh trên màn) đi qua queue hữu hạn, protocol task xử
 *     lý tối đa 1 request mỗi vòng; queue đầy trả BUSY ngay, không block LVGL.
 *   - Conflict: expected_revision khác active → CONFLICT + snapshot mới,
 *     không tự ghi đè.
 *   - Remote staging bắt đầu ghi base_revision; commit kiểm lại base == active;
 *     lệch → ACK lỗi revision, bỏ staging.
 *   - Active/revision chỉ đổi khi persist NVS thành công.
 */

typedef struct {
    int brightness;   /* 10..100, bước 5 */
    char theme[8];    /* "auto" | "day" | "night" */
    char layout[8];   /* "speed" | "waze" */
    bool alerts;
    int alert_dist;   /* 200..2000 m */
    int dim_idle;     /* 0..30 phút, 0 = không bao giờ tắt */
} hud_config_t;

typedef struct {
    hud_config_t value;
    uint32_t revision;
} hud_config_snapshot_t;

typedef enum {
    HUD_CONFIG_FIELD_BRIGHTNESS = 0,
    HUD_CONFIG_FIELD_THEME,
    HUD_CONFIG_FIELD_LAYOUT,
    HUD_CONFIG_FIELD_ALERTS,
    HUD_CONFIG_FIELD_ALERT_DIST,
    HUD_CONFIG_FIELD_DIM_IDLE,
    HUD_CONFIG_FIELD_COUNT,
} hud_config_field_t;

typedef struct {
    hud_config_field_t field;
    hud_config_t value;         /* candidate: bản copy active với đúng field thay đổi */
    uint32_t expected_revision;
    uint32_t request_id;
} hud_config_patch_t;

typedef enum {
    HUD_CONFIG_OK = 0,
    HUD_CONFIG_CONFLICT,
    HUD_CONFIG_INVALID,
    HUD_CONFIG_STORAGE_ERROR,
    HUD_CONFIG_BUSY,
} hud_config_result_kind_t;

typedef struct {
    uint32_t request_id;
    hud_config_result_kind_t kind;
    hud_config_snapshot_t snapshot; /* snapshot mới nhất kèm theo */
} hud_config_result_t;

/* Publish snapshot sau mỗi commit thành công + một lần lúc khởi động sau khi
 * nạp NVS. Chạy trong protocol task — KHÔNG được khoá LVGL ở đây; UI áp
 * theme/brightness sau khi lấy snapshot ở tick của mình. */
typedef void (*hud_config_snapshot_cb_t)(const hud_config_snapshot_t *snap);

void hud_config_set_snapshot_cb(hud_config_snapshot_cb_t cb);

/* `hi` là nơi DUY NHẤT mang giờ thật (unix + tz theo phút); `ts` của message
 * state chỉ là uptime bên phát. Bắt ở đây vì `hi` đã được parse sẵn. */
typedef void (*hud_config_clock_cb_t)(int64_t unix_seconds, int tz_minutes);

void hud_config_set_clock_cb(hud_config_clock_cb_t cb);

/* ---- Local patch (UI task → queue → protocol task) ---- */
void hud_config_local_patch(const hud_config_patch_t *patch);
/* Gọi từ protocol task mỗi vòng lặp (sau send_dev, trước RX). */
void hud_config_process_local(void);

/* LVGL tick: lấy snapshot/result mới nhất (mailbox 1 slot, coalesce). */
bool hud_config_take_snapshot(hud_config_snapshot_t *out);
bool hud_config_take_result(hud_config_result_t *out);

/* Phần hlp_device_config_init/publish/handle khai báo ở hlp/hlp_device_config.h
 * (chữ ký SDK), hiện thực nằm trong hud_config.c. */
