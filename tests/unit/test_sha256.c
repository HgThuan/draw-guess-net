#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>
#include <cmocka.h>
#include <string.h>

#include "sha256.h"

static void test_sha256_nist_vectors(void **state)
{
    uint8_t digest[SHA256_DIGEST_SIZE];
    char hex[SHA256_HEX_SIZE];
    const char *msg_56 = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    (void)state;

    /* 1. Empty string */
    sha256_compute("", 0, digest);
    sha256_to_hex(digest, hex);
    assert_string_equal(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");

    /* 2. "abc" */
    sha256_compute("abc", 3, digest);
    sha256_to_hex(digest, hex);
    assert_string_equal(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    /* 3. 56-byte NIST vector (triggers 2-block padding) */
    sha256_compute(msg_56, strlen(msg_56), digest);
    sha256_to_hex(digest, hex);
    assert_string_equal(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

static void test_sha256_multiblock_and_streaming(void **state)
{
    uint8_t d1[SHA256_DIGEST_SIZE];
    uint8_t d2[SHA256_DIGEST_SIZE];
    char buf[200];
    Sha256Ctx ctx;
    (void)state;

    for (size_t i = 0; i < sizeof(buf); i++) {
        buf[i] = (char)('a' + (i % 26));
    }

    sha256_compute(buf, sizeof(buf), d1);

    sha256_init(&ctx);
    sha256_update(&ctx, buf, 17);
    sha256_update(&ctx, buf + 17, 64);
    sha256_update(&ctx, buf + 81, sizeof(buf) - 81);
    sha256_final(&ctx, d2);

    assert_memory_equal(d1, d2, SHA256_DIGEST_SIZE);
}

static void test_sha256_salted_and_null_guards(void **state)
{
    uint8_t salt1[SHA256_SALT_SIZE];
    uint8_t salt2[SHA256_SALT_SIZE];
    uint8_t h1[SHA256_DIGEST_SIZE];
    uint8_t h2[SHA256_DIGEST_SIZE];
    uint8_t h3[SHA256_DIGEST_SIZE];
    Sha256Ctx ctx;
    (void)state;

    memset(salt1, 0x11, sizeof(salt1));
    memset(salt2, 0x22, sizeof(salt2));

    sha256_salted(salt1, "secret123", h1);
    sha256_salted(salt1, "secret123", h2);
    sha256_salted(salt2, "secret123", h3);

    assert_memory_equal(h1, h2, SHA256_DIGEST_SIZE);
    assert_memory_not_equal(h1, h3, SHA256_DIGEST_SIZE);

    /* Null safety */
    sha256_init(NULL);
    sha256_init(&ctx);
    sha256_update(NULL, "a", 1);
    sha256_update(&ctx, NULL, 1);
    sha256_final(NULL, h1);
    sha256_final(&ctx, NULL);
    sha256_compute("a", 1, NULL);
    sha256_salted(NULL, "pass", h1);
    sha256_salted(salt1, NULL, h1);
    sha256_salted(salt1, "pass", NULL);
    sha256_to_hex(NULL, NULL);
}

int run_sha256_tests(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test(test_sha256_nist_vectors),
        cmocka_unit_test(test_sha256_multiblock_and_streaming),
        cmocka_unit_test(test_sha256_salted_and_null_guards),
    };
    return cmocka_run_group_tests_name("sha256", tests, NULL, NULL);
}
