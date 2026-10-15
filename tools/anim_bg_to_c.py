#!/usr/bin/env python3
"""Đổi một thư mục khung PNG (ảnh nền pixel-art động) thành mảng C cho màn CLOCK.

    .venv/bin/python tools/anim_bg_to_c.py \\
        "../Animated Pixel-Art Backgrounds Free/Art/Animations/Pixel-Art Background 3" \\
        bg_forest_city --x 80 --y 30 --frame-ms 83

Cắt 1:1 ra 320x240 (không co giãn — pixel-art co tỉ lệ lẻ là nhoè). Lưu khung
đầu nguyên vẹn (RGB565, 150 KB) + chênh lệch từng khung so với khung trước,
dạng các đoạn liên tiếp trên cùng hàng. Nền rừng/thành phố chỉ đổi ~100 điểm
ảnh mỗi khung nên cả vòng lặp 143 khung còn ~200 KB thay vì 21 MB.

Định dạng delta (uint16): mỗi đoạn = [start_hi, start_lo, len, px * len],
start là chỉ số điểm ảnh (y * W + x). frame_offsets[i]..frame_offsets[i+1] là
các đoạn đưa khung i-1 thành khung i; khung 0 đưa khung cuối về khung đầu để
lặp liền mạch. Thứ tự byte RGB565 là của CPU (little-endian) — esp_lvgl_port
tự đảo byte lúc gửi ra panel (swap_bytes trong main.c).
"""
import argparse
import sys
from pathlib import Path

from PIL import Image

W, H = 320, 240
MERGE_GAP = 3  # hai đoạn cách nhau ≤ 3 điểm thì gộp: rẻ hơn thêm 3 word header


def rgb565(img):
    raw = img.tobytes()  # RGB liền nhau
    return [((raw[i] >> 3) << 11) | ((raw[i + 1] >> 2) << 5) | (raw[i + 2] >> 3)
            for i in range(0, len(raw), 3)]


def runs(prev, cur):
    """Các đoạn [start, end) khác nhau giữa 2 khung, không vượt qua cuối hàng."""
    result = []
    for y in range(H):
        row = y * W
        x = 0
        while x < W:
            if prev[row + x] == cur[row + x]:
                x += 1
                continue
            start = x
            end = x + 1
            gap = 0
            x += 1
            while x < W:
                if prev[row + x] != cur[row + x]:
                    end = x + 1
                    gap = 0
                elif gap >= MERGE_GAP:
                    break
                else:
                    gap += 1
                x += 1
            result.append((row + start, row + end))
    return result


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("frames_dir")
    ap.add_argument("name", help="tên biến C, ví dụ bg_forest_city")
    ap.add_argument("--x", type=int, default=None, help="mép trái vùng cắt (mặc định: giữa)")
    ap.add_argument("--y", type=int, default=None, help="mép trên vùng cắt (mặc định: sát đáy)")
    ap.add_argument("--frame-ms", type=int, default=83)
    ap.add_argument("--out", default=None)
    a = ap.parse_args()

    files = sorted(p for p in Path(a.frames_dir).iterdir() if p.suffix.lower() == ".png")
    if not files:
        sys.exit("không có file PNG")

    frames = []
    for f in files:
        im = Image.open(f).convert("RGB")
        x0 = (im.width - W) // 2 if a.x is None else a.x
        y0 = im.height - H if a.y is None else a.y
        if x0 < 0 or y0 < 0 or x0 + W > im.width or y0 + H > im.height:
            sys.exit(f"{f.name}: vùng cắt {x0},{y0} vượt ảnh {im.width}x{im.height}")
        frames.append(rgb565(im.crop((x0, y0, x0 + W, y0 + H))))

    delta = []
    offsets = []
    n = len(frames)
    for i in range(n):
        offsets.append(len(delta))
        prev, cur = frames[i - 1], frames[i]  # i = 0: khung cuối → khung đầu
        for start, end in runs(prev, cur):
            delta += [start >> 16, start & 0xFFFF, end - start]
            delta += cur[start:end]
    offsets.append(len(delta))

    out = Path(a.out or f"firmware/main/ui/assets/{a.name}.c")
    kb_base = W * H * 2 / 1024
    kb_delta = len(delta) * 2 / 1024
    lines = [
        "/* Ảnh nền động — sinh bằng tools/anim_bg_to_c.py, đừng sửa tay.",
        f" * Nguồn: {Path(a.frames_dir).name} · {n} khung · {a.frame_ms} ms/khung",
        f" * {W}x{H} RGB565 · gốc {kb_base:.0f} KB + delta {kb_delta:.0f} KB flash",
        " */",
        "",
        '#include "anim_bg.h"',
        "",
        f"static const uint16_t {a.name}_base[{W * H}] = {{",
    ]
    for i in range(0, len(frames[0]), 16):
        lines.append("    " + ",".join(f"0x{v:04X}" for v in frames[0][i:i + 16]) + ",")
    lines += ["};", "", f"static const uint16_t {a.name}_delta[{max(1, len(delta))}] = {{"]
    for i in range(0, len(delta), 16):
        lines.append("    " + ",".join(f"0x{v:04X}" for v in delta[i:i + 16]) + ",")
    if not delta:
        lines.append("    0,")
    lines += ["};", "", f"static const uint32_t {a.name}_offsets[{n + 1}] = {{"]
    for i in range(0, len(offsets), 12):
        lines.append("    " + ",".join(str(v) for v in offsets[i:i + 12]) + ",")
    lines += [
        "};",
        "",
        f"const anim_bg_t {a.name} = {{",
        f"    .w = {W},",
        f"    .h = {H},",
        f"    .frame_count = {n},",
        f"    .frame_ms = {a.frame_ms},",
        f"    .base = {a.name}_base,",
        f"    .delta = {a.name}_delta,",
        f"    .frame_offsets = {a.name}_offsets,",
        "};",
        "",
    ]
    out.write_text("\n".join(lines))
    print(f"{out}  {n} khung  gốc {kb_base:.0f} KB + delta {kb_delta:.0f} KB")


if __name__ == "__main__":
    sys.exit(main())
