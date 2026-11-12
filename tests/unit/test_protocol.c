#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "protocol.h"

static void test_protocol_all_valid_commands(void **state)
{
    (void)state;
    Command cmd;

    assert_int_equal(protocol_parse("PING", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_PING);
    assert_int_equal(cmd.argc, 0);

    assert_int_equal(protocol_parse("REGISTER|alice|secret1", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_REGISTER);
    assert_int_equal(cmd.argc, 2);
    assert_string_equal(cmd.argv[0], "alice");
    assert_string_equal(cmd.argv[1], "secret1");

    assert_int_equal(protocol_parse("LOGIN|bob|pass123", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_LOGIN);
    assert_int_equal(cmd.argc, 2);
    assert_string_equal(cmd.argv[0], "bob");
    assert_string_equal(cmd.argv[1], "pass123");

    assert_int_equal(protocol_parse("LOGOUT", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_LOGOUT);
    assert_int_equal(cmd.argc, 0);

    assert_int_equal(protocol_parse("CREATE_ROOM|Phong Vui|4|3|60", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_CREATE_ROOM);
    assert_int_equal(cmd.argc, 4);
    assert_string_equal(cmd.argv[0], "Phong Vui");
    assert_string_equal(cmd.argv[1], "4");
    assert_string_equal(cmd.argv[2], "3");
    assert_string_equal(cmd.argv[3], "60");

    assert_int_equal(protocol_parse("LIST_ROOMS|1", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_LIST_ROOMS);
    assert_int_equal(cmd.argc, 1);
    assert_string_equal(cmd.argv[0], "1");

    assert_int_equal(protocol_parse("JOIN_ROOM|12", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_JOIN_ROOM);
    assert_int_equal(cmd.argc, 1);
    assert_string_equal(cmd.argv[0], "12");

    assert_int_equal(protocol_parse("LEAVE_ROOM", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_LEAVE_ROOM);

    assert_int_equal(protocol_parse("START", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_START);

    assert_int_equal(protocol_parse("SCOREBOARD", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_SCOREBOARD);

    assert_int_equal(protocol_parse("CHOOSE_WORD|2", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_CHOOSE_WORD);
    assert_string_equal(cmd.argv[0], "2");

    assert_int_equal(protocol_parse("STROKE|FF0000|4|100,100,200,150", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_STROKE);
    assert_int_equal(cmd.argc, 3);
    assert_string_equal(cmd.argv[0], "FF0000");
    assert_string_equal(cmd.argv[1], "4");
    assert_string_equal(cmd.argv[2], "100,100,200,150");

    assert_int_equal(protocol_parse("CLEAR", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_CLEAR);

    assert_int_equal(protocol_parse("GUESS|ice cream", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_GUESS);
    assert_int_equal(cmd.argc, 1);
    assert_string_equal(cmd.argv[0], "ice cream");

    assert_int_equal(protocol_parse("CHAT|xin chao", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_CHAT);
    assert_int_equal(cmd.argc, 1);
    assert_string_equal(cmd.argv[0], "xin chao");
}

static void test_protocol_free_text_preserves_pipes(void **state)
{
    (void)state;
    Command cmd;

    assert_int_equal(protocol_parse("CHAT|a|b|c|d", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_CHAT);
    assert_int_equal(cmd.argc, 1);
    assert_string_equal(cmd.argv[0], "a|b|c|d");

    assert_int_equal(protocol_parse("GUESS|cat | dog", &cmd), ERR_OK);
    assert_int_equal(cmd.type, CMD_GUESS);
    assert_string_equal(cmd.argv[0], "cat | dog");
}

static void test_protocol_unknown_and_case_sensitive(void **state)
{
    (void)state;
    Command cmd;

    assert_int_equal(protocol_parse("", &cmd), ERR_UNKNOWN_CMD);
    assert_int_equal(protocol_parse("FOOBAR", &cmd), ERR_UNKNOWN_CMD);
    assert_int_equal(protocol_parse("FOOBAR|1|2", &cmd), ERR_UNKNOWN_CMD);
    assert_int_equal(protocol_parse("ping", &cmd), ERR_UNKNOWN_CMD);
    assert_int_equal(protocol_parse("Login|alice|pass", &cmd), ERR_UNKNOWN_CMD);
}

static void test_protocol_wrong_arg_counts(void **state)
{
    (void)state;
    Command cmd;

    assert_int_equal(protocol_parse("PING|", &cmd), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("PING|1", &cmd), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("LOGOUT|now", &cmd), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("LIST_ROOMS", &cmd), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("LIST_ROOMS|1|2", &cmd), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("LOGIN|alice", &cmd), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("LOGIN|alice|pass|extra", &cmd), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("CREATE_ROOM|r|4|3", &cmd), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("CREATE_ROOM|r|4|3|60|x", &cmd), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("GUESS", &cmd), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("CHAT", &cmd), ERR_BAD_FORMAT);
}

static void test_protocol_control_chars_and_bounds(void **state)
{
    (void)state;
    Command cmd;
    char long_line[FRAME_MAX_LINE + 4];

    assert_int_equal(protocol_parse(NULL, &cmd), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("PING", NULL), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("CHAT|hello\tworld", &cmd), ERR_BAD_FORMAT);
    assert_int_equal(protocol_parse("CHAT|hello\x7fworld", &cmd), ERR_BAD_FORMAT);

    memset(long_line, 'A', FRAME_MAX_LINE);
    long_line[FRAME_MAX_LINE] = '\0';
    assert_int_equal(protocol_parse(long_line, &cmd), ERR_LINE_TOO_LONG);
}

static void test_protocol_parse_int_cases(void **state)
{
    (void)state;
    int v = -1;

    assert_true(protocol_parse_int("0", &v));
    assert_int_equal(v, 0);
    assert_true(protocol_parse_int("1000", &v));
    assert_int_equal(v, 1000);
    assert_true(protocol_parse_int("2147483647", &v));
    assert_int_equal(v, 2147483647);

    assert_false(protocol_parse_int(NULL, &v));
    assert_false(protocol_parse_int("10", NULL));
    assert_false(protocol_parse_int("", &v));
    assert_false(protocol_parse_int("-1", &v));
    assert_false(protocol_parse_int("+5", &v));
    assert_false(protocol_parse_int(" 12", &v));
    assert_false(protocol_parse_int("12a", &v));
    assert_false(protocol_parse_int("2147483648", &v));
}

static void test_protocol_error_messages_and_formatting(void **state)
{
    (void)state;
    static const int codes[] = {
        ERR_BAD_FORMAT, ERR_NOT_LOGGED_IN, ERR_ALREADY_LOGGED_IN,
        ERR_UNKNOWN_CMD, ERR_LINE_TOO_LONG, ERR_USER_EXISTS,
        ERR_BAD_CREDENTIALS, ERR_INVALID_USERNAME, ERR_INVALID_PASSWORD,
        ERR_ACCOUNT_IN_USE, ERR_ROOM_NOT_FOUND, ERR_ALREADY_IN_ROOM,
        ERR_ROOM_FULL, ERR_ROOM_IN_GAME, ERR_NOT_IN_ROOM,
        ERR_INVALID_ROOM_PARAMS, ERR_NOT_HOST, ERR_NOT_ENOUGH_PLAYERS,
        ERR_ROOM_LIMIT, ERR_WRONG_STATE, ERR_NOT_DRAWER, ERR_IS_DRAWER,
        ERR_ALREADY_GUESSED, ERR_INVALID_WORD_CHOICE, ERR_INVALID_STROKE,
        ERR_CHAT_BLOCKED, ERR_INTERNAL, ERR_SERVER_FULL, 999
    };

    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        assert_non_null(protocol_err_msg(codes[i]));
    }

    char out[64];
    assert_true(protocol_format_err(out, sizeof(out), ERR_BAD_CREDENTIALS) > 0);
    assert_string_equal(out, "ERR|111|bad credentials\n");

    assert_int_equal(protocol_format_err(NULL, sizeof(out), ERR_BAD_FORMAT), -1);
    assert_int_equal(protocol_format_err(out, 0, ERR_BAD_FORMAT), -1);
    assert_int_equal(protocol_format_err(out, 6, ERR_BAD_FORMAT), -1);
}

static void test_protocol_fuzz_deterministic_seed(void **state)
{
    (void)state;
    Command cmd;
    uint32_t seed = 0x87654321u;
    char buf[300];

    for (int iter = 0; iter < 300; iter++) {
        size_t len = seed % (sizeof(buf) - 1);
        for (size_t i = 0; i < len; i++) {
            seed = seed * 1664525u + 1013904223u;
            uint8_t r = (uint8_t)(seed >> 24);
            buf[i] = (r == 0) ? '|' : (char)r;
        }
        buf[len] = '\0';
        int rc = protocol_parse(buf, &cmd);
        assert_true(rc == ERR_OK || rc == ERR_BAD_FORMAT ||
                    rc == ERR_UNKNOWN_CMD || rc == ERR_LINE_TOO_LONG);
    }
}

int run_protocol_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_protocol_all_valid_commands),
        cmocka_unit_test(test_protocol_free_text_preserves_pipes),
        cmocka_unit_test(test_protocol_unknown_and_case_sensitive),
        cmocka_unit_test(test_protocol_wrong_arg_counts),
        cmocka_unit_test(test_protocol_control_chars_and_bounds),
        cmocka_unit_test(test_protocol_parse_int_cases),
        cmocka_unit_test(test_protocol_error_messages_and_formatting),
        cmocka_unit_test(test_protocol_fuzz_deterministic_seed),
    };
    return cmocka_run_group_tests_name("protocol", tests, NULL, NULL);
}
