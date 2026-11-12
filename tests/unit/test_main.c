/* tests/unit/test_main.c — Điểm chạy toàn bộ unit test với cmocka */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include "netsketch.h"

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
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sanity_check),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
