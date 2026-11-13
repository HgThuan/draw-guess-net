#include "dispatch.h"
#include <stddef.h>

static void h_ping(Server *s, Conn *c, const Command *cmd)
{
    (void)cmd;
    conn_send_line(&s->conns, c, "PONG");
}

static const CmdSpec CMD_SPECS[] = {
    { CMD_PING,        AUTH_ANY,        ROOM_ANY,      h_ping },
    { CMD_REGISTER,    AUTH_GUEST_ONLY, ROOM_ANY,      NULL   },
    { CMD_LOGIN,       AUTH_GUEST_ONLY, ROOM_ANY,      NULL   },
    { CMD_LOGOUT,      AUTH_REQUIRED,   ROOM_ANY,      NULL   },
    { CMD_CREATE_ROOM, AUTH_REQUIRED,   ROOM_OUT_ONLY, NULL   },
    { CMD_LIST_ROOMS,  AUTH_REQUIRED,   ROOM_ANY,      NULL   },
    { CMD_JOIN_ROOM,   AUTH_REQUIRED,   ROOM_OUT_ONLY, NULL   },
    { CMD_LEAVE_ROOM,  AUTH_REQUIRED,   ROOM_IN_ONLY,  NULL   },
    { CMD_START,       AUTH_REQUIRED,   ROOM_IN_ONLY,  NULL   },
    { CMD_SCOREBOARD,  AUTH_REQUIRED,   ROOM_IN_ONLY,  NULL   },
    { CMD_CHOOSE_WORD, AUTH_REQUIRED,   ROOM_IN_ONLY,  NULL   },
    { CMD_STROKE,      AUTH_REQUIRED,   ROOM_IN_ONLY,  NULL   },
    { CMD_CLEAR,       AUTH_REQUIRED,   ROOM_IN_ONLY,  NULL   },
    { CMD_GUESS,       AUTH_REQUIRED,   ROOM_IN_ONLY,  NULL   },
    { CMD_CHAT,        AUTH_REQUIRED,   ROOM_IN_ONLY,  NULL   },
};

void reply_ok(Server *s, Conn *c, const char *payload)
{
    if (!s || !c) {
        return;
    }
    if (!payload || payload[0] == '\0') {
        conn_send_line(&s->conns, c, "OK");
    } else {
        conn_sendf(&s->conns, c, "OK|%s", payload);
    }
}

void reply_err(Server *s, Conn *c, int code)
{
    char buf[128];
    if (!s || !c) {
        return;
    }
    int n = protocol_format_err(buf, sizeof(buf), code);
    if (n > 0) {
        conn_send_raw(&s->conns, c, buf, (size_t)n);
    }
}

static const CmdSpec *find_spec(CmdType type)
{
    size_t n = sizeof(CMD_SPECS) / sizeof(CMD_SPECS[0]);
    for (size_t i = 0; i < n; i++) {
        if (CMD_SPECS[i].type == type) {
            return &CMD_SPECS[i];
        }
    }
    return NULL; /* GCOVR_EXCL_LINE */
}

void dispatch_line(Server *s, Conn *c, const char *line)
{
    Command cmd;
    if (!s || !c || !line) {
        return;
    }

    int err = protocol_parse(line, &cmd);
    if (err != ERR_OK) {
        reply_err(s, c, err);
        return;
    }

    const CmdSpec *spec = find_spec(cmd.type);
    if (!spec) {
        reply_err(s, c, ERR_UNKNOWN_CMD); /* GCOVR_EXCL_LINE */
        return;                           /* GCOVR_EXCL_LINE */
    }

    if (spec->auth_req == AUTH_REQUIRED && c->state != CONN_AUTHED) {
        reply_err(s, c, ERR_NOT_LOGGED_IN);
        return;
    }
    if (spec->auth_req == AUTH_GUEST_ONLY && c->state == CONN_AUTHED) {
        reply_err(s, c, ERR_ALREADY_LOGGED_IN);
        return;
    }

    if (spec->room_req == ROOM_IN_ONLY && c->room_id < 0) {
        reply_err(s, c, ERR_NOT_IN_ROOM);
        return;
    }
    if (spec->room_req == ROOM_OUT_ONLY && c->room_id >= 0) {
        reply_err(s, c, ERR_ALREADY_IN_ROOM);
        return;
    }

    if (!spec->handler) {
        reply_err(s, c, ERR_INTERNAL);
        return;
    }

    spec->handler(s, c, &cmd);
}
