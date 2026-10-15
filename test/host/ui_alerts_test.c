/* Include the renderer to inspect its slots without exposing test-only firmware APIs. */
#include "../../firmware/main/ui/ui_navigator.c"
#include "hud_decode.h"
#include <assert.h>

int main(void)
{
    lv_init();
    lv_display_create(320, 240);
    ui_create();
    ui_set_link(UI_LINK_LIVE);
    ui_set_layout(UI_LAYOUT_WAZE);
    hud_state_t s;
    const char *line = "{\"v\":1,\"t\":\"s\",\"lim\":60,\"alrs\":["
        "{\"k\":8,\"d\":100,\"v\":80},{\"k\":8,\"d\":200,\"v\":125},"
        "{\"k\":2,\"d\":300},{\"k\":3,\"d\":400}]}";
    assert(hud_decode_state(line, strlen(line), &s));
    ui_update(&s);
    for (int i = 0; i < HUD_VISIBLE_ALERTS; ++i) {
        assert(!lv_obj_has_flag(ui.alert_distances[i], LV_OBJ_FLAG_HIDDEN));
        assert(!lv_obj_has_flag(ui.alert_images[i], LV_OBJ_FLAG_HIDDEN)
            || !lv_obj_has_flag(ui.alert_fallback[i], LV_OBJ_FLAG_HIDDEN));
    }
    assert(strstr(lv_label_get_text(ui.alert_details[0]), "Tăng") != NULL);
    assert(strcmp(lv_label_get_text(ui.alert_fallback[1]), "125") == 0);
    assert(lv_color_eq(lv_obj_get_style_text_color(ui.alert_fallback[1], 0), lv_color_black()));
    assert(lv_color_eq(lv_obj_get_style_bg_color(ui.alert_fallback[1], 0), lv_color_white()));
    for (int theme = UI_THEME_DAY; theme <= UI_THEME_NIGHT; ++theme) {
        ui_set_theme(theme);
        ui_update(&s);
        assert(lv_color_eq(lv_obj_get_style_text_color(ui.speed_unit, 0), lv_color_white()));
        assert(!lv_color_eq(palette()->muted, lv_color_white()));
    }
    s.alert_count = 1;
    s.alerts[0].value = 40;
    ui_update(&s);
    assert(strstr(lv_label_get_text(ui.alert_details[0]), "Giảm") != NULL);
    assert(lv_obj_has_flag(ui.alert_fallback[1], LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(ui.alert_images[2], LV_OBJ_FLAG_HIDDEN));
    s.alerts[0].value = 60;
    ui_update(&s);
    assert(strstr(lv_label_get_text(ui.alert_details[0]), "Không đổi") != NULL);
    s.alert_count = 0;
    ui_update(&s);
    for (int i = 0; i < HUD_VISIBLE_ALERTS; ++i) {
        assert(lv_obj_has_flag(ui.alert_images[i], LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(ui.alert_fallback[i], LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(ui.alert_distances[i], LV_OBJ_FLAG_HIDDEN));
        assert(lv_obj_has_flag(ui.alert_details[i], LV_OBJ_FLAG_HIDDEN));
    }
    puts("UI: three slots, numeric fallback, directions, text/sign contrast and clearing passed");
    return 0;
}
