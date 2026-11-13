#ifndef NETSKETCH_SHA256_H
#define NETSKETCH_SHA256_H

#include <stddef.h>
#include <stdint.h>

#define SHA256_BLOCK_SIZE  64
#define SHA256_DIGEST_SIZE 32
#define SHA256_SALT_SIZE   16
#define SHA256_HEX_SIZE    65

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t  data[SHA256_BLOCK_SIZE];
    size_t   datalen;
} Sha256Ctx;

void sha256_init(Sha256Ctx *ctx);
void sha256_update(Sha256Ctx *ctx, const void *data, size_t len);
void sha256_final(Sha256Ctx *ctx, uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256_compute(const void *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256_salted(const uint8_t salt[SHA256_SALT_SIZE], const char *password,
                   uint8_t digest[SHA256_DIGEST_SIZE]);
void sha256_to_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char out_hex[SHA256_HEX_SIZE]);

#endif
