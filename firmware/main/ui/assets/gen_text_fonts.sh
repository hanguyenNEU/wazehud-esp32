#!/bin/bash
# Regenerate only text fonts as Regular; retain bold speed/sign digits and icons.
# Source: https://github.com/google/fonts/tree/main/ofl/barlowcondensed (SIL OFL).
set -euo pipefail
OUT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FONT="${FONT_SRC:-/tmp/fonts}/BarlowCondensed-Regular.ttf"
for size in 16 20 22 26; do
  npx --yes lv_font_conv@1.5.3 --font "$FONT" --size "$size" --bpp 4 \
    --format lvgl --force-fast-kern-format --no-compress --lv-include lvgl.h \
    -o "$OUT/font_vn_$size.c" \
    -r 0x20-0x7E -r 0xB7 \
    -r 0xC0-0xC5 -r 0xC8-0xCF -r 0xD2-0xD6 -r 0xD9-0xDD \
    -r 0xE0-0xE5 -r 0xE8-0xEF -r 0xF2-0xF6 -r 0xF9-0xFD \
    -r 0x102-0x103 -r 0x110-0x111 -r 0x128-0x129 -r 0x168-0x169 \
    -r 0x1A0-0x1A1 -r 0x1AF-0x1B0 -r 0x1EA0-0x1EF9
done
for size in 30 34 40; do
  npx --yes lv_font_conv@1.5.3 --font "$FONT" --size "$size" --bpp 4 \
    --format lvgl --force-fast-kern-format --no-compress --lv-include lvgl.h \
    -o "$OUT/font_dist_$size.c" -r 0x30-0x39 -r 0x2C -r 0x6B -r 0x6D -r 0x20
done
