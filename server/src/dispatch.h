#ifndef NETSKETCH_DISPATCH_H
#define NETSKETCH_DISPATCH_H

#include "protocol.h"
#include "server.h"

typedef enum {
    AUTH_ANY = 0,
    AUTH_GUEST_ONLY,
    AUTH_REQUIRED
} AuthReq;

typedef enum {
    ROOM_ANY = 0,
    ROOM_OUT_ONLY,
    ROOM_IN_ONLY
} RoomReq;

typedef void (*CmdHandler)(Server *s, Conn *c, const Command *cmd);

typedef struct {
    CmdType    type;
    AuthReq    auth_req;
    RoomReq    room_req;
    CmdHandler handler;
} CmdSpec;

void reply_ok(Server *s, Conn *c, const char *payload);
void reply_err(Server *s, Conn *c, int code);
void dispatch_line(Server *s, Conn *c, const char *line);

#endif
