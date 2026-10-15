#include "hud_alerts.h"
#include "hud_decode.h"
#include "../../firmware/main/ui/ui_icons.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static hud_state_t decode(const char *line)
{
    hud_state_t state;
    assert(hud_decode_state(line, strlen(line), &state));
    return state;
}

int main(void)
{
    hud_alert_entry_t out[HUD_VISIBLE_ALERTS];
    assert(HUD_VISIBLE_ALERTS == 3);
    assert(strcmp(ui_alert_name(HUD_ALERT_SPEED_DROP), "Giới hạn tốc độ tiếp theo") == 0);
    assert(strcmp(ui_limit_change(60, 80), "Tăng") == 0);
    assert(strcmp(ui_limit_change(60, 40), "Giảm") == 0);
    assert(strcmp(ui_limit_change(60, 60), "Không đổi") == 0);
    assert(strcmp(ui_limit_change(-1, 60), "") == 0);
    assert(strcmp(ui_limit_change(0, 60), "") == 0);
    assert(strcmp(ui_limit_change(60, 0), "") == 0);
    hud_state_t s = decode("{\"v\":1,\"t\":\"s\",\"alr\":1,\"alrD\":10,\"alrs\":[]}");
    assert(hud_select_alerts(&s, true, 800, out) == 0);
    s = decode("{\"v\":1,\"t\":\"s\",\"alrs\":[{\"k\":8,\"d\":200,\"v\":40},"
               "{\"k\":8,\"d\":200,\"v\":60},{\"k\":8,\"d\":200,\"v\":60},{\"k\":2,\"d\":900}]}");
    assert(hud_select_alerts(&s, true, 800, out) == 2);
    assert(out[0].value == 60 && out[1].value == 40);
    s = decode("{\"v\":1,\"t\":\"s\",\"avg\":1,\"avgL\":1200,"
               "\"alrs\":[{\"k\":9,\"d\":1200},{\"k\":2,\"d\":300},{\"k\":3,\"d\":500}]}");
    assert(hud_select_alerts(&s, true, 800, out) == 3);
    assert(out[0].kind == 9 && out[1].kind == 2 && out[2].kind == 3);
    assert(hud_select_alerts(&s, false, 800, out) == 1);
    s = decode("{\"v\":1,\"t\":\"s\",\"alr\":6,\"alrS\":4,\"alrM\":8}");
    assert(hud_select_alerts(&s, true, 800, out) == 1);
    assert(out[0].distance == -1 && out[0].traffic_severity == 4 && out[0].traffic_delay_min == 8);
    s = decode("{\"v\":1,\"t\":\"s\",\"alr\":1,\"alrs\":[{\"k\":6,\"d\":0,\"s\":5,\"m\":0}]}");
    assert(hud_select_alerts(&s, true, 800, out) == 1);
    assert(out[0].kind == 6 && out[0].traffic_severity == 5 && out[0].traffic_delay_min == 0);
    s = decode("{\"v\":1,\"t\":\"s\",\"alrs\":[{\"k\":6,\"s\":9,\"m\":-5},{\"k\":2,\"s\":4,\"m\":8}]}");
    assert(hud_select_alerts(&s, true, 800, out) == 2);
    assert(out[0].traffic_severity == 0 && out[0].traffic_delay_min == -1);
    assert(out[1].traffic_severity == 0 && out[1].traffic_delay_min == -1);
    /* Four distinct incoming alerts occupy exactly three slots, in producer order. */
    s = decode("{\"v\":1,\"t\":\"s\",\"lim\":60,\"alrs\":[{\"k\":8,\"d\":100,\"v\":80},"
               "{\"k\":8,\"d\":200,\"v\":40},{\"k\":8,\"d\":300,\"v\":60},{\"k\":2,\"d\":400}]}");
    assert(hud_select_alerts(&s, true, 800, out) == 3);
    assert(out[0].value == 80 && out[1].value == 40 && out[2].value == 60);
    assert(strcmp(ui_limit_change(s.limit, out[0].value), "Tăng") == 0);
    assert(strcmp(ui_limit_change(s.limit, out[1].value), "Giảm") == 0);
    assert(strcmp(ui_limit_change(s.limit, out[2].value), "Không đổi") == 0);
    assert(hud_select_alerts(&s, true, 200, out) == 2);
    assert(hud_select_alerts(&s, false, 800, out) == 0);
    s = decode("{\"v\":1,\"t\":\"s\",\"alr\":8,\"alrV\":80,\"alrD\":100,\"alrs\":[]}");
    assert(hud_select_alerts(&s, true, 800, out) == 0);
    s = decode("{\"v\":1,\"t\":\"s\",\"alr\":8,\"alrV\":80,\"alrD\":100}");
    assert(hud_select_alerts(&s, true, 800, out) == 1);
    assert(out[0].has_value && out[0].value == 80);
    s = decode("{\"v\":1,\"t\":\"s\",\"alr\":8,\"alrD\":100}");
    assert(hud_select_alerts(&s, true, 800, out) == 1);
    assert(!out[0].has_value);
    s = decode("{\"v\":1,\"t\":\"s\"}");
    assert(hud_select_alerts(&s, true, 800, out) == 0);
    puts("Alerts: three-slot cap, clear, next-limit directions, canonical list, ties, duplicates, zone, range and traffic details passed");
    return 0;
}
