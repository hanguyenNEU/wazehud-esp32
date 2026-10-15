#include "gps_link.h"

#include "cJSON.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#include <string.h>

#define GPS_FRAME_MAX 256

typedef struct {
    uint16_t length;
    int64_t received_us;
    char bytes[GPS_FRAME_MAX];
} gps_raw_t;

/* Mailbox 1 chỗ: bản tin mới đè bản cũ chưa đọc — chỉ cần vị trí mới nhất. */
static QueueHandle_t mailbox;
static QueueHandle_t road_mailbox; /* riêng — GPS 1 Hz không được đè mất bản tin đường */

void gps_link_init(void)
{
    if (!mailbox) mailbox = xQueueCreate(1, sizeof(gps_raw_t));
    if (!road_mailbox) road_mailbox = xQueueCreate(1, sizeof(gps_raw_t));
}

void gps_link_feed(const uint8_t *data, size_t length)
{
    if (!mailbox || !data || length == 0 || length >= GPS_FRAME_MAX) return;
    static gps_raw_t raw; /* chỉ task NimBLE gọi — để tĩnh cho khỏi tốn stack */
    raw.length = (uint16_t)length;
    raw.received_us = esp_timer_get_time();
    memcpy(raw.bytes, data, length);
    raw.bytes[length] = '\0';
    bool road = strstr(raw.bytes, "\"t\":\"road\"") != NULL;
    xQueueOverwrite(road ? road_mailbox : mailbox, &raw);
}

static double number(const cJSON *root, const char *key, double fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsNumber(item) ? item->valuedouble : fallback;
}

bool gps_link_take_road(gps_road_t *out)
{
    static gps_raw_t raw;
    if (!road_mailbox || xQueueReceive(road_mailbox, &raw, 0) != pdTRUE) return false;
    cJSON *root = cJSON_ParseWithLength(raw.bytes, raw.length);
    if (!root) return false;
    *out = (gps_road_t){
        .lim = (int)number(root, "lim", 0),
        .alr = (int)number(root, "alr", 0),
        .received_us = raw.received_us,
    };
    const cJSON *st = cJSON_GetObjectItemCaseSensitive(root, "st");
    if (cJSON_IsString(st) && st->valuestring) {
        size_t n = strlen(st->valuestring);
        if (n >= sizeof(out->st)) { /* không cắt giữa ký tự UTF-8 */
            n = sizeof(out->st) - 1;
            while (n > 0 && (st->valuestring[n] & 0xC0) == 0x80) n--;
        }
        memcpy(out->st, st->valuestring, n);
        out->st[n] = '\0';
    }
    cJSON_Delete(root);
    return true;
}

bool gps_link_take(gps_fix_t *out)
{
    static gps_raw_t raw;
    if (!mailbox || xQueueReceive(mailbox, &raw, 0) != pdTRUE) return false;

    cJSON *root = cJSON_ParseWithLength(raw.bytes, raw.length);
    if (!root) return false;
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "t");
    bool ok = cJSON_IsString(type) && !strcmp(type->valuestring, "gps")
              && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(root, "lat"))
              && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(root, "lon"));
    if (ok) {
        *out = (gps_fix_t){
            .lat = number(root, "lat", 0),
            .lon = number(root, "lon", 0),
            .speed_kmh = (float)number(root, "spd", -1),
            .heading = (float)number(root, "hdg", -1),
            .accuracy_m = (float)number(root, "acc", -1),
            .unix_s = (int64_t)number(root, "unix", 0),
            .tz_min = (int)number(root, "tz", 420),
            .received_us = raw.received_us,
        };
    }
    cJSON_Delete(root);
    return ok;
}
