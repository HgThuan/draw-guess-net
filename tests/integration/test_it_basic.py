import time


def test_tcp_line_framing(server):
    """Split a command byte-by-byte and coalesce multiple commands in one packet."""
    c = server.connect()

    # 1. Send "PING\n" byte-by-byte across multiple TCP writes
    for b in b"PING\n":
        c.send_raw(bytes([b]))
        time.sleep(0.005)
    assert c.recv_line() == "PONG"

    # 2. Send multiple commands coalesced in a single write (including CRLF and empty line)
    c.send_raw(b"PING\r\n\nFOO\nPING\n")
    assert c.recv_line() == "PONG"
    assert c.recv_line().startswith("ERR|103|")
    assert c.recv_line() == "PONG"

    # 3. Overlong line (513 bytes) returns ERR|104 without closing the connection
    c.send_raw((b"A" * 512) + b"\nPING\n")
    assert c.recv_line().startswith("ERR|104|")
    assert c.recv_line() == "PONG"


def test_graceful_shutdown_sigterm(server):
    """Server exits with returncode == 0 on SIGTERM while clients are connected."""
    clients = [server.connect() for _ in range(3)]
    for c in clients:
        c.send("PING")
    for c in clients:
        assert c.recv_line() == "PONG"

    # Stop server while client sockets are still open
    rc = server.stop()
    assert rc == 0
