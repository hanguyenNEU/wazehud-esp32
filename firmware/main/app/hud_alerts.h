#pragma once
#include "hud_state.h"

#define HUD_VISIBLE_ALERTS 3
int hud_select_alerts(const hud_state_t *state, bool enabled, int max_distance,
                      hud_alert_entry_t out[HUD_VISIBLE_ALERTS]);
