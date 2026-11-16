import time
import pytest


def test_account_persistence_across_restart(server_factory, tmp_path):
    """Register an account, stop the server, restart with the same data_dir, and log in."""
    srv1 = server_factory(data_dir=tmp_path)
    c1 = srv1.connect()
    c1.send("REGISTER|alice|Secret123")
    assert c1.recv_line() == "OK"
    srv1.stop()

    srv2 = server_factory(data_dir=tmp_path)
    c2 = srv2.connect()
    c2.send("LOGIN|alice|Secret123")
    resp = c2.recv_line()
    assert resp.startswith("OK|")
    assert len(resp.split("|")[1]) == 16


def test_single_connection_per_account(server):
    """Second connection to the same account gets 114 until logout or disconnect."""
    c1 = server.connect()
    c2 = server.connect()

    c1.send("REGISTER|bob|Pass1234")
    assert c1.recv_line() == "OK"

    c1.send("LOGIN|bob|Pass1234")
    assert c1.recv_line().startswith("OK|")

    # Second concurrent login must be rejected with 114
    c2.send("LOGIN|bob|Pass1234")
    assert c2.recv_line().startswith("ERR|114|")

    # After c1 logs out, c2 can log in
    c1.send("LOGOUT")
    assert c1.recv_line() == "OK"

    c2.send("LOGIN|bob|Pass1234")
    assert c2.recv_line().startswith("OK|")

    # If c2 disconnects abruptly, c1 can log in again once server reaps c2
    c2.close()
    deadline = time.monotonic() + 2.0
    logged_in = False
    while time.monotonic() < deadline:
        c1.send("LOGIN|bob|Pass1234")
        line = c1.recv_line()
        if line.startswith("OK|"):
            logged_in = True
            break
        time.sleep(0.05)
    assert logged_in


def test_session_idle_timeout_and_ping_keepalive(server_factory):
    """Silent client receives KICK|IDLE after idle timeout while active client stays open."""
    srv = server_factory(extra_args=["--idle-timeout", "1"])
    c_idle = srv.connect()
    c_keep = srv.connect()

    # Keep c_keep alive with PING while c_idle stays silent past 1s timeout
    for _ in range(3):
        time.sleep(0.45)
        c_keep.send("PING")
        assert c_keep.recv_line() == "PONG"

    assert c_idle.recv_line(timeout=2.0) == "KICK|IDLE"
    with pytest.raises(EOFError):
        c_idle.recv_line(timeout=1.0)

    # Active client is still operational
    c_keep.send("PING")
    assert c_keep.recv_line() == "PONG"
