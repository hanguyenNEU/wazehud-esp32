#!/bin/bash
# Sinh font LVGL từ Barlow Condensed (SIL OFL — nhúng vào sản phẩm hợp lệ).
#
# Chọn font condensed vì màn chỉ rộng 42,6 mm: chữ hẹp cho nhiều ký tự hơn trên
# cùng bề ngang mà không giảm chiều cao — thứ quyết định khả năng đọc khi liếc.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/firmware/main/ui/assets"
SRC="${FONT_SRC:-/tmp/fonts}"
CONV="npx --yes lv_font_conv@1.5.3"

mkdir -p "$OUT"

# Dải ký tự tiếng Việt: Latin cơ bản + các ô rời rạc ở Latin-1/Extended-A mà
# tiếng Việt thực sự dùng + khối Latin Extended Additional (dấu tổ hợp dựng sẵn).
# Lấy nguyên 0x100-0x1FF sẽ thừa hơn 200 glyph không bao giờ dùng.
VN_RANGES="-r 0x20-0x7E -r 0xB7 \
  -r 0xC0-0xC5 -r 0xC8-0xCF -r 0xD2-0xD6 -r 0xD9-0xDD \
  -r 0xE0-0xE5 -r 0xE8-0xEF -r 0xF2-0xF6 -r 0xF9-0xFD \
  -r 0x102-0x103 -r 0x110-0x111 -r 0x128-0x129 -r 0x168-0x169 \
  -r 0x1A0-0x1A1 -r 0x1AF-0x1B0 -r 0x1EA0-0x1EF9"

gen() { # tên  file-ttf  cỡ  bpp  dải
  local name=$1 ttf=$2 size=$3 bpp=$4; shift 4
  echo "── $name  ${size}px  ${bpp}bpp"
  $CONV --font "$SRC/$ttf" --size "$size" --bpp "$bpp" \
        --format lvgl --force-fast-kern-format --no-compress \
        --lv-include lvgl.h -o "$OUT/$name.c" "$@"
}

# Số tốc độ: chỉ 0-9. Cỡ 112 cho chiều cao chữ số ~80 px ≈ 10,6 mm,
# tức ~1° góc nhìn từ ghế lái — đủ để liếc mà đọc được.
gen font_speed_112 BarlowCondensed-SemiBold.ttf 112 4 -r 0x30-0x39

# Bố cục kiểu Waze: số nằm trong vòng tròn nên nhỏ hơn, cần hai cỡ riêng.
gen font_circle_70 BarlowCondensed-SemiBold.ttf 70 4 -r 0x30-0x39
gen font_circle_54 BarlowCondensed-SemiBold.ttf 54 4 -r 0x30-0x39

# Biển giới hạn tốc độ trong vòng tròn đỏ.
gen font_limit_40  BarlowCondensed-SemiBold.ttf  40 4 -r 0x30-0x39

# Khoảng cách tới ngã rẽ: số + "m" + "k" + dấu phẩy thập phân.
gen font_dist_30   BarlowCondensed-SemiBold.ttf  30 4 -r 0x30-0x39 -r 0x2C -r 0x6B -r 0x6D -r 0x20

# Tên đường và dòng ETA: cần đủ tiếng Việt.
# shellcheck disable=SC2086
gen font_vn_22     BarlowCondensed-SemiBold.ttf  22 4 $VN_RANGES
# shellcheck disable=SC2086
gen font_vn_16     BarlowCondensed-Medium.ttf    16 4 $VN_RANGES


# --- Cỡ cho màn 2.8" 320x240 (ES3C28P) ---------------------------------------
# Màn lớn hơn 2,6 lần nên phóng to hết mức: chữ số tốc độ đạt ~1,2 độ góc nhìn
# ở khoảng cách 60 cm, vượt cả đồng hồ taplo xe hơi (0,6-0,9 độ).
gen font_circle_100 BarlowCondensed-SemiBold.ttf 100 4 -r 0x30-0x39
gen font_circle_82  BarlowCondensed-SemiBold.ttf  82 4 -r 0x30-0x39
gen font_dist_40    BarlowCondensed-SemiBold.ttf  40 4 -r 0x30-0x39 -r 0x2C -r 0x6B -r 0x6D -r 0x20
# shellcheck disable=SC2086
gen font_vn_26      BarlowCondensed-SemiBold.ttf  26 4 $VN_RANGES
# shellcheck disable=SC2086
gen font_vn_20      BarlowCondensed-Medium.ttf    20 4 $VN_RANGES

# --- Cỡ cho màn vuông 240x240 (AI box xingzhi-abs-2.0) -----------------------
gen font_circle_78 BarlowCondensed-SemiBold.ttf 78 4 -r 0x30-0x39
gen font_circle_62 BarlowCondensed-SemiBold.ttf 62 4 -r 0x30-0x39
gen font_dist_34   BarlowCondensed-SemiBold.ttf 34 4 -r 0x30-0x39 -r 0x2C -r 0x6B -r 0x6D -r 0x20

# --- Icon: Material Symbols Rounded (Apache-2.0) -----------------------------
# Chỉ lấy đúng những codepoint dùng tới. Nguyên bộ font là 14 MB; trích ra còn
# vài chục KB.
ICONS="\
  -r 0xeb95 -r 0xeba6 -r 0xebab -r 0xeba4 -r 0xeb9a -r 0xeba7 -r 0xebaa \
  -r 0xeba1 -r 0xeba2 -r 0xeb99 -r 0xeba3 -r 0xeba0 -r 0xebac -r 0xeb9c \
  -r 0xeb96 -r 0xf06e -r 0xeff5 \
  -r 0xef56 -r 0xf470 -r 0xe565 -r 0xf083 -r 0xebf2 -r 0xf46f -r 0xf08c \
  -r 0xe9e4 -r 0xf08f -r 0xf0be -r 0xe9d1 -r 0xe8e0 -r 0xeff7 -r 0xe1a9"

# shellcheck disable=SC2086
gen font_icon_44 MaterialSymbols.ttf 44 4 $ICONS
# shellcheck disable=SC2086
gen font_icon_26 MaterialSymbols.ttf 26 4 $ICONS
# shellcheck disable=SC2086
gen font_icon_64 MaterialSymbols.ttf 64 4 $ICONS

echo
echo "Kích thước sinh ra:"
for f in "$OUT"/font_*.c; do
  printf "  %-22s %6.0f KB\n" "$(basename "$f")" "$(du -k "$f" | cut -f1)"
done
