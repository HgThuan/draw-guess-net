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

static void test_sanity_check(void **state)
{
    char buf[16];
    (void)state;
    assert_null(ns_connect("127.0.0.1", 9000));
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
    return rc;
}
