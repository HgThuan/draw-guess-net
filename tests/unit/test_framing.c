#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "framing.h"

static void test_framing_single_complete_line(void **state)
{
    (void)state;
    LineBuf lb;
    char out[FRAME_MAX_LINE];
    size_t consumed = 0;

    linebuf_init(&lb);
    FrameStatus st = linebuf_feed(&lb, "PING\n", 5, &consumed, out, sizeof(out));
    assert_int_equal(st, FRAME_OK);
    assert_int_equal(consumed, 5);
    assert_string_equal(out, "PING");
}

static void test_framing_split_across_two_feeds(void **state)
{
    (void)state;
    LineBuf lb;
    char out[FRAME_MAX_LINE];
    size_t consumed = 0;

    linebuf_init(&lb);
    assert_int_equal(linebuf_feed(&lb, "REGISTER|al", 11, &consumed, out, sizeof(out)),
                     FRAME_NEED_MORE);
    assert_int_equal(consumed, 11);

    assert_int_equal(linebuf_feed(&lb, "ice|secret1\n", 12, &consumed, out, sizeof(out)),
                     FRAME_OK);
    assert_int_equal(consumed, 12);
    assert_string_equal(out, "REGISTER|alice|secret1");
}

static void test_framing_multiple_lines_in_one_feed(void **state)
{
    (void)state;
    LineBuf lb;
    char out[FRAME_MAX_LINE];
    const char *raw = "PING\nLOGIN|alice|pass123\nLOGOUT\n";
    size_t len = strlen(raw);
    size_t offset = 0, consumed = 0;

    linebuf_init(&lb);

    assert_int_equal(linebuf_feed(&lb, raw + offset, len - offset, &consumed, out, sizeof(out)),
                     FRAME_OK);
    offset += consumed;
    assert_string_equal(out, "PING");

    assert_int_equal(linebuf_feed(&lb, raw + offset, len - offset, &consumed, out, sizeof(out)),
                     FRAME_OK);
    offset += consumed;
    assert_string_equal(out, "LOGIN|alice|pass123");

    assert_int_equal(linebuf_feed(&lb, raw + offset, len - offset, &consumed, out, sizeof(out)),
                     FRAME_OK);
    offset += consumed;
    assert_string_equal(out, "LOGOUT");
    assert_int_equal(offset, len);
}

static void test_framing_exact_512_bytes_succeeds(void **state)
{
    (void)state;
    LineBuf lb;
    char in[FRAME_MAX_LINE];
    char out[FRAME_MAX_LINE];
    size_t consumed = 0;

    memset(in, 'A', FRAME_MAX_LINE - 1);
    in[FRAME_MAX_LINE - 1] = '\n';

    linebuf_init(&lb);
    assert_int_equal(linebuf_feed(&lb, in, FRAME_MAX_LINE, &consumed, out, sizeof(out)),
                     FRAME_OK);
    assert_int_equal(consumed, FRAME_MAX_LINE);
    assert_int_equal(strlen(out), FRAME_MAX_LINE - 1);
    assert_int_equal(out[0], 'A');
    assert_int_equal(out[FRAME_MAX_LINE - 2], 'A');
}

static void test_framing_513_bytes_returns_too_long_and_recovers(void **state)
{
    (void)state;
    LineBuf lb;
    char in[FRAME_MAX_LINE + 16];
    char out[FRAME_MAX_LINE];
    size_t consumed = 0;

    memset(in, 'B', FRAME_MAX_LINE);
    in[FRAME_MAX_LINE] = '\n';
    memcpy(in + FRAME_MAX_LINE + 1, "PING\n", 5);
    size_t total = FRAME_MAX_LINE + 1 + 5;

    linebuf_init(&lb);
    assert_int_equal(linebuf_feed(&lb, in, total, &consumed, out, sizeof(out)),
                     FRAME_TOO_LONG);
    assert_int_equal(consumed, FRAME_MAX_LINE + 1);

    size_t consumed2 = 0;
    assert_int_equal(linebuf_feed(&lb, in + consumed, total - consumed, &consumed2, out, sizeof(out)),
                     FRAME_OK);
    assert_int_equal(consumed2, 5);
    assert_string_equal(out, "PING");
}

static void test_framing_crlf_and_empty_lines(void **state)
{
    (void)state;
    LineBuf lb;
    char out[FRAME_MAX_LINE];
    size_t consumed = 0;

    linebuf_init(&lb);
    assert_int_equal(linebuf_feed(&lb, "HELLO|netsketch|1\r\n", 19, &consumed, out, sizeof(out)),
                     FRAME_OK);
    assert_int_equal(consumed, 19);
    assert_string_equal(out, "HELLO|netsketch|1");

    assert_int_equal(linebuf_feed(&lb, "\n", 1, &consumed, out, sizeof(out)), FRAME_OK);
    assert_string_equal(out, "");

    assert_int_equal(linebuf_feed(&lb, "\r\n", 2, &consumed, out, sizeof(out)), FRAME_OK);
    assert_string_equal(out, "");
}

static void test_framing_byte_by_byte_and_trailing_partial(void **state)
{
    (void)state;
    LineBuf lb;
    char out[FRAME_MAX_LINE];
    const char *msg = "CHAT|hello\nPARTIAL";
    size_t len = strlen(msg);
    size_t consumed = 0;
    int ok_count = 0;

    linebuf_init(&lb);
    for (size_t i = 0; i < len; i++) {
        FrameStatus st = linebuf_feed(&lb, &msg[i], 1, &consumed, out, sizeof(out));
        assert_int_equal(consumed, 1);
        if (st == FRAME_OK) {
            assert_string_equal(out, "CHAT|hello");
            ok_count++;
        } else {
            assert_int_equal(st, FRAME_NEED_MORE);
        }
    }
    assert_int_equal(ok_count, 1);
    assert_int_equal(lb.len, 7);
}

static void test_framing_invalid_args_and_small_out_cap(void **state)
{
    (void)state;
    LineBuf lb;
    char out[4];
    size_t consumed = 99;

    linebuf_init(NULL);
    linebuf_init(&lb);

    assert_int_equal(linebuf_feed(NULL, "A\n", 2, &consumed, out, sizeof(out)), FRAME_TOO_LONG);
    assert_int_equal(consumed, 0);
    assert_int_equal(linebuf_feed(&lb, NULL, 2, &consumed, out, sizeof(out)), FRAME_TOO_LONG);
    assert_int_equal(linebuf_feed(&lb, "A\n", 2, NULL, out, sizeof(out)), FRAME_TOO_LONG);
    assert_int_equal(linebuf_feed(&lb, "A\n", 2, &consumed, NULL, sizeof(out)), FRAME_TOO_LONG);
    assert_int_equal(linebuf_feed(&lb, "A\n", 2, &consumed, out, 0), FRAME_TOO_LONG);

    assert_int_equal(linebuf_feed(&lb, "PING\n", 5, &consumed, out, sizeof(out)), FRAME_TOO_LONG);
    assert_int_equal(consumed, 5);
}

static void test_framing_fuzz_deterministic_seed(void **state)
{
    (void)state;
    LineBuf lb;
    char out[FRAME_MAX_LINE];
    uint32_t seed = 0x12345678u;
    char chunk[256];

    linebuf_init(&lb);
    for (int iter = 0; iter < 200; iter++) {
        size_t chunk_len = (seed % sizeof(chunk)) + 1;
        for (size_t i = 0; i < chunk_len; i++) {
            seed = seed * 1664525u + 1013904223u;
            uint8_t r = (uint8_t)(seed >> 24);
            chunk[i] = (r < 8) ? '\n' : (r < 12) ? '\r' : (char)r;
        }
        size_t off = 0;
        while (off < chunk_len) {
            size_t consumed = 0;
            FrameStatus st = linebuf_feed(&lb, chunk + off, chunk_len - off,
                                          &consumed, out, sizeof(out));
            assert_true(consumed > 0);
            assert_true(st == FRAME_NEED_MORE || st == FRAME_OK || st == FRAME_TOO_LONG);
            off += consumed;
        }
    }
}

int run_framing_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_framing_single_complete_line),
        cmocka_unit_test(test_framing_split_across_two_feeds),
        cmocka_unit_test(test_framing_multiple_lines_in_one_feed),
        cmocka_unit_test(test_framing_exact_512_bytes_succeeds),
        cmocka_unit_test(test_framing_513_bytes_returns_too_long_and_recovers),
        cmocka_unit_test(test_framing_crlf_and_empty_lines),
        cmocka_unit_test(test_framing_byte_by_byte_and_trailing_partial),
        cmocka_unit_test(test_framing_invalid_args_and_small_out_cap),
        cmocka_unit_test(test_framing_fuzz_deterministic_seed),
    };
    return cmocka_run_group_tests_name("framing", tests, NULL, NULL);
}
