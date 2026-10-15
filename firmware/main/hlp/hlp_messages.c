#include "hlp_messages.h"
#include "hlp_device_config.h"
#include "cJSON.h"
#include <stdio.h>
#include <string.h>

void hlp_send_dev(hlp_send_line_t send, void *user, const char *transport,
                  const char *model, const char *firmware, unsigned rate) {
    if (!send) return;
    char line[480];
    snprintf(line, sizeof(line),
             "{\"v\":1,\"t\":\"dev\",\"name\":\"%s\",\"fw\":\"%s\","
             "\"proto\":[1],\"want\":{\"rate\":%u,\"fields\":[\"nav\",\"spd\",\"lim\","
             "\"over\",\"trn\",\"trn2\",\"dst\",\"exit\",\"st\",\"st2\",\"eta\","
             "\"rmin\",\"rkm\",\"rm\",\"lan\",\"avg\",\"avgL\",\"avgR\",\"avgP\",\"alr\",\"alrD\","
             "\"alrV\",\"alrS\",\"alrM\",\"alrs\"]},\"can\":[\"speed\",\"limit\",\"turn\",\"street\","
             "\"eta\",\"avgzone\",\"alerts\"],\"transport\":\"%s\"}",
             model ? model : "ESP32", firmware ? firmware : "0.0.0", rate,
             transport ? transport : "unknown");
    send(line, user);
}

void hlp_handle_line(const char *line, size_t length, hlp_send_line_t send,
                     hlp_state_line_t state, void *user) {
    (void)length; // hlp_core supplies a NUL-terminated bounded line.
    cJSON *root = cJSON_Parse(line);
    if (!root || !cJSON_IsObject(root)) {
        if (root) cJSON_Delete(root);
        return;
    }
    cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "v");
    cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "t");
    if (!cJSON_IsNumber(version) || version->valueint != 1 || !cJSON_IsString(type)) {
        cJSON_Delete(root);
        return;
    }
    if (hlp_device_config_handle(root, send, user)) {
        /* Dynamic config messages are consumed by the device-config module. */
    } else if (strcmp(type->valuestring, "ping") == 0 && send) {
        send("{\"v\":1,\"t\":\"pong\"}", user);
    } else if (strcmp(type->valuestring, "s") == 0 && state) {
        state(line, length, user);
    }
    cJSON_Delete(root);
}
