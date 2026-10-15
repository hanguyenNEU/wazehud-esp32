#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Gọi từ protocol task với mỗi message trạng thái `s` hợp lệ.
   Chuỗi đã kết thúc bằng NUL và chỉ sống trong lúc callback chạy. */
typedef void (*hlp_ble_state_cb_t)(const char *line, size_t length,
                                   uint32_t generation, void *user);

void hlp_ble_start(hlp_ble_state_cb_t on_state, void *user);

bool hlp_ble_connected(void);
bool hlp_ble_subscribed(void);
/* Changes at every connection boundary, including rapid reconnects. */
uint32_t hlp_ble_generation(void);

/* Số message `s` đã nhận, để hiển thị và chẩn đoán. */
unsigned hlp_ble_state_count(void);
