#pragma once

/* Đồng bộ giờ qua NTP khi có WiFi — bổ sung cho `hi.unix` của HLP để đồng hồ
   có giờ đúng cả khi chưa nối điện thoại. Kết quả đi vào hud_clock_sync()
   (mailbox), cùng đường với BLE; BLE tới sau vẫn ghi đè bình thường.
   Múi giờ: lấy của lần sync BLE gần nhất, chưa có thì mặc định UTC+7. */

/* Gọi khi WiFi vừa có IP. Lần đầu khởi động SNTP; sau đó SNTP tự cập nhật
   định kỳ (CONFIG_LWIP_SNTP_UPDATE_DELAY), gọi lại là no-op. */
void ntp_sync_on_connected(void);
