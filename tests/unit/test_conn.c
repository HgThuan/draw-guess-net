#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "clock.h"
#include "conn.h"
#include "rng.h"

static ConnTable g_tbl;

static void test_clock_real_and_fake(void **state)
{
    (void)state;
    Clock clk;

    clock_init_real(NULL);
    clock_init_fake(NULL, 0);
    clock_advance(NULL, 10);
    assert_int_equal(clock_now_ms(NULL), 0);

    clock_init_real(&clk);
    int64_t t1 = clock_now_ms(&clk);
    clock_advance(&clk, 5000);
    int64_t t2 = clock_now_ms(&clk);
    assert_true(t1 > 0);
    assert_true(t2 >= t1);

    clock_init_fake(&clk, 1000);
    assert_int_equal(clock_now_ms(&clk), 1000);
    clock_advance(&clk, 250);
    assert_int_equal(clock_now_ms(&clk), 1250);
}

static void test_rng_deterministic_and_helpers(void **state)
{
    (void)state;
    Rng r1, r2, r_real;

    rng_init_real(NULL);
    rng_init_fake(NULL, 0);
    assert_int_equal(rng_next_u32(NULL), 0);
    assert_int_equal(rng_range(NULL, 5, 10), 5);

    rng_init_real(&r_real);
    (void)rng_next_u32(&r_real);

    rng_init_fake(&r1, 0);
    rng_init_fake(&r2, 0);
    assert_int_equal(rng_next_u32(&r1), rng_next_u32(&r2));

    rng_init_fake(&r1, 42);
    rng_init_fake(&r2, 42);
    for (int i = 0; i < 20; i++) {
        int v1 = rng_range(&r1, 10, 20);
        int v2 = rng_range(&r2, 10, 20);
        assert_int_equal(v1, v2);
        assert_true(v1 >= 10 && v1 <= 20);
    }
    assert_int_equal(rng_range(&r1, 7, 7), 7);
    assert_int_equal(rng_range(&r1, 9, 3), 9);

    uint8_t b1[16], b2[16];
    rng_bytes(&r1, b1, sizeof(b1));
    rng_bytes(&r2, b2, sizeof(b2));
    assert_memory_equal(b1, b2, sizeof(b1));
    rng_bytes(&r1, NULL, 4);

    char hex1[17], hex2[17];
    rng_hex(&r1, hex1, 8);
    rng_hex(&r2, hex2, 8);
    assert_int_equal(strlen(hex1), 16);
    assert_string_equal(hex1, hex2);
    rng_hex(&r1, NULL, 8);
}

static void test_conn_alloc_free_and_generation(void **state)
{
    (void)state;
    conn_table_init(NULL, 4);
    conn_table_init(&g_tbl, 2);

    assert_null(conn_alloc(NULL, 10, 100));
    ConnId bad_id = conn_id(NULL);
    assert_int_equal(bad_id.idx, CONN_IDX_NONE);
    assert_null(conn_get(&g_tbl, bad_id));
    assert_null(conn_get(NULL, bad_id));

    Conn *c1 = conn_alloc(&g_tbl, 10, 1000);
    Conn *c2 = conn_alloc(&g_tbl, 11, 1000);
    assert_non_null(c1);
    assert_non_null(c2);
    assert_null(conn_alloc(&g_tbl, 12, 1000));

    ConnId id1 = conn_id(c1);
    assert_ptr_equal(conn_get(&g_tbl, id1), c1);

    conn_free(&g_tbl, c1);
    conn_free(&g_tbl, c1);
    assert_null(conn_get(&g_tbl, id1));

    Conn *c1_reused = conn_alloc(&g_tbl, 13, 2000);
    assert_ptr_equal(c1_reused, c1);
    assert_null(conn_get(&g_tbl, id1));
    assert_ptr_equal(conn_get(&g_tbl, conn_id(c1_reused)), c1_reused);

    c1_reused->gen = 0xFFFFu;
    conn_free(&g_tbl, c1_reused);
    assert_int_equal(c1_reused->gen, 1);

    conn_table_init(&g_tbl, 0);
    assert_int_equal(g_tbl.max_clients, MAX_CLIENTS);
}

static void test_conn_send_coalesce_and_overflow(void **state)
{
    (void)state;
    conn_table_init(&g_tbl, 4);
    Conn *c = conn_alloc(&g_tbl, -1, 0);
    assert_non_null(c);

    assert_false(conn_send_raw(NULL, c, "A", 1));
    assert_false(conn_send_line(&g_tbl, c, NULL));
    assert_false(conn_send_line(NULL, c, "PING"));
    assert_false(conn_sendf(NULL, c, "OK"));
    assert_true(conn_send_raw(&g_tbl, c, "X", 0));

    assert_true(conn_send_line(&g_tbl, c, "HELLO|netsketch|1"));
    assert_true(conn_send_line(&g_tbl, c, "PONG\n"));
    assert_true(conn_sendf(&g_tbl, c, "OK|%d", 42));

    assert_int_equal(g_tbl.ndirty, 1);
    assert_int_equal(g_tbl.dirty[0], c->idx);
    assert_int_equal(c->out_len, 29);
    assert_memory_equal(c->out, "HELLO|netsketch|1\nPONG\nOK|42\n", 29);

    conn_consume_out(NULL, 5);
    conn_consume_out(c, 0);
    conn_consume_out(c, 18);
    assert_int_equal(c->out_len, 11);
    assert_memory_equal(c->out, "PONG\nOK|42\n", 11);

    conn_consume_out(c, 100);
    assert_int_equal(c->out_len, 0);
    conn_clear_dirty(NULL);
    conn_clear_dirty(&g_tbl);
    assert_int_equal(g_tbl.ndirty, 0);
    assert_false(c->in_dirty);

    char big[1024];
    memset(big, 'X', sizeof(big));
    for (int i = 0; i < (OUT_BUF / (int)sizeof(big)); i++) {
        assert_true(conn_send_raw(&g_tbl, c, big, sizeof(big)));
    }
    assert_false(conn_send_raw(&g_tbl, c, "overflow", 8));
    assert_true(c->overflow);
    assert_true(c->closing);

    c->out_len = OUT_BUF - 2;
    c->overflow = false;
    c->closing = false;
    assert_false(conn_send_line(&g_tbl, c, "TOO_BIG"));
    assert_true(c->overflow);
    assert_true(c->closing);
}

int run_conn_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_clock_real_and_fake),
        cmocka_unit_test(test_rng_deterministic_and_helpers),
        cmocka_unit_test(test_conn_alloc_free_and_generation),
        cmocka_unit_test(test_conn_send_coalesce_and_overflow),
    };
    return cmocka_run_group_tests_name("conn", tests, NULL, NULL);
}
