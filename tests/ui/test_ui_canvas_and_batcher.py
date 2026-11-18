"""Fast, server-free unit tests for canvas coordinate conversion, StrokeBatcher, and stroke parsing."""
from __future__ import annotations

import random
import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[2]
UI_DIR = ROOT_DIR / "client" / "ui"
if str(UI_DIR) not in sys.path:
    sys.path.insert(0, str(UI_DIR))

from netsketch import (  # noqa: E402
    Event,
    RoomModel,
    StrokeBatcher,
    StrokeItem,
    norm_to_pixel,
    parse_stroke_event,
    pixel_to_norm,
    scale_stroke_width,
)


def _extract_points(cmd_line: str) -> list[tuple[int, int]]:
    parts = cmd_line.split("|")
    assert parts[0] == "STROKE" and len(parts) == 4
    nums = [int(v) for v in parts[3].split(",")]
    assert 4 <= len(nums) <= 80 and len(nums) % 2 == 0
    return [(nums[i], nums[i + 1]) for i in range(0, len(nums), 2)]


def test_coordinate_bounds_and_round_trip_within_1px():
    # Out-of-bounds negative and > W/H clamp to [0, 1000]
    assert pixel_to_norm(-25, -10, 600, 450) == (0, 0)
    assert pixel_to_norm(0, 0, 600, 450) == (0, 0)
    assert pixel_to_norm(600, 450, 600, 450) == (1000, 1000)
    assert pixel_to_norm(750, 999, 600, 450) == (1000, 1000)
    assert pixel_to_norm(10, 10, 0, 0) == (0, 0)
    assert norm_to_pixel(500, 500, 0, 0) == (0, 0)

    # Predictable half-up rounding: 300 / 600 -> 500
    assert pixel_to_norm(300, 225, 600, 450) == (500, 500)

    # Round-trip (pixel -> norm -> pixel) across multiple canvas dimensions stays within <= 1 px
    for w, h in ((400, 300), (480, 360), (600, 450), (800, 600)):
        for x in range(0, w + 1, 7):
            for y in range(0, h + 1, 9):
                nx, ny = pixel_to_norm(x, y, w, h)
                assert 0 <= nx <= 1000 and 0 <= ny <= 1000
                x2, y2 = norm_to_pixel(nx, ny, w, h)
                assert abs(x2 - x) <= 1
                assert abs(y2 - y) <= 1

    # Stroke width scaling against 600 px reference canvas
    assert scale_stroke_width(4, 600) == 4
    assert scale_stroke_width(4, 300) == 2
    assert scale_stroke_width(1, 300) == 1
    assert scale_stroke_width(10, 900) == 15


def test_stroke_batcher_45_points_exact_40_single_dot_tool_change_and_cancel():
    sent: list[str] = []
    local_batches: list[StrokeItem] = []
    b = StrokeBatcher(send_line_fn=lambda s: (sent.append(s), True)[1], on_local_batch=local_batches.append)

    # 1. 45 distinct points in one drag -> batch 1 has 40 points, batch 2 has 6 points,
    #    and point #0 of batch 2 equals point #39 of batch 1
    b.start_stroke(0, 0, color="E53935", width=6, now_ms=0.0)
    for i in range(1, 45):
        b.add_point(i * 10, i * 5, now_ms=5.0)
    assert len(sent) == 1
    pts1 = _extract_points(sent[0])
    assert len(pts1) == 40
    assert pts1[0] == (0, 0) and pts1[-1] == (390, 195)

    b.end_stroke(now_ms=10.0)
    assert len(sent) == 2
    pts2 = _extract_points(sent[1])
    assert len(pts2) == 6
    assert pts2[0] == pts1[-1] == (390, 195)
    assert pts2[-1] == (440, 220)

    # 2. Dragging exactly 40 points then releasing mouse:
    #    batch 1 flushes at point 40, leaving only 1 connecting point in buffer;
    #    releasing mouse must NOT emit a duplicate second batch
    sent.clear()
    b.start_stroke(10, 10, color="000000", width=4, now_ms=100.0)
    for i in range(1, 40):
        b.add_point(10 + i, 10 + i, now_ms=105.0)
    assert len(sent) == 1
    assert b.buffered_count == 1
    dot = b.end_stroke(now_ms=110.0)
    assert dot is None
    assert len(sent) == 1

    # 3. Clicking a single dot (1 point, then release without moving): emits [p0, p0]
    sent.clear()
    b.start_stroke(250, 350, color="1E88E5", width=8, now_ms=200.0)
    dot = b.end_stroke(now_ms=205.0)
    assert dot == (250, 350)
    assert len(sent) == 1
    assert sent[0] == "STROKE|1E88E5|8|250,350,250,350"

    # 4. 30 ms timer tick flushes mid-drag and keeps the connecting point;
    #    releasing right after emits nothing extra
    sent.clear()
    b.start_stroke(10, 20, color="43A047", width=5, now_ms=300.0)
    b.add_point(30, 40, now_ms=310.0)
    b.add_point(50, 60, now_ms=320.0)
    assert len(sent) == 0
    assert b.tick(329.0) is False
    assert b.tick(330.0) is True
    assert len(sent) == 1
    assert _extract_points(sent[0]) == [(10, 20), (30, 40), (50, 60)]
    assert b.buffered_count == 1
    b.end_stroke(now_ms=335.0)
    assert len(sent) == 1

    # 5. Changing tool mid-drag flushes existing points before switching color/width
    sent.clear()
    b.start_stroke(0, 0, color="000000", width=3, now_ms=400.0)
    b.add_point(100, 100, now_ms=405.0)
    b.change_tool(color="FFFFFF", width=12, now_ms=410.0)
    assert len(sent) == 1
    assert sent[0] == "STROKE|000000|3|0,0,100,100"
    b.add_point(200, 200, now_ms=415.0)
    b.end_stroke(now_ms=420.0)
    assert len(sent) == 2
    assert sent[1] == "STROKE|FFFFFF|12|100,100,200,200"

    # 6. cancel() on CLEAR or TURN_END discards unsent points
    sent.clear()
    b.start_stroke(10, 10, now_ms=500.0)
    b.add_point(20, 20, now_ms=505.0)
    b.cancel()
    assert b.buffered_count == 0 and not b.dragging
    b.end_stroke(now_ms=510.0)
    assert len(sent) == 0


def test_property_1000_random_point_sequences():
    rng = random.Random(20261118)
    all_emitted: list[str] = []

    for _ in range(1000):
        drag_batches: list[str] = []
        b = StrokeBatcher(send_line_fn=lambda s, out=drag_batches: (out.append(s), True)[1])
        now = 0.0
        n_pts = rng.randint(1, 95)
        color = rng.choice(["000000", "FF00AA", "FFFFFF", "123456"])
        width = rng.randint(1, 20)

        b.start_stroke(rng.randint(-50, 1050), rng.randint(-50, 1050), color=color, width=width, now_ms=now)
        for _ in range(n_pts - 1):
            now += rng.choice([2.0, 5.0, 15.0, 35.0])
            if rng.random() < 0.2:
                b.tick(now)
            b.add_point(rng.randint(-50, 1050), rng.randint(-50, 1050), now_ms=now)
        b.end_stroke(now_ms=now + 1.0)

        assert len(drag_batches) >= 1
        prev_last = None
        for cmd in drag_batches:
            pts = _extract_points(cmd)
            assert 2 <= len(pts) <= 40
            for x, y in pts:
                assert 0 <= x <= 1000 and 0 <= y <= 1000
            if prev_last is not None:
                assert pts[0] == prev_last, "Adjacent batches in a continuous drag must share the boundary point"
            prev_last = pts[-1]
        all_emitted.extend(drag_batches)

    assert len(all_emitted) >= 1000


def test_parse_stroke_event_and_room_model_strokes():
    assert parse_stroke_event(["FF0000", "4", "10,20,30,40"]) == StrokeItem(
        color="FF0000", width=4, points=[(10, 20), (30, 40)]
    )
    # Invalid inputs safely return None without raising
    assert parse_stroke_event([]) is None
    assert parse_stroke_event(["GGGGGG", "4", "10,20,30,40"]) is None
    assert parse_stroke_event(["FF0000", "0", "10,20,30,40"]) is None
    assert parse_stroke_event(["FF0000", "21", "10,20,30,40"]) is None
    assert parse_stroke_event(["FF0000", "4", "10,20,30"]) is None
    assert parse_stroke_event(["FF0000", "4", "10,20"]) is None
    assert parse_stroke_event(["FF0000", "4", "10,20,1001,40"]) is None
    assert parse_stroke_event(["FF0000", "4", "a,b,c,d"]) is None

    m = RoomModel()
    m.apply_event(Event("ROOM_INFO", ["1", "R1", "alice", "4", "2", "60", "PLAYING", "alice,bob"]))
    m.apply_event(Event("DRAW_START", ["alice", "60", "___"]))
    assert m.apply_event(Event("STROKE", ["00ff00", "6", "0,0,500,500,1000,1000"])) is True
    assert len(m.strokes) == 1
    assert m.strokes[0].color == "00FF00"
    assert m.apply_event(Event("STROKE", ["bad", "6", "0,0"])) is False
    assert len(m.strokes) == 1
    assert m.apply_event(Event("CLEAR", [])) is True
    assert len(m.strokes) == 0
