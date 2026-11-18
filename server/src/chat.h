#ifndef NETSKETCH_CHAT_H
#define NETSKETCH_CHAT_H

#include "conn.h"
#include "protocol.h"

struct Server;

void h_chat(struct Server *s, Conn *c, const Command *cmd);

#endif
