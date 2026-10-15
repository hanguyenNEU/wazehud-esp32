"""HLP/1 codec — bản Python phản chiếu đúng ngữ nghĩa của shared/hlp_core.c.

Giữ cùng hành vi với firmware C để những gì test được ở đây cũng đúng trên ESP32:
buffer 512 byte, bỏ dòng tràn tới LF kế, xoá `\\r` cuối dòng trước khi kết thúc
chuỗi, và loại dòng không hợp lệ UTF-8.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Callable

HLP_MAX_FRAME = 512
HLP_MAX_PAYLOAD = HLP_MAX_FRAME - 1


class Reassembler:
    """Ghép byte thành dòng HLP. Ranh giới packet BLE không mang ý nghĩa."""

    def __init__(self, on_line: Callable[[str], None]) -> None:
        self._on_line = on_line
        self._buf = bytearray()
        self._overflow = False
        self.oversized = 0
        self.malformed_utf8 = 0
        self.lines = 0
        self.max_frame = 0

    def feed(self, data: bytes) -> None:
        for byte in data:
            if byte == 0x0A:
                self._flush()
            elif not self._overflow:
                if len(self._buf) >= HLP_MAX_PAYLOAD:
                    self._overflow = True
                else:
                    self._buf.append(byte)

    def _flush(self) -> None:
        if self._overflow:
            self.oversized += 1
        else:
            raw = self._buf
            if raw and raw[-1] == 0x0D:  # xoá \r cuối dòng, không chỉ bỏ qua
                raw = raw[:-1]
            self.max_frame = max(self.max_frame, len(raw) + 1)
            try:
                line = raw.decode("utf-8")
            except UnicodeDecodeError:
                self.malformed_utf8 += 1
            else:
                self.lines += 1
                self._on_line(line)
        self._buf = bytearray()
        self._overflow = False


# --- Enum §4.1 / §4.2 -------------------------------------------------------

TURNS = {
    0: ("", "Không"),
    1: ("↑", "Đi thẳng"),
    2: ("←", "Rẽ trái"),
    3: ("→", "Rẽ phải"),
    4: ("↖", "Chếch trái"),
    5: ("↗", "Chếch phải"),
    6: ("↰", "Gắt trái"),
    7: ("↱", "Gắt phải"),
    8: ("⟲", "Quay đầu"),
    9: ("⟳", "Quay đầu phải"),
    10: ("◍", "Vòng xuyến"),
    11: ("◍←", "Vòng xuyến trái"),
    12: ("◍→", "Vòng xuyến phải"),
    13: ("⇖", "Giữ trái"),
    14: ("⇗", "Giữ phải"),
    15: ("⤺", "Ra nhánh trái"),
    16: ("⤻", "Ra nhánh phải"),
    17: ("⚑", "Đến nơi"),
    18: ("⛴", "Đi phà"),
    19: ("◍↑", "Thẳng qua vòng xuyến"),
}

ALERTS = {
    0: "Không",
    1: "Cảnh sát",
    2: "Camera tốc độ",
    3: "Camera đèn đỏ",
    4: "Nguy hiểm",
    5: "Tai nạn",
    6: "Kẹt xe",
    7: "Đường đóng",
    8: "Giảm tốc độ",
    9: "Cấm vượt",
    10: "Hết cấm vượt",
    11: "Đường sắt",
    12: "Trạm thu phí",
    13: "Xe dừng",
}


def turn_name(code: int) -> tuple[str, str]:
    """Mã lạ phải xử như 0 — không đoán thành hướng khác (§4.1)."""
    return TURNS.get(code, ("", f"Mã lạ {code}"))


def alert_name(code: int) -> str:
    """Mã 10..74 chưa biết fallback về nguy hiểm chung (§4.2)."""
    if code in ALERTS:
        return ALERTS[code]
    return f"Nguy hiểm (mã {code})"


# --- Message trạng thái -----------------------------------------------------


@dataclass
class State:
    """Snapshot đầy đủ. Mặc định đúng theo §3: 0 cho lim/exit/avg*, -1 cho dst/alrD."""

    nav: int = 0
    spd: int = 0
    lim: int = 0
    over: int = 0
    trn: int = 0
    trn2: int = 0
    dst: int = -1
    exit: int = 0
    lan: list = field(default_factory=list)
    st: str = ""
    st2: str = ""
    eta: str = ""
    rmin: int = 0
    rm: int = 0
    rkm: float = 0.0
    avg: int = 0
    avgL: int = 0
    avgR: int = 0
    avgP: int = 0
    alr: int = 0
    alrD: int = -1
    alrV: int | None = None
    alrs: list = field(default_factory=list)
    ts: int = 0

    @classmethod
    def from_obj(cls, obj: dict) -> "State":
        s = cls()
        for key in (
            "nav", "spd", "lim", "over", "trn", "trn2", "dst", "exit",
            "rmin", "rm", "avg", "avgL", "avgR", "avgP", "alr", "alrD", "ts",
        ):
            if isinstance(obj.get(key), (int, float)):
                setattr(s, key, int(obj[key]))
        for key in ("st", "st2", "eta"):
            if isinstance(obj.get(key), str):
                setattr(s, key, obj[key])
        if isinstance(obj.get("rkm"), (int, float)):
            s.rkm = float(obj["rkm"])
        if isinstance(obj.get("alrV"), (int, float)):
            s.alrV = int(obj["alrV"])
        if isinstance(obj.get("lan"), list):
            s.lan = obj["lan"]
        if isinstance(obj.get("alrs"), list):
            s.alrs = obj["alrs"]
        return s

    def distance_text(self) -> str:
        """rm là giá trị mét chính xác và có thẩm quyền; rkm chỉ để tương thích cũ."""
        metres = self.rm if self.rm else int(self.rkm * 1000)
        if metres >= 1000:
            return f"{metres / 1000:.1f} km".replace(".", ",")
        return f"{metres} m"


# --- Khai báo `dev` của bên nhận (§2.5.1) -----------------------------------

WANT_FIELDS = [
    "nav", "spd", "lim", "over", "trn", "trn2", "dst", "exit", "lan",
    "st", "st2", "eta", "rmin", "rm", "rkm",
    "avg", "avgL", "avgR", "avgP",
    "alr", "alrD", "alrV", "alrs",
]


def build_dev(name: str, fw: str, rate: int, width: int, height: int) -> str:
    """`lan` và `alrs` là opt-in — phải ghi tên vào want.fields mới được nhận."""
    return json.dumps(
        {
            "v": 1,
            "t": "dev",
            "name": name,
            "fw": fw,
            "proto": [1],
            "disp": {"w": width, "h": height, "color": 1},
            "can": ["speed", "limit", "turn", "lanes", "street", "eta", "avgzone", "alerts"],
            "want": {"rate": rate, "fields": WANT_FIELDS},
        },
        ensure_ascii=False,
        separators=(",", ":"),
    )


PONG = '{"v":1,"t":"pong"}'
