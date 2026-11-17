#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>

#include "dispatch.h"
#include "draw.h"
#include "game.h"

static Server s_srv;

static void clear_out(Conn *c)
{
    conn_consume_out(c, c->out_len);
    conn_clear_dirty(&s_srv.conns);
}

static Conn *make_user(const char *name)
{
    Conn *c = conn_alloc(&s_srv.conns, -1, clock_now_ms(&s_srv.clock));
    assert_non_null(c);
    char cmd[64];
    (void)snprintf(cmd, sizeof(cmd), "REGISTER|%s|Secret123", name);
    dispatch_line(&s_srv, c, cmd);
    clear_out(c);
    (void)snprintf(cmd, sizeof(cmd), "LOGIN|%s|Secret123", name);
    dispatch_line(&s_srv, c, cmd);
    clear_out(c);
    return c;
}

static void init_test_server(void)
{
    server_init(&s_srv, NULL);
    clock_init_fake(&s_srv.clock, 1000);
    rng_init_fake(&s_srv.rng, 100);
    user_store_init(&s_srv.users, NULL);
    (void)words_load(&s_srv.words, "tests/data/words_small.txt");
}

static void test_game_start_8_players_and_no_side_effects_on_900(void **state)
{
    Conn *p[8];
    const char *names[8] = {
        "player_01", "player_02", "player_03", "player_04",
        "player_05", "player_06", "player_07", "player_08"
    };
    (void)state;

    init_test_server();
    s_srv.cfg.choose_time_s = 12;

    for (int i = 0; i < 8; i++) {
        p[i] = make_user(names[i]);
    }

    dispatch_line(&s_srv, p[0], "CREATE_ROOM|BigRoom|8|3|60");
    clear_out(p[0]);

    /* 1 player -> 207 */
    dispatch_line(&s_srv, p[0], "START");
    assert_non_null(strstr(p[0]->out, "ERR|207|"));
    clear_out(p[0]);

    for (int i = 1; i < 8; i++) {
        dispatch_line(&s_srv, p[i], "JOIN_ROOM|1");
        for (int j = 0; j <= i; j++) {
            clear_out(p[j]);
        }
    }

    /* Non-host -> 206 */
    dispatch_line(&s_srv, p[1], "START");
    assert_non_null(strstr(p[1]->out, "ERR|206|"));
    clear_out(p[1]);

    /* Words < 3 -> 900 with ZERO side effects (room stays in LOBBY, no OK/GAME_START) */
    s_srv.words.count = 2;
    dispatch_line(&s_srv, p[0], "START");
    assert_string_equal(p[0]->out, "ERR|900|internal error\n");
    assert_int_equal(p[1]->out_len, 0);
    assert_int_equal(s_srv.rooms[0].state, ROOM_LOBBY);
    clear_out(p[0]);

    (void)words_load(&s_srv.words, "tests/data/words_small.txt");

    /* Valid START with 8 players */
    dispatch_line(&s_srv, p[0], "START");
    Room *r = &s_srv.rooms[0];
    assert_int_equal(r->state, ROOM_CHOOSING);

    /* Verify exact order on host: OK, GAME_START, TURN_START, WORD_OPTIONS */
    char *ok_pos = strstr(p[0]->out, "OK\n");
    char *gs_pos = strstr(p[0]->out, "GAME_START|3|player_01,player_02,player_03,player_04,player_05,player_06,player_07,player_08\n");
    char *ts_pos = strstr(p[0]->out, "TURN_START|1|player_01|12\n");
    char *wo_pos = strstr(p[0]->out, "WORD_OPTIONS|");
    assert_non_null(ok_pos);
    assert_non_null(gs_pos);
    assert_non_null(ts_pos);
    assert_non_null(wo_pos);
    assert_true(ok_pos < gs_pos && gs_pos < ts_pos && ts_pos < wo_pos);

    /* Verify 3 picked options are distinct and in word list */
    assert_string_not_equal(r->options[0], r->options[1]);
    assert_string_not_equal(r->options[0], r->options[2]);
    assert_string_not_equal(r->options[1], r->options[2]);

    /* Guessers receive GAME_START and TURN_START, but NOT WORD_OPTIONS */
    for (int i = 1; i < 8; i++) {
        assert_non_null(strstr(p[i]->out, "GAME_START|3|"));
        assert_non_null(strstr(p[i]->out, "TURN_START|1|player_01|12\n"));
        assert_null(strstr(p[i]->out, "WORD_OPTIONS|"));
        clear_out(p[i]);
    }
    clear_out(p[0]);
}

static void test_game_choose_word_priority_and_draw_start(void **state)
{
    Conn *c1;
    Conn *c2;
    Conn *unauth;
    (void)state;

    init_test_server();
    unauth = conn_alloc(&s_srv.conns, -1, 1000);
    c1 = make_user("alice");
    c2 = make_user("bob");

    /* Step 1: CHOOSE_WORD|abc returns 100 in every state */
    dispatch_line(&s_srv, unauth, "CHOOSE_WORD|abc");
    assert_non_null(strstr(unauth->out, "ERR|100|"));
    clear_out(unauth);

    dispatch_line(&s_srv, c1, "CHOOSE_WORD|abc");
    assert_non_null(strstr(c1->out, "ERR|100|"));
    clear_out(c1);

    dispatch_line(&s_srv, c1, "CREATE_ROOM|R1|4|2|60");
    dispatch_line(&s_srv, c2, "JOIN_ROOM|1");
    clear_out(c1);
    clear_out(c2);

    /* In LOBBY: CHOOSE_WORD|abc -> 100, CHOOSE_WORD|1 -> 300 */
    dispatch_line(&s_srv, c1, "CHOOSE_WORD|abc");
    assert_non_null(strstr(c1->out, "ERR|100|"));
    clear_out(c1);

    dispatch_line(&s_srv, c1, "CHOOSE_WORD|1");
    assert_non_null(strstr(c1->out, "ERR|300|"));
    clear_out(c1);

    dispatch_line(&s_srv, c1, "START");
    clear_out(c1);
    clear_out(c2);

    /* Guesser sending CHOOSE_WORD|abc -> 100; CHOOSE_WORD|9 -> 301 (before 304) */
    dispatch_line(&s_srv, c2, "CHOOSE_WORD|abc");
    assert_non_null(strstr(c2->out, "ERR|100|"));
    clear_out(c2);

    dispatch_line(&s_srv, c2, "CHOOSE_WORD|9");
    assert_non_null(strstr(c2->out, "ERR|301|"));
    clear_out(c2);

    /* Drawer sending invalid index 0 or 4 -> 304 */
    dispatch_line(&s_srv, c1, "CHOOSE_WORD|0");
    assert_non_null(strstr(c1->out, "ERR|304|"));
    clear_out(c1);

    dispatch_line(&s_srv, c1, "CHOOSE_WORD|4");
    assert_non_null(strstr(c1->out, "ERR|304|"));
    clear_out(c1);

    /* Force option 2 to "ice cream" to verify hint with spaces */
    Room *r = &s_srv.rooms[0];
    (void)snprintf(r->options[1], sizeof(r->options[1]), "ice cream");

    dispatch_line(&s_srv, c1, "CHOOSE_WORD|2");
    assert_string_equal(c1->out, "OK\nDRAW_START|alice|60|ice cream\n");
    assert_string_equal(c2->out, "DRAW_START|alice|60|___ _____\n");
    assert_int_equal(r->state, ROOM_DRAWING);
    assert_int_equal(r->phase_deadline_ms, 1000 + 60000);
    clear_out(c1);
    clear_out(c2);

    /* Calling CHOOSE_WORD again during DRAWING -> 300 */
    dispatch_line(&s_srv, c1, "CHOOSE_WORD|1");
    assert_non_null(strstr(c1->out, "ERR|300|"));
    clear_out(c1);
}

static void test_game_auto_pick_timeout_and_all_indices(void **state)
{
    Conn *c1;
    Conn *c2;
    bool seen_idx[3] = { false, false, false };
    (void)state;

    /* Verify boundary at 14999 ms vs 15000 ms and cover all 3 random indices */
    for (uint64_t seed = 1; seed <= 30; seed++) {
        init_test_server();
        c1 = make_user("alice");
        c2 = make_user("bob");

        dispatch_line(&s_srv, c1, "CREATE_ROOM|R|4|2|60");
        dispatch_line(&s_srv, c2, "JOIN_ROOM|1");
        dispatch_line(&s_srv, c1, "START");
        clear_out(c1);
        clear_out(c2);

        Room *r = &s_srv.rooms[0];
        (void)snprintf(r->options[0], sizeof(r->options[0]), "opt_zero");
        (void)snprintf(r->options[1], sizeof(r->options[1]), "opt_one");
        (void)snprintf(r->options[2], sizeof(r->options[2]), "opt_two");

        rng_init_fake(&s_srv.rng, seed);

        /* At 14999 ms elapsed: still ROOM_CHOOSING */
        clock_advance(&s_srv.clock, 14999);
        server_tick(&s_srv, clock_now_ms(&s_srv.clock));
        assert_int_equal(r->state, ROOM_CHOOSING);
        assert_int_equal(c1->out_len, 0);

        /* At 15000 ms elapsed: auto-picks without sending OK */
        clock_advance(&s_srv.clock, 1);
        server_tick(&s_srv, clock_now_ms(&s_srv.clock));
        assert_int_equal(r->state, ROOM_DRAWING);
        assert_null(strstr(c1->out, "OK\n"));
        assert_true(strncmp(c1->out, "DRAW_START|alice|60|", 20) == 0);
        assert_true(strncmp(c2->out, "DRAW_START|alice|60|", 20) == 0);

        if (strcmp(r->word, "opt_zero") == 0) {
            seen_idx[0] = true;
        } else if (strcmp(r->word, "opt_one") == 0) {
            seen_idx[1] = true;
        } else if (strcmp(r->word, "opt_two") == 0) {
            seen_idx[2] = true;
        }
        clear_out(c1);
        clear_out(c2);

        /* Late CHOOSE_WORD after auto-pick returns 300 */
        dispatch_line(&s_srv, c1, "CHOOSE_WORD|1");
        assert_non_null(strstr(c1->out, "ERR|300|"));
        clear_out(c1);
    }

    assert_true(seen_idx[0] && seen_idx[1] && seen_idx[2]);
}

static void test_game_time_ticks_schedule_and_isolation(void **state)
{
    Conn *a1;
    Conn *a2;
    Conn *b1;
    Conn *b2;
    (void)state;

    init_test_server();
    s_srv.cfg.idle_timeout_s = 1000;
    a1 = make_user("alice");
    a2 = make_user("bob");
    b1 = make_user("carol");
    b2 = make_user("dave");

    /* Room 1: 60s draw_time; Room 2: 33s draw_time */
    dispatch_line(&s_srv, a1, "CREATE_ROOM|R60|4|2|60");
    dispatch_line(&s_srv, a2, "JOIN_ROOM|1");
    dispatch_line(&s_srv, a1, "START");
    dispatch_line(&s_srv, a1, "CHOOSE_WORD|1");
    clear_out(a1);
    clear_out(a2);

    dispatch_line(&s_srv, b1, "CREATE_ROOM|R33|4|2|33");
    dispatch_line(&s_srv, b2, "JOIN_ROOM|2");
    dispatch_line(&s_srv, b1, "START");
    dispatch_line(&s_srv, b1, "CHOOSE_WORD|1");
    clear_out(b1);
    clear_out(b2);

    /* Room 1: 60s in 5s steps -> exactly 11 TIME events (55 down to 5), no TIME|0 */
    int count_r1 = 0;
    int count_r2 = 0;
    for (int step = 1; step <= 12; step++) {
        clock_advance(&s_srv.clock, 5000);
        int64_t now = clock_now_ms(&s_srv.clock);
        /* Calling server_tick twice at the same timestamp must not duplicate TIME */
        server_tick(&s_srv, now);
        server_tick(&s_srv, now);

        if (a1->out_len > 0) {
            if (strncmp(a1->out, "TIME|", 5) == 0) {
                char exp[32];
                (void)snprintf(exp, sizeof(exp), "TIME|%d\n", 60 - step * 5);
                assert_string_equal(a1->out, exp);
                assert_string_equal(a2->out, exp);
                count_r1++;
            } else {
                assert_int_equal(step, 12);
                assert_non_null(strstr(a1->out, "TURN_END|TIMEUP|"));
            }
            clear_out(a1);
            clear_out(a2);
        }
        if (b1->out_len > 0) {
            if (step < 7) {
                char exp[32];
                (void)snprintf(exp, sizeof(exp), "TIME|%d\n", 33 - step * 5);
                assert_string_equal(b1->out, exp);
                count_r2++;
            } else if (step == 7) {
                assert_non_null(strstr(b1->out, "TURN_END|TIMEUP|"));
            }
            clear_out(b1);
            clear_out(b2);
        }
    }
    assert_int_equal(count_r1, 11);
    assert_int_equal(count_r2, 6);

    /* Jump 20s in one step: emits only ONE TIME|40 event (skips 55, 50, 45) */
    s_srv.rooms[0].state = ROOM_CHOOSING;
    game_begin_drawing(&s_srv, &s_srv.rooms[0], 0, clock_now_ms(&s_srv.clock));
    clear_out(a1);
    clear_out(a2);

    clock_advance(&s_srv.clock, 20100);
    server_tick(&s_srv, clock_now_ms(&s_srv.clock));
    assert_string_equal(a1->out, "TIME|40\n");
    clear_out(a1);
    clear_out(a2);
}

static void test_game_drawer_leaves_during_choosing(void **state)
{
    Conn *c1;
    Conn *c2;
    Conn *c3;
    (void)state;

    init_test_server();
    c1 = make_user("alice");
    c2 = make_user("bob");
    c3 = make_user("carol");

    dispatch_line(&s_srv, c1, "CREATE_ROOM|R|4|2|60");
    dispatch_line(&s_srv, c2, "JOIN_ROOM|1");
    dispatch_line(&s_srv, c3, "JOIN_ROOM|1");
    dispatch_line(&s_srv, c1, "START");
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    /* Drawer (alice) leaves during ROOM_CHOOSING -> enters ROOM_TURN_END, then after 5s bob becomes drawer */
    dispatch_line(&s_srv, c1, "LEAVE_ROOM");
    assert_int_equal(s_srv.rooms[0].state, ROOM_TURN_END);
    clock_advance(&s_srv.clock, 5000);
    server_tick(&s_srv, clock_now_ms(&s_srv.clock));
    assert_int_equal(s_srv.rooms[0].state, ROOM_CHOOSING);
    assert_string_equal(s_srv.rooms[0].drawer, "bob");
    clock_advance(&s_srv.clock, 15000);
    server_tick(&s_srv, clock_now_ms(&s_srv.clock));
    assert_int_equal(s_srv.rooms[0].state, ROOM_DRAWING);

    /* Second player (carol) leaves -> < 2 players returns room to ROOM_LOBBY */
    dispatch_line(&s_srv, c3, "LEAVE_ROOM");
    assert_int_equal(s_srv.rooms[0].state, ROOM_LOBBY);

    game_tick(NULL, 0);
    game_begin_drawing(NULL, NULL, 0, 0);
    game_on_player_left(NULL, NULL, NULL, false);
}

static void test_game_stroke_and_clear_validation_and_forwarding(void **state)
{
    Conn *a1;
    Conn *a2;
    Conn *a3;
    Conn *b1;
    Conn *b2;
    Conn *unauth;
    (void)state;

    init_test_server();
    unauth = conn_alloc(&s_srv.conns, -1, 1000);
    a1 = make_user("alice");
    a2 = make_user("bob");
    a3 = make_user("carol");
    b1 = make_user("dave");
    b2 = make_user("erin");

    /* Room 1: alice (drawer), bob, carol; Room 2: dave (drawer), erin */
    dispatch_line(&s_srv, a1, "CREATE_ROOM|R1|4|2|60");
    dispatch_line(&s_srv, a2, "JOIN_ROOM|1");
    dispatch_line(&s_srv, a3, "JOIN_ROOM|1");
    dispatch_line(&s_srv, a1, "START");
    dispatch_line(&s_srv, a1, "CHOOSE_WORD|1");
    clear_out(a1);
    clear_out(a2);
    clear_out(a3);

    dispatch_line(&s_srv, b1, "CREATE_ROOM|R2|4|2|60");
    dispatch_line(&s_srv, b2, "JOIN_ROOM|2");
    dispatch_line(&s_srv, b1, "START");
    dispatch_line(&s_srv, b1, "CHOOSE_WORD|1");
    clear_out(b1);
    clear_out(b2);

    /* Step 1: Invalid width number syntax (-1, +5, 99999999999, abc) -> 100 in all roles */
    const char *bad_widths[] = {
        "STROKE|FF0000|-1|0,0,10,10",
        "STROKE|FF0000|+5|0,0,10,10",
        "STROKE|FF0000|99999999999|0,0,10,10",
        "STROKE|GG|abc|0,0,10,10"
    };
    for (size_t i = 0; i < 4; i++) {
        dispatch_line(&s_srv, unauth, bad_widths[i]);
        assert_non_null(strstr(unauth->out, "ERR|100|"));
        clear_out(unauth);

        dispatch_line(&s_srv, a2, bad_widths[i]);
        assert_non_null(strstr(a2->out, "ERR|100|"));
        clear_out(a2);

        dispatch_line(&s_srv, a1, bad_widths[i]);
        assert_non_null(strstr(a1->out, "ERR|100|"));
        clear_out(a1);
    }

    /* Step 5 before Step 6: Guesser sending STROKE|GG|5|... gets 301 (not 305), CLEAR gets 301 */
    dispatch_line(&s_srv, a2, "STROKE|GG|5|0,0,10,10");
    assert_non_null(strstr(a2->out, "ERR|301|"));
    clear_out(a2);

    dispatch_line(&s_srv, a2, "CLEAR");
    assert_non_null(strstr(a2->out, "ERR|301|"));
    clear_out(a2);

    /* Step 6: Drawer sending invalid stroke parameters -> 305 */
    const char *bad_strokes[] = {
        "STROKE|FFF|4|0,0,10,10",
        "STROKE|1234567|4|0,0,10,10",
        "STROKE|GG0000|4|0,0,10,10",
        "STROKE|FF0000|0|0,0,10,10",
        "STROKE|FF0000|21|0,0,10,10",
        "STROKE|FF0000|4|10,20",
        "STROKE|FF0000|4|10,20,30,40,50",
        "STROKE|FF0000|4|0,-5,10,10",
        "STROKE|FF0000|4|0,+5,10,10",
        "STROKE|FF0000|4|0,1a,10,10",
        "STROKE|FF0000|4|0, 5,10,10",
        "STROKE|FF0000|4|0,1001,10,10",
        "STROKE|FF0000|4|0,999999999999999999,10,10",
        "STROKE|FF0000|4|,0,0,10,10",
        "STROKE|FF0000|4|0,0,,10,10",
        "STROKE|FF0000|4|0,0,10,10,"
    };
    for (size_t i = 0; i < sizeof(bad_strokes) / sizeof(bad_strokes[0]); i++) {
        dispatch_line(&s_srv, a1, bad_strokes[i]);
        assert_non_null(strstr(a1->out, "ERR|305|"));
        assert_int_equal(a2->out_len, 0);
        clear_out(a1);
    }

    /* 41 points (82 numbers) -> 305; 40 points (80 numbers) -> valid */
    char pts82[400];
    size_t pos = 0;
    for (int i = 0; i < 41; i++) {
        pos += (size_t)snprintf(pts82 + pos, sizeof(pts82) - pos, "%s%d,%d",
                                (i > 0) ? "," : "", i, i);
    }
    assert_int_equal(stroke_validate("ffffff", 20, pts82), ERR_INVALID_STROKE);
    assert_int_equal(stroke_validate(NULL, 4, "0,0,1,1"), ERR_INVALID_STROKE);
    assert_int_equal(stroke_validate("FF0000", 4, ""), ERR_INVALID_STROKE);

    char pts80[400];
    pos = 0;
    for (int i = 0; i < 40; i++) {
        pos += (size_t)snprintf(pts80 + pos, sizeof(pts80) - pos, "%s%d,%d",
                                (i > 0) ? "," : "", i * 25, 1000);
    }
    assert_int_equal(stroke_validate("aAbBcC", 1, pts80), ERR_OK);

    /* Valid STROKE with leading zeros 0000000007 via CRLF framing */
    const char *raw_crlf = "STROKE|00ffAA|20|0,1000,0000000007,500\r\n";
    size_t consumed = 0;
    char framed_line[FRAME_MAX_LINE];
    assert_int_equal(linebuf_feed(&a1->in, raw_crlf, strlen(raw_crlf),
                                  &consumed, framed_line, sizeof(framed_line)),
                     FRAME_OK);
    dispatch_line(&s_srv, a1, framed_line);

    /* Drawer gets no reply; guessers in Room 1 get exact single-\n line; Room 2 gets nothing */
    assert_int_equal(a1->out_len, 0);
    assert_string_equal(a2->out, "STROKE|00ffAA|20|0,1000,0000000007,500\n");
    assert_string_equal(a3->out, "STROKE|00ffAA|20|0,1000,0000000007,500\n");
    assert_int_equal(b1->out_len, 0);
    assert_int_equal(b2->out_len, 0);
    clear_out(a2);
    clear_out(a3);

    /* Valid CLEAR */
    dispatch_line(&s_srv, a1, "CLEAR");
    assert_int_equal(a1->out_len, 0);
    assert_string_equal(a2->out, "CLEAR\n");
    assert_string_equal(a3->out, "CLEAR\n");
    assert_int_equal(b1->out_len, 0);
    assert_int_equal(b2->out_len, 0);
    clear_out(a2);
    clear_out(a3);

    /* Deterministic fuzzing of stroke_validate under ASan */
    Rng rng;
    rng_init_fake(&rng, 20261117);
    char fbuf[128];
    for (int iter = 0; iter < 300; iter++) {
        size_t flen = (size_t)rng_range(&rng, 0, 100);
        for (size_t k = 0; k < flen; k++) {
            fbuf[k] = (char)rng_range(&rng, 1, 126);
        }
        fbuf[flen] = '\0';
        (void)stroke_validate(fbuf, rng_range(&rng, -2, 25), fbuf);
    }
}

static void test_game_guess_scoring_and_deadline_bounds(void **state)
{
    Conn *a1;
    Conn *a2;
    Conn *a3;
    Conn *a4;
    Conn *b1;
    Conn *b2;
    Conn *unauth;
    (void)state;

    init_test_server();
    unauth = conn_alloc(&s_srv.conns, -1, 1000);
    a1 = make_user("alice");
    a2 = make_user("bob");
    a3 = make_user("carol");
    a4 = make_user("dave");
    b1 = make_user("erin");
    b2 = make_user("frank");

    /* Step 1: Empty/whitespace-only, tab control char, or >200 bytes -> 100 in all states */
    char cmd_201[220];
    memcpy(cmd_201, "GUESS|", 6);
    memset(cmd_201 + 6, 'x', 201);
    cmd_201[207] = '\0';

    const char *bad_guesses[] = {
        "GUESS|",
        "GUESS|     ",
        "GUESS|cat\tdog",
        cmd_201
    };
    for (size_t i = 0; i < 4; i++) {
        dispatch_line(&s_srv, unauth, bad_guesses[i]);
        assert_non_null(strstr(unauth->out, "ERR|100|"));
        clear_out(unauth);
    }

    dispatch_line(&s_srv, a1, "CREATE_ROOM|R1|4|2|60");
    dispatch_line(&s_srv, a2, "JOIN_ROOM|1");
    dispatch_line(&s_srv, a3, "JOIN_ROOM|1");
    dispatch_line(&s_srv, a4, "JOIN_ROOM|1");
    clear_out(a1);
    clear_out(a2);
    clear_out(a3);
    clear_out(a4);

    /* In LOBBY: GUESS| -> 100, GUESS|cat -> 300 */
    dispatch_line(&s_srv, a1, "GUESS|");
    assert_non_null(strstr(a1->out, "ERR|100|"));
    clear_out(a1);

    dispatch_line(&s_srv, a1, "GUESS|cat");
    assert_non_null(strstr(a1->out, "ERR|300|"));
    clear_out(a1);

    dispatch_line(&s_srv, a1, "START");
    /* Force option 1 to "cat" */
    (void)snprintf(s_srv.rooms[0].options[0], sizeof(s_srv.rooms[0].options[0]), "cat");
    dispatch_line(&s_srv, a1, "CHOOSE_WORD|1");
    clear_out(a1);
    clear_out(a2);
    clear_out(a3);
    clear_out(a4);

    /* Room 2 for 15001 ms rounding comparison */
    dispatch_line(&s_srv, b1, "CREATE_ROOM|R2|4|2|60");
    dispatch_line(&s_srv, b2, "JOIN_ROOM|2");
    dispatch_line(&s_srv, b1, "START");
    (void)snprintf(s_srv.rooms[1].options[0], sizeof(s_srv.rooms[1].options[0]), "cat");
    dispatch_line(&s_srv, b1, "CHOOSE_WORD|1");
    clear_out(b1);
    clear_out(b2);

    /* Drawer sending GUESS| -> 100 (Step 1); GUESS|cat -> 302 (Step 5) */
    dispatch_line(&s_srv, a1, "GUESS|");
    assert_non_null(strstr(a1->out, "ERR|100|"));
    clear_out(a1);

    dispatch_line(&s_srv, a1, "GUESS|cat");
    assert_non_null(strstr(a1->out, "ERR|302|"));
    clear_out(a1);

    /* Wrong guess with 200 bytes and '|' inside free-text */
    char cmd_200[220];
    memcpy(cmd_200, "GUESS|a|b|", 10);
    memset(cmd_200 + 10, 'z', 196);
    cmd_200[206] = '\0';
    dispatch_line(&s_srv, a2, cmd_200);
    assert_true(strncmp(a2->out, "OK|WRONG\nGUESS_WRONG|bob|a|b|", 29) == 0);
    assert_true(strlen(a1->out) < FRAME_MAX_LINE);
    assert_true(strncmp(a1->out, "GUESS_WRONG|bob|a|b|", 20) == 0);
    clear_out(a1);
    clear_out(a2);
    clear_out(a3);
    clear_out(a4);

    /* Advance 14999 ms (rem_ms = 45001 -> remaining_s = 45): carol (k=1) gets 400 pts */
    clock_advance(&s_srv.clock, 14999);
    dispatch_line(&s_srv, a3, "GUESS|   CaT  ");
    assert_string_equal(a3->out, "OK|CORRECT|400\nGUESS_CORRECT|carol|400\n");
    assert_string_equal(a1->out, "GUESS_CORRECT|carol|400\n");
    assert_string_equal(a2->out, "GUESS_CORRECT|carol|400\n");
    assert_int_equal(s_srv.rooms[0].players[2].score, 400);
    assert_int_equal(s_srv.rooms[0].players[2].turn_delta, 400);
    assert_int_equal(s_srv.rooms[0].turn_guess_total, 400);
    clear_out(a1);
    clear_out(a2);
    clear_out(a3);
    clear_out(a4);

    /* carol guessing again -> 303 */
    dispatch_line(&s_srv, a3, "GUESS|cat");
    assert_non_null(strstr(a3->out, "ERR|303|"));
    clear_out(a3);

    /* Advance 2 ms to 15001 ms elapsed (rem_ms = 44999 -> remaining_s = 44): frank in Room 2 gets 393 pts (and ends turn since 2 players) */
    clock_advance(&s_srv.clock, 2);
    dispatch_line(&s_srv, b2, "GUESS|cat");
    assert_true(strncmp(b2->out, "OK|CORRECT|393\nGUESS_CORRECT|frank|393\nTURN_END|ALL_GUESSED|cat|", 64) == 0);
    clear_out(b1);
    clear_out(b2);

    /* Reset Room 2 back to ROOM_DRAWING with unguessed frank so we can test 60000 ms deadline */
    s_srv.rooms[1].state = ROOM_CHOOSING;
    game_begin_drawing(&s_srv, &s_srv.rooms[1], 0, 1000);
    clear_out(b1);
    clear_out(b2);

    /* Advance to 30000 ms elapsed (remaining_s = 30): bob (k=2) in Room 1 gets 275 pts */
    clock_advance(&s_srv.clock, 14999);
    dispatch_line(&s_srv, a2, "GUESS|CAT");
    assert_string_equal(a2->out, "OK|CORRECT|275\nGUESS_CORRECT|bob|275\n");
    assert_int_equal(s_srv.rooms[0].turn_guess_total, 675);
    clear_out(a1);
    clear_out(a2);
    clear_out(a3);
    clear_out(a4);

    /* Advance to 59999 ms elapsed (rem_ms = 1 -> remaining_s = 0): STROKE/CLEAR/GUESS still accepted */
    clock_advance(&s_srv.clock, 29999);
    dispatch_line(&s_srv, a1, "CLEAR");
    assert_string_equal(a2->out, "CLEAR\n");
    clear_out(a2);
    clear_out(a3);
    clear_out(a4);

    /* Advance 1 ms to 60000 ms (now_ms == phase_deadline_ms): STROKE, CLEAR, GUESS all return 300 before tick */
    clock_advance(&s_srv.clock, 1);
    dispatch_line(&s_srv, a1, "STROKE|FF0000|4|0,0,10,10");
    assert_non_null(strstr(a1->out, "ERR|300|"));
    clear_out(a1);

    dispatch_line(&s_srv, a1, "CLEAR");
    assert_non_null(strstr(a1->out, "ERR|300|"));
    clear_out(a1);

    dispatch_line(&s_srv, a4, "GUESS|cat");
    assert_non_null(strstr(a4->out, "ERR|300|"));
    clear_out(a4);

    dispatch_line(&s_srv, b2, "GUESS|cat");
    assert_non_null(strstr(b2->out, "ERR|300|"));
    clear_out(b2);
}

static void test_game_all_guessed_cases(void **state)
{
    Room r;
    (void)state;

    assert_false(game_all_guessed(NULL));

    memset(&r, 0, sizeof(r));
    r.id = 1;
    memcpy(r.drawer, "alice", 6);
    memcpy(r.players[0].name, "alice", 6);
    memcpy(r.players[1].name, "bob", 4);
    memcpy(r.players[2].name, "carol", 6);
    r.nplayers = 3;

    /* Nobody guessed */
    assert_false(game_all_guessed(&r));

    /* Partial guessed */
    r.players[1].guessed = true;
    assert_false(game_all_guessed(&r));

    /* Un-guessed player leaves -> remaining guesser has guessed -> true */
    r.nplayers = 2;
    assert_true(game_all_guessed(&r));

    /* All 3 present and all guessers guessed -> true */
    r.nplayers = 3;
    r.players[2].guessed = true;
    assert_true(game_all_guessed(&r));

    /* Only drawer left in room (0 guessers) -> must be false */
    r.nplayers = 1;
    assert_false(game_all_guessed(&r));
}

static void test_game_turn_end_all_guessed_timeup_and_idempotence(void **state)
{
    Conn *c1;
    Conn *c2;
    Conn *c3;
    (void)state;

    init_test_server();
    c1 = make_user("alice");
    c2 = make_user("bob");
    c3 = make_user("carol");

    dispatch_line(&s_srv, c1, "CREATE_ROOM|R1|3|2|60");
    dispatch_line(&s_srv, c2, "JOIN_ROOM|1");
    dispatch_line(&s_srv, c3, "JOIN_ROOM|1");
    dispatch_line(&s_srv, c1, "START");
    Room *r = &s_srv.rooms[0];
    (void)snprintf(r->options[0], sizeof(r->options[0]), "cat");
    dispatch_line(&s_srv, c1, "CHOOSE_WORD|1");
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    /* carol guesses at remaining=45s (400 pts), bob guesses at remaining=30s (275 pts) */
    clock_advance(&s_srv.clock, 15000);
    dispatch_line(&s_srv, c3, "GUESS|cat");
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    clock_advance(&s_srv.clock, 15000);
    dispatch_line(&s_srv, c2, "GUESS|cat");
    assert_string_equal(
        c2->out,
        "OK|CORRECT|275\n"
        "GUESS_CORRECT|bob|275\n"
        "TURN_END|ALL_GUESSED|cat|alice:337:337,bob:275:275,carol:400:400\n"
    );
    assert_string_equal(
        c1->out,
        "GUESS_CORRECT|bob|275\n"
        "TURN_END|ALL_GUESSED|cat|alice:337:337,bob:275:275,carol:400:400\n"
    );
    assert_int_equal(r->state, ROOM_TURN_END);
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    /* Calling game_end_turn again in ROOM_TURN_END is idempotent (no-op) */
    game_end_turn(&s_srv, r, TURN_END_ALL_GUESSED, clock_now_ms(&s_srv.clock));
    assert_int_equal(c1->out_len, 0);
    assert_int_equal(r->players[0].score, 337);

    /* Commands during ROOM_TURN_END pause: GUESS, STROKE, CLEAR, CHOOSE_WORD -> 300 */
    dispatch_line(&s_srv, c2, "GUESS|cat");
    assert_non_null(strstr(c2->out, "ERR|300|"));
    clear_out(c2);

    dispatch_line(&s_srv, c1, "STROKE|FF0000|4|0,0,10,10");
    assert_non_null(strstr(c1->out, "ERR|300|"));
    clear_out(c1);

    dispatch_line(&s_srv, c1, "CLEAR");
    assert_non_null(strstr(c1->out, "ERR|300|"));
    clear_out(c1);

    dispatch_line(&s_srv, c1, "CHOOSE_WORD|1");
    assert_non_null(strstr(c1->out, "ERR|300|"));
    clear_out(c1);

    /* After 5000 ms pause: enters Turn 2 (round 1, drawer bob), word is cleared */
    clock_advance(&s_srv.clock, 5000);
    server_tick(&s_srv, clock_now_ms(&s_srv.clock));
    assert_int_equal(r->state, ROOM_CHOOSING);
    assert_string_equal(r->drawer, "bob");
    assert_string_equal(r->word, "");
    (void)snprintf(r->options[0], sizeof(r->options[0]), "house");
    dispatch_line(&s_srv, c2, "CHOOSE_WORD|1");
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    /* TIMEUP boundary: at 59999 ms elapsed, still ROOM_DRAWING; at 60000 ms, TURN_END|TIMEUP */
    clock_advance(&s_srv.clock, 59999);
    server_tick(&s_srv, clock_now_ms(&s_srv.clock));
    assert_int_equal(r->state, ROOM_DRAWING);
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    clock_advance(&s_srv.clock, 1);
    server_tick(&s_srv, clock_now_ms(&s_srv.clock));
    assert_int_equal(r->state, ROOM_TURN_END);
    assert_string_equal(
        c1->out,
        "TURN_END|TIMEUP|house|alice:337:0,bob:275:0,carol:400:0\n"
    );
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    /* 1-hour jump during ROOM_DRAWING only ends ONE turn */
    clock_advance(&s_srv.clock, 5000);
    server_tick(&s_srv, clock_now_ms(&s_srv.clock));
    assert_int_equal(r->state, ROOM_CHOOSING);
    dispatch_line(&s_srv, c3, "CHOOSE_WORD|1");
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    clock_advance(&s_srv.clock, 3600000);
    game_tick(&s_srv, clock_now_ms(&s_srv.clock));
    assert_int_equal(r->state, ROOM_TURN_END);
    assert_non_null(strstr(c1->out, "TURN_END|TIMEUP|"));
    assert_null(strstr(c1->out, "TURN_START|"));
}

static void test_game_full_rotation_2_rounds_3_players_and_two_games(void **state)
{
    Conn *c[3];
    const char *expected_drawer[6] = { "alice", "bob", "carol", "alice", "bob", "carol" };
    const int expected_round[6]    = { 1, 1, 1, 2, 2, 2 };
    (void)state;

    init_test_server();
    c[0] = make_user("alice");
    c[1] = make_user("bob");
    c[2] = make_user("carol");

    dispatch_line(&s_srv, c[0], "CREATE_ROOM|Phong Vui|3|2|60");
    dispatch_line(&s_srv, c[1], "JOIN_ROOM|1");
    dispatch_line(&s_srv, c[2], "JOIN_ROOM|1");
    clear_out(c[0]);
    clear_out(c[1]);
    clear_out(c[2]);

    /* Play 2 full consecutive games in the same room */
    for (int g = 0; g < 2; g++) {
        dispatch_line(&s_srv, c[0], "START");
        Room *r = &s_srv.rooms[0];

        for (int turn = 0; turn < 6; turn++) {
            int d_idx = turn % 3;
            assert_int_equal(r->state, ROOM_CHOOSING);
            assert_int_equal(r->round, expected_round[turn]);
            assert_string_equal(r->drawer, expected_drawer[turn]);

            char exp_ts[64];
            (void)snprintf(exp_ts, sizeof(exp_ts), "TURN_START|%d|%s|15\n",
                           expected_round[turn], expected_drawer[turn]);
            assert_non_null(strstr(c[0]->out, exp_ts));

            for (int k = 0; k < 3; k++) {
                clear_out(c[k]);
            }

            (void)snprintf(r->options[0], sizeof(r->options[0]), "cat");
            dispatch_line(&s_srv, c[d_idx], "CHOOSE_WORD|1");
            for (int k = 0; k < 3; k++) {
                clear_out(c[k]);
            }

            /* Both non-drawers guess "cat" without getting 303 */
            for (int k = 0; k < 3; k++) {
                if (k == d_idx) {
                    continue;
                }
                dispatch_line(&s_srv, c[k], "GUESS|cat");
                assert_non_null(strstr(c[k]->out, "OK|CORRECT|"));
            }
            assert_int_equal(r->state, ROOM_TURN_END);
            for (int k = 0; k < 3; k++) {
                clear_out(c[k]);
            }

            clock_advance(&s_srv.clock, 5000);
            server_tick(&s_srv, clock_now_ms(&s_srv.clock));
        }

        /* After 6th turn pause: GAME_END|FINISHED + ROOM_INFO in LOBBY + reset to 0 */
        assert_int_equal(r->state, ROOM_LOBBY);
        assert_int_equal(r->round, 0);
        assert_string_equal(r->drawer, "");
        assert_string_equal(r->word, "");
        for (int k = 0; k < 3; k++) {
            assert_int_equal(r->players[k].score, 0);
            assert_false(r->players[k].guessed);
            assert_int_equal(r->players[k].turn_delta, 0);
            assert_non_null(strstr(c[k]->out, "GAME_END|FINISHED|"));
            assert_non_null(strstr(c[k]->out, "ROOM_INFO|1|Phong Vui|alice|3|2|60|LOBBY|alice,bob,carol\n"));
            clear_out(c[k]);
        }
    }
}

static void test_game_scoreboard_and_ranking_edge_cases(void **state)
{
    Conn *c1;
    Conn *c2;
    Conn *c3;
    Conn *unauth;
    (void)state;

    init_test_server();
    unauth = conn_alloc(&s_srv.conns, -1, 1000);
    c1 = make_user("alice");
    c2 = make_user("Bob");
    c3 = make_user("carol");

    /* Unauthenticated -> 101; outside room -> 204 */
    dispatch_line(&s_srv, unauth, "SCOREBOARD");
    assert_non_null(strstr(unauth->out, "ERR|101|"));
    clear_out(unauth);

    dispatch_line(&s_srv, c1, "SCOREBOARD");
    assert_non_null(strstr(c1->out, "ERR|204|"));
    clear_out(c1);

    dispatch_line(&s_srv, c1, "CREATE_ROOM|R1|8|2|60");
    dispatch_line(&s_srv, c2, "JOIN_ROOM|1");
    dispatch_line(&s_srv, c3, "JOIN_ROOM|1");
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    /* In LOBBY (all 0): ASCII tie-break puts "Bob" before "alice" before "carol" */
    dispatch_line(&s_srv, c1, "SCOREBOARD");
    assert_string_equal(c1->out, "OK|Bob:0,alice:0,carol:0\n");
    clear_out(c1);

    /* Start turn: Bob guesses (500) then leaves room, carol guesses (475) -> nplayers=2 at turn end so alice gets (500+475)/1 = 975 */
    dispatch_line(&s_srv, c1, "START");
    Room *r = &s_srv.rooms[0];
    (void)snprintf(r->options[0], sizeof(r->options[0]), "cat");
    dispatch_line(&s_srv, c1, "CHOOSE_WORD|1");
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    dispatch_line(&s_srv, c2, "GUESS|cat");
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    /* Mid-turn SCOREBOARD */
    dispatch_line(&s_srv, c1, "SCOREBOARD");
    assert_string_equal(c1->out, "OK|Bob:500,alice:0,carol:0\n");
    clear_out(c1);

    /* Bob leaves room -> excluded from ranking */
    dispatch_line(&s_srv, c2, "LEAVE_ROOM");
    clear_out(c1);
    clear_out(c2);
    clear_out(c3);

    dispatch_line(&s_srv, c3, "GUESS|cat");
    assert_non_null(strstr(c1->out, "TURN_END|ALL_GUESSED|cat|alice:975:975,carol:475:475\n"));
    clear_out(c1);
    clear_out(c3);

    dispatch_line(&s_srv, c1, "SCOREBOARD");
    assert_string_equal(c1->out, "OK|alice:975,carol:475\n");
    clear_out(c1);

    game_format_ranking(NULL, NULL, 0);
    game_end_game(NULL, NULL, GAME_END_FINISHED);
    game_end_turn(NULL, NULL, TURN_END_TIMEUP, 0);
}

int run_game_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_game_start_8_players_and_no_side_effects_on_900),
        cmocka_unit_test(test_game_choose_word_priority_and_draw_start),
        cmocka_unit_test(test_game_auto_pick_timeout_and_all_indices),
        cmocka_unit_test(test_game_time_ticks_schedule_and_isolation),
        cmocka_unit_test(test_game_drawer_leaves_during_choosing),
        cmocka_unit_test(test_game_stroke_and_clear_validation_and_forwarding),
        cmocka_unit_test(test_game_guess_scoring_and_deadline_bounds),
        cmocka_unit_test(test_game_all_guessed_cases),
        cmocka_unit_test(test_game_turn_end_all_guessed_timeup_and_idempotence),
        cmocka_unit_test(test_game_full_rotation_2_rounds_3_players_and_two_games),
        cmocka_unit_test(test_game_scoreboard_and_ranking_edge_cases),
    };
    return cmocka_run_group_tests_name("game", tests, NULL, NULL);
}
