#include "ntp_sync.h"

#include "hud_clock.h"

#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_netif_sntp.h"

#include <stdbool.h>
#include <sys/time.h>

/* Server anycast trả lời nhanh và ổn định hơn pool.ntp.org — lần đầu dùng
   pool hay rơi gói, lwIP chờ 15 giây mỗi lần mới thử lại (~35 s mới có giờ).
   Hết hạn chờ ở server này thì lwIP tự chuyển sang server kế tiếp. */
#define NTP_SERVER_COUNT 3
/* Gói đầu ngay sau khi có IP hay bị rơi; lwIP chỉ thử lại sau 15 giây. Tự
   gửi lại mỗi 2 giây cho tới khi có giờ (tối đa 10 lần, sau đó để lwIP lo). */
#define NTP_KICK_US (2 * 1000 * 1000)
#define NTP_KICK_MAX 10
#define DEFAULT_TZ_MINUTES (7 * 60) /* Việt Nam */

static const char *TAG = "ntp";
static bool started;
static volatile bool synced;
static esp_timer_handle_t kick_timer;
static int kicks;

static void kick(void *arg)
{
    (void)arg;
    if (synced || ++kicks > NTP_KICK_MAX) {
        esp_timer_stop(kick_timer);
        return;
    }
    esp_sntp_restart(); /* qua tcpip_callback — gọi từ task khác được */
}

/* Chạy trong task SNTP của lwip — chỉ ghi mailbox của hud_clock. */
static void on_time_sync(struct timeval *tv)
{
    int tz = hud_clock_synced() ? hud_clock_tz_minutes() : DEFAULT_TZ_MINUTES;
    synced = true;
    hud_clock_sync((int64_t)tv->tv_sec, tz);
    ESP_LOGI(TAG, "đã đồng bộ giờ: unix=%lld tz=%+d phút", (long long)tv->tv_sec, tz);
}

void ntp_sync_on_connected(void)
{
    if (started) return;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        NTP_SERVER_COUNT,
        ESP_SNTP_SERVER_LIST("time.cloudflare.com", "time.google.com", "pool.ntp.org"));
    config.sync_cb = on_time_sync;
    esp_err_t err = esp_netif_sntp_init(&config);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "không khởi động được SNTP: %s", esp_err_to_name(err));
        return;
    }
    started = true;
    const esp_timer_create_args_t args = {.callback = kick, .name = "ntp_kick"};
    if (esp_timer_create(&args, &kick_timer) == ESP_OK)
        esp_timer_start_periodic(kick_timer, NTP_KICK_US);
    ESP_LOGI(TAG, "bắt đầu đồng bộ giờ");
}
