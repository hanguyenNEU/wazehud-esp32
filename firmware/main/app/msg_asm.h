#pragma once

/* Ghép mảnh kênh tin 8a7e0012 (thống kê chuyến đi) khi có tới 2 central cùng
 * ghi (vd 2 iPhone cùng chạy WazeHUD).
 *
 * Chỉ một bộ đệm (không tốn thêm RAM cho central thứ 2): mảnh 0x02 của
 * central nào tới trước thì central đó GIỮ bộ đệm tới khi gặp '\n'. Mảnh của
 * central còn lại trong lúc đó bị bỏ cả bản tin — bên đó gửi lại ở lượt sau
 * (tweak gửi lại khi HUD nối lại hoặc khi số đổi). Người giữ ngắt kết nối hoặc im quá MSG_ASM_HOLD_US thì nhả.
 *
 * Thuần C, không FreeRTOS — test host gọi trực tiếp (test/host/msg_asm_test.c). */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MSG_FRAME_MAX 512 /* bản tin chuyến đi ~120 byte, còn dư nhiều */
#define MSG_ASM_HOLD_US 2000000 /* bản tin vài trăm byte ghi có phản hồi xong < 1 s */
#define MSG_ASM_NO_CONN 0xFFFF

typedef struct {
    uint16_t length;
    char bytes[MSG_FRAME_MAX];
} msg_frame_t;

typedef struct {
    msg_frame_t frame;
    uint16_t owner;        /* central đang giữ bộ đệm, MSG_ASM_NO_CONN = rảnh */
    int64_t last_us;       /* lần cuối owner gửi mảnh */
    bool overflow;
} msg_asm_t;

/* Gọi khi đủ 1 bản tin (frame.bytes kết thúc '\0'). Chỉ được đọc frame
 * trong callback — trả về là bộ đệm dùng lại. */
typedef void (*msg_asm_cb_t)(const msg_frame_t *frame, void *user);

void msg_asm_init(msg_asm_t *a);
void msg_asm_feed(msg_asm_t *a, uint16_t conn, int64_t now_us,
                    const uint8_t *data, size_t length, msg_asm_cb_t cb, void *user);
/* Central ngắt kết nối: nếu đang giữ bộ đệm thì nhả. */
void msg_asm_disconnect(msg_asm_t *a, uint16_t conn);

/* FNV-1a để bỏ qua bản tin trùng y hệt bản trước. */
uint32_t msg_frame_hash(const msg_frame_t *frame);
