#pragma once

/* Đăng ký FT6336G của ES3C28P làm pointer input cho LVGL. */
void touch_es3c28p_init(void);

/* Nạp phép hiệu chỉnh đã lưu trong NVS; nếu chưa có (lần đầu chạy trên máy
   này) thì tự chạy hiệu chỉnh 2 điểm ngay. PHẢI gọi SAU khi đã unlock
   lvgl_port ở app_main — hàm này tự khoá/mở lvgl_port theo từng bước, gọi
   từ trong 1 khối lock() đang giữ sẵn sẽ làm treo tác vụ vẽ nền. */
void touch_es3c28p_calibrate_if_needed(void);

/* Yêu cầu hiệu chỉnh lại — an toàn gọi từ bất kỳ đâu (chỉ set 1 cờ, một
   task nền riêng sẽ chạy hiệu chỉnh). Tự động kích hoạt khi giữ tay chạm
   liên tục ≥3s ở bất kỳ đâu trên màn (xem read_cb trong .c) — cố tình dùng
   THỜI GIAN giữ tay thay vì vị trí, vì lúc cần hiệu chỉnh lại thì toạ độ
   đang tính ra có thể sai hoàn toàn, không thể dựa vào nó để bấm 1 nút. */
void touch_es3c28p_request_recalibrate(void);
