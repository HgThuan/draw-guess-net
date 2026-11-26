#!/usr/bin/env python3
"""Check weekly git commit cadence (minimum commits/week and gap between consecutive commits)."""
from __future__ import annotations

import argparse
from collections import defaultdict
from datetime import datetime, timezone
import subprocess
import sys


def load_commits(rev: str, no_merges: bool) -> list[tuple[str, int, str]]:
    cmd = ["git", "log", rev, "--reverse", "--format=%h\t%ct\t%s"]
    if no_merges:
        cmd.insert(3, "--no-merges")
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or f"git log failed for '{rev}'")

    commits: list[tuple[str, int, str]] = []
    for raw in proc.stdout.splitlines():
        parts = raw.split("\t", 2)
        if len(parts) == 3:
            commits.append((parts[0], int(parts[1]), parts[2]))
    return commits


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Verify weekly commit count and spacing between consecutive commits."
    )
    parser.add_argument("--rev", default="HEAD", help="Git revision/branch to inspect (default: HEAD)")
    parser.add_argument("--min-per-week", type=int, default=5, help="Minimum commits per ISO week (default: 5)")
    parser.add_argument("--min-gap-min", type=int, default=30, help="Minimum minutes between consecutive commits (default: 30)")
    parser.add_argument("--include-merges", action="store_true", help="Include merge commits in counts")
    args = parser.parse_args(argv)

    try:
        commits = load_commits(args.rev, no_merges=not args.include_merges)
    except RuntimeError as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 2

    if not commits:
        print("Khong tim thay commit nao.")
        return 1

    by_week: dict[str, list[tuple[str, int, str]]] = defaultdict(list)
    for sha, ts, msg in commits:
        dt = datetime.fromtimestamp(ts, tz=timezone.utc).astimezone()
        iso = dt.isocalendar()
        week_key = f"{iso.year}-W{iso.week:02d}"
        by_week[week_key].append((sha, ts, msg))

    min_gap_s = args.min_gap_min * 60
    print(f"{'Tuan':<10} {'So commit':>10} {'Khoang nho nhat':>18} {'Trang thai':<14}")
    print("-" * 58)

    all_ok = True
    short_gaps: list[tuple[str, str, str, float]] = []

    for week in sorted(by_week.keys()):
        items = by_week[week]
        count = len(items)
        min_gap = None
        for i in range(1, count):
            gap = items[i][1] - items[i - 1][1]
            if min_gap is None or gap < min_gap:
                min_gap = gap
            if gap < min_gap_s:
                short_gaps.append((week, items[i - 1][0], items[i][0], gap / 60.0))

        gap_str = "-" if min_gap is None else f"{min_gap / 60.0:.1f} phut"
        ok_week = count >= args.min_per_week and (min_gap is None or min_gap >= min_gap_s)
        status = "DAT" if ok_week else "CAN XEM LAI"
        if not ok_week:
            all_ok = False
        print(f"{week:<10} {count:>10} {gap_str:>18} {status:<14}")

    if short_gaps:
        print("\nCac cap commit cach nhau < {} phut:".format(args.min_gap_min))
        for week, a, b, mins in short_gaps:
            print(f"  - [{week}] {a} -> {b}: {mins:.1f} phut")

    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
