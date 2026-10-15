#pragma once
#include <stddef.h>

typedef void (*hlp_send_line_t)(const char *line, void *user);
typedef void (*hlp_state_line_t)(const char *line, size_t length, void *user);

void hlp_send_dev(hlp_send_line_t send, void *user, const char *transport,
                  const char *model, const char *firmware, unsigned rate);
void hlp_handle_line(const char *line, size_t length, hlp_send_line_t send,
                     hlp_state_line_t state, void *user);
