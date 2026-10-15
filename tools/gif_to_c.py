#!/usr/bin/env python3
"""Đổi một file GIF bất kỳ thành mảng C nhúng vào firmware.

    .venv/bin/python tools/gif_to_c.py duck.gif
    .venv/bin/python tools/gif_to_c.py duck.gif --size 96 --fps 12

Ràng buộc quan trọng: bộ giải mã GIF của LVGL giữ NGUYÊN MỘT KHUNG trong RAM, và
chỉ định dạng ARGB8888 mới cho màu đúng (đường RGB565 đảo bảng màu, còn
RGB565_SWAPPED không vẽ gì). Tức 4 byte mỗi điểm ảnh:

    128×128 →  64 KB   ✅ an toàn
    160×160 → 100 KB   ⚠️ sát, chỉ còn ~131 KB trống
    240×240 → 225 KB   ❌ không có cửa

Số khung KHÔNG ảnh hưởng RAM, chỉ ảnh hưởng dung lượng flash — mà flash còn gần
9 MB chưa cấp phát, nên cứ thoải mái.
"""

from PIL import Image, ImageSequence
import argparse
import pathlib
import sys

RAM_BUDGET_KB = 100  # cảnh báo khi vượt


def main() -> None:
    ap = argparse.ArgumentParser(description="GIF → mảng C cho LVGL")
    ap.add_argument("gif", help="file GIF nguồn")
    ap.add_argument("--size", type=int, default=128,
                    help="cạnh dài tối đa, mặc định 128")
    ap.add_argument("--fps", type=float, default=0,
                    help="giảm còn bấy nhiêu khung/giây (0 = giữ nguyên)")
    ap.add_argument("--name", default="welcome_gif", help="tên biến C")
    ap.add_argument("--out", default="firmware/main/ui/assets/welcome_gif.c")
    args = ap.parse_args()

    src = Image.open(args.gif)
    frames, durations = [], []
    for frame in ImageSequence.Iterator(src):
        # convert("RGB") sau mỗi seek để Pillow tự ghép các khung vá đè lên nhau
        frames.append(frame.convert("RGB"))
        durations.append(frame.info.get("duration", 80))

    if not frames:
        sys.exit("không đọc được khung nào")

    # Giảm nhịp nếu được yêu cầu: bỏ bớt khung, cộng dồn thời lượng vào khung giữ
    if args.fps > 0:
        target = 1000.0 / args.fps
        kept, kept_dur, acc = [], [], 0.0
        for frame, d in zip(frames, durations):
            acc += d
            if acc >= target or not kept:
                kept.append(frame)
                kept_dur.append(int(round(acc)))
                acc = 0.0
        frames, durations = kept, kept_dur

    w, h = frames[0].size
    scale = min(args.size / w, args.size / h, 1.0)
    if scale < 1.0:
        w, h = max(1, int(w * scale)), max(1, int(h * scale))
        frames = [f.resize((w, h), Image.LANCZOS) for f in frames]

    ram_kb = w * h * 4 / 1024
    if ram_kb > RAM_BUDGET_KB:
        print(f"⚠️  {w}×{h} cần {ram_kb:.0f} KB RAM khi giải mã, quá ngưỡng "
              f"{RAM_BUDGET_KB} KB.\n    Chạy lại với --size nhỏ hơn.",
              file=sys.stderr)

    tmp = pathlib.Path("/tmp/_gif_to_c.gif")
    frames[0].save(tmp, save_all=True, append_images=frames[1:],
                   duration=durations, loop=0, optimize=True)
    data = tmp.read_bytes()

    rows = "\n".join("    " + " ".join(f"0x{b:02X}," for b in data[i:i + 16])
                     for i in range(0, len(data), 16))
    out = pathlib.Path(args.out)
    out.write_text(f'''/* Hoạt ảnh nhúng — sinh bằng tools/gif_to_c.py, đừng sửa tay.
 * Nguồn: {pathlib.Path(args.gif).name} · {len(frames)} khung · {w}×{h}
 *
 * Nhúng thẳng vào flash thay vì để trên thẻ SD: chỉ {len(data)//1024} KB, mà đổi lại
 * không phụ thuộc có cắm thẻ hay không.
 */

#include "lvgl.h"

static const uint8_t {args.name}_data[] = {{
{rows}
}};

const lv_image_dsc_t {args.name} = {{
    .header = {{
        .magic = LV_IMAGE_HEADER_MAGIC,
        /* RAW: LVGL chuyển thẳng khối byte này cho bộ giải mã GIF. */
        .cf = LV_COLOR_FORMAT_RAW,
        .w = {w},
        .h = {h},
    }},
    .data = {args.name}_data,
    .data_size = sizeof({args.name}_data),
}};
''')

    print(f"{out}")
    print(f"  {len(frames)} khung · {w}×{h} · flash {len(data)/1024:.0f} KB "
          f"· RAM giải mã {ram_kb:.0f} KB")


if __name__ == "__main__":
    main()
