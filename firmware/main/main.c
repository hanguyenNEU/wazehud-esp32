/* Waze HUD — ESP32-S3 ES3C28P
 *
 * Luồng: BLE (NimBLE) → ghép frame HLP → decode → hud_state_t → LVGL → panel.
 * Cấu hình đi ngược lại: app Waze Mod → cfg_* → NVS → snapshot → UI áp ở tick.
 *
 * Kiến trúc UI mới (§3 kế hoạch): một LVGL screen, 5 root container; router
 * sở hữu màn hình/mode; mọi dữ liệu ngoài LVGL task đi qua mailbox/queue.
 */

#include "display.h"
#include "hlp_ble.h"
#include "hud_config.h"
#include "hud_decode.h"
#include "ota.h"
#include "ota_online.h"
#include "wifi_manager.h"
#include "wifi_ap_config.h"
#include "gps_link.h"
#include "bg_store.h"
#include "nvs.h"
#include "ntp_sync.h"
#include "hud_clock.h"
#include "screen_power.h"
#include "ui_navigator.h"
#include "ui_system.h"
#include "ui_wifi.h"
#include "ui_clock.h"
#include "ui_input.h"
#include "ui_router.h"
#include "ui_settings.h"
#include "ui_trip.h"
#include "trip_link.h"
#include "ui_wifi.h"
#include "touch_es3c28p.h"

#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "wazehud";

/* Cờ "vào chế độ cấu hình WiFi qua điện thoại" truyền qua esp_restart() —
   để trong RTC memory (không mất khi soft-reset, không chạm NVS/flash) thay
   vì NVS: một lần thử dùng NVS cho việc này (đọc cờ ngay đầu app_main, dù
   luôn là false trên boot bình thường) đã làm BLE advertising luôn thất bại
   với rc=519 "Memory Capacity Exceeded" — xác nhận thật bằng A/B trên máy,
   không rõ cơ chế chính xác nhưng bỏ hẳn early NVS access là hết. RTC memory
   không đụng flash nên an toàn hơn cho việc này. */
#define WIFI_CFG_MAGIC 0xC0FFEE01u
RTC_NOINIT_ATTR static uint32_t wifi_cfg_magic;
RTC_NOINIT_ATTR static char wifi_cfg_hint[33];

#define LVGL_LOCK_MS 100
#define STALE_MS     3000 /* §5: quá 3 giây không message ⇒ mất tín hiệu */
#define BTN_GPIO     14   /* nút KEY của board; GPIO0 để dành cho chế độ nạp */
#define BTN_HOLD_MS  3000
#define MARK_VALID_MS 30000 /* chạy ổn định bấy nhiêu mới xác nhận bản mới */

typedef struct {
    hud_state_t state;
    int64_t received_us;
    uint32_t generation;
} state_update_t;

static QueueHandle_t state_updates;
static uint32_t ui_generation;
static int64_t last_state_us;
static int64_t last_nav_us;
static int64_t last_activity_us;
static int64_t boot_us;
static bool marked_valid;
static bool ota_mode;
static int64_t btn_down_us;
static gps_fix_t last_gps;
static gps_road_t last_road;
#define ROAD_FRESH_MS 12000 /* app nhắc lại mỗi 5 s */

/* ---- Wi-Fi: LINK và SCAN tách biệt, hai mailbox riêng (§7) ---- */

typedef struct {
    wifi_manager_status_t status;
} wifi_link_update_t;
static QueueHandle_t wifi_link_updates;

typedef struct {
    size_t count;
    wifi_manager_scan_entry_t entries[WIFI_MANAGER_MAX_SCAN];
} wifi_scan_update_t;
static QueueHandle_t wifi_scan_updates;

static void on_wifi_status(wifi_manager_status_t status, void *user)
{
    (void)user;
    /* Có IP là có Internet → lấy giờ NTP, đồng hồ đúng cả khi chưa nối Waze. */
    if (status == WIFI_MANAGER_CONNECTED) ntp_sync_on_connected();
    wifi_link_update_t update = {.status = status};
    xQueueOverwrite(wifi_link_updates, &update);
}

static void on_wifi_scan(const wifi_manager_scan_entry_t *entries, size_t count, void *user)
{
    (void)user;
    wifi_scan_update_t update = {.count = count > WIFI_MANAGER_MAX_SCAN
                                          ? WIFI_MANAGER_MAX_SCAN : count};
    if (update.count) memcpy(update.entries, entries, update.count * sizeof(*entries));
    xQueueOverwrite(wifi_scan_updates, &update);
}

static const char *wifi_status_text(wifi_manager_status_t status)
{
    switch (status) {
    case WIFI_MANAGER_CONNECTING: return "Đang kết nối...";
    case WIFI_MANAGER_CONNECTED: return "Đã kết nối";
    case WIFI_MANAGER_DISCONNECTED: return "Mất kết nối, sẽ thử lại";
    case WIFI_MANAGER_ERROR: return "Lỗi WiFi";
    default: return "Chưa kết nối";
    }
}

static void on_wifi_save(const char *ssid, const char *password, void *user)
{
    (void)user;
    wifi_manager_save_and_connect(ssid, password);
}

static void on_wifi_scan_request(void *user)
{
    (void)user;
    wifi_manager_scan();
}

/* Bàn phím ảo trên cảm ứng nhỏ 320x240 không bật lên đáng tin cậy (xem lịch
   sử debug trong ui/touch_es3c28p.c) — thay vì gõ mật khẩu WiFi trên màn,
   phát 1 SoftAP riêng + trang HTML để gõ bằng bàn phím thật của điện thoại.
   LƯU cờ vào RTC memory rồi esp_restart() — sau khi khởi động lại, app_main()
   rẽ hẳn sang wifi_cfg_boot_mode() TRƯỚC KHI khởi động BLE/wifi_manager. */
static void on_wifi_phone(const char *ssid_hint, void *user)
{
    (void)user;
    snprintf(wifi_cfg_hint, sizeof(wifi_cfg_hint), "%s", ssid_hint ? ssid_hint : "");
    wifi_cfg_magic = WIFI_CFG_MAGIC;
    esp_restart();
}

static void ota_task(void *arg);

/* ---- Cập nhật firmware qua Internet (Cài đặt › Kiểm tra cập nhật) ----
   Kiểm tra chạy ở task riêng (HTTPS chặn vài giây), kết quả về LVGL task qua
   mailbox như WiFi. Có bản mới thì nút đổi thành "Cập nhật vX", bấm lần nữa
   mới tải — vào UI_MODE_OTA giống chế độ cập nhật qua SoftAP. */
typedef struct {
    esp_err_t err;
    ota_online_info_t info;
} ota_check_update_t;
static QueueHandle_t ota_check_updates;
static ota_online_info_t ota_available;
static bool ota_has_update;

static void ota_check_task(void *arg)
{
    (void)arg;
    ota_check_update_t update;
    update.err = ota_online_check(&update.info);
    xQueueOverwrite(ota_check_updates, &update);
    vTaskDelete(NULL);
}

static void on_ota_online_progress(int percent)
{
    char l1[48], l2[40];
    snprintf(l1, sizeof(l1), "Đang tải bản v%s", ota_available.version);
    if (percent >= 100) snprintf(l2, sizeof(l2), "Xong, đang khởi động lại");
    else snprintf(l2, sizeof(l2), "%d%%", percent);
    if (lvgl_port_lock(LVGL_LOCK_MS)) {
        ui_show_ota(l1, l2, percent);
        lvgl_port_unlock();
    }
}

static void ota_online_task(void *arg)
{
    (void)arg;
    on_ota_online_progress(0);
    esp_err_t err = ota_online_install(ota_available.url, on_ota_online_progress);
    /* Trả về là lỗi; bản đang chạy vẫn nguyên. Khởi động lại để thoát
       UI_MODE_OTA (chế độ này không có đường quay lại). */
    if (lvgl_port_lock(LVGL_LOCK_MS)) {
        ui_show_ota("Cập nhật thất bại", esp_err_to_name(err), -1);
        lvgl_port_unlock();
    }
    vTaskDelay(pdMS_TO_TICKS(4000));
    esp_restart();
}

/* Gọi trong LVGL task (sự kiện bấm nút trên trang Cài đặt). */
static void on_update_request(void *user)
{
    (void)user;
    if (ota_has_update) {
        ota_mode = true;
        ui_router_set_mode(UI_MODE_OTA);
        screen_power_set_mode_min(40);
        if (xTaskCreate(ota_online_task, "ota_online", 8192, NULL, 4, NULL) != pdPASS) {
            ESP_LOGE(TAG, "không tạo được ota_online task");
            ota_mode = false;
            screen_power_set_mode_min(0);
            ui_router_set_mode(UI_MODE_NORMAL);
            ui_settings_set_update_status("Không đủ bộ nhớ, thử lại", NULL, false);
        }
        return;
    }
    if (!OTA_ONLINE_ENABLED) {
        ui_settings_set_update_status("Bản build này chưa cấu hình server cập nhật.",
                                      "Kiểm tra cập nhật", false);
        return;
    }
    if (!wifi_manager_is_connected()) {
        ui_settings_set_update_status("Chưa có WiFi. Vào Menu > WiFi để kết nối.",
                                      "Kiểm tra cập nhật", false);
        return;
    }
    ui_settings_set_update_status("Đang kiểm tra...", "Đang kiểm tra...", true);
    if (xTaskCreate(ota_check_task, "ota_check", 8192, NULL, 4, NULL) != pdPASS)
        ui_settings_set_update_status("Không đủ bộ nhớ, thử lại", "Kiểm tra cập nhật", false);
}

static void apply_ota_check(const ota_check_update_t *check)
{
    char line[64], button[32];
    if (check->err != ESP_OK) {
        ui_settings_set_update_status("Không kiểm tra được, thử lại sau",
                                      "Kiểm tra cập nhật", false);
    } else if (check->info.newer) {
        ota_available = check->info;
        ota_has_update = true;
        snprintf(line, sizeof(line), "Có bản mới v%s", check->info.version);
        snprintf(button, sizeof(button), "Cập nhật v%s", check->info.version);
        ui_settings_set_update_status(line, button, false);
    } else {
        snprintf(line, sizeof(line), "Đã là bản mới nhất (v%s)",
                 esp_app_get_description()->version);
        ui_settings_set_update_status(line, "Kiểm tra lại", false);
    }
}

/* ---- Ảnh nền màn Đồng hồ: mặc định (nhúng) ⇄ ảnh tải lên từ app ----
   Lựa chọn lưu NVS; ảnh tải lên đọc từ phân vùng storage vào PSRAM 1 lần. */
static uint16_t *custom_pixels;
static bool custom_ready;
static uint8_t bg_choice; /* 0 = mặc định, 1 = ảnh tải lên */
static anim_bg_t custom_bg = {.w = BG_W, .h = BG_H, .frame_count = 1, .frame_ms = 0};

static void bg_choice_save(void)
{
    nvs_handle_t h;
    if (nvs_open("ui", NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_u8(h, "bg", bg_choice);
    nvs_commit(h);
    nvs_close(h);
}

static void bg_load_custom(void)
{
    custom_ready = false;
    if (!bg_store_has_custom()) return;
    if (!custom_pixels)
        custom_pixels = heap_caps_malloc(BG_W * BG_H * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!custom_pixels) return;
    custom_ready = bg_store_load(custom_pixels);
    custom_bg.base = custom_pixels;
}

/* Gọi trong LVGL context. */
static void bg_apply(void)
{
    ui_clock_set_background(bg_choice == 1 && custom_ready ? &custom_bg : NULL);
}

/* Vuốt dọc trên màn Đồng hồ: đổi qua lại 2 ảnh nền. */
static void on_vswipe(bool upward, void *user)
{
    (void)upward;
    (void)user;
    if (ui_router_screen() != UI_SCREEN_CLOCK) return;
    if (!custom_ready) {
        ui_clock_show_hint("Chưa có ảnh nền — chọn trong Waze › Cài đặt HUD");
        return;
    }
    bg_choice = bg_choice == 1 ? 0 : 1;
    bg_choice_save();
    bg_apply();
    ui_clock_show_hint(bg_choice == 1 ? "Ảnh của bạn" : "Ảnh mặc định");
}

/* Keep protocol reception independent of LCD rendering. */
static void on_state(const char *line, size_t length, uint32_t generation, void *user)
{
    (void)user;
    state_update_t update = {.generation = generation};
    if (!hud_decode_state(line, length, &update.state)) return;
    update.received_us = esp_timer_get_time();
    xQueueOverwrite(state_updates, &update);
}

static void on_clock(int64_t unix_seconds, int tz_minutes)
{
    hud_clock_sync(unix_seconds, tz_minutes);
}

/* ---- Cấu hình: snapshot/result từ hud_config → UI áp ở tick ---- */

static void on_settings_patch(const hud_config_patch_t *patch, void *user)
{
    (void)user;
    hud_config_local_patch(patch);
}

static void on_settings_calibrate(void *user)
{
    (void)user;
    touch_es3c28p_request_recalibrate();
}

/* ---- ui_input handlers (chạy trong LVGL task) ---- */

static void on_swipe(bool rightward, void *user)
{
    (void)user;
    ui_router_swipe(rightward);
}

static void on_recovery(void *user)
{
    (void)user;
    ESP_LOGI(TAG, "giữ tay 3s -> yêu cầu hiệu chỉnh lại");
    touch_es3c28p_request_recalibrate();
}

static bool on_activity(void *user)
{
    (void)user;
    last_activity_us = esp_timer_get_time();
    /* Đang dim thì contact này chỉ để đánh thức: consume cả contact. */
    return screen_power_consume_wake_contact();
}

static void apply_config_snapshot(const hud_config_snapshot_t *snap)
{
    screen_power_apply_config(snap->value.brightness, snap->value.dim_idle);

    ui_set_theme(!strcmp(snap->value.theme, "day")     ? UI_THEME_DAY
                 : !strcmp(snap->value.theme, "night") ? UI_THEME_NIGHT
                                                       : UI_THEME_AUTO);
    ui_set_layout(!strcmp(snap->value.layout, "speed") ? UI_LAYOUT_SPEED
                                                       : UI_LAYOUT_WAZE);
    ui_set_alerts(snap->value.alerts, snap->value.alert_dist);

    ESP_LOGI(TAG, "cấu hình: sáng %d%%, theme %s, bố cục %s, cảnh báo %d/%dm, tắt sau %d phút",
             snap->value.brightness, snap->value.theme, snap->value.layout,
             snap->value.alerts, snap->value.alert_dist, snap->value.dim_idle);
}

/* Timer của LVGL: đã chạy sẵn trong LVGL task nên không cần tự khoá. */
static void tick_cb(lv_timer_t *timer)
{
    (void)timer;
    int64_t now = esp_timer_get_time();

    /* Xác nhận bản firmware mới là tốt sau khi đã chạy ổn định một lúc. Gọi
       ngay lúc khởi động thì bản hỏng cũng được coi là tốt, mất luôn khả năng
       tự lùi bản. */
    if (!marked_valid && (now - boot_us) / 1000 > MARK_VALID_MS) {
        marked_valid = true;
        ota_mark_valid();
    }

    /* Giữ nút KEY 3 giây để vào chế độ cập nhật. Không quay lại được, nên phải
       giữ đủ lâu để không bấm nhầm khi đang lái. */
    if (!ota_mode && ui_router_mode() == UI_MODE_NORMAL) {
        bool pressed = gpio_get_level(BTN_GPIO) == 0;
        if (!pressed) {
            btn_down_us = 0;
        } else if (btn_down_us == 0) {
            btn_down_us = now;
        } else if ((now - btn_down_us) / 1000 > BTN_HOLD_MS) {
            ota_mode = true;
            ui_router_set_mode(UI_MODE_OTA);
            /* Màn OTA phải sáng tối thiểu 40% suốt quá trình — không dim. */
            screen_power_set_mode_min(40);
            if (xTaskCreate(ota_task, "ota", 4096, NULL, 4, NULL) != pdPASS) {
                /* Không kẹt ota_mode khi tạo task lỗi — trả mode thường. */
                ESP_LOGE(TAG, "không tạo được ota task");
                ota_mode = false;
                screen_power_set_mode_min(0);
                ui_router_set_mode(UI_MODE_NORMAL);
            }
        }
    }

    /* Đồng hồ: áp sync pending (mailbox từ protocol task) rồi đẩy cho UI. */
    hud_clock_poll();
    struct tm local_tm;
    if (hud_clock_get_local(&local_tm)) {
        ui_set_local_hour(local_tm.tm_hour);
        ui_set_local_datetime(local_tm.tm_hour, local_tm.tm_min, local_tm.tm_wday,
                              local_tm.tm_mday, local_tm.tm_mon + 1,
                              local_tm.tm_year + 1900);
        ui_clock_set_time(hud_clock_unix_now(), hud_clock_tz_minutes(), true);
    } else {
        ui_set_local_hour(-1);
        ui_set_local_datetime(-1, -1, -1, -1, -1, -1);
        ui_clock_set_time(0, 0, false);
    }

    /* Cấu hình: snapshot → áp + đẩy cho Settings page; result → Settings. */
    hud_config_snapshot_t snap;
    if (hud_config_take_snapshot(&snap)) {
        apply_config_snapshot(&snap);
        ui_settings_set_snapshot(&snap);
    }
    hud_config_result_t result;
    if (hud_config_take_result(&result)) {
        ui_settings_set_result(&result);
    }
    /* Thống kê chuyến đi từ tweak (kênh tin 8a7e0012). */
    trip_stats_t trip;
    if (trip_link_take(&trip)) ui_trip_set(&trip);
    ota_check_update_t ota_check;
    if (xQueueReceive(ota_check_updates, &ota_check, 0) == pdTRUE) apply_ota_check(&ota_check);

    /* Wi-Fi: link và scan riêng rẽ — scan không xoá trạng thái liên kết. */
    wifi_link_update_t link_update;
    if (xQueueReceive(wifi_link_updates, &link_update, 0) == pdTRUE) {
        ui_wifi_set_status(wifi_status_text(link_update.status));
        ui_set_wifi_link(link_update.status == WIFI_MANAGER_CONNECTED ? UI_WIFI_CONNECTED
                         : link_update.status == WIFI_MANAGER_CONNECTING   ? UI_WIFI_CONNECTING
                         : link_update.status == WIFI_MANAGER_DISCONNECTED
                           || link_update.status == WIFI_MANAGER_ERROR     ? UI_WIFI_ERROR
                                                                           : UI_WIFI_OFF);
    }
    ui_wifi_set_scan_status(wifi_manager_scan_in_progress() ? "Đang quét..." : "");
    wifi_scan_update_t scan_update;
    if (xQueueReceive(wifi_scan_updates, &scan_update, 0) == pdTRUE) {
        ui_wifi_scan_entry_t entries[WIFI_MANAGER_MAX_SCAN];
        for (size_t i = 0; i < scan_update.count; ++i) {
            memcpy(entries[i].ssid, scan_update.entries[i].ssid, sizeof(entries[i].ssid));
            entries[i].rssi = scan_update.entries[i].rssi;
            entries[i].open = scan_update.entries[i].open;
        }
        ui_wifi_set_scan_list(entries, scan_update.count);
    }

    /* Navigation state. */
    uint32_t generation = hlp_ble_generation();
    if (ui_generation != generation) {
        ui_generation = generation;
        last_state_us = 0;
        hud_state_t empty;
        hud_state_reset(&empty);
        ui_update(&empty);
    }
    state_update_t update;
    if (xQueueReceive(state_updates, &update, 0) == pdTRUE
        && update.generation == generation && hlp_ble_connected()) {
        last_state_us = update.received_us;
        if (update.state.nav) last_nav_us = last_state_us;
        ui_update(&update.state);
    }

    /* Ảnh nền mới vừa lưu xong (từ app) → dùng luôn. */
    if (bg_store_take_updated()) {
        bg_load_custom();
        if (custom_ready) {
            bg_choice = 1;
            bg_choice_save();
            bg_apply();
            ui_clock_show_hint("Đã cập nhật ảnh nền");
        }
    }

    /* GPS từ app CarDash (iPhone chính) — kèm giờ thật, đồng bộ luôn đồng hồ. */
    gps_fix_t fix;
    if (gps_link_take(&fix)) {
        last_gps = fix;
        if (fix.unix_s > 0) hud_clock_sync(fix.unix_s, fix.tz_min);
    }
    bool gps_fresh = last_gps.received_us != 0
                     && (now - last_gps.received_us) / 1000 <= STALE_MS;
    gps_road_t road;
    if (gps_link_take_road(&road)) {
        if (road.lim != last_road.lim || road.alr != last_road.alr
            || strcmp(road.st, last_road.st))
            ESP_LOGI(TAG, "bộ nhớ đường từ app: lim=%d alr=%d st=%s", road.lim, road.alr, road.st);
        last_road = road;
    }
    bool road_fresh = gps_fresh && last_road.received_us != 0
                      && (now - last_road.received_us) / 1000 <= ROAD_FRESH_MS;

    /* Chẩn đoán nguồn dữ liệu dự phòng — 10 s/lần, đọc qua cổng USB. */
    static int64_t last_diag_us;
    if (now - last_diag_us > 10000000) {
        last_diag_us = now;
        ESP_LOGI(TAG, "nguồn: waze=%s gps=%llds road=%llds lim=%d st=%s",
                 hlp_ble_connected() ? "nối" : "không",
                 last_gps.received_us ? (long long)((now - last_gps.received_us) / 1000000) : -1LL,
                 last_road.received_us ? (long long)((now - last_road.received_us) / 1000000) : -1LL,
                 last_road.lim, last_road.st);
    }

    ui_link_t link;
    if (!hlp_ble_connected()) link = UI_LINK_ADVERTISING;
    else if (last_state_us == 0) link = UI_LINK_CONNECTED;
    else if ((now - last_state_us) / 1000 > STALE_MS) link = UI_LINK_STALE;
    else link = UI_LINK_LIVE;

    /* Waze không live (chưa mở tweak / mất tín hiệu): thay màn chờ bằng dữ
       liệu dự phòng từ app CarDash — tốc độ GPS, lim/tên đường từ bộ nhớ
       đường. Waze live lại thì frame của nó thắng ngay. */
    if (link != UI_LINK_LIVE) {
        if (gps_fresh) {
            hud_state_t s;
            hud_state_reset(&s);
            s.nav = true;
            if (last_gps.speed_kmh >= 0) s.speed = (int)(last_gps.speed_kmh + 0.5f);
            if (road_fresh) {
                s.limit = last_road.lim;
                snprintf(s.street, sizeof(s.street), "%s", last_road.st);
                if (last_road.alr > 0) {
                    s.alert = last_road.alr;
                    s.alerts[0].kind = last_road.alr;
                    s.alerts[0].distance = -1;
                    s.alert_count = 1;
                    s.alerts_present = true;
                }
            }
            s.over = s.limit > 0 && s.speed > s.limit;
            ui_update(&s);
            link = UI_LINK_GPS;
            last_nav_us = now; /* đang lái — không tự tối màn */
        }
    }
    ui_set_link(link);

    /* Dim: mốc idle = max(lần nav cuối, tương tác người dùng cuối) (§7).
       Touch wake đã xử lý trong on_activity (consume contact + interval mới). */
    screen_power_set_mode_min(ota_mode || ui_router_mode() == UI_MODE_CALIBRATION ? 40 : 0);
    screen_power_tick(now, last_nav_us, last_activity_us);
}

static void on_ota_progress(ota_status_t status, int percent, const char *detail)
{
    char line[64];
    const char *l1 = "WiFi: " OTA_AP_SSID;
    const char *l2;
    switch (status) {
    case OTA_AP_READY:  l2 = "Mở http://" OTA_AP_IP; break;
    case OTA_CLIENT:    l2 = "Đã có thiết bị nối vào"; break;
    case OTA_RECEIVING: snprintf(line, sizeof(line), "Đang nhận… %d%%", percent);
                        l2 = line; break;
    case OTA_DONE:      l2 = "Xong, đang khởi động lại"; break;
    case OTA_FAILED:    l2 = detail ? detail : "Lỗi"; break;
    default:            l2 = ""; break;
    }
    if (lvgl_port_lock(LVGL_LOCK_MS)) {
        ui_show_ota(l1, l2, status == OTA_RECEIVING ? percent : -1);
        lvgl_port_unlock();
    }
}

static void ota_task(void *arg)
{
    (void)arg;
    /* UI tick already set the OTA brightness override before starting us. */
    on_ota_progress(OTA_IDLE, -1, NULL);
    wifi_manager_stop_for_ota();
    ota_start_portal(on_ota_progress);
    vTaskDelete(NULL);
}

static void on_wifi_cfg_progress(wifi_cfg_status_t status)
{
    const char *l1 = "WiFi: " WIFI_CFG_AP_SSID;
    const char *l2;
    switch (status) {
    case WIFI_CFG_AP_READY: l2 = "Mở http://" WIFI_CFG_AP_IP; break;
    case WIFI_CFG_CLIENT:   l2 = "Đã có điện thoại nối vào"; break;
    case WIFI_CFG_SAVED:    l2 = "Đã lưu, đang khởi động lại"; break;
    default:                l2 = "Đang bật WiFi cấu hình…"; break;
    }
    if (lvgl_port_lock(LVGL_LOCK_MS)) {
        ui_show_ota(l1, l2, -1);
        lvgl_port_unlock();
    }
}

/* Nhánh khởi động RIÊNG cho chế độ "nhập mật khẩu qua điện thoại" — chỉ dựng
   đúng display+LVGL để hiện hướng dẫn rồi bật AP, KHÔNG đụng tới
   wifi_manager_init()/hlp_ble_start()/HUD đầy đủ. Mode PROVISIONING khóa điều
   hướng — không vuốt sang NAV được (§2.2). */
static void wifi_cfg_boot_mode(const char *hint)
{
    display_init();
    display_set_backlight(80);

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = display_io(),
        .panel_handle = display_panel(),
        .buffer_size = DISPLAY_W * 40,
        .double_buffer = true,
        .hres = DISPLAY_W,
        .vres = DISPLAY_H,
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = true,
        },
        .flags = {
            .buff_dma = true,
            .swap_bytes = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_ERROR_CHECK(disp ? ESP_OK : ESP_FAIL);

    if (lvgl_port_lock(0)) {
        ui_create();
        ui_router_set_mode(UI_MODE_WIFI_PROVISIONING);
        lvgl_port_unlock();
    }
    on_wifi_cfg_progress(WIFI_CFG_STARTING);
    wifi_ap_config_start(hint, on_wifi_cfg_progress);
    /* httpd + wifi driver chạy trong task riêng của chúng; main_task có thể
       trả về bình thường như boot thường, không cần chờ ở đây. */
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    /* Cờ do on_wifi_phone() đặt trước khi esp_restart() — rẽ hẳn sang chế độ
       nhẹ chỉ bật AP+HTTP cấu hình WiFi, bỏ qua toàn bộ BLE/HUD/wifi STA để
       không cạnh tranh heap (xem chú thích ở wifi_cfg_boot_mode()). RTC
       memory không được khởi tạo lại khi soft-reset (esp_restart), nhưng
       CŨNG không xác định ở lần bật nguồn đầu tiên — luôn kiểm magic number
       thay vì coi giá trị mặc định là false. */
    if (wifi_cfg_magic == WIFI_CFG_MAGIC) {
        wifi_cfg_magic = 0;
        wifi_cfg_boot_mode(wifi_cfg_hint);
        return;
    }
    wifi_cfg_magic = 0;

    boot_us = esp_timer_get_time();
    state_updates = xQueueCreate(1, sizeof(state_update_t));
    ESP_ERROR_CHECK(state_updates ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_link_updates = xQueueCreate(1, sizeof(wifi_link_update_t));
    ESP_ERROR_CHECK(wifi_link_updates ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_scan_updates = xQueueCreate(1, sizeof(wifi_scan_update_t));
    ESP_ERROR_CHECK(wifi_scan_updates ? ESP_OK : ESP_ERR_NO_MEM);
    ota_check_updates = xQueueCreate(1, sizeof(ota_check_update_t));
    ESP_ERROR_CHECK(ota_check_updates ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_manager_set_callbacks(on_wifi_status, on_wifi_scan, NULL);
    wifi_manager_init();

    gpio_config_t button = {
        .pin_bit_mask = 1ULL << BTN_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&button));

    bg_store_init();
    {
        nvs_handle_t h;
        if (nvs_open("ui", NVS_READONLY, &h) == ESP_OK) {
            nvs_get_u8(h, "bg", &bg_choice);
            nvs_close(h);
        }
    }
    bg_load_custom();

    display_init();
    display_set_backlight(screen_power_brightness());

    const lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    ESP_ERROR_CHECK(lvgl_port_init(&port_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = display_io(),
        .panel_handle = display_panel(),
        /* Vẽ theo băng 40 dòng thay vì cả khung: 320×40×2 = 25 KB nằm gọn
           trong SRAM trong và DMA được, khỏi tốn 109 KB cho framebuffer đầy. */
        .buffer_size = DISPLAY_W * 40,
        .double_buffer = true,
        .hres = DISPLAY_W,
        .vres = DISPLAY_H,
        /* Phải khớp đúng rotation đã áp trong driver panel. esp_lvgl_port sẽ
           gọi lại swap/mirror khi add display; nếu để mặc định false, ILI9341
           quay về 240×320 trong lúc LVGL vẫn flush vùng 320×240. */
        .rotation = {
            .swap_xy = true,
            .mirror_x = true,
            .mirror_y = true,
        },
        .flags = {
            .buff_dma = true,
            /* LCDwiki/BSP ES3C28P đặt RGB565 big-endian. SPI panel IO của
               IDF 5.5 không có swap_color_bytes, nên để esp_lvgl_port swap. */
            .swap_bytes = true,
        },
    };
    lv_display_t *disp = lvgl_port_add_disp(&disp_cfg);
    ESP_ERROR_CHECK(disp ? ESP_OK : ESP_FAIL);

    if (lvgl_port_lock(0)) {
        ui_create();
        /* Boot thường bắt đầu từ CLOCK (ui_create đã dựng); KHÔNG chạy
           welcome animation — giữ code và asset cho chế độ cần về sau. */
        ui_wifi_set_callbacks(on_wifi_save, on_wifi_scan_request, NULL);
        ui_wifi_set_phone_callback(on_wifi_phone, NULL);
        ui_settings_set_handlers(on_settings_patch, on_settings_calibrate, NULL);
        ui_settings_set_firmware(esp_app_get_description()->version);
        ui_settings_set_update_handler(on_update_request, NULL);
        ui_input_set_handlers(on_swipe, on_recovery, on_activity, NULL);
        ui_input_set_vswipe_handler(on_vswipe, NULL);
        bg_apply();
        touch_es3c28p_init();
        lv_timer_create(tick_cb, 250, NULL);
        lvgl_port_unlock();
    }

    /* Phải gọi SAU khi đã unlock ở trên — hàm này tự khoá/mở lvgl_port theo
       từng bước trong lúc hiệu chỉnh (nếu cần), gọi trong lúc đang giữ khoá
       sẽ làm treo tác vụ vẽ nền và người dùng không thấy được màn hiệu
       chỉnh. */
    touch_es3c28p_calibrate_if_needed();

    hud_config_set_snapshot_cb(NULL); /* snapshot publish qua mailbox nội bộ */
    hud_config_set_clock_cb(on_clock);

    ESP_LOGI(TAG, "RAM trong: trống %u B, khối lớn nhất %u B; PSRAM trống %u B",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    hlp_ble_start(on_state, NULL);
    ESP_LOGI(TAG, "HUD sẵn sàng");
}
