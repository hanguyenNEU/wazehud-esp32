#pragma once

/* Lịch âm Việt Nam — thuần C, không LVGL/ESP-IDF, để chạy host test.
 *
 * Quy ước đã chốt (§5 kế hoạch UI):
 *   - Ngày âm tính từ UTC instant chuyển sang UTC+7 (không phụ thuộc timezone
 *     điện thoại; khi timezone điện thoại khác UTC+7 thì UI phải ghi rõ nhãn).
 *   - Phạm vi hỗ trợ 1900–2099; ngoài phạm vi trả valid=false (UI hiện
 *     "Ngoài phạm vi").
 *   - Can Chi tính theo NĂM ÂM, không theo năm dương trước Tết.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int day;        /* 1..30 */
    int month;      /* 1..12 */
    int year;       /* năm âm */
    int leap_month; /* 0 = tháng thường; >0 = tháng nhuận đi theo tháng số này */
    bool valid;
} lunar_date_t;

/* unix_seconds: UTC epoch giây. Trả valid=false nếu ngoài phạm vi. */
bool lunar_from_unix(int64_t unix_seconds, lunar_date_t *out);

/* Tách ngày/giờ dân sự theo timezone (phút) từ UTC instant. wday: 0=CN..6=T7.
   Dùng chung cho đồng hồ HUD lẫn CLOCK (chỉ một chỗ định nghĩa quy tắc lịch). */
void lunar_civil_from_unix(int64_t unix_seconds, int tz_minutes,
                           int *year, int *month, int *day,
                           int *hour, int *minute, int *second, int *wday);

/* Can Chi của năm âm: gan 0..9 (Giáp..Quý), zhi 0..11 (Tý..Hợi).
   gan/zhi có thể NULL nếu không cần. */
void lunar_year_ganzhi(int lunar_year, int *gan, int *zhi);
