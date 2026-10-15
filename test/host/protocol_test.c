#include "hlp_core.h"
#include "hlp_messages.h"
#include "hlp_device_config.h"
#include "cJSON.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char sent[HLP_MAX_FRAME];
static unsigned states;

bool hlp_device_config_handle(const cJSON *root, hlp_send_line_t send, void *user)
{
    (void)root;
    (void)send;
    (void)user;
    return false;
}

static void capture(const char *line, void *user)
{
    (void)user;
    assert(strlen(line) + 1 <= HLP_MAX_FRAME);
    strcpy(sent, line);
}

static void state(const char *line, size_t length, void *user)
{
    (void)line;
    (void)length;
    (void)user;
    ++states;
}

static void receive(const char *line, size_t length, void *user)
{
    hlp_handle_line(line, length, capture, state, user);
}

int main(void)
{
    hlp_send_dev(capture, NULL, "ble", "WazeHUD", "0.1.0", 4);
    cJSON *dev = cJSON_Parse(sent);
    assert(dev);
    cJSON *fields = cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(dev, "want"), "fields");
    const char *required[] = {"lan", "rm", "rkm", "alrs", "alrS", "alrM"};
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i) {
        bool found = false;
        cJSON *field;
        cJSON_ArrayForEach(field, fields) {
            if (cJSON_IsString(field) && !strcmp(field->valuestring, required[i]))
                found = true;
        }
        assert(found);
    }
    cJSON_Delete(dev);

    hlp_rx_t rx;
    hlp_rx_init(&rx, receive, NULL);
    const char ping[] = "{\"v\":1,\"t\":\"ping\"}\n";
    for (size_t i = 0; i < strlen(ping); ++i)
        hlp_rx_feed(&rx, (const uint8_t *)ping + i, 1);
    assert(!strcmp(sent, "{\"v\":1,\"t\":\"pong\"}"));

    const char partial[] = "{\"v\":1,\"t\":\"s\"";
    hlp_rx_feed(&rx, (const uint8_t *)partial, strlen(partial));
    /* A new BLE generation must discard the previous partial frame. */
    hlp_rx_init(&rx, receive, NULL);
    const char snapshot[] = "{\"v\":1,\"t\":\"s\",\"spd\":42}\n";
    hlp_rx_feed(&rx, (const uint8_t *)snapshot, strlen(snapshot));
    assert(states == 1);
    puts("Protocol: negotiation, fragmented ping, receiver reset passed");
    return 0;
}
