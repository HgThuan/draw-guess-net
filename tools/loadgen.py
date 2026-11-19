#!/usr/bin/env python3
"""Asyncio load generator and latency/resource profiler for netsketch-server.

Measures real-time stroke relay latency by encoding a 24-bit sequence number
into the 6-hex-digit STROKE color field and comparing send/receive timestamps
within the same process (shared time.perf_counter() clock).
"""
from __future__ import annotations

import argparse
import asyncio
import csv
import datetime as dt
import json
import os
import platform
import random
import statistics
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple


STROKE_POINTS_10 = "10,10,20,20,30,30,40,40,50,50,60,60,70,70,80,80,90,90,100,100"
BOT_PASS = "BotPass123"


def percentile(sorted_vals: List[float], p: float) -> float:
    if not sorted_vals:
        return 0.0
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    k = (len(sorted_vals) - 1) * (p / 100.0)
    f = int(k)
    c = min(f + 1, len(sorted_vals) - 1)
    return sorted_vals[f] + (sorted_vals[c] - sorted_vals[f]) * (k - f)


@dataclass
class RoomShared:
    group_idx: int
    room_id: int = 0
    room_ready: asyncio.Event = field(default_factory=asyncio.Event)
    joined_count: int = 0
    all_joined: asyncio.Event = field(default_factory=asyncio.Event)
    current_word: str = ""
    stroke_send_ts: Dict[int, float] = field(default_factory=dict)


@dataclass
class LoadStats:
    steady_start: float = 0.0
    steady_end: float = 0.0
    strokes_sent: int = 0
    strokes_received: int = 0
    total_commands: int = 0
    protocol_errors: int = 0
    unexpected_disconnects: int = 0
    stroke_latencies: List[Tuple[float, float]] = field(default_factory=list)
    login_latencies_ms: List[float] = field(default_factory=list)
    room_latencies_ms: List[float] = field(default_factory=list)
    guess_latencies_ms: List[float] = field(default_factory=list)
    resources: List[Tuple[float, float, float]] = field(default_factory=list)

    def in_steady(self, now: float) -> bool:
        return self.steady_start > 0.0 and self.steady_start <= now <= self.steady_end


def read_proc_stats(pid: int) -> Optional[Tuple[float, float]]:
    """Read (cpu_ticks, rss_mb) from Linux /proc/<pid>."""
    try:
        with open(f"/proc/{pid}/stat", "r", encoding="utf-8") as f:
            raw = f.read()
        rparen = raw.rfind(")")
        fields = raw[rparen + 2 :].split()
        utime = int(fields[11])
        stime = int(fields[12])
        ticks = float(utime + stime)

        rss_kb = 0
        with open(f"/proc/{pid}/status", "r", encoding="utf-8") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    rss_kb = int(line.split()[1])
                    break
        return ticks, rss_kb / 1024.0
    except OSError:
        return None


async def monitor_resources(pid: int, stats: LoadStats, stop_ev: asyncio.Event) -> None:
    clk_tck = float(os.sysconf(os.sysconf_names["SC_CLK_TCK"]))
    prev = read_proc_stats(pid)
    prev_t = time.perf_counter()
    start_t = prev_t

    while not stop_ev.is_set():
        await asyncio.sleep(1.0)
        cur = read_proc_stats(pid)
        now = time.perf_counter()
        if cur is None:
            break
        if prev is not None and now > prev_t:
            dt_cpu = (cur[0] - prev[0]) / clk_tck
            cpu_pct = max(0.0, (dt_cpu / (now - prev_t)) * 100.0)
            stats.resources.append((now - start_t, cpu_pct, cur[1]))
        prev = cur
        prev_t = now


async def seed_users(host: str, port: int, count: int) -> None:
    """Pre-register bot_0000..bot_<count-1> in batches so login measurements avoid disk writes."""
    sem = asyncio.Semaphore(50)

    async def _reg_one(idx: int) -> None:
        username = f"bot_{idx:04d}"
        async with sem:
            reader, writer = await asyncio.open_connection(host, port)
            try:
                hello = await asyncio.wait_for(reader.readline(), timeout=5.0)
                if not hello.startswith(b"HELLO|"):
                    raise RuntimeError(f"Bad HELLO during seed: {hello!r}")
                writer.write(f"REGISTER|{username}|{BOT_PASS}\n".encode("utf-8"))
                await writer.drain()
                resp = (await asyncio.wait_for(reader.readline(), timeout=5.0)).decode("utf-8", errors="replace").strip()
                if resp != "OK" and not resp.startswith("ERR|110|"):
                    raise RuntimeError(f"Seed register failed for {username}: {resp}")
            finally:
                writer.close()
                await writer.wait_closed()

    await asyncio.gather(*(_reg_one(i) for i in range(count)))


class BotClient:
    def __init__(
        self,
        bot_idx: int,
        group_size: int,
        is_host: bool,
        room: RoomShared,
        args: argparse.Namespace,
        stats: LoadStats,
        stop_ev: asyncio.Event,
        seq_counter: List[int],
    ) -> None:
        self.bot_idx = bot_idx
        self.username = f"bot_{bot_idx:04d}"
        self.group_size = group_size
        self.is_host = is_host
        self.room = room
        self.args = args
        self.stats = stats
        self.stop_ev = stop_ev
        self.seq_counter = seq_counter
        self.rng = random.Random(20261119 + bot_idx)

        self.reader: Optional[asyncio.StreamReader] = None
        self.writer: Optional[asyncio.StreamWriter] = None
        self.state = "LOBBY"
        self.drawer = ""
        self.guessed = False
        self.draw_started_at = 0.0
        self.pending_replies: asyncio.Queue[str] = asyncio.Queue()
        self.turn_task: Optional[asyncio.Task] = None

    async def _send_raw(self, line: str) -> None:
        assert self.writer is not None
        self.writer.write((line + "\n").encode("utf-8"))
        await self.writer.drain()
        self.stats.total_commands += 1

    async def _send_cmd(self, line: str) -> str:
        await self._send_raw(line)
        return await asyncio.wait_for(self.pending_replies.get(), timeout=10.0)

    def _cancel_turn_task(self) -> None:
        if self.turn_task is not None and not self.turn_task.done():
            self.turn_task.cancel()
        self.turn_task = None

    async def _drawer_loop(self) -> None:
        interval = 1.0 / max(1.0, float(self.args.stroke_rate))
        next_t = time.perf_counter()
        try:
            while not self.stop_ev.is_set() and self.state == "DRAWING" and self.drawer == self.username:
                seq = self.seq_counter[0] & 0xFFFFFF
                self.seq_counter[0] = (seq + 1) & 0xFFFFFF
                now = time.perf_counter()
                self.room.stroke_send_ts[seq] = now
                if len(self.room.stroke_send_ts) > 20000:
                    # Drop oldest entries to bound memory during long soak runs
                    cutoff = now - 10.0
                    self.room.stroke_send_ts = {
                        k: v for k, v in self.room.stroke_send_ts.items() if v >= cutoff
                    }
                color_hex = f"{seq:06X}"
                await self._send_raw(f"STROKE|{color_hex}|4|{STROKE_POINTS_10}")
                if self.stats.in_steady(now):
                    self.stats.strokes_sent += 1
                next_t += interval
                sleep_s = next_t - time.perf_counter()
                if sleep_s > 0:
                    await asyncio.sleep(sleep_s)
                else:
                    next_t = time.perf_counter()
        except asyncio.CancelledError:
            pass
        except OSError:
            pass

    async def _guesser_loop(self) -> None:
        # One designated guesser solves around ~18-22s so turns rotate continuously.
        will_solve = (self.bot_idx % self.group_size) == 1
        solve_delay = self.rng.uniform(16.0, 22.0)
        try:
            while not self.stop_ev.is_set() and self.state == "DRAWING" and not self.guessed:
                await asyncio.sleep(self.rng.uniform(3.0, 7.0))
                if self.stop_ev.is_set() or self.state != "DRAWING" or self.guessed:
                    break
                elapsed = time.perf_counter() - self.draw_started_at
                guess_word = (
                    self.room.current_word
                    if (will_solve and elapsed >= solve_delay and self.room.current_word)
                    else "wrongbot"
                )
                t0 = time.perf_counter()
                resp = await self._send_cmd(f"GUESS|{guess_word}")
                t1 = time.perf_counter()
                if self.stats.in_steady(t1):
                    self.stats.guess_latencies_ms.append((t1 - t0) * 1000.0)
                if resp.startswith("OK|CORRECT|"):
                    self.guessed = True
                    break
                if resp.startswith("ERR|") and not (
                    resp.startswith("ERR|300|") or resp.startswith("ERR|303|")
                ):
                    self.stats.protocol_errors += 1
        except asyncio.CancelledError:
            pass
        except (OSError, asyncio.TimeoutError):
            pass

    async def _reader_loop(self) -> None:
        assert self.reader is not None
        try:
            while not self.stop_ev.is_set():
                raw = await self.reader.readline()
                if not raw:
                    if not self.stop_ev.is_set():
                        self.stats.unexpected_disconnects += 1
                    break
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                if not line:
                    continue

                if line == "PONG":
                    continue
                if line == "OK" or line.startswith("OK|") or line.startswith("ERR|"):
                    await self.pending_replies.put(line)
                    continue

                parts = line.split("|")
                ev = parts[0]

                if ev == "STROKE" and len(parts) >= 4:
                    now = time.perf_counter()
                    if self.stats.in_steady(now):
                        self.stats.strokes_received += 1
                        try:
                            seq = int(parts[1], 16)
                            t0 = self.room.stroke_send_ts.get(seq)
                            if t0 is not None:
                                lat_ms = (now - t0) * 1000.0
                                self.stats.stroke_latencies.append((now - self.stats.steady_start, lat_ms))
                        except ValueError:
                            pass
                elif ev == "WORD_OPTIONS" and len(parts) >= 4:
                    self.room.current_word = parts[1]
                    asyncio.create_task(self._choose_word_1())
                elif ev == "DRAW_START" and len(parts) >= 4:
                    self._cancel_turn_task()
                    self.state = "DRAWING"
                    self.drawer = parts[1]
                    self.guessed = False
                    self.draw_started_at = time.perf_counter()
                    if self.drawer == self.username:
                        self.room.current_word = parts[3]
                        self.turn_task = asyncio.create_task(self._drawer_loop())
                    else:
                        self.turn_task = asyncio.create_task(self._guesser_loop())
                elif ev == "TURN_END":
                    self._cancel_turn_task()
                    self.state = "TURN_END"
                elif ev == "GAME_END":
                    self._cancel_turn_task()
                    self.state = "LOBBY"
                elif ev == "ROOM_INFO" and len(parts) >= 9:
                    self.state = parts[7]
                    if self.state == "LOBBY" and self.is_host and self.room.all_joined.is_set() and not self.stop_ev.is_set():
                        asyncio.create_task(self._restart_game())
        except asyncio.CancelledError:
            pass
        except OSError:
            if not self.stop_ev.is_set():
                self.stats.unexpected_disconnects += 1
        finally:
            self._cancel_turn_task()

    async def _choose_word_1(self) -> None:
        try:
            resp = await self._send_cmd("CHOOSE_WORD|1")
            if resp.startswith("ERR|") and not resp.startswith("ERR|300|"):
                self.stats.protocol_errors += 1
        except (OSError, asyncio.TimeoutError, asyncio.CancelledError):
            pass

    async def _restart_game(self) -> None:
        try:
            await asyncio.sleep(0.05)
            if self.stop_ev.is_set() or self.state != "LOBBY":
                return
            resp = await self._send_cmd("START")
            if resp.startswith("ERR|") and not resp.startswith("ERR|300|"):
                self.stats.protocol_errors += 1
        except (OSError, asyncio.TimeoutError, asyncio.CancelledError):
            pass

    async def run(self) -> None:
        try:
            self.reader, self.writer = await asyncio.open_connection(self.args.host, self.args.port)
            hello = (await asyncio.wait_for(self.reader.readline(), timeout=5.0)).decode("utf-8", errors="replace").strip()
            if not hello.startswith("HELLO|"):
                self.stats.protocol_errors += 1
                return

            reader_task = asyncio.create_task(self._reader_loop())
            try:
                t0 = time.perf_counter()
                resp = await self._send_cmd(f"LOGIN|{self.username}|{BOT_PASS}")
                self.stats.login_latencies_ms.append((time.perf_counter() - t0) * 1000.0)
                if not resp.startswith("OK|"):
                    self.stats.protocol_errors += 1
                    return

                if self.is_host:
                    t0 = time.perf_counter()
                    resp = await self._send_cmd(f"CREATE_ROOM|Room{self.room.group_idx}|{self.group_size}|10|60")
                    self.stats.room_latencies_ms.append((time.perf_counter() - t0) * 1000.0)
                    if not resp.startswith("OK|"):
                        self.stats.protocol_errors += 1
                        return
                    self.room.room_id = int(resp.split("|")[1])
                    self.room.joined_count += 1
                    self.room.room_ready.set()
                    if self.room.joined_count >= self.group_size:
                        self.room.all_joined.set()
                else:
                    await asyncio.wait_for(self.room.room_ready.wait(), timeout=20.0)
                    t0 = time.perf_counter()
                    resp = await self._send_cmd(f"JOIN_ROOM|{self.room.room_id}")
                    self.stats.room_latencies_ms.append((time.perf_counter() - t0) * 1000.0)
                    if not resp.startswith("OK|"):
                        self.stats.protocol_errors += 1
                        return
                    self.room.joined_count += 1
                    if self.room.joined_count >= self.group_size:
                        self.room.all_joined.set()

                if self.is_host:
                    await asyncio.wait_for(self.room.all_joined.wait(), timeout=30.0)
                    if self.group_size >= 2 and not self.stop_ev.is_set():
                        resp = await self._send_cmd("START")
                        if resp != "OK":
                            self.stats.protocol_errors += 1

                # Wait until steady state ends, sending periodic PINGs every 30s
                while not self.stop_ev.is_set():
                    try:
                        await asyncio.wait_for(self.stop_ev.wait(), timeout=30.0)
                    except asyncio.TimeoutError:
                        if not self.stop_ev.is_set():
                            await self._send_raw("PING")
            finally:
                self._cancel_turn_task()
                reader_task.cancel()
                try:
                    await reader_task
                except asyncio.CancelledError:
                    pass
        except (OSError, asyncio.TimeoutError):
            if not self.stop_ev.is_set():
                self.stats.unexpected_disconnects += 1
        finally:
            if self.writer is not None:
                self.writer.close()
                try:
                    await self.writer.wait_closed()
                except OSError:
                    pass


async def run_churn_worker(host: str, port: int, rate: float, stats: LoadStats, stop_ev: asyncio.Event) -> None:
    """Continuously connect, send PING, and disconnect at the requested churn rate."""
    if rate <= 0:
        return
    interval = 1.0 / rate
    while not stop_ev.is_set():
        t0 = time.perf_counter()
        try:
            reader, writer = await asyncio.open_connection(host, port)
            hello = await asyncio.wait_for(reader.readline(), timeout=3.0)
            if not hello.startswith(b"HELLO|"):
                stats.protocol_errors += 1
            writer.write(b"PING\n")
            await writer.drain()
            pong = await asyncio.wait_for(reader.readline(), timeout=3.0)
            if pong.strip() != b"PONG":
                stats.protocol_errors += 1
            writer.close()
            await writer.wait_closed()
        except (OSError, asyncio.TimeoutError):
            if not stop_ev.is_set():
                stats.unexpected_disconnects += 1
        sleep_s = interval - (time.perf_counter() - t0)
        if sleep_s > 0:
            await asyncio.sleep(sleep_s)


def write_outputs(out_dir: Path, args: argparse.Namespace, stats: LoadStats) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    lats = sorted(ms for _, ms in stats.stroke_latencies)
    duration = max(0.001, stats.steady_end - stats.steady_start)
    total_err = stats.protocol_errors + stats.unexpected_disconnects

    summary = {
        "scenario": args.scenario,
        "clients": args.clients,
        "room_size": args.room_size,
        "duration_s": args.duration,
        "stroke_rate": args.stroke_rate,
        "strokes_sent": stats.strokes_sent,
        "strokes_received": stats.strokes_received,
        "throughput_strokes_per_s": round(stats.strokes_received / duration, 2),
        "latency_samples": len(lats),
        "p50_ms": round(percentile(lats, 50), 3),
        "p95_ms": round(percentile(lats, 95), 3),
        "p99_ms": round(percentile(lats, 99), 3),
        "max_ms": round(lats[-1], 3) if lats else 0.0,
        "login_p50_ms": round(statistics.median(stats.login_latencies_ms), 3) if stats.login_latencies_ms else 0.0,
        "room_p50_ms": round(statistics.median(stats.room_latencies_ms), 3) if stats.room_latencies_ms else 0.0,
        "guess_p50_ms": round(statistics.median(stats.guess_latencies_ms), 3) if stats.guess_latencies_ms else 0.0,
        "total_commands": stats.total_commands,
        "protocol_errors": stats.protocol_errors,
        "unexpected_disconnects": stats.unexpected_disconnects,
        "error_rate": round(total_err / max(1, stats.total_commands), 6),
        "avg_cpu_pct": round(statistics.mean(r[1] for r in stats.resources), 2) if stats.resources else 0.0,
        "max_rss_mb": round(max(r[2] for r in stats.resources), 2) if stats.resources else 0.0,
        "env": {
            "platform": platform.platform(),
            "python": platform.python_version(),
            "cpu_count": os.cpu_count(),
        },
    }

    with open(out_dir / "summary.json", "w", encoding="utf-8") as f:
        json.dump(summary, f, indent=2)

    with open(out_dir / "latency.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["elapsed_s", "latency_ms"])
        # Cap CSV rows to 50,000 evenly sampled points so long soak runs stay manageable
        step = max(1, len(stats.stroke_latencies) // 50000)
        for t_s, ms in stats.stroke_latencies[::step]:
            w.writerow([f"{t_s:.3f}", f"{ms:.3f}"])

    with open(out_dir / "resources.csv", "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["elapsed_s", "cpu_pct", "rss_mb"])
        for t_s, cpu, rss in stats.resources:
            w.writerow([f"{t_s:.2f}", f"{cpu:.2f}", f"{rss:.2f}"])

    if not args.no_plots:
        try:
            import matplotlib

            matplotlib.use("Agg")
            import matplotlib.pyplot as plt

            if stats.stroke_latencies:
                step = max(1, len(stats.stroke_latencies) // 5000)
                xs = [t for t, _ in stats.stroke_latencies[::step]]
                ys = [m for _, m in stats.stroke_latencies[::step]]

                plt.figure(figsize=(8, 4))
                plt.plot(xs, ys, ".", markersize=2, alpha=0.5)
                plt.xlabel("Elapsed Time (s)")
                plt.ylabel("Stroke Relay Latency (ms)")
                plt.title(f"Latency Over Time ({args.clients} clients, p95={summary['p95_ms']} ms)")
                plt.grid(True, alpha=0.3)
                plt.tight_layout()
                plt.savefig(out_dir / "latency_over_time.png", dpi=120)
                plt.close()

                plt.figure(figsize=(8, 4))
                plt.hist(lats, bins=50, color="#1E88E5", alpha=0.8)
                plt.axvline(summary["p95_ms"], color="#E53935", linestyle="--", label=f"p95={summary['p95_ms']}ms")
                plt.xlabel("Latency (ms)")
                plt.ylabel("Count")
                plt.title(f"Latency Distribution ({args.clients} clients)")
                plt.legend()
                plt.grid(True, alpha=0.3)
                plt.tight_layout()
                plt.savefig(out_dir / "latency_hist.png", dpi=120)
                plt.close()

            if stats.resources:
                ts = [r[0] for r in stats.resources]
                cpus = [r[1] for r in stats.resources]
                rss = [r[2] for r in stats.resources]

                fig, ax1 = plt.subplots(figsize=(8, 4))
                ax1.plot(ts, cpus, color="#E53935", label="CPU (%)")
                ax1.set_xlabel("Elapsed Time (s)")
                ax1.set_ylabel("CPU (%)", color="#E53935")
                ax2 = ax1.twinx()
                ax2.plot(ts, rss, color="#1E88E5", label="RSS (MB)")
                ax2.set_ylabel("RSS (MB)", color="#1E88E5")
                plt.title(f"Server Resources ({args.clients} clients, max RSS={summary['max_rss_mb']} MB)")
                plt.tight_layout()
                plt.savefig(out_dir / "resources.png", dpi=120)
                plt.close()
        except ImportError:
            pass

    return summary


async def run_load(args: argparse.Namespace) -> dict:
    if args.seed_users:
        await seed_users(args.host, args.port, args.clients)
        print(f"Seeded {args.clients} bot accounts (bot_0000..bot_{args.clients - 1:04d}).")
        return {}

    stats = LoadStats()
    stop_ev = asyncio.Event()
    seq_counter = [1]

    # Partition clients into rooms of up to room_size (merging a trailing singleton into the last room)
    groups: List[List[int]] = []
    idx = 0
    while idx < args.clients:
        chunk = list(range(idx, min(idx + args.room_size, args.clients)))
        if len(chunk) == 1 and groups:
            groups[-1].extend(chunk)
        else:
            groups.append(chunk)
        idx += args.room_size

    bots: List[BotClient] = []
    for g_idx, members in enumerate(groups):
        shared = RoomShared(group_idx=g_idx)
        for pos, bot_idx in enumerate(members):
            bots.append(
                BotClient(
                    bot_idx=bot_idx,
                    group_size=len(members),
                    is_host=(pos == 0),
                    room=shared,
                    args=args,
                    stats=stats,
                    stop_ev=stop_ev,
                    seq_counter=seq_counter,
                )
            )

    res_task = (
        asyncio.create_task(monitor_resources(args.server_pid, stats, stop_ev))
        if args.server_pid > 0
        else None
    )
    churn_task = (
        asyncio.create_task(run_churn_worker(args.host, args.port, args.churn_rate, stats, stop_ev))
        if args.churn_rate > 0
        else None
    )

    # Ramp-up phase
    bot_tasks: List[asyncio.Task] = []
    ramp_delay = 1.0 / max(1.0, float(args.ramp_rate))
    for b in bots:
        bot_tasks.append(asyncio.create_task(b.run()))
        await asyncio.sleep(ramp_delay)

    # Brief settle window for all rooms to enter DRAWING, then measure steady-state window
    await asyncio.sleep(1.5)
    stats.steady_start = time.perf_counter()
    stats.steady_end = stats.steady_start + float(args.duration)
    await asyncio.sleep(float(args.duration))

    stop_ev.set()
    await asyncio.gather(*bot_tasks, return_exceptions=True)
    if res_task is not None:
        await res_task
    if churn_task is not None:
        await churn_task

    today = dt.date.today().isoformat()
    out_dir = Path(args.out_dir) if args.out_dir else Path("results") / f"{today}_{args.scenario}"
    return write_outputs(out_dir, args, stats)


def main() -> int:
    parser = argparse.ArgumentParser(description="netsketch asyncio load generator")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9000)
    parser.add_argument("--clients", type=int, default=100)
    parser.add_argument("--room-size", type=int, default=8)
    parser.add_argument("--duration", type=int, default=20, help="Steady-state measurement duration (s)")
    parser.add_argument("--ramp-rate", type=float, default=50.0, help="Clients per second during ramp-up")
    parser.add_argument("--stroke-rate", type=float, default=20.0, help="STROKE messages/s per drawer")
    parser.add_argument("--churn-rate", type=float, default=0.0, help="Extra connect/disconnect cycles/s")
    parser.add_argument("--scenario", default="ci")
    parser.add_argument("--server-pid", type=int, default=0, help="PID of server to sample /proc CPU & RSS")
    parser.add_argument("--out-dir", default="")
    parser.add_argument("--seed-users", action="store_true", help="Pre-register bot accounts and exit")
    parser.add_argument("--no-plots", action="store_true", help="Skip generating matplotlib PNG charts")
    parser.add_argument("--fail-on-error", action="store_true", help="Exit non-zero if any error occurs")
    args = parser.parse_args()

    summary = asyncio.run(run_load(args))
    if not summary:
        return 0

    print(json.dumps(summary, indent=2))
    if args.fail_on_error:
        if summary["protocol_errors"] > 0 or summary["unexpected_disconnects"] > 0:
            return 1
        if summary["strokes_received"] == 0:
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
