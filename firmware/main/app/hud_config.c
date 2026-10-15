/* Cấu hình thiết bị qua `cfg_*` — 6 mục thật của HUD.
 *
 * Giữ nguyên logic giao thức của bản mẫu trong SDK vì nó đã đúng: staging
 * riêng, kiểm tx/rev/count/id/kiểu/khoảng, và CHỈ ghi NVS sau `cfg_set_commit`
 * hợp lệ. Áp từng giá trị ngay khi nhận sẽ để thiết bị ở trạng thái nửa vời nếu
 * gói bị đứt giữa chừng.
 *
 * §6 kế hoạch UI — bổ sung so với bản cũ:
 *   - persist_candidate() dùng chung cho đường remote lẫn local.
 *   - Local patch qua queue SPSC hữu hạn (4), protocol task xử lý 1/vòng;
 *     queue đầy trả BUSY ngay, không block LVGL.
 *   - Remote begin ghi base_revision; commit kiểm lại base == active revision,
 *     lệch → ACK lỗi revision, bỏ staging.
 *   - Apply callback thay bằng publish snapshot; UI tự áp ở tick.
 *   - Kết quả local request đi result mailbox riêng với request_id; snapshot
 *     broadcast 1 slot (coalesce tự nhiên).
 */

#include "hud_config.h"
#include "hlp_device_config.h"

#include "cJSON.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>

#define CFG_NS "hud_cfg"
#define CFG_COUNT 6
#define LOCAL_QUEUE_DEPTH 4

/* Thứ tự này quyết định bit trong received_mask, đừng đổi. */
enum {
    F_BRIGHTNESS = 0,
    F_THEME,
    F_LAYOUT,
    F_ALERTS,
    F_ALERT_DIST,
    F_DIM_IDLE,
};

static hud_config_t active = {
    .brightness = 80,
    .theme = "auto",
    .layout = "waze",
    .alerts = true,
    .alert_dist = 800,
    .dim_idle = 5,
};
static hud_config_t draft;
static uint32_t revision = 1;
static uint32_t transaction;
static uint32_t received_mask;
static int received_count;
static int expected_count;
static uint32_t base_revision; /* rev lúc cfg_set_begin — kiểm lại lúc commit */
static hud_config_snapshot_cb_t snapshot_cb;
static hud_config_clock_cb_t clock_cb;

/* ---- Mailbox snapshot/result (1 slot; protocol task ghi, LVGL task đọc) ---- */
static hud_config_snapshot_t snap_slot;
static volatile uint32_t snap_valid;
static hud_config_result_t result_slot;
static volatile uint32_t result_valid;

/* ---- Local queue SPSC (UI task ghi, protocol task đọc) ---- */
static hud_config_patch_t local_queue[LOCAL_QUEUE_DEPTH];
static volatile uint32_t local_head;
static volatile uint32_t local_tail;

void hud_config_set_snapshot_cb(hud_config_snapshot_cb_t cb) { snapshot_cb = cb; }
void hud_config_set_clock_cb(hud_config_clock_cb_t cb) { clock_cb = cb; }

uint32_t hud_config_revision(void) { return revision; }
const hud_config_t *hud_config_get(void) { return &active; }

/* ---- Validation dùng chung remote + local ---- */

static bool enum_ok(const char *value, const char *const *allowed, int count)
{
    for (int i = 0; i < count; ++i)
        if (!strcmp(value, allowed[i])) return true;
    return false;
}

static bool value_valid(hud_config_field_t field, const hud_config_t *candidate)
{
    static const char *const themes[] = {"auto", "day", "night"};
    static const char *const layouts[] = {"waze", "speed"};
    switch (field) {
    case HUD_CONFIG_FIELD_BRIGHTNESS:
        return candidate->brightness >= 10 && candidate->brightness <= 100
               && (candidate->brightness - 10) % 5 == 0;
    case HUD_CONFIG_FIELD_THEME:
        return enum_ok(candidate->theme, themes, 3);
    case HUD_CONFIG_FIELD_LAYOUT:
        return enum_ok(candidate->layout, layouts, 2);
    case HUD_CONFIG_FIELD_ALERTS:
        return true; /* bool */
    case HUD_CONFIG_FIELD_ALERT_DIST:
        return candidate->alert_dist >= 200 && candidate->alert_dist <= 2000;
    case HUD_CONFIG_FIELD_DIM_IDLE:
        return candidate->dim_idle >= 0 && candidate->dim_idle <= 30;
    default:
        return false;
    }
}

static bool candidate_valid(const hud_config_t *candidate)
{
    for (int f = 0; f < HUD_CONFIG_FIELD_COUNT; ++f)
        if (!value_valid((hud_config_field_t)f, candidate)) return false;
    return true;
}

/* ---- Persist chung: ghi toàn bộ 6 key + rev. Active chỉ đổi khi trả true. ---- */

static bool persist_candidate(const hud_config_t *candidate, uint32_t next_revision)
{
    nvs_handle_t nvs;
    if (nvs_open(CFG_NS, NVS_READWRITE, &nvs) != ESP_OK) return false;
    esp_err_t err = nvs_set_i32(nvs, "bright", candidate->brightness);
    if (err == ESP_OK) err = nvs_set_str(nvs, "theme", candidate->theme);
    if (err == ESP_OK) err = nvs_set_str(nvs, "layout", candidate->layout);
    if (err == ESP_OK) err = nvs_set_u8(nvs, "alerts", candidate->alerts ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_i32(nvs, "alrdist", candidate->alert_dist);
    if (err == ESP_OK) err = nvs_set_i32(nvs, "dimidle", candidate->dim_idle);
    if (err == ESP_OK) err = nvs_set_u32(nvs, "rev", next_revision);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err == ESP_OK;
}

static void publish_snapshot(void)
{
    snap_slot.value = active;
    snap_slot.revision = revision;
    snap_valid = 1;
    if (snapshot_cb) snapshot_cb(&snap_slot);
}

bool hud_config_take_snapshot(hud_config_snapshot_t *out)
{
    if (!out || !snap_valid) return false;
    *out = snap_slot;
    snap_valid = 0;
    return true;
}

static void post_result(uint32_t request_id, hud_config_result_kind_t kind)
{
    result_slot.request_id = request_id;
    result_slot.kind = kind;
    result_slot.snapshot.value = active;
    result_slot.snapshot.revision = revision;
    result_valid = 1;
}

bool hud_config_take_result(hud_config_result_t *out)
{
    if (!out || !result_valid) return false;
    *out = result_slot;
    result_valid = 0;
    return true;
}

/* ---- Local patch ---- */

void hud_config_local_patch(const hud_config_patch_t *patch)
{
    if (!patch || patch->field < 0 || patch->field >= HUD_CONFIG_FIELD_COUNT) {
        post_result(patch ? patch->request_id : 0, HUD_CONFIG_INVALID);
        return;
    }
    uint32_t head = local_head;
    uint32_t tail = local_tail;
    if (head - tail >= LOCAL_QUEUE_DEPTH) {
        post_result(patch->request_id, HUD_CONFIG_BUSY);
        return;
    }
    local_queue[head % LOCAL_QUEUE_DEPTH] = *patch;
    local_head = head + 1;
}

void hud_config_process_local(void)
{
    if (local_tail == local_head) return;
    hud_config_patch_t patch = local_queue[local_tail % LOCAL_QUEUE_DEPTH];
    local_tail++;

    if (patch.expected_revision != revision) {
        post_result(patch.request_id, HUD_CONFIG_CONFLICT);
        return;
    }
    if (!value_valid(patch.field, &patch.value)) {
        post_result(patch.request_id, HUD_CONFIG_INVALID);
        return;
    }
    hud_config_t candidate = active;
    switch (patch.field) {
    case HUD_CONFIG_FIELD_BRIGHTNESS: candidate.brightness = patch.value.brightness; break;
    case HUD_CONFIG_FIELD_THEME:      memcpy(candidate.theme, patch.value.theme, sizeof(candidate.theme)); break;
    case HUD_CONFIG_FIELD_LAYOUT:     memcpy(candidate.layout, patch.value.layout, sizeof(candidate.layout)); break;
    case HUD_CONFIG_FIELD_ALERTS:     candidate.alerts = patch.value.alerts; break;
    case HUD_CONFIG_FIELD_ALERT_DIST: candidate.alert_dist = patch.value.alert_dist; break;
    case HUD_CONFIG_FIELD_DIM_IDLE:   candidate.dim_idle = patch.value.dim_idle; break;
    default: break;
    }
    if (!candidate_valid(&candidate)) {
        post_result(patch.request_id, HUD_CONFIG_INVALID);
        return;
    }
    /* Local patch hợp lệ commit ngay cả khi có remote staging — revision tăng
       đúng một lần; staging remote không đụng active cho tới commit của nó. */
    if (!persist_candidate(&candidate, revision + 1)) {
        post_result(patch.request_id, HUD_CONFIG_STORAGE_ERROR);
        return;
    }
    active = candidate;
    revision++;
    publish_snapshot();
    post_result(patch.request_id, HUD_CONFIG_OK);
}

/* ---- Giao thức SDK (remote) ---- */

static void send_json(cJSON *object, hlp_send_line_t send, void *user)
{
    if (!object || !send) {
        if (object) cJSON_Delete(object);
        return;
    }
    char *line = cJSON_PrintUnformatted(object);
    if (line) {
        send(line, user);
        free(line);
    }
    cJSON_Delete(object);
}

static cJSON *envelope(const char *type)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "t", type);
    return root;
}

static cJSON *item(const char *id, const char *kind, const char *label)
{
    cJSON *root = envelope("cfg_item");
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "rev", revision);
    cJSON_AddStringToObject(root, "id", id);
    cJSON_AddStringToObject(root, "kind", kind);
    cJSON_AddStringToObject(root, "label", label);
    return root;
}

static void load_string(nvs_handle_t nvs, const char *key, char *out, size_t cap)
{
    size_t length = cap;
    char tmp[16];
    if (cap > sizeof(tmp)) return;
    if (nvs_get_str(nvs, key, tmp, &length) == ESP_OK) {
        tmp[cap - 1] = '\0';
        strcpy(out, tmp);
    }
}

void hlp_device_config_init(void)
{
    nvs_handle_t nvs;
    if (nvs_open(CFG_NS, NVS_READONLY, &nvs) == ESP_OK) {
        int32_t value;
        uint8_t flag;
        if (nvs_get_i32(nvs, "bright", &value) == ESP_OK) active.brightness = value;
        if (nvs_get_i32(nvs, "alrdist", &value) == ESP_OK) active.alert_dist = value;
        if (nvs_get_i32(nvs, "dimidle", &value) == ESP_OK) active.dim_idle = value;
        if (nvs_get_u8(nvs, "alerts", &flag) == ESP_OK) active.alerts = flag != 0;
        (void)nvs_get_u32(nvs, "rev", &revision);
        load_string(nvs, "theme", active.theme, sizeof(active.theme));
        load_string(nvs, "layout", active.layout, sizeof(active.layout));
        nvs_close(nvs);
    }
    /* Publish snapshot ngay lúc khởi động, trước cả khi có điện thoại. */
    publish_snapshot();
}

static void add_options(cJSON *root, const char *const *values,
                        const char *const *labels, int count)
{
    cJSON *options = cJSON_AddArrayToObject(root, "options");
    for (int i = 0; i < count; ++i) {
        cJSON *option = cJSON_CreateObject();
        cJSON_AddStringToObject(option, "value", values[i]);
        cJSON_AddStringToObject(option, "label", labels[i]);
        cJSON_AddItemToArray(options, option);
    }
}

void hlp_device_config_publish(hlp_send_line_t send, void *user)
{
    cJSON *root = envelope("cfg_begin");
    cJSON_AddNumberToObject(root, "rev", revision);
    cJSON_AddNumberToObject(root, "count", CFG_COUNT);
    cJSON_AddStringToObject(root, "title", "Cấu hình HUD");
    send_json(root, send, user);

    root = item("brightness", "slider", "Độ sáng");
    cJSON_AddNumberToObject(root, "value", active.brightness);
    cJSON_AddNumberToObject(root, "min", 10);
    cJSON_AddNumberToObject(root, "max", 100);
    cJSON_AddNumberToObject(root, "step", 5);
    send_json(root, send, user);

    root = item("theme", "selection", "Giao diện");
    cJSON_AddStringToObject(root, "value", active.theme);
    static const char *const theme_v[] = {"auto", "day", "night"};
    static const char *const theme_l[] = {"Tự động theo giờ", "Ban ngày", "Ban đêm"};
    add_options(root, theme_v, theme_l, 3);
    send_json(root, send, user);

    root = item("layout", "selection", "Bố cục");
    cJSON_AddStringToObject(root, "value", active.layout);
    static const char *const layout_v[] = {"waze", "speed"};
    static const char *const layout_l[] = {"Vòng tròn (Waze)", "Dải ngang (số to)"};
    add_options(root, layout_v, layout_l, 2);
    send_json(root, send, user);

    root = item("alerts", "toggle", "Hiện cảnh báo");
    cJSON_AddBoolToObject(root, "value", active.alerts);
    cJSON_AddStringToObject(root, "description", "Camera, cảnh sát, biển báo");
    send_json(root, send, user);

    root = item("alert_dist", "integer", "Ngưỡng cảnh báo (m)");
    cJSON_AddNumberToObject(root, "value", active.alert_dist);
    cJSON_AddNumberToObject(root, "min", 200);
    cJSON_AddNumberToObject(root, "max", 2000);
    send_json(root, send, user);

    root = item("dim_idle", "integer", "Tắt màn sau (phút)");
    cJSON_AddNumberToObject(root, "value", active.dim_idle);
    cJSON_AddNumberToObject(root, "min", 0);
    cJSON_AddNumberToObject(root, "max", 30);
    cJSON_AddStringToObject(root, "description", "0 = không bao giờ tắt");
    send_json(root, send, user);

    root = envelope("cfg_end");
    cJSON_AddNumberToObject(root, "rev", revision);
    send_json(root, send, user);
}

static void ack(hlp_send_line_t send, void *user, bool ok, const char *field,
                const char *error)
{
    cJSON *root = envelope("cfg_ack");
    cJSON_AddNumberToObject(root, "tx", transaction);
    cJSON_AddBoolToObject(root, "ok", ok);
    if (ok) cJSON_AddNumberToObject(root, "rev", revision);
    if (field) cJSON_AddStringToObject(root, "field", field);
    if (error) cJSON_AddStringToObject(root, "error", error);
    send_json(root, send, user);
}

static bool enum_value(const cJSON *value, char *out, size_t cap,
                       const char *const *allowed, int count)
{
    if (!cJSON_IsString(value) || !value->valuestring
        || strlen(value->valuestring) >= cap)
        return false;
    for (int i = 0; i < count; ++i)
        if (!strcmp(value->valuestring, allowed[i])) {
            strcpy(out, value->valuestring);
            return true;
        }
    return false;
}

static bool in_range(const cJSON *value, int lo, int hi)
{
    return cJSON_IsNumber(value) && value->valueint >= lo && value->valueint <= hi;
}

bool hlp_device_config_handle(const cJSON *root, hlp_send_line_t send, void *user)
{
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "t");
    if (!cJSON_IsString(type)) return false;

    if (!strcmp(type->valuestring, "hi")) {
        const cJSON *unix_s = cJSON_GetObjectItemCaseSensitive(root, "unix");
        const cJSON *tz = cJSON_GetObjectItemCaseSensitive(root, "tz");
        if (clock_cb && cJSON_IsNumber(unix_s))
            clock_cb((int64_t)unix_s->valuedouble,
                     cJSON_IsNumber(tz) ? tz->valueint : 0);
        /* `hi` có thể lặp khi kết nối lại — công bố phải idempotent. */
        hlp_device_config_publish(send, user);
        return true;
    }

    if (!strcmp(type->valuestring, "cfg_set_begin")) {
        cJSON *tx = cJSON_GetObjectItemCaseSensitive(root, "tx");
        cJSON *rev = cJSON_GetObjectItemCaseSensitive(root, "rev");
        cJSON *count = cJSON_GetObjectItemCaseSensitive(root, "count");
        if (!cJSON_IsNumber(tx) || !cJSON_IsNumber(rev) || !cJSON_IsNumber(count))
            return true;
        transaction = (uint32_t)tx->valuedouble;
        expected_count = count->valueint;
        received_mask = 0;
        received_count = 0;
        draft = active;
        base_revision = (uint32_t)rev->valuedouble;
        if (base_revision != revision || expected_count != CFG_COUNT) {
            ack(send, user, false, NULL, "schema revision/count mismatch");
            transaction = 0;
        }
        return true;
    }

    if (!strcmp(type->valuestring, "cfg_set")) {
        cJSON *tx = cJSON_GetObjectItemCaseSensitive(root, "tx");
        cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
        cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");
        if (!transaction || !cJSON_IsNumber(tx)
            || (uint32_t)tx->valuedouble != transaction || !cJSON_IsString(id)
            || !value)
            return true;

        const char *field = id->valuestring;
        uint32_t mask_before = received_mask;
        bool valid = true;

        if (!strcmp(field, "brightness")) {
            valid = in_range(value, 10, 100) && ((value->valueint - 10) % 5) == 0;
            if (valid) draft.brightness = value->valueint;
            received_mask |= valid ? 1u << F_BRIGHTNESS : 0;
        } else if (!strcmp(field, "theme")) {
            static const char *const allowed[] = {"auto", "day", "night"};
            valid = enum_value(value, draft.theme, sizeof(draft.theme), allowed, 3);
            received_mask |= valid ? 1u << F_THEME : 0;
        } else if (!strcmp(field, "layout")) {
            static const char *const allowed[] = {"waze", "speed"};
            valid = enum_value(value, draft.layout, sizeof(draft.layout), allowed, 2);
            received_mask |= valid ? 1u << F_LAYOUT : 0;
        } else if (!strcmp(field, "alerts")) {
            valid = cJSON_IsBool(value);
            if (valid) draft.alerts = cJSON_IsTrue(value);
            received_mask |= valid ? 1u << F_ALERTS : 0;
        } else if (!strcmp(field, "alert_dist")) {
            valid = in_range(value, 200, 2000);
            if (valid) draft.alert_dist = value->valueint;
            received_mask |= valid ? 1u << F_ALERT_DIST : 0;
        } else if (!strcmp(field, "dim_idle")) {
            valid = in_range(value, 0, 30);
            if (valid) draft.dim_idle = value->valueint;
            received_mask |= valid ? 1u << F_DIM_IDLE : 0;
        } else {
            valid = false;
        }

        if (valid && received_mask == mask_before) valid = false; /* id trùng */
        if (valid) received_count++;
        if (!valid) {
            ack(send, user, false, field, "invalid value");
            transaction = 0;
        }
        return true;
    }

    if (!strcmp(type->valuestring, "cfg_set_commit")) {
        cJSON *tx = cJSON_GetObjectItemCaseSensitive(root, "tx");
        if (!transaction || !cJSON_IsNumber(tx)
            || (uint32_t)tx->valuedouble != transaction)
            return true;

        if (received_count != expected_count
            || received_mask != ((1u << CFG_COUNT) - 1u)) {
            ack(send, user, false, NULL, "incomplete transaction");
        } else if (base_revision != revision) {
            /* Local patch (hoặc phiên remote khác) đã commit giữa begin và
               commit — bỏ staging, điện thoại có thể hỏi lại schema. */
            ack(send, user, false, NULL, "revision changed mid-transaction");
        } else if (!candidate_valid(&draft)) {
            ack(send, user, false, NULL, "invalid candidate");
        } else if (!persist_candidate(&draft, revision + 1)) {
            /* Ghi NVS hỏng ⇒ RAM giữ nguyên active/revision cũ, không apply. */
            ack(send, user, false, NULL, "NVS write failed");
        } else {
            active = draft;
            revision++;
            ack(send, user, true, NULL, NULL);
            publish_snapshot();
        }
        transaction = 0;
        return true;
    }

    return false;
}
