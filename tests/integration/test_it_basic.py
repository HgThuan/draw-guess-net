import subprocess
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


def test_startup_rejects_missing_or_short_words_file(request, tmp_path):
    """Server fails fast with non-zero exit when --words is missing or has < 3 words."""
    binary = request.config.getoption("--server")

    missing = subprocess.run(
        [binary, "--port", "19991", "--words", str(tmp_path / "does_not_exist.txt")],
        capture_output=True,
        text=True,
        timeout=5,
    )
    assert missing.returncode != 0
    assert "words" in missing.stderr.lower()

    short_words = tmp_path / "two_words.txt"
    short_words.write_text("cat\ndog\n", encoding="utf-8")
    short = subprocess.run(
        [binary, "--port", "19992", "--words", str(short_words)],
        capture_output=True,
        text=True,
        timeout=5,
    )
    assert short.returncode != 0
    assert "at least 3" in short.stderr.lower()
