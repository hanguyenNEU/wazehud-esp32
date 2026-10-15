# WazeHUD Community Firmware

Firmware mã nguồn mở cho màn HUD trên taplo xe hơi: hiển thị hướng rẽ, khoảng
cách, tốc độ, giới hạn tốc độ, làn đường và cảnh báo từ Waze (qua tweak
WazeHUDProbe / Waze Mod trên iPhone) bằng BLE.

> *Open-source ESP32-S3 + LVGL firmware for a dashboard HUD that mirrors Waze
> turn-by-turn data over BLE (HLP/1 protocol). MIT licensed. Contributions for
> new boards and UI themes are welcome — see [CONTRIBUTING.md](CONTRIBUTING.md).*

> Dự án độc lập, không liên kết với Waze / Google. Tên chỉ dùng để mô tả khả năng
> tương thích — xem [DISCLAIMER.md](DISCLAIMER.md). Chỉ cấu hình khi xe đã dừng đỗ.

## Tính năng

- Màn dẫn đường: hướng rẽ, khoảng cách, tên đường, ETA, làn, tốc độ / giới hạn.
- Cảnh báo (camera, cảnh sát, biển cấm…) theo biển báo Việt Nam.
- Màn đồng hồ (âm lịch, ảnh nền động), thống kê chuyến đi.
- Cài đặt WiFi và cập nhật firmware qua SoftAP (không cần máy tính).
- Simulator SDL trên máy tính và bộ test host — làm UI không cần phần cứng.

## Phần cứng hỗ trợ

| Board | Màn hình | Cảm ứng | Trạng thái |
|---|---|---|---|
| LCDwiki ES3C28P (ESP32-S3 N16R8) | 2.8" ILI9341 320×240 SPI | FT6336G | ✅ Chính thức |

Muốn thêm board? Xem [docs/PORTING.md](docs/PORTING.md).

## Cấu trúc

```
firmware/        Dự án ESP-IDF (main/{app,ble,hlp,net,system,ui})
sim/             Simulator LVGL + SDL2 chạy chính mã UI của firmware
test/host/       Test chạy trên máy tính (decoder HLP, cảnh báo, UI…)
tools/           Công cụ Python: thiết bị BLE giả, codec HLP, convert ảnh/font
assets/signs/    Ảnh biển báo nguồn (public domain)
docs/            PORTING.md, PROTOCOL.md
```

## Build & nạp

Cần [ESP-IDF](https://docs.espressif.com/projects/esp-idf/) ≥ 5.5 (đã kiểm với v6.1).

```bash
cd firmware
idf.py set-target esp32s3
idf.py menuconfig        # HUD community → URL OTA, mật khẩu SoftAP (tuỳ chọn)
idf.py build flash monitor
```

## Simulator & test (không cần phần cứng)

```bash
cd firmware && idf.py reconfigure   # tải LVGL, cJSON vào managed_components
cd ../test/host && ./run.sh         # test host
cd ../../sim && make run            # cần SDL2 (brew install sdl2)
```

Phát dữ liệu Waze mẫu không cần điện thoại: `python3 tools/fake_esp32.py --demo`
(chi tiết trong [tools/README.md](tools/README.md)).

## Cấu hình

| Kconfig | Mặc định | Ý nghĩa |
|---|---|---|
| `HUD_OTA_MANIFEST_URL` | rỗng (tắt) | URL `ota.json` cho Cài đặt › Kiểm tra cập nhật. Mỗi bản fork dùng server riêng. |
| `HUD_AP_PASSWORD` | `wazehud1234` | Mật khẩu WiFi SoftAP khi cập nhật firmware / cấu hình WiFi. Nên đổi. |

## Đóng góp

Xem [CONTRIBUTING.md](CONTRIBUTING.md). Giao thức BLE/HLP: [docs/PROTOCOL.md](docs/PROTOCOL.md).

## License

Mã nguồn: [MIT](LICENSE). Font, ảnh, icon giữ license gốc — xem
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Miễn trừ trách nhiệm: [DISCLAIMER.md](DISCLAIMER.md).
