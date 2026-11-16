#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>

#include "dispatch.h"
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
            char exp[32];
            (void)snprintf(exp, sizeof(exp), "TIME|%d\n", 60 - step * 5);
            assert_string_equal(a1->out, exp);
            assert_string_equal(a2->out, exp);
            count_r1++;
            clear_out(a1);
            clear_out(a2);
        }
        if (b1->out_len > 0) {
            char exp[32];
            (void)snprintf(exp, sizeof(exp), "TIME|%d\n", 33 - step * 5);
            assert_string_equal(b1->out, exp);
            count_r2++;
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

    /* Drawer (alice) leaves during ROOM_CHOOSING -> bob becomes drawer, no crash on tick */
    dispatch_line(&s_srv, c1, "LEAVE_ROOM");
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

int run_game_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_game_start_8_players_and_no_side_effects_on_900),
        cmocka_unit_test(test_game_choose_word_priority_and_draw_start),
        cmocka_unit_test(test_game_auto_pick_timeout_and_all_indices),
        cmocka_unit_test(test_game_time_ticks_schedule_and_isolation),
        cmocka_unit_test(test_game_drawer_leaves_during_choosing),
    };
    return cmocka_run_group_tests_name("game", tests, NULL, NULL);
}
