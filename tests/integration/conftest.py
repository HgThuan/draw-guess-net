import os
import signal
import socket
import subprocess
import time
from pathlib import Path
import pytest


def pytest_addoption(parser):
    parser.addoption(
        "--server",
        action="store",
        default="build/netsketch-server",
        help="Path to netsketch-server binary",
    )


def find_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Client:
    def __init__(self, host: str, port: int, timeout: float = 2.0):
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.sock.settimeout(timeout)
        self.sock.connect((host, port))
        self._buf = bytearray()

    def send_raw(self, data: bytes) -> None:
        self.sock.sendall(data)

    def send(self, line: str) -> None:
        if not line.endswith("\n"):
            line += "\n"
        self.send_raw(line.encode("utf-8"))

    def recv_line(self, timeout: float = 2.0) -> str:
        deadline = time.monotonic() + timeout
        while True:
            nl = self._buf.find(b"\n")
            if nl != -1:
                raw = bytes(self._buf[:nl])
                del self._buf[: nl + 1]
                if raw.endswith(b"\r"):
                    raw = raw[:-1]
                return raw.decode("utf-8", errors="replace")

            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("Timed out waiting for line from server")

            self.sock.settimeout(remaining)
            chunk = self.sock.recv(4096)
            if not chunk:
                raise EOFError("Connection closed by server")
            self._buf.extend(chunk)

    def expect(self, prefix: str, timeout: float = 2.0) -> str:
        deadline = time.monotonic() + timeout
        while True:
            remaining = max(0.05, deadline - time.monotonic())
            line = self.recv_line(timeout=remaining)
            if line.startswith(prefix):
                return line
            if time.monotonic() >= deadline:
                raise AssertionError(f"Expected prefix {prefix!r}, last got {line!r}")

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass


class ServerInstance:
    def __init__(self, binary: str, data_dir: Path, words_path: str, extra_args=None):
        self.binary = binary
        self.data_dir = data_dir
        self.words_path = words_path
        self.port = find_free_port()
        self.clients: list[Client] = []
        self._stopped = False

        cmd = [
            self.binary,
            "--port",
            str(self.port),
            "--data-dir",
            str(self.data_dir),
            "--words",
            self.words_path,
            "--choose-time",
            "2",
            "--turn-pause",
            "1",
            "--idle-timeout",
            "2",
        ]
        if extra_args:
            cmd.extend(extra_args)

        self.proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        self._wait_until_listening(timeout=5.0)

    def _wait_until_listening(self, timeout: float = 5.0) -> None:
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if self.proc.poll() is not None:
                _, err = self.proc.communicate(timeout=1.0)
                raise RuntimeError(
                    f"Server exited early with code {self.proc.returncode}: {err}"
                )
            try:
                c = Client("127.0.0.1", self.port, timeout=0.5)
                hello = c.recv_line(timeout=1.0)
                assert hello == "HELLO|netsketch|1"
                c.close()
                return
            except OSError:
                time.sleep(0.02)
        raise TimeoutError(f"Server did not open port {self.port} within {timeout}s")

    def connect(self, expect_hello: bool = True) -> Client:
        c = Client("127.0.0.1", self.port)
        self.clients.append(c)
        if expect_hello:
            line = c.recv_line(timeout=2.0)
            assert line == "HELLO|netsketch|1"
        return c

    def stop(self) -> int:
        if self._stopped:
            return self.proc.returncode
        self._stopped = True

        for c in self.clients:
            c.close()
        self.clients.clear()

        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGTERM)
        try:
            _, stderr = self.proc.communicate(timeout=5.0)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            _, stderr = self.proc.communicate(timeout=2.0)
            raise AssertionError("Server did not exit within 5s after SIGTERM")

        for marker in ("AddressSanitizer", "LeakSanitizer", "UndefinedBehaviorSanitizer"):
            assert marker not in stderr, f"Sanitizer error detected:\n{stderr}"

        assert self.proc.returncode == 0, (
            f"Server exited with non-zero code {self.proc.returncode}.\nstderr:\n{stderr}"
        )
        return self.proc.returncode


@pytest.fixture
def server_factory(request, tmp_path):
    binary = request.config.getoption("--server")
    assert os.path.exists(binary), f"Server binary not found: {binary}"
    words_path = "tests/data/words_small.txt"
    instances: list[ServerInstance] = []

    def _start(extra_args=None, data_dir=None) -> ServerInstance:
        d = Path(data_dir) if data_dir is not None else tmp_path
        d.mkdir(parents=True, exist_ok=True)
        srv = ServerInstance(binary, d, words_path, extra_args=extra_args)
        instances.append(srv)
        return srv

    yield _start

    for srv in instances:
        srv.stop()


@pytest.fixture
def server(server_factory) -> ServerInstance:
    return server_factory()
