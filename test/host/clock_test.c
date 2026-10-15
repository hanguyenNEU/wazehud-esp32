/* Host test đồng hồ — fake monotonic clock, không ESP-IDF. */
#include "../../firmware/main/system/hud_clock.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int64_t esp_timer_get_time(void);

static int64_t fake_now_us;

int64_t esp_timer_get_time(void)
{
    return fake_now_us;
}

static int64_t test_now_fn(void)
{
    return fake_now_us;
}

/* 2023-11-14 22:13:20 UTC */
#define UNIX0 1700000000LL

int main(void)
{
    hud_clock_set_now_fn(test_now_fn);
    fake_now_us = 0;

    /* Chưa sync: không có giờ. */
    struct tm tm;
    assert(!hud_clock_get_local(&tm));

    /* Sync hợp lệ: 2023-11-15 05:13:20 VN (tz +420). */
    hud_clock_sync(UNIX0, 420);
    assert(!hud_clock_get_local(&tm)); /* chưa poll */
    hud_clock_poll();
    assert(hud_clock_get_local(&tm));
    assert(tm.tm_year + 1900 == 2023 && tm.tm_mon + 1 == 11 && tm.tm_mday == 15);
    assert(tm.tm_hour == 5 && tm.tm_min == 13 && tm.tm_sec == 20);
    assert(hud_clock_tz_minutes() == 420);
    puts("ok sync + local breakdown");

    /* Đếm tiếp bằng monotonic sau khi "mất BLE". */
    fake_now_us += 90LL * 1000000;
    assert(hud_clock_get_local(&tm));
    assert(tm.tm_min == 14 && tm.tm_sec == 50);
    assert(hud_clock_unix_now() == UNIX0 + 90);
    puts("ok monotonic counting after sync loss");

    /* tz ngoài khoảng -12h..+14h: bỏ sync, giữ state cũ. */
    hud_clock_sync(UNIX0 + 1000, 900);
    hud_clock_poll();
    assert(hud_clock_tz_minutes() == 420);
    assert(hud_clock_unix_now() == UNIX0 + 90);
    /* unix ngoài khoảng: cũng bỏ. */
    hud_clock_sync(99999999999LL, 420);
    hud_clock_poll();
    assert(hud_clock_unix_now() == UNIX0 + 90);
    puts("ok invalid sync rejected");

    /* Sync lại hợp lệ sau lỗi. */
    hud_clock_sync(UNIX0 + 7200, 600);
    hud_clock_poll();
    assert(hud_clock_get_local(&tm));
    assert(tm.tm_hour == 10 && tm.tm_min == 13); /* 00:13Z + 10h = 10:13 */
    puts("ok resync after invalid");

    /* Giao thừa: 2023-12-31 17:00 UTC = 2024-01-01 00:00 VN. */
    fake_now_us = 0;
    hud_clock_sync(1704042000LL, 420); /* 2024-01-01 00:00:00 VN */
    hud_clock_poll();
    assert(hud_clock_get_local(&tm));
    assert(tm.tm_year + 1900 == 2024 && tm.tm_mon + 1 == 1 && tm.tm_mday == 1);
    assert(tm.tm_hour == 0 && tm.tm_min == 0 && tm.tm_sec == 0);
    /* Lùi 1 giây về 23:59:59 ngày 31/12/2023. */
    fake_now_us -= 1000000;
    assert(hud_clock_get_local(&tm));
    assert(tm.tm_year + 1900 == 2023 && tm.tm_mday == 31 && tm.tm_hour == 23);
    puts("ok new year rollover");

    puts("CLOCK: all tests passed");
    return 0;
}
