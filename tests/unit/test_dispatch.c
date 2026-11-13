#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
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

    /* Unauthenticated state */
    dispatch_line(&s_srv, c, "PING");
    assert_string_equal(c->out, "PONG\n");
    clear_out(c);

    /* Authenticated outside room */
    c->state = CONN_AUTHED;
    c->room_id = -1;
    dispatch_line(&s_srv, c, "PING");
    assert_string_equal(c->out, "PONG\n");
    clear_out(c);

    /* Authenticated inside room */
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

    /* Bad format (extra argument on PING) returns 100 */
    dispatch_line(&s_srv, c, "PING|extra");
    assert_non_null(strstr(c->out, "ERR|100|"));
    clear_out(c);

    /* Line >= 512 bytes returns 104 */
    memset(long_line, 'X', FRAME_MAX_LINE);
    long_line[FRAME_MAX_LINE] = '\0';
    dispatch_line(&s_srv, c, long_line);
    assert_non_null(strstr(c->out, "ERR|104|"));
    clear_out(c);
}

static void test_dispatch_auth_and_room_checks(void **state)
{
    Conn *c;
    (void)state;

    server_init(&s_srv, NULL);
    c = conn_alloc(&s_srv.conns, -1, 0);
    assert_non_null(c);

    /* Unauthenticated calling AUTH_REQUIRED -> 101 */
    dispatch_line(&s_srv, c, "LOGOUT");
    assert_non_null(strstr(c->out, "ERR|101|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "CREATE_ROOM|r1|4|3|60");
    assert_non_null(strstr(c->out, "ERR|101|"));
    clear_out(c);

    /* Authenticated calling AUTH_GUEST_ONLY -> 102 */
    c->state = CONN_AUTHED;
    c->room_id = -1;
    dispatch_line(&s_srv, c, "REGISTER|alice|secret1");
    assert_non_null(strstr(c->out, "ERR|102|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "LOGIN|alice|secret1");
    assert_non_null(strstr(c->out, "ERR|102|"));
    clear_out(c);

    /* Authenticated outside room calling ROOM_IN_ONLY -> 204 */
    dispatch_line(&s_srv, c, "LEAVE_ROOM");
    assert_non_null(strstr(c->out, "ERR|204|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "START");
    assert_non_null(strstr(c->out, "ERR|204|"));
    clear_out(c);

    /* Authenticated inside room calling ROOM_OUT_ONLY -> 201 */
    c->room_id = 1;
    dispatch_line(&s_srv, c, "CREATE_ROOM|r1|4|3|60");
    assert_non_null(strstr(c->out, "ERR|201|"));
    clear_out(c);

    dispatch_line(&s_srv, c, "JOIN_ROOM|1");
    assert_non_null(strstr(c->out, "ERR|201|"));
    clear_out(c);

    /* Unimplemented handler returns 900 */
    dispatch_line(&s_srv, c, "LEAVE_ROOM");
    assert_non_null(strstr(c->out, "ERR|900|"));
    clear_out(c);
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
        cmocka_unit_test(test_dispatch_auth_and_room_checks),
        cmocka_unit_test(test_dispatch_reply_helpers_and_nulls),
    };
    return cmocka_run_group_tests_name("dispatch", tests, NULL, NULL);
}
