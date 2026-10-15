#include "ota.h"

#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <sys/param.h>  /* MIN */

static const char *TAG = "ota";

static ota_progress_cb_t progress;
static httpd_handle_t server;

static void report(ota_status_t status, int percent, const char *detail)
{
    if (progress) progress(status, percent, detail);
}

/* Trang một file, không tài nguyên ngoài — thiết bị không có Internet để tải
   thêm bất cứ thứ gì. */
static const char PAGE[] =
    "<!doctype html><meta charset=utf-8>"
    "<meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Waze HUD</title><style>"
    "body{margin:0;font:16px/1.5 -apple-system,system-ui,sans-serif;"
    "background:#111417;color:#e8e8ea;display:grid;place-items:center;"
    "min-height:100vh}"
    ".c{width:min(420px,90vw);padding:28px}"
    "h1{font-size:20px;margin:0 0 4px}p{color:#9a9aa2;margin:0 0 22px}"
    "input[type=file]{width:100%;padding:14px;border:1px dashed #3a3f45;"
    "border-radius:10px;background:#181c20;color:#e8e8ea;margin-bottom:14px}"
    "button{width:100%;padding:14px;border:0;border-radius:10px;"
    "background:#2f6fed;color:#fff;font-size:16px;font-weight:600}"
    "button:disabled{background:#33383e;color:#7a7a82}"
    "#s{margin-top:16px;height:6px;border-radius:3px;background:#22262b;"
    "overflow:hidden}#b{height:100%;width:0;background:#2f6fed;"
    "transition:width .2s}#m{margin-top:10px;color:#9a9aa2;font-size:14px}"
    "</style><div class=c><h1>Cập nhật Waze HUD</h1>"
    "<p>Chọn file <code>wazehud_s3.bin</code> rồi bấm cập nhật.</p>"
    "<input type=file id=f accept='.bin'>"
    "<button id=go disabled>Cập nhật</button>"
    "<div id=s><div id=b></div></div><div id=m></div></div>"
    "<script>"
    "const f=document.getElementById('f'),go=document.getElementById('go'),"
    "b=document.getElementById('b'),m=document.getElementById('m');"
    "f.onchange=()=>go.disabled=!f.files.length;"
    "go.onclick=()=>{const x=new XMLHttpRequest();"
    "go.disabled=f.disabled=true;m.textContent='Đang tải lên…';"
    "x.upload.onprogress=e=>{if(e.lengthComputable){"
    "const p=Math.round(e.loaded/e.total*100);b.style.width=p+'%';"
    "m.textContent='Đang tải lên… '+p+'%'}};"
    "x.onload=()=>{m.textContent=x.status==200"
    "?'Xong. Thiết bị đang khởi động lại.':'Lỗi: '+x.responseText};"
    "x.onerror=()=>m.textContent='Mất kết nối';"
    "x.open('POST','/update');x.send(f.files[0])};"
    "</script>";

static esp_err_t page_handler(httpd_req_t *req)
{
    report(OTA_CLIENT, 0, NULL);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t update_handler(httpd_req_t *req)
{
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    if (!target) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "khong tim thay phan vung OTA");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "ghi vào phân vùng %s (%" PRIu32 " byte)", target->label,
             target->size);

    esp_ota_handle_t handle = 0;
    if (esp_ota_begin(target, OTA_WITH_SEQUENTIAL_WRITES, &handle) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin loi");
        report(OTA_FAILED, 0, "esp_ota_begin");
        return ESP_FAIL;
    }

    char buf[2048];
    int remaining = req->content_len;
    int total = remaining;
    int written = 0;

    while (remaining > 0) {
        int chunk = httpd_req_recv(req, buf, MIN((size_t)remaining, sizeof(buf)));
        if (chunk == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (chunk <= 0) {
            esp_ota_abort(handle);
            report(OTA_FAILED, 0, "mất kết nối");
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "mat ket noi");
            return ESP_FAIL;
        }
        if (esp_ota_write(handle, buf, chunk) != ESP_OK) {
            esp_ota_abort(handle);
            report(OTA_FAILED, 0, "ghi flash lỗi");
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "ghi flash loi");
            return ESP_FAIL;
        }
        remaining -= chunk;
        written += chunk;
        report(OTA_RECEIVING, total ? written * 100 / total : 0, NULL);
    }

    /* esp_ota_end kiểm tra tính hợp lệ của ảnh vừa ghi (và cả chữ ký,
       nếu bật secure boot). */
    if (esp_ota_end(handle) != ESP_OK) {
        report(OTA_FAILED, 0, "ảnh firmware hỏng");
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "anh firmware hong");
        return ESP_FAIL;
    }
    if (esp_ota_set_boot_partition(target) != ESP_OK) {
        report(OTA_FAILED, 0, "đặt boot lỗi");
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "dat boot loi");
        return ESP_FAIL;
    }

    httpd_resp_sendstr(req, "ok");
    report(OTA_DONE, 100, NULL);
    ESP_LOGI(TAG, "xong, khởi động lại sau 1 giây");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base != WIFI_EVENT) return;
    if (id == WIFI_EVENT_AP_STACONNECTED) report(OTA_CLIENT, 0, NULL);
    else if (id == WIFI_EVENT_AP_STADISCONNECTED) report(OTA_AP_READY, 0, NULL);
}

void ota_start_portal(ota_progress_cb_t cb)
{
    progress = cb;

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        report(OTA_FAILED, 0, "WiFi init");
        return;
    }
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));

    wifi_config_t config = {
        .ap = {
            .ssid = OTA_AP_SSID,
            .ssid_len = sizeof(OTA_AP_SSID) - 1,
            .password = OTA_AP_PASS,
            .max_connection = 2,
            .authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &config));
    ESP_ERROR_CHECK(esp_wifi_start());

    httpd_config_t http = HTTPD_DEFAULT_CONFIG();
    http.stack_size = 8192;
    http.lru_purge_enable = true;
    /* Firmware gần 1 MB nên upload chạy lâu; timeout mặc định quá ngắn. */
    http.recv_wait_timeout = 30;
    http.send_wait_timeout = 30;
    ESP_ERROR_CHECK(httpd_start(&server, &http));

    static const httpd_uri_t page = {
        .uri = "/", .method = HTTP_GET, .handler = page_handler};
    static const httpd_uri_t update = {
        .uri = "/update", .method = HTTP_POST, .handler = update_handler};
    httpd_register_uri_handler(server, &page);
    httpd_register_uri_handler(server, &update);

    ESP_LOGI(TAG, "SoftAP %s · http://%s", OTA_AP_SSID, OTA_AP_IP);
    report(OTA_AP_READY, 0, NULL);
}

void ota_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) return;
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return;

    esp_ota_mark_app_valid_cancel_rollback();
    ESP_LOGI(TAG, "bản firmware mới đã được xác nhận là tốt");
}
