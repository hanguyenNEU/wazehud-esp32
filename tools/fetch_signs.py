#!/usr/bin/env python3
"""Tải biển báo giao thông Việt Nam (QCVN 41) từ Wikimedia Commons.

    .venv/bin/python tools/fetch_signs.py --width 64

Commons tự rasterise SVG sang PNG ở bề rộng mình yêu cầu, nên máy không cần
cài rsvg/inkscape/cairosvg.

Biển do Chính phủ Việt Nam ban hành nên thuộc phạm vi công cộng; script vẫn
ghi lại giấy phép của từng file vào manifest.json để sau này kiểm chứng được.
"""
import argparse, json, time, urllib.request, urllib.parse
from pathlib import Path

API = "https://commons.wikimedia.org/w/api.php"
# Chính sách của Wikimedia: User-Agent phải mô tả được, UA mặc định bị chặn 403.
UA = {"User-Agent": "WazeHUD-personal-project/0.1 (embedded car HUD)"}

# Mã QCVN 41 ↔ mã cảnh báo HLP (§4.2, xem firmware/main/app/hud_state.h).
SIGNS = {
    "P125":  ("Vietnam road sign P.125 (QCVN 41-2019-BGTVT).svg", 9,  "cấm vượt"),
    "P101":  ("Vietnam road sign P101.svg",  7,  "đường cấm"),
    "W210":  ("Vietnam road sign W210.svg",  11, "giao nhau đường sắt có rào"),
    "W211a": ("Vietnam road sign W211a.svg", 11, "giao nhau đường sắt không rào"),
    "W245a": ("Vietnam road sign W245a.svg", 6,  "đi chậm"),
    "W247":  ("Vietnam road sign W247.svg",  13, "chú ý xe đỗ"),
    "DP127a":("Vietnam road sign DP127a.svg", 8, "hết hạn chế tốc độ tối đa"),
}

def fetch(url, tries=4):
    """Commons trả 429 nếu gọi dồn. Chờ tăng dần rồi thử lại thay vì bỏ cuộc."""
    for attempt in range(tries):
        try:
            req = urllib.request.Request(url, headers=UA)
            with urllib.request.urlopen(req, timeout=30) as r:
                return r.read()
        except urllib.error.HTTPError as e:
            if e.code != 429 or attempt == tries - 1:
                raise
            wait = 3 * (attempt + 1)
            print(f"    429, chờ {wait}s rồi thử lại…")
            time.sleep(wait)

def api(params):
    return json.loads(fetch(f"{API}?{urllib.parse.urlencode(params)}"))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--width", type=int, default=64)
    ap.add_argument("--out", default="assets/signs")
    a = ap.parse_args()

    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    manifest = {}

    for code, (filename, hlp, vn) in SIGNS.items():
        d = api({"action": "query", "prop": "imageinfo",
                 "titles": f"File:{filename}",
                 "iiprop": "url|extmetadata", "iiurlwidth": a.width,
                 "format": "json"})
        page = list(d["query"]["pages"].values())[0]
        if "imageinfo" not in page:
            print(f"  {code:7s} KHÔNG TÌM THẤY  {filename}")
            continue
        info = page["imageinfo"][0]
        url = info["thumburl"]
        meta = info.get("extmetadata", {})

        data = fetch(url)
        png = out / f"{code}.png"
        png.write_bytes(data)

        manifest[code] = {
            "file": png.name,
            "source": info["descriptionurl"],
            "license": meta.get("LicenseShortName", {}).get("value", "?"),
            "author": meta.get("Artist", {}).get("value", "?"),
            "hlp_alert": hlp,
            "vi": vn,
        }
        print(f"  {code:7s} alr={hlp:<2} {vn:32s} {len(data)/1024:5.1f} KB")
        time.sleep(1.5)   # gọi thưa cho lịch sự

    (out / "manifest.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=1))
    print(f"\n{len(manifest)} biển → {out}/  (kèm manifest.json ghi nguồn + giấy phép)")

if __name__ == "__main__":
    main()
