#!/bin/bash
# Đối chiếu decoder C (thứ sẽ chạy trên ESP32) với bản tham chiếu Python viết
# độc lập từ đặc tả. Không cần phần cứng.
set -uo pipefail

# CLT SDK 27 có tbd định dạng mới mà ld của CLT chưa đọc được ("unknown
# architecture arm64e.x1-macos"); dùng SDK của Xcode nếu chưa có SDKROOT.
if [ "$(uname -s)" = Darwin ] && [ -z "${SDKROOT:-}" ]; then
  XSDK=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk
  [ -d "$XSDK" ] && export SDKROOT="$XSDK"
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT="$ROOT/test/host/build"
# IDF 6.x đã gỡ components/json khỏi IDF; cJSON giờ ở managed_components
# qua dependency espressif/cjson. Fallback đường dẫn cũ (IDF <=5.x).
if [ -z "${CJSON:-}" ]; then
  CJSON="$ROOT/firmware/managed_components/espressif__cjson/cJSON"
  if [ ! -f "$CJSON/cJSON.c" ]; then
    CJSON="${IDF_PATH:-$HOME/esp/esp-idf}/components/json/cJSON"
  fi
fi

if [ ! -f "$CJSON/cJSON.c" ]; then
  echo "Không thấy cJSON trong $CJSON — đặt IDF_PATH cho đúng." >&2
  exit 1
fi

mkdir -p "$OUT"

cc -std=gnu17 -O1 -Wall -Wextra -Werror \
   -I "$ROOT/firmware/main/hlp" -I "$CJSON" \
   "$ROOT/test/host/protocol_test.c" \
   "$ROOT/firmware/main/hlp/hlp_core.c" \
   "$ROOT/firmware/main/hlp/hlp_messages.c" \
   "$CJSON/cJSON.c" -o "$OUT/protocol_test" || exit 1
"$OUT/protocol_test" || exit 1
cc -std=gnu17 -O1 -Wall -Wextra -Werror \
   -I "$ROOT/firmware/main/hlp" -I "$ROOT/firmware/main/app" -I "$CJSON" \
   "$ROOT/test/host/alerts_test.c" \
   "$ROOT/firmware/main/app/hud_alerts.c" \
   "$ROOT/firmware/main/app/hud_decode.c" \
   "$CJSON/cJSON.c" -o "$OUT/alerts_test" || exit 1
"$OUT/alerts_test" || exit 1

# Ghép mảnh kênh tin 8a7e0012 (thống kê chuyến đi).
cc -std=gnu17 -O1 -Wall -Wextra -Werror \
   -I "$ROOT/firmware/main/app" \
   "$ROOT/test/host/msg_asm_test.c" \
   "$ROOT/firmware/main/app/msg_asm.c" -o "$OUT/msg_asm_test" || exit 1
"$OUT/msg_asm_test" || exit 1

# Bộ test mới cho kiến trúc UI: gesture, lịch âm, đồng hồ, cấu hình.
cc -std=gnu17 -O1 -Wall -Wextra -Werror \
   -I "$ROOT/firmware/main/system" \
   "$ROOT/test/host/gesture_test.c" -o "$OUT/gesture_test" || exit 1
"$OUT/gesture_test" || exit 1
cc -std=gnu17 -O1 -Wall -Wextra -Werror \
   -I "$ROOT/firmware/main/system" \
   "$ROOT/test/host/lunar_test.c" -o "$OUT/lunar_test" || exit 1
"$OUT/lunar_test" || exit 1
cc -std=gnu17 -O1 -Wall -Wextra -Werror \
   -I "$ROOT/test/host" -I "$ROOT/firmware/main/system" \
   "$ROOT/test/host/clock_test.c" -o "$OUT/clock_test" || exit 1
"$OUT/clock_test" || exit 1
cc -std=gnu17 -O1 -Wall -Wextra -Werror \
   -I "$ROOT/test/host" -I "$ROOT/firmware/main/hlp" \
   -I "$ROOT/firmware/main/app" -I "$CJSON" \
   "$ROOT/test/host/config_test.c" "$CJSON/cJSON.c" -o "$OUT/config_test" || exit 1
"$OUT/config_test" || exit 1

echo "── biên dịch"
cc -std=gnu17 -O1 -Wall -Wextra -Werror \
   -I "$ROOT/firmware/main/hlp" \
   -I "$ROOT/firmware/main/app" \
   -I "$CJSON" \
   "$ROOT/test/host/decode_dump.c" \
   "$ROOT/firmware/main/hlp/hlp_core.c" \
   "$ROOT/firmware/main/app/hud_decode.c" \
   "$CJSON/cJSON.c" \
   -o "$OUT/decode_dump" || exit 1

PY="$ROOT/.venv/bin/python"
[ -x "$PY" ] || PY=python3

fail=0
for capture in "$@"; do
  name="$(basename "$capture")"
  echo
  echo "── $name"

  # File .jsonl là bản thu; file khác coi như đã là byte HLP thô.
  if [[ "$capture" == *.jsonl ]]; then
  "$PY" - "$capture" "$OUT/raw.txt" <<'PY'
import json, sys
src, dst = sys.argv[1], sys.argv[2]
with open(src, encoding="utf-8") as fh, open(dst, "w", encoding="utf-8") as out:
    for raw in fh:
        raw = raw.strip()
        if not raw:
            continue
        entry = json.loads(raw)
        out.write(entry["line"] if isinstance(entry, dict) and "line" in entry else raw)
        out.write("\n")
PY
  else
    cp "$capture" "$OUT/raw.txt"
  fi

  "$OUT/decode_dump" "$OUT/raw.txt" > "$OUT/c.tsv" 2> "$OUT/c.err"
  "$PY" "$ROOT/test/host/ref_dump.py" "$OUT/raw.txt" > "$OUT/py.tsv" 2> "$OUT/py.err"

  cat "$OUT/c.err" | sed 's/^/   C : /'
  cat "$OUT/py.err" | sed 's/^/   PY: /'

  if diff -q "$OUT/c.tsv" "$OUT/py.tsv" >/dev/null; then
    echo "   ✅ khớp $(wc -l < "$OUT/c.tsv" | tr -d ' ') dòng"
  else
    echo "   ❌ LỆCH — 5 khác biệt đầu tiên:"
    diff "$OUT/c.tsv" "$OUT/py.tsv" | head -20 | sed 's/^/      /'
    fail=1
  fi
done

echo
[ $fail -eq 0 ] && echo "TẤT CẢ KHỚP" || echo "CÓ SAI LỆCH"
exit $fail
