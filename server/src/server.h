#ifndef NETSKETCH_SERVER_H
#define NETSKETCH_SERVER_H

#include <stdbool.h>
#include <stdint.h>
#include "auth.h"
#include "config.h"
#include "clock.h"
#include "rng.h"
#include "conn.h"
#include "logger.h"
#include "room.h"

typedef struct Server {
    Config    cfg;
    Clock     clock;
    Rng       rng;
    Logger    log;
    UserStore users;
    Room      rooms[MAX_ROOMS];
    int       next_room_id;
    ConnTable conns;
    bool      stop;
} Server;

void server_init(Server *s, const Config *cfg);
void server_on_line(Server *s, Conn *c, const char *line);
void server_tick(Server *s, int64_t now_ms);

#endif
