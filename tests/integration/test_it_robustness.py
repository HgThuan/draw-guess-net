"""Robustness tests over real TCP verifying server stability under hostile/malformed traffic.

Scenarios covered:
1. 1 MB of deterministic pseudo-random bytes (fixed seed).
2. 100 KB without a newline, followed by a newline and a valid command (recovery).
3. Lines containing embedded NUL bytes, ASCII control characters, and invalid UTF-8 sequences.
4. Opening and closing 1000 connections sequentially (including mid-line disconnects and RSTs).
5. Slow client sending huge batches of PINGs without reading until OUT_BUF overflows.
6. Rapid flood of wrong-state commands (e.g. STROKE/GUESS when unauthenticated or in LOBBY).
"""
from __future__ import annotations

import random
import select
import socket
import struct
import time

from conftest import Client, login, make_room


def _assert_healthy(c: Client) -> None:
    c.send("PING")
    assert c.next_line(timeout=3.0) == "PONG"


def test_random_1mb_stream_does_not_crash_or_affect_others(server):
    """Sending 1 MB of seeded random bytes does not crash the server or disturb other clients."""
    clients, _ = make_room(server, ["alice", "bob"])
    c_good = clients[0]

    rng = random.Random(20261118)
    payload = rng.randbytes(1024 * 1024)

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s_bad:
        s_bad.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s_bad.connect(("127.0.0.1", server.port))
        s_bad.setblocking(False)

        offset = 0
        chunk_size = 16384
        deadline = time.monotonic() + 10.0
        while offset < len(payload) and time.monotonic() < deadline:
            rlist, wlist, _ = select.select([s_bad], [s_bad], [], 0.05)
            if rlist:
                try:
                    while s_bad.recv(65536):
                        pass
                except BlockingIOError:
                    pass
                except OSError:
                    break
            if wlist:
                try:
                    sent = s_bad.send(payload[offset : offset + chunk_size])
                    offset += sent
                except BlockingIOError:
                    pass
                except OSError:
                    break

    assert offset == len(payload), f"Only sent {offset} of {len(payload)} bytes"
    _assert_healthy(c_good)
    _assert_healthy(clients[1])


def test_100kb_without_newline_then_valid_command_recovers(server):
    """Sending 100 KB without '\\n' returns ERR|104 once and processes the next valid command."""
    c_good = login(server, "alice")
    c_long = server.connect()

    # Send 100 KB of 'A' without newline in chunks, then '\nPING\n'
    chunk = b"A" * 16384
    for _ in range(6):
        c_long.send_raw(chunk)
    c_long.send_raw(b"A" * (100 * 1024 - 6 * 16384) + b"\nPING\n")

    line1 = c_long.recv_line(timeout=3.0)
    assert line1.startswith("ERR|104|")
    line2 = c_long.recv_line(timeout=3.0)
    assert line2 == "PONG"

    _assert_healthy(c_good)


def test_nul_bytes_control_chars_and_invalid_utf8(server):
    """Lines with NUL bytes, control chars, or invalid UTF-8 are handled safely without crashing."""
    clients, _ = make_room(server, ["alice", "bob"])
    c_edge, c_good = clients[0], clients[1]

    # 1. Embedded NUL in command name -> 103
    c_edge.send_raw(b"PING\x00EXTRA\n")
    assert c_edge.next_line().startswith("ERR|103|")

    # 2. Only a NUL byte on a line -> 103
    c_edge.send_raw(b"\x00\n")
    assert c_edge.next_line().startswith("ERR|103|")

    # 3. Embedded NUL in argument -> 100 (control character rejected)
    c_edge.send_raw(b"CHAT|hello\x00world\n")
    assert c_edge.next_line().startswith("ERR|100|")

    # 4. Various ASCII control characters (0x01, 0x07, TAB 0x09, 0x1F, DEL 0x7F) -> 100
    for ctrl in (b"\x01", b"\x07", b"\t", b"\x1f", b"\x7f"):
        c_edge.send_raw(b"CHAT|bad" + ctrl + b"char\n")
        assert c_edge.next_line().startswith("ERR|100|")

    # 5. Invalid UTF-8 sequences (> 0x7F) inside CHAT -> server does not crash and forwards safely
    c_edge.send_raw(b"CHAT|invalid_utf8_\xff\xfe\x80\xc3\x28_ok\n")
    assert c_edge.next_line() == "OK"
    ev_edge = c_edge.next_line()
    ev_good = c_good.next_line()
    assert ev_edge.startswith("CHAT|alice|invalid_utf8_")
    assert ev_good.startswith("CHAT|alice|invalid_utf8_")

    _assert_healthy(c_edge)
    _assert_healthy(c_good)


def test_1000_sequential_connections_with_partial_line_disconnects(server):
    """Opening and closing 1000 connections (some mid-line or via RST) leaks no slots."""
    c_good = login(server, "keeper")

    for i in range(1000):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        s.connect(("127.0.0.1", server.port))
        mode = i % 4
        if mode == 0:
            # Close immediately after connect
            s.close()
        elif mode == 1:
            # Send half a line without '\n' then close
            s.sendall(b"REGISTER|partial_user|secret")
            s.close()
        elif mode == 2:
            # Send half a line then abort with TCP RST (SO_LINGER = 0)
            s.sendall(b"STROKE|FF0000|4|10,20,30")
            s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
            s.close()
        else:
            # Send valid PING then close without reading
            s.sendall(b"PING\n")
            s.close()

    _assert_healthy(c_good)
    c_after = login(server, "after1000")
    _assert_healthy(c_after)


def test_slow_client_flooding_pings_without_reading(server):
    """A client sending hundreds of thousands of bytes of PING without reading gets closed cleanly."""
    c_good = login(server, "healthy")

    slow = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    slow.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    slow.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    slow.connect(("127.0.0.1", server.port))
    slow.setblocking(False)

    # Each "PING\n" (5 bytes) produces "PONG\n" (5 bytes) into the client's 16 KB OUT_BUF
    batch = b"PING\n" * 4096
    closed_by_server = False
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        try:
            slow.send(batch)
        except BlockingIOError:
            time.sleep(0.005)
        except (BrokenPipeError, ConnectionResetError):
            closed_by_server = True
            break

    slow.close()
    assert closed_by_server
    _assert_healthy(c_good)


def test_rapid_wrong_state_commands_flood(server):
    """Flooding commands in wrong states (unauthenticated and in LOBBY) returns deterministic errors."""
    c_good = login(server, "bystander")
    c_unauth = server.connect()
    clients_lobby, _ = make_room(server, ["hostuser", "guestuser"])
    c_lobby = clients_lobby[0]

    # 1. 500 rapid wrong-state commands while unauthenticated -> all return 101
    unauth_cmds = [
        "STROKE|FF0000|4|0,0,10,10",
        "CLEAR",
        "GUESS|cat",
        "CHOOSE_WORD|1",
        "START",
    ]
    payload_unauth = "".join(unauth_cmds[i % len(unauth_cmds)] + "\n" for i in range(500)).encode("utf-8")
    c_unauth.send_raw(payload_unauth)
    for _ in range(500):
        line = c_unauth.recv_line(timeout=3.0)
        assert line.startswith("ERR|101|")

    # 2. 500 rapid wrong-state gameplay commands while in LOBBY -> all return 300
    lobby_cmds = [
        "STROKE|FF0000|4|0,0,10,10",
        "CLEAR",
        "GUESS|cat",
        "CHOOSE_WORD|1",
    ]
    payload_lobby = "".join(lobby_cmds[i % len(lobby_cmds)] + "\n" for i in range(500)).encode("utf-8")
    c_lobby.send_raw(payload_lobby)
    for _ in range(500):
        line = c_lobby.next_line(timeout=3.0)
        assert line.startswith("ERR|300|")

    _assert_healthy(c_unauth)
    _assert_healthy(c_lobby)
    _assert_healthy(c_good)
