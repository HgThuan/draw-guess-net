#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "dispatch.h"
#include "session.h"

static Server s_srv;
static LogMemSink s_mem;

static void setup_env(void)
{
    server_init(&s_srv, NULL);
    clock_init_fake(&s_srv.clock, 1000);
    rng_init_fake(&s_srv.rng, 12345);
    logger_init_mem(&s_srv.log, &s_mem, LOG_DEBUG);
    user_store_init(&s_srv.users, NULL);
}

static void clear_out(Conn *c)
{
    conn_consume_out(c, c->out_len);
    conn_clear_dirty(&s_srv.conns);
}

static void test_session_login_and_single_account_rule(void **state)
{
    Conn *c1;
    Conn *c2;
    (void)state;

    setup_env();
    c1 = conn_alloc(&s_srv.conns, -1, 1000);
    c2 = conn_alloc(&s_srv.conns, -1, 1000);
    assert_non_null(c1);
    assert_non_null(c2);

    dispatch_line(&s_srv, c1, "REGISTER|alice|SecretPass1");
    assert_string_equal(c1->out, "OK\n");
    clear_out(c1);

    /* Wrong username and wrong password both return 111 */
    dispatch_line(&s_srv, c1, "LOGIN|nobody|SecretPass1");
    assert_non_null(strstr(c1->out, "ERR|111|"));
    clear_out(c1);

    dispatch_line(&s_srv, c1, "LOGIN|alice|WrongPass9");
    assert_non_null(strstr(c1->out, "ERR|111|"));
    clear_out(c1);

    /* Valid login returns OK|<16_hex_chars> */
    dispatch_line(&s_srv, c1, "LOGIN|alice|SecretPass1");
    assert_true(strncmp(c1->out, "OK|", 3) == 0);
    assert_int_equal(strlen(c1->out), 3 + 16 + 1);
    assert_int_equal(c1->state, CONN_AUTHED);
    assert_string_equal(c1->user, "alice");
    clear_out(c1);

    /* Calling LOGIN again on the same connection returns 102 */
    dispatch_line(&s_srv, c1, "LOGIN|alice|SecretPass1");
    assert_non_null(strstr(c1->out, "ERR|102|"));
    clear_out(c1);

    /* Second connection logging into the same online account returns 114 */
    dispatch_line(&s_srv, c2, "LOGIN|alice|SecretPass1");
    assert_non_null(strstr(c2->out, "ERR|114|"));
    assert_int_equal(c2->state, CONN_CONNECTED);
    clear_out(c2);
}

static void test_session_logout_and_disconnect_relogin(void **state)
{
    Conn *c1;
    Conn *c2;
    (void)state;

    setup_env();
    c1 = conn_alloc(&s_srv.conns, -1, 1000);
    c2 = conn_alloc(&s_srv.conns, -1, 1000);

    /* LOGOUT before login returns 101 */
    dispatch_line(&s_srv, c1, "LOGOUT");
    assert_non_null(strstr(c1->out, "ERR|101|"));
    clear_out(c1);

    dispatch_line(&s_srv, c1, "REGISTER|bob|SecretPass2");
    clear_out(c1);
    dispatch_line(&s_srv, c1, "LOGIN|bob|SecretPass2");
    clear_out(c1);

    /* LOGOUT frees the account for c2 */
    dispatch_line(&s_srv, c1, "LOGOUT");
    assert_string_equal(c1->out, "OK\n");
    assert_int_equal(c1->state, CONN_CONNECTED);
    clear_out(c1);

    dispatch_line(&s_srv, c2, "LOGIN|bob|SecretPass2");
    assert_true(strncmp(c2->out, "OK|", 3) == 0);
    clear_out(c2);

    /* Abrupt disconnect on c2 also frees the account for c1 */
    session_on_disconnect(&s_srv, c2);
    conn_free(&s_srv.conns, c2);

    dispatch_line(&s_srv, c1, "LOGIN|bob|SecretPass2");
    assert_true(strncmp(c1->out, "OK|", 3) == 0);
    clear_out(c1);

    /* Null safety */
    session_on_disconnect(NULL, c1);
    session_on_disconnect(&s_srv, NULL);
    session_tick(NULL, 0);
}

static void test_session_idle_timeout_and_ping_refresh(void **state)
{
    Conn *c_idle;
    Conn *c_active;
    (void)state;

    setup_env();
    c_idle = conn_alloc(&s_srv.conns, -1, 1000);
    c_active = conn_alloc(&s_srv.conns, -1, 1000);

    dispatch_line(&s_srv, c_idle, "REGISTER|carol|SecretPass3");
    clear_out(c_idle);
    dispatch_line(&s_srv, c_idle, "LOGIN|carol|SecretPass3");
    clear_out(c_idle);

    /* Advance 60s (< 120s default timeout) */
    clock_advance(&s_srv.clock, 60000);
    server_tick(&s_srv, clock_now_ms(&s_srv.clock));
    assert_false(c_idle->closing);
    assert_false(c_active->closing);

    /* c_active sends PING at t=61s, refreshing its last_active_ms */
    dispatch_line(&s_srv, c_active, "PING");
    assert_string_equal(c_active->out, "PONG\n");
    clear_out(c_active);

    /* Advance another 60s (total 120s for c_idle, 60s for c_active) */
    clock_advance(&s_srv.clock, 60000);
    server_tick(&s_srv, clock_now_ms(&s_srv.clock));

    assert_true(c_idle->closing);
    assert_string_equal(c_idle->out, "KICK|IDLE\n");
    assert_false(c_active->closing);
}

static void test_session_logging_and_password_masking(void **state)
{
    Conn *c;
    (void)state;

    setup_env();
    c = conn_alloc(&s_srv.conns, -1, 1000);

    dispatch_line(&s_srv, c, "REGISTER|dave|UltraSecret#99");
    dispatch_line(&s_srv, c, "LOGIN|dave|WrongSecret#00");
    dispatch_line(&s_srv, c, "LOGIN|dave|UltraSecret#99");
    dispatch_line(&s_srv, c, "LOGOUT");
    session_on_disconnect(&s_srv, c);

    /* Mandatory events must be logged */
    assert_non_null(strstr(s_mem.buf, "event=REGISTER user=dave"));
    assert_non_null(strstr(s_mem.buf, "event=LOGIN_FAIL user=dave code=111"));
    assert_non_null(strstr(s_mem.buf, "event=LOGIN user=dave"));
    assert_non_null(strstr(s_mem.buf, "event=LOGOUT user=dave"));
    assert_non_null(strstr(s_mem.buf, "event=DISCONNECT"));

    /* Passwords must never appear in logs even at DEBUG level */
    assert_null(strstr(s_mem.buf, "UltraSecret#99"));
    assert_null(strstr(s_mem.buf, "WrongSecret#00"));
    assert_non_null(strstr(s_mem.buf, "proto=REGISTER|dave|***"));
    assert_non_null(strstr(s_mem.buf, "proto=LOGIN|dave|***"));
}

int run_session_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_session_login_and_single_account_rule),
        cmocka_unit_test(test_session_logout_and_disconnect_relogin),
        cmocka_unit_test(test_session_idle_timeout_and_ping_refresh),
        cmocka_unit_test(test_session_logging_and_password_masking),
    };
    return cmocka_run_group_tests_name("session", tests, NULL, NULL);
}
