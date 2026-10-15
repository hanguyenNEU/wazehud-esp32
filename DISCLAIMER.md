# Tuyên bố miễn trừ (Disclaimer)

*English summary at the end.*

## 1. Dự án độc lập, nhãn hiệu

WazeHUD Community là dự án cộng đồng độc lập. Tên "Waze", "Google", "iPhone",
"iOS", "Apple" và tên các board phần cứng chỉ được nhắc tới để mô tả khả năng
tương thích. Mọi tên và nhãn hiệu thuộc về chủ sở hữu tương ứng.

Dự án **không** liên kết, không được tài trợ, phê duyệt, chứng nhận hay bảo trợ
bởi Waze Mobile, Google, Apple hay nhà sản xuất phần cứng nào. Logo hiển thị lúc
khởi động do tác giả dự án tự vẽ, không phải logo chính thức của Waze.

## 2. An toàn và độ chính xác

- Firmware chỉ hiển thị thông tin bổ trợ. Nó **không** phải đồng hồ tốc độ được
  chứng nhận và không thay thế việc quan sát đường, biển báo và luật giao thông.
- Dữ liệu (hướng rẽ, giới hạn tốc độ, cảnh báo) đến từ ứng dụng trên điện thoại,
  có thể thiếu, trễ hoặc sai. Cảnh báo chỉ hiển thị bằng hình.
- Chỉ cài đặt, cấu hình và thử nghiệm khi xe đã dừng đỗ an toàn. Gắn thiết bị
  sao cho không che tầm nhìn và không cản túi khí.

## 3. Sử dụng hợp pháp và quyền của bên thứ ba

- License MIT chỉ áp dụng cho mã nguồn do dự án viết. Nó không áp dụng cho
  ứng dụng, dữ liệu, giao diện hay nhãn hiệu của bên thứ ba. Asset của bên thứ ba
  giữ license gốc, xem [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- Dự án không cung cấp, không phân phối và không hướng dẫn vượt qua DRM, nội
  dung trả phí hay điều khoản dịch vụ của bên nào. Người dùng tự chịu trách nhiệm
  tuân thủ luật và điều khoản của ứng dụng mình dùng.
- Việc sử dụng thiết bị hiển thị trên xe phải tuân thủ quy định giao thông tại
  nơi bạn lái xe.

## 4. Tương thích và rủi ro thiết bị

- Kết nối với điện thoại dựa trên giao thức HLP/1 và các chi tiết không được
  công bố chính thức. Bản cập nhật ứng dụng có thể làm hỏng tính năng bất kỳ lúc nào.
- Nạp firmware có rủi ro làm hỏng thiết bị. Luôn giữ cách khôi phục (nạp lại qua
  USB). Cấp nguồn trên xe phải ổn định, đúng điện áp.

## 5. Quyền riêng tư

- Firmware không gửi dữ liệu đi đâu ngoài thiết bị và điện thoại đã kết nối.
  Không có máy chủ thu thập dữ liệu. Cập nhật qua Internet chỉ bật khi người build
  tự cấu hình `HUD_OTA_MANIFEST_URL`.
- Mật khẩu WiFi lưu trong NVS của thiết bị. Log serial và file thu
  (`tools/captures/`) có thể chứa tên đường, lộ trình: hãy xem lại trước khi chia sẻ.

## 6. Bảo hành và trách nhiệm

Phần mềm được cung cấp "NGUYÊN TRẠNG" theo điều khoản của [LICENSE](LICENSE),
trong phạm vi pháp luật cho phép. Không bảo đảm tính đúng đắn, phù hợp mục đích,
không xâm phạm quyền hay phù hợp để dùng khi lái xe. Tác giả và người đóng góp
không chịu trách nhiệm cho thiệt hại phát sinh từ việc sử dụng.

## 7. Báo cáo và quan ngại về quyền

Nếu bạn là chủ sở hữu quyền và có quan ngại về nội dung trong repo, hãy mở Issue.
Khi báo lỗi, **không** đăng thông tin đăng nhập, lộ trình/GPS cá nhân, định danh
thiết bị hay bản dump ứng dụng độc quyền.

---

## English summary

WazeHUD Community is an independent project. "Waze", "Google", "Apple" and other
names are used only to describe compatibility; all marks belong to their owners.
The project is not affiliated with, sponsored, approved or endorsed by them. The
boot logo is the author's own artwork, not an official Waze logo. The firmware is a supplementary display, not a
certified speedometer. Data may be missing, stale or wrong. Configure it only
while parked. The MIT license covers project-authored code only. Third-party
assets keep their own licenses. Provided "AS IS" without warranty. Rights
concerns: open an Issue, and do not post credentials or personal routes.
