#include "sha256.h"
#include <string.h>

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)       (ROR32(x, 2) ^ ROR32(x, 13) ^ ROR32(x, 22))
#define EP1(x)       (ROR32(x, 6) ^ ROR32(x, 11) ^ ROR32(x, 25))
#define SIG0(x)      (ROR32(x, 7) ^ ROR32(x, 18) ^ ((x) >> 3))
#define SIG1(x)      (ROR32(x, 17) ^ ROR32(x, 19) ^ ((x) >> 10))

static const uint32_t K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static void sha256_transform(Sha256Ctx *ctx, const uint8_t data[SHA256_BLOCK_SIZE])
{
    uint32_t m[64];
    uint32_t a, b, c, d, e, f, g, h, t1, t2;

    for (int i = 0, j = 0; i < 16; i++, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) |
               ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) |
               ((uint32_t)data[j + 3]);
    }
    for (int i = 16; i < 64; i++) {
        m[i] = SIG1(m[i - 2]) + m[i - 7] + SIG0(m[i - 15]) + m[i - 16];
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];
    f = ctx->state[5];
    g = ctx->state[6];
    h = ctx->state[7];

    for (int i = 0; i < 64; i++) {
        t1 = h + EP1(e) + CH(e, f, g) + K[i] + m[i];
        t2 = EP0(a) + MAJ(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

void sha256_init(Sha256Ctx *ctx)
{
    if (!ctx) {
        return;
    }
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667u;
    ctx->state[1] = 0xbb67ae85u;
    ctx->state[2] = 0x3c6ef372u;
    ctx->state[3] = 0xa54ff53au;
    ctx->state[4] = 0x510e527fu;
    ctx->state[5] = 0x9b05688cu;
    ctx->state[6] = 0x1f83d9abu;
    ctx->state[7] = 0x5be0cd19u;
}

void sha256_update(Sha256Ctx *ctx, const void *data, size_t len)
{
    if (!ctx || (!data && len > 0)) {
        return;
    }
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0; i < len; i++) {
        ctx->data[ctx->datalen++] = bytes[i];
        if (ctx->datalen == SHA256_BLOCK_SIZE) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

void sha256_final(Sha256Ctx *ctx, uint8_t digest[SHA256_DIGEST_SIZE])
{
    if (!ctx || !digest) {
        return;
    }

    size_t i = ctx->datalen;
    ctx->bitlen += (uint64_t)ctx->datalen * 8u;

    ctx->data[i++] = 0x80u;
    if (i > 56) {
        while (i < SHA256_BLOCK_SIZE) {
            ctx->data[i++] = 0x00u;
        }
        sha256_transform(ctx, ctx->data);
        i = 0;
    }
    while (i < 56) {
        ctx->data[i++] = 0x00u;
    }

    for (int b = 0; b < 8; b++) {
        ctx->data[63 - b] = (uint8_t)(ctx->bitlen >> (b * 8));
    }
    sha256_transform(ctx, ctx->data);

    for (int w = 0; w < 8; w++) {
        digest[w * 4 + 0] = (uint8_t)(ctx->state[w] >> 24);
        digest[w * 4 + 1] = (uint8_t)(ctx->state[w] >> 16);
        digest[w * 4 + 2] = (uint8_t)(ctx->state[w] >> 8);
        digest[w * 4 + 3] = (uint8_t)(ctx->state[w]);
    }
}

void sha256_compute(const void *data, size_t len, uint8_t digest[SHA256_DIGEST_SIZE])
{
    Sha256Ctx ctx;
    if (!digest) {
        return;
    }
    sha256_init(&ctx);
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, digest);
}

void sha256_salted(const uint8_t salt[SHA256_SALT_SIZE], const char *password,
                   uint8_t digest[SHA256_DIGEST_SIZE])
{
    Sha256Ctx ctx;
    if (!salt || !password || !digest) {
        return;
    }
    sha256_init(&ctx);
    sha256_update(&ctx, salt, SHA256_SALT_SIZE);
    sha256_update(&ctx, password, strlen(password));
    sha256_final(&ctx, digest);
}

void sha256_to_hex(const uint8_t digest[SHA256_DIGEST_SIZE], char out_hex[SHA256_HEX_SIZE])
{
    static const char HEX[] = "0123456789abcdef";
    if (!digest || !out_hex) {
        return;
    }
    for (size_t i = 0; i < SHA256_DIGEST_SIZE; i++) {
        out_hex[i * 2]     = HEX[(digest[i] >> 4) & 0x0Fu];
        out_hex[i * 2 + 1] = HEX[digest[i] & 0x0Fu];
    }
    out_hex[SHA256_DIGEST_SIZE * 2] = '\0';
}
