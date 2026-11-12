#ifndef NETSKETCH_RNG_H
#define NETSKETCH_RNG_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint64_t state;
} Rng;

void     rng_init_real(Rng *r);
void     rng_init_fake(Rng *r, uint64_t seed);
uint32_t rng_next_u32(Rng *r);
int      rng_range(Rng *r, int min_incl, int max_incl);
void     rng_bytes(Rng *r, uint8_t *out, size_t len);
void     rng_hex(Rng *r, char *out, size_t nbytes);

#endif
