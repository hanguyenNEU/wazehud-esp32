/* Cảm ứng FT6336G của ES3C28P — đăng ký pointer indev cho LVGL + nguồn mẫu
 * cho gesture classifier (ui_input).
 *
 * §4 kế hoạch UI:
 *   - Kết quả đọc phân biệt TOUCH/RELEASE/ERROR. Lỗi ngắn giữ contact
 *     (không tạo UP giả); lỗi liên tục ≥150ms hủy contact và suppress tới
 *     release thật. FT6336 báo số điểm chạm đáng tin nên nhả ngay ở mẫu
 *     no-touch đầu tiên (debounce cũ làm mỗi lần bấm trễ ~70ms).
 *   - 2 điểm chạm (thường là điểm ma do nhiễu trên xe): lấy điểm đầu, không
 *     hủy contact.
 *   - Chân INT (IO17, theo BSP/ESPHome của ES3C28P): chỉ đọc I2C khi có ngắt
 *     hoặc đang giữ tay; vẫn quét dự phòng thưa để không mất chạm nếu INT
 *     không hoạt động như mong đợi.
 *   - Calibration là owner ĐỘC QUYỀN của bus: mode được công bố (qua
 *     ui_router) TRƯỚC khi task đọc raw; mutex chờ giao dịch đang chạy;
 *     LVGL read_cb dừng đọc bus trong lúc đó.
 *   - Giữ tay 3s để phục hồi giờ do classifier quyết (raw displacement nhỏ,
 *     chưa bị claim) — không còn logic long-press trong driver.
 *   - Không sleep trong read_cb; timeout I2C truyền thẳng 20ms.
 */

#include "touch_es3c28p.h"

#include "ui_system.h"
#include "ui_input.h"
#include "ui_router.h"
#include "board.h"

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"

#include <math.h>
#include <stdatomic.h>

#define TOUCH_I2C_PORT I2C_NUM_0
#define TOUCH_SDA 16
#define TOUCH_SCL 15
#define TOUCH_RST 18
#define TOUCH_INT 17
#define TOUCH_ADDR 0x38

#define TOUCH_READ_PERIOD_MS 15 /* chu kỳ đọc indev; LVGL mặc định 33ms quá thưa cho vuốt */
#define TOUCH_IDLE_POLL_TICKS 6 /* không có INT: vẫn đọc dự phòng mỗi ~90ms */

/* Bật khi cần soi toạ độ trên máy — mặc định tắt để không spam log. */
#define TOUCH_DEBUG 0

#define CAL_ERROR_MS 150    /* lỗi liên tục → hủy contact */
#define CAL_TAP_TIMEOUT_MS 30000 /* deadline mỗi điểm */

static const char *TAG = "touch";
static i2c_master_dev_handle_t touch_dev;
static SemaphoreHandle_t bus_mutex;
static bool ready;

/* Calibration task là owner độc quyền khi đang đọc bus. */
static _Atomic bool cal_active;

/* Mapping: screen_x = cal.a * raw_y + cal.b ; screen_y = cal.c * raw_x + cal.d
   (FT6336G luôn báo theo mặt phẳng portrait gốc 240×320 → swap trục).
   Mặc định đo trên board thật (repo esphome-es3c28p-light-panel), cùng cấu
   hình màn swap_xy + mirror_x + mirror_y như main.c: touch cần swap + mirror_x,
   KHÔNG mirror_y. Bản cũ lật cả Y nên bấm lệch dọc (vuốt ngang vẫn đúng). */
typedef struct {
    float a, b, c, d;
} touch_cal_t;

static const touch_cal_t cal_default = {-1.0f, (float)(DISPLAY_W - 1), 1.0f, 0.0f};
static touch_cal_t cal = cal_default;
static _Atomic bool recal_flag;

#define CAL_NVS_NS "touch_cal"
#define CAL_NVS_KEY "abcd"

static bool cal_load(touch_cal_t *out)
{
    nvs_handle_t h;
    if (nvs_open(CAL_NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sz = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, CAL_NVS_KEY, out, &sz);
    nvs_close(h);
    return err == ESP_OK && sz == sizeof(*out);
}

static bool cal_save(const touch_cal_t *in)
{
    nvs_handle_t h;
    if (nvs_open(CAL_NVS_NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, CAL_NVS_KEY, in, sizeof(*in));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}

/* Hệ số đã lưu phải cùng hướng với mặc định và scale gần 1 (raw FT6336 đã
   là pixel). Lệch thế là bản hiệu chỉnh hỏng/cũ — bỏ, dùng mặc định. */
static bool cal_plausible(const touch_cal_t *c)
{
    if (!isfinite(c->a) || !isfinite(c->b) || !isfinite(c->c) || !isfinite(c->d))
        return false;
    if ((c->a < 0) != (cal_default.a < 0) || (c->c < 0) != (cal_default.c < 0))
        return false;
    float sa = fabsf(c->a), sc = fabsf(c->c);
    return sa > 0.7f && sa < 1.4f && sc > 0.7f && sc < 1.4f;
}

/* ---- Đọc bus ---- */

typedef enum {
    SAMPLE_TOUCH = 0,
    SAMPLE_RELEASE,
    SAMPLE_ERROR,
} touch_sample_t;

static esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t length)
{
    if (!touch_dev) return ESP_ERR_INVALID_STATE;
    return i2c_master_transmit_receive(touch_dev, &reg, 1, data, length, 20);
}

static touch_sample_t read_sample(int *rx, int *ry)
{
    uint8_t data[5];
    if (read_regs(0x02, data, sizeof(data)) != ESP_OK) return SAMPLE_ERROR;
    uint8_t points = data[0] & 0x0F;
    if (points == 0 || points > 2) return SAMPLE_RELEASE; /* >2: giá trị rác */
    /* Cờ sự kiện điểm 1: 1 = lift up — coi như đã nhả. */
    if ((data[1] >> 6) == 1) return SAMPLE_RELEASE;
    *rx = ((data[1] & 0x0F) << 8) | data[2];
    *ry = ((data[3] & 0x0F) << 8) | data[4];
    return SAMPLE_TOUCH;
}

/* ---- Ngắt INT ---- */

static _Atomic bool int_pending;
static bool int_ok;

static void IRAM_ATTR touch_int_isr(void *arg)
{
    (void)arg;
    int_pending = true;
}

static void int_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << TOUCH_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    if (gpio_config(&io) != ESP_OK) return;
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return; /* đã cài: dùng chung */
    int_ok = gpio_isr_handler_add(TOUCH_INT, touch_int_isr, NULL) == ESP_OK;
    ESP_LOGI(TAG, "INT IO%d %s", TOUCH_INT, int_ok ? "bật" : "lỗi — chỉ polling");
}

/* Có cần đọc bus ở lượt này không. Đang giữ tay luôn đọc để bắt nhả. */
static bool should_read(bool pressed)
{
    static int idle_ticks;
    if (!int_ok || pressed) { idle_ticks = 0; return true; }
    bool hit = atomic_exchange(&int_pending, false) || gpio_get_level(TOUCH_INT) == 0;
    if (hit || ++idle_ticks >= TOUCH_IDLE_POLL_TICKS) { idle_ticks = 0; return true; }
    return false;
}

static bool read_touch(int *x, int *y)
{
    int raw_x, raw_y;
    if (read_sample(&raw_x, &raw_y) != SAMPLE_TOUCH) return false;

    *x = (int)lroundf(cal.a * (float)raw_y + cal.b);
    *y = (int)lroundf(cal.c * (float)raw_x + cal.d);
    if (*x < 0) *x = 0;
    if (*x >= DISPLAY_W) *x = DISPLAY_W - 1;
    if (*y < 0) *y = 0;
    if (*y >= DISPLAY_H) *y = DISPLAY_H - 1;
    return true;
}

/* ---- Trạng thái read_cb ---- */

static struct {
    bool pressed;
    bool suppressed;      /* sau CANCEL: chặn mọi thứ tới release thật */
    int release_streak;   /* số mẫu no-touch liên tiếp */
    int64_t error_since_us;
    lv_point_t last_point;
} tc;

static uint32_t sample_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void feed_gesture(gesture_event_t event, int x, int y, int raw_x, int raw_y)
{
    gesture_sample_t s = {
        .event = event,
        .time_ms = sample_ms(),
        .x = x, .y = y,
        .raw_x = raw_x, .raw_y = raw_y,
        .in_edge = x <= ui_input_edge_width() || x >= DISPLAY_W - ui_input_edge_width(),
    };
    ui_input_feed(&s);
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;

    if (cal_active || ui_router_input_locked()) {
        /* Calibration/special mode sở hữu bus — indev chỉ báo nhả, không đọc. */
        data->point = tc.last_point;
        data->state = LV_INDEV_STATE_RELEASED;
        if (tc.pressed)
            feed_gesture(GESTURE_EVENT_CANCEL, tc.last_point.x, tc.last_point.y, -1, -1);
        tc.pressed = false;
        tc.suppressed = true;
        tc.release_streak = 0;
        return;
    }

    /* Không có ngắt và không giữ tay: khỏi chiếm bus I2C trong LVGL task. */
    if (ready && !should_read(tc.pressed || tc.suppressed)) {
        data->point = tc.last_point;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    int raw_x = 0, raw_y = 0;
    touch_sample_t s = ready ? read_sample(&raw_x, &raw_y) : SAMPLE_ERROR;
    int64_t now = esp_timer_get_time();
    int x = 0, y = 0;
    if (s == SAMPLE_TOUCH) {
        x = (int)lroundf(cal.a * (float)raw_y + cal.b);
        y = (int)lroundf(cal.c * (float)raw_x + cal.d);
        if (x < 0) x = 0;
        if (x >= DISPLAY_W) x = DISPLAY_W - 1;
        if (y < 0) y = 0;
        if (y >= DISPLAY_H) y = DISPLAY_H - 1;
    }

    switch (s) {
    case SAMPLE_TOUCH: {
        tc.release_streak = 0;
        if (tc.suppressed) {
            data->point = tc.last_point;
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }
        tc.error_since_us = -1;
        if (!tc.pressed) {
            tc.pressed = true;
            tc.release_streak = 0;
            feed_gesture(GESTURE_EVENT_DOWN, x, y, raw_x, raw_y);
#if TOUCH_DEBUG
            ESP_LOGI(TAG, "press x=%d y=%d", x, y);
#endif
        } else {
            feed_gesture(GESTURE_EVENT_MOVE, x, y, raw_x, raw_y);
        }
        tc.last_point.x = x;
        tc.last_point.y = y;
        data->point = tc.last_point;
        data->state = LV_INDEV_STATE_PRESSED;
        return;
    }

    case SAMPLE_RELEASE: {
        tc.error_since_us = -1;
        tc.release_streak = 0;
        if (tc.suppressed) {
            /* Suppression kết thúc ở lần nhả thật đầu tiên. */
            tc.suppressed = false;
            tc.pressed = false;
        } else if (tc.pressed) {
            tc.pressed = false;
            feed_gesture(GESTURE_EVENT_UP, tc.last_point.x, tc.last_point.y, -1, -1);
#if TOUCH_DEBUG
            ESP_LOGI(TAG, "release x=%d y=%d", tc.last_point.x, tc.last_point.y);
#endif
        }
        data->point = tc.last_point;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    case SAMPLE_ERROR:
    default: {
        tc.release_streak = 0;
        if (tc.pressed && !tc.suppressed) {
            if (tc.error_since_us < 0) tc.error_since_us = now;
            else if (now - tc.error_since_us >= CAL_ERROR_MS * 1000) {
                /* Lỗi liên tục: hủy contact, chặn tới release thật. */
                tc.pressed = false;
                tc.suppressed = true;
                tc.release_streak = 0;
                feed_gesture(GESTURE_EVENT_CANCEL, tc.last_point.x, tc.last_point.y,
                             -1, -1);
            }
        }
        /* Lỗi ngắn: giữ contact — không tạo UP. */
        data->point = tc.last_point;
        data->state = tc.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        return;
    }
    }
}

/* ---- Hiệu chỉnh 2 điểm ---- */

static void cal_show(const char *msg, int x, int y)
{
    if (lvgl_port_lock(100)) {
        ui_show_touch_cal(msg, x, y);
        lvgl_port_unlock();
    }
}

static void cal_hide(void)
{
    if (lvgl_port_lock(100)) {
        ui_hide_touch_cal();
        lvgl_port_unlock();
    }
}

/* Đợi 1 chạm trong deadline; hết giờ trả false. Nhả hẳn tay trước khi về. */
static bool wait_for_tap(int *out_rx, int *out_ry)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)CAL_TAP_TIMEOUT_MS * 1000;
    int x = 0, y = 0;

    for (;;) {
        touch_sample_t s = read_sample(&x, &y);
        if (s == SAMPLE_TOUCH) break;
        if (esp_timer_get_time() > deadline) return false;
        vTaskDelay(pdMS_TO_TICKS(15));
    }

    long sx = 0, sy = 0;
    int n = 0;
    for (int i = 0; i < 10; ++i) {
        int rx, ry;
        if (read_sample(&rx, &ry) == SAMPLE_TOUCH) { sx += rx; sy += ry; n++; }
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    *out_rx = n ? (int)(sx / n) : x;
    *out_ry = n ? (int)(sy / n) : y;

    int released_streak = 0;
    int rx, ry;
    while (released_streak < 4) {
        if (esp_timer_get_time() > deadline) return false;
        touch_sample_t s = read_sample(&rx, &ry);
        released_streak = (s == SAMPLE_RELEASE) ? released_streak + 1 : 0;
        vTaskDelay(pdMS_TO_TICKS(15));
    }
    return true;
}

static bool cal_valid(const touch_cal_t *c, int rx1, int ry1, int rx2, int ry2)
{
    if (!isfinite(c->a) || !isfinite(c->b) || !isfinite(c->c) || !isfinite(c->d))
        return false;
    /* Scale hợp lý: 0.02..50 px/raw — ngoài khoảng là nhiễu hoặc chạm nhầm. */
    if (fabsf(c->a) < 0.02f || fabsf(c->a) > 50.0f) return false;
    if (fabsf(c->c) < 0.02f || fabsf(c->c) > 50.0f) return false;
    /* Hai điểm quá gần: phép nội suy không đáng tin — phải retry. */
    if (abs(rx2 - rx1) < 40 || abs(ry2 - ry1) < 40) return false;
    /* Cùng ngưỡng với lúc nạp NVS — không lưu thứ lần boot sau sẽ bỏ. */
    return cal_plausible(c);
}

#define CAL_MARGIN 40

static void run_calibration(void)
{
    ESP_LOGI(TAG, "Bắt đầu hiệu chỉnh cảm ứng 2 điểm");
    const int tx1 = CAL_MARGIN, ty1 = CAL_MARGIN;
    const int tx2 = DISPLAY_W - CAL_MARGIN, ty2 = DISPLAY_H - CAL_MARGIN;

    /* Công bố mode CALIBRATION TRƯỚC khi đọc raw — LVGL read_cb thấy
       input_locked và ngừng đọc bus. Đợi tới khi router đã chuyển mode. */
    if (!lvgl_port_lock(1000)) return;
    if (ui_router_mode() != UI_MODE_NORMAL) {
        lvgl_port_unlock();
        return;
    }
    ui_router_set_mode(UI_MODE_CALIBRATION);
    /* Outside event dispatch, under the LVGL lock: publish before bus handoff. */
    ui_router_process();
    lvgl_port_unlock();

    /* Chờ giao dịch I2C đang chạy dở (nếu có) kết thúc, rồi sở hữu bus. */
    xSemaphoreTake(bus_mutex, portMAX_DELAY);
    atomic_store(&cal_active, true);

    bool ok = false;
    int rx1 = 0, ry1 = 0, rx2 = 0, ry2 = 0;
    for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
        cal_show("Chạm vào chấm xanh (1/2)", tx1, ty1);
        if (!wait_for_tap(&rx1, &ry1)) {
            ESP_LOGW(TAG, "hết giờ điểm 1 — giữ hệ số cũ");
            break;
        }
        cal_show("Chạm vào chấm xanh (2/2)", tx2, ty2);
        if (!wait_for_tap(&rx2, &ry2)) {
            ESP_LOGW(TAG, "hết giờ điểm 2 — giữ hệ số cũ");
            break;
        }
        cal_hide();

        touch_cal_t c = cal;
        if (ry2 != ry1) {
            c.a = (float)(tx2 - tx1) / (float)(ry2 - ry1);
            c.b = tx1 - c.a * ry1;
        }
        if (rx2 != rx1) {
            c.c = (float)(ty2 - ty1) / (float)(rx2 - rx1);
            c.d = ty1 - c.c * rx1;
        }
        if (!cal_valid(&c, rx1, ry1, rx2, ry2)) {
            ESP_LOGW(TAG, "điểm chạm không hợp lệ — thử lại");
            cal_show("Điểm quá gần hoặc nhiễu — thử lại", tx1, ty1);
            vTaskDelay(pdMS_TO_TICKS(800));
            continue;
        }
        /* Apply ĐỦ bộ hệ số sau khi hợp lệ. NVS lỗi phải báo, RAM vẫn dùng. */
        cal = c;
        if (!cal_save(&c)) {
            ESP_LOGE(TAG, "không ghi NVS được hiệu chỉnh mới");
            cal_show("Không lưu được — chỉ dùng phiên này", DISPLAY_W / 2, DISPLAY_H / 2);
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
        ok = true;
        ESP_LOGI(TAG, "Hiệu chỉnh xong: raw1=(%d,%d) raw2=(%d,%d) -> a=%.4f b=%.2f c=%.4f d=%.2f",
                 rx1, ry1, rx2, ry2, c.a, c.b, c.c, c.d);
    }

    /* Nhả bus, trả input cho LVGL, về màn trước. */
    atomic_store(&cal_active, false);
    xSemaphoreGive(bus_mutex);
    if (lvgl_port_lock(0)) {
        ui_hide_touch_cal();
        tc.suppressed = true;
        tc.pressed = false;
        tc.release_streak = 0;
        ui_router_return_from_calibration();
        lvgl_port_unlock();
    }
}

static void calibration_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (atomic_exchange(&recal_flag, false)) {
            run_calibration();
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

void touch_es3c28p_request_recalibrate(void)
{
    recal_flag = true;
}

void touch_es3c28p_calibrate_if_needed(void)
{
    if (!ready) return;
    if (!lvgl_port_lock(0)) return;
    /* Không ép hiệu chỉnh lần đầu: raw FT6336 đã là pixel, mapping mặc định
       đúng sẵn. Bản lưu chỉ dùng khi cùng hướng với mặc định. */
    touch_cal_t stored;
    if (cal_load(&stored) && cal_plausible(&stored)) {
        cal = stored;
        ESP_LOGI(TAG, "Đã nạp hiệu chỉnh: a=%.4f b=%.2f c=%.4f d=%.2f",
                 cal.a, cal.b, cal.c, cal.d);
    } else {
        cal = cal_default;
        ESP_LOGI(TAG, "Dùng mapping mặc định (không có/bỏ hiệu chỉnh đã lưu)");
    }
    lvgl_port_unlock();
    if (xTaskCreate(calibration_task, "touch_cal", 4096, NULL, 3, NULL) != pdPASS)
        ESP_LOGE(TAG, "Không tạo được task hiệu chỉnh");
}

void touch_es3c28p_init(void)
{
    /* IDF 6.x: driver I2C legacy (i2c_param_config + i2c_driver_install)
       đã bị gỡ, chuyển sang API bus/device mới. */
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = TOUCH_I2C_PORT,
        .sda_io_num = TOUCH_SDA,
        .scl_io_num = TOUCH_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus = NULL;
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &bus);
    if (err == ESP_OK) {
        i2c_device_config_t dev_cfg = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = TOUCH_ADDR,
            .scl_speed_hz = 400000,
        };
        err = i2c_master_bus_add_device(bus, &dev_cfg, &touch_dev);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C touch init lỗi: %s", esp_err_to_name(err));
        return;
    }
    bus_mutex = xSemaphoreCreateMutex();
    if (!bus_mutex) return;

    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << TOUCH_RST,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&rst));
    gpio_set_level(TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    uint8_t chip = 0;
    ready = read_regs(0xA8, &chip, 1) == ESP_OK;
    ESP_LOGI(TAG, "FT6336G %s (chip 0x%02x)", ready ? "sẵn sàng" : "không phản hồi", chip);
    if (!ready) return;

    int_init();

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);
    lv_timer_set_period(lv_indev_get_read_timer(indev), TOUCH_READ_PERIOD_MS);
    ui_input_init(indev);
}
