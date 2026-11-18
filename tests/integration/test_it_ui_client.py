"""Automated tests for the Python ctypes client wrapper and pure UI models/helpers.

Covers:
1. Line classification via socketpair() without a server (empty queue ERR, PONG token,
   async STROKE error interleaved with a pending command, pipes in CHAT/ERR, invalid UTF-8).
2. Disconnect handling (pending callbacks fail with code -1, on_disconnect called at most once).
3. Poll throttling (5000 lines split across bounded cycles; callback exceptions do not stop polling).
4. Timeouts (closed port, server that never sends HELLO, bad HELLO version).
5. Real server integration (two clients interleaving commands/events, periodic PING,
   KICK|IDLE, and repeated ROOM_INFO after GAME_END).
6. Pure Tk-independent helpers (pagination for 0, 1, 8, 9, 17 rooms, room names with spaces,
   RoomModel state transitions, and input/stroke validators).
"""
from __future__ import annotations

import socket
import sys
import time
from pathlib import Path

import pytest

ROOT_DIR = Path(__file__).resolve().parents[2]
UI_DIR = ROOT_DIR / "client" / "ui"
if str(UI_DIR) not in sys.path:
    sys.path.insert(0, str(UI_DIR))

from netsketch import (  # noqa: E402
     Event,
    NetSketchClient,
    Response,
    RoomModel,
    calc_total_pages,
    format_error_vi,
    parse_room_list,
    validate_password,
    validate_room_params,
    validate_stroke,
    validate_username,
)


def _make_socketpair_client(**kwargs):
    s_client, s_peer = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
    cli = NetSketchClient(**kwargs)
    # Detach fd so Python's socket wrapper does not close the fd owned by ns_close.
    fd = s_client.detach()
    assert cli.connect_fd(fd)
    return cli, s_peer


def test_socketpair_classification_interleaved_stroke_err_and_utf8():
    events: list[Event] = []
    async_errs: list[Response] = []
    replies: list[tuple[str, Response]] = []

    cli, peer = _make_socketpair_client(
        on_event=events.append,
        on_async_error=async_errs.append,
    )
    try:
        peer.sendall(b"HELLO|netsketch|1\n")
        cli.poll_once()
        assert cli.hello_received

        # 1. ERR when _pending is empty goes to on_async_error
        peer.sendall(b"ERR|104|line too long|extra\n")
        cli.poll_once()
        assert len(async_errs) == 1
        assert async_errs[0].code == 104
        assert async_errs[0].args == ["line too long|extra"]

        # 2. PONG is handled by token and never pops _pending;
        #    ERR|305 from a prior STROKE arriving while LEAVE_ROOM is pending
        #    is routed to on_async_error instead of consuming LEAVE_ROOM's callback.
        cli.send_no_reply("STROKE|FF0000|4|0,0,10,10")
        cli.send_cmd("LEAVE_ROOM", lambda r: replies.append(("LEAVE_ROOM", r)))
        cli.send_ping()

        peer.sendall(b"PONG\nERR|305|invalid stroke\nOK\n")
        cli.poll_once()

        assert cli.pong_count == 1
        assert len(async_errs) == 2
        assert async_errs[1].code == 305
        assert len(replies) == 1
        assert replies[0][0] == "LEAVE_ROOM"
        assert replies[0][1].ok is True

        # 3. Pipes inside CHAT and GUESS_WRONG are preserved, and invalid UTF-8 does not crash
        peer.sendall(b"CHAT|alice|a|b|c \xff\xfe end\nGUESS_WRONG|bob|x|y\n")
        cli.poll_once()
        chat_ev = [e for e in events if e.name == "CHAT"][0]
        assert chat_ev.args[0] == "alice"
        assert chat_ev.args[1].startswith("a|b|c ")
        assert chat_ev.args[1].endswith(" end")

        gw_ev = [e for e in events if e.name == "GUESS_WRONG"][0]
        assert gw_ev.args == ["bob", "x|y"]
    finally:
        peer.close()
        cli.close()


def test_disconnect_flushes_pending_and_notifies_once():
    disconnects: list[str] = []
    pending_results: list[Response] = []

    cli, peer = _make_socketpair_client(on_disconnect=disconnects.append)
    try:
        peer.sendall(b"HELLO|netsketch|1\n")
        cli.poll_once()

        cli.send_cmd("LIST_ROOMS|1", pending_results.append)
        cli.send_cmd("SCOREBOARD", pending_results.append)
        assert cli.pending_count == 2

        # Server sends KICK|IDLE then closes the socket
        peer.sendall(b"KICK|IDLE\n")
        peer.close()

        cli.poll_once()
        cli.poll_once()
        cli.close(notify=True)

        assert len(pending_results) == 2
        assert all(not r.ok and r.code == -1 for r in pending_results)
        assert disconnects == ["KICK:IDLE"]
    finally:
        cli.close()


def test_poll_throttling_5000_lines_and_callback_exception_safety():
    events: list[Event] = []

    def _flaky_on_event(ev: Event) -> None:
        events.append(ev)
        if len(events) == 5:
            raise RuntimeError("boom inside callback")

    cli, peer = _make_socketpair_client(on_event=_flaky_on_event)
    try:
        peer.sendall(b"HELLO|netsketch|1\n")
        cli.poll_once()

        # Send 5000 lines in batches while draining in bounded poll_once(max_lines=200) cycles
        total_sent = 5000
        chunk = b"TIME|55\n" * 500
        cycles = 0
        for _ in range(total_sent // 500):
            peer.sendall(chunk)
            while True:
                n = cli.poll_once(max_lines=200, max_ms=50.0)
                if n == 0:
                    break
                assert n <= 200
                cycles += 1

        # 1 HELLO + 5000 TIME events = 5001 events; took at least 25 cycles of <= 200 lines
        assert len(events) == 5001
        assert cycles >= 25
    finally:
        peer.close()
        cli.close()


def test_connect_closed_port_and_hello_timeout():
    # Closed port returns False quickly
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        closed_port = s.getsockname()[1]

    cli = NetSketchClient()
    assert cli.connect("127.0.0.1", closed_port) is False

    # Server that accepts TCP but never sends HELLO times out cleanly
    fake_clock = [100.0]
    disconnects: list[str] = []
    cli2, peer2 = _make_socketpair_client(
        hello_timeout_s=1.5,
        clock_fn=lambda: fake_clock[0],
        on_disconnect=disconnects.append,
    )
    try:
        cli2.poll_once()
        assert cli2.connected
        fake_clock[0] += 1.6
        cli2.poll_once()
        assert not cli2.connected
        assert disconnects == ["HELLO_TIMEOUT"]
    finally:
        peer2.close()
        cli2.close()

    # Bad HELLO version is rejected immediately
    disconnects3: list[str] = []
    cli3, peer3 = _make_socketpair_client(on_disconnect=disconnects3.append)
    try:
        peer3.sendall(b"HELLO|netsketch|99\n")
        cli3.poll_once()
        assert not cli3.connected
        assert disconnects3 == ["BAD_HELLO"]
    finally:
        peer3.close()
        cli3.close()


def test_pure_pagination_room_model_and_validators():
    # Pagination for 0, 1, 8, 9, 17 rooms
    assert calc_total_pages(0) == 1
    assert calc_total_pages(1) == 1
    assert calc_total_pages(8) == 1
    assert calc_total_pages(9) == 2
    assert calc_total_pages(17) == 3

    total, page, rooms = parse_room_list(["2", "1", "1,Phong Vui,2,4,LOBBY", "2,Room Two,3,8,PLAYING"])
    assert (total, page) == (2, 1)
    assert len(rooms) == 2
    assert rooms[0].name == "Phong Vui"
    assert rooms[1].state == "PLAYING"

    # RoomModel transitions across full lifecycle
    m = RoomModel()
    assert m.apply_event(Event("ROOM_INFO", ["1", "Phong Vui", "alice", "4", "2", "60", "LOBBY", "alice,bob"]))
    assert m.in_room and m.host == "alice" and m.players == ["alice", "bob"]

    assert m.apply_event(Event("PLAYER_JOINED", ["carol"]))
    assert m.players == ["alice", "bob", "carol"]

    assert m.apply_event(Event("PLAYER_LEFT", ["alice", "LEAVE"]))
    assert m.apply_event(Event("HOST_CHANGED", ["bob"]))
    assert m.host == "bob" and m.players == ["bob", "carol"]

    assert m.apply_event(Event("GAME_START", ["2", "bob,carol"]))
    assert m.state == "PLAYING"
    assert m.apply_event(Event("TURN_START", ["1", "bob", "15"]))
    assert m.state == "CHOOSING_WORD" and m.drawer == "bob"
    assert m.apply_event(Event("DRAW_START", ["bob", "60", "___"]))
    assert m.state == "DRAWING" and m.word_hint == "___"
    assert m.apply_event(Event("TIME", ["55"]))
    assert m.remaining_s == 55
    assert m.apply_event(Event("TURN_END", ["ALL_GUESSED", "cat", "bob:500:500,carol:500:500"]))
    assert m.state == "TURN_END"
    assert m.apply_event(Event("GAME_END", ["FINISHED", "bob:500,carol:500"]))
    assert m.state == "LOBBY"

    # Validators & Vietnamese error mapping
    assert validate_username("ab") is not None
    assert validate_username("alice_1") is None
    assert validate_password("12345") is not None
    assert validate_password("bad|pass") is not None
    assert validate_password("Secret123") is None
    assert validate_room_params("Phong Vui", 4, 2, 60) is None
    assert validate_room_params("", 4, 2, 60) is not None
    assert validate_stroke("FF00AA", 4, [(0, 0), (100, 200)]) is True
    assert validate_stroke("GGGGGG", 4, [(0, 0), (100, 200)]) is False
    assert "[111]" in format_error_vi(111)


def _drive_until(clients: list[NetSketchClient], pred, timeout: float = 4.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        for c in clients:
            c.poll_once()
        if pred():
            return
        time.sleep(0.01)
    raise AssertionError("Timed out waiting for condition across NetSketchClient instances")


def test_real_server_two_ui_clients_full_lobby_and_game_end_flow(server_factory):
    srv = server_factory(extra_args=["--idle-timeout", "2"])

    m1, m2 = RoomModel(), RoomModel()
    ev1, ev2 = [], []
    disc2 = []

    c1 = NetSketchClient(
        on_event=lambda e: (ev1.append(e), m1.apply_event(e)),
        ping_interval_ms=80,
    )
    c2 = NetSketchClient(
        on_event=lambda e: (ev2.append(e), m2.apply_event(e)),
        on_disconnect=disc2.append,
        ping_interval_ms=0,  # Disable auto-ping on c2 to test KICK|IDLE later
    )

    try:
        assert c1.connect("127.0.0.1", srv.port)
        assert c2.connect("127.0.0.1", srv.port)
        _drive_until([c1, c2], lambda: c1.hello_received and c2.hello_received)

        res: dict[str, Response] = {}
        c1.send_cmd("REGISTER|alice|Secret123", lambda r: res.setdefault("r1", r))
        c1.send_cmd("LOGIN|alice|Secret123", lambda r: res.setdefault("l1", r))
        c2.send_cmd("REGISTER|bob|Secret123", lambda r: res.setdefault("r2", r))
        c2.send_cmd("LOGIN|bob|Secret123", lambda r: res.setdefault("l2", r))
        _drive_until([c1, c2], lambda: len(res) == 4)
        assert all(r.ok for r in res.values())

        # Create room with spaces in name, list rooms, join from c2
        c1.send_cmd("CREATE_ROOM|Phong Vui|4|1|60", lambda r: res.setdefault("cr", r))
        _drive_until([c1, c2], lambda: "cr" in res and m1.in_room)
        assert m1.name == "Phong Vui" and m1.players == ["alice"]

        c2.send_cmd("LIST_ROOMS|1", lambda r: res.setdefault("lr", r))
        _drive_until([c1, c2], lambda: "lr" in res)
        total, page, rooms = parse_room_list(res["lr"].args)
        assert total == 1 and rooms[0].name == "Phong Vui"

        c2.send_cmd("JOIN_ROOM|1", lambda r: res.setdefault("jr", r))
        _drive_until([c1, c2], lambda: "jr" in res and m2.in_room and "bob" in m1.players)
        assert m1.players == ["alice", "bob"]
        assert m2.players == ["alice", "bob"]

        # Start game, then bob leaves -> GAME_END|NOT_ENOUGH_PLAYERS followed by ROOM_INFO
        c1.send_cmd("START", lambda r: res.setdefault("st", r))
        _drive_until([c1, c2], lambda: m1.state == "CHOOSING_WORD" and m2.state == "CHOOSING_WORD")

        c2.send_cmd("LEAVE_ROOM", lambda r: res.setdefault("lv", r))
        _drive_until([c1, c2], lambda: "lv" in res and m1.state == "LOBBY" and m1.players == ["alice"])
        room_infos_c1 = [e for e in ev1 if e.name == "ROOM_INFO"]
        assert len(room_infos_c1) == 2, "ROOM_INFO must arrive both on create and after GAME_END"

        # Wait ~2.2s: c1 stays alive via 80ms auto-PING, while c2 (ping disabled) gets KICK|IDLE
        _drive_until([c1, c2], lambda: len(disc2) == 1, timeout=4.0)
        assert disc2 == ["KICK:IDLE"]
        assert c1.connected and c1.pong_count >= 5
    finally:
        c1.close()
        c2.close()
