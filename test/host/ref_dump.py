#!/usr/bin/env python3
"""Bản tham chiếu độc lập, viết thẳng từ đặc tả HLP/1 §3.

Cố ý KHÔNG dùng lại tools/hlp.py: hai bản triển khai độc lập mà cho cùng kết
quả mới là bằng chứng, chứ hai bản dùng chung một hàm thì chỉ chứng minh chúng
sai giống nhau.

In ra cùng định dạng phân cách bằng TAB như decode_dump.c.
"""

import json
import pathlib
import sys

STREET_MAX = 128  # phải khớp HUD_STREET_MAX


def clip(text: str) -> str:
    """Cắt cho vừa buffer nhưng không cắt giữa một ký tự UTF-8.

    Phải xét byte ĐẦU TIÊN BỊ LOẠI, không phải byte cuối được giữ: byte cuối
    của một ký tự nhiều byte cũng là byte nối, nên xét nó sẽ xoá nhầm cả một
    ký tự vốn đã vừa buffer.
    """
    raw = text.encode("utf-8")
    if len(raw) < STREET_MAX:
        return text
    cut = STREET_MAX - 1
    while cut > 0 and (raw[cut] & 0xC0) == 0x80:
        cut -= 1
    return raw[:cut].decode("utf-8")


def num(obj, key, default):
    value = obj.get(key)
    return int(value) if isinstance(value, (int, float)) and not isinstance(value, bool) else default


def text(obj, key):
    value = obj.get(key)
    return clip(value) if isinstance(value, str) else ""


def dump(obj) -> str:
    nav = 1 if num(obj, "nav", 0) else 0
    over = 1 if num(obj, "over", 0) else 0

    # `rm` có thẩm quyền; chỉ quy đổi từ `rkm` khi producer cũ không gửi `rm`.
    if isinstance(obj.get("rm"), (int, float)):
        remain_m = int(obj["rm"])
    elif isinstance(obj.get("rkm"), (int, float)):
        remain_m = int(obj["rkm"] * 1000 + 0.5)
    else:
        remain_m = 0

    alert = num(obj, "alr", 0)
    alert_d = num(obj, "alrD", -1)
    alert_v = obj.get("alrV")
    alert_has_v = isinstance(alert_v, (int, float))

    lanes = obj.get("lan")
    lanes_present = isinstance(lanes, list)
    lane_pairs = []
    if lanes_present:
        for entry in lanes[:12]:
            if isinstance(entry, list) and len(entry) >= 2 \
                    and all(isinstance(v, (int, float)) for v in entry[:2]):
                lane_pairs.append((int(entry[0]) & 0xFF, int(entry[1]) & 0xFF))

    alrs = obj.get("alrs")
    alerts_present = isinstance(alrs, list)
    alert_list = []
    if alerts_present:
        for entry in alrs[:4]:
            if not isinstance(entry, dict):
                continue
            value = entry.get("v")
            alert_list.append((num(entry, "k", 0), num(entry, "d", -1),
                               int(value) if isinstance(value, (int, float)) else None))

    # alr/alrD/alrV mirror alrs[0]; bù lại khi producer cắt bớt vì giới hạn frame.
    if alert == 0 and alert_list:
        alert, alert_d, value = alert_list[0]
        alert_has_v = value is not None
        alert_v = value

    def fmt_lanes():
        if not lanes_present:
            return "-"
        if not lane_pairs:
            return "[]"
        return ",".join(f"{d}:{s}" for d, s in lane_pairs)

    def fmt_alerts():
        if not alerts_present:
            return "-"
        if not alert_list:
            return "[]"
        return ",".join(f"{k}/{d}" + (f"/{v}" if v is not None else "")
                        for k, d, v in alert_list)

    return "\t".join(str(x) for x in [
        nav, num(obj, "spd", 0), num(obj, "lim", 0), over,
        num(obj, "trn", 0), num(obj, "trn2", 0), num(obj, "dst", -1),
        num(obj, "exit", 0), remain_m, num(obj, "rmin", 0),
        text(obj, "eta") or "-",
        1 if num(obj, "avg", 0) else 0, num(obj, "avgL", 0),
        num(obj, "avgR", 0), num(obj, "avgP", 0),
        alert, alert_d, int(alert_v) if alert_has_v else "-",
        text(obj, "st") or "-", text(obj, "st2") or "-",
        fmt_lanes(), fmt_alerts(), num(obj, "ts", 0),
    ])


def valid_utf8(raw: bytes) -> bool:
    try:
        raw.decode("utf-8")
    except UnicodeDecodeError:
        return False
    return True


def frames(path: str):
    """Ghép frame theo §1, viết độc lập với hlp_core.c.

    Mỗi dòng kết bằng LF; `\r` cuối dòng bị XOÁ chứ không chỉ bỏ qua; dòng dài
    quá 511 byte payload bị bỏ và đồng bộ lại ở LF kế; dòng không hợp lệ UTF-8
    bị loại.
    """
    raw = pathlib.Path(path).read_bytes()
    buf = bytearray()
    overflow = False
    for byte in raw:
        if byte == 0x0A:
            if not overflow:
                line = buf[:-1] if buf.endswith(b"\r") else buf
                if valid_utf8(bytes(line)):
                    yield bytes(line).decode("utf-8")
            buf, overflow = bytearray(), False
        elif not overflow:
            if len(buf) >= 511:
                overflow = True
            else:
                buf.append(byte)


def main() -> None:
    count = 0
    for line in frames(sys.argv[1]):
        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            continue
        if not isinstance(obj, dict) or obj.get("v") != 1 or obj.get("t") != "s":
            continue
        print(dump(obj))
        count += 1
    print(f"state decode được: {count}", file=sys.stderr)

if __name__ == "__main__":
    main()
