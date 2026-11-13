#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>

#include "auth.h"
#include "protocol.h"

static UserStore s_us1;
static UserStore s_us2;

static void test_auth_username_and_password_bounds(void **state)
{
    (void)state;

    assert_false(auth_valid_username(NULL));
    assert_false(auth_valid_username(""));
    assert_false(auth_valid_username("ab"));
    assert_true(auth_valid_username("abc"));
    assert_true(auth_valid_username("Alice_0123456789"));
    assert_false(auth_valid_username("Alice_01234567890"));
    assert_false(auth_valid_username("bad name"));
    assert_false(auth_valid_username("bad-name"));
    assert_false(auth_valid_username("bad|name"));

    assert_false(auth_valid_password(NULL));
    assert_false(auth_valid_password("12345"));
    assert_true(auth_valid_password("123456"));
    assert_true(auth_valid_password("12345678901234567890123456789012"));
    assert_false(auth_valid_password("123456789012345678901234567890123"));
    assert_false(auth_valid_password("has space"));
    assert_false(auth_valid_password("has|pipe"));
    assert_false(auth_valid_password("has\tctrl"));
    assert_false(auth_valid_password("has\x7f" "del"));
}

static void test_auth_register_and_verify_memory(void **state)
{
    Rng rng;
    User *u = NULL;
    (void)state;

    rng_init_fake(&rng, 42);
    user_store_init(&s_us1, NULL);
    assert_int_equal(user_store_load(&s_us1), 0);

    assert_int_equal(auth_register(&s_us1, &rng, "ab", "secret1"), ERR_INVALID_USERNAME);
    assert_int_equal(auth_register(&s_us1, &rng, "alice", "12345"), ERR_INVALID_PASSWORD);

    assert_int_equal(auth_register(&s_us1, &rng, "alice", "secret1"), ERR_OK);
    assert_int_equal(auth_register(&s_us1, &rng, "alice", "other12"), ERR_USER_EXISTS);
    /* Case-sensitive usernames */
    assert_int_equal(auth_register(&s_us1, &rng, "Alice", "secret2"), ERR_OK);

    assert_int_equal(auth_verify(&s_us1, "alice", "secret1", &u), ERR_OK);
    assert_non_null(u);
    assert_string_equal(u->name, "alice");
    assert_int_equal(u->online.idx, CONN_IDX_NONE);

    assert_int_equal(auth_verify(&s_us1, "alice", "wrongpw", &u), ERR_BAD_CREDENTIALS);
    assert_null(u);
    assert_int_equal(auth_verify(&s_us1, "unknown", "secret1", &u), ERR_BAD_CREDENTIALS);
    assert_null(u);
}

static void test_auth_persistence_and_no_raw_password(void **state)
{
    Rng rng;
    User *u = NULL;
    char raw_file[1024];
    FILE *fp;
    (void)state;

    rng_init_fake(&rng, 999);
    user_store_init(&s_us1, "build");
    (void)remove(s_us1.file_path);

    /* Loading when file does not exist yet succeeds with 0 users */
    assert_int_equal(user_store_load(&s_us1), 0);

    assert_int_equal(auth_register(&s_us1, &rng, "carol", "MyPlaintextPass!"), ERR_OK);
    assert_int_equal(auth_register(&s_us1, &rng, "dave_1", "SecondPass#99"), ERR_OK);

    fp = fopen(s_us1.file_path, "r");
    assert_non_null(fp);
    memset(raw_file, 0, sizeof(raw_file));
    assert_true(fread(raw_file, 1, sizeof(raw_file) - 1, fp) > 0);
    fclose(fp);

    /* Raw passwords must never appear in users.dat */
    assert_null(strstr(raw_file, "MyPlaintextPass!"));
    assert_null(strstr(raw_file, "SecondPass#99"));
    assert_non_null(strstr(raw_file, "carol:"));
    assert_non_null(strstr(raw_file, "dave_1:"));

    /* Reload into a fresh UserStore and verify credentials */
    user_store_init(&s_us2, "build");
    assert_int_equal(user_store_load(&s_us2), 2);
    assert_int_equal(auth_verify(&s_us2, "carol", "MyPlaintextPass!", &u), ERR_OK);
    assert_non_null(u);
    assert_int_equal(auth_verify(&s_us2, "dave_1", "SecondPass#99", NULL), ERR_OK);
    assert_int_equal(auth_verify(&s_us2, "carol", "WrongPass!", NULL), ERR_BAD_CREDENTIALS);

    (void)remove(s_us1.file_path);
}

static void test_auth_corrupted_file_and_edge_cases(void **state)
{
    Rng rng;
    FILE *fp;
    (void)state;

    rng_init_fake(&rng, 7);
    user_store_init(&s_us1, "build");

    fp = fopen(s_us1.file_path, "w");
    assert_non_null(fp);
    fprintf(fp, "\n\r\n");
    fprintf(fp, "no_colons_at_all\n");
    fprintf(fp, "one_colon:only\n");
    fprintf(fp, "too:many:colons:here\n");
    fprintf(fp, "x:00112233445566778899aabbccddeeff:00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n");
    fprintf(fp, "bob:short_salt:00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n");
    fprintf(fp, "bob:00112233445566778899aabbccddeefg:00112233445566778899aabbccddeeff00112233445566778899aabbccddeeff\n");
    fprintf(fp, "bob:00112233445566778899aabbccddeeff:short_hash\n");
    fprintf(fp, "bob:00112233445566778899AABBCCDDEEFF:00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF\n");
    fprintf(fp, "bob:00112233445566778899AABBCCDDEEFF:00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF\n");
    fclose(fp);

    assert_int_equal(user_store_load(&s_us1), 1);
    assert_non_null(user_store_find(&s_us1, "bob"));

    (void)remove(s_us1.file_path);

    /* Unwritable path and full store */
    user_store_init(&s_us2, "/nonexistent_dir_netsketch_test");
    assert_int_equal(auth_register(&s_us2, &rng, "eve", "secret1"), ERR_INTERNAL);

    user_store_init(&s_us2, NULL);
    s_us2.count = USER_STORE_CAP - 1;
    assert_int_equal(auth_register(&s_us2, &rng, "eve", "secret1"), ERR_INTERNAL);

    /* Null safety */
    user_store_init(NULL, NULL);
    assert_int_equal(user_store_load(NULL), -1);
    assert_null(user_store_find(NULL, "a"));
    assert_null(user_store_find(&s_us1, NULL));
    assert_null(user_store_find(&s_us1, ""));
    assert_int_equal(auth_register(NULL, &rng, "eve", "secret1"), ERR_INTERNAL);
    assert_int_equal(auth_verify(NULL, "eve", "secret1", NULL), ERR_BAD_CREDENTIALS);
}

int run_auth_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_auth_username_and_password_bounds),
        cmocka_unit_test(test_auth_register_and_verify_memory),
        cmocka_unit_test(test_auth_persistence_and_no_raw_password),
        cmocka_unit_test(test_auth_corrupted_file_and_edge_cases),
    };
    return cmocka_run_group_tests_name("auth", tests, NULL, NULL);
}
