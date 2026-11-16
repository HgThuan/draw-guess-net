#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include "scoring.h"

static void test_scoring_guess_examples_and_bounds(void **state)
{
    (void)state;

    /* Standard examples with draw_time = 60 */
    assert_int_equal(scoring_guess(1, 60, 60), 500);
    assert_int_equal(scoring_guess(1, 45, 60), 400);
    assert_int_equal(scoring_guess(2, 30, 60), 275);
    assert_int_equal(scoring_guess(3, 0, 60), 50);

    /* Floor of 50 even for late/high-rank guessers */
    assert_int_equal(scoring_guess(10, 0, 60), 50);

    /* Large valid values without 32-bit overflow */
    assert_int_equal(scoring_guess(1, 10000000, 10000000), 500);

    /* Invalid inputs return 0 */
    assert_int_equal(scoring_guess(0, 30, 60), 0);
    assert_int_equal(scoring_guess(-1, 30, 60), 0);
    assert_int_equal(scoring_guess(1, -1, 60), 0);
    assert_int_equal(scoring_guess(1, 61, 60), 0);
    assert_int_equal(scoring_guess(1, 30, 0), 0);
    assert_int_equal(scoring_guess(1, 30, -60), 0);
}

static void test_scoring_drawer_examples_and_bounds(void **state)
{
    (void)state;

    /* 3 players in room, two guessers got 400 and 275 -> (400 + 275) / 2 = 337 */
    assert_int_equal(scoring_drawer(400 + 275, 3), 337);

    /* 2 players in room -> drawer gets full guesser points */
    assert_int_equal(scoring_drawer(400, 2), 400);

    /* Nobody guessed -> 0 */
    assert_int_equal(scoring_drawer(0, 4), 0);

    /* Invalid inputs -> 0 */
    assert_int_equal(scoring_drawer(-10, 3), 0);
    assert_int_equal(scoring_drawer(400, 1), 0);
    assert_int_equal(scoring_drawer(400, 0), 0);
}

int run_scoring_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_scoring_guess_examples_and_bounds),
        cmocka_unit_test(test_scoring_drawer_examples_and_bounds),
    };
    return cmocka_run_group_tests_name("scoring", tests, NULL, NULL);
}
