#include "ota_online.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ota_online";

#define MANIFEST_MAX 1024

/* "0.2.0" / "v0.2.0" → số so sánh được; thiếu phần nào coi như 0. */
static long version_key(const char *v)
{
    int major = 0, minor = 0, patch = 0;
    if (v && (*v == 'v' || *v == 'V')) v++;
    if (v) sscanf(v, "%d.%d.%d", &major, &minor, &patch);
    return (long)major * 1000000L + (long)minor * 1000L + patch;
}

esp_err_t ota_online_check(ota_online_info_t *out)
{
    memset(out, 0, sizeof(*out));
    if (!OTA_ONLINE_ENABLED) return ESP_ERR_NOT_SUPPORTED;

    esp_http_client_config_t http = {
        .url = OTA_MANIFEST_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http);
    if (!client) return ESP_ERR_NO_MEM;

    char *body = calloc(1, MANIFEST_MAX + 1);
    esp_err_t err = body ? esp_http_client_open(client, 0) : ESP_ERR_NO_MEM;
    int len = 0;
    if (err == ESP_OK) {
        esp_http_client_fetch_headers(client);
        int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            ESP_LOGW(TAG, "ota.json HTTP %d", status);
            err = ESP_ERR_INVALID_RESPONSE;
        } else {
            int n;
            while (len < MANIFEST_MAX
                   && (n = esp_http_client_read(client, body + len, MANIFEST_MAX - len)) > 0)
                len += n;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (err != ESP_OK) {
        free(body);
        return err;
    }

    cJSON *root = cJSON_ParseWithLength(body, len);
    free(body);
    const cJSON *version = cJSON_GetObjectItem(root, "version");
    const cJSON *url = cJSON_GetObjectItem(root, "url");
    const cJSON *notes = cJSON_GetObjectItem(root, "notes");
    if (!cJSON_IsString(version) || !cJSON_IsString(url)) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }
    strlcpy(out->version, version->valuestring, sizeof(out->version));
    strlcpy(out->url, url->valuestring, sizeof(out->url));
    if (cJSON_IsString(notes)) strlcpy(out->notes, notes->valuestring, sizeof(out->notes));
    cJSON_Delete(root);

    const char *running = esp_app_get_description()->version;
    out->newer = version_key(out->version) > version_key(running);
    ESP_LOGI(TAG, "đang chạy %s, server %s%s", running, out->version,
             out->newer ? " → có bản mới" : "");
    return ESP_OK;
}

esp_err_t ota_online_install(const char *url, ota_online_progress_cb_t cb)
{
    esp_http_client_config_t http = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
        .buffer_size = 4096,
    };
    esp_https_ota_config_t config = {.http_config = &http};
    esp_https_ota_handle_t handle = NULL;
    esp_err_t err = esp_https_ota_begin(&config, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "begin lỗi: %s", esp_err_to_name(err));
        return err;
    }

    /* Chặn nạp nhầm firmware của dự án khác (cùng chip vẫn ghi được). */
    esp_app_desc_t incoming;
    err = esp_https_ota_get_img_desc(handle, &incoming);
    if (err == ESP_OK
        && strcmp(incoming.project_name, esp_app_get_description()->project_name) != 0) {
        ESP_LOGE(TAG, "sai firmware: %s", incoming.project_name);
        err = ESP_ERR_INVALID_VERSION;
    }

    int last = -1;
    while (err == ESP_OK) {
        err = esp_https_ota_perform(handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        err = ESP_OK;
        int total = esp_https_ota_get_image_size(handle);
        int read = esp_https_ota_get_image_len_read(handle);
        int percent = total > 0 ? (int)((int64_t)read * 100 / total) : 0;
        if (percent != last && cb) cb(percent);
        last = percent;
    }
    if (err == ESP_OK && !esp_https_ota_is_complete_data_received(handle))
        err = ESP_ERR_INVALID_SIZE;
    if (err != ESP_OK) {
        esp_https_ota_abort(handle);
        ESP_LOGE(TAG, "tải lỗi: %s", esp_err_to_name(err));
        return err;
    }

    /* finish: kiểm tra ảnh vừa ghi rồi đặt vùng boot sang bản mới. */
    err = esp_https_ota_finish(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "finish lỗi: %s", esp_err_to_name(err));
        return err;
    }
    if (cb) cb(100);
    ESP_LOGI(TAG, "xong, khởi động lại");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}
