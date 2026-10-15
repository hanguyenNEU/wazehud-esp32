#include "wifi_manager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "wifi_manager";

static wifi_manager_status_cb_t status_cb;
static wifi_manager_scan_cb_t scan_cb;
static void *callback_user;
static TaskHandle_t reconnect_task;
static int reconnect_attempt;
static bool initialized;
static bool has_credentials;
static bool stopped_for_ota;
/* Link state tách khỏi scan state (§7): scan start/done không được xóa
   connection state, và sau scan không ép IDLE nếu đang connected. */
static bool link_connected;
static bool scan_in_progress;

static void report_status(wifi_manager_status_t status)
{
    if (status_cb) status_cb(status, callback_user);
}

static void reconnect_fn(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int delay_ms = reconnect_attempt < 5 ? (1 << reconnect_attempt) * 1000 : 30000;
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        if (initialized && !stopped_for_ota) {
            report_status(WIFI_MANAGER_CONNECTING);
            esp_wifi_connect();
        }
    }
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base != WIFI_EVENT && base != IP_EVENT) return;
    if (base == IP_EVENT) {
        if (id == IP_EVENT_STA_GOT_IP) {
            reconnect_attempt = 0;
            link_connected = true;
            report_status(WIFI_MANAGER_CONNECTED);
        }
        return;
    }
    if (id == WIFI_EVENT_STA_START) {
        if (has_credentials) {
            report_status(WIFI_MANAGER_CONNECTING);
        } else report_status(WIFI_MANAGER_IDLE);
    } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *ev = data;
        ESP_LOGW(TAG, "mất kết nối: ssid=%.*s reason=%d", (int)ev->ssid_len,
                 (char *)ev->ssid, ev->reason);
        link_connected = false;
        if (stopped_for_ota) return;
        report_status(WIFI_MANAGER_DISCONNECTED);
        if (reconnect_task) {
            if (reconnect_attempt < 6) reconnect_attempt++;
            xTaskNotifyGive(reconnect_task);
        }
    } else if (id == WIFI_EVENT_SCAN_DONE) {
        uint16_t count = 0;
        /* Only the serialized default event loop uses these scan buffers.
           Keeping them off-stack also keeps STA_START within its stack budget. */
        static wifi_ap_record_t records[WIFI_MANAGER_MAX_SCAN];
        esp_wifi_scan_get_ap_num(&count);
        if (count > WIFI_MANAGER_MAX_SCAN) count = WIFI_MANAGER_MAX_SCAN;
        memset(records, 0, sizeof(records));
        esp_wifi_scan_get_ap_records(&count, records);
        static wifi_manager_scan_entry_t entries[WIFI_MANAGER_MAX_SCAN];
        size_t out = 0;
        for (uint16_t i = 0; i < count; ++i) {
            if (!records[i].ssid[0]) continue;
            bool duplicate = false;
            for (size_t j = 0; j < out; ++j)
                duplicate |= !strcmp(entries[j].ssid, (char *)records[i].ssid);
            if (duplicate) continue;
            strlcpy(entries[out].ssid, (char *)records[i].ssid, sizeof(entries[out].ssid));
            entries[out].open = records[i].authmode == WIFI_AUTH_OPEN;
            entries[out++].rssi = records[i].rssi;
        }
        ESP_LOGI(TAG, "quét xong: %u mạng", (unsigned)out);
        for (size_t i = 0; i < out; ++i)
            ESP_LOGI(TAG, "  [%u] %s (rssi %d)", (unsigned)i, entries[i].ssid, entries[i].rssi);
        scan_in_progress = false;
        /* KHÔNG report IDLE ở đây — sẽ xóa mất trạng thái connected mà
           main.c vừa hiển thị (§7). */
        if (scan_cb) scan_cb(entries, out, callback_user);
    }
}

bool wifi_manager_is_connected(void)
{
    return initialized && link_connected;
}

bool wifi_manager_scan_in_progress(void)
{
    return scan_in_progress;
}

void wifi_manager_set_callbacks(wifi_manager_status_cb_t status,
                                wifi_manager_scan_cb_t scan, void *user)
{
    status_cb = status;
    scan_cb = scan;
    callback_user = user;
}

void wifi_manager_init(void)
{
    if (initialized) return;
    stopped_for_ota = false;
    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t config = {0};
    nvs_handle_t nvs;
    int active = 0;
    if (nvs_open("wifi_ui", NVS_READONLY, &nvs) == ESP_OK) {
        uint8_t saved = 0;
        if (nvs_get_u8(nvs, "active", &saved) == ESP_OK && saved < WIFI_MANAGER_MAX_SLOTS)
            active = saved;
        char key[12];
        snprintf(key, sizeof(key), "ssid%d", active);
        size_t size = sizeof(config.sta.ssid);
        nvs_get_str(nvs, key, (char *)config.sta.ssid, &size);
        snprintf(key, sizeof(key), "pass%d", active);
        size = sizeof(config.sta.password);
        nvs_get_str(nvs, key, (char *)config.sta.password, &size);
        nvs_close(nvs);
    }
    has_credentials = config.sta.ssid[0] != '\0';
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_ERROR_CHECK(esp_wifi_start());
    initialized = true;
    xTaskCreate(reconnect_fn, "wifi_reconnect", 3072, NULL, 4, &reconnect_task);
    if (has_credentials) {
        report_status(WIFI_MANAGER_CONNECTING);
        ESP_ERROR_CHECK(esp_wifi_connect());
    }
}

void wifi_manager_save_credentials(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0]) return;
    nvs_handle_t nvs;
    if (nvs_open("wifi_ui", NVS_READWRITE, &nvs) != ESP_OK) return;
    int slot = 0;
    char key[12], current[WIFI_MANAGER_SSID_MAX + 1];
    for (; slot < WIFI_MANAGER_MAX_SLOTS; ++slot) {
        snprintf(key, sizeof(key), "ssid%d", slot);
        size_t size = sizeof(current);
        if (nvs_get_str(nvs, key, current, &size) != ESP_OK || !current[0] || !strcmp(current, ssid))
            break;
    }
    if (slot >= WIFI_MANAGER_MAX_SLOTS) slot = WIFI_MANAGER_MAX_SLOTS - 1;
    snprintf(key, sizeof(key), "ssid%d", slot); nvs_set_str(nvs, key, ssid);
    snprintf(key, sizeof(key), "pass%d", slot); nvs_set_str(nvs, key, password ? password : "");
    nvs_set_u8(nvs, "active", (uint8_t)slot);
    nvs_commit(nvs); nvs_close(nvs);
}

void wifi_manager_save_and_connect(const char *ssid, const char *password)
{
    if (!ssid || !ssid[0] || !initialized) return;
    wifi_manager_save_credentials(ssid, password);

    wifi_config_t config = {0};
    strlcpy((char *)config.sta.ssid, ssid, sizeof(config.sta.ssid));
    strlcpy((char *)config.sta.password, password ? password : "", sizeof(config.sta.password));
    esp_wifi_disconnect();
    esp_wifi_set_config(WIFI_IF_STA, &config);
    reconnect_attempt = 0;
    link_connected = false;
    report_status(WIFI_MANAGER_CONNECTING);
    esp_wifi_connect();
}

esp_err_t wifi_manager_scan(void)
{
    if (!initialized) return ESP_ERR_INVALID_STATE;
    wifi_scan_config_t config = {.show_hidden = false, .scan_type = WIFI_SCAN_TYPE_ACTIVE};
    esp_err_t err = esp_wifi_scan_start(&config, false);
    if (err != ESP_OK) {
        /* Lỗi phải kết thúc busy và GIỮ link state — không đụng status. */
        scan_in_progress = false;
        ESP_LOGW(TAG, "scan lỗi: %s", esp_err_to_name(err));
        return err;
    }
    scan_in_progress = true;
    return ESP_OK;
}

void wifi_manager_stop_for_ota(void)
{
    if (!initialized) return;
    stopped_for_ota = true;
    initialized = false;
    esp_wifi_stop();
    esp_wifi_deinit();
}
