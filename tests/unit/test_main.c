/* tests/unit/test_main.c — Điểm chạy toàn bộ unit test với cmocka */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include "netsketch.h"

static void test_sanity_check(void **state)
{
    (void)state;
    assert_null(ns_connect("127.0.0.1", 9000));
    assert_int_equal(ns_send_line(NULL, "PING"), -1);
}

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sanity_check),
    };

    return cmocka_run_group_tests(tests, NULL, NULL);
}
