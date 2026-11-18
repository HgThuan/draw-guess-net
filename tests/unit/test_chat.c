#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>

#include "chat.h"
#include "dispatch.h"
#include "game.h"
#include "session.h"

static Server s_srv;
static LogMemSink s_mem;

static void clear_out(Conn *c)
{
    conn_consume_out(c, c->out_len);
    conn_clear_dirty(&s_srv.conns);
}

static void clear_all(Conn **cs, int n)
{
    for (int i = 0; i < n; i++) {
        clear_out(cs[i]);
    }
}

static Conn *make_user(const char *name)
{
    Conn *c = conn_alloc(&s_srv.conns, -1, clock_now_ms(&s_srv.clock));
    assert_non_null(c);
    char cmd[64];
    (void)snprintf(cmd, sizeof(cmd), "REGISTER|%s|Secret123", name);
    dispatch_line(&s_srv, c, cmd);
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
    logger_init_mem(&s_srv.log, &s_mem, LOG_INFO);
    user_store_init(&s_srv.users, NULL);
    (void)words_load(&s_srv.words, "tests/data/words_small.txt");
}

static void reset_log(void)
{
    s_mem.len = 0;
    s_mem.buf[0] = '\0';
}

static int count_substr(const char *hay, const char *needle)
{
    int n = 0;
    size_t len = strlen(needle);
    for (const char *p = strstr(hay, needle); p; p = strstr(p + len, needle)) {
        n++;
    }
    return n;
}

/* alice hosts room 1 with bob and carol, then starts and picks "cat". */
static Room *start_drawing_cat(Conn **c)
{
    dispatch_line(&s_srv, c[0], "CREATE_ROOM|Phong Vui|3|2|60");
    dispatch_line(&s_srv, c[1], "JOIN_ROOM|1");
    dispatch_line(&s_srv, c[2], "JOIN_ROOM|1");
    dispatch_line(&s_srv, c[0], "START");
    Room *r = room_find(&s_srv, 1);
    (void)snprintf(r->options[0], sizeof(r->options[0]), "cat");
    dispatch_line(&s_srv, c[0], "CHOOSE_WORD|1");
    clear_all(c, 3);
    return r;
}

static void test_chat_lobby_bounds_and_step1_priority(void **state)
{
    Conn *c[2];
    char line[260];
    (void)state;

    init_test_server();
    Conn *guest = conn_alloc(&s_srv.conns, -1, 0);
    c[0] = make_user("alice");
    c[1] = make_user("bob");

    /* Blank text is a format error before the login and room checks */
    dispatch_line(&s_srv, guest, "CHAT|    ");
    assert_string_equal(guest->out, "ERR|100|bad format\n");
    clear_out(guest);
    dispatch_line(&s_srv, guest, "CHAT|hi");
    assert_non_null(strstr(guest->out, "ERR|101|"));
    clear_out(guest);
    dispatch_line(&s_srv, c[0], "CHAT|    ");
    assert_non_null(strstr(c[0]->out, "ERR|100|"));
    clear_out(c[0]);
    dispatch_line(&s_srv, c[0], "CHAT|hi");
    assert_non_null(strstr(c[0]->out, "ERR|204|"));
    clear_out(c[0]);

    dispatch_line(&s_srv, c[0], "CREATE_ROOM|R1|4|2|60");
    dispatch_line(&s_srv, c[1], "JOIN_ROOM|1");
    clear_all(c, 2);

    /* Lobby: sender gets OK then the event; pipes inside the text survive */
    dispatch_line(&s_srv, c[0], "CHAT|  a|b c  ");
    assert_string_equal(c[0]->out, "OK\nCHAT|alice|a|b c\n");
    assert_string_equal(c[1]->out, "CHAT|alice|a|b c\n");
    clear_all(c, 2);

    /* Exactly 200 bytes passes, 201 bytes is rejected */
    memcpy(line, "CHAT|", 5);
    memset(line + 5, 'x', 200);
    line[205] = '\0';
    dispatch_line(&s_srv, c[1], line);
    assert_int_equal(strncmp(c[1]->out, "OK\nCHAT|bob|xxx", 15), 0);
    clear_all(c, 2);

    line[205] = 'x';
    line[206] = '\0';
    dispatch_line(&s_srv, c[1], line);
    assert_non_null(strstr(c[1]->out, "ERR|100|"));
    assert_int_equal(c[0]->out_len, 0);
    clear_all(c, 2);
}

static void test_chat_drawer_blocked_and_answer_filter(void **state)
{
    Conn *c[3];
    (void)state;

    init_test_server();
    c[0] = make_user("alice");
    c[1] = make_user("bob");
    c[2] = make_user("carol");

    dispatch_line(&s_srv, c[0], "CREATE_ROOM|Phong Vui|3|2|60");
    dispatch_line(&s_srv, c[1], "JOIN_ROOM|1");
    dispatch_line(&s_srv, c[2], "JOIN_ROOM|1");
    dispatch_line(&s_srv, c[0], "START");
    Room *r = room_find(&s_srv, 1);
    clear_all(c, 3);

    /* Drawer cannot chat while choosing; guessers still can */
    dispatch_line(&s_srv, c[0], "CHAT|hello");
    assert_non_null(strstr(c[0]->out, "ERR|302|"));
    assert_int_equal(c[1]->out_len, 0);
    clear_all(c, 3);
    dispatch_line(&s_srv, c[1], "CHAT|good luck");
    assert_string_equal(c[2]->out, "CHAT|bob|good luck\n");
    clear_all(c, 3);

    (void)snprintf(r->options[0], sizeof(r->options[0]), "cat");
    dispatch_line(&s_srv, c[0], "CHOOSE_WORD|1");
    clear_all(c, 3);

    dispatch_line(&s_srv, c[0], "CHAT|hello");
    assert_non_null(strstr(c[0]->out, "ERR|302|"));
    assert_int_equal(c[1]->out_len, 0);
    assert_int_equal(c[2]->out_len, 0);
    clear_all(c, 3);

    /* Answer at the start, in the middle, in another case, or mixed with UTF-8 is blocked */
    const char *leaks[] = {
        "CHAT|cat is it",
        "CHAT|maybe a cat?",
        "CHAT|CAT",
        "CHAT|đáp án là cat nhé"
    };
    for (size_t i = 0; i < 4; i++) {
        dispatch_line(&s_srv, c[1], leaks[i]);
        assert_string_equal(c[1]->out, "ERR|306|chat blocked\n");
        assert_int_equal(c[0]->out_len, 0);
        assert_int_equal(c[2]->out_len, 0);
        clear_all(c, 3);
    }

    /* Unrelated words containing the answer as a fragment pass */
    dispatch_line(&s_srv, c[1], "CHAT|educate me");
    assert_string_equal(c[1]->out, "OK\nCHAT|bob|educate me\n");
    assert_string_equal(c[0]->out, "CHAT|bob|educate me\n");
    assert_string_equal(c[2]->out, "CHAT|bob|educate me\n");
    clear_all(c, 3);
}

static void test_chat_solved_channel_turn_end_and_next_turn(void **state)
{
    Conn *c[3];
    (void)state;

    init_test_server();
    c[0] = make_user("alice");
    c[1] = make_user("bob");
    c[2] = make_user("carol");
    Room *r = start_drawing_cat(c);

    dispatch_line(&s_srv, c[1], "GUESS|cat");
    clear_all(c, 3);

    /* Solved player may mention the answer, but only the drawer and solvers hear it */
    dispatch_line(&s_srv, c[1], "CHAT|cat was easy");
    assert_string_equal(c[1]->out, "OK\nCHAT|bob|cat was easy\n");
    assert_string_equal(c[0]->out, "CHAT|bob|cat was easy\n");
    assert_int_equal(c[2]->out_len, 0);
    clear_all(c, 3);

    dispatch_line(&s_srv, c[2], "GUESS|cat");
    assert_int_equal(r->state, ROOM_TURN_END);
    clear_all(c, 3);

    /* During the pause everyone, drawer included, talks to the whole room */
    dispatch_line(&s_srv, c[0], "CHAT|it was a cat");
    assert_string_equal(c[0]->out, "OK\nCHAT|alice|it was a cat\n");
    assert_string_equal(c[1]->out, "CHAT|alice|it was a cat\n");
    assert_string_equal(c[2]->out, "CHAT|alice|it was a cat\n");
    clear_all(c, 3);

    /* Next turn: the previous answer no longer filters chat */
    clock_advance(&s_srv.clock, 5000);
    game_tick(&s_srv, clock_now_ms(&s_srv.clock));
    assert_int_equal(r->state, ROOM_CHOOSING);
    assert_string_equal(r->drawer, "bob");
    clear_all(c, 3);
    dispatch_line(&s_srv, c[2], "CHAT|cat again?");
    assert_string_equal(c[2]->out, "OK\nCHAT|carol|cat again?\n");
    clear_all(c, 3);

    (void)snprintf(r->options[0], sizeof(r->options[0]), "ice cream");
    dispatch_line(&s_srv, c[1], "CHOOSE_WORD|1");
    clear_all(c, 3);
    dispatch_line(&s_srv, c[2], "CHAT|cat");
    assert_string_equal(c[2]->out, "OK\nCHAT|carol|cat\n");
    clear_all(c, 3);

    /* Extra spaces or no space do not bypass the filter; the message itself is kept intact */
    dispatch_line(&s_srv, c[2], "CHAT|ice   cream");
    assert_non_null(strstr(c[2]->out, "ERR|306|"));
    clear_all(c, 3);
    dispatch_line(&s_srv, c[2], "CHAT|ICECREAM");
    assert_non_null(strstr(c[2]->out, "ERR|306|"));
    assert_int_equal(c[0]->out_len, 0);
    clear_all(c, 3);
}

static void test_chat_rooms_are_isolated(void **state)
{
    Conn *a[2];
    Conn *b[2];
    (void)state;

    init_test_server();
    a[0] = make_user("alice");
    a[1] = make_user("bob");
    b[0] = make_user("carol");
    b[1] = make_user("dave");

    dispatch_line(&s_srv, a[0], "CREATE_ROOM|R1|4|2|60");
    dispatch_line(&s_srv, a[1], "JOIN_ROOM|1");
    dispatch_line(&s_srv, b[0], "CREATE_ROOM|R2|4|2|60");
    dispatch_line(&s_srv, b[1], "JOIN_ROOM|2");
    clear_all(a, 2);
    clear_all(b, 2);

    dispatch_line(&s_srv, a[0], "CHAT|room one only");
    assert_string_equal(a[1]->out, "CHAT|alice|room one only\n");
    assert_int_equal(b[0]->out_len, 0);
    assert_int_equal(b[1]->out_len, 0);
}

static void test_log_room_and_game_events_exact_sequence(void **state)
{
    Conn *c[3];
    char exp[2048];
    (void)state;

    init_test_server();
    c[0] = make_user("alice");
    c[1] = make_user("bob");
    c[2] = make_user("carol");
    reset_log();

    Room *r = start_drawing_cat(c);
    dispatch_line(&s_srv, c[1], "GUESS|cat");
    dispatch_line(&s_srv, c[2], "CHAT|secret chatter");
    dispatch_line(&s_srv, c[2], "GUESS|cat");
    assert_int_equal(r->state, ROOM_TURN_END);

    char sid_a[17];
    char sid_b[17];
    char sid_c[17];
    memcpy(sid_a, c[0]->session_id, sizeof(sid_a));
    memcpy(sid_b, c[1]->session_id, sizeof(sid_b));
    memcpy(sid_c, c[2]->session_id, sizeof(sid_c));

    dispatch_line(&s_srv, c[0], "LEAVE_ROOM"); /* host leaves during the pause */
    dispatch_line(&s_srv, c[1], "LEAVE_ROOM"); /* bob was host too: carol takes over, game ends */
    session_on_disconnect(&s_srv, c[2]);       /* last one: room is deleted */
    assert_null(room_find(&s_srv, 1));

#define TS "1970-01-01T00:00:01.000Z INFO "
    (void)snprintf(exp, sizeof(exp),
        TS "sid=%s room=1 event=ROOM_CREATE user=alice name=\"Phong Vui\" max=3 rounds=2 draw_time=60\n"
        TS "sid=%s room=1 event=ROOM_JOIN user=bob\n"
        TS "sid=%s room=1 event=ROOM_JOIN user=carol\n"
        TS "sid=%s room=1 event=GAME_START rounds=2 nplayers=3\n"
        TS "sid=%s room=1 event=GUESS_CORRECT user=bob points=500\n"
        TS "sid=%s room=1 event=GUESS_CORRECT user=carol points=475\n"
        TS "sid=- room=1 event=TURN_END reason=ALL_GUESSED round=1 drawer=alice\n"
        TS "sid=%s room=1 event=ROOM_LEAVE user=alice reason=LEAVE\n"
        TS "sid=- room=1 event=HOST_CHANGED host=bob\n"
        TS "sid=%s room=1 event=ROOM_LEAVE user=bob reason=LEAVE\n"
        TS "sid=- room=1 event=HOST_CHANGED host=carol\n"
        TS "sid=- room=1 event=GAME_END reason=NOT_ENOUGH_PLAYERS\n"
        TS "sid=%s room=1 event=ROOM_LEAVE user=carol reason=DISCONNECT\n"
        TS "sid=- room=1 event=ROOM_DELETE\n",
        sid_a, sid_b, sid_c, sid_a, sid_b, sid_c, sid_a, sid_b, sid_c);
#undef TS
    /* Disconnect itself is logged by the session layer after the room cleanup */
    assert_int_equal(strncmp(s_mem.buf, exp, strlen(exp)), 0);
    assert_int_equal(count_substr(s_mem.buf, "event=HOST_CHANGED"), 2);
    assert_int_equal(count_substr(s_mem.buf, "event=ROOM_DELETE"), 1);

    /* Answers, chat text, strokes and passwords never reach the log */
    assert_null(strstr(s_mem.buf, "cat"));
    assert_null(strstr(s_mem.buf, "secret chatter"));
    assert_null(strstr(s_mem.buf, "Secret123"));
    assert_null(strstr(s_mem.buf, "STROKE"));
}

int run_chat_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_chat_lobby_bounds_and_step1_priority),
        cmocka_unit_test(test_chat_drawer_blocked_and_answer_filter),
        cmocka_unit_test(test_chat_solved_channel_turn_end_and_next_turn),
        cmocka_unit_test(test_chat_rooms_are_isolated),
        cmocka_unit_test(test_log_room_and_game_events_exact_sequence),
    };
    return cmocka_run_group_tests_name("chat", tests, NULL, NULL);
}
