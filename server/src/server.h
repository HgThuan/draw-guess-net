#ifndef NETSKETCH_SERVER_H
#define NETSKETCH_SERVER_H

#include <stdbool.h>
#include "config.h"
#include "clock.h"
#include "rng.h"
#include "conn.h"

typedef struct Server {
    Config    cfg;
    Clock     clock;
    Rng       rng;
    ConnTable conns;
    bool      stop;
} Server;

void server_init(Server *s, const Config *cfg);

#endif
