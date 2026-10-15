#pragma once

#include "hlp_messages.h"
#include "cJSON.h"
#include <stdbool.h>

/* Reference editable device configuration. Replace the five demo fields with product settings. */
void hlp_device_config_init(void);
void hlp_device_config_publish(hlp_send_line_t send, void *user);
bool hlp_device_config_handle(const cJSON *root, hlp_send_line_t send, void *user);
