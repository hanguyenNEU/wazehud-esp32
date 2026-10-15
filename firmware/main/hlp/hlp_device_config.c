#include "hlp_device_config.h"
#include "nvs.h"
#include <stdlib.h>
#include <string.h>

#define CFG_COUNT 5
#define CFG_NS "hlp_cfg"

typedef struct {
    bool show_eta;
    int brightness;
    int offset;
    char theme[8];
    char label[21];
} device_config_t;

static device_config_t active = { true, 70, 0, "auto", "Waze HUD" };
static device_config_t draft;
static uint32_t revision = 1;
static uint32_t transaction;
static uint32_t received_mask;
static int received_count;
static int expected_count;

static void send_json(cJSON *object, hlp_send_line_t send, void *user) {
    if (!object || !send) { if (object) cJSON_Delete(object); return; }
    char *line = cJSON_PrintUnformatted(object);
    if (line) { send(line, user); free(line); }
    cJSON_Delete(object);
}

static cJSON *envelope(const char *type) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "v", 1);
    cJSON_AddStringToObject(root, "t", type);
    return root;
}

static void load_string(nvs_handle_t nvs, const char *key, char *out, size_t capacity) {
    size_t length = capacity;
    if (nvs_get_str(nvs, key, out, &length) != ESP_OK) out[capacity - 1] = 0;
}

void hlp_device_config_init(void) {
    nvs_handle_t nvs;
    if (nvs_open(CFG_NS, NVS_READONLY, &nvs) != ESP_OK) return;
    uint8_t eta;
    int32_t value;
    if (nvs_get_u8(nvs, "eta", &eta) == ESP_OK) active.show_eta = eta != 0;
    if (nvs_get_i32(nvs, "bright", &value) == ESP_OK) active.brightness = value;
    if (nvs_get_i32(nvs, "offset", &value) == ESP_OK) active.offset = value;
    (void)nvs_get_u32(nvs, "rev", &revision);
    load_string(nvs, "theme", active.theme, sizeof(active.theme));
    load_string(nvs, "label", active.label, sizeof(active.label));
    nvs_close(nvs);
}

static bool save_active(void) {
    nvs_handle_t nvs;
    if (nvs_open(CFG_NS, NVS_READWRITE, &nvs) != ESP_OK) return false;
    esp_err_t err = nvs_set_u8(nvs, "eta", active.show_eta ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_i32(nvs, "bright", active.brightness);
    if (err == ESP_OK) err = nvs_set_i32(nvs, "offset", active.offset);
    if (err == ESP_OK) err = nvs_set_str(nvs, "theme", active.theme);
    if (err == ESP_OK) err = nvs_set_str(nvs, "label", active.label);
    if (err == ESP_OK) err = nvs_set_u32(nvs, "rev", revision);
    if (err == ESP_OK) err = nvs_commit(nvs);
    nvs_close(nvs);
    return err == ESP_OK;
}

static cJSON *item(const char *id, const char *kind, const char *label) {
    cJSON *root = envelope("cfg_item");
    if (!root) return NULL;
    cJSON_AddNumberToObject(root, "rev", revision);
    cJSON_AddStringToObject(root, "id", id);
    cJSON_AddStringToObject(root, "kind", kind);
    cJSON_AddStringToObject(root, "label", label);
    return root;
}

void hlp_device_config_publish(hlp_send_line_t send, void *user) {
    cJSON *root = envelope("cfg_begin");
    cJSON_AddNumberToObject(root, "rev", revision);
    cJSON_AddNumberToObject(root, "count", CFG_COUNT);
    cJSON_AddStringToObject(root, "title", "Cấu hình thiết bị HUD");
    send_json(root, send, user);

    root = item("show_eta", "toggle", "Hiện thời gian đến");
    cJSON_AddBoolToObject(root, "value", active.show_eta);
    send_json(root, send, user);

    root = item("brightness", "slider", "Độ sáng");
    cJSON_AddNumberToObject(root, "value", active.brightness);
    cJSON_AddNumberToObject(root, "min", 10);
    cJSON_AddNumberToObject(root, "max", 100);
    cJSON_AddNumberToObject(root, "step", 5);
    send_json(root, send, user);

    root = item("theme", "selection", "Giao diện");
    cJSON_AddStringToObject(root, "value", active.theme);
    cJSON *options = cJSON_AddArrayToObject(root, "options");
    const char *values[] = { "auto", "day", "night" };
    const char *labels[] = { "Tự động", "Ban ngày", "Ban đêm" };
    for (int i = 0; i < 3; ++i) {
        cJSON *option = cJSON_CreateObject();
        cJSON_AddStringToObject(option, "value", values[i]);
        cJSON_AddStringToObject(option, "label", labels[i]);
        cJSON_AddItemToArray(options, option);
    }
    send_json(root, send, user);

    root = item("offset", "integer", "Hiệu chỉnh ngang");
    cJSON_AddNumberToObject(root, "value", active.offset);
    cJSON_AddNumberToObject(root, "min", -20);
    cJSON_AddNumberToObject(root, "max", 20);
    send_json(root, send, user);

    root = item("label", "text", "Tên hiển thị");
    cJSON_AddStringToObject(root, "value", active.label);
    cJSON_AddNumberToObject(root, "maxLength", 20);
    send_json(root, send, user);

    root = envelope("cfg_end");
    cJSON_AddNumberToObject(root, "rev", revision);
    send_json(root, send, user);
}

static void ack(hlp_send_line_t send, void *user, bool ok,
                const char *field, const char *error) {
    cJSON *root = envelope("cfg_ack");
    cJSON_AddNumberToObject(root, "tx", transaction);
    cJSON_AddBoolToObject(root, "ok", ok);
    if (ok) cJSON_AddNumberToObject(root, "rev", revision);
    if (field) cJSON_AddStringToObject(root, "field", field);
    if (error) cJSON_AddStringToObject(root, "error", error);
    send_json(root, send, user);
}

static bool string_value(const cJSON *value, char *out, size_t capacity) {
    if (!cJSON_IsString(value) || !value->valuestring
            || strlen(value->valuestring) >= capacity) return false;
    strcpy(out, value->valuestring);
    return true;
}

bool hlp_device_config_handle(const cJSON *root, hlp_send_line_t send, void *user) {
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "t");
    if (!cJSON_IsString(type)) return false;
    if (strcmp(type->valuestring, "hi") == 0) {
        hlp_device_config_publish(send, user);
        return true;
    }
    if (strcmp(type->valuestring, "cfg_set_begin") == 0) {
        cJSON *tx = cJSON_GetObjectItemCaseSensitive(root, "tx");
        cJSON *rev = cJSON_GetObjectItemCaseSensitive(root, "rev");
        cJSON *count = cJSON_GetObjectItemCaseSensitive(root, "count");
        if (!cJSON_IsNumber(tx) || !cJSON_IsNumber(rev) || !cJSON_IsNumber(count)) return true;
        transaction = (uint32_t)tx->valuedouble;
        expected_count = count->valueint;
        received_mask = 0;
        received_count = 0;
        draft = active;
        if ((uint32_t)rev->valuedouble != revision || expected_count != CFG_COUNT) {
            ack(send, user, false, NULL, "schema revision/count mismatch");
            transaction = 0;
        }
        return true;
    }
    if (strcmp(type->valuestring, "cfg_set") == 0) {
        cJSON *tx = cJSON_GetObjectItemCaseSensitive(root, "tx");
        cJSON *id = cJSON_GetObjectItemCaseSensitive(root, "id");
        cJSON *value = cJSON_GetObjectItemCaseSensitive(root, "value");
        if (!transaction || !cJSON_IsNumber(tx) || (uint32_t)tx->valuedouble != transaction
                || !cJSON_IsString(id) || !value) return true;
        const char *field = id->valuestring;
        bool valid = true;
        uint32_t mask_before = received_mask;
        if (strcmp(field, "show_eta") == 0) {
            valid = cJSON_IsBool(value); if (valid) draft.show_eta = cJSON_IsTrue(value);
            received_mask |= valid ? 1u << 0 : 0;
        } else if (strcmp(field, "brightness") == 0) {
            valid = cJSON_IsNumber(value) && value->valueint >= 10 && value->valueint <= 100
                    && ((value->valueint - 10) % 5) == 0;
            if (valid) draft.brightness = value->valueint;
            received_mask |= valid ? 1u << 1 : 0;
        } else if (strcmp(field, "theme") == 0) {
            valid = string_value(value, draft.theme, sizeof(draft.theme))
                    && (!strcmp(draft.theme, "auto") || !strcmp(draft.theme, "day")
                        || !strcmp(draft.theme, "night"));
            received_mask |= valid ? 1u << 2 : 0;
        } else if (strcmp(field, "offset") == 0) {
            valid = cJSON_IsNumber(value) && value->valueint >= -20 && value->valueint <= 20;
            if (valid) draft.offset = value->valueint;
            received_mask |= valid ? 1u << 3 : 0;
        } else if (strcmp(field, "label") == 0) {
            valid = string_value(value, draft.label, sizeof(draft.label));
            received_mask |= valid ? 1u << 4 : 0;
        } else valid = false;
        if (valid && received_mask == mask_before) valid = false; /* duplicate id */
        if (valid) received_count++;
        if (!valid) { ack(send, user, false, field, "invalid value"); transaction = 0; }
        return true;
    }
    if (strcmp(type->valuestring, "cfg_set_commit") == 0) {
        cJSON *tx = cJSON_GetObjectItemCaseSensitive(root, "tx");
        if (!transaction || !cJSON_IsNumber(tx) || (uint32_t)tx->valuedouble != transaction)
            return true;
        if (received_count != expected_count
                || received_mask != ((1u << CFG_COUNT) - 1u)) {
            ack(send, user, false, NULL, "incomplete transaction");
        } else {
            device_config_t previous = active;
            active = draft;
            revision++;
            if (save_active()) ack(send, user, true, NULL, NULL);
            else {
                active = previous;
                revision--;
                ack(send, user, false, NULL, "NVS write failed");
            }
        }
        transaction = 0;
        return true;
    }
    return false;
}
