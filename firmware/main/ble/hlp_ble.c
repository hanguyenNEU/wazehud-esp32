/* GATT server HLP/1 — bám sát project `esp32-hlp-ble` của SDK chisadin.
 *
 * Khác bản mẫu:
 *   - State được đẩy ra callback của ứng dụng thay vì in thẳng ra serial.
 *   - Nhận tối đa 2 central cùng lúc: tweak Waze (HLP) và app CarDash trên
 *     iPhone chính (chỉ gửi GPS, xem gps_link.h). Kết nối HLP là central ĐẦU
 *     TIÊN thực sự nói HLP (ghi TX hoặc bật notify RX) — không phải central
 *     kết nối đầu tiên — để app GPS nối trước cũng không chiếm chỗ của tweak.
 *   - Chuyển tiếp nguyên dòng state HLP của tweak sang app CarDash (notify
 *     8a7e0014) — CarDash trên máy không chạy Waze vẫn có lim/tên đường/cảnh
 *     báo: Waze (iPhone chính) → HUD → CarDash (máy khác).
 */

#include "hlp_ble.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_hs_id.h"
#include "host/ble_uuid.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "gps_link.h"
#include "bg_store.h"
#include "trip_link.h"
#include "hlp_core.h"
#include "hlp_device_config.h"
#include "hlp_messages.h"
#include "hud_config.h"

#include <string.h>
#include <stdatomic.h>

static const char *TAG = "hlp_ble";

#define DEVICE_NAME "WazeHUD"
#define FIRMWARE    "0.1.0"
#define WANT_RATE   4 /* BLE bị chặn ở 1..4 Hz (§2.5.3) */

/* §8 — sai một bit là Waze Mod không nhìn thấy thiết bị. */
static const ble_uuid128_t SERVICE =
    BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
                     0x48, 0x4c, 0x6e, 0x4d, 0x01, 0x00, 0x7e, 0x8a);
static const ble_uuid128_t TX =
    BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
                     0x48, 0x4c, 0x6e, 0x4d, 0x02, 0x00, 0x7e, 0x8a);
static const ble_uuid128_t RX =
    BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
                     0x48, 0x4c, 0x6e, 0x4d, 0x03, 0x00, 0x7e, 0x8a);
static const ble_uuid128_t CAPABILITIES =
    BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
                     0x48, 0x4c, 0x6e, 0x4d, 0x04, 0x00, 0x7e, 0x8a);

/* Service CarDash (riêng, không thuộc HLP): 8a7e0010-… / GPS 8a7e0011-….
   Không quảng bá UUID này (gói quảng bá hết chỗ) — app quét theo SERVICE
   HLP ở trên rồi discover service này sau khi kết nối. */
static const ble_uuid128_t CARDASH_SERVICE =
    BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
                     0x48, 0x4c, 0x6e, 0x4d, 0x10, 0x00, 0x7e, 0x8a);
static const ble_uuid128_t CARDASH_GPS =
    BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
                     0x48, 0x4c, 0x6e, 0x4d, 0x11, 0x00, 0x7e, 0x8a);
/* Kênh tin 8a7e0012-… (thống kê chuyến đi) — nhiều mảnh, ghi có phản hồi
   (xem trip_link.h). */
static const ble_uuid128_t CARDASH_MSG =
    BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
                     0x48, 0x4c, 0x6e, 0x4d, 0x12, 0x00, 0x7e, 0x8a);
/* Ảnh nền đồng hồ 8a7e0013-… — nhị phân nhiều mảnh + notify kết quả
   (giao thức trong bg_store.h). */
static const ble_uuid128_t CARDASH_BG =
    BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
                     0x48, 0x4c, 0x6e, 0x4d, 0x13, 0x00, 0x7e, 0x8a);
/* Chuyển tiếp state Waze 8a7e0014-… — notify, mỗi state là 1 dòng JSON HLP
   nguyên bản kết thúc '\n', chia mảnh theo MTU (app ghép lại theo '\n'). */
static const ble_uuid128_t CARDASH_NAV =
    BLE_UUID128_INIT(0x01, 0x00, 0x4c, 0x50, 0x4c, 0x48, 0x9d, 0x9a,
                     0x48, 0x4c, 0x6e, 0x4d, 0x14, 0x00, 0x7e, 0x8a);

#define MAX_CENTRALS 2 /* khớp CONFIG_BT_NIMBLE_MAX_CONNECTIONS */

typedef struct {
    uint32_t generation;
    uint16_t length;
    uint8_t bytes[HLP_MAX_FRAME];
} ble_chunk_t;

static QueueHandle_t chunks;
static hlp_rx_t receiver;
static uint32_t receiver_generation;
static _Atomic uint16_t connection = BLE_HS_CONN_HANDLE_NONE;
static _Atomic uint32_t generation;
static uint16_t rx_value_handle;
static uint16_t gps_value_handle;
static uint16_t bg_value_handle;
static uint16_t msg_value_handle;
static _Atomic uint16_t bg_conn = BLE_HS_CONN_HANDLE_NONE;
static uint16_t nav_value_handle;
static _Atomic uint16_t nav_conn = BLE_HS_CONN_HANDLE_NONE;

/* Kết quả lưu ảnh nền → app (chạy ở task bg_store). */
static void bg_reply(const char *msg)
{
    uint16_t conn = bg_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, strlen(msg));
    if (om && ble_gatts_notify_custom(conn, bg_value_handle, om) != 0)
        ESP_LOGW(TAG, "không báo được kết quả ảnh nền");
}
static _Atomic int central_count;
static atomic_bool notify_enabled;
static atomic_bool send_dev_pending;
static uint8_t own_addr_type;
static struct ble_gap_adv_params adv_params;

static hlp_ble_state_cb_t app_state_cb;
static void *app_state_user;
static atomic_uint state_count;

bool hlp_ble_connected(void) { return connection != BLE_HS_CONN_HANDLE_NONE; }
bool hlp_ble_subscribed(void) { return notify_enabled; }
uint32_t hlp_ble_generation(void) { return generation; }
unsigned hlp_ble_state_count(void) { return state_count; }

static void send_line(const char *line, void *user)
{
    (void)user;
    if (!notify_enabled || connection == BLE_HS_CONN_HANDLE_NONE) return;
    uint32_t send_generation = generation;
    uint16_t send_connection = connection;

    size_t length = strlen(line);
    if (length + 1 > HLP_MAX_FRAME) return;

    char frame[HLP_MAX_FRAME];
    memcpy(frame, line, length);
    frame[length++] = '\n';

    /* Chia theo MTU đã thương lượng; chạy được cả ở MTU 23 lẫn 247. */
    uint16_t mtu = ble_att_mtu(send_connection);
    size_t payload = mtu > 3 ? mtu - 3 : BLE_ATT_MTU_DFLT - 3;

    for (size_t offset = 0; offset < length; offset += payload) {
        size_t chunk_length = length - offset;
        if (chunk_length > payload) chunk_length = payload;

        int rc = BLE_HS_ENOMEM;
        for (int attempt = 0; attempt < 3 && rc == BLE_HS_ENOMEM; ++attempt) {
            if (generation != send_generation || !notify_enabled
                || connection != send_connection) return;
            struct os_mbuf *om = ble_hs_mbuf_from_flat(frame + offset, chunk_length);
            rc = om ? ble_gatts_notify_custom(send_connection, rx_value_handle, om)
                    : BLE_HS_ENOMEM;
            if (rc == BLE_HS_ENOMEM) vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (rc != 0) {
            ESP_LOGW(TAG, "notify RX lỗi: rc=%d", rc);
            return;
        }
    }
}

/* Chỉ chạy trong protocol_task (qua state_line) nên buffer tĩnh là đủ —
   không cộng thêm 512 B vào stack vốn đã có cJSON + send_line. */
static char nav_frame[HLP_MAX_FRAME];

/* Best-effort: rớt 1 state không sao, state kế tiếp (≤ WANT_RATE Hz) là bản
   đầy đủ (§5). Không bao giờ được chặn đường HLP chính. */
static void relay_nav(const char *line, size_t length)
{
    uint16_t conn = nav_conn;
    if (conn == BLE_HS_CONN_HANDLE_NONE || conn == connection) return;
    if (length + 1 > HLP_MAX_FRAME) return;

    memcpy(nav_frame, line, length);
    nav_frame[length++] = '\n';

    uint16_t mtu = ble_att_mtu(conn);
    size_t payload = mtu > 3 ? mtu - 3 : BLE_ATT_MTU_DFLT - 3;

    for (size_t offset = 0; offset < length; offset += payload) {
        size_t chunk_length = length - offset;
        if (chunk_length > payload) chunk_length = payload;

        int rc = BLE_HS_ENOMEM;
        for (int attempt = 0; attempt < 3 && rc == BLE_HS_ENOMEM; ++attempt) {
            if (nav_conn != conn) return;
            struct os_mbuf *om = ble_hs_mbuf_from_flat(nav_frame + offset, chunk_length);
            rc = om ? ble_gatts_notify_custom(conn, nav_value_handle, om) : BLE_HS_ENOMEM;
            if (rc == BLE_HS_ENOMEM) vTaskDelay(pdMS_TO_TICKS(5));
        }
        if (rc != 0) {
            ESP_LOGW(TAG, "notify NAV lỗi: rc=%d", rc);
            return;
        }
    }
}

static void state_line(const char *line, size_t length, void *user)
{
    (void)user;
    state_count++;
    if (app_state_cb) app_state_cb(line, length, receiver_generation, app_state_user);
    relay_nav(line, length);
}

static void on_line(const char *line, size_t length, void *user)
{
    (void)user;
    hlp_handle_line(line, length, send_line, state_line, NULL);
}

/* Central này là kết nối HLP? Chưa ai nhận thì nhận luôn (chạy trong task
   NimBLE — mọi chỗ ghi `connection` đều ở đây nên không cần khoá). */
static bool claim_hlp(uint16_t conn_handle)
{
    if (connection == conn_handle) return true;
    if (connection != BLE_HS_CONN_HANDLE_NONE) return false;
    atomic_fetch_add(&generation, 1);
    connection = conn_handle;
    ESP_LOGI(TAG, "kết nối %u là HLP (tweak Waze)", conn_handle);
    return true;
}

static int access_cb(uint16_t conn_handle, uint16_t attr_handle,
                     struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)arg;

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && attr_handle == bg_value_handle) {
        uint8_t buf[520];
        size_t length = OS_MBUF_PKTLEN(ctxt->om);
        if (length > sizeof(buf)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (os_mbuf_copydata(ctxt->om, 0, length, buf) != 0) return BLE_ATT_ERR_UNLIKELY;
        bg_conn = conn_handle;
        bg_store_feed(buf, length, bg_reply);
        return 0;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && attr_handle == msg_value_handle) {
        uint8_t buf[512];
        size_t length = OS_MBUF_PKTLEN(ctxt->om);
        if (length > sizeof(buf)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (os_mbuf_copydata(ctxt->om, 0, length, buf) != 0) return BLE_ATT_ERR_UNLIKELY;
        trip_link_feed(conn_handle, buf, length);
        return 0;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR && attr_handle == gps_value_handle) {
        uint8_t buf[256];
        size_t length = OS_MBUF_PKTLEN(ctxt->om);
        if (length > sizeof(buf)) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if (os_mbuf_copydata(ctxt->om, 0, length, buf) != 0) return BLE_ATT_ERR_UNLIKELY;
        gps_link_feed(buf, length);
        return 0;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (!claim_hlp(conn_handle)) return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        /* Chỉ copy byte rồi thoát ngay. Android chờ callback của chunk trước
           mới gửi chunk sau, với timeout 3 giây — làm gì nặng ở đây là rớt. */
        size_t length = OS_MBUF_PKTLEN(ctxt->om);
        if (length > HLP_MAX_FRAME) return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;

        ble_chunk_t chunk = {.generation = generation, .length = (uint16_t)length};
        if (os_mbuf_copydata(ctxt->om, 0, length, chunk.bytes) != 0)
            return BLE_ATT_ERR_UNLIKELY;
        if (xQueueSend(chunks, &chunk, 0) != pdTRUE)
            return BLE_ATT_ERR_INSUFFICIENT_RES;
        return 0;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        const char caps[] =
            "{\"v\":1,\"caps\":{\"transport\":\"ble\",\"maxFrame\":512}}\n";
        return os_mbuf_append(ctxt->om, caps, sizeof(caps) - 1) == 0
                   ? 0
                   : BLE_ATT_ERR_INSUFFICIENT_RES;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static struct ble_gatt_chr_def characteristics[] = {
    {.uuid = &TX.u, .access_cb = access_cb, .flags = BLE_GATT_CHR_F_WRITE},
    {.uuid = &RX.u, .val_handle = &rx_value_handle, .access_cb = access_cb,
     .flags = BLE_GATT_CHR_F_NOTIFY},
    {.uuid = &CAPABILITIES.u, .access_cb = access_cb, .flags = BLE_GATT_CHR_F_READ},
    {0},
};

static struct ble_gatt_chr_def cardash_characteristics[] = {
    {.uuid = &CARDASH_GPS.u, .val_handle = &gps_value_handle, .access_cb = access_cb,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
    {.uuid = &CARDASH_MSG.u, .val_handle = &msg_value_handle, .access_cb = access_cb,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
    {.uuid = &CARDASH_BG.u, .val_handle = &bg_value_handle, .access_cb = access_cb,
     .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP | BLE_GATT_CHR_F_NOTIFY},
    {.uuid = &CARDASH_NAV.u, .val_handle = &nav_value_handle, .access_cb = access_cb,
     .flags = BLE_GATT_CHR_F_NOTIFY},
    {0},
};

static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &SERVICE.u,
     .characteristics = characteristics},
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &CARDASH_SERVICE.u,
     .characteristics = cardash_characteristics},
    {0},
};

/* Còn chỗ cho central nữa thì quảng bá tiếp (NimBLE tự dừng khi có kết nối). */
static int gap_event(struct ble_gap_event *event, void *arg);
static void readvertise(void)
{
    if (central_count >= MAX_CENTRALS || ble_gap_adv_active()) return;
    int rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                               gap_event, NULL);
    if (rc != 0) ESP_LOGW(TAG, "quảng bá lại lỗi: rc=%d", rc);
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            central_count++;
            ESP_LOGI(TAG, "central %u đã kết nối (%d/%d)", event->connect.conn_handle,
                     (int)central_count, MAX_CENTRALS);
        }
        readvertise();
        break;

    case BLE_GAP_EVENT_DISCONNECT: {
        uint16_t handle = event->disconnect.conn.conn_handle;
        if (central_count > 0) central_count--;
        ESP_LOGI(TAG, "central %u mất kết nối (%d/%d)", handle, (int)central_count,
                 MAX_CENTRALS);
        if (handle == bg_conn) bg_conn = BLE_HS_CONN_HANDLE_NONE;
        trip_link_disconnected(handle);
        if (handle == nav_conn) nav_conn = BLE_HS_CONN_HANDLE_NONE;
        if (handle == connection) {
            connection = BLE_HS_CONN_HANDLE_NONE;
            notify_enabled = false;
            send_dev_pending = false;
            atomic_fetch_add(&generation, 1);
        }
        /* Bắt buộc: Android chỉ retry theo lịch backoff, thiết bị phải sẵn sàng. */
        readvertise();
        break;
    }

    case BLE_GAP_EVENT_SUBSCRIBE:
        if (event->subscribe.attr_handle == nav_value_handle) {
            if (event->subscribe.cur_notify) {
                nav_conn = event->subscribe.conn_handle;
                ESP_LOGI(TAG, "kết nối %u nhận chuyển tiếp state Waze", nav_conn);
            } else if (event->subscribe.conn_handle == nav_conn) {
                nav_conn = BLE_HS_CONN_HANDLE_NONE;
            }
            break;
        }
        /* Tắt notify (kể cả lúc NimBLE dọn kết nối đứt) không được nhận
           quyền HLP cho một kết nối khác. */
        if (event->subscribe.attr_handle == rx_value_handle
            && (event->subscribe.cur_notify ? claim_hlp(event->subscribe.conn_handle)
                                            : event->subscribe.conn_handle == connection)) {
            notify_enabled = event->subscribe.cur_notify;
            /* Gửi `dev` NGAY khi Android bật notification — nó chỉ chờ 500 ms
               rồi tự chốt mặc định, và mặc định đó loại bỏ `alrs` (§2.5). */
            if (notify_enabled) send_dev_pending = true;
        }
        break;

    default:
        break;
    }
    return 0;
}

static void advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (uint8_t *)DEVICE_NAME;
    fields.name_len = strlen(DEVICE_NAME);
    fields.name_is_complete = 1;
    /* Picker của Waze Mod tìm thiết bị BLE theo service UUID này. */
    fields.uuids128 = (ble_uuid128_t *)&SERVICE;
    fields.num_uuids128 = 1;
    fields.uuids128_is_complete = 1;

    int rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "không đặt được advertising fields: rc=%d", rc);
        return;
    }

    adv_params = (struct ble_gap_adv_params){0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    rc = ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           gap_event, NULL);
    if (rc != 0) ESP_LOGE(TAG, "không quảng bá được: rc=%d", rc);
    else ESP_LOGI(TAG, "đang quảng bá tên %s", DEVICE_NAME);
}

static void on_sync(void)
{
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "không suy ra được kiểu địa chỉ: rc=%d", rc);
        return;
    }
    advertise();
}

static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void protocol_task(void *arg)
{
    (void)arg;
    ble_chunk_t chunk;
    receiver_generation = generation;
    for (;;) {
        if (atomic_exchange(&send_dev_pending, false)) {
            hlp_send_dev(send_line, NULL, "ble", DEVICE_NAME, FIRMWARE, WANT_RATE);
        }
        /* Local config patch (UI): tối đa 1 request mỗi vòng, sau send_dev,
           trước RX — không tạo task NVS mới, không block LVGL. */
        hud_config_process_local();
        if (xQueueReceive(chunks, &chunk, pdMS_TO_TICKS(100)) == pdTRUE
            && chunk.length > 0) {
            uint32_t current_generation = generation;
            if (chunk.generation != current_generation || !hlp_ble_connected()) continue;
            if (receiver_generation != current_generation) {
                hlp_rx_init(&receiver, on_line, NULL);
                receiver_generation = current_generation;
            }
            hlp_rx_feed(&receiver, chunk.bytes, chunk.length);
        }
    }
}

void hlp_ble_start(hlp_ble_state_cb_t on_state, void *user)
{
    app_state_cb = on_state;
    app_state_user = user;

    hlp_device_config_init();
    gps_link_init();
    trip_link_init();

    chunks = xQueueCreate(16, sizeof(ble_chunk_t));
    ESP_ERROR_CHECK(chunks ? ESP_OK : ESP_ERR_NO_MEM);

    hlp_rx_init(&receiver, on_line, NULL);

    ESP_ERROR_CHECK(nimble_port_init());
    ble_svc_gap_init();
    ble_svc_gatt_init();

    int rc = ble_gatts_count_cfg(services);
    if (rc == 0) rc = ble_gatts_add_svcs(services);
    if (rc == 0) rc = ble_svc_gap_device_name_set(DEVICE_NAME);
    if (rc != 0) {
        ESP_LOGE(TAG, "không cấu hình được GATT server: rc=%d", rc);
        return;
    }

    ble_hs_cfg.sync_cb = on_sync;

    if (xTaskCreate(protocol_task, "hlp_protocol", 4096, NULL, 6, NULL) != pdPASS) {
        ESP_LOGE(TAG, "không tạo được protocol task");
        return;
    }

    nimble_port_freertos_init(host_task);
    ESP_LOGI(TAG, "HLP BLE service đã khởi động");
}
