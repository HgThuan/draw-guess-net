#define _POSIX_C_SOURCE 200809L

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>

#include "logger.h"

static void test_logger_format_and_level_filter(void **state)
{
    Logger lg;
    LogMemSink mem;
    (void)state;

    logger_init_mem(&lg, &mem, LOG_INFO);

    /* Filtered out because DEBUG < INFO */
    logger_log(&lg, LOG_DEBUG, 0, "sid=1 event=IGNORED");
    assert_int_equal(mem.len, 0);

    /* Logged at epoch 0 -> 1970-01-01T00:00:00.000Z */
    logger_log(&lg, LOG_INFO, 0, "sid=9f3a1c2b room=1 event=GUESS_CORRECT user=carol points=%d", 400);
    assert_string_equal(
        mem.buf,
        "1970-01-01T00:00:00.000Z INFO sid=9f3a1c2b room=1 event=GUESS_CORRECT user=carol points=400\n"
    );

    logger_log(&lg, LOG_WARN, -5, "event=WARN_TEST");
    logger_log(&lg, LOG_ERROR, 1234, "event=ERR_TEST");
    assert_non_null(strstr(mem.buf, "WARN event=WARN_TEST\n"));
    assert_non_null(strstr(mem.buf, "1970-01-01T00:00:01.234Z ERROR event=ERR_TEST\n"));

    logger_close(&lg);
}

static void test_logger_masks_passwords_in_debug(void **state)
{
    Logger lg;
    LogMemSink mem;
    (void)state;

    /* At INFO level, proto lines are not logged */
    logger_init_mem(&lg, &mem, LOG_INFO);
    logger_log_proto(&lg, 0, "abcd", "LOGIN|alice|TopSecretPass99");
    assert_int_equal(mem.len, 0);

    /* At DEBUG level (--verbose), proto lines are logged with passwords masked */
    logger_init_mem(&lg, &mem, LOG_DEBUG);
    logger_log_proto(&lg, 0, NULL, "REGISTER|alice|MySuperSecret123");
    logger_log_proto(&lg, 0, "deadbeef01234567", "LOGIN|bob|AnotherSecret456");
    logger_log_proto(&lg, 0, "deadbeef01234567", "PING");

    assert_null(strstr(mem.buf, "MySuperSecret123"));
    assert_null(strstr(mem.buf, "AnotherSecret456"));
    assert_non_null(strstr(mem.buf, "sid=- proto=REGISTER|alice|***\n"));
    assert_non_null(strstr(mem.buf, "sid=deadbeef01234567 proto=LOGIN|bob|***\n"));
    assert_non_null(strstr(mem.buf, "sid=deadbeef01234567 proto=PING\n"));
}

static void test_logger_file_sink_and_tick_flush(void **state)
{
    Logger lg;
    FILE *fp = tmpfile();
    char buf[256];
    (void)state;

    assert_non_null(fp);
    logger_init_file(&lg, fp, LOG_INFO);
    logger_log(&lg, LOG_INFO, 500, "event=FILE_TEST");

    /* Tick < 1000ms does not update last_flush_ms */
    logger_tick(&lg, 500);
    assert_int_equal(lg.last_flush_ms, 0);

    /* Tick >= 1000ms flushes and updates last_flush_ms */
    logger_tick(&lg, 1000);
    assert_int_equal(lg.last_flush_ms, 1000);

    rewind(fp);
    memset(buf, 0, sizeof(buf));
    assert_true(fread(buf, 1, sizeof(buf) - 1, fp) > 0);
    assert_non_null(strstr(buf, "INFO event=FILE_TEST\n"));

    logger_close(&lg);
    fclose(fp);

    /* Null safety */
    logger_init_file(NULL, NULL, LOG_INFO);
    logger_init_mem(NULL, NULL, LOG_INFO);
    logger_log(NULL, LOG_INFO, 0, "x");
    logger_log_proto(NULL, 0, NULL, "PING");
    logger_flush(NULL);
    logger_tick(NULL, 0);
    logger_close(NULL);
}

int run_logger_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_logger_format_and_level_filter),
        cmocka_unit_test(test_logger_masks_passwords_in_debug),
        cmocka_unit_test(test_logger_file_sink_and_tick_flush),
    };
    return cmocka_run_group_tests_name("logger", tests, NULL, NULL);
}
