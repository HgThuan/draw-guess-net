#define _POSIX_C_SOURCE 200809L
#include "rng.h"
#include <stdio.h>
#include <time.h>

void rng_init_fake(Rng *r, uint64_t seed)
{
    if (!r) {
        return;
    }
    r->state = (seed != 0) ? seed : 0x9e3779b97f4a7c15ULL;
}

void rng_init_real(Rng *r)
{
    if (!r) {
        return;
    }
    uint64_t seed = 0;
    FILE *fp = fopen("/dev/urandom", "rb");
    if (fp) {
        size_t n = fread(&seed, 1, sizeof(seed), fp);
        fclose(fp);
        if (n == sizeof(seed) && seed != 0) {
            r->state = seed;
            return;
        }
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    seed = ((uint64_t)ts.tv_sec << 32) ^ (uint64_t)ts.tv_nsec ^ (uintptr_t)r;
    rng_init_fake(r, seed);
}

uint32_t rng_next_u32(Rng *r)
{
    if (!r) {
        return 0;
    }
    uint64_t x = r->state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    r->state = x;
    return (uint32_t)((x * 0x2545F4914F6CDD1DULL) >> 32);
}

int rng_range(Rng *r, int min_incl, int max_incl)
{
    if (!r || max_incl <= min_incl) {
        return min_incl;
    }
    uint32_t span = (uint32_t)(max_incl - min_incl) + 1u;
    return min_incl + (int)(rng_next_u32(r) % span);
}

void rng_bytes(Rng *r, uint8_t *out, size_t len)
{
    if (!r || !out) {
        return;
    }
    uint32_t v = 0;
    for (size_t i = 0; i < len; i++) {
        if ((i & 3u) == 0) {
            v = rng_next_u32(r);
        }
        out[i] = (uint8_t)(v & 0xFFu);
        v >>= 8;
    }
}

void rng_hex(Rng *r, char *out, size_t nbytes)
{
    static const char HEX[] = "0123456789abcdef";
    if (!r || !out) {
        return;
    }
    uint32_t v = 0;
    for (size_t i = 0; i < nbytes; i++) {
        if ((i & 3u) == 0) {
            v = rng_next_u32(r);
        }
        uint8_t b = (uint8_t)(v & 0xFFu);
        v >>= 8;
        out[2 * i]     = HEX[b >> 4];
        out[2 * i + 1] = HEX[b & 0x0Fu];
    }
    out[2 * nbytes] = '\0';
}
