"""Điều khiển màn Turing Smart Screen 3.5" (320x480) qua USB serial.

Dùng để xem trước giao diện HUD ở kích thước vật lý thật khi board ESP32 chưa
về. Màn to hơn hẳn mọi bo đang cân nhắc (74x49 mm, so với 43x23 mm của
T-Display-S3), nên ngắm cỡ chữ và độ tương phản trên nó sát thực tế hơn nhiều
so với nhìn ảnh trên màn MacBook.

    from turing35 import Turing35
    d = Turing35()                 # 480x320 nằm ngang
    d.show(pil_image)

Giao thức tham khảo mathoudebine/turing-smart-screen-python (GPL-3.0), nhưng có
hai chỗ khác quan trọng, đều rút ra từ thực nghiệm trên chính con màn này:

1. VẼ TỪNG HÀNG MỘT. Gửi cả khung trong một lệnh DISPLAY_BITMAP thì ảnh trượt
   chéo và đổi màu loạn — màn suy ra vị trí hàng sai. Nói thẳng toạ độ y cho
   từng hàng thì hết, và KHÔNG tốn thêm thời gian nào (đo được 1.96 s cho cả
   khung ở cả hai cách).

   Cảnh báo cho ai định kiểm chứng lại: ảnh sọc NGANG không phát hiện được lỗi
   này, vì mỗi hàng chỉ có một màu nên hàng lệch đi vẫn ra sọc ngang đẹp. Phải
   dùng sọc DỌC.

2. KHÔNG DÙNG SET_ORIENTATION SANG LANDSCAPE. Lệnh đó cho ra ảnh nát dù màn
   hoàn toàn khoẻ. Muốn nằm ngang thì tự xoay ảnh bằng Pillow rồi gửi như
   portrait — cùng số byte, cùng thời gian.
"""

import time

import serial
from PIL import Image
from serial.tools.list_ports import comports

NATIVE_W, NATIVE_H = 320, 480

CLEAR, SCREEN_OFF, SCREEN_ON = 102, 108, 109
SET_BRIGHTNESS, SET_ORIENTATION, DISPLAY_BITMAP = 110, 121, 197
PORTRAIT = 0

# Màn chạy ở ~153 KB/s. Nếu một khung đầy (300 KB) gửi xong nhanh hơn ngưỡng
# này thì byte đã rơi vào hư vô: màn kẹt, vẫn nhận dữ liệu, vẫn báo thành công,
# nhưng không vẽ nữa. Lệnh RESET của giao thức KHÔNG cứu được — phải rút cắm
# lại cáp USB. Không có phép đo này thì không tài nào phân biệt "code sai" với
# "phần cứng chết đứng".
THROUGHPUT_KBPS = 153
MIN_RATIO = 0.5


class ScreenStalled(RuntimeError):
    pass


def _pack(x, y, ex, ey, cmd):
    """Nhồi 4 toạ độ 10 bit + mã lệnh vào đúng 6 byte."""
    return bytearray([
        x >> 2,
        ((x & 3) << 6) + (y >> 4),
        ((y & 15) << 4) + (ex >> 6),
        ((ex & 63) << 2) + (ey >> 8),
        ey & 255,
        cmd,
    ])


def find_port():
    for p in comports():
        if p.serial_number == "USB35INCHIPSV2" or (p.vid == 0x1A86 and p.pid == 0x5722):
            return p.device
    return None


class Turing35:
    def __init__(self, port=None, landscape=True, brightness=100):
        port = port or find_port()
        if not port:
            raise RuntimeError("không thấy màn Turing 3.5\" — kiểm tra cáp USB")
        self.s = serial.Serial(port, 115200, timeout=1, rtscts=True)
        self.landscape = landscape

        self._orientation(PORTRAIT)
        time.sleep(0.4)  # màn cần thời gian dựng lại panel sau lệnh xoay
        self.set_brightness(brightness)
        time.sleep(0.2)

    @property
    def size(self):
        """Khổ vẽ mà người gọi làm việc trên đó."""
        return (NATIVE_H, NATIVE_W) if self.landscape else (NATIVE_W, NATIVE_H)

    def _cmd(self, code, x=0, y=0, ex=0, ey=0):
        self.s.write(_pack(x, y, ex, ey, code))
        self.s.flush()

    def _orientation(self, orientation):
        buf = _pack(0, 0, 0, 0, SET_ORIENTATION) + bytearray(10)
        buf[6] = orientation + 100
        buf[7], buf[8] = NATIVE_W >> 8, NATIVE_W & 255
        buf[9], buf[10] = NATIVE_H >> 8, NATIVE_H & 255
        self.s.write(bytes(buf))
        self.s.flush()

    def set_brightness(self, percent):
        """0 tối hẳn … 100 sáng nhất. Phần cứng đánh số ngược lại."""
        self._cmd(SET_BRIGHTNESS, x=int(255 - percent / 100 * 255))

    def screen_on(self):
        self._cmd(SCREEN_ON)

    def screen_off(self):
        self._cmd(SCREEN_OFF)

    def show(self, image, x=0, y=0, fit=True, check=True):
        """Đẩy một ảnh PIL lên màn. (x, y) tính trong hệ toạ độ người gọi thấy."""
        w, h = self.size
        img = image.convert("RGB")
        if fit and img.size != (w, h):
            img = img.resize((w, h), Image.LANCZOS)
        if self.landscape:
            img = img.rotate(-90, expand=True)
            x, y = NATIVE_W - y - img.size[0], x

        iw, ih = img.size
        rgb = img.tobytes()
        started = time.time()

        for row in range(ih):
            base = row * iw * 3
            line = bytearray(iw * 2)
            for i in range(iw):
                r, g, b = rgb[base + i * 3], rgb[base + i * 3 + 1], rgb[base + i * 3 + 2]
                v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                line[i * 2], line[i * 2 + 1] = v & 0xFF, v >> 8  # little-endian
            self.s.write(_pack(x, y + row, x + iw - 1, y + row, DISPLAY_BITMAP))
            self.s.flush()
            # Xả sau MỖI hàng: trên macOS nếu để hệ điều hành tự gom thì màn
            # nhận thiếu byte.
            self.s.write(line)
            self.s.flush()

        elapsed = time.time() - started
        expected = iw * ih * 2 / 1024 / THROUGHPUT_KBPS
        if check and elapsed < expected * MIN_RATIO:
            raise ScreenStalled(
                f"gửi {iw}x{ih} hết {elapsed:.2f}s, đáng lẽ ~{expected:.2f}s. "
                f"Màn đang kẹt — rút cắm lại cáp USB rồi thử lại."
            )
        return elapsed

    def close(self):
        self.s.close()
