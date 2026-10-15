#include "hud_decode.h"

#include "cJSON.h"
#include <string.h>

void hud_state_reset(hud_state_t *state)
{
    memset(state, 0, sizeof(*state));
    /* §6: `-1` nghĩa là không có với dst/alrD, vì 0 mét vẫn là khoảng cách hợp
       lệ. Các field khác dùng `0`, đã do memset lo. */
    state->distance = -1;
    state->alert_distance = -1;
    state->alert_traffic_delay_min = -1;
}

bool hud_turn_known(int turn)
{
    return turn > HUD_TURN_NONE && turn < HUD_TURN_MAX;
}

bool hud_alert_known(int alert)
{
    return alert > HUD_ALERT_NONE && alert < HUD_ALERT_KNOWN_MAX;
}

int hud_remaining_metres(const hud_state_t *state)
{
    return state->remain_m;
}

static int get_int(const cJSON *root, const char *key, int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    return cJSON_IsNumber(item) ? (int)item->valuedouble : fallback;
}

static bool get_bool(const cJSON *root, const char *key)
{
    return get_int(root, key, 0) != 0;
}

static void get_string(const cJSON *root, const char *key, char *out, size_t size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(item) || !item->valuestring) return;

    size_t length = strlen(item->valuestring);
    if (length >= size) {
        /* Cắt cho vừa buffer nhưng không được cắt giữa một ký tự UTF-8, nếu
           không màn hình sẽ hiện ký tự rác ở cuối tên đường. */
        length = size - 1;
        while (length > 0 && (item->valuestring[length] & 0xC0) == 0x80) length--;
    }
    memcpy(out, item->valuestring, length);
    out[length] = '\0';
}

static void decode_lanes(const cJSON *root, hud_state_t *state)
{
    const cJSON *lanes = cJSON_GetObjectItemCaseSensitive(root, "lan");
    if (!cJSON_IsArray(lanes)) return;

    /* Mảng rỗng là tín hiệu xoá chỉ dẫn cũ, khác hẳn với việc thiếu key. */
    state->lanes_present = true;

    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, lanes) {
        if (state->lane_count >= HUD_LANES_MAX) break;
        if (!cJSON_IsArray(entry) || cJSON_GetArraySize(entry) < 2) continue;

        const cJSON *directions = cJSON_GetArrayItem(entry, 0);
        const cJSON *selected = cJSON_GetArrayItem(entry, 1);
        if (!cJSON_IsNumber(directions) || !cJSON_IsNumber(selected)) continue;

        state->lanes[state->lane_count].directions = (uint8_t)directions->valuedouble;
        state->lanes[state->lane_count].selected = (uint8_t)selected->valuedouble;
        state->lane_count++;
    }
}

static void decode_alerts(const cJSON *root, hud_state_t *state)
{
    const cJSON *alerts = cJSON_GetObjectItemCaseSensitive(root, "alrs");
    if (!cJSON_IsArray(alerts)) return;

    state->alerts_present = true;

    const cJSON *entry = NULL;
    cJSON_ArrayForEach(entry, alerts) {
        if (state->alert_count >= HUD_ALERTS_MAX) break;
        if (!cJSON_IsObject(entry)) continue;

        hud_alert_entry_t *slot = &state->alerts[state->alert_count];
        /* Trong mảng, key loại là `k` — KHÔNG phải `t`, vì `t` là loại message
           ở envelope. */
        slot->kind = get_int(entry, "k", 0);
        slot->distance = get_int(entry, "d", -1);

        const cJSON *value = cJSON_GetObjectItemCaseSensitive(entry, "v");
        slot->has_value = cJSON_IsNumber(value);
        slot->value = slot->has_value ? (int)value->valuedouble : 0;
        slot->traffic_severity = slot->kind == HUD_ALERT_TRAFFIC_JAM
            ? get_int(entry, "s", 0) : 0;
        if (slot->traffic_severity < 1 || slot->traffic_severity > 5)
            slot->traffic_severity = 0;
        slot->traffic_delay_min = slot->kind == HUD_ALERT_TRAFFIC_JAM
            ? get_int(entry, "m", -1) : -1;
        if (slot->traffic_delay_min < 0) slot->traffic_delay_min = -1;

        state->alert_count++;
    }
}

bool hud_decode_state(const char *line, size_t length, hud_state_t *state)
{
    (void)length; /* hlp_core giao chuỗi đã có NUL và bị chặn độ dài */

    cJSON *root = cJSON_Parse(line);
    if (!root) return false;

    bool ok = false;
    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "t");

    /* §2: bỏ qua message có `v` không hỗ trợ và loại `t` chưa biết. */
    if (!cJSON_IsObject(root) || !cJSON_IsNumber(version) || version->valueint != 1
        || !cJSON_IsString(type) || strcmp(type->valuestring, "s") != 0) {
        goto done;
    }

    /* Bắt buộc: Android bỏ hẳn field đang ở giá trị mặc định để tiết kiệm
       budget 512 byte, nên phải dựng lại từ đầu chứ không vá lên struct cũ. */
    hud_state_reset(state);

    state->nav = get_bool(root, "nav");
    state->speed = get_int(root, "spd", 0);
    state->limit = get_int(root, "lim", 0);
    state->over = get_bool(root, "over");
    state->turn = get_int(root, "trn", 0);
    state->turn_next = get_int(root, "trn2", 0);
    state->distance = get_int(root, "dst", -1);
    state->exit = get_int(root, "exit", 0);

    get_string(root, "st", state->street, sizeof(state->street));
    get_string(root, "st2", state->street_next, sizeof(state->street_next));
    get_string(root, "eta", state->eta, sizeof(state->eta));

    state->remain_min = get_int(root, "rmin", 0);

    /* `rm` là giá trị mét chính xác và có thẩm quyền; `rkm` chỉ để tương thích
       với producer cũ không thương lượng `rm`. */
    const cJSON *remain_m = cJSON_GetObjectItemCaseSensitive(root, "rm");
    if (cJSON_IsNumber(remain_m)) {
        state->remain_m = (int)remain_m->valuedouble;
    } else {
        const cJSON *remain_km = cJSON_GetObjectItemCaseSensitive(root, "rkm");
        if (cJSON_IsNumber(remain_km))
            state->remain_m = (int)(remain_km->valuedouble * 1000.0 + 0.5);
    }

    state->avg_zone = get_bool(root, "avg");
    state->avg_left = get_int(root, "avgL", 0);
    state->avg_speed = get_int(root, "avgR", 0);
    state->avg_pct = get_int(root, "avgP", 0);

    state->alert = get_int(root, "alr", 0);
    state->alert_distance = get_int(root, "alrD", -1);
    const cJSON *alert_value = cJSON_GetObjectItemCaseSensitive(root, "alrV");
    state->alert_has_value = cJSON_IsNumber(alert_value);
    state->alert_value = state->alert_has_value ? (int)alert_value->valuedouble : 0;
    state->alert_traffic_severity = state->alert == HUD_ALERT_TRAFFIC_JAM
        ? get_int(root, "alrS", 0) : 0;
    if (state->alert_traffic_severity < 1 || state->alert_traffic_severity > 5)
        state->alert_traffic_severity = 0;
    state->alert_traffic_delay_min = state->alert == HUD_ALERT_TRAFFIC_JAM
        ? get_int(root, "alrM", -1) : -1;
    if (state->alert_traffic_delay_min < 0) state->alert_traffic_delay_min = -1;

    /* `ts` là uptime ms, có thể vượt INT_MAX sau ~25 ngày producer chạy liên
       tục, nên đọc qua double rồi mới thu về uint32. */
    const cJSON *ts = cJSON_GetObjectItemCaseSensitive(root, "ts");
    if (cJSON_IsNumber(ts) && ts->valuedouble > 0)
        state->ts = (uint32_t)ts->valuedouble;

    decode_lanes(root, state);
    decode_alerts(root, state);

    /* `alr`/`alrD`/`alrV` mirror `alrs[0]`. Khi producer phải cắt bớt vì giới
       hạn 512 byte, ba field đầu có thể vắng trong khi mảng vẫn còn — bù lại
       để renderer chỉ cần đọc một chỗ. */
    if (state->alert == HUD_ALERT_NONE && state->alert_count > 0) {
        state->alert = state->alerts[0].kind;
        state->alert_distance = state->alerts[0].distance;
        state->alert_has_value = state->alerts[0].has_value;
        state->alert_value = state->alerts[0].value;
        state->alert_traffic_severity = state->alerts[0].traffic_severity;
        state->alert_traffic_delay_min = state->alerts[0].traffic_delay_min;
    }

    ok = true;

done:
    cJSON_Delete(root);
    return ok;
}
