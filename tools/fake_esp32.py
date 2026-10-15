#!/usr/bin/env python3
"""ESP32 giả lập — biến Mac thành thiết bị HUD Link để thu dữ liệu HLP/1 thật.

Công bố đúng bộ UUID HLP cố định nên Waze Mod nhìn thấy y hệt một con ESP32:
Android ghi state vào TX, thiết bị trả `dev`/`pong` qua RX notification.

Mọi dòng nhận được đều lưu vào tools/captures/ kèm mốc thời gian, để phát lại
sau này mà không cần điện thoại.

    Cài đặt Mod → Thiết bị → HUD Link → Transport: BLE → Chọn thiết bị → WazeHUD

Dùng:  python3 tools/fake_esp32.py --demo      (không cần Bluetooth)
       python3 tools/fake_esp32.py             (BLE thật, cần `pip install bless`)
"""

from __future__ import annotations

import argparse
import asyncio
import json
import re
import signal
import sys
import time
import unicodedata
from datetime import datetime
from pathlib import Path

# bless chỉ cần cho chế độ BLE thật; --demo/--replay chạy không cần.
try:
    from bless import (
        BlessServer,
        BlessGATTCharacteristic,
        GATTCharacteristicProperties,
        GATTAttributePermissions,
    )
except ImportError:
    BlessServer = None

sys.path.insert(0, str(Path(__file__).parent))
import hlp  # noqa: E402

# §8 Binding theo transport — sai một bit là Waze Mod không thấy thiết bị.
SERVICE_UUID = "8a7e0001-4d6e-4c48-9a9d-484c504c0001"
TX_UUID = "8a7e0002-4d6e-4c48-9a9d-484c504c0001"  # Android → thiết bị
RX_UUID = "8a7e0003-4d6e-4c48-9a9d-484c504c0001"  # thiết bị → Android (notify)
CAPS_UUID = "8a7e0004-4d6e-4c48-9a9d-484c504c0001"
STATE_UUID = "8a7e0005-4d6e-4c48-9a9d-484c504c0001"

DEVICE_NAME = "WazeHUD"
FIRMWARE = "0.1.0-sim"
DISPLAY_W, DISPLAY_H = 320, 170  # khớp T-Display-S3 nằm ngang
NOTIFY_CHUNK = 20  # an toàn cho cả ATT MTU 23
STALE_MS = 3000


class Session:
    """Trạng thái một phiên. `sess` đổi ⇒ phiên mới, phải xoá cache (§2.5)."""

    def __init__(self) -> None:
        self.sess: int | None = None
        self.rate: int | None = None
        self.fields: list | str | None = None
        self.app_version: str = ""
        self.state: hlp.State | None = None
        self.last_rx_ms: float = 0.0
        self.frames = 0
        self.pings = 0
        self.dev_sent = 0
        self.recent: list[float] = []

    def note_hi(self, obj: dict) -> bool:
        new_sess = obj.get("sess")
        changed = new_sess != self.sess
        if changed:
            self.state = None  # phiên mới ⇒ vứt state cũ
            self.frames = 0
        self.sess = new_sess
        self.rate = obj.get("rate")
        self.fields = obj.get("fields")
        self.app_version = str(obj.get("appv", ""))
        return changed

    def note_state(self, state: hlp.State, now: float) -> None:
        self.state = state
        self.frames += 1
        self.recent.append(now)
        self.recent = [t for t in self.recent if now - t < 5.0]

    @property
    def measured_hz(self) -> float:
        if len(self.recent) < 2:
            return 0.0
        span = self.recent[-1] - self.recent[0]
        return (len(self.recent) - 1) / span if span > 0 else 0.0


class NullCapture:
    """Chế độ xem thử / phát lại thì không ghi đè lên dữ liệu đã thu."""

    count = 0
    path = Path("(không ghi)")

    def write(self, line: str) -> None:
        pass

    def close(self) -> None:
        pass


class Capture:
    """Ghi lại nguyên văn từng dòng kèm mốc ms, đủ để phát lại đúng nhịp."""

    def __init__(self, directory: Path) -> None:
        directory.mkdir(parents=True, exist_ok=True)
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        self.path = directory / f"hlp-{stamp}.jsonl"
        self._fh = self.path.open("w", encoding="utf-8")
        self._t0 = time.monotonic()
        self.count = 0

    def write(self, line: str) -> None:
        ms = int((time.monotonic() - self._t0) * 1000)
        self._fh.write(json.dumps({"ms": ms, "line": line}, ensure_ascii=False) + "\n")
        self._fh.flush()
        self.count += 1

    def close(self) -> None:
        self._fh.close()


# --- Vẽ HUD ra terminal -----------------------------------------------------

RESET, DIM, BOLD = "\x1b[0m", "\x1b[2m", "\x1b[1m"
RED, BLUE, GREEN, YELLOW, CYAN = (
    "\x1b[91m", "\x1b[94m", "\x1b[92m", "\x1b[93m", "\x1b[96m",
)
WIDTH = 62


ANSI_RE = re.compile(r"\x1b\[[0-9;]*m")


def vis_len(text: str) -> int:
    """Bề rộng thật khi in ra terminal: bỏ mã màu, dấu tiếng Việt không chiếm ô."""
    plain = ANSI_RE.sub("", text)
    width = 0
    for ch in plain:
        if unicodedata.combining(ch):
            continue
        width += 2 if unicodedata.east_asian_width(ch) in ("W", "F") else 1
    return width


def _row(content: str = "") -> str:
    return "│ " + content + " " * max(0, WIDTH - vis_len(content)) + " │"


def _title(text: str) -> str:
    return "┌─" + text + "─" * max(0, WIDTH + 2 - vis_len(text) - 1) + "┐"


def render(session: Session, capture, connected: bool, rx: hlp.Reassembler) -> str:
    now = time.monotonic() * 1000
    stale = session.last_rx_ms and (now - session.last_rx_ms) > STALE_MS
    s = session.state

    if not connected:
        status = f"{DIM}chờ điện thoại kết nối…{RESET}"
    elif stale:
        status = f"{RED}{BOLD}MẤT TÍN HIỆU{RESET}"
    else:
        status = f"{GREEN}● đang nhận{RESET}"

    out = [
        _title(f" {BOLD}WazeHUD{RESET} {DIM}· ESP32 giả lập trên Mac{RESET} "),
        _row(status),
    ]

    if s is None:
        out.append(_row(f"{DIM}chưa nhận được message trạng thái nào{RESET}"))
    else:
        arrow, turn_label = hlp.turn_name(s.trn)
        dist = f"{s.dst} m" if s.dst >= 0 else "—"
        spd_col = RED if s.over else BLUE
        limit = f"{s.lim}" if s.lim else "—"

        out.append(_row())
        out.append(_row(
            f"  {BOLD}{arrow or '·'}{RESET}{' ' * (4 - vis_len(arrow or '·'))}"
            f"{dist:>7}     {spd_col}{BOLD}{s.spd:>3}{RESET} km/h"
            f"     {RED}({limit:>3}){RESET} giới hạn"))
        out.append(_row(f"  {DIM}{turn_label}{RESET}"))
        out.append(_row())

        if s.st:
            out.append(_row(f"  {s.st[:44]}"))
        if s.st2:
            out.append(_row(f"{DIM}  → {s.st2[:42]}{RESET}"))

        tail = []
        if s.eta:
            tail.append(f"ETA {s.eta}")
        if s.rmin:
            tail.append(f"{s.rmin} phút")
        if s.rm or s.rkm:
            tail.append(s.distance_text())
        if tail:
            out.append(_row(f"{DIM}  {' · '.join(tail)}{RESET}"))

        if s.avg:
            out.append(_row(
                f"{YELLOW}  ⊘ Cấm vượt · còn {s.avgL} m"
                f" · khuyến nghị {s.avgR} km/h ({s.avgP}%){RESET}"))

        if s.alr:
            val = f" · {s.alrV} km/h" if s.alrV is not None else ""
            d = f"{s.alrD} m" if s.alrD >= 0 else "—"
            out.append(_row(f"{YELLOW}  ⚠ {hlp.alert_name(s.alr)} · {d}{val}{RESET}"))

        for extra in s.alrs[1:]:
            out.append(_row(
                f"{DIM}    {hlp.alert_name(extra.get('k', 0))}"
                f" · {extra.get('d', -1)} m{RESET}"))

        if s.lan:
            lanes = " ".join(str(p[1] or "·") for p in s.lan if len(p) >= 2)
            out.append(_row(f"{DIM}  làn: {lanes}{RESET}"))

    out.append("└" + "─" * (WIDTH + 2) + "┘")

    # Bảng đầy đủ: mọi field của message `s` theo §3, kể cả field mà HUD phía
    # trên không vẽ. Dùng để đối chiếu xem Android có gửi thiếu gì không.
    if s is not None:
        alrs = ", ".join(
            f"k{a.get('k')}/{a.get('d')}m" + (f"/{a['v']}" if "v" in a else "")
            for a in s.alrs) or "—"
        lanes = ", ".join(f"{p[0]}→{p[1]}" for p in s.lan if len(p) >= 2) or "—"
        out += [
            "",
            f"{DIM}  ── tất cả field ──{RESET}",
            f"  nav {s.nav}   spd {s.spd}   lim {s.lim}   over {s.over}"
            f"   ts {s.ts}",
            f"  trn {s.trn} ({hlp.turn_name(s.trn)[1]})   trn2 {s.trn2}"
            f" ({hlp.turn_name(s.trn2)[1]})   dst {s.dst}   exit {s.exit}",
            f"  st  {s.st or '—'!r}",
            f"  st2 {s.st2 or '—'!r}",
            f"  eta {s.eta or '—'}   rmin {s.rmin}   rm {s.rm}   rkm {s.rkm}",
            f"  avg {s.avg}   avgL {s.avgL}   avgR {s.avgR}   avgP {s.avgP}",
            f"  alr {s.alr} ({hlp.alert_name(s.alr)})   alrD {s.alrD}"
            f"   alrV {s.alrV if s.alrV is not None else '—'}",
            f"  alrs [{alrs}]",
            f"  lan  [{lanes}]",
        ]
        if session.fields:
            negotiated = (session.fields if isinstance(session.fields, str)
                          else " ".join(session.fields))
            out.append(f"{DIM}  đã thương lượng: {negotiated}{RESET}")
        if session.app_version:
            out.append(f"{DIM}  Waze {session.app_version}{RESET}")

    stats = (f"  sess {session.sess if session.sess is not None else '—'}"
             f" · rate {session.rate or '—'} Hz · đo được {session.measured_hz:.1f} Hz"
             f" · {session.frames} state · {session.pings} ping"
             f" · dev ×{session.dev_sent}"
             f" · frame lớn nhất {rx.max_frame} B")
    problems = (f"  {RED}tràn {rx.oversized} · UTF-8 lỗi {rx.malformed_utf8}{RESET}"
                if rx.oversized or rx.malformed_utf8 else "")
    saved = f"  {CYAN}↓ {capture.count} dòng → {capture.path.name}{RESET}"

    return "\n".join(out) + f"\n{DIM}{stats}{RESET}\n{saved}{problems}\n"


# --- Nguồn dữ liệu offline --------------------------------------------------


def demo_trip() -> list[tuple[int, dict]]:
    """Một chuyến đi tổng hợp ở 4 Hz, dựng đủ các tình huống cần vẽ đúng.

    Không thay thế dữ liệu thật — chỉ để kiểm tra đường ống decode + render khi
    chưa cấp được quyền Bluetooth hoặc chưa có điện thoại bên cạnh.
    """
    frames: list[tuple[int, dict]] = []
    ts = 0

    def push(**fields) -> None:
        nonlocal ts
        ts += 250
        frames.append((250, {"v": 1, "t": "s", "ts": ts, **fields}))

    # Đứng yên, chưa dẫn đường: chỉ có giới hạn tốc độ và tên đường.
    for _ in range(8):
        push(nav=0, spd=0, lim=50, st="Nguyễn Trãi")

    # Vào tuyến, tiến tới ngã rẽ phải; camera tốc độ hiện dần từ 600 m.
    for i in range(40):
        dst = 800 - i * 20
        alerts = {}
        if dst <= 600:
            alerts = {"alr": 2, "alrD": dst - 50, "alrs": [{"k": 2, "d": dst - 50}]}
        push(nav=1, spd=48 + (i % 5), lim=50, trn=3, trn2=10, dst=dst, exit=2,
             st="Nguyễn Trãi", st2="Khuất Duy Tiến", eta="20:01", rmin=23,
             rm=6742 - i * 20, rkm=round((6742 - i * 20) / 1000, 1), **alerts)

    # Vượt tốc: `over` bật, HUD phải đổi màu chứ không chỉ hiện số.
    for i in range(20):
        push(nav=1, spd=66 + (i % 4), lim=50, over=1, trn=10, dst=1200 - i * 40,
             exit=2, st="Khuất Duy Tiến", st2="Vành đai 3", eta="20:04",
             rmin=19, rm=5200 - i * 40, rkm=round((5200 - i * 40) / 1000, 1),
             alr=8, alrD=400 - i * 10, alrV=40)

    # Vùng cấm vượt — vẽ biển cấm vượt kèm đếm ngược, tuyệt đối không vẽ camera.
    for i in range(24):
        push(nav=1, spd=52, lim=60, trn=1, dst=2400 - i * 60, st="Vành đai 3",
             st2="CT.HCM - Long Thành - Dầu Giây", eta="20:11", rmin=12,
             rm=3800 - i * 60, rkm=round((3800 - i * 60) / 1000, 1),
             avg=1, avgL=1400 - i * 55, avgR=58, avgP=min(100, i * 4),
             lan=[[5, 4], [1, 1], [17, 16]])

    # Nhiều cảnh báo cùng lúc: alr phải mirror alrs[0] (§3).
    for i in range(16):
        push(nav=1, spd=61, lim=80, trn=5, dst=900 - i * 50,
             st="CT.HCM - Long Thành - Dầu Giây", st2="Quốc lộ 51",
             eta="20:18", rmin=6, rm=1800 - i * 50,
             rkm=round((1800 - i * 50) / 1000, 1),
             alr=1, alrD=300 - i * 15, alrs=[{"k": 1, "d": 300 - i * 15},
                                             {"k": 12, "d": 1200 - i * 30},
                                             {"k": 11, "d": 2600}])

    # Đến nơi.
    for i in range(10):
        push(nav=1, spd=max(0, 30 - i * 4), lim=50, trn=17,
             dst=max(0, 120 - i * 15), st="Quốc lộ 51", eta="20:20", rmin=0,
             rm=max(0, 120 - i * 15))

    return frames


def load_capture(path: Path) -> list[tuple[int, dict]]:
    """Đọc file đã thu; giữ nguyên khoảng cách thời gian giữa các dòng."""
    frames: list[tuple[int, dict]] = []
    prev_ms = 0
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw.strip():
            continue
        entry = json.loads(raw)
        try:
            obj = json.loads(entry["line"])
        except json.JSONDecodeError:
            continue
        delay = max(0, entry.get("ms", prev_ms) - prev_ms)
        prev_ms = entry.get("ms", prev_ms)
        frames.append((delay, obj))
    return frames


# --- Server -----------------------------------------------------------------


class FakeEsp32:
    def __init__(self, args: argparse.Namespace) -> None:
        self.args = args
        self.session = Session()
        self.offline = bool(args.demo or args.replay)
        self.capture = NullCapture() if self.offline else Capture(Path(args.out))
        self.rx = hlp.Reassembler(self._on_line)
        self.outbox: asyncio.Queue[str] = asyncio.Queue()
        self.loop: asyncio.AbstractEventLoop | None = None
        self.server: BlessServer | None = None
        self.connected = False
        self.dev_acked = False
        self.log_raw = args.raw

    # gọi từ thread của CoreBluetooth — chỉ được làm việc nhẹ rồi thoát ngay
    def _on_write(self, characteristic: BlessGATTCharacteristic, value, **kwargs) -> None:
        self.rx.feed(bytes(value))

    def _on_read(self, characteristic: BlessGATTCharacteristic, **kwargs) -> bytearray:
        uuid = str(characteristic.uuid).lower()
        if uuid == CAPS_UUID:
            return bytearray(json.dumps(
                {"v": 1, "name": DEVICE_NAME, "fw": FIRMWARE,
                 "disp": {"w": DISPLAY_W, "h": DISPLAY_H, "color": 1}},
                ensure_ascii=False, separators=(",", ":")).encode("utf-8"))
        if uuid == STATE_UUID:
            return bytearray(json.dumps(
                {"v": 1, "connected": self.connected,
                 "frames": self.session.frames},
                separators=(",", ":")).encode("utf-8"))
        return characteristic.value or bytearray()

    def _send(self, line: str) -> None:
        if self.offline:
            return  # không có link thật để trả lời
        assert self.loop is not None
        self.loop.call_soon_threadsafe(self.outbox.put_nowait, line)

    def _on_line(self, line: str) -> None:
        self.session.last_rx_ms = time.monotonic() * 1000
        self.capture.write(line)
        if self.log_raw:
            print(line, flush=True)

        try:
            obj = json.loads(line)
        except json.JSONDecodeError:
            return
        if not isinstance(obj, dict) or obj.get("v") != 1:
            return  # bỏ qua phiên bản không hỗ trợ (§2)

        kind = obj.get("t")
        if kind == "ping":
            # Trả pong TRƯỚC khi làm bất cứ việc gì chậm (§8, Doc 8).
            self._send(hlp.PONG)
            self.session.pings += 1
        elif kind == "hi":
            self.session.note_hi(obj)
            # `fields` là danh sách cụ thể ⇒ `dev` của ta đã tới nơi. Còn `"*"`
            # nghĩa là Android đã tự chốt mặc định vì không nhận được `dev`,
            # và ở mặc định đó `alrs` bị loại (§2.5.3) — phải gửi lại.
            if isinstance(obj.get("fields"), list):
                self.dev_acked = True
        elif kind == "s":
            self.session.note_state(hlp.State.from_obj(obj), time.monotonic())
        # loại message lạ (cfg_*, error…) bỏ qua để giữ tương thích về sau

    async def _notify(self, line: str) -> None:
        """Chia frame nhỏ để chạy được cả ở ATT MTU 23; bên nhận ghép theo LF."""
        if self.server is None:
            return
        char = self.server.get_characteristic(RX_UUID)
        payload = (line + "\n").encode("utf-8")
        for i in range(0, len(payload), NOTIFY_CHUNK):
            char.value = bytearray(payload[i:i + NOTIFY_CHUNK])
            self.server.update_value(SERVICE_UUID, RX_UUID)
            await asyncio.sleep(0.01)

    async def _sender(self) -> None:
        while True:
            line = await self.outbox.get()
            try:
                await self._notify(line)
            except Exception as exc:  # noqa: BLE001
                print(f"notify lỗi: {exc}", file=sys.stderr)

    async def _announce(self) -> None:
        """Gửi `dev` lặp lại cho tới khi Android trả `hi` có danh sách field cụ thể.

        Notification chỉ tới nơi sau khi Android bật CCCD, mà `bless` không báo
        thời điểm đó, nên gửi một lần rất dễ hụt. `hi` là idempotent và `dev`
        đến muộn được phép — Android sẽ thương lượng lại rồi gửi `hi` mới (§2.5).
        """
        dev = hlp.build_dev(DEVICE_NAME, FIRMWARE, self.args.rate,
                            DISPLAY_W, DISPLAY_H)
        for attempt in range(10):
            if not self.connected or self.dev_acked:
                return
            self._send(dev)
            self.session.dev_sent += 1
            await asyncio.sleep(0.4 if attempt < 4 else 2.0)

    async def _watch_connection(self) -> None:
        while True:
            try:
                now_connected = await self.server.is_connected()
            except Exception:  # noqa: BLE001
                now_connected = False
            if now_connected and not self.connected:
                self.connected = True
                self.dev_acked = False
                self.session = Session()
                asyncio.create_task(self._announce())
            elif not now_connected and self.connected:
                self.connected = False
            await asyncio.sleep(0.25)

    async def _draw(self) -> None:
        while True:
            sys.stdout.write("\x1b[H\x1b[J" + render(
                self.session, self.capture, self.connected, self.rx))
            sys.stdout.flush()
            await asyncio.sleep(0.2)

    async def _feed_offline(self, frames: list[tuple[int, dict]], loop: bool) -> None:
        """Bơm dữ liệu qua đúng đường đi của BLE: chunk nhỏ → ghép LF → parse.

        Nhờ vậy chế độ xem thử vẫn kiểm tra được bộ ghép frame chứ không chỉ
        phần vẽ.
        """
        self.connected = True
        self.session.sess = 0
        self.session.rate = self.args.rate
        while True:
            for delay, obj in frames:
                payload = (json.dumps(obj, ensure_ascii=False,
                                      separators=(",", ":")) + "\n").encode("utf-8")
                for i in range(0, len(payload), NOTIFY_CHUNK):
                    self.rx.feed(payload[i:i + NOTIFY_CHUNK])
                await asyncio.sleep(delay / 1000)
            if not loop:
                break
            await asyncio.sleep(1.0)

    async def run_offline(self) -> None:
        """Xem thử hoặc phát lại — không đụng tới Bluetooth."""
        self.loop = asyncio.get_running_loop()
        if self.args.replay:
            path = Path(self.args.replay)
            frames = load_capture(path)
            if not frames:
                print(f"Không đọc được dòng nào từ {path}", file=sys.stderr)
                return
            print(f"Phát lại {len(frames)} dòng từ {path.name}")
        else:
            frames = demo_trip()

        tasks = [asyncio.create_task(self._feed_offline(frames, self.args.repeat))]
        if not self.log_raw:
            tasks.append(asyncio.create_task(self._draw()))

        stop = asyncio.Event()
        for sig in (signal.SIGINT, signal.SIGTERM):
            self.loop.add_signal_handler(sig, stop.set)
        try:
            done = asyncio.create_task(stop.wait())
            await asyncio.wait([done, tasks[0]], return_when=asyncio.FIRST_COMPLETED)
        finally:
            for task in tasks:
                task.cancel()
            sys.stdout.write("\x1b[H\x1b[J")
            print(f"Đã dựng lại {self.session.frames} message trạng thái, "
                  f"frame lớn nhất {self.rx.max_frame} B, "
                  f"tràn {self.rx.oversized}, UTF-8 lỗi {self.rx.malformed_utf8}")

    async def run(self) -> None:
        self.loop = asyncio.get_running_loop()
        self.server = BlessServer(name=DEVICE_NAME, loop=self.loop)
        self.server.read_request_func = self._on_read
        self.server.write_request_func = self._on_write

        await self.server.add_new_service(SERVICE_UUID)
        await self.server.add_new_characteristic(
            SERVICE_UUID, TX_UUID,
            GATTCharacteristicProperties.write
            | GATTCharacteristicProperties.write_without_response,
            None, GATTAttributePermissions.writeable)
        await self.server.add_new_characteristic(
            SERVICE_UUID, RX_UUID,
            GATTCharacteristicProperties.notify | GATTCharacteristicProperties.read,
            # Giá trị khởi tạo PHẢI là None: CoreBluetooth chỉ cho characteristic
            # mang sẵn giá trị khi nó read-only, mà RX thì có notify.
            None, GATTAttributePermissions.readable)
        # Hai characteristic tuỳ chọn (§8). Để None và trả giá trị động trong
        # read_request_func, tránh luôn luật "cached value ⇒ read-only".
        await self.server.add_new_characteristic(
            SERVICE_UUID, CAPS_UUID, GATTCharacteristicProperties.read,
            None, GATTAttributePermissions.readable)
        await self.server.add_new_characteristic(
            SERVICE_UUID, STATE_UUID, GATTCharacteristicProperties.read,
            None, GATTAttributePermissions.readable)

        # prioritize_local_name=False để chắc chắn service UUID nằm trong gói
        # quảng bá — picker của Waze Mod tìm thiết bị BLE theo UUID này.
        await self.server.start(prioritize_local_name=False)

        stop = asyncio.Event()
        for sig in (signal.SIGINT, signal.SIGTERM):
            self.loop.add_signal_handler(sig, stop.set)

        tasks = [
            asyncio.create_task(self._sender()),
            asyncio.create_task(self._watch_connection()),
        ]
        if self.log_raw:
            print(f"{DEVICE_NAME} đang quảng bá · service {SERVICE_UUID}")
        else:
            tasks.append(asyncio.create_task(self._draw()))
        try:
            await stop.wait()
        finally:
            for task in tasks:
                task.cancel()
            await self.server.stop()
            self.capture.close()
            sys.stdout.write("\x1b[H\x1b[J")
            print(f"Đã lưu {self.capture.count} dòng → {self.capture.path}")


def main() -> None:
    parser = argparse.ArgumentParser(description="ESP32 giả lập cho Waze HUD Link")
    parser.add_argument("--rate", type=int, default=4,
                        help="rate mong muốn, BLE bị giới hạn 1..4 Hz (mặc định 4)")
    parser.add_argument("--out", default=str(Path(__file__).parent / "captures"),
                        help="thư mục lưu file thu được")
    parser.add_argument("--raw", action="store_true",
                        help="in nguyên văn từng dòng thay vì vẽ HUD")
    parser.add_argument("--demo", action="store_true",
                        help="chạy chuyến đi tổng hợp, không cần Bluetooth")
    parser.add_argument("--replay", metavar="FILE",
                        help="phát lại một file đã thu trong tools/captures/")
    parser.add_argument("--repeat", action="store_true",
                        help="lặp lại khi hết dữ liệu (dùng với --demo/--replay)")
    args = parser.parse_args()

    if not 1 <= args.rate <= 4:
        parser.error("BLE chỉ chấp nhận rate 1..4 Hz")
    if args.demo and args.replay:
        parser.error("chọn một trong --demo hoặc --replay")

    if not (args.demo or args.replay) and BlessServer is None:
        parser.error("chế độ BLE cần thư viện bless: pip install bless")

    sim = FakeEsp32(args)
    asyncio.run(sim.run_offline() if sim.offline else sim.run())


if __name__ == "__main__":
    main()
