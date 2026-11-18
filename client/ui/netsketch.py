"""Thin ctypes wrapper over libnetsketch and pure protocol/model helpers for the UI.

Contains no game business logic (never scores guesses, checks answers, or picks words).
All network polling runs on a single thread via bounded non-blocking polls.
"""
from __future__ import annotations

import collections
import ctypes
import logging
import math
import os
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Deque, Dict, List, Optional, Set, Tuple

logger = logging.getLogger("netsketch.ui")

RECV_BUF_CAP = 1024
DEFAULT_PING_INTERVAL_MS = 30000
DEFAULT_HELLO_TIMEOUT_S = 3.0
DEFAULT_POLL_MAX_LINES = 200
DEFAULT_POLL_MAX_MS = 10.0
ROOMS_PER_PAGE = 8

# Events whose final field is free text that may contain '|' characters.
EVENT_MAXSPLIT: Dict[str, int] = {
    "CHAT": 2,
    "GUESS_WRONG": 2,
}

# Valid error codes per command (including common syntax/framing codes 100, 103, 104).
_COMMON_SYNTAX = {100, 103, 104, 900}
COMMAND_ERRORS: Dict[str, Set[int]] = {
    "REGISTER":    _COMMON_SYNTAX | {102, 110, 112, 113},
    "LOGIN":       _COMMON_SYNTAX | {102, 111, 114},
    "LOGOUT":      _COMMON_SYNTAX | {101},
    "CREATE_ROOM": _COMMON_SYNTAX | {101, 201, 205, 208},
    "LIST_ROOMS":  _COMMON_SYNTAX | {101},
    "JOIN_ROOM":   _COMMON_SYNTAX | {101, 200, 201, 202, 203},
    "LEAVE_ROOM":  _COMMON_SYNTAX | {101, 204},
    "START":       _COMMON_SYNTAX | {101, 204, 206, 207, 300},
    "SCOREBOARD":  _COMMON_SYNTAX | {101, 204},
    "CHOOSE_WORD": _COMMON_SYNTAX | {101, 204, 300, 301, 304},
    "GUESS":       _COMMON_SYNTAX | {101, 204, 300, 302, 303},
    "CHAT":        _COMMON_SYNTAX | {101, 204, 302, 306},
}

ERROR_MESSAGES: Dict[int, str] = {
    -1:  "Mất kết nối tới máy chủ",
    100: "Sai định dạng lệnh hoặc tham số",
    101: "Bạn chưa đăng nhập",
    102: "Tài khoản đã đăng nhập trên kết nối này",
    103: "Lệnh không tồn tại",
    104: "Dòng dữ liệu vượt quá 512 byte",
    110: "Tên tài khoản đã tồn tại",
    111: "Sai tên đăng nhập hoặc mật khẩu",
    112: "Tên tài khoản không hợp lệ (3..16 ký tự A-Z, a-z, 0-9, _)",
    113: "Mật khẩu không hợp lệ (6..32 ký tự, không khoảng trắng hay '|')",
    114: "Tài khoản đang được đăng nhập ở nơi khác",
    200: "Phòng không tồn tại",
    201: "Bạn đang ở trong một phòng khác",
    202: "Phòng đã đầy",
    203: "Phòng đang trong ván chơi, không thể vào",
    204: "Bạn chưa vào phòng nào",
    205: "Thông số phòng không hợp lệ",
    206: "Chỉ chủ phòng mới có quyền bắt đầu",
    207: "Cần ít nhất 2 người chơi để bắt đầu",
    208: "Máy chủ đã đạt giới hạn số phòng",
    300: "Thao tác không hợp lệ ở giai đoạn hiện tại",
    301: "Chỉ người vẽ mới được thực hiện thao tác này",
    302: "Người vẽ không được đoán hoặc chat trong lượt vẽ",
    303: "Bạn đã đoán đúng trong lượt này rồi",
    304: "Lựa chọn từ khóa không hợp lệ",
    305: "Dữ liệu nét vẽ không hợp lệ",
    306: "Tin nhắn bị chặn vì chứa từ khóa đáp án",
    900: "Lỗi nội bộ máy chủ",
    901: "Máy chủ đã đầy",
}


def format_error_vi(code: int, fallback: str = "") -> str:
    msg = ERROR_MESSAGES.get(code) or fallback or "Lỗi không xác định"
    if code < 0:
        return msg
    return f"[{code}] {msg}"


@dataclass
class Response:
    ok: bool
    code: int = 0
    args: List[str] = field(default_factory=list)
    raw: str = ""


@dataclass
class Event:
    name: str
    args: List[str] = field(default_factory=list)
    raw: str = ""


@dataclass
class PendingCommand:
    cmd_name: str
    valid_errors: Set[int]
    callback: Optional[Callable[[Response], None]]


@dataclass
class RoomSummary:
    room_id: int
    name: str
    nplayers: int
    max_players: int
    state: str


@dataclass
class RoomModel:
    """State snapshot updated strictly from server events, independent of Tkinter widgets."""
    in_room: bool = False
    room_id: int = 0
    name: str = ""
    host: str = ""
    max_players: int = 0
    rounds: int = 0
    draw_time: int = 0
    state: str = "LOBBY"
    players: List[str] = field(default_factory=list)
    round_no: int = 0
    drawer: str = ""
    word_hint: str = ""
    remaining_s: int = 0

    def reset(self) -> None:
        self.in_room = False
        self.room_id = 0
        self.name = ""
        self.host = ""
        self.max_players = 0
        self.rounds = 0
        self.draw_time = 0
        self.state = "LOBBY"
        self.players.clear()
        self.round_no = 0
        self.drawer = ""
        self.word_hint = ""
        self.remaining_s = 0

    def apply_event(self, ev: Event) -> bool:
        """Update model from an incoming Event. Returns True if room state changed."""
        if ev.name == "ROOM_INFO" and len(ev.args) >= 8:
            try:
                self.room_id = int(ev.args[0])
                self.name = ev.args[1]
                self.host = ev.args[2]
                self.max_players = int(ev.args[3])
                self.rounds = int(ev.args[4])
                self.draw_time = int(ev.args[5])
                self.state = ev.args[6]
                plist = ev.args[7]
                self.players = [p for p in plist.split(",") if p] if plist else []
                self.in_room = True
                if self.state == "LOBBY":
                    self.round_no = 0
                    self.drawer = ""
                    self.word_hint = ""
                    self.remaining_s = 0
                return True
            except ValueError:
                return False

        if not self.in_room:
            return False

        if ev.name == "PLAYER_JOINED" and len(ev.args) >= 1:
            user = ev.args[0]
            if user and user not in self.players:
                self.players.append(user)
            return True

        if ev.name == "PLAYER_LEFT" and len(ev.args) >= 1:
            user = ev.args[0]
            if user in self.players:
                self.players.remove(user)
            return True

        if ev.name == "HOST_CHANGED" and len(ev.args) >= 1:
            self.host = ev.args[0]
            return True

        if ev.name == "GAME_START" and len(ev.args) >= 2:
            self.state = "PLAYING"
            return True

        if ev.name == "TURN_START" and len(ev.args) >= 3:
            self.state = "CHOOSING_WORD"
            try:
                self.round_no = int(ev.args[0])
                self.remaining_s = int(ev.args[2])
            except ValueError:
                pass
            self.drawer = ev.args[1]
            self.word_hint = ""
            return True

        if ev.name == "DRAW_START" and len(ev.args) >= 3:
            self.state = "DRAWING"
            self.drawer = ev.args[0]
            try:
                self.remaining_s = int(ev.args[1])
            except ValueError:
                pass
            self.word_hint = ev.args[2]
            return True

        if ev.name == "TIME" and len(ev.args) >= 1:
            try:
                self.remaining_s = int(ev.args[0])
            except ValueError:
                pass
            return True

        if ev.name == "TURN_END":
            self.state = "TURN_END"
            self.remaining_s = 0
            return True

        if ev.name == "GAME_END":
            self.state = "LOBBY"
            self.drawer = ""
            self.word_hint = ""
            self.remaining_s = 0
            return True

        return False


def calc_total_pages(total_rooms: int, per_page: int = ROOMS_PER_PAGE) -> int:
    if total_rooms <= 0 or per_page <= 0:
        return 1
    return max(1, math.ceil(total_rooms / per_page))


def parse_room_list(args: List[str]) -> Tuple[int, int, List[RoomSummary]]:
    """Parse args of OK|total|page|r1|r2... into (total, page, [RoomSummary, ...])."""
    if len(args) < 2:
        raise ValueError("Missing total/page in LIST_ROOMS response")
    total = int(args[0])
    page = int(args[1])
    rooms: List[RoomSummary] = []
    for raw_item in args[2:]:
        if not raw_item:
            continue
        parts = raw_item.split(",")
        if len(parts) != 5:
            continue
        rooms.append(
            RoomSummary(
                room_id=int(parts[0]),
                name=parts[1],
                nplayers=int(parts[2]),
                max_players=int(parts[3]),
                state=parts[4],
            )
        )
    return total, page, rooms


def validate_username(name: str) -> Optional[str]:
    if not (3 <= len(name) <= 16):
        return "Tên tài khoản phải từ 3 đến 16 ký tự"
    for ch in name:
        if not (("A" <= ch <= "Z") or ("a" <= ch <= "z") or ("0" <= ch <= "9") or ch == "_"):
            return "Tên tài khoản chỉ gồm chữ cái không dấu, chữ số và '_'"
    return None


def validate_password(password: str) -> Optional[str]:
    raw = password.encode("utf-8", errors="replace")
    if not (6 <= len(raw) <= 32):
        return "Mật khẩu phải từ 6 đến 32 byte"
    for b in raw:
        if b <= 0x20 or b >= 0x7F or b == ord("|"):
            return "Mật khẩu không được chứa khoảng trắng, '|' hoặc ký tự điều khiển"
    return None


def validate_room_params(name: str, max_players: int, rounds: int, draw_time: int) -> Optional[str]:
    trimmed = name.strip(" ")
    if not (1 <= len(trimmed) <= 24):
        return "Tên phòng phải từ 1 đến 24 ký tự"
    for ch in trimmed:
        if not (("A" <= ch <= "Z") or ("a" <= ch <= "z") or ("0" <= ch <= "9") or ch in " _-"):
            return "Tên phòng chỉ gồm chữ cái không dấu, số, khoảng trắng, '_' hoặc '-'"
    if not (2 <= max_players <= 8):
        return "Số người tối đa phải từ 2 đến 8"
    if not (1 <= rounds <= 10):
        return "Số vòng phải từ 1 đến 10"
    if not (30 <= draw_time <= 180):
        return "Thời gian vẽ phải từ 30 đến 180 giây"
    return None


def validate_stroke(color: str, width: int, points: List[Tuple[int, int]]) -> bool:
    """Client-side precheck before sending STROKE so invalid stroke errors do not occur."""
    if len(color) != 6 or any(c not in "0123456789abcdefABCDEF" for c in color):
        return False
    if not (1 <= width <= 20):
        return False
    if not (2 <= len(points) <= 40):
        return False
    for x, y in points:
        if not (0 <= x <= 1000 and 0 <= y <= 1000):
            return False
    return True


def parse_server_line(line: str) -> Tuple[Optional[Response], Optional[Event]]:
    """Classify a single line (without trailing CR/LF) into either a Response or an Event."""
    if line == "OK":
        return Response(ok=True, code=0, args=[], raw=line), None
    if line.startswith("OK|"):
        payload = line[3:]
        return Response(ok=True, code=0, args=payload.split("|"), raw=line), None
    if line == "PONG":
        return Response(ok=True, code=0, args=["PONG"], raw=line), None
    if line.startswith("ERR|"):
        parts = line.split("|", 2)
        code = 0
        if len(parts) >= 2:
            try:
                code = int(parts[1])
            except ValueError:
                code = 0
        desc = parts[2] if len(parts) >= 3 else ""
        return Response(ok=False, code=code, args=[desc], raw=line), None

    head = line.split("|", 1)[0]
    maxsplit = EVENT_MAXSPLIT.get(head, -1)
    if maxsplit > 0:
        parts = line.split("|", maxsplit)
    else:
        parts = line.split("|")
    return None, Event(name=parts[0], args=parts[1:], raw=line)


def load_netsketch_lib(lib_path: Optional[str] = None) -> ctypes.CDLL:
    if lib_path:
        candidates = [Path(lib_path)]
    else:
        root = Path(__file__).resolve().parents[2]
        env_path = os.environ.get("NETSKETCH_LIB")
        candidates = []
        if env_path:
            candidates.append(Path(env_path))
        candidates.extend([
            root / "build" / "libnetsketch.so",
            root / "build" / "libnetsketch.dylib",
            root / "build" / "cov" / "libnetsketch.so",
        ])

    chosen = next((p for p in candidates if p.exists()), None)
    if chosen is None:
        raise FileNotFoundError(
            f"Cannot find libnetsketch shared library in {[str(c) for c in candidates]}"
        )

    lib = ctypes.CDLL(str(chosen))
    lib.ns_connect.argtypes = [ctypes.c_char_p, ctypes.c_int]
    lib.ns_connect.restype = ctypes.c_void_p

    lib.ns_from_fd.argtypes = [ctypes.c_int]
    lib.ns_from_fd.restype = ctypes.c_void_p

    lib.ns_send_line.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
    lib.ns_send_line.restype = ctypes.c_int

    lib.ns_recv_line.argtypes = [
        ctypes.c_void_p,
        ctypes.c_char_p,
        ctypes.c_size_t,
        ctypes.c_int,
    ]
    lib.ns_recv_line.restype = ctypes.c_int

    lib.ns_close.argtypes = [ctypes.c_void_p]
    lib.ns_close.restype = None
    return lib


class NetSketchClient:
    """Single-threaded non-blocking client wrapping libnetsketch via ctypes."""

    def __init__(
        self,
        lib_path: Optional[str] = None,
        on_event: Optional[Callable[[Event], None]] = None,
        on_async_error: Optional[Callable[[Response], None]] = None,
        on_disconnect: Optional[Callable[[str], None]] = None,
        ping_interval_ms: int = DEFAULT_PING_INTERVAL_MS,
        hello_timeout_s: float = DEFAULT_HELLO_TIMEOUT_S,
        clock_fn: Callable[[], float] = time.monotonic,
    ) -> None:
        self._lib = load_netsketch_lib(lib_path)
        self._conn: Optional[int] = None
        self._rx_buf = ctypes.create_string_buffer(RECV_BUF_CAP)
        self._pending: Deque[PendingCommand] = collections.deque()

        self.on_event = on_event
        self.on_async_error = on_async_error
        self.on_disconnect = on_disconnect

        self.ping_interval_ms = ping_interval_ms
        self.hello_timeout_s = hello_timeout_s
        self._clock = clock_fn

        self.hello_received = False
        self.pong_count = 0
        self._hello_deadline = 0.0
        self._next_ping_at = 0.0
        self._disconnect_notified = False
        self._disconnect_reason = "DISCONNECT"

        self._tk_root = None
        self._poll_after_id = None

    @property
    def connected(self) -> bool:
        return self._conn is not None

    @property
    def pending_count(self) -> int:
        return len(self._pending)

    def _reset_session_state(self) -> None:
        self._pending.clear()
        self.hello_received = False
        self._hello_deadline = self._clock() + self.hello_timeout_s
        self._next_ping_at = self._clock() + (self.ping_interval_ms / 1000.0)
        self._disconnect_notified = False
        self._disconnect_reason = "DISCONNECT"

    def connect(self, host: str, port: int) -> bool:
        self.close(notify=False)
        ptr = self._lib.ns_connect(host.encode("utf-8"), int(port))
        if not ptr:
            return False
        self._conn = ptr
        self._reset_session_state()
        return True

    def connect_fd(self, fd: int) -> bool:
        """Attach to an existing file descriptor (ownership transferred to ns_close)."""
        self.close(notify=False)
        ptr = self._lib.ns_from_fd(int(fd))
        if not ptr:
            return False
        self._conn = ptr
        self._reset_session_state()
        return True

    def close(self, notify: bool = False, reason: str = "DISCONNECT") -> None:
        if self._tk_root is not None and self._poll_after_id is not None:
            try:
                self._tk_root.after_cancel(self._poll_after_id)
            except Exception:
                pass
            self._poll_after_id = None

        was_open = self._conn is not None
        if self._conn is not None:
            self._lib.ns_close(self._conn)
            self._conn = None

        pending_copy = list(self._pending)
        self._pending.clear()
        for item in pending_copy:
            if item.callback is not None:
                self._safe_call(
                    item.callback,
                    Response(ok=False, code=-1, args=[reason], raw=""),
                )

        if notify and (was_open or not self._disconnect_notified) and not self._disconnect_notified:
            self._disconnect_notified = True
            if self.on_disconnect is not None:
                self._safe_call(self.on_disconnect, reason)

    @staticmethod
    def _safe_call(fn: Callable, *args) -> None:
        try:
            fn(*args)
        except Exception:
            logger.exception("Unhandled exception in UI callback")

    def _raw_send(self, line: str) -> bool:
        if self._conn is None:
            return False
        rc = self._lib.ns_send_line(self._conn, line.encode("utf-8"))
        if rc != 0:
            self.close(notify=True, reason="SEND_FAILED")
            return False
        self._next_ping_at = self._clock() + (self.ping_interval_ms / 1000.0)
        return True

    def send_cmd(
        self,
        line: str,
        callback: Optional[Callable[[Response], None]] = None,
    ) -> bool:
        """Send a command that expects a single OK/ERR response in FIFO order."""
        cmd_name = line.split("|", 1)[0].strip()
        if cmd_name == "PING":
            return self.send_ping()
        if cmd_name in ("STROKE", "CLEAR"):
            return self.send_no_reply(line)

        if self._conn is None:
            if callback is not None:
                self._safe_call(
                    callback,
                    Response(ok=False, code=-1, args=["NOT_CONNECTED"], raw=""),
                )
            return False

        valid_errs = COMMAND_ERRORS.get(cmd_name, _COMMON_SYNTAX)
        self._pending.append(PendingCommand(cmd_name, valid_errs, callback))
        if not self._raw_send(line):
            return False
        return True

    def send_no_reply(self, line: str) -> bool:
        """Send STROKE or CLEAR (which produce no OK response on success)."""
        return self._raw_send(line)

    def send_ping(self) -> bool:
        """Send PING without adding to _pending; PONG is matched by token."""
        return self._raw_send("PING")

    def _handle_line(self, line: str) -> None:
        resp, ev = parse_server_line(line)

        if ev is not None:
            if ev.name == "HELLO":
                if len(ev.args) >= 2 and ev.args[0] == "netsketch" and ev.args[1] == "1":
                    self.hello_received = True
                else:
                    self.close(notify=True, reason="BAD_HELLO")
                    return
            elif ev.name == "KICK":
                kick_reason = ev.args[0] if ev.args else "KICK"
                self._disconnect_reason = f"KICK:{kick_reason}"

            if self.on_event is not None:
                self._safe_call(self.on_event, ev)
            return

        assert resp is not None
        # PONG is matched directly by token so PING never occupies the FIFO queue.
        if resp.raw == "PONG":
            self.pong_count += 1
            return

        if not resp.ok:
            # If queue is empty OR the error code cannot belong to the head command
            # (e.g., an async ERR|300/301/305 from STROKE/CLEAR arriving while another
            # command is pending), route it to on_async_error without popping _pending.
            if not self._pending or resp.code not in self._pending[0].valid_errors:
                if self.on_async_error is not None:
                    self._safe_call(self.on_async_error, resp)
                return

        if self._pending:
            item = self._pending.popleft()
            if item.callback is not None:
                self._safe_call(item.callback, resp)

    def poll_once(
        self,
        max_lines: int = DEFAULT_POLL_MAX_LINES,
        max_ms: float = DEFAULT_POLL_MAX_MS,
    ) -> int:
        """Drain up to max_lines or max_ms from the non-blocking socket."""
        if self._conn is None:
            return 0

        start = self._clock()
        deadline = start + (max_ms / 1000.0)
        processed = 0

        while self._conn is not None and processed < max_lines:
            rc = self._lib.ns_recv_line(self._conn, self._rx_buf, RECV_BUF_CAP, 0)
            if rc == 1:
                raw_bytes = ctypes.string_at(self._rx_buf)
                line = raw_bytes.decode("utf-8", errors="replace")
                processed += 1
                self._handle_line(line)
                if processed < max_lines and self._clock() >= deadline:
                    break
            elif rc == 0:
                break
            else:
                self.close(notify=True, reason=self._disconnect_reason)
                return processed

        if self._conn is not None:
            now = self._clock()
            if not self.hello_received and now >= self._hello_deadline:
                self.close(notify=True, reason="HELLO_TIMEOUT")
                return processed
            if self.hello_received and self.ping_interval_ms > 0 and now >= self._next_ping_at:
                self.send_ping()

        return processed

    def start_polling(self, tk_root, interval_ms: int = 20) -> None:
        """Schedule recurring poll_once on a Tkinter root without threads."""
        self._tk_root = tk_root

        def _tick() -> None:
            self._poll_after_id = None
            try:
                self.poll_once()
            except Exception:
                logger.exception("Unexpected error in poll_once")
            finally:
                if self._tk_root is not None and self._conn is not None:
                    try:
                        self._poll_after_id = self._tk_root.after(interval_ms, _tick)
                    except Exception:
                        self._poll_after_id = None

        self._poll_after_id = tk_root.after(interval_ms, _tick)
