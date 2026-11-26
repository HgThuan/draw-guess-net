#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <stdio.h>
#include <string.h>

#include "words.h"

static WordList s_wl;

static void test_words_load_dedup_crlf_and_overlong(void **state)
{
    const char *tmp_path = "build/test_words_tmp.txt";
    FILE *fp;
    (void)state;

    assert_int_equal(words_load(&s_wl, "tests/data/words_small.txt"), 4);
    assert_int_equal(words_load(NULL, "tests/data/words_small.txt"), -1);
    assert_int_equal(words_load(&s_wl, NULL), -1);
    assert_int_equal(words_load(&s_wl, "/nonexistent_words_file.txt"), -1);

    fp = fopen(tmp_path, "w");
    assert_non_null(fp);
    fprintf(fp, "\r\n   \t  \r\n");
    fprintf(fp, "  cat \r\n");
    fprintf(fp, "CAT\n");
    fprintf(fp, "  Cat  \n");
    /* Overlong line > 64 bytes: must be discarded completely without corrupting next line */
    fprintf(fp, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
    /* Overlong word > 31 chars but < 64 bytes */
    fprintf(fp, "abcdefghijklmnopqrstuvwxyz1234567890\n");
    /* Line with control character or disallowed characters (pipe, digit, punctuation) */
    fprintf(fp, "bad\x01word\n");
    fprintf(fp, "dog|x\n");
    fprintf(fp, "k9\n");
    fprintf(fp, "hello!\n");
    fprintf(fp, "  HOUSE  \r\n");
    fprintf(fp, "  ice   cream  \r\n");
    fprintf(fp, "tree\n");
    fclose(fp);

    assert_int_equal(words_load(&s_wl, tmp_path), 4);
    assert_string_equal(s_wl.words[0], "cat");
    assert_string_equal(s_wl.words[1], "house");
    assert_string_equal(s_wl.words[2], "ice cream");
    assert_string_equal(s_wl.words[3], "tree");

    (void)remove(tmp_path);
}

static void test_words_pick3_and_fewer_than_3(void **state)
{
    const char *tmp_path = "build/test_words_few.txt";
    char opts[3][WORD_MAX];
    Rng rng;
    FILE *fp;
    (void)state;

    rng_init_fake(&rng, 777);

    /* File with only 2 unique words (despite 3 lines) -> words_pick3 returns false */
    fp = fopen(tmp_path, "w");
    assert_non_null(fp);
    fprintf(fp, "apple\nbanana\nAPPLE\n");
    fclose(fp);

    assert_int_equal(words_load(&s_wl, tmp_path), 2);
    assert_false(words_pick3(&s_wl, &rng, opts));
    (void)remove(tmp_path);

    /* Default list has 3 distinct words */
    words_init_default(&s_wl);
    assert_int_equal(s_wl.count, 3);
    for (int iter = 0; iter < 30; iter++) {
        assert_true(words_pick3(&s_wl, &rng, opts));
        assert_string_not_equal(opts[0], opts[1]);
        assert_string_not_equal(opts[0], opts[2]);
        assert_string_not_equal(opts[1], opts[2]);
    }

    words_init(NULL);
    words_init_default(NULL);
    assert_false(words_pick3(NULL, &rng, opts));
}

static void test_words_match_contains_and_hint(void **state)
{
    char hint[WORD_MAX];
    (void)state;

    /* words_match: case-insensitive, trims leading/trailing spaces only */
    assert_true(words_match("cat", "cat"));
    assert_true(words_match("  CaT  ", "  cat  "));
    assert_true(words_match("  ICE Cream ", "ice cream"));
    assert_false(words_match("ice  cream", "ice cream"));
    assert_false(words_match("cat\t", "cat"));
    assert_false(words_match("ca\x01", "cat"));
    assert_false(words_match("cat", "dog"));
    assert_false(words_match("", "cat"));
    assert_false(words_match(NULL, "cat"));
    assert_false(words_match("cat", NULL));

    /* words_contains: substring match catches answers embedded in longer tokens */
    assert_true(words_contains("camerawowowoww", "camera"));
    assert_true(words_contains("xxxCAMERAxxx", "camera"));
    assert_true(words_contains("I want to educate children.", "cat"));
    assert_true(words_contains("scattered clouds", "cat"));
    assert_true(words_contains("I think the word is cat!", "cat"));
    assert_true(words_contains("CAT?", "cat"));
    assert_true(words_contains("(cat)", "cat"));
    assert_true(words_contains("cat123", "cat"));
    assert_true(words_contains("maybe   ICE   CREAM, right?", "ice cream"));
    assert_false(words_contains("ice", "ice cream"));
    assert_true(words_contains("ice  cream", "ice cream"));
    assert_true(words_contains("I love ICECREAM", "ice cream"));
    assert_true(words_contains("icecreammmm", "ice cream"));
    assert_true(words_contains("nice creamy", "ice cream"));
    assert_true(words_contains("đáp án là CAT nhé", "cat"));
    assert_false(words_contains("c a t", "cat"));
    assert_false(words_contains("ca t", "cat"));
    assert_false(words_contains(NULL, "cat"));
    assert_false(words_contains("cat", ""));

    /* words_make_hint: underscores preserving spaces */
    words_make_hint("cat", hint);
    assert_string_equal(hint, "___");

    words_make_hint("ice cream", hint);
    assert_string_equal(hint, "___ _____");

    words_make_hint(NULL, hint);
    assert_string_equal(hint, "");
    words_make_hint("cat", NULL);
}

int run_words_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_words_load_dedup_crlf_and_overlong),
        cmocka_unit_test(test_words_pick3_and_fewer_than_3),
        cmocka_unit_test(test_words_match_contains_and_hint),
    };
    return cmocka_run_group_tests_name("words", tests, NULL, NULL);
}
