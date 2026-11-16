#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>

#include "dispatch.h"

static Server s_srv;

static void clear_out(Conn *c)
{
    conn_consume_out(c, c->out_len);
    conn_clear_dirty(&s_srv.conns);
}

static void test_dispatch_ping_all_states(void **state)
{
    Conn *c;
    (void)state;

    server_init(&s_srv, NULL);
    c = conn_alloc(&s_srv.conns, -1, 0);
    assert_non_null(c);

    dispatch_line(&s_srv, c, "PING");
    assert_string_equal(c->out, "PONG\n");
    clear_out(c);

    c->state = CONN_AUTHED;
    c->room_id = -1;
    dispatch_line(&s_srv, c, "PING");
    assert_string_equal(c->out, "PONG\n");
    clear_out(c);

    c->room_id = 1;
    dispatch_line(&s_srv, c, "PING");
    assert_string_equal(c->out, "PONG\n");
    clear_out(c);
}

static void test_dispatch_syntax_and_priority(void **state)
{
    Conn *c;
    char long_line[FRAME_MAX_LINE + 4];
    (void)state;

    server_init(&s_srv, NULL);
    c = conn_alloc(&s_srv.conns, -1, 0);
    assert_non_null(c);

    /* Unknown command while unauthenticated must return 103 (not 101) */
    dispatch_line(&s_srv, c, "FOOBAR");
    assert_non_null(strstr(c->out, "ERR|103|"));
    clear_out(c);

    /* Bad format returns 100 */
    dispatch_line(&s_srv, c, "PING|extra");
    assert_non_null(strstr(c->out, "ERR|100|"));
    clear_out(c);

    /* Line >= 512 bytes returns 104 */
    memset(long_line, 'X', FRAME_MAX_LINE);
    long_line[FRAME_MAX_LINE] = '\0';
    dispatch_line(&s_srv, c, long_line);
    assert_non_null(strstr(c->out, "ERR|104|"));
    clear_out(c);

    /* Non-numeric integer arg returns 100 at Step 1 even when unauthenticated */
    dispatch_line(&s_srv, c, "CHOOSE_WORD|abc");
    assert_non_null(strstr(c->out, "ERR|100|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "LIST_ROOMS|abc");
    assert_non_null(strstr(c->out, "ERR|100|"));
    clear_out(c);
}

typedef enum {
    ST_UNAUTH = 0,
    ST_OUT_ROOM,
    ST_IN_LOBBY,
    ST_IN_CHOOSING,
    ST_IN_DRAWING
} MatrixState;

typedef struct {
    const char *cmd;
    int         exp[5]; /* 0 = OK/PONG/900 (passed dispatch checks), >0 = expected error code */
} MatrixRow;

static void prepare_conn_in_state(Conn *c, MatrixState st)
{
    room_table_init(&s_srv);
    if (st == ST_UNAUTH) {
        c->state = CONN_CONNECTED;
        c->room_id = -1;
        return;
    }

    c->state = CONN_AUTHED;
    memcpy(c->user, "tester", 7);
    if (st == ST_OUT_ROOM) {
        c->room_id = -1;
        return;
    }

    Room *r = &s_srv.rooms[0];
    r->id = 1;
    memcpy(r->name, "TestRoom", 9);
    r->max_players = 4;
    r->rounds = 3;
    r->draw_time = 60;
    r->nplayers = 1;
    r->players[0].conn = conn_id(c);
    memcpy(r->players[0].name, "tester", 7);
    if (st == ST_IN_LOBBY) {
        r->state = ROOM_LOBBY;
    } else if (st == ST_IN_CHOOSING) {
        r->state = ROOM_CHOOSING;
        memcpy(r->drawer, "tester", 7);
        memcpy(r->options[0], "cat", 4);
        memcpy(r->options[1], "dog", 4);
        memcpy(r->options[2], "tree", 5);
    } else {
        r->state = ROOM_DRAWING;
    }
    c->room_id = 1;
}

static void test_dispatch_full_state_matrix(void **state)
{
    static const MatrixRow ROWS[] = {
        /* cmd                             UNAUTH OUT  LOBBY CHOOSE DRAW */
        { "PING",                        {     0,   0,    0,     0,    0 } },
        { "REGISTER|u_mat|secret1",      {     0, 102,  102,   102,  102 } },
        { "LOGIN|u_mat|secret1",         {     0, 102,  102,   102,  102 } },
        { "LOGOUT",                      {   101,   0,    0,     0,    0 } },
        { "LIST_ROOMS|1",                {   101,   0,    0,     0,    0 } },
        { "CREATE_ROOM|R1|4|3|60",       {   101,   0,  201,   201,  201 } },
        { "JOIN_ROOM|1",                 {   101, 200,  201,   201,  201 } },
        { "LEAVE_ROOM",                  {   101, 204,    0,     0,    0 } },
        { "START",                       {   101, 204,  207,   300,  300 } },
        { "SCOREBOARD",                  {   101, 204,    0,     0,    0 } },
        { "CHOOSE_WORD|1",               {   101, 204,  300,     0,  300 } },
        { "STROKE|FF0000|4|0,0,10,10",   {   101, 204,  300,   300,    0 } },
        { "CLEAR",                       {   101, 204,  300,   300,    0 } },
        { "GUESS|cat",                   {   101, 204,  300,   300,    0 } },
        { "CHAT|hello",                  {   101, 204,    0,     0,    0 } },
    };

    Conn *c;
    (void)state;

    server_init(&s_srv, NULL);
    user_store_init(&s_srv.users, NULL);
    c = conn_alloc(&s_srv.conns, -1, 0);
    assert_non_null(c);

    size_t nrows = sizeof(ROWS) / sizeof(ROWS[0]);
    for (size_t i = 0; i < nrows; i++) {
        for (int st = ST_UNAUTH; st <= ST_IN_DRAWING; st++) {
            user_store_init(&s_srv.users, NULL);
            (void)auth_register(&s_srv.users, &s_srv.rng, "u_mat", "secret1");
            prepare_conn_in_state(c, (MatrixState)st);
            clear_out(c);

            /* For REGISTER in UNAUTH, use a fresh username */
            const char *line = ROWS[i].cmd;
            if (st == ST_UNAUTH && strncmp(line, "REGISTER|", 9) == 0) {
                line = "REGISTER|u_new|secret1";
            }

            dispatch_line(&s_srv, c, line);
            int expected = ROWS[i].exp[st];
            if (expected == 0) {
                /* Passed pre-handler state checks: response is PONG, OK..., or ERR|900 for unimplemented */
                bool passed = (strncmp(c->out, "OK", 2) == 0) ||
                              (strncmp(c->out, "PONG", 4) == 0) ||
                              (strncmp(c->out, "ERR|900|", 8) == 0);
                assert_true(passed);
            } else {
                char prefix[24];
                (void)snprintf(prefix, sizeof(prefix), "ERR|%d|", expected);
                assert_non_null(strstr(c->out, prefix));
            }
        }
    }
}

static void test_dispatch_reply_helpers_and_nulls(void **state)
{
    Conn *c;
    (void)state;

    server_init(&s_srv, NULL);
    c = conn_alloc(&s_srv.conns, -1, 0);
    assert_non_null(c);

    reply_ok(NULL, c, NULL);
    reply_ok(&s_srv, NULL, NULL);
    reply_err(NULL, c, ERR_BAD_FORMAT);
    reply_err(&s_srv, NULL, ERR_BAD_FORMAT);
    dispatch_line(NULL, c, "PING");
    dispatch_line(&s_srv, NULL, "PING");
    dispatch_line(&s_srv, c, NULL);

    reply_ok(&s_srv, c, NULL);
    assert_string_equal(c->out, "OK\n");
    clear_out(c);

    reply_ok(&s_srv, c, "");
    assert_string_equal(c->out, "OK\n");
    clear_out(c);

    reply_ok(&s_srv, c, "123");
    assert_string_equal(c->out, "OK|123\n");
    clear_out(c);
}

int run_dispatch_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_dispatch_ping_all_states),
        cmocka_unit_test(test_dispatch_syntax_and_priority),
        cmocka_unit_test(test_dispatch_full_state_matrix),
        cmocka_unit_test(test_dispatch_reply_helpers_and_nulls),
    };
    return cmocka_run_group_tests_name("dispatch", tests, NULL, NULL);
}
