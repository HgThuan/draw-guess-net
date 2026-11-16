import socket
import time
import pytest


def _register_and_login(srv, username: str):
    c = srv.connect()
    c.send(f"REGISTER|{username}|Secret123")
    assert c.recv_line() == "OK"
    c.send(f"LOGIN|{username}|Secret123")
    assert c.recv_line().startswith("OK|")
    return c


def test_server_full_and_slot_recovery(server_factory):
    """Connecting beyond max_clients returns 901 + KICK|SERVER_FULL; freeing a slot allows new clients."""
    srv = server_factory(extra_args=["--max-clients", "2"])
    c1 = srv.connect()
    c2 = srv.connect()

    # 3rd client exceeds max_clients=2
    c3 = srv.connect(expect_hello=False)
    line1 = c3.recv_line()
    line2 = c3.recv_line()
    assert line1.startswith("ERR|901|")
    assert line2 == "KICK|SERVER_FULL"
    with pytest.raises(EOFError):
        c3.recv_line(timeout=1.0)
    c3.close()

    # Close c1 and wait until server reaps the slot, then connect c4
    c1.close()
    deadline = time.monotonic() + 2.0
    connected = False
    while time.monotonic() < deadline:
        c4 = srv.connect(expect_hello=False)
        first = c4.recv_line()
        if first == "HELLO|netsketch|1":
            connected = True
            c4.send("PING")
            assert c4.recv_line() == "PONG"
            break
        c4.close()
        time.sleep(0.05)
    assert connected
    c2.send("PING")
    assert c2.recv_line() == "PONG"


def test_slow_client_out_buf_overflow(server):
    """A client that floods requests without reading gets closed when OUT_BUF overflows, without affecting others."""
    c_good = server.connect()

    slow_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    slow_sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4096)
    slow_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    slow_sock.connect(("127.0.0.1", server.port))
    slow_sock.setblocking(False)

    # 2 bytes in ("X\n") -> 24 bytes out ("ERR|103|unknown command\n") = 12x amplification
    payload = b"X\n" * 2048
    closed_by_server = False
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        try:
            slow_sock.send(payload)
        except BlockingIOError:
            time.sleep(0.005)
        except (BrokenPipeError, ConnectionResetError):
            closed_by_server = True
            break

    slow_sock.close()
    assert closed_by_server

    # Healthy client must remain completely unaffected
    c_good.send("PING")
    assert c_good.recv_line() == "PONG"


def test_parallel_rooms_event_isolation(server):
    """Events in one room never leak to players in another room."""
    a1 = _register_and_login(server, "alice")
    a2 = _register_and_login(server, "bob")
    b1 = _register_and_login(server, "carol")
    b2 = _register_and_login(server, "dave")

    # Room 1 created by alice, bob joins
    a1.send("CREATE_ROOM|RoomAlpha|4|3|60")
    assert a1.recv_line() == "OK|1"
    assert a1.recv_line().startswith("ROOM_INFO|1|RoomAlpha|alice|")

    # Room 2 created by carol, dave joins
    b1.send("CREATE_ROOM|RoomBeta|4|3|60")
    assert b1.recv_line() == "OK|2"
    assert b1.recv_line().startswith("ROOM_INFO|2|RoomBeta|carol|")

    a2.send("JOIN_ROOM|1")
    assert a2.recv_line() == "OK|1"
    assert a2.recv_line().startswith("ROOM_INFO|1|RoomAlpha|alice|4|3|60|LOBBY|alice,bob")
    assert a1.recv_line() == "PLAYER_JOINED|bob"

    b2.send("JOIN_ROOM|2")
    assert b2.recv_line() == "OK|2"
    assert b2.recv_line().startswith("ROOM_INFO|2|RoomBeta|carol|4|3|60|LOBBY|carol,dave")
    assert b1.recv_line() == "PLAYER_JOINED|dave"

    # alice leaves Room 1 -> only bob gets PLAYER_LEFT + HOST_CHANGED; Room 2 gets nothing
    a1.send("LEAVE_ROOM")
    assert a1.recv_line() == "OK"
    assert a2.recv_line() == "PLAYER_LEFT|alice|LEAVE"
    assert a2.recv_line() == "HOST_CHANGED|bob"

    # Verify b1 and b2 have no stray events in their buffers before PONG
    b1.send("PING")
    b2.send("PING")
    assert b1.recv_line() == "PONG"
    assert b2.recv_line() == "PONG"
