#!/usr/bin/env python3
"""Đổi một file PNG có kênh alpha thành mảng C nhúng vào firmware.

    .venv/bin/python tools/png_to_c.py my_car.png car_img --width 100

Dùng ARGB8888 vì ảnh có nền trong suốt và phải chồng lên nền đen của HUD.
Thứ tự byte trong bộ nhớ của LVGL là B, G, R, A (xem lv_color32_t), KHÔNG phải
R, G, B, A — ghi nhầm thì xe hoá xanh lam.

Ảnh nằm trong flash dưới dạng const nên LVGL vẽ thẳng, không tốn RAM.
"""
import sys, argparse
from pathlib import Path
from PIL import Image

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("png")
    ap.add_argument("name", help="tên biến C, ví dụ car_img")
    ap.add_argument("--width", type=int, default=100)
    ap.add_argument("--out", default=None)
    ap.add_argument("--format", choices=["argb8888", "a8"], default="argb8888",
                    help="a8 = chỉ giữ kênh alpha, 1 byte/điểm ảnh; LVGL tô màu "
                         "bằng style image_recolor nên một file dùng được cho "
                         "cả theme ngày lẫn đêm")
    a = ap.parse_args()

    im = Image.open(a.png).convert("RGBA")
    # Cắt sát vùng có alpha: ảnh nguồn thường thừa rất nhiều nền trong suốt,
    # giữ lại thì vừa tốn flash vừa khó canh vị trí trên màn.
    box = im.getchannel("A").getbbox()
    if box:
        im = im.crop(box)
    h = max(1, round(im.height * a.width / im.width))
    im = im.resize((a.width, h), Image.LANCZOS)

    px = im.load()
    body = []
    if a.format == "a8":
        # Chỉ giữ hình dáng. Dùng cho ký hiệu một màu; biển nhiều màu như P.125
        # (xe đỏ + xe đen) thì KHÔNG dùng được, phải để ARGB8888.
        for y in range(h):
            for x in range(a.width):
                body.append((px[x, y][3],))
    else:
        for y in range(h):
            for x in range(a.width):
                r, g, b, al = px[x, y]
                body.append((b, g, r, al))      # thứ tự của lv_color32_t

    out = Path(a.out or f"firmware/main/ui/assets/{a.name}.c")
    lines = [
        f"/* Ảnh nhúng — sinh bằng tools/png_to_c.py, đừng sửa tay.",
        f" * Nguồn: {Path(a.png).name} · {a.width}x{h} · {a.format.upper()} · "
        f"{len(body)*len(body[0])/1024:.1f} KB flash",
        f" */",
        "",
        '#include "lvgl.h"',
        "",
        f"static const uint8_t {a.name}_data[] = {{",
    ]
    bpp = 1 if a.format == "a8" else 4
    flat = [v for px4 in body for v in px4]
    for i in range(0, len(flat), 16):
        lines.append("    " + ", ".join(f"0x{v:02X}" for v in flat[i:i+16]) + ",")
    lines += [
        "};",
        "",
        f"const lv_image_dsc_t {a.name} = {{",
        "    .header.magic  = LV_IMAGE_HEADER_MAGIC,",
        "    .header.cf     = LV_COLOR_FORMAT_{CF},",
        "    .header.flags  = 0,",
        f"    .header.w      = {a.width},",
        f"    .header.h      = {h},",
        f"    .header.stride = {a.width * bpp},",
        "    .header.reserved_2 = 0,",
        f"    .data_size = sizeof({a.name}_data),",
        f"    .data      = {a.name}_data,",
        "};",
        "",
    ]
    cf = "A8" if a.format == "a8" else "ARGB8888"
    out.write_text("\n".join(lines).replace("{CF}", cf))
    print(f"{out}  {a.width}x{h}  {a.format}  {len(flat)/1024:.1f} KB")

if __name__ == "__main__":
    sys.exit(main())
