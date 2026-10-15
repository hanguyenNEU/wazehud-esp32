/* Host test cấu hình — NVS mock trong RAM + failure injection (§6, §9).
 * Chạy đúng hud_config.c sẽ nạp vào ESP32, chỉ thay NVS thật bằng mock. */
#include "../../firmware/main/app/hud_config.c"
#include "nvs_mock.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static char send_buf[8192];

static void send_cb(const char *line, void *user)
{
    (void)user;
    if (strstr(line, "\"cfg_ack\"")) {
        snprintf(send_buf, sizeof(send_buf), "%s", line);
    }
}

static bool last_ack(cJSON **ack_root)
{
    if (!send_buf[0]) return false;
    cJSON *root = cJSON_Parse(send_buf);
    if (!root) return false;
    *ack_root = root;
    return true;
}

static bool ack_ok(void)
{
    cJSON *root;
    if (!last_ack(&root)) return false;
    cJSON *ok = cJSON_GetObjectItemCaseSensitive(root, "ok");
    bool v = cJSON_IsBool(ok) && cJSON_IsTrue(ok);
    cJSON_Delete(root);
    return v;
}

static bool ack_error_contains(const char *needle)
{
    cJSON *root;
    if (!last_ack(&root)) return false;
    cJSON *err = cJSON_GetObjectItemCaseSensitive(root, "error");
    bool v = err && cJSON_IsString(err) && strstr(err->valuestring, needle);
    cJSON_Delete(root);
    return v;
}

/* Snapshot cache cho patch: take_snapshot chỉ đọc được MỘT lần mỗi publish,
 * nên giữ bản mới nhất để dựng candidate. */
static hud_config_snapshot_t last_snap;
static bool have_snap;

static bool take_cached(hud_config_snapshot_t *out)
{
    hud_config_snapshot_t snap;
    if (!hud_config_take_snapshot(&snap)) return false;
    last_snap = snap;
    have_snap = true;
    if (out) *out = snap;
    return true;
}

static hud_config_patch_t patch_of(hud_config_field_t field, int value,
                                   uint32_t expected_rev, uint32_t req_id)
{
    hud_config_patch_t p = {
        .field = field,
        .expected_revision = expected_rev,
        .request_id = req_id,
    };
    (void)take_cached(NULL);
    assert(have_snap);
    p.value = last_snap.value;
    switch (field) {
    case HUD_CONFIG_FIELD_BRIGHTNESS: p.value.brightness = value; break;
    case HUD_CONFIG_FIELD_ALERT_DIST: p.value.alert_dist = value; break;
    case HUD_CONFIG_FIELD_DIM_IDLE:   p.value.dim_idle = value; break;
    case HUD_CONFIG_FIELD_ALERTS:     p.value.alerts = value != 0; break;
    default: break;
    }
    return p;
}
static void do_patch(hud_config_field_t field, int value,
                    uint32_t expected_rev, uint32_t req_id)
{
    hud_config_patch_t p = patch_of(field, value, expected_rev, req_id);
    hud_config_local_patch(&p);
}



static void seed_store(void)
{
    nvs_mock_reset();
    nvs_set_i32(1, "bright", 60);
    nvs_set_str(1, "theme", "night");
    nvs_set_str(1, "layout", "speed");
    nvs_set_u8(1, "alerts", 0);
    nvs_set_i32(1, "alrdist", 1000);
    nvs_set_i32(1, "dimidle", 0);
    nvs_set_u32(1, "rev", 7);
}

int main(void)
{
    hud_config_snapshot_t snap;
    hud_config_result_t res;

    /* 1. Init nạp NVS + publish snapshot. */
    seed_store();
    hlp_device_config_init();
    assert(take_cached(&snap));
    assert(snap.revision == 7);
    assert(snap.value.brightness == 60);
    assert(!strcmp(snap.value.theme, "night"));
    assert(!strcmp(snap.value.layout, "speed"));
    assert(snap.value.alerts == false);
    assert(snap.value.alert_dist == 1000);
    assert(snap.value.dim_idle == 0);
    puts("ok init loads + publishes snapshot");

    /* 2. Local patch hợp lệ → OK, revision tăng MỘT lần, NVS ghi đủ. */
    do_patch(HUD_CONFIG_FIELD_BRIGHTNESS, 90, 7, 1);
    hud_config_process_local();
    assert(hud_config_take_result(&res));
    assert(res.request_id == 1 && res.kind == HUD_CONFIG_OK);
    assert(take_cached(&snap));
    assert(snap.revision == 8 && snap.value.brightness == 90);
    int32_t v = 0;
    assert(nvs_get_i32(1, "bright", &v) == ESP_OK && v == 90);
    uint32_t rev = 0;
    assert(nvs_get_u32(1, "rev", &rev) == ESP_OK && rev == 8);
    puts("ok local patch commit, revision +1, NVS written");

    /* 3. expected_revision cũ → CONFLICT, active không đổi. */
    do_patch(HUD_CONFIG_FIELD_BRIGHTNESS, 100, 7, 2);
    hud_config_process_local();
    assert(hud_config_take_result(&res));
    assert(res.request_id == 2 && res.kind == HUD_CONFIG_CONFLICT);
    assert(hud_config_revision() == 8);
    assert(nvs_get_i32(1, "bright", &v) == ESP_OK && v == 90);
    puts("ok conflict keeps active");

    /* 4. Giá trị không hợp lệ (không đúng bước 5) → INVALID. */
    do_patch(HUD_CONFIG_FIELD_BRIGHTNESS, 13, 8, 3);
    hud_config_process_local();
    assert(hud_config_take_result(&res));
    assert(res.request_id == 3 && res.kind == HUD_CONFIG_INVALID);
    puts("ok invalid value rejected");

    /* 5. Queue đầy → BUSY ngay, không block; xử lý sau vẫn đúng thứ tự. */
    for (int i = 0; i < 4; ++i)
        do_patch(HUD_CONFIG_FIELD_DIM_IDLE, 10 + i, 8, 10 + i);
    do_patch(HUD_CONFIG_FIELD_DIM_IDLE, 30, 8, 99);
    assert(hud_config_take_result(&res));
    assert(res.request_id == 99 && res.kind == HUD_CONFIG_BUSY);
    /* Cả 4 patch dựng trên rev 8: patch đầu OK (rev -> 9), 3 patch sau đúng
       nghĩa CONFLICT — đây cũng chính là hành vi thật khi UI gửi gộp. */
    static const hud_config_result_kind_t expect_kind[4] = {
        HUD_CONFIG_OK, HUD_CONFIG_CONFLICT, HUD_CONFIG_CONFLICT, HUD_CONFIG_CONFLICT};
    for (int i = 0; i < 4; ++i) {
        hud_config_process_local();
        assert(hud_config_take_result(&res));
        assert(res.request_id == (uint32_t)(10 + i) && res.kind == expect_kind[i]);
    }
    assert(hud_config_revision() == 9); /* chỉ patch đầu commit */
    puts("ok queue full BUSY + fifo order");

    /* 6. NVS lỗi → STORAGE_ERROR; RAM giữ active/revision cũ, không publish
       snapshot mới (snapshot mailbox không có gì mới). */
    assert(take_cached(&snap)); /* dọn mailbox cũ */
    nvs_mock_fail_next = true;
    do_patch(HUD_CONFIG_FIELD_ALERT_DIST, 1800, hud_config_revision(), 20);
    hud_config_process_local();
    assert(hud_config_take_result(&res));
    assert(res.request_id == 20 && res.kind == HUD_CONFIG_STORAGE_ERROR);
    assert(hud_config_revision() == 9); /* chỉ patch đầu commit */
    assert(!take_cached(&snap)); /* không có snapshot mới */
    int32_t d = 0;
    assert(nvs_get_i32(1, "alrdist", &d) == ESP_OK && d == 1000);
    puts("ok NVS failure rolls back RAM, no apply");

    /* 7. Remote happy path: begin → 6 set → commit → ACK ok, revision+1. */
    uint32_t base_rev = hud_config_revision();
    send_buf[0] = '\0';
    char begin_buf[96];
    snprintf(begin_buf, sizeof(begin_buf),
             "{\"t\":\"cfg_set_begin\",\"tx\":1,\"rev\":%u,\"count\":6}", base_rev);
    cJSON *msg = cJSON_Parse(begin_buf);
    assert(msg && hlp_device_config_handle(msg, send_cb, NULL));
    cJSON_Delete(msg);
    static const char *sets[] = {
        "{\"t\":\"cfg_set\",\"tx\":1,\"id\":\"brightness\",\"value\":70}",
        "{\"t\":\"cfg_set\",\"tx\":1,\"id\":\"theme\",\"value\":\"day\"}",
        "{\"t\":\"cfg_set\",\"tx\":1,\"id\":\"layout\",\"value\":\"waze\"}",
        "{\"t\":\"cfg_set\",\"tx\":1,\"id\":\"alerts\",\"value\":true}",
        "{\"t\":\"cfg_set\",\"tx\":1,\"id\":\"alert_dist\",\"value\":600}",
        "{\"t\":\"cfg_set\",\"tx\":1,\"id\":\"dim_idle\",\"value\":5}",
    };
    for (int i = 0; i < 6; ++i) {
        msg = cJSON_Parse(sets[i]);
        assert(msg && hlp_device_config_handle(msg, send_cb, NULL));
        cJSON_Delete(msg);
    }
    msg = cJSON_Parse("{\"t\":\"cfg_set_commit\",\"tx\":1}");
    assert(msg && hlp_device_config_handle(msg, send_cb, NULL));
    cJSON_Delete(msg);
    assert(ack_ok());
    assert(take_cached(&snap));
    assert(snap.revision == base_rev + 1);
    assert(snap.value.brightness == 70 && !strcmp(snap.value.theme, "day"));
    assert(!strcmp(snap.value.layout, "waze") && snap.value.alerts == true);
    assert(snap.value.alert_dist == 600 && snap.value.dim_idle == 5);
    puts("ok remote transaction happy path");

    /* 8. Revision đổi giữa begin và commit → ACK lỗi revision, bỏ staging. */
    base_rev = hud_config_revision();
    snprintf(begin_buf, sizeof(begin_buf),
             "{\"t\":\"cfg_set_begin\",\"tx\":2,\"rev\":%u,\"count\":6}", base_rev);
    msg = cJSON_Parse(begin_buf);
    assert(msg && hlp_device_config_handle(msg, send_cb, NULL));
    cJSON_Delete(msg);
    for (int i = 0; i < 6; ++i) {
        msg = cJSON_Parse(sets[i]);
        /* đổi tx sang 2 */
        cJSON_ReplaceItemInObjectCaseSensitive(msg, "tx", cJSON_CreateNumber(2));
        assert(hlp_device_config_handle(msg, send_cb, NULL));
        cJSON_Delete(msg);
    }
    /* Local patch commit xen giữa → revision tăng. */
    do_patch(HUD_CONFIG_FIELD_DIM_IDLE, 20, base_rev, 30);
    hud_config_process_local();
    assert(hud_config_take_result(&res) && res.kind == HUD_CONFIG_OK);
    msg = cJSON_Parse("{\"t\":\"cfg_set_commit\",\"tx\":2}");
    assert(msg && hlp_device_config_handle(msg, send_cb, NULL));
    cJSON_Delete(msg);
    assert(ack_error_contains("revision changed"));
    puts("ok mid-transaction revision conflict");

    /* 9. Transaction thiếu field → ACK lỗi, không commit. */
    base_rev = hud_config_revision();
    char begin[64];
    snprintf(begin, sizeof(begin), "{\"t\":\"cfg_set_begin\",\"tx\":3,\"rev\":%u,\"count\":6}",
             base_rev);
    msg = cJSON_Parse(begin);
    assert(msg && hlp_device_config_handle(msg, send_cb, NULL));
    cJSON_Delete(msg);
    msg = cJSON_Parse("{\"t\":\"cfg_set\",\"tx\":3,\"id\":\"brightness\",\"value\":80}");
    assert(msg && hlp_device_config_handle(msg, send_cb, NULL));
    cJSON_Delete(msg);
    msg = cJSON_Parse("{\"t\":\"cfg_set_commit\",\"tx\":3}");
    assert(msg && hlp_device_config_handle(msg, send_cb, NULL));
    cJSON_Delete(msg);
    assert(ack_error_contains("incomplete"));
    assert(hud_config_revision() == base_rev);
    puts("ok incomplete transaction rejected");

    /* 10. Remote NVS lỗi → ACK "NVS write failed", RAM rollback. */
    msg = cJSON_Parse(begin);
    assert(msg && hlp_device_config_handle(msg, send_cb, NULL));
    cJSON_Delete(msg);
    for (int i = 0; i < 6; ++i) {
        msg = cJSON_Parse(sets[i]);
        cJSON_ReplaceItemInObjectCaseSensitive(msg, "tx", cJSON_CreateNumber(3));
        assert(hlp_device_config_handle(msg, send_cb, NULL));
        cJSON_Delete(msg);
    }
    nvs_mock_fail_next = true;
    msg = cJSON_Parse("{\"t\":\"cfg_set_commit\",\"tx\":3}");
    assert(msg && hlp_device_config_handle(msg, send_cb, NULL));
    cJSON_Delete(msg);
    assert(ack_error_contains("NVS write failed"));
    assert(hud_config_revision() == base_rev);
    puts("ok remote NVS failure rolls back");

    puts("CONFIG: all tests passed");
    return 0;
}
