#define _POSIX_C_SOURCE 200809L
#include "clock.h"
#include <time.h>

void clock_init_real(Clock *c)
{
    if (!c) {
        return;
    }
    c->is_fake = false;
    c->fake_now_ms = 0;
}

void clock_init_fake(Clock *c, int64_t initial_ms)
{
    if (!c) {
        return;
    }
    c->is_fake = true;
    c->fake_now_ms = initial_ms;
}

void clock_advance(Clock *c, int64_t delta_ms)
{
    if (c && c->is_fake) {
        c->fake_now_ms += delta_ms;
    }
}

int64_t clock_now_ms(const Clock *c)
{
    if (!c) {
        return 0;
    }
    if (c->is_fake) {
        return c->fake_now_ms;
    }
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}
