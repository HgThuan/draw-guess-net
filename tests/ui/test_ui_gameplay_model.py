"""Fast, server-free unit tests for RoomModel scoring, monotonic clock, hints, chat channels, and final ranking."""
from __future__ import annotations

import sys
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parents[2]
UI_DIR = ROOT_DIR / "client" / "ui"
if str(UI_DIR) not in sys.path:
    sys.path.insert(0, str(UI_DIR))

from app import SILENT_ASYNC_CODES  # noqa: E402
from netsketch import (  # noqa: E402
    Event,
    RoomModel,
    format_error_vi,
    format_word_hint,
    parse_ranking_list,
    parse_turn_end_scores,
)


def test_word_hint_formatting_single_and_multiword():
    # Guesser view: single space between letters, 3 spaces between words, lengths in parens
    assert format_word_hint("___", is_drawer=False) == "_ _ _ (3)"
    assert format_word_hint("___ _____", is_drawer=False) == "_ _ _   _ _ _ _ _ (3, 5)"
    # Drawer view: secret word shown verbatim
    assert format_word_hint("ice cream", is_drawer=True) == "ice cream"
    assert format_word_hint("cat", is_drawer=False) == "cat"
    assert format_word_hint("", is_drawer=False) == ""


def test_monotonic_clock_sync_and_no_auto_end_at_zero():
    now = [100.0]
    m = RoomModel(clock_fn=lambda: now[0])
    m.apply_event(Event("ROOM_INFO", ["1", "R1", "alice", "4", "2", "60", "PLAYING", "alice,bob"]))
    m.apply_event(Event("DRAW_START", ["alice", "60", "___"]))
    assert m.current_remaining_s() == 60

    # Advance 4.2s -> ceil(60 - 4.2) = 56s; then receive TIME|55 -> resyncs deadline to now + 55
    now[0] += 4.2
    assert m.current_remaining_s() == 56
    m.apply_event(Event("TIME", ["55"]))
    assert m.current_remaining_s() == 55

    # Advance another 2.1s -> ceil(55 - 2.1) = 53s
    now[0] += 2.1
    assert m.current_remaining_s() == 53

    # Advance past deadline -> displays 0 and waits for TURN_END (does NOT change state on its own)
    now[0] += 100.0
    assert m.current_remaining_s() == 0
    assert m.state == "DRAWING"


def test_room_model_scores_no_double_add_turn_end_override_and_ranking_persistence():
    m = RoomModel()
    m.apply_event(Event("ROOM_INFO", ["1", "Phong Vui", "alice", "3", "2", "60", "LOBBY", "alice,bob,carol"]))
    assert m.scores == {"alice": 0, "bob": 0, "carol": 0}

    m.apply_event(Event("GAME_START", ["2", "alice,bob,carol"]))
    m.apply_event(Event("TURN_START", ["1", "alice", "15"]))
    m.apply_event(Event("WORD_OPTIONS", ["cat", "house", "tree"]))
    assert m.word_options == ["cat", "house", "tree"]

    m.apply_event(Event("DRAW_START", ["alice", "60", "___"]))
    assert m.word_options == []

    # 1. Provisional score from GUESS_CORRECT is added only once, and chat message is added once
    m.apply_event(Event("GUESS_CORRECT", ["bob", "400"]))
    m.apply_event(Event("GUESS_CORRECT", ["bob", "400"]))  # duplicate guard
    assert m.scores["bob"] == 400
    assert "bob" in m.guessed_users

    # Solved player's chat during DRAWING is tagged as "solved_chat", unguessed player's as "chat"
    m.apply_event(Event("CHAT", ["bob", "easy word"]))
    m.apply_event(Event("CHAT", ["carol", "still thinking"]))
    chat_items = [f for f in m.feed if f.kind in ("chat", "solved_chat")]
    assert len(chat_items) == 2
    assert chat_items[0].kind == "solved_chat" and "[Kênh đã đoán]" in chat_items[0].text
    assert chat_items[1].kind == "chat" and "[Chat]" in chat_items[1].text

    # 2. TURN_END overwrites provisional scores with authoritative server totals (no double counting!)
    m.apply_event(Event("TURN_END", ["ALL_GUESSED", "cat", "alice:337:337,bob:400:400,carol:275:275"]))
    assert m.scores == {"alice": 337, "bob": 400, "carol": 275}
    assert m.turn_deltas == {"alice": 337, "bob": 400, "carol": 275}

    # 3. SCOREBOARD response overwrites scores authoritatively
    m.apply_scoreboard(["carol:500,bob:400,alice:337"])
    assert m.scores["carol"] == 500

    # 4. Player leaving removes them from players and scores
    m.apply_event(Event("PLAYER_LEFT", ["carol", "LEAVE"]))
    assert "carol" not in m.players and "carol" not in m.scores

    # 5. GAME_END sets final_ranking, and immediate ROOM_INFO|...|LOBBY resets scores to 0
    #    WITHOUT wiping final_ranking or game_end_reason!
    m.apply_event(Event("GAME_END", ["FINISHED", "bob:900,alice:750"]))
    assert m.game_end_reason == "FINISHED"
    assert m.final_ranking == [("bob", 900), ("alice", 750)]

    m.apply_event(Event("ROOM_INFO", ["1", "Phong Vui", "alice", "3", "2", "60", "LOBBY", "alice,bob"]))
    assert m.state == "LOBBY"
    assert m.scores == {"alice": 0, "bob": 0}
    assert m.final_ranking == [("bob", 900), ("alice", 750)]
    assert m.game_end_reason == "FINISHED"

    # Dismissing or starting a new game clears final_ranking
    m.dismiss_final_ranking()
    assert m.final_ranking == [] and m.game_end_reason == ""

    # 6. NOT_ENOUGH_PLAYERS reason is preserved across ROOM_INFO
    m.apply_event(Event("GAME_START", ["2", "alice,bob"]))
    m.apply_event(Event("PLAYER_LEFT", ["bob", "DISCONNECT"]))
    m.apply_event(Event("GAME_END", ["NOT_ENOUGH_PLAYERS", "alice:250"]))
    m.apply_event(Event("ROOM_INFO", ["1", "Phong Vui", "alice", "3", "2", "60", "LOBBY", "alice"]))
    assert m.game_end_reason == "NOT_ENOUGH_PLAYERS"
    assert m.final_ranking == [("alice", 250)]
    assert m.scores == {"alice": 0}


def test_async_error_policy_and_parsers():
    # Late race codes 300, 301, 302, 303, 305 are silent; 306 is NOT silent
    for code in (300, 301, 302, 303, 305):
        assert code in SILENT_ASYNC_CODES
    assert 306 not in SILENT_ASYNC_CODES
    assert "[306]" in format_error_vi(306)

    assert parse_turn_end_scores("alice:337:337,bob:275:275") == [
        ("alice", 337, 337),
        ("bob", 275, 275),
    ]
    assert parse_ranking_list("carol:1215,alice:1002") == [
        ("carol", 1215),
        ("alice", 1002),
    ]


def test_scoreboard_banner_and_drawer_chat():
    m = RoomModel(clock_fn=lambda: 100.0)
    m.apply_event(Event("ROOM_INFO", ["1", "Phong Vui", "alice", "3", "2", "60", "DRAWING", "alice,bob"]))
    m.drawer = "alice"

    m.apply_scoreboard(["bob:250,alice:100"])
    assert m.show_scoreboard is True
    assert m.scoreboard_ranking == [("bob", 250), ("alice", 100)]
    assert m.scores["bob"] == 250
    assert m.scores["alice"] == 100

    m.dismiss_final_ranking()
    assert m.show_scoreboard is False
    assert m.scoreboard_ranking == []

    m.apply_event(Event("CHAT", ["alice", "chao moi nguoi"]))
    assert m.feed[-1].kind == "chat"
    assert "alice: chao moi nguoi" in m.feed[-1].text
