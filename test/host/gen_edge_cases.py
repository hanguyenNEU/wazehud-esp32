#!/usr/bin/env python3
"""Sinh file byte HLP thô chứa các ca dễ làm firmware sai.

Ghi thẳng ra byte chứ không qua JSON, để dựng được cả những ca không hợp lệ
UTF-8 và frame vượt 512 byte.
"""

import json
import pathlib
import sys


def line(obj) -> bytes:
    return json.dumps(obj, ensure_ascii=False,
                      separators=(",", ":")).encode("utf-8") + b"\n"


def build() -> bytes:
    out = bytearray()

    # Chuẩn, đủ field.
    out += line({"v": 1, "t": "s", "nav": 1, "spd": 47, "lim": 50, "over": 0,
                 "trn": 8, "trn2": 2, "dst": 47, "exit": 0,
                 "lan": [[5, 4], [1, 1], [17, 16]],
                 "st": "Nguyễn Trãi", "st2": "Khuất Duy Tiến",
                 "eta": "20:01", "rmin": 23, "rm": 6742, "rkm": 6.7,
                 "avg": 0, "alr": 2, "alrD": 300,
                 "alrs": [{"k": 2, "d": 300}, {"k": 8, "d": 800, "v": 40}],
                 "ts": 124890})

    # Thiếu gần hết field ⇒ phải rơi về mặc định, đặc biệt dst/alrD = -1.
    out += line({"v": 1, "t": "s", "spd": 0, "lim": 50, "ts": 1})

    # Ngay sau message dày ở trên: nếu decoder không reset struct thì giá trị cũ
    # sẽ dính lại. Đây là bug số một cần bắt.
    out += line({"v": 1, "t": "s", "ts": 2})

    # CRLF — `\r` phải bị XOÁ, không được lọt vào tên đường.
    out += (b'{"v":1,"t":"s","st":"'
            + "Nguyễn Trãi".encode("utf-8") + b'","ts":3}\r\n')

    # Mã trn và alr lạ: giữ nguyên số, renderer tự fallback.
    out += line({"v": 1, "t": "s", "trn": 42, "trn2": 99, "alr": 61,
                 "alrD": 500, "ts": 4})

    # trn=19 đi thẳng qua vòng xuyến — mở rộng của HLP/1, có exit kèm.
    out += line({"v": 1, "t": "s", "trn": 19, "exit": 3, "dst": 250, "ts": 5})

    # Chỉ có rkm, không có rm ⇒ phải quy đổi ra mét.
    out += line({"v": 1, "t": "s", "rkm": 6.7, "rmin": 12, "ts": 6})

    # rm và rkm mâu thuẫn ⇒ rm thắng.
    out += line({"v": 1, "t": "s", "rm": 1234, "rkm": 9.9, "ts": 7})

    # Mảng rỗng = xoá chỉ dẫn cũ, khác với thiếu key.
    out += line({"v": 1, "t": "s", "lan": [], "alrs": [], "ts": 8})

    # alr vắng nhưng alrs còn ⇒ phải mirror từ alrs[0], gồm cả `v`.
    out += line({"v": 1, "t": "s", "alrs": [{"k": 8, "d": 250, "v": 40},
                                            {"k": 1, "d": 900}], "ts": 9})

    # Nhiều hơn 4 cảnh báo và hơn 12 làn ⇒ phải cắt, không tràn buffer.
    out += line({"v": 1, "t": "s",
                 "alrs": [{"k": i, "d": i * 100} for i in range(1, 8)],
                 "lan": [[1, 1]] * 20, "ts": 10})

    # Vùng cấm vượt đầy đủ.
    out += line({"v": 1, "t": "s", "avg": 1, "avgL": 1400, "avgR": 58,
                 "avgP": 37, "lim": 60, "ts": 11})

    # Tên đường dài hơn 40 code point, toàn dấu tiếng Việt (3 byte/ký tự):
    # phép cắt không được xé đôi một ký tự UTF-8.
    out += line({"v": 1, "t": "s", "st": "Đường " + "ệ" * 60,
                 "st2": "ọ" * 50, "ts": 12})

    # Loại message khác phải bị bỏ qua, không đụng vào state.
    out += line({"v": 1, "t": "ping", "ts": 1046540208})
    out += line({"v": 1, "t": "hi", "sess": 48213, "rate": 4})
    out += line({"v": 1, "t": "cfg_begin", "rev": 7, "count": 2})

    # Phiên bản không hỗ trợ ⇒ bỏ qua.
    out += line({"v": 2, "t": "s", "spd": 999, "ts": 13})

    # JSON hỏng ⇒ bỏ dòng, đồng bộ lại ở LF kế.
    out += b'{"v":1,"t":"s","spd":\n'

    # Frame vượt 512 byte ⇒ bỏ cả dòng.
    out += b'{"v":1,"t":"s","st":"' + b"x" * 600 + b'","ts":14}\n'

    # Ngay sau dòng tràn: phải decode bình thường, chứng minh đã resync.
    out += line({"v": 1, "t": "s", "spd": 55, "ts": 15})

    # UTF-8 hỏng ⇒ loại cả dòng.
    out += b'{"v":1,"t":"s","st":"\xff\xfe","ts":16}\n'

    # Dòng cuối cùng, chốt lại là stream vẫn lành.
    out += line({"v": 1, "t": "s", "nav": 1, "spd": 61, "lim": 80, "trn": 5,
                 "dst": 900, "st": "CT.HCM - Long Thành - Dầu Giây",
                 "st2": "Quốc lộ 51", "eta": "20:18", "rmin": 6, "rm": 1800,
                 "ts": 17})

    return bytes(out)


def main() -> None:
    dst = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                       else "test/host/edge_cases.hlp")
    dst.parent.mkdir(parents=True, exist_ok=True)
    data = build()
    dst.write_bytes(data)
    print(f"{dst}: {len(data)} byte, {data.count(10)} dòng")


if __name__ == "__main__":
    main()
