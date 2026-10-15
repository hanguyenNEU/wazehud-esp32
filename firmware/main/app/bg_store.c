#include "bg_store.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "jpeg_decoder.h"

#include <stdatomic.h>
#include <string.h>

#define MAGIC 0x31424443u      /* "CDB1" */
#define PIXEL_OFFSET 0x1000
#define PIXEL_BYTES (BG_W * BG_H * 2)
#define JPEG_MAX (200 * 1024)  /* app nén còn ~20–40 KB, chừa rộng */

typedef struct {
    uint32_t magic;
    uint16_t w, h;
    uint32_t size;
    uint32_t crc;
} bg_header_t;

static const char *TAG = "bg_store";
static const esp_partition_t *part;
static _Atomic bool has_custom;
static _Atomic bool updated;

/* Phiên nhận (task NimBLE) → giao cho worker khi đủ. */
static uint8_t *jpeg;
static uint32_t jpeg_total, jpeg_received;
static _Atomic bool busy;            /* worker đang giải mã/ghi flash */
static bg_store_reply_cb_t reply_cb;
static TaskHandle_t worker;

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static bool read_header(bg_header_t *h)
{
    if (!part || esp_partition_read(part, 0, h, sizeof(*h)) != ESP_OK) return false;
    return h->magic == MAGIC && h->w == BG_W && h->h == BG_H && h->size == PIXEL_BYTES;
}

bool bg_store_has_custom(void) { return has_custom; }

bool bg_store_take_updated(void) { return atomic_exchange(&updated, false); }

bool bg_store_load(uint16_t *dst)
{
    bg_header_t h;
    if (!dst || !read_header(&h)) return false;
    if (esp_partition_read(part, PIXEL_OFFSET, dst, PIXEL_BYTES) != ESP_OK) return false;
    if (esp_rom_crc32_le(0, (const uint8_t *)dst, PIXEL_BYTES) != h.crc) {
        ESP_LOGW(TAG, "ảnh nền hỏng (CRC sai) — bỏ qua");
        return false;
    }
    return true;
}

static void reply(const char *msg)
{
    ESP_LOGI(TAG, "%s", msg);
    if (reply_cb) reply_cb(msg);
}

/* Giải JPEG → RGB565 → flash. Chạy ở task riêng: xoá/ghi ~40 sector mất
   cỡ 1 giây, không được chặn task NimBLE. */
static void worker_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        uint16_t *pixels = heap_caps_malloc(PIXEL_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        const char *result = "ok";
        if (!pixels) {
            result = "err:hết bộ nhớ";
            goto done;
        }

        esp_jpeg_image_cfg_t cfg = {
            .indata = jpeg,
            .indata_size = jpeg_total,
            .outbuf = (uint8_t *)pixels,
            .outbuf_size = PIXEL_BYTES,
            .out_format = JPEG_IMAGE_FORMAT_RGB565,
            .out_scale = JPEG_IMAGE_SCALE_0,
        };
        esp_jpeg_image_output_t info;
        if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK || info.width != BG_W
            || info.height != BG_H) {
            result = "err:ảnh phải là JPEG 320x240";
            goto done;
        }
        if (esp_jpeg_decode(&cfg, &info) != ESP_OK) {
            result = "err:giải JPEG lỗi";
            goto done;
        }

        bg_header_t h = {.magic = MAGIC, .w = BG_W, .h = BG_H, .size = PIXEL_BYTES,
                         .crc = esp_rom_crc32_le(0, (const uint8_t *)pixels, PIXEL_BYTES)};
        has_custom = false; /* đang ghi đè — tạm coi như không có */
        if (esp_partition_erase_range(part, 0, PIXEL_OFFSET) != ESP_OK
            || esp_partition_erase_range(part, PIXEL_OFFSET,
                                         (PIXEL_BYTES + 0xFFF) & ~0xFFFu) != ESP_OK
            || esp_partition_write(part, PIXEL_OFFSET, pixels, PIXEL_BYTES) != ESP_OK
            || esp_partition_write(part, 0, &h, sizeof(h)) != ESP_OK) {
            result = "err:ghi flash lỗi";
            goto done;
        }
        has_custom = true;
        updated = true;

    done:
        heap_caps_free(pixels);
        heap_caps_free(jpeg);
        jpeg = NULL;
        busy = false;
        reply(result);
    }
}

void bg_store_init(void)
{
    part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    if (!part || part->size < PIXEL_OFFSET + PIXEL_BYTES) {
        ESP_LOGE(TAG, "không có phân vùng storage đủ chỗ");
        part = NULL;
        return;
    }
    bg_header_t h;
    has_custom = read_header(&h);
    ESP_LOGI(TAG, "ảnh nền tự chọn: %s", has_custom ? "có" : "chưa có");
    if (xTaskCreate(worker_task, "bg_store", 4096, NULL, 3, &worker) != pdPASS)
        ESP_LOGE(TAG, "không tạo được task lưu ảnh nền");
}

void bg_store_feed(const uint8_t *data, size_t length, bg_store_reply_cb_t reply_fn)
{
    if (!part || !worker || length < 1) return;
    reply_cb = reply_fn;

    switch (data[0]) {
    case 0x01: { /* BEGIN */
        if (length < 5) return;
        if (busy) { reply("err:đang lưu ảnh trước"); return; }
        uint32_t total = rd32(data + 1);
        heap_caps_free(jpeg);
        jpeg = NULL;
        if (total == 0 || total > JPEG_MAX) { reply("err:ảnh quá lớn"); return; }
        jpeg = heap_caps_malloc(total, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!jpeg) { reply("err:hết bộ nhớ"); return; }
        jpeg_total = total;
        jpeg_received = 0;
        ESP_LOGI(TAG, "bắt đầu nhận ảnh nền %u byte", (unsigned)total);
        break;
    }
    case 0x02: { /* DATA */
        if (!jpeg || length < 5) return;
        uint32_t offset = rd32(data + 1);
        size_t n = length - 5;
        if (offset != jpeg_received || offset + n > jpeg_total) {
            heap_caps_free(jpeg);
            jpeg = NULL;
            reply("err:mất gói, gửi lại");
            return;
        }
        memcpy(jpeg + offset, data + 5, n);
        jpeg_received += n;
        break;
    }
    case 0x03: { /* END */
        if (!jpeg || length < 5) return;
        if (jpeg_received != jpeg_total
            || esp_rom_crc32_le(0, jpeg, jpeg_total) != rd32(data + 1)) {
            heap_caps_free(jpeg);
            jpeg = NULL;
            reply("err:dữ liệu hỏng, gửi lại");
            return;
        }
        busy = true;
        xTaskNotifyGive(worker); /* worker giữ `jpeg` tới khi xong */
        break;
    }
    default:
        break;
    }
}
