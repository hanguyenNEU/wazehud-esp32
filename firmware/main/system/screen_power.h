#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Dim màn hình (§7):
 *   - Không tự tối trước phiên navigation đầu tiên.
 *   - Đã có nav: mốc idle = max(last_nav_us, last_activity_us); dim khi quá
 *     dim_idle phút. Tương tác người dùng hợp lệ cập nhật activity nên
 *     Clock/Menu không tối giữa lúc dùng.
 *   - Đang dim: contact đầu chỉ đánh thức ở brightness đã cấu hình và bị
 *     CONSUME cả contact (không click/swipe) tới lúc nhả; sau wake có nguyên
 *     một khoảng dim_idle mới.
 *   - Special mode (OTA/provisioning) ép sáng tối thiểu, không dim. */

void screen_power_apply_config(int brightness_percent, int dim_idle_min);
int screen_power_brightness(void);

/* Gọi từ UI tick với mốc thời gian mới nhất. */
void screen_power_tick(int64_t now_us, int64_t last_nav_us,
                       int64_t last_activity_us);

/* Gọi lúc contact DOWN. Trả true nếu đang dim → wake + consume contact;
   caller phải chặn mọi output của contact tới lúc release. */
bool screen_power_consume_wake_contact(void);

/* OTA/provisioning: giữ sáng tối thiểu min_percent (0 = tắt ép buộc). */
void screen_power_set_mode_min(int min_percent);

void screen_power_force_on(void);
bool screen_power_is_dimmed(void);
