"""Gameplay over real TCP: full game, drawer disconnect, and dropping below two players.

Points depend on the real clock, so scores are checked through invariants rather than
exact numbers. Event order is only guaranteed per connection, so each client checks
its own stream; absence of an event is proven by a marker (PING or a later message),
never by sleeping.
"""
import socket
import struct

import pytest

from conftest import login, make_room

GAME_ARGS = ["--idle-timeout", "30"]


def parse_scores(field: str) -> dict:
    out = {}
    for item in field.split(","):
        user, total, delta = item.split(":")
        out[user] = (int(total), int(delta))
    return out


def start_turn(clients, names, drawer, round_no, auto_pick=False):
    """Consume TURN_START/WORD_OPTIONS/DRAW_START and return the chosen word."""
    for c in clients:
        assert c.next_line() == f"TURN_START|{round_no}|{names[drawer]}|2"
    d = clients[drawer]
    opts = d.next_line()
    assert opts.startswith("WORD_OPTIONS|")
    options = opts.split("|")[1:]
    assert len(options) == 3 and len(set(options)) == 3

    if not auto_pick:
        d.send("CHOOSE_WORD|1")
        assert d.next_line() == "OK"
    head = d.next_line(timeout=5.0)
    _, who, draw_time, word = head.split("|")
    assert (who, draw_time) == (names[drawer], "60")
    assert word in options
    if not auto_pick:
        assert word == options[0]

    hint = "".join(" " if ch == " " else "_" for ch in word)
    for i, c in enumerate(clients):
        if i != drawer:
            assert c.next_line(timeout=5.0) == f"DRAW_START|{names[drawer]}|60|{hint}"
    return word


def guess_right(clients, names, idx, word) -> int:
    g = clients[idx]
    g.send(f"GUESS|{word}")
    ok = g.next_line()
    assert ok.startswith("OK|CORRECT|")
    pts = int(ok.split("|")[2])
    assert 50 <= pts <= 500
    for c in clients:
        assert c.next_line() == f"GUESS_CORRECT|{names[idx]}|{pts}"
    return pts


def finish_turn_all_guessed(clients, names, drawer, word, totals):
    """Everyone else guesses; verify TURN_END scores against the running totals."""
    guessed = {}
    for i in range(len(clients)):
        if i != drawer:
            guessed[names[i]] = guess_right(clients, names, i, word)

    pts = list(guessed.values())
    assert all(a > b for a, b in zip(pts, pts[1:])), "later guessers must earn less"

    lines = {c.next_line() for c in clients}
    assert len(lines) == 1, "every player sees the same TURN_END"
    _, reason, w, scores = lines.pop().split("|")
    assert (reason, w) == ("ALL_GUESSED", word)

    parsed = parse_scores(scores)
    assert list(parsed) == names, "scores are listed in join order"
    expected_drawer = sum(pts) // (len(names) - 1)
    for name, (total, delta) in parsed.items():
        want = expected_drawer if name == names[drawer] else guessed[name]
        assert delta == want
        totals[name] = totals.get(name, 0) + delta
        assert total == totals[name]


def test_it01_first_turn_strokes_guesses_and_chat(server_factory):
    srv = server_factory(extra_args=GAME_ARGS)
    names = ["alice", "bob", "carol"]
    clients, _ = make_room(srv, names)
    alice, bob, carol = clients

    alice.send("START")
    assert alice.next_line() == "OK"
    for c in clients:
        assert c.next_line() == "GAME_START|2|alice,bob,carol"
    word = start_turn(clients, names, 0, 1)

    # Strokes reach guessers only; the drawer gets no echo and no OK
    stroke = "STROKE|FF0000|4|100,100,200,150,300,200"
    alice.send(stroke)
    alice.send("PING")
    assert alice.next_line() == "PONG"
    assert bob.next_line() == stroke
    assert carol.next_line() == stroke
    alice.send("CLEAR")
    alice.send("PING")
    assert alice.next_line() == "PONG"
    assert bob.next_line() == "CLEAR"
    assert carol.next_line() == "CLEAR"

    bob.send("GUESS|definitelywrong")
    assert bob.next_line() == "OK|WRONG"
    for c in clients:
        assert c.next_line() == "GUESS_WRONG|bob|definitelywrong"

    # Leaking the answer is refused and nobody else sees it
    carol.send(f"CHAT|is it {word.upper()}?")
    assert carol.next_line() == "ERR|306|chat blocked"

    guess_right(clients, names, 1, word)

    # bob already solved: only the drawer and solvers hear him
    bob.send(f"CHAT|{word} was easy")
    assert bob.next_line() == "OK"
    assert bob.next_line() == f"CHAT|bob|{word} was easy"
    assert alice.next_line() == f"CHAT|bob|{word} was easy"
    carol.send("CHAT|hello all")
    assert carol.next_line() == "OK"
    assert carol.next_line() == "CHAT|carol|hello all", "carol must not receive bob's private chat"
    assert alice.next_line() == "CHAT|carol|hello all"
    assert bob.next_line() == "CHAT|carol|hello all"

    alice.send("CHAT|hi")
    assert alice.next_line().startswith("ERR|302|")


def test_it01_full_game(server_factory):
    srv = server_factory(extra_args=GAME_ARGS)
    names = ["alice", "bob", "carol"]
    clients, room_id = make_room(srv, names)

    clients[0].send("START")
    assert clients[0].next_line() == "OK"
    for c in clients:
        assert c.next_line() == "GAME_START|2|alice,bob,carol"

    totals = {}
    turn = 0
    for round_no in (1, 2):
        for drawer in range(3):
            # One turn lets the choose timer expire so the server picks the word
            word = start_turn(clients, names, drawer, round_no, auto_pick=(turn == 2))
            finish_turn_all_guessed(clients, names, drawer, word, totals)
            turn += 1

    end = {c.next_line(timeout=5.0) for c in clients}
    assert len(end) == 1
    _, reason, ranking = end.pop().split("|")
    assert reason == "FINISHED"
    pairs = [(u, int(s)) for u, s in (item.split(":") for item in ranking.split(","))]
    assert dict(pairs) == totals
    assert pairs == sorted(pairs, key=lambda p: (-p[1], p[0]))

    for c in clients:
        assert c.next_line() == f"ROOM_INFO|{room_id}|Phong Vui|alice|4|2|60|LOBBY|alice,bob,carol"
    clients[1].send("SCOREBOARD")
    assert clients[1].next_line() == "OK|alice:0,bob:0,carol:0"


def hard_close(c):
    """Close with RST instead of FIN so the server sees ECONNRESET."""
    c.sock.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    c.close()


@pytest.mark.parametrize("drawer_is_host", [True, False])
@pytest.mark.parametrize("rst", [False, True])
def test_it03_drawer_disconnects(server_factory, drawer_is_host, rst):
    srv = server_factory(extra_args=GAME_ARGS)
    names = ["alice", "bob", "carol"]
    clients, _ = make_room(srv, names)
    clients[0].send("START")
    assert clients[0].next_line() == "OK"
    for c in clients:
        assert c.next_line().startswith("GAME_START|")

    totals = {}
    drawer = 0
    word = start_turn(clients, names, 0, 1)
    if not drawer_is_host:
        finish_turn_all_guessed(clients, names, 0, word, totals)
        drawer = 1
        word = start_turn(clients, names, 1, 1)

    # One guesser scores before the drawer vanishes; that score must survive
    first = 2 if drawer == 1 else 1
    pts = guess_right(clients, names, first, word)

    (hard_close if rst else lambda c: c.close())(clients[drawer])
    rest = [i for i in range(3) if i != drawer]
    for i in rest:
        c = clients[i]
        assert c.next_line() == f"PLAYER_LEFT|{names[drawer]}|DISCONNECT"
        if drawer_is_host:
            assert c.next_line() == "HOST_CHANGED|bob"
        line = c.next_line()
        _, reason, w, scores = line.split("|")
        assert (reason, w) == ("DRAWER_LEFT", word)
        parsed = parse_scores(scores)
        assert names[drawer] not in parsed
        assert parsed[names[first]] == (totals.get(names[first], 0) + pts, pts)

    # The game goes on with the next drawer after the pause
    next_drawer = "bob" if drawer_is_host else "carol"
    for i in rest:
        assert clients[i].next_line(timeout=5.0) == f"TURN_START|1|{next_drawer}|2"


@pytest.mark.parametrize("leaver,how", [("bob", "leave"), ("alice", "close")])
def test_it04_not_enough_players(server_factory, leaver, how):
    srv = server_factory(extra_args=GAME_ARGS)
    names = ["alice", "bob"]
    clients, room_id = make_room(srv, names)
    clients[0].send("START")
    assert clients[0].next_line() == "OK"
    for c in clients:
        assert c.next_line().startswith("GAME_START|")

    # Play one full turn so the survivor has a non-zero score to reset
    totals = {}
    word = start_turn(clients, names, 0, 1)
    finish_turn_all_guessed(clients, names, 0, word, totals)
    start_turn(clients, names, 1, 1)

    li = names.index(leaver)
    si = 1 - li
    survivor, sname = clients[si], names[si]
    if how == "leave":
        clients[li].send("LEAVE_ROOM")
        assert clients[li].next_line() == "OK"
        reason = "LEAVE"
    else:
        clients[li].close()
        reason = "DISCONNECT"

    assert survivor.next_line() == f"PLAYER_LEFT|{leaver}|{reason}"
    if li == 0:
        assert survivor.next_line() == f"HOST_CHANGED|{sname}"
    assert survivor.next_line() == f"GAME_END|NOT_ENOUGH_PLAYERS|{sname}:{totals[sname]}"
    assert survivor.next_line().startswith(f"ROOM_INFO|{room_id}|Phong Vui|{sname}|")

    survivor.send("SCOREBOARD")
    assert survivor.next_line() == f"OK|{sname}:0"
    survivor.send("START")
    assert survivor.next_line().startswith("ERR|207|")

    dave = login(srv, "dave")
    dave.send(f"JOIN_ROOM|{room_id}")
    assert dave.next_line() == f"OK|{room_id}"
    assert dave.next_line().startswith(f"ROOM_INFO|{room_id}|")
    assert survivor.next_line() == "PLAYER_JOINED|dave"
    survivor.send("START")
    assert survivor.next_line() == "OK"
    assert survivor.next_line() == f"GAME_START|2|{sname},dave"
