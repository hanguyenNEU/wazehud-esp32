/* Bộ ghép mảnh kênh tin 8a7e0012 khi 2 central cùng ghi (app/msg_asm.c). */
#include "msg_asm.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(cond) do { if (!(cond)) { printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)

static char got[MSG_FRAME_MAX];
static int got_count;

static void on_frame(const msg_frame_t *frame, void *user)
{
    (void)user;
    memcpy(got, frame->bytes, frame->length + 1);
    got_count++;
}

static void feed(msg_asm_t *a, uint16_t conn, int64_t t, const char *s)
{
    msg_asm_feed(a, conn, t, (const uint8_t *)s, strlen(s), on_frame, NULL);
}

static void reset(msg_asm_t *a)
{
    msg_asm_init(a);
    got[0] = '\0';
    got_count = 0;
}

int main(void)
{
    msg_asm_t a;

    /* 1 central, nhiều mảnh. */
    reset(&a);
    feed(&a, 1, 0, "\x02{\"t\":\"trip\",");
    feed(&a, 1, 10, "\"items\":[]}\n");
    CHECK(got_count == 1);
    CHECK(!strcmp(got, "{\"t\":\"trip\",\"items\":[]}"));

    /* 2 central xen kẽ: A bắt đầu trước giữ bộ đệm, toàn bộ bản tin B bị bỏ. */
    reset(&a);
    feed(&a, 1, 0, "\x02{\"from\":");
    feed(&a, 2, 5, "\x02{\"from\":\"B\"");
    feed(&a, 1, 10, "\"A\"");
    feed(&a, 2, 15, "}\n");
    feed(&a, 1, 20, "}\n");
    CHECK(got_count == 1);
    CHECK(!strcmp(got, "{\"from\":\"A\"}"));

    /* A xong thì B gửi được. */
    feed(&a, 2, 30, "\x02{\"from\":\"B\"}\n");
    CHECK(got_count == 2);
    CHECK(!strcmp(got, "{\"from\":\"B\"}"));

    /* A ngắt kết nối giữa chừng: nhả ngay cho B. */
    reset(&a);
    feed(&a, 1, 0, "\x02{\"from\":\"A\"");
    msg_asm_disconnect(&a, 2);           /* central khác ngắt: không ảnh hưởng */
    CHECK(a.owner == 1);
    msg_asm_disconnect(&a, 1);
    feed(&a, 2, 10, "\x02{\"from\":\"B\"}\n");
    CHECK(got_count == 1);
    CHECK(!strcmp(got, "{\"from\":\"B\"}"));

    /* A im quá MSG_ASM_HOLD_US: B chiếm được, mảnh muộn của A bị bỏ. */
    reset(&a);
    feed(&a, 1, 0, "\x02{\"from\":\"A\"");
    feed(&a, 2, MSG_ASM_HOLD_US - 1, "\x02{\"x\":1}\n");
    CHECK(got_count == 0);
    feed(&a, 2, MSG_ASM_HOLD_US + 1, "\x02{\"from\":\"B\"}\n");
    CHECK(got_count == 1);
    feed(&a, 1, MSG_ASM_HOLD_US + 2, "}\n");
    CHECK(got_count == 1);
    CHECK(!strcmp(got, "{\"from\":\"B\"}"));

    /* Mảnh giữa không có 0x02 mở đầu từ central chưa giữ: bỏ. */
    reset(&a);
    feed(&a, 3, 0, "{\"orphan\":1}\n");
    CHECK(got_count == 0);

    /* Bản tin quá dài: bỏ cả bản, nhả bộ đệm, bản sau vẫn nhận. */
    reset(&a);
    static char big[MSG_FRAME_MAX + 10];
    memset(big, 'x', sizeof(big) - 1);
    big[0] = 0x02;
    big[sizeof(big) - 1] = '\0';
    feed(&a, 1, 0, big);
    feed(&a, 1, 10, "\n");
    CHECK(got_count == 0);
    CHECK(a.owner == MSG_ASM_NO_CONN);
    feed(&a, 2, 20, "\x02{\"ok\":1}\n");
    CHECK(got_count == 1);

    /* Hash: cùng nội dung cùng hash, khác nội dung khác hash. */
    msg_frame_t f1 = {.length = 3, .bytes = "abc"}, f2 = {.length = 3, .bytes = "abd"};
    CHECK(msg_frame_hash(&f1) == msg_frame_hash(&f1));
    CHECK(msg_frame_hash(&f1) != msg_frame_hash(&f2));

    if (failures) { printf("msg_asm_test: %d lỗi\n", failures); return 1; }
    printf("msg_asm_test: OK\n");
    return 0;
}
