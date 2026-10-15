#include "screen_power.h"

#include "display.h"

static int backlight_percent = 80;
static int dim_idle_min = 5;
static int mode_min_percent; /* 0 = không ép */
static bool screen_dimmed;

static int effective_brightness(void)
{
    int b = backlight_percent;
    if (b < mode_min_percent) b = mode_min_percent;
    return b;
}

void screen_power_apply_config(int brightness, int dim_idle)
{
    backlight_percent = brightness;
    dim_idle_min = dim_idle;
    if (!screen_dimmed) display_set_backlight(effective_brightness());
}

int screen_power_brightness(void)
{
    return backlight_percent;
}

void screen_power_tick(int64_t now_us, int64_t last_nav_us, int64_t last_activity_us)
{
    if (dim_idle_min > 0 && mode_min_percent == 0) {
        /* Do not dim before the first navigation session. */
        int64_t anchor = last_nav_us > last_activity_us ? last_nav_us
                                                        : last_activity_us;
        bool idle = last_nav_us != 0
                    && (now_us - anchor) / 1000000 > (int64_t)dim_idle_min * 60;
        if (idle != screen_dimmed) {
            screen_dimmed = idle;
            display_set_backlight(idle ? 0 : effective_brightness());
        }
    } else if (screen_dimmed) {
        screen_dimmed = false;
        display_set_backlight(effective_brightness());
    }
}

bool screen_power_consume_wake_contact(void)
{
    if (!screen_dimmed) return false;
    screen_dimmed = false;
    display_set_backlight(effective_brightness());
    /* Contact này chỉ để đánh thức: consume — caller chặn click/swipe tới
       lúc release, và activity của contact này mở một khoảng dim_idle mới. */
    return true;
}

void screen_power_set_mode_min(int min_percent)
{
    if (mode_min_percent == min_percent) return;
    mode_min_percent = min_percent;
    if (screen_dimmed) {
        screen_dimmed = false;
        display_set_backlight(effective_brightness());
    } else {
        display_set_backlight(effective_brightness());
    }
}

void screen_power_force_on(void)
{
    screen_dimmed = false;
    int b = backlight_percent < 40 ? 40 : backlight_percent;
    if (b < mode_min_percent) b = mode_min_percent;
    display_set_backlight(b);
}

bool screen_power_is_dimmed(void)
{
    return screen_dimmed;
}
