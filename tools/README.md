# Bộ công cụ HUD trên Mac — không cần ESP32

Biến Mac thành một thiết bị HUD Link giả để **thu dữ liệu HLP/1 thật** từ Waze
Mod, rồi phát lại bất cứ lúc nào. Nhờ vậy phần decoder và giao diện làm xong
trước khi phần cứng về tay.

## Có gì

| File | Việc |
|---|---|
| `hlp.py` | Codec HLP/1 — phản chiếu đúng ngữ nghĩa `shared/hlp_core.c` của SDK |
| `fake_esp32.py` | Thiết bị BLE giả + HUD vẽ ra terminal + thu/phát lại |
| `cbin2fnt.py` | Convert font cbin (78/xiaozhi-fonts) → LVGL 9 `.fnt` binfont |
| `captures/` | Dữ liệu thu được, mỗi dòng kèm mốc ms |

## Xem thử ngay (không cần Bluetooth, không cần điện thoại)

```bash
python3 tools/fake_esp32.py --demo
```

Chạy một chuyến đi tổng hợp ở 4 Hz đi qua đủ các tình huống cần vẽ đúng: rẽ,
vòng xuyến, vượt tốc, vùng cấm vượt, nhiều cảnh báo chồng nhau, đến nơi. Dữ liệu
được bơm qua **đúng đường đi của BLE** — cắt thành chunk 20 byte rồi ghép lại
theo LF — nên nó kiểm tra cả bộ ghép frame chứ không riêng phần vẽ.

## Thu dữ liệu thật từ điện thoại

```bash
pip install bless
python3 tools/fake_esp32.py
```

Máy tính đóng vai thiết bị BLE `WazeHUD`. Trên macOS, terminal phải được cấp
quyền Bluetooth (System Settings › Privacy & Security › Bluetooth). Sau đó trên điện thoại:

**Cài đặt Mod → Thiết bị → HUD Link** → Transport **BLE** → Chọn thiết bị →
`WazeHUD` → Bật HUD Link.

Terminal sẽ vẽ HUD theo thời gian thực, đồng thời ghi mọi dòng nhận được vào
`tools/captures/hlp-<thời-gian>.jsonl` (đã gitignore — chứa tên đường thật,
đừng commit).

Phát lại về sau:

```bash
python3 tools/fake_esp32.py --replay tools/captures/hlp-20260825-171530.jsonl
```

Thêm `--repeat` để lặp vô hạn, `--raw` để in nguyên văn JSON thay vì vẽ HUD.

## Ghi chú thiết kế bám theo đặc tả

- Buffer 512 byte, dòng tràn bị bỏ và đồng bộ lại ở `\n` kế tiếp.
- `\r` cuối dòng bị **xoá**, không chỉ bỏ qua (§1).
- `pong` được trả **trước** khi decode hay vẽ (§8).
- `dev` gửi ngay khi có kết nối — Android chỉ chờ 500 ms rồi tự chốt mặc định.
- `lan` và `alrs` là opt-in, phải ghi rõ trong `dev.want.fields` mới được nhận.
- `sess` đổi ⇒ phiên mới, xoá state cũ (§2.5).
- Mã `trn` lạ xử như 0; mã `alr` lạ fallback về nguy hiểm chung (§4).
- `avg` là **vùng cấm vượt** ở Việt Nam — vẽ biển cấm vượt, không vẽ camera.
