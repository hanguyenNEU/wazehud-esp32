#include "msg_asm.h"

#include <string.h>

void msg_asm_init(msg_asm_t *a)
{
    memset(a, 0, sizeof(*a));
    a->owner = MSG_ASM_NO_CONN;
}

static void release(msg_asm_t *a)
{
    a->owner = MSG_ASM_NO_CONN;
    a->frame.length = 0;
    a->overflow = false;
}

void msg_asm_feed(msg_asm_t *a, uint16_t conn, int64_t now_us,
                    const uint8_t *data, size_t length, msg_asm_cb_t cb, void *user)
{
    if (!data || length == 0) return;

    /* Người giữ im quá lâu (bản tin dở dang, mất mảnh cuối) thì coi như rảnh. */
    if (a->owner != MSG_ASM_NO_CONN && now_us - a->last_us > MSG_ASM_HOLD_US) release(a);

    if (data[0] == 0x02) { /* mảnh đầu của bản tin mới */
        if (a->owner != MSG_ASM_NO_CONN && a->owner != conn) return; /* bên kia đang gửi */
        a->owner = conn;
        a->frame.length = 0;
        a->overflow = false;
        data++;
        length--;
    } else if (a->owner != conn) {
        return; /* mảnh giữa của bản tin không được nhận */
    }
    a->last_us = now_us;

    for (size_t i = 0; i < length; ++i) {
        if (data[i] == '\n') {
            if (!a->overflow && a->frame.length > 0) {
                a->frame.bytes[a->frame.length] = '\0';
                if (cb) cb(&a->frame, user);
            }
            /* Phần còn lại sau '\n' (nếu có) không thuộc bản tin nào — bỏ. */
            release(a);
            return;
        }
        if (a->frame.length >= MSG_FRAME_MAX - 1) {
            a->overflow = true; /* bỏ cả bản tin, chờ '\n' rồi nhả */
            continue;
        }
        a->frame.bytes[a->frame.length++] = (char)data[i];
    }
}

void msg_asm_disconnect(msg_asm_t *a, uint16_t conn)
{
    if (a->owner == conn) release(a);
}

uint32_t msg_frame_hash(const msg_frame_t *frame)
{
    uint32_t h = 2166136261u;
    for (uint16_t i = 0; i < frame->length; ++i) {
        h ^= (uint8_t)frame->bytes[i];
        h *= 16777619u;
    }
    return h;
}
