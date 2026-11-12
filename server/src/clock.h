#ifndef NETSKETCH_CLOCK_H
#define NETSKETCH_CLOCK_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool    is_fake;
    int64_t fake_now_ms;
} Clock;

void    clock_init_real(Clock *c);
void    clock_init_fake(Clock *c, int64_t initial_ms);
void    clock_advance(Clock *c, int64_t delta_ms);
int64_t clock_now_ms(const Clock *c);

#endif
