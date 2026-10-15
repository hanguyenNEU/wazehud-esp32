#include "hud_clock.h"

#include "esp_timer.h"

#include <stddef.h>

/* Unix hợp lý: từ 1/1/1970 tới 1/1/2100. */
#define UNIX_MAX 4102444800LL

static int64_t clock_unix;
static int64_t clock_at_us;
static int clock_tz_min;
static bool clock_valid;

/* Mailbox sync: protocol task ghi, LVGL task đọc (poll). */
static volatile bool sync_pending;
static volatile int64_t pending_unix;
static volatile int32_t pending_tz;

static int64_t default_now(void)
{
    return esp_timer_get_time();
}

static int64_t (*now_fn)(void) = default_now;

void hud_clock_set_now_fn(int64_t (*fn)(void))
{
    now_fn = fn ? fn : default_now;
}

bool hud_clock_compute(int64_t unix_anchor, int64_t anchor_us, int tz_minutes,
                       int64_t now_us, struct tm *out)
{
    if (!out) return false;
    if (unix_anchor <= 0 || unix_anchor > UNIX_MAX) return false;
    if (tz_minutes < -720 || tz_minutes > 840) return false;

    int64_t elapsed_s = (now_us - anchor_us) / 1000000;
    int64_t local = unix_anchor + elapsed_s + (int64_t)tz_minutes * 60;
    time_t local_time = (time_t)local;
    gmtime_r(&local_time, out);
    return true;
}

void hud_clock_sync(int64_t unix_seconds, int tz_minutes)
{
    pending_unix = unix_seconds;
    pending_tz = (int32_t)tz_minutes;
    sync_pending = true;
}

void hud_clock_poll(void)
{
    if (!sync_pending) return;
    sync_pending = false;

    struct tm probe;
    int64_t now = now_fn();
    if (!hud_clock_compute(pending_unix, now, pending_tz, now, &probe)) {
        /* Sync không hợp lệ: giữ trạng thái cũ. */
        return;
    }
    clock_unix = pending_unix;
    clock_tz_min = (int)pending_tz;
    clock_at_us = now;
    clock_valid = true;
}

bool hud_clock_get_local(struct tm *out)
{
    if (!clock_valid) return false;
    return hud_clock_compute(clock_unix, clock_at_us, clock_tz_min, now_fn(), out);
}

bool hud_clock_synced(void) { return clock_valid; }
int hud_clock_tz_minutes(void) { return clock_tz_min; }

int64_t hud_clock_unix_now(void)
{
    if (!clock_valid) return 0;
    return clock_unix + (now_fn() - clock_at_us) / 1000000;
}
