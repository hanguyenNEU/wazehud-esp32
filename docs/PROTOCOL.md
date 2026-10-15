# Giao thức HLP/1 (tóm tắt)

HLP/1 (HUD Link Protocol) là luồng **JSON một dòng / message, kết thúc bằng `\n`**,
UTF-8, tối đa 512 byte mỗi frame. Điện thoại (tweak / Waze Mod) là BLE central,
HUD là peripheral.

Nguồn chuẩn là code: `firmware/main/hlp/` (frame, message), `firmware/main/app/hud_decode.c`
(trường state), `firmware/main/app/hud_state.h` (mã rẽ / cảnh báo), `tools/hlp.py` (bản
tham chiếu Python). Test vectors: `test/host/*.hlp`.

## BLE GATT

Mọi UUID có dạng `8a7e00XX-4d6e-4c48-9a9d-484c504c0001`.

| XX | Tên | Hướng | Ghi chú |
|---|---|---|---|
| `01` | Service HLP | — | Được quảng bá; app quét theo UUID này |
| `02` | TX | HUD → phone (notify) | |
| `03` | RX | phone → HUD (write) | Chunk theo MTU, ghép lại theo `\n` |
| `04` | Capabilities | read | |
| `10` | Service CarDash | — | Mở rộng riêng, không quảng bá |
| `11` | GPS | write | `app/gps_link.h` |
| `12` | Tin nhắn / chuyến đi | write + notify | Nhiều mảnh, `app/trip_link.h`, `app/msg_asm.h` |
| `13` | Ảnh nền | write + notify | JPEG nhiều mảnh, `app/bg_store.h` |
| `14` | Chuyển tiếp state | notify | Mỗi state HLP nguyên bản |

Tên quảng bá mặc định: `WazeHUD`. Tốc độ state trên BLE: 1–4 Hz.

## Envelope

Mọi message có `"v":1` và `"t":"<loại>"`. Message có `v` khác 1 hoặc JSON hỏng bị bỏ qua.

| `t` | Hướng | Nội dung |
|---|---|---|
| `dev` | HUD → phone | Giới thiệu thiết bị: `name`, `fw`, `proto`, `want.rate`, `want.fields`, `can`, `transport` |
| `ping` / `pong` | phone ↔ HUD | Giữ kết nối |
| `s` | phone → HUD | State dẫn đường (bảng dưới) |
| khác | | Cấu hình thiết bị — `hlp/hlp_device_config.c`, `app/hud_config.c` |

## State `s`

| Trường | Kiểu | Ý nghĩa |
|---|---|---|
| `nav` | bool/0-1 | Đang dẫn đường |
| `spd` / `lim` / `over` | int / int / bool | Tốc độ hiện tại, giới hạn (km/h), đang vượt tốc |
| `trn` / `trn2` | int | Mã rẽ hiện tại / kế tiếp (`hud_turn_t`, mã lạ ⇒ NONE) |
| `dst` | int (m) | Khoảng cách tới điểm rẽ; thiếu = -1 |
| `exit` | int | Số lối ra vòng xuyến |
| `st` / `st2` | string | Tên đường hiện tại / kế tiếp (≤ 40 code point) |
| `eta` | string | Giờ đến `HH:MM` |
| `rmin` / `rm` / `rkm` | int / int (m) / number | Thời gian còn lại; `rm` có thẩm quyền, `rkm` để tương thích |
| `lan` | `[[directions, selected], …]` | Làn (≤ 12); mảng rỗng = xoá chỉ dẫn làn |
| `avg`, `avgL`, `avgR` | bool, int, int | Vùng đo tốc độ trung bình |
| `alr`, `alrD`, `alrV` | int, int, number | Cảnh báo đơn (cũ) |
| `alrs` | `[{k, d, v?, s?, m?}, …]` | Danh sách cảnh báo (≤ 4). `k` = mã `hud_alert_t`, `d` = mét, `v` = giá trị (vd. giới hạn), `s`/`m` = mức độ / phút trễ cho kẹt xe |
| `ts` | int | Mốc thời gian gửi |

Ví dụ:

```json
{"v":1,"t":"s","nav":1,"spd":0,"lim":50,"trn":2,"dst":120,"st":"ĐT.281","eta":"12:48","rmin":72,"rm":50877,"ts":1000}
```

## Quy tắc tương thích

- Trường thiếu ⇒ giữ mặc định, không coi là lỗi. Trường lạ ⇒ bỏ qua.
- Mã rẽ / cảnh báo lạ phải hiển thị an toàn (NONE / icon nguy hiểm chung), không đoán.
- Mã 0–9 của cảnh báo bị đóng băng. Chỉ được **thêm** mã / trường mới, không đổi nghĩa.
