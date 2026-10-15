#include "wifi_ap_config.h"
#include "wifi_manager.h"

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdlib.h>
#include <string.h>

static const char *TAG = "wifi_ap_cfg";
static httpd_handle_t server;
static wifi_cfg_progress_cb_t progress;
static char hint[33];

static void report(wifi_cfg_status_t s)
{
    if (progress) progress(s);
}

static void url_decode(char *s)
{
    char *o = s;
    while (*s) {
        if (*s == '+') { *o++ = ' '; s++; }
        else if (*s == '%' && s[1] && s[2]) {
            char h[3] = {s[1], s[2], 0};
            *o++ = (char)strtol(h, NULL, 16);
            s += 3;
        } else *o++ = *s++;
    }
    *o = 0;
}

static bool form_get(const char *body, const char *key, char *out, size_t out_size)
{
    size_t klen = strlen(key);
    const char *p = body;
    while (p && *p) {
        if (!strncmp(p, key, klen) && p[klen] == '=') {
            p += klen + 1;
            const char *amp = strchr(p, '&');
            size_t vlen = amp ? (size_t)(amp - p) : strlen(p);
            if (vlen >= out_size) vlen = out_size - 1;
            memcpy(out, p, vlen);
            out[vlen] = 0;
            url_decode(out);
            return true;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    return false;
}

static esp_err_t root_get(httpd_req_t *req)
{
    char page[1024];
    snprintf(page, sizeof(page),
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>WazeHUD WiFi</title>"
        "<style>body{font-family:sans-serif;background:#0b0e14;color:#fff;padding:24px}"
        "input{width:100%%;box-sizing:border-box;padding:12px;margin:8px 0;border-radius:8px;"
        "border:1px solid #333;background:#161b26;color:#fff;font-size:16px}"
        "button{width:100%%;padding:14px;border-radius:8px;border:0;background:#198754;"
        "color:#fff;font-size:16px;margin-top:12px}</style></head><body>"
        "<h2>Kết nối WiFi cho HUD</h2>"
        "<form method='POST' action='/save'>"
        "<label>Tên WiFi (SSID)</label>"
        "<input name='ssid' value='%s' required autocapitalize='off' autocorrect='off'>"
        "<label>Mật khẩu</label>"
        "<input name='password' type='password'>"
        "<button type='submit'>Lưu và khởi động lại</button>"
        "</form></body></html>",
        hint);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, page, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t save_post(httpd_req_t *req)
{
    char body[256] = {0};
    int len = req->content_len < (int)sizeof(body) - 1 ? (int)req->content_len
                                                        : (int)sizeof(body) - 1;
    int received = len > 0 ? httpd_req_recv(req, body, len) : 0;
    if (received <= 0) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    body[received] = 0;

    char ssid[33] = {0}, password[65] = {0};
    form_get(body, "ssid", ssid, sizeof(ssid));
    form_get(body, "password", password, sizeof(password));

    static const char *OK_PAGE =
        "<!doctype html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Đã lưu</title>"
        "<style>body{font-family:sans-serif;background:#0b0e14;color:#fff;padding:24px}"
        "</style></head><body><h2>Đã lưu — HUD đang khởi động lại…</h2>"
        "<p>Có thể tắt WiFi này trên điện thoại và quay lại xem HUD.</p></body></html>";
    static const char *ERR_PAGE =
        "<!doctype html><html><head><meta charset='utf-8'></head>"
        "<body><h2>Thiếu tên WiFi (SSID)</h2></body></html>";

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    esp_err_t err = httpd_resp_send(req, ssid[0] ? OK_PAGE : ERR_PAGE, HTTPD_RESP_USE_STRLEN);

    if (ssid[0]) {
        wifi_manager_save_credentials(ssid, password);
        report(WIFI_CFG_SAVED);
        vTaskDelay(pdMS_TO_TICKS(800)); /* để kịp gửi xong trang trả lời trước khi mất mạng */
        esp_restart();
    }
    return err;
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base != WIFI_EVENT) return;
    if (id == WIFI_EVENT_AP_STACONNECTED) report(WIFI_CFG_CLIENT);
}

void wifi_ap_config_start(const char *ssid_hint, wifi_cfg_progress_cb_t cb)
{
    progress = cb;
    snprintf(hint, sizeof(hint), "%s", ssid_hint ? ssid_hint : "");

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_init lỗi: %s", esp_err_to_name(err));
        return;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));

    wifi_config_t config = {
        .ap = {
            .ssid = WIFI_CFG_AP_SSID,
            .ssid_len = sizeof(WIFI_CFG_AP_SSID) - 1,
            .password = WIFI_CFG_AP_PASS,
            .max_connection = 2,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
    /* Board này chạy BLE (NimBLE) + LVGL + HUD song song với WiFi. Nếu heap
       nội bộ (DMA-capable) đã cạn tới lúc này, driver WiFi cấp phát buffer
       beacon thất bại và deref NULL → Guru Meditation LoadProhibited (đã
       thấy thật trên máy: "alloc eb len=752 type=4 fail" ngay trước khi
       crash). Log ra để biết chắc có phải do đây không trước khi sửa. */
    ESP_LOGI(TAG, "heap free: total=%u internal=%u trước esp_wifi_start(AP)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.lru_purge_enable = true;
    ESP_ERROR_CHECK(httpd_start(&server, &http));

    static const httpd_uri_t root = {.uri = "/", .method = HTTP_GET, .handler = root_get};
    static const httpd_uri_t save = {.uri = "/save", .method = HTTP_POST, .handler = save_post};
    httpd_register_uri_handler(server, &root);
    httpd_register_uri_handler(server, &save);

    ESP_LOGI(TAG, "SoftAP %s · http://%s", WIFI_CFG_AP_SSID, WIFI_CFG_AP_IP);
    report(WIFI_CFG_AP_READY);
}
