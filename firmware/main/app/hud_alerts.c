#include "hud_alerts.h"

static bool same_alert(const hud_alert_entry_t *a, const hud_alert_entry_t *b)
{
    return a->kind == b->kind && a->distance == b->distance
        && a->has_value == b->has_value && a->value == b->value
        && a->traffic_severity == b->traffic_severity
        && a->traffic_delay_min == b->traffic_delay_min;
}

int hud_select_alerts(const hud_state_t *s, bool enabled, int max_distance,
                      hud_alert_entry_t out[HUD_VISIBLE_ALERTS])
{
    int count = 0;
    if (s->avg_zone) {
        out[count++] = (hud_alert_entry_t){.kind = HUD_ALERT_NO_PASSING,
            .distance = s->avg_left, .traffic_delay_min = -1};
    }
    if (!enabled) return count;

    hud_alert_entry_t candidates[HUD_ALERTS_MAX];
    int candidate_count = 0;
    if (s->alerts_present) {
        for (int i = 0; i < s->alert_count && i < HUD_ALERTS_MAX; ++i)
            candidates[candidate_count++] = s->alerts[i];
    } else {
        candidates[candidate_count++] = (hud_alert_entry_t){
            .kind = s->alert, .distance = s->alert_distance,
            .value = s->alert_value, .has_value = s->alert_has_value,
            .traffic_severity = s->alert_traffic_severity,
            .traffic_delay_min = s->alert_traffic_delay_min};
    }
    /* Preserve producer order except simultaneous speed drops, as in the reference. */
    for (int i = 1; i < candidate_count; ++i) {
        hud_alert_entry_t value = candidates[i];
        int j = i;
        while (j > 0 && value.kind == HUD_ALERT_SPEED_DROP
               && candidates[j - 1].kind == value.kind
               && candidates[j - 1].distance == value.distance
               && candidates[j - 1].value < value.value) {
            candidates[j] = candidates[j - 1];
            --j;
        }
        candidates[j] = value;
    }
    for (int i = 0; i < candidate_count && count < HUD_VISIBLE_ALERTS; ++i) {
        hud_alert_entry_t *a = &candidates[i];
        if (a->kind <= HUD_ALERT_NONE || a->distance > max_distance
            || (s->avg_zone && a->kind == HUD_ALERT_NO_PASSING)) continue;
        bool duplicate = false;
        for (int j = 0; j < count; ++j)
            if (same_alert(a, &out[j])) duplicate = true;
        if (!duplicate) out[count++] = *a;
    }
    return count;
}
