/* Golden test lịch âm — fixture lấy từ mốc công khai độc lập (ngày Tết được
 * công bố rộng rãi, tháng nhuận 2023/2025, "29 Tết" 2025/2026), không sinh ra
 * từ chính thuật toán đang test. Include trực tiếp .c để dùng days_from_civil. */
#include "../../firmware/main/system/lunar_calendar.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static int64_t vn_midnight(int y, int m, int d)
{
    /* Ngày dân sự y-m-d, giờ VN (UTC+7): unix = days*86400 - 7*3600. */
    return days_from_civil(y, m, d) * 86400 - 7 * 3600;
}

static void expect(int y, int m, int d, int ld, int lm, int ly, int leap, const char *label)
{
    lunar_date_t out;
    assert(lunar_from_unix(vn_midnight(y, m, d), &out));
    if (!(out.day == ld && out.month == lm && out.year == ly
          && out.leap_month == leap)) {
        fprintf(stderr, "%s: got %d/%d/%d leap=%d, want %d/%d/%d leap=%d\n",
                label, out.day, out.month, out.year, out.leap_month, ld, lm, ly, leap);
        assert(0);
    }
    printf("ok %s\n", label);
}

int main(void)
{
    expect(2024, 2, 10, 1, 1, 2024, 0, "Tet 2024");
    expect(2025, 1, 29, 1, 1, 2025, 0, "Tet 2025");
    expect(2026, 2, 17, 1, 1, 2026, 0, "Tet 2026");
    expect(2023, 1, 22, 1, 1, 2023, 0, "Tet 2023");
    expect(2022, 2, 1, 1, 1, 2022, 0, "Tet 2022");
    expect(2021, 2, 12, 1, 1, 2021, 0, "Tet 2021");
    expect(1900, 1, 31, 1, 1, 1900, 0, "Tet 1900");

    expect(2024, 9, 17, 15, 8, 2024, 0, "Trung thu 2024");

    /* 2025 "29 Tết": tháng Chạp thiếu → 28/1/2025 là 29/12 âm 2024. */
    expect(2025, 1, 28, 29, 12, 2024, 0, "29 Tet 2025");
    /* 2026 cũng "29 Tết": 16/2/2026 là 29/12 âm 2025. */
    expect(2026, 2, 16, 29, 12, 2025, 0, "29 Tet 2026");

    /* Midnight VN: 1 giây trước giao thừa 2024 vẫn là 30/12/2023. */
    lunar_date_t out;
    assert(lunar_from_unix(vn_midnight(2024, 2, 10) - 1, &out));
    assert(out.day == 30 && out.month == 12 && out.year == 2023 && out.leap_month == 0);
    puts("ok midnight VN boundary");

    /* Nhuận tháng 2/2023: quét từ Tết tới hết tháng 3 dương, phải thấy một dải
       ngày liên tục có leap_month == 2. */
    bool saw_leap2 = false;
    for (int d = 0; d < 70; ++d) {
        assert(lunar_from_unix(vn_midnight(2023, 1, 22) + d * 86400, &out));
        if (out.leap_month == 2) {
            saw_leap2 = true;
            assert(out.month == 2);
        }
    }
    assert(saw_leap2);
    puts("ok nhuan thang 2/2023");

    /* Nhuận tháng 6/2025: tương tự — phải có dải leap_month == 6. */
    bool saw_leap6 = false;
    for (int d = 0; d < 130; ++d) {
        assert(lunar_from_unix(vn_midnight(2025, 6, 1) + d * 86400, &out));
        if (out.leap_month == 6) {
            saw_leap6 = true;
            assert(out.month == 6);
        }
    }
    assert(saw_leap6);
    puts("ok nhuan thang 6/2025");

    /* Ngoài phạm vi: 1899 và 2100 → valid=false, không crash. */
    assert(!lunar_from_unix(vn_midnight(1899, 12, 31), &out));
    assert(!out.valid);
    assert(!lunar_from_unix(vn_midnight(2100, 1, 1), &out));
    assert(!out.valid);
    puts("ok out of range");

    /* Can Chi theo năm ÂM: Giáp Thìn 2024, Ất Tỵ 2025, Quý Mão 2023. */
    int gan, zhi;
    lunar_year_ganzhi(2024, &gan, &zhi);
    assert(gan == 0 && zhi == 4);
    lunar_year_ganzhi(2025, &gan, &zhi);
    assert(gan == 1 && zhi == 5);
    lunar_year_ganzhi(2023, &gan, &zhi);
    assert(gan == 9 && zhi == 3);
    puts("ok Can Chi");

    puts("LUNAR: all golden fixtures passed");
    return 0;
}
