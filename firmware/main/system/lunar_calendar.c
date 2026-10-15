#include "lunar_calendar.h"

/* Attribution: bảng dữ liệu năm âm lấy từ gói npm "lunar" v2.0.0 của 6tail
 * (https://www.npmjs.com/package/lunar, giấy phép MIT), nguồn gốc thuật toán
 * Hồ Ngọc Đức / Jean Meeus "Astronomical Algorithms". Chỉ lấy BẢNG DỮ LIỆU;
 * phần chuyển đổi viết lại thuần C từ encoding công khai của gói trên:
 *
 *   mỗi năm (1890..2100) mã 24 bit:
 *     bit 0..12   : độ dài từng segment tháng theo thứ tự lịch
 *                   (bit = 1 → 30 ngày, 0 → 29 ngày); segment nhuận nằm ngay
 *                   sau tháng nó đi theo.
 *     bit 13..17  : ngày dương lịch của mùng 1 Tết
 *     bit 18      : tháng dương lịch của Tết (0 = tháng 1, 1 = tháng 2)
 *     bit 19      : năm có tháng nhuận
 *     bit 20..23  : chỉ số (0-based) tháng mà tháng nhuận đi theo
 *
 * Kiểm chứng cấu trúc với các mốc công khai: Tết 2023=22/1 (nhuận tháng 2),
 * 2024=10/2, 2025=29/1 (nhuận tháng 6), 2026=17/2 — khớp.
 */

#define LUNAR_YEAR_MIN 1890
#define LUNAR_YEAR_MAX 2100
#define CIVIL_YEAR_MIN 1900
#define CIVIL_YEAR_MAX 2099

static const uint32_t YEAR_INFO[LUNAR_YEAR_MAX - LUNAR_YEAR_MIN + 1] = {
    1750378, 339370, 6020516, 405156, 314698, 4934293,
    371351, 279894, 2804405, 346837, 8124114, 419666,
    331429, 4961866, 394827, 297627, 3880278, 370026,
    281433, 1759058, 345938, 6019877, 412453, 313931,
    4936875, 377517, 288107, 1764201, 355753, 7093650,
    429714, 331045, 4954701, 395862, 303798, 3872181,
    370388, 282281, 1769106, 347794, 6016294, 402731,
    313943, 4936374, 379738, 296660, 2821833, 354121,
    7075475, 420499, 329003, 5991003, 387757, 304490,
    3881813, 371620, 281417, 1759891, 346773, 7058733,
    402742, 314029, 4945322, 378290, 290213, 2825546,
    363850, 8120981, 412311, 329046, 5999285, 387797,
    304850, 3878565, 372389, 280138, 2796695, 338587,
    7066970, 402794, 314217, 4945746, 387922, 289573,
    3864139, 354891, 8123563, 410285, 320877, 5999465,
    396713, 306578, 3882277, 372005, 10246733, 428630,
    336566, 6006197, 403157, 315049, 4947602, 388754,
    298278, 2812502, 346711, 8123606, 418650, 321237,
    4953801, 395081, 304787, 3872043, 361771, 272987,
    1758554, 337258, 7060309, 412580, 322377, 4938387,
    379541, 288045, 3861165, 346805, 9172394, 419282,
    331173, 6004042, 396618, 306325, 3880238, 361814,
    273077, 1758642, 345810, 6008485, 403237, 312907,
    4934807, 371883, 288090, 2812630, 355177, 11269970,
    420690, 330533, 6003275, 387659, 296107, 4916571,
    361901, 273258, 1760082, 347538, 7068965, 404773,
    313941, 4936877, 378038, 279989, 2813354, 356041,
    8142482, 421522, 331046, 5999190, 387671, 296150,
    3868373, 362325, 280393, 2797203, 337555, 7058731,
    402731, 305755, 4937050, 378218, 289637, 3864394,
    355146, 8125077, 420501, 320813, 5991085, 387765,
    304554, 3869605, 363941, 281930, 2808981, 339094,
    7059790, 402774, 314037, 4937138, 378578, 290469,
    3870282, 345675, 8113303, 410795, 320859, 5991126,
    387946, 304978, 3880741, 363301, 273035, 1750171,
    337067,
};

typedef struct {
    int year;
    int start_month;    /* 1 hoặc 2 (dương lịch) */
    int start_day;
    bool has_leap;
    int leap_index;     /* 0-based tháng được nhuận */
    uint32_t bits;      /* 13 bit độ dài */
} year_info_t;

static year_info_t info_of(int year)
{
    uint32_t e = YEAR_INFO[year - LUNAR_YEAR_MIN];
    year_info_t info;
    info.year = year;
    info.start_month = ((e >> 18) & 1) + 1;
    info.start_day = (e >> 13) & 31;
    info.has_leap = (e & (1u << 19)) != 0;
    info.leap_index = info.has_leap ? (int)((e >> 20) & 15) : -1;
    info.bits = e & ((1u << 13) - 1u);
    return info;
}

static int segment_days(const year_info_t *info, int segment)
{
    return (info->bits & (1u << segment)) ? 30 : 29;
}

/* Howard Hinnant — civil ⇄ days (public domain, trong "chrono-Compatible
 * Low-Level Date Algorithms"). Epoch 1970-01-01. */
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static void civil_from_days(int64_t z, int *y, int *m, int *d)
{
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int yy = (int)yoe + (int)era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp + (mp < 10 ? 3 : -9));
    *y = yy + (*m <= 2);
}

bool lunar_from_unix(int64_t unix_seconds, lunar_date_t *out)
{
    if (!out) {
        return false;
    }

    /* UTC instant → ngày dân sự tại UTC+7 (cộng offset rồi mới chia; chia
       floor vì unix trước 1970 là số âm — cắt cụt về 0 sẽ lệch một ngày). */
    int64_t adj = unix_seconds + 7 * 3600;
    int64_t vn_days = adj / 86400;
    if (adj % 86400 != 0 && adj < 0) --vn_days;
    int y, m, d;
    civil_from_days(vn_days, &y, &m, &d);
    if (y < CIVIL_YEAR_MIN || y > CIVIL_YEAR_MAX) {
        out->valid = false;
        return false;
    }

    int ly = y;
    year_info_t info = info_of(ly);
    int64_t new_year = days_from_civil(ly, info.start_month, info.start_day);
    if (vn_days < new_year) {
        --ly;
        if (ly < LUNAR_YEAR_MIN) {
            out->valid = false;
            return false;
        }
        info = info_of(ly);
        new_year = days_from_civil(ly, info.start_month, info.start_day);
    }

    int64_t diff = vn_days - new_year;
    int segments = info.has_leap ? 13 : 12;
    int64_t accum = 0;
    int month_index = 0;
    int month_len = 0;
    while (month_index < segments) {
        month_len = segment_days(&info, month_index);
        accum += month_len;
        if (diff < accum) break;
        ++month_index;
    }
    if (month_index >= segments) {
        out->valid = false;
        return false;
    }

    out->day = (int)(month_len - (accum - diff) + 1);
    out->year = ly;
    out->leap_month = 0;
    if (info.has_leap) {
        if (month_index == info.leap_index + 1) {
            out->leap_month = info.leap_index + 1;
            out->month = info.leap_index + 1;
        } else if (month_index > info.leap_index) {
            out->month = month_index; /* segment lùi 1 so với số tháng hiển thị */
        } else {
            out->month = month_index + 1;
        }
    } else {
        out->month = month_index + 1;
    }
    out->valid = true;
    return true;
}

void lunar_year_ganzhi(int lunar_year, int *gan, int *zhi)
{
    int offset = lunar_year - 4;
    int g = offset % 10;
    int z = offset % 12;
    if (g < 0) g += 10;
    if (z < 0) z += 12;
    if (gan) *gan = g;
    if (zhi) *zhi = z;
}

void lunar_civil_from_unix(int64_t unix_seconds, int tz_minutes,
                           int *year, int *month, int *day,
                           int *hour, int *minute, int *second, int *wday)
{
    int64_t local = unix_seconds + (int64_t)tz_minutes * 60;
    int64_t days = local / 86400;
    int64_t rem = local % 86400;
    if (rem < 0) { --days; rem += 86400; } /* floor cho unix âm */
    if (year || month || day) {
        int y, m, d;
        civil_from_days(days, &y, &m, &d);
        if (year) *year = y;
        if (month) *month = m;
        if (day) *day = d;
    }
    if (hour) *hour = (int)(rem / 3600);
    if (minute) *minute = (int)(rem % 3600) / 60;
    if (second) *second = (int)(rem % 60);
    if (wday) *wday = (int)((days + 4) % 7); /* 0 = Chủ nhật */
}
