#ifndef NETSKETCH_DISPATCH_H
#define NETSKETCH_DISPATCH_H

#include <stdint.h>
#include "protocol.h"
#include "room.h"
#include "server.h"

#define RSTATE_NONE     0u
#define RSTATE_LOBBY    (1u << ROOM_LOBBY)
#define RSTATE_CHOOSING (1u << ROOM_CHOOSING)
#define RSTATE_DRAWING  (1u << ROOM_DRAWING)
#define RSTATE_TURN_END (1u << ROOM_TURN_END)
#define RSTATE_ANY      (RSTATE_LOBBY | RSTATE_CHOOSING | RSTATE_DRAWING | RSTATE_TURN_END)

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
    uint32_t   room_states;
    CmdHandler handler;
} CmdSpec;

void reply_ok(Server *s, Conn *c, const char *payload);
void reply_err(Server *s, Conn *c, int code);
void dispatch_line(Server *s, Conn *c, const char *line);

#endif
