/* Chạy hlp_core + hud_decode trên máy chủ và in ra dạng chuẩn hoá để đối chiếu.
 *
 * Byte được bơm theo từng chunk nhỏ đúng như BLE, nên bài test này kiểm tra cả
 * bộ ghép frame chứ không riêng decoder.
 */

#include "hlp_core.h"
#include "hud_decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK 20 /* an toàn cho cả ATT MTU 23 */

static hud_state_t state;
static unsigned decoded;

static void field_or_dash(const char *value)
{
    fputs(value[0] ? value : "-", stdout);
}

static void on_line(const char *line, size_t length, void *user)
{
    (void)user;
    if (!hud_decode_state(line, length, &state)) return;
    decoded++;

    printf("%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t%d\t",
           state.nav ? 1 : 0, state.speed, state.limit, state.over ? 1 : 0,
           state.turn, state.turn_next, state.distance, state.exit,
           state.remain_m, state.remain_min);

    field_or_dash(state.eta);

    printf("\t%d\t%d\t%d\t%d\t%d\t%d\t",
           state.avg_zone ? 1 : 0, state.avg_left, state.avg_speed,
           state.avg_pct, state.alert, state.alert_distance);

    if (state.alert_has_value) printf("%d", state.alert_value);
    else fputs("-", stdout);

    fputs("\t", stdout);
    field_or_dash(state.street);
    fputs("\t", stdout);
    field_or_dash(state.street_next);

    fputs("\t", stdout);
    if (!state.lanes_present) {
        fputs("-", stdout);
    } else if (state.lane_count == 0) {
        fputs("[]", stdout);
    } else {
        for (int i = 0; i < state.lane_count; ++i)
            printf("%s%u:%u", i ? "," : "",
                   state.lanes[i].directions, state.lanes[i].selected);
    }

    fputs("\t", stdout);
    if (!state.alerts_present) {
        fputs("-", stdout);
    } else if (state.alert_count == 0) {
        fputs("[]", stdout);
    } else {
        for (int i = 0; i < state.alert_count; ++i) {
            printf("%s%d/%d", i ? "," : "",
                   state.alerts[i].kind, state.alerts[i].distance);
            if (state.alerts[i].has_value) printf("/%d", state.alerts[i].value);
        }
    }

    printf("\t%u\n", state.ts);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "dùng: %s <file-dòng-HLP-thô>\n", argv[0]);
        return 2;
    }
    FILE *fh = fopen(argv[1], "rb");
    if (!fh) {
        perror(argv[1]);
        return 2;
    }

    hlp_rx_t rx;
    hlp_rx_init(&rx, on_line, NULL);
    hud_state_reset(&state);

    uint8_t buffer[CHUNK];
    size_t read;
    while ((read = fread(buffer, 1, sizeof(buffer), fh)) > 0)
        hlp_rx_feed(&rx, buffer, read);
    fclose(fh);

    fprintf(stderr, "state decode được: %u · frame tràn: %u · UTF-8 lỗi: %u\n",
            decoded, rx.oversized, rx.malformed_utf8);
    return 0;
}
