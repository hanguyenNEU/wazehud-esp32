# Đóng góp

Cảm ơn bạn quan tâm! Các hướng đóng góp được chào đón nhất:

- **Board mới** (màn khác kích thước, driver khác, cảm ứng khác) — [docs/PORTING.md](docs/PORTING.md).
- **Giao diện** — theme, bố cục màn dẫn đường, màn đồng hồ, icon.
- **Sửa lỗi / tối ưu** decoder, BLE, nguồn điện, bộ nhớ.

## Quy trình

1. Mở issue trước với thay đổi lớn (board mới, đổi giao thức, đổi bố cục chính).
2. Fork → nhánh riêng → PR vào `main`. Mỗi PR một chủ đề.
3. Trước khi gửi PR, chạy:
   ```bash
   cd firmware && idf.py build
   cd ../test/host && ./run.sh
   ```
   Thay đổi UI: đính kèm ảnh chụp từ simulator (`sim/`) hoặc ảnh thiết bị thật.
4. CI sẽ build firmware và chạy test host cho mọi PR.

## Quy ước code

- C17, style giống code xung quanh (4 space, `snake_case`, tiền tố theo module: `ui_`, `hud_`, `hlp_`…).
- Tên biến / hàm: tiếng Anh. Comment: tiếng Việt hoặc tiếng Anh đều được — không dịch comment đã có.
- Code UI phải build được cả trong `sim/` (không include header ESP-IDF trong `ui/*.c` trừ file board).
- Không hardcode kích thước màn — dùng `DISPLAY_W` / `DISPLAY_H` từ `board.h`.
- Asset sinh bằng script trong `tools/` — commit cả script/lệnh sinh, không sửa tay file `.c` sinh ra.

## Giao thức HLP/1

Giao thức là hợp đồng với ứng dụng điện thoại. **Không đổi nghĩa trường hay mã đã có.**
Chỉ thêm trường / mã mới theo kiểu additive, mã lạ phải được bỏ qua an toàn.
Đổi gì liên quan thì cập nhật đồng thời `firmware/main/hlp`, `app/hud_decode.c`,
`tools/hlp.py` và [docs/PROTOCOL.md](docs/PROTOCOL.md).

## Asset & license

- Mã đóng góp được phát hành theo MIT.
- Font/ảnh/icon mới phải có license cho phép phân phối lại (OFL, Apache, CC0, CC-BY, public domain…).
  Ghi nguồn + license vào [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- Không dùng logo, icon hay giao diện chính thức của bên thứ ba (Waze, Google, hãng xe…) — xem [DISCLAIMER.md](DISCLAIMER.md).

## Không commit

GPS trace thật, log thiết bị, dump BLE có dữ liệu cá nhân, `sdkconfig` cá nhân, file `.bin`.
