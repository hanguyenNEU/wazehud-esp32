# Thêm board mới

Hiện firmware hỗ trợ một board: **LCDwiki ES3C28P** (ESP32-S3, ILI9341 320×240 SPI,
cảm ứng FT6336G). Tài liệu này mô tả phần nào phụ thuộc phần cứng và cách thêm board.

## Yêu cầu tối thiểu

- ESP32-S3 (BLE 5 + NimBLE). Chip khác cần sửa `sdkconfig.defaults`.
- Flash ≥ 8 MB (ảnh firmware + 2 vùng OTA, xem `firmware/partitions.csv`).
- PSRAM khuyến nghị (ảnh nền động, TLS). Thiếu PSRAM thì `anim_bg` tự lùi về ảnh tĩnh.
- Màn có driver `esp_lcd` (ILI9341, ST7789, GC9A01, ST7701 RGB…). Cảm ứng không bắt buộc
  nhưng UI hiện dùng vuốt/chạm để chuyển màn.

## Phần phụ thuộc phần cứng

| File | Nội dung |
|---|---|
| `firmware/main/ui/board.h` | `DISPLAY_W/H`, kích thước vật lý (mm). Không include header IDF — sim dùng chung. |
| `firmware/main/ui/board_es3c28p.c` | Chân SPI/BL, khởi tạo panel, đèn nền LEDC. Triển khai `display.h`. |
| `firmware/main/ui/touch_es3c28p.{c,h}` | Driver cảm ứng + hiệu chỉnh 2 điểm lưu NVS. |
| `firmware/main/main.c` (`app_main`) | `lvgl_port_add_disp`: rotation, `swap_bytes`, kích thước buffer; gọi `touch_*_init`. |
| `firmware/main/idf_component.yml` | Driver panel (`esp_lcd_ili9341`). |
| `firmware/sdkconfig.defaults` | Flash/PSRAM, console USB. |
| `firmware/main/ui/ui_navigator.c` … | Bố cục đang canh cho 320×240. |

## Các bước

1. **Header board** — tạo `firmware/main/ui/board_<ten>.h` với `DISPLAY_W/H`, `DISPLAY_MM_W/H`;
   cho `board.h` chọn theo macro:
   ```c
   #if defined(BOARD_<TEN>)
   #include "board_<ten>.h"
   #else /* ES3C28P mặc định */
   ...
   #endif
   ```
2. **Driver màn** — `board_<ten>.c` triển khai đủ hàm trong `display.h`
   (`display_init`, `display_io`, `display_panel`, `display_set_backlight`, `display_fill*`,
   `display_selftest`). Thêm driver panel vào `idf_component.yml`.
3. **Cảm ứng** — `touch_<ten>.c` đăng ký `lv_indev` pointer. Nên giữ API giống
   `touch_es3c28p.h` (`init`, `calibrate_if_needed`, `request_recalibrate`); cảm ứng
   điện dung có toạ độ chuẩn có thể để `calibrate_if_needed` rỗng.
4. **Chọn board khi build** — thêm `choice HUD_BOARD` vào `firmware/main/Kconfig.projbuild`,
   trong `firmware/main/CMakeLists.txt` chọn file `board_*.c` / `touch_*.c` theo
   `CONFIG_HUD_BOARD_*` và `add_compile_definitions(BOARD_<TEN>)`.
   Thêm `sdkconfig.defaults.<ten>` nếu flash/PSRAM khác, build bằng
   `idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.<ten>" build`.
5. **Rotation & màu** — chỉnh `.rotation` và `.flags.swap_bytes` trong `app_main`
   (nên chuyển thành hằng trong header board). Dùng `display_selftest()` để kiểm offset:
   thiếu một dấu góc nghĩa là offset cột/hàng sai.
6. **Bố cục** — nếu độ phân giải khác 320×240, kiểm tra từng màn trong simulator:
   ```bash
   cd sim && make BOARD=<TEN> run
   ```
   Ưu tiên tính toạ độ theo `DISPLAY_W/H` thay vì thêm `#if BOARD_…` vào file UI.
7. **CI** — thêm board vào ma trận build trong `.github/workflows/ci.yml`.
8. **README** — thêm dòng vào bảng "Phần cứng hỗ trợ", kèm link mua / schematic và ảnh thực tế.

## Checklist PR board mới

- [ ] `idf.py build` sạch cho board mới **và** ES3C28P
- [ ] `test/host/run.sh` pass
- [ ] Ảnh chụp: màn dẫn đường, cảnh báo, đồng hồ, cài đặt trên phần cứng thật
- [ ] Chạm / vuốt / hiệu chỉnh hoạt động
- [ ] Đèn nền điều chỉnh được, màn tắt khi idle
- [ ] OTA qua SoftAP thành công (bảng phân vùng hợp lệ)
