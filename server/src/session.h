#ifndef NETSKETCH_SESSION_H
#define NETSKETCH_SESSION_H

#include <stdint.h>
#include "conn.h"
#include "protocol.h"

struct Server;

void h_login(struct Server *s, Conn *c, const Command *cmd);
void h_logout(struct Server *s, Conn *c, const Command *cmd);
void session_on_disconnect(struct Server *s, Conn *c);
void session_tick(struct Server *s, int64_t now_ms);

#endif
