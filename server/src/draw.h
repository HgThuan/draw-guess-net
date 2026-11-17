#ifndef NETSKETCH_DRAW_H
#define NETSKETCH_DRAW_H

#include "conn.h"
#include "protocol.h"

struct Server;

int  stroke_validate(const char *color, int width, const char *points);
void h_stroke(struct Server *s, Conn *c, const Command *cmd);
void h_clear(struct Server *s, Conn *c, const Command *cmd);

#endif
