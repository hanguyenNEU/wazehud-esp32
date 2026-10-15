#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HLP_MAX_FRAME 512
#define HLP_MAX_PAYLOAD (HLP_MAX_FRAME - 1)

typedef void (*hlp_line_cb_t)(const char *line, size_t length, void *user);

typedef struct {
    uint8_t buffer[HLP_MAX_FRAME]; /* one extra byte for a temporary NUL */
    size_t length;
    bool overflow;
    uint32_t oversized;
    uint32_t malformed_utf8;
    hlp_line_cb_t callback;
    void *user;
} hlp_rx_t;

void hlp_rx_init(hlp_rx_t *rx, hlp_line_cb_t callback, void *user);
void hlp_rx_feed(hlp_rx_t *rx, const uint8_t *data, size_t length);
bool hlp_utf8_valid(const uint8_t *data, size_t length);
