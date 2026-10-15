#!/usr/bin/env python3
"""Sinh hoạt ảnh chào mừng cho màn 240x240.

Cỡ 128x128 là có chủ ý: bộ giải mã GIF của LVGL giữ nguyên một khung trong RAM,
ở RGB565 thì 160x160 tốn 50 KB — vừa thoải mái trong 131 KB còn trống. Toàn màn
240x240 sẽ tốn 112 KB, sát quá.
"""

from PIL import Image, ImageDraw
import pathlib, sys

S = 128
BG, FACE = (0, 0, 0), (255, 255, 255)
ACC = (30, 111, 232)  # cùng tông xanh với bong bóng tốc độ trong HUD


def face(eye_open, mouth, glow):
    im = Image.new("RGB", (S, S), BG)
    d = ImageDraw.Draw(im)
    if glow > 0:
        r = int(S * 0.46)
        d.ellipse([S // 2 - r, S // 2 - r, S // 2 + r, S // 2 + r],
                  outline=tuple(int(v * glow) for v in ACC), width=3)
    for cx in (S * 0.34, S * 0.66):
        w = S * 0.16
        h = w * max(eye_open, 0.06)
        d.ellipse([cx - w / 2, S * 0.42 - h / 2, cx + w / 2, S * 0.42 + h / 2],
                  fill=FACE)
    mw, depth = S * 0.34, S * 0.14 * mouth
    if mouth > 0.05:
        d.arc([S / 2 - mw / 2, S * 0.58 - depth, S / 2 + mw / 2, S * 0.58 + depth],
              start=10, end=170, fill=FACE, width=max(3, S // 32))
    else:
        d.line([S / 2 - mw / 3, S * 0.60, S / 2 + mw / 3, S * 0.60],
               fill=FACE, width=max(3, S // 32))
    return im


frames = [face(i / 7, 0.0, i / 7 * 0.9) for i in range(8)]        # mắt mở dần
frames += [face(v, 0.1, 0.9) for v in (0.6, 0.15, 0.6, 1.0)]      # nháy mắt
frames += [face(1.0, i / 5, 0.9) for i in range(6)]               # cười lên
frames += [face(1.0, 1.0, 0.9)] * 4                               # giữ

out = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "sim/welcome.gif")
frames[0].save(out, save_all=True, append_images=frames[1:],
               duration=70, loop=0, optimize=True)
print(f"{out} · {len(frames)} khung · {out.stat().st_size/1024:.0f} KB "
      f"· RAM giải mã RGB565 {S*S*2//1024} KB")
