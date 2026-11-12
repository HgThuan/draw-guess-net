#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>

#include "server.h"

/* Server is ~35 MB due to ConnTable[MAX_CLIENTS]; keep out of test stack frames. */
static Server s_srv;

static void test_config_defaults(void **state)
{
    Config cfg;
    char *argv_only_prog[] = {"netsketch-server"};
    (void)state;

    config_init_defaults(NULL);

    config_init_defaults(&cfg);
    assert_int_equal(cfg.port, DEFAULT_PORT);
    assert_string_equal(cfg.data_dir, ".");
    assert_string_equal(cfg.words_path, "data/words.txt");
    assert_int_equal(cfg.choose_time_s, DEFAULT_CHOOSE_TIME);
    assert_int_equal(cfg.turn_pause_s, DEFAULT_TURN_PAUSE);
    assert_int_equal(cfg.idle_timeout_s, DEFAULT_IDLE_TIMEOUT);
    assert_int_equal(cfg.max_clients, MAX_CLIENTS);
    assert_false(cfg.verbose);

    assert_true(config_parse_args(&cfg, 1, argv_only_prog));
    assert_int_equal(cfg.port, DEFAULT_PORT);
}

static void test_config_parse_valid(void **state)
{
    Config cfg;
    char *argv[] = {
        "netsketch-server",
        "--port", "8080",
        "--data-dir", "/tmp/ns",
        "--words", "tests/data/words_small.txt",
        "--choose-time", "20",
        "--turn-pause", "3",
        "--idle-timeout", "60",
        "--max-clients", "128",
        "--verbose"
    };
    int argc = (int)(sizeof(argv) / sizeof(argv[0]));
    (void)state;

    assert_true(config_parse_args(&cfg, argc, argv));
    assert_int_equal(cfg.port, 8080);
    assert_string_equal(cfg.data_dir, "/tmp/ns");
    assert_string_equal(cfg.words_path, "tests/data/words_small.txt");
    assert_int_equal(cfg.choose_time_s, 20);
    assert_int_equal(cfg.turn_pause_s, 3);
    assert_int_equal(cfg.idle_timeout_s, 60);
    assert_int_equal(cfg.max_clients, 128);
    assert_true(cfg.verbose);
}

static void test_config_parse_invalid(void **state)
{
    Config cfg;
    char *argv_null_elem[]   = {"srv", NULL};
    char *argv_missing_val[] = {"srv", "--port"};
    char *argv_unknown[]     = {"srv", "--bad-flag", "1"};
    char *argv_port_low[]    = {"srv", "--port", "0"};
    char *argv_port_high[]   = {"srv", "--port", "65536"};
    char *argv_port_nan[]    = {"srv", "--port", "abc"};
    char *argv_empty_dir[]   = {"srv", "--data-dir", ""};
    char *argv_empty_words[] = {"srv", "--words", ""};
    char *argv_choose_zero[] = {"srv", "--choose-time", "0"};
    char *argv_pause_zero[]  = {"srv", "--turn-pause", "-1"};
    char *argv_idle_zero[]   = {"srv", "--idle-timeout", "0"};
    char *argv_max_zero[]    = {"srv", "--max-clients", "0"};
    char *argv_max_over[]    = {"srv", "--max-clients", "2049"};
    (void)state;

    assert_false(config_parse_args(NULL, 1, argv_missing_val));
    assert_false(config_parse_args(&cfg, 0, argv_missing_val));
    assert_false(config_parse_args(&cfg, 1, NULL));
    assert_false(config_parse_args(&cfg, 2, argv_null_elem));
    assert_false(config_parse_args(&cfg, 2, argv_missing_val));
    assert_false(config_parse_args(&cfg, 3, argv_unknown));
    assert_false(config_parse_args(&cfg, 3, argv_port_low));
    assert_false(config_parse_args(&cfg, 3, argv_port_high));
    assert_false(config_parse_args(&cfg, 3, argv_port_nan));
    assert_false(config_parse_args(&cfg, 3, argv_empty_dir));
    assert_false(config_parse_args(&cfg, 3, argv_empty_words));
    assert_false(config_parse_args(&cfg, 3, argv_choose_zero));
    assert_false(config_parse_args(&cfg, 3, argv_pause_zero));
    assert_false(config_parse_args(&cfg, 3, argv_idle_zero));
    assert_false(config_parse_args(&cfg, 3, argv_max_zero));
    assert_false(config_parse_args(&cfg, 3, argv_max_over));
}

static void test_server_init(void **state)
{
    Config cfg;
    (void)state;

    server_init(NULL, NULL);

    server_init(&s_srv, NULL);
    assert_int_equal(s_srv.cfg.port, DEFAULT_PORT);
    assert_int_equal(s_srv.conns.max_clients, MAX_CLIENTS);
    assert_int_equal(s_srv.conns.active_count, 0);
    assert_false(s_srv.stop);
    assert_true(clock_now_ms(&s_srv.clock) > 0);

    config_init_defaults(&cfg);
    cfg.port = 9123;
    cfg.max_clients = 32;
    server_init(&s_srv, &cfg);
    assert_int_equal(s_srv.cfg.port, 9123);
    assert_int_equal(s_srv.conns.max_clients, 32);
    assert_false(s_srv.stop);
}

int run_server_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_config_defaults),
        cmocka_unit_test(test_config_parse_valid),
        cmocka_unit_test(test_config_parse_invalid),
        cmocka_unit_test(test_server_init),
    };
    return cmocka_run_group_tests_name("server", tests, NULL, NULL);
}
