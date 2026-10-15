#include "trip_link.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "msg_asm.h"

#include <string.h>

static const char *TAG = "trip";
static QueueHandle_t mailbox;

/* Bộ ghép mảnh — chỉ task NimBLE đụng tới (feed + disconnect). */
static msg_asm_t assembler;
/* Hash bản tin gần nhất: bản trùng y hệt không cần parse + vẽ lại. */
static uint32_t last_hash;

void trip_link_init(void)
{
    if (!mailbox) mailbox = xQueueCreate(1, sizeof(msg_frame_t));
    msg_asm_init(&assembler);
}

static void on_frame(const msg_frame_t *frame, void *user)
{
    (void)user;
    if (!strstr(frame->bytes, "\"t\":\"trip\"")) return; /* loại tin khác: bỏ */
    uint32_t hash = msg_frame_hash(frame);
    if (hash == last_hash) return;
    last_hash = hash;
    xQueueOverwrite(mailbox, frame);
}

void trip_link_feed(uint16_t conn, const uint8_t *data, size_t length)
{
    if (!mailbox) return;
    msg_asm_feed(&assembler, conn, esp_timer_get_time(), data, length, on_frame, NULL);
}

void trip_link_disconnected(uint16_t conn)
{
    msg_asm_disconnect(&assembler, conn);
}

static trip_span_t span(const cJSON *root, const char *key)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, key);
    const cJSON *km = cJSON_GetArrayItem(arr, 0), *mins = cJSON_GetArrayItem(arr, 1);
    if (!cJSON_IsArray(arr) || !cJSON_IsNumber(km)) return (trip_span_t){.km = -1, .mins = -1};
    return (trip_span_t){.km = (float)km->valuedouble,
                         .mins = cJSON_IsNumber(mins) ? mins->valueint : -1};
}

bool trip_link_take(trip_stats_t *out)
{
    static msg_frame_t raw;
    if (!mailbox || xQueueReceive(mailbox, &raw, 0) != pdTRUE) return false;
    cJSON *root = cJSON_ParseWithLength(raw.bytes, raw.length);
    if (!root) {
        ESP_LOGW(TAG, "bản tin chuyến đi hỏng (%u byte)", raw.length);
        return false;
    }
    const cJSON *m = cJSON_GetObjectItemCaseSensitive(root, "m");
    const cJSON *lm = cJSON_GetObjectItemCaseSensitive(root, "lm");
    *out = (trip_stats_t){
        .month = cJSON_IsNumber(m) ? m->valueint : 0,
        .last_month = cJSON_IsNumber(lm) ? lm->valueint : 0,
        .today = span(root, "today"),
        .yesterday = span(root, "yday"),
        .this_month = span(root, "month"),
        .prev_month = span(root, "prev"),
    };
    cJSON_Delete(root);
    ESP_LOGI(TAG, "nhận thống kê: hôm nay %.1f km, hôm qua %.1f km, tháng %.1f km, tháng trước %.1f km",
             out->today.km, out->yesterday.km, out->this_month.km, out->prev_month.km);
    return true;
}
