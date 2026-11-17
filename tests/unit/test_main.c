#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include "netsketch.h"

int run_framing_tests(void);
int run_protocol_tests(void);
int run_conn_tests(void);
int run_server_tests(void);
int run_net_loop_tests(void);
int run_dispatch_tests(void);
int run_sha256_tests(void);
int run_logger_tests(void);
int run_auth_tests(void);
int run_session_tests(void);
int run_room_tests(void);
int run_client_lib_tests(void);
int run_words_tests(void);
int run_scoring_tests(void);
int run_game_tests(void);

static void test_sanity_check(void **state)
{
    char buf[16];
    (void)state;
    assert_null(ns_connect("127.0.0.1", 1));
    assert_int_equal(ns_send_line(NULL, "PING"), -1);
    assert_int_equal(ns_recv_line(NULL, buf, sizeof(buf), 0), -1);
    ns_close(NULL);
}

int main(void)
{
    const struct CMUnitTest sanity_tests[] = {
        cmocka_unit_test(test_sanity_check),
    };

    int rc = 0;
    rc |= cmocka_run_group_tests_name("sanity", sanity_tests, NULL, NULL);
    rc |= run_framing_tests();
    rc |= run_protocol_tests();
    rc |= run_conn_tests();
    rc |= run_server_tests();
    rc |= run_net_loop_tests();
    rc |= run_dispatch_tests();
    rc |= run_sha256_tests();
    rc |= run_logger_tests();
    rc |= run_auth_tests();
    rc |= run_session_tests();
    rc |= run_room_tests();
    rc |= run_client_lib_tests();
    rc |= run_words_tests();
    rc |= run_scoring_tests();
    rc |= run_game_tests();
    return rc;
}
